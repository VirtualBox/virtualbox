/* $Id: semeventmulti-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Multiple Release Event Semaphore, POSIX.
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

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/lockvalidator.h>
#include <iprt/mem.h>
#include <iprt/time.h>

#include "internal/strict.h"

#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>

#include "semwait.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Posix internal representation of a Mutex Multi semaphore.
 * The POSIX implementation uses a mutex and a condition variable to implement
 * the automatic reset event semaphore semantics. */
struct RTSEMEVENTMULTIINTERNAL
{
    /** pthread condition. */
    pthread_cond_t      Cond;
    /** pthread mutex which protects the condition and the event state. */
    pthread_mutex_t     Mutex;
    /** The state of the semaphore.
     * This is operated while owning mutex and using atomic updating. */
    volatile uint32_t   u32State;
    /** Number of waiters. */
    volatile uint32_t   cWaiters;
#ifdef RTSEMEVENTMULTI_STRICT
    /** Signallers. */
    RTLOCKVALRECSHRD    Signallers;
    /** Indicates that lock validation should be performed. */
    bool volatile       fEverHadSignallers;
#endif
    /** Set if we're using the monotonic clock. */
    bool                fMonotonicClock;
};

/** The values of the u32State variable in RTSEMEVENTMULTIINTERNAL.
 * @{ */
/** The object isn't initialized. */
#define EVENTMULTI_STATE_UNINITIALIZED   0
/** The semaphore is signaled. */
#define EVENTMULTI_STATE_SIGNALED        0xff00ff00
/** The semaphore is not signaled. */
#define EVENTMULTI_STATE_NOT_SIGNALED    0x00ff00ff
/** @} */



RTDECL(int)  RTSemEventMultiCreate(PRTSEMEVENTMULTI phEventMultiSem)
{
    return RTSemEventMultiCreateEx(phEventMultiSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, NULL);
}


