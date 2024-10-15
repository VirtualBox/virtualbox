/* $Id: req.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Request packets
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


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/**
 * Allocate a new request from the heap.
 *
 * @returns IPRT status code.
 * @param   enmType         The reques type.
 * @param   fPoolOrQueue    The owner type.
 * @param   pvOwner         The owner.
 * @param   phReq           Where to return the request handle.
 */
DECLHIDDEN(int) rtReqAlloc(RTREQTYPE enmType, bool fPoolOrQueue, void *pvOwner, PRTREQ *phReq)
{
    PRTREQ pReq = (PRTREQ)RTMemAllocZ(sizeof(*pReq));
    if (RT_UNLIKELY(!pReq))
        return VERR_NO_MEMORY;

    /*
     * Create the semaphore used for waiting.
     */
    int rc = RTSemEventCreate(&pReq->EventSem);
    AssertRCReturnStmt(rc, RTMemFree(pReq), rc);

    /*
     * Initialize the packet and return it.
     */
    pReq->u32Magic          = RTREQ_MAGIC;
    pReq->fEventSemClear    = true;
    pReq->fSignalPushBack   = true;
    pReq->fPoolOrQueue      = fPoolOrQueue;
    pReq->iStatusX          = VERR_RT_REQUEST_STATUS_STILL_PENDING;
    pReq->enmState          = RTREQSTATE_ALLOCATED;
    pReq->pNext             = NULL;
    pReq->uOwner.pv         = pvOwner;
    pReq->fFlags            = RTREQFLAGS_IPRT_STATUS;
    pReq->enmType           = enmType;
    pReq->cRefs             = 1;

    *phReq = pReq;
    return VINF_SUCCESS;
}


/**
 * Re-initializes a request when it's being recycled.
 *
 * @returns IRPT status code, the request is freed on failure.
 * @param   pReq                The request.
 * @param   enmType             The request type.
 */
DECLHIDDEN(int) rtReqReInit(PRTREQINT pReq, RTREQTYPE enmType)
{
    Assert(pReq->u32Magic == RTREQ_MAGIC);
    Assert(pReq->enmType  == RTREQTYPE_INVALID);
    Assert(pReq->enmState == RTREQSTATE_FREE);
    Assert(pReq->cRefs    == 0);

    /*
     * Make sure the event sem is not signaled.
     */
    if (!pReq->fEventSemClear)
    {
        int rc = RTSemEventWait(pReq->EventSem, 0);
        if (rc != VINF_SUCCESS && rc != VERR_TIMEOUT)
        {
            /*
             * This shall not happen, but if it does we'll just destroy
             * the semaphore and create a new one.
             */
            AssertMsgFailed(("rc=%Rrc from RTSemEventWait(%#x).\n", rc, pReq->EventSem));
            RTSemEventDestroy(pReq->EventSem);
            rc = RTSemEventCreate(&pReq->EventSem);
            if (RT_FAILURE(rc))
            {
                AssertRC(rc);
                pReq->EventSem = NIL_RTSEMEVENT;
                rtReqFreeIt(pReq);
                return rc;
            }
        }
        pReq->fEventSemClear = true;
    }
    else
        Assert(RTSemEventWait(pReq->EventSem, 0) == VERR_TIMEOUT);

    /*
     * Initialize the packet and return it.
     */
    ASMAtomicWriteNullPtr(&pReq->pNext);
    pReq->iStatusX = VERR_RT_REQUEST_STATUS_STILL_PENDING;
    pReq->enmState = RTREQSTATE_ALLOCATED;
    pReq->fFlags   = RTREQFLAGS_IPRT_STATUS;
    pReq->enmType  = enmType;
    pReq->cRefs    = 1;
    return VINF_SUCCESS;
}


RTDECL(uint32_t) RTReqRetain(PRTREQ hReq)
{
    PRTREQINT pReq = hReq;
    AssertPtrReturn(pReq, UINT32_MAX);
    AssertReturn(pReq->u32Magic == RTREQ_MAGIC, UINT32_MAX);

    return ASMAtomicIncU32(&pReq->cRefs);
}
RT_EXPORT_SYMBOL(RTReqRetain);


/**
 * Frees a request.
 *
 * @param   pReq                The request.
 */
