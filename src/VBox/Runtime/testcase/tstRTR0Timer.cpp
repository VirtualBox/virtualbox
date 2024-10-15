/* $Id: tstRTR0Timer.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT R0 Testcase - Timers.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

#include <iprt/asm.h>
#include <iprt/asm-amd64-x86.h>
#include <iprt/cpuset.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/mp.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/time.h>
#include <VBox/sup.h>
#include "tstRTR0Timer.h"
#include "tstRTR0Common.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct
{
    /** Array of nano second timestamp of the first few shots. */
    uint64_t volatile   aShotNsTSes[10];
    /** The number of shots. */
    uint32_t volatile   cShots;
    /** The shot at which action is to be taken. */
    uint32_t            iActionShot;
    /** The RC of whatever operation performed in the handler. */
    int volatile        rc;
    /** Set if it's a periodic test. */
    bool                fPeriodic;
    /** Test specific stuff. */
    union
    {
        /** tstRTR0TimerCallbackU32ChangeInterval parameters.  */
        struct
        {
            /** The interval change step. */
            uint32_t            cNsChangeStep;
            /** The current timer interval. */
            uint32_t            cNsCurInterval;
            /** The minimum interval. */
            uint32_t            cNsMinInterval;
            /** The maximum interval. */
            uint32_t            cNsMaxInterval;
            /** Direction flag; false = decrement, true = increment. */
            bool                fDirection;
            /** The number of steps between each change. */
            uint8_t             cStepsBetween;
        } ChgInt;
        /** tstRTR0TimerCallbackSpecific parameters.  */
        struct
        {
            /** The expected CPU. */
            RTCPUID             idCpu;
            /** Set if this failed. */
            bool                fFailed;
        } Specific;
    } u;
} TSTRTR0TIMERS1;
typedef TSTRTR0TIMERS1 *PTSTRTR0TIMERS1;


/**
 * Per cpu state for an omni timer test.
 */
typedef struct TSTRTR0TIMEROMNI1
{
    /** When we started receiving timer callbacks on this CPU. */
    uint64_t            u64Start;
    /** When we received the last tick on this timer. */
    uint64_t            u64Last;
    /** The number of ticks received on this CPU. */
    uint32_t volatile   cTicks;
    uint32_t            u32Padding;
} TSTRTR0TIMEROMNI1;
typedef TSTRTR0TIMEROMNI1 *PTSTRTR0TIMEROMNI1;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * Latency data.
 */
static struct TSTRTR0TIMEROMNILATENCY
{
    /** The number of samples.  */
    volatile uint32_t   cSamples;
    uint32_t            auPadding[3];
    struct
    {
        uint64_t        uTsc;
        uint64_t        uNanoTs;
    } aSamples[4096];
} g_aOmniLatency[16];




/**
 * Callback for the omni timer latency test, adds a sample to g_aOmniLatency.
 *
 * @param   pTimer      The timer.
 * @param   iTick       The current tick.
 * @param   pvUser      The user argument.
 */
static DECLCALLBACK(void) tstRTR0TimerCallbackLatencyOmni(PRTTIMER pTimer, void *pvUser, uint64_t iTick)
{
    RTCPUID             idCpu    = RTMpCpuId();
    uint32_t            iCpu     = RTMpCpuIdToSetIndex(idCpu);
    NOREF(pTimer); NOREF(pvUser); NOREF(iTick);

    //RTR0TESTR0_CHECK_MSG(iCpu < RT_ELEMENTS(g_aOmniLatency), ("iCpu=%d idCpu=%u\n", iCpu, idCpu));
    if (iCpu < RT_ELEMENTS(g_aOmniLatency))
    {
        uint32_t iSample = g_aOmniLatency[iCpu].cSamples;
        if (iSample < RT_ELEMENTS(g_aOmniLatency[iCpu].aSamples))
        {
            g_aOmniLatency[iCpu].aSamples[iSample].uTsc    = ASMReadTSC();
            g_aOmniLatency[iCpu].aSamples[iSample].uNanoTs = RTTimeSystemNanoTS();
            g_aOmniLatency[iCpu].cSamples = iSample + 1;
        }
    }
}


/**
 * Callback which increments a 32-bit counter.
 *
 * @param   pTimer      The timer.
 * @param   iTick       The current tick.
 * @param   pvUser      The user argument.
 */
static DECLCALLBACK(void) tstRTR0TimerCallbackOmni(PRTTIMER pTimer, void *pvUser, uint64_t iTick)
{
    PTSTRTR0TIMEROMNI1  paStates = (PTSTRTR0TIMEROMNI1)pvUser;
    RTCPUID             idCpu    = RTMpCpuId();
    uint32_t            iCpu     = RTMpCpuIdToSetIndex(idCpu);
    NOREF(pTimer);

    RTR0TESTR0_CHECK_MSG(iCpu < RTCPUSET_MAX_CPUS, ("iCpu=%d idCpu=%u\n", iCpu, idCpu));
    if (iCpu < RTCPUSET_MAX_CPUS)
    {
        uint32_t iCountedTick = ASMAtomicIncU32(&paStates[iCpu].cTicks);
        RTR0TESTR0_CHECK_MSG(iCountedTick == iTick,
                             ("iCountedTick=%u iTick=%u iCpu=%d idCpu=%u\n", iCountedTick, iTick, iCpu, idCpu));
        paStates[iCpu].u64Last = RTTimeSystemNanoTS();
        if (!paStates[iCpu].u64Start)
        {
            paStates[iCpu].u64Start = paStates[iCpu].u64Last;
            RTR0TESTR0_CHECK_MSG(iCountedTick == 1, ("iCountedTick=%u iCpu=%d idCpu=%u\n", iCountedTick, iCpu, idCpu));
        }
    }
}


/**
 * Callback for one-shot resolution detection.
 *
 * @param   pTimer      The timer.
 * @param   iTick       The current tick.
 * @param   pvUser      Points to variable with the start TS, update to time
 *                      elapsed till this call.
 */
static DECLCALLBACK(void) tstRTR0TimerCallbackOneShotElapsed(PRTTIMER pTimer, void *pvUser, uint64_t iTick)
{
    RT_NOREF(pTimer, iTick);
    uint64_t *puNanoTS = (uint64_t *)pvUser;
    *puNanoTS = RTTimeSystemNanoTS() - *puNanoTS;
}


/**
 * Callback which increments a 32-bit counter.
 *
 * @param   pTimer      The timer.
 * @param   iTick       The current tick.
 * @param   pvUser      The user argument.
 */
