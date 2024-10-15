/* $Id: timer-r0drv-nt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Timers, Ring-0 Driver, NT.
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
#include "the-nt-kernel.h"

#include <iprt/timer.h>
#include <iprt/mp.h>
#include <iprt/cpuset.h>
#include <iprt/err.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/thread.h>

#include "internal-r0drv-nt.h"
#include "internal/magics.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** This seems to provide better accuracy. */
#define RTR0TIMER_NT_MANUAL_RE_ARM 1

#if !defined(IN_GUEST) || defined(DOXYGEN_RUNNING)
/** This using high resolution timers introduced with windows 8.1. */
# define RTR0TIMER_NT_HIGH_RES 1
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * A sub timer structure.
 *
 * This is used for keeping the per-cpu tick and DPC object.
 */
typedef struct RTTIMERNTSUBTIMER
{
    /** The tick counter. */
    uint64_t                iTick;
    /** Pointer to the parent timer. */
    PRTTIMER                pParent;
    /** Thread active executing the worker function, NIL if inactive. */
    RTNATIVETHREAD volatile hActiveThread;
    /** The NT DPC object. */
    KDPC                    NtDpc;
    /** Whether we failed to set the target CPU for the DPC and that this needs
     * to be done at RTTimerStart (simple timers) or during timer callback (omni). */
    bool                    fDpcNeedTargetCpuSet;
} RTTIMERNTSUBTIMER;
/** Pointer to a NT sub-timer structure. */
typedef RTTIMERNTSUBTIMER *PRTTIMERNTSUBTIMER;

/**
 * The internal representation of an Linux timer handle.
 */
typedef struct RTTIMER
{
    /** Magic.
     * This is RTTIMER_MAGIC, but changes to something else before the timer
     * is destroyed to indicate clearly that thread should exit. */
    uint32_t volatile       u32Magic;
    /** Suspend count down for single shot omnit timers. */
    int32_t volatile        cOmniSuspendCountDown;
    /** Flag indicating the timer is suspended. */
    bool volatile           fSuspended;
    /** Whether the timer must run on one specific CPU or not. */
    bool                    fSpecificCpu;
    /** Whether the timer must run on all CPUs or not. */
    bool                    fOmniTimer;
    /** The CPU it must run on if fSpecificCpu is set.
     * The master CPU for an omni-timer. */
    RTCPUID                 idCpu;
    /** Callback. */
    PFNRTTIMER              pfnTimer;
    /** User argument. */
    void                   *pvUser;

    /** @name Periodic scheduling / RTTimerChangeInterval.
     *  @{  */
    /** Spinlock protecting the u64NanoInterval, iMasterTick, uNtStartTime,
     *  uNtDueTime and (at least for updating) fSuspended. */
    KSPIN_LOCK              Spinlock;
    /** The timer interval. 0 if one-shot. */
    uint64_t volatile       u64NanoInterval;
    /** The the current master tick.  This does not necessarily follow that of
     *  the subtimer, as RTTimerChangeInterval may cause it to reset. */
    uint64_t volatile       iMasterTick;
#ifdef RTR0TIMER_NT_MANUAL_RE_ARM
    /** The desired NT time of the first tick.
     *  This is not set for one-shot timers, only periodic ones. */
    uint64_t volatile       uNtStartTime;
    /** The current due time (absolute interrupt time).
     *  This is not set for one-shot timers, only periodic ones.  */
    uint64_t volatile       uNtDueTime;
#endif
    /** @} */

    /** The NT timer object. */
    KTIMER                  NtTimer;
#ifdef RTR0TIMER_NT_HIGH_RES
    /** High resolution timer.  If not NULL, this must be used instead of NtTimer. */
    PEX_TIMER               pHighResTimer;
#endif
    /** The number of sub-timers. */
    RTCPUID                 cSubTimers;
    /** Sub-timers.
     * Normally there is just one, but for RTTIMER_FLAGS_CPU_ALL this will contain
     * an entry for all possible cpus. In that case the index will be the same as
     * for the RTCpuSet. */
    RTTIMERNTSUBTIMER       aSubTimers[1];
} RTTIMER;


#ifdef RTR0TIMER_NT_MANUAL_RE_ARM

/**
 * Get current NT interrupt time.
 * @return NT interrupt time
 */
static uint64_t rtTimerNtQueryInterruptTime(void)
{
# ifdef RT_ARCH_AMD64
    return KeQueryInterruptTime(); /* macro */
# else
    if (g_pfnrtKeQueryInterruptTime)
        return g_pfnrtKeQueryInterruptTime();

    /* NT4 */
    ULARGE_INTEGER InterruptTime;
    do
    {
        InterruptTime.HighPart = ((KUSER_SHARED_DATA volatile *)SharedUserData)->InterruptTime.High1Time;
        InterruptTime.LowPart  = ((KUSER_SHARED_DATA volatile *)SharedUserData)->InterruptTime.LowPart;
    } while (((KUSER_SHARED_DATA volatile *)SharedUserData)->InterruptTime.High2Time != (LONG)InterruptTime.HighPart);
    return InterruptTime.QuadPart;
# endif
}

/**
 * Get current NT interrupt time, high resolution variant.
 * @return High resolution NT interrupt time
 */
static uint64_t rtTimerNtQueryInterruptTimeHighRes(void)
{
    if (g_pfnrtKeQueryInterruptTimePrecise)
    {
        ULONG64 uQpcIgnored;
        return g_pfnrtKeQueryInterruptTimePrecise(&uQpcIgnored);
    }
    return rtTimerNtQueryInterruptTime();
}

#endif /* RTR0TIMER_NT_MANUAL_RE_ARM */


