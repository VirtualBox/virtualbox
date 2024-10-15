/* $Id: TMAll.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * TM - Timeout Manager, all contexts.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_TM
#ifdef DEBUG_bird
# define DBGFTRACE_DISABLED /* annoying */
#endif
#include <VBox/vmm/tm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/dbgftrace.h>
#ifdef IN_RING3
#endif
#include <VBox/vmm/pdmdev.h> /* (for TMTIMER_GET_CRITSECT implementation) */
#include "TMInternal.h"
#include <VBox/vmm/vmcc.h>

#include <VBox/param.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/sup.h>
#include <iprt/time.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/asm-math.h>
#include <iprt/string.h>
#ifdef IN_RING3
# include <iprt/thread.h>
#endif

#include "TMInline.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifdef VBOX_STRICT
/** @def TMTIMER_GET_CRITSECT
 * Helper for safely resolving the critical section for a timer belonging to a
 * device instance.
 * @todo needs reworking later as it uses PDMDEVINSR0::pDevInsR0RemoveMe.  */
# ifdef IN_RING3
#  define TMTIMER_GET_CRITSECT(a_pVM, a_pTimer) ((a_pTimer)->pCritSect)
# else
#  define TMTIMER_GET_CRITSECT(a_pVM, a_pTimer) tmRZTimerGetCritSect(a_pVM, a_pTimer)
# endif
#endif

/** @def TMTIMER_ASSERT_CRITSECT
 * Checks that the caller owns the critical section if one is associated with
 * the timer. */
#ifdef VBOX_STRICT
# define TMTIMER_ASSERT_CRITSECT(a_pVM, a_pTimer) \
    do { \
        if ((a_pTimer)->pCritSect) \
        { \
            VMSTATE      enmState; \
            PPDMCRITSECT pCritSect = TMTIMER_GET_CRITSECT(a_pVM, a_pTimer); \
            AssertMsg(   pCritSect \
                      && (   PDMCritSectIsOwner((a_pVM), pCritSect) \
                          || (enmState = (a_pVM)->enmVMState) == VMSTATE_CREATING \
                          || enmState == VMSTATE_RESETTING \
                          || enmState == VMSTATE_RESETTING_LS ),\
                      ("pTimer=%p (%s) pCritSect=%p (%s)\n", a_pTimer, (a_pTimer)->szName, \
                       (a_pTimer)->pCritSect, R3STRING(PDMR3CritSectName((a_pTimer)->pCritSect)) )); \
        } \
    } while (0)
#else
# define TMTIMER_ASSERT_CRITSECT(pVM, pTimer) do { } while (0)
#endif

/** @def TMTIMER_ASSERT_SYNC_CRITSECT_ORDER
 * Checks for lock order trouble between the timer critsect and the critical
 * section critsect.  The virtual sync critsect must always be entered before
 * the one associated with the timer (see TMR3TimerQueuesDo).  It is OK if there
 * isn't any critical section associated with the timer or if the calling thread
 * doesn't own it, ASSUMING of course that the thread using this macro is going
 * to enter the virtual sync critical section anyway.
 *
 * @remarks This is a sligtly relaxed timer locking attitude compared to
 *          TMTIMER_ASSERT_CRITSECT, however, the calling device/whatever code
 *          should know what it's doing if it's stopping or starting a timer
 *          without taking the device lock.
 */
#ifdef VBOX_STRICT
# define TMTIMER_ASSERT_SYNC_CRITSECT_ORDER(pVM, pTimer) \
    do { \
        if ((pTimer)->pCritSect) \
        { \
            VMSTATE      enmState; \
            PPDMCRITSECT pCritSect = TMTIMER_GET_CRITSECT(pVM, pTimer); \
            AssertMsg(   pCritSect \
                      && (   !PDMCritSectIsOwner((pVM), pCritSect) \
                          || PDMCritSectIsOwner((pVM), &(pVM)->tm.s.VirtualSyncLock) \
                          || (enmState = (pVM)->enmVMState) == VMSTATE_CREATING \
                          || enmState == VMSTATE_RESETTING \
                          || enmState == VMSTATE_RESETTING_LS ),\
                      ("pTimer=%p (%s) pCritSect=%p (%s)\n", pTimer, pTimer->szName, \
                       (pTimer)->pCritSect, R3STRING(PDMR3CritSectName((pTimer)->pCritSect)) )); \
        } \
    } while (0)
#else
# define TMTIMER_ASSERT_SYNC_CRITSECT_ORDER(pVM, pTimer) do { } while (0)
#endif


#if defined(VBOX_STRICT) && defined(IN_RING0)
/**
 * Helper for  TMTIMER_GET_CRITSECT
 * @todo This needs a redo!
 */
DECLINLINE(PPDMCRITSECT) tmRZTimerGetCritSect(PVMCC pVM, PTMTIMER pTimer)
{
    if (pTimer->enmType == TMTIMERTYPE_DEV)
    {
        RTCCUINTREG  fSavedFlags = ASMAddFlags(X86_EFL_AC); /** @todo fix ring-3 pointer use */
        PPDMDEVINSR0 pDevInsR0   = ((struct PDMDEVINSR3 *)pTimer->u.Dev.pDevIns)->pDevInsR0RemoveMe; /* !ring-3 read! */
        ASMSetFlags(fSavedFlags);
        struct PDMDEVINSR3 *pDevInsR3 = pDevInsR0->pDevInsForR3R0;
        if (pTimer->pCritSect == pDevInsR3->pCritSectRoR3)
            return pDevInsR0->pCritSectRoR0;
        uintptr_t offCritSect = (uintptr_t)pTimer->pCritSect - (uintptr_t)pDevInsR3->pvInstanceDataR3;
        if (offCritSect < pDevInsR0->pReg->cbInstanceShared)
            return (PPDMCRITSECT)((uintptr_t)pDevInsR0->pvInstanceDataR0 + offCritSect);
    }
    RT_NOREF(pVM);
    Assert(pTimer->pCritSect == NULL);
    return NULL;
}
#endif /* VBOX_STRICT && IN_RING0 */


/**
 * Notification that execution is about to start.
 *
 * This call must always be paired with a TMNotifyEndOfExecution call.
 *
 * The function may, depending on the configuration, resume the TSC and future
 * clocks that only ticks when we're executing guest code.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMDECL(void) TMNotifyStartOfExecution(PVMCC pVM, PVMCPUCC pVCpu)
{
#ifndef VBOX_WITHOUT_NS_ACCOUNTING
    pVCpu->tm.s.uTscStartExecuting = SUPReadTsc();
    pVCpu->tm.s.fExecuting         = true;
#endif
    if (pVM->tm.s.fTSCTiedToExecution)
        tmCpuTickResume(pVM, pVCpu);
}


/**
 * Notification that execution has ended.
 *
 * This call must always be paired with a TMNotifyStartOfExecution call.
 *
 * The function may, depending on the configuration, suspend the TSC and future
 * clocks that only ticks when we're executing guest code.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   uTsc        TSC value when exiting guest context.
 */
VMMDECL(void) TMNotifyEndOfExecution(PVMCC pVM, PVMCPUCC pVCpu, uint64_t uTsc)
{
    if (pVM->tm.s.fTSCTiedToExecution)
        tmCpuTickPause(pVCpu); /** @todo use uTsc here if we can. */

#ifndef VBOX_WITHOUT_NS_ACCOUNTING
    /*
     * Calculate the elapsed tick count and convert it to nanoseconds.
     */
# ifdef IN_RING3
    PSUPGLOBALINFOPAGE const pGip = g_pSUPGlobalInfoPage;
    uint64_t       cTicks = uTsc - pVCpu->tm.s.uTscStartExecuting - SUPGetTscDelta(pGip);
    uint64_t const uCpuHz = pGip ? SUPGetCpuHzFromGip(pGip) : pVM->tm.s.cTSCTicksPerSecondHost;
# else
    uint64_t       cTicks = uTsc - pVCpu->tm.s.uTscStartExecuting - SUPGetTscDeltaByCpuSetIndex(pVCpu->iHostCpuSet);
    uint64_t const uCpuHz = SUPGetCpuHzFromGipBySetIndex(g_pSUPGlobalInfoPage, pVCpu->iHostCpuSet);
# endif
    AssertStmt(cTicks <= uCpuHz << 2, cTicks = uCpuHz << 2); /* max 4 sec */

    uint64_t cNsExecutingDelta;
    if (uCpuHz < _4G)
        cNsExecutingDelta = ASMMultU64ByU32DivByU32(cTicks, RT_NS_1SEC, uCpuHz);
    else if (uCpuHz < 16*_1G64)
        cNsExecutingDelta = ASMMultU64ByU32DivByU32(cTicks >> 2, RT_NS_1SEC, uCpuHz >> 2);
    else
    {
        Assert(uCpuHz < 64 * _1G64);
        cNsExecutingDelta = ASMMultU64ByU32DivByU32(cTicks >> 4, RT_NS_1SEC, uCpuHz >> 4);
    }

    /*
     * Update the data.
     *
     * Note! We're not using strict memory ordering here to speed things us.
     *       The data is in a single cache line and this thread is the only
     *       one writing to that line, so I cannot quite imagine why we would
     *       need any strict ordering here.
     */
    uint64_t const cNsExecutingNew = pVCpu->tm.s.cNsExecuting + cNsExecutingDelta;
    uint32_t uGen = ASMAtomicUoIncU32(&pVCpu->tm.s.uTimesGen); Assert(uGen & 1);
    ASMCompilerBarrier();
    pVCpu->tm.s.fExecuting   = false;
    pVCpu->tm.s.cNsExecuting = cNsExecutingNew;
    pVCpu->tm.s.cPeriodsExecuting++;
    ASMCompilerBarrier();
    ASMAtomicUoWriteU32(&pVCpu->tm.s.uTimesGen, (uGen | 1) + 1);

    /*
     * Update stats.
     */
# if defined(VBOX_WITH_STATISTICS) || defined(VBOX_WITH_NS_ACCOUNTING_STATS)
    STAM_REL_PROFILE_ADD_PERIOD(&pVCpu->tm.s.StatNsExecuting, cNsExecutingDelta);
    if (cNsExecutingDelta < 5000)
        STAM_REL_PROFILE_ADD_PERIOD(&pVCpu->tm.s.StatNsExecTiny, cNsExecutingDelta);
    else if (cNsExecutingDelta < 50000)
        STAM_REL_PROFILE_ADD_PERIOD(&pVCpu->tm.s.StatNsExecShort, cNsExecutingDelta);
    else
        STAM_REL_PROFILE_ADD_PERIOD(&pVCpu->tm.s.StatNsExecLong, cNsExecutingDelta);
# endif

    /* The timer triggers occational updating of the others and total stats: */
    if (RT_LIKELY(!pVCpu->tm.s.fUpdateStats))
    { /*likely*/ }
    else
    {
        pVCpu->tm.s.fUpdateStats = false;

        uint64_t const cNsTotalNew = RTTimeNanoTS() - pVCpu->tm.s.nsStartTotal;
        uint64_t const cNsOtherNew = cNsTotalNew - cNsExecutingNew - pVCpu->tm.s.cNsHalted;

# if defined(VBOX_WITH_STATISTICS) || defined(VBOX_WITH_NS_ACCOUNTING_STATS)
        STAM_REL_COUNTER_ADD(&pVCpu->tm.s.StatNsTotal, cNsTotalNew - pVCpu->tm.s.cNsTotalStat);
        int64_t const cNsOtherNewDelta = cNsOtherNew - pVCpu->tm.s.cNsOtherStat;
        if (cNsOtherNewDelta > 0)
            STAM_REL_COUNTER_ADD(&pVCpu->tm.s.StatNsOther, (uint64_t)cNsOtherNewDelta);
# endif

        pVCpu->tm.s.cNsTotalStat = cNsTotalNew;
        pVCpu->tm.s.cNsOtherStat = cNsOtherNew;
    }

#endif
}


/**
 * Notification that the cpu is entering the halt state
 *
 * This call must always be paired with a TMNotifyEndOfExecution call.
 *
 * The function may, depending on the configuration, resume the TSC and future
 * clocks that only ticks when we're halted.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMM_INT_DECL(void) TMNotifyStartOfHalt(PVMCPUCC pVCpu)
{
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);

#ifndef VBOX_WITHOUT_NS_ACCOUNTING
    pVCpu->tm.s.nsStartHalting = RTTimeNanoTS();
    pVCpu->tm.s.fHalting       = true;
#endif

    if (    pVM->tm.s.fTSCTiedToExecution
        &&  !pVM->tm.s.fTSCNotTiedToHalt)
        tmCpuTickResume(pVM, pVCpu);
}


/**
 * Notification that the cpu is leaving the halt state
 *
 * This call must always be paired with a TMNotifyStartOfHalt call.
 *
 * The function may, depending on the configuration, suspend the TSC and future
 * clocks that only ticks when we're halted.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMM_INT_DECL(void) TMNotifyEndOfHalt(PVMCPUCC pVCpu)
{
    PVM pVM = pVCpu->CTX_SUFF(pVM);

    if (    pVM->tm.s.fTSCTiedToExecution
        &&  !pVM->tm.s.fTSCNotTiedToHalt)
        tmCpuTickPause(pVCpu);

#ifndef VBOX_WITHOUT_NS_ACCOUNTING
    uint64_t const u64NsTs        = RTTimeNanoTS();
    uint64_t const cNsTotalNew    = u64NsTs - pVCpu->tm.s.nsStartTotal;
    uint64_t const cNsHaltedDelta = u64NsTs - pVCpu->tm.s.nsStartHalting;
    uint64_t const cNsHaltedNew   = pVCpu->tm.s.cNsHalted + cNsHaltedDelta;
    uint64_t const cNsOtherNew    = cNsTotalNew - pVCpu->tm.s.cNsExecuting - cNsHaltedNew;

    uint32_t uGen = ASMAtomicUoIncU32(&pVCpu->tm.s.uTimesGen); Assert(uGen & 1);
    ASMCompilerBarrier();
    pVCpu->tm.s.fHalting     = false;
    pVCpu->tm.s.fUpdateStats = false;
    pVCpu->tm.s.cNsHalted    = cNsHaltedNew;
    pVCpu->tm.s.cPeriodsHalted++;
    ASMCompilerBarrier();
    ASMAtomicUoWriteU32(&pVCpu->tm.s.uTimesGen, (uGen | 1) + 1);

# if defined(VBOX_WITH_STATISTICS) || defined(VBOX_WITH_NS_ACCOUNTING_STATS)
    STAM_REL_PROFILE_ADD_PERIOD(&pVCpu->tm.s.StatNsHalted, cNsHaltedDelta);
    STAM_REL_COUNTER_ADD(&pVCpu->tm.s.StatNsTotal, cNsTotalNew - pVCpu->tm.s.cNsTotalStat);
    int64_t const cNsOtherNewDelta = cNsOtherNew - pVCpu->tm.s.cNsOtherStat;
    if (cNsOtherNewDelta > 0)
        STAM_REL_COUNTER_ADD(&pVCpu->tm.s.StatNsOther, (uint64_t)cNsOtherNewDelta);
# endif
    pVCpu->tm.s.cNsTotalStat = cNsTotalNew;
    pVCpu->tm.s.cNsOtherStat = cNsOtherNew;
#endif
}


/**
 * Raise the timer force action flag and notify the dedicated timer EMT.
 *
 * @param   pVM         The cross context VM structure.
 */
DECLINLINE(void) tmScheduleNotify(PVMCC pVM)
{
    VMCPUID idCpu = pVM->tm.s.idTimerCpu;
    AssertReturnVoid(idCpu < pVM->cCpus);
    PVMCPUCC pVCpuDst = VMCC_GET_CPU(pVM, idCpu);

    if (!VMCPU_FF_IS_SET(pVCpuDst, VMCPU_FF_TIMER))
    {
        Log5(("TMAll(%u): FF: 0 -> 1\n", __LINE__));
        VMCPU_FF_SET(pVCpuDst, VMCPU_FF_TIMER);
#ifdef IN_RING3
        VMR3NotifyCpuFFU(pVCpuDst->pUVCpu, VMNOTIFYFF_FLAGS_DONE_REM);
#endif
        STAM_COUNTER_INC(&pVM->tm.s.StatScheduleSetFF);
    }
}


