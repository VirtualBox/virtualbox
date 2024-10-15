/* $Id: reqqueue.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Request Queue.
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
#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/time.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>
#include <iprt/log.h>
#include <iprt/mem.h>

#include "internal/req.h"
#include "internal/magics.h"



RTDECL(int) RTReqQueueCreate(RTREQQUEUE *phQueue)
{
    PRTREQQUEUEINT pQueue = (PRTREQQUEUEINT)RTMemAllocZ(sizeof(RTREQQUEUEINT));
    if (!pQueue)
        return VERR_NO_MEMORY;
    int rc = RTSemEventCreate(&pQueue->EventSem);
    if (RT_SUCCESS(rc))
    {
        pQueue->u32Magic = RTREQQUEUE_MAGIC;

        *phQueue = pQueue;
        return VINF_SUCCESS;
    }

    RTMemFree(pQueue);
    return rc;
}
RT_EXPORT_SYMBOL(RTReqQueueCreate);


RTDECL(int) RTReqQueueDestroy(RTREQQUEUE hQueue)
{
    /*
     * Check input.
     */
    if (hQueue == NIL_RTREQQUEUE)
        return VINF_SUCCESS;
    PRTREQQUEUEINT pQueue = hQueue;
    AssertPtrReturn(pQueue, VERR_INVALID_HANDLE);
    AssertReturn(ASMAtomicCmpXchgU32(&pQueue->u32Magic, RTREQQUEUE_MAGIC_DEAD, RTREQQUEUE_MAGIC), VERR_INVALID_HANDLE);

    RTSemEventDestroy(pQueue->EventSem);
    pQueue->EventSem = NIL_RTSEMEVENT;

    for (unsigned i = 0; i < RT_ELEMENTS(pQueue->apReqFree); i++)
    {
        PRTREQ pReq = (PRTREQ)ASMAtomicXchgPtr((void **)&pQueue->apReqFree[i], NULL);
        while (pReq)
        {
            PRTREQ pNext = pReq->pNext;
            rtReqFreeIt(pReq);
            pReq = pNext;
        }
    }

    RTMemFree(pQueue);
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTReqQueueDestroy);