/**
 * Worker for rtTimerNtRearmInternval that calculates the next due time.
 *
 * @returns The next due time (relative, so always negative).
 * @param   uNtNow                  The current time.
 * @param   uNtStartTime            The start time of the timer.
 * @param   iTick                   The next tick number (zero being @a uNtStartTime).
 * @param   cNtInterval             The timer interval in NT ticks.
 * @param   cNtNegDueSaftyMargin    The due time safety margin in negative NT
 *                                  ticks.
 * @param   cNtMinNegInterval       The minium interval to use when in catchup
 *                                  mode, also negative NT ticks.
 */
DECLINLINE(int64_t) rtTimerNtCalcNextDueTime(uint64_t uNtNow, uint64_t uNtStartTime, uint64_t iTick, uint64_t cNtInterval,
                                             int32_t const cNtNegDueSaftyMargin, int32_t const cNtMinNegInterval)
{
    /* Calculate the actual time elapsed since timer start: */
    int64_t iDueTime = uNtNow - uNtStartTime;
    if (iDueTime < 0)
        iDueTime = 0;

    /* Now calculate the nominal time since timer start for the next tick: */
    uint64_t const uNtNextRelStart = iTick * cNtInterval;

    /* Calulate now much time we have to the next tick: */
    iDueTime -= uNtNextRelStart;

    /* If we haven't already overshot the due time, including some safety margin, we're good: */
    if (iDueTime < cNtNegDueSaftyMargin)
        return iDueTime;

    /* Okay, we've overshot it and are in catchup mode: */
    if (iDueTime < (int64_t)cNtInterval)
        iDueTime = -(int64_t)(cNtInterval / 2); /* double time */
    else if (iDueTime < (int64_t)(cNtInterval * 4))
        iDueTime = -(int64_t)(cNtInterval / 4); /* quadruple time */
    else
        return cNtMinNegInterval;

    /* Make sure we don't try intervals smaller than the minimum specified by the caller: */
    if (iDueTime > cNtMinNegInterval)
        iDueTime = cNtMinNegInterval;
    return iDueTime;
}

/**
 * Manually re-arms an internval timer.
 *
 * Turns out NT doesn't necessarily do a very good job at re-arming timers
 * accurately, this is in part due to KeSetTimerEx API taking the interval in
 * milliseconds.
 *
 * @param   pTimer              The timer.
 * @param   pMasterDpc          The master timer DPC for passing to KeSetTimerEx
 *                              in low-resolution mode.  Ignored for high-res.
 */
static void rtTimerNtRearmInternval(PRTTIMER pTimer, PKDPC pMasterDpc)
{
#ifdef RTR0TIMER_NT_MANUAL_RE_ARM
    Assert(pTimer->u64NanoInterval);

    /*
     * For simplicity we acquire the spinlock for the whole operation.
     * This should be perfectly fine as it doesn't change the IRQL.
     */
    Assert(KeGetCurrentIrql() >= DISPATCH_LEVEL);
    KeAcquireSpinLockAtDpcLevel(&pTimer->Spinlock);

    /*
     * Make sure it wasn't suspended
     */
    if (!ASMAtomicUoReadBool(&pTimer->fSuspended))
    {
        uint64_t const cNtInterval  = ASMAtomicUoReadU64(&pTimer->u64NanoInterval) / 100;
        uint64_t const uNtStartTime = ASMAtomicUoReadU64(&pTimer->uNtStartTime);
        uint64_t const iTick        = ++pTimer->iMasterTick;

        /*
         * Calculate the deadline for the next timer tick and arm the timer.
         * We always use a relative tick, i.e. negative DueTime value.  This is
         * crucial for the the high resolution API as it will bugcheck otherwise.
         */
        int64_t  iDueTime;
        uint64_t uNtNow;
# ifdef RTR0TIMER_NT_HIGH_RES
        if (pTimer->pHighResTimer)
        {
            /* Must use highres time here. */
            uNtNow   = rtTimerNtQueryInterruptTimeHighRes();
            iDueTime = rtTimerNtCalcNextDueTime(uNtNow, uNtStartTime, iTick, cNtInterval,
                                                -100 /* 10us safety */, -2000 /* 200us min interval*/);
            g_pfnrtExSetTimer(pTimer->pHighResTimer, iDueTime, 0, NULL);
        }
        else
# endif
        {
            /* Expect interrupt time and timers to expire at the same time, so
               don't use high res time api here. */
            uNtNow   = rtTimerNtQueryInterruptTime();
            iDueTime = rtTimerNtCalcNextDueTime(uNtNow, uNtStartTime, iTick, cNtInterval,
                                                -100 /* 10us safety */, -2500 /* 250us min interval*/); /** @todo use max interval here */
            LARGE_INTEGER DueTime;
            DueTime.QuadPart = iDueTime;
            KeSetTimerEx(&pTimer->NtTimer, DueTime, 0, pMasterDpc);
        }

        pTimer->uNtDueTime = uNtNow + -iDueTime;
    }

    KeReleaseSpinLockFromDpcLevel(&pTimer->Spinlock);
#else
    RT_NOREF(pTimer, iTick, pMasterDpc);
#endif
}


/**
 * Common timer callback worker for the non-omni timers.
 *
 * @param   pTimer          The timer.
 */
static void rtTimerNtSimpleCallbackWorker(PRTTIMER pTimer)
{
    /*
     * Check that we haven't been suspended before doing the callout.
     */
    if (    !ASMAtomicUoReadBool(&pTimer->fSuspended)
        &&  pTimer->u32Magic == RTTIMER_MAGIC)
    {
        ASMAtomicWriteHandle(&pTimer->aSubTimers[0].hActiveThread, RTThreadNativeSelf());

        if (!pTimer->u64NanoInterval)
            ASMAtomicWriteBool(&pTimer->fSuspended, true);
        uint64_t iTick = ++pTimer->aSubTimers[0].iTick;

        pTimer->pfnTimer(pTimer, pTimer->pvUser, iTick);

        /* We re-arm the timer after calling pfnTimer, as it may stop the timer
           or change the interval, which would mean doing extra work. */
        if (!pTimer->fSuspended && pTimer->u64NanoInterval)
            rtTimerNtRearmInternval(pTimer, &pTimer->aSubTimers[0].NtDpc);

        ASMAtomicWriteHandle(&pTimer->aSubTimers[0].hActiveThread, NIL_RTNATIVETHREAD);
    }
}


