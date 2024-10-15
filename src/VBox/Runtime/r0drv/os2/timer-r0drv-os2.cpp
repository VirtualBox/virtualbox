/* $Id: timer-r0drv-os2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Memory Allocation, Ring-0 Driver, OS/2.
 */

/*
 * Contributed by knut st. osmundsen.
 *
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
 * --------------------------------------------------------------------
 *
 * This code is based on:
 *
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "the-os2-kernel.h"

#include <iprt/timer.h>
#include <iprt/time.h>
#include <iprt/spinlock.h>
#include <iprt/err.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/alloc.h>

#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The internal representation of an OS/2 timer handle.
 */
typedef struct RTTIMER
{
    /** Magic.
     * This is RTTIMER_MAGIC, but changes to something else before the timer
     * is destroyed to indicate clearly that thread should exit. */
    uint32_t volatile       u32Magic;
    /** The next timer in the timer list. */
    PRTTIMER                pNext;
    /** Flag indicating the timer is suspended. */
    uint8_t volatile        fSuspended;
    /** Cleared at the start of timer processing, set when calling pfnTimer.
     * If any timer changes occurs while doing the callback this will be used to resume the cycle. */
    bool                    fDone;
    /** Callback. */
    PFNRTTIMER              pfnTimer;
    /** User argument. */
    void                   *pvUser;
    /** The timer interval. 0 if one-shot. */
    uint64_t                u64NanoInterval;
    /** The start of the current run.
     * This is used to calculate when the timer ought to fire the next time. */
    uint64_t volatile       u64StartTS;
    /** The start of the current run.
     * This is used to calculate when the timer ought to fire the next time. */
    uint64_t volatile       u64NextTS;
    /** The current tick number (since u64StartTS). */
    uint64_t volatile       iTick;
} RTTIMER;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Spinlock protecting the timers. */
static RTSPINLOCK           g_Spinlock = NIL_RTSPINLOCK;
/** The timer head. */
static PRTTIMER volatile    g_pTimerHead = NULL;
/** The number of active timers. */
static uint32_t volatile    g_cActiveTimers = 0;
/** The number of active timers. */
static uint32_t volatile    g_cTimers = 0;
/** The change number.
 * This is used to detect list changes during the timer callback loop. */
static uint32_t volatile    g_u32ChangeNo;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
RT_C_DECLS_BEGIN
DECLASM(void) rtTimerOs2Tick(void);
DECLASM(int) rtTimerOs2Arm(void);
DECLASM(int) rtTimerOs2Dearm(void);
RT_C_DECLS_END



RTDECL(int) RTTimerCreateEx(PRTTIMER *ppTimer, uint64_t u64NanoInterval, uint32_t fFlags, PFNRTTIMER pfnTimer, void *pvUser)
{
    *ppTimer = NULL;

    /*
     * We don't support the fancy MP features.
     */
    if (fFlags & RTTIMER_FLAGS_CPU_SPECIFIC)
        return VERR_NOT_SUPPORTED;

    /*
     * Lazy initialize the spinlock.
     */
    if (g_Spinlock == NIL_RTSPINLOCK)
    {
        RTSPINLOCK Spinlock;
        int rc = RTSpinlockCreate(&Spinlock, RTSPINLOCK_FLAGS_INTERRUPT_SAFE, "RTTimerOS2");
        AssertRCReturn(rc, rc);
        //bool fRc;
        //ASMAtomicCmpXchgSize(&g_Spinlock, Spinlock, NIL_RTSPINLOCK, fRc);
        //if (!fRc)
        if (!ASMAtomicCmpXchgPtr((void * volatile *)&g_Spinlock, Spinlock, NIL_RTSPINLOCK))
            RTSpinlockDestroy(Spinlock);
    }

    /*
     * Allocate and initialize the timer handle.
     */
    PRTTIMER pTimer = (PRTTIMER)RTMemAlloc(sizeof(*pTimer));
    if (!pTimer)
        return VERR_NO_MEMORY;

    pTimer->u32Magic = RTTIMER_MAGIC;
    pTimer->pNext = NULL;
    pTimer->fSuspended = true;
    pTimer->pfnTimer = pfnTimer;
    pTimer->pvUser = pvUser;
    pTimer->u64NanoInterval = u64NanoInterval;
    pTimer->u64StartTS = 0;

    /*
     * Insert the timer into the list (LIFO atm).
     */
    RTSpinlockAcquire(g_Spinlock);
    g_u32ChangeNo++;
    pTimer->pNext = g_pTimerHead;
    g_pTimerHead = pTimer;
    g_cTimers++;
    RTSpinlockRelease(g_Spinlock);

    *ppTimer = pTimer;
    return VINF_SUCCESS;
}


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
     * Remove it from the list.
     */
    RTSpinlockAcquire(g_Spinlock);
    g_u32ChangeNo++;
    if (g_pTimerHead == pTimer)
        g_pTimerHead = pTimer->pNext;
    else
    {
        PRTTIMER pPrev = g_pTimerHead;
        while (pPrev->pNext != pTimer)
        {
            pPrev = pPrev->pNext;
            if (RT_UNLIKELY(!pPrev))
            {
                RTSpinlockRelease(g_Spinlock);
                return VERR_INVALID_HANDLE;
            }
        }
        pPrev->pNext = pTimer->pNext;
    }
    Assert(g_cTimers > 0);
    g_cTimers--;
    if (!pTimer->fSuspended)
    {
        Assert(g_cActiveTimers > 0);
        g_cActiveTimers--;
        if (!g_cActiveTimers)
            rtTimerOs2Dearm();
    }
    RTSpinlockRelease(g_Spinlock);

    /*
     * Free the associated resources.
     */
    pTimer->u32Magic++;
    RTMemFree(pTimer);
    return VINF_SUCCESS;
}


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

    RTSpinlockAcquire(g_Spinlock);
    g_u32ChangeNo++;
    if (!g_cActiveTimers)
    {
        int rc = rtTimerOs2Arm();
        if (RT_FAILURE(rc))
        {
            RTSpinlockRelease(g_Spinlock);
            return rc;
        }
    }
    g_cActiveTimers++;
    pTimer->fSuspended = false;
    pTimer->fDone = true;               /* next tick, not current! */
    pTimer->iTick = 0;
    pTimer->u64StartTS = u64First;
    pTimer->u64NextTS = u64First;
    RTSpinlockRelease(g_Spinlock);

    return VINF_SUCCESS;
}


