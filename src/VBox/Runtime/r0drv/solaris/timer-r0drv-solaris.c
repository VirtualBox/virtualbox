/* $Id: timer-r0drv-solaris.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Timer, Ring-0 Driver, Solaris.
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
#include "the-solaris-kernel.h"
#include "internal/iprt.h"
#include <iprt/timer.h>

#include <iprt/asm.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/mp.h>
#include <iprt/spinlock.h>
#include <iprt/time.h>
#include <iprt/thread.h>
#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The internal representation of a Solaris timer handle.
 */
typedef struct RTTIMER
{
    /** Magic.
     * This is RTTIMER_MAGIC, but changes to something else before the timer
     * is destroyed to indicate clearly that thread should exit. */
    uint32_t volatile       u32Magic;
    /** Reference counter. */
    uint32_t volatile       cRefs;
    /** Flag indicating that the timer is suspended (hCyclicId should be
     *  CYCLIC_NONE). */
    bool volatile           fSuspended;
    /** Flag indicating that the timer was suspended from the timer callback and
     * therefore the hCyclicId may still be valid. */
    bool volatile           fSuspendedFromTimer;
    /** Flag indicating that the timer interval was changed and that it requires
     * manual expiration time programming for each callout. */
    bool volatile           fIntervalChanged;
    /** Whether the timer must run on all CPUs or not. */
    uint8_t                 fAllCpus;
    /** Whether the timer must run on a specific CPU or not. */
    uint8_t                 fSpecificCpu;
    /** The CPU it must run on if fSpecificCpu is set. */
    uint32_t                iCpu;
    /** The nano second interval for repeating timers. */
    uint64_t volatile       cNsInterval;
    /** Cyclic timer Id.  This is CYCLIC_NONE if no active timer.
     * @remarks Please keep in mind that cyclic may call us back before the
     *          cyclic_add/cyclic_add_omni functions returns, so don't use this
     *          unguarded with cyclic_reprogram. */
    cyclic_id_t             hCyclicId;
    /** The user callback. */
    PFNRTTIMER              pfnTimer;
    /** The argument for the user callback. */
    void                   *pvUser;
    /** Union with timer type specific data. */
    union
    {
        /** Single timer (fAllCpus == false). */
        struct
        {
            /** Timer ticks. */
            uint64_t        u64Tick;
            /** The next tick when fIntervalChanged is true, otherwise 0. */
            uint64_t        nsNextTick;
            /** The (interrupt) thread currently active in the callback. */
            kthread_t * volatile pActiveThread;
        } Single;

        /** Omni timer (fAllCpus == true). */
        struct
        {
            /** Absolute timestamp of when the timer should fire first when starting up. */
            uint64_t        u64When;
            /** Array of per CPU data (variable size). */
            struct
            {
                /** Timer ticks (reinitialized when online'd). */
                uint64_t    u64Tick;
                /** The (interrupt) thread currently active in the callback. */
                kthread_t * volatile pActiveThread;
                /** The next tick when fIntervalChanged is true, otherwise 0. */
                uint64_t    nsNextTick;
            } aPerCpu[1];
        } Omni;
    } u;
} RTTIMER;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Validates that the timer is valid. */
#define RTTIMER_ASSERT_VALID_RET(pTimer) \
    do \
    { \
        AssertPtrReturn(pTimer, VERR_INVALID_HANDLE); \
        AssertMsgReturn((pTimer)->u32Magic == RTTIMER_MAGIC, ("pTimer=%p u32Magic=%x expected %x\n", (pTimer), (pTimer)->u32Magic, RTTIMER_MAGIC), \
            VERR_INVALID_HANDLE); \
    } while (0)


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void rtTimerSolSingleCallbackWrapper(void *pvArg);
static void rtTimerSolStopIt(PRTTIMER pTimer);


/**
 * Retains a reference to the timer.
 *
 * @returns New reference counter value.
 * @param   pTimer              The timer.
 */
DECLINLINE(uint32_t) rtTimerSolRetain(PRTTIMER pTimer)
{
    return ASMAtomicIncU32(&pTimer->cRefs);
}


/**
 * Destroys the timer when the reference counter has reached zero.
 *
 * @returns 0 (new references counter value).
 * @param   pTimer              The timer.
 */
static uint32_t rtTimeSolReleaseCleanup(PRTTIMER pTimer)
{
    Assert(pTimer->hCyclicId == CYCLIC_NONE);
    ASMAtomicWriteU32(&pTimer->u32Magic, ~RTTIMER_MAGIC);
    RTMemFree(pTimer);
    return 0;
}


