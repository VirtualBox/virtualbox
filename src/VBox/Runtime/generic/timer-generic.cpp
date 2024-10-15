/* $Id: timer-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Timers, Generic.
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
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The internal representation of a timer handle.
 */
typedef struct RTTIMER
{
    /** Magic.
     * This is RTTIMER_MAGIC, but changes to something else before the timer
     * is destroyed to indicate clearly that thread should exit. */
    uint32_t volatile       u32Magic;
    /** Flag indicating the timer is suspended. */
    uint8_t volatile        fSuspended;
    /** Flag indicating that the timer has been destroyed. */
    uint8_t volatile        fDestroyed;
    /** Callback. */
    PFNRTTIMER              pfnTimer;
    /** User argument. */
    void                   *pvUser;
    /** The timer thread. */
    RTTHREAD                Thread;
    /** Event semaphore on which the thread is blocked. */
    RTSEMEVENT              Event;
    /** The timer interval. 0 if one-shot. */
    uint64_t                u64NanoInterval;
    /** The start of the current run (ns).
     * This is used to calculate when the timer ought to fire the next time. */
    uint64_t volatile       u64StartTS;
    /** The start of the current run (ns).
     * This is used to calculate when the timer ought to fire the next time. */
    uint64_t volatile       u64NextTS;
    /** The current tick number (since u64StartTS). */
    uint64_t volatile       iTick;
} RTTIMER;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int) rtTimerThread(RTTHREAD Thread, void *pvUser);


RTDECL(int) RTTimerCreateEx(PRTTIMER *ppTimer, uint64_t u64NanoInterval, uint32_t fFlags, PFNRTTIMER pfnTimer, void *pvUser)
{
    *ppTimer = NULL;

    /*
     * We don't support the fancy MP features.
     */
    if (fFlags & RTTIMER_FLAGS_CPU_SPECIFIC)
        return VERR_NOT_SUPPORTED;

    /*
     * Allocate and initialize the timer handle.
     */
    PRTTIMER pTimer = (PRTTIMER)RTMemAlloc(sizeof(*pTimer));
    if (!pTimer)
        return VERR_NO_MEMORY;

    pTimer->u32Magic = RTTIMER_MAGIC;
    pTimer->fSuspended = true;
    pTimer->fDestroyed = false;
    pTimer->pfnTimer = pfnTimer;
    pTimer->pvUser = pvUser;
    pTimer->Thread = NIL_RTTHREAD;
    pTimer->Event = NIL_RTSEMEVENT;
    pTimer->u64NanoInterval = u64NanoInterval;
    pTimer->u64StartTS = 0;

    int rc = RTSemEventCreate(&pTimer->Event);
    if (RT_SUCCESS(rc))
    {
        rc = RTThreadCreate(&pTimer->Thread, rtTimerThread, pTimer, 0, RTTHREADTYPE_TIMER, RTTHREADFLAGS_WAITABLE, "Timer");
        if (RT_SUCCESS(rc))
        {
            *ppTimer = pTimer;
            return VINF_SUCCESS;
        }

        pTimer->u32Magic = 0;
        RTSemEventDestroy(pTimer->Event);
        pTimer->Event = NIL_RTSEMEVENT;
    }
    RTMemFree(pTimer);

    return rc;
}
RT_EXPORT_SYMBOL(RTTimerCreateEx);


/**
 * Validates the timer handle.
 *
 * @returns true if valid, false if invalid.
 * @param   pTimer  The handle.
 */
DECLINLINE(bool) rtTimerIsValid(PRTTIMER pTimer)
{
    AssertPtrReturn(pTimer, false);
    AssertReturn(pTimer->u32Magic == RTTIMER_MAGIC, false);
    AssertReturn(!pTimer->fDestroyed, false);
    return true;
}


RTDECL(int) RTTimerDestroy(PRTTIMER pTimer)
{
    /* It's ok to pass NULL pointer. */
    if (pTimer == /*NIL_RTTIMER*/ NULL)
        return VINF_SUCCESS;
    if (!rtTimerIsValid(pTimer))
        return VERR_INVALID_HANDLE;

    /*
     * If the timer is active, we stop and destruct it in one go, to avoid
     * unnecessary waiting for the next tick. If it's suspended we can safely
     * set the destroy flag and signal it.
     */
    RTTHREAD Thread = pTimer->Thread;
    if (!pTimer->fSuspended)
        ASMAtomicXchgU8(&pTimer->fSuspended, true);
    ASMAtomicXchgU8(&pTimer->fDestroyed, true);
    int rc = RTSemEventSignal(pTimer->Event);
    if (rc == VERR_ALREADY_POSTED)
        rc = VINF_SUCCESS;
    AssertRC(rc);

    RTThreadWait(Thread, 250, NULL);
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTTimerDestroy);


