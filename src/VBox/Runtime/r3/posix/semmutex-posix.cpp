/* $Id: semmutex-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Mutex Semaphore, POSIX.
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
#include "internal/magics.h"
#include "internal/strict.h"

#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Posix internal representation of a Mutex semaphore. */
struct RTSEMMUTEXINTERNAL
{
    /** pthread mutex. */
    pthread_mutex_t     Mutex;
    /** The owner of the mutex. */
    volatile pthread_t  Owner;
    /** Nesting count. */
    volatile uint32_t   cNesting;
    /** Magic value (RTSEMMUTEX_MAGIC). */
    uint32_t            u32Magic;
#ifdef RTSEMMUTEX_STRICT
    /** Lock validator record associated with this mutex. */
    RTLOCKVALRECEXCL    ValidatorRec;
#endif
};

#if defined(RT_OS_DARWIN) || defined(RT_OS_NETBSD)
/**
 * This function is a crude approximation of pthread_mutex_timedlock.
 */
static int rtSemFallbackPthreadMutexTimedlock(pthread_mutex_t *mutex, RTMSINTERVAL cMillies)
{
    struct timespec ts;
    int rc;

    rc = pthread_mutex_trylock(mutex);
    if (rc != EBUSY)
        return rc;

    ts.tv_sec = cMillies / 1000;
    ts.tv_nsec = (cMillies % 1000) * 1000000;

    while (ts.tv_sec > 0 || ts.tv_nsec > 0)
    {
        struct timespec delta, remaining;

        if (ts.tv_sec > 0)
        {
            delta.tv_sec = 1;
            delta.tv_nsec = 0;
            ts.tv_sec--;
        }
        else
        {
            delta.tv_sec = 0;
            delta.tv_nsec = ts.tv_nsec;
            ts.tv_nsec = 0;
        }

        nanosleep(&delta, &remaining);

        rc = pthread_mutex_trylock(mutex);
        if (rc != EBUSY)
            return rc;

        if (RT_UNLIKELY(remaining.tv_nsec > 0 || remaining.tv_sec > 0))
        {
            ts.tv_sec += remaining.tv_sec;
            ts.tv_nsec += remaining.tv_nsec;
            if (ts.tv_nsec >= 1000000000)
            {
                ts.tv_nsec -= 1000000000;
                ts.tv_sec++;
            }
        }
    }

    return ETIMEDOUT;
}
#endif


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
    int rc;
    struct RTSEMMUTEXINTERNAL *pThis = (struct RTSEMMUTEXINTERNAL *)RTMemAlloc(sizeof(struct RTSEMMUTEXINTERNAL));
    if (pThis)
    {
        /*
         * Create the semaphore.
         */
        rc = pthread_mutex_init(&pThis->Mutex, NULL);
        if (!rc)
        {
            pThis->Owner    = (pthread_t)-1;
            pThis->cNesting = 0;
            pThis->u32Magic = RTSEMMUTEX_MAGIC;
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
            RT_NOREF_PV(hClass); RT_NOREF_PV(uSubClass); RT_NOREF_PV(pszNameFmt);
#endif

            *phMutexSem = pThis;
            return VINF_SUCCESS;
        }
        RTMemFree(pThis);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
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
    AssertReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Try destroy it.
     */
    int rc = pthread_mutex_destroy(&pThis->Mutex);
    if (rc)
    {
        AssertMsgFailed(("Failed to destroy mutex sem %p, rc=%d.\n", hMutexSem, rc));
        return RTErrConvertFromErrno(rc);
    }

    /*
     * Free the memory and be gone.
     */
    ASMAtomicWriteU32(&pThis->u32Magic, RTSEMMUTEX_MAGIC_DEAD);
    pThis->Owner    = (pthread_t)-1;
    pThis->cNesting = UINT32_MAX;
#ifdef RTSEMMUTEX_STRICT
    RTLockValidatorRecExclDelete(&pThis->ValidatorRec);
#endif
    RTMemTmpFree(pThis);

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
    RT_NOREF_PV(hMutexSem); RT_NOREF_PV(uSubClass);
    return RTLOCKVAL_SUB_CLASS_INVALID;
#endif
}