RTDECL(int)  RTSemEventMultiCreateEx(PRTSEMEVENTMULTI phEventMultiSem, uint32_t fFlags, RTLOCKVALCLASS hClass,
                                     const char *pszNameFmt, ...)
{
    AssertReturn(!(fFlags & ~RTSEMEVENTMULTI_FLAGS_NO_LOCK_VAL), VERR_INVALID_PARAMETER);

    /*
     * Allocate semaphore handle.
     */
    int rc;
    struct RTSEMEVENTMULTIINTERNAL *pThis = (struct RTSEMEVENTMULTIINTERNAL *)RTMemAlloc(sizeof(struct RTSEMEVENTMULTIINTERNAL));
    if (pThis)
    {
        /*
         * Create the condition variable.
         */
        pthread_condattr_t CondAttr;
        rc = pthread_condattr_init(&CondAttr);
        if (!rc)
        {
#if defined(CLOCK_MONOTONIC) && defined(IPRT_HAVE_PTHREAD_CONDATTR_SETCLOCK)
            /* ASSUMES RTTimeSystemNanoTS() == RTTimeNanoTS() == clock_gettime(CLOCK_MONOTONIC). */
            rc = pthread_condattr_setclock(&CondAttr, CLOCK_MONOTONIC);
            pThis->fMonotonicClock = rc == 0;
#else
            pThis->fMonotonicClock = false;
#endif
            rc = pthread_cond_init(&pThis->Cond, &CondAttr);
            if (!rc)
            {
                /*
                 * Create the semaphore.
                 */
                rc = pthread_mutex_init(&pThis->Mutex, NULL);
                if (!rc)
                {
                    pthread_condattr_destroy(&CondAttr);

                    ASMAtomicWriteU32(&pThis->u32State, EVENTMULTI_STATE_NOT_SIGNALED);
                    ASMAtomicWriteU32(&pThis->cWaiters, 0);
#ifdef RTSEMEVENTMULTI_STRICT
                    if (!pszNameFmt)
                    {
                        static uint32_t volatile s_iSemEventMultiAnon = 0;
                        RTLockValidatorRecSharedInit(&pThis->Signallers, hClass, RTLOCKVAL_SUB_CLASS_ANY, pThis,
                                                     true /*fSignaller*/, !(fFlags & RTSEMEVENTMULTI_FLAGS_NO_LOCK_VAL),
                                                     "RTSemEventMulti-%u", ASMAtomicIncU32(&s_iSemEventMultiAnon) - 1);
                    }
                    else
                    {
                        va_list va;
                        va_start(va, pszNameFmt);
                        RTLockValidatorRecSharedInitV(&pThis->Signallers, hClass, RTLOCKVAL_SUB_CLASS_ANY, pThis,
                                                      true /*fSignaller*/, !(fFlags & RTSEMEVENTMULTI_FLAGS_NO_LOCK_VAL),
                                                      pszNameFmt, va);
                        va_end(va);
                    }
                    pThis->fEverHadSignallers = false;
#else
                    RT_NOREF_PV(hClass); RT_NOREF_PV(pszNameFmt);
#endif

                    *phEventMultiSem = pThis;
                    return VINF_SUCCESS;
                }

                pthread_cond_destroy(&pThis->Cond);
            }
            pthread_condattr_destroy(&CondAttr);
        }
        rc = RTErrConvertFromErrno(rc);
        RTMemFree(pThis);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;

}


RTDECL(int)  RTSemEventMultiDestroy(RTSEMEVENTMULTI hEventMultiSem)
{
    /*
     * Validate handle.
     */
    struct RTSEMEVENTMULTIINTERNAL *pThis = hEventMultiSem;
    if (pThis == NIL_RTSEMEVENTMULTI)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    uint32_t u32 = pThis->u32State;
    AssertReturn(u32 == EVENTMULTI_STATE_NOT_SIGNALED || u32 == EVENTMULTI_STATE_SIGNALED, VERR_INVALID_HANDLE);

    /*
     * Abort all waiters forcing them to return failure.
     */
    int rc;
    for (int i = 30; i > 0; i--)
    {
        ASMAtomicXchgU32(&pThis->u32State, EVENTMULTI_STATE_UNINITIALIZED);
        rc = pthread_cond_destroy(&pThis->Cond);
        if (rc != EBUSY)
            break;
        pthread_cond_broadcast(&pThis->Cond);
        usleep(1000);
    }
    if (rc)
    {
        AssertMsgFailed(("Failed to destroy event sem %p, rc=%d.\n", hEventMultiSem, rc));
        return RTErrConvertFromErrno(rc);
    }

    /*
     * Destroy the semaphore
     * If it's busy we'll wait a bit to give the threads a chance to be scheduled.
     */
    for (int i = 30; i > 0; i--)
    {
        rc = pthread_mutex_destroy(&pThis->Mutex);
        if (rc != EBUSY)
            break;
        usleep(1000);
    }
    if (rc)
    {
        AssertMsgFailed(("Failed to destroy event sem %p, rc=%d. (mutex)\n", hEventMultiSem, rc));
        return RTErrConvertFromErrno(rc);
    }

    /*
     * Free the semaphore memory and be gone.
     */
#ifdef RTSEMEVENTMULTI_STRICT
    RTLockValidatorRecSharedDelete(&pThis->Signallers);
#endif
    RTMemFree(pThis);
    return VINF_SUCCESS;
}


RTDECL(int)  RTSemEventMultiSignal(RTSEMEVENTMULTI hEventMultiSem)
{
    /*
     * Validate input.
     */
    struct RTSEMEVENTMULTIINTERNAL *pThis = hEventMultiSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    uint32_t u32 = pThis->u32State;
    AssertReturn(u32 == EVENTMULTI_STATE_NOT_SIGNALED || u32 == EVENTMULTI_STATE_SIGNALED, VERR_INVALID_HANDLE);

#ifdef RTSEMEVENTMULTI_STRICT
    if (pThis->fEverHadSignallers)
    {
        int rc9 = RTLockValidatorRecSharedCheckSignaller(&pThis->Signallers, NIL_RTTHREAD);
        if (RT_FAILURE(rc9))
            return rc9;
    }
#endif

    /*
     * Lock the mutex semaphore.
     */
    int rc = pthread_mutex_lock(&pThis->Mutex);
    if (rc)
    {
        AssertMsgFailed(("Failed to lock event sem %p, rc=%d.\n", hEventMultiSem, rc));
        return RTErrConvertFromErrno(rc);
    }

    /*
     * Check the state.
     */
    if (pThis->u32State == EVENTMULTI_STATE_NOT_SIGNALED)
    {
        ASMAtomicXchgU32(&pThis->u32State, EVENTMULTI_STATE_SIGNALED);
        rc = pthread_cond_broadcast(&pThis->Cond);
        AssertMsg(!rc, ("Failed to signal event sem %p, rc=%d.\n", hEventMultiSem, rc));
    }
    else if (pThis->u32State == EVENTMULTI_STATE_SIGNALED)
    {
        rc = pthread_cond_broadcast(&pThis->Cond); /* give'm another kick... */
        AssertMsg(!rc, ("Failed to signal event sem %p, rc=%d. (2)\n", hEventMultiSem, rc));
    }
    else
        rc = VERR_SEM_DESTROYED;

    /*
     * Release the mutex and return.
     */
    int rc2 = pthread_mutex_unlock(&pThis->Mutex);
    AssertMsg(!rc2, ("Failed to unlock event sem %p, rc=%d.\n", hEventMultiSem, rc));
    if (rc)
        return RTErrConvertFromErrno(rc);
    if (rc2)
        return RTErrConvertFromErrno(rc2);

    return VINF_SUCCESS;
}


RTDECL(int)  RTSemEventMultiReset(RTSEMEVENTMULTI hEventMultiSem)
{
    /*
     * Validate input.
     */
    int rc = VINF_SUCCESS;
    struct RTSEMEVENTMULTIINTERNAL *pThis = hEventMultiSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    uint32_t u32 = pThis->u32State;
    AssertReturn(u32 == EVENTMULTI_STATE_NOT_SIGNALED || u32 == EVENTMULTI_STATE_SIGNALED, VERR_INVALID_HANDLE);

    /*
     * Lock the mutex semaphore.
     */
    int rcPosix = pthread_mutex_lock(&pThis->Mutex);
    if (RT_UNLIKELY(rcPosix))
    {
        AssertMsgFailed(("Failed to lock event multi sem %p, rc=%d.\n", hEventMultiSem, rcPosix));
        return RTErrConvertFromErrno(rcPosix);
    }

    /*
     * Check the state.
     */
    if (pThis->u32State == EVENTMULTI_STATE_SIGNALED)
        ASMAtomicXchgU32(&pThis->u32State, EVENTMULTI_STATE_NOT_SIGNALED);
    else if (pThis->u32State != EVENTMULTI_STATE_NOT_SIGNALED)
        rc = VERR_SEM_DESTROYED;

    /*
     * Release the mutex and return.
     */
    rcPosix = pthread_mutex_unlock(&pThis->Mutex);
    if (RT_UNLIKELY(rcPosix))
    {
        AssertMsgFailed(("Failed to unlock event multi sem %p, rc=%d.\n", hEventMultiSem, rcPosix));
        return RTErrConvertFromErrno(rcPosix);
    }

    return rc;
}


/**
 * Handle polling (timeout already expired at the time of the call).
 *
 * @returns VINF_SUCCESS, VERR_TIMEOUT, VERR_SEM_DESTROYED.
 * @param   pThis               The semaphore.
 */
DECLINLINE(int) rtSemEventMultiPosixWaitPoll(struct RTSEMEVENTMULTIINTERNAL *pThis)
{
    int rc = pthread_mutex_lock(&pThis->Mutex);
    AssertMsgReturn(!rc, ("Failed to lock event multi sem %p, rc=%d.\n", pThis, rc), RTErrConvertFromErrno(rc));

    uint32_t const u32State = pThis->u32State;

    rc = pthread_mutex_unlock(&pThis->Mutex);
    AssertMsg(!rc, ("Failed to unlock event multi sem %p, rc=%d.\n", pThis, rc)); NOREF(rc);

    return u32State == EVENTMULTI_STATE_SIGNALED
         ? VINF_SUCCESS
         : u32State != EVENTMULTI_STATE_UNINITIALIZED
         ? VERR_TIMEOUT
         : VERR_SEM_DESTROYED;
}



/**
 * Implements the indefinite wait.
 *
 * @returns See RTSemEventMultiWaitEx.
 * @param   pThis               The semaphore.
 * @param   fFlags              See RTSemEventMultiWaitEx.
 * @param   pSrcPos             The source position, can be NULL.
 */
static int rtSemEventMultiPosixWaitIndefinite(struct RTSEMEVENTMULTIINTERNAL *pThis, uint32_t fFlags, PCRTLOCKVALSRCPOS pSrcPos)
{
    /* take mutex */
    int rc = pthread_mutex_lock(&pThis->Mutex);
    AssertMsgReturn(!rc, ("Failed to lock event multi sem %p, rc=%d.\n", pThis, rc), RTErrConvertFromErrno(rc));
    ASMAtomicIncU32(&pThis->cWaiters);

    for (;;)
    {
        /* check state. */
        uint32_t const u32State = pThis->u32State;
        if (u32State != EVENTMULTI_STATE_NOT_SIGNALED)
        {
            ASMAtomicDecU32(&pThis->cWaiters);
            rc = pthread_mutex_unlock(&pThis->Mutex);
            AssertMsg(!rc, ("Failed to unlock event multi sem %p, rc=%d.\n", pThis, rc));
            return u32State == EVENTMULTI_STATE_SIGNALED
                 ? VINF_SUCCESS
                 : VERR_SEM_DESTROYED;
        }

        /* wait */
#ifdef RTSEMEVENTMULTI_STRICT
        RTTHREAD hThreadSelf = RTThreadSelfAutoAdopt();
        if (pThis->fEverHadSignallers)
        {
            rc = RTLockValidatorRecSharedCheckBlocking(&pThis->Signallers, hThreadSelf, pSrcPos, false,
                                                       RT_INDEFINITE_WAIT, RTTHREADSTATE_EVENT_MULTI, true);
            if (RT_FAILURE(rc))
            {
                ASMAtomicDecU32(&pThis->cWaiters);
                pthread_mutex_unlock(&pThis->Mutex);
                return rc;
            }
        }
#else
        RTTHREAD hThreadSelf = RTThreadSelf();
        RT_NOREF_PV(pSrcPos);
#endif
        RTThreadBlocking(hThreadSelf, RTTHREADSTATE_EVENT_MULTI, true);
        /** @todo interruptible wait is not implementable... */ NOREF(fFlags);
        rc = pthread_cond_wait(&pThis->Cond, &pThis->Mutex);
        RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_EVENT_MULTI);
        if (RT_UNLIKELY(rc))
        {
            AssertMsgFailed(("Failed to wait on event multi sem %p, rc=%d.\n", pThis, rc));
            ASMAtomicDecU32(&pThis->cWaiters);
            int rc2 = pthread_mutex_unlock(&pThis->Mutex);
            AssertMsg(!rc2, ("Failed to unlock event multi sem %p, rc=%d.\n", pThis, rc2)); NOREF(rc2);
            return RTErrConvertFromErrno(rc);
        }
    }
}


