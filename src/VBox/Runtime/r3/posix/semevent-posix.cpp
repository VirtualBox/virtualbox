/* $Id: semevent-posix.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Event Semaphore, POSIX.
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
#include <iprt/mem.h>
#include <iprt/lockvalidator.h>
#include <iprt/time.h>

#include "internal/mem.h"
#include "internal/strict.h"

#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
#include <sched.h>

#include "semwait.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/** Internal representation of the POSIX implementation of an Event semaphore.
 * The POSIX implementation uses a mutex and a condition variable to implement
 * the automatic reset event semaphore semantics.
 */
struct RTSEMEVENTINTERNAL
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
#ifdef RTSEMEVENT_STRICT
    /** Signallers. */
    RTLOCKVALRECSHRD    Signallers;
    /** Indicates that lock validation should be performed. */
    bool volatile       fEverHadSignallers;
#endif
    /** The creation flags. */
    uint32_t            fFlags;
    /** Set if we're using the monotonic clock. */
    bool                fMonotonicClock;
};

/** The values of the u32State variable in a RTSEMEVENTINTERNAL.
 * @{ */
/** The object isn't initialized. */
#define EVENT_STATE_UNINITIALIZED   0
/** The semaphore is signaled. */
#define EVENT_STATE_SIGNALED        0xff00ff00
/** The semaphore is not signaled. */
#define EVENT_STATE_NOT_SIGNALED    0x00ff00ff
/** @} */


RTDECL(int)  RTSemEventCreate(PRTSEMEVENT phEventSem)
{
    return RTSemEventCreateEx(phEventSem, 0 /*fFlags*/, NIL_RTLOCKVALCLASS, NULL);
}


RTDECL(int)  RTSemEventCreateEx(PRTSEMEVENT phEventSem, uint32_t fFlags, RTLOCKVALCLASS hClass, const char *pszNameFmt, ...)
{
    AssertReturn(!(fFlags & ~(RTSEMEVENT_FLAGS_NO_LOCK_VAL | RTSEMEVENT_FLAGS_BOOTSTRAP_HACK)), VERR_INVALID_PARAMETER);
    Assert(!(fFlags & RTSEMEVENT_FLAGS_BOOTSTRAP_HACK) || (fFlags & RTSEMEVENT_FLAGS_NO_LOCK_VAL));

    /*
     * Allocate semaphore handle.
     */
    int rc;
    struct RTSEMEVENTINTERNAL *pThis;
    if (!(fFlags & RTSEMEVENT_FLAGS_BOOTSTRAP_HACK))
        pThis = (struct RTSEMEVENTINTERNAL *)RTMemAlloc(sizeof(*pThis));
    else
        pThis = (struct RTSEMEVENTINTERNAL *)rtMemBaseAlloc(sizeof(*pThis));
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

                    ASMAtomicWriteU32(&pThis->u32State, EVENT_STATE_NOT_SIGNALED);
                    ASMAtomicWriteU32(&pThis->cWaiters, 0);
                    pThis->fFlags = fFlags;
#ifdef RTSEMEVENT_STRICT
                    if (!pszNameFmt)
                    {
                        static uint32_t volatile s_iSemEventAnon = 0;
                        RTLockValidatorRecSharedInit(&pThis->Signallers, hClass, RTLOCKVAL_SUB_CLASS_ANY, pThis,
                                                     true /*fSignaller*/, !(fFlags & RTSEMEVENT_FLAGS_NO_LOCK_VAL),
                                                     "RTSemEvent-%u", ASMAtomicIncU32(&s_iSemEventAnon) - 1);
                    }
                    else
                    {
                        va_list va;
                        va_start(va, pszNameFmt);
                        RTLockValidatorRecSharedInitV(&pThis->Signallers, hClass, RTLOCKVAL_SUB_CLASS_ANY, pThis,
                                                      true /*fSignaller*/, !(fFlags & RTSEMEVENT_FLAGS_NO_LOCK_VAL),
                                                      pszNameFmt, va);
                        va_end(va);
                    }
                    pThis->fEverHadSignallers = false;
#else
                    RT_NOREF_PV(hClass); RT_NOREF_PV(pszNameFmt);
#endif

                    *phEventSem = pThis;
                    return VINF_SUCCESS;
                }
                pthread_cond_destroy(&pThis->Cond);
            }
            pthread_condattr_destroy(&CondAttr);
        }

        rc = RTErrConvertFromErrno(rc);
        if (!(fFlags & RTSEMEVENT_FLAGS_BOOTSTRAP_HACK))
            RTMemFree(pThis);
        else
            rtMemBaseFree(pThis);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


