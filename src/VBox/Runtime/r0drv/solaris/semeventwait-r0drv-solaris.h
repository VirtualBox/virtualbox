/* $Id: semeventwait-r0drv-solaris.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Solaris Ring-0 Driver Helpers for Event Semaphore Waits.
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

#ifndef IPRT_INCLUDED_SRC_r0drv_solaris_semeventwait_r0drv_solaris_h
#define IPRT_INCLUDED_SRC_r0drv_solaris_semeventwait_r0drv_solaris_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "the-solaris-kernel.h"

#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/time.h>


/** The resolution (nanoseconds) specified when using timeout_generic. */
#define RTR0SEMSOLWAIT_RESOLUTION   50000

/** Disables the cyclic fallback code for old S10 installs - see @bugref{5342}.
 * @todo Fixed by @bugref{5595}, can be reenabled after checking out
 *       CY_HIGH_LEVEL. */
#define RTR0SEMSOLWAIT_NO_OLD_S10_FALLBACK

#define SOL_THREAD_TINTR_PTR        ((kthread_t **)((char *)curthread + g_offrtSolThreadIntrThread))


/**
 * Solaris semaphore wait structure.
 */
typedef struct RTR0SEMSOLWAIT
{
    /** The absolute timeout given as nanoseconds since the start of the
     *  monotonic clock. */
    uint64_t        uNsAbsTimeout;
    /** The timeout in nanoseconds relative to the start of the wait. */
    uint64_t        cNsRelTimeout;
    /** The native timeout value. */
    union
    {
        /** The timeout (in ticks) when fHighRes is false.  */
        clock_t     lTimeout;
    } u;
    /** Set if we use high resolution timeouts. */
    bool            fHighRes;
    /** Set if it's an indefinite wait. */
    bool            fIndefinite;
    /** Set if the waiting thread is ready to be woken up.
     * Avoids false setrun() calls due to temporary mutex exits. */
    bool volatile   fWantWakeup;
    /** Set if we've already timed out.
     * Set by rtR0SemSolWaitDoIt or rtR0SemSolWaitHighResTimeout, read by
     * rtR0SemSolWaitHasTimedOut. */
    bool volatile   fTimedOut;
    /** Whether the wait was interrupted. */
    bool            fInterrupted;
    /** Interruptible or uninterruptible wait. */
    bool            fInterruptible;
    /** The thread to wake up. */
    kthread_t      *pThread;
#ifndef RTR0SEMSOLWAIT_NO_OLD_S10_FALLBACK
    /** Cylic timer ID (used by the timeout callback). */
    cyclic_id_t     idCy;
#endif
    /** The mutex associated with the condition variable wait. */
    void volatile  *pvMtx;
} RTR0SEMSOLWAIT;
/** Pointer to a solaris semaphore wait structure.  */
typedef RTR0SEMSOLWAIT *PRTR0SEMSOLWAIT;


/**
 * Initializes a wait.
 *
 * The caller MUST check the wait condition BEFORE calling this function or the
 * timeout logic will be flawed.
 *
 * @returns VINF_SUCCESS or VERR_TIMEOUT.
 * @param   pWait               The wait structure.
 * @param   fFlags              The wait flags.
 * @param   uTimeout            The timeout.
 */
