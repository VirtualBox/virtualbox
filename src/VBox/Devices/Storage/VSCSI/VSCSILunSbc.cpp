/* $Id: VSCSILunSbc.cpp 110132 2025-07-07 11:33:52Z michal.necasek@oracle.com $ */
/** @file
 * Virtual SCSI driver: SBC LUN implementation (hard disks)
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
#include <iprt/errcore.h>
#include <VBox/types.h>
#include <VBox/vscsi.h>
#include <iprt/cdefs.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/string.h>

#include "VSCSIInternal.h"

/** Maximum of amount of LBAs to unmap with one command. */
#define VSCSI_UNMAP_LBAS_MAX(a_cbSector) ((10*_1M) / a_cbSector)

/**
 * SBC LUN instance
 */
typedef struct VSCSILUNSBC
{
    /** Core LUN structure */
    VSCSILUNINT    Core;
    /** Sector size of the medium. */
    uint64_t       cbSector;
    /** Size of the virtual disk. */
    uint64_t       cSectors;
    /** VPD page pool. */
    VSCSIVPDPOOL   VpdPagePool;
} VSCSILUNSBC;
/** Pointer to a SBC LUN instance */
typedef VSCSILUNSBC *PVSCSILUNSBC;

static DECLCALLBACK(int) vscsiLunSbcInit(PVSCSILUNINT pVScsiLun)
{
    PVSCSILUNSBC pVScsiLunSbc = (PVSCSILUNSBC)pVScsiLun;
    int rc = VINF_SUCCESS;
    int cVpdPages = 0;

    uint32_t cRegions = vscsiLunMediumGetRegionCount(pVScsiLun);
    if (cRegions != 1)
        rc = VERR_INVALID_PARAMETER;

    if (RT_SUCCESS(rc))
        rc = vscsiLunMediumQueryRegionProperties(pVScsiLun, 0, NULL, &pVScsiLunSbc->cSectors,
                                                 &pVScsiLunSbc->cbSector, NULL);
    if (RT_SUCCESS(rc))
        rc = vscsiVpdPagePoolInit(&pVScsiLunSbc->VpdPagePool);

    /* Create device identification page - mandatory. */
    if (RT_SUCCESS(rc))
    {
        PVSCSIVPDPAGEDEVID pDevIdPage;

        rc = vscsiVpdPagePoolAllocNewPage(&pVScsiLunSbc->VpdPagePool, VSCSI_VPD_DEVID_NUMBER,
                                          VSCSI_VPD_DEVID_SIZE, (uint8_t **)&pDevIdPage);
        if (RT_SUCCESS(rc))
        {
            /** @todo Not conforming to the SPC spec but Solaris needs at least a stub to work. */
            pDevIdPage->u5PeripheralDeviceType = SCSI_INQUIRY_DATA_PERIPHERAL_DEVICE_TYPE_DIRECT_ACCESS;
            pDevIdPage->u3PeripheralQualifier  = SCSI_INQUIRY_DATA_PERIPHERAL_QUALIFIER_CONNECTED;
            pDevIdPage->u16PageLength          = RT_H2BE_U16(0x0);
            cVpdPages++;
        }
    }

    if (   RT_SUCCESS(rc)
        && (pVScsiLun->fFeatures & VSCSI_LUN_FEATURE_UNMAP))
    {
        PVSCSIVPDPAGEBLOCKLIMITS pBlkPage;
        PVSCSIVPDPAGEBLOCKPROV   pBlkProvPage;

        /* Create the page and fill it. */
        rc = vscsiVpdPagePoolAllocNewPage(&pVScsiLunSbc->VpdPagePool, VSCSI_VPD_BLOCK_LIMITS_NUMBER,
                                          VSCSI_VPD_BLOCK_LIMITS_SIZE, (uint8_t **)&pBlkPage);
        if (RT_SUCCESS(rc))
        {
                pBlkPage->u5PeripheralDeviceType       = SCSI_INQUIRY_DATA_PERIPHERAL_DEVICE_TYPE_DIRECT_ACCESS;
                pBlkPage->u3PeripheralQualifier        = SCSI_INQUIRY_DATA_PERIPHERAL_QUALIFIER_CONNECTED;
                pBlkPage->u16PageLength                = RT_H2BE_U16(0x3c);
                pBlkPage->u8MaxCmpWriteLength          = 0;
                pBlkPage->u16OptTrfLengthGran          = 0;
                pBlkPage->u32MaxTrfLength              = 0;
                pBlkPage->u32OptTrfLength              = 0;
                pBlkPage->u32MaxPreXdTrfLength         = 0;
                pBlkPage->u32MaxUnmapLbaCount          = RT_H2BE_U32(VSCSI_UNMAP_LBAS_MAX(pVScsiLunSbc->cbSector));
                pBlkPage->u32MaxUnmapBlkDescCount      = UINT32_C(0xffffffff);
                pBlkPage->u32OptUnmapGranularity       = 0;
                pBlkPage->u32UnmapGranularityAlignment = 0;
                cVpdPages++;
        }

        if (RT_SUCCESS(rc))
        {
            rc = vscsiVpdPagePoolAllocNewPage(&pVScsiLunSbc->VpdPagePool, VSCSI_VPD_BLOCK_PROV_NUMBER,
                                              VSCSI_VPD_BLOCK_PROV_SIZE, (uint8_t **)&pBlkProvPage);
            if (RT_SUCCESS(rc))
            {
                pBlkProvPage->u5PeripheralDeviceType = SCSI_INQUIRY_DATA_PERIPHERAL_DEVICE_TYPE_DIRECT_ACCESS;
                pBlkProvPage->u3PeripheralQualifier  = SCSI_INQUIRY_DATA_PERIPHERAL_QUALIFIER_CONNECTED;
                pBlkProvPage->u16PageLength          = RT_H2BE_U16(0x4);
                pBlkProvPage->u8ThresholdExponent    = 1;
                pBlkProvPage->fLBPU                  = true;
                cVpdPages++;
            }
        }
    }

    if (   RT_SUCCESS(rc)
        && (pVScsiLun->fFeatures & VSCSI_LUN_FEATURE_NON_ROTATIONAL))
    {
        PVSCSIVPDPAGEBLOCKCHARACTERISTICS pBlkPage;

        /* Create the page and fill it. */
        rc = vscsiVpdPagePoolAllocNewPage(&pVScsiLunSbc->VpdPagePool, VSCSI_VPD_BLOCK_CHARACTERISTICS_NUMBER,
                                          VSCSI_VPD_BLOCK_CHARACTERISTICS_SIZE, (uint8_t **)&pBlkPage);
        if (RT_SUCCESS(rc))
        {
                pBlkPage->u5PeripheralDeviceType       = SCSI_INQUIRY_DATA_PERIPHERAL_DEVICE_TYPE_DIRECT_ACCESS;
                pBlkPage->u3PeripheralQualifier        = SCSI_INQUIRY_DATA_PERIPHERAL_QUALIFIER_CONNECTED;
                pBlkPage->u16PageLength                = RT_H2BE_U16(0x3c);
                pBlkPage->u16MediumRotationRate        = RT_H2BE_U16(VSCSI_VPD_BLOCK_CHARACT_MEDIUM_ROTATION_RATE_NON_ROTATING);
                cVpdPages++;
        }
    }

    if (   RT_SUCCESS(rc)
        && cVpdPages)
    {
        PVSCSIVPDPAGESUPPORTEDPAGES pVpdPages;

        rc = vscsiVpdPagePoolAllocNewPage(&pVScsiLunSbc->VpdPagePool, VSCSI_VPD_SUPPORTED_PAGES_NUMBER,
                                          VSCSI_VPD_SUPPORTED_PAGES_SIZE + cVpdPages, (uint8_t **)&pVpdPages);
        if (RT_SUCCESS(rc))
        {
            unsigned idxVpdPage = 0;
            pVpdPages->u5PeripheralDeviceType = SCSI_INQUIRY_DATA_PERIPHERAL_DEVICE_TYPE_DIRECT_ACCESS;
            pVpdPages->u3PeripheralQualifier  = SCSI_INQUIRY_DATA_PERIPHERAL_QUALIFIER_CONNECTED;
            pVpdPages->u16PageLength          = RT_H2BE_U16(cVpdPages);

            pVpdPages->abVpdPages[idxVpdPage++] = VSCSI_VPD_DEVID_NUMBER;

            if (pVScsiLun->fFeatures & VSCSI_LUN_FEATURE_UNMAP)
            {
                pVpdPages->abVpdPages[idxVpdPage++] = VSCSI_VPD_BLOCK_LIMITS_NUMBER;
                pVpdPages->abVpdPages[idxVpdPage++] = VSCSI_VPD_BLOCK_PROV_NUMBER;
            }

            if (pVScsiLun->fFeatures & VSCSI_LUN_FEATURE_NON_ROTATIONAL)
                pVpdPages->abVpdPages[idxVpdPage++] = VSCSI_VPD_BLOCK_CHARACTERISTICS_NUMBER;
        }
    }

    /* For SBC LUNs, there will be no ready state transitions. */
    pVScsiLunSbc->Core.fReady = true;

    return rc;
}

