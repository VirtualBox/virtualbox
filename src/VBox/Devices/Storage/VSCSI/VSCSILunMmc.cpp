/* $Id: VSCSILunMmc.cpp 110132 2025-07-07 11:33:52Z michal.necasek@oracle.com $ */
/** @file
 * Virtual SCSI driver: MMC LUN implementation (CD/DVD-ROM)
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_VSCSI
#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/types.h>
#include <VBox/vscsi.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/string.h>

#include "VSCSIInternal.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Different event status types.
 */
typedef enum MMCEVENTSTATUSTYPE
{
    /** Medium event status not changed. */
    MMCEVENTSTATUSTYPE_UNCHANGED = 0,
    /** Medium eject requested (eject button pressed). */
    MMCEVENTSTATUSTYPE_MEDIA_EJECT_REQUESTED,
    /** New medium inserted. */
    MMCEVENTSTATUSTYPE_MEDIA_NEW,
    /** Medium removed. */
    MMCEVENTSTATUSTYPE_MEDIA_REMOVED,
    /** Medium was removed + new medium was inserted. */
    MMCEVENTSTATUSTYPE_MEDIA_CHANGED,
    /** 32bit hack. */
    MMCEVENTSTATUSTYPE_32BIT_HACK = 0x7fffffff
} MMCEVENTSTATUSTYPE;

/** @name Media track types.
 * @{ */
/** Unknown media type. */
#define MMC_MEDIA_TYPE_UNKNOWN          0
/** Door closed, no media. */
#define MMC_MEDIA_TYPE_NO_DISC       0x70
/** @} */


/**
 * MMC LUN instance
 */
typedef struct VSCSILUNMMC
{
    /** Core LUN structure */
    VSCSILUNINT                 Core;
    /** Size of the virtual disk. */
    uint64_t                    cSectors;
    /** Medium locked indicator. */
    bool                        fLocked;
    /** Media event status. */
    volatile MMCEVENTSTATUSTYPE MediaEventStatus;
    /** Media track type. */
    volatile uint32_t           u32MediaTrackType;
} VSCSILUNMMC, *PVSCSILUNMMC;


/**
 * Callback to fill a feature for a GET CONFIGURATION request.
 *
 * @returns Number of bytes used for this feature in the buffer.
 * @param   pbBuf           The buffer to use.
 * @param   cbBuf           Size of the buffer.
 */
typedef DECLCALLBACKTYPE(size_t, FNVSCSILUNMMCFILLFEATURE,(uint8_t *pbBuf, size_t cbBuf));
/** Pointer to a fill feature callback. */
typedef FNVSCSILUNMMCFILLFEATURE *PFNVSCSILUNMMCFILLFEATURE;

/**
 * VSCSI MMC feature descriptor.
 */
typedef struct VSCSILUNMMCFEATURE
{
    /** The feature number. */
    uint16_t                  u16Feat;
    /** The callback to call for this feature. */
    PFNVSCSILUNMMCFILLFEATURE pfnFeatureFill;
} VSCSILUNMMCFEATURE;
/** Pointer to a VSCSI MMC feature descriptor. */
typedef VSCSILUNMMCFEATURE *PVSCSILUNMMCFEATURE;
/** Pointer to a const VSCSI MMC feature descriptor. */
typedef const VSCSILUNMMCFEATURE *PCVSCSILUNMMCFEATURE;



/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
RT_C_DECLS_BEGIN
static DECLCALLBACK(size_t) vscsiLunMmcGetConfigurationFillFeatureListProfiles(uint8_t *pbBuf, size_t cbBuf);
static DECLCALLBACK(size_t) vscsiLunMmcGetConfigurationFillFeatureCore(uint8_t *pbBuf, size_t cbBuf);
static DECLCALLBACK(size_t) vscsiLunMmcGetConfigurationFillFeatureMorphing(uint8_t *pbBuf, size_t cbBuf);
static DECLCALLBACK(size_t) vscsiLunMmcGetConfigurationFillFeatureRemovableMedium(uint8_t *pbBuf, size_t cbBuf);
static DECLCALLBACK(size_t) vscsiLunMmcGetConfigurationFillFeatureRandomReadable(uint8_t *pbBuf, size_t cbBuf);
static DECLCALLBACK(size_t) vscsiLunMmcGetConfigurationFillFeatureCDRead(uint8_t *pbBuf, size_t cbBuf);
static DECLCALLBACK(size_t) vscsiLunMmcGetConfigurationFillFeaturePowerManagement(uint8_t *pbBuf, size_t cbBuf);
static DECLCALLBACK(size_t) vscsiLunMmcGetConfigurationFillFeatureTimeout(uint8_t *pbBuf, size_t cbBuf);
RT_C_DECLS_END

/**
 * List of supported MMC features.
 */
static const VSCSILUNMMCFEATURE g_aVScsiMmcFeatures[] =
{
    { 0x0000, vscsiLunMmcGetConfigurationFillFeatureListProfiles},
    { 0x0001, vscsiLunMmcGetConfigurationFillFeatureCore},
    { 0x0002, vscsiLunMmcGetConfigurationFillFeatureMorphing},
    { 0x0003, vscsiLunMmcGetConfigurationFillFeatureRemovableMedium},
    { 0x0010, vscsiLunMmcGetConfigurationFillFeatureRandomReadable},
    { 0x001e, vscsiLunMmcGetConfigurationFillFeatureCDRead},
    { 0x0100, vscsiLunMmcGetConfigurationFillFeaturePowerManagement},
    { 0x0105, vscsiLunMmcGetConfigurationFillFeatureTimeout}
};

/* Fabricate normal TOC information. */
static int mmcReadTOCNormal(PVSCSILUNINT pVScsiLun, PVSCSIREQINT pVScsiReq, uint16_t cbMaxTransfer, bool fMSF)
{
    uint8_t         aReply[2+99*8 + 32]; RT_ZERO(aReply); /* Maximum possible reply plus some safety. */
    uint8_t         *pbBuf = aReply;
    uint8_t         *q;
    uint8_t         iStartTrack;
    uint32_t        cbSize;
    uint32_t        cTracks = vscsiLunMediumGetRegionCount(pVScsiLun);

    iStartTrack = pVScsiReq->pbCDB[6];
    if (iStartTrack == 0)
        iStartTrack = 1;
    if (iStartTrack > cTracks && iStartTrack != 0xaa)
        return vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00);

    q = pbBuf + 2;
    *q++ = iStartTrack; /* first track number */
    *q++ = cTracks;     /* last track number */
    for (uint32_t iTrack = iStartTrack; iTrack <= cTracks; iTrack++)
    {
        uint64_t uLbaStart = 0;
        VDREGIONDATAFORM enmDataForm = VDREGIONDATAFORM_MODE1_2048;

        int rc = vscsiLunMediumQueryRegionProperties(pVScsiLun, iTrack - 1, &uLbaStart,
                                                     NULL, NULL, &enmDataForm);
        if (rc == VERR_NOT_FOUND || rc == VERR_MEDIA_NOT_PRESENT)
             return vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_NOT_READY,
                                             SCSI_ASC_MEDIUM_NOT_PRESENT, 0x00);
        else
            AssertRC(rc);

        *q++ = 0;                  /* reserved */

        if (enmDataForm == VDREGIONDATAFORM_CDDA)
            *q++ = 0x10;           /* ADR, control */
        else
            *q++ = 0x14;           /* ADR, control */

        *q++ = (uint8_t)iTrack;    /* track number */
        *q++ = 0;                  /* reserved */
        if (fMSF)
        {
            *q++ = 0; /* reserved */
            scsiLBA2MSF(q, (uint32_t)uLbaStart);
            q += 3;
        }
        else
        {
            /* sector 0 */
            scsiH2BE_U32(q, (uint32_t)uLbaStart);
            q += 4;
        }
    }
    /* lead out track */
    *q++ = 0; /* reserved */
    *q++ = 0x14; /* ADR, control */
    *q++ = 0xaa; /* track number */
    *q++ = 0; /* reserved */

    /* Query start and length of last track to get the start of the lead out track. */
    uint64_t uLbaStart = 0;
    uint64_t cBlocks = 0;

    int rc = vscsiLunMediumQueryRegionProperties(pVScsiLun, cTracks - 1, &uLbaStart,
                                                 &cBlocks, NULL, NULL);
    if (rc == VERR_NOT_FOUND || rc == VERR_MEDIA_NOT_PRESENT)
         return vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_NOT_READY,
                                         SCSI_ASC_MEDIUM_NOT_PRESENT, 0x00);
    else
        AssertRC(rc);

    uLbaStart += cBlocks;
    if (fMSF)
    {
        *q++ = 0; /* reserved */
        scsiLBA2MSF(q, (uint32_t)uLbaStart);
        q += 3;
    }
    else
    {
        scsiH2BE_U32(q, (uint32_t)uLbaStart);
        q += 4;
    }
    cbSize = q - pbBuf;
    Assert(cbSize <= sizeof(aReply));
    scsiH2BE_U16(pbBuf, cbSize - 2);
    if (cbSize < cbMaxTransfer)
        cbMaxTransfer = cbSize;

    vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_T2I);
    vscsiReqSetXferSize(pVScsiReq, cbMaxTransfer);

    RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, cbMaxTransfer);
    return vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
}