DECLINLINE(int) rtR0SemSolWaitInit(PRTR0SEMSOLWAIT pWait, uint32_t fFlags, uint64_t uTimeout)
{
    /*
     * Process the flags and timeout.
     */
    if (!(fFlags & RTSEMWAIT_FLAGS_INDEFINITE))
    {
        if (fFlags & RTSEMWAIT_FLAGS_MILLISECS)
            uTimeout = uTimeout < UINT64_MAX / RT_NS_1MS
                     ? uTimeout * RT_NS_1MS
                     : UINT64_MAX;
        if (uTimeout == UINT64_MAX)
            fFlags |= RTSEMWAIT_FLAGS_INDEFINITE;
        else
        {
            uint64_t u64Now;
            if (fFlags & RTSEMWAIT_FLAGS_RELATIVE)
            {
                if (uTimeout == 0)
                    return VERR_TIMEOUT;

                u64Now = RTTimeSystemNanoTS();
                pWait->cNsRelTimeout = uTimeout;
                pWait->uNsAbsTimeout = u64Now + uTimeout;
                if (pWait->uNsAbsTimeout < u64Now) /* overflow */
                    fFlags |= RTSEMWAIT_FLAGS_INDEFINITE;
            }
            else
            {
                u64Now = RTTimeSystemNanoTS();
                if (u64Now >= uTimeout)
                    return VERR_TIMEOUT;

                pWait->cNsRelTimeout = uTimeout - u64Now;
                pWait->uNsAbsTimeout = uTimeout;
            }
        }
    }

    if (!(fFlags & RTSEMWAIT_FLAGS_INDEFINITE))
    {
        pWait->fIndefinite      = false;
        if (  (   (fFlags & (RTSEMWAIT_FLAGS_NANOSECS | RTSEMWAIT_FLAGS_ABSOLUTE))
               || pWait->cNsRelTimeout < UINT32_C(1000000000) / 100 /*Hz*/ * 4)
#ifdef RTR0SEMSOLWAIT_NO_OLD_S10_FALLBACK
            && g_pfnrtR0Sol_timeout_generic != NULL
#endif
           )
            pWait->fHighRes     = true;
        else
        {
            uint64_t cTicks     = NSEC_TO_TICK_ROUNDUP(uTimeout);
            if (cTicks >= LONG_MAX)
                fFlags |= RTSEMWAIT_FLAGS_INDEFINITE;
            else
            {
                pWait->u.lTimeout = cTicks;
                pWait->fHighRes = false;
            }
        }
    }

    if (fFlags & RTSEMWAIT_FLAGS_INDEFINITE)
    {
        pWait->fIndefinite      = true;
        pWait->fHighRes         = false;
        pWait->uNsAbsTimeout    = UINT64_MAX;
        pWait->cNsRelTimeout    = UINT64_MAX;
        pWait->u.lTimeout       = LONG_MAX;
    }

    pWait->fWantWakeup      = false;
    pWait->fTimedOut        = false;
    pWait->fInterrupted     = false;
    pWait->fInterruptible   = !!(fFlags & RTSEMWAIT_FLAGS_INTERRUPTIBLE);
    pWait->pThread          = curthread;
    pWait->pvMtx            = NULL;
#ifndef RTR0SEMSOLWAIT_NO_OLD_S10_FALLBACK
    pWait->idCy             = CYCLIC_NONE;
#endif

    return VINF_SUCCESS;
}


#ifndef RTR0SEMSOLWAIT_NO_OLD_S10_FALLBACK
/**
 * Cyclic timeout callback that sets the timeout indicator and wakes up the
 * waiting thread.
 *
 * @param   pvUser              The wait structure.
 */
static void rtR0SemSolWaitHighResTimeout(void *pvUser)
{
    PRTR0SEMSOLWAIT pWait   = (PRTR0SEMSOLWAIT)pvUser;
    kthread_t      *pThread = pWait->pThread;
    kmutex_t       *pMtx    = (kmutex_t *)ASMAtomicReadPtr(&pWait->pvMtx);
    if (RT_VALID_PTR(pMtx))
    {
        /* Enter the mutex here to make sure the thread has gone to sleep
           before we wake it up.
           Note: Trying to take the cpu_lock here doesn't work. */
        mutex_enter(pMtx);
        if (mutex_owner(&cpu_lock) == curthread)
        {
            cyclic_remove(pWait->idCy);
            pWait->idCy = CYCLIC_NONE;
        }
        bool const fWantWakeup = pWait->fWantWakeup;
        ASMAtomicWriteBool(&pWait->fTimedOut, true);
        mutex_exit(pMtx);

        if (fWantWakeup)
            setrun(pThread);
    }
}
#endif


/**
 * Timeout callback that sets the timeout indicator and wakes up the waiting
 * thread.
 *
 * @param   pvUser              The wait structure.
 */
static void rtR0SemSolWaitTimeout(void *pvUser)
{
    PRTR0SEMSOLWAIT pWait   = (PRTR0SEMSOLWAIT)pvUser;
    kthread_t      *pThread = pWait->pThread;
    kmutex_t       *pMtx    = (kmutex_t *)ASMAtomicReadPtr((void * volatile *)&pWait->pvMtx);
    if (RT_VALID_PTR(pMtx))
    {
        /* Enter the mutex here to make sure the thread has gone to sleep
           before we wake it up. */
        mutex_enter(pMtx);
        bool const fWantWakeup = pWait->fWantWakeup;
        ASMAtomicWriteBool(&pWait->fTimedOut, true);
        mutex_exit(pMtx);

        if (fWantWakeup)
            setrun(pThread);
    }
}