static DECLCALLBACK(int) vscsiLunSbcDestroy(PVSCSILUNINT pVScsiLun)
{
    PVSCSILUNSBC pVScsiLunSbc = (PVSCSILUNSBC)pVScsiLun;

    vscsiVpdPagePoolDestroy(&pVScsiLunSbc->VpdPagePool);

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) vscsiLunSbcReqProcess(PVSCSILUNINT pVScsiLun, PVSCSIREQINT pVScsiReq)
{
    PVSCSILUNSBC pVScsiLunSbc = (PVSCSILUNSBC)pVScsiLun;
    int rc = VINF_SUCCESS;
    int rcReq = SCSI_STATUS_OK;
    uint64_t uLbaStart = 0;
    uint32_t cSectorTransfer = 0;
    VSCSIIOREQTXDIR enmTxDir = VSCSIIOREQTXDIR_INVALID;

    switch(pVScsiReq->pbCDB[0])
    {
        case SCSI_INQUIRY:
        {
            /* Check for EVPD bit. */
            if (pVScsiReq->pbCDB[1] & 0x1)
            {
                rc = vscsiVpdPagePoolQueryPage(&pVScsiLunSbc->VpdPagePool, pVScsiReq, pVScsiReq->pbCDB[2]);
                if (RT_UNLIKELY(rc == VERR_NOT_FOUND))
                {
                    rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST,
                                                     SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
                    rc = VINF_SUCCESS;
                }
                else
                    rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
            }
            else if (pVScsiReq->pbCDB[2] != 0) /* A non zero page code is an error. */
                rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST,
                                                 SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
            else
            {
                if (pVScsiReq->cbCDB >= 5)
                {
                    SCSIINQUIRYDATA ScsiInquiryReply;

                    memset(&ScsiInquiryReply, 0, sizeof(ScsiInquiryReply));

                    ScsiInquiryReply.cbAdditional           = 31;
                    ScsiInquiryReply.u5PeripheralDeviceType = SCSI_INQUIRY_DATA_PERIPHERAL_DEVICE_TYPE_DIRECT_ACCESS;
                    ScsiInquiryReply.u3PeripheralQualifier  = SCSI_INQUIRY_DATA_PERIPHERAL_QUALIFIER_CONNECTED;
                    ScsiInquiryReply.u3AnsiVersion          = 0x05; /* SPC-4 compliant */
                    ScsiInquiryReply.fCmdQue                = 1;    /* Command queuing supported. */
                    ScsiInquiryReply.fWBus16                = 1;

                    const char *pszVendorId = "VBOX";
                    const char *pszProductId = "HARDDISK";
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
                }
                else
                    rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INVALID_MESSAGE, 0x00);
            }
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
            if (pVScsiLunSbc->cSectors > UINT32_C(0xffffffff))
                scsiH2BE_U32(aReply, UINT32_C(0xffffffff));
            else
                scsiH2BE_U32(aReply, pVScsiLunSbc->cSectors - 1);
            scsiH2BE_U32(&aReply[4], (uint32_t)pVScsiLunSbc->cbSector);

            vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_T2I);
            vscsiReqSetXferSize(pVScsiReq, sizeof(aReply));

            RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, sizeof(aReply));
            rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
            break;
        }
        case SCSI_MODE_SENSE_6:
        {
            uint8_t uModePage = pVScsiReq->pbCDB[2] & 0x3f;
            uint8_t aReply[16];
            uint8_t *pu8ReplyPos;
            bool    fValid = false;

            memset(aReply, 0, sizeof(aReply));
            aReply[0] = 4 - 1;      /* Reply length 4 (sans length byte). */
            aReply[1] = 0;          /* Default media type. */
            aReply[2] = RT_BIT(4);  /* Caching supported. */
            aReply[3] = 0;          /* Block descriptor length. */

            if (pVScsiLun->fFeatures & VSCSI_LUN_FEATURE_READONLY)
                aReply[2] |= RT_BIT(7); /* Set write protect bit */

            pu8ReplyPos = aReply + 4;

            if ((uModePage == 0x08) || (uModePage == 0x3f))
            {
                *pu8ReplyPos++ = 0x08;  /* Caching Page code. */
                *pu8ReplyPos++ = 10;    /* Size of the page minus header, SCSI-2 format. */
                *pu8ReplyPos++ = 0x4;   /* Write cache enabled. */
                aReply[0] += 12;        /* Add page size to total reply length. */
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
        case SCSI_MODE_SELECT_6:
        {
            uint8_t abParms[12];
            size_t  cbCopied;
            size_t  cbList = pVScsiReq->pbCDB[4];

            vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_I2T);
            vscsiReqSetXferSize(pVScsiReq, pVScsiReq->pbCDB[4]);

            /* Copy the parameters. */
            cbCopied = RTSgBufCopyToBuf(&pVScsiReq->SgBuf, &abParms[0], sizeof(abParms));

            /* Handle short LOGICAL BLOCK LENGTH parameter. */
            if (   !(pVScsiReq->pbCDB[1] & 0x01)
                && cbCopied == sizeof(abParms)
                && cbList >= 12
                && abParms[3] == 8)
            {
                uint32_t    cbBlock;

                cbBlock = scsiBE2H_U24(&abParms[4 + 5]);
                Log2(("SBC: set LOGICAL BLOCK LENGTH to %u\n", cbBlock));
                if (cbBlock == 512) /* Fixed block size. */
                {
                    rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                    break;
                }
            }
            /* Fail any other requests. */
            rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
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
            break;
        }
        case SCSI_READ_10:
        {
            enmTxDir        = VSCSIIOREQTXDIR_READ;
            uLbaStart       = scsiBE2H_U32(&pVScsiReq->pbCDB[2]);
            cSectorTransfer = scsiBE2H_U16(&pVScsiReq->pbCDB[7]);
            break;
        }
        case SCSI_READ_12:
        {
            enmTxDir        = VSCSIIOREQTXDIR_READ;
            uLbaStart       = scsiBE2H_U32(&pVScsiReq->pbCDB[2]);
            cSectorTransfer = scsiBE2H_U32(&pVScsiReq->pbCDB[6]);
            break;
        }
        case SCSI_READ_16:
        {
            enmTxDir        = VSCSIIOREQTXDIR_READ;
            uLbaStart       = scsiBE2H_U64(&pVScsiReq->pbCDB[2]);
            cSectorTransfer = scsiBE2H_U32(&pVScsiReq->pbCDB[10]);
            break;
        }
        case SCSI_WRITE_6:
        {
            enmTxDir        = VSCSIIOREQTXDIR_WRITE;
            uLbaStart       = ((uint64_t)  pVScsiReq->pbCDB[3]
                                        | (pVScsiReq->pbCDB[2] <<  8)
                                        | ((pVScsiReq->pbCDB[1] & 0x1f) << 16));
            cSectorTransfer = pVScsiReq->pbCDB[4];
            cSectorTransfer = cSectorTransfer ? cSectorTransfer : 256;  /* Zero blocks means 256 */
            break;
        }
        case SCSI_WRITE_10:
        {
            enmTxDir        = VSCSIIOREQTXDIR_WRITE;
            uLbaStart       = scsiBE2H_U32(&pVScsiReq->pbCDB[2]);
            cSectorTransfer = scsiBE2H_U16(&pVScsiReq->pbCDB[7]);
            break;
        }
        case SCSI_WRITE_12:
        {
            enmTxDir        = VSCSIIOREQTXDIR_WRITE;
            uLbaStart       = scsiBE2H_U32(&pVScsiReq->pbCDB[2]);
            cSectorTransfer = scsiBE2H_U32(&pVScsiReq->pbCDB[6]);
            break;
        }
        case SCSI_WRITE_16:
        {
            enmTxDir        = VSCSIIOREQTXDIR_WRITE;
            uLbaStart       = scsiBE2H_U64(&pVScsiReq->pbCDB[2]);
            cSectorTransfer = scsiBE2H_U32(&pVScsiReq->pbCDB[10]);
            break;
        }
        case SCSI_SYNCHRONIZE_CACHE:
        {
            break; /* Handled below */
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

                    /* We do not implement an echo buffer. */
                    memset(aReply, 0, sizeof(aReply));

                    vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_T2I);
                    vscsiReqSetXferSize(pVScsiReq, RT_MIN(sizeof(aReply), scsiBE2H_U24(&pVScsiReq->pbCDB[6])));

                    RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, sizeof(aReply));
                    rcReq =  vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
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
        case SCSI_VERIFY_10:
        case SCSI_START_STOP_UNIT:
        {
            vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_NONE);
            vscsiReqSetXferSize(pVScsiReq, 0);
            rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
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
                    scsiH2BE_U64(aReply, pVScsiLunSbc->cSectors - 1);
                    scsiH2BE_U32(&aReply[8], 512);
                    if (pVScsiLun->fFeatures & VSCSI_LUN_FEATURE_UNMAP)
                        aReply[14] = 0x80; /* LPME enabled */
                    /* Leave the rest 0 */

                    vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_T2I);
                    vscsiReqSetXferSize(pVScsiReq, sizeof(aReply));
                    RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, sizeof(aReply));
                    rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                    break;
                }
                default:
                    rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00); /* Don't know if this is correct */
            }
            break;
        }
        case SCSI_UNMAP:
        {
            if (pVScsiLun->fFeatures & VSCSI_LUN_FEATURE_UNMAP)
            {
                uint8_t abHdr[8];
                size_t cbCopied;
                size_t cbList = scsiBE2H_U16(&pVScsiReq->pbCDB[7]);

                /* Copy the header. */
                vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_I2T);
                vscsiReqSetXferSize(pVScsiReq, cbList);
                cbCopied = RTSgBufCopyToBuf(&pVScsiReq->SgBuf, &abHdr[0], sizeof(abHdr));

                /* Using the anchor bit is not supported. */
                if (   !(pVScsiReq->pbCDB[1] & 0x01)
                    && cbCopied == sizeof(abHdr)
                    && cbList >= 8)
                {
                    uint32_t    cBlkDesc = scsiBE2H_U16(&abHdr[2]) / 16;

                    if (cBlkDesc)
                    {
                        PRTRANGE paRanges = (PRTRANGE)RTMemAllocZ(cBlkDesc * sizeof(RTRANGE));
                        if (paRanges)
                        {
                            for (unsigned i = 0; i < cBlkDesc; i++)
                            {
                                uint8_t abBlkDesc[16];

                                cbCopied = RTSgBufCopyToBuf(&pVScsiReq->SgBuf, &abBlkDesc[0], sizeof(abBlkDesc));
                                if (RT_UNLIKELY(cbCopied != sizeof(abBlkDesc)))
                                {
                                    rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
                                    break;
                                }

                                paRanges[i].offStart = scsiBE2H_U64(&abBlkDesc[0]) * 512;
                                paRanges[i].cbRange = (size_t)scsiBE2H_U32(&abBlkDesc[8]) * 512;
                            }

                            if (rcReq == SCSI_STATUS_OK)
                                rc = vscsiIoReqUnmapEnqueue(pVScsiLun, pVScsiReq, paRanges, cBlkDesc);
                            if (   rcReq != SCSI_STATUS_OK
                                || RT_FAILURE(rc))
                                RTMemFree(paRanges);
                        }
                        else /* Out of memory. */
                            rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_HARDWARE_ERROR, SCSI_ASC_SYSTEM_RESOURCE_FAILURE,
                                                             SCSI_ASCQ_SYSTEM_BUFFER_FULL);
                    }
                    else /* No block descriptors is not an error condition. */
                        rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
                }
                else /* Invalid CDB. */
                    rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
            }
            else
                rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_ILLEGAL_OPCODE, 0x00);

            break;
        }
        default:
            //AssertMsgFailed(("Command %#x [%s] not implemented\n", pRequest->pbCDB[0], SCSICmdText(pRequest->pbCDB[0])));
            rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_ILLEGAL_OPCODE, 0x00);
    }

    if (enmTxDir != VSCSIIOREQTXDIR_INVALID)
    {
        LogFlow(("%s: uLbaStart=%llu cSectorTransfer=%u\n",
                 __FUNCTION__, uLbaStart, cSectorTransfer));

        vscsiReqSetXferSize(pVScsiReq, cSectorTransfer * 512);

        if (RT_UNLIKELY(uLbaStart + cSectorTransfer > pVScsiLunSbc->cSectors))
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
            /* Enqueue new I/O request */
            if (   (   enmTxDir == VSCSIIOREQTXDIR_WRITE
                    || enmTxDir == VSCSIIOREQTXDIR_FLUSH)
                && (pVScsiLun->fFeatures & VSCSI_LUN_FEATURE_READONLY))
            {
                rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_DATA_PROTECT, SCSI_ASC_WRITE_PROTECTED, 0x00);
                vscsiDeviceReqComplete(pVScsiLun->pVScsiDevice, pVScsiReq, rcReq, false, VINF_SUCCESS);
            }
            else
            {
                vscsiReqSetXferDir(pVScsiReq, enmTxDir == VSCSIIOREQTXDIR_WRITE ? VSCSIXFERDIR_I2T : VSCSIXFERDIR_T2I);
                rc = vscsiIoReqTransferEnqueue(pVScsiLun, pVScsiReq, enmTxDir,
                                               uLbaStart * 512, cSectorTransfer * 512);
            }
        }
    }
    else if (pVScsiReq->pbCDB[0] == SCSI_SYNCHRONIZE_CACHE)
    {
        /* Enqueue flush */
        vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_NONE);
        vscsiReqSetXferSize(pVScsiReq, 0);
        rc = vscsiIoReqFlushEnqueue(pVScsiLun, pVScsiReq);
    }
    else if (pVScsiReq->pbCDB[0] !=  SCSI_UNMAP) /* Request completed */
        vscsiDeviceReqComplete(pVScsiLun->pVScsiDevice, pVScsiReq, rcReq, false, VINF_SUCCESS);

    return rc;
}