/* Fabricate session information. */
static int mmcReadTOCMulti(PVSCSILUNINT pVScsiLun, PVSCSIREQINT pVScsiReq, uint16_t cbMaxTransfer, bool fMSF)
{
    RT_NOREF1(cbMaxTransfer);
    uint8_t         aReply[32];
    uint8_t         *pbBuf = aReply;

    /* multi session: only a single session defined */
    memset(pbBuf, 0, 12);
    pbBuf[1] = 0x0a;
    pbBuf[2] = 0x01;    /* first complete session number */
    pbBuf[3] = 0x01;    /* last complete session number */

    VDREGIONDATAFORM enmDataForm = VDREGIONDATAFORM_MODE1_2048;
    int rc = vscsiLunMediumQueryRegionProperties(pVScsiLun, 0, NULL,
                                                 NULL, NULL, &enmDataForm);
    if (rc == VERR_NOT_FOUND || rc == VERR_MEDIA_NOT_PRESENT)
         return vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_NOT_READY,
                                         SCSI_ASC_MEDIUM_NOT_PRESENT, 0x00);
    else
        AssertRC(rc);

    if (enmDataForm == VDREGIONDATAFORM_CDDA)
        pbBuf[5] = 0x10;           /* ADR, control */
    else
        pbBuf[5] = 0x14;           /* ADR, control */

    pbBuf[6] = 1;       /* first track in last complete session */

    if (fMSF)
    {
        pbBuf[8] = 0;   /* reserved */
        scsiLBA2MSF(pbBuf + 8, 0);
    }
    else
    {
        /* sector 0 */
        scsiH2BE_U32(pbBuf + 8, 0);
    }

    vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_T2I);
    vscsiReqSetXferSize(pVScsiReq, 12);

    RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, 12);

    return vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
}

/**
 * Create raw TOC data information.
 *
 * @returns SCSI status code.
 * @param   pVScsiLun     The LUN instance.
 * @param   pVScsiReq     The VSCSI request.
 * @param   cbMaxTransfer The maximum transfer size.
 * @param   fMSF          Flag whether to use MSF format to encode sector numbers.
 */
static int mmcReadTOCRaw(PVSCSILUNINT pVScsiLun, PVSCSIREQINT pVScsiReq, uint16_t cbMaxTransfer, bool fMSF)
{
    PVSCSILUNMMC pVScsiLunMmc = (PVSCSILUNMMC)pVScsiLun;
    uint8_t aReply[50]; /* Counted a maximum of 45 bytes but better be on the safe side. */
    uint32_t cbSize;
    uint8_t *pbBuf = &aReply[0] + 2;

    *pbBuf++ = 1; /* first session */
    *pbBuf++ = 1; /* last session */

    *pbBuf++ = 1; /* session number */
    *pbBuf++ = 0x14; /* data track */
    *pbBuf++ = 0; /* track number */
    *pbBuf++ = 0xa0; /* first track in program area */
    *pbBuf++ = 0; /* min */
    *pbBuf++ = 0; /* sec */
    *pbBuf++ = 0; /* frame */
    *pbBuf++ = 0;
    *pbBuf++ = 1; /* first track */
    *pbBuf++ = 0x00; /* disk type CD-DA or CD data */
    *pbBuf++ = 0;

    *pbBuf++ = 1; /* session number */
    *pbBuf++ = 0x14; /* data track */
    *pbBuf++ = 0; /* track number */
    *pbBuf++ = 0xa1; /* last track in program area */
    *pbBuf++ = 0; /* min */
    *pbBuf++ = 0; /* sec */
    *pbBuf++ = 0; /* frame */
    *pbBuf++ = 0;
    *pbBuf++ = 1; /* last track */
    *pbBuf++ = 0;
    *pbBuf++ = 0;

    *pbBuf++ = 1; /* session number */
    *pbBuf++ = 0x14; /* data track */
    *pbBuf++ = 0; /* track number */
    *pbBuf++ = 0xa2; /* lead-out */
    *pbBuf++ = 0; /* min */
    *pbBuf++ = 0; /* sec */
    *pbBuf++ = 0; /* frame */
    if (fMSF)
    {
        *pbBuf++ = 0; /* reserved */
        scsiLBA2MSF(pbBuf, pVScsiLunMmc->cSectors);
        pbBuf += 3;
    }
    else
    {
        scsiH2BE_U32(pbBuf, pVScsiLunMmc->cSectors);
        pbBuf += 4;
    }

    *pbBuf++ = 1; /* session number */
    *pbBuf++ = 0x14; /* ADR, control */
    *pbBuf++ = 0;    /* track number */
    *pbBuf++ = 1;    /* point */
    *pbBuf++ = 0; /* min */
    *pbBuf++ = 0; /* sec */
    *pbBuf++ = 0; /* frame */
    if (fMSF)
    {
        *pbBuf++ = 0; /* reserved */
        scsiLBA2MSF(pbBuf, 0);
        pbBuf += 3;
    }
    else
    {
        /* sector 0 */
        scsiH2BE_U32(pbBuf, 0);
        pbBuf += 4;
    }

    cbSize = pbBuf - aReply;
    scsiH2BE_U16(&aReply[0], cbSize - 2);

    vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_T2I);
    vscsiReqSetXferSize(pVScsiReq, RT_MIN(cbMaxTransfer, cbSize));

    RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, RT_MIN(cbMaxTransfer, cbSize));
    return vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
}

static DECLCALLBACK(size_t) vscsiLunMmcGetConfigurationFillFeatureListProfiles(uint8_t *pbBuf, size_t cbBuf)
{
    if (cbBuf < 3*4)
        return 0;

    scsiH2BE_U16(pbBuf, 0x0); /* feature 0: list of profiles supported */
    pbBuf[2] = (0 << 2) | (1 << 1) | (1 << 0); /* version 0, persistent, current */
    pbBuf[3] = 8; /* additional bytes for profiles */
    /* The MMC-3 spec says that DVD-ROM read capability should be reported
     * before CD-ROM read capability. */
    scsiH2BE_U16(pbBuf + 4, 0x10); /* profile: read-only DVD */
    pbBuf[6] = (0 << 0); /* NOT current profile */
    scsiH2BE_U16(pbBuf + 8, 0x08); /* profile: read only CD */
    pbBuf[10] = (1 << 0); /* current profile */

    return 3*4; /* Header + 2 profiles entries */
}

static DECLCALLBACK(size_t) vscsiLunMmcGetConfigurationFillFeatureCore(uint8_t *pbBuf, size_t cbBuf)
{
    if (cbBuf < 12)
        return 0;

    scsiH2BE_U16(pbBuf, 0x1); /* feature 0001h: Core Feature */
    pbBuf[2] = (0x2 << 2) | RT_BIT(1) | RT_BIT(0); /* Version | Persistent | Current */
    pbBuf[3] = 8; /* Additional length */
    scsiH2BE_U16(pbBuf + 4, 0x00000002); /* Physical interface ATAPI. */
    pbBuf[8] = RT_BIT(0); /* DBE */
    /* Rest is reserved. */

    return 12;
}

static DECLCALLBACK(size_t) vscsiLunMmcGetConfigurationFillFeatureMorphing(uint8_t *pbBuf, size_t cbBuf)
{
    if (cbBuf < 8)
        return 0;

    scsiH2BE_U16(pbBuf, 0x2); /* feature 0002h: Morphing Feature */
    pbBuf[2] = (0x1 << 2) | RT_BIT(1) | RT_BIT(0); /* Version | Persistent | Current */
    pbBuf[3] = 4; /* Additional length */
    pbBuf[4] = RT_BIT(1) | 0x0; /* OCEvent | !ASYNC */
    /* Rest is reserved. */

    return 8;
}

static DECLCALLBACK(size_t) vscsiLunMmcGetConfigurationFillFeatureRemovableMedium(uint8_t *pbBuf, size_t cbBuf)
{
    if (cbBuf < 8)
        return 0;

    scsiH2BE_U16(pbBuf, 0x3); /* feature 0003h: Removable Medium Feature */
    pbBuf[2] = (0x2 << 2) | RT_BIT(1) | RT_BIT(0); /* Version | Persistent | Current */
    pbBuf[3] = 4; /* Additional length */
    /* Tray type loading | Load | Eject | !Pvnt Jmpr | !DBML | Lock */
    pbBuf[4] = (0x2 << 5) | RT_BIT(4) | RT_BIT(3) | (0x0 << 2) | (0x0 << 1) | RT_BIT(0);
    /* Rest is reserved. */

    return 8;
}

static DECLCALLBACK(size_t) vscsiLunMmcGetConfigurationFillFeatureRandomReadable(uint8_t *pbBuf, size_t cbBuf)
{
    if (cbBuf < 12)
        return 0;

    scsiH2BE_U16(pbBuf, 0x10); /* feature 0010h: Random Readable Feature */
    pbBuf[2] = (0x0 << 2) | RT_BIT(1) | RT_BIT(0); /* Version | Persistent | Current */
    pbBuf[3] = 8; /* Additional length */
    scsiH2BE_U32(pbBuf + 4, 2048); /* Logical block size. */
    scsiH2BE_U16(pbBuf + 8, 0x10); /* Blocking (0x10 for DVD, CD is not defined). */
    pbBuf[10] = 0; /* PP not present */
    /* Rest is reserved. */

    return 12;
}

static DECLCALLBACK(size_t) vscsiLunMmcGetConfigurationFillFeatureCDRead(uint8_t *pbBuf, size_t cbBuf)
{
    if (cbBuf < 8)
        return 0;

    scsiH2BE_U16(pbBuf, 0x1e); /* feature 001Eh: CD Read Feature */
    pbBuf[2] = (0x2 << 2) | RT_BIT(1) | RT_BIT(0); /* Version | Persistent | Current */
    pbBuf[3] = 0; /* Additional length */
    pbBuf[4] = (0x0 << 7) | (0x0 << 1) | 0x0; /* !DAP | !C2-Flags | !CD-Text. */
    /* Rest is reserved. */

    return 8;
}

static DECLCALLBACK(size_t) vscsiLunMmcGetConfigurationFillFeaturePowerManagement(uint8_t *pbBuf, size_t cbBuf)
{
    if (cbBuf < 4)
        return 0;

    scsiH2BE_U16(pbBuf, 0x100); /* feature 0100h: Power Management Feature */
    pbBuf[2] = (0x0 << 2) | RT_BIT(1) | RT_BIT(0); /* Version | Persistent | Current */
    pbBuf[3] = 0; /* Additional length */

    return 4;
}

static DECLCALLBACK(size_t) vscsiLunMmcGetConfigurationFillFeatureTimeout(uint8_t *pbBuf, size_t cbBuf)
{
    if (cbBuf < 8)
        return 0;

    scsiH2BE_U16(pbBuf, 0x105); /* feature 0105h: Timeout Feature */
    pbBuf[2] = (0x0 << 2) | RT_BIT(1) | RT_BIT(0); /* Version | Persistent | Current */
    pbBuf[3] = 4; /* Additional length */
    pbBuf[4] = 0x0; /* !Group3 */

    return 8;
}

