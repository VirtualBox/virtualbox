/* $Id: VSCSIIoReq.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Virtual SCSI driver: I/O request handling.
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
#include <iprt/errcore.h>
#include <VBox/types.h>
#include <VBox/vscsi.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/asm.h>

#include "VSCSIInternal.h"

int vscsiIoReqInit(PVSCSILUNINT pVScsiLun)
{
    return vscsiLunReqAllocSizeSet(pVScsiLun, sizeof(VSCSIIOREQINT));
}

int vscsiIoReqFlushEnqueue(PVSCSILUNINT pVScsiLun, PVSCSIREQINT pVScsiReq)
{
    int rc = VINF_SUCCESS;
    PVSCSIIOREQINT pVScsiIoReq = NULL;

    rc = vscsiLunReqAlloc(pVScsiLun, (uintptr_t)pVScsiReq, &pVScsiIoReq);
    if (RT_SUCCESS(rc))
    {
        pVScsiIoReq->pVScsiReq = pVScsiReq;
        pVScsiIoReq->pVScsiLun = pVScsiLun;
        pVScsiIoReq->enmTxDir  = VSCSIIOREQTXDIR_FLUSH;

        ASMAtomicIncU32(&pVScsiLun->IoReq.cReqOutstanding);

        rc = vscsiLunReqTransferEnqueue(pVScsiLun, pVScsiIoReq);
        if (RT_FAILURE(rc))
        {
            ASMAtomicDecU32(&pVScsiLun->IoReq.cReqOutstanding);
            vscsiLunReqFree(pVScsiLun, pVScsiIoReq);
        }
    }

    return rc;
}


int vscsiIoReqTransferEnqueue(PVSCSILUNINT pVScsiLun, PVSCSIREQINT pVScsiReq,
                              VSCSIIOREQTXDIR enmTxDir, uint64_t uOffset,
                              size_t cbTransfer)
{
    int rc = VINF_SUCCESS;
    PVSCSIIOREQINT pVScsiIoReq = NULL;

    LogFlowFunc(("pVScsiLun=%#p pVScsiReq=%#p enmTxDir=%u uOffset=%llu cbTransfer=%u\n",
                 pVScsiLun, pVScsiReq, enmTxDir, uOffset, cbTransfer));

    rc = vscsiLunReqAlloc(pVScsiLun, (uintptr_t)pVScsiReq, &pVScsiIoReq);
    if (RT_SUCCESS(rc))
    {
        pVScsiIoReq->pVScsiReq       = pVScsiReq;
        pVScsiIoReq->pVScsiLun       = pVScsiLun;
        pVScsiIoReq->enmTxDir        = enmTxDir;
        pVScsiIoReq->u.Io.uOffset    = uOffset;
        pVScsiIoReq->u.Io.cbTransfer = cbTransfer;
        pVScsiIoReq->u.Io.paSeg      = pVScsiReq->SgBuf.paSegs;
        pVScsiIoReq->u.Io.cSeg       = pVScsiReq->SgBuf.cSegs;

        ASMAtomicIncU32(&pVScsiLun->IoReq.cReqOutstanding);

        rc = vscsiLunReqTransferEnqueue(pVScsiLun, pVScsiIoReq);
        if (RT_FAILURE(rc))
        {
            ASMAtomicDecU32(&pVScsiLun->IoReq.cReqOutstanding);
            vscsiLunReqFree(pVScsiLun, pVScsiIoReq);
        }
    }

    return rc;
}


int vscsiIoReqTransferEnqueueEx(PVSCSILUNINT pVScsiLun, PVSCSIREQINT pVScsiReq,
                                VSCSIIOREQTXDIR enmTxDir, uint64_t uOffset,
                                PCRTSGSEG paSegs, unsigned cSegs, size_t cbTransfer)
{
    int rc = VINF_SUCCESS;
    PVSCSIIOREQINT pVScsiIoReq = NULL;

    LogFlowFunc(("pVScsiLun=%#p pVScsiReq=%#p enmTxDir=%u uOffset=%llu cbTransfer=%u\n",
                 pVScsiLun, pVScsiReq, enmTxDir, uOffset, cbTransfer));

    rc = vscsiLunReqAlloc(pVScsiLun, (uintptr_t)pVScsiReq, &pVScsiIoReq);
    if (RT_SUCCESS(rc))
    {
        pVScsiIoReq->pVScsiReq       = pVScsiReq;
        pVScsiIoReq->pVScsiLun       = pVScsiLun;
        pVScsiIoReq->enmTxDir        = enmTxDir;
        pVScsiIoReq->u.Io.uOffset    = uOffset;
        pVScsiIoReq->u.Io.cbTransfer = cbTransfer;
        pVScsiIoReq->u.Io.paSeg      = paSegs;
        pVScsiIoReq->u.Io.cSeg       = cSegs;

        ASMAtomicIncU32(&pVScsiLun->IoReq.cReqOutstanding);

        rc = vscsiLunReqTransferEnqueue(pVScsiLun, pVScsiIoReq);
        if (RT_FAILURE(rc))
        {
            ASMAtomicDecU32(&pVScsiLun->IoReq.cReqOutstanding);
            vscsiLunReqFree(pVScsiLun, pVScsiIoReq);
        }
    }

    return rc;
}


int vscsiIoReqUnmapEnqueue(PVSCSILUNINT pVScsiLun, PVSCSIREQINT pVScsiReq,
                           PRTRANGE paRanges, unsigned cRanges)
{
    int rc = VINF_SUCCESS;
    PVSCSIIOREQINT pVScsiIoReq = NULL;

    LogFlowFunc(("pVScsiLun=%#p pVScsiReq=%#p paRanges=%#p cRanges=%u\n",
                 pVScsiLun, pVScsiReq, paRanges, cRanges));

    rc = vscsiLunReqAlloc(pVScsiLun, (uintptr_t)pVScsiReq, &pVScsiIoReq);
    if (RT_SUCCESS(rc))
    {
        pVScsiIoReq->pVScsiReq        = pVScsiReq;
        pVScsiIoReq->pVScsiLun        = pVScsiLun;
        pVScsiIoReq->enmTxDir         = VSCSIIOREQTXDIR_UNMAP;
        pVScsiIoReq->u.Unmap.paRanges = paRanges;
        pVScsiIoReq->u.Unmap.cRanges  = cRanges;

        ASMAtomicIncU32(&pVScsiLun->IoReq.cReqOutstanding);

        rc = vscsiLunReqTransferEnqueue(pVScsiLun, pVScsiIoReq);
        if (RT_FAILURE(rc))
        {
            ASMAtomicDecU32(&pVScsiLun->IoReq.cReqOutstanding);
            vscsiLunReqFree(pVScsiLun, pVScsiIoReq);
        }
    }

    return rc;
}


uint32_t vscsiIoReqOutstandingCountGet(PVSCSILUNINT pVScsiLun)
{
    return ASMAtomicReadU32(&pVScsiLun->IoReq.cReqOutstanding);
}


VBOXDDU_DECL(int) VSCSIIoReqCompleted(VSCSIIOREQ hVScsiIoReq, int rcIoReq, bool fRedoPossible)
{
    PVSCSIIOREQINT pVScsiIoReq = hVScsiIoReq;
    PVSCSILUNINT pVScsiLun;
    PVSCSIREQINT pVScsiReq;
    int rcReq = SCSI_STATUS_OK;

    AssertPtrReturn(pVScsiIoReq, VERR_INVALID_HANDLE);

    LogFlowFunc(("hVScsiIoReq=%#p rcIoReq=%Rrc\n", hVScsiIoReq, rcIoReq));

    pVScsiLun = pVScsiIoReq->pVScsiLun;
    pVScsiReq = pVScsiIoReq->pVScsiReq;

    AssertMsg(pVScsiLun->IoReq.cReqOutstanding > 0,
              ("Unregistered I/O request completed\n"));

    ASMAtomicDecU32(&pVScsiLun->IoReq.cReqOutstanding);

    if (RT_SUCCESS(rcIoReq))
        rcReq = vscsiLunReqSenseOkSet(pVScsiLun, pVScsiReq);
    else if (!fRedoPossible)
    {
        /** @todo Not 100% correct for the write case as the 0x00 ASCQ for write errors
         * is not used for SBC devices. */
        rcReq = vscsiLunReqSenseErrorSet(pVScsiLun, pVScsiReq, SCSI_SENSE_MEDIUM_ERROR,
                                         pVScsiIoReq->enmTxDir == VSCSIIOREQTXDIR_READ
                                         ? SCSI_ASC_READ_ERROR
                                         : SCSI_ASC_WRITE_ERROR,
                                         0x00);
    }
    else
        rcReq = SCSI_STATUS_CHECK_CONDITION;

    if (pVScsiIoReq->enmTxDir == VSCSIIOREQTXDIR_UNMAP)
        RTMemFree(pVScsiIoReq->u.Unmap.paRanges);

    /* Free the I/O request */
    vscsiLunReqFree(pVScsiLun, pVScsiIoReq);

    /* Notify completion of the SCSI request. */
    vscsiDeviceReqComplete(pVScsiLun->pVScsiDevice, pVScsiReq, rcReq, fRedoPossible, rcIoReq);

    return VINF_SUCCESS;
}