DECL_FORCE_INLINE(int) rtSemMutexRequest(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies, PCRTLOCKVALSRCPOS pSrcPos)
{
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
        &&  pThis->cNesting > 0)
    {
#ifdef RTSEMMUTEX_STRICT
        int rc9 = RTLockValidatorRecExclRecursion(&pThis->ValidatorRec, pSrcPos);
        if (RT_FAILURE(rc9))
            return rc9;
#endif
        ASMAtomicIncU32(&pThis->cNesting);
        return VINF_SUCCESS;
    }

    /*
     * Lock it.
     */
    RTTHREAD hThreadSelf = NIL_RTTHREAD;
    if (cMillies != 0)
    {
#ifdef RTSEMMUTEX_STRICT
        hThreadSelf = RTThreadSelfAutoAdopt();
        int rc9 = RTLockValidatorRecExclCheckOrderAndBlocking(&pThis->ValidatorRec, hThreadSelf, pSrcPos, true,
                                                              cMillies, RTTHREADSTATE_MUTEX, true);
        if (RT_FAILURE(rc9))
            return rc9;
#else
        hThreadSelf = RTThreadSelf();
        RTThreadBlocking(hThreadSelf, RTTHREADSTATE_MUTEX, true);
        RT_NOREF_PV(pSrcPos);
#endif
    }

    if (cMillies == RT_INDEFINITE_WAIT)
    {
        /* take mutex */
        int rc = pthread_mutex_lock(&pThis->Mutex);
        RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_MUTEX);
        if (rc)
        {
            AssertMsgFailed(("Failed to lock mutex sem %p, rc=%d.\n", hMutexSem, rc)); NOREF(rc);
            return RTErrConvertFromErrno(rc);
        }
    }
    else
    {
        int rc;
#if !defined(RT_OS_DARWIN) && !defined(RT_OS_NETBSD)
        struct timespec     ts = {0,0};
# if defined(RT_OS_HAIKU)
        struct timeval      tv = {0,0};
        gettimeofday(&tv, NULL);
        ts.tv_sec = tv.tv_sec;
        ts.tv_nsec = tv.tv_usec * 1000;
# else
        clock_gettime(CLOCK_REALTIME, &ts);
# endif
        if (cMillies != 0)
        {
            ts.tv_nsec += (cMillies % 1000) * 1000000;
            ts.tv_sec  += cMillies / 1000;
            if (ts.tv_nsec >= 1000000000)
            {
                ts.tv_nsec -= 1000000000;
                ts.tv_sec++;
            }
        }

        /* take mutex */
        rc = pthread_mutex_timedlock(&pThis->Mutex, &ts);
#else
        /*
         * When there's no pthread_mutex_timedlock() use a crude sleep
         * and retry approximation.  Since the sleep interval is
         * relative, we don't need to convert to the absolute time
         * here only to convert back to relative in the fallback
         * function.
         */
        rc = rtSemFallbackPthreadMutexTimedlock(&pThis->Mutex, cMillies);
#endif
        RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_MUTEX);
        if (rc)
        {
            AssertMsg(rc == ETIMEDOUT, ("Failed to lock mutex sem %p, rc=%d.\n", hMutexSem, rc)); NOREF(rc);
            return RTErrConvertFromErrno(rc);
        }
    }

    /*
     * Set the owner and nesting.
     */
    pThis->Owner = Self;
    ASMAtomicWriteU32(&pThis->cNesting, 1);
#ifdef RTSEMMUTEX_STRICT
    RTLockValidatorRecExclSetOwner(&pThis->ValidatorRec, hThreadSelf, pSrcPos, true);
#endif

    return VINF_SUCCESS;
}


#undef RTSemMutexRequest
RTDECL(int) RTSemMutexRequest(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies)
{
#ifndef RTSEMMUTEX_STRICT
    return rtSemMutexRequest(hMutexSem, cMillies, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtSemMutexRequest(hMutexSem, cMillies, &SrcPos);
#endif
}


RTDECL(int) RTSemMutexRequestDebug(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtSemMutexRequest(hMutexSem, cMillies, &SrcPos);
}


#undef RTSemMutexRequestNoResume
RTDECL(int) RTSemMutexRequestNoResume(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies)
{
    /* (EINTR isn't returned by the wait functions we're using.) */
#ifndef RTSEMMUTEX_STRICT
    return rtSemMutexRequest(hMutexSem, cMillies, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtSemMutexRequest(hMutexSem, cMillies, &SrcPos);
#endif
}


RTDECL(int) RTSemMutexRequestNoResumeDebug(RTSEMMUTEX hMutexSem, RTMSINTERVAL cMillies, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtSemMutexRequest(hMutexSem, cMillies, &SrcPos);
}


RTDECL(int)  RTSemMutexRelease(RTSEMMUTEX hMutexSem)
{
    /*
     * Validate input.
     */
    struct RTSEMMUTEXINTERNAL *pThis = hMutexSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMMUTEX_MAGIC, VERR_INVALID_HANDLE);

#ifdef RTSEMMUTEX_STRICT
    int rc9 = RTLockValidatorRecExclReleaseOwner(&pThis->ValidatorRec, pThis->cNesting == 1);
    if (RT_FAILURE(rc9))
        return rc9;
#endif

    /*
     * Check if nested.
     */
    pthread_t Self = pthread_self();
    if (RT_UNLIKELY(    pThis->Owner != Self
                    ||  pThis->cNesting == 0))
    {
        AssertMsgFailed(("Not owner of mutex %p!! Self=%08x Owner=%08x cNesting=%d\n",
                         pThis, Self, pThis->Owner, pThis->cNesting));
        return VERR_NOT_OWNER;
    }

    /*
     * If nested we'll just pop a nesting.
     */
    if (pThis->cNesting > 1)
    {
        ASMAtomicDecU32(&pThis->cNesting);
        return VINF_SUCCESS;
    }

    /*
     * Clear the state. (cNesting == 1)
     */
    pThis->Owner = (pthread_t)-1;
    ASMAtomicWriteU32(&pThis->cNesting, 0);

    /*
     * Unlock mutex semaphore.
     */
    int rc = pthread_mutex_unlock(&pThis->Mutex);
    if (RT_UNLIKELY(rc))
    {
        AssertMsgFailed(("Failed to unlock mutex sem %p, rc=%d.\n", hMutexSem, rc)); NOREF(rc);
        return RTErrConvertFromErrno(rc);
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

    return pThis->Owner != (pthread_t)-1;
}