/**
 * Processes the GET CONFIGURATION SCSI request.
 *
 * @returns SCSI status code.
 * @param   pVScsiLunMmc  The MMC LUN instance.
 * @param   pVScsiReq     The VSCSI request.
 * @param   cbMaxTransfer The maximum transfer size.
 */
static int vscsiLunMmcGetConfiguration(PVSCSILUNMMC pVScsiLunMmc, PVSCSIREQINT pVScsiReq, size_t cbMaxTransfer)
{
    uint8_t aReply[80];
    uint8_t *pbBuf = &aReply[0];
    size_t cbBuf = sizeof(aReply);
    size_t cbCopied = 0;
    uint16_t u16Sfn = scsiBE2H_U16(&pVScsiReq->pbCDB[2]);
    uint8_t u8Rt = pVScsiReq->pbCDB[1] & 0x03;

    /* Accept valid request types only. */
    if (u8Rt == 3)
        return vscsiLunReqSenseErrorSet(&pVScsiLunMmc->Core, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST,
                                        SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00);

    /** @todo implement switching between CD-ROM and DVD-ROM profile (the only
     * way to differentiate them right now is based on the image size). */
    if (pVScsiLunMmc->cSectors)
        scsiH2BE_U16(pbBuf + 6, 0x08); /* current profile: read-only CD */
    else
        scsiH2BE_U16(pbBuf + 6, 0x00); /* current profile: none -> no media */
    cbBuf    -= 8;
    pbBuf    += 8;

    if (u8Rt == 0x2)
    {
        for (uint32_t i = 0; i < RT_ELEMENTS(g_aVScsiMmcFeatures); i++)
        {
            if (g_aVScsiMmcFeatures[i].u16Feat == u16Sfn)
            {
                cbCopied = g_aVScsiMmcFeatures[i].pfnFeatureFill(pbBuf, cbBuf);
                cbBuf -= cbCopied;
                pbBuf += cbCopied;
                break;
            }
        }
    }
    else
    {
        for (uint32_t i = 0; i < RT_ELEMENTS(g_aVScsiMmcFeatures); i++)
        {
            if (g_aVScsiMmcFeatures[i].u16Feat > u16Sfn)
            {
                cbCopied = g_aVScsiMmcFeatures[i].pfnFeatureFill(pbBuf, cbBuf);
                cbBuf -= cbCopied;
                pbBuf += cbCopied;
            }
        }
    }

    /* Set data length now. */
    uint32_t    cbDataLength = (uint32_t)(sizeof(aReply) - cbBuf);
    scsiH2BE_U32(&aReply[0], cbDataLength);

    vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_T2I);
    vscsiReqSetXferSize(pVScsiReq, RT_MIN(cbMaxTransfer, cbDataLength));

    RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, RT_MIN(cbMaxTransfer, cbDataLength));
    return vscsiLunReqSenseOkSet(&pVScsiLunMmc->Core, pVScsiReq);
}

/**
 * Processes the READ DVD STRUCTURE SCSI request.
 *
 * @returns SCSI status code.
 * @param   pVScsiLunMmc  The MMC LUN instance.
 * @param   pVScsiReq     The VSCSI request.
 * @param   cbMaxTransfer The maximum transfer size.
 */
static int vscsiLunMmcReadDvdStructure(PVSCSILUNMMC pVScsiLunMmc, PVSCSIREQINT pVScsiReq, size_t cbMaxTransfer)
{
    uint8_t aReply[25]; /* Counted a maximum of 20 bytes but better be on the safe side. */

    RT_ZERO(aReply);

    /* Act according to the indicated format. */
    switch (pVScsiReq->pbCDB[7])
    {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
        case 0x08:
        case 0x09:
        case 0x0a:
        case 0x0b:
        case 0x0c:
        case 0x0d:
        case 0x0e:
        case 0x0f:
        case 0x10:
        case 0x11:
        case 0x30:
        case 0x31:
        case 0xff:
            if (pVScsiReq->pbCDB[1] == 0)
            {
                int uASC = SCSI_ASC_NONE;

                switch (pVScsiReq->pbCDB[7])
                {
                    case 0x0: /* Physical format information */
                    {
                        uint8_t uLayer = pVScsiReq->pbCDB[6];
                        uint64_t cTotalSectors;

                        if (uLayer != 0)
                        {
                            uASC = -SCSI_ASC_INV_FIELD_IN_CMD_PACKET;
                            break;
                        }

                        cTotalSectors = pVScsiLunMmc->cSectors;
                        cTotalSectors >>= 2;
                        if (cTotalSectors == 0)
                        {
                            uASC = -SCSI_ASC_MEDIUM_NOT_PRESENT;
                            break;
                        }

                        aReply[4] = 1;   /* DVD-ROM, part version 1 */
                        aReply[5] = 0xf; /* 120mm disc, minimum rate unspecified */
                        aReply[6] = 1;   /* one layer, read-only (per MMC-2 spec) */
                        aReply[7] = 0;   /* default densities */

                        /* FIXME: 0x30000 per spec? */
                        scsiH2BE_U32(&aReply[8], 0); /* start sector */
                        scsiH2BE_U32(&aReply[12], cTotalSectors - 1); /* end sector */
                        scsiH2BE_U32(&aReply[16], cTotalSectors - 1); /* l0 end sector */

                        /* Size of buffer, not including 2 byte size field */
                        scsiH2BE_U32(&aReply[0], 2048 + 2);

                        /* 2k data + 4 byte header */
                        uASC = (2048 + 4);
                        break;
                    }
                    case 0x01: /* DVD copyright information */
                        aReply[4] = 0; /* no copyright data */
                        aReply[5] = 0; /* no region restrictions */

                        /* Size of buffer, not including 2 byte size field */
                        scsiH2BE_U16(&aReply[0], 4 + 2);

                        /* 4 byte header + 4 byte data */
                        uASC = (4 + 4);
                        break;

                    case 0x03: /* BCA information - invalid field for no BCA info */
                        uASC = -SCSI_ASC_INV_FIELD_IN_CMD_PACKET;
                        break;

                    case 0x04: /* DVD disc manufacturing information */
                        /* Size of buffer, not including 2 byte size field */
                        scsiH2BE_U16(&aReply[0], 2048 + 2);

                        /* 2k data + 4 byte header */
                        uASC = (2048 + 4);
                        break;
                    case 0xff:
                        /*
                         * This lists all the command capabilities above.  Add new ones
                         * in order and update the length and buffer return values.
                         */

                        aReply[4] = 0x00; /* Physical format */
                        aReply[5] = 0x40; /* Not writable, is readable */
                        scsiH2BE_U16(&aReply[6], 2048 + 4);

                        aReply[8] = 0x01; /* Copyright info */
                        aReply[9] = 0x40; /* Not writable, is readable */
                        scsiH2BE_U16(&aReply[10], 4 + 4);

                        aReply[12] = 0x03; /* BCA info */
                        aReply[13] = 0x40; /* Not writable, is readable */
                        scsiH2BE_U16(&aReply[14], 188 + 4);

                        aReply[16] = 0x04; /* Manufacturing info */
                        aReply[17] = 0x40; /* Not writable, is readable */
                        scsiH2BE_U16(&aReply[18], 2048 + 4);

                        /* Size of buffer, not including 2 byte size field */
                        scsiH2BE_U16(&aReply[0], 16 + 2);

                        /* data written + 4 byte header */
                        uASC = (16 + 4);
                        break;
                    default: /** @todo formats beyond DVD-ROM requires */
                        uASC = -SCSI_ASC_INV_FIELD_IN_CMD_PACKET;
                }

                if (uASC < 0)
                    return vscsiLunReqSenseErrorSet(&pVScsiLunMmc->Core, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST,
                                                    -uASC, 0x00);
                break;
            }
            /** @todo BD support, fall through for now */
            RT_FALL_THRU();

        /* Generic disk structures */
        case 0x80: /** @todo AACS volume identifier */
        case 0x81: /** @todo AACS media serial number */
        case 0x82: /** @todo AACS media identifier */
        case 0x83: /** @todo AACS media key block */
        case 0x90: /** @todo List of recognized format layers */
        case 0xc0: /** @todo Write protection status */
        default:
            return vscsiLunReqSenseErrorSet(&pVScsiLunMmc->Core, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST,
                                            SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
    }

    vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_T2I);
    vscsiReqSetXferSize(pVScsiReq, RT_MIN(cbMaxTransfer, sizeof(aReply)));

    RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, RT_MIN(cbMaxTransfer, sizeof(aReply)));
    return vscsiLunReqSenseOkSet(&pVScsiLunMmc->Core, pVScsiReq);
}

/**
 * Processes the MODE SENSE 10 SCSI request.
 *
 * @returns SCSI status code.
 * @param   pVScsiLunMmc  The MMC LUN instance.
 * @param   pVScsiReq     The VSCSI request.
 * @param   cbMaxTransfer The maximum transfer size.
 */