DECLHIDDEN(void) rtReqFreeIt(PRTREQINT pReq)
{
    Assert(pReq->u32Magic == RTREQ_MAGIC);
    Assert(pReq->cRefs == 0);

    pReq->u32Magic     = RTREQ_MAGIC_DEAD;
    RTSemEventDestroy(pReq->EventSem);
    pReq->EventSem     = NIL_RTSEMEVENT;
    RTSemEventMultiDestroy(pReq->hPushBackEvt);
    pReq->hPushBackEvt = NIL_RTSEMEVENTMULTI;
    RTMemFree(pReq);
}


RTDECL(uint32_t) RTReqRelease(PRTREQ hReq)
{
    /*
     * Ignore NULL and validate the request.
     */
    if (!hReq)
        return 0;
    PRTREQINT pReq = hReq;
    AssertPtrReturn(pReq, UINT32_MAX);
    AssertReturn(pReq->u32Magic == RTREQ_MAGIC, UINT32_MAX);

    /*
     * Drop a reference, recycle the request when we reach 0.
     */
    uint32_t cRefs = ASMAtomicDecU32(&pReq->cRefs);
    if (cRefs == 0)
    {
        /*
         * Check packet state.
         */
        RTREQSTATE const enmState = pReq->enmState;
        switch (enmState)
        {
            case RTREQSTATE_ALLOCATED:
            case RTREQSTATE_COMPLETED:
                break;
            default:
                AssertMsgFailedReturn(("Invalid state %d!\n", enmState), 0);
        }

        /*
         * Make it a free packet and put it into one of the free packet lists.
         */
        pReq->enmState = RTREQSTATE_FREE;
        pReq->iStatusX = VERR_RT_REQUEST_STATUS_FREED;
        pReq->enmType  = RTREQTYPE_INVALID;

        bool fRecycled;
        if (pReq->fPoolOrQueue)
            fRecycled = rtReqPoolRecycle(pReq->uOwner.hPool, pReq);
        else
            fRecycled = rtReqQueueRecycle(pReq->uOwner.hQueue, pReq);
        if (!fRecycled)
            rtReqFreeIt(pReq);
    }

    return cRefs;
}
RT_EXPORT_SYMBOL(RTReqRelease);


RTDECL(int) RTReqSubmit(PRTREQ hReq, RTMSINTERVAL cMillies)
{
    LogFlow(("RTReqSubmit: hReq=%p cMillies=%d\n", hReq, cMillies));

    /*
     * Verify the supplied package.
     */
    PRTREQINT pReq = hReq;
    AssertPtrReturn(pReq, VERR_INVALID_HANDLE);
    AssertReturn(pReq->u32Magic == RTREQ_MAGIC, VERR_INVALID_HANDLE);
    AssertMsgReturn(pReq->enmState == RTREQSTATE_ALLOCATED, ("%d\n", pReq->enmState), VERR_RT_REQUEST_STATE);
    AssertMsgReturn(pReq->uOwner.hQueue && !pReq->pNext && pReq->EventSem != NIL_RTSEMEVENT,
                    ("Invalid request package! Anyone cooking their own packages???\n"),
                    VERR_RT_REQUEST_INVALID_PACKAGE);
    AssertMsgReturn(pReq->enmType > RTREQTYPE_INVALID && pReq->enmType < RTREQTYPE_MAX,
                    ("Invalid package type %d valid range %d-%d inclusively. This was verified on alloc too...\n",
                     pReq->enmType, RTREQTYPE_INVALID + 1, RTREQTYPE_MAX - 1),
                    VERR_RT_REQUEST_INVALID_TYPE);

    /*
     * Insert it.  Always grab a reference for the queue (we used to
     * donate the caller's reference in the NO_WAIT case once upon a time).
     */
    pReq->uSubmitNanoTs = RTTimeNanoTS();
    pReq->enmState      = RTREQSTATE_QUEUED;
    unsigned fFlags = ((RTREQ volatile *)pReq)->fFlags;                    /* volatile paranoia */
    RTReqRetain(pReq);

    if (!pReq->fPoolOrQueue)
        rtReqQueueSubmit(pReq->uOwner.hQueue, pReq);
    else
        rtReqPoolSubmit(pReq->uOwner.hPool, pReq);

    /*
     * Wait and return.
     */
    int rc = VINF_SUCCESS;
    if (!(fFlags & RTREQFLAGS_NO_WAIT))
        rc = RTReqWait(pReq, cMillies);

    LogFlow(("RTReqSubmit: returns %Rrc\n", rc));
    return rc;
}
RT_EXPORT_SYMBOL(RTReqSubmit);


