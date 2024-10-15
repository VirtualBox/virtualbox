/* $Id: timerlr-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Low Resolution Timers, Generic.
 *
 * This code is more or less identical to timer-generic.cpp, so
 * bugfixes goes into both files.
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
#include <iprt/timer.h>
#include "internal/iprt.h"

#include <iprt/thread.h>
#include <iprt/err.h>
#include <iprt/assert.h>
#include <iprt/alloc.h>
#include <iprt/asm.h>
#include <iprt/semaphore.h>
#include <iprt/time.h>
#include <iprt/log.h>
#include "internal/magics.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The smallest interval for low resolution timers. */
#define RTTIMERLR_MIN_INTERVAL  RT_NS_100MS


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The internal representation of a timer handle.
 */
typedef struct RTTIMERLRINT
{
    /** Magic.
     * This is RTTIMERRT_MAGIC, but changes to something else before the timer
     * is destroyed to indicate clearly that thread should exit. */
    uint32_t volatile       u32Magic;
    /** Flag indicating the timer is suspended. */
    bool volatile           fSuspended;
    /** Flag indicating that the timer has been destroyed. */
    bool volatile           fDestroyed;
    /** Set when the thread is blocked. */
    bool volatile           fBlocked;
    bool                    fPadding;
    /** The timer interval. 0 if one-shot. */
    uint64_t volatile       u64NanoInterval;
    /** The start of the current run (ns).
     * This is used to calculate when the timer ought to fire the next time. */
    uint64_t volatile       u64StartTS;
    /** The start of the current run (ns).
     * This is used to calculate when the timer ought to fire the next time. */
    uint64_t volatile       u64NextTS;
    /** The current tick number (since u64StartTS). */
    uint64_t volatile       iTick;

    /** Callback. */
    PFNRTTIMERLR            pfnTimer;
    /** User argument. */
    void                   *pvUser;
    /** The timer thread. */
    RTTHREAD                hThread;
    /** Event semaphore on which the thread is blocked. */
    RTSEMEVENT              hEvent;
} RTTIMERLRINT;
typedef RTTIMERLRINT *PRTTIMERLRINT;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int) rtTimerLRThread(RTTHREAD hThread, void *pvUser);


