/* $Id: semmutex-linux.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Mutex Semaphore, Linux  (2.6.x+).
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
#include <iprt/semaphore.h>
#include "internal/iprt.h"

#include <iprt/alloc.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/lockvalidator.h>
#include <iprt/thread.h>
#include <iprt/time.h>
#include "internal/magics.h"
#include "internal/strict.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/syscall.h>
#if 0 /* With 2.6.17 futex.h has become C++ unfriendly. */
# include <linux/futex.h>
#else
# define FUTEX_WAIT 0
# define FUTEX_WAKE 1
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Linux internal representation of a Mutex semaphore.
 */
struct RTSEMMUTEXINTERNAL
{
    /** The futex state variable.
     * 0 means unlocked.
     * 1 means locked, no waiters.
     * 2 means locked, one or more waiters.
     */
    int32_t volatile    iState;
    /** Nesting count. */
    uint32_t volatile   cNestings;
    /** The owner of the mutex. */
    pthread_t volatile  Owner;
    /** Magic value (RTSEMMUTEX_MAGIC). */
    uint32_t  volatile  u32Magic;
#ifdef RTSEMMUTEX_STRICT
    /** Lock validator record associated with this mutex. */
    RTLOCKVALRECEXCL    ValidatorRec;
#endif
};



/**
 * Wrapper for the futex syscall.
 */
static long sys_futex(int32_t volatile *uaddr, int op, int val, struct timespec *utime, int32_t *uaddr2, int val3)
{
    errno = 0;
    long rc = syscall(__NR_futex, uaddr, op, val, utime, uaddr2, val3);
    if (rc < 0)
    {
        Assert(rc == -1);
        rc = -errno;
    }
    return rc;
}


#undef RTSemMutexCreate
RTDECL(int)  RTSemMutexCreate(PRTSEMMUTEX phMutexSem)
{
    return RTSemMutexCreateEx(phMutexSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE, NULL);
}


RTDECL(int) RTSemMutexCreateEx(PRTSEMMUTEX phMutexSem, uint32_t fFlags,
                               RTLOCKVALCLASS hClass, uint32_t uSubClass, const char *pszNameFmt, ...)
{
    AssertReturn(!(fFlags & ~RTSEMMUTEX_FLAGS_NO_LOCK_VAL), VERR_INVALID_PARAMETER);

    /*
     * Allocate semaphore handle.
     */
    struct RTSEMMUTEXINTERNAL *pThis = (struct RTSEMMUTEXINTERNAL *)RTMemAlloc(sizeof(struct RTSEMMUTEXINTERNAL));
    if (pThis)
    {
        pThis->u32Magic     = RTSEMMUTEX_MAGIC;
        pThis->iState       = 0;
        pThis->Owner        = (pthread_t)~0;
        pThis->cNestings    = 0;
#ifdef RTSEMMUTEX_STRICT
        if (!pszNameFmt)
        {
            static uint32_t volatile s_iMutexAnon = 0;
            RTLockValidatorRecExclInit(&pThis->ValidatorRec, hClass, uSubClass, pThis,
                                       !(fFlags & RTSEMMUTEX_FLAGS_NO_LOCK_VAL),
                                       "RTSemMutex-%u", ASMAtomicIncU32(&s_iMutexAnon) - 1);
        }
        else
        {
            va_list va;
            va_start(va, pszNameFmt);
            RTLockValidatorRecExclInitV(&pThis->ValidatorRec, hClass, uSubClass, pThis,
                                        !(fFlags & RTSEMMUTEX_FLAGS_NO_LOCK_VAL), pszNameFmt, va);
            va_end(va);
        }
#else
        RT_NOREF(hClass, uSubClass, pszNameFmt);
#endif

        *phMutexSem = pThis;
        return VINF_SUCCESS;
    }

    return VERR_NO_MEMORY;
}


RTDECL(int)  RTSemMutexDestroy(RTSEMMUTEX hMutexSem)
{
    /*
     * Validate input.
     */
    if (hMutexSem == NIL_RTSEMMUTEX)
        return VINF_SUCCESS;
    struct RTSEMMUTEXINTERNAL *pThis = hMutexSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC,
                    ("hMutexSem=%p u32Magic=%#x\n", pThis, pThis->u32Magic),
                    VERR_INVALID_HANDLE);

    /*
     * Invalidate the semaphore and wake up anyone waiting on it.
     */
    ASMAtomicWriteU32(&pThis->u32Magic, RTSEMMUTEX_MAGIC_DEAD);
    if (ASMAtomicXchgS32(&pThis->iState, 0) > 0)
    {
        sys_futex(&pThis->iState, FUTEX_WAKE, INT_MAX, NULL, NULL, 0);
        usleep(1000);
    }
    pThis->Owner        = (pthread_t)~0;
    pThis->cNestings    = 0;
