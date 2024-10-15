/* $Id: reqpool.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Request Pool.
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
#include <iprt/req.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/critsect.h>
#include <iprt/err.h>
#include <iprt/list.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/time.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>

#include "internal/req.h"
#include "internal/magics.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The max number of worker threads. */
#define RTREQPOOL_MAX_THREADS           UINT32_C(16384)
/** The max number of milliseconds to push back. */
#define RTREQPOOL_PUSH_BACK_MAX_MS      RT_MS_1MIN
/** The max number of free requests to keep around. */
#define RTREQPOOL_MAX_FREE_REQUESTS     (RTREQPOOL_MAX_THREADS * 2U)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct RTREQPOOLTHREAD
{
    /** Node in the  RTREQPOOLINT::IdleThreads list. */
    RTLISTNODE              IdleNode;
    /** Node in the  RTREQPOOLINT::WorkerThreads list. */
    RTLISTNODE              ListNode;

    /** The submit timestamp of the pending request. */
    uint64_t                uPendingNanoTs;
    /** The submit timestamp of the request processing. */
    uint64_t                uProcessingNanoTs;
    /** When this CPU went idle the last time. */
    uint64_t                uIdleNanoTs;
    /** The number of requests processed by this thread. */
    uint64_t                cReqProcessed;
    /** Total time the requests processed by this thread took to process. */
    uint64_t                cNsTotalReqProcessing;
    /** Total time the requests processed by this thread had to wait in
     * the queue before being scheduled. */
    uint64_t                cNsTotalReqQueued;
    /** The CPU this was scheduled last time we checked. */
    RTCPUID                 idLastCpu;

    /** The submitter will put an incoming request here when scheduling an idle
     * thread.  */
    PRTREQINT volatile      pTodoReq;
    /** The request the thread is currently processing. */
    PRTREQINT volatile      pPendingReq;

    /** The thread handle. */
    RTTHREAD                hThread;
    /** Nano seconds timestamp representing the birth time of the thread.  */
    uint64_t                uBirthNanoTs;
    /** Pointer to the request thread pool instance the thread is associated
     *  with. */
    struct RTREQPOOLINT    *pPool;
} RTREQPOOLTHREAD;
/** Pointer to a worker thread. */
typedef RTREQPOOLTHREAD *PRTREQPOOLTHREAD;

/**
 * Request thread pool instance data.
 */
typedef struct RTREQPOOLINT
{
    /** Magic value (RTREQPOOL_MAGIC). */
    uint32_t                u32Magic;
    /** The request pool name. */
    char                    szName[12];

    /** @name Config
     * @{  */
    /** The worker thread type. */
    RTTHREADTYPE            enmThreadType;
    /** The work thread flags (RTTHREADFLAGS). */
    uint32_t                fThreadFlags;
    /** The maximum number of worker threads. */
    uint32_t                cMaxThreads;
    /** The minimum number of worker threads. */
    uint32_t                cMinThreads;
    /** The number of milliseconds a thread needs to be idle before it is
     * considered for retirement. */
    uint32_t                cMsMinIdle;
    /** cMsMinIdle in nano seconds. */
    uint64_t                cNsMinIdle;
    /** The idle thread sleep interval in milliseconds. */
    RTMSINTERVAL            cMsIdleSleep;
    /** The number of threads which should be spawned before throttling kicks
     * in. */
    uint32_t                cThreadsPushBackThreshold;
    /** The max number of milliseconds to push back a submitter before creating
     * a new worker thread once the threshold has been reached. */
    uint32_t                cMsMaxPushBack;
    /** The minimum number of milliseconds to push back a submitter before
     * creating a new worker thread once the threshold has been reached. */
    uint32_t                cMsMinPushBack;
    /** The max number of free requests in the recycle LIFO. */
    uint32_t                cMaxFreeRequests;
    /** @}  */

    /** Signaled by terminating worker threads. */
    RTSEMEVENTMULTI         hThreadTermEvt;

    /** Destruction indicator.  The worker threads checks in their loop. */
    bool volatile           fDestructing;

    /** The current submitter push back in milliseconds.
     * This is recalculated when worker threads come and go.  */
    uint32_t                cMsCurPushBack;
    /** The current number of worker threads. */
    uint32_t                cCurThreads;
    /** Statistics: The total number of threads created. */
    uint32_t                cThreadsCreated;
    /** Statistics: The timestamp when the last thread was created. */
    uint64_t                uLastThreadCreateNanoTs;
    /** Linked list of worker threads. */
    RTLISTANCHOR            WorkerThreads;

    /** The number of requests processed and counted in the time totals. */
    uint64_t                cReqProcessed;
    /** Total time the requests processed by this thread took to process. */
    uint64_t                cNsTotalReqProcessing;
    /** Total time the requests processed by this thread had to wait in
     * the queue before being scheduled. */
    uint64_t                cNsTotalReqQueued;

    /** Reference counter. */
    uint32_t volatile       cRefs;
    /** The number of idle thread or threads in the process of becoming
     * idle.  This is increased before the to-be-idle thread tries to enter
     * the critical section and add itself to the list. */
    uint32_t volatile       cIdleThreads;
    /** Linked list of idle threads. */
    RTLISTANCHOR            IdleThreads;

    /** Head of the request FIFO. */
    PRTREQINT               pPendingRequests;
    /** Where to insert the next request. */
    PRTREQINT              *ppPendingRequests;
    /** The number of requests currently pending. */
    uint32_t                cCurPendingRequests;
    /** The number of requests currently being executed. */
    uint32_t volatile       cCurActiveRequests;
    /** The number of requests submitted. */
    uint64_t                cReqSubmitted;
    /** The number of cancelled. */
    uint64_t                cReqCancelled;

    /** Head of the request recycling LIFO. */
    PRTREQINT               pFreeRequests;
    /** The number of requests in the recycling LIFO.  This is read without
     * entering the critical section, thus volatile. */
    uint32_t volatile       cCurFreeRequests;

    /** Critical section serializing access to members of this structure.  */
    RTCRITSECT              CritSect;

} RTREQPOOLINT;


/**
 * Used by exiting thread and the pool destruction code to cancel unexpected
 * requests.
 *
 * @param   pReq                The request.
 */