/**
 * Do the actual wait.
 *
 * @param   pWait               The wait structure.
 * @param   pCnd                The condition variable to wait on.
 * @param   pMtx                The mutex related to the condition variable.
 *                              The caller has entered this.
 * @param   pfState             The state variable to check if have changed
 *                              after leaving the mutex (spinlock).
 * @param   fCurState           The current value of @a pfState.  We'll return
 *                              without sleeping if @a pfState doesn't hold
 *                              this value after reacquiring the mutex.
 *
 * @remarks This must be call with the object mutex (spinlock) held.
 */
DECLINLINE(void) rtR0SemSolWaitDoIt(PRTR0SEMSOLWAIT pWait, kcondvar_t *pCnd, kmutex_t *pMtx,
                                    uint32_t volatile *pfState, uint32_t const fCurState)
{
    union
    {
        callout_id_t    idCo;
        timeout_id_t    idTom;
    } u;

    u.idCo = 0; /* Silence a spurious gcc [-Wmaybe-uninitialized] warning. */

    /*
     * Arm the timeout callback.
     *
     * We will have to leave the mutex (spinlock) when doing this because S10
     * (didn't check S11) will not correctly preserve PIL across calls to
     * timeout_generic() - @bugref{5595}.  We do it for all timeout methods to
     * be on the safe side, the nice sideeffect of which is that it solves the
     * lock inversion problem found in @bugref{5342}.
     */
    bool const  fHasTimeout = !pWait->fIndefinite;
    bool        fGoToSleep  = !fHasTimeout;
    if (fHasTimeout)
    {
        pWait->fWantWakeup = false;             /* only want fTimedOut */
        ASMAtomicWritePtr(&pWait->pvMtx, pMtx); /* atomic is paranoia */
        mutex_exit(pMtx);

        if (pWait->fHighRes)
        {
#ifndef RTR0SEMSOLWAIT_NO_OLD_S10_FALLBACK
            if (g_pfnrtR0Sol_timeout_generic != NULL)
#endif
            {
                /*
                 * High resolution timeout - arm a high resolution timeout callback
                 * for waking up the thread at the desired time.
                 */
                u.idCo = g_pfnrtR0Sol_timeout_generic(CALLOUT_REALTIME, rtR0SemSolWaitTimeout, pWait,
                                                      pWait->uNsAbsTimeout, RTR0SEMSOLWAIT_RESOLUTION,
                                                      CALLOUT_FLAG_ABSOLUTE);
            }
#ifndef RTR0SEMSOLWAIT_NO_OLD_S10_FALLBACK
            else
            {
                /*
                 * High resolution timeout - arm a one-shot cyclic for waking up
                 * the thread at the desired time.
                 */
                cyc_handler_t   Cyh;
                Cyh.cyh_arg      = pWait;
                Cyh.cyh_func     = rtR0SemSolWaitHighResTimeout;
                Cyh.cyh_level    = CY_LOW_LEVEL; /// @todo try CY_LOCK_LEVEL and CY_HIGH_LEVEL?

                cyc_time_t      Cyt;
                Cyt.cyt_when     = pWait->uNsAbsTimeout;
                Cyt.cyt_interval = UINT64_C(1000000000) * 60;

                mutex_enter(&cpu_lock);
                pWait->idCy = cyclic_add(&Cyh, &Cyt);
                mutex_exit(&cpu_lock);
            }
#endif
        }
        else
        {
            /*
             * Normal timeout.
             * We're better off with our own callback like on the timeout man page,
             * than calling cv_timedwait[_sig]().
             */
            u.idTom = realtime_timeout(rtR0SemSolWaitTimeout, pWait, pWait->u.lTimeout);
        }

        /*
         * Reacquire the mutex and check if the sleep condition still holds and
         * that we didn't already time out.
         */
        mutex_enter(pMtx);
        pWait->fWantWakeup = true;
        fGoToSleep = !ASMAtomicUoReadBool(&pWait->fTimedOut)
                  && ASMAtomicReadU32(pfState) == fCurState;
    }

    /*
     * Do the waiting if that's still desirable.
     * (rc > 0 - normal wake-up; rc == 0 - interruption; rc == -1 - timeout)
     */
    if (fGoToSleep)
    {
        if (pWait->fInterruptible)
        {
            int rc = cv_wait_sig(pCnd, pMtx);
            if (RT_UNLIKELY(rc <= 0))
            {
                if (RT_LIKELY(rc == 0))
                    pWait->fInterrupted = true;
                else
                    AssertMsgFailed(("rc=%d\n", rc)); /* no timeouts, see above! */
            }
        }
        else
            cv_wait(pCnd, pMtx);
    }

    /*
     * Remove the timeout callback.  Drop the lock while we're doing that
     * to reduce lock contention / deadlocks.  Before dropping the lock,
     * indicate that the callback shouldn't do anything.
     *
     * (Too bad we are stuck with the cv_* API here, it's doing a little
     * bit too much.)
     */
    if (fHasTimeout)
    {
        pWait->fWantWakeup = false;
        ASMAtomicWritePtr(&pWait->pvMtx, NULL);
        mutex_exit(pMtx);

        if (pWait->fHighRes)
        {
#ifndef RTR0SEMSOLWAIT_NO_OLD_S10_FALLBACK
            if (g_pfnrtR0Sol_timeout_generic != NULL)
#endif
                g_pfnrtR0Sol_untimeout_generic(u.idCo, 0 /*nowait*/);
#ifndef RTR0SEMSOLWAIT_NO_OLD_S10_FALLBACK
            else
            {
                mutex_enter(&cpu_lock);
                if (pWait->idCy != CYCLIC_NONE)
                {
                    cyclic_remove(pWait->idCy);
                    pWait->idCy = CYCLIC_NONE;
                }
                mutex_exit(&cpu_lock);
            }
#endif
        }
        else
            untimeout(u.idTom);

        mutex_enter(pMtx);
    }
}