#ifdef RTSEMMUTEX_STRICT
    RTLockValidatorRecExclDelete(&pThis->ValidatorRec);
#endif

    /*
     * Free the semaphore memory and be gone.
     */
    RTMemFree(pThis);
    return VINF_SUCCESS;
}


RTDECL(uint32_t) RTSemMutexSetSubClass(RTSEMMUTEX hMutexSem, uint32_t uSubClass)
{
#ifdef RTSEMMUTEX_STRICT
    /*
     * Validate.
     */
    RTSEMMUTEXINTERNAL *pThis = hMutexSem;
    AssertPtrReturn(pThis, RTLOCKVAL_SUB_CLASS_INVALID);
    AssertReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, RTLOCKVAL_SUB_CLASS_INVALID);

    return RTLockValidatorRecExclSetSubClass(&pThis->ValidatorRec, uSubClass);
#else
    RT_NOREF(hMutexSem, uSubClass);
    return RTLOCKVAL_SUB_CLASS_INVALID;
#endif
}


DECL_FORCE_INLINE(int) rtSemMutexRequest(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies, bool fAutoResume, PCRTLOCKVALSRCPOS pSrcPos)
{
    RT_NOREF(pSrcPos);

    /*
     * Validate input.
     */
    struct RTSEMMUTEXINTERNAL *pThis = hMutexSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Check if nested request.
     */
    pthread_t Self = pthread_self();
    if (    pThis->Owner == Self
        &&  pThis->cNestings > 0)
    {
#ifdef RTSEMMUTEX_STRICT
        int rc9 = RTLockValidatorRecExclRecursion(&pThis->ValidatorRec, pSrcPos);
        if (RT_FAILURE(rc9))
            return rc9;
#endif
        ASMAtomicIncU32(&pThis->cNestings);
        return VINF_SUCCESS;
    }

#ifdef RTSEMMUTEX_STRICT
    RTTHREAD hThreadSelf = RTThreadSelfAutoAdopt();
    if (cMillies)
    {
        int rc9 = RTLockValidatorRecExclCheckOrder(&pThis->ValidatorRec, hThreadSelf, pSrcPos, cMillies);
        if (RT_FAILURE(rc9))
            return rc9;
    }
#else
    RTTHREAD hThreadSelf = RTThreadSelf();
#endif

    /*
     * Convert timeout value.
     */
    struct timespec ts;
    struct timespec *pTimeout = NULL;
    uint64_t u64End = 0; /* shut up gcc */
    if (cMillies != RT_INDEFINITE_WAIT)
    {
        ts.tv_sec  = cMillies / 1000;
        ts.tv_nsec = (cMillies % 1000) * UINT32_C(1000000);
        u64End = RTTimeSystemNanoTS() + cMillies * UINT64_C(1000000);
        pTimeout = &ts;
    }

    /*
     * Lock the mutex.
     * Optimize for the uncontended case (makes 1-2 ns difference).
     */
    if (RT_UNLIKELY(!ASMAtomicCmpXchgS32(&pThis->iState, 1, 0)))
    {
        for (;;)
        {
            int32_t iOld = ASMAtomicXchgS32(&pThis->iState, 2);

            /*
             * Was the lock released in the meantime? This is unlikely (but possible)
             */
            if (RT_UNLIKELY(iOld == 0))
                break;

            /*
             * Go to sleep.
             */
            if (pTimeout && ( pTimeout->tv_sec || pTimeout->tv_nsec ))
            {
#ifdef RTSEMMUTEX_STRICT
                int rc9 = RTLockValidatorRecExclCheckBlocking(&pThis->ValidatorRec, hThreadSelf, pSrcPos, true,
                                                              cMillies, RTTHREADSTATE_MUTEX, true);
                if (RT_FAILURE(rc9))
                    return rc9;
#else
                RTThreadBlocking(hThreadSelf, RTTHREADSTATE_MUTEX, true);
#endif
            }

            long rc = sys_futex(&pThis->iState, FUTEX_WAIT, 2, pTimeout, NULL, 0);

            RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_MUTEX);
            if (RT_UNLIKELY(pThis->u32Magic != RTSEMMUTEX_MAGIC))
                return VERR_SEM_DESTROYED;

            /*
             * Act on the wakup code.
             */
            if (rc == -ETIMEDOUT)
            {
                Assert(pTimeout);
                return VERR_TIMEOUT;
            }
            if (rc == 0)
                /* we'll leave the loop now unless another thread is faster */;
            else if (rc == -EWOULDBLOCK)
                /* retry with new value. */;
            else if (rc == -EINTR)
            {
                if (!fAutoResume)
                    return VERR_INTERRUPTED;
            }
            else
            {
                /* this shouldn't happen! */
                AssertMsgFailed(("rc=%ld errno=%d\n", rc, errno));
                return RTErrConvertFromErrno(rc);
            }

            /* adjust the relative timeout */
            if (pTimeout)
            {
                int64_t i64Diff = u64End - RTTimeSystemNanoTS();
                if (i64Diff < 1000)
                {
                    rc = VERR_TIMEOUT;
                    break;
                }
                ts.tv_sec  = (uint64_t)i64Diff / UINT32_C(1000000000);
                ts.tv_nsec = (uint64_t)i64Diff % UINT32_C(1000000000);
            }
        }

        /*
         * When leaving this loop, iState is set to 2. This means that we gained the
         * lock and there are _possibly_ some waiters. We don't know exactly as another
         * thread might entered this loop at nearly the same time. Therefore we will
         * call futex_wakeup once too often (if _no_ other thread entered this loop).
         * The key problem is the simple futex_wait test for x != y (iState != 2) in
         * our case).
         */
    }

    /*
     * Set the owner and nesting.
     */
    pThis->Owner = Self;
    ASMAtomicWriteU32(&pThis->cNestings, 1);