/**
 * Timer callback function for the low-resolution non-omni timers.
 *
 * @param   pDpc                Pointer to the DPC.
 * @param   pvUser              Pointer to our internal timer structure.
 * @param   SystemArgument1     Some system argument.
 * @param   SystemArgument2     Some system argument.
 */
static void _stdcall rtTimerNtSimpleCallback(IN PKDPC pDpc, IN PVOID pvUser, IN PVOID SystemArgument1, IN PVOID SystemArgument2)
{
    PRTTIMER pTimer = (PRTTIMER)pvUser;
    AssertPtr(pTimer);
#ifdef RT_STRICT
    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
        RTAssertMsg2Weak("rtTimerNtSimpleCallback: Irql=%d expected >=%d\n", KeGetCurrentIrql(), DISPATCH_LEVEL);
#endif

    rtTimerNtSimpleCallbackWorker(pTimer);

    RT_NOREF(pDpc, SystemArgument1, SystemArgument2);
}


#ifdef RTR0TIMER_NT_HIGH_RES
/**
 * Timer callback function for the high-resolution non-omni timers.
 *
 * @param   pExTimer            The windows timer.
 * @param   pvUser              Pointer to our internal timer structure.
 */
static void _stdcall rtTimerNtHighResSimpleCallback(PEX_TIMER pExTimer, void *pvUser)
{
    PRTTIMER pTimer = (PRTTIMER)pvUser;
    AssertPtr(pTimer);
    Assert(pTimer->pHighResTimer == pExTimer);
# ifdef RT_STRICT
    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
        RTAssertMsg2Weak("rtTimerNtHighResSimpleCallback: Irql=%d expected >=%d\n", KeGetCurrentIrql(), DISPATCH_LEVEL);
# endif

    /* If we're not on the desired CPU, trigger the DPC.  That will rearm the
       timer and such. */
    if (   !pTimer->fSpecificCpu
        || pTimer->idCpu == RTMpCpuId())
        rtTimerNtSimpleCallbackWorker(pTimer);
    else
        KeInsertQueueDpc(&pTimer->aSubTimers[0].NtDpc, 0, 0);

    RT_NOREF(pExTimer);
}
#endif /* RTR0TIMER_NT_HIGH_RES */


/**
 * The slave DPC callback for an omni timer.
 *
 * @param   pDpc                The DPC object.
 * @param   pvUser              Pointer to the sub-timer.
 * @param   SystemArgument1     Some system stuff.
 * @param   SystemArgument2     Some system stuff.
 */
static void _stdcall rtTimerNtOmniSlaveCallback(IN PKDPC pDpc, IN PVOID pvUser, IN PVOID SystemArgument1, IN PVOID SystemArgument2)
{
    PRTTIMERNTSUBTIMER pSubTimer = (PRTTIMERNTSUBTIMER)pvUser;
    PRTTIMER pTimer = pSubTimer->pParent;

    AssertPtr(pTimer);
#ifdef RT_STRICT
    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
        RTAssertMsg2Weak("rtTimerNtOmniSlaveCallback: Irql=%d expected >=%d\n", KeGetCurrentIrql(), DISPATCH_LEVEL);
    int iCpuSelf = RTMpCpuIdToSetIndex(RTMpCpuId());
    if (pSubTimer - &pTimer->aSubTimers[0] != iCpuSelf)
        RTAssertMsg2Weak("rtTimerNtOmniSlaveCallback: iCpuSelf=%d pSubTimer=%p / %d\n", iCpuSelf, pSubTimer, pSubTimer - &pTimer->aSubTimers[0]);
#endif

    /*
     * Check that we haven't been suspended before doing the callout.
     */
    if (    !ASMAtomicUoReadBool(&pTimer->fSuspended)
        &&  pTimer->u32Magic == RTTIMER_MAGIC)
    {
        ASMAtomicWriteHandle(&pSubTimer->hActiveThread, RTThreadNativeSelf());

        if (!pTimer->u64NanoInterval)
            if (ASMAtomicDecS32(&pTimer->cOmniSuspendCountDown) <= 0)
                ASMAtomicWriteBool(&pTimer->fSuspended, true);

        pTimer->pfnTimer(pTimer, pTimer->pvUser, ++pSubTimer->iTick);

        ASMAtomicWriteHandle(&pSubTimer->hActiveThread, NIL_RTNATIVETHREAD);
    }

    NOREF(pDpc); NOREF(SystemArgument1); NOREF(SystemArgument2);
}


/**
 * Called when we have an impcomplete DPC object.
 *
 * @returns KeInsertQueueDpc return value.
 * @param   pSubTimer       The sub-timer to queue an DPC for.
 * @param   iCpu            The CPU set index corresponding to that sub-timer.
 */
DECL_NO_INLINE(static, BOOLEAN) rtTimerNtOmniQueueDpcSlow(PRTTIMERNTSUBTIMER pSubTimer, int iCpu)
{
    int rc = rtMpNtSetTargetProcessorDpc(&pSubTimer->NtDpc, RTMpCpuIdFromSetIndex(iCpu));
    if (RT_SUCCESS(rc))
    {
        pSubTimer->fDpcNeedTargetCpuSet = false;
        return KeInsertQueueDpc(&pSubTimer->NtDpc, 0, 0);
    }
    return FALSE;
}


