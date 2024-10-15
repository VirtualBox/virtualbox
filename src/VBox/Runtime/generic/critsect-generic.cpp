/* $Id: critsect-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Critical Section, Generic.
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
#define RTCRITSECT_WITHOUT_REMAPPING
#include <iprt/critsect.h>
#include "internal/iprt.h"

#include <iprt/semaphore.h>
#include <iprt/thread.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/err.h>
#include "internal/thread.h"
#include "internal/strict.h"

/* Two issues here, (1) the tracepoint generator uses IPRT, and (2) only one .d
   file per module. */
#ifdef IPRT_WITH_DTRACE
# include IPRT_DTRACE_INCLUDE
# ifdef IPRT_DTRACE_PREFIX
#  define IPRT_CRITSECT_ENTERED  RT_CONCAT(IPRT_DTRACE_PREFIX,IPRT_CRITSECT_ENTERED)
#  define IPRT_CRITSECT_LEAVING  RT_CONCAT(IPRT_DTRACE_PREFIX,IPRT_CRITSECT_LEAVING)
#  define IPRT_CRITSECT_BUSY     RT_CONCAT(IPRT_DTRACE_PREFIX,IPRT_CRITSECT_BUSY)
#  define IPRT_CRITSECT_WAITING  RT_CONCAT(IPRT_DTRACE_PREFIX,IPRT_CRITSECT_WAITING)
# endif
#else
# define IPRT_CRITSECT_ENTERED(a_pvCritSect, a_pszName, a_cLockers, a_cNestings)            do {} while (0)
# define IPRT_CRITSECT_LEAVING(a_pvCritSect, a_pszName, a_cLockers, a_cNestings)            do {} while (0)
# define IPRT_CRITSECT_BUSY(   a_pvCritSect, a_pszName, a_cLockers, a_pvNativeOwnerThread)  do {} while (0)
# define IPRT_CRITSECT_WAITING(a_pvCritSect, a_pszName, a_cLockers, a_pvNativeOwnerThread)  do {} while (0)
#endif



RTDECL(int) RTCritSectInit(PRTCRITSECT pCritSect)
{
    return RTCritSectInitEx(pCritSect, 0, NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE, "RTCritSect");
}
RT_EXPORT_SYMBOL(RTCritSectInit);


RTDECL(int) RTCritSectInitNamed(PRTCRITSECT pCritSect, const char *pszName)
{
    return RTCritSectInitEx(pCritSect, 0, NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE, "%s", pszName);
}
RT_EXPORT_SYMBOL(RTCritSectInitNamed);


RTDECL(int) RTCritSectInitEx(PRTCRITSECT pCritSect, uint32_t fFlags, RTLOCKVALCLASS hClass, uint32_t uSubClass,
                             const char *pszNameFmt, ...)
{
    AssertReturn(!(fFlags & ~(RTCRITSECT_FLAGS_NO_NESTING | RTCRITSECT_FLAGS_NO_LOCK_VAL | RTCRITSECT_FLAGS_BOOTSTRAP_HACK | RTCRITSECT_FLAGS_NOP)),
                 VERR_INVALID_PARAMETER);
    RT_NOREF_PV(hClass); RT_NOREF_PV(uSubClass); RT_NOREF_PV(pszNameFmt);

    /*
     * Initialize the structure and
     */
    pCritSect->u32Magic             = RTCRITSECT_MAGIC;
#ifdef IN_RING0
    pCritSect->fFlags               = fFlags | RTCRITSECT_FLAGS_RING0;
#else
    pCritSect->fFlags               = fFlags & ~RTCRITSECT_FLAGS_RING0;
#endif
    pCritSect->cNestings            = 0;
    pCritSect->cLockers             = -1;
    pCritSect->NativeThreadOwner    = NIL_RTNATIVETHREAD;
    pCritSect->pValidatorRec        = NULL;
    int rc = VINF_SUCCESS;
#ifdef RTCRITSECT_STRICT
    if (!(fFlags & (RTCRITSECT_FLAGS_BOOTSTRAP_HACK | RTCRITSECT_FLAGS_NOP)))
    {
        if (!pszNameFmt)
        {
            static uint32_t volatile s_iCritSectAnon = 0;
            rc = RTLockValidatorRecExclCreate(&pCritSect->pValidatorRec, hClass, uSubClass, pCritSect,
                                              !(fFlags & RTCRITSECT_FLAGS_NO_LOCK_VAL),
                                              "RTCritSect-%u", ASMAtomicIncU32(&s_iCritSectAnon) - 1);
        }
        else
        {
            va_list va;
            va_start(va, pszNameFmt);
            rc = RTLockValidatorRecExclCreateV(&pCritSect->pValidatorRec, hClass, uSubClass, pCritSect,
                                               !(fFlags & RTCRITSECT_FLAGS_NO_LOCK_VAL), pszNameFmt, va);
            va_end(va);
        }
    }
#endif
    if (RT_SUCCESS(rc))
    {
#ifdef IN_RING0
        rc = RTSemEventCreate(&pCritSect->EventSem);

#else
        rc = RTSemEventCreateEx(&pCritSect->EventSem,
                                fFlags & RTCRITSECT_FLAGS_BOOTSTRAP_HACK
                                ? RTSEMEVENT_FLAGS_NO_LOCK_VAL | RTSEMEVENT_FLAGS_BOOTSTRAP_HACK
                                : RTSEMEVENT_FLAGS_NO_LOCK_VAL,
                                NIL_RTLOCKVALCLASS,
                                NULL);
#endif
        if (RT_SUCCESS(rc))
            return VINF_SUCCESS;
#ifdef RTCRITSECT_STRICT
        RTLockValidatorRecExclDestroy(&pCritSect->pValidatorRec);
#endif
    }

    AssertRC(rc);
    pCritSect->EventSem = NULL;
    pCritSect->u32Magic = (uint32_t)rc;
    return rc;
}
RT_EXPORT_SYMBOL(RTCritSectInitEx);