/**
 * Schedule the queue which was changed.
 */
DECLINLINE(void) tmSchedule(PVMCC pVM, PTMTIMERQUEUECC pQueueCC, PTMTIMERQUEUE pQueue, PTMTIMER pTimer)
{
    int rc = PDMCritSectTryEnter(pVM, &pQueue->TimerLock);
    if (RT_SUCCESS_NP(rc))
    {
        STAM_PROFILE_START(&pVM->tm.s.CTX_SUFF_Z(StatScheduleOne), a);
        Log3(("tmSchedule: tmTimerQueueSchedule\n"));
        tmTimerQueueSchedule(pVM, pQueueCC, pQueue);
#ifdef VBOX_STRICT
        tmTimerQueuesSanityChecks(pVM, "tmSchedule");
#endif
        STAM_PROFILE_STOP(&pVM->tm.s.CTX_SUFF_Z(StatScheduleOne), a);
        PDMCritSectLeave(pVM, &pQueue->TimerLock);
        return;
    }

    TMTIMERSTATE enmState = pTimer->enmState;
    if (TMTIMERSTATE_IS_PENDING_SCHEDULING(enmState))
        tmScheduleNotify(pVM);
}


/**
 * Try change the state to enmStateNew from enmStateOld
 * and link the timer into the scheduling queue.
 *
 * @returns Success indicator.
 * @param   pTimer          Timer in question.
 * @param   enmStateNew     The new timer state.
 * @param   enmStateOld     The old timer state.
 */
DECLINLINE(bool) tmTimerTry(PTMTIMER pTimer, TMTIMERSTATE enmStateNew, TMTIMERSTATE enmStateOld)
{
    /*
     * Attempt state change.
     */
    bool fRc;
    TM_TRY_SET_STATE(pTimer, enmStateNew, enmStateOld, fRc);
    return fRc;
}


/**
 * Links the timer onto the scheduling queue.
 *
 * @param   pQueueCC    The current context queue (same as @a pQueue for
 *                      ring-3).
 * @param   pQueue      The shared queue data.
 * @param   pTimer      The timer.
 *
 * @todo    FIXME: Look into potential race with the thread running the queues
 *          and stuff.
 */
DECLINLINE(void) tmTimerLinkSchedule(PTMTIMERQUEUECC pQueueCC, PTMTIMERQUEUE pQueue, PTMTIMER pTimer)
{
    Assert(pTimer->idxScheduleNext == UINT32_MAX);
    const uint32_t idxHeadNew = pTimer - &pQueueCC->paTimers[0];
    AssertReturnVoid(idxHeadNew < pQueueCC->cTimersAlloc);

    uint32_t idxHead;
    do
    {
        idxHead = pQueue->idxSchedule;
        Assert(idxHead == UINT32_MAX || idxHead < pQueueCC->cTimersAlloc);
        pTimer->idxScheduleNext = idxHead;
    } while (!ASMAtomicCmpXchgU32(&pQueue->idxSchedule, idxHeadNew, idxHead));
}


/**
 * Try change the state to enmStateNew from enmStateOld
 * and link the timer into the scheduling queue.
 *
 * @returns Success indicator.
 * @param   pQueueCC    The current context queue (same as @a pQueue for
 *                      ring-3).
 * @param   pQueue      The shared queue data.
 * @param   pTimer      Timer in question.
 * @param   enmStateNew The new timer state.
 * @param   enmStateOld The old timer state.
 */
DECLINLINE(bool) tmTimerTryWithLink(PTMTIMERQUEUECC pQueueCC, PTMTIMERQUEUE pQueue, PTMTIMER pTimer,
                                    TMTIMERSTATE enmStateNew, TMTIMERSTATE enmStateOld)
{
    if (tmTimerTry(pTimer, enmStateNew, enmStateOld))
    {
        tmTimerLinkSchedule(pQueueCC, pQueue, pTimer);
        return true;
    }
    return false;
}


/**
 * Links a timer into the active list of a timer queue.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pQueueCC        The current context queue (same as @a pQueue for
 *                          ring-3).
 * @param   pQueue          The shared queue data.
 * @param   pTimer          The timer.
 * @param   u64Expire       The timer expiration time.
 *
 * @remarks Called while owning the relevant queue lock.
 */
DECL_FORCE_INLINE(void) tmTimerQueueLinkActive(PVMCC pVM, PTMTIMERQUEUECC pQueueCC, PTMTIMERQUEUE pQueue,
                                               PTMTIMER pTimer, uint64_t u64Expire)
{
    Assert(pTimer->idxNext == UINT32_MAX);
    Assert(pTimer->idxPrev == UINT32_MAX);
    Assert(pTimer->enmState == TMTIMERSTATE_ACTIVE || pQueue->enmClock != TMCLOCK_VIRTUAL_SYNC); /* (active is not a stable state) */
    RT_NOREF(pVM);

    PTMTIMER pCur = tmTimerQueueGetHead(pQueueCC, pQueue);
    if (pCur)
    {
        for (;; pCur = tmTimerGetNext(pQueueCC, pCur))
        {
            if (pCur->u64Expire > u64Expire)
            {
                const PTMTIMER pPrev = tmTimerGetPrev(pQueueCC, pCur);
                tmTimerSetNext(pQueueCC, pTimer, pCur);
                tmTimerSetPrev(pQueueCC, pTimer, pPrev);
                if (pPrev)
                    tmTimerSetNext(pQueueCC, pPrev, pTimer);
                else
                {
                    tmTimerQueueSetHead(pQueueCC, pQueue, pTimer);
                    ASMAtomicWriteU64(&pQueue->u64Expire, u64Expire);
                    DBGFTRACE_U64_TAG2(pVM, u64Expire, "tmTimerQueueLinkActive head", pTimer->szName);
                }
                tmTimerSetPrev(pQueueCC, pCur, pTimer);
                return;
            }
            if (pCur->idxNext == UINT32_MAX)
            {
                tmTimerSetNext(pQueueCC, pCur, pTimer);
                tmTimerSetPrev(pQueueCC, pTimer, pCur);
                DBGFTRACE_U64_TAG2(pVM, u64Expire, "tmTimerQueueLinkActive tail", pTimer->szName);
                return;
            }
        }
    }
    else
    {
        tmTimerQueueSetHead(pQueueCC, pQueue, pTimer);
        ASMAtomicWriteU64(&pQueue->u64Expire, u64Expire);
        DBGFTRACE_U64_TAG2(pVM, u64Expire, "tmTimerQueueLinkActive empty", pTimer->szName);
    }
}



/**
 * Schedules the given timer on the given queue.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pQueueCC    The current context queue (same as @a pQueue for
 *                      ring-3).
 * @param   pQueue      The shared queue data.
 * @param   pTimer      The timer that needs scheduling.
 *
 * @remarks Called while owning the lock.
 */
DECLINLINE(void) tmTimerQueueScheduleOne(PVMCC pVM, PTMTIMERQUEUECC pQueueCC, PTMTIMERQUEUE pQueue, PTMTIMER pTimer)
{
    Assert(pQueue->enmClock != TMCLOCK_VIRTUAL_SYNC);
    RT_NOREF(pVM);

    /*
     * Processing.
     */
    unsigned cRetries = 2;
    do
    {
        TMTIMERSTATE enmState = pTimer->enmState;
        switch (enmState)
        {
            /*
             * Reschedule timer (in the active list).
             */
            case TMTIMERSTATE_PENDING_RESCHEDULE:
                if (RT_UNLIKELY(!tmTimerTry(pTimer, TMTIMERSTATE_PENDING_SCHEDULE, TMTIMERSTATE_PENDING_RESCHEDULE)))
                    break; /* retry */
                tmTimerQueueUnlinkActive(pVM, pQueueCC, pQueue, pTimer);
                RT_FALL_THRU();

            /*
             * Schedule timer (insert into the active list).
             */
            case TMTIMERSTATE_PENDING_SCHEDULE:
                Assert(pTimer->idxNext == UINT32_MAX); Assert(pTimer->idxPrev == UINT32_MAX);
                if (RT_UNLIKELY(!tmTimerTry(pTimer, TMTIMERSTATE_ACTIVE, TMTIMERSTATE_PENDING_SCHEDULE)))
                    break; /* retry */
                tmTimerQueueLinkActive(pVM, pQueueCC, pQueue, pTimer, pTimer->u64Expire);
                return;

            /*
             * Stop the timer in active list.
             */
            case TMTIMERSTATE_PENDING_STOP:
                if (RT_UNLIKELY(!tmTimerTry(pTimer, TMTIMERSTATE_PENDING_STOP_SCHEDULE, TMTIMERSTATE_PENDING_STOP)))
                    break; /* retry */
                tmTimerQueueUnlinkActive(pVM, pQueueCC, pQueue, pTimer);
                RT_FALL_THRU();

            /*
             * Stop the timer (not on the active list).
             */
            case TMTIMERSTATE_PENDING_STOP_SCHEDULE:
                Assert(pTimer->idxNext == UINT32_MAX); Assert(pTimer->idxPrev == UINT32_MAX);
                if (RT_UNLIKELY(!tmTimerTry(pTimer, TMTIMERSTATE_STOPPED, TMTIMERSTATE_PENDING_STOP_SCHEDULE)))
                    break;
                return;

            /*
             * The timer is pending destruction by TMR3TimerDestroy, our caller.
             * Nothing to do here.
             */
            case TMTIMERSTATE_DESTROY:
                break;

            /*
             * Postpone these until they get into the right state.
             */
            case TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE:
            case TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE:
                tmTimerLinkSchedule(pQueueCC, pQueue, pTimer);
                STAM_COUNTER_INC(&pVM->tm.s.CTX_SUFF_Z(StatPostponed));
                return;

            /*
             * None of these can be in the schedule.
             */
            case TMTIMERSTATE_FREE:
            case TMTIMERSTATE_STOPPED:
            case TMTIMERSTATE_ACTIVE:
            case TMTIMERSTATE_EXPIRED_GET_UNLINK:
            case TMTIMERSTATE_EXPIRED_DELIVER:
            default:
                AssertMsgFailed(("Timer (%p) in the scheduling list has an invalid state %s (%d)!",
                                 pTimer, tmTimerState(pTimer->enmState), pTimer->enmState));
                return;
        }
    } while (cRetries-- > 0);
}


/**
 * Schedules the specified timer queue.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pQueueCC        The current context queue (same as @a pQueue for
 *                          ring-3) data of the queue to schedule.
 * @param   pQueue          The shared queue data of the queue to schedule.
 *
 * @remarks Called while owning the lock.
 */
void tmTimerQueueSchedule(PVMCC pVM, PTMTIMERQUEUECC pQueueCC, PTMTIMERQUEUE pQueue)
{
    Assert(PDMCritSectIsOwner(pVM, &pQueue->TimerLock));

    /*
     * Dequeue the scheduling list and iterate it.
     */
    uint32_t idxNext = ASMAtomicXchgU32(&pQueue->idxSchedule, UINT32_MAX);
    Log2(("tmTimerQueueSchedule: pQueue=%p:{.enmClock=%d, idxNext=%RI32, .u64Expired=%'RU64}\n", pQueue, pQueue->enmClock, idxNext, pQueue->u64Expire));
    while (idxNext != UINT32_MAX)
    {
        AssertBreak(idxNext < pQueueCC->cTimersAlloc);

        /*
         * Unlink the head timer and take down the index of the next one.
         */
        PTMTIMER pTimer = &pQueueCC->paTimers[idxNext];
        idxNext = pTimer->idxScheduleNext;
        pTimer->idxScheduleNext = UINT32_MAX;

        /*
         * Do the scheduling.
         */
        Log2(("tmTimerQueueSchedule: %p:{.enmState=%s, .enmClock=%d, .enmType=%d, .szName=%s}\n",
              pTimer, tmTimerState(pTimer->enmState), pQueue->enmClock, pTimer->enmType, pTimer->szName));
        tmTimerQueueScheduleOne(pVM, pQueueCC, pQueue, pTimer);
        Log2(("tmTimerQueueSchedule: %p: new %s\n", pTimer, tmTimerState(pTimer->enmState)));
    }
    Log2(("tmTimerQueueSchedule: u64Expired=%'RU64\n", pQueue->u64Expire));
}


#ifdef VBOX_STRICT
/**
 * Checks that the timer queues are sane.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pszWhere    Caller location clue.
 */
void tmTimerQueuesSanityChecks(PVMCC pVM, const char *pszWhere)
{
    for (uint32_t idxQueue = 0; idxQueue < RT_ELEMENTS(pVM->tm.s.aTimerQueues); idxQueue++)
    {
        PTMTIMERQUEUE const   pQueue   = &pVM->tm.s.aTimerQueues[idxQueue];
        PTMTIMERQUEUECC const pQueueCC = TM_GET_TIMER_QUEUE_CC(pVM, idxQueue, pQueue);
        Assert(pQueue->enmClock == (TMCLOCK)idxQueue);

        int rc = PDMCritSectTryEnter(pVM, &pQueue->TimerLock);
        if (RT_SUCCESS(rc))
        {
            if (   pQueue->enmClock != TMCLOCK_VIRTUAL_SYNC
                || PDMCritSectTryEnter(pVM, &pVM->tm.s.VirtualSyncLock) == VINF_SUCCESS)
            {
                /* Check the linking of the active lists. */
                PTMTIMER pPrev = NULL;
                for (PTMTIMER pCur = tmTimerQueueGetHead(pQueueCC, pQueue);
                     pCur;
                     pPrev = pCur, pCur = tmTimerGetNext(pQueueCC, pCur))
                {
                    AssertMsg(tmTimerGetPrev(pQueueCC, pCur) == pPrev, ("%s: %p != %p\n", pszWhere, tmTimerGetPrev(pQueueCC, pCur), pPrev));
                    TMTIMERSTATE enmState = pCur->enmState;
                    switch (enmState)
                    {
                        case TMTIMERSTATE_ACTIVE:
                            AssertMsg(   pCur->idxScheduleNext == UINT32_MAX
                                      || pCur->enmState != TMTIMERSTATE_ACTIVE,
                                      ("%s: %RI32\n", pszWhere, pCur->idxScheduleNext));
                            break;
                        case TMTIMERSTATE_PENDING_STOP:
                        case TMTIMERSTATE_PENDING_RESCHEDULE:
                        case TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE:
                            break;
                        default:
                            AssertMsgFailed(("%s: Invalid state enmState=%d %s\n", pszWhere, enmState, tmTimerState(enmState)));
                            break;
                    }
                }

# ifdef IN_RING3
                /* Go thru all the timers and check that the active ones all are in the active lists. */
                int const rcAllocLock = PDMCritSectRwTryEnterShared(pVM, &pQueue->AllocLock);
                uint32_t  idxTimer    = pQueue->cTimersAlloc;
                uint32_t  cFree       = 0;
                while (idxTimer-- > 0)
                {
                    PTMTIMER const     pTimer   = &pQueue->paTimers[idxTimer];
                    TMTIMERSTATE const enmState = pTimer->enmState;
                    switch (enmState)
                    {
                        case TMTIMERSTATE_FREE:
                            cFree++;
                            break;

                        case TMTIMERSTATE_ACTIVE:
                        case TMTIMERSTATE_PENDING_STOP:
                        case TMTIMERSTATE_PENDING_RESCHEDULE:
                        case TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE:
                        {
                            PTMTIMERR3 pCurAct = tmTimerQueueGetHead(pQueueCC, pQueue);
                            Assert(pTimer->idxPrev != UINT32_MAX || pTimer == pCurAct);
                            while (pCurAct && pCurAct != pTimer)
                                pCurAct = tmTimerGetNext(pQueueCC, pCurAct);
                            Assert(pCurAct == pTimer);
                            break;
                        }

                        case TMTIMERSTATE_PENDING_SCHEDULE:
                        case TMTIMERSTATE_PENDING_STOP_SCHEDULE:
                        case TMTIMERSTATE_STOPPED:
                        case TMTIMERSTATE_EXPIRED_DELIVER:
                        {
                            Assert(pTimer->idxNext == UINT32_MAX);
                            Assert(pTimer->idxPrev == UINT32_MAX);
                            for (PTMTIMERR3 pCurAct = tmTimerQueueGetHead(pQueueCC, pQueue);
                                 pCurAct;
                                 pCurAct = tmTimerGetNext(pQueueCC, pCurAct))
                            {
                                Assert(pCurAct != pTimer);
                                Assert(tmTimerGetNext(pQueueCC, pCurAct) != pTimer);
                                Assert(tmTimerGetPrev(pQueueCC, pCurAct) != pTimer);
                            }
                            break;
                        }

                        /* ignore */
                        case TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE:
                            break;

                        case TMTIMERSTATE_INVALID:
                            Assert(idxTimer == 0);
                            break;

                        /* shouldn't get here! */
                        case TMTIMERSTATE_EXPIRED_GET_UNLINK:
                        case TMTIMERSTATE_DESTROY:
                        default:
                            AssertMsgFailed(("Invalid state enmState=%d %s\n", enmState, tmTimerState(enmState)));
                            break;
                    }

                    /* Check the handle value. */
                    if (enmState > TMTIMERSTATE_INVALID && enmState < TMTIMERSTATE_DESTROY)
                    {
                        Assert((pTimer->hSelf & TMTIMERHANDLE_TIMER_IDX_MASK) == idxTimer);
                        Assert(((pTimer->hSelf >> TMTIMERHANDLE_QUEUE_IDX_SHIFT) & TMTIMERHANDLE_QUEUE_IDX_SMASK) == idxQueue);
                    }
                }
                if (RT_SUCCESS(rcAllocLock))
                {
                    Assert(cFree == pQueue->cTimersFree);
                    PDMCritSectRwLeaveShared(pVM, &pQueue->AllocLock);
                }
                else
                    Assert(cFree >= pQueue->cTimersFree); /* Can be lower as the tmr3TimerCreate may run concurrent. */

# endif /* IN_RING3 */

                if (pQueue->enmClock == TMCLOCK_VIRTUAL_SYNC)
                    PDMCritSectLeave(pVM, &pVM->tm.s.VirtualSyncLock);
            }
            PDMCritSectLeave(pVM, &pQueue->TimerLock);
        }
    }
}
#endif /* !VBOX_STRICT */

