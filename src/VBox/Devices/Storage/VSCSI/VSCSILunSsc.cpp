/* $Id: VSCSILunSsc.cpp 110132 2025-07-07 11:33:52Z michal.necasek@oracle.com $ */
/** @file
 * Virtual SCSI driver: SSC LUN implementation (Streaming tape)
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
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/string.h>

#include "VSCSIInternal.h"

/**
 * SSC LUN instance
 */
typedef struct VSCSILUNSSC
{
    /** Core LUN structure */
    VSCSILUNINT     Core;
    /** Size of the virtual tape. */
    uint64_t        cbTape;
    /** Current position. */
    uint64_t        uCurPos;
    /** Number of blocks. */
    uint64_t        cBlocks;
    /** Block size. */
    uint64_t        cbBlock;
    /** Medium locked indicator. */
    bool            fLocked;
} VSCSILUNSSC, *PVSCSILUNSSC;


static int vscsiLUNSSCInit(PVSCSILUNINT pVScsiLun)
{
    PVSCSILUNSSC    pVScsiLunSsc = (PVSCSILUNSSC)pVScsiLun;
    int             rc = VINF_SUCCESS;

    pVScsiLunSsc->cbBlock = 512;    /* Default to 512-byte blocks. */
    pVScsiLunSsc->uCurPos = 0;      /* Start at beginning of tape. */
    pVScsiLunSsc->cbTape  = 0;

    uint32_t cRegions = vscsiLunMediumGetRegionCount(pVScsiLun);
    if (cRegions != 1)
        rc = VERR_INVALID_PARAMETER;

    if (RT_SUCCESS(rc))
        rc = vscsiLunMediumQueryRegionProperties(pVScsiLun, 0, NULL, &pVScsiLunSsc->cBlocks,
                                                 &pVScsiLunSsc->cbBlock, NULL);

    if (RT_SUCCESS(rc))
        pVScsiLunSsc->cbTape = pVScsiLunSsc->cBlocks * pVScsiLunSsc->cbBlock;

    return rc;
}

static int vscsiLUNSSCDestroy(PVSCSILUNINT pVScsiLun)
{
    PVSCSILUNSSC    pVScsiLUNSSC = (PVSCSILUNSSC)pVScsiLun;

    pVScsiLUNSSC->uCurPos = 0;      // shut compiler up

    return VINF_SUCCESS;
}