static DECLCALLBACK(void) tstRTR0TimerCallbackSpecific(PRTTIMER pTimer, void *pvUser, uint64_t iTick)
{
    PTSTRTR0TIMERS1 pState = (PTSTRTR0TIMERS1)pvUser;
    uint32_t        iShot  = ASMAtomicIncU32(&pState->cShots);
    NOREF(pTimer);

    if (iShot <= RT_ELEMENTS(pState->aShotNsTSes))
        pState->aShotNsTSes[iShot - 1] = RTTimeSystemNanoTS();

    RTCPUID idCpu = RTMpCpuId();
    if (pState->u.Specific.idCpu != idCpu)
        pState->u.Specific.fFailed = true;
    RTR0TESTR0_CHECK_MSG(pState->u.Specific.idCpu == idCpu, ("idCpu=%u, expected %u\n", idCpu, pState->u.Specific.idCpu));

    if (pState->fPeriodic)
        RTR0TESTR0_CHECK_MSG(iShot == iTick, ("iShot=%u iTick=%u\n", iShot, iTick));
    else
        RTR0TESTR0_CHECK_MSG(iTick == 1, ("iShot=%u iTick=%u\n", iShot, iTick));
}


/**
 * Callback which changes the interval at each invocation.
 *
 * The changes are governed by TSTRTR0TIMERS1::ChangeInterval.  The callback
 * calls RTTimerStop at iActionShot.
 *
 * @param   pTimer      The timer.
 * @param   iTick       The current tick.
 * @param   pvUser      The user argument.
 */
static DECLCALLBACK(void) tstRTR0TimerCallbackChangeInterval(PRTTIMER pTimer, void *pvUser, uint64_t iTick)
{
    PTSTRTR0TIMERS1 pState = (PTSTRTR0TIMERS1)pvUser;
    uint32_t        iShot  = ASMAtomicIncU32(&pState->cShots) - 1;

    if (iShot < RT_ELEMENTS(pState->aShotNsTSes))
        pState->aShotNsTSes[iShot] = RTTimeSystemNanoTS();
    if (pState->fPeriodic)
        RTR0TESTR0_CHECK_MSG(iShot + 1 == iTick, ("iShot=%u iTick=%u\n", iShot, iTick));
    else
        RTR0TESTR0_CHECK_MSG(iTick == 1, ("iShot=%u iTick=%u\n", iShot, iTick));

    if (!(iShot % pState->u.ChgInt.cStepsBetween))
    {
        if (pState->u.ChgInt.fDirection)
        {
            pState->u.ChgInt.cNsCurInterval += pState->u.ChgInt.cNsChangeStep;
            if (   pState->u.ChgInt.cNsCurInterval > pState->u.ChgInt.cNsMaxInterval
                || pState->u.ChgInt.cNsCurInterval < pState->u.ChgInt.cNsMinInterval
                || !pState->u.ChgInt.cNsCurInterval)
            {
                pState->u.ChgInt.cNsCurInterval = pState->u.ChgInt.cNsMaxInterval;
                pState->u.ChgInt.fDirection = false;
            }
        }
        else
        {
            pState->u.ChgInt.cNsCurInterval -= pState->u.ChgInt.cNsChangeStep;
            if (   pState->u.ChgInt.cNsCurInterval < pState->u.ChgInt.cNsMinInterval
                || pState->u.ChgInt.cNsCurInterval > pState->u.ChgInt.cNsMaxInterval
                || pState->u.ChgInt.cNsCurInterval)
            {
                pState->u.ChgInt.cNsCurInterval = pState->u.ChgInt.cNsMinInterval;
                pState->u.ChgInt.fDirection = true;
            }
        }

        RTR0TESTR0_CHECK_RC(RTTimerChangeInterval(pTimer, pState->u.ChgInt.cNsCurInterval), VINF_SUCCESS);
    }

    if (iShot == pState->iActionShot)
        RTR0TESTR0_CHECK_RC(pState->rc = RTTimerStop(pTimer), VINF_SUCCESS);
}


/**
 * Callback which increments destroy the timer when it fires.
 *
 * @param   pTimer      The timer.
 * @param   iTick       The current tick.
 * @param   pvUser      The user argument.
 */
static DECLCALLBACK(void) tstRTR0TimerCallbackDestroyOnce(PRTTIMER pTimer, void *pvUser, uint64_t iTick)
{
    PTSTRTR0TIMERS1 pState = (PTSTRTR0TIMERS1)pvUser;
    uint32_t        iShot  = ASMAtomicIncU32(&pState->cShots);

    if (iShot <= RT_ELEMENTS(pState->aShotNsTSes))
        pState->aShotNsTSes[iShot - 1] = RTTimeSystemNanoTS();
    if (pState->fPeriodic)
        RTR0TESTR0_CHECK_MSG(iShot == iTick, ("iShot=%u iTick=%u\n", iShot, iTick));
    else
        RTR0TESTR0_CHECK_MSG(iTick == 1, ("iShot=%u iTick=%u\n", iShot, iTick));

    if (iShot == pState->iActionShot + 1)
        RTR0TESTR0_CHECK_RC(pState->rc = RTTimerDestroy(pTimer), VINF_SUCCESS);
}


/**
 * Callback which increments restarts a timer once.
 *
 * @param   pTimer      The timer.
 * @param   iTick       The current tick.
 * @param   pvUser      The user argument.
 */
static DECLCALLBACK(void) tstRTR0TimerCallbackRestartOnce(PRTTIMER pTimer, void *pvUser, uint64_t iTick)
{
    PTSTRTR0TIMERS1 pState = (PTSTRTR0TIMERS1)pvUser;
    uint32_t        iShot  = ASMAtomicIncU32(&pState->cShots);

    if (iShot <= RT_ELEMENTS(pState->aShotNsTSes))
        pState->aShotNsTSes[iShot - 1] = RTTimeSystemNanoTS();
    if (pState->fPeriodic)
        RTR0TESTR0_CHECK_MSG(iShot == iTick, ("iShot=%u iTick=%u\n", iShot, iTick));
    else
        RTR0TESTR0_CHECK_MSG(iTick == 1, ("iShot=%u iTick=%u\n", iShot, iTick));

    if (iShot == pState->iActionShot + 1)
        RTR0TESTR0_CHECK_RC(pState->rc = RTTimerStart(pTimer, 10000000 /* 10ms */), VINF_SUCCESS);
}


/**
 * Callback which increments a 32-bit counter.
 *
 * @param   pTimer      The timer.
 * @param   iTick       The current tick.
 * @param   pvUser      The user argument.
 */