RTDECL(int)  RTSemEventDestroy(RTSEMEVENT hEventSem)
{
    /*
     * Validate handle.
     */
    struct RTSEMEVENTINTERNAL *pThis = hEventSem;
    if (pThis == NIL_RTSEMEVENT)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    uint32_t    u32 = pThis->u32State;
    AssertReturn(u32 == EVENT_STATE_NOT_SIGNALED || u32 == EVENT_STATE_SIGNALED, VERR_INVALID_HANDLE);

    /*
     * Abort all waiters forcing them to return failure.
     */
    int rc;
    for (int i = 30; i > 0; i--)
    {
        ASMAtomicWriteU32(&pThis->u32State, EVENT_STATE_UNINITIALIZED);
        rc = pthread_cond_destroy(&pThis->Cond);
        if (rc != EBUSY)
            break;
        pthread_cond_broadcast(&pThis->Cond);
        usleep(1000);
    }
    if (rc)
    {
        AssertMsgFailed(("Failed to destroy event sem %p, rc=%d.\n", pThis, rc));
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
        AssertMsgFailed(("Failed to destroy event sem %p, rc=%d. (mutex)\n", pThis, rc));
        return RTErrConvertFromErrno(rc);
    }

    /*
     * Free the semaphore memory and be gone.
     */
#ifdef RTSEMEVENT_STRICT
    RTLockValidatorRecSharedDelete(&pThis->Signallers);
#endif
    if (!(pThis->fFlags & RTSEMEVENT_FLAGS_BOOTSTRAP_HACK))
        RTMemFree(pThis);
    else
        rtMemBaseFree(pThis);
    return VINF_SUCCESS;
}


RTDECL(int)  RTSemEventSignal(RTSEMEVENT hEventSem)
{
    /*
     * Validate input.
     */
    struct RTSEMEVENTINTERNAL *pThis = hEventSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    uint32_t    u32 = pThis->u32State;
    AssertReturn(u32 == EVENT_STATE_NOT_SIGNALED || u32 == EVENT_STATE_SIGNALED, VERR_INVALID_HANDLE);

#ifdef RTSEMEVENT_STRICT
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
        AssertMsgFailed(("Failed to lock event sem %p, rc=%d.\n", hEventSem, rc));
        return RTErrConvertFromErrno(rc);
    }

    /*
     * Check the state.
     */
    if (pThis->u32State == EVENT_STATE_NOT_SIGNALED)
    {
        ASMAtomicWriteU32(&pThis->u32State, EVENT_STATE_SIGNALED);
        rc = pthread_cond_signal(&pThis->Cond);
        AssertMsg(!rc, ("Failed to signal event sem %p, rc=%d.\n", hEventSem, rc));
    }
    else if (pThis->u32State == EVENT_STATE_SIGNALED)
    {
        rc = pthread_cond_signal(&pThis->Cond); /* give'm another kick... */
        AssertMsg(!rc, ("Failed to signal event sem %p, rc=%d. (2)\n", hEventSem, rc));
    }
    else
        rc = VERR_SEM_DESTROYED;

    /*
     * Release the mutex and return.
     */
    int rc2 = pthread_mutex_unlock(&pThis->Mutex);
    AssertMsg(!rc2, ("Failed to unlock event sem %p, rc=%d.\n", hEventSem, rc));
    if (rc)
        return RTErrConvertFromErrno(rc);
    if (rc2)
        return RTErrConvertFromErrno(rc2);

    return VINF_SUCCESS;
}