/**
 * Wrapper around KeInsertQueueDpc that makes sure the target CPU has been set.
 *
 * This is for handling deferred rtMpNtSetTargetProcessorDpc failures during
 * creation.  These errors happens for offline CPUs which probably never every
 * will come online, as very few systems do CPU hotplugging.
 *
 * @returns KeInsertQueueDpc return value.
 * @param   pSubTimer       The sub-timer to queue an DPC for.
 * @param   iCpu            The CPU set index corresponding to that sub-timer.
 */
DECLINLINE(BOOLEAN) rtTimerNtOmniQueueDpc(PRTTIMERNTSUBTIMER pSubTimer, int iCpu)
{
    if (RT_LIKELY(!pSubTimer->fDpcNeedTargetCpuSet))
        return KeInsertQueueDpc(&pSubTimer->NtDpc, 0, 0);
    return rtTimerNtOmniQueueDpcSlow(pSubTimer, iCpu);
}


/**
 * Common timer callback worker for omni-timers.
 *
 * This is responsible for queueing the DPCs for the other CPUs and
 * perform the callback on the CPU on which it is called.
 *
 * @param   pTimer          The timer.
 * @param   pSubTimer       The sub-timer of the calling CPU.
 * @param   iCpuSelf        The set index of the CPU we're running on.
 */
static void rtTimerNtOmniMasterCallbackWorker(PRTTIMER pTimer, PRTTIMERNTSUBTIMER pSubTimer, int iCpuSelf)
{
    /*
     * Check that we haven't been suspended before scheduling the other DPCs
     * and doing the callout.
     */
    if (   !ASMAtomicUoReadBool(&pTimer->fSuspended)
        && pTimer->u32Magic == RTTIMER_MAGIC)
    {
        RTCPUSET OnlineSet;
        RTMpGetOnlineSet(&OnlineSet);

        ASMAtomicWriteHandle(&pSubTimer->hActiveThread, RTThreadNativeSelf());

        if (pTimer->u64NanoInterval)
        {
            /*
             * Recurring timer.
             */
            for (int iCpu = 0; iCpu < RTCPUSET_MAX_CPUS; iCpu++)
                if (   RTCpuSetIsMemberByIndex(&OnlineSet, iCpu)
                    && iCpuSelf != iCpu)
                    rtTimerNtOmniQueueDpc(&pTimer->aSubTimers[iCpu], iCpu);

            pTimer->pfnTimer(pTimer, pTimer->pvUser, ++pSubTimer->iTick);

            /* We re-arm the timer after calling pfnTimer, as it may stop the timer
               or change the interval, which would mean doing extra work. */
            if (!pTimer->fSuspended && pTimer->u64NanoInterval)
                rtTimerNtRearmInternval(pTimer, &pSubTimer->NtDpc);
        }
        else
        {
            /*
             * Single shot timers gets complicated wrt to fSuspended maintance.
             */
            uint32_t cCpus = 0;
            for (int iCpu = 0; iCpu < RTCPUSET_MAX_CPUS; iCpu++)
                if (RTCpuSetIsMemberByIndex(&OnlineSet, iCpu))
                    cCpus++;
            ASMAtomicAddS32(&pTimer->cOmniSuspendCountDown, cCpus); /** @todo this is bogus bogus bogus. The counter is only used here. */

            for (int iCpu = 0; iCpu < RTCPUSET_MAX_CPUS; iCpu++)
                if (   RTCpuSetIsMemberByIndex(&OnlineSet, iCpu)
                    && iCpuSelf != iCpu)
                    if (!rtTimerNtOmniQueueDpc(&pTimer->aSubTimers[iCpu], iCpu))
                        ASMAtomicDecS32(&pTimer->cOmniSuspendCountDown); /* already queued and counted. */

            if (ASMAtomicDecS32(&pTimer->cOmniSuspendCountDown) <= 0)
                ASMAtomicWriteBool(&pTimer->fSuspended, true);

            pTimer->pfnTimer(pTimer, pTimer->pvUser, ++pSubTimer->iTick);
        }

        ASMAtomicWriteHandle(&pSubTimer->hActiveThread, NIL_RTNATIVETHREAD);
    }
}


/**
 * The timer callback for an omni-timer, low-resolution.
 *
 * @param   pDpc                The DPC object.
 * @param   pvUser              Pointer to the sub-timer.
 * @param   SystemArgument1     Some system stuff.
 * @param   SystemArgument2     Some system stuff.
 */
static void _stdcall rtTimerNtOmniMasterCallback(IN PKDPC pDpc, IN PVOID pvUser, IN PVOID SystemArgument1, IN PVOID SystemArgument2)
{
    PRTTIMERNTSUBTIMER const pSubTimer = (PRTTIMERNTSUBTIMER)pvUser;
    PRTTIMER const           pTimer    = pSubTimer->pParent;
    RTCPUID                  idCpu     = RTMpCpuId();
    int const                iCpuSelf  = RTMpCpuIdToSetIndex(idCpu);

    AssertPtr(pTimer);
#ifdef RT_STRICT
    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
        RTAssertMsg2Weak("rtTimerNtOmniMasterCallback: Irql=%d expected >=%d\n", KeGetCurrentIrql(), DISPATCH_LEVEL);
    /* We must be called on the master CPU or the tick variable goes south. */
    if (pSubTimer - &pTimer->aSubTimers[0] != iCpuSelf)
        RTAssertMsg2Weak("rtTimerNtOmniMasterCallback: iCpuSelf=%d pSubTimer=%p / %d\n", iCpuSelf, pSubTimer, pSubTimer - &pTimer->aSubTimers[0]);
    if (pTimer->idCpu != idCpu)
        RTAssertMsg2Weak("rtTimerNtOmniMasterCallback: pTimer->idCpu=%d vs idCpu=%d\n", pTimer->idCpu, idCpu);
#endif

    rtTimerNtOmniMasterCallbackWorker(pTimer, pSubTimer, iCpuSelf);

    RT_NOREF(pDpc, SystemArgument1, SystemArgument2);
}