/**
 * Releases a reference to the timer.
 *
 * @returns New reference counter value.
 * @param   pTimer              The timer.
 */
DECLINLINE(uint32_t) rtTimerSolRelease(PRTTIMER pTimer)
{
    uint32_t cRefs = ASMAtomicDecU32(&pTimer->cRefs);
    if (!cRefs)
        return rtTimeSolReleaseCleanup(pTimer);
    return cRefs;
}


/**
 * Callback wrapper for single-CPU timers.
 *
 * @param    pvArg              Opaque pointer to the timer.
 *
 * @remarks This will be executed in interrupt context but only at the specified
 *          level i.e. CY_LOCK_LEVEL in our case. We -CANNOT- call into the
 *          cyclic subsystem here, neither should pfnTimer().
 */
static void rtTimerSolSingleCallbackWrapper(void *pvArg)
{
    PRTTIMER pTimer = (PRTTIMER)pvArg;
    AssertPtrReturnVoid(pTimer);
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));
    Assert(!pTimer->fAllCpus);

    /* Make sure one-shots do not fire another time. */
    Assert(   !pTimer->fSuspended
           || pTimer->cNsInterval != 0);

    if (!pTimer->fSuspendedFromTimer)
    {
        /* Make sure we are firing on the right CPU. */
        Assert(   !pTimer->fSpecificCpu
               || pTimer->iCpu == RTMpCpuId());

        /* For one-shot, we may allow the callback to restart them. */
        if (pTimer->cNsInterval == 0)
            pTimer->fSuspendedFromTimer = true;

        /*
         * Perform the callout.
         */
        pTimer->u.Single.pActiveThread = curthread;

        uint64_t u64Tick = ++pTimer->u.Single.u64Tick;
        pTimer->pfnTimer(pTimer, pTimer->pvUser, u64Tick);

        pTimer->u.Single.pActiveThread = NULL;

        if (RT_LIKELY(!pTimer->fSuspendedFromTimer))
        {
            if (   !pTimer->fIntervalChanged
                || RT_UNLIKELY(pTimer->hCyclicId == CYCLIC_NONE))
                return;

            /*
             * The interval was changed, we need to set the expiration time
             * ourselves before returning.  This comes at a slight cost,
             * which is why we don't do it all the time.
             */
            if (pTimer->u.Single.nsNextTick)
                pTimer->u.Single.nsNextTick += ASMAtomicUoReadU64(&pTimer->cNsInterval);
            else
                pTimer->u.Single.nsNextTick = RTTimeSystemNanoTS() + ASMAtomicUoReadU64(&pTimer->cNsInterval);
            cyclic_reprogram(pTimer->hCyclicId, pTimer->u.Single.nsNextTick);
            return;
        }

        /*
         * The timer has been suspended, set expiration time to infinitiy.
         */
    }
    if (RT_LIKELY(pTimer->hCyclicId != CYCLIC_NONE))
        cyclic_reprogram(pTimer->hCyclicId, CY_INFINITY);
}


/**
 * Callback wrapper for Omni-CPU timers.
 *
 * @param    pvArg              Opaque pointer to the timer.
 *
 * @remarks This will be executed in interrupt context but only at the specified
 *          level i.e. CY_LOCK_LEVEL in our case. We -CANNOT- call into the
 *          cyclic subsystem here, neither should pfnTimer().
 */
static void rtTimerSolOmniCallbackWrapper(void *pvArg)
{
    PRTTIMER pTimer = (PRTTIMER)pvArg;
    AssertPtrReturnVoid(pTimer);
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));
    Assert(pTimer->fAllCpus);

    if (!pTimer->fSuspendedFromTimer)
    {
        /*
         * Perform the callout.
         */
        uint32_t const iCpu = CPU->cpu_id;

        pTimer->u.Omni.aPerCpu[iCpu].pActiveThread = curthread;
        uint64_t u64Tick = ++pTimer->u.Omni.aPerCpu[iCpu].u64Tick;

        pTimer->pfnTimer(pTimer, pTimer->pvUser, u64Tick);

        pTimer->u.Omni.aPerCpu[iCpu].pActiveThread = NULL;

        if (RT_LIKELY(!pTimer->fSuspendedFromTimer))
        {
            if (   !pTimer->fIntervalChanged
                || RT_UNLIKELY(pTimer->hCyclicId == CYCLIC_NONE))
                return;

            /*
             * The interval was changed, we need to set the expiration time
             * ourselves before returning.  This comes at a slight cost,
             * which is why we don't do it all the time.
             *
             * Note! The cyclic_reprogram call only affects the omni cyclic
             *       component for this CPU.
             */
            if (pTimer->u.Omni.aPerCpu[iCpu].nsNextTick)
                pTimer->u.Omni.aPerCpu[iCpu].nsNextTick += ASMAtomicUoReadU64(&pTimer->cNsInterval);
            else
                pTimer->u.Omni.aPerCpu[iCpu].nsNextTick = RTTimeSystemNanoTS() + ASMAtomicUoReadU64(&pTimer->cNsInterval);
            cyclic_reprogram(pTimer->hCyclicId, pTimer->u.Omni.aPerCpu[iCpu].nsNextTick);
            return;
        }

        /*
         * The timer has been suspended, set expiration time to infinitiy.
         */
    }
    if (RT_LIKELY(pTimer->hCyclicId != CYCLIC_NONE))
        cyclic_reprogram(pTimer->hCyclicId, CY_INFINITY);
}