static void rtReqPoolCancelReq(PRTREQINT pReq)
{
    pReq->uOwner.hPool = NIL_RTREQPOOL; /* force free */
    pReq->enmState     = RTREQSTATE_COMPLETED;
    ASMAtomicWriteS32(&pReq->iStatusX, VERR_CANCELLED);
    if (pReq->hPushBackEvt != NIL_RTSEMEVENTMULTI)
        RTSemEventMultiSignal(pReq->hPushBackEvt);
    RTSemEventSignal(pReq->EventSem);

    RTReqRelease(pReq);
}


/**
 * Recalculate the max pushback interval when adding or removing worker threads.
 *
 * @param   pPool               The pool. cMsCurPushBack will be changed.
 */
static void rtReqPoolRecalcPushBack(PRTREQPOOLINT pPool)
{
    uint32_t const cMsRange = pPool->cMsMaxPushBack - pPool->cMsMinPushBack;
    uint32_t const cSteps   = pPool->cMaxThreads - pPool->cThreadsPushBackThreshold;
    uint32_t const iStep    = pPool->cCurThreads - pPool->cThreadsPushBackThreshold;

    uint32_t cMsCurPushBack;
    if (cSteps == 0 /* disabled */)
        cMsCurPushBack = 0;
    else if ((cMsRange >> 2) >= cSteps)
        cMsCurPushBack = cMsRange / cSteps * iStep;
    else
        cMsCurPushBack = (uint32_t)( (uint64_t)cMsRange * RT_NS_1MS  / cSteps * iStep / RT_NS_1MS );
    cMsCurPushBack += pPool->cMsMinPushBack;

    pPool->cMsCurPushBack = cMsCurPushBack;
}



/**
 * Performs thread exit.
 *
 * @returns Thread termination status code (VINF_SUCCESS).
 * @param   pPool               The pool.
 * @param   pThread             The thread.
 * @param   fLocked             Whether we are inside the critical section
 *                              already.
 */
static int rtReqPoolThreadExit(PRTREQPOOLINT pPool, PRTREQPOOLTHREAD pThread, bool fLocked)
{
    if (!fLocked)
        RTCritSectEnter(&pPool->CritSect);

    /* Get out of the idle list. */
    if (!RTListIsEmpty(&pThread->IdleNode))
    {
        RTListNodeRemove(&pThread->IdleNode);
        Assert(pPool->cIdleThreads > 0);
        ASMAtomicDecU32(&pPool->cIdleThreads);
    }

    /* Get out of the thread list. */
    RTListNodeRemove(&pThread->ListNode);
    Assert(pPool->cCurThreads > 0);
    pPool->cCurThreads--;
    rtReqPoolRecalcPushBack(pPool);

    /* This shouldn't happen... */
    PRTREQINT pReq = pThread->pTodoReq;
    if (pReq)
    {
        AssertFailed();
        pThread->pTodoReq = NULL;
        rtReqPoolCancelReq(pReq);
    }

    /* If we're the last thread terminating, ping the destruction thread before
       we leave the critical section. */
    if (   RTListIsEmpty(&pPool->WorkerThreads)
        && pPool->hThreadTermEvt != NIL_RTSEMEVENT)
        RTSemEventMultiSignal(pPool->hThreadTermEvt);

    RTCritSectLeave(&pPool->CritSect);

    RTMemFree(pThread);
    return VINF_SUCCESS;
}



/**
 * Process one request.
 *
 * @param   pPool               The pool.
 * @param   pThread             The worker thread.
 * @param   pReq                The request to process.
 */
static void rtReqPoolThreadProcessRequest(PRTREQPOOLINT pPool, PRTREQPOOLTHREAD pThread, PRTREQINT pReq)
{
    /*
     * Update thread state.
     */
    pThread->uProcessingNanoTs  = RTTimeNanoTS();
    pThread->uPendingNanoTs     = pReq->uSubmitNanoTs;
    pThread->pPendingReq        = pReq;
    ASMAtomicIncU32(&pPool->cCurActiveRequests);
    Assert(pReq->u32Magic == RTREQ_MAGIC);

    /*
     * Do the actual processing.
     */
    rtReqProcessOne(pReq);

    /*
     * Update thread statistics and state.
     */
    ASMAtomicDecU32(&pPool->cCurActiveRequests);
    pThread->pPendingReq    = NULL;
    uint64_t const uNsTsEnd = RTTimeNanoTS();
    pThread->cNsTotalReqProcessing += uNsTsEnd - pThread->uProcessingNanoTs;
    pThread->cNsTotalReqQueued     += pThread->uProcessingNanoTs - pThread->uPendingNanoTs;
    pThread->cReqProcessed++;
}



/**
 * The Worker Thread Procedure.
 *
 * @returns VINF_SUCCESS.
 * @param   hThreadSelf         The thread handle (unused).
 * @param   pvArg               Pointer to the thread data.
 */