static int vscsiLunMmcModeSense10(PVSCSILUNMMC pVScsiLunMmc, PVSCSIREQINT pVScsiReq, size_t cbMaxTransfer)
{
    int rcReq;
    uint8_t uPageControl = pVScsiReq->pbCDB[2] >> 6;
    uint8_t uPageCode = pVScsiReq->pbCDB[2] & 0x3f;

    switch (uPageControl)
    {
        case SCSI_PAGECONTROL_CURRENT:
            switch (uPageCode)
            {
                case SCSI_MODEPAGE_ERROR_RECOVERY:
                {
                    uint8_t aReply[16];

                    scsiH2BE_U16(&aReply[0], 16 + 6);
                    aReply[2] = (uint8_t)pVScsiLunMmc->u32MediaTrackType;
                    aReply[3] = 0;
                    aReply[4] = 0;
                    aReply[5] = 0;
                    aReply[6] = 0;
                    aReply[7] = 0;

                    aReply[8] = 0x01;
                    aReply[9] = 0x06;
                    aReply[10] = 0x00;
                    aReply[11] = 0x05;
                    aReply[12] = 0x00;
                    aReply[13] = 0x00;
                    aReply[14] = 0x00;
                    aReply[15] = 0x00;

                    vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_T2I);
                    vscsiReqSetXferSize(pVScsiReq, RT_MIN(cbMaxTransfer, sizeof(aReply)));

                    RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, RT_MIN(cbMaxTransfer, sizeof(aReply)));
                    rcReq = vscsiLunReqSenseOkSet(&pVScsiLunMmc->Core, pVScsiReq);
                    break;
                }
                case SCSI_MODEPAGE_CD_STATUS:
                {
                    uint8_t aReply[40];

                    scsiH2BE_U16(&aReply[0], 38);
                    aReply[2] = (uint8_t)pVScsiLunMmc->u32MediaTrackType;
                    aReply[3] = 0;
                    aReply[4] = 0;
                    aReply[5] = 0;
                    aReply[6] = 0;
                    aReply[7] = 0;

                    aReply[8] = 0x2a;
                    aReply[9] = 30; /* page length */
                    aReply[10] = 0x08; /* DVD-ROM read support */
                    aReply[11] = 0x00; /* no write support */
                    /* The following claims we support audio play. This is obviously false,
                     * but the Linux generic CDROM support makes many features depend on this
                     * capability. If it's not set, this causes many things to be disabled. */
                    aReply[12] = 0x71; /* multisession support, mode 2 form 1/2 support, audio play */
                    aReply[13] = 0x00; /* no subchannel reads supported */
                    aReply[14] = (1 << 0) | (1 << 3) | (1 << 5); /* lock supported, eject supported, tray type loading mechanism */
                    if (pVScsiLunMmc->fLocked)
                        aReply[14] |= 1 << 1; /* report lock state */
                    aReply[15] = 0; /* no subchannel reads supported, no separate audio volume control, no changer etc. */
                    scsiH2BE_U16(&aReply[16], 5632); /* (obsolete) claim 32x speed support */
                    scsiH2BE_U16(&aReply[18], 2); /* number of audio volume levels */
                    scsiH2BE_U16(&aReply[20], 128); /* buffer size supported in Kbyte - We don't have a buffer because we write directly into guest memory.
                                                       Just write some dummy value. */
                    scsiH2BE_U16(&aReply[22], 5632); /* (obsolete) current read speed 32x */
                    aReply[24] = 0; /* reserved */
                    aReply[25] = 0; /* reserved for digital audio (see idx 15) */
                    scsiH2BE_U16(&aReply[26], 0); /* (obsolete) maximum write speed */
                    scsiH2BE_U16(&aReply[28], 0); /* (obsolete) current write speed */
                    scsiH2BE_U16(&aReply[30], 0); /* copy management revision supported 0=no CSS */
                    aReply[32] = 0; /* reserved */
                    aReply[33] = 0; /* reserved */
                    aReply[34] = 0; /* reserved */
                    aReply[35] = 1; /* rotation control CAV */
                    scsiH2BE_U16(&aReply[36], 0); /* current write speed */
                    scsiH2BE_U16(&aReply[38], 0); /* number of write speed performance descriptors */

                    vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_T2I);
                    vscsiReqSetXferSize(pVScsiReq, RT_MIN(cbMaxTransfer, sizeof(aReply)));

                    RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, RT_MIN(cbMaxTransfer, sizeof(aReply)));
                    rcReq = vscsiLunReqSenseOkSet(&pVScsiLunMmc->Core, pVScsiReq);
                    break;
                }
                default:
                    rcReq = vscsiLunReqSenseErrorSet(&pVScsiLunMmc->Core, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST,
                                                     SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
                    break;
            }
            break;
        case SCSI_PAGECONTROL_CHANGEABLE:
        case SCSI_PAGECONTROL_DEFAULT:
            rcReq = vscsiLunReqSenseErrorSet(&pVScsiLunMmc->Core, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST,
                                             SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
            break;
        default:
        case SCSI_PAGECONTROL_SAVED:
            rcReq = vscsiLunReqSenseErrorSet(&pVScsiLunMmc->Core, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST,
                                             SCSI_ASC_SAVING_PARAMETERS_NOT_SUPPORTED, 0x00);
            break;
    }

    return rcReq;
}

/**
 * Processes the GET EVENT STATUS NOTIFICATION SCSI request.
 *
 * @returns SCSI status code.
 * @param   pVScsiLunMmc  The MMC LUN instance.
 * @param   pVScsiReq     The VSCSI request.
 * @param   cbMaxTransfer The maximum transfer size.
 */
static int vscsiLunMmcGetEventStatusNotification(PVSCSILUNMMC pVScsiLunMmc, PVSCSIREQINT pVScsiReq,
                                                 size_t cbMaxTransfer)
{
    uint32_t OldStatus;
    uint32_t NewStatus;
    uint8_t aReply[8];
    RT_ZERO(aReply);

    LogFlowFunc(("pVScsiLunMmc=%#p pVScsiReq=%#p cbMaxTransfer=%zu\n",
                 pVScsiLunMmc, pVScsiReq, cbMaxTransfer));

    do
    {
        OldStatus = ASMAtomicReadU32((volatile uint32_t *)&pVScsiLunMmc->MediaEventStatus);
        NewStatus = MMCEVENTSTATUSTYPE_UNCHANGED;

        switch (OldStatus)
        {
            case MMCEVENTSTATUSTYPE_MEDIA_NEW:
                /* mount */
                scsiH2BE_U16(&aReply[0], 6);
                aReply[2] = 0x04; /* media */
                aReply[3] = 0x5e; /* supported = busy|media|external|power|operational */
                aReply[4] = 0x02; /* new medium */
                aReply[5] = 0x02; /* medium present / door closed */
                aReply[6] = 0x00;
                aReply[7] = 0x00;
                pVScsiLunMmc->Core.fReady = true;
                break;

            case MMCEVENTSTATUSTYPE_MEDIA_CHANGED:
            case MMCEVENTSTATUSTYPE_MEDIA_REMOVED:
                /* umount */
                scsiH2BE_U16(&aReply[0], 6);
                aReply[2] = 0x04; /* media */
                aReply[3] = 0x5e; /* supported = busy|media|external|power|operational */
                aReply[4] = (OldStatus == MMCEVENTSTATUSTYPE_MEDIA_CHANGED) ? 0x04 /* media changed */ : 0x03; /* media removed */
                aReply[5] = 0x00; /* medium absent / door closed */
                aReply[6] = 0x00;
                aReply[7] = 0x00;
                if (OldStatus == MMCEVENTSTATUSTYPE_MEDIA_CHANGED)
                    NewStatus = MMCEVENTSTATUSTYPE_MEDIA_NEW;
                break;

            case MMCEVENTSTATUSTYPE_MEDIA_EJECT_REQUESTED: /* currently unused */
                scsiH2BE_U16(&aReply[0], 6);
                aReply[2] = 0x04; /* media */
                aReply[3] = 0x5e; /* supported = busy|media|external|power|operational */
                aReply[4] = 0x01; /* eject requested (eject button pressed) */
                aReply[5] = 0x02; /* medium present / door closed */
                aReply[6] = 0x00;
                aReply[7] = 0x00;
                break;

            case MMCEVENTSTATUSTYPE_UNCHANGED:
            default:
                scsiH2BE_U16(&aReply[0], 6);
                aReply[2] = 0x01; /* operational change request / notification */
                aReply[3] = 0x5e; /* supported = busy|media|external|power|operational */
                aReply[4] = 0x00;
                aReply[5] = 0x00;
                aReply[6] = 0x00;
                aReply[7] = 0x00;
                break;
        }

        LogFlowFunc(("OldStatus=%u NewStatus=%u\n", OldStatus, NewStatus));

    } while (!ASMAtomicCmpXchgU32((volatile uint32_t *)&pVScsiLunMmc->MediaEventStatus, NewStatus, OldStatus));

    vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_T2I);
    vscsiReqSetXferSize(pVScsiReq, RT_MIN(cbMaxTransfer, sizeof(aReply)));

    RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, RT_MIN(cbMaxTransfer, sizeof(aReply)));
    return vscsiLunReqSenseOkSet(&pVScsiLunMmc->Core, pVScsiReq);
}

/**
 * Processes a READ TRACK INFORMATION SCSI request.
 *
 * @returns SCSI status code.
 * @param   pVScsiLunMmc  The MMC LUN instance.
 * @param   pVScsiReq     The VSCSI request.
 * @param   cbMaxTransfer The maximum transfer size.
 */
