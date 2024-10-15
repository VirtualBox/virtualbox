/* $Id: ioqueue-stdfile-provider.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - I/O queue, Standard file provider.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_IOQUEUE
#include <iprt/ioqueue.h>

#include <iprt/asm.h>
#include <iprt/errcore.h>
#include <iprt/file.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/thread.h>

#include "internal/ioqueue.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** The I/O queue worker thread needs to wake up the waiting thread when requests completed. */
#define RTIOQUEUE_STDFILE_PROV_STATE_F_EVTWAIT_NEED_WAKEUP      RT_BIT(0)
/** The waiting thread was interrupted by the external wakeup call. */
#define RTIOQUEUE_STDFILE_PROV_STATE_F_EVTWAIT_INTR             RT_BIT(1)
#define RTIOQUEUE_STDFILE_PROV_STATE_F_EVTWAIT_INTR_BIT         1
/** The I/O queue worker thread needs to be woken up to process new requests. */
#define RTIOQUEUE_STDFILE_PROV_STATE_F_WORKER_NEED_WAKEUP       RT_BIT(2)
#define RTIOQUEUE_STDFILE_PROV_STATE_F_WORKER_NEED_WAKEUP_BIT   2


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/


/**
 * Submission queue entry.
 */
typedef struct RTIOQUEUESSQENTRY
{
    /** The file to work on. */
    RTFILE                      hFile;
    /** I/O operation. */
    RTIOQUEUEOP                 enmOp;
    /** Start offset. */
    uint64_t                    off;
    /** Additional request flags. */
    uint32_t                    fReqFlags;
    /** Size of the request. */
    size_t                      cbReq;
    /** Opaque user data passed on completion. */
    void                        *pvUser;
    /** Flag whether this is a S/G or standard request. */
    bool                        fSg;
    /** Type dependent data. */
    union
    {
        /** Pointer to buffer for non S/G requests. */
        void                    *pvBuf;
        /** Pointer to S/G buffer. */
        PCRTSGBUF               pSgBuf;
    } u;
} RTIOQUEUESSQENTRY;
/** Pointer to a submission queue entry. */
typedef RTIOQUEUESSQENTRY *PRTIOQUEUESSQENTRY;
/** Pointer to a constant submission queue entry. */
typedef const RTIOQUEUESSQENTRY *PCRTIOQUEUESSQENTRY;


/**
 * Internal I/O queue provider instance data.
 */
typedef struct RTIOQUEUEPROVINT
{
    /** Size of the submission queue in entries. */
    uint32_t                    cSqEntries;
    /** Size of the completion queue in entries. */
    uint32_t                    cCqEntries;
    /** Pointer to the submission queue base. */
    PRTIOQUEUESSQENTRY          paSqEntryBase;
    /** Submission queue producer index. */
    volatile uint32_t           idxSqProd;
    /** Submission queue producer value for any uncommitted requests. */
    uint32_t                    idxSqProdUncommit;
    /** Submission queue consumer index. */
    volatile uint32_t           idxSqCons;
    /** Pointer to the completion queue base. */
    PRTIOQUEUECEVT              paCqEntryBase;
    /** Completion queue producer index. */
    volatile uint32_t           idxCqProd;
    /** Completion queue consumer index. */
    volatile uint32_t           idxCqCons;
    /** Various state flags for synchronizing the worker thread with other participants. */
    volatile uint32_t           fState;
    /** The worker thread handle. */
    RTTHREAD                    hThrdWork;
    /** Event semaphore the worker thread waits on for work. */
    RTSEMEVENT                  hSemEvtWorker;
    /** Event semaphore the caller waits for completion events. */
    RTSEMEVENT                  hSemEvtWaitEvts;
    /** Flag whether to shutdown the worker thread. */
    volatile bool               fShutdown;
} RTIOQUEUEPROVINT;
/** Pointer to the internal I/O queue provider instance data. */
typedef RTIOQUEUEPROVINT *PRTIOQUEUEPROVINT;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/**
 * Processes the given submission queue entry and reports back the result in the completion queue.
 *
 * @param   pSqEntry            The submission queue entry to process.
 * @param   pCqEntry            The comppletion queue entry to store the result in.
 */