#ifdef VBOX_HIGH_RES_TIMERS_HACK

/**
 * Worker for tmTimerPollInternal that handles misses when the dedicated timer
 * EMT is polling.
 *
 * @returns See tmTimerPollInternal.
 * @param   pVM                 The cross context VM structure.
 * @param   u64Now              Current virtual clock timestamp.
 * @param   u64Delta            The delta to the next even in ticks of the
 *                              virtual clock.
 * @param   pu64Delta           Where to return the delta.
 */
DECLINLINE(uint64_t) tmTimerPollReturnMiss(PVM pVM, uint64_t u64Now, uint64_t u64Delta, uint64_t *pu64Delta)
{
    Assert(!(u64Delta & RT_BIT_64(63)));

    if (!pVM->tm.s.fVirtualWarpDrive)
    {
        *pu64Delta = u64Delta;
        return u64Delta + u64Now + pVM->tm.s.u64VirtualOffset;
    }

    /*
     * Warp drive adjustments - this is the reverse of what tmVirtualGetRaw is doing.
     */
    uint64_t const u64Start = pVM->tm.s.u64VirtualWarpDriveStart;
    uint32_t const u32Pct   = pVM->tm.s.u32VirtualWarpDrivePercentage;

    uint64_t u64GipTime = u64Delta + u64Now + pVM->tm.s.u64VirtualOffset;
    u64GipTime -= u64Start; /* the start is GIP time. */
    if (u64GipTime >= u64Delta)
    {
        ASMMultU64ByU32DivByU32(u64GipTime, 100, u32Pct);
        ASMMultU64ByU32DivByU32(u64Delta, 100, u32Pct);
    }
    else
    {
        u64Delta -= u64GipTime;
        ASMMultU64ByU32DivByU32(u64GipTime, 100, u32Pct);
        u64Delta += u64GipTime;
    }
    *pu64Delta = u64Delta;
    u64GipTime += u64Start;
    return u64GipTime;
}


/**
 * Worker for tmTimerPollInternal dealing with returns on virtual CPUs other
 * than the one dedicated to timer work.
 *
 * @returns See tmTimerPollInternal.
 * @param   pVM                 The cross context VM structure.
 * @param   u64Now              Current virtual clock timestamp.
 * @param   pu64Delta           Where to return the delta.
 */
DECL_FORCE_INLINE(uint64_t) tmTimerPollReturnOtherCpu(PVM pVM, uint64_t u64Now, uint64_t *pu64Delta)
{
    static const uint64_t s_u64OtherRet = 500000000; /* 500 ms for non-timer EMTs. */
    *pu64Delta = s_u64OtherRet;
    return u64Now + pVM->tm.s.u64VirtualOffset + s_u64OtherRet;
}


/**
 * Worker for tmTimerPollInternal.
 *
 * @returns See tmTimerPollInternal.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   pVCpuDst    The cross context virtual CPU structure of the dedicated
 *                      timer EMT.
 * @param   u64Now      Current virtual clock timestamp.
 * @param   pu64Delta   Where to return the delta.
 * @param   pCounter    The statistics counter to update.
 */
DECL_FORCE_INLINE(uint64_t) tmTimerPollReturnHit(PVM pVM, PVMCPU pVCpu, PVMCPU pVCpuDst, uint64_t u64Now,
                                                 uint64_t *pu64Delta, PSTAMCOUNTER pCounter)
{
    STAM_COUNTER_INC(pCounter); NOREF(pCounter);
    if (pVCpuDst != pVCpu)
        return tmTimerPollReturnOtherCpu(pVM, u64Now, pu64Delta);
    *pu64Delta = 0;
    return 0;
}


/**
 * Common worker for TMTimerPollGIP and TMTimerPoll.
 *
 * This function is called before FFs are checked in the inner execution EM loops.
 *
 * @returns The GIP timestamp of the next event.
 *          0 if the next event has already expired.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   pu64Delta   Where to store the delta.
 * @param   pu64Now     Where to store the current time. Optional.
 *
 * @thread  The emulation thread.
 *
 * @remarks GIP uses ns ticks.
 */
DECL_FORCE_INLINE(uint64_t) tmTimerPollInternal(PVMCC pVM, PVMCPUCC pVCpu, uint64_t *pu64Delta, uint64_t *pu64Now)
{
    VMCPUID idCpu = pVM->tm.s.idTimerCpu;
    AssertReturn(idCpu < pVM->cCpus, 0);
    PVMCPUCC pVCpuDst = VMCC_GET_CPU(pVM, idCpu);

    const uint64_t u64Now = TMVirtualGetNoCheck(pVM);
    STAM_COUNTER_INC(&pVM->tm.s.StatPoll);
    if (pu64Now)
        *pu64Now = u64Now;

    /*
     * Return straight away if the timer FF is already set ...
     */
    if (VMCPU_FF_IS_SET(pVCpuDst, VMCPU_FF_TIMER))
        return tmTimerPollReturnHit(pVM, pVCpu, pVCpuDst, u64Now, pu64Delta, &pVM->tm.s.StatPollAlreadySet);

    /*
     * ... or if timers are being run.
     */
    if (ASMAtomicReadBool(&pVM->tm.s.fRunningQueues))
    {
        STAM_COUNTER_INC(&pVM->tm.s.StatPollRunning);
        return tmTimerPollReturnOtherCpu(pVM, u64Now, pu64Delta);
    }

    /*
     * Check for TMCLOCK_VIRTUAL expiration.
     */
    const uint64_t  u64Expire1 = ASMAtomicReadU64(&pVM->tm.s.aTimerQueues[TMCLOCK_VIRTUAL].u64Expire);
    const int64_t   i64Delta1  = u64Expire1 - u64Now;
    if (i64Delta1 <= 0)
    {
        if (!VMCPU_FF_IS_SET(pVCpuDst, VMCPU_FF_TIMER))
        {
            Log5(("TMAll(%u): FF: %d -> 1\n", __LINE__, VMCPU_FF_IS_SET(pVCpuDst, VMCPU_FF_TIMER)));
            VMCPU_FF_SET(pVCpuDst, VMCPU_FF_TIMER);
        }
        LogFlow(("TMTimerPoll: expire1=%'RU64 <= now=%'RU64\n", u64Expire1, u64Now));
        return tmTimerPollReturnHit(pVM, pVCpu, pVCpuDst, u64Now, pu64Delta, &pVM->tm.s.StatPollVirtual);
    }

    /*
     * Check for TMCLOCK_VIRTUAL_SYNC expiration.
     * This isn't quite as straight forward if in a catch-up, not only do
     * we have to adjust the 'now' but when have to adjust the delta as well.
     */

    /*
     * Optimistic lockless approach.
     */
    uint64_t u64VirtualSyncNow;
    uint64_t u64Expire2 = ASMAtomicUoReadU64(&pVM->tm.s.aTimerQueues[TMCLOCK_VIRTUAL_SYNC].u64Expire);
    if (ASMAtomicUoReadBool(&pVM->tm.s.fVirtualSyncTicking))
    {
        if (!ASMAtomicUoReadBool(&pVM->tm.s.fVirtualSyncCatchUp))
        {
            u64VirtualSyncNow = ASMAtomicReadU64(&pVM->tm.s.offVirtualSync);
            if (RT_LIKELY(   ASMAtomicUoReadBool(&pVM->tm.s.fVirtualSyncTicking)
                          && !ASMAtomicUoReadBool(&pVM->tm.s.fVirtualSyncCatchUp)
                          && u64VirtualSyncNow == ASMAtomicReadU64(&pVM->tm.s.offVirtualSync)
                          && u64Expire2 == ASMAtomicUoReadU64(&pVM->tm.s.aTimerQueues[TMCLOCK_VIRTUAL_SYNC].u64Expire)))
            {
                u64VirtualSyncNow = u64Now - u64VirtualSyncNow;
                int64_t i64Delta2 = u64Expire2 - u64VirtualSyncNow;
                if (i64Delta2 > 0)
                {
                    STAM_COUNTER_INC(&pVM->tm.s.StatPollSimple);
                    STAM_COUNTER_INC(&pVM->tm.s.StatPollMiss);

                    if (pVCpu == pVCpuDst)
                        return tmTimerPollReturnMiss(pVM, u64Now, RT_MIN(i64Delta1, i64Delta2), pu64Delta);
                    return tmTimerPollReturnOtherCpu(pVM, u64Now, pu64Delta);
                }

                if (    !pVM->tm.s.fRunningQueues
                    &&  !VMCPU_FF_IS_SET(pVCpuDst, VMCPU_FF_TIMER))
                {
                    Log5(("TMAll(%u): FF: %d -> 1\n", __LINE__, VMCPU_FF_IS_SET(pVCpuDst, VMCPU_FF_TIMER)));
                    VMCPU_FF_SET(pVCpuDst, VMCPU_FF_TIMER);
                }

                STAM_COUNTER_INC(&pVM->tm.s.StatPollSimple);
                LogFlow(("TMTimerPoll: expire2=%'RU64 <= now=%'RU64\n", u64Expire2, u64Now));
                return tmTimerPollReturnHit(pVM, pVCpu, pVCpuDst, u64Now, pu64Delta, &pVM->tm.s.StatPollVirtualSync);
            }
        }
    }
    else
    {
        STAM_COUNTER_INC(&pVM->tm.s.StatPollSimple);
        LogFlow(("TMTimerPoll: stopped\n"));
        return tmTimerPollReturnHit(pVM, pVCpu, pVCpuDst, u64Now, pu64Delta, &pVM->tm.s.StatPollVirtualSync);
    }

    /*
     * Complicated lockless approach.
     */
    uint64_t    off;
    uint32_t    u32Pct = 0;
    bool        fCatchUp;
    int         cOuterTries = 42;
    for (;; cOuterTries--)
    {
        fCatchUp   = ASMAtomicReadBool(&pVM->tm.s.fVirtualSyncCatchUp);
        off        = ASMAtomicReadU64(&pVM->tm.s.offVirtualSync);
        u64Expire2 = ASMAtomicReadU64(&pVM->tm.s.aTimerQueues[TMCLOCK_VIRTUAL_SYNC].u64Expire);
        if (fCatchUp)
        {
            /* No changes allowed, try get a consistent set of parameters. */
            uint64_t const u64Prev    = ASMAtomicReadU64(&pVM->tm.s.u64VirtualSyncCatchUpPrev);
            uint64_t const offGivenUp = ASMAtomicReadU64(&pVM->tm.s.offVirtualSyncGivenUp);
            u32Pct                    = ASMAtomicReadU32(&pVM->tm.s.u32VirtualSyncCatchUpPercentage);
            if (    (   u64Prev    == ASMAtomicReadU64(&pVM->tm.s.u64VirtualSyncCatchUpPrev)
                     && offGivenUp == ASMAtomicReadU64(&pVM->tm.s.offVirtualSyncGivenUp)
                     && u32Pct     == ASMAtomicReadU32(&pVM->tm.s.u32VirtualSyncCatchUpPercentage)
                     && off        == ASMAtomicReadU64(&pVM->tm.s.offVirtualSync)
                     && u64Expire2 == ASMAtomicReadU64(&pVM->tm.s.aTimerQueues[TMCLOCK_VIRTUAL_SYNC].u64Expire)
                     && ASMAtomicReadBool(&pVM->tm.s.fVirtualSyncCatchUp)
                     && ASMAtomicReadBool(&pVM->tm.s.fVirtualSyncTicking))
                ||  cOuterTries <= 0)
            {
                uint64_t u64Delta = u64Now - u64Prev;
                if (RT_LIKELY(!(u64Delta >> 32)))
                {
                    uint64_t u64Sub = ASMMultU64ByU32DivByU32(u64Delta, u32Pct, 100);
                    if (off > u64Sub + offGivenUp)
                        off -= u64Sub;
                    else /* we've completely caught up. */
                        off = offGivenUp;
                }
                else
                    /* More than 4 seconds since last time (or negative), ignore it. */
                    Log(("TMVirtualGetSync: u64Delta=%RX64 (NoLock)\n", u64Delta));

                /* Check that we're still running and in catch up. */
                if (    ASMAtomicUoReadBool(&pVM->tm.s.fVirtualSyncTicking)
                    &&  ASMAtomicReadBool(&pVM->tm.s.fVirtualSyncCatchUp))
                    break;
            }
        }
        else if (   off        == ASMAtomicReadU64(&pVM->tm.s.offVirtualSync)
                 && u64Expire2 == ASMAtomicReadU64(&pVM->tm.s.aTimerQueues[TMCLOCK_VIRTUAL_SYNC].u64Expire)
                 && !ASMAtomicReadBool(&pVM->tm.s.fVirtualSyncCatchUp)
                 && ASMAtomicReadBool(&pVM->tm.s.fVirtualSyncTicking))
            break; /* Got an consistent offset */

        /* Repeat the initial checks before iterating. */
        if (VMCPU_FF_IS_SET(pVCpuDst, VMCPU_FF_TIMER))
            return tmTimerPollReturnHit(pVM, pVCpu, pVCpuDst, u64Now, pu64Delta, &pVM->tm.s.StatPollAlreadySet);
        if (ASMAtomicUoReadBool(&pVM->tm.s.fRunningQueues))
        {
            STAM_COUNTER_INC(&pVM->tm.s.StatPollRunning);
            return tmTimerPollReturnOtherCpu(pVM, u64Now, pu64Delta);
        }
        if (!ASMAtomicUoReadBool(&pVM->tm.s.fVirtualSyncTicking))
        {
            LogFlow(("TMTimerPoll: stopped\n"));
            return tmTimerPollReturnHit(pVM, pVCpu, pVCpuDst, u64Now, pu64Delta, &pVM->tm.s.StatPollVirtualSync);
        }
        if (cOuterTries <= 0)
            break; /* that's enough */
    }
    if (cOuterTries <= 0)
        STAM_COUNTER_INC(&pVM->tm.s.StatPollELoop);
    u64VirtualSyncNow = u64Now - off;

    /* Calc delta and see if we've got a virtual sync hit. */
    int64_t i64Delta2 = u64Expire2 - u64VirtualSyncNow;
    if (i64Delta2 <= 0)
    {
        if (    !pVM->tm.s.fRunningQueues
            &&  !VMCPU_FF_IS_SET(pVCpuDst, VMCPU_FF_TIMER))
        {
            Log5(("TMAll(%u): FF: %d -> 1\n", __LINE__, VMCPU_FF_IS_SET(pVCpuDst, VMCPU_FF_TIMER)));
            VMCPU_FF_SET(pVCpuDst, VMCPU_FF_TIMER);
        }
        STAM_COUNTER_INC(&pVM->tm.s.StatPollVirtualSync);
        LogFlow(("TMTimerPoll: expire2=%'RU64 <= now=%'RU64\n", u64Expire2, u64Now));
        return tmTimerPollReturnHit(pVM, pVCpu, pVCpuDst, u64Now, pu64Delta, &pVM->tm.s.StatPollVirtualSync);
    }

    /*
     * Return the time left to the next event.
     */
    STAM_COUNTER_INC(&pVM->tm.s.StatPollMiss);
    if (pVCpu == pVCpuDst)
    {
        if (fCatchUp)
            i64Delta2 = ASMMultU64ByU32DivByU32(i64Delta2, 100, u32Pct + 100);
        return tmTimerPollReturnMiss(pVM, u64Now, RT_MIN(i64Delta1, i64Delta2), pu64Delta);
    }
    return tmTimerPollReturnOtherCpu(pVM, u64Now, pu64Delta);
}


