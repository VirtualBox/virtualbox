/* $Id: VSCSISense.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Virtual SCSI driver: Sense handling
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
#define LOG_GROUP LOG_GROUP_VSCSI
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/string.h>

#include "VSCSIInternal.h"

void vscsiSenseInit(PVSCSISENSE pVScsiSense)
{
    memset(pVScsiSense->abSenseBuf, 0, sizeof(pVScsiSense->abSenseBuf));

    /* Fill in valid sense information (can't be just zeros). */
    pVScsiSense->abSenseBuf[0]  = (1 << 7) | SCSI_SENSE_RESPONSE_CODE_CURR_FIXED; /* Fixed format */
    pVScsiSense->abSenseBuf[2]  = SCSI_SENSE_NONE;
    pVScsiSense->abSenseBuf[7]  = 10;
    pVScsiSense->abSenseBuf[12] = SCSI_ASC_NONE;
}

int vscsiReqSenseOkSet(PVSCSISENSE pVScsiSense, PVSCSIREQINT pVScsiReq)
{
    LogFlowFunc(("STATUS OK\n"));
    memset(pVScsiSense->abSenseBuf, 0, sizeof(pVScsiSense->abSenseBuf));

    pVScsiSense->abSenseBuf[0]  = (1 << 7) | SCSI_SENSE_RESPONSE_CODE_CURR_FIXED; /* Fixed format */
    pVScsiSense->abSenseBuf[2]  = SCSI_SENSE_NONE;
    pVScsiSense->abSenseBuf[7]  = 10;
    pVScsiSense->abSenseBuf[12] = SCSI_ASC_NONE;
    pVScsiSense->abSenseBuf[13] = SCSI_ASC_NONE; /* Should be ASCQ but it has the same value for success. */

    if (pVScsiReq->pbSense && pVScsiReq->cbSense)
    {
        pVScsiReq->cbSenseWritten = RT_MIN(sizeof(pVScsiSense->abSenseBuf), pVScsiReq->cbSense);
        memcpy(pVScsiReq->pbSense, pVScsiSense->abSenseBuf, pVScsiReq->cbSenseWritten);
    }

    return SCSI_STATUS_OK;
}

int vscsiReqSenseErrorSet(PVSCSISENSE pVScsiSense, PVSCSIREQINT pVScsiReq, uint8_t uSCSISenseKey, uint8_t uSCSIASC, uint8_t uSCSIASCQ)
{
    LogFlowFunc(("CHECK CONDITION: %s %s\n", SCSISenseText(uSCSISenseKey), SCSISenseExtText(uSCSIASC, uSCSIASCQ)));
    memset(pVScsiSense->abSenseBuf, 0, sizeof(pVScsiSense->abSenseBuf));
    pVScsiSense->abSenseBuf[0] = (1 << 7) | SCSI_SENSE_RESPONSE_CODE_CURR_FIXED; /* Fixed format */
    pVScsiSense->abSenseBuf[2] = uSCSISenseKey;
    pVScsiSense->abSenseBuf[7]  = 10;
    pVScsiSense->abSenseBuf[12] = uSCSIASC;
    pVScsiSense->abSenseBuf[13] = uSCSIASCQ;

    if (pVScsiReq->pbSense && pVScsiReq->cbSense)
    {
        pVScsiReq->cbSenseWritten = RT_MIN(sizeof(pVScsiSense->abSenseBuf), pVScsiReq->cbSense);
        memcpy(pVScsiReq->pbSense, pVScsiSense->abSenseBuf, pVScsiReq->cbSenseWritten);
    }

    return SCSI_STATUS_CHECK_CONDITION;
}

int vscsiReqSenseErrorInfoSet(PVSCSISENSE pVScsiSense, PVSCSIREQINT pVScsiReq, uint8_t uSCSISenseKey, uint8_t uSCSIASC, uint8_t uSCSIASCQ, uint32_t uInfo)
{
    LogFlowFunc(("CHECK CONDITION: %s %s uInfo:%02x\n", SCSISenseText(uSCSISenseKey), SCSISenseExtText(uSCSIASC, uSCSIASCQ), uInfo));
    memset(pVScsiSense->abSenseBuf, 0, sizeof(pVScsiSense->abSenseBuf));
    pVScsiSense->abSenseBuf[0] = RT_BIT(7) | SCSI_SENSE_RESPONSE_CODE_CURR_FIXED; /* Fixed format */
    pVScsiSense->abSenseBuf[2] = uSCSISenseKey;
    scsiH2BE_U32(&pVScsiSense->abSenseBuf[3], uInfo);
    pVScsiSense->abSenseBuf[7]  = 10;
    pVScsiSense->abSenseBuf[12] = uSCSIASC;
    pVScsiSense->abSenseBuf[13] = uSCSIASCQ;

    if (pVScsiReq->pbSense && pVScsiReq->cbSense)
    {
        pVScsiReq->cbSenseWritten = RT_MIN(sizeof(pVScsiSense->abSenseBuf), pVScsiReq->cbSense);
        memcpy(pVScsiReq->pbSense, pVScsiSense->abSenseBuf, pVScsiReq->cbSenseWritten);
    }

    return SCSI_STATUS_CHECK_CONDITION;
}

int vscsiReqSenseCmd(PVSCSISENSE pVScsiSense, PVSCSIREQINT pVScsiReq)
{
    /* Copy the current sense data to the buffer. */
    RTSgBufCopyFromBuf(&pVScsiReq->SgBuf, pVScsiSense->abSenseBuf, sizeof(pVScsiSense->abSenseBuf));
    return vscsiReqSenseOkSet(pVScsiSense, pVScsiReq);
}