static void rtIoQueueStdFileProv_SqEntryProcess(PCRTIOQUEUESSQENTRY pSqEntry, PRTIOQUEUECEVT pCqEntry)
{
    int rcReq = VINF_SUCCESS;

    switch (pSqEntry->enmOp)
    {
        case RTIOQUEUEOP_READ:
            if (!pSqEntry->fSg)
                rcReq = RTFileReadAt(pSqEntry->hFile, pSqEntry->off, pSqEntry->u.pvBuf, pSqEntry->cbReq, NULL);
            else
            {
                RTSGBUF SgBuf;
                RTSgBufClone(&SgBuf, pSqEntry->u.pSgBuf);
                rcReq = RTFileSgReadAt(pSqEntry->hFile, pSqEntry->off, &SgBuf, pSqEntry->cbReq, NULL);
            }
            break;
        case RTIOQUEUEOP_WRITE:
            if (!pSqEntry->fSg)
                rcReq = RTFileWriteAt(pSqEntry->hFile, pSqEntry->off, pSqEntry->u.pvBuf, pSqEntry->cbReq, NULL);
            else
            {
                RTSGBUF SgBuf;
                RTSgBufClone(&SgBuf, pSqEntry->u.pSgBuf);
                rcReq = RTFileSgWriteAt(pSqEntry->hFile, pSqEntry->off, &SgBuf, pSqEntry->cbReq, NULL);
            }
            break;
        case RTIOQUEUEOP_SYNC:
            rcReq = RTFileFlush(pSqEntry->hFile);
            break;
        default:
            AssertMsgFailedReturnVoid(("Invalid I/O queue operation: %d\n", pSqEntry->enmOp));
    }

    /* Write the result back into the completion queue. */
    pCqEntry->rcReq    = rcReq;
    pCqEntry->pvUser   = pSqEntry->pvUser;
    pCqEntry->cbXfered = RT_SUCCESS(rcReq) ? pSqEntry->cbReq : 0;
}


/**
 * The main I/O queue worker loop which processes the incoming I/O requests.
 */
