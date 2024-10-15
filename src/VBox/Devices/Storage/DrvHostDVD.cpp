/* $Id: DrvHostDVD.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DrvHostDVD - Host DVD block driver.
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
#define LOG_GROUP LOG_GROUP_DRV_HOST_DVD
#include <iprt/asm.h>
#include <VBox/vmm/pdmdrv.h>
#include <VBox/vmm/pdmstorageifs.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/file.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/critsect.h>
#include <VBox/scsi.h>
#include <VBox/scsiinline.h>

#include "VBoxDD.h"
#include "DrvHostBase.h"
#include "ATAPIPassthrough.h"

/** ATAPI sense info size. */
#define ATAPI_SENSE_SIZE  64
/** Size of an ATAPI packet. */
#define ATAPI_PACKET_SIZE 12

/**
 * Host DVD driver instance data.
 */
typedef struct DRVHOSTDVD
{
    /** Base driver data. */
    DRVHOSTBASE             Core;
    /** The current tracklist of the loaded medium if passthrough is used. */
    PTRACKLIST              pTrackList;
    /** ATAPI sense data. */
    uint8_t                 abATAPISense[ATAPI_SENSE_SIZE];
    /** Flag whether to overwrite the inquiry data with our emulated settings. */
    bool                    fInquiryOverwrite;
} DRVHOSTDVD;
/** Pointer to the host DVD driver instance data. */
typedef DRVHOSTDVD *PDRVHOSTDVD;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


static uint8_t drvHostDvdCmdOK(PDRVHOSTDVD pThis)
{
    memset(pThis->abATAPISense, '\0', sizeof(pThis->abATAPISense));
    pThis->abATAPISense[0] = 0x70;
    pThis->abATAPISense[7] = 10;
    return SCSI_STATUS_OK;
}

static uint8_t drvHostDvdCmdError(PDRVHOSTDVD pThis, const uint8_t *pabATAPISense, size_t cbATAPISense)
{
    Log(("%s: sense=%#x (%s) asc=%#x ascq=%#x (%s)\n", __FUNCTION__, pabATAPISense[2] & 0x0f, SCSISenseText(pabATAPISense[2] & 0x0f),
             pabATAPISense[12], pabATAPISense[13], SCSISenseExtText(pabATAPISense[12], pabATAPISense[13])));
    memset(pThis->abATAPISense, '\0', sizeof(pThis->abATAPISense));
    memcpy(pThis->abATAPISense, pabATAPISense, RT_MIN(cbATAPISense, sizeof(pThis->abATAPISense)));
    return SCSI_STATUS_CHECK_CONDITION;
}

/** @todo deprecated function - doesn't provide enough info. Replace by direct
 * calls to drvHostDvdCmdError()  with full data. */
static uint8_t drvHostDvdCmdErrorSimple(PDRVHOSTDVD pThis, uint8_t uATAPISenseKey, uint8_t uATAPIASC)
{
    uint8_t abATAPISense[ATAPI_SENSE_SIZE];
    memset(abATAPISense, '\0', sizeof(abATAPISense));
    abATAPISense[0] = 0x70 | (1 << 7);
    abATAPISense[2] = uATAPISenseKey & 0x0f;
    abATAPISense[7] = 10;
    abATAPISense[12] = uATAPIASC;
    return drvHostDvdCmdError(pThis, abATAPISense, sizeof(abATAPISense));
}


/**
 * Parse the CDB and check whether it can be passed through safely.
 *
 * @returns Flag whether to passthrough to the device is considered safe.
 * @param   pThis         The host DVD driver instance.
 * @param   pReq          The request.
 * @param   pbCdb         The CDB to parse.
 * @param   cbCdb         Size of the CDB in bytes.
 * @param   cbBuf         Size of the guest buffer.
 * @param   penmTxDir     Where to store the transfer direction (guest to host or vice versa).
 * @param   pcbXfer       Where to store the transfer size encoded in the CDB.
 * @param   pcbSector     Where to store the sector size used for the transfer.
 * @param   pu8ScsiSts    Where to store the SCSI status code.
 */