RTDECL(int) RTTimerLRCreateEx(RTTIMERLR *phTimerLR, uint64_t u64NanoInterval, uint32_t fFlags, PFNRTTIMERLR pfnTimer, void *pvUser)
{
    AssertPtr(phTimerLR);
    *phTimerLR = NIL_RTTIMERLR;

    /*
     * We don't support the fancy MP features, nor intervals lower than 100 ms.
     */
    AssertReturn(!(fFlags & RTTIMER_FLAGS_CPU_SPECIFIC), VERR_NOT_SUPPORTED);
    AssertReturn(!u64NanoInterval || u64NanoInterval >= RTTIMERLR_MIN_INTERVAL, VERR_OUT_OF_RANGE);

    /*
     * Allocate and initialize the timer handle.
     */
    PRTTIMERLRINT pThis = (PRTTIMERLRINT)RTMemAlloc(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;

    pThis->u32Magic = RTTIMERLR_MAGIC;
    pThis->fSuspended = true;
    pThis->fDestroyed = false;
    pThis->fBlocked = false;
    pThis->fPadding = false;
    pThis->pfnTimer = pfnTimer;
    pThis->pvUser = pvUser;
    pThis->hThread = NIL_RTTHREAD;
    pThis->hEvent = NIL_RTSEMEVENT;
    pThis->u64NanoInterval = u64NanoInterval;
    pThis->u64StartTS = 0;

    int rc = RTSemEventCreate(&pThis->hEvent);
    if (RT_SUCCESS(rc))
    {
        rc = RTThreadCreate(&pThis->hThread, rtTimerLRThread, pThis, 0, RTTHREADTYPE_TIMER, RTTHREADFLAGS_WAITABLE, "TimerLR");
        if (RT_SUCCESS(rc))
        {
            *phTimerLR = pThis;
            return VINF_SUCCESS;
        }

        pThis->u32Magic = 0;
        RTSemEventDestroy(pThis->hEvent);
        pThis->hEvent = NIL_RTSEMEVENT;
    }
    RTMemFree(pThis);

    return rc;
}
RT_EXPORT_SYMBOL(RTTimerLRCreateEx);


RTDECL(int) RTTimerLRDestroy(RTTIMERLR hTimerLR)
{
    /*
     * Validate input, NIL is fine though.
     */
    if (hTimerLR == NIL_RTTIMERLR)
        return VINF_SUCCESS;
    PRTTIMERLRINT pThis = hTimerLR;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTTIMERLR_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(!pThis->fDestroyed, VERR_INVALID_HANDLE);

    /*
     * If the timer is active, we stop and destruct it in one go, to avoid
     * unnecessary waiting for the next tick. If it's suspended we can safely
     * set the destroy flag and signal it.
     */
    RTTHREAD hThread = pThis->hThread;
    if (!pThis->fSuspended)
        ASMAtomicWriteBool(&pThis->fSuspended, true);
    ASMAtomicWriteBool(&pThis->fDestroyed, true);
    int rc = RTSemEventSignal(pThis->hEvent);
    if (rc == VERR_ALREADY_POSTED)
        rc = VINF_SUCCESS;
    AssertRC(rc);

    RTThreadWait(hThread, 250, NULL);
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTTimerLRDestroy);


/**
 * Internal worker fro RTTimerLRStart and RTTiemrLRChangeInterval.
 */
static int rtTimerLRStart(PRTTIMERLRINT pThis, uint64_t u64First)
{
    if (!pThis->fSuspended)
        return VERR_TIMER_ACTIVE;

    /*
     * Calc when it should start firing and give the thread a kick so it get going.
     */
    u64First += RTTimeNanoTS();
    ASMAtomicWriteU64(&pThis->iTick, 0);
    ASMAtomicWriteU64(&pThis->u64StartTS, u64First);
    ASMAtomicWriteU64(&pThis->u64NextTS, u64First);
    ASMAtomicWriteBool(&pThis->fSuspended, false);
    int rc = RTSemEventSignal(pThis->hEvent);
    if (rc == VERR_ALREADY_POSTED)
        rc = VINF_SUCCESS;
    AssertRC(rc);
    return rc;
}


RTDECL(int) RTTimerLRStart(RTTIMERLR hTimerLR, uint64_t u64First)
{
    /*
     * Validate input.
     */
    PRTTIMERLRINT pThis = hTimerLR;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTTIMERLR_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(!pThis->fDestroyed, VERR_INVALID_HANDLE);
    AssertReturn(!u64First || u64First >= RTTIMERLR_MIN_INTERVAL, VERR_OUT_OF_RANGE);

    /*
     * Do the job.
     */
    return rtTimerLRStart(pThis, u64First);
}
RT_EXPORT_SYMBOL(RTTimerLRStart);


/**
 * Internal worker for RTTimerLRStop and RTTimerLRChangeInterval
 */
static int rtTimerLRStop(PRTTIMERLRINT pThis, bool fSynchronous)
{
    /*
     * Fail if already suspended.
     */
    if (pThis->fSuspended)
        return VERR_TIMER_SUSPENDED;

    /*
     * Mark it as suspended and kick the thread.
     * It's simpler to always reset the thread user semaphore, so we do that first.
     */
    int rc = RTThreadUserReset(pThis->hThread);
    AssertRC(rc);

    ASMAtomicWriteBool(&pThis->fSuspended, true);
    rc = RTSemEventSignal(pThis->hEvent);
    if (rc == VERR_ALREADY_POSTED)
        rc = VINF_SUCCESS;
    AssertRC(rc);

    /*
     * Wait for the thread to stop running if synchronous.
     */
    if (fSynchronous && RT_SUCCESS(rc))
    {
        rc = RTThreadUserWait(pThis->hThread, RT_MS_1MIN);
        AssertRC(rc);
    }

    return rc;
}


RTDECL(int) RTTimerLRStop(RTTIMERLR hTimerLR)
{
    /*
     * Validate input.
     */
    PRTTIMERLRINT pThis = hTimerLR;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTTIMERLR_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(!pThis->fDestroyed, VERR_INVALID_HANDLE);

    /*
     * Do the job.
     */
    return rtTimerLRStop(pThis, false);
}
RT_EXPORT_SYMBOL(RTTimerLRStop);


RTDECL(int) RTTimerLRChangeInterval(RTTIMERLR hTimerLR, uint64_t u64NanoInterval)
{
    /*
     * Validate input.
     */
    PRTTIMERLRINT pThis = hTimerLR;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTTIMERLR_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(!pThis->fDestroyed, VERR_INVALID_HANDLE);
    AssertReturn(!u64NanoInterval || u64NanoInterval >= RTTIMERLR_MIN_INTERVAL, VERR_OUT_OF_RANGE);

    /*
     * Do the job accoring to state and caller.
     */
    int rc;
    if (pThis->fSuspended)
    {
        /* Stopped: Just update the interval. */
        ASMAtomicWriteU64(&pThis->u64NanoInterval, u64NanoInterval);
        rc = VINF_SUCCESS;
    }
    else if (RTThreadSelf() == pThis->hThread)
    {
        /* Running: Updating interval from the callback. */
        uint64_t u64Now = RTTimeNanoTS();
        pThis->iTick           = 0;
        pThis->u64StartTS      = u64Now;
        pThis->u64NextTS       = u64Now;
        ASMAtomicWriteU64(&pThis->u64NanoInterval, u64NanoInterval);
        rc = VINF_SUCCESS;
    }
    else
    {
        /* Running: Stopping  */
        rc = rtTimerLRStop(pThis, true);
        if (RT_SUCCESS(rc))
        {
            ASMAtomicWriteU64(&pThis->u64NanoInterval, u64NanoInterval);
            rc = rtTimerLRStart(pThis, 0);
        }
    }

    return rc;
}
RT_EXPORT_SYMBOL(RTTimerLRChangeInterval);


static DECLCALLBACK(int) rtTimerLRThread(RTTHREAD hThreadSelf, void *pvUser)
{
    PRTTIMERLRINT pThis = (PRTTIMERLRINT)pvUser;
    NOREF(hThreadSelf);

    /*
     * The loop.
     */
    while (!ASMAtomicUoReadBool(&pThis->fDestroyed))
    {
        if (ASMAtomicUoReadBool(&pThis->fSuspended))
        {
            /* Signal rtTimerLRStop thread. */
            int rc = RTThreadUserSignal(hThreadSelf);
            AssertRC(rc);

            ASMAtomicWriteBool(&pThis->fBlocked, true);
            rc = RTSemEventWait(pThis->hEvent, RT_INDEFINITE_WAIT);
            if (RT_FAILURE(rc) && rc != VERR_INTERRUPTED)
            {
                AssertRC(rc);
                RTThreadSleep(1000); /* Don't cause trouble! */
            }
            ASMAtomicWriteBool(&pThis->fBlocked, false);
        }
        else
        {
            uint64_t        cNanoSeconds;
            const uint64_t  u64NanoTS = RTTimeNanoTS();
            uint64_t        u64NextTS = pThis->u64NextTS;
            if (u64NanoTS >= u64NextTS)
            {
                uint64_t iTick = ++pThis->iTick;
                pThis->pfnTimer(pThis, pThis->pvUser, iTick);

                /* status changed? */
                if (   ASMAtomicUoReadBool(&pThis->fSuspended)
                    || ASMAtomicUoReadBool(&pThis->fDestroyed))
                    continue;

                /*
                 * Read timer data (it's all volatile and better if we read it all at once):
                 */
                iTick = pThis->iTick;
                uint64_t const u64StartTS       = pThis->u64StartTS;
                uint64_t const u64NanoInterval  = pThis->u64NanoInterval;
                ASMCompilerBarrier();

                /*
                 * Suspend if one shot.
                 */
                if (!u64NanoInterval)
                {
                    ASMAtomicWriteBool(&pThis->fSuspended, true);
                    continue;
                }

                /*
                 * Calc the next time we should fire.
                 *
                 * If we're more than 60 intervals behind, just skip ahead. We
                 * don't want the timer thread running wild just because the
                 * clock changed in an unexpected way. As seen in @bugref{3611} this
                 * does happen during suspend/resume, but it may also happen
                 * if we're using a non-monotonic clock as time source.
                 */
                u64NextTS = u64StartTS + iTick * u64NanoInterval;
                if (RT_LIKELY(u64NextTS > u64NanoTS))
                    cNanoSeconds = u64NextTS - u64NanoTS;
                else
                {
                    uint64_t iActualTick = (u64NanoTS - u64StartTS) / u64NanoInterval;
                    if (iActualTick - iTick > 60)
                        pThis->iTick = iActualTick - 1;
#ifdef IN_RING0
                    cNanoSeconds = RTTimerGetSystemGranularity() / 2;
#else
                    cNanoSeconds = RT_NS_1MS;
#endif
                    u64NextTS = u64NanoTS + cNanoSeconds;
                }

                pThis->u64NextTS = u64NextTS;
            }
            else
                cNanoSeconds = u64NextTS - u64NanoTS;

            /* block. */
            ASMAtomicWriteBool(&pThis->fBlocked, true);
            int rc = RTSemEventWait(pThis->hEvent,
                                    (RTMSINTERVAL)(cNanoSeconds < 1000000 ? 1 : cNanoSeconds / 1000000));
            if (RT_FAILURE(rc) && rc != VERR_INTERRUPTED && rc != VERR_TIMEOUT)
            {
                AssertRC(rc);
                RTThreadSleep(1000); /* Don't cause trouble! */
            }
            ASMAtomicWriteBool(&pThis->fBlocked, false);
        }
    }

    /*
     * Release the timer resources.
     */
    ASMAtomicWriteU32(&pThis->u32Magic, ~RTTIMERLR_MAGIC); /* make the handle invalid. */
    int rc = RTSemEventDestroy(pThis->hEvent); AssertRC(rc);
    pThis->hEvent = NIL_RTSEMEVENT;
    pThis->hThread = NIL_RTTHREAD;
    RTMemFree(pThis);

    return VINF_SUCCESS;
}