RTDECL(int) RTReqQueueProcess(RTREQQUEUE hQueue, RTMSINTERVAL cMillies)
{
    LogFlow(("RTReqQueueProcess %x\n", hQueue));

    /*
     * Check input.
     */
    PRTREQQUEUEINT pQueue = hQueue;
    AssertPtrReturn(pQueue, VERR_INVALID_HANDLE);
    AssertReturn(pQueue->u32Magic == RTREQQUEUE_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Process loop.  Stop (break) after the first non-VINF_SUCCESS status code.
     */
    int rc = VINF_SUCCESS;
    for (;;)
    {
        /*
         * Get pending requests.
         */
        PRTREQ pReqs = ASMAtomicXchgPtrT(&pQueue->pAlreadyPendingReqs, NULL, PRTREQ);
        if (RT_LIKELY(!pReqs))
        {
            pReqs = ASMAtomicXchgPtrT(&pQueue->pReqs, NULL, PRTREQ);
            if (!pReqs)
            {
                /* We do not adjust cMillies (documented behavior). */
                ASMAtomicWriteBool(&pQueue->fBusy, false); /* this aint 100% perfect, but it's good enough for now... */
                rc = RTSemEventWait(pQueue->EventSem, cMillies);
                if (rc != VINF_SUCCESS)
                    break;
                continue;
            }

            ASMAtomicWriteBool(&pQueue->fBusy, true);

            /*
             * Reverse the list to process it in FIFO order.
             */
            PRTREQ pReq = pReqs;
            if (pReq->pNext)
                Log2(("RTReqQueueProcess: 2+ requests: %p %p %p\n", pReq, pReq->pNext, pReq->pNext->pNext));
            pReqs = NULL;
            while (pReq)
            {
                Assert(pReq->enmState == RTREQSTATE_QUEUED);
                Assert(pReq->uOwner.hQueue == pQueue);
                PRTREQ pCur = pReq;
                pReq = pReq->pNext;
                pCur->pNext = pReqs;
                pReqs = pCur;
            }

        }
        else
            ASMAtomicWriteBool(&pQueue->fBusy, true);

        /*
         * Process the requests.
         */
        while (pReqs)
        {
            /* Unchain the first request and advance the list. */
            PRTREQ pReq = pReqs;
            pReqs = pReqs->pNext;
            pReq->pNext = NULL;

            /* Process the request. */
            rc = rtReqProcessOne(pReq);
            if (rc != VINF_SUCCESS)
            {
                /* Propagate the return code to caller.  If more requests pending, queue them for later. */
                if (pReqs)
                {
                    pReqs = ASMAtomicXchgPtrT(&pQueue->pAlreadyPendingReqs, pReqs, PRTREQ);
                    Assert(!pReqs);
                }
                break;
            }
        }
        if (rc != VINF_SUCCESS)
            break;
    }

    LogFlow(("RTReqQueueProcess: returns %Rrc\n", rc));
    return rc;
}
RT_EXPORT_SYMBOL(RTReqQueueProcess);


RTDECL(int) RTReqQueueCall(RTREQQUEUE hQueue, PRTREQ *ppReq, RTMSINTERVAL cMillies, PFNRT pfnFunction, unsigned cArgs, ...)
{
    va_list va;
    va_start(va, cArgs);
    int rc = RTReqQueueCallV(hQueue, ppReq, cMillies, RTREQFLAGS_IPRT_STATUS, pfnFunction, cArgs, va);
    va_end(va);
    return rc;
}
RT_EXPORT_SYMBOL(RTReqQueueCall);


RTDECL(int) RTReqQueueCallVoid(RTREQQUEUE hQueue, PRTREQ *ppReq, RTMSINTERVAL cMillies, PFNRT pfnFunction, unsigned cArgs, ...)
{
    va_list va;
    va_start(va, cArgs);
    int rc = RTReqQueueCallV(hQueue, ppReq, cMillies, RTREQFLAGS_VOID, pfnFunction, cArgs, va);
    va_end(va);
    return rc;
}
RT_EXPORT_SYMBOL(RTReqQueueCallVoid);


RTDECL(int) RTReqQueueCallEx(RTREQQUEUE hQueue, PRTREQ *ppReq, RTMSINTERVAL cMillies, unsigned fFlags, PFNRT pfnFunction, unsigned cArgs, ...)
{
    va_list va;
    va_start(va, cArgs);
    int rc = RTReqQueueCallV(hQueue, ppReq, cMillies, fFlags, pfnFunction, cArgs, va);
    va_end(va);
    return rc;
}
RT_EXPORT_SYMBOL(RTReqQueueCallEx);


RTDECL(int) RTReqQueueCallV(RTREQQUEUE hQueue, PRTREQ *ppReq, RTMSINTERVAL cMillies, unsigned fFlags, PFNRT pfnFunction, unsigned cArgs, va_list Args)
{
    LogFlow(("RTReqQueueCallV: cMillies=%d fFlags=%#x pfnFunction=%p cArgs=%d\n", cMillies, fFlags, pfnFunction, cArgs));

    /*
     * Check input.
     */
    PRTREQQUEUEINT pQueue = hQueue;
    AssertPtrReturn(pQueue, VERR_INVALID_HANDLE);
    AssertReturn(pQueue->u32Magic == RTREQQUEUE_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pfnFunction, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~(RTREQFLAGS_RETURN_MASK | RTREQFLAGS_NO_WAIT)), VERR_INVALID_PARAMETER);

    if (!(fFlags & RTREQFLAGS_NO_WAIT) || ppReq)
    {
        AssertPtrReturn(ppReq, VERR_INVALID_POINTER);
        *ppReq = NIL_RTREQ;
    }

    PRTREQ pReq = NULL;
    AssertMsgReturn(cArgs <= 9, ("cArgs=%u\n", cArgs), VERR_TOO_MUCH_DATA); /* @bugref{10725} */
    AssertMsgReturn(cArgs * sizeof(uintptr_t) <= sizeof(pReq->u.Internal.aArgs), ("cArgs=%u\n", cArgs), VERR_TOO_MUCH_DATA);

    /*
     * Allocate request
     */
    int rc = RTReqQueueAlloc(pQueue, RTREQTYPE_INTERNAL, &pReq);
    if (rc != VINF_SUCCESS)
        return rc;

    /*
     * Initialize the request data.
     */
    pReq->fFlags         = fFlags;
    pReq->u.Internal.pfn = pfnFunction;
    pReq->u.Internal.cArgs = cArgs;
    for (unsigned iArg = 0; iArg < cArgs; iArg++)
        pReq->u.Internal.aArgs[iArg] = va_arg(Args, uintptr_t);

    /*
     * Queue the request and return.
     */
    rc = RTReqSubmit(pReq, cMillies);
    if (   rc != VINF_SUCCESS
        && rc != VERR_TIMEOUT)
    {
        RTReqRelease(pReq);
        pReq = NULL;
    }
    if (ppReq)
    {
        *ppReq = pReq;
        LogFlow(("RTReqQueueCallV: returns %Rrc *ppReq=%p\n", rc, pReq));
    }
    else
    {
        RTReqRelease(pReq);
        LogFlow(("RTReqQueueCallV: returns %Rrc\n", rc));
    }
    Assert(rc != VERR_INTERRUPTED);
    return rc;
}
RT_EXPORT_SYMBOL(RTReqQueueCallV);