RTDECL(uint32_t) RTCritSectSetSubClass(PRTCRITSECT pCritSect, uint32_t uSubClass)
{
# ifdef RTCRITSECT_STRICT
    AssertPtrReturn(pCritSect, RTLOCKVAL_SUB_CLASS_INVALID);
    AssertReturn(pCritSect->u32Magic == RTCRITSECT_MAGIC, RTLOCKVAL_SUB_CLASS_INVALID);
    AssertReturn(!(pCritSect->fFlags & RTCRITSECT_FLAGS_NOP), RTLOCKVAL_SUB_CLASS_INVALID);
    return RTLockValidatorRecExclSetSubClass(pCritSect->pValidatorRec, uSubClass);
# else
    RT_NOREF_PV(pCritSect); RT_NOREF_PV(uSubClass);
    return RTLOCKVAL_SUB_CLASS_INVALID;
# endif
}


DECL_FORCE_INLINE(int) rtCritSectTryEnter(PRTCRITSECT pCritSect, PCRTLOCKVALSRCPOS pSrcPos)
{
    Assert(pCritSect);
    Assert(pCritSect->u32Magic == RTCRITSECT_MAGIC);
    /*AssertReturn(pCritSect->u32Magic == RTCRITSECT_MAGIC, VERR_SEM_DESTROYED);*/
#ifdef IN_RING0
    Assert(pCritSect->fFlags & RTCRITSECT_FLAGS_RING0);
#else
    Assert(!(pCritSect->fFlags & RTCRITSECT_FLAGS_RING0));
#endif
    RT_NOREF_PV(pSrcPos);

    /*
     * Return straight away if NOP.
     */
    if (pCritSect->fFlags & RTCRITSECT_FLAGS_NOP)
        return VINF_SUCCESS;

    /*
     * Try take the lock. (cLockers is -1 if it's free)
     */
    RTNATIVETHREAD NativeThreadSelf = RTThreadNativeSelf();
    if (!ASMAtomicCmpXchgS32(&pCritSect->cLockers, 0, -1))
    {
        /*
         * Somebody is owning it (or will be soon). Perhaps it's us?
         */
        if (pCritSect->NativeThreadOwner == NativeThreadSelf)
        {
            if (!(pCritSect->fFlags & RTCRITSECT_FLAGS_NO_NESTING))
            {
#ifdef RTCRITSECT_STRICT
                int rc9 = RTLockValidatorRecExclRecursion(pCritSect->pValidatorRec, pSrcPos);
                if (RT_FAILURE(rc9))
                    return rc9;
#endif
                int32_t cLockers = ASMAtomicIncS32(&pCritSect->cLockers); NOREF(cLockers);
                pCritSect->cNestings++;
                IPRT_CRITSECT_ENTERED(pCritSect, NULL, cLockers, pCritSect->cNestings);
                return VINF_SUCCESS;
            }
            AssertMsgFailed(("Nested entry of critsect %p\n", pCritSect));
            return VERR_SEM_NESTED;
        }
        IPRT_CRITSECT_BUSY(pCritSect, NULL, pCritSect->cLockers, (void *)pCritSect->NativeThreadOwner);
        return VERR_SEM_BUSY;
    }

    /*
     * First time
     */
    pCritSect->cNestings = 1;
    ASMAtomicWriteHandle(&pCritSect->NativeThreadOwner, NativeThreadSelf);
#ifdef RTCRITSECT_STRICT
    RTLockValidatorRecExclSetOwner(pCritSect->pValidatorRec, NIL_RTTHREAD, pSrcPos, true);
#endif
    IPRT_CRITSECT_ENTERED(pCritSect, NULL, 0, 1);

    return VINF_SUCCESS;
}