static int vscsiLunMmcReadTrackInformation(PVSCSILUNMMC pVScsiLunMmc, PVSCSIREQINT pVScsiReq,
                                           size_t cbMaxTransfer)
{
    int rcReq;
    uint32_t u32LogAddr = scsiBE2H_U32(&pVScsiReq->pbCDB[2]);
    uint8_t u8LogAddrType = pVScsiReq->pbCDB[1] & 0x03;

    int rc = VINF_SUCCESS;
    uint64_t u64LbaStart = 0;
    uint32_t uRegion = 0;
    uint64_t cBlocks = 0;
    uint64_t cbBlock = 0;
    VDREGIONDATAFORM enmDataForm = VDREGIONDATAFORM_INVALID;

    switch (u8LogAddrType)
    {
        case 0x00:
            rc = vscsiLunMediumQueryRegionPropertiesForLba(&pVScsiLunMmc->Core, u32LogAddr, &uRegion,
                                                           NULL, NULL, NULL);
            if (RT_SUCCESS(rc))
                rc = vscsiLunMediumQueryRegionProperties(&pVScsiLunMmc->Core, uRegion, &u64LbaStart,
                                                         &cBlocks, &cbBlock, &enmDataForm);
            break;
        case 0x01:
        {
            if (u32LogAddr >= 1)
            {
                uRegion = u32LogAddr - 1;
                rc = vscsiLunMediumQueryRegionProperties(&pVScsiLunMmc->Core, uRegion, &u64LbaStart,
                                                         &cBlocks, &cbBlock, &enmDataForm);
            }
            else
                rc = VERR_NOT_FOUND; /** @todo Return lead-in information. */
            break;
        }
        case 0x02:
        default:
            rc = VERR_INVALID_PARAMETER;
    }

    if (RT_SUCCESS(rc))
    {
        uint8_t u8DataMode = 0xf; /* Unknown data mode. */
        uint8_t u8TrackMode = 0;
        uint8_t aReply[36];
        RT_ZERO(aReply);

        switch (enmDataForm)
        {
            case VDREGIONDATAFORM_MODE1_2048:
            case VDREGIONDATAFORM_MODE1_2352:
            case VDREGIONDATAFORM_MODE1_0:
                u8DataMode = 1;
                break;
            case VDREGIONDATAFORM_XA_2336:
            case VDREGIONDATAFORM_XA_2352:
            case VDREGIONDATAFORM_XA_0:
            case VDREGIONDATAFORM_MODE2_2336:
            case VDREGIONDATAFORM_MODE2_2352:
            case VDREGIONDATAFORM_MODE2_0:
                u8DataMode = 2;
                break;
            default:
                u8DataMode = 0xf;
        }

        if (enmDataForm == VDREGIONDATAFORM_CDDA)
            u8TrackMode = 0x0;
        else
            u8TrackMode = 0x4;

        scsiH2BE_U16(&aReply[0], 34);
        aReply[2] = uRegion + 1;                                            /* track number (LSB) */
        aReply[3] = 1;                                                      /* session number (LSB) */
        aReply[5] = (0 << 5) | (0 << 4) | u8TrackMode;                      /* not damaged, primary copy, data track */
        aReply[6] = (0 << 7) | (0 << 6) | (0 << 5) | (0 << 6) | u8DataMode; /* not reserved track, not blank, not packet writing, not fixed packet, data mode 1 */
        aReply[7] = (0 << 1) | (0 << 0);                                    /* last recorded address not valid, next recordable address not valid */
        scsiH2BE_U32(&aReply[8], (uint32_t)u64LbaStart);                    /* track start address is 0 */
        scsiH2BE_U32(&aReply[24], (uint32_t)cBlocks);                       /* track size */
        aReply[32] = 0;                                                     /* track number (MSB) */
        aReply[33] = 0;                                                     /* session number (MSB) */

        vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_T2I);
        vscsiReqSetXferSize(pVScsiReq, RT_MIN(sizeof(aReply), cbMaxTransfer));

        RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, RT_MIN(sizeof(aReply), cbMaxTransfer));
        rcReq = vscsiLunReqSenseOkSet(&pVScsiLunMmc->Core, pVScsiReq);
    }
    else
        rcReq = vscsiLunReqSenseErrorSet(&pVScsiLunMmc->Core, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST,
                                         SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00);

    return rcReq;
}

static DECLCALLBACK(int) vscsiLunMmcInit(PVSCSILUNINT pVScsiLun)
{
    PVSCSILUNMMC    pVScsiLunMmc = (PVSCSILUNMMC)pVScsiLun;
    int             rc = VINF_SUCCESS;

    ASMAtomicWriteU32((volatile uint32_t *)&pVScsiLunMmc->MediaEventStatus, MMCEVENTSTATUSTYPE_UNCHANGED);
    pVScsiLunMmc->u32MediaTrackType = MMC_MEDIA_TYPE_UNKNOWN;
    pVScsiLunMmc->cSectors          = 0;

    uint32_t cTracks = vscsiLunMediumGetRegionCount(pVScsiLun);
    if (cTracks)
    {
        for (uint32_t i = 0; i < cTracks; i++)
        {
            uint64_t cBlocks = 0;
            rc = vscsiLunMediumQueryRegionProperties(pVScsiLun, i, NULL, &cBlocks,
                                                     NULL, NULL);
            AssertRC(rc);

            pVScsiLunMmc->cSectors += cBlocks;
        }

        pVScsiLunMmc->Core.fMediaPresent = true;
        pVScsiLunMmc->Core.fReady = false;
    }
    else
    {
        pVScsiLunMmc->Core.fMediaPresent = false;
        pVScsiLunMmc->Core.fReady = false;
    }

    return rc;
}