/**
 * Omni-CPU cyclic online event. This is called before the omni cycle begins to
 * fire on the specified CPU.
 *
 * @param    pvArg              Opaque pointer to the timer.
 * @param    pCpu               Pointer to the CPU on which it will fire.
 * @param    pCyclicHandler     Pointer to a cyclic handler to add to the CPU
 *                              specified in @a pCpu.
 * @param    pCyclicTime        Pointer to the cyclic time and interval object.
 *
 * @remarks We -CANNOT- call back into the cyclic subsystem here, we can however
 *          block (sleep).
 */
static void rtTimerSolOmniCpuOnline(void *pvArg, cpu_t *pCpu, cyc_handler_t *pCyclicHandler, cyc_time_t *pCyclicTime)
{
    PRTTIMER pTimer = (PRTTIMER)pvArg;
    AssertPtrReturnVoid(pTimer);
    AssertPtrReturnVoid(pCpu);
    AssertPtrReturnVoid(pCyclicHandler);
    AssertPtrReturnVoid(pCyclicTime);
    uint32_t const iCpu = pCpu->cpu_id; /* Note! CPU is not necessarily the same as pCpu. */

    pTimer->u.Omni.aPerCpu[iCpu].u64Tick = 0;
    pTimer->u.Omni.aPerCpu[iCpu].nsNextTick = 0;

    pCyclicHandler->cyh_func  = (cyc_func_t)rtTimerSolOmniCallbackWrapper;
    pCyclicHandler->cyh_arg   = pTimer;
    pCyclicHandler->cyh_level = CY_LOCK_LEVEL;

    uint64_t u64Now = RTTimeSystemNanoTS();
    if (pTimer->u.Omni.u64When < u64Now)
        pCyclicTime->cyt_when = u64Now + pTimer->cNsInterval / 2;
    else
        pCyclicTime->cyt_when = pTimer->u.Omni.u64When;

    pCyclicTime->cyt_interval = pTimer->cNsInterval;
}


RTDECL(int) RTTimerCreateEx(PRTTIMER *ppTimer, uint64_t u64NanoInterval, uint32_t fFlags, PFNRTTIMER pfnTimer, void *pvUser)
{
    RT_ASSERT_PREEMPTIBLE();
    *ppTimer = NULL;

    /*
     * Validate flags.
     */
    if (!RTTIMER_FLAGS_ARE_VALID(fFlags))
        return VERR_INVALID_PARAMETER;

    if (    (fFlags & RTTIMER_FLAGS_CPU_SPECIFIC)
        &&  (fFlags & RTTIMER_FLAGS_CPU_ALL) != RTTIMER_FLAGS_CPU_ALL
        &&  !RTMpIsCpuPossible(RTMpCpuIdFromSetIndex(fFlags & RTTIMER_FLAGS_CPU_MASK)))
        return VERR_CPU_NOT_FOUND;

    /* One-shot omni timers are not supported by the cyclic system. */
    if (   (fFlags & RTTIMER_FLAGS_CPU_ALL) == RTTIMER_FLAGS_CPU_ALL
        && u64NanoInterval == 0)
        return VERR_NOT_SUPPORTED;

    /*
     * Allocate and initialize the timer handle.  The omni variant has a
     * variable sized array of ticks counts, thus the size calculation.
     */
    PRTTIMER pTimer = (PRTTIMER)RTMemAllocZ(  (fFlags & RTTIMER_FLAGS_CPU_ALL) == RTTIMER_FLAGS_CPU_ALL
                                            ? RT_UOFFSETOF_DYN(RTTIMER, u.Omni.aPerCpu[RTMpGetCount()])
                                            : sizeof(RTTIMER));
    if (!pTimer)
        return VERR_NO_MEMORY;

    pTimer->u32Magic = RTTIMER_MAGIC;
    pTimer->cRefs = 1;
    pTimer->fSuspended = true;
    pTimer->fSuspendedFromTimer = false;
    pTimer->fIntervalChanged = false;
    if ((fFlags & RTTIMER_FLAGS_CPU_ALL) == RTTIMER_FLAGS_CPU_ALL)
    {
        pTimer->fAllCpus = true;
        pTimer->fSpecificCpu = false;
        pTimer->iCpu = UINT32_MAX;
    }
    else if (fFlags & RTTIMER_FLAGS_CPU_SPECIFIC)
    {
        pTimer->fAllCpus = false;
        pTimer->fSpecificCpu = true;
        pTimer->iCpu = fFlags & RTTIMER_FLAGS_CPU_MASK; /* ASSUMES: index == cpuid */
    }
    else
    {
        pTimer->fAllCpus = false;
        pTimer->fSpecificCpu = false;
        pTimer->iCpu = UINT32_MAX;
    }
    pTimer->cNsInterval = u64NanoInterval;
    pTimer->pfnTimer = pfnTimer;
    pTimer->pvUser = pvUser;
    pTimer->hCyclicId = CYCLIC_NONE;

    *ppTimer = pTimer;
    return VINF_SUCCESS;
}