RTDECL(bool) RTReqQueueIsBusy(RTREQQUEUE hQueue)
{
    PRTREQQUEUEINT pQueue = hQueue;
    AssertPtrReturn(pQueue, false);

    if (ASMAtomicReadBool(&pQueue->fBusy))
        return true;
    if (ASMAtomicReadPtrT(&pQueue->pReqs, PRTREQ) != NULL)
        return true;
    if (ASMAtomicReadBool(&pQueue->fBusy))
        return true;
    return false;
}
RT_EXPORT_SYMBOL(RTReqQueueIsBusy);


/**
 * Joins the list pList with whatever is linked up at *pHead.
 */
static void vmr3ReqJoinFreeSub(volatile PRTREQ *ppHead, PRTREQ pList)
{
    for (unsigned cIterations = 0;; cIterations++)
    {
        PRTREQ pHead = ASMAtomicXchgPtrT(ppHead, pList, PRTREQ);
        if (!pHead)
            return;
        PRTREQ pTail = pHead;
        while (pTail->pNext)
            pTail = pTail->pNext;
        pTail->pNext = pList;
        if (ASMAtomicCmpXchgPtr(ppHead, pHead, pList))
            return;
        pTail->pNext = NULL;
        if (ASMAtomicCmpXchgPtr(ppHead, pHead, NULL))
            return;
        pList = pHead;
        Assert(cIterations != 32);
        Assert(cIterations != 64);
    }
}


/**
 * Joins the list pList with whatever is linked up at *pHead.
 */
static void vmr3ReqJoinFree(PRTREQQUEUEINT pQueue, PRTREQ pList)
{
    /*
     * Split the list if it's too long.
     */
    unsigned cReqs = 1;
    PRTREQ pTail = pList;
    while (pTail->pNext)
    {
        if (cReqs++ > 25)
        {
            const uint32_t i = pQueue->iReqFree;
            vmr3ReqJoinFreeSub(&pQueue->apReqFree[(i + 2) % RT_ELEMENTS(pQueue->apReqFree)], pTail->pNext);

            pTail->pNext = NULL;
            vmr3ReqJoinFreeSub(&pQueue->apReqFree[(i + 2 + (i == pQueue->iReqFree)) % RT_ELEMENTS(pQueue->apReqFree)], pTail->pNext);
            return;
        }
        pTail = pTail->pNext;
    }
    vmr3ReqJoinFreeSub(&pQueue->apReqFree[(pQueue->iReqFree + 2) % RT_ELEMENTS(pQueue->apReqFree)], pList);
}