static bool drvHostDvdParseCdb(PDRVHOSTDVD pThis, PDRVHOSTBASEREQ pReq,
                               const uint8_t *pbCdb, size_t cbCdb, size_t cbBuf,
                               PDMMEDIATXDIR *penmTxDir, size_t *pcbXfer,
                               size_t *pcbSector, uint8_t *pu8ScsiSts)
{
    bool fPassthrough = false;

    if (   pbCdb[0] == SCSI_REQUEST_SENSE
        && (pThis->abATAPISense[2] & 0x0f) != SCSI_SENSE_NONE)
    {
        /* Handle the command here and copy sense data over. */
        void *pvBuf = NULL;
        int rc = drvHostBaseBufferRetain(&pThis->Core, pReq, cbBuf, false /*fWrite*/, &pvBuf);
        if (RT_SUCCESS(rc))
        {
            memcpy(pvBuf, &pThis->abATAPISense[0], RT_MIN(sizeof(pThis->abATAPISense), cbBuf));
            rc = drvHostBaseBufferRelease(&pThis->Core, pReq, cbBuf, false /* fWrite */, pvBuf);
            AssertRC(rc);
            drvHostDvdCmdOK(pThis);
            *pu8ScsiSts = SCSI_STATUS_OK;
        }
    }
    else
        fPassthrough = ATAPIPassthroughParseCdb(pbCdb, cbCdb, cbBuf, pThis->pTrackList,
                                                &pThis->abATAPISense[0], sizeof(pThis->abATAPISense),
                                                penmTxDir, pcbXfer, pcbSector, pu8ScsiSts);

    return fPassthrough;
}


/**
 * Locks or unlocks the drive.
 *
 * @returns VBox status code.
 * @param   pThis       The instance data.
 * @param   fLock       True if the request is to lock the drive, false if to unlock.
 */
static DECLCALLBACK(int) drvHostDvdDoLock(PDRVHOSTBASE pThis, bool fLock)
{
    int rc = drvHostBaseDoLockOs(pThis, fLock);

    LogFlow(("drvHostDvdDoLock(, fLock=%RTbool): returns %Rrc\n", fLock, rc));
    return rc;
}


/** @interface_method_impl{PDMIMEDIA,pfnSendCmd} */
static DECLCALLBACK(int) drvHostDvdSendCmd(PPDMIMEDIA pInterface, const uint8_t *pbCdb, size_t cbCdb,
                                           PDMMEDIATXDIR enmTxDir, void *pvBuf, uint32_t *pcbBuf,
                                           uint8_t *pabSense, size_t cbSense, uint32_t cTimeoutMillies)
{
    PDRVHOSTBASE pThis = RT_FROM_MEMBER(pInterface, DRVHOSTBASE, IMedia);
    int rc;
    LogFlow(("%s: cmd[0]=%#04x txdir=%d pcbBuf=%d timeout=%d\n", __FUNCTION__, pbCdb[0], enmTxDir, *pcbBuf, cTimeoutMillies));

    RTCritSectEnter(&pThis->CritSect);
    /* Pass the request on to the internal scsi command interface. */
    if (enmTxDir == PDMMEDIATXDIR_FROM_DEVICE)
        memset(pvBuf, '\0', *pcbBuf); /* we got read size, but zero it anyway. */
    rc = drvHostBaseScsiCmdOs(pThis, pbCdb, cbCdb, enmTxDir, pvBuf, pcbBuf, pabSense, cbSense, cTimeoutMillies);
    if (rc == VERR_UNRESOLVED_ERROR)
        /* sense information set */
        rc = VERR_DEV_IO_ERROR;

    if (pbCdb[0] == SCSI_GET_EVENT_STATUS_NOTIFICATION)
    {
        uint8_t *pbBuf = (uint8_t*)pvBuf;
        Log2(("Event Status Notification class=%#02x supported classes=%#02x\n", pbBuf[2], pbBuf[3]));
        if (RT_BE2H_U16(*(uint16_t*)pbBuf) >= 6)
            Log2(("  event %#02x %#02x %#02x %#02x\n", pbBuf[4], pbBuf[5], pbBuf[6], pbBuf[7]));
    }
    RTCritSectLeave(&pThis->CritSect);

    LogFlow(("%s: rc=%Rrc\n", __FUNCTION__, rc));
    return rc;
}