static DECLCALLBACK(int) vscsiLunMmcDestroy(PVSCSILUNINT pVScsiLun)
{
    RT_NOREF1(pVScsiLun);
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) vscsiLunMmcReqProcess(PVSCSILUNINT pVScsiLun, PVSCSIREQINT pVScsiReq)
{
    PVSCSILUNMMC    pVScsiLunMmc = (PVSCSILUNMMC)pVScsiLun;
    VSCSIIOREQTXDIR enmTxDir = VSCSIIOREQTXDIR_INVALID;
    uint64_t        uLbaStart = 0;
    uint32_t        cSectorTransfer = 0;
    size_t          cbSector = 0;
    int             rc = VINF_SUCCESS;
    int             rcReq = SCSI_STATUS_OK;
    unsigned        uCmd = pVScsiReq->pbCDB[0];
    PCRTSGSEG       paSegs = pVScsiReq->SgBuf.paSegs;
    unsigned        cSegs  = pVScsiReq->SgBuf.cSegs;

    LogFlowFunc(("pVScsiLun=%#p{.fReady=%RTbool, .fMediaPresent=%RTbool} pVScsiReq=%#p{.pbCdb[0]=%#x}\n",
                 pVScsiLun, pVScsiLun->fReady, pVScsiLun->fMediaPresent, pVScsiReq, uCmd));

    /*
     * GET CONFIGURATION, GET EVENT/STATUS NOTIFICATION, INQUIRY, and REQUEST SENSE commands
     * operate even when a unit attention condition exists for initiator; every other command
     * needs to report CHECK CONDITION in that case.
     */
    if (   !pVScsiLunMmc->Core.fReady
        && uCmd != SCSI_INQUIRY
        && uCmd != SCSI_GET_CONFIGURATION
        && uCmd != SCSI_GET_EVENT_STATUS_NOTIFICATION)
    {
        /*
         * A note on media changes: As long as a medium is not present, the unit remains in
         * the 'not ready' state. Technically the unit becomes 'ready' soon after a medium
         * is inserted; however, we internally keep the 'not ready' state until we've had
         * a chance to report the UNIT ATTENTION status indicating a media change.
         */
        if (pVScsiLunMmc->Core.fMediaPresent)
        {
            rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_UNIT_ATTENTION,
                                             SCSI_ASC_MEDIUM_MAY_HAVE_CHANGED, 0x00);
            pVScsiLunMmc->Core.fReady = true;
        }
        else
            rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_NOT_READY,
                                             SCSI_ASC_MEDIUM_NOT_PRESENT, 0x00);
    }
    else
    {
        switch (uCmd)
        {
            case SCSI_TEST_UNIT_READY:
                Assert(!pVScsiLunMmc->Core.fReady); /* Only should get here if LUN isn't ready. */
                vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_NONE);
                rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_NOT_READY, SCSI_ASC_MEDIUM_NOT_PRESENT, 0x00);
                break;

            case SCSI_INQUIRY:
            {
                SCSIINQUIRYDATA ScsiInquiryReply;

                memset(&ScsiInquiryReply, 0, sizeof(ScsiInquiryReply));

                ScsiInquiryReply.cbAdditional           = 31;
                ScsiInquiryReply.fRMB                   = 1;    /* Removable. */
                ScsiInquiryReply.u5PeripheralDeviceType = SCSI_INQUIRY_DATA_PERIPHERAL_DEVICE_TYPE_CD_DVD;
                ScsiInquiryReply.u3PeripheralQualifier  = SCSI_INQUIRY_DATA_PERIPHERAL_QUALIFIER_CONNECTED;
                ScsiInquiryReply.u3AnsiVersion          = 0x05; /* MMC-?? compliant */
                ScsiInquiryReply.fCmdQue                = 1;    /* Command queuing supported. */
                ScsiInquiryReply.fWBus16                = 1;

                const char *pszVendorId = "VBOX";
                const char *pszProductId = "CD-ROM";
                const char *pszProductLevel = "1.0";
                int rcTmp = vscsiLunQueryInqStrings(pVScsiLun, &pszVendorId, &pszProductId, &pszProductLevel);
                Assert(RT_SUCCESS(rcTmp) || rcTmp == VERR_NOT_FOUND); RT_NOREF(rcTmp);

                scsiPadStrS(ScsiInquiryReply.achVendorId, pszVendorId, 8);
                scsiPadStrS(ScsiInquiryReply.achProductId, pszProductId, 16);
                scsiPadStrS(ScsiInquiryReply.achProductLevel, pszProductLevel, 4);

                uint16_t cbDataReq = scsiBE2H_U16(&pVScsiReq->pbCDB[3]);
                vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_T2I);
                vscsiReqSetXferSize(pVScsiReq, RT_MIN(sizeof(SCSIINQUIRYDATA), cbDataReq));

                RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, (uint8_t *)&ScsiInquiryReply, sizeof(SCSIINQUIRYDATA));
                rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                break;
            }
            case SCSI_READ_CAPACITY:
            {
                uint8_t aReply[8];
                memset(aReply, 0, sizeof(aReply));

                /*
                 * If sector size exceeds the maximum value that is
                 * able to be stored in 4 bytes return 0xffffffff in this field
                 */
                if (pVScsiLunMmc->cSectors > UINT32_C(0xffffffff))
                    scsiH2BE_U32(aReply, UINT32_C(0xffffffff));
                else
                    scsiH2BE_U32(aReply, pVScsiLunMmc->cSectors - 1);
                scsiH2BE_U32(&aReply[4], _2K);

                vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_T2I);
                vscsiReqSetXferSize(pVScsiReq, sizeof(aReply));

                RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, sizeof(aReply));
                rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                break;
            }
            case SCSI_MODE_SENSE_6:
            {
                uint8_t uModePage = pVScsiReq->pbCDB[2] & 0x3f;
                uint8_t aReply[24];
                uint8_t *pu8ReplyPos;
                bool    fValid = false;

                memset(aReply, 0, sizeof(aReply));
                aReply[0] = 4; /* Reply length 4. */
                aReply[1] = 0; /* Default media type. */
                aReply[2] = RT_BIT(4); /* Caching supported. */
                aReply[3] = 0; /* Block descriptor length. */

                pu8ReplyPos = aReply + 4;

                if ((uModePage == 0x08) || (uModePage == 0x3f))
                {
                    memset(pu8ReplyPos, 0, 20);
                    *pu8ReplyPos++ = 0x08; /* Page code. */
                    *pu8ReplyPos++ = 0x12; /* Size of the page. */
                    *pu8ReplyPos++ = 0x4;  /* Write cache enabled. */
                    fValid = true;
                } else if (uModePage == 0) {
                    fValid = true;
                }

                /* Querying unknown pages must fail. */
                if (fValid) {
                    vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_T2I);
                    vscsiReqSetXferSize(pVScsiReq, RT_MIN(sizeof(aReply), pVScsiReq->pbCDB[4]));

                    RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, sizeof(aReply));
                    rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                } else {
                    rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
                }
                break;
            }
            case SCSI_MODE_SENSE_10:
            {
                size_t cbMax = scsiBE2H_U16(&pVScsiReq->pbCDB[7]);
                rcReq = vscsiLunMmcModeSense10(pVScsiLunMmc, pVScsiReq, cbMax);
                break;
            }
            case SCSI_SEEK_10:
            {
                uint32_t uLba = scsiBE2H_U32(&pVScsiReq->pbCDB[2]);

                vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_NONE);
                if (uLba > pVScsiLunMmc->cSectors)
                    rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST,
                                                     SCSI_ASC_LOGICAL_BLOCK_OOR, 0x00);
                else
                    rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                break;
            }
            case SCSI_MODE_SELECT_6:
            {
                /** @todo implement!! */
                vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_I2T);
                vscsiReqSetXferSize(pVScsiReq, pVScsiReq->pbCDB[4]);
                rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                break;
            }
            case SCSI_READ_6:
            {
                enmTxDir       = VSCSIIOREQTXDIR_READ;
                uLbaStart      = ((uint64_t)    pVScsiReq->pbCDB[3]
                                            |  (pVScsiReq->pbCDB[2] <<  8)
                                            | ((pVScsiReq->pbCDB[1] & 0x1f) << 16));
                cSectorTransfer = pVScsiReq->pbCDB[4];
                cSectorTransfer = cSectorTransfer ? cSectorTransfer : 256;  /* Zero blocks means 256 */
                cbSector        = _2K;
                break;
            }
            case SCSI_READ_10:
            {
                enmTxDir        = VSCSIIOREQTXDIR_READ;
                uLbaStart       = scsiBE2H_U32(&pVScsiReq->pbCDB[2]);
                cSectorTransfer = scsiBE2H_U16(&pVScsiReq->pbCDB[7]);
                cbSector        = _2K;
                break;
            }
            case SCSI_READ_12:
            {
                enmTxDir        = VSCSIIOREQTXDIR_READ;
                uLbaStart       = scsiBE2H_U32(&pVScsiReq->pbCDB[2]);
                cSectorTransfer = scsiBE2H_U32(&pVScsiReq->pbCDB[6]);
                cbSector        = _2K;
                break;
            }
            case SCSI_READ_16:
            {
                enmTxDir        = VSCSIIOREQTXDIR_READ;
                uLbaStart       = scsiBE2H_U64(&pVScsiReq->pbCDB[2]);
                cSectorTransfer = scsiBE2H_U32(&pVScsiReq->pbCDB[10]);
                cbSector        = _2K;
                break;
            }
            case SCSI_READ_CD:
            {
                uLbaStart       = scsiBE2H_U32(&pVScsiReq->pbCDB[2]);
                cSectorTransfer = (pVScsiReq->pbCDB[6] << 16) | (pVScsiReq->pbCDB[7] << 8) | pVScsiReq->pbCDB[8];

                /*
                 * If the LBA is in an audio track we are required to ignore pretty much all
                 * of the channel selection values (except 0x00) and map everything to 0x10
                 * which means read user data with a sector size of 2352 bytes.
                 *
                 * (MMC-6 chapter 6.19.2.6)
                 */
                uint8_t uChnSel = pVScsiReq->pbCDB[9] & 0xf8;
                VDREGIONDATAFORM enmDataForm;
                uint64_t cbSectorRegion = 0;
                rc = vscsiLunMediumQueryRegionPropertiesForLba(pVScsiLun, uLbaStart,
                                                               NULL, NULL, &cbSectorRegion,
                                                               &enmDataForm);
                if (RT_FAILURE(rc))
                {
                    rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST,
                                                     SCSI_ASC_LOGICAL_BLOCK_OOR, 0x00);
                    break;
                }

                if (enmDataForm == VDREGIONDATAFORM_CDDA)
                {
                    if (uChnSel == 0)
                    {
                        /* nothing */
                        rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                    }
                    else
                    {
                        enmTxDir = VSCSIIOREQTXDIR_READ;
                        cbSector = 2352;
                    }
                }
                else
                {
                    switch (uChnSel)
                    {
                        case 0x00:
                            /* nothing */
                            rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                            break;
                        case 0x10:
                            /* user data only (normal read) */
                            enmTxDir = VSCSIIOREQTXDIR_READ;
                            cbSector = _2K;
                            break;
                        case 0xf8:
                        {
                            /* everything (sync, headers, user data, ECC) */
                            if (cbSectorRegion == 2048)
                            {
                                /*
                                 * Read all data, sector size is 2352.
                                 * Rearrange the buffer and fill the gaps with the sync bytes.
                                 */
                                /* Count the number of segments for the buffer we require. */
                                RTSGBUF SgBuf;
                                bool fBufTooSmall = false;
                                uint32_t cSegsNew = 0;
                                RTSgBufClone(&SgBuf, &pVScsiReq->SgBuf);
                                for (uint32_t uLba = (uint32_t)uLbaStart; uLba < uLbaStart + cSectorTransfer; uLba++)
                                {
                                    size_t cbTmp = RTSgBufAdvance(&SgBuf, 16);
                                    if (cbTmp < 16)
                                    {
                                        fBufTooSmall = true;
                                        break;
                                    }

                                    cbTmp = 2048;
                                    while (cbTmp)
                                    {
                                        size_t cbBuf = cbTmp;
                                        RTSgBufGetNextSegment(&SgBuf, &cbBuf);
                                        if (!cbBuf)
                                        {
                                            fBufTooSmall = true;
                                            break;
                                        }

                                        cbTmp -= cbBuf;
                                        cSegsNew++;
                                    }

                                    cbTmp = RTSgBufAdvance(&SgBuf, 280);
                                    if (cbTmp < 280)
                                    {
                                        fBufTooSmall = true;
                                        break;
                                    }
                                }

                                if (!fBufTooSmall)
                                {
                                    PRTSGSEG paSegsNew = (PRTSGSEG)RTMemAllocZ(cSegsNew * sizeof(RTSGSEG));
                                    if (paSegsNew)
                                    {
                                        Assert(cbSectorRegion == 2048);
                                        cbSector = cbSectorRegion;
                                        enmTxDir = VSCSIIOREQTXDIR_READ;

                                        uint32_t idxSeg = 0;
                                        for (uint32_t uLba = (uint32_t)uLbaStart; uLba < uLbaStart + cSectorTransfer; uLba++)
                                        {
                                            /* Sync bytes, see 4.2.3.8 CD Main Channel Block Formats */
                                            uint8_t abBuf[16];
                                            abBuf[0] = 0x00;
                                            memset(&abBuf[1], 0xff, 10);
                                            abBuf[11] = 0x00;
                                            /* MSF */
                                            scsiLBA2MSF(&abBuf[12], uLba);
                                            abBuf[15] = 0x01; /* mode 1 data */
                                            RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, &abBuf[0], sizeof(abBuf));

                                            size_t cbTmp = 2048;
                                            while (cbTmp)
                                            {
                                                size_t cbBuf = cbTmp;
                                                paSegsNew[idxSeg].pvSeg = RTSgBufGetNextSegment(&pVScsiReq->SgBuf, &cbBuf);
                                                paSegsNew[idxSeg].cbSeg = cbBuf;
                                                idxSeg++;

                                                cbTmp -= cbBuf;
                                            }

                                            /**
                                             * @todo: maybe compute ECC and parity, layout is:
                                             * 2072 4   EDC
                                             * 2076 172 P parity symbols
                                             * 2248 104 Q parity symbols
                                             */
                                            RTSgBufSet(&pVScsiReq->SgBuf, 0, 280);
                                        }

                                        paSegs = paSegsNew;
                                        cSegs  = cSegsNew;
                                        pVScsiReq->pvLun = paSegsNew;
                                    }
                                    else
                                        rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST,
                                                                         SCSI_ASC_LOGICAL_BLOCK_OOR, 0x00);
                                }
                                else
                                    rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST,
                                                                     SCSI_ASC_LOGICAL_BLOCK_OOR, 0x00);
                            }
                            else if (cbSectorRegion == 2352)
                            {
                                /* Sector size matches what is read. */
                                cbSector = cbSectorRegion;
                                enmTxDir = VSCSIIOREQTXDIR_READ;
                            }
                            break;
                        }
                        default:
                            rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST,
                                                             SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
                            break;
                    }
                }
                break;
            }
            case SCSI_READ_BUFFER:
            {
                uint8_t uDataMode = pVScsiReq->pbCDB[1] & 0x1f;

                switch (uDataMode)
                {
                    case 0x00:
                    case 0x01:
                    case 0x02:
                    case 0x03:
                    case 0x0a:
                        break;
                    case 0x0b:
                    {
                        uint8_t aReply[4];
                        RT_ZERO(aReply);

                        vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_T2I);
                        vscsiReqSetXferSize(pVScsiReq, RT_MIN(sizeof(aReply), scsiBE2H_U24(&pVScsiReq->pbCDB[6])));

                        RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, sizeof(aReply));
                        rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                        break;
                    }
                    case 0x1a:
                    case 0x1c:
                        break;
                    default:
                        AssertMsgFailed(("Invalid data mode\n"));
                }
                break;
            }
            case SCSI_START_STOP_UNIT:
            {
                int rc2 = VINF_SUCCESS;

                vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_NONE);
                switch (pVScsiReq->pbCDB[4] & 3)
                {
                    case 0: /* 00 - Stop motor */
                    case 1: /* 01 - Start motor */
                        break;
                    case 2: /* 10 - Eject media */
                        rc2 = vscsiLunMediumEject(pVScsiLun);
                        break;
                    case 3: /* 11 - Load media */
                        /** @todo */
                        break;
                }
                if (RT_SUCCESS(rc2))
                    rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                else
                    rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_MEDIA_LOAD_OR_EJECT_FAILED, 0x02);
                break;
            }
            case SCSI_LOG_SENSE:
            {
                uint8_t uPageCode = pVScsiReq->pbCDB[2] & 0x3f;
                uint8_t uSubPageCode = pVScsiReq->pbCDB[3];

                switch (uPageCode)
                {
                    case 0x00:
                    {
                        if (uSubPageCode == 0)
                        {
                            uint8_t aReply[4];

                            aReply[0] = 0;
                            aReply[1] = 0;
                            aReply[2] = 0;
                            aReply[3] = 0;

                            vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_T2I);
                            vscsiReqSetXferSize(pVScsiReq, RT_MIN(sizeof(aReply), scsiBE2H_U16(&pVScsiReq->pbCDB[7])));

                            RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, sizeof(aReply));
                            rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                            break;
                        }
                    }
                    RT_FALL_THRU();
                    default:
                        rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
                }
                break;
            }
            case SCSI_SERVICE_ACTION_IN_16:
            {
                switch (pVScsiReq->pbCDB[1] & 0x1f)
                {
                    case SCSI_SVC_ACTION_IN_READ_CAPACITY_16:
                    {
                        uint8_t aReply[32];

                        memset(aReply, 0, sizeof(aReply));
                        scsiH2BE_U64(aReply, pVScsiLunMmc->cSectors - 1);
                        scsiH2BE_U32(&aReply[8], _2K);
                        /* Leave the rest 0 */

                        vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_T2I);
                        vscsiReqSetXferSize(pVScsiReq, sizeof(aReply));

                        RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, sizeof(aReply));
                        rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                        break;
                    }
                    default:
                        rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST,
                                                         SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00); /* Don't know if this is correct */
                }
                break;
            }
            case SCSI_PREVENT_ALLOW_MEDIUM_REMOVAL:
            {
                pVScsiLunMmc->fLocked = RT_BOOL(pVScsiReq->pbCDB[4] & 0x01);
                vscsiLunMediumSetLock(pVScsiLun, pVScsiLunMmc->fLocked);
                rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                break;
            }
            case SCSI_READ_TOC_PMA_ATIP:
            {
                uint8_t     format;
                uint16_t    cbMax;
                bool        fMSF;

                format = pVScsiReq->pbCDB[2] & 0x0f;
                cbMax  = scsiBE2H_U16(&pVScsiReq->pbCDB[7]);
                fMSF   = (pVScsiReq->pbCDB[1] >> 1) & 1;

                switch (format)
                {
                    case 0x00:
                        rcReq = mmcReadTOCNormal(pVScsiLun, pVScsiReq, cbMax, fMSF);
                        break;
                    case 0x01:
                        rcReq = mmcReadTOCMulti(pVScsiLun, pVScsiReq, cbMax, fMSF);
                        break;
                    case 0x02:
                        rcReq = mmcReadTOCRaw(pVScsiLun, pVScsiReq, cbMax, fMSF);
                        break;
                    default:
                        rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
                }
                break;
            }
            case SCSI_GET_EVENT_STATUS_NOTIFICATION:
            {
                /* Only supporting polled mode at the moment. */
                size_t cbMax = scsiBE2H_U16(&pVScsiReq->pbCDB[7]);

                if (pVScsiReq->pbCDB[1] & 0x1)
                    rcReq = vscsiLunMmcGetEventStatusNotification(pVScsiLunMmc, pVScsiReq, cbMax);
                else
                    rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
                break;
            }
            case SCSI_MECHANISM_STATUS:
            {
                uint8_t aReply[8];

                scsiH2BE_U16(&aReply[0], 0);
                /* no current LBA */
                aReply[2] = 0;
                aReply[3] = 0;
                aReply[4] = 0;
                aReply[5] = 1;
                scsiH2BE_U16(&aReply[6], 0);

                size_t cbMax = scsiBE2H_U16(&pVScsiReq->pbCDB[8]);
                vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_T2I);
                vscsiReqSetXferSize(pVScsiReq, RT_MIN(sizeof(aReply), cbMax));

                RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, RT_MIN(sizeof(aReply), cbMax));
                rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                break;
            }
            case SCSI_READ_DISC_INFORMATION:
            {
                uint8_t aReply[34];
                size_t cbMax = scsiBE2H_U16(&pVScsiReq->pbCDB[7]);

                memset(aReply, '\0', sizeof(aReply));
                scsiH2BE_U16(&aReply[0], 32);
                aReply[2] = (0 << 4) | (3 << 2) | (2 << 0); /* not erasable, complete session, complete disc */
                aReply[3] = 1; /* number of first track */
                aReply[4] = 1; /* number of sessions (LSB) */
                aReply[5] = 1; /* first track number in last session (LSB) */
                aReply[6] = (uint8_t)vscsiLunMediumGetRegionCount(pVScsiLun); /* last track number in last session (LSB) */
                aReply[7] = (0 << 7) | (0 << 6) | (1 << 5) | (0 << 2) | (0 << 0); /* disc id not valid, disc bar code not valid, unrestricted use, not dirty, not RW medium */
                aReply[8] = 0; /* disc type = CD-ROM */
                aReply[9] = 0; /* number of sessions (MSB) */
                aReply[10] = 0; /* number of sessions (MSB) */
                aReply[11] = 0; /* number of sessions (MSB) */
                scsiH2BE_U32(&aReply[16], 0x00ffffff); /* last session lead-in start time is not available */
                scsiH2BE_U32(&aReply[20], 0x00ffffff); /* last possible start time for lead-out is not available */

                vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_T2I);
                vscsiReqSetXferSize(pVScsiReq, RT_MIN(sizeof(aReply), cbMax));

                RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, RT_MIN(sizeof(aReply), cbMax));
                rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                break;
            }
            case SCSI_READ_TRACK_INFORMATION:
            {
                size_t cbMax = scsiBE2H_U16(&pVScsiReq->pbCDB[7]);

                rcReq = vscsiLunMmcReadTrackInformation(pVScsiLunMmc, pVScsiReq, cbMax);
                break;
            }
            case SCSI_GET_CONFIGURATION:
            {
                size_t cbMax = scsiBE2H_U16(&pVScsiReq->pbCDB[7]);

                rcReq = vscsiLunMmcGetConfiguration(pVScsiLunMmc, pVScsiReq, cbMax);
                break;
            }
            case SCSI_READ_DVD_STRUCTURE:
            {
                size_t cbMax = scsiBE2H_U16(&pVScsiReq->pbCDB[8]);

                rcReq = vscsiLunMmcReadDvdStructure(pVScsiLunMmc, pVScsiReq, cbMax);
                break;
            }
            default:
                //AssertMsgFailed(("Command %#x [%s] not implemented\n", pVScsiReq->pbCDB[0], SCSICmdText(pVScsiReq->pbCDB[0])));
                rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_ILLEGAL_OPCODE, 0x00);
        }
    }

    if (enmTxDir != VSCSIIOREQTXDIR_INVALID)
    {
        LogFlow(("%s: uLbaStart=%llu cSectorTransfer=%u\n",
                 __FUNCTION__, uLbaStart, cSectorTransfer));

        vscsiReqSetXferDir(pVScsiReq, enmTxDir == VSCSIIOREQTXDIR_WRITE ? VSCSIXFERDIR_I2T : VSCSIXFERDIR_T2I);
        vscsiReqSetXferSize(pVScsiReq, cSectorTransfer * cbSector);
        if (RT_UNLIKELY(uLbaStart + cSectorTransfer > pVScsiLunMmc->cSectors))
        {
            vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_NONE);
            rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_LOGICAL_BLOCK_OOR, 0x00);
            vscsiDeviceReqComplete(pVScsiLun->pVScsiDevice, pVScsiReq, rcReq, false, VINF_SUCCESS);
        }
        else if (!cSectorTransfer)
        {
            /* A 0 transfer length is not an error. */
            vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_NONE);
            rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
            vscsiDeviceReqComplete(pVScsiLun->pVScsiDevice, pVScsiReq, rcReq, false, VINF_SUCCESS);
        }
        else
        {
            /* Check that the sector size is valid. */
            VDREGIONDATAFORM enmDataForm = VDREGIONDATAFORM_INVALID;
            rc = vscsiLunMediumQueryRegionPropertiesForLba(pVScsiLun, uLbaStart,
                                                           NULL, NULL, NULL, &enmDataForm);
            if (RT_FAILURE(rc))
            {
                rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_LOGICAL_BLOCK_OOR, 0x00);
                vscsiDeviceReqComplete(pVScsiLun->pVScsiDevice, pVScsiReq, rcReq, false, VINF_SUCCESS);
                rc = VINF_SUCCESS; /* The request was completed properly, so don't indicate an error here which might cause another completion. */
            }
            else if (   enmDataForm != VDREGIONDATAFORM_MODE1_2048
                     && enmDataForm != VDREGIONDATAFORM_MODE1_2352
                     && enmDataForm != VDREGIONDATAFORM_MODE2_2336
                     && enmDataForm != VDREGIONDATAFORM_MODE2_2352
                     && enmDataForm != VDREGIONDATAFORM_RAW
                     && cbSector == _2K)
            {
                rcReq = vscsiLunReqSenseErrorInfoSet(pVScsiLun, pVScsiReq,
                                                     SCSI_SENSE_ILLEGAL_REQUEST | SCSI_SENSE_FLAG_ILI,
                                                     SCSI_ASC_ILLEGAL_MODE_FOR_THIS_TRACK, 0, uLbaStart);
                vscsiDeviceReqComplete(pVScsiLun->pVScsiDevice, pVScsiReq, rcReq, false, VINF_SUCCESS);
            }
            else
            {
                /* Enqueue new I/O request */
                rc = vscsiIoReqTransferEnqueueEx(pVScsiLun, pVScsiReq, enmTxDir,
                                                 uLbaStart * cbSector,
                                                 paSegs, cSegs, cSectorTransfer * cbSector);
            }
        }
    }
    else /* Request completed */
        vscsiDeviceReqComplete(pVScsiLun->pVScsiDevice, pVScsiReq, rcReq, false, VINF_SUCCESS);

    return rc;
}