static DECLCALLBACK(void) tstRTR0TimerCallbackU32Counter(PRTTIMER pTimer, void *pvUser, uint64_t iTick)
{
    PTSTRTR0TIMERS1 pState = (PTSTRTR0TIMERS1)pvUser;
    uint32_t        iShot  = ASMAtomicIncU32(&pState->cShots);
    NOREF(pTimer);

    if (iShot <= RT_ELEMENTS(pState->aShotNsTSes))
        pState->aShotNsTSes[iShot - 1] = RTTimeSystemNanoTS();
    if (pState->fPeriodic)
        RTR0TESTR0_CHECK_MSG(iShot == iTick, ("iShot=%u iTick=%u\n", iShot, iTick));
    else
        RTR0TESTR0_CHECK_MSG(iTick == 1, ("iShot=%u iTick=%u\n", iShot, iTick));
}


#ifdef SOME_UNUSED_FUNCTION
/**
 * Checks that the interval between two timer shots are within the specified
 * range.
 *
 * @returns 0 if ok, 1 if bad.
 * @param   iShot               The shot number (for bitching).
 * @param   uPrevTS             The time stamp of the previous shot (ns).
 * @param   uThisTS             The timer stamp of this shot (ns).
 * @param   uMin                The minimum interval (ns).
 * @param   uMax                The maximum interval (ns).
 */
static int tstRTR0TimerCheckShotInterval(uint32_t iShot, uint64_t uPrevTS, uint64_t uThisTS, uint32_t uMin, uint32_t uMax)
{
    uint64_t uDelta = uThisTS - uPrevTS;
    RTR0TESTR0_CHECK_MSG_RET(uDelta >= uMin, ("iShot=%u uDelta=%lld uMin=%u\n", iShot, uDelta, uMin), 1);
    RTR0TESTR0_CHECK_MSG_RET(uDelta <= uMax, ("iShot=%u uDelta=%lld uMax=%u\n", iShot, uDelta, uMax), 1);
    return 0;
}
#endif


/**
 * Checks that the interval between timer shots are within a certain range.
 *
 * @returns Number of violations (i.e. 0 is ok).
 * @param   pState              The state.
 * @param   uStartNsTS          The start time stamp (ns).
 * @param   uMin                The minimum interval (ns).
 * @param   uMax                The maximum interval (ns).
 */
static int tstRTR0TimerCheckShotIntervals(PTSTRTR0TIMERS1 pState, uint64_t uStartNsTS, uint32_t uMin, uint32_t uMax)
{
    uint64_t uMaxDelta = 0;
    uint64_t uMinDelta = UINT64_MAX;
    uint32_t cBadShots = 0;
    uint32_t cShots    = pState->cShots;
    uint64_t uPrevTS   = uStartNsTS;
    for (uint32_t iShot = 0; iShot < cShots; iShot++)
    {
        uint64_t uThisTS = pState->aShotNsTSes[iShot];
        uint64_t uDelta  = uThisTS - uPrevTS;
        if (uDelta > uMaxDelta)
            uMaxDelta = uDelta;
        if (uDelta < uMinDelta)
            uMinDelta = uDelta;
        cBadShots += !(uDelta >= uMin && uDelta <= uMax);

        RTR0TESTR0_CHECK_MSG(uDelta >= uMin, ("iShot=%u uDelta=%lld uMin=%u\n", iShot, uDelta, uMin));
        RTR0TESTR0_CHECK_MSG(uDelta <= uMax, ("iShot=%u uDelta=%lld uMax=%u\n", iShot, uDelta, uMax));

        uPrevTS = uThisTS;
    }

    RTR0TestR0Info("uMaxDelta=%llu uMinDelta=%llu\n", uMaxDelta, uMinDelta);
    return cBadShots;
}


/**
 * Service request callback function.
 *
 * @returns VBox status code.
 * @param   pSession    The caller's session.
 * @param   u64Arg      64-bit integer argument.
 * @param   pReqHdr     The request header. Input / Output. Optional.
 */