/**
 * Checks if the calling thread is currently executing the timer proceduce for
 * the given timer.
 *
 * @returns true if it is, false if it isn't.
 * @param   pTimer              The timer in question.
 */
DECLINLINE(bool) rtTimerSolIsCallingFromTimerProc(PRTTIMER pTimer)
{
    kthread_t *pCurThread = curthread;
    AssertReturn(pCurThread, false); /* serious paranoia */

    if (!pTimer->fAllCpus)
        return pTimer->u.Single.pActiveThread == pCurThread;
    return pTimer->u.Omni.aPerCpu[CPU->cpu_id].pActiveThread == pCurThread;
}


RTDECL(int) RTTimerDestroy(PRTTIMER pTimer)
{
    if (pTimer == NULL)
        return VINF_SUCCESS;
    RTTIMER_ASSERT_VALID_RET(pTimer);
    RT_ASSERT_INTS_ON();

    /*
     * It is not possible to destroy a timer from it's callback function.
     * Cyclic makes that impossible (or at least extremely risky).
     */
    AssertReturn(!rtTimerSolIsCallingFromTimerProc(pTimer), VERR_INVALID_CONTEXT);

    /*
     * Invalidate the handle, make sure it's stopped and free the associated resources.
     */
    ASMAtomicWriteU32(&pTimer->u32Magic, ~RTTIMER_MAGIC);

    if (   !pTimer->fSuspended
        || pTimer->hCyclicId != CYCLIC_NONE) /* 2nd check shouldn't happen */
        rtTimerSolStopIt(pTimer);

    rtTimerSolRelease(pTimer);
    return VINF_SUCCESS;
}