/** @interface_method_impl{VSCSILUNDESC,pfnVScsiLunReqFree} */
static DECLCALLBACK(void) vscsiLunMmcReqFree(PVSCSILUNINT pVScsiLun, PVSCSIREQINT pVScsiReq,
                                             void *pvLun)
{
    RT_NOREF2(pVScsiLun, pVScsiReq);
    RTMemFree(pvLun);
}

/** @interface_method_impl{VSCSILUNDESC,pfnVScsiLunMediumInserted} */
static DECLCALLBACK(int) vscsiLunMmcMediumInserted(PVSCSILUNINT pVScsiLun)
{
    int rc = VINF_SUCCESS;
    PVSCSILUNMMC pVScsiLunMmc = (PVSCSILUNMMC)pVScsiLun;

    pVScsiLunMmc->cSectors = 0;
    uint32_t cTracks = vscsiLunMediumGetRegionCount(pVScsiLun);
    for (uint32_t i = 0; i < cTracks && RT_SUCCESS(rc); i++)
    {
        uint64_t cBlocks = 0;
        rc = vscsiLunMediumQueryRegionProperties(pVScsiLun, i, NULL, &cBlocks,
                                                 NULL, NULL);
        if (RT_FAILURE(rc))
            break;
        pVScsiLunMmc->cSectors += cBlocks;
    }

    if (RT_SUCCESS(rc))
    {
        uint32_t OldStatus, NewStatus;
        do
        {
            OldStatus = ASMAtomicReadU32((volatile uint32_t *)&pVScsiLunMmc->MediaEventStatus);
            switch (OldStatus)
            {
                case MMCEVENTSTATUSTYPE_MEDIA_CHANGED:
                case MMCEVENTSTATUSTYPE_MEDIA_REMOVED:
                    /* no change, we will send "medium removed" + "medium inserted" */
                    NewStatus = MMCEVENTSTATUSTYPE_MEDIA_CHANGED;
                    break;
                default:
                    NewStatus = MMCEVENTSTATUSTYPE_MEDIA_NEW;
                    break;
            }
        } while (!ASMAtomicCmpXchgU32((volatile uint32_t *)&pVScsiLunMmc->MediaEventStatus,
                                      NewStatus, OldStatus));

        ASMAtomicXchgU32(&pVScsiLunMmc->u32MediaTrackType, MMC_MEDIA_TYPE_UNKNOWN);
    }

    return rc;
}