RTDECL(int) RTTimerStop(PRTTIMER pTimer)
{
    if (!rtTimerIsValid(pTimer))
        return VERR_INVALID_HANDLE;
    if (pTimer->fSuspended)
        return VERR_TIMER_SUSPENDED;

    /*
     * Suspend the timer.
     */
    RTSpinlockAcquire(g_Spinlock);
    g_u32ChangeNo++;
    pTimer->fSuspended = true;
    Assert(g_cActiveTimers > 0);
    g_cActiveTimers--;
    if (!g_cActiveTimers)
        rtTimerOs2Dearm();
    RTSpinlockRelease(g_Spinlock);

    return VINF_SUCCESS;
}


RTDECL(int) RTTimerChangeInterval(PRTTIMER pTimer, uint64_t u64NanoInterval)
{
    if (!rtTimerIsValid(pTimer))
        return VERR_INVALID_HANDLE;
    RT_NOREF(u64NanoInterval);
    return VERR_NOT_SUPPORTED;
}


DECLASM(void) rtTimerOs2Tick(void)
{
    /*
     * Query the current time and then take the lock.
     */
    const uint64_t u64NanoTS = RTTimeNanoTS();

    RTSpinlockAcquire(g_Spinlock);

    /*
     * Clear the fDone flag.
     */
    PRTTIMER pTimer;
    for (pTimer = g_pTimerHead; pTimer; pTimer = pTimer->pNext)
        pTimer->fDone = false;

    /*
     * Walk the timer list and do the callbacks for any active timer.
     */
    uint32_t u32CurChangeNo = g_u32ChangeNo;
    pTimer = g_pTimerHead;
    while (pTimer)
    {
        PRTTIMER pNext = pTimer->pNext;
        if (    !pTimer->fSuspended
            &&  !pTimer->fDone
            &&  pTimer->u64NextTS <= u64NanoTS)
        {
            pTimer->fDone = true;
            pTimer->iTick++;

            /* calculate the next timeout */
            if (!pTimer->u64NanoInterval)
                pTimer->fSuspended = true;
            else
            {
                pTimer->u64NextTS = pTimer->u64StartTS + pTimer->iTick * pTimer->u64NanoInterval;
                if (pTimer->u64NextTS < u64NanoTS)
                    pTimer->u64NextTS = u64NanoTS + RTTimerGetSystemGranularity() / 2;
            }

            /* do the callout */
            PFNRTTIMER  pfnTimer = pTimer->pfnTimer;
            void       *pvUser   = pTimer->pvUser;
            RTSpinlockRelease(g_Spinlock);
            pfnTimer(pTimer, pvUser, pTimer->iTick);

            RTSpinlockAcquire(g_Spinlock);

            /* check if anything changed. */
            if (u32CurChangeNo != g_u32ChangeNo)
            {
                u32CurChangeNo = g_u32ChangeNo;
                pNext = g_pTimerHead;
            }
        }

        /* next */
        pTimer = pNext;
    }

    RTSpinlockRelease(g_Spinlock);
}


RTDECL(uint32_t) RTTimerGetSystemGranularity(void)
{
    return 32000000; /* 32ms */
}


RTDECL(int) RTTimerRequestSystemGranularity(uint32_t u32Request, uint32_t *pu32Granted)
{
    RT_NOREF(u32Request, pu32Granted);
    return VERR_NOT_SUPPORTED;
}


RTDECL(int) RTTimerReleaseSystemGranularity(uint32_t u32Granted)
{
    RT_NOREF(u32Granted);
    return VERR_NOT_SUPPORTED;
}


RTDECL(bool) RTTimerCanDoHighResolution(void)
{
    return false;
}