RTDECL(int) RTTimerStart(PRTTIMER pTimer, uint64_t u64First)
{
    RTTIMER_ASSERT_VALID_RET(pTimer);
    RT_ASSERT_INTS_ON();

    /*
     * It's not possible to restart a one-shot time from it's callback function,
     * at least not at the moment.
     */
    AssertReturn(!rtTimerSolIsCallingFromTimerProc(pTimer), VERR_INVALID_CONTEXT);

    mutex_enter(&cpu_lock);

    /*
     * Make sure it's not active already.  If it was suspended from a timer
     * callback function, we need to do some cleanup work here before we can
     * restart the timer.
     */
    if (!pTimer->fSuspended)
    {
        if (!pTimer->fSuspendedFromTimer)
        {
            mutex_exit(&cpu_lock);
            return VERR_TIMER_ACTIVE;
        }
        cyclic_remove(pTimer->hCyclicId);
        pTimer->hCyclicId = CYCLIC_NONE;
    }

    pTimer->fSuspended = false;
    pTimer->fSuspendedFromTimer = false;
    pTimer->fIntervalChanged = false;
    if (pTimer->fAllCpus)
    {
        /*
         * Setup omni (all CPU) timer. The Omni-CPU online event will fire
         * and from there we setup periodic timers per CPU.
         */
        pTimer->u.Omni.u64When  = RTTimeSystemNanoTS() + (u64First ? u64First : pTimer->cNsInterval);

        cyc_omni_handler_t HandlerOmni;
        HandlerOmni.cyo_online  = rtTimerSolOmniCpuOnline;
        HandlerOmni.cyo_offline = NULL;
        HandlerOmni.cyo_arg     = pTimer;

        pTimer->hCyclicId = cyclic_add_omni(&HandlerOmni);
    }
    else
    {
        cyc_handler_t Handler;
        cyc_time_t    FireTime;

        /*
         * Setup a single CPU timer.   If a specific CPU was requested, it
         * must be online or the timer cannot start.
         */
        if (   pTimer->fSpecificCpu
            && !RTMpIsCpuOnline(pTimer->iCpu)) /* ASSUMES: index == cpuid */
        {
            pTimer->fSuspended = true;

            mutex_exit(&cpu_lock);
            return VERR_CPU_OFFLINE;
        }

        Handler.cyh_func  = (cyc_func_t)rtTimerSolSingleCallbackWrapper;
        Handler.cyh_arg   = pTimer;
        Handler.cyh_level = CY_LOCK_LEVEL;

        /*
         * Use a large interval (1 hour) so that we don't get a timer-callback between
         * cyclic_add() and cyclic_bind(). Program the correct interval once cyclic_bind() is done.
         * See @bugref{7691#c20}.
         */
        if (!pTimer->fSpecificCpu)
            FireTime.cyt_when = RTTimeSystemNanoTS() + u64First;
        else
            FireTime.cyt_when = RTTimeSystemNanoTS() + u64First + RT_NS_1HOUR;
        FireTime.cyt_interval = pTimer->cNsInterval != 0
                              ? pTimer->cNsInterval
                              : CY_INFINITY /* Special value, see cyclic_fire(). */;
        pTimer->u.Single.u64Tick = 0;
        pTimer->u.Single.nsNextTick = 0;

        pTimer->hCyclicId = cyclic_add(&Handler, &FireTime);
        if (pTimer->fSpecificCpu)
        {
            cyclic_bind(pTimer->hCyclicId, cpu[pTimer->iCpu], NULL /* cpupart */);
            cyclic_reprogram(pTimer->hCyclicId, RTTimeSystemNanoTS() + u64First);
        }
    }

    mutex_exit(&cpu_lock);
    return VINF_SUCCESS;
}


/**
 * Worker common for RTTimerStop and RTTimerDestroy.
 *
 * @param   pTimer      The timer to stop.
 */
static void rtTimerSolStopIt(PRTTIMER pTimer)
{
    mutex_enter(&cpu_lock);

    pTimer->fSuspended = true;
    if (pTimer->hCyclicId != CYCLIC_NONE)
    {
        cyclic_remove(pTimer->hCyclicId);
        pTimer->hCyclicId = CYCLIC_NONE;
    }
    pTimer->fSuspendedFromTimer = false;

    mutex_exit(&cpu_lock);
}


RTDECL(int) RTTimerStop(PRTTIMER pTimer)
{
    RTTIMER_ASSERT_VALID_RET(pTimer);
    RT_ASSERT_INTS_ON();

    if (pTimer->fSuspended)
        return VERR_TIMER_SUSPENDED;

    /* Trying the cpu_lock stuff and calling cyclic_remove may deadlock
       the system, so just mark the timer as suspened and deal with it in
       the callback wrapper function above. */
    if (rtTimerSolIsCallingFromTimerProc(pTimer))
        pTimer->fSuspendedFromTimer = true;
    else
        rtTimerSolStopIt(pTimer);

    return VINF_SUCCESS;
}


RTDECL(int) RTTimerChangeInterval(PRTTIMER pTimer, uint64_t u64NanoInterval)
{
    /*
     * Validate.
     */
    RTTIMER_ASSERT_VALID_RET(pTimer);
    AssertReturn(u64NanoInterval > 0, VERR_INVALID_PARAMETER);
    AssertReturn(u64NanoInterval < UINT64_MAX / 8, VERR_INVALID_PARAMETER);
    AssertReturn(pTimer->cNsInterval, VERR_INVALID_STATE);

    if (pTimer->fSuspended || pTimer->fSuspendedFromTimer)
        pTimer->cNsInterval = u64NanoInterval;
    else
    {
        ASMAtomicWriteU64(&pTimer->cNsInterval, u64NanoInterval);
        ASMAtomicWriteBool(&pTimer->fIntervalChanged, true);

        if (   !pTimer->fAllCpus
            && !pTimer->u.Single.nsNextTick
            && pTimer->hCyclicId != CYCLIC_NONE
            && rtTimerSolIsCallingFromTimerProc(pTimer))
            pTimer->u.Single.nsNextTick = RTTimeSystemNanoTS();
    }

    return VINF_SUCCESS;
}


RTDECL(uint32_t) RTTimerGetSystemGranularity(void)
{
    return nsec_per_tick;
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
    return true;
}