/**
 * Implements the timed wait.
 *
 * @returns See RTSemEventMultiWaitEx
 * @param   pThis               The semaphore.
 * @param   fFlags              See RTSemEventMultiWaitEx.
 * @param   uTimeout            See RTSemEventMultiWaitEx.
 * @param   pSrcPos             The source position, can be NULL.
 */
static int rtSemEventMultiPosixWaitTimed(struct RTSEMEVENTMULTIINTERNAL *pThis, uint32_t fFlags, uint64_t uTimeout,
                                         PCRTLOCKVALSRCPOS pSrcPos)
{
    /*
     * Convert the timeout specification to absolute and relative deadlines,
     * divierting polling and infinite waits to the appropriate workers.
     */
    struct timespec AbsDeadline         = { 0, 0 };
    uint64_t const  cNsRelativeDeadline = rtSemPosixCalcDeadline(fFlags, uTimeout, pThis->fMonotonicClock, &AbsDeadline);
    if (cNsRelativeDeadline == 0)
        return rtSemEventMultiPosixWaitPoll(pThis);
    if (cNsRelativeDeadline == UINT64_MAX)
        return rtSemEventMultiPosixWaitIndefinite(pThis, fFlags, pSrcPos);

    /*
     * To business!
     */
    /* take mutex */
    int rc = pthread_mutex_lock(&pThis->Mutex);
    AssertMsgReturn(rc == 0, ("rc=%d pThis=%p\n", rc, pThis), RTErrConvertFromErrno(rc)); NOREF(rc);
    ASMAtomicIncU32(&pThis->cWaiters);

    for (;;)
    {
        /* check state. */
        uint32_t const u32State = pThis->u32State;
        if (u32State != EVENTMULTI_STATE_NOT_SIGNALED)
        {
            ASMAtomicDecU32(&pThis->cWaiters);
            rc = pthread_mutex_unlock(&pThis->Mutex);
            AssertMsg(!rc, ("Failed to unlock event multi sem %p, rc=%d.\n", pThis, rc));
            return u32State == EVENTMULTI_STATE_SIGNALED
                 ? VINF_SUCCESS
                 : VERR_SEM_DESTROYED;
        }

        /* wait */
#ifdef RTSEMEVENTMULTI_STRICT
        RTTHREAD hThreadSelf = RTThreadSelfAutoAdopt();
        if (pThis->fEverHadSignallers)
        {
            rc = RTLockValidatorRecSharedCheckBlocking(&pThis->Signallers, hThreadSelf, pSrcPos, false,
                                                       (uTimeout + RT_NS_1MS - 1)/ RT_NS_1MS, RTTHREADSTATE_EVENT_MULTI, true);
            if (RT_FAILURE(rc))
            {
                ASMAtomicDecU32(&pThis->cWaiters);
                pthread_mutex_unlock(&pThis->Mutex);
                return rc;
            }
        }
#else
        RTTHREAD hThreadSelf = RTThreadSelf();
#endif
        RTThreadBlocking(hThreadSelf, RTTHREADSTATE_EVENT_MULTI, true);
        rc = pthread_cond_timedwait(&pThis->Cond, &pThis->Mutex, &AbsDeadline);
        RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_EVENT_MULTI);

        /* According to SuS this function shall not return EINTR, but linux man page might have said differently at some point... */
        if (    rc != 0
            && (   rc != EINTR
                || (fFlags & RTSEMWAIT_FLAGS_NORESUME)) )
        {
            AssertMsg(rc == ETIMEDOUT, ("Failed to wait on event multi sem %p, rc=%d.\n", pThis, rc));
            ASMAtomicDecU32(&pThis->cWaiters);
            int rc2 = pthread_mutex_unlock(&pThis->Mutex);
            AssertMsg(!rc2, ("Failed to unlock event multi sem %p, rc=%d.\n", pThis, rc2)); NOREF(rc2);
            return RTErrConvertFromErrno(rc);
        }
    }
}