/**
 * Set FF if we've passed the next virtual event.
 *
 * This function is called before FFs are checked in the inner execution EM loops.
 *
 * @returns true if timers are pending, false if not.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @thread  The emulation thread.
 */
VMMDECL(bool) TMTimerPollBool(PVMCC pVM, PVMCPUCC pVCpu)
{
    AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
    uint64_t off = 0;
    tmTimerPollInternal(pVM, pVCpu, &off, NULL);
    return off == 0;
}


/**
 * Set FF if we've passed the next virtual event and return virtual time as MS.
 *
 * This function is called before FFs are checked in the inner execution EM loops.
 *
 * This is used by the IEM recompiler for polling timers while also providing a
 * free time source for recent use tracking and such.
 *
 * @returns Nanoseconds till the next event, 0 if event already pending.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   pnsNow      Where to return the current virtual time in nanoseconds.
 * @thread  The emulation thread.
 */
VMM_INT_DECL(uint64_t) TMTimerPollBoolWithNanoTS(PVMCC pVM, PVMCPUCC pVCpu, uint64_t *pnsNow)
{
    AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
    uint64_t offDelta = 0;
    tmTimerPollInternal(pVM, pVCpu, &offDelta, pnsNow);
    return offDelta;
}


/**
 * Set FF if we've passed the next virtual event.
 *
 * This function is called before FFs are checked in the inner execution EM loops.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @thread  The emulation thread.
 */
VMM_INT_DECL(void) TMTimerPollVoid(PVMCC pVM, PVMCPUCC pVCpu)
{
    uint64_t off;
    tmTimerPollInternal(pVM, pVCpu, &off, NULL);
}


/**
 * Set FF if we've passed the next virtual event.
 *
 * This function is called before FFs are checked in the inner execution EM loops.
 *
 * @returns The GIP timestamp of the next event.
 *          0 if the next event has already expired.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   pu64Delta   Where to store the delta.
 * @thread  The emulation thread.
 */
VMM_INT_DECL(uint64_t) TMTimerPollGIP(PVMCC pVM, PVMCPUCC pVCpu, uint64_t *pu64Delta)
{
    return tmTimerPollInternal(pVM, pVCpu, pu64Delta, NULL);
}

#endif /* VBOX_HIGH_RES_TIMERS_HACK */

/**
 * Locks the timer clock.
 *
 * @returns VINF_SUCCESS on success, @a rcBusy if busy, and VERR_NOT_SUPPORTED
 *          if the clock does not have a lock.
 * @param   pVM         The cross context VM structure.
 * @param   hTimer      Timer handle as returned by one of the create functions.
 * @param   rcBusy      What to return in ring-0 and raw-mode context if the
 *                      lock is busy.  Pass VINF_SUCCESS to acquired the
 *                      critical section thru a ring-3 call if necessary.
 *
 * @remarks Currently only supported on timers using the virtual sync clock.
 */
VMMDECL(int) TMTimerLock(PVMCC pVM, TMTIMERHANDLE hTimer, int rcBusy)
{
    TMTIMER_HANDLE_TO_VARS_RETURN(pVM, hTimer); /* => pTimer, pQueueCC, pQueue, idxTimer, idxQueue */
    AssertReturn(idxQueue == TMCLOCK_VIRTUAL_SYNC, VERR_NOT_SUPPORTED);
    return PDMCritSectEnter(pVM, &pVM->tm.s.VirtualSyncLock, rcBusy);
}


/**
 * Unlocks a timer clock locked by TMTimerLock.
 *
 * @param   pVM         The cross context VM structure.
 * @param   hTimer      Timer handle as returned by one of the create functions.
 */
VMMDECL(void) TMTimerUnlock(PVMCC pVM, TMTIMERHANDLE hTimer)
{
    TMTIMER_HANDLE_TO_VARS_RETURN_VOID(pVM, hTimer); /* => pTimer, pQueueCC, pQueue, idxTimer, idxQueue */
    AssertReturnVoid(idxQueue == TMCLOCK_VIRTUAL_SYNC);
    PDMCritSectLeave(pVM, &pVM->tm.s.VirtualSyncLock);
}


/**
 * Checks if the current thread owns the timer clock lock.
 *
 * @returns @c true if its the owner, @c false if not.
 * @param   pVM         The cross context VM structure.
 * @param   hTimer      Timer handle as returned by one of the create functions.
 */
VMMDECL(bool) TMTimerIsLockOwner(PVMCC pVM, TMTIMERHANDLE hTimer)
{
    TMTIMER_HANDLE_TO_VARS_RETURN_EX(pVM, hTimer, false); /* => pTimer, pQueueCC, pQueue, idxTimer, idxQueue */
    AssertReturn(idxQueue == TMCLOCK_VIRTUAL_SYNC, false);
    return PDMCritSectIsOwner(pVM, &pVM->tm.s.VirtualSyncLock);
}


/**
 * Optimized TMTimerSet code path for starting an inactive timer.
 *
 * @returns VBox status code.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pTimer          The timer handle.
 * @param   u64Expire       The new expire time.
 * @param   pQueue          Pointer to the shared timer queue data.
 * @param   idxQueue        The queue index.
 */
static int tmTimerSetOptimizedStart(PVMCC pVM, PTMTIMER pTimer, uint64_t u64Expire, PTMTIMERQUEUE pQueue, uint32_t idxQueue)
{
    Assert(pTimer->idxPrev == UINT32_MAX);
    Assert(pTimer->idxNext == UINT32_MAX);
    Assert(pTimer->enmState == TMTIMERSTATE_ACTIVE);

    /*
     * Calculate and set the expiration time.
     */
    if (idxQueue == TMCLOCK_VIRTUAL_SYNC)
    {
        uint64_t u64Last = ASMAtomicReadU64(&pVM->tm.s.u64VirtualSync);
        AssertMsgStmt(u64Expire >= u64Last,
                      ("exp=%#llx last=%#llx\n", u64Expire, u64Last),
                      u64Expire = u64Last);
    }
    ASMAtomicWriteU64(&pTimer->u64Expire, u64Expire);
    Log2(("tmTimerSetOptimizedStart: %p:{.pszDesc='%s', .u64Expire=%'RU64}\n", pTimer, pTimer->szName, u64Expire));

    /*
     * Link the timer into the active list.
     */
    tmTimerQueueLinkActive(pVM, TM_GET_TIMER_QUEUE_CC(pVM, idxQueue, pQueue), pQueue, pTimer, u64Expire);

    STAM_COUNTER_INC(&pVM->tm.s.StatTimerSetOpt);
    return VINF_SUCCESS;
}


/**
 * TMTimerSet for the virtual sync timer queue.
 *
 * This employs a greatly simplified state machine by always acquiring the
 * queue lock and bypassing the scheduling list.
 *
 * @returns VBox status code
 * @param   pVM                 The cross context VM structure.
 * @param   pTimer              The timer handle.
 * @param   u64Expire           The expiration time.
 */
static int tmTimerVirtualSyncSet(PVMCC pVM, PTMTIMER pTimer, uint64_t u64Expire)
{
    STAM_PROFILE_START(&pVM->tm.s.CTX_SUFF_Z(StatTimerSetVs), a);
    VM_ASSERT_EMT(pVM);
    TMTIMER_ASSERT_SYNC_CRITSECT_ORDER(pVM, pTimer);
    int rc = PDMCritSectEnter(pVM, &pVM->tm.s.VirtualSyncLock, VINF_SUCCESS);
    AssertRCReturn(rc, rc);

    PTMTIMERQUEUE const     pQueue   = &pVM->tm.s.aTimerQueues[TMCLOCK_VIRTUAL_SYNC];
    PTMTIMERQUEUECC const   pQueueCC = TM_GET_TIMER_QUEUE_CC(pVM, TMCLOCK_VIRTUAL_SYNC, pQueue);
    TMTIMERSTATE const      enmState = pTimer->enmState;
    switch (enmState)
    {
        case TMTIMERSTATE_EXPIRED_DELIVER:
        case TMTIMERSTATE_STOPPED:
            if (enmState == TMTIMERSTATE_EXPIRED_DELIVER)
                STAM_COUNTER_INC(&pVM->tm.s.StatTimerSetVsStExpDeliver);
            else
                STAM_COUNTER_INC(&pVM->tm.s.StatTimerSetVsStStopped);

            AssertMsg(u64Expire >= pVM->tm.s.u64VirtualSync,
                      ("%'RU64 < %'RU64 %s\n", u64Expire, pVM->tm.s.u64VirtualSync, pTimer->szName));
            pTimer->u64Expire = u64Expire;
            TM_SET_STATE(pTimer, TMTIMERSTATE_ACTIVE);
            tmTimerQueueLinkActive(pVM, pQueueCC, pQueue, pTimer, u64Expire);
            rc = VINF_SUCCESS;
            break;

        case TMTIMERSTATE_ACTIVE:
            STAM_COUNTER_INC(&pVM->tm.s.StatTimerSetVsStActive);
            tmTimerQueueUnlinkActive(pVM, pQueueCC, pQueue, pTimer);
            pTimer->u64Expire = u64Expire;
            tmTimerQueueLinkActive(pVM, pQueueCC, pQueue, pTimer, u64Expire);
            rc = VINF_SUCCESS;
            break;

        case TMTIMERSTATE_PENDING_RESCHEDULE:
        case TMTIMERSTATE_PENDING_STOP:
        case TMTIMERSTATE_PENDING_SCHEDULE:
        case TMTIMERSTATE_PENDING_STOP_SCHEDULE:
        case TMTIMERSTATE_EXPIRED_GET_UNLINK:
        case TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE:
        case TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE:
        case TMTIMERSTATE_DESTROY:
        case TMTIMERSTATE_FREE:
            AssertLogRelMsgFailed(("Invalid timer state %s: %s\n", tmTimerState(enmState), pTimer->szName));
            rc = VERR_TM_INVALID_STATE;
            break;

        default:
            AssertMsgFailed(("Unknown timer state %d: %s\n", enmState, pTimer->szName));
            rc = VERR_TM_UNKNOWN_STATE;
            break;
    }

    STAM_PROFILE_STOP(&pVM->tm.s.CTX_SUFF_Z(StatTimerSetVs), a);
    PDMCritSectLeave(pVM, &pVM->tm.s.VirtualSyncLock);
    return rc;
}


/**
 * Arm a timer with a (new) expire time.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   hTimer      Timer handle as returned by one of the create functions.
 * @param   u64Expire   New expire time.
 */