VBOXDDU_DECL(VSCSIIOREQTXDIR) VSCSIIoReqTxDirGet(VSCSIIOREQ hVScsiIoReq)
{
    PVSCSIIOREQINT pVScsiIoReq = hVScsiIoReq;

    AssertPtrReturn(pVScsiIoReq, VSCSIIOREQTXDIR_INVALID);

    return pVScsiIoReq->enmTxDir;
}


VBOXDDU_DECL(int) VSCSIIoReqParamsGet(VSCSIIOREQ hVScsiIoReq, uint64_t *puOffset,
                                      size_t *pcbTransfer, unsigned *pcSeg,
                                      size_t *pcbSeg, PCRTSGSEG *ppaSeg)
{
    PVSCSIIOREQINT pVScsiIoReq = hVScsiIoReq;

    AssertPtrReturn(pVScsiIoReq, VERR_INVALID_HANDLE);
    AssertReturn(   pVScsiIoReq->enmTxDir != VSCSIIOREQTXDIR_FLUSH
                 && pVScsiIoReq->enmTxDir != VSCSIIOREQTXDIR_UNMAP,
                 VERR_NOT_SUPPORTED);

    *puOffset    = pVScsiIoReq->u.Io.uOffset;
    *pcbTransfer = pVScsiIoReq->u.Io.cbTransfer;
    *pcSeg       = pVScsiIoReq->u.Io.cSeg;
    *pcbSeg      = pVScsiIoReq->u.Io.cbSeg;
    *ppaSeg      = pVScsiIoReq->u.Io.paSeg;

    return VINF_SUCCESS;
}

VBOXDDU_DECL(int) VSCSIIoReqUnmapParamsGet(VSCSIIOREQ hVScsiIoReq, PCRTRANGE *ppaRanges,
                                           unsigned *pcRanges)
{
    PVSCSIIOREQINT pVScsiIoReq = hVScsiIoReq;

    AssertPtrReturn(pVScsiIoReq, VERR_INVALID_HANDLE);
    AssertReturn(pVScsiIoReq->enmTxDir == VSCSIIOREQTXDIR_UNMAP, VERR_NOT_SUPPORTED);

    *ppaRanges = pVScsiIoReq->u.Unmap.paRanges;
    *pcRanges  = pVScsiIoReq->u.Unmap.cRanges;

    return VINF_SUCCESS;
}