DECLINLINE(int) rtSemEventMultiPosixWait(RTSEMEVENTMULTI hEventMultiSem, uint32_t fFlags, uint64_t uTimeout,
                                         PCRTLOCKVALSRCPOS pSrcPos)
{
    /*
     * Validate input.
     */
    struct RTSEMEVENTMULTIINTERNAL *pThis = hEventMultiSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    uint32_t u32 = pThis->u32State;
    AssertReturn(u32 == EVENTMULTI_STATE_NOT_SIGNALED || u32 == EVENTMULTI_STATE_SIGNALED, VERR_INVALID_HANDLE);
    AssertReturn(RTSEMWAIT_FLAGS_ARE_VALID(fFlags), VERR_INVALID_PARAMETER);

    /*
     * Optimize the case where the event is signalled.
     */
    if (ASMAtomicUoReadU32(&pThis->u32State) == EVENTMULTI_STATE_SIGNALED)
    {
        int rc = rtSemEventMultiPosixWaitPoll(pThis);
        if (RT_LIKELY(rc != VERR_TIMEOUT))
            return rc;
    }

    /*
     * Indefinite or timed wait?
     */
    if (fFlags & RTSEMWAIT_FLAGS_INDEFINITE)
        return rtSemEventMultiPosixWaitIndefinite(pThis, fFlags, pSrcPos);
    return rtSemEventMultiPosixWaitTimed(pThis, fFlags, uTimeout, pSrcPos);
}