VMMDECL(int) TMTimerSet(PVMCC pVM, TMTIMERHANDLE hTimer, uint64_t u64Expire)
{
    TMTIMER_HANDLE_TO_VARS_RETURN(pVM, hTimer); /* => pTimer, pQueueCC, pQueue, idxTimer, idxQueue */
    STAM_COUNTER_INC(&pTimer->StatSetAbsolute);

    /* Treat virtual sync timers specially. */
    if (idxQueue == TMCLOCK_VIRTUAL_SYNC)
        return tmTimerVirtualSyncSet(pVM, pTimer, u64Expire);

    STAM_PROFILE_START(&pVM->tm.s.CTX_SUFF_Z(StatTimerSet), a);
    TMTIMER_ASSERT_CRITSECT(pVM, pTimer);

    DBGFTRACE_U64_TAG2(pVM, u64Expire, "TMTimerSet", pTimer->szName);

#ifdef VBOX_WITH_STATISTICS
    /*
     * Gather optimization info.
     */
    STAM_COUNTER_INC(&pVM->tm.s.StatTimerSet);
    TMTIMERSTATE enmOrgState = pTimer->enmState;
    switch (enmOrgState)
    {
        case TMTIMERSTATE_STOPPED:                  STAM_COUNTER_INC(&pVM->tm.s.StatTimerSetStStopped); break;
        case TMTIMERSTATE_EXPIRED_DELIVER:          STAM_COUNTER_INC(&pVM->tm.s.StatTimerSetStExpDeliver); break;
        case TMTIMERSTATE_ACTIVE:                   STAM_COUNTER_INC(&pVM->tm.s.StatTimerSetStActive); break;
        case TMTIMERSTATE_PENDING_STOP:             STAM_COUNTER_INC(&pVM->tm.s.StatTimerSetStPendStop); break;
        case TMTIMERSTATE_PENDING_STOP_SCHEDULE:    STAM_COUNTER_INC(&pVM->tm.s.StatTimerSetStPendStopSched); break;
        case TMTIMERSTATE_PENDING_SCHEDULE:         STAM_COUNTER_INC(&pVM->tm.s.StatTimerSetStPendSched); break;
        case TMTIMERSTATE_PENDING_RESCHEDULE:       STAM_COUNTER_INC(&pVM->tm.s.StatTimerSetStPendResched); break;
        default:                                    STAM_COUNTER_INC(&pVM->tm.s.StatTimerSetStOther); break;
    }
#endif

#if 1
    /*
     * The most common case is setting the timer again during the callback.
     * The second most common case is starting a timer at some other time.
     */
    TMTIMERSTATE enmState1 = pTimer->enmState;
    if (    enmState1 == TMTIMERSTATE_EXPIRED_DELIVER
        ||  (   enmState1 == TMTIMERSTATE_STOPPED
             && pTimer->pCritSect))
    {
        /* Try take the TM lock and check the state again. */
        int rc = PDMCritSectTryEnter(pVM, &pQueue->TimerLock);
        if (RT_SUCCESS_NP(rc))
        {
            if (RT_LIKELY(tmTimerTry(pTimer, TMTIMERSTATE_ACTIVE, enmState1)))
            {
                tmTimerSetOptimizedStart(pVM, pTimer, u64Expire, pQueue, idxQueue);
                STAM_PROFILE_STOP(&pVM->tm.s.CTX_SUFF_Z(StatTimerSet), a);
                PDMCritSectLeave(pVM, &pQueue->TimerLock);
                return VINF_SUCCESS;
            }
            PDMCritSectLeave(pVM, &pQueue->TimerLock);
        }
    }
#endif

    /*
     * Unoptimized code path.
     */
    int cRetries = 1000;
    do
    {
        /*
         * Change to any of the SET_EXPIRE states if valid and then to SCHEDULE or RESCHEDULE.
         */
        TMTIMERSTATE enmState = pTimer->enmState;
        Log2(("TMTimerSet: %p:{.enmState=%s, .pszDesc='%s'} cRetries=%d u64Expire=%'RU64\n",
              pTimer, tmTimerState(enmState), pTimer->szName, cRetries, u64Expire));
        switch (enmState)
        {
            case TMTIMERSTATE_EXPIRED_DELIVER:
            case TMTIMERSTATE_STOPPED:
                if (tmTimerTryWithLink(pQueueCC, pQueue, pTimer, TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE, enmState))
                {
                    Assert(pTimer->idxPrev == UINT32_MAX);
                    Assert(pTimer->idxNext == UINT32_MAX);
                    pTimer->u64Expire = u64Expire;
                    TM_SET_STATE(pTimer, TMTIMERSTATE_PENDING_SCHEDULE);
                    tmSchedule(pVM, pQueueCC, pQueue, pTimer);
                    STAM_PROFILE_STOP(&pVM->tm.s.CTX_SUFF_Z(StatTimerSet), a);
                    return VINF_SUCCESS;
                }
                break;

            case TMTIMERSTATE_PENDING_SCHEDULE:
            case TMTIMERSTATE_PENDING_STOP_SCHEDULE:
                if (tmTimerTry(pTimer, TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE, enmState))
                {
                    pTimer->u64Expire = u64Expire;
                    TM_SET_STATE(pTimer, TMTIMERSTATE_PENDING_SCHEDULE);
                    tmSchedule(pVM, pQueueCC, pQueue, pTimer);
                    STAM_PROFILE_STOP(&pVM->tm.s.CTX_SUFF_Z(StatTimerSet), a);
                    return VINF_SUCCESS;
                }
                break;


            case TMTIMERSTATE_ACTIVE:
                if (tmTimerTryWithLink(pQueueCC, pQueue, pTimer, TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE, enmState))
                {
                    pTimer->u64Expire = u64Expire;
                    TM_SET_STATE(pTimer, TMTIMERSTATE_PENDING_RESCHEDULE);
                    tmSchedule(pVM, pQueueCC, pQueue, pTimer);
                    STAM_PROFILE_STOP(&pVM->tm.s.CTX_SUFF_Z(StatTimerSet), a);
                    return VINF_SUCCESS;
                }
                break;

            case TMTIMERSTATE_PENDING_RESCHEDULE:
            case TMTIMERSTATE_PENDING_STOP:
                if (tmTimerTry(pTimer, TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE, enmState))
                {
                    pTimer->u64Expire = u64Expire;
                    TM_SET_STATE(pTimer, TMTIMERSTATE_PENDING_RESCHEDULE);
                    tmSchedule(pVM, pQueueCC, pQueue, pTimer);
                    STAM_PROFILE_STOP(&pVM->tm.s.CTX_SUFF_Z(StatTimerSet), a);
                    return VINF_SUCCESS;
                }
                break;


            case TMTIMERSTATE_EXPIRED_GET_UNLINK:
            case TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE:
            case TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE:
#ifdef IN_RING3
                if (!RTThreadYield())
                    RTThreadSleep(1);
#else
/** @todo call host context and yield after a couple of iterations */
#endif
                break;

            /*
             * Invalid states.
             */
            case TMTIMERSTATE_DESTROY:
            case TMTIMERSTATE_FREE:
                AssertMsgFailed(("Invalid timer state %d (%s)\n", enmState, pTimer->szName));
                return VERR_TM_INVALID_STATE;
            default:
                AssertMsgFailed(("Unknown timer state %d (%s)\n", enmState, pTimer->szName));
                return VERR_TM_UNKNOWN_STATE;
        }
    } while (cRetries-- > 0);

    AssertMsgFailed(("Failed waiting for stable state. state=%d (%s)\n", pTimer->enmState, pTimer->szName));
    STAM_PROFILE_STOP(&pVM->tm.s.CTX_SUFF_Z(StatTimerSet), a);
    return VERR_TM_TIMER_UNSTABLE_STATE;
}


/**
 * Return the current time for the specified clock, setting pu64Now if not NULL.
 *
 * @returns Current time.
 * @param   pVM             The cross context VM structure.
 * @param   enmClock        The clock to query.
 * @param   pu64Now         Optional pointer where to store the return time
 */
DECL_FORCE_INLINE(uint64_t) tmTimerSetRelativeNowWorker(PVMCC pVM, TMCLOCK enmClock, uint64_t *pu64Now)
{
    uint64_t u64Now;
    switch (enmClock)
    {
        case TMCLOCK_VIRTUAL_SYNC:
            u64Now = TMVirtualSyncGet(pVM);
            break;
        case TMCLOCK_VIRTUAL:
            u64Now = TMVirtualGet(pVM);
            break;
        case TMCLOCK_REAL:
            u64Now = TMRealGet(pVM);
            break;
        default:
            AssertFatalMsgFailed(("%d\n", enmClock));
    }

    if (pu64Now)
        *pu64Now = u64Now;
    return u64Now;
}


/**
 * Optimized TMTimerSetRelative code path.
 *
 * @returns VBox status code.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pTimer          The timer handle.
 * @param   cTicksToNext    Clock ticks until the next time expiration.
 * @param   pu64Now         Where to return the current time stamp used.
 *                          Optional.
 * @param   pQueueCC        The context specific queue data (same as @a pQueue
 *                          for ring-3).
 * @param   pQueue          The shared queue data.
 */
static int tmTimerSetRelativeOptimizedStart(PVMCC pVM, PTMTIMER pTimer, uint64_t cTicksToNext, uint64_t *pu64Now,
                                            PTMTIMERQUEUECC pQueueCC, PTMTIMERQUEUE pQueue)
{
    Assert(pTimer->idxPrev == UINT32_MAX);
    Assert(pTimer->idxNext == UINT32_MAX);
    Assert(pTimer->enmState == TMTIMERSTATE_ACTIVE);

    /*
     * Calculate and set the expiration time.
     */
    uint64_t const  u64Expire = cTicksToNext + tmTimerSetRelativeNowWorker(pVM, pQueue->enmClock, pu64Now);
    pTimer->u64Expire         = u64Expire;
    Log2(("tmTimerSetRelativeOptimizedStart: %p:{.pszDesc='%s', .u64Expire=%'RU64} cTicksToNext=%'RU64\n", pTimer, pTimer->szName, u64Expire, cTicksToNext));

    /*
     * Link the timer into the active list.
     */
    DBGFTRACE_U64_TAG2(pVM, u64Expire, "tmTimerSetRelativeOptimizedStart", pTimer->szName);
    tmTimerQueueLinkActive(pVM, pQueueCC, pQueue, pTimer, u64Expire);

    STAM_COUNTER_INC(&pVM->tm.s.StatTimerSetRelativeOpt);
    return VINF_SUCCESS;
}


/**
 * TMTimerSetRelative for the virtual sync timer queue.
 *
 * This employs a greatly simplified state machine by always acquiring the
 * queue lock and bypassing the scheduling list.
 *
 * @returns VBox status code
 * @param   pVM                 The cross context VM structure.
 * @param   pTimer              The timer to (re-)arm.
 * @param   cTicksToNext        Clock ticks until the next time expiration.
 * @param   pu64Now             Where to return the current time stamp used.
 *                              Optional.
 */
static int tmTimerVirtualSyncSetRelative(PVMCC pVM, PTMTIMER pTimer, uint64_t cTicksToNext, uint64_t *pu64Now)
{
    STAM_PROFILE_START(pVM->tm.s.CTX_SUFF_Z(StatTimerSetRelativeVs), a);
    VM_ASSERT_EMT(pVM);
    TMTIMER_ASSERT_SYNC_CRITSECT_ORDER(pVM, pTimer);
    int rc = PDMCritSectEnter(pVM, &pVM->tm.s.VirtualSyncLock, VINF_SUCCESS);
    AssertRCReturn(rc, rc);

    /* Calculate the expiration tick. */
    uint64_t u64Expire = TMVirtualSyncGetNoCheck(pVM);
    if (pu64Now)
        *pu64Now = u64Expire;
    u64Expire += cTicksToNext;

    /* Update the timer. */
    PTMTIMERQUEUE const     pQueue   = &pVM->tm.s.aTimerQueues[TMCLOCK_VIRTUAL_SYNC];
    PTMTIMERQUEUECC const   pQueueCC = TM_GET_TIMER_QUEUE_CC(pVM, TMCLOCK_VIRTUAL_SYNC, pQueue);
    TMTIMERSTATE const      enmState = pTimer->enmState;
    switch (enmState)
    {
        case TMTIMERSTATE_EXPIRED_DELIVER:
        case TMTIMERSTATE_STOPPED:
            if (enmState == TMTIMERSTATE_EXPIRED_DELIVER)
                STAM_COUNTER_INC(&pVM->tm.s.StatTimerSetRelativeVsStExpDeliver);
            else
                STAM_COUNTER_INC(&pVM->tm.s.StatTimerSetRelativeVsStStopped);
            pTimer->u64Expire = u64Expire;
            TM_SET_STATE(pTimer, TMTIMERSTATE_ACTIVE);
            tmTimerQueueLinkActive(pVM, pQueueCC, pQueue, pTimer, u64Expire);
            rc = VINF_SUCCESS;
            break;

        case TMTIMERSTATE_ACTIVE:
            STAM_COUNTER_INC(&pVM->tm.s.StatTimerSetRelativeVsStActive);
            tmTimerQueueUnlinkActive(pVM, pQueueCC, pQueue, pTimer);
            pTimer->u64Expire = u64Expire;
            tmTimerQueueLinkActive(pVM, pQueueCC, pQueue, pTimer, u64Expire);
            rc = VINF_SUCCESS;
            break;

        case TMTIMERSTATE_PENDING_RESCHEDULE:
        case TMTIMERSTATE_PENDING_STOP:
        case TMTIMERSTATE_PENDING_SCHEDULE:
        case TMTIMERSTATE_PENDING_STOP_SCHEDULE:
        case TMTIMERSTATE_EXPIRED_GET_UNLINK:
        case TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE:
        case TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE:
        case TMTIMERSTATE_DESTROY:
        case TMTIMERSTATE_FREE:
            AssertLogRelMsgFailed(("Invalid timer state %s: %s\n", tmTimerState(enmState), pTimer->szName));
            rc = VERR_TM_INVALID_STATE;
            break;

        default:
            AssertMsgFailed(("Unknown timer state %d: %s\n", enmState, pTimer->szName));
            rc = VERR_TM_UNKNOWN_STATE;
            break;
    }

    STAM_PROFILE_STOP(&pVM->tm.s.CTX_SUFF_Z(StatTimerSetRelativeVs), a);
    PDMCritSectLeave(pVM, &pVM->tm.s.VirtualSyncLock);
    return rc;
}


/**
 * Arm a timer with a expire time relative to the current time.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pTimer          The timer to arm.
 * @param   cTicksToNext    Clock ticks until the next time expiration.
 * @param   pu64Now         Where to return the current time stamp used.
 *                          Optional.
 * @param   pQueueCC        The context specific queue data (same as @a pQueue
 *                          for ring-3).
 * @param   pQueue          The shared queue data.
 */