/** @interface_method_impl{VSCSILUNDESC,pfnVScsiLunMediumRemoved} */
static DECLCALLBACK(int) vscsiLunMmcMediumRemoved(PVSCSILUNINT pVScsiLun)
{
    PVSCSILUNMMC pVScsiLunMmc = (PVSCSILUNMMC)pVScsiLun;

    ASMAtomicWriteU32((volatile uint32_t *)&pVScsiLunMmc->MediaEventStatus, MMCEVENTSTATUSTYPE_MEDIA_REMOVED);
    ASMAtomicXchgU32(&pVScsiLunMmc->u32MediaTrackType, MMC_MEDIA_TYPE_NO_DISC);
    pVScsiLunMmc->cSectors = 0;
    return VINF_SUCCESS;
}


/**
 * The supported operation codes for the MMC LUN type.
 *
 * @note This gives the minimum size required by our implementation
 *       which may be smaller than what the spec defines (for example
 *       we do not access the control byte at the end).
 */
static uint8_t s_acbCdbOpc[] =
{
    1,                               /**< 0x00        TEST UNIT READY */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x01        Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X2,     /**< 0x02 - 0x03 Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X4,     /**< 0x04 - 0x07 Invalid */
    5,                               /**< 0x08        READ (6) */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x09        Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X2,     /**< 0x0a - 0x0b Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X4,     /**< 0x0c - 0x0f Invalid */

    VSCSI_LUN_CDB_SZ_INVALID_X2,     /**< 0x10 - 0x11 Invalid */
    5,                               /**< 0x12        INQUIRY */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x13        Invalid */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x14        Invalid */
    5,                               /**< 0x15        MODE SELECT (6) */
    VSCSI_LUN_CDB_SZ_INVALID_X2,     /**< 0x16 - 0x17 Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X2,     /**< 0x18 - 0x19 Invalid */
    5,                               /**< 0x1a        MODE SENSE (6) */
    5,                               /**< 0x1b        START STOP UNIT */
    VSCSI_LUN_CDB_SZ_INVALID_X2,     /**< 0x1c - 0x1d Invalid */
    5,                               /**< 0x1e        PREVENT ALLOW MEDIUM REMOVAL */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x1f        Invalid */

    VSCSI_LUN_CDB_SZ_INVALID_X4,     /**< 0x20 - 0x23 Invalid */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x24        Invalid */
    1,                               /**< 0x25        READ CAPACITY */
    VSCSI_LUN_CDB_SZ_INVALID_X2,     /**< 0x26 - 0x27 Invalid */
    9,                               /**< 0x28        READ (10) */
    VSCSI_LUN_CDB_SZ_INVALID_X2,     /**< 0x29 - 0x2a Invalid */
    6,                               /**< 0x2b        SEEK (10) */
    VSCSI_LUN_CDB_SZ_INVALID_X2,     /**< 0x2c - 0x2d Invalid */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x2e        Invalid */
    5,                               /**< 0x2f        VERIFY (10) */

    VSCSI_LUN_CDB_SZ_INVALID_X8,     /**< 0x30 - 0x37 Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X4,     /**< 0x38 - 0x3b Invalid */
    8,                               /**< 0x3c        READ BUFFER */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x3d        Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X2,     /**< 0x3e - 0x3f Invalid */

    VSCSI_LUN_CDB_SZ_INVALID_X2,     /**< 0x40 - 0x41 Invalid */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x42        Invalid */
    9,                               /**< 0x43        READ TOC PMA ATIP */
    VSCSI_LUN_CDB_SZ_INVALID_X2,     /**< 0x44 - 0x45 Invalid */
    9,                               /**< 0x46        GET CONFIGURATION */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x47        Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X2,     /**< 0x48 - 0x49 Invalid */
    9,                               /**< 0x4a        GET EVENT STATUS NOTIFICATION */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x4b        Invalid */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x4c        Invalid */
    9,                               /**< 0x4d        LOG SENSE */
    VSCSI_LUN_CDB_SZ_INVALID_X2,     /**< 0x4e - 0x4f Invalid */

    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x50        Invalid */
    9,                               /**< 0x51        READ DISC INFORMATION */
    9,                               /**< 0x52        READ TRACK INFORMATION */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x53        Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X4,     /**< 0x54 - 0x57 Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X2,     /**< 0x58 - 0x59 Invalid */
    9,                               /**< 0x5a        MODE SENSE (10) */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x5b        Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X4,     /**< 0x5c - 0x5f Invalid */

    VSCSI_LUN_CDB_SZ_INVALID_X16,    /**< 0x60 - 0x6f Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X16,    /**< 0x70 - 0x7f Invalid */

    VSCSI_LUN_CDB_SZ_INVALID_X8,     /**< 0x80 - 0x87 Invalid */
    14,                              /**< 0x88        READ (16) */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x89        Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X2,     /**< 0x8a - 0x8b Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X4,     /**< 0x8c - 0x8f Invalid */

    VSCSI_LUN_CDB_SZ_INVALID_X8,     /**< 0x90 - 0x97 Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X4,     /**< 0x98 - 0x9b Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X2,     /**< 0x9c - 0x9d Invalid */
    2,                               /**< 0x9e        SERVICE ACTION IN (16) (at least 2). */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x9f        Invalid */

    VSCSI_LUN_CDB_SZ_INVALID_X8,     /**< 0xa0 - 0xa7 Invalid */
    10,                              /**< 0xa8        READ (12) */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0xa9        Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X2,     /**< 0xaa - 0xab Invalid */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0xac        Invalid */
    10,                              /**< 0xad        READ DVD STRUCTURE */
    VSCSI_LUN_CDB_SZ_INVALID_X2,     /**< 0xae - 0xaf Invalid */

    VSCSI_LUN_CDB_SZ_INVALID_X8,     /**< 0xb0 - 0xb7 Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X4,     /**< 0xb8 - 0xbb Invalid */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0xbc        Invalid */
    10,                              /**< 0xbd        MECHANISM STATUS */
    10,                              /**< 0xbe        READ CD */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0xbf        Invalid */

    VSCSI_LUN_CDB_SZ_INVALID_X16,    /**< 0xc0 - 0xcf Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X16,    /**< 0xd0 - 0xdf Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X16,    /**< 0xe0 - 0xef Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X16     /**< 0xf0 - 0xff Invalid */
};
AssertCompileSize(s_acbCdbOpc, 256 * sizeof(uint8_t));


VSCSILUNDESC g_VScsiLunTypeMmc =
{
    /** enmLunType */
    VSCSILUNTYPE_MMC,
    /** pcszDescName */
    "MMC",
    /** cbLun */
    sizeof(VSCSILUNMMC),
    /** pacbCdbOpc */
    &s_acbCdbOpc[0],
    /** cSupOpcInfo */
    0,
    /** paSupOpcInfo */
    NULL,
    /** pfnVScsiLunInit */
    vscsiLunMmcInit,
    /** pfnVScsiLunDestroy */
    vscsiLunMmcDestroy,
    /** pfnVScsiLunReqProcess */
    vscsiLunMmcReqProcess,
    /** pfnVScsiLunReqFree */
    vscsiLunMmcReqFree,
    /** pfnVScsiLunMediumInserted */
    vscsiLunMmcMediumInserted,
    /** pfnVScsiLunMediumRemoved */
    vscsiLunMmcMediumRemoved
};