#undef RTSemEventMultiWaitEx
RTDECL(int)  RTSemEventMultiWaitEx(RTSEMEVENTMULTI hEventMultiSem, uint32_t fFlags, uint64_t uTimeout)
{
#ifndef RTSEMEVENT_STRICT
    return rtSemEventMultiPosixWait(hEventMultiSem, fFlags, uTimeout, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtSemEventMultiPosixWait(hEventMultiSem, fFlags, uTimeout, &SrcPos);
#endif
}


RTDECL(int)  RTSemEventMultiWaitExDebug(RTSEMEVENTMULTI hEventMultiSem, uint32_t fFlags, uint64_t uTimeout,
                                        RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtSemEventMultiPosixWait(hEventMultiSem, fFlags, uTimeout, &SrcPos);
}


RTDECL(void) RTSemEventMultiSetSignaller(RTSEMEVENTMULTI hEventMultiSem, RTTHREAD hThread)
{
#ifdef RTSEMEVENTMULTI_STRICT
    struct RTSEMEVENTMULTIINTERNAL *pThis = hEventMultiSem;
    AssertPtrReturnVoid(pThis);
    uint32_t u32 = pThis->u32State;
    AssertReturnVoid(u32 == EVENTMULTI_STATE_NOT_SIGNALED || u32 == EVENTMULTI_STATE_SIGNALED);

    ASMAtomicWriteBool(&pThis->fEverHadSignallers, true);
    RTLockValidatorRecSharedResetOwner(&pThis->Signallers, hThread, NULL);
#else
    RT_NOREF_PV(hEventMultiSem); RT_NOREF_PV(hThread);
#endif
}


RTDECL(void) RTSemEventMultiAddSignaller(RTSEMEVENTMULTI hEventMultiSem, RTTHREAD hThread)
{
#ifdef RTSEMEVENTMULTI_STRICT
    struct RTSEMEVENTMULTIINTERNAL *pThis = hEventMultiSem;
    AssertPtrReturnVoid(pThis);
    uint32_t u32 = pThis->u32State;
    AssertReturnVoid(u32 == EVENTMULTI_STATE_NOT_SIGNALED || u32 == EVENTMULTI_STATE_SIGNALED);

    ASMAtomicWriteBool(&pThis->fEverHadSignallers, true);
    RTLockValidatorRecSharedAddOwner(&pThis->Signallers, hThread, NULL);
#else
    RT_NOREF_PV(hEventMultiSem); RT_NOREF_PV(hThread);
#endif
}


RTDECL(void) RTSemEventMultiRemoveSignaller(RTSEMEVENTMULTI hEventMultiSem, RTTHREAD hThread)
{
#ifdef RTSEMEVENTMULTI_STRICT
    struct RTSEMEVENTMULTIINTERNAL *pThis = hEventMultiSem;
    AssertPtrReturnVoid(pThis);
    uint32_t u32 = pThis->u32State;
    AssertReturnVoid(u32 == EVENTMULTI_STATE_NOT_SIGNALED || u32 == EVENTMULTI_STATE_SIGNALED);

    RTLockValidatorRecSharedRemoveOwner(&pThis->Signallers, hThread);
#else
    RT_NOREF_PV(hEventMultiSem); RT_NOREF_PV(hThread);
#endif
}