/**
 * Handle polling (timeout already expired at the time of the call).
 *
 * @returns VINF_SUCCESS, VERR_TIMEOUT, VERR_SEM_DESTROYED.
 * @param   pThis               The semaphore.
 */
DECLINLINE(int) rtSemEventPosixWaitPoll(struct RTSEMEVENTINTERNAL *pThis)
{
    int rc = pthread_mutex_lock(&pThis->Mutex);
    AssertMsgReturn(!rc, ("Failed to lock event sem %p, rc=%d.\n", pThis, rc), RTErrConvertFromErrno(rc));

    uint32_t u32OldState;
    bool fSuccess = ASMAtomicCmpXchgExU32(&pThis->u32State, EVENT_STATE_NOT_SIGNALED, EVENT_STATE_SIGNALED, &u32OldState);

    rc = pthread_mutex_unlock(&pThis->Mutex);
    AssertMsg(!rc, ("Failed to unlock event sem %p, rc=%d.\n", pThis, rc)); NOREF(rc);

    return fSuccess
         ? VINF_SUCCESS
         : u32OldState != EVENT_STATE_UNINITIALIZED
         ? VERR_TIMEOUT
         : VERR_SEM_DESTROYED;
}


/**
 * Performs an indefinite wait on the event.
 */
static int rtSemEventPosixWaitIndefinite(struct RTSEMEVENTINTERNAL *pThis, uint32_t fFlags, PCRTLOCKVALSRCPOS pSrcPos)
{
    RT_NOREF_PV(pSrcPos);

    /* for fairness, yield before going to sleep. */
    if (    ASMAtomicIncU32(&pThis->cWaiters) > 1
        &&  pThis->u32State == EVENT_STATE_SIGNALED)
        sched_yield();

     /* take mutex */
    int rc = pthread_mutex_lock(&pThis->Mutex);
    if (rc)
    {
        ASMAtomicDecU32(&pThis->cWaiters);
        AssertMsgFailed(("Failed to lock event sem %p, rc=%d.\n", pThis, rc));
        return RTErrConvertFromErrno(rc);
    }

    for (;;)
    {
        /* check state. */
        if (pThis->u32State == EVENT_STATE_SIGNALED)
        {
            ASMAtomicWriteU32(&pThis->u32State, EVENT_STATE_NOT_SIGNALED);
            ASMAtomicDecU32(&pThis->cWaiters);
            rc = pthread_mutex_unlock(&pThis->Mutex);
            AssertMsg(!rc, ("Failed to unlock event sem %p, rc=%d.\n", pThis, rc)); NOREF(rc);
            return VINF_SUCCESS;
        }
        if (pThis->u32State == EVENT_STATE_UNINITIALIZED)
        {
            rc = pthread_mutex_unlock(&pThis->Mutex);
            AssertMsg(!rc, ("Failed to unlock event sem %p, rc=%d.\n", pThis, rc)); NOREF(rc);
            return VERR_SEM_DESTROYED;
        }

        /* wait */
#ifdef RTSEMEVENT_STRICT
        RTTHREAD hThreadSelf = !(pThis->fFlags & RTSEMEVENT_FLAGS_BOOTSTRAP_HACK)
                             ? RTThreadSelfAutoAdopt()
                             : RTThreadSelf();
        if (pThis->fEverHadSignallers)
        {
            rc = RTLockValidatorRecSharedCheckBlocking(&pThis->Signallers, hThreadSelf, pSrcPos, false,
                                                       RT_INDEFINITE_WAIT, RTTHREADSTATE_EVENT, true);
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
        RTThreadBlocking(hThreadSelf, RTTHREADSTATE_EVENT, true);
        RT_NOREF_PV(fFlags); /** @todo interruptible wait is not implementable... */
        rc = pthread_cond_wait(&pThis->Cond, &pThis->Mutex);
        RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_EVENT);
        if (rc)
        {
            AssertMsgFailed(("Failed to wait on event sem %p, rc=%d.\n", pThis, rc));
            ASMAtomicDecU32(&pThis->cWaiters);
            int rc2 = pthread_mutex_unlock(&pThis->Mutex);
            AssertMsg(!rc2, ("Failed to unlock event sem %p, rc=%d.\n", pThis, rc2)); NOREF(rc2);
            return RTErrConvertFromErrno(rc);
        }
    }
}


