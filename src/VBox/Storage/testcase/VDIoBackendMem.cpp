/* $Id: VDIoBackendMem.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox HDD container test utility, async I/O memory backend
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#define LOGGROUP LOGGROUP_DEFAULT /** @todo Log group */
#include <iprt/errcore.h>
#include <iprt/log.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/mem.h>
#include <iprt/thread.h>
#include <iprt/circbuf.h>
#include <iprt/semaphore.h>

#include "VDMemDisk.h"
#include "VDIoBackendMem.h"

#define VDMEMIOBACKEND_REQS 1024

/**
 * Memory I/O request.
 */
typedef struct VDIOBACKENDREQ
{
    /** I/O request direction. */
    VDIOTXDIR       enmTxDir;
    /** Memory disk handle. */
    PVDMEMDISK      pMemDisk;
    /** Start offset. */
    uint64_t        off;
    /** Size of the transfer. */
    size_t          cbTransfer;
    /** Completion handler to call. */
    PFNVDIOCOMPLETE pfnComplete;
    /** Opaque user data. */
    void           *pvUser;
    /** S/G buffer. */
    RTSGBUF         SgBuf;
    /** Segment array - variable size. */
    RTSGSEG         aSegs[1];
} VDIOBACKENDREQ, *PVDIOBACKENDREQ;

typedef PVDIOBACKENDREQ *PPVDIOBACKENDREQ;

/**
 * I/O memory backend
 */
typedef struct VDIOBACKENDMEM
{
    /** Thread handle for the backend. */
    RTTHREAD    hThreadIo;
    /** Circular buffer used for submitting requests. */
    PRTCIRCBUF  pRequestRing;
    /** Size of the buffer in request items. */
    unsigned    cReqsRing;
    /** Event semaphore the thread waits on for more work. */
    RTSEMEVENT  EventSem;
    /** Flag whether the server should be still running. */
    volatile bool fRunning;
    /** Number of requests waiting in the request buffer. */
    volatile uint32_t cReqsWaiting;
} VDIOBACKENDMEM;

static DECLCALLBACK(int) vdIoBackendMemThread(RTTHREAD hThread, void *pvUser);

/**
 * Pokes the I/O thread that something interesting happened.
 *
 * @returns IPRT status code.
 *
 * @param pIoBackend    The backend to poke.
 */
static int vdIoBackendMemThreadPoke(PVDIOBACKENDMEM pIoBackend)
{
    return RTSemEventSignal(pIoBackend->EventSem);
}