static int vscsiLUNSSCReqProcess(PVSCSILUNINT pVScsiLun, PVSCSIREQINT pVScsiReq)
{
    PVSCSILUNSSC    pVScsiLUNSSC = (PVSCSILUNSSC)pVScsiLun;
    VSCSIIOREQTXDIR enmTxDir = VSCSIIOREQTXDIR_INVALID;
    uint64_t        uTransferStart = 0;
    uint32_t        cBlocksTransfer = 0;
    uint32_t        cbTransfer = 0;
    int             rc = VINF_SUCCESS;
    int             rcReq = SCSI_STATUS_OK;
    unsigned        uCmd = pVScsiReq->pbCDB[0];

    /*
     * GET CONFIGURATION, GET EVENT/STATUS NOTIFICATION, INQUIRY, and REQUEST SENSE commands
     * operate even when a unit attention condition exists for initiator; every other command
     * needs to report CHECK CONDITION in that case.
     */
    if (!pVScsiLUNSSC->Core.fReady && uCmd != SCSI_INQUIRY)
    {
        /*
         * A note on media changes: As long as a medium is not present, the unit remains in
         * the 'not ready' state. Technically the unit becomes 'ready' soon after a medium
         * is inserted; however, we internally keep the 'not ready' state until we've had
         * a chance to report the UNIT ATTENTION status indicating a media change.
         */
        if (pVScsiLUNSSC->Core.fMediaPresent)
        {
            rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_UNIT_ATTENTION,
                                             SCSI_ASC_MEDIUM_MAY_HAVE_CHANGED, 0x00);
            pVScsiLUNSSC->Core.fReady = true;
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
            Assert(!pVScsiLUNSSC->Core.fReady); /* Only should get here if LUN isn't ready. */
            vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_NONE);
            rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_NOT_READY, SCSI_ASC_MEDIUM_NOT_PRESENT, 0x00);
            break;

        case SCSI_INQUIRY:
        {
            SCSIINQUIRYDATA ScsiInquiryReply;

            memset(&ScsiInquiryReply, 0, sizeof(ScsiInquiryReply));

            ScsiInquiryReply.cbAdditional           = 31;
            ScsiInquiryReply.fRMB                   = 1;    /* Removable. */
            ScsiInquiryReply.u5PeripheralDeviceType = SCSI_INQUIRY_DATA_PERIPHERAL_DEVICE_TYPE_SEQUENTIAL_ACCESS;
            ScsiInquiryReply.u3PeripheralQualifier  = SCSI_INQUIRY_DATA_PERIPHERAL_QUALIFIER_CONNECTED;
            ScsiInquiryReply.u3AnsiVersion          = 0x05; /* SSC-?? compliant */
            ScsiInquiryReply.fCmdQue                = 1;    /* Command queuing supported. */
            ScsiInquiryReply.fWBus16                = 1;
            scsiPadStrS(ScsiInquiryReply.achVendorId, "VBOX", 8);
            scsiPadStrS(ScsiInquiryReply.achProductId, "TAPE DRIVE", 16);
            scsiPadStrS(ScsiInquiryReply.achProductLevel, "1.0", 4);

            uint16_t cbDataReq = scsiBE2H_U16(&pVScsiReq->pbCDB[3]);
            vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_T2I);
            vscsiReqSetXferSize(pVScsiReq, RT_MIN(sizeof(SCSIINQUIRYDATA), cbDataReq));

            RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, (uint8_t *)&ScsiInquiryReply, sizeof(SCSIINQUIRYDATA));
            rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
            break;
        }
        case SCSI_MODE_SENSE_6:
        {
//            uint8_t uModePage = pVScsiReq->pbCDB[2] & 0x3f;
            uint8_t aReply[24];
            uint8_t *pu8ReplyPos;
            uint8_t uReplyLen;
            bool    fWantBlkDesc = !!(pVScsiReq->pbCDB[1] & RT_BIT(3)); /* DBD bit. */

            memset(aReply, 0, sizeof(aReply));
            if (fWantBlkDesc)
                uReplyLen = 4 + 8;
            else
                uReplyLen = 4;

            aReply[0] = uReplyLen - 1;  /* Reply length. */
            aReply[1] = 0xB6;           /* Travan TR-4 medium (whatever). */
            aReply[2] = 0; //RT_BIT(7);      /* Write Protected. */      //@todo!
            aReply[3] = uReplyLen - 4;  /* Block descriptor length. */

            pu8ReplyPos = aReply + 4;

            /* Fill out the Block Descriptor. */
            if (fWantBlkDesc)
            {
                *pu8ReplyPos++ = 0x45;  /* Travan TR-4 density. */
                *pu8ReplyPos++ = 0;     /* All blocks are the same. */
                *pu8ReplyPos++ = 0;     /// @todo this calls for some macros!
                *pu8ReplyPos++ = 0;
                *pu8ReplyPos++ = 0;     /* Reserved. */
                *pu8ReplyPos++ = 0x00;  /* Block length (512). */
                *pu8ReplyPos++ = 0x02;
                *pu8ReplyPos++ = 0x00;
            }

            vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_T2I);
            vscsiReqSetXferSize(pVScsiReq, RT_MIN(sizeof(aReply), pVScsiReq->pbCDB[4]));

            RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, sizeof(aReply));
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
            enmTxDir        = VSCSIIOREQTXDIR_READ;
            cbTransfer      = ((uint32_t)   pVScsiReq->pbCDB[4]
                                         | (pVScsiReq->pbCDB[3] <<  8)
                                         | (pVScsiReq->pbCDB[2] << 16));
            cBlocksTransfer = pVScsiReq->pbCDB[4];
            cSectorTransfer = cSectorTransfer ? cSectorTransfer : 256;  /* Zero blocks means 256 */
            uTransferStart  = pVScsiLUNSSC->uCurPos;
            pVScsiLUNSSC->uCurPos += cbTransfer;
            break;
        }
        case SCSI_WRITE_6:
        {
            enmTxDir        = VSCSIIOREQTXDIR_WRITE;
            cbTransfer      = ((uint32_t)   pVScsiReq->pbCDB[4]
                                         | (pVScsiReq->pbCDB[3] <<  8)
                                         | (pVScsiReq->pbCDB[2] << 16));
            cBlocksTransfer = pVScsiReq->pbCDB[4];
            cSectorTransfer = cSectorTransfer ? cSectorTransfer : 256;  /* Zero blocks means 256 */
            uTransferStart  = pVScsiLUNSSC->uCurPos;
            pVScsiLUNSSC->uCurPos += cbTransfer;
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

                    /* We do not implement an echo buffer. */
                    memset(aReply, 0, sizeof(aReply));

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
        case SCSI_LOAD_UNLOAD:
        {
            /// @todo should load/unload do anyhting?
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
                default:
                    rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00);
            }
            break;
        }
        case SCSI_SERVICE_ACTION_IN_16:
        {
            switch (pVScsiReq->pbCDB[1] & 0x1f)
            {
                default:
                    rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_INV_FIELD_IN_CMD_PACKET, 0x00); /* Don't know if this is correct */
            }
            break;
        }
        case SCSI_PREVENT_ALLOW_MEDIUM_REMOVAL:
        {
            pVScsiLUNSSC->fLocked = pVScsiReq->pbCDB[4] & 1;
            vscsiLunMediumSetLock(pVScsiLun, pVScsiLUNSSC->fLocked);
            rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
            break;
        }
        case SCSI_REWIND:
            /// @todo flush data + write EOD? immed bit? partitions?
            pVScsiLUNSSC->uCurPos = 0;
            rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
            break;
        case SCSI_RESERVE_6:
            /// @todo perform actual reservation
            rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
            break;
        case SCSI_RELEASE_6:
            /// @todo perform actual release
            rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
            break;
        case SCSI_READ_BLOCK_LIMITS:
        {
            uint8_t     aReply[6];

            /* Report unrestricted block sizes (1-FFFFFFh). */
            memset(aReply, 0, sizeof(aReply));
            /// @todo Helpers for big-endian 16-bit/24-bit/32-bit constants?
            aReply[1] = aReply[2] = aReply[3] = 0xff;
            aReply[5] = 0x01;

            vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_T2I);
            vscsiReqSetXferSize(pVScsiReq, sizeof(aReply));

            RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, aReply, sizeof(aReply));
            rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
            break;
        }
        default:
            //AssertMsgFailed(("Command %#x [%s] not implemented\n", pVScsiReq->pbCDB[0], SCSICmdText(pVScsiReq->pbCDB[0])));
            rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ASC_ILLEGAL_OPCODE, 0x00);
        }
    }

    if (enmTxDir != VSCSIIOREQTXDIR_INVALID)
    {
        LogFlow(("%s: uTransferStart=%llu cbTransfer=%u\n",
                 __FUNCTION__, uTransferStart, cbTransfer));

        if (RT_UNLIKELY(uTransferStart + cbTransfer > pVScsiLUNSSC->cbTape))
        {
            uint64_t    cbResidue = uTransferStart + cbTransfer - pVScsiLUNSSC->cbTape;

            if (enmTxDir == VSCSIIOREQTXDIR_READ && cbResidue < cbTransfer)
            {
                /* If it's a read and some data is still available, read what we can. */
                vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_T2I);
                vscsiReqSetXferSize(pVScsiReq, cbTransfer - cbResidue);
                rc = vscsiIoReqTransferEnqueue(pVScsiLun, pVScsiReq, enmTxDir,
                                               uTransferStart, cbTransfer - cbResidue);
                rcReq = vscsiLunReqSenseErrorInfoSet(pVScsiLun, pVScsiReq, SCSI_SENSE_NONE | SCSI_SENSE_FLAG_FILEMARK, SCSI_ASC_NONE, SCSI_ASCQ_FILEMARK_DETECTED, cbResidue);
            }
            else
            {
//                rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_, SCSI_ASC_NONE, SCSI_ASCQ_END_OF_DATA_DETECTED);
                /* Report a file mark. */
                vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_NONE);
                rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_NONE | SCSI_SENSE_FLAG_FILEMARK, SCSI_ASC_NONE, SCSI_ASCQ_FILEMARK_DETECTED);
                vscsiDeviceReqComplete(pVScsiLun->pVScsiDevice, pVScsiReq, rcReq, false, VINF_SUCCESS);
            }
        }
        else if (!cbTransfer)
        {
            /* A 0 transfer length is not an error. */
            vscsiReqSetXferDir(pVScsiReq, VSCSIXFERDIR_NONE);
            rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
            vscsiDeviceReqComplete(pVScsiLun->pVScsiDevice, pVScsiReq, rcReq, false, VINF_SUCCESS);
        }
        else
        {
            /* Enqueue new I/O request */
            vscsiReqSetXferDir(pVScsiReq, enmTxDir == VSCSIIOREQTXDIR_WRITE ? VSCSIXFERDIR_I2T : VSCSIXFERDIR_T2I);
            vscsiReqSetXferSize(pVScsiReq, cbTransfer);
            rc = vscsiIoReqTransferEnqueue(pVScsiLun, pVScsiReq, enmTxDir,
                                           uTransferStart, cbTransfer);
        }
    }
    else /* Request completed */
        vscsiDeviceReqComplete(pVScsiLun->pVScsiDevice, pVScsiReq, rcReq, false, VINF_SUCCESS);

    return rc;
}