/**
 * Performs an timed wait on the event.
 */
static int rtSemEventPosixWaitTimed(struct RTSEMEVENTINTERNAL *pThis, uint32_t fFlags, uint64_t uTimeout,
                                    PCRTLOCKVALSRCPOS pSrcPos)
{
    /*
     * Convert the timeout specification to absolute and relative deadlines,
     * divierting polling and infinite waits to the appropriate workers.
     */
    struct timespec AbsDeadline         = { 0, 0 };
    uint64_t const  cNsRelativeDeadline = rtSemPosixCalcDeadline(fFlags, uTimeout, pThis->fMonotonicClock, &AbsDeadline);
    if (cNsRelativeDeadline == 0)
        return rtSemEventPosixWaitPoll(pThis);
    if (cNsRelativeDeadline == UINT64_MAX)
        return rtSemEventPosixWaitIndefinite(pThis, fFlags, pSrcPos);

    /*
     * Now to the business of waiting...
     */

    /* for fairness, yield before going to sleep. */
    if (ASMAtomicIncU32(&pThis->cWaiters) > 1)
        sched_yield();

    /* take mutex */
    int rc = pthread_mutex_lock(&pThis->Mutex);
    if (rc)
    {
        ASMAtomicDecU32(&pThis->cWaiters);
        AssertMsg(rc == ETIMEDOUT, ("Failed to lock event sem %p, rc=%d.\n", pThis, rc));
        return RTErrConvertFromErrno(rc);
    }

    for (;;)
    {
        /* check state. */
        uint32_t const u32State = pThis->u32State;
        if (u32State != EVENT_STATE_NOT_SIGNALED)
        {
            if (u32State == EVENT_STATE_SIGNALED)
            {
                ASMAtomicWriteU32(&pThis->u32State, EVENT_STATE_NOT_SIGNALED);
                ASMAtomicDecU32(&pThis->cWaiters);
                rc = VINF_SUCCESS;
            }
            else
            {
                Assert(u32State == EVENT_STATE_UNINITIALIZED);
                rc = VERR_SEM_DESTROYED;
            }
            int rc2 = pthread_mutex_unlock(&pThis->Mutex);
            AssertMsg(!rc2, ("Failed to unlock event sem %p, rc2=%d.\n", pThis, rc2)); RT_NOREF(rc2);
            return rc;
        }

        /* wait */
#ifdef RTSEMEVENT_STRICT
        RTTHREAD hThreadSelf = !(pThis->fFlags & RTSEMEVENT_FLAGS_BOOTSTRAP_HACK)
                             ? RTThreadSelfAutoAdopt()
                             : RTThreadSelf();
        if (pThis->fEverHadSignallers)
        {
            rc = RTLockValidatorRecSharedCheckBlocking(&pThis->Signallers, hThreadSelf, pSrcPos, false,
                                                       (cNsRelativeDeadline + RT_NS_1MS - 1) / RT_NS_1MS,
                                                       RTTHREADSTATE_EVENT, true);
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
        RTThreadBlocking(hThreadSelf, RTTHREADSTATE_EVENT, true);
        rc = pthread_cond_timedwait(&pThis->Cond, &pThis->Mutex, &AbsDeadline);
        RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_EVENT);

        /* According to SuS this function shall not return EINTR, but linux man page might have said differently at some point... */
        if (   rc != 0
            && (   rc != EINTR
                || !(fFlags & RTSEMWAIT_FLAGS_NORESUME)))
        {
            AssertMsg(rc == ETIMEDOUT, ("Failed to wait on event sem %p, rc=%d.\n", pThis, rc));
            ASMAtomicDecU32(&pThis->cWaiters);
            int rc2 = pthread_mutex_unlock(&pThis->Mutex);
            AssertMsg(!rc2, ("Failed to unlock event sem %p, rc2=%d.\n", pThis, rc2)); NOREF(rc2);
            return RTErrConvertFromErrno(rc);
        }
    } /* for (;;) */
}


