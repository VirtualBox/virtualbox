/* $Id: timer-r0drv-freebsd.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Memory Allocation, Ring-0 Driver, FreeBSD.
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
#include "the-freebsd-kernel.h"

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
 * The internal representation of an FreeBSD timer handle.
 */
typedef struct RTTIMER
{
    /** Magic.
     * This is RTTIMER_MAGIC, but changes to something else before the timer
     * is destroyed to indicate clearly that thread should exit. */
    uint32_t volatile       u32Magic;
    /** Flag indicating that the timer is suspended. */
    uint8_t volatile        fSuspended;
    /** Whether the timer must run on a specific CPU or not. */
    uint8_t                 fSpecificCpu;
    /** The CPU it must run on if fSpecificCpu is set. */
    uint32_t                iCpu;
    /** The FreeBSD callout structure. */
    struct callout          Callout;
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
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void rtTimerFreeBSDCallback(void *pvTimer);



RTDECL(int) RTTimerCreateEx(PRTTIMER *ppTimer, uint64_t u64NanoInterval, uint32_t fFlags, PFNRTTIMER pfnTimer, void *pvUser)
{
    *ppTimer = NULL;

    /*
     * Validate flags.
     */
    if (!RTTIMER_FLAGS_ARE_VALID(fFlags))
        return VERR_INVALID_PARAMETER;
    if (    (fFlags & RTTIMER_FLAGS_CPU_SPECIFIC)
        &&  (fFlags & RTTIMER_FLAGS_CPU_ALL) != RTTIMER_FLAGS_CPU_ALL
        &&  (fFlags & RTTIMER_FLAGS_CPU_MASK) > mp_maxid)
        return VERR_CPU_NOT_FOUND;

    /*
     * Allocate and initialize the timer handle.
     */
    PRTTIMER pTimer = (PRTTIMER)RTMemAlloc(sizeof(*pTimer));
    if (!pTimer)
        return VERR_NO_MEMORY;

    pTimer->u32Magic = RTTIMER_MAGIC;
    pTimer->fSuspended = true;
    pTimer->fSpecificCpu = !!(fFlags & RTTIMER_FLAGS_CPU_SPECIFIC);
    pTimer->iCpu = fFlags & RTTIMER_FLAGS_CPU_MASK;
    pTimer->pfnTimer = pfnTimer;
    pTimer->pvUser = pvUser;
    pTimer->u64NanoInterval = u64NanoInterval;
    pTimer->u64StartTS = 0;
    callout_init(&pTimer->Callout, CALLOUT_MPSAFE);

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
     * Free the associated resources.
     */
    pTimer->u32Magic++;
    callout_stop(&pTimer->Callout);
    RTMemFree(pTimer);
    return VINF_SUCCESS;
}


RTDECL(int) RTTimerStart(PRTTIMER pTimer, uint64_t u64First)
{
    struct timeval tv;

    if (!rtTimerIsValid(pTimer))
        return VERR_INVALID_HANDLE;
    if (!pTimer->fSuspended)
        return VERR_TIMER_ACTIVE;
    if (   pTimer->fSpecificCpu
        && !RTMpIsCpuOnline(RTMpCpuIdFromSetIndex(pTimer->iCpu)))
        return VERR_CPU_OFFLINE;

    /*
     * Calc when it should start firing.
     */
    u64First += RTTimeNanoTS();

    pTimer->fSuspended = false;
    pTimer->iTick = 0;
    pTimer->u64StartTS = u64First;
    pTimer->u64NextTS = u64First;

    tv.tv_sec  =  u64First / 1000000000;
    tv.tv_usec = (u64First % 1000000000) / 1000;
    callout_reset(&pTimer->Callout, tvtohz(&tv), rtTimerFreeBSDCallback, pTimer);

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
    pTimer->fSuspended = true;
    callout_stop(&pTimer->Callout);

    return VINF_SUCCESS;
}


RTDECL(int) RTTimerChangeInterval(PRTTIMER pTimer, uint64_t u64NanoInterval)
{
    if (!rtTimerIsValid(pTimer))
        return VERR_INVALID_HANDLE;
    return VERR_NOT_SUPPORTED;
}


/**
 * smp_rendezvous action callback.
 *
 * This will perform the timer callback if we're on the right CPU.
 *
 * @param   pvTimer The timer.
 */
static void rtTimerFreeBSDIpiAction(void *pvTimer)
{
    PRTTIMER pTimer = (PRTTIMER)pvTimer;
    if (    pTimer->iCpu == RTTIMER_FLAGS_CPU_MASK
        ||  (u_int)pTimer->iCpu == curcpu)
        pTimer->pfnTimer(pTimer, pTimer->pvUser, pTimer->iTick);
}


static void rtTimerFreeBSDCallback(void *pvTimer)
{
    PRTTIMER pTimer = (PRTTIMER)pvTimer;

    /* calculate and set the next timeout */
    pTimer->iTick++;
    if (!pTimer->u64NanoInterval)
    {
        pTimer->fSuspended = true;
        callout_stop(&pTimer->Callout);
    }
    else
    {
        struct timeval tv;
        const uint64_t u64NanoTS = RTTimeNanoTS();
        pTimer->u64NextTS = pTimer->u64StartTS + pTimer->iTick * pTimer->u64NanoInterval;
        if (pTimer->u64NextTS < u64NanoTS)
            pTimer->u64NextTS = u64NanoTS + RTTimerGetSystemGranularity() / 2;

        tv.tv_sec = pTimer->u64NextTS / 1000000000;
        tv.tv_usec = (pTimer->u64NextTS % 1000000000) / 1000;
        callout_reset(&pTimer->Callout, tvtohz(&tv), rtTimerFreeBSDCallback, pTimer);
    }

    /* callback */
    if (    !pTimer->fSpecificCpu
        ||  pTimer->iCpu == curcpu)
        pTimer->pfnTimer(pTimer, pTimer->pvUser, pTimer->iTick);
    else
        smp_rendezvous(NULL, rtTimerFreeBSDIpiAction, NULL, pvTimer);
}


RTDECL(uint32_t) RTTimerGetSystemGranularity(void)
{
    return 1000000000 / hz; /* ns */
}


RTDECL(int) RTTimerRequestSystemGranularity(uint32_t u32Request, uint32_t *pu32Granted)
{
    return VERR_NOT_SUPPORTED;
}


RTDECL(int) RTTimerReleaseSystemGranularity(uint32_t u32Granted)
{
    return VERR_NOT_SUPPORTED;
}


RTDECL(bool) RTTimerCanDoHighResolution(void)
{
    return false;
}