/** @interface_method_impl{PDMIMEDIAEX,pfnIoReqSendScsiCmd} */
static DECLCALLBACK(int) drvHostDvdIoReqSendScsiCmd(PPDMIMEDIAEX pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                    uint32_t uLun, const uint8_t *pbCdb, size_t cbCdb,
                                                    PDMMEDIAEXIOREQSCSITXDIR enmTxDir, PDMMEDIAEXIOREQSCSITXDIR *penmTxDirRet,
                                                    size_t cbBuf, uint8_t *pabSense, size_t cbSense, size_t *pcbSenseRet,
                                                    uint8_t *pu8ScsiSts, uint32_t cTimeoutMillies)
{
    RT_NOREF3(uLun, cTimeoutMillies, enmTxDir);

    PDRVHOSTDVD pThis = RT_FROM_MEMBER(pInterface, DRVHOSTDVD, Core.IMediaEx);
    PDRVHOSTBASEREQ pReq = (PDRVHOSTBASEREQ)hIoReq;
    int rc = VINF_SUCCESS;

    LogFlow(("%s: pbCdb[0]=%#04x{%s} enmTxDir=%d cbBuf=%zu timeout=%u\n",
             __FUNCTION__, pbCdb[0], SCSICmdText(pbCdb[0]), enmTxDir, cbBuf, cTimeoutMillies));

    RTCritSectEnter(&pThis->Core.CritSect);

    /*
     * Parse the command first to fend off any illegal or dangerous commands we don't want the guest
     * to execute on the host drive.
     */
    PDMMEDIATXDIR enmXferDir = PDMMEDIATXDIR_NONE;
    size_t cbXfer = 0;
    size_t cbSector = 0;
    size_t cbScsiCmdBufLimit = drvHostBaseScsiCmdGetBufLimitOs(&pThis->Core);
    bool fPassthrough = drvHostDvdParseCdb(pThis, pReq, pbCdb, cbCdb, cbBuf,
                                           &enmXferDir, &cbXfer, &cbSector, pu8ScsiSts);
    if (fPassthrough)
    {
        void *pvBuf = NULL;
        size_t cbXferCur = cbXfer;

        pReq->cbReq = cbXfer;
        pReq->cbResidual = cbXfer;

        if (cbXfer)
            rc = drvHostBaseBufferRetain(&pThis->Core, pReq, cbXfer, enmXferDir == PDMMEDIATXDIR_TO_DEVICE, &pvBuf);

        if (cbXfer > cbScsiCmdBufLimit)
        {
            /* Linux accepts commands with up to 100KB of data, but expects
             * us to handle commands with up to 128KB of data. The usual
             * imbalance of powers. */
            uint8_t aATAPICmd[ATAPI_PACKET_SIZE];
            uint32_t iATAPILBA, cSectors;
            uint8_t *pbBuf = (uint8_t *)pvBuf;

            switch (pbCdb[0])
            {
                case SCSI_READ_10:
                case SCSI_WRITE_10:
                case SCSI_WRITE_AND_VERIFY_10:
                    iATAPILBA = scsiBE2H_U32(pbCdb + 2);
                    cSectors = scsiBE2H_U16(pbCdb + 7);
                    break;
                case SCSI_READ_12:
                case SCSI_WRITE_12:
                    iATAPILBA = scsiBE2H_U32(pbCdb + 2);
                    cSectors = scsiBE2H_U32(pbCdb + 6);
                    break;
                case SCSI_READ_CD:
                    iATAPILBA = scsiBE2H_U32(pbCdb + 2);
                    cSectors = scsiBE2H_U24(pbCdb + 6);
                    break;
                case SCSI_READ_CD_MSF:
                    iATAPILBA = scsiMSF2LBA(pbCdb + 3);
                    cSectors = scsiMSF2LBA(pbCdb + 6) - iATAPILBA;
                    break;
                default:
                    AssertMsgFailed(("Don't know how to split command %#04x\n", pbCdb[0]));
                    LogRelMax(10, ("HostDVD#%u: CD-ROM passthrough split error\n", pThis->Core.pDrvIns->iInstance));
                    *pu8ScsiSts = drvHostDvdCmdErrorSimple(pThis, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_ILLEGAL_OPCODE);
                    rc = drvHostBaseBufferRelease(&pThis->Core, pReq, cbBuf, enmXferDir == PDMMEDIATXDIR_TO_DEVICE, pvBuf);
                    RTCritSectLeave(&pThis->Core.CritSect);
                    return VINF_SUCCESS;
            }
            memcpy(aATAPICmd, pbCdb, RT_MIN(cbCdb, ATAPI_PACKET_SIZE));
            uint32_t cReqSectors = 0;
            for (uint32_t i = cSectors; i > 0; i -= cReqSectors)
            {
                if (i * cbSector > cbScsiCmdBufLimit)
                    cReqSectors = (uint32_t)(cbScsiCmdBufLimit / cbSector);
                else
                    cReqSectors = i;
                uint32_t cbCurrTX = (uint32_t)cbSector * cReqSectors;
                switch (pbCdb[0])
                {
                    case SCSI_READ_10:
                    case SCSI_WRITE_10:
                    case SCSI_WRITE_AND_VERIFY_10:
                        scsiH2BE_U32(aATAPICmd + 2, iATAPILBA);
                        scsiH2BE_U16(aATAPICmd + 7, cReqSectors);
                        break;
                    case SCSI_READ_12:
                    case SCSI_WRITE_12:
                        scsiH2BE_U32(aATAPICmd + 2, iATAPILBA);
                        scsiH2BE_U32(aATAPICmd + 6, cReqSectors);
                        break;
                    case SCSI_READ_CD:
                        scsiH2BE_U32(aATAPICmd + 2, iATAPILBA);
                        scsiH2BE_U24(aATAPICmd + 6, cReqSectors);
                        break;
                    case SCSI_READ_CD_MSF:
                        scsiLBA2MSF(aATAPICmd + 3, iATAPILBA);
                        scsiLBA2MSF(aATAPICmd + 6, iATAPILBA + cReqSectors);
                        break;
                }
                rc = drvHostBaseScsiCmdOs(&pThis->Core, aATAPICmd, sizeof(aATAPICmd),
                                          enmXferDir, pbBuf, &cbCurrTX,
                                          &pThis->abATAPISense[0], sizeof(pThis->abATAPISense),
                                          cTimeoutMillies /**< @todo timeout */);
                if (rc != VINF_SUCCESS)
                    break;

                pReq->cbResidual -= cbCurrTX;
                iATAPILBA += cReqSectors;
                pbBuf += cbSector * cReqSectors;
            }
        }
        else
        {
            uint32_t cbXferTmp = (uint32_t)cbXferCur;
            rc = drvHostBaseScsiCmdOs(&pThis->Core, pbCdb, cbCdb, enmXferDir, pvBuf, &cbXferTmp,
                                      &pThis->abATAPISense[0], sizeof(pThis->abATAPISense), cTimeoutMillies);
            if (RT_SUCCESS(rc))
                pReq->cbResidual -= cbXferTmp;
        }

        if (RT_SUCCESS(rc))
        {
            /* Do post processing for certain commands. */
            switch (pbCdb[0])
            {
                case SCSI_SEND_CUE_SHEET:
                case SCSI_READ_TOC_PMA_ATIP:
                {
                    if (!pThis->pTrackList)
                        rc = ATAPIPassthroughTrackListCreateEmpty(&pThis->pTrackList);

                    if (RT_SUCCESS(rc))
                        rc = ATAPIPassthroughTrackListUpdate(pThis->pTrackList, pbCdb, pvBuf, cbXfer);

                    if (RT_FAILURE(rc))
                        LogRelMax(10, ("HostDVD#%u: Error (%Rrc) while updating the tracklist during %s, burning the disc might fail\n",
                                  pThis->Core.pDrvIns->iInstance, rc,
                                  pbCdb[0] == SCSI_SEND_CUE_SHEET ? "SEND CUE SHEET" : "READ TOC/PMA/ATIP"));
                    break;
                }
                case SCSI_SYNCHRONIZE_CACHE:
                {
                    if (pThis->pTrackList)
                        ATAPIPassthroughTrackListClear(pThis->pTrackList);
                    break;
                }
            }

            if (enmXferDir == PDMMEDIATXDIR_FROM_DEVICE)
            {
               Assert(cbXferCur <= cbXfer);

                if (   pbCdb[0] == SCSI_INQUIRY
                    && pThis->fInquiryOverwrite)
                {
                    const char *pszInqVendorId  = "VBOX";
                    const char *pszInqProductId = "CD-ROM";
                    const char *pszInqRevision  = "1.0";

                    if (pThis->Core.pDrvMediaPort->pfnQueryScsiInqStrings)
                    {
                        rc = pThis->Core.pDrvMediaPort->pfnQueryScsiInqStrings(pThis->Core.pDrvMediaPort, &pszInqVendorId,
                                                                               &pszInqProductId, &pszInqRevision);
                        AssertRC(rc);
                    }
                    /* Make sure that the real drive cannot be identified.
                     * Motivation: changing the VM configuration should be as
                     *             invisible as possible to the guest. */
                    if (cbXferCur >= 8 + 8)
                        scsiPadStr((uint8_t *)pvBuf + 8, pszInqVendorId, 8);
                    if (cbXferCur >= 16 + 16)
                        scsiPadStr((uint8_t *)pvBuf + 16, pszInqProductId, 16);
                    if (cbXferCur >= 32 + 4)
                        scsiPadStr((uint8_t *)pvBuf + 32, pszInqRevision, 4);
                }

                if (cbXferCur)
                    Log3(("ATAPI PT data read (%d): %.*Rhxs\n", cbXferCur, cbXferCur, (uint8_t *)pvBuf));
            }

            *pu8ScsiSts = drvHostDvdCmdOK(pThis);
        }
        else
        {
            do
            {
                /* don't log superfluous errors */
                if (    rc == VERR_DEV_IO_ERROR
                    && (   pbCdb[0] == SCSI_TEST_UNIT_READY
                        || pbCdb[0] == SCSI_READ_CAPACITY
                        || pbCdb[0] == SCSI_READ_DVD_STRUCTURE
                        || pbCdb[0] == SCSI_READ_TOC_PMA_ATIP))
                    break;
                LogRelMax(10, ("HostDVD#%u: CD-ROM passthrough cmd=%#04x sense=%d ASC=%#02x ASCQ=%#02x %Rrc\n",
                          pThis->Core.pDrvIns->iInstance, pbCdb[0], pThis->abATAPISense[2] & 0x0f,
                          pThis->abATAPISense[12], pThis->abATAPISense[13], rc));
            } while (0);
            *pu8ScsiSts = SCSI_STATUS_CHECK_CONDITION;
            rc = VINF_SUCCESS;
        }

        if (cbXfer)
            rc = drvHostBaseBufferRelease(&pThis->Core, pReq, cbXfer, enmXferDir == PDMMEDIATXDIR_TO_DEVICE, pvBuf);
    }

    /*
     * We handled the command, check the status code and copy over the sense data if
     * it is CHECK CONDITION.
     */
    if (   *pu8ScsiSts == SCSI_STATUS_CHECK_CONDITION
        && RT_VALID_PTR(pabSense)
        && cbSense > 0)
    {
        size_t cbSenseCpy = RT_MIN(cbSense, sizeof(pThis->abATAPISense));

        memcpy(pabSense, &pThis->abATAPISense[0], cbSenseCpy);
        if (pcbSenseRet)
            *pcbSenseRet = cbSenseCpy;
    }

    if (penmTxDirRet)
    {
        switch (enmXferDir)
        {
            case PDMMEDIATXDIR_NONE:
                *penmTxDirRet = PDMMEDIAEXIOREQSCSITXDIR_NONE;
                break;
            case PDMMEDIATXDIR_FROM_DEVICE:
                *penmTxDirRet = PDMMEDIAEXIOREQSCSITXDIR_FROM_DEVICE;
                break;
            case PDMMEDIATXDIR_TO_DEVICE:
                *penmTxDirRet = PDMMEDIAEXIOREQSCSITXDIR_TO_DEVICE;
                break;
            default:
                *penmTxDirRet = PDMMEDIAEXIOREQSCSITXDIR_UNKNOWN;
        }
    }

    RTCritSectLeave(&pThis->Core.CritSect);

    LogFlow(("%s: rc=%Rrc\n", __FUNCTION__, rc));
    return rc;
}