static DECLCALLBACK(int) rtReqPoolThreadProc(RTTHREAD hThreadSelf, void *pvArg)
{
    PRTREQPOOLTHREAD    pThread = (PRTREQPOOLTHREAD)pvArg;
    PRTREQPOOLINT       pPool   = pThread->pPool;

    /*
     * The work loop.
     */
    uint64_t cReqPrevProcessedIdle     = UINT64_MAX;
    uint64_t cReqPrevProcessedStat     = 0;
    uint64_t cNsPrevTotalReqProcessing = 0;
    uint64_t cNsPrevTotalReqQueued     = 0;
    while (!pPool->fDestructing)
    {
        /*
         * Process pending work.
         */

        /* Check if anything is scheduled directly to us. */
        PRTREQINT pReq = ASMAtomicXchgPtrT(&pThread->pTodoReq, NULL, PRTREQINT);
        if (pReq)
        {
            Assert(RTListIsEmpty(&pThread->IdleNode)); /* Must not be in the idle list. */
            rtReqPoolThreadProcessRequest(pPool, pThread, pReq);
            continue;
        }

        ASMAtomicIncU32(&pPool->cIdleThreads);
        RTCritSectEnter(&pPool->CritSect);

        /* Update the global statistics. */
        if (cReqPrevProcessedStat != pThread->cReqProcessed)
        {
            pPool->cReqProcessed         += pThread->cReqProcessed         - cReqPrevProcessedStat;
            cReqPrevProcessedStat         = pThread->cReqProcessed;
            pPool->cNsTotalReqProcessing += pThread->cNsTotalReqProcessing - cNsPrevTotalReqProcessing;
            cNsPrevTotalReqProcessing     = pThread->cNsTotalReqProcessing;
            pPool->cNsTotalReqQueued     += pThread->cNsTotalReqQueued     - cNsPrevTotalReqQueued;
            cNsPrevTotalReqQueued         = pThread->cNsTotalReqQueued;
        }

        /* Recheck the todo request pointer after entering the critsect. */
        pReq = ASMAtomicXchgPtrT(&pThread->pTodoReq, NULL, PRTREQINT);
        if (pReq)
        {
            Assert(RTListIsEmpty(&pThread->IdleNode)); /* Must not be in the idle list. */
            ASMAtomicDecU32(&pPool->cIdleThreads); /* Was already marked as idle above. */
            RTCritSectLeave(&pPool->CritSect);

            rtReqPoolThreadProcessRequest(pPool, pThread, pReq);
            continue;
        }

        /* Any pending requests in the queue? */
        pReq = pPool->pPendingRequests;
        if (pReq)
        {
            pPool->pPendingRequests = pReq->pNext;
            if (pReq->pNext == NULL)
                pPool->ppPendingRequests = &pPool->pPendingRequests;
            Assert(pPool->cCurPendingRequests > 0);
            pPool->cCurPendingRequests--;

            /* Un-idle ourselves and process the request. */
            if (!RTListIsEmpty(&pThread->IdleNode))
            {
                RTListNodeRemove(&pThread->IdleNode);
                RTListInit(&pThread->IdleNode);
                ASMAtomicDecU32(&pPool->cIdleThreads);
            }
            ASMAtomicDecU32(&pPool->cIdleThreads);
            RTCritSectLeave(&pPool->CritSect);

            rtReqPoolThreadProcessRequest(pPool, pThread, pReq);
            continue;
        }

        /*
         * Nothing to do, go idle.
         */
        if (cReqPrevProcessedIdle != pThread->cReqProcessed)
        {
            cReqPrevProcessedIdle = pThread->cReqProcessed;
            pThread->uIdleNanoTs  = RTTimeNanoTS();
        }
        else if (pPool->cCurThreads > pPool->cMinThreads)
        {
            uint64_t cNsIdle = RTTimeNanoTS() - pThread->uIdleNanoTs;
            if (cNsIdle >= pPool->cNsMinIdle)
            {
                ASMAtomicDecU32(&pPool->cIdleThreads); /* Was already marked as idle above. */
                return rtReqPoolThreadExit(pPool, pThread, true /*fLocked*/);
            }
        }

        if (RTListIsEmpty(&pThread->IdleNode))
            RTListPrepend(&pPool->IdleThreads, &pThread->IdleNode);
        else
            ASMAtomicDecU32(&pPool->cIdleThreads);
        RTThreadUserReset(hThreadSelf);
        uint32_t const cMsSleep = pPool->cMsIdleSleep;

        RTCritSectLeave(&pPool->CritSect);

        RTThreadUserWait(hThreadSelf, cMsSleep);
    }

    return rtReqPoolThreadExit(pPool, pThread, false /*fLocked*/);
}


/**
 * Create a new worker thread.
 *
 * @param   pPool               The pool needing new worker thread.
 * @remarks Caller owns the critical section
 */
static void rtReqPoolCreateNewWorker(RTREQPOOL pPool)
{
    PRTREQPOOLTHREAD pThread = (PRTREQPOOLTHREAD)RTMemAllocZ(sizeof(RTREQPOOLTHREAD));
    if (!pThread)
        return;

    pThread->uBirthNanoTs = RTTimeNanoTS();
    pThread->pPool        = pPool;
    pThread->idLastCpu    = NIL_RTCPUID;
    pThread->hThread      = NIL_RTTHREAD;
    RTListInit(&pThread->IdleNode);
    RTListAppend(&pPool->WorkerThreads, &pThread->ListNode);
    pPool->cCurThreads++;
    pPool->cThreadsCreated++;

    int rc = RTThreadCreateF(&pThread->hThread, rtReqPoolThreadProc, pThread, 0 /*default stack size*/,
                             pPool->enmThreadType, pPool->fThreadFlags, "%s%02u", pPool->szName, pPool->cThreadsCreated);
    if (RT_SUCCESS(rc))
        pPool->uLastThreadCreateNanoTs = pThread->uBirthNanoTs;
    else
    {
        pPool->cCurThreads--;
        RTListNodeRemove(&pThread->ListNode);
        RTMemFree(pThread);
    }
}


/**
 * Repel the submitter, giving the worker threads a chance to process the
 * incoming request.
 *
 * @returns Success if a worker picked up the request, failure if not.  The
 *          critical section has been left on success, while we'll be inside it
 *          on failure.
 * @param   pPool               The pool.
 * @param   pReq                The incoming request.
 */
static int rtReqPoolPushBack(PRTREQPOOLINT pPool, PRTREQINT pReq)
{
    /*
     * Lazily create the push back semaphore that we'll be blociing on.
     */
    int rc;
    RTSEMEVENTMULTI hEvt = pReq->hPushBackEvt;
    if (hEvt == NIL_RTSEMEVENTMULTI)
    {
        rc = RTSemEventMultiCreate(&hEvt);
        if (RT_FAILURE(rc))
            return rc;
        pReq->hPushBackEvt = hEvt;
    }

    /*
     * Prepare the request and semaphore.
     */
    uint32_t const cMsTimeout = pPool->cMsCurPushBack;
    pReq->fSignalPushBack = true;
    RTReqRetain(pReq);
    RTSemEventMultiReset(hEvt);

    RTCritSectLeave(&pPool->CritSect);

    /*
     * Block.
     */
    rc = RTSemEventMultiWait(hEvt, cMsTimeout);
    if (RT_FAILURE(rc))
    {
        AssertMsg(rc == VERR_TIMEOUT, ("%Rrc\n", rc));
        RTCritSectEnter(&pPool->CritSect);
    }
    RTReqRelease(pReq);
    return rc;
}