/** @interface_method_impl{VSCSILUNDESC,pfnVScsiLunMediumInserted} */
static DECLCALLBACK(int) vscsiLunSSCMediumInserted(PVSCSILUNINT pVScsiLun)
{
    int rc = VINF_SUCCESS;
    uint32_t cRegions = vscsiLunMediumGetRegionCount(pVScsiLun);
    if (cRegions != 1)
        rc = VERR_INVALID_PARAMETER;

    if (RT_SUCCESS(rc))
    {
#if 0
        PVSCSILUNSSC    pVScsiLUNSSC = (PVSCSILUNSSC)pVScsiLun;

        pVScsiLUNSSC->cSectors = cbDisk / pVScsiLUNSSC->cbSector;

        uint32_t OldStatus, NewStatus;
        do
        {
            OldStatus = ASMAtomicReadU32((volatile uint32_t *)&pVScsiLUNSSC->MediaEventStatus);
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
        } while (!ASMAtomicCmpXchgU32((volatile uint32_t *)&pVScsiLUNSSC->MediaEventStatus,
                                      NewStatus, OldStatus));

        ASMAtomicXchgU32(&pVScsiLUNSSC->u32MediaTrackType, MMC_MEDIA_TYPE_UNKNOWN);
#endif
    }

    return rc;
}