#ifdef RTR0TIMER_NT_HIGH_RES
/**
 * The timer callback for an high-resolution omni-timer.
 *
 * @param   pExTimer            The windows timer.
 * @param   pvUser              Pointer to our internal timer structure.
 */
static void __stdcall rtTimerNtHighResOmniCallback(PEX_TIMER pExTimer, void *pvUser)
{
    PRTTIMER const           pTimer    = (PRTTIMER)pvUser;
    int const                iCpuSelf  = RTMpCpuIdToSetIndex(RTMpCpuId());
    PRTTIMERNTSUBTIMER const pSubTimer = &pTimer->aSubTimers[iCpuSelf];

    AssertPtr(pTimer);
    Assert(pTimer->pHighResTimer == pExTimer);
# ifdef RT_STRICT
    if (KeGetCurrentIrql() < DISPATCH_LEVEL)
        RTAssertMsg2Weak("rtTimerNtHighResOmniCallback: Irql=%d expected >=%d\n", KeGetCurrentIrql(), DISPATCH_LEVEL);
# endif

    rtTimerNtOmniMasterCallbackWorker(pTimer, pSubTimer, iCpuSelf);

    RT_NOREF(pExTimer);
}
#endif /* RTR0TIMER_NT_HIGH_RES */


RTDECL(int) RTTimerStart(PRTTIMER pTimer, uint64_t u64First)
{
    /*
     * Validate.
     */
    AssertPtrReturn(pTimer, VERR_INVALID_HANDLE);
    AssertReturn(pTimer->u32Magic == RTTIMER_MAGIC, VERR_INVALID_HANDLE);

    /*
     * The operation is protected by the spinlock.
     */
    KIRQL bSavedIrql;
    KeAcquireSpinLock(&pTimer->Spinlock, &bSavedIrql);

    /*
     * Check the state.
     */
    if (ASMAtomicUoReadBool(&pTimer->fSuspended))
    { /* likely */ }
    else
    {
        KeReleaseSpinLock(&pTimer->Spinlock, bSavedIrql);
        return VERR_TIMER_ACTIVE;
    }
    if (   !pTimer->fSpecificCpu
        || RTMpIsCpuOnline(pTimer->idCpu))
    { /* likely */ }
    else
    {
        KeReleaseSpinLock(&pTimer->Spinlock, bSavedIrql);
        return VERR_CPU_OFFLINE;
    }

    /*
     * Lazy set the DPC target CPU if needed.
     */
    if (   !pTimer->fSpecificCpu
        || !pTimer->aSubTimers[0].fDpcNeedTargetCpuSet)
    { /* likely */ }
    else
    {
        int rc = rtMpNtSetTargetProcessorDpc(&pTimer->aSubTimers[0].NtDpc, pTimer->idCpu);
        if (RT_FAILURE(rc))
        {
            KeReleaseSpinLock(&pTimer->Spinlock, bSavedIrql);
            return rc;
        }
    }

    /*
     * Do the starting.
     */
#ifndef RTR0TIMER_NT_MANUAL_RE_ARM
    /* Calculate the interval time: */
    uint64_t u64Interval = pTimer->u64NanoInterval / 1000000; /* This is ms, believe it or not. */
    ULONG ulInterval = (ULONG)u64Interval;
    if (ulInterval != u64Interval)
        ulInterval = MAXLONG;
    else if (!ulInterval && pTimer->u64NanoInterval)
        ulInterval = 1;
#endif

    /* Translate u64First to a DueTime: */
    LARGE_INTEGER DueTime;
    DueTime.QuadPart = -(int64_t)(u64First / 100); /* Relative, NT time. */
    if (!DueTime.QuadPart)
        DueTime.QuadPart = -10; /* 1us */

    /* Reset tick counters: */
    unsigned cSubTimers = pTimer->fOmniTimer ? pTimer->cSubTimers : 1;
    for (unsigned iCpu = 0; iCpu < cSubTimers; iCpu++)
        pTimer->aSubTimers[iCpu].iTick = 0;
    pTimer->iMasterTick = 0;

    /* Update timer state: */
#ifdef RTR0TIMER_NT_MANUAL_RE_ARM
    if (pTimer->u64NanoInterval > 0)
    {
#ifdef RTR0TIMER_NT_HIGH_RES
        uint64_t const uNtNow = pTimer->pHighResTimer ? rtTimerNtQueryInterruptTimeHighRes() : rtTimerNtQueryInterruptTime();
# else
        uint64_t const uNtNow = rtTimerNtQueryInterruptTime();
# endif
        pTimer->uNtStartTime  = uNtNow + -DueTime.QuadPart;
        pTimer->uNtDueTime    = pTimer->uNtStartTime;
    }
#endif
    pTimer->cOmniSuspendCountDown = 0;
    ASMAtomicWriteBool(&pTimer->fSuspended, false);

    /*
     * Finally start the NT timer.
     *
     * We do this without holding the spinlock to err on the side of
     * caution in case ExSetTimer or KeSetTimerEx ever should have the idea
     * of running the callback before returning.
     */
    KeReleaseSpinLock(&pTimer->Spinlock, bSavedIrql);

#ifdef RTR0TIMER_NT_HIGH_RES
    if (pTimer->pHighResTimer)
    {
# ifdef RTR0TIMER_NT_MANUAL_RE_ARM
        g_pfnrtExSetTimer(pTimer->pHighResTimer, DueTime.QuadPart, 0, NULL);
# else
        g_pfnrtExSetTimer(pTimer->pHighResTimer, DueTime.QuadPart, RT_MIN(pTimer->u64NanoInterval / 100, MAXLONG), NULL);
# endif
    }
    else
#endif
    {
        PKDPC const pMasterDpc = &pTimer->aSubTimers[pTimer->fOmniTimer ? RTMpCpuIdToSetIndex(pTimer->idCpu) : 0].NtDpc;
#ifdef RTR0TIMER_NT_MANUAL_RE_ARM
        KeSetTimerEx(&pTimer->NtTimer, DueTime, 0, pMasterDpc);
#else
        KeSetTimerEx(&pTimer->NtTimer, DueTime, ulInterval, pMasterDpc);
#endif
    }
    return VINF_SUCCESS;
}