#ifdef RTSEMMUTEX_STRICT
    RTLockValidatorRecExclSetOwner(&pThis->ValidatorRec, hThreadSelf, pSrcPos, true);
#endif
    return VINF_SUCCESS;
}


#undef RTSemMutexRequest
RTDECL(int) RTSemMutexRequest(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies)
{
#ifndef RTSEMMUTEX_STRICT
    int rc = rtSemMutexRequest(hMutexSem, cMillies, true, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    int rc = rtSemMutexRequest(hMutexSem, cMillies, true, &SrcPos);
#endif
    Assert(rc != VERR_INTERRUPTED);
    return rc;
}


RTDECL(int) RTSemMutexRequestDebug(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    int rc = rtSemMutexRequest(hMutexSem, cMillies, true, &SrcPos);
    Assert(rc != VERR_INTERRUPTED);
    return rc;
}


#undef RTSemMutexRequestNoResume
RTDECL(int) RTSemMutexRequestNoResume(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies)
{
#ifndef RTSEMMUTEX_STRICT
    return rtSemMutexRequest(hMutexSem, cMillies, false, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtSemMutexRequest(hMutexSem, cMillies, false, &SrcPos);
#endif
}


RTDECL(int) RTSemMutexRequestNoResumeDebug(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtSemMutexRequest(hMutexSem, cMillies, false, &SrcPos);
}


RTDECL(int) RTSemMutexRelease(RTSEMMUTEX hMutexSem)
{
    /*
     * Validate input.
     */
    struct RTSEMMUTEXINTERNAL *pThis = hMutexSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, VERR_INVALID_HANDLE);

#ifdef RTSEMMUTEX_STRICT
    int rc9 = RTLockValidatorRecExclReleaseOwner(&pThis->ValidatorRec, pThis->cNestings == 1);
    if (RT_FAILURE(rc9))
        return rc9;
#endif

    /*
     * Check if nested.
     */
    pthread_t Self = pthread_self();
    if (RT_UNLIKELY(    pThis->Owner != Self
                    ||  pThis->cNestings == 0))
    {
        AssertMsgFailed(("Not owner of mutex %p!! Self=%08x Owner=%08x cNestings=%d\n",
                         pThis, Self, pThis->Owner, pThis->cNestings));
        return VERR_NOT_OWNER;
    }

    /*
     * If nested we'll just pop a nesting.
     */
    if (pThis->cNestings > 1)
    {
        ASMAtomicDecU32(&pThis->cNestings);
        return VINF_SUCCESS;
    }

    /*
     * Clear the state. (cNestings == 1)
     */
    pThis->Owner = (pthread_t)~0;
    ASMAtomicWriteU32(&pThis->cNestings, 0);

    /*
     * Release the mutex.
     */
    int32_t iNew = ASMAtomicDecS32(&pThis->iState);
    if (RT_UNLIKELY(iNew != 0))
    {
        /* somebody is waiting, try wake up one of them. */
        ASMAtomicXchgS32(&pThis->iState, 0);
        (void)sys_futex(&pThis->iState, FUTEX_WAKE, 1, NULL, NULL, 0);
    }
    return VINF_SUCCESS;
}


RTDECL(bool) RTSemMutexIsOwned(RTSEMMUTEX hMutexSem)
{
    /*
     * Validate.
     */
    RTSEMMUTEXINTERNAL *pThis = hMutexSem;
    AssertPtrReturn(pThis, false);
    AssertReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, false);

    return pThis->Owner != (pthread_t)~0;
}