/** @interface_method_impl{VSCSILUNDESC,pfnVScsiLunMediumRemoved} */
static DECLCALLBACK(int) vscsiLunSSCMediumRemoved(PVSCSILUNINT pVScsiLun)
{
    NOREF(pVScsiLun);
#if 0
    PVSCSILUNSSC    pVScsiLUNSSC = (PVSCSILUNSSC)pVScsiLun;

    ASMAtomicWriteU32((volatile uint32_t *)&pVScsiLUNSSC->MediaEventStatus, MMCEVENTSTATUSTYPE_MEDIA_REMOVED);
    ASMAtomicXchgU32(&pVScsiLUNSSC->u32MediaTrackType, MMC_MEDIA_TYPE_NO_DISC);
#endif
    return VINF_SUCCESS;
}

VSCSILUNDESC g_VScsiLunTypeSsc =
{
    /** enmLunType */
    VSCSILUNTYPE_SSC,
    /** pcszDescName */
    "SSC",
    /** cbLun */
    sizeof(VSCSILUNSSC),
    /** pacbCdbOpc */
    NULL,
    /** cSupOpcInfo */
    0,
    /** paSupOpcInfo */
    NULL,
    /** pfnVScsiLunInit */
    vscsiLUNSSCInit,
    /** pfnVScsiLunDestroy */
    vscsiLUNSSCDestroy,
    /** pfnVScsiLunReqProcess */
    vscsiLUNSSCReqProcess,
    /** pfnVScsiLunReqFree */
    NULL,
    /** pfnVScsiLunMediumInserted */
    vscsiLunSSCMediumInserted,
    /** pfnVScsiLunMediumRemoved */
    vscsiLunSSCMediumRemoved
};