static DECLCALLBACK(int) rtIoQueueStdFileProv_WorkerLoop(RTTHREAD hThrdSelf, void *pvUser)
{
    PRTIOQUEUEPROVINT pThis = (PRTIOQUEUEPROVINT)pvUser;

    /* Signal that we started up. */
    int rc = RTThreadUserSignal(hThrdSelf);
    AssertRC(rc);

    while (!ASMAtomicReadBool(&pThis->fShutdown))
    {
        /* Wait for some work. */
        ASMAtomicOrU32(&pThis->fState, RTIOQUEUE_STDFILE_PROV_STATE_F_WORKER_NEED_WAKEUP);
        uint32_t idxSqProd = ASMAtomicReadU32(&pThis->idxSqProd);
        uint32_t idxSqCons = ASMAtomicReadU32(&pThis->idxSqCons);
        uint32_t idxCqCons = ASMAtomicReadU32(&pThis->idxCqCons);

        if (idxSqCons == idxSqProd)
        {
            rc = RTSemEventWait(pThis->hSemEvtWorker, RT_INDEFINITE_WAIT);
            AssertRC(rc);

            idxSqProd = ASMAtomicReadU32(&pThis->idxSqProd);
            idxSqCons = ASMAtomicReadU32(&pThis->idxSqCons);
            idxCqCons = ASMAtomicReadU32(&pThis->idxCqCons);
        }

        ASMAtomicBitTestAndClear(&pThis->fState, RTIOQUEUE_STDFILE_PROV_STATE_F_WORKER_NEED_WAKEUP_BIT);

        /* Process all requests. */
        uint32_t cCqFree = 0;
        if (idxCqCons > pThis->idxCqProd)
            cCqFree = pThis->cCqEntries - (pThis->cCqEntries - idxCqCons) - pThis->idxCqProd;
        else
            cCqFree = pThis->cCqEntries - pThis->idxCqProd - idxCqCons;
        do
        {
            while (   idxSqCons != idxSqProd
                   && cCqFree)
            {
                PCRTIOQUEUESSQENTRY pSqEntry = &pThis->paSqEntryBase[idxSqCons];
                PRTIOQUEUECEVT pCqEntry = &pThis->paCqEntryBase[pThis->idxCqProd];

                rtIoQueueStdFileProv_SqEntryProcess(pSqEntry, pCqEntry);
                ASMWriteFence();

                idxSqCons = (idxSqCons + 1) % pThis->cSqEntries;
                cCqFree--;
                pThis->idxCqProd = (pThis->idxCqProd + 1) % pThis->cCqEntries;
                ASMAtomicWriteU32(&pThis->idxSqCons, idxSqCons);
                ASMWriteFence();
                if (ASMAtomicReadU32(&pThis->fState) & RTIOQUEUE_STDFILE_PROV_STATE_F_EVTWAIT_NEED_WAKEUP)
                {
                    rc = RTSemEventSignal(pThis->hSemEvtWaitEvts);
                    AssertRC(rc);
                }
            }

            idxSqProd = ASMAtomicReadU32(&pThis->idxSqProd);
        } while (   idxSqCons != idxSqProd
                 && cCqFree);
    }

    return VINF_SUCCESS;
}


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnIsSupported} */
static DECLCALLBACK(bool) rtIoQueueStdFileProv_IsSupported(void)
{
    /* The common code/public API already checked for the proper handle type. */
    return true;
}


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnQueueInit} */
static DECLCALLBACK(int) rtIoQueueStdFileProv_QueueInit(RTIOQUEUEPROV hIoQueueProv, uint32_t fFlags,
                                                        uint32_t cSqEntries, uint32_t cCqEntries)
{
    RT_NOREF(fFlags);

    PRTIOQUEUEPROVINT pThis = hIoQueueProv;
    int rc = VINF_SUCCESS;

    cSqEntries++;
    cCqEntries++;

    pThis->cSqEntries        = cSqEntries;
    pThis->cCqEntries        = cCqEntries;
    pThis->idxSqProd         = 0;
    pThis->idxSqProdUncommit = 0;
    pThis->idxSqCons         = 0;
    pThis->idxCqProd         = 0;
    pThis->idxCqCons         = 0;
    pThis->fShutdown         = false;
    pThis->fState            = 0;

    pThis->paSqEntryBase = (PRTIOQUEUESSQENTRY)RTMemAllocZ(cSqEntries * sizeof(RTIOQUEUESSQENTRY));
    if (RT_LIKELY(pThis->paSqEntryBase))
    {
        pThis->paCqEntryBase = (PRTIOQUEUECEVT)RTMemAllocZ(cCqEntries * sizeof(RTIOQUEUECEVT));
        if (RT_LIKELY(pThis->paSqEntryBase))
        {
            rc = RTSemEventCreate(&pThis->hSemEvtWorker);
            if (RT_SUCCESS(rc))
            {
                rc = RTSemEventCreate(&pThis->hSemEvtWaitEvts);
                if (RT_SUCCESS(rc))
                {
                    /* Spin up the worker thread. */
                    rc = RTThreadCreate(&pThis->hThrdWork, rtIoQueueStdFileProv_WorkerLoop, pThis, 0, RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE,
                                        "IoQ-StdFile");
                    if (RT_SUCCESS(rc))
                    {
                        rc = RTThreadUserWait(pThis->hThrdWork, 10 * RT_MS_1SEC);
                        AssertRC(rc);

                        return VINF_SUCCESS;
                    }

                    RTSemEventDestroy(pThis->hSemEvtWaitEvts);
                }

                RTSemEventDestroy(pThis->hSemEvtWorker);
            }

            RTMemFree(pThis->paCqEntryBase);
        }
        else
            rc = VERR_NO_MEMORY;

        RTMemFree(pThis->paSqEntryBase);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnQueueDestroy} */
static DECLCALLBACK(void) rtIoQueueStdFileProv_QueueDestroy(RTIOQUEUEPROV hIoQueueProv)
{
    PRTIOQUEUEPROVINT pThis = hIoQueueProv;

    ASMAtomicXchgBool(&pThis->fShutdown, true);
    RTSemEventSignal(pThis->hSemEvtWorker);

    int rc = RTThreadWait(pThis->hThrdWork, 60 * RT_MS_1SEC, NULL);
    AssertRC(rc);

    RTSemEventDestroy(pThis->hSemEvtWaitEvts);
    RTSemEventDestroy(pThis->hSemEvtWorker);
    RTMemFree(pThis->paCqEntryBase);
    RTMemFree(pThis->paSqEntryBase);
    RT_BZERO(pThis, sizeof(*pThis));
}


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnHandleRegister} */
static DECLCALLBACK(int) rtIoQueueStdFileProv_HandleRegister(RTIOQUEUEPROV hIoQueueProv, PCRTHANDLE pHandle)
{
    RT_NOREF(hIoQueueProv, pHandle);

    /* Nothing to do here. */
    return VINF_SUCCESS;
}


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnHandleDeregister} */
static DECLCALLBACK(int) rtIoQueueStdFileProv_HandleDeregister(RTIOQUEUEPROV hIoQueueProv, PCRTHANDLE pHandle)
{
    RT_NOREF(hIoQueueProv, pHandle);

    /* Nothing to do here. */
    return VINF_SUCCESS;
}


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnReqPrepare} */
static DECLCALLBACK(int) rtIoQueueStdFileProv_ReqPrepare(RTIOQUEUEPROV hIoQueueProv, PCRTHANDLE pHandle, RTIOQUEUEOP enmOp,
                                                         uint64_t off, void *pvBuf, size_t cbBuf, uint32_t fReqFlags,
                                                         void *pvUser)
{
    PRTIOQUEUEPROVINT pThis = hIoQueueProv;
    PRTIOQUEUESSQENTRY pSqEntry = &pThis->paSqEntryBase[pThis->idxSqProdUncommit];

    pSqEntry->hFile     = pHandle->u.hFile;
    pSqEntry->enmOp     = enmOp;
    pSqEntry->off       = off;
    pSqEntry->fReqFlags = fReqFlags;
    pSqEntry->cbReq     = cbBuf;
    pSqEntry->pvUser    = pvUser;
    pSqEntry->fSg       = false;
    pSqEntry->u.pvBuf   = pvBuf;

    pThis->idxSqProdUncommit = (pThis->idxSqProdUncommit + 1) % pThis->cSqEntries;
    return VINF_SUCCESS;
}


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnReqPrepareSg} */
static DECLCALLBACK(int) rtIoQueueStdFileProv_ReqPrepareSg(RTIOQUEUEPROV hIoQueueProv, PCRTHANDLE pHandle, RTIOQUEUEOP enmOp,
                                                           uint64_t off, PCRTSGBUF pSgBuf, size_t cbSg, uint32_t fReqFlags,
                                                           void *pvUser)
{
    PRTIOQUEUEPROVINT pThis = hIoQueueProv;
    PRTIOQUEUESSQENTRY pSqEntry = &pThis->paSqEntryBase[pThis->idxSqProdUncommit];

    pSqEntry->hFile     = pHandle->u.hFile;
    pSqEntry->enmOp     = enmOp;
    pSqEntry->off       = off;
    pSqEntry->fReqFlags = fReqFlags;
    pSqEntry->cbReq     = cbSg;
    pSqEntry->pvUser    = pvUser;
    pSqEntry->fSg       = true;
    pSqEntry->u.pSgBuf  = pSgBuf;

    pThis->idxSqProdUncommit = (pThis->idxSqProdUncommit + 1) % pThis->cSqEntries;
    return VINF_SUCCESS;
}


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnCommit} */
static DECLCALLBACK(int) rtIoQueueStdFileProv_Commit(RTIOQUEUEPROV hIoQueueProv, uint32_t *pcReqsCommitted)
{
    PRTIOQUEUEPROVINT pThis = hIoQueueProv;

    if (pThis->idxSqProd > pThis->idxSqProdUncommit)
        *pcReqsCommitted = pThis->cSqEntries - pThis->idxSqProd + pThis->idxSqProdUncommit;
    else
        *pcReqsCommitted = pThis->idxSqProdUncommit - pThis->idxSqProd;

    ASMWriteFence();
    ASMAtomicWriteU32(&pThis->idxSqProd, pThis->idxSqProdUncommit);
    return RTSemEventSignal(pThis->hSemEvtWorker);
}


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnEvtWait} */
static DECLCALLBACK(int) rtIoQueueStdFileProv_EvtWait(RTIOQUEUEPROV hIoQueueProv, PRTIOQUEUECEVT paCEvt, uint32_t cCEvt,
                                                      uint32_t cMinWait, uint32_t *pcCEvt, uint32_t fFlags)
{
    RT_NOREF(fFlags);

    PRTIOQUEUEPROVINT pThis = hIoQueueProv;
    int rc = VINF_SUCCESS;
    uint32_t idxCEvt = 0;

    while (   RT_SUCCESS(rc)
           && cMinWait
           && cCEvt)
    {
        ASMAtomicOrU32(&pThis->fState, RTIOQUEUE_STDFILE_PROV_STATE_F_EVTWAIT_NEED_WAKEUP);
        uint32_t idxCqProd = ASMAtomicReadU32(&pThis->idxCqProd);
        uint32_t idxCqCons = ASMAtomicReadU32(&pThis->idxCqCons);

        if (idxCqCons == idxCqProd)
        {
            rc = RTSemEventWait(pThis->hSemEvtWaitEvts, RT_INDEFINITE_WAIT);
            AssertRC(rc);
            if (ASMAtomicBitTestAndClear(&pThis->fState, RTIOQUEUE_STDFILE_PROV_STATE_F_EVTWAIT_INTR_BIT))
            {
                rc = VERR_INTERRUPTED;
                ASMAtomicBitTestAndClear(&pThis->fState, RTIOQUEUE_STDFILE_PROV_STATE_F_WORKER_NEED_WAKEUP_BIT);
                break;
            }

            idxCqProd = ASMAtomicReadU32(&pThis->idxCqProd);
            idxCqCons = ASMAtomicReadU32(&pThis->idxCqCons);
        }

        ASMAtomicBitTestAndClear(&pThis->fState, RTIOQUEUE_STDFILE_PROV_STATE_F_WORKER_NEED_WAKEUP_BIT);

        /* Process all requests. */
        while (   idxCqCons != idxCqProd
               && cCEvt)
        {
            PRTIOQUEUECEVT pCqEntry = &pThis->paCqEntryBase[idxCqCons];

            paCEvt[idxCEvt].rcReq    = pCqEntry->rcReq;
            paCEvt[idxCEvt].pvUser   = pCqEntry->pvUser;
            paCEvt[idxCEvt].cbXfered = pCqEntry->cbXfered;
            ASMReadFence();

            idxCEvt++;
            cCEvt--;
            cMinWait--;

            idxCqCons = (idxCqCons + 1) % pThis->cCqEntries;
            pThis->idxCqCons = (pThis->idxCqCons + 1) % pThis->cCqEntries;
            ASMWriteFence();
        }
    }

    *pcCEvt = idxCEvt;
    return rc;
}