static int tmTimerSetRelative(PVMCC pVM, PTMTIMER pTimer, uint64_t cTicksToNext, uint64_t *pu64Now,
                              PTMTIMERQUEUECC pQueueCC, PTMTIMERQUEUE pQueue)
{
    STAM_COUNTER_INC(&pTimer->StatSetRelative);

    /* Treat virtual sync timers specially. */
    if (pQueue->enmClock == TMCLOCK_VIRTUAL_SYNC)
        return tmTimerVirtualSyncSetRelative(pVM, pTimer, cTicksToNext, pu64Now);

    STAM_PROFILE_START(&pVM->tm.s.CTX_SUFF_Z(StatTimerSetRelative), a);
    TMTIMER_ASSERT_CRITSECT(pVM, pTimer);

    DBGFTRACE_U64_TAG2(pVM, cTicksToNext, "TMTimerSetRelative", pTimer->szName);

#ifdef VBOX_WITH_STATISTICS
    /*
     * Gather optimization info.
     */
    STAM_COUNTER_INC(&pVM->tm.s.StatTimerSetRelative);
    TMTIMERSTATE enmOrgState = pTimer->enmState;
    switch (enmOrgState)
    {
        case TMTIMERSTATE_STOPPED:                  STAM_COUNTER_INC(&pVM->tm.s.StatTimerSetRelativeStStopped); break;
        case TMTIMERSTATE_EXPIRED_DELIVER:          STAM_COUNTER_INC(&pVM->tm.s.StatTimerSetRelativeStExpDeliver); break;
        case TMTIMERSTATE_ACTIVE:                   STAM_COUNTER_INC(&pVM->tm.s.StatTimerSetRelativeStActive); break;
        case TMTIMERSTATE_PENDING_STOP:             STAM_COUNTER_INC(&pVM->tm.s.StatTimerSetRelativeStPendStop); break;
        case TMTIMERSTATE_PENDING_STOP_SCHEDULE:    STAM_COUNTER_INC(&pVM->tm.s.StatTimerSetRelativeStPendStopSched); break;
        case TMTIMERSTATE_PENDING_SCHEDULE:         STAM_COUNTER_INC(&pVM->tm.s.StatTimerSetRelativeStPendSched); break;
        case TMTIMERSTATE_PENDING_RESCHEDULE:       STAM_COUNTER_INC(&pVM->tm.s.StatTimerSetRelativeStPendResched); break;
        default:                                    STAM_COUNTER_INC(&pVM->tm.s.StatTimerSetRelativeStOther); break;
    }
#endif

    /*
     * Try to take the TM lock and optimize the common cases.
     *
     * With the TM lock we can safely make optimizations like immediate
     * scheduling and we can also be 100% sure that we're not racing the
     * running of the timer queues. As an additional restraint we require the
     * timer to have a critical section associated with to be 100% there aren't
     * concurrent operations on the timer. (This latter isn't necessary any
     * longer as this isn't supported for any timers, critsect or not.)
     *
     * Note! Lock ordering doesn't apply when we only _try_ to
     *       get the innermost locks.
     */
    bool fOwnTMLock = RT_SUCCESS_NP(PDMCritSectTryEnter(pVM, &pQueue->TimerLock));
#if 1
    if (    fOwnTMLock
        &&  pTimer->pCritSect)
    {
        TMTIMERSTATE enmState = pTimer->enmState;
        if (RT_LIKELY(  (   enmState == TMTIMERSTATE_EXPIRED_DELIVER
                         || enmState == TMTIMERSTATE_STOPPED)
                      && tmTimerTry(pTimer, TMTIMERSTATE_ACTIVE, enmState)))
        {
            tmTimerSetRelativeOptimizedStart(pVM, pTimer, cTicksToNext, pu64Now, pQueueCC, pQueue);
            STAM_PROFILE_STOP(&pVM->tm.s.CTX_SUFF_Z(StatTimerSetRelative), a);
            PDMCritSectLeave(pVM, &pQueue->TimerLock);
            return VINF_SUCCESS;
        }

        /* Optimize other states when it becomes necessary. */
    }
#endif

    /*
     * Unoptimized path.
     */
    int rc;
    for (int cRetries = 1000; ; cRetries--)
    {
        /*
         * Change to any of the SET_EXPIRE states if valid and then to SCHEDULE or RESCHEDULE.
         */
        TMTIMERSTATE enmState = pTimer->enmState;
        switch (enmState)
        {
            case TMTIMERSTATE_STOPPED:
                if (pQueue->enmClock == TMCLOCK_VIRTUAL_SYNC)
                {
                    /** @todo To fix assertion in tmR3TimerQueueRunVirtualSync:
                     *              Figure a safe way of activating this timer while the queue is
                     *              being run.
                     *        (99.9% sure this that the assertion is caused by DevAPIC.cpp
                     *        re-starting the timer in response to a initial_count write.) */
                }
                RT_FALL_THRU();
            case TMTIMERSTATE_EXPIRED_DELIVER:
                if (tmTimerTryWithLink(pQueueCC, pQueue, pTimer, TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE, enmState))
                {
                    Assert(pTimer->idxPrev == UINT32_MAX);
                    Assert(pTimer->idxNext == UINT32_MAX);
                    pTimer->u64Expire = cTicksToNext + tmTimerSetRelativeNowWorker(pVM, pQueue->enmClock, pu64Now);
                    Log2(("TMTimerSetRelative: %p:{.enmState=%s, .pszDesc='%s', .u64Expire=%'RU64} cRetries=%d [EXP/STOP]\n",
                          pTimer, tmTimerState(enmState), pTimer->szName, pTimer->u64Expire, cRetries));
                    TM_SET_STATE(pTimer, TMTIMERSTATE_PENDING_SCHEDULE);
                    tmSchedule(pVM, pQueueCC, pQueue, pTimer);
                    rc = VINF_SUCCESS;
                    break;
                }
                rc = VERR_TRY_AGAIN;
                break;

            case TMTIMERSTATE_PENDING_SCHEDULE:
            case TMTIMERSTATE_PENDING_STOP_SCHEDULE:
                if (tmTimerTry(pTimer, TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE, enmState))
                {
                    pTimer->u64Expire = cTicksToNext + tmTimerSetRelativeNowWorker(pVM, pQueue->enmClock, pu64Now);
                    Log2(("TMTimerSetRelative: %p:{.enmState=%s, .pszDesc='%s', .u64Expire=%'RU64} cRetries=%d [PEND_SCHED]\n",
                          pTimer, tmTimerState(enmState), pTimer->szName, pTimer->u64Expire, cRetries));
                    TM_SET_STATE(pTimer, TMTIMERSTATE_PENDING_SCHEDULE);
                    tmSchedule(pVM, pQueueCC, pQueue, pTimer);
                    rc = VINF_SUCCESS;
                    break;
                }
                rc = VERR_TRY_AGAIN;
                break;


            case TMTIMERSTATE_ACTIVE:
                if (tmTimerTryWithLink(pQueueCC, pQueue, pTimer, TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE, enmState))
                {
                    pTimer->u64Expire = cTicksToNext + tmTimerSetRelativeNowWorker(pVM, pQueue->enmClock, pu64Now);
                    Log2(("TMTimerSetRelative: %p:{.enmState=%s, .pszDesc='%s', .u64Expire=%'RU64} cRetries=%d [ACTIVE]\n",
                          pTimer, tmTimerState(enmState), pTimer->szName, pTimer->u64Expire, cRetries));
                    TM_SET_STATE(pTimer, TMTIMERSTATE_PENDING_RESCHEDULE);
                    tmSchedule(pVM, pQueueCC, pQueue, pTimer);
                    rc = VINF_SUCCESS;
                    break;
                }
                rc = VERR_TRY_AGAIN;
                break;

            case TMTIMERSTATE_PENDING_RESCHEDULE:
            case TMTIMERSTATE_PENDING_STOP:
                if (tmTimerTry(pTimer, TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE, enmState))
                {
                    pTimer->u64Expire = cTicksToNext + tmTimerSetRelativeNowWorker(pVM, pQueue->enmClock, pu64Now);
                    Log2(("TMTimerSetRelative: %p:{.enmState=%s, .pszDesc='%s', .u64Expire=%'RU64} cRetries=%d [PEND_RESCH/STOP]\n",
                          pTimer, tmTimerState(enmState), pTimer->szName, pTimer->u64Expire, cRetries));
                    TM_SET_STATE(pTimer, TMTIMERSTATE_PENDING_RESCHEDULE);
                    tmSchedule(pVM, pQueueCC, pQueue, pTimer);
                    rc = VINF_SUCCESS;
                    break;
                }
                rc = VERR_TRY_AGAIN;
                break;


            case TMTIMERSTATE_EXPIRED_GET_UNLINK:
            case TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE:
            case TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE:
#ifdef IN_RING3
                if (!RTThreadYield())
                    RTThreadSleep(1);
#else
/** @todo call host context and yield after a couple of iterations */
#endif
                rc = VERR_TRY_AGAIN;
                break;

            /*
             * Invalid states.
             */
            case TMTIMERSTATE_DESTROY:
            case TMTIMERSTATE_FREE:
                AssertMsgFailed(("Invalid timer state %d (%s)\n", enmState, pTimer->szName));
                rc = VERR_TM_INVALID_STATE;
                break;

            default:
                AssertMsgFailed(("Unknown timer state %d (%s)\n", enmState, pTimer->szName));
                rc = VERR_TM_UNKNOWN_STATE;
                break;
        }

        /* switch + loop is tedious to break out of. */
        if (rc == VINF_SUCCESS)
            break;

        if (rc != VERR_TRY_AGAIN)
        {
            tmTimerSetRelativeNowWorker(pVM, pQueue->enmClock, pu64Now);
            break;
        }
        if (cRetries <= 0)
        {
            AssertMsgFailed(("Failed waiting for stable state. state=%d (%s)\n", pTimer->enmState, pTimer->szName));
            rc = VERR_TM_TIMER_UNSTABLE_STATE;
            tmTimerSetRelativeNowWorker(pVM, pQueue->enmClock, pu64Now);
            break;
        }

        /*
         * Retry to gain locks.
         */
        if (!fOwnTMLock)
            fOwnTMLock = RT_SUCCESS_NP(PDMCritSectTryEnter(pVM, &pQueue->TimerLock));

    } /* for (;;) */

    /*
     * Clean up and return.
     */
    if (fOwnTMLock)
        PDMCritSectLeave(pVM, &pQueue->TimerLock);

    STAM_PROFILE_STOP(&pVM->tm.s.CTX_SUFF_Z(StatTimerSetRelative), a);
    return rc;
}


/**
 * Arm a timer with a expire time relative to the current time.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   hTimer          Timer handle as returned by one of the create functions.
 * @param   cTicksToNext    Clock ticks until the next time expiration.
 * @param   pu64Now         Where to return the current time stamp used.
 *                          Optional.
 */
VMMDECL(int) TMTimerSetRelative(PVMCC pVM, TMTIMERHANDLE hTimer, uint64_t cTicksToNext, uint64_t *pu64Now)
{
    TMTIMER_HANDLE_TO_VARS_RETURN(pVM, hTimer); /* => pTimer, pQueueCC, pQueue, idxTimer, idxQueue */
    return tmTimerSetRelative(pVM, pTimer, cTicksToNext, pu64Now, pQueueCC, pQueue);
}


/**
 * Drops a hint about the frequency of the timer.
 *
 * This is used by TM and the VMM to calculate how often guest execution needs
 * to be interrupted.  The hint is automatically cleared by TMTimerStop.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   hTimer      Timer handle as returned by one of the create functions.
 * @param   uHzHint     The frequency hint.  Pass 0 to clear the hint.
 *
 * @remarks We're using an integer hertz value here since anything above 1 HZ
 *          is not going to be any trouble satisfying scheduling wise.  The
 *          range where it makes sense is >= 100 HZ.
 */
VMMDECL(int) TMTimerSetFrequencyHint(PVMCC pVM, TMTIMERHANDLE hTimer, uint32_t uHzHint)
{
    TMTIMER_HANDLE_TO_VARS_RETURN(pVM, hTimer); /* => pTimer, pQueueCC, pQueue, idxTimer, idxQueue */
    TMTIMER_ASSERT_CRITSECT(pVM, pTimer);

    uint32_t const uHzOldHint = pTimer->uHzHint;
    pTimer->uHzHint = uHzHint;

    uint32_t const uMaxHzHint = pQueue->uMaxHzHint;
    if (   uHzHint    >  uMaxHzHint
        || uHzOldHint >= uMaxHzHint)
        ASMAtomicOrU64(&pVM->tm.s.HzHint.u64Combined, RT_BIT_32(idxQueue) | RT_BIT_32(idxQueue + 16));

    return VINF_SUCCESS;
}


/**
 * TMTimerStop for the virtual sync timer queue.
 *
 * This employs a greatly simplified state machine by always acquiring the
 * queue lock and bypassing the scheduling list.
 *
 * @returns VBox status code
 * @param   pVM                 The cross context VM structure.
 * @param   pTimer              The timer handle.
 */
static int tmTimerVirtualSyncStop(PVMCC pVM, PTMTIMER pTimer)
{
    STAM_PROFILE_START(&pVM->tm.s.CTX_SUFF_Z(StatTimerStopVs), a);
    VM_ASSERT_EMT(pVM);
    TMTIMER_ASSERT_SYNC_CRITSECT_ORDER(pVM, pTimer);
    int rc = PDMCritSectEnter(pVM, &pVM->tm.s.VirtualSyncLock, VINF_SUCCESS);
    AssertRCReturn(rc, rc);

    /* Reset the HZ hint. */
    uint32_t uOldHzHint = pTimer->uHzHint;
    if (uOldHzHint)
    {
        if (uOldHzHint >= pVM->tm.s.aTimerQueues[TMCLOCK_VIRTUAL_SYNC].uMaxHzHint)
            ASMAtomicOrU64(&pVM->tm.s.HzHint.u64Combined, RT_BIT_32(TMCLOCK_VIRTUAL_SYNC) | RT_BIT_32(TMCLOCK_VIRTUAL_SYNC + 16));
        pTimer->uHzHint = 0;
    }

    /* Update the timer state. */
    TMTIMERSTATE const enmState = pTimer->enmState;
    switch (enmState)
    {
        case TMTIMERSTATE_ACTIVE:
        {
            PTMTIMERQUEUE const pQueue = &pVM->tm.s.aTimerQueues[TMCLOCK_VIRTUAL_SYNC];
            tmTimerQueueUnlinkActive(pVM, TM_GET_TIMER_QUEUE_CC(pVM, TMCLOCK_VIRTUAL_SYNC, pQueue), pQueue, pTimer);
            TM_SET_STATE(pTimer, TMTIMERSTATE_STOPPED);
            rc = VINF_SUCCESS;
            break;
        }

        case TMTIMERSTATE_EXPIRED_DELIVER:
            TM_SET_STATE(pTimer, TMTIMERSTATE_STOPPED);
            rc = VINF_SUCCESS;
            break;

        case TMTIMERSTATE_STOPPED:
            rc = VINF_SUCCESS;
            break;

        case TMTIMERSTATE_PENDING_RESCHEDULE:
        case TMTIMERSTATE_PENDING_STOP:
        case TMTIMERSTATE_PENDING_SCHEDULE:
        case TMTIMERSTATE_PENDING_STOP_SCHEDULE:
        case TMTIMERSTATE_EXPIRED_GET_UNLINK:
        case TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE:
        case TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE:
        case TMTIMERSTATE_DESTROY:
        case TMTIMERSTATE_FREE:
            AssertLogRelMsgFailed(("Invalid timer state %s: %s\n", tmTimerState(enmState), pTimer->szName));
            rc = VERR_TM_INVALID_STATE;
            break;

        default:
            AssertMsgFailed(("Unknown timer state %d: %s\n", enmState, pTimer->szName));
            rc = VERR_TM_UNKNOWN_STATE;
            break;
    }

    STAM_PROFILE_STOP(&pVM->tm.s.CTX_SUFF_Z(StatTimerStopVs), a);
    PDMCritSectLeave(pVM, &pVM->tm.s.VirtualSyncLock);
    return rc;
}


/**
 * Stop the timer.
 * Use TMR3TimerArm() to "un-stop" the timer.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   hTimer      Timer handle as returned by one of the create functions.
 */
VMMDECL(int) TMTimerStop(PVMCC pVM, TMTIMERHANDLE hTimer)
{
    TMTIMER_HANDLE_TO_VARS_RETURN(pVM, hTimer); /* => pTimer, pQueueCC, pQueue, idxTimer, idxQueue */
    STAM_COUNTER_INC(&pTimer->StatStop);

    /* Treat virtual sync timers specially. */
    if (idxQueue == TMCLOCK_VIRTUAL_SYNC)
        return tmTimerVirtualSyncStop(pVM, pTimer);

    STAM_PROFILE_START(&pVM->tm.s.CTX_SUFF_Z(StatTimerStop), a);
    TMTIMER_ASSERT_CRITSECT(pVM, pTimer);

    /*
     * Reset the HZ hint.
     */
    uint32_t const uOldHzHint = pTimer->uHzHint;
    if (uOldHzHint)
    {
        if (uOldHzHint >= pQueue->uMaxHzHint)
            ASMAtomicOrU64(&pVM->tm.s.HzHint.u64Combined, RT_BIT_32(idxQueue) | RT_BIT_32(idxQueue + 16));
        pTimer->uHzHint = 0;
    }

    /** @todo see if this function needs optimizing. */
    int cRetries = 1000;
    do
    {
        /*
         * Change to any of the SET_EXPIRE states if valid and then to SCHEDULE or RESCHEDULE.
         */
        TMTIMERSTATE    enmState = pTimer->enmState;
        Log2(("TMTimerStop: %p:{.enmState=%s, .pszDesc='%s'} cRetries=%d\n",
              pTimer, tmTimerState(enmState), pTimer->szName, cRetries));
        switch (enmState)
        {
            case TMTIMERSTATE_EXPIRED_DELIVER:
                //AssertMsgFailed(("You don't stop an expired timer dude!\n"));
                return VERR_INVALID_PARAMETER;

            case TMTIMERSTATE_STOPPED:
            case TMTIMERSTATE_PENDING_STOP:
            case TMTIMERSTATE_PENDING_STOP_SCHEDULE:
                STAM_PROFILE_STOP(&pVM->tm.s.CTX_SUFF_Z(StatTimerStop), a);
                return VINF_SUCCESS;

            case TMTIMERSTATE_PENDING_SCHEDULE:
                if (tmTimerTry(pTimer, TMTIMERSTATE_PENDING_STOP_SCHEDULE, enmState))
                {
                    tmSchedule(pVM, pQueueCC, pQueue, pTimer);
                    STAM_PROFILE_STOP(&pVM->tm.s.CTX_SUFF_Z(StatTimerStop), a);
                    return VINF_SUCCESS;
                }
                break;

            case TMTIMERSTATE_PENDING_RESCHEDULE:
                if (tmTimerTry(pTimer, TMTIMERSTATE_PENDING_STOP, enmState))
                {
                    tmSchedule(pVM, pQueueCC, pQueue, pTimer);
                    STAM_PROFILE_STOP(&pVM->tm.s.CTX_SUFF_Z(StatTimerStop), a);
                    return VINF_SUCCESS;
                }
                break;

            case TMTIMERSTATE_ACTIVE:
                if (tmTimerTryWithLink(pQueueCC, pQueue, pTimer, TMTIMERSTATE_PENDING_STOP, enmState))
                {
                    tmSchedule(pVM, pQueueCC, pQueue, pTimer);
                    STAM_PROFILE_STOP(&pVM->tm.s.CTX_SUFF_Z(StatTimerStop), a);
                    return VINF_SUCCESS;
                }
                break;

            case TMTIMERSTATE_EXPIRED_GET_UNLINK:
            case TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE:
            case TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE:
#ifdef IN_RING3
                if (!RTThreadYield())
                    RTThreadSleep(1);
#else
/** @todo call host and yield cpu after a while. */
#endif
                break;

            /*
             * Invalid states.
             */
            case TMTIMERSTATE_DESTROY:
            case TMTIMERSTATE_FREE:
                AssertMsgFailed(("Invalid timer state %d (%s)\n", enmState, pTimer->szName));
                return VERR_TM_INVALID_STATE;
            default:
                AssertMsgFailed(("Unknown timer state %d (%s)\n", enmState, pTimer->szName));
                return VERR_TM_UNKNOWN_STATE;
        }
    } while (cRetries-- > 0);

    AssertMsgFailed(("Failed waiting for stable state. state=%d (%s)\n", pTimer->enmState, pTimer->szName));
    STAM_PROFILE_STOP(&pVM->tm.s.CTX_SUFF_Z(StatTimerStop), a);
    return VERR_TM_TIMER_UNSTABLE_STATE;
}