/**
 * Worker function that stops an active timer.
 *
 * Shared by RTTimerStop and RTTimerDestroy.
 *
 * @param   pTimer      The active timer.
 */
static int rtTimerNtStopWorker(PRTTIMER pTimer)
{
    /*
     * Update the state from with the spinlock context.
     */
    KIRQL bSavedIrql;
    KeAcquireSpinLock(&pTimer->Spinlock, &bSavedIrql);

    bool const fWasSuspended = ASMAtomicXchgBool(&pTimer->fSuspended, true);

    KeReleaseSpinLock(&pTimer->Spinlock, bSavedIrql);
    if (!fWasSuspended)
    {
        /*
         * We should cacnel the timer and dequeue DPCs.
         */
#ifdef RTR0TIMER_NT_HIGH_RES
        if (pTimer->pHighResTimer)
        {
            g_pfnrtExCancelTimer(pTimer->pHighResTimer, NULL);

            /* We can skip the DPC stuff, unless this is an omni timer or for a specific CPU. */
            if (!pTimer->fSpecificCpu && !pTimer->fOmniTimer)
                return VINF_SUCCESS;
        }
        else
#endif
            KeCancelTimer(&pTimer->NtTimer);

        for (RTCPUID iCpu = 0; iCpu < pTimer->cSubTimers; iCpu++)
            KeRemoveQueueDpc(&pTimer->aSubTimers[iCpu].NtDpc);
        return VINF_SUCCESS;
    }
    return VERR_TIMER_SUSPENDED;
}