RTDECL(int) RTReqQueueAlloc(RTREQQUEUE hQueue, RTREQTYPE enmType, PRTREQ *phReq)
{
    /*
     * Validate input.
     */
    PRTREQQUEUEINT pQueue = hQueue;
    AssertPtrReturn(pQueue, VERR_INVALID_HANDLE);
    AssertReturn(pQueue->u32Magic == RTREQQUEUE_MAGIC, VERR_INVALID_HANDLE);
    AssertMsgReturn(enmType > RTREQTYPE_INVALID && enmType < RTREQTYPE_MAX, ("%d\n", enmType), VERR_RT_REQUEST_INVALID_TYPE);

    /*
     * Try get a recycled packet.
     *
     * While this could all be solved with a single list with a lock, it's a sport
     * of mine to avoid locks.
     */
    int cTries = RT_ELEMENTS(pQueue->apReqFree) * 2;
    while (--cTries >= 0)
    {
        PRTREQ volatile *ppHead = &pQueue->apReqFree[ASMAtomicIncU32(&pQueue->iReqFree) % RT_ELEMENTS(pQueue->apReqFree)];
        PRTREQ pReq = ASMAtomicXchgPtrT(ppHead, NULL, PRTREQ);
        if (pReq)
        {
            PRTREQ pNext = pReq->pNext;
            if (    pNext
                &&  !ASMAtomicCmpXchgPtr(ppHead, pNext, NULL))
                vmr3ReqJoinFree(pQueue, pReq->pNext);
            ASMAtomicDecU32(&pQueue->cReqFree);

            Assert(pReq->uOwner.hQueue == pQueue);
            Assert(!pReq->fPoolOrQueue);

            int rc = rtReqReInit(pReq, enmType);
            if (RT_SUCCESS(rc))
            {
                *phReq = pReq;
                LogFlow(("RTReqQueueAlloc: returns VINF_SUCCESS *phReq=%p recycled\n", pReq));
                return VINF_SUCCESS;
            }
        }
    }

    /*
     * Ok, allocate a new one.
     */
    int rc = rtReqAlloc(enmType, false /*fPoolOrQueue*/, pQueue, phReq);
    LogFlow(("RTReqQueueAlloc: returns %Rrc *phReq=%p\n", rc, *phReq));
    return rc;
}
RT_EXPORT_SYMBOL(RTReqQueueAlloc);


/**
 * Recycles a requst.
 *
 * @returns true if recycled, false if it should be freed.
 * @param   pQueue              The queue.
 * @param   pReq                The request.
 */
DECLHIDDEN(bool) rtReqQueueRecycle(PRTREQQUEUEINT pQueue, PRTREQINT pReq)
{
    if (   !pQueue
        || pQueue->cReqFree >= 128)
        return false;

    ASMAtomicIncU32(&pQueue->cReqFree);
    PRTREQ volatile *ppHead = &pQueue->apReqFree[ASMAtomicIncU32(&pQueue->iReqFree) % RT_ELEMENTS(pQueue->apReqFree)];
    PRTREQ pNext;
    do
    {
        pNext = *ppHead;
        ASMAtomicWritePtr(&pReq->pNext, pNext);
    } while (!ASMAtomicCmpXchgPtr(ppHead, pReq, pNext));

    return true;
}


/**
 * Submits a request to the queue.
 *
 * @param   pQueue              The queue.
 * @param   pReq                The request.
 */
DECLHIDDEN(void) rtReqQueueSubmit(PRTREQQUEUEINT pQueue, PRTREQINT pReq)
{
    PRTREQ pNext;
    do
    {
        pNext = pQueue->pReqs;
        pReq->pNext = pNext;
        ASMAtomicWriteBool(&pQueue->fBusy, true);
    } while (!ASMAtomicCmpXchgPtr(&pQueue->pReqs, pReq, pNext));

    /*
     * Notify queue thread.
     */
    RTSemEventSignal(pQueue->EventSem);
}