RTDECL(int) RTTimerStart(PRTTIMER pTimer, uint64_t u64First)
{
    if (!rtTimerIsValid(pTimer))
        return VERR_INVALID_HANDLE;
    if (!pTimer->fSuspended)
        return VERR_TIMER_ACTIVE;

    /*
     * Calc when it should start firing and give the thread a kick so it get going.
     */
    u64First += RTTimeNanoTS();
    ASMAtomicXchgU64(&pTimer->iTick, 0);
    ASMAtomicXchgU64(&pTimer->u64StartTS, u64First);
    ASMAtomicXchgU64(&pTimer->u64NextTS, u64First);
    ASMAtomicXchgU8(&pTimer->fSuspended, false);
    int rc = RTSemEventSignal(pTimer->Event);
    if (rc == VERR_ALREADY_POSTED)
        rc = VINF_SUCCESS;
    AssertRC(rc);
    return rc;
}
RT_EXPORT_SYMBOL(RTTimerStart);


RTDECL(int) RTTimerStop(PRTTIMER pTimer)
{
    if (!rtTimerIsValid(pTimer))
        return VERR_INVALID_HANDLE;
    if (pTimer->fSuspended)
        return VERR_TIMER_SUSPENDED;

    /*
     * Mark it as suspended and kick the thread.
     */
    ASMAtomicXchgU8(&pTimer->fSuspended, true);
    int rc = RTSemEventSignal(pTimer->Event);
    if (rc == VERR_ALREADY_POSTED)
        rc = VINF_SUCCESS;
    AssertRC(rc);
    return rc;
}
RT_EXPORT_SYMBOL(RTTimerStop);


RTDECL(int) RTTimerChangeInterval(PRTTIMER pTimer, uint64_t u64NanoInterval)
{
    if (!rtTimerIsValid(pTimer))
        return VERR_INVALID_HANDLE;
    NOREF(u64NanoInterval);
    return VERR_NOT_SUPPORTED;
}
RT_EXPORT_SYMBOL(RTTimerChangeInterval);


static DECLCALLBACK(int) rtTimerThread(RTTHREAD hThreadSelf, void *pvUser)
{
    PRTTIMER pTimer = (PRTTIMER)pvUser;
    NOREF(hThreadSelf);

    /*
     * The loop.
     */
    while (!pTimer->fDestroyed)
    {
        if (pTimer->fSuspended)
        {
            int rc = RTSemEventWait(pTimer->Event, RT_INDEFINITE_WAIT);
            if (RT_FAILURE(rc) && rc != VERR_INTERRUPTED)
            {
                AssertRC(rc);
                RTThreadSleep(1000); /* Don't cause trouble! */
            }
        }
        else
        {
            const uint64_t u64NanoTS = RTTimeNanoTS();
            if (u64NanoTS >= pTimer->u64NextTS)
            {
                pTimer->iTick++;

                /* one shot? */
                if (!pTimer->u64NanoInterval)
                    ASMAtomicXchgU8(&pTimer->fSuspended, true);
                pTimer->pfnTimer(pTimer, pTimer->pvUser, pTimer->iTick);

                /* status changed? */
                if (pTimer->fSuspended || pTimer->fDestroyed)
                    continue;

                /* calc the next time we should fire. */
                pTimer->u64NextTS = pTimer->u64StartTS + pTimer->iTick * pTimer->u64NanoInterval;
                if (pTimer->u64NextTS < u64NanoTS)
#ifdef IN_RING3 /* In ring-3 we'll catch up lost ticks immediately. */
                    pTimer->u64NextTS = u64NanoTS + 1;
#else
                    pTimer->u64NextTS = u64NanoTS + RTTimerGetSystemGranularity() / 2;
#endif
            }

            /* block. */
            uint64_t cNanoSeconds = pTimer->u64NextTS - u64NanoTS;
#ifdef IN_RING3 /* In ring-3 we'll catch up lost ticks immediately. */
            if (cNanoSeconds > 10)
#endif
            {
                int rc = RTSemEventWait(pTimer->Event, cNanoSeconds < 1000000 ? 1 : cNanoSeconds / 1000000);
                if (RT_FAILURE(rc) && rc != VERR_INTERRUPTED && rc != VERR_TIMEOUT)
                {
                    AssertRC(rc);
                    RTThreadSleep(1000); /* Don't cause trouble! */
                }
            }
        }
    }

    /*
     * Release the timer resources.
     */
    ASMAtomicIncU32(&pTimer->u32Magic); /* make the handle invalid. */
    int rc = RTSemEventDestroy(pTimer->Event); AssertRC(rc);
    pTimer->Event = NIL_RTSEMEVENT;
    pTimer->Thread = NIL_RTTHREAD;
    RTMemFree(pTimer);

    return VINF_SUCCESS;
}




RTDECL(uint32_t) RTTimerGetSystemGranularity(void)
{
    return 10000000; /* 10ms */
}
RT_EXPORT_SYMBOL(RTTimerGetSystemGranularity);


RTDECL(int) RTTimerRequestSystemGranularity(uint32_t u32Request, uint32_t *pu32Granted)
{
    NOREF(u32Request); NOREF(pu32Granted);
    return VERR_NOT_SUPPORTED;
}
RT_EXPORT_SYMBOL(RTTimerRequestSystemGranularity);


RTDECL(int) RTTimerReleaseSystemGranularity(uint32_t u32Granted)
{
    NOREF(u32Granted);
    return VERR_NOT_SUPPORTED;
}
RT_EXPORT_SYMBOL(RTTimerReleaseSystemGranularity);


RTDECL(bool) RTTimerCanDoHighResolution(void)
{
    return false;
}
RT_EXPORT_SYMBOL(RTTimerCanDoHighResolution);