RTDECL(int) RTCritSectTryEnter(PRTCRITSECT pCritSect)
{
#ifndef RTCRTISECT_STRICT
    return rtCritSectTryEnter(pCritSect, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtCritSectTryEnter(pCritSect, &SrcPos);
#endif
}
RT_EXPORT_SYMBOL(RTCritSectTryEnter);


RTDECL(int) RTCritSectTryEnterDebug(PRTCRITSECT pCritSect, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtCritSectTryEnter(pCritSect, &SrcPos);
}
RT_EXPORT_SYMBOL(RTCritSectTryEnterDebug);


DECL_FORCE_INLINE(int) rtCritSectEnter(PRTCRITSECT pCritSect, PCRTLOCKVALSRCPOS pSrcPos)
{
    AssertPtr(pCritSect);
    AssertReturn(pCritSect->u32Magic == RTCRITSECT_MAGIC, VERR_SEM_DESTROYED);
#ifdef IN_RING0
    Assert(pCritSect->fFlags & RTCRITSECT_FLAGS_RING0);
#else
    Assert(!(pCritSect->fFlags & RTCRITSECT_FLAGS_RING0));
#endif
    RT_NOREF_PV(pSrcPos);

    /*
     * Return straight away if NOP.
     */
    if (pCritSect->fFlags & RTCRITSECT_FLAGS_NOP)
        return VINF_SUCCESS;

    /*
     * How is calling and is the order right?
     */
    RTNATIVETHREAD  NativeThreadSelf = RTThreadNativeSelf();
#ifdef RTCRITSECT_STRICT
    RTTHREAD        hThreadSelf = pCritSect->pValidatorRec
                                ? RTThreadSelfAutoAdopt()
                                : RTThreadSelf();
    int             rc9;
    if (pCritSect->pValidatorRec) /* (bootstap) */
    {
         rc9 = RTLockValidatorRecExclCheckOrder(pCritSect->pValidatorRec, hThreadSelf, pSrcPos, RT_INDEFINITE_WAIT);
         if (RT_FAILURE(rc9))
             return rc9;
    }
#endif

    /*
     * Increment the waiter counter.
     * This becomes 0 when the section is free.
     */
    int32_t cLockers = ASMAtomicIncS32(&pCritSect->cLockers);
    if (cLockers > 0)
    {
        /*
         * Nested?
         */
        if (pCritSect->NativeThreadOwner == NativeThreadSelf)
        {
            if (!(pCritSect->fFlags & RTCRITSECT_FLAGS_NO_NESTING))
            {
#ifdef RTCRITSECT_STRICT
                rc9 = RTLockValidatorRecExclRecursion(pCritSect->pValidatorRec, pSrcPos);
                if (RT_FAILURE(rc9))
                {
                    ASMAtomicDecS32(&pCritSect->cLockers);
                    return rc9;
                }
#endif
                pCritSect->cNestings++;
                IPRT_CRITSECT_ENTERED(pCritSect, NULL, cLockers, pCritSect->cNestings);
                return VINF_SUCCESS;
            }

            AssertBreakpoint(); /* don't do normal assertion here, the logger uses this code too. */
            ASMAtomicDecS32(&pCritSect->cLockers);
            return VERR_SEM_NESTED;
        }

        /*
         * Wait for the current owner to release it.
         */
        IPRT_CRITSECT_WAITING(pCritSect, NULL, cLockers, (void *)pCritSect->NativeThreadOwner);
#if !defined(RTCRITSECT_STRICT) && defined(IN_RING3)
        RTTHREAD hThreadSelf = RTThreadSelf();
#endif
        for (;;)
        {
#ifdef RTCRITSECT_STRICT
            rc9 = RTLockValidatorRecExclCheckBlocking(pCritSect->pValidatorRec, hThreadSelf, pSrcPos,
                                                      !(pCritSect->fFlags & RTCRITSECT_FLAGS_NO_NESTING),
                                                      RT_INDEFINITE_WAIT, RTTHREADSTATE_CRITSECT, false);
            if (RT_FAILURE(rc9))
            {
                ASMAtomicDecS32(&pCritSect->cLockers);
                return rc9;
            }
#elif defined(IN_RING3)
            RTThreadBlocking(hThreadSelf, RTTHREADSTATE_CRITSECT, false);
#endif
            int rc = RTSemEventWait(pCritSect->EventSem, RT_INDEFINITE_WAIT);
#ifdef IN_RING3
            RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_CRITSECT);
#endif

            if (pCritSect->u32Magic != RTCRITSECT_MAGIC)
                return VERR_SEM_DESTROYED;
            if (rc == VINF_SUCCESS)
                break;
            AssertMsg(rc == VERR_TIMEOUT || rc == VERR_INTERRUPTED, ("rc=%Rrc\n", rc));
        }
        AssertMsg(pCritSect->NativeThreadOwner == NIL_RTNATIVETHREAD, ("pCritSect->NativeThreadOwner=%p\n", pCritSect->NativeThreadOwner));
    }

    /*
     * First time
     */
    pCritSect->cNestings = 1;
    ASMAtomicWriteHandle(&pCritSect->NativeThreadOwner, NativeThreadSelf);
#ifdef RTCRITSECT_STRICT
    RTLockValidatorRecExclSetOwner(pCritSect->pValidatorRec, hThreadSelf, pSrcPos, true);
#endif
    IPRT_CRITSECT_ENTERED(pCritSect, NULL, 0, 1);

    return VINF_SUCCESS;
}