RTDECL(int) RTReqWait(PRTREQ hReq, RTMSINTERVAL cMillies)
{
    LogFlow(("RTReqWait: hReq=%p cMillies=%d\n", hReq, cMillies));

    /*
     * Verify the supplied package.
     */
    PRTREQINT pReq = hReq;
    AssertPtrReturn(pReq, VERR_INVALID_HANDLE);
    AssertReturn(pReq->u32Magic == RTREQ_MAGIC, VERR_INVALID_HANDLE);
    RTREQSTATE enmState = pReq->enmState;
    AssertMsgReturn(   enmState == RTREQSTATE_QUEUED
                    || enmState == RTREQSTATE_PROCESSING
                    || enmState == RTREQSTATE_COMPLETED
                    || enmState == RTREQSTATE_CANCELLED,
                    ("Invalid state %d\n", enmState),
                    VERR_RT_REQUEST_STATE);
    AssertMsgReturn(pReq->uOwner.hQueue && pReq->EventSem != NIL_RTSEMEVENT,
                    ("Invalid request package! Anyone cooking their own packages???\n"),
                    VERR_RT_REQUEST_INVALID_PACKAGE);
    AssertMsgReturn(pReq->enmType > RTREQTYPE_INVALID && pReq->enmType < RTREQTYPE_MAX,
                    ("Invalid package type %d valid range %d-%d inclusively. This was verified on alloc too...\n",
                     pReq->enmType, RTREQTYPE_INVALID + 1, RTREQTYPE_MAX - 1),
                    VERR_RT_REQUEST_INVALID_TYPE);

    /*
     * Wait on the package.
     */
    int rc;
    if (cMillies != RT_INDEFINITE_WAIT)
        rc = RTSemEventWait(pReq->EventSem, cMillies);
    else
    {
        do
        {
            rc = RTSemEventWait(pReq->EventSem, RT_INDEFINITE_WAIT);
            Assert(rc != VERR_TIMEOUT);
        } while (pReq->enmState != RTREQSTATE_COMPLETED);
    }
    if (rc == VINF_SUCCESS)
        ASMAtomicWriteBool(&pReq->fEventSemClear, true);
    if (pReq->enmState == RTREQSTATE_COMPLETED)
        rc = VINF_SUCCESS;
    LogFlow(("RTReqWait: returns %Rrc\n", rc));
    Assert(rc != VERR_INTERRUPTED);
    Assert(pReq->cRefs >= 1);
    return rc;
}
RT_EXPORT_SYMBOL(RTReqWait);


RTDECL(int) RTReqCancel(PRTREQ hReq)
{
    LogFlow(("RTReqCancel: hReq=%p\n", hReq));

    /*
     * Verify the supplied package.
     */
    PRTREQINT pReq = hReq;
    AssertPtrReturn(pReq, VERR_INVALID_HANDLE);
    AssertReturn(pReq->u32Magic == RTREQ_MAGIC, VERR_INVALID_HANDLE);
    AssertMsgReturn(pReq->uOwner.hQueue && pReq->EventSem != NIL_RTSEMEVENT,
                    ("Invalid request package! Anyone cooking their own packages???\n"),
                    VERR_RT_REQUEST_INVALID_PACKAGE);
    AssertMsgReturn(pReq->enmType > RTREQTYPE_INVALID && pReq->enmType < RTREQTYPE_MAX,
                    ("Invalid package type %d valid range %d-%d inclusively. This was verified on alloc too...\n",
                     pReq->enmType, RTREQTYPE_INVALID + 1, RTREQTYPE_MAX - 1),
                    VERR_RT_REQUEST_INVALID_TYPE);

    /*
     * Try cancel the request itself by changing its state.
     */
    int rc;
    if (ASMAtomicCmpXchgU32((uint32_t volatile *)&pReq->enmState, RTREQSTATE_CANCELLED, RTREQSTATE_QUEUED))
    {
        if (pReq->fPoolOrQueue)
            rtReqPoolCancel(pReq->uOwner.hPool, pReq);
        rc = VINF_SUCCESS;
    }
    else
    {
        Assert(pReq->enmState == RTREQSTATE_PROCESSING || pReq->enmState == RTREQSTATE_COMPLETED);
        rc = VERR_RT_REQUEST_STATE;
    }

    LogFlow(("RTReqCancel: returns %Rrc\n", rc));
    return rc;
}
RT_EXPORT_SYMBOL(RTReqCancel);