/**
 * Internal wait worker function.
 */
DECLINLINE(int) rtSemEventPosixWait(RTSEMEVENT hEventSem, uint32_t fFlags, uint64_t uTimeout, PCRTLOCKVALSRCPOS pSrcPos)
{
    /*
     * Validate input.
     */
    struct RTSEMEVENTINTERNAL *pThis = hEventSem;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    uint32_t    u32 = pThis->u32State;
    AssertReturn(u32 == EVENT_STATE_NOT_SIGNALED || u32 == EVENT_STATE_SIGNALED, VERR_INVALID_HANDLE);
    AssertReturn(RTSEMWAIT_FLAGS_ARE_VALID(fFlags), VERR_INVALID_PARAMETER);

    /*
     * Timed or indefinite wait?
     */
    if (fFlags & RTSEMWAIT_FLAGS_INDEFINITE)
        return rtSemEventPosixWaitIndefinite(pThis, fFlags, pSrcPos);
    return rtSemEventPosixWaitTimed(hEventSem, fFlags, uTimeout, pSrcPos);
}


RTDECL(int) RTSemEventWait(RTSEMEVENT hEventSem, RTMSINTERVAL cMillies)
{
    int rc;
#ifndef RTSEMEVENT_STRICT
    if (cMillies == RT_INDEFINITE_WAIT)
        rc = rtSemEventPosixWait(hEventSem, RTSEMWAIT_FLAGS_RESUME | RTSEMWAIT_FLAGS_INDEFINITE, 0, NULL);
    else
        rc = rtSemEventPosixWait(hEventSem, RTSEMWAIT_FLAGS_RESUME | RTSEMWAIT_FLAGS_RELATIVE | RTSEMWAIT_FLAGS_MILLISECS,
                                 cMillies, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    if (cMillies == RT_INDEFINITE_WAIT)
        rc = rtSemEventPosixWait(hEventSem, RTSEMWAIT_FLAGS_RESUME | RTSEMWAIT_FLAGS_INDEFINITE, 0, &SrcPos);
    else
        rc = rtSemEventPosixWait(hEventSem, RTSEMWAIT_FLAGS_RESUME | RTSEMWAIT_FLAGS_RELATIVE | RTSEMWAIT_FLAGS_MILLISECS,
                                 cMillies, &SrcPos);
#endif
    Assert(rc != VERR_INTERRUPTED);
    return rc;
}


RTDECL(int)  RTSemEventWaitNoResume(RTSEMEVENT hEventSem, RTMSINTERVAL cMillies)
{
    int rc;
#ifndef RTSEMEVENT_STRICT
    if (cMillies == RT_INDEFINITE_WAIT)
        rc = rtSemEventPosixWait(hEventSem, RTSEMWAIT_FLAGS_NORESUME | RTSEMWAIT_FLAGS_INDEFINITE, 0, NULL);
    else
        rc = rtSemEventPosixWait(hEventSem, RTSEMWAIT_FLAGS_NORESUME | RTSEMWAIT_FLAGS_RELATIVE | RTSEMWAIT_FLAGS_MILLISECS,
                                 cMillies, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    if (cMillies == RT_INDEFINITE_WAIT)
        rc = rtSemEventPosixWait(hEventSem, RTSEMWAIT_FLAGS_NORESUME | RTSEMWAIT_FLAGS_INDEFINITE, 0, &SrcPos);
    else
        rc = rtSemEventPosixWait(hEventSem, RTSEMWAIT_FLAGS_NORESUME | RTSEMWAIT_FLAGS_RELATIVE | RTSEMWAIT_FLAGS_MILLISECS,
                                 cMillies, &SrcPos);
#endif
    Assert(rc != VERR_INTERRUPTED);
    return rc;
}


RTDECL(int)  RTSemEventWaitEx(RTSEMEVENT hEventSem, uint32_t fFlags, uint64_t uTimeout)
{
#ifndef RTSEMEVENT_STRICT
    return rtSemEventPosixWait(hEventSem, fFlags, uTimeout, NULL);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return rtSemEventPosixWait(hEventSem, fFlags, uTimeout, &SrcPos);
#endif
}


RTDECL(int)  RTSemEventWaitExDebug(RTSEMEVENT hEventSem, uint32_t fFlags, uint64_t uTimeout,
                                   RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return rtSemEventPosixWait(hEventSem, fFlags, uTimeout, &SrcPos);
}


RTDECL(uint32_t) RTSemEventGetResolution(void)
{
    /** @todo we have 1ns parameter resolution, but we need to check each host
     *        what the actual resolution might be once the parameter makes it to the
     *        kernel and is processed there. */
    return 1;
}


RTDECL(void) RTSemEventSetSignaller(RTSEMEVENT hEventSem, RTTHREAD hThread)
{
#ifdef RTSEMEVENT_STRICT
    struct RTSEMEVENTINTERNAL *pThis = hEventSem;
    AssertPtrReturnVoid(pThis);
    uint32_t u32 = pThis->u32State;
    AssertReturnVoid(u32 == EVENT_STATE_NOT_SIGNALED || u32 == EVENT_STATE_SIGNALED);

    ASMAtomicWriteBool(&pThis->fEverHadSignallers, true);
    RTLockValidatorRecSharedResetOwner(&pThis->Signallers, hThread, NULL);
#else
    RT_NOREF_PV(hEventSem); RT_NOREF_PV(hThread);
#endif
}


RTDECL(void) RTSemEventAddSignaller(RTSEMEVENT hEventSem, RTTHREAD hThread)
{
#ifdef RTSEMEVENT_STRICT
    struct RTSEMEVENTINTERNAL *pThis = hEventSem;
    AssertPtrReturnVoid(pThis);
    uint32_t u32 = pThis->u32State;
    AssertReturnVoid(u32 == EVENT_STATE_NOT_SIGNALED || u32 == EVENT_STATE_SIGNALED);

    ASMAtomicWriteBool(&pThis->fEverHadSignallers, true);
    RTLockValidatorRecSharedAddOwner(&pThis->Signallers, hThread, NULL);
#else
    RT_NOREF_PV(hEventSem); RT_NOREF_PV(hThread);
#endif
}


RTDECL(void) RTSemEventRemoveSignaller(RTSEMEVENT hEventSem, RTTHREAD hThread)
{
#ifdef RTSEMEVENT_STRICT
    struct RTSEMEVENTINTERNAL *pThis = hEventSem;
    AssertPtrReturnVoid(pThis);
    uint32_t u32 = pThis->u32State;
    AssertReturnVoid(u32 == EVENT_STATE_NOT_SIGNALED || u32 == EVENT_STATE_SIGNALED);

    RTLockValidatorRecSharedRemoveOwner(&pThis->Signallers, hThread);
#else
    RT_NOREF_PV(hEventSem); RT_NOREF_PV(hThread);
#endif
}