/**
 * Get the current clock time.
 * Handy for calculating the new expire time.
 *
 * @returns Current clock time.
 * @param   pVM         The cross context VM structure.
 * @param   hTimer      Timer handle as returned by one of the create functions.
 */
VMMDECL(uint64_t) TMTimerGet(PVMCC pVM, TMTIMERHANDLE hTimer)
{
    TMTIMER_HANDLE_TO_VARS_RETURN_EX(pVM, hTimer, 0); /* => pTimer, pQueueCC, pQueue, idxTimer, idxQueue */
    STAM_COUNTER_INC(&pTimer->StatGet);

    uint64_t u64;
    switch (pQueue->enmClock)
    {
        case TMCLOCK_VIRTUAL:
            u64 = TMVirtualGet(pVM);
            break;
        case TMCLOCK_VIRTUAL_SYNC:
            u64 = TMVirtualSyncGet(pVM);
            break;
        case TMCLOCK_REAL:
            u64 = TMRealGet(pVM);
            break;
        default:
            AssertMsgFailed(("Invalid enmClock=%d\n", pQueue->enmClock));
            return UINT64_MAX;
    }
    //Log2(("TMTimerGet: returns %'RU64 (pTimer=%p:{.enmState=%s, .pszDesc='%s'})\n",
    //      u64, pTimer, tmTimerState(pTimer->enmState), pTimer->szName));
    return u64;
}


/**
 * Get the frequency of the timer clock.
 *
 * @returns Clock frequency (as Hz of course).
 * @param   pVM         The cross context VM structure.
 * @param   hTimer      Timer handle as returned by one of the create functions.
 */
VMMDECL(uint64_t) TMTimerGetFreq(PVMCC pVM, TMTIMERHANDLE hTimer)
{
    TMTIMER_HANDLE_TO_VARS_RETURN_EX(pVM, hTimer, 0); /* => pTimer, pQueueCC, pQueue, idxTimer, idxQueue */
    switch (pQueue->enmClock)
    {
        case TMCLOCK_VIRTUAL:
        case TMCLOCK_VIRTUAL_SYNC:
            return TMCLOCK_FREQ_VIRTUAL;

        case TMCLOCK_REAL:
            return TMCLOCK_FREQ_REAL;

        default:
            AssertMsgFailed(("Invalid enmClock=%d\n", pQueue->enmClock));
            return 0;
    }
}


/**
 * Get the expire time of the timer.
 * Only valid for active timers.
 *
 * @returns Expire time of the timer.
 * @param   pVM         The cross context VM structure.
 * @param   hTimer      Timer handle as returned by one of the create functions.
 */
VMMDECL(uint64_t) TMTimerGetExpire(PVMCC pVM, TMTIMERHANDLE hTimer)
{
    TMTIMER_HANDLE_TO_VARS_RETURN_EX(pVM, hTimer, UINT64_MAX); /* => pTimer, pQueueCC, pQueue, idxTimer, idxQueue */
    TMTIMER_ASSERT_CRITSECT(pVM, pTimer);
    int cRetries = 1000;
    do
    {
        TMTIMERSTATE enmState = pTimer->enmState;
        switch (enmState)
        {
            case TMTIMERSTATE_EXPIRED_GET_UNLINK:
            case TMTIMERSTATE_EXPIRED_DELIVER:
            case TMTIMERSTATE_STOPPED:
            case TMTIMERSTATE_PENDING_STOP:
            case TMTIMERSTATE_PENDING_STOP_SCHEDULE:
                Log2(("TMTimerGetExpire: returns ~0 (pTimer=%p:{.enmState=%s, .pszDesc='%s'})\n",
                      pTimer, tmTimerState(pTimer->enmState), pTimer->szName));
                return UINT64_MAX;

            case TMTIMERSTATE_ACTIVE:
            case TMTIMERSTATE_PENDING_RESCHEDULE:
            case TMTIMERSTATE_PENDING_SCHEDULE:
                Log2(("TMTimerGetExpire: returns %'RU64 (pTimer=%p:{.enmState=%s, .pszDesc='%s'})\n",
                      pTimer->u64Expire, pTimer, tmTimerState(pTimer->enmState), pTimer->szName));
                return pTimer->u64Expire;

            case TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE:
            case TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE:
#ifdef IN_RING3
                if (!RTThreadYield())
                    RTThreadSleep(1);
#endif
                break;

            /*
             * Invalid states.
             */
            case TMTIMERSTATE_DESTROY:
            case TMTIMERSTATE_FREE:
                AssertMsgFailed(("Invalid timer state %d (%s)\n", enmState, pTimer->szName));
                Log2(("TMTimerGetExpire: returns ~0 (pTimer=%p:{.enmState=%s, .pszDesc='%s'})\n",
                      pTimer, tmTimerState(pTimer->enmState), pTimer->szName));
                return UINT64_MAX;
            default:
                AssertMsgFailed(("Unknown timer state %d (%s)\n", enmState, pTimer->szName));
                return UINT64_MAX;
        }
    } while (cRetries-- > 0);

    AssertMsgFailed(("Failed waiting for stable state. state=%d (%s)\n", pTimer->enmState, pTimer->szName));
    Log2(("TMTimerGetExpire: returns ~0 (pTimer=%p:{.enmState=%s, .pszDesc='%s'})\n",
          pTimer, tmTimerState(pTimer->enmState), pTimer->szName));
    return UINT64_MAX;
}


/**
 * Checks if a timer is active or not.
 *
 * @returns True if active.
 * @returns False if not active.
 * @param   pVM         The cross context VM structure.
 * @param   hTimer      Timer handle as returned by one of the create functions.
 */
VMMDECL(bool) TMTimerIsActive(PVMCC pVM, TMTIMERHANDLE hTimer)
{
    TMTIMER_HANDLE_TO_VARS_RETURN_EX(pVM, hTimer, false); /* => pTimer, pQueueCC, pQueue, idxTimer, idxQueue */
    TMTIMERSTATE enmState = pTimer->enmState;
    switch (enmState)
    {
        case TMTIMERSTATE_STOPPED:
        case TMTIMERSTATE_EXPIRED_GET_UNLINK:
        case TMTIMERSTATE_EXPIRED_DELIVER:
        case TMTIMERSTATE_PENDING_STOP:
        case TMTIMERSTATE_PENDING_STOP_SCHEDULE:
            Log2(("TMTimerIsActive: returns false (pTimer=%p:{.enmState=%s, .pszDesc='%s'})\n",
                  pTimer, tmTimerState(pTimer->enmState), pTimer->szName));
            return false;

        case TMTIMERSTATE_ACTIVE:
        case TMTIMERSTATE_PENDING_RESCHEDULE:
        case TMTIMERSTATE_PENDING_SCHEDULE:
        case TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE:
        case TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE:
            Log2(("TMTimerIsActive: returns true (pTimer=%p:{.enmState=%s, .pszDesc='%s'})\n",
                  pTimer, tmTimerState(pTimer->enmState), pTimer->szName));
            return true;

        /*
         * Invalid states.
         */
        case TMTIMERSTATE_DESTROY:
        case TMTIMERSTATE_FREE:
            AssertMsgFailed(("Invalid timer state %s (%s)\n", tmTimerState(enmState), pTimer->szName));
            Log2(("TMTimerIsActive: returns false (pTimer=%p:{.enmState=%s, .pszDesc='%s'})\n",
                  pTimer, tmTimerState(pTimer->enmState), pTimer->szName));
            return false;
        default:
            AssertMsgFailed(("Unknown timer state %d (%s)\n", enmState, pTimer->szName));
            return false;
    }
}


/* -=-=-=-=-=-=- Convenience APIs -=-=-=-=-=-=- */


/**
 * Arm a timer with a (new) expire time relative to current time.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   hTimer          Timer handle as returned by one of the create functions.
 * @param   cMilliesToNext  Number of milliseconds to the next tick.
 */
VMMDECL(int) TMTimerSetMillies(PVMCC pVM, TMTIMERHANDLE hTimer, uint32_t cMilliesToNext)
{
    TMTIMER_HANDLE_TO_VARS_RETURN(pVM, hTimer); /* => pTimer, pQueueCC, pQueue, idxTimer, idxQueue */
    switch (pQueue->enmClock)
    {
        case TMCLOCK_VIRTUAL:
            AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
            return tmTimerSetRelative(pVM, pTimer, cMilliesToNext * UINT64_C(1000000), NULL, pQueueCC, pQueue);

        case TMCLOCK_VIRTUAL_SYNC:
            AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
            return tmTimerSetRelative(pVM, pTimer, cMilliesToNext * UINT64_C(1000000), NULL, pQueueCC, pQueue);

        case TMCLOCK_REAL:
            AssertCompile(TMCLOCK_FREQ_REAL == 1000);
            return tmTimerSetRelative(pVM, pTimer, cMilliesToNext, NULL, pQueueCC, pQueue);

        default:
            AssertMsgFailed(("Invalid enmClock=%d\n", pQueue->enmClock));
            return VERR_TM_TIMER_BAD_CLOCK;
    }
}


/**
 * Arm a timer with a (new) expire time relative to current time.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   hTimer          Timer handle as returned by one of the create functions.
 * @param   cMicrosToNext   Number of microseconds to the next tick.
 */
VMMDECL(int) TMTimerSetMicro(PVMCC pVM, TMTIMERHANDLE hTimer, uint64_t cMicrosToNext)
{
    TMTIMER_HANDLE_TO_VARS_RETURN(pVM, hTimer); /* => pTimer, pQueueCC, pQueue, idxTimer, idxQueue */
    switch (pQueue->enmClock)
    {
        case TMCLOCK_VIRTUAL:
            AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
            return tmTimerSetRelative(pVM, pTimer, cMicrosToNext * 1000, NULL, pQueueCC, pQueue);

        case TMCLOCK_VIRTUAL_SYNC:
            AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
            return tmTimerSetRelative(pVM, pTimer, cMicrosToNext * 1000, NULL, pQueueCC, pQueue);

        case TMCLOCK_REAL:
            AssertCompile(TMCLOCK_FREQ_REAL == 1000);
            return tmTimerSetRelative(pVM, pTimer, cMicrosToNext / 1000, NULL, pQueueCC, pQueue);

        default:
            AssertMsgFailed(("Invalid enmClock=%d\n", pQueue->enmClock));
            return VERR_TM_TIMER_BAD_CLOCK;
    }
}


/**
 * Arm a timer with a (new) expire time relative to current time.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   hTimer          Timer handle as returned by one of the create functions.
 * @param   cNanosToNext    Number of nanoseconds to the next tick.
 */
VMMDECL(int) TMTimerSetNano(PVMCC pVM, TMTIMERHANDLE hTimer, uint64_t cNanosToNext)
{
    TMTIMER_HANDLE_TO_VARS_RETURN(pVM, hTimer); /* => pTimer, pQueueCC, pQueue, idxTimer, idxQueue */
    switch (pQueue->enmClock)
    {
        case TMCLOCK_VIRTUAL:
            AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
            return tmTimerSetRelative(pVM, pTimer, cNanosToNext, NULL, pQueueCC, pQueue);

        case TMCLOCK_VIRTUAL_SYNC:
            AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
            return tmTimerSetRelative(pVM, pTimer, cNanosToNext, NULL, pQueueCC, pQueue);

        case TMCLOCK_REAL:
            AssertCompile(TMCLOCK_FREQ_REAL == 1000);
            return tmTimerSetRelative(pVM, pTimer, cNanosToNext / 1000000, NULL, pQueueCC, pQueue);

        default:
            AssertMsgFailed(("Invalid enmClock=%d\n", pQueue->enmClock));
            return VERR_TM_TIMER_BAD_CLOCK;
    }
}


/**
 * Get the current clock time as nanoseconds.
 *
 * @returns The timer clock as nanoseconds.
 * @param   pVM         The cross context VM structure.
 * @param   hTimer      Timer handle as returned by one of the create functions.
 */
VMMDECL(uint64_t) TMTimerGetNano(PVMCC pVM, TMTIMERHANDLE hTimer)
{
    return TMTimerToNano(pVM, hTimer, TMTimerGet(pVM, hTimer));
}


/**
 * Get the current clock time as microseconds.
 *
 * @returns The timer clock as microseconds.
 * @param   pVM         The cross context VM structure.
 * @param   hTimer      Timer handle as returned by one of the create functions.
 */
VMMDECL(uint64_t) TMTimerGetMicro(PVMCC pVM, TMTIMERHANDLE hTimer)
{
    return TMTimerToMicro(pVM, hTimer, TMTimerGet(pVM, hTimer));
}


/**
 * Get the current clock time as milliseconds.
 *
 * @returns The timer clock as milliseconds.
 * @param   pVM         The cross context VM structure.
 * @param   hTimer      Timer handle as returned by one of the create functions.
 */
VMMDECL(uint64_t) TMTimerGetMilli(PVMCC pVM, TMTIMERHANDLE hTimer)
{
    return TMTimerToMilli(pVM, hTimer, TMTimerGet(pVM, hTimer));
}


/**
 * Converts the specified timer clock time to nanoseconds.
 *
 * @returns nanoseconds.
 * @param   pVM         The cross context VM structure.
 * @param   hTimer      Timer handle as returned by one of the create functions.
 * @param   cTicks      The clock ticks.
 * @remark  There could be rounding errors here. We just do a simple integer divide
 *          without any adjustments.
 */
VMMDECL(uint64_t) TMTimerToNano(PVMCC pVM, TMTIMERHANDLE hTimer, uint64_t cTicks)
{
    TMTIMER_HANDLE_TO_VARS_RETURN_EX(pVM, hTimer, 0); /* => pTimer, pQueueCC, pQueue, idxTimer, idxQueue */
    switch (pQueue->enmClock)
    {
        case TMCLOCK_VIRTUAL:
        case TMCLOCK_VIRTUAL_SYNC:
            AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
            return cTicks;

        case TMCLOCK_REAL:
            AssertCompile(TMCLOCK_FREQ_REAL == 1000);
            return cTicks * 1000000;

        default:
            AssertMsgFailed(("Invalid enmClock=%d\n", pQueue->enmClock));
            return 0;
    }
}


/**
 * Converts the specified timer clock time to microseconds.
 *
 * @returns microseconds.
 * @param   pVM         The cross context VM structure.
 * @param   hTimer      Timer handle as returned by one of the create functions.
 * @param   cTicks      The clock ticks.
 * @remark  There could be rounding errors here. We just do a simple integer divide
 *          without any adjustments.
 */
VMMDECL(uint64_t) TMTimerToMicro(PVMCC pVM, TMTIMERHANDLE hTimer, uint64_t cTicks)
{
    TMTIMER_HANDLE_TO_VARS_RETURN_EX(pVM, hTimer, 0); /* => pTimer, pQueueCC, pQueue, idxTimer, idxQueue */
    switch (pQueue->enmClock)
    {
        case TMCLOCK_VIRTUAL:
        case TMCLOCK_VIRTUAL_SYNC:
            AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
            return cTicks / 1000;

        case TMCLOCK_REAL:
            AssertCompile(TMCLOCK_FREQ_REAL == 1000);
            return cTicks * 1000;

        default:
            AssertMsgFailed(("Invalid enmClock=%d\n", pQueue->enmClock));
            return 0;
    }
}


/**
 * Converts the specified timer clock time to milliseconds.
 *
 * @returns milliseconds.
 * @param   pVM         The cross context VM structure.
 * @param   hTimer      Timer handle as returned by one of the create functions.
 * @param   cTicks      The clock ticks.
 * @remark  There could be rounding errors here. We just do a simple integer divide
 *          without any adjustments.
 */