/** @interface_method_impl{RTIOQUEUEPROVVTABLE,pfnEvtWaitWakeup} */
static DECLCALLBACK(int) rtIoQueueStdFileProv_EvtWaitWakeup(RTIOQUEUEPROV hIoQueueProv)
{
    PRTIOQUEUEPROVINT pThis = hIoQueueProv;

    ASMAtomicOrU32(&pThis->fState, RTIOQUEUE_STDFILE_PROV_STATE_F_EVTWAIT_INTR);
    return RTSemEventSignal(pThis->hSemEvtWaitEvts);
}


/**
 * Standard file I/O queue provider virtual method table.
 */
RT_DECL_DATA_CONST(RTIOQUEUEPROVVTABLE const) g_RTIoQueueStdFileProv =
{
    /** uVersion */
    RTIOQUEUEPROVVTABLE_VERSION,
    /** pszId */
    "StdFile",
    /** cbIoQueueProv */
    sizeof(RTIOQUEUEPROVINT),
    /** enmHnd */
    RTHANDLETYPE_FILE,
    /** fFlags */
    0,
    /** pfnIsSupported */
    rtIoQueueStdFileProv_IsSupported,
    /** pfnQueueInit  */
    rtIoQueueStdFileProv_QueueInit,
    /** pfnQueueDestroy */
    rtIoQueueStdFileProv_QueueDestroy,
    /** pfnHandleRegister */
    rtIoQueueStdFileProv_HandleRegister,
    /** pfnHandleDeregister */
    rtIoQueueStdFileProv_HandleDeregister,
    /** pfnReqPrepare */
    rtIoQueueStdFileProv_ReqPrepare,
    /** pfnReqPrepareSg */
    rtIoQueueStdFileProv_ReqPrepareSg,
    /** pfnCommit */
    rtIoQueueStdFileProv_Commit,
    /** pfnEvtWait */
    rtIoQueueStdFileProv_EvtWait,
    /** pfnEvtWaitWakeup */
    rtIoQueueStdFileProv_EvtWaitWakeup,
    /** uEndMarker */
    RTIOQUEUEPROVVTABLE_VERSION
};