DECLHIDDEN(void) rtReqPoolSubmit(PRTREQPOOLINT pPool, PRTREQINT pReq)
{
    RTCritSectEnter(&pPool->CritSect);

    pPool->cReqSubmitted++;

    /*
     * Try schedule the request to a thread that's currently idle.
     */
    PRTREQPOOLTHREAD pThread = RTListGetFirst(&pPool->IdleThreads, RTREQPOOLTHREAD, IdleNode);
    if (pThread)
    {
        /** @todo CPU affinity??? */
        ASMAtomicWritePtr(&pThread->pTodoReq, pReq);

        RTListNodeRemove(&pThread->IdleNode);
        RTListInit(&pThread->IdleNode);
        ASMAtomicDecU32(&pPool->cIdleThreads);

        RTThreadUserSignal(pThread->hThread);

        RTCritSectLeave(&pPool->CritSect);
        return;
    }
    Assert(RTListIsEmpty(&pPool->IdleThreads));

    /*
     * Put the request in the pending queue.
     */
    pReq->pNext = NULL;
    *pPool->ppPendingRequests = pReq;
    pPool->ppPendingRequests  = (PRTREQINT *)&pReq->pNext;
    pPool->cCurPendingRequests++;

    /*
     * If there is an incoming worker thread already or we've reached the
     * maximum number of worker threads, we're done.
     */
    if (   pPool->cIdleThreads >= pPool->cCurPendingRequests
        || pPool->cCurThreads >= pPool->cMaxThreads)
    {
        RTCritSectLeave(&pPool->CritSect);
        return;
    }

    /*
     * Push back before creating a new worker thread.
     */
    if (   pPool->cCurThreads > pPool->cThreadsPushBackThreshold
        && (RTTimeNanoTS() - pReq->uSubmitNanoTs) / RT_NS_1MS >= pPool->cMsCurPushBack )
    {
        int rc = rtReqPoolPushBack(pPool, pReq);
        if (RT_SUCCESS(rc))
            return;
    }

    /*
     * Create a new thread for processing the request.
     * For simplicity, we don't bother leaving the critical section while doing so.
     */
    rtReqPoolCreateNewWorker(pPool);

    RTCritSectLeave(&pPool->CritSect);
    return;
}


/**
 * Worker for RTReqCancel that looks for the request in the pending list and
 * completes it if found there.
 *
 * @param   pPool               The request thread pool.
 * @param   pReq                The request.
 */
DECLHIDDEN(void) rtReqPoolCancel(PRTREQPOOLINT pPool, PRTREQINT pReq)
{
    RTCritSectEnter(&pPool->CritSect);

    pPool->cReqCancelled++;

    /*
     * Check if the request is in the pending list.
     */
    PRTREQINT pPrev = NULL;
    PRTREQINT pCur  = pPool->pPendingRequests;
    while (pCur)
        if (pCur != pReq)
        {
            pPrev = pCur;
            pCur  = pCur->pNext;
        }
        else
        {
            /*
             * Unlink it and process it.
             */
            if (!pPrev)
            {
                pPool->pPendingRequests = pReq->pNext;
                if (!pReq->pNext)
                    pPool->ppPendingRequests = &pPool->pPendingRequests;
            }
            else
            {
                pPrev->pNext = pReq->pNext;
                if (!pReq->pNext)
                    pPool->ppPendingRequests = (PRTREQINT *)&pPrev->pNext;
            }
            Assert(pPool->cCurPendingRequests > 0);
            pPool->cCurPendingRequests--;

            rtReqProcessOne(pReq);
            break;
        }

    RTCritSectLeave(&pPool->CritSect);
    return;
}


/**
 * Frees a requst.
 *
 * @returns true if recycled, false if not.
 * @param   pPool               The request thread pool.
 * @param   pReq                The request.
 */
DECLHIDDEN(bool) rtReqPoolRecycle(PRTREQPOOLINT pPool, PRTREQINT pReq)
{
    if (   pPool
        && ASMAtomicReadU32(&pPool->cCurFreeRequests) < pPool->cMaxFreeRequests)
    {
        RTCritSectEnter(&pPool->CritSect);
        if (pPool->cCurFreeRequests < pPool->cMaxFreeRequests)
        {
            pReq->pNext = pPool->pFreeRequests;
            pPool->pFreeRequests = pReq;
            ASMAtomicIncU32(&pPool->cCurFreeRequests);

            RTCritSectLeave(&pPool->CritSect);
            return true;
        }

        RTCritSectLeave(&pPool->CritSect);
    }
    return false;
}


RTDECL(int) RTReqPoolCreate(uint32_t cMaxThreads, RTMSINTERVAL cMsMinIdle,
                            uint32_t cThreadsPushBackThreshold, uint32_t cMsMaxPushBack,
                            const char *pszName, PRTREQPOOL phPool)
{
    /*
     * Validate and massage the config.
     */
    if (cMaxThreads == UINT32_MAX)
        cMaxThreads = RTREQPOOL_MAX_THREADS;
    AssertMsgReturn(cMaxThreads > 0 && cMaxThreads <= RTREQPOOL_MAX_THREADS, ("%u\n", cMaxThreads), VERR_OUT_OF_RANGE);
    uint32_t const cMinThreads = cMaxThreads > 2 ? 2 : cMaxThreads - 1;

    if (cThreadsPushBackThreshold == 0)
        cThreadsPushBackThreshold = cMinThreads;
    else if (cThreadsPushBackThreshold == UINT32_MAX)
        cThreadsPushBackThreshold = cMaxThreads;
    AssertMsgReturn(cThreadsPushBackThreshold <= cMaxThreads, ("%u/%u\n", cThreadsPushBackThreshold, cMaxThreads), VERR_OUT_OF_RANGE);

    if (cMsMaxPushBack == UINT32_MAX)
        cMsMaxPushBack = RTREQPOOL_PUSH_BACK_MAX_MS;
    AssertMsgReturn(cMsMaxPushBack <= RTREQPOOL_PUSH_BACK_MAX_MS, ("%llu\n",  cMsMaxPushBack), VERR_OUT_OF_RANGE);
    uint32_t const cMsMinPushBack = cMsMaxPushBack >= 200 ? 100 : cMsMaxPushBack / 2;

    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    size_t cchName = strlen(pszName);
    AssertReturn(cchName > 0, VERR_INVALID_PARAMETER);
    Assert(cchName <= 10);

    AssertPtrReturn(phPool, VERR_INVALID_POINTER);

    /*
     * Create and initialize the pool.
     */
    PRTREQPOOLINT pPool = (PRTREQPOOLINT)RTMemAlloc(sizeof(*pPool));
    if (!pPool)
        return VERR_NO_MEMORY;

    pPool->u32Magic             = RTREQPOOL_MAGIC;
    RTStrCopy(pPool->szName, sizeof(pPool->szName), pszName);

    pPool->enmThreadType        = RTTHREADTYPE_DEFAULT;
    pPool->fThreadFlags         = 0;
    pPool->cMaxThreads          = cMaxThreads;
    pPool->cMinThreads          = cMinThreads;
    pPool->cMsMinIdle           = cMsMinIdle == RT_INDEFINITE_WAIT || cMsMinIdle >= UINT32_MAX ? UINT32_MAX : cMsMinIdle;
    pPool->cNsMinIdle           = pPool->cMsMinIdle == UINT32_MAX ? UINT64_MAX         : cMsMinIdle * RT_NS_1MS_64;
    pPool->cMsIdleSleep         = pPool->cMsMinIdle == UINT32_MAX ? RT_INDEFINITE_WAIT : RT_MAX(RT_MS_1SEC, pPool->cMsMinIdle);
    pPool->cThreadsPushBackThreshold = cThreadsPushBackThreshold;
    pPool->cMsMaxPushBack       = cMsMaxPushBack;
    pPool->cMsMinPushBack       = cMsMinPushBack;
    pPool->cMaxFreeRequests     = cMaxThreads * 2;
    pPool->hThreadTermEvt       = NIL_RTSEMEVENTMULTI;
    pPool->fDestructing         = false;
    pPool->cMsCurPushBack       = 0;
    pPool->cCurThreads          = 0;
    pPool->cThreadsCreated      = 0;
    pPool->uLastThreadCreateNanoTs = 0;
    RTListInit(&pPool->WorkerThreads);
    pPool->cReqProcessed        = 0;
    pPool->cNsTotalReqProcessing= 0;
    pPool->cNsTotalReqQueued    = 0;
    pPool->cRefs                = 1;
    pPool->cIdleThreads         = 0;
    RTListInit(&pPool->IdleThreads);
    pPool->pPendingRequests     = NULL;
    pPool->ppPendingRequests    = &pPool->pPendingRequests;
    pPool->cCurPendingRequests  = 0;
    pPool->cCurActiveRequests   = 0;
    pPool->cReqSubmitted        = 0;
    pPool->cReqCancelled        = 0;
    pPool->pFreeRequests        = NULL;
    pPool->cCurFreeRequests     = 0;

    int rc = RTSemEventMultiCreate(&pPool->hThreadTermEvt);
    if (RT_SUCCESS(rc))
    {
        rc = RTCritSectInit(&pPool->CritSect);
        if (RT_SUCCESS(rc))
        {
            *phPool = pPool;
            return VINF_SUCCESS;
        }

        RTSemEventMultiDestroy(pPool->hThreadTermEvt);
    }
    pPool->u32Magic = RTREQPOOL_MAGIC_DEAD;
    RTMemFree(pPool);
    return rc;
}