int VDIoBackendMemCreate(PPVDIOBACKENDMEM ppIoBackend)
{
    int rc = VINF_SUCCESS;
    PVDIOBACKENDMEM pIoBackend = NULL;

    pIoBackend = (PVDIOBACKENDMEM)RTMemAllocZ(sizeof(VDIOBACKENDMEM));
    if (pIoBackend)
    {
        rc = RTCircBufCreate(&pIoBackend->pRequestRing, VDMEMIOBACKEND_REQS * sizeof(PVDIOBACKENDREQ));
        if (RT_SUCCESS(rc))
        {
            pIoBackend->cReqsRing = VDMEMIOBACKEND_REQS * sizeof(VDIOBACKENDREQ);
            pIoBackend->fRunning  = true;

            rc = RTSemEventCreate(&pIoBackend->EventSem);
            if (RT_SUCCESS(rc))
            {
                rc = RTThreadCreate(&pIoBackend->hThreadIo, vdIoBackendMemThread, pIoBackend, 0, RTTHREADTYPE_IO,
                                    RTTHREADFLAGS_WAITABLE, "MemIo");
                if (RT_SUCCESS(rc))
                {
                    *ppIoBackend = pIoBackend;

                    LogFlowFunc(("returns success\n"));
                    return VINF_SUCCESS;
                }
                RTSemEventDestroy(pIoBackend->EventSem);
            }

            RTCircBufDestroy(pIoBackend->pRequestRing);
        }

        RTMemFree(pIoBackend);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

int VDIoBackendMemDestroy(PVDIOBACKENDMEM pIoBackend)
{
    ASMAtomicXchgBool(&pIoBackend->fRunning, false);
    vdIoBackendMemThreadPoke(pIoBackend);

    RTThreadWait(pIoBackend->hThreadIo, RT_INDEFINITE_WAIT, NULL);
    RTSemEventDestroy(pIoBackend->EventSem);
    RTCircBufDestroy(pIoBackend->pRequestRing);
    RTMemFree(pIoBackend);

    return VINF_SUCCESS;
}

int VDIoBackendMemTransfer(PVDIOBACKENDMEM pIoBackend, PVDMEMDISK pMemDisk,
                           VDIOTXDIR enmTxDir, uint64_t off, size_t cbTransfer,
                           PRTSGBUF pSgBuf, PFNVDIOCOMPLETE pfnComplete, void *pvUser)
{
    PVDIOBACKENDREQ pReq = NULL;
    PPVDIOBACKENDREQ ppReq = NULL;
    size_t cbData;
    unsigned cSegs = 0;

    LogFlowFunc(("Queuing request\n"));

    if (enmTxDir != VDIOTXDIR_FLUSH)
        RTSgBufSegArrayCreate(pSgBuf, NULL, &cSegs, cbTransfer);

    pReq = (PVDIOBACKENDREQ)RTMemAlloc(RT_UOFFSETOF_DYN(VDIOBACKENDREQ, aSegs[cSegs]));
    if (!pReq)
        return VERR_NO_MEMORY;

    RTCircBufAcquireWriteBlock(pIoBackend->pRequestRing, sizeof(PVDIOBACKENDREQ), (void **)&ppReq, &cbData);
    if (!ppReq)
    {
        RTMemFree(pReq);
        return VERR_NO_MEMORY;
    }

    Assert(cbData == sizeof(PVDIOBACKENDREQ));
    pReq->enmTxDir    = enmTxDir;
    pReq->cbTransfer  = cbTransfer;
    pReq->off         = off;
    pReq->pMemDisk    = pMemDisk;
    pReq->pfnComplete = pfnComplete;
    pReq->pvUser      = pvUser;
    if (enmTxDir != VDIOTXDIR_FLUSH)
    {
        RTSgBufSegArrayCreate(pSgBuf, &pReq->aSegs[0], &cSegs, cbTransfer);
        RTSgBufInit(&pReq->SgBuf, pReq->aSegs, cSegs);
    }

    *ppReq = pReq;
    RTCircBufReleaseWriteBlock(pIoBackend->pRequestRing, sizeof(PVDIOBACKENDREQ));
    uint32_t cReqsWaiting = ASMAtomicIncU32(&pIoBackend->cReqsWaiting);
    if (cReqsWaiting == 1)
        vdIoBackendMemThreadPoke(pIoBackend);

    return VINF_SUCCESS;
}

/**
 * I/O thread for the memory backend.
 *
 * @returns IPRT status code.
 *
 * @param hThread    The thread handle.
 * @param pvUser     Opaque user data.
 */
static DECLCALLBACK(int) vdIoBackendMemThread(RTTHREAD hThread, void *pvUser)
{
    PVDIOBACKENDMEM pIoBackend = (PVDIOBACKENDMEM)pvUser;
    RT_NOREF1(hThread);

    while (pIoBackend->fRunning)
    {
        int rc = RTSemEventWait(pIoBackend->EventSem, RT_INDEFINITE_WAIT);
        if (RT_FAILURE(rc) || !pIoBackend->fRunning)
            break;

        PVDIOBACKENDREQ pReq;
        PPVDIOBACKENDREQ ppReq;
        size_t cbData;
        uint32_t cReqsWaiting = ASMAtomicXchgU32(&pIoBackend->cReqsWaiting, 0);

        while (cReqsWaiting)
        {
            int rcReq = VINF_SUCCESS;

            /* Do we have another request? */
            RTCircBufAcquireReadBlock(pIoBackend->pRequestRing, sizeof(PVDIOBACKENDREQ), (void **)&ppReq, &cbData);
            Assert(!ppReq || cbData == sizeof(PVDIOBACKENDREQ));
            RTCircBufReleaseReadBlock(pIoBackend->pRequestRing, cbData);

            pReq = *ppReq;
            cReqsWaiting--;

            LogFlowFunc(("Processing request\n"));
            switch (pReq->enmTxDir)
            {
                case VDIOTXDIR_READ:
                {
                    rcReq = VDMemDiskRead(pReq->pMemDisk, pReq->off, pReq->cbTransfer, &pReq->SgBuf);
                    break;
                }
                case VDIOTXDIR_WRITE:
                {
                    rcReq = VDMemDiskWrite(pReq->pMemDisk, pReq->off, pReq->cbTransfer, &pReq->SgBuf);
                    break;
                }
                case VDIOTXDIR_FLUSH:
                    break;
                default:
                    AssertMsgFailed(("Invalid TX direction!\n"));
            }

            /* Notify completion. */
            pReq->pfnComplete(pReq->pvUser, rcReq);
            RTMemFree(pReq);
        }
    }

    return VINF_SUCCESS;
}