RTDECL(int) RTTimerStop(PRTTIMER pTimer)
{
    /*
     * Validate.
     */
    AssertPtrReturn(pTimer, VERR_INVALID_HANDLE);
    AssertReturn(pTimer->u32Magic == RTTIMER_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Call the worker we share with RTTimerDestroy.
     */
    return rtTimerNtStopWorker(pTimer);
}


RTDECL(int) RTTimerChangeInterval(PRTTIMER pTimer, uint64_t u64NanoInterval)
{
    AssertPtrReturn(pTimer, VERR_INVALID_HANDLE);
    AssertReturn(pTimer->u32Magic == RTTIMER_MAGIC, VERR_INVALID_HANDLE);

    /*
     * We do all the state changes while holding the spinlock.
     */
    int   rc = VINF_SUCCESS;
    KIRQL bSavedIrql;
    KeAcquireSpinLock(&pTimer->Spinlock, &bSavedIrql);

    /*
     * When the timer isn't running, this is an simple job:
     */
    if (!ASMAtomicUoReadBool(&pTimer->fSuspended))
        pTimer->u64NanoInterval = u64NanoInterval;
    else
    {
        /*
         * We only implement changing the interval in RTR0TIMER_NT_MANUAL_RE_ARM
         * mode right now. We typically let the new interval take effect after
         * the next timer callback, unless that's too far ahead.
         */
#ifdef RTR0TIMER_NT_MANUAL_RE_ARM
        pTimer->u64NanoInterval  = u64NanoInterval;
        pTimer->iMasterTick      = 0;
# ifdef RTR0TIMER_NT_HIGH_RES
        uint64_t const uNtNow = pTimer->pHighResTimer ? rtTimerNtQueryInterruptTimeHighRes() : rtTimerNtQueryInterruptTime();
# else
        uint64_t const uNtNow = rtTimerNtQueryInterruptTime();
# endif
        if (uNtNow >= pTimer->uNtDueTime)
            pTimer->uNtStartTime = uNtNow;
        else
        {
            pTimer->uNtStartTime = pTimer->uNtDueTime;

            /*
             * Re-arm the timer if the next DueTime is both more than 1.25 new
             * intervals and at least 0.5 ms ahead.
             */
            uint64_t cNtToNext = pTimer->uNtDueTime - uNtNow;
            if (   cNtToNext >= RT_NS_1MS / 2 / 100 /* 0.5 ms */
                && cNtToNext * 100 > u64NanoInterval + u64NanoInterval / 4)
            {
                pTimer->uNtStartTime = pTimer->uNtDueTime = uNtNow + u64NanoInterval / 100;
# ifdef RTR0TIMER_NT_HIGH_RES
                if (pTimer->pHighResTimer)
                    g_pfnrtExSetTimer(pTimer->pHighResTimer, -(int64_t)u64NanoInterval / 100, 0, NULL);
                else
# endif
                {
                    LARGE_INTEGER DueTime;
                    DueTime.QuadPart = -(int64_t)u64NanoInterval / 100;
                    KeSetTimerEx(&pTimer->NtTimer, DueTime, 0,
                                 &pTimer->aSubTimers[pTimer->fOmniTimer ? RTMpCpuIdToSetIndex(pTimer->idCpu) : 0].NtDpc);
                }
            }
        }
#else
        rc = VERR_NOT_SUPPORTED;
#endif
    }

    KeReleaseSpinLock(&pTimer->Spinlock, bSavedIrql);

    return rc;
}


RTDECL(int) RTTimerDestroy(PRTTIMER pTimer)
{
    /* It's ok to pass NULL pointer. */
    if (pTimer == /*NIL_RTTIMER*/ NULL)
        return VINF_SUCCESS;
    AssertPtrReturn(pTimer, VERR_INVALID_HANDLE);
    AssertReturn(pTimer->u32Magic == RTTIMER_MAGIC, VERR_INVALID_HANDLE);

    /*
     * We do not support destroying a timer from the callback because it is
     * not 101% safe since we cannot flush DPCs.  Solaris has the same restriction.
     */
    AssertReturn(KeGetCurrentIrql() == PASSIVE_LEVEL, VERR_INVALID_CONTEXT);

    /*
     * Invalidate the timer, stop it if it's running and finally free up the memory.
     */
    ASMAtomicWriteU32(&pTimer->u32Magic, ~RTTIMER_MAGIC);
    rtTimerNtStopWorker(pTimer);

#ifdef RTR0TIMER_NT_HIGH_RES
    /*
     * Destroy the high-resolution timer before flushing DPCs.
     */
    if (pTimer->pHighResTimer)
    {
        g_pfnrtExDeleteTimer(pTimer->pHighResTimer, TRUE /*fCancel*/, TRUE /*fWait*/, NULL);
        pTimer->pHighResTimer = NULL;
    }
#endif

    /*
     * Flush DPCs to be on the safe side.
     */
    if (g_pfnrtNtKeFlushQueuedDpcs)
        g_pfnrtNtKeFlushQueuedDpcs();

    RTMemFree(pTimer);

    return VINF_SUCCESS;
}


RTDECL(int) RTTimerCreateEx(PRTTIMER *ppTimer, uint64_t u64NanoInterval, uint32_t fFlags, PFNRTTIMER pfnTimer, void *pvUser)
{
    *ppTimer = NULL;

    /*
     * Validate flags.
     */
    if (!RTTIMER_FLAGS_ARE_VALID(fFlags))
        return VERR_INVALID_FLAGS;
    if (    (fFlags & RTTIMER_FLAGS_CPU_SPECIFIC)
        &&  (fFlags & RTTIMER_FLAGS_CPU_ALL) != RTTIMER_FLAGS_CPU_ALL
        &&  !RTMpIsCpuPossible(RTMpCpuIdFromSetIndex(fFlags & RTTIMER_FLAGS_CPU_MASK)))
        return VERR_CPU_NOT_FOUND;

    /*
     * Allocate the timer handler.
     */
    RTCPUID cSubTimers = 1;
    if ((fFlags & RTTIMER_FLAGS_CPU_ALL) == RTTIMER_FLAGS_CPU_ALL)
    {
        cSubTimers = RTMpGetMaxCpuId() + 1;
        Assert(cSubTimers <= RTCPUSET_MAX_CPUS); /* On Windows we have a 1:1 relationship between cpuid and set index. */
    }

    PRTTIMER pTimer = (PRTTIMER)RTMemAllocZ(RT_UOFFSETOF_DYN(RTTIMER, aSubTimers[cSubTimers]));
    if (!pTimer)
        return VERR_NO_MEMORY;

    /*
     * Initialize it.
     *
     * Note! The difference between a SynchronizationTimer and a NotificationTimer
     *       (KeInitializeTimer) is, as far as I can gather, only that the former
     *       will wake up exactly one waiting thread and the latter will wake up
     *       everyone.  Since we don't do any waiting on the NtTimer, that is not
     *       relevant to us.
     */
    pTimer->u32Magic = RTTIMER_MAGIC;
    pTimer->cOmniSuspendCountDown = 0;
    pTimer->fSuspended = true;
    pTimer->fSpecificCpu = (fFlags & RTTIMER_FLAGS_CPU_SPECIFIC) && (fFlags & RTTIMER_FLAGS_CPU_ALL) != RTTIMER_FLAGS_CPU_ALL;
    pTimer->fOmniTimer = (fFlags & RTTIMER_FLAGS_CPU_ALL) == RTTIMER_FLAGS_CPU_ALL;
    pTimer->idCpu = pTimer->fSpecificCpu ? RTMpCpuIdFromSetIndex(fFlags & RTTIMER_FLAGS_CPU_MASK) : NIL_RTCPUID;
    pTimer->cSubTimers = cSubTimers;
    pTimer->pfnTimer = pfnTimer;
    pTimer->pvUser = pvUser;
    KeInitializeSpinLock(&pTimer->Spinlock);
    pTimer->u64NanoInterval = u64NanoInterval;

    int rc = VINF_SUCCESS;
#ifdef RTR0TIMER_NT_HIGH_RES
    if (   (fFlags & RTTIMER_FLAGS_HIGH_RES)
        && RTTimerCanDoHighResolution())
    {
        pTimer->pHighResTimer = g_pfnrtExAllocateTimer(pTimer->fOmniTimer ? rtTimerNtHighResOmniCallback
                                                       : rtTimerNtHighResSimpleCallback, pTimer,
                                                       EX_TIMER_HIGH_RESOLUTION | EX_TIMER_NOTIFICATION);
        if (!pTimer->pHighResTimer)
            rc = VERR_OUT_OF_RESOURCES;
    }
    else
#endif
    {
        if (g_pfnrtKeInitializeTimerEx) /** @todo just call KeInitializeTimer. */
            g_pfnrtKeInitializeTimerEx(&pTimer->NtTimer, SynchronizationTimer);
        else
            KeInitializeTimer(&pTimer->NtTimer);
    }
    if (RT_SUCCESS(rc))
    {
        RTCPUSET OnlineSet;
        RTMpGetOnlineSet(&OnlineSet);

        if (pTimer->fOmniTimer)
        {
            /*
             * Initialize the per-cpu "sub-timers", select the first online cpu to be
             * the master.  This ASSUMES that no cpus will ever go offline.
             *
             * Note! For the high-resolution scenario, all DPC callbacks are slaves as
             *       we have a dedicated timer callback, set above during allocation,
             *       and don't control which CPU it (rtTimerNtHighResOmniCallback) is
             *       called on.
             */
            pTimer->iMasterTick = 0;
            pTimer->idCpu       = NIL_RTCPUID;
            for (unsigned iCpu = 0; iCpu < cSubTimers; iCpu++)
            {
                pTimer->aSubTimers[iCpu].iTick   = 0;
                pTimer->aSubTimers[iCpu].pParent = pTimer;

                if (   pTimer->idCpu == NIL_RTCPUID
                    && RTCpuSetIsMemberByIndex(&OnlineSet, iCpu))
                {
                    pTimer->idCpu = RTMpCpuIdFromSetIndex(iCpu);
#ifdef RTR0TIMER_NT_HIGH_RES
                    if (pTimer->pHighResTimer)
                        KeInitializeDpc(&pTimer->aSubTimers[iCpu].NtDpc, rtTimerNtOmniSlaveCallback, &pTimer->aSubTimers[iCpu]);
                    else
#endif
                        KeInitializeDpc(&pTimer->aSubTimers[iCpu].NtDpc, rtTimerNtOmniMasterCallback, &pTimer->aSubTimers[iCpu]);
                }
                else
                    KeInitializeDpc(&pTimer->aSubTimers[iCpu].NtDpc, rtTimerNtOmniSlaveCallback, &pTimer->aSubTimers[iCpu]);
                if (g_pfnrtKeSetImportanceDpc)
                    g_pfnrtKeSetImportanceDpc(&pTimer->aSubTimers[iCpu].NtDpc, HighImportance);

                /* This does not necessarily work for offline CPUs that could potentially be onlined
                   at runtime, so postpone it. (See troubles on testboxmem1 after r148799.) */
                int rc2 = rtMpNtSetTargetProcessorDpc(&pTimer->aSubTimers[iCpu].NtDpc, iCpu);
                if (RT_SUCCESS(rc2))
                    pTimer->aSubTimers[0].fDpcNeedTargetCpuSet = false;
                else if (!RTCpuSetIsMemberByIndex(&OnlineSet, iCpu))
                    pTimer->aSubTimers[0].fDpcNeedTargetCpuSet = true;
                else
                {
                    rc = rc2;
                    break;
                }
            }
            Assert(pTimer->idCpu != NIL_RTCPUID);
        }
        else
        {
            /*
             * Initialize the first "sub-timer", target the DPC on a specific processor
             * if requested to do so.
             */
            pTimer->iMasterTick           = 0;
            pTimer->aSubTimers[0].iTick   = 0;
            pTimer->aSubTimers[0].pParent = pTimer;

            KeInitializeDpc(&pTimer->aSubTimers[0].NtDpc, rtTimerNtSimpleCallback, pTimer);
            if (g_pfnrtKeSetImportanceDpc)
                g_pfnrtKeSetImportanceDpc(&pTimer->aSubTimers[0].NtDpc, HighImportance);
            if (pTimer->fSpecificCpu)
            {
                /* This does not necessarily work for offline CPUs that could potentially be onlined
                   at runtime, so postpone it. (See troubles on testboxmem1 after r148799.) */
                int rc2 = rtMpNtSetTargetProcessorDpc(&pTimer->aSubTimers[0].NtDpc, pTimer->idCpu);
                if (RT_SUCCESS(rc2))
                    pTimer->aSubTimers[0].fDpcNeedTargetCpuSet = false;
                else if (!RTCpuSetIsMember(&OnlineSet, pTimer->idCpu))
                    pTimer->aSubTimers[0].fDpcNeedTargetCpuSet = true;
                else
                    rc = rc2;
            }
        }
        if (RT_SUCCESS(rc))
        {
            *ppTimer = pTimer;
            return VINF_SUCCESS;
        }

#ifdef RTR0TIMER_NT_HIGH_RES
        if (pTimer->pHighResTimer)
        {
            g_pfnrtExDeleteTimer(pTimer->pHighResTimer, FALSE, FALSE, NULL);
            pTimer->pHighResTimer = NULL;
        }
#endif
    }

    RTMemFree(pTimer);
    return rc;
}


RTDECL(int) RTTimerRequestSystemGranularity(uint32_t u32Request, uint32_t *pu32Granted)
{
    if (!g_pfnrtNtExSetTimerResolution)
        return VERR_NOT_SUPPORTED;

    ULONG ulGranted = g_pfnrtNtExSetTimerResolution(u32Request / 100, TRUE);
    if (pu32Granted)
        *pu32Granted = ulGranted * 100; /* NT -> ns */
    return VINF_SUCCESS;
}


RTDECL(int) RTTimerReleaseSystemGranularity(uint32_t u32Granted)
{
    if (!g_pfnrtNtExSetTimerResolution)
        return VERR_NOT_SUPPORTED;

    g_pfnrtNtExSetTimerResolution(0 /* ignored */, FALSE);
    NOREF(u32Granted);
    return VINF_SUCCESS;
}


RTDECL(bool) RTTimerCanDoHighResolution(void)
{
#ifdef RTR0TIMER_NT_HIGH_RES
    return g_pfnrtExAllocateTimer != NULL
        && g_pfnrtExDeleteTimer   != NULL
        && g_pfnrtExSetTimer      != NULL
        && g_pfnrtExCancelTimer   != NULL;
#else
    return false;
#endif
}