RTDECL(int) RTReqPoolSetCfgVar(RTREQPOOL hPool, RTREQPOOLCFGVAR enmVar, uint64_t uValue)
{
    PRTREQPOOLINT pPool = hPool;
    AssertPtrReturn(pPool, VERR_INVALID_HANDLE);
    AssertReturn(pPool->u32Magic == RTREQPOOL_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(enmVar > RTREQPOOLCFGVAR_INVALID && enmVar < RTREQPOOLCFGVAR_END, VERR_INVALID_PARAMETER);

    RTCritSectEnter(&pPool->CritSect);

    bool fWakeUpIdleThreads = false;
    int  rc                 = VINF_SUCCESS;
    switch (enmVar)
    {
        case RTREQPOOLCFGVAR_THREAD_TYPE:
            AssertMsgBreakStmt(uValue > (uint64_t)RTTHREADTYPE_INVALID && uValue < (uint64_t)RTTHREADTYPE_END,
                               ("%llu\n",  uValue), rc = VERR_OUT_OF_RANGE);

            pPool->enmThreadType = (RTTHREADTYPE)uValue;
            break;

        case RTREQPOOLCFGVAR_THREAD_FLAGS:
            AssertMsgBreakStmt(!(uValue & ~(uint64_t)RTTHREADFLAGS_MASK) && !(uValue & RTTHREADFLAGS_WAITABLE),
                               ("%#llx\n",  uValue), rc = VERR_INVALID_FLAGS);

            pPool->fThreadFlags = (uint32_t)uValue;
            break;

        case RTREQPOOLCFGVAR_MIN_THREADS:
            AssertMsgBreakStmt(uValue <= RTREQPOOL_MAX_THREADS,  ("%llu\n",  uValue), rc = VERR_OUT_OF_RANGE);
            fWakeUpIdleThreads = pPool->cMinThreads > (uint32_t)uValue;
            pPool->cMinThreads = (uint32_t)uValue;
            if (pPool->cMinThreads > pPool->cMaxThreads)
                pPool->cMaxThreads = pPool->cMinThreads;
            if (   pPool->cThreadsPushBackThreshold < pPool->cMinThreads
                || pPool->cThreadsPushBackThreshold > pPool->cMaxThreads)
                pPool->cThreadsPushBackThreshold = pPool->cMinThreads + (pPool->cMaxThreads - pPool->cMinThreads) / 2;
            rtReqPoolRecalcPushBack(pPool);
            break;

        case RTREQPOOLCFGVAR_MAX_THREADS:
            AssertMsgBreakStmt(uValue <= RTREQPOOL_MAX_THREADS && uValue >= 1,  ("%llu\n",  uValue), rc = VERR_OUT_OF_RANGE);
            pPool->cMaxThreads = (uint32_t)uValue;
            if (pPool->cMaxThreads < pPool->cMinThreads)
            {
                pPool->cMinThreads = pPool->cMaxThreads;
                fWakeUpIdleThreads = true;
            }
            if (pPool->cMaxThreads < pPool->cThreadsPushBackThreshold)
                pPool->cThreadsPushBackThreshold = pPool->cMinThreads + (pPool->cMaxThreads - pPool->cMinThreads) / 2;
            rtReqPoolRecalcPushBack(pPool);
            break;

        case RTREQPOOLCFGVAR_MS_MIN_IDLE:
            AssertMsgBreakStmt(uValue < UINT32_MAX || uValue == RT_INDEFINITE_WAIT,  ("%llu\n",  uValue), rc = VERR_OUT_OF_RANGE);
            if (uValue < UINT32_MAX && uValue != RT_INDEFINITE_WAIT)
            {
                fWakeUpIdleThreads = pPool->cMsMinIdle != (uint32_t)uValue;
                pPool->cMsMinIdle = (uint32_t)uValue;
                pPool->cNsMinIdle = pPool->cMsMinIdle * RT_NS_1MS_64;
                if (pPool->cMsIdleSleep > pPool->cMsMinIdle)
                    pPool->cMsIdleSleep = RT_MAX(RT_MS_1SEC, pPool->cMsMinIdle);
            }
            else
            {
                pPool->cMsMinIdle   = UINT32_MAX;
                pPool->cNsMinIdle   = UINT64_MAX;
                pPool->cMsIdleSleep = RT_INDEFINITE_WAIT;
            }
            break;

        case RTREQPOOLCFGVAR_MS_IDLE_SLEEP:
            AssertMsgBreakStmt(uValue <= RT_INDEFINITE_WAIT,  ("%llu\n",  uValue), rc = VERR_OUT_OF_RANGE);
            fWakeUpIdleThreads = pPool->cMsMinIdle > (RTMSINTERVAL)uValue;
            pPool->cMsIdleSleep = (RTMSINTERVAL)uValue;
            if (pPool->cMsIdleSleep == RT_INDEFINITE_WAIT)
            {
                pPool->cMsMinIdle = UINT32_MAX;
                pPool->cNsMinIdle = UINT64_MAX;
            }
            break;

        case RTREQPOOLCFGVAR_PUSH_BACK_THRESHOLD:
            if (uValue == UINT64_MAX)
                pPool->cThreadsPushBackThreshold = pPool->cMaxThreads;
            else if (uValue == 0)
                pPool->cThreadsPushBackThreshold = pPool->cMinThreads;
            else
            {
                AssertMsgBreakStmt(uValue <= pPool->cMaxThreads,  ("%llu\n",  uValue), rc = VERR_OUT_OF_RANGE);
                AssertMsgBreakStmt(uValue >= pPool->cMinThreads,  ("%llu\n",  uValue), rc = VERR_OUT_OF_RANGE);
                pPool->cThreadsPushBackThreshold = (uint32_t)uValue;
            }
            break;

        case RTREQPOOLCFGVAR_PUSH_BACK_MIN_MS:
            if (uValue == UINT32_MAX || uValue == UINT64_MAX)
                uValue = RTREQPOOL_PUSH_BACK_MAX_MS;
            else
                AssertMsgBreakStmt(uValue <= RTREQPOOL_PUSH_BACK_MAX_MS,  ("%llu\n",  uValue), rc = VERR_OUT_OF_RANGE);
            pPool->cMsMinPushBack = (uint32_t)uValue;
            if (pPool->cMsMaxPushBack < pPool->cMsMinPushBack)
                pPool->cMsMaxPushBack = pPool->cMsMinPushBack;
            rtReqPoolRecalcPushBack(pPool);
            break;

        case RTREQPOOLCFGVAR_PUSH_BACK_MAX_MS:
            if (uValue == UINT32_MAX || uValue == UINT64_MAX)
                uValue = RTREQPOOL_PUSH_BACK_MAX_MS;
            else
                AssertMsgBreakStmt(uValue <= RTREQPOOL_PUSH_BACK_MAX_MS,  ("%llu\n",  uValue), rc = VERR_OUT_OF_RANGE);
            pPool->cMsMaxPushBack = (uint32_t)uValue;
            if (pPool->cMsMinPushBack < pPool->cMsMaxPushBack)
                pPool->cMsMinPushBack = pPool->cMsMaxPushBack;
            rtReqPoolRecalcPushBack(pPool);
            break;

        case RTREQPOOLCFGVAR_MAX_FREE_REQUESTS:
            if (uValue == UINT64_MAX)
            {
                pPool->cMaxFreeRequests = pPool->cMaxThreads * 2;
                if (pPool->cMaxFreeRequests < 16)
                    pPool->cMaxFreeRequests = 16;
            }
            else
            {
                AssertMsgBreakStmt(uValue <= RTREQPOOL_MAX_FREE_REQUESTS,  ("%llu\n",  uValue), rc = VERR_OUT_OF_RANGE);
                pPool->cMaxFreeRequests = (uint32_t)uValue;
            }

            while (pPool->cCurFreeRequests > pPool->cMaxFreeRequests)
            {
                PRTREQINT pReq = pPool->pFreeRequests;
                pPool->pFreeRequests = pReq->pNext;
                ASMAtomicDecU32(&pPool->cCurFreeRequests);
                rtReqFreeIt(pReq);
            }
            break;

        default:
            AssertFailed();
            rc = VERR_IPE_NOT_REACHED_DEFAULT_CASE;
    }

    /* Wake up all idle threads if required. */
    if (fWakeUpIdleThreads)
    {
        Assert(rc == VINF_SUCCESS);
        PRTREQPOOLTHREAD pThread;
        RTListForEach(&pPool->WorkerThreads, pThread, RTREQPOOLTHREAD, ListNode)
        {
            RTThreadUserSignal(pThread->hThread);
        }
    }

    RTCritSectLeave(&pPool->CritSect);

    return rc;
}
RT_EXPORT_SYMBOL(RTReqPoolSetCfgVar);


RTDECL(uint64_t) RTReqPoolGetCfgVar(RTREQPOOL hPool, RTREQPOOLCFGVAR enmVar)
{
    PRTREQPOOLINT pPool = hPool;
    AssertPtrReturn(pPool, UINT64_MAX);
    AssertReturn(pPool->u32Magic == RTREQPOOL_MAGIC, UINT64_MAX);
    AssertReturn(enmVar > RTREQPOOLCFGVAR_INVALID && enmVar < RTREQPOOLCFGVAR_END, UINT64_MAX);

    RTCritSectEnter(&pPool->CritSect);

    uint64_t u64;
    switch (enmVar)
    {
        case RTREQPOOLCFGVAR_THREAD_TYPE:
            u64 = pPool->enmThreadType;
            break;

        case RTREQPOOLCFGVAR_THREAD_FLAGS:
            u64 = pPool->fThreadFlags;
            break;

        case RTREQPOOLCFGVAR_MIN_THREADS:
            u64 = pPool->cMinThreads;
            break;

        case RTREQPOOLCFGVAR_MAX_THREADS:
            u64 = pPool->cMaxThreads;
            break;

        case RTREQPOOLCFGVAR_MS_MIN_IDLE:
            u64 = pPool->cMsMinIdle;
            break;

        case RTREQPOOLCFGVAR_MS_IDLE_SLEEP:
            u64 = pPool->cMsIdleSleep;
            break;

        case RTREQPOOLCFGVAR_PUSH_BACK_THRESHOLD:
            u64 = pPool->cThreadsPushBackThreshold;
            break;

        case RTREQPOOLCFGVAR_PUSH_BACK_MIN_MS:
            u64 = pPool->cMsMinPushBack;
            break;

        case RTREQPOOLCFGVAR_PUSH_BACK_MAX_MS:
            u64 = pPool->cMsMaxPushBack;
            break;

        case RTREQPOOLCFGVAR_MAX_FREE_REQUESTS:
            u64 = pPool->cMaxFreeRequests;
            break;

        default:
            AssertFailed();
            u64 = UINT64_MAX;
            break;
    }

    RTCritSectLeave(&pPool->CritSect);

    return u64;
}
RT_EXPORT_SYMBOL(RTReqGetQueryCfgVar);


RTDECL(uint64_t) RTReqPoolGetStat(RTREQPOOL hPool, RTREQPOOLSTAT enmStat)
{
    PRTREQPOOLINT pPool = hPool;
    AssertPtrReturn(pPool, UINT64_MAX);
    AssertReturn(pPool->u32Magic == RTREQPOOL_MAGIC, UINT64_MAX);
    AssertReturn(enmStat > RTREQPOOLSTAT_INVALID && enmStat < RTREQPOOLSTAT_END, UINT64_MAX);

    RTCritSectEnter(&pPool->CritSect);

    uint64_t u64;
    switch (enmStat)
    {
        case RTREQPOOLSTAT_THREADS:                     u64 = pPool->cCurThreads; break;
        case RTREQPOOLSTAT_THREADS_CREATED:             u64 = pPool->cThreadsCreated; break;
        case RTREQPOOLSTAT_REQUESTS_PROCESSED:          u64 = pPool->cReqProcessed; break;
        case RTREQPOOLSTAT_REQUESTS_SUBMITTED:          u64 = pPool->cReqSubmitted; break;
        case RTREQPOOLSTAT_REQUESTS_CANCELLED:          u64 = pPool->cReqCancelled; break;
        case RTREQPOOLSTAT_REQUESTS_PENDING:            u64 = pPool->cCurPendingRequests; break;
        case RTREQPOOLSTAT_REQUESTS_ACTIVE:             u64 = pPool->cCurActiveRequests; break;
        case RTREQPOOLSTAT_REQUESTS_FREE:               u64 = pPool->cCurFreeRequests; break;
        case RTREQPOOLSTAT_NS_TOTAL_REQ_PROCESSING:     u64 = pPool->cNsTotalReqProcessing; break;
        case RTREQPOOLSTAT_NS_TOTAL_REQ_QUEUED:         u64 = pPool->cNsTotalReqQueued; break;
        case RTREQPOOLSTAT_NS_AVERAGE_REQ_PROCESSING:   u64 = pPool->cNsTotalReqProcessing / RT_MAX(pPool->cReqProcessed, 1); break;
        case RTREQPOOLSTAT_NS_AVERAGE_REQ_QUEUED:       u64 = pPool->cNsTotalReqQueued / RT_MAX(pPool->cReqProcessed, 1); break;
        default:
            AssertFailed();
            u64 = UINT64_MAX;
            break;
    }

    RTCritSectLeave(&pPool->CritSect);

    return u64;
}
RT_EXPORT_SYMBOL(RTReqPoolGetStat);


RTDECL(uint32_t) RTReqPoolRetain(RTREQPOOL hPool)
{
    PRTREQPOOLINT pPool = hPool;
    AssertPtrReturn(pPool, UINT32_MAX);
    AssertReturn(pPool->u32Magic == RTREQPOOL_MAGIC, UINT32_MAX);

    return ASMAtomicIncU32(&pPool->cRefs);
}
RT_EXPORT_SYMBOL(RTReqPoolRetain);


RTDECL(uint32_t) RTReqPoolRelease(RTREQPOOL hPool)
{
    /*
     * Ignore NULL and validate the request.
     */
    if (!hPool)
        return 0;
    PRTREQPOOLINT pPool = hPool;
    AssertPtrReturn(pPool, UINT32_MAX);
    AssertReturn(pPool->u32Magic == RTREQPOOL_MAGIC, UINT32_MAX);

    /*
     * Drop a reference, free it when it reaches zero.
     */
    uint32_t cRefs = ASMAtomicDecU32(&pPool->cRefs);
    if (cRefs == 0)
    {
        AssertReturn(ASMAtomicCmpXchgU32(&pPool->u32Magic, RTREQPOOL_MAGIC_DEAD, RTREQPOOL_MAGIC), UINT32_MAX);

        RTCritSectEnter(&pPool->CritSect);
#ifdef RT_STRICT
        RTTHREAD const hSelf = RTThreadSelf();
#endif

        /* Indicate to the worker threads that we're shutting down. */
        ASMAtomicWriteBool(&pPool->fDestructing, true);
        PRTREQPOOLTHREAD pThread;
        RTListForEach(&pPool->WorkerThreads, pThread, RTREQPOOLTHREAD, ListNode)
        {
            Assert(pThread->hThread != hSelf);
            RTThreadUserSignal(pThread->hThread);
        }

        /* Cancel pending requests. */
        Assert(!pPool->pPendingRequests);
        while (pPool->pPendingRequests)
        {
            PRTREQINT pReq = pPool->pPendingRequests;
            pPool->pPendingRequests = pReq->pNext;
            rtReqPoolCancelReq(pReq);
        }
        pPool->ppPendingRequests = NULL;
        pPool->cCurPendingRequests = 0;

        /* Wait for the workers to shut down. */
        while (!RTListIsEmpty(&pPool->WorkerThreads))
        {
            RTCritSectLeave(&pPool->CritSect);
            RTSemEventMultiWait(pPool->hThreadTermEvt, RT_MS_1MIN);
            RTCritSectEnter(&pPool->CritSect);
            /** @todo should we wait forever here? */
        }

        /* Free recycled requests. */
        for (;;)
        {
            PRTREQINT pReq = pPool->pFreeRequests;
            if (!pReq)
                break;
            pPool->pFreeRequests = pReq->pNext;
            pPool->cCurFreeRequests--;
            rtReqFreeIt(pReq);
        }

        /* Finally, free the critical section and pool instance. */
        RTSemEventMultiDestroy(pPool->hThreadTermEvt);
        RTCritSectLeave(&pPool->CritSect);
        RTCritSectDelete(&pPool->CritSect);
        RTMemFree(pPool);
    }

    return cRefs;
}
RT_EXPORT_SYMBOL(RTReqPoolRelease);


RTDECL(int) RTReqPoolAlloc(RTREQPOOL hPool, RTREQTYPE enmType, PRTREQ *phReq)
{
    PRTREQPOOLINT pPool = hPool;
    AssertPtrReturn(pPool, VERR_INVALID_HANDLE);
    AssertReturn(pPool->u32Magic == RTREQPOOL_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Try recycle old requests.
     */
    if (ASMAtomicReadU32(&pPool->cCurFreeRequests) > 0)
    {
        RTCritSectEnter(&pPool->CritSect);
        PRTREQINT pReq = pPool->pFreeRequests;
        if (pReq)
        {
            ASMAtomicDecU32(&pPool->cCurFreeRequests);
            pPool->pFreeRequests = pReq->pNext;

            RTCritSectLeave(&pPool->CritSect);

            Assert(pReq->fPoolOrQueue);
            Assert(pReq->uOwner.hPool == pPool);

            int rc = rtReqReInit(pReq, enmType);
            if (RT_SUCCESS(rc))
            {
                *phReq = pReq;
                LogFlow(("RTReqPoolAlloc: returns VINF_SUCCESS *phReq=%p recycled\n", pReq));
                return rc;
            }
        }
        else
            RTCritSectLeave(&pPool->CritSect);
    }

    /*
     * Allocate a new request.
     */
    int rc = rtReqAlloc(enmType, true /*fPoolOrQueue*/, pPool, phReq);
    LogFlow(("RTReqPoolAlloc: returns %Rrc *phReq=%p\n", rc, *phReq));
    return rc;
}
RT_EXPORT_SYMBOL(RTReqPoolAlloc);


RTDECL(int) RTReqPoolCallEx( RTREQPOOL hPool, RTMSINTERVAL cMillies, PRTREQ *phReq, uint32_t fFlags, PFNRT pfnFunction, unsigned cArgs, ...)
{
    va_list va;
    va_start(va, cArgs);
    int rc = RTReqPoolCallExV(hPool, cMillies, phReq, fFlags, pfnFunction, cArgs, va);
    va_end(va);
    return rc;
}
RT_EXPORT_SYMBOL(RTReqPoolCallEx);


RTDECL(int) RTReqPoolCallExV(RTREQPOOL hPool, RTMSINTERVAL cMillies, PRTREQ *phReq, uint32_t fFlags, PFNRT pfnFunction, unsigned cArgs, va_list va)
{
    /*
     * Check input.
     */
    AssertPtrReturn(pfnFunction, VERR_INVALID_POINTER);
    AssertMsgReturn(!((uint32_t)fFlags & ~(uint32_t)(RTREQFLAGS_NO_WAIT | RTREQFLAGS_RETURN_MASK)), ("%#x\n", (uint32_t)fFlags), VERR_INVALID_PARAMETER);
    if (!(fFlags & RTREQFLAGS_NO_WAIT) || phReq)
    {
        AssertPtrReturn(phReq, VERR_INVALID_POINTER);
        *phReq = NIL_RTREQ;
    }

    PRTREQINT pReq = NULL;
    AssertMsgReturn(cArgs * sizeof(uintptr_t) <= sizeof(pReq->u.Internal.aArgs), ("cArgs=%u\n", cArgs), VERR_TOO_MUCH_DATA);

    /*
     * Allocate and initialize the request.
     */
    int rc = RTReqPoolAlloc(hPool, RTREQTYPE_INTERNAL, &pReq);
    if (RT_FAILURE(rc))
        return rc;
    pReq->fFlags           = fFlags;
    pReq->u.Internal.pfn   = pfnFunction;
    pReq->u.Internal.cArgs = cArgs;
    for (unsigned iArg = 0; iArg < cArgs; iArg++)
        pReq->u.Internal.aArgs[iArg] = va_arg(va, uintptr_t);

    /*
     * Submit the request.
     */
    rc = RTReqSubmit(pReq, cMillies);
    if (   rc != VINF_SUCCESS
        && rc != VERR_TIMEOUT)
    {
        Assert(rc != VERR_INTERRUPTED);
        RTReqRelease(pReq);
        pReq = NULL;
    }

    if (phReq)
    {
        *phReq = pReq;
        LogFlow(("RTReqPoolCallExV: returns %Rrc *phReq=%p\n", rc, pReq));
    }
    else
    {
        RTReqRelease(pReq);
        LogFlow(("RTReqPoolCallExV: returns %Rrc\n", rc));
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTReqPoolCallExV);


RTDECL(int) RTReqPoolCallWait(RTREQPOOL hPool, PFNRT pfnFunction, unsigned cArgs, ...)
{
    PRTREQINT pReq;
    va_list   va;
    va_start(va, cArgs);
    int rc = RTReqPoolCallExV(hPool, RT_INDEFINITE_WAIT, &pReq, RTREQFLAGS_IPRT_STATUS,
                              pfnFunction, cArgs, va);
    va_end(va);
    if (RT_SUCCESS(rc))
        rc = pReq->iStatusX;
    RTReqRelease(pReq);
    return rc;
}
RT_EXPORT_SYMBOL(RTReqPoolCallWait);


RTDECL(int) RTReqPoolCallNoWait(RTREQPOOL hPool, PFNRT pfnFunction, unsigned cArgs, ...)
{
    va_list   va;
    va_start(va, cArgs);
    int rc = RTReqPoolCallExV(hPool, 0, NULL, RTREQFLAGS_IPRT_STATUS | RTREQFLAGS_NO_WAIT,
                              pfnFunction, cArgs, va);
    va_end(va);
    return rc;
}
RT_EXPORT_SYMBOL(RTReqPoolCallNoWait);


RTDECL(int) RTReqPoolCallVoidWait(RTREQPOOL hPool, PFNRT pfnFunction, unsigned cArgs, ...)
{
    PRTREQINT pReq;
    va_list   va;
    va_start(va, cArgs);
    int rc = RTReqPoolCallExV(hPool, RT_INDEFINITE_WAIT, &pReq, RTREQFLAGS_VOID,
                              pfnFunction, cArgs, va);
    va_end(va);
    if (RT_SUCCESS(rc))
        rc = pReq->iStatusX;
    RTReqRelease(pReq);
    return rc;
}
RT_EXPORT_SYMBOL(RTReqPoolCallVoidWait);


RTDECL(int) RTReqPoolCallVoidNoWait(RTREQPOOL hPool, PFNRT pfnFunction, unsigned cArgs, ...)
{
    va_list   va;
    va_start(va, cArgs);
    int rc = RTReqPoolCallExV(hPool, 0, NULL, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                              pfnFunction, cArgs, va);
    va_end(va);
    return rc;
}
RT_EXPORT_SYMBOL(RTReqPoolCallVoidNoWait);