DECLEXPORT(int) TSTRTR0TimerSrvReqHandler(PSUPDRVSESSION pSession, uint32_t uOperation,
                                          uint64_t u64Arg, PSUPR0SERVICEREQHDR pReqHdr)
{
    RTR0TESTR0_SRV_REQ_PROLOG_RET(pReqHdr);
    NOREF(pSession);

    /*
     * Common parameter and state variables.
     */
    uint32_t const cNsSysHz        = RTTimerGetSystemGranularity();
    uint32_t const cNsMaxHighResHz = 10000; /** @todo need API for this */
    TSTRTR0TIMERS1 State;
    if (   cNsSysHz             < UINT32_C(1000)
        || cNsSysHz             > UINT32_C(1000000000)
        || cNsMaxHighResHz      < UINT32_C(1)
        || cNsMaxHighResHz      > UINT32_C(1000000000))
    {
        RTR0TESTR0_CHECK_MSG(cNsSysHz        > UINT32_C(1000) && cNsSysHz     < UINT32_C(1000000000), ("%u", cNsSysHz));
        RTR0TESTR0_CHECK_MSG(cNsMaxHighResHz > UINT32_C(1) && cNsMaxHighResHz < UINT32_C(1000000000), ("%u", cNsMaxHighResHz));
        RTR0TESTR0_SRV_REQ_EPILOG(pReqHdr);
        return VINF_SUCCESS;
    }

    /*
     * The big switch.
     */
    switch (uOperation)
    {
        RTR0TESTR0_IMPLEMENT_SANITY_CASES();
        RTR0TESTR0_IMPLEMENT_DEFAULT_CASE(uOperation);

        case TSTRTR0TIMER_ONE_SHOT_BASIC:
        case TSTRTR0TIMER_ONE_SHOT_BASIC_HIRES:
        {
            /* Create a one-shot timer and take one shot. */
            PRTTIMER pTimer;
            uint32_t fFlags = TSTRTR0TIMER_IS_HIRES(uOperation) ? RTTIMER_FLAGS_HIGH_RES : 0;
            int rc = RTTimerCreateEx(&pTimer, 0, fFlags, tstRTR0TimerCallbackU32Counter, &State);
            if (rc == VERR_NOT_SUPPORTED)
            {
                RTR0TestR0Info("one-shot timer are not supported, skipping\n");
                RTR0TESTR0_SKIP();
                break;
            }
            RTR0TESTR0_CHECK_RC_BREAK(rc, VINF_SUCCESS);

            do /* break loop */
            {
                RT_ZERO(State); ASMAtomicWriteU32(&State.cShots, State.cShots);
                RTR0TESTR0_CHECK_RC_BREAK(RTTimerStart(pTimer, 0), VINF_SUCCESS);
                for (uint32_t i = 0; i < 1000 && !ASMAtomicUoReadU32(&State.cShots); i++)
                    RTThreadSleep(5);
                RTR0TESTR0_CHECK_MSG_BREAK(ASMAtomicUoReadU32(&State.cShots) == 1, ("cShots=%u\n", State.cShots));

                /* check that it is restartable. */
                RT_ZERO(State); ASMAtomicWriteU32(&State.cShots, State.cShots);
                RTR0TESTR0_CHECK_RC_BREAK(RTTimerStart(pTimer, 0), VINF_SUCCESS);
                for (uint32_t i = 0; i < 1000 && !ASMAtomicUoReadU32(&State.cShots); i++)
                    RTThreadSleep(5);
                RTR0TESTR0_CHECK_MSG_BREAK(ASMAtomicUoReadU32(&State.cShots) == 1, ("cShots=%u\n", State.cShots));

                /* check that it respects the timeout value and can be cancelled. */
                RT_ZERO(State); ASMAtomicWriteU32(&State.cShots, State.cShots);
                RTR0TESTR0_CHECK_RC(RTTimerStart(pTimer, 5*UINT64_C(1000000000)), VINF_SUCCESS);
                RTR0TESTR0_CHECK_RC(RTTimerStop(pTimer), VINF_SUCCESS);
                RTThreadSleep(1);
                RTR0TESTR0_CHECK_MSG_BREAK(ASMAtomicUoReadU32(&State.cShots) == 0, ("cShots=%u\n", State.cShots));

                /* Check some double starts and stops (shall not assert). */
                RT_ZERO(State); ASMAtomicWriteU32(&State.cShots, State.cShots);
                RTR0TESTR0_CHECK_RC(RTTimerStart(pTimer, 5*UINT64_C(1000000000)), VINF_SUCCESS);
                RTR0TESTR0_CHECK_RC(RTTimerStart(pTimer, 0), VERR_TIMER_ACTIVE);
                RTR0TESTR0_CHECK_RC(RTTimerStop(pTimer), VINF_SUCCESS);
                RTR0TESTR0_CHECK_RC(RTTimerStop(pTimer), VERR_TIMER_SUSPENDED);
                RTThreadSleep(1);
                RTR0TESTR0_CHECK_MSG_BREAK(ASMAtomicUoReadU32(&State.cShots) == 0, ("cShots=%u\n", State.cShots));
            } while (0);
            RTR0TESTR0_CHECK_RC(RTTimerDestroy(pTimer), VINF_SUCCESS);
            RTR0TESTR0_CHECK_RC(RTTimerDestroy(NULL), VINF_SUCCESS);
            break;
        }

        case TSTRTR0TIMER_ONE_SHOT_RESTART:
        case TSTRTR0TIMER_ONE_SHOT_RESTART_HIRES:
        {
#if !defined(RT_OS_SOLARIS) /* Not expected to work on all hosts. */
            /* Create a one-shot timer and restart it in the callback handler. */
            PRTTIMER pTimer;
            uint32_t fFlags = TSTRTR0TIMER_IS_HIRES(uOperation) ? RTTIMER_FLAGS_HIGH_RES : 0;
            for (uint32_t iTest = 0; iTest < 2; iTest++)
            {
                int rc = RTTimerCreateEx(&pTimer, 0, fFlags, tstRTR0TimerCallbackRestartOnce, &State);
                if (rc == VERR_NOT_SUPPORTED)
                {
                    RTR0TestR0Info("one-shot timer are not supported, skipping\n");
                    RTR0TESTR0_SKIP();
                    break;
                }
                RTR0TESTR0_CHECK_RC_BREAK(rc, VINF_SUCCESS);

                RT_ZERO(State);
                State.iActionShot = 0;
                ASMAtomicWriteU32(&State.cShots, State.cShots);
                do /* break loop */
                {
                    RTR0TESTR0_CHECK_RC_BREAK(RTTimerStart(pTimer, cNsSysHz * iTest), VINF_SUCCESS);
                    for (uint32_t i = 0; i < 1000 && ASMAtomicUoReadU32(&State.cShots) < 2; i++)
                        RTThreadSleep(5);
                    RTR0TESTR0_CHECK_MSG_BREAK(ASMAtomicUoReadU32(&State.cShots) == 2, ("cShots=%u\n", State.cShots));
                } while (0);
                RTR0TESTR0_CHECK_RC(RTTimerDestroy(pTimer), VINF_SUCCESS);
            }
#else
            RTR0TestR0Info("restarting from callback not supported on this platform\n");
            RTR0TESTR0_SKIP();
#endif
            break;
        }

        case TSTRTR0TIMER_ONE_SHOT_DESTROY:
        case TSTRTR0TIMER_ONE_SHOT_DESTROY_HIRES:
        {
#if !defined(RT_OS_SOLARIS) && !defined(RT_OS_WINDOWS) /* Not expected to work on all hosts. */
            /* Create a one-shot timer and destroy it in the callback handler. */
            PRTTIMER pTimer;
            uint32_t fFlags = TSTRTR0TIMER_IS_HIRES(uOperation) ? RTTIMER_FLAGS_HIGH_RES : 0;
            for (uint32_t iTest = 0; iTest < 2; iTest++)
            {
                int rc = RTTimerCreateEx(&pTimer, 0, fFlags, tstRTR0TimerCallbackDestroyOnce, &State);
                if (rc == VERR_NOT_SUPPORTED)
                {
                    RTR0TestR0Info("one-shot timer are not supported, skipping\n");
                    RTR0TESTR0_SKIP();
                    break;
                }
                RTR0TESTR0_CHECK_RC_BREAK(rc, VINF_SUCCESS);

                RT_ZERO(State);
                State.rc = VERR_IPE_UNINITIALIZED_STATUS;
                State.iActionShot = 0;
                ASMAtomicWriteU32(&State.cShots, State.cShots);
                do /* break loop */
                {
                    RTR0TESTR0_CHECK_RC_BREAK(RTTimerStart(pTimer, cNsSysHz * iTest), VINF_SUCCESS);
                    for (uint32_t i = 0; i < 1000 && (ASMAtomicUoReadU32(&State.cShots) < 1 || State.rc == VERR_IPE_UNINITIALIZED_STATUS); i++)
                        RTThreadSleep(5);
                    RTR0TESTR0_CHECK_MSG_BREAK(ASMAtomicReadU32(&State.cShots) == 1, ("cShots=%u\n", State.cShots));
                    RTR0TESTR0_CHECK_MSG_BREAK(State.rc == VINF_SUCCESS, ("rc=%Rrc\n", State.rc));
                } while (0);
                if (RT_FAILURE(State.rc))
                    RTR0TESTR0_CHECK_RC(RTTimerDestroy(pTimer), VINF_SUCCESS);
            }
#else
            RTR0TestR0Info("destroying from callback not supported on this platform\n");
            RTR0TESTR0_SKIP();
#endif
            break;
        }

        case TSTRTR0TIMER_ONE_SHOT_SPECIFIC:
        case TSTRTR0TIMER_ONE_SHOT_SPECIFIC_HIRES:
        {
            PRTTIMER pTimer = NULL;
            RTCPUSET OnlineSet;
            RTMpGetOnlineSet(&OnlineSet);
            for (uint32_t iCpu = 0; iCpu < RTCPUSET_MAX_CPUS; iCpu++)
                if (RTCpuSetIsMemberByIndex(&OnlineSet, iCpu))
                {
                    RT_ZERO(State);
                    State.iActionShot       = 0;
                    State.rc                = VINF_SUCCESS;
                    State.u.Specific.idCpu  = RTMpCpuIdFromSetIndex(iCpu);
                    ASMAtomicWriteU32(&State.cShots, State.cShots);

                    uint32_t fFlags = TSTRTR0TIMER_IS_HIRES(uOperation) ? RTTIMER_FLAGS_HIGH_RES : 0;
                    fFlags |= RTTIMER_FLAGS_CPU(iCpu);
                    int rc = RTTimerCreateEx(&pTimer, 0, fFlags, tstRTR0TimerCallbackSpecific, &State);
                    if (rc == VERR_NOT_SUPPORTED)
                    {
                        RTR0TestR0Info("one-shot specific timer are not supported, skipping\n");
                        RTR0TESTR0_SKIP();
                        break;
                    }
                    RTR0TESTR0_CHECK_RC_BREAK(rc, VINF_SUCCESS);

                    for (uint32_t i = 0; i < 5 && !RTR0TestR0HaveErrors(); i++)
                    {
                        ASMAtomicWriteU32(&State.cShots, 0);
                        RTR0TESTR0_CHECK_RC_BREAK(RTTimerStart(pTimer, (i & 2 ? cNsSysHz : cNsSysHz / 2) * (i & 1)), VINF_SUCCESS);
                        uint64_t cNsElapsed = RTTimeSystemNanoTS();
                        for (uint32_t j = 0; j < 1000 && ASMAtomicUoReadU32(&State.cShots) < 1; j++)
                            RTThreadSleep(5);
                        cNsElapsed = RTTimeSystemNanoTS() - cNsElapsed;
                        RTR0TESTR0_CHECK_MSG_BREAK(ASMAtomicReadU32(&State.cShots) == 1,
                                                   ("cShots=%u iCpu=%u i=%u iCurCpu=%u cNsElapsed=%'llu\n",
                                                    State.cShots, iCpu, i, RTMpCpuIdToSetIndex(RTMpCpuId()), cNsElapsed ));
                        RTR0TESTR0_CHECK_MSG_BREAK(State.rc == VINF_SUCCESS, ("rc=%Rrc\n", State.rc));
                        RTR0TESTR0_CHECK_MSG_BREAK(!State.u.Specific.fFailed, ("iCpu=%u i=%u\n", iCpu, i));
                    }

                    RTR0TESTR0_CHECK_RC(RTTimerDestroy(pTimer), VINF_SUCCESS);
                    pTimer = NULL;
                    if (RTR0TestR0HaveErrors())
                        break;

                    RTMpGetOnlineSet(&OnlineSet);
                }
            RTR0TESTR0_CHECK_RC(RTTimerDestroy(pTimer), VINF_SUCCESS);
            break;
        }

        case TSTRTR0TIMER_ONE_SHOT_RESOLUTION:
        case TSTRTR0TIMER_ONE_SHOT_RESOLUTION_HIRES:
        {
            /* Just create a timer and do a number of RTTimerStart with a small
               interval and see how quickly it gets called. */
            PRTTIMER  pTimer;
            uint32_t  fFlags = TSTRTR0TIMER_IS_HIRES(uOperation) ? RTTIMER_FLAGS_HIGH_RES : 0;
            uint64_t  volatile cNsElapsed = 0;
            RTR0TESTR0_CHECK_RC_BREAK(RTTimerCreateEx(&pTimer, 0, fFlags, tstRTR0TimerCallbackOneShotElapsed, (void *)&cNsElapsed),
                                      VINF_SUCCESS);

            uint32_t cTotal   = 0;
            uint32_t cNsTotal = 0;
            uint32_t cNsMin   = UINT32_MAX;
            uint32_t cNsMax   = 0;
            for (uint32_t i = 0; i < 200; i++)
            {
                cNsElapsed = RTTimeSystemNanoTS();
                RTR0TESTR0_CHECK_RC_BREAK(RTTimerStart(pTimer, RT_NS_1US), VINF_SUCCESS);
                RTThreadSleep(10);
                cTotal   += 1;
                cNsTotal += cNsElapsed;
                if (cNsMin > cNsElapsed)
                    cNsMin = cNsElapsed;
                if (cNsMax < cNsElapsed)
                    cNsMax = cNsElapsed;
            }
            RTR0TESTR0_CHECK_RC(RTTimerDestroy(pTimer), VINF_SUCCESS);
            pTimer = NULL;
            RTR0TestR0Info("nsMin=%u nsAvg=%u nsMax=%u cTotal=%u\n", cNsMin, cNsTotal / cTotal, cNsMax, cTotal);
            break;
        }

        case TSTRTR0TIMER_PERIODIC_BASIC:
        case TSTRTR0TIMER_PERIODIC_BASIC_HIRES:
        {
            /* Create a periodic timer running at 10 HZ. */
            uint32_t const  u10HzAsNs    = RT_NS_1SEC / 10;
            uint32_t const  u10HzAsNsMin = u10HzAsNs - u10HzAsNs / 2;
            uint32_t const  u10HzAsNsMax = u10HzAsNs + u10HzAsNs / 2;
            PRTTIMER        pTimer;
            uint32_t        fFlags = TSTRTR0TIMER_IS_HIRES(uOperation) ? RTTIMER_FLAGS_HIGH_RES : 0;
            RTR0TESTR0_CHECK_RC_BREAK(RTTimerCreateEx(&pTimer, u10HzAsNs, fFlags, tstRTR0TimerCallbackU32Counter, &State),
                                      VINF_SUCCESS);

            for (uint32_t iTest = 0; iTest < 2; iTest++)
            {
                RT_ZERO(State);
                State.fPeriodic = true;
                ASMAtomicWriteU32(&State.cShots, State.cShots);

                uint64_t uStartNsTS = RTTimeSystemNanoTS();
                RTR0TESTR0_CHECK_RC_BREAK(RTTimerStart(pTimer, u10HzAsNs), VINF_SUCCESS);
                for (uint32_t i = 0; i < 1000 && ASMAtomicUoReadU32(&State.cShots) < 10; i++)
                    RTThreadSleep(10);
                RTR0TESTR0_CHECK_RC_BREAK(RTTimerStop(pTimer), VINF_SUCCESS);
                RTR0TESTR0_CHECK_MSG_BREAK(ASMAtomicUoReadU32(&State.cShots) == 10, ("cShots=%u\n", State.cShots));
                if (tstRTR0TimerCheckShotIntervals(&State, uStartNsTS, u10HzAsNsMin, u10HzAsNsMax))
                    break;
                RTThreadSleep(1); /** @todo RTTimerStop doesn't currently make sure the timer callback not is running
                                   *        before returning on windows, linux (low res) and possible other plaforms. */
            }
            RTR0TESTR0_CHECK_RC(RTTimerDestroy(pTimer), VINF_SUCCESS);
            RTR0TESTR0_CHECK_RC(RTTimerDestroy(NULL), VINF_SUCCESS);
            break;
        }

        case TSTRTR0TIMER_PERIODIC_CSSD_LOOPS:
        case TSTRTR0TIMER_PERIODIC_CSSD_LOOPS_HIRES:
        {
            /* create, start, stop & destroy high res timers a number of times. */
            uint32_t fFlags = TSTRTR0TIMER_IS_HIRES(uOperation) ? RTTIMER_FLAGS_HIGH_RES : 0;
            for (uint32_t i = 0; i < 40; i++)
            {
                PRTTIMER pTimer;
                RTR0TESTR0_CHECK_RC_BREAK(RTTimerCreateEx(&pTimer, cNsSysHz, fFlags, tstRTR0TimerCallbackU32Counter, &State),
                                          VINF_SUCCESS);
                for (uint32_t j = 0; j < 10; j++)
                {
                    RT_ZERO(State);
                    State.fPeriodic = true;
                    ASMAtomicWriteU32(&State.cShots, State.cShots); /* ordered, necessary? */

                    RTR0TESTR0_CHECK_RC_BREAK(RTTimerStart(pTimer, i < 20 ? 0 : cNsSysHz), VINF_SUCCESS);
                    for (uint32_t k = 0; k < 1000 && ASMAtomicUoReadU32(&State.cShots) < 2; k++)
                        RTThreadSleep(1);
                    RTR0TESTR0_CHECK_RC_BREAK(RTTimerStop(pTimer), VINF_SUCCESS);
                    RTThreadSleep(1); /** @todo RTTimerStop doesn't currently make sure the timer callback not is running
                                       *        before returning on windows, linux (low res) and possible other plaforms. */
                }
                RTR0TESTR0_CHECK_RC(RTTimerDestroy(pTimer), VINF_SUCCESS);
            }
            break;
        }

        case TSTRTR0TIMER_PERIODIC_CHANGE_INTERVAL:
        case TSTRTR0TIMER_PERIODIC_CHANGE_INTERVAL_HIRES:
        {
            /* Initialize the test parameters, using the u64Arg value for selecting variations.  */
            RT_ZERO(State);
            State.cShots        = 0;
            State.rc            = VERR_IPE_UNINITIALIZED_STATUS;
            State.iActionShot   = 42;
            State.fPeriodic     = true;
            State.u.ChgInt.fDirection     = !!(u64Arg & 1);
            if (uOperation == TSTRTR0TIMER_PERIODIC_CHANGE_INTERVAL_HIRES)
            {
                State.u.ChgInt.cNsMaxInterval = RT_MAX(cNsMaxHighResHz * 10, 20000000); /* 10x / 20 ms */
                State.u.ChgInt.cNsMinInterval = RT_MAX(cNsMaxHighResHz,         10000); /* min / 10 us */
            }
            else
            {
                State.u.ChgInt.cNsMaxInterval = cNsSysHz * 4;
                State.u.ChgInt.cNsMinInterval = cNsSysHz;
            }
            State.u.ChgInt.cNsChangeStep  = (State.u.ChgInt.cNsMaxInterval - State.u.ChgInt.cNsMinInterval) / 10;
            State.u.ChgInt.cNsCurInterval = State.u.ChgInt.fDirection
                                          ? State.u.ChgInt.cNsMaxInterval : State.u.ChgInt.cNsMinInterval;
            State.u.ChgInt.cStepsBetween  = u64Arg & 4 ? 1 : 3;
            RTR0TESTR0_CHECK_MSG_BREAK(State.u.ChgInt.cNsMinInterval > 1000, ("%u\n", State.u.ChgInt.cNsMinInterval));
            RTR0TESTR0_CHECK_MSG_BREAK(State.u.ChgInt.cNsMaxInterval > State.u.ChgInt.cNsMinInterval, ("max=%u min=%u\n", State.u.ChgInt.cNsMaxInterval, State.u.ChgInt.cNsMinInterval));
            ASMAtomicWriteU32(&State.cShots, State.cShots);

            /* create the timer and check if RTTimerChangeInterval is supported. */
            PRTTIMER pTimer;
            uint32_t fFlags = TSTRTR0TIMER_IS_HIRES(uOperation) ? RTTIMER_FLAGS_HIGH_RES : 0;
            RTR0TESTR0_CHECK_RC_BREAK(RTTimerCreateEx(&pTimer, cNsSysHz, fFlags, tstRTR0TimerCallbackChangeInterval, &State),
                                      VINF_SUCCESS);
            int rc = RTTimerChangeInterval(pTimer, State.u.ChgInt.cNsMinInterval);
            if (rc == VERR_NOT_SUPPORTED)
            {
                RTR0TestR0Info("RTTimerChangeInterval not supported, skipped");
                RTR0TESTR0_CHECK_RC(RTTimerDestroy(pTimer), VINF_SUCCESS);
                RTR0TESTR0_SKIP();
                break;
            }

            /* do the test. */
            RTR0TESTR0_CHECK_RC_BREAK(RTTimerStart(pTimer, u64Arg & 2 ? State.u.ChgInt.cNsCurInterval : 0), VINF_SUCCESS);
            for (uint32_t k = 0;
                    k < 1000
                 && ASMAtomicReadU32(&State.cShots) <= State.iActionShot
                 && State.rc == VERR_IPE_UNINITIALIZED_STATUS;
                 k++)
                RTThreadSleep(10);

            rc = RTTimerStop(pTimer);
            RTR0TESTR0_CHECK_MSG_BREAK(rc == VERR_TIMER_SUSPENDED || rc == VINF_SUCCESS, ("rc = %Rrc (RTTimerStop)\n", rc));
            RTR0TESTR0_CHECK_RC(RTTimerDestroy(pTimer), VINF_SUCCESS);
            break;
        }

        case TSTRTR0TIMER_PERIODIC_SPECIFIC:
        case TSTRTR0TIMER_PERIODIC_SPECIFIC_HIRES:
        {
            PRTTIMER pTimer = NULL;
            RTCPUSET OnlineSet;
            RTMpGetOnlineSet(&OnlineSet);
            for (uint32_t iCpu = 0; iCpu < RTCPUSET_MAX_CPUS; iCpu++)
                if (RTCpuSetIsMemberByIndex(&OnlineSet, iCpu))
                {
                    RT_ZERO(State);
                    State.iActionShot       = 0;
                    State.rc                = VINF_SUCCESS;
                    State.fPeriodic         = true;
                    State.u.Specific.idCpu  = RTMpCpuIdFromSetIndex(iCpu);
                    ASMAtomicWriteU32(&State.cShots, State.cShots);

                    uint32_t fFlags = TSTRTR0TIMER_IS_HIRES(uOperation) ? RTTIMER_FLAGS_HIGH_RES : 0;
                    fFlags |= RTTIMER_FLAGS_CPU(iCpu);
                    int rc = RTTimerCreateEx(&pTimer, cNsSysHz, fFlags, tstRTR0TimerCallbackSpecific, &State);
                    if (rc == VERR_NOT_SUPPORTED)
                    {
                        RTR0TestR0Info("specific timer are not supported, skipping\n");
                        RTR0TESTR0_SKIP();
                        break;
                    }
                    RTR0TESTR0_CHECK_RC_BREAK(rc, VINF_SUCCESS);

                    for (uint32_t i = 0; i < 3 && !RTR0TestR0HaveErrors(); i++)
                    {
                        ASMAtomicWriteU32(&State.cShots, 0);
                        RTR0TESTR0_CHECK_RC_BREAK(RTTimerStart(pTimer, (i & 2 ? cNsSysHz : cNsSysHz / 2) * (i & 1)), VINF_SUCCESS);
                        uint64_t cNsElapsed = RTTimeSystemNanoTS();
                        for (uint32_t j = 0; j < 1000 && ASMAtomicUoReadU32(&State.cShots) < 8; j++)
                            RTThreadSleep(5);
                        cNsElapsed = RTTimeSystemNanoTS() - cNsElapsed;
                        RTR0TESTR0_CHECK_RC_BREAK(RTTimerStop(pTimer), VINF_SUCCESS);
                        RTR0TESTR0_CHECK_MSG_BREAK(ASMAtomicReadU32(&State.cShots) > 5,
                                                   ("cShots=%u iCpu=%u i=%u iCurCpu=%u cNsElapsed=%'llu\n",
                                                    State.cShots, iCpu, i, RTMpCpuIdToSetIndex(RTMpCpuId()), cNsElapsed));
                        RTThreadSleep(1); /** @todo RTTimerStop doesn't currently make sure the timer callback not is running
                                           *        before returning on windows, linux (low res) and possible other plaforms. */
                        RTR0TESTR0_CHECK_MSG_BREAK(State.rc == VINF_SUCCESS, ("rc=%Rrc\n", State.rc));
                        RTR0TESTR0_CHECK_MSG_BREAK(!State.u.Specific.fFailed, ("iCpu=%u i=%u\n", iCpu, i));
                    }

                    RTR0TESTR0_CHECK_RC(RTTimerDestroy(pTimer), VINF_SUCCESS);
                    pTimer = NULL;
                    if (RTR0TestR0HaveErrors())
                        break;

                    RTMpGetOnlineSet(&OnlineSet);
                }
            RTR0TESTR0_CHECK_RC(RTTimerDestroy(pTimer), VINF_SUCCESS);
            break;
        }

        case TSTRTR0TIMER_PERIODIC_OMNI:
        case TSTRTR0TIMER_PERIODIC_OMNI_HIRES:
        {
            /* Create a periodic timer running at max host frequency, but no more than 1000 Hz. */
            uint32_t        cNsInterval = cNsSysHz;
            while (cNsInterval < UINT32_C(1000000))
                cNsInterval *= 2;
            PTSTRTR0TIMEROMNI1 paStates = (PTSTRTR0TIMEROMNI1)RTMemAllocZ(sizeof(paStates[0]) * RTCPUSET_MAX_CPUS);
            RTR0TESTR0_CHECK_MSG_BREAK(paStates, ("%d\n", RTCPUSET_MAX_CPUS));

            PRTTIMER        pTimer;
            uint32_t        fFlags = (TSTRTR0TIMER_IS_HIRES(uOperation) ? RTTIMER_FLAGS_HIGH_RES : 0)
                                   | RTTIMER_FLAGS_CPU_ALL;
            int             rc = RTTimerCreateEx(&pTimer, cNsInterval, fFlags, tstRTR0TimerCallbackOmni, paStates);
            if (rc == VERR_NOT_SUPPORTED)
            {
                RTR0TESTR0_SKIP_BREAK();
            }
            RTR0TESTR0_CHECK_RC_BREAK(rc, VINF_SUCCESS);

            for (uint32_t iTest = 0; iTest < 3 && !RTR0TestR0HaveErrors(); iTest++)
            {
                /* reset the state */
                for (uint32_t iCpu = 0; iCpu < RTCPUSET_MAX_CPUS; iCpu++)
                {
                    paStates[iCpu].u64Start = 0;
                    paStates[iCpu].u64Last  = 0;
                    ASMAtomicWriteU32(&paStates[iCpu].cTicks, 0);
                }

                /* run it for 5 seconds. */
                RTCPUSET OnlineSet;
                uint64_t uStartNsTS = RTTimeSystemNanoTS();
                RTR0TESTR0_CHECK_RC_BREAK(RTTimerStart(pTimer, 0), VINF_SUCCESS);
                RTMpGetOnlineSet(&OnlineSet);

                for (uint32_t i = 0; i < 5000 && RTTimeSystemNanoTS() - uStartNsTS <= UINT64_C(5000000000); i++)
                    RTThreadSleep(2);

                RTR0TESTR0_CHECK_RC_BREAK(RTTimerStop(pTimer), VINF_SUCCESS);
                uint64_t    cNsElapsedX = RTTimeNanoTS() - uStartNsTS;

                /* Do a min/max on the start and stop times and calculate the test period. */
                uint64_t    u64MinStart = UINT64_MAX;
                uint64_t    u64MaxStop  = 0;
                for (uint32_t iCpu = 0; iCpu < RTCPUSET_MAX_CPUS; iCpu++)
                {
                    if (paStates[iCpu].u64Start)
                    {
                        if (paStates[iCpu].u64Start < u64MinStart)
                            u64MinStart = paStates[iCpu].u64Start;
                        if (paStates[iCpu].u64Last  > u64MaxStop)
                            u64MaxStop  = paStates[iCpu].u64Last;
                    }
                }
                RTR0TESTR0_CHECK_MSG(u64MinStart < u64MaxStop, ("%llu, %llu", u64MinStart, u64MaxStop));
                uint64_t cNsElapsed = u64MaxStop - u64MinStart;
                RTR0TESTR0_CHECK_MSG(cNsElapsed <= cNsElapsedX + 100000, ("%llu, %llu", cNsElapsed, cNsElapsedX)); /* the fudge factor is time drift */
                uint32_t cAvgTicks  = cNsElapsed / cNsInterval + 1;

                /* Check tick counts. ASSUMES no cpu on- or offlining.
                   This only catches really bad stuff. */
                uint32_t cMargin = TSTRTR0TIMER_IS_HIRES(uOperation) ? 10 : 5; /* Allow a wider deviation for the non hires timers. */
                uint32_t cMinTicks = cAvgTicks - cAvgTicks / cMargin;
                uint32_t cMaxTicks = cAvgTicks + cAvgTicks / cMargin + 1;
                for (uint32_t iCpu = 0; iCpu < RTCPUSET_MAX_CPUS; iCpu++)
                    if (paStates[iCpu].cTicks)
                    {
                        RTR0TESTR0_CHECK_MSG(RTCpuSetIsMemberByIndex(&OnlineSet, iCpu), ("%d\n", iCpu));
                        RTR0TESTR0_CHECK_MSG(paStates[iCpu].cTicks <= cMaxTicks && paStates[iCpu].cTicks >= cMinTicks,
                                             ("min=%u, ticks=%u, avg=%u max=%u, iCpu=%u, iCpuCurr=%u, interval=%'u, elapsed=%'llu/%'llu\n",
                                              cMinTicks, paStates[iCpu].cTicks, cAvgTicks, cMaxTicks, iCpu,
                                              RTMpCpuIdToSetIndex(RTMpCpuId()),
                                              cNsInterval, cNsElapsed, cNsElapsedX));
                    }
                    else
                        RTR0TESTR0_CHECK_MSG(!RTCpuSetIsMemberByIndex(&OnlineSet, iCpu), ("%d\n", iCpu));
            }

            RTR0TESTR0_CHECK_RC(RTTimerDestroy(pTimer), VINF_SUCCESS);
            RTMemFree(paStates);
            break;
        }


        case TSTRTR0TIMER_LATENCY_OMNI:
        case TSTRTR0TIMER_LATENCY_OMNI_HIRES:
        {
            /*
             * Create a periodic timer running at max host frequency, but no more than 1000 Hz.
             * Unless it's a high resolution timer, which we try at double the rate.
             * Windows seems to limit the highres stuff to around 500-600 us interval.
             */
            PRTTIMER        pTimer;
            uint32_t        fFlags = (TSTRTR0TIMER_IS_HIRES(uOperation) ? RTTIMER_FLAGS_HIGH_RES : 0)
                                   | RTTIMER_FLAGS_CPU_ALL;
            uint32_t const  cNsMinInterval = TSTRTR0TIMER_IS_HIRES(uOperation) ? cNsMaxHighResHz : RT_NS_1MS;
            uint32_t        cNsInterval    = TSTRTR0TIMER_IS_HIRES(uOperation) ? cNsSysHz / 2 : cNsSysHz;
            while (cNsInterval < cNsMinInterval)
                cNsInterval *= 2;
            int rc = RTTimerCreateEx(&pTimer, cNsInterval, fFlags, tstRTR0TimerCallbackLatencyOmni, NULL);
            if (rc == VERR_NOT_SUPPORTED)
            {
                RTR0TESTR0_SKIP_BREAK();
            }
            RTR0TESTR0_CHECK_RC_BREAK(rc, VINF_SUCCESS);

            /*
             * Reset the state and run the test for 4 seconds.
             */
            RT_ZERO(g_aOmniLatency);

            RTCPUSET OnlineSet;
            uint64_t uStartNsTS = RTTimeSystemNanoTS();
            RTR0TESTR0_CHECK_RC_BREAK(RTTimerStart(pTimer, 0), VINF_SUCCESS);
            RTMpGetOnlineSet(&OnlineSet);

            for (uint32_t i = 0; i < 5000 && RTTimeSystemNanoTS() - uStartNsTS <= UINT64_C(4000000000); i++)
                RTThreadSleep(2);

            RTR0TESTR0_CHECK_RC_BREAK(RTTimerStop(pTimer), VINF_SUCCESS);

            /*
             * Process the result.
             */
            int32_t     cNsLow  = cNsInterval / 4 * 3; /* 75% */
            int32_t     cNsHigh = cNsInterval / 4 * 5; /* 125% */
            uint32_t    cTotal  = 0;
            uint32_t    cLow    = 0;
            uint32_t    cHigh   = 0;
            for (uint32_t iCpu = 0; iCpu < RT_ELEMENTS(g_aOmniLatency); iCpu++)
            {
                uint32_t cSamples = g_aOmniLatency[iCpu].cSamples;
                if (cSamples > 1)
                {
                    cTotal += cSamples - 1;
                    for (uint32_t iSample = 1; iSample < cSamples; iSample++)
                    {
                        int64_t cNsDelta = g_aOmniLatency[iCpu].aSamples[iSample].uNanoTs
                                         - g_aOmniLatency[iCpu].aSamples[iSample - 1].uNanoTs;
                        if (cNsDelta < cNsLow)
                            cLow++;
                        else if (cNsDelta > cNsHigh)
                            cHigh++;
                    }
                }
            }
            RTR0TestR0Info("125%%: %u; 75%%: %u; total: %u", cHigh, cLow, cTotal);
            RTR0TESTR0_CHECK_RC(RTTimerDestroy(pTimer), VINF_SUCCESS);
#if 1
            RTR0TestR0Info("cNsSysHz=%u cNsInterval=%RU32 cNsLow=%d cNsHigh=%d", cNsSysHz, cNsInterval, cNsLow, cNsHigh);
            if (TSTRTR0TIMER_IS_HIRES(uOperation))
                RTR0TestR0Info("RTTimerCanDoHighResolution -> %d", RTTimerCanDoHighResolution());
            for (uint32_t iSample = 1; iSample < 6; iSample++)
                RTR0TestR0Info("%RU64/%#RU64",
                                g_aOmniLatency[0].aSamples[iSample].uNanoTs - g_aOmniLatency[0].aSamples[iSample - 1].uNanoTs,
                                g_aOmniLatency[0].aSamples[iSample].uTsc - g_aOmniLatency[0].aSamples[iSample - 1].uTsc);
#endif
            break;
        }

    }

    RTR0TESTR0_SRV_REQ_EPILOG(pReqHdr);
    /* The error indicator is the '!' in the message buffer. */
    return VINF_SUCCESS;
}