/**
 * Checks if a solaris wait was interrupted.
 *
 * @returns true / false
 * @param   pWait               The wait structure.
 * @remarks This shall be called before the first rtR0SemSolWaitDoIt().
 */
DECLINLINE(bool) rtR0SemSolWaitWasInterrupted(PRTR0SEMSOLWAIT pWait)
{
    return pWait->fInterrupted;
}


/**
 * Checks if a solaris wait has timed out.
 *
 * @returns true / false
 * @param   pWait               The wait structure.
 */
DECLINLINE(bool) rtR0SemSolWaitHasTimedOut(PRTR0SEMSOLWAIT pWait)
{
    return pWait->fTimedOut;
}


/**
 * Deletes a solaris wait.
 *
 * @param   pWait               The wait structure.
 */
DECLINLINE(void) rtR0SemSolWaitDelete(PRTR0SEMSOLWAIT pWait)
{
    pWait->pThread = NULL;
}


/**
 * Enters the mutex, unpinning the underlying current thread if contended and
 * we're on an interrupt thread.
 *
 * The unpinning is done to prevent a deadlock, see s this could lead to a
 * deadlock (see @bugref{4259} for the full explanation)
 *
 * @param   pMtx            The mutex to enter.
 */
DECLINLINE(void) rtR0SemSolWaitEnterMutexWithUnpinningHack(kmutex_t *pMtx)
{
    int fAcquired = mutex_tryenter(pMtx);
    if (!fAcquired)
    {
        /*
         * Note! This assumes nobody is using the RTThreadPreemptDisable() in an
         *       interrupt context and expects it to work right.  The swtch will
         *       result in a voluntary preemption.  To fix this, we would have to
         *       do our own counting in RTThreadPreemptDisable/Restore() like we do
         *       on systems which doesn't do preemption (OS/2, linux, ...) and
         *       check whether preemption was disabled via RTThreadPreemptDisable()
         *       or not and only call swtch if RTThreadPreemptDisable() wasn't called.
         */
        kthread_t **ppIntrThread = SOL_THREAD_TINTR_PTR;
        if (   *ppIntrThread
            && getpil() < DISP_LEVEL)
        {
            RTTHREADPREEMPTSTATE PreemptState = RTTHREADPREEMPTSTATE_INITIALIZER;
            RTThreadPreemptDisable(&PreemptState);
            preempt();
            RTThreadPreemptRestore(&PreemptState);
        }
        mutex_enter(pMtx);
    }
}


/**
 * Gets the max resolution of the timeout machinery.
 *
 * @returns Resolution specified in nanoseconds.
 */
DECLINLINE(uint32_t) rtR0SemSolWaitGetResolution(void)
{
    return g_pfnrtR0Sol_timeout_generic != NULL
         ? RTR0SEMSOLWAIT_RESOLUTION
         : cyclic_getres();
}

#endif /* !IPRT_INCLUDED_SRC_r0drv_solaris_semeventwait_r0drv_solaris_h */