RTDECL(int) RTCritSectEnter(PRTCRITSECT pCritSect)
{
#ifndef RTCRITSECT_STRICT
    return rtCritSectEnter(pCritSect, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtCritSectEnter(pCritSect, &SrcPos);
#endif
}
RT_EXPORT_SYMBOL(RTCritSectEnter);


RTDECL(int) RTCritSectEnterDebug(PRTCRITSECT pCritSect, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtCritSectEnter(pCritSect, &SrcPos);
}
RT_EXPORT_SYMBOL(RTCritSectEnterDebug);


RTDECL(int) RTCritSectLeave(PRTCRITSECT pCritSect)
{
    /*
     * Assert sanity and check for NOP.
     */
    Assert(pCritSect);
    Assert(pCritSect->u32Magic == RTCRITSECT_MAGIC);
#ifdef IN_RING0
    Assert(pCritSect->fFlags & RTCRITSECT_FLAGS_RING0);
#else
    Assert(!(pCritSect->fFlags & RTCRITSECT_FLAGS_RING0));
#endif
    if (pCritSect->fFlags & RTCRITSECT_FLAGS_NOP)
        return VINF_SUCCESS;

    /*
     * Assert ownership and so on.
     */
    Assert(pCritSect->cNestings > 0);
    Assert(pCritSect->cLockers >= 0);
    Assert(pCritSect->NativeThreadOwner == RTThreadNativeSelf());

#ifdef RTCRITSECT_STRICT
    int rc9 = RTLockValidatorRecExclReleaseOwner(pCritSect->pValidatorRec, pCritSect->cNestings == 1);
    if (RT_FAILURE(rc9))
        return rc9;
#endif

    /*
     * Decrement nestings, if <= 0 when we'll release the critsec.
     */
    uint32_t cNestings = --pCritSect->cNestings;
    IPRT_CRITSECT_LEAVING(pCritSect, NULL, ASMAtomicUoReadS32(&pCritSect->cLockers) - 1, cNestings);
    if (cNestings > 0)
        ASMAtomicDecS32(&pCritSect->cLockers);
    else
    {
        /*
         * Set owner to zero.
         * Decrement waiters, if >= 0 then we have to wake one of them up.
         */
        ASMAtomicWriteHandle(&pCritSect->NativeThreadOwner, NIL_RTNATIVETHREAD);
        if (ASMAtomicDecS32(&pCritSect->cLockers) >= 0)
        {
            int rc = RTSemEventSignal(pCritSect->EventSem);
            AssertReleaseMsgRC(rc, ("RTSemEventSignal -> %Rrc\n", rc));
        }
    }
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTCritSectLeave);



#ifdef IN_RING3

static int rtCritSectEnterMultiple(size_t cCritSects, PRTCRITSECT *papCritSects, PCRTLOCKVALSRCPOS pSrcPos)
{
    Assert(cCritSects > 0);
    AssertPtr(papCritSects);

    /*
     * Try get them all.
     */
    int rc = VERR_INVALID_PARAMETER;
    size_t i;
    for (i = 0; i < cCritSects; i++)
    {
        rc = rtCritSectTryEnter(papCritSects[i], pSrcPos);
        if (RT_FAILURE(rc))
            break;
    }
    if (RT_SUCCESS(rc))
        return rc;

    /*
     * The retry loop.
     */
    for (unsigned cTries = 0; ; cTries++)
    {
        /*
         * We've failed, release any locks we might have gotten. ('i' is the lock that failed btw.)
         */
        size_t j = i;
        while (j-- > 0)
        {
            int rc2 = RTCritSectLeave(papCritSects[j]);
            AssertRC(rc2);
        }
        if (rc != VERR_SEM_BUSY)
            return rc;

        /*
         * Try prevent any theoretical synchronous races with other threads.
         */
        Assert(cTries < 1000000);
        if (cTries > 10000)
            RTThreadSleep(cTries % 3);

        /*
         * Wait on the one we failed to get.
         */
        rc = rtCritSectEnter(papCritSects[i], pSrcPos);
        if (RT_FAILURE(rc))
            return rc;

        /*
         * Try take the others.
         */
        for (j = 0; j < cCritSects; j++)
        {
            if (j != i)
            {
                rc = rtCritSectTryEnter(papCritSects[j], pSrcPos);
                if (RT_FAILURE(rc))
                    break;
            }
        }
        if (RT_SUCCESS(rc))
            return rc;

        /*
         * We failed.
         */
        if (i > j)
        {
            int rc2 = RTCritSectLeave(papCritSects[i]);
            AssertRC(rc2);
        }
        i = j;
    }
}


RTDECL(int) RTCritSectEnterMultiple(size_t cCritSects, PRTCRITSECT *papCritSects)
{
#ifndef RTCRITSECT_STRICT
    return rtCritSectEnterMultiple(cCritSects, papCritSects, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtCritSectEnterMultiple(cCritSects, papCritSects, &SrcPos);
#endif
}
RT_EXPORT_SYMBOL(RTCritSectEnterMultiple);


RTDECL(int) RTCritSectEnterMultipleDebug(size_t cCritSects, PRTCRITSECT *papCritSects, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtCritSectEnterMultiple(cCritSects, papCritSects, &SrcPos);
}
RT_EXPORT_SYMBOL(RTCritSectEnterMultipleDebug);



RTDECL(int) RTCritSectLeaveMultiple(size_t cCritSects, PRTCRITSECT *papCritSects)
{
    int rc = VINF_SUCCESS;
    for (size_t i = 0; i < cCritSects; i++)
    {
        int rc2 = RTCritSectLeave(papCritSects[i]);
        if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
            rc = rc2;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTCritSectLeaveMultiple);

#endif /* IN_RING3 */



RTDECL(int) RTCritSectDelete(PRTCRITSECT pCritSect)
{
    /*
     * Assert free waiters and so on.
     */
    Assert(pCritSect);
    Assert(pCritSect->u32Magic == RTCRITSECT_MAGIC);
    Assert(pCritSect->cNestings == 0);
    Assert(pCritSect->cLockers == -1);
    Assert(pCritSect->NativeThreadOwner == NIL_RTNATIVETHREAD);
#ifdef IN_RING0
    Assert(pCritSect->fFlags & RTCRITSECT_FLAGS_RING0);
#else
    Assert(!(pCritSect->fFlags & RTCRITSECT_FLAGS_RING0));
#endif

    /*
     * Invalidate the structure and free the mutex.
     * In case someone is waiting we'll signal the semaphore cLockers + 1 times.
     */
    ASMAtomicWriteU32(&pCritSect->u32Magic, ~RTCRITSECT_MAGIC);
    pCritSect->fFlags           = 0;
    pCritSect->cNestings        = 0;
    pCritSect->NativeThreadOwner= NIL_RTNATIVETHREAD;
    RTSEMEVENT EventSem = pCritSect->EventSem;
    pCritSect->EventSem         = NIL_RTSEMEVENT;

    while (pCritSect->cLockers-- >= 0)
        RTSemEventSignal(EventSem);
    ASMAtomicWriteS32(&pCritSect->cLockers, -1);
    int rc = RTSemEventDestroy(EventSem);
    AssertRC(rc);

#ifdef RTCRITSECT_STRICT
    RTLockValidatorRecExclDestroy(&pCritSect->pValidatorRec);
#endif

    return rc;
}
RT_EXPORT_SYMBOL(RTCritSectDelete);