/**
 * The supported operation codes for the SBC LUN type.
 *
 * @note This gives the minimum size required by our implementation
 *       which may be smaller than what the spec defines (for example
 *       we do not access the control byte at the end).
 */
static uint8_t s_acbCdbOpc[] =
{
    VSCSI_LUN_CDB_SZ_INVALID_X8,     /**< 0x00 - 0x07 Invalid */
    5,                               /**< 0x08        READ (6) */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x09        Invalid */
    5,                               /**< 0x0a        WRITE (6) */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x0b        Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X4,     /**< 0x0c - 0x0f Invalid */

    VSCSI_LUN_CDB_SZ_INVALID_X2,     /**< 0x10 - 0x11 Invalid */
    3,                               /**< 0x12        INQUIRY (at least 3) */
    VSCSI_LUN_CDB_SZ_INVALID_X2,     /**< 0x13 - 0x14 Invalid */
    5,                               /**< 0x15        MODE SELECT (6) */
    VSCSI_LUN_CDB_SZ_INVALID_X4,     /**< 0x16 - 0x19 Invalid */
    5,                               /**< 0x1a        MODE SENSE (6) */
    1,                               /**< 0x1b        START STOP UNIT */
    VSCSI_LUN_CDB_SZ_INVALID_X4,     /**< 0x1c - 0x1f Invalid */

    VSCSI_LUN_CDB_SZ_INVALID_X4,     /**< 0x20 - 0x23 Invalid */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x24        Invalid */
    1,                               /**< 0x25        READ CAPACITY */
    VSCSI_LUN_CDB_SZ_INVALID_X2,     /**< 0x26 - 0x27 Invalid */
    9,                               /**< 0x28        READ (10) */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x29        Invalid */
    9,                               /**< 0x2a        WRITE (10) */
    VSCSI_LUN_CDB_SZ_INVALID_X4,     /**< 0x2b - 0x2e Invalid */
    1,                               /**< 0x2f        VERIFY (10) */

    VSCSI_LUN_CDB_SZ_INVALID_X4,     /**< 0x30 - 0x33 Invalid */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x34        Invalid */
    1,                               /**< 0x35        SYNCHRONIZE CACHE */
    VSCSI_LUN_CDB_SZ_INVALID_X4,     /**< 0x36 - 0x39 Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X2,     /**< 0x3a - 0x3b Invalid */
    8,                               /**< 0x3c        READ BUFFER */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x3d        Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X2,     /**< 0x3e - 0x3f Invalid */

    VSCSI_LUN_CDB_SZ_INVALID_X2,     /**< 0x40 - 0x41 Invalid */
    9,                               /**< 0x42        UNMAP */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x43        Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X8,     /**< 0x44 - 0x4b Invalid */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x4c        Invalid */
    9,                               /**< 0x4d        LOG SENSE */
    VSCSI_LUN_CDB_SZ_INVALID_X2,     /**< 0x4e - 0x4f Invalid */

    VSCSI_LUN_CDB_SZ_INVALID_X16,    /**< 0x50 - 0x5f Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X16,    /**< 0x60 - 0x6f Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X16,    /**< 0x70 - 0x7f Invalid */

    VSCSI_LUN_CDB_SZ_INVALID_X8,     /**< 0x80 - 0x87 Invalid */
    14,                              /**< 0x88        READ (16) */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x89        Invalid */
    14,                              /**< 0x8a        WRITE (16) */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x8b        Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X4,     /**< 0x8c - 0x8f Invalid */

    VSCSI_LUN_CDB_SZ_INVALID_X8,     /**< 0x90 - 0x97 Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X4,     /**< 0x98 - 0x9b Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X2,     /**< 0x9c - 0x9d Invalid */
    2,                               /**< 0x9e        SERVICE ACTION IN (16) (at least 2). */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0x9f        Invalid */

    VSCSI_LUN_CDB_SZ_INVALID_X8,     /**< 0xa0 - 0xa7 Invalid */
    10,                              /**< 0xa8        READ (12) */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0xa9        Invalid */
    10,                              /**< 0xaa        WRITE (12) */
    VSCSI_LUN_CDB_SZ_INVALID,        /**< 0xab        Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X4,     /**< 0xac - 0xaf Invalid */

    VSCSI_LUN_CDB_SZ_INVALID_X16,    /**< 0xb0 - 0xbf Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X16,    /**< 0xc0 - 0xcf Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X16,    /**< 0xd0 - 0xdf Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X16,    /**< 0xe0 - 0xef Invalid */
    VSCSI_LUN_CDB_SZ_INVALID_X16     /**< 0xf0 - 0xff Invalid */
};
AssertCompileSize(s_acbCdbOpc, 256 * sizeof(uint8_t));


VSCSILUNDESC g_VScsiLunTypeSbc =
{
    /** enmLunType */
    VSCSILUNTYPE_SBC,
    /** pcszDescName */
    "SBC",
    /** cbLun */
    sizeof(VSCSILUNSBC),
    /** pacbCdbOpc */
    &s_acbCdbOpc[0],
    /** cSupOpcInfo */
    0,
    /** paSupOpcInfo */
    NULL,
    /** pfnVScsiLunInit */
    vscsiLunSbcInit,
    /** pfnVScsiLunDestroy */
    vscsiLunSbcDestroy,
    /** pfnVScsiLunReqProcess */
    vscsiLunSbcReqProcess,
    /** pfnVScsiLunReqFree */
    NULL,
    /** pfnVScsiLunMediumInserted */
    NULL,
    /** pfnVScsiLunMediumRemoved */
    NULL
};