RTDECL(int) RTReqGetStatus(PRTREQ hReq)
{
    PRTREQINT pReq = hReq;
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertReturn(pReq->u32Magic == RTREQ_MAGIC, VERR_INVALID_POINTER);
    return pReq->iStatusX;
}
RT_EXPORT_SYMBOL(RTReqGetStatus);



/**
 * Process one request.
 *
 * @returns IPRT status code.
 *
 * @param   pReq        Request packet to process.
 */
DECLHIDDEN(int) rtReqProcessOne(PRTREQINT pReq)
{
    LogFlow(("rtReqProcessOne: pReq=%p type=%d fFlags=%#x\n", pReq, pReq->enmType, pReq->fFlags));

    /*
     * Try switch the request status to processing.
     */
    int     rcRet = VINF_SUCCESS;           /* the return code of this function. */
    int     rcReq = VERR_NOT_IMPLEMENTED;   /* the request status. */
    if (ASMAtomicCmpXchgU32((uint32_t volatile *)&pReq->enmState, RTREQSTATE_PROCESSING, RTREQSTATE_QUEUED))
    {
        /*
         * Process the request.
         */
        pReq->enmState = RTREQSTATE_PROCESSING;
        switch (pReq->enmType)
        {
            /*
             * A packed down call frame.
             */
            case RTREQTYPE_INTERNAL:
            {
                uintptr_t *pauArgs = &pReq->u.Internal.aArgs[0];
                union
                {
                    PFNRT pfn;
                    DECLCALLBACKMEMBER(int, pfn00,(void));
                    DECLCALLBACKMEMBER(int, pfn01,(uintptr_t));
                    DECLCALLBACKMEMBER(int, pfn02,(uintptr_t, uintptr_t));
                    DECLCALLBACKMEMBER(int, pfn03,(uintptr_t, uintptr_t, uintptr_t));
                    DECLCALLBACKMEMBER(int, pfn04,(uintptr_t, uintptr_t, uintptr_t, uintptr_t));
                    DECLCALLBACKMEMBER(int, pfn05,(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t));
                    DECLCALLBACKMEMBER(int, pfn06,(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t));
                    DECLCALLBACKMEMBER(int, pfn07,(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t));
                    DECLCALLBACKMEMBER(int, pfn08,(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t));
                    DECLCALLBACKMEMBER(int, pfn09,(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t));
                    DECLCALLBACKMEMBER(int, pfn10,(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t));
                    DECLCALLBACKMEMBER(int, pfn11,(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t));
                    DECLCALLBACKMEMBER(int, pfn12,(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t));
                } u;
                u.pfn = pReq->u.Internal.pfn;
#ifndef RT_ARCH_X86
                switch (pReq->u.Internal.cArgs)
                {
                    case 0:  rcRet = u.pfn00(); break;
                    case 1:  rcRet = u.pfn01(pauArgs[0]); break;
                    case 2:  rcRet = u.pfn02(pauArgs[0], pauArgs[1]); break;
                    case 3:  rcRet = u.pfn03(pauArgs[0], pauArgs[1], pauArgs[2]); break;
                    case 4:  rcRet = u.pfn04(pauArgs[0], pauArgs[1], pauArgs[2], pauArgs[3]); break;
                    case 5:  rcRet = u.pfn05(pauArgs[0], pauArgs[1], pauArgs[2], pauArgs[3], pauArgs[4]); break;
                    case 6:  rcRet = u.pfn06(pauArgs[0], pauArgs[1], pauArgs[2], pauArgs[3], pauArgs[4], pauArgs[5]); break;
                    case 7:  rcRet = u.pfn07(pauArgs[0], pauArgs[1], pauArgs[2], pauArgs[3], pauArgs[4], pauArgs[5], pauArgs[6]); break;
                    case 8:  rcRet = u.pfn08(pauArgs[0], pauArgs[1], pauArgs[2], pauArgs[3], pauArgs[4], pauArgs[5], pauArgs[6], pauArgs[7]); break;
                    case 9:  rcRet = u.pfn09(pauArgs[0], pauArgs[1], pauArgs[2], pauArgs[3], pauArgs[4], pauArgs[5], pauArgs[6], pauArgs[7], pauArgs[8]); break;
                    case 10: rcRet = u.pfn10(pauArgs[0], pauArgs[1], pauArgs[2], pauArgs[3], pauArgs[4], pauArgs[5], pauArgs[6], pauArgs[7], pauArgs[8], pauArgs[9]); break;
                    case 11: rcRet = u.pfn11(pauArgs[0], pauArgs[1], pauArgs[2], pauArgs[3], pauArgs[4], pauArgs[5], pauArgs[6], pauArgs[7], pauArgs[8], pauArgs[9], pauArgs[10]); break;
                    case 12: rcRet = u.pfn12(pauArgs[0], pauArgs[1], pauArgs[2], pauArgs[3], pauArgs[4], pauArgs[5], pauArgs[6], pauArgs[7], pauArgs[8], pauArgs[9], pauArgs[10], pauArgs[11]); break;
                    default:
                        AssertReleaseMsgFailed(("cArgs=%d\n", pReq->u.Internal.cArgs));
                        rcRet = rcReq = VERR_INTERNAL_ERROR;
                        break;
                }
#else /* RT_ARCH_X86 */
                size_t cbArgs = pReq->u.Internal.cArgs * sizeof(uintptr_t);
# ifdef __GNUC__
                __asm__ __volatile__("movl  %%esp, %%edx\n\t"
                                     "subl  %2, %%esp\n\t"
                                     "andl  $0xfffffff0, %%esp\n\t"
                                     "shrl  $2, %2\n\t"
                                     "movl  %%esp, %%edi\n\t"
                                     "rep movsl\n\t"
                                     "movl  %%edx, %%edi\n\t"
                                     "call  *%%eax\n\t"
                                     "mov   %%edi, %%esp\n\t"
                                     : "=a" (rcRet),
                                       "=S" (pauArgs),
                                       "=c" (cbArgs)
                                     : "0" (u.pfn),
                                       "1" (pauArgs),
                                       "2" (cbArgs)
                                     : "edi", "edx");
# else
                __asm
                {
                    xor     edx, edx        /* just mess it up. */
                    mov     eax, u.pfn
                    mov     ecx, cbArgs
                    shr     ecx, 2
                    mov     esi, pauArgs
                    mov     ebx, esp
                    sub     esp, cbArgs
                    and     esp, 0xfffffff0
                    mov     edi, esp
                    rep movsd
                    call    eax
                    mov     esp, ebx
                    mov     rcRet, eax
                }
# endif
#endif /* RT_ARCH_X86 */
                if ((pReq->fFlags & (RTREQFLAGS_RETURN_MASK)) == RTREQFLAGS_VOID)
                    rcRet = VINF_SUCCESS;
                rcReq = rcRet;
                break;
            }

            default:
                AssertMsgFailed(("pReq->enmType=%d\n", pReq->enmType));
                rcReq = VERR_NOT_IMPLEMENTED;
                break;
        }
    }
    else
    {
        Assert(pReq->enmState == RTREQSTATE_CANCELLED);
        rcReq = VERR_CANCELLED;
    }

    /*
     * Complete the request and then release our request handle reference.
     */
    pReq->iStatusX = rcReq;
    pReq->enmState = RTREQSTATE_COMPLETED;
    if (pReq->fFlags & RTREQFLAGS_NO_WAIT)
        LogFlow(("rtReqProcessOne: Completed request %p: rcReq=%Rrc rcRet=%Rrc (no wait)\n",
                 pReq, rcReq, rcRet));
    else
    {
        /* Notify the waiting thread. */
        LogFlow(("rtReqProcessOne: Completed request %p: rcReq=%Rrc rcRet=%Rrc - notifying waiting thread\n",
                 pReq, rcReq, rcRet));
        ASMAtomicWriteBool(&pReq->fEventSemClear, false);
        int rc2 = RTSemEventSignal(pReq->EventSem);
        if (rc2 != VINF_SUCCESS)
        {
            AssertRC(rc2);
            rcRet = rc2;
        }
    }
    RTReqRelease(pReq);
    return rcRet;
}