VMMDECL(uint64_t) TMTimerToMilli(PVMCC pVM, TMTIMERHANDLE hTimer, uint64_t cTicks)
{
    TMTIMER_HANDLE_TO_VARS_RETURN_EX(pVM, hTimer, 0); /* => pTimer, pQueueCC, pQueue, idxTimer, idxQueue */
    switch (pQueue->enmClock)
    {
        case TMCLOCK_VIRTUAL:
        case TMCLOCK_VIRTUAL_SYNC:
            AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
            return cTicks / 1000000;

        case TMCLOCK_REAL:
            AssertCompile(TMCLOCK_FREQ_REAL == 1000);
            return cTicks;

        default:
            AssertMsgFailed(("Invalid enmClock=%d\n", pQueue->enmClock));
            return 0;
    }
}


/**
 * Converts the specified nanosecond timestamp to timer clock ticks.
 *
 * @returns timer clock ticks.
 * @param   pVM         The cross context VM structure.
 * @param   hTimer      Timer handle as returned by one of the create functions.
 * @param   cNanoSecs   The nanosecond value ticks to convert.
 * @remark  There could be rounding and overflow errors here.
 */
VMMDECL(uint64_t) TMTimerFromNano(PVMCC pVM, TMTIMERHANDLE hTimer, uint64_t cNanoSecs)
{
    TMTIMER_HANDLE_TO_VARS_RETURN_EX(pVM, hTimer, 0); /* => pTimer, pQueueCC, pQueue, idxTimer, idxQueue */
    switch (pQueue->enmClock)
    {
        case TMCLOCK_VIRTUAL:
        case TMCLOCK_VIRTUAL_SYNC:
            AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
            return cNanoSecs;

        case TMCLOCK_REAL:
            AssertCompile(TMCLOCK_FREQ_REAL == 1000);
            return cNanoSecs / 1000000;

        default:
            AssertMsgFailed(("Invalid enmClock=%d\n", pQueue->enmClock));
            return 0;
    }
}


/**
 * Converts the specified microsecond timestamp to timer clock ticks.
 *
 * @returns timer clock ticks.
 * @param   pVM         The cross context VM structure.
 * @param   hTimer      Timer handle as returned by one of the create functions.
 * @param   cMicroSecs  The microsecond value ticks to convert.
 * @remark  There could be rounding and overflow errors here.
 */
VMMDECL(uint64_t) TMTimerFromMicro(PVMCC pVM, TMTIMERHANDLE hTimer, uint64_t cMicroSecs)
{
    TMTIMER_HANDLE_TO_VARS_RETURN_EX(pVM, hTimer, 0); /* => pTimer, pQueueCC, pQueue, idxTimer, idxQueue */
    switch (pQueue->enmClock)
    {
        case TMCLOCK_VIRTUAL:
        case TMCLOCK_VIRTUAL_SYNC:
            AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
            return cMicroSecs * 1000;

        case TMCLOCK_REAL:
            AssertCompile(TMCLOCK_FREQ_REAL == 1000);
            return cMicroSecs / 1000;

        default:
            AssertMsgFailed(("Invalid enmClock=%d\n", pQueue->enmClock));
            return 0;
    }
}


/**
 * Converts the specified millisecond timestamp to timer clock ticks.
 *
 * @returns timer clock ticks.
 * @param   pVM         The cross context VM structure.
 * @param   hTimer      Timer handle as returned by one of the create functions.
 * @param   cMilliSecs  The millisecond value ticks to convert.
 * @remark  There could be rounding and overflow errors here.
 */
VMMDECL(uint64_t) TMTimerFromMilli(PVMCC pVM, TMTIMERHANDLE hTimer, uint64_t cMilliSecs)
{
    TMTIMER_HANDLE_TO_VARS_RETURN_EX(pVM, hTimer, 0); /* => pTimer, pQueueCC, pQueue, idxTimer, idxQueue */
    switch (pQueue->enmClock)
    {
        case TMCLOCK_VIRTUAL:
        case TMCLOCK_VIRTUAL_SYNC:
            AssertCompile(TMCLOCK_FREQ_VIRTUAL == 1000000000);
            return cMilliSecs * 1000000;

        case TMCLOCK_REAL:
            AssertCompile(TMCLOCK_FREQ_REAL == 1000);
            return cMilliSecs;

        default:
            AssertMsgFailed(("Invalid enmClock=%d\n", pQueue->enmClock));
            return 0;
    }
}


/**
 * Convert state to string.
 *
 * @returns Readonly status name.
 * @param   enmState    State.
 */
const char *tmTimerState(TMTIMERSTATE enmState)
{
    switch (enmState)
    {
#define CASE(num, state) \
            case TMTIMERSTATE_##state: \
                AssertCompile(TMTIMERSTATE_##state == (num)); \
                return #num "-" #state
        CASE( 0,INVALID);
        CASE( 1,STOPPED);
        CASE( 2,ACTIVE);
        CASE( 3,EXPIRED_GET_UNLINK);
        CASE( 4,EXPIRED_DELIVER);
        CASE( 5,PENDING_STOP);
        CASE( 6,PENDING_STOP_SCHEDULE);
        CASE( 7,PENDING_SCHEDULE_SET_EXPIRE);
        CASE( 8,PENDING_SCHEDULE);
        CASE( 9,PENDING_RESCHEDULE_SET_EXPIRE);
        CASE(10,PENDING_RESCHEDULE);
        CASE(11,DESTROY);
        CASE(12,FREE);
        default:
            AssertMsgFailed(("Invalid state enmState=%d\n", enmState));
            return "Invalid state!";
#undef CASE
    }
}


#if defined(IN_RING0) || defined(IN_RING3)
/**
 * Copies over old timers and initialized newly allocted ones.
 *
 * Helper for TMR0TimerQueueGrow an tmR3TimerQueueGrow.
 *
 * @param   paTimers            The new timer allocation.
 * @param   paOldTimers         The old timers.
 * @param   cNewTimers          Number of new timers.
 * @param   cOldTimers          Number of old timers.
 */
void tmHCTimerQueueGrowInit(PTMTIMER paTimers, TMTIMER const *paOldTimers, uint32_t cNewTimers, uint32_t cOldTimers)
{
    Assert(cOldTimers < cNewTimers);

    /*
     * Copy over the old info and initialize the new handles.
     */
    if (cOldTimers > 0)
        memcpy(paTimers, paOldTimers, sizeof(TMTIMER) * cOldTimers);

    size_t i = cNewTimers;
    while (i-- > cOldTimers)
    {
        paTimers[i].u64Expire       = UINT64_MAX;
        paTimers[i].enmType         = TMTIMERTYPE_INVALID;
        paTimers[i].enmState        = TMTIMERSTATE_FREE;
        paTimers[i].idxScheduleNext = UINT32_MAX;
        paTimers[i].idxNext         = UINT32_MAX;
        paTimers[i].idxPrev         = UINT32_MAX;
        paTimers[i].hSelf           = NIL_TMTIMERHANDLE;
    }

    /*
     * Mark the zero'th entry as allocated but invalid if we just allocated it.
     */
    if (cOldTimers == 0)
    {
        paTimers[0].enmState = TMTIMERSTATE_INVALID;
        paTimers[0].szName[0] = 'n';
        paTimers[0].szName[1] = 'i';
        paTimers[0].szName[2] = 'l';
        paTimers[0].szName[3] = '\0';
    }
}
#endif /* IN_RING0 || IN_RING3 */


/**
 * The slow path of tmGetFrequencyHint() where we try to recalculate the value.
 *
 * @returns The highest frequency.  0 if no timers care.
 * @param   pVM             The cross context VM structure.
 * @param   uOldMaxHzHint   The old global hint.
 */
DECL_NO_INLINE(static, uint32_t) tmGetFrequencyHintSlow(PVMCC pVM, uint32_t uOldMaxHzHint)
{
    /* Set two bits, though not entirely sure it's needed (too exhaused to think clearly)
       but it should force other callers thru the slow path while we're recalculating and
       help us detect changes while we're recalculating. */
    AssertCompile(RT_ELEMENTS(pVM->tm.s.aTimerQueues) <= 16);

    /*
     * The "right" highest frequency value isn't so important that we'll block
     * waiting on the timer semaphores.
     */
    uint32_t uMaxHzHint = 0;
    for (uint32_t idxQueue = 0; idxQueue < RT_ELEMENTS(pVM->tm.s.aTimerQueues); idxQueue++)
    {
        PTMTIMERQUEUE pQueue = &pVM->tm.s.aTimerQueues[idxQueue];

        /* Get the max Hz hint for the queue. */
        uint32_t uMaxHzHintQueue;
        if (  !(ASMAtomicUoReadU64(&pVM->tm.s.HzHint.u64Combined) & (RT_BIT_32(idxQueue) | RT_BIT_32(idxQueue + 16)))
            || RT_FAILURE_NP(PDMCritSectTryEnter(pVM, &pQueue->TimerLock)))
            uMaxHzHintQueue = ASMAtomicReadU32(&pQueue->uMaxHzHint);
        else
        {
            /* Is it still necessary to do updating? */
            if (ASMAtomicUoReadU64(&pVM->tm.s.HzHint.u64Combined) & (RT_BIT_32(idxQueue) | RT_BIT_32(idxQueue + 16)))
            {
                ASMAtomicAndU64(&pVM->tm.s.HzHint.u64Combined, ~RT_BIT_64(idxQueue + 16)); /* clear one flag up front */

                PTMTIMERQUEUECC pQueueCC = TM_GET_TIMER_QUEUE_CC(pVM, idxQueue, pQueue);
                uMaxHzHintQueue = 0;
                for (PTMTIMER pCur = tmTimerQueueGetHead(pQueueCC, pQueue);
                     pCur;
                     pCur = tmTimerGetNext(pQueueCC, pCur))
                {
                    uint32_t uHzHint = ASMAtomicUoReadU32(&pCur->uHzHint);
                    if (uHzHint > uMaxHzHintQueue)
                    {
                        TMTIMERSTATE enmState = pCur->enmState;
                        switch (enmState)
                        {
                            case TMTIMERSTATE_ACTIVE:
                            case TMTIMERSTATE_EXPIRED_GET_UNLINK:
                            case TMTIMERSTATE_EXPIRED_DELIVER:
                            case TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE:
                            case TMTIMERSTATE_PENDING_SCHEDULE:
                            case TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE:
                            case TMTIMERSTATE_PENDING_RESCHEDULE:
                                uMaxHzHintQueue = uHzHint;
                                break;

                            case TMTIMERSTATE_STOPPED:
                            case TMTIMERSTATE_PENDING_STOP:
                            case TMTIMERSTATE_PENDING_STOP_SCHEDULE:
                            case TMTIMERSTATE_DESTROY:
                            case TMTIMERSTATE_FREE:
                            case TMTIMERSTATE_INVALID:
                                break;
                            /* no default, want gcc warnings when adding more states. */
                        }
                    }
                }

                /* Write the new Hz hint for the quest and clear the other update flag. */
                ASMAtomicUoWriteU32(&pQueue->uMaxHzHint, uMaxHzHintQueue);
                ASMAtomicAndU64(&pVM->tm.s.HzHint.u64Combined, ~RT_BIT_64(idxQueue));
            }
            else
                uMaxHzHintQueue = ASMAtomicUoReadU32(&pQueue->uMaxHzHint);

            PDMCritSectLeave(pVM, &pQueue->TimerLock);
        }

        /* Update the global max Hz hint. */
        if (uMaxHzHint < uMaxHzHintQueue)
            uMaxHzHint = uMaxHzHintQueue;
    }

    /*
     * Update the frequency hint if no pending frequency changes and we didn't race anyone thru here.
     */
    uint64_t u64Actual = RT_MAKE_U64(0 /*no pending updates*/, uOldMaxHzHint);
    if (ASMAtomicCmpXchgExU64(&pVM->tm.s.HzHint.u64Combined, RT_MAKE_U64(0, uMaxHzHint), u64Actual, &u64Actual))
        Log(("tmGetFrequencyHintSlow: New value %u Hz\n", uMaxHzHint));
    else
        for (uint32_t iTry = 1;; iTry++)
        {
            if (RT_LO_U32(u64Actual) != 0)
                Log(("tmGetFrequencyHintSlow: Outdated value %u Hz (%#x, try %u)\n", uMaxHzHint, RT_LO_U32(u64Actual), iTry));
            else if (iTry >= 4)
                Log(("tmGetFrequencyHintSlow: Unable to set %u Hz (try %u)\n", uMaxHzHint, iTry));
            else if (ASMAtomicCmpXchgExU64(&pVM->tm.s.HzHint.u64Combined, RT_MAKE_U64(0, uMaxHzHint), u64Actual, &u64Actual))
                Log(("tmGetFrequencyHintSlow: New value %u Hz (try %u)\n", uMaxHzHint, iTry));
            else
                continue;
            break;
        }
    return uMaxHzHint;
}


/**
 * Gets the highest frequency hint for all the important timers.
 *
 * @returns The highest frequency.  0 if no timers care.
 * @param   pVM         The cross context VM structure.
 */
DECLINLINE(uint32_t) tmGetFrequencyHint(PVMCC pVM)
{
    /*
     * Query the value, recalculate it if necessary.
     */
    uint64_t u64Combined = ASMAtomicReadU64(&pVM->tm.s.HzHint.u64Combined);
    if (RT_HI_U32(u64Combined) == 0)
        return RT_LO_U32(u64Combined); /* hopefully somewhat likely */
    return tmGetFrequencyHintSlow(pVM, RT_LO_U32(u64Combined));
}


/**
 * Calculates a host timer frequency that would be suitable for the current
 * timer load.
 *
 * This will take the highest timer frequency, adjust for catch-up and warp
 * driver, and finally add a little fudge factor.  The caller (VMM) will use
 * the result to adjust the per-cpu preemption timer.
 *
 * @returns The highest frequency.  0 if no important timers around.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 */
VMM_INT_DECL(uint32_t) TMCalcHostTimerFrequency(PVMCC pVM, PVMCPUCC pVCpu)
{
    uint32_t uHz = tmGetFrequencyHint(pVM);

    /* Catch up, we have to be more aggressive than the % indicates at the
       beginning of the effort. */
    if (ASMAtomicUoReadBool(&pVM->tm.s.fVirtualSyncCatchUp))
    {
        uint32_t u32Pct = ASMAtomicReadU32(&pVM->tm.s.u32VirtualSyncCatchUpPercentage);
        if (ASMAtomicReadBool(&pVM->tm.s.fVirtualSyncCatchUp))
        {
            if (u32Pct <= 100)
                u32Pct = u32Pct * pVM->tm.s.cPctHostHzFudgeFactorCatchUp100 / 100;
            else if (u32Pct <= 200)
                u32Pct = u32Pct * pVM->tm.s.cPctHostHzFudgeFactorCatchUp200 / 100;
            else if (u32Pct <= 400)
                u32Pct = u32Pct * pVM->tm.s.cPctHostHzFudgeFactorCatchUp400 / 100;
            uHz *= u32Pct + 100;
            uHz /= 100;
        }
    }

    /* Warp drive. */
    if (ASMAtomicUoReadBool(&pVM->tm.s.fVirtualWarpDrive))
    {
        uint32_t u32Pct = ASMAtomicReadU32(&pVM->tm.s.u32VirtualWarpDrivePercentage);
        if (ASMAtomicReadBool(&pVM->tm.s.fVirtualWarpDrive))
        {
            uHz *= u32Pct;
            uHz /= 100;
        }
    }

    /* Fudge factor. */
    if (pVCpu->idCpu == pVM->tm.s.idTimerCpu)
        uHz *= pVM->tm.s.cPctHostHzFudgeFactorTimerCpu;
    else
        uHz *= pVM->tm.s.cPctHostHzFudgeFactorOtherCpu;
    uHz /= 100;

    /* Make sure it isn't too high. */
    if (uHz > pVM->tm.s.cHostHzMax)
        uHz = pVM->tm.s.cHostHzMax;

    return uHz;
}


/**
 * Whether the guest virtual clock is ticking.
 *
 * @returns true if ticking, false otherwise.
 * @param   pVM     The cross context VM structure.
 */
VMM_INT_DECL(bool) TMVirtualIsTicking(PVM pVM)
{
    return RT_BOOL(pVM->tm.s.cVirtualTicking);
}