/* -=-=-=-=- driver interface -=-=-=-=- */


/** @interface_method_impl{PDMDRVREG,pfnDestruct} */
static DECLCALLBACK(void) drvHostDvdDestruct(PPDMDRVINS pDrvIns)
{
    PDRVHOSTDVD pThis = PDMINS_2_DATA(pDrvIns, PDRVHOSTDVD);

    if (pThis->pTrackList)
    {
        ATAPIPassthroughTrackListDestroy(pThis->pTrackList);
        pThis->pTrackList = NULL;
    }

    DRVHostBaseDestruct(pDrvIns);
}

/**
 * Construct a host dvd drive driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvHostDvdConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF(fFlags);
    PDRVHOSTDVD     pThis = PDMINS_2_DATA(pDrvIns, PDRVHOSTDVD);
    PCPDMDRVHLPR3   pHlp  = pDrvIns->pHlpR3;

    LogFlow(("drvHostDvdConstruct: iInstance=%d\n", pDrvIns->iInstance));

    int rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "InquiryOverwrite", &pThis->fInquiryOverwrite, true);
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("HostDVD configuration error: failed to read \"InquiryOverwrite\" as boolean"));

    bool fPassthrough;
    rc = pHlp->pfnCFGMQueryBool(pCfg, "Passthrough", &fPassthrough);
    if (RT_SUCCESS(rc) && fPassthrough)
    {
        pThis->Core.IMedia.pfnSendCmd            = drvHostDvdSendCmd;
        pThis->Core.IMediaEx.pfnIoReqSendScsiCmd = drvHostDvdIoReqSendScsiCmd;
        /* Passthrough requires opening the device in R/W mode. */
        pThis->Core.fReadOnlyConfig = false;
    }

    pThis->Core.pfnDoLock = drvHostDvdDoLock;

    /*
     * Init instance data.
     */
    rc = DRVHostBaseInit(pDrvIns, pCfg, "Path\0Interval\0Locked\0BIOSVisible\0AttachFailError\0Passthrough\0InquiryOverwrite\0",
                         PDMMEDIATYPE_DVD);
    LogFlow(("drvHostDvdConstruct: returns %Rrc\n", rc));
    return rc;
}

/**
 * Reset a host dvd drive driver instance.
 *
 * @copydoc FNPDMDRVRESET
 */
static DECLCALLBACK(void) drvHostDvdReset(PPDMDRVINS pDrvIns)
{
    PDRVHOSTDVD pThis = PDMINS_2_DATA(pDrvIns, PDRVHOSTDVD);

    if (pThis->pTrackList)
    {
        ATAPIPassthroughTrackListDestroy(pThis->pTrackList);
        pThis->pTrackList = NULL;
    }

    int rc = drvHostBaseDoLockOs(&pThis->Core, false);
    RT_NOREF(rc);
}


/**
 * Block driver registration record.
 */
const PDMDRVREG g_DrvHostDVD =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "HostDVD",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Host DVD Block Driver.",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_BLOCK,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVHOSTDVD),
    /* pfnConstruct */
    drvHostDvdConstruct,
    /* pfnDestruct */
    drvHostDvdDestruct,
    /* pfnRelocate */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    drvHostDvdReset,
    /* pfnSuspend */
    NULL,
    /* pfnResume */
    NULL,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    NULL,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};

