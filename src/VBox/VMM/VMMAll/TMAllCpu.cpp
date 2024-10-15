/* $Id: TMAllCpu.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * TM - Timeout Manager, CPU Time, All Contexts.
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
#include <VBox/vmm/tm.h>
#include <VBox/vmm/gim.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/nem.h>
#if   defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h> /* for SUPGetCpuHzFromGIP; ASMReadTSC */
#elif defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32)
# include <iprt/asm-arm.h>
#endif
#include "TMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/sup.h>

#include <VBox/param.h>
#include <VBox/err.h>
#include <iprt/asm-math.h>
#include <iprt/assert.h>
#include <VBox/log.h>



/**
 * Converts from virtual time to raw CPU ticks.
 *
 * Mainly to have the ASMMultU64ByU32DivByU32 overflow trickery in one place.
 *
 * @returns raw CPU ticks.
 * @param   pVM             The cross context VM structure.
 * @param   u64VirtualTime  The virtual time to convert.
 */
DECLINLINE(uint64_t) tmCpuTickCalcFromVirtual(PVMCC pVM, uint64_t u64VirtualTime)
{
    if (pVM->tm.s.cTSCTicksPerSecond <= UINT32_MAX)
        return ASMMultU64ByU32DivByU32(u64VirtualTime, (uint32_t)pVM->tm.s.cTSCTicksPerSecond, TMCLOCK_FREQ_VIRTUAL);
    Assert(pVM->tm.s.cTSCTicksPerSecond <= ((uint64_t)UINT32_MAX << 2)); /* <= 15.99 GHz */
    return ASMMultU64ByU32DivByU32(u64VirtualTime, (uint32_t)(pVM->tm.s.cTSCTicksPerSecond >> 2), TMCLOCK_FREQ_VIRTUAL >> 2);
}


/**
 * Gets the raw cpu tick from current virtual time.
 *
 * @param   pVM             The cross context VM structure.
 * @param   fCheckTimers    Whether to check timers.
 */
DECLINLINE(uint64_t) tmCpuTickGetRawVirtual(PVMCC pVM, bool fCheckTimers)
{
    if (fCheckTimers)
        return tmCpuTickCalcFromVirtual(pVM, TMVirtualSyncGet(pVM));
    return tmCpuTickCalcFromVirtual(pVM, TMVirtualSyncGetNoCheck(pVM));
}


#ifdef IN_RING3
/**
 * Used by tmR3CpuTickParavirtEnable and tmR3CpuTickParavirtDisable.
 *
 * @param   pVM     The cross context VM structure.
 */
uint64_t tmR3CpuTickGetRawVirtualNoCheck(PVM pVM)
{
    return tmCpuTickGetRawVirtual(pVM, false /*fCheckTimers*/);
}
#endif


/**
 * Resumes the CPU timestamp counter ticking.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @internal
 */
int tmCpuTickResume(PVMCC pVM, PVMCPUCC pVCpu)
{
    if (!pVCpu->tm.s.fTSCTicking)
    {
        pVCpu->tm.s.fTSCTicking = true;

        /** @todo Test that pausing and resuming doesn't cause lag! (I.e. that we're
         *        unpaused before the virtual time and stopped after it. */
        switch (pVM->tm.s.enmTSCMode)
        {
            case TMTSCMODE_REAL_TSC_OFFSET:
                pVCpu->tm.s.offTSCRawSrc = SUPReadTsc() * pVM->tm.s.u8TSCMultiplier - pVCpu->tm.s.u64TSC;
                break;
            case TMTSCMODE_VIRT_TSC_EMULATED:
            case TMTSCMODE_DYNAMIC:
                pVCpu->tm.s.offTSCRawSrc = tmCpuTickGetRawVirtual(pVM, false /* don't check for pending timers */)
                                         - pVCpu->tm.s.u64TSC;
                break;
            case TMTSCMODE_NATIVE_API:
                pVCpu->tm.s.offTSCRawSrc = 0; /** @todo ?? */
                /* Looks like this is only used by weird modes and MSR TSC writes.  We cannot support either on NEM/win. */
                break;
            default:
                AssertFailedReturn(VERR_IPE_NOT_REACHED_DEFAULT_CASE);
        }
        return VINF_SUCCESS;
    }
    AssertFailed();
    return VERR_TM_TSC_ALREADY_TICKING;
}


/**
 * Resumes the CPU timestamp counter ticking.
 *
 * @returns VINF_SUCCESS or VERR_TM_VIRTUAL_TICKING_IPE (asserted).
 * @param   pVM     The cross context VM structure.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
int tmCpuTickResumeLocked(PVMCC pVM, PVMCPUCC pVCpu)
{
    if (!pVCpu->tm.s.fTSCTicking)
    {
        /* TSC must be ticking before calling tmCpuTickGetRawVirtual()! */
        pVCpu->tm.s.fTSCTicking = true;
        uint32_t c = ASMAtomicIncU32(&pVM->tm.s.cTSCsTicking);
        AssertMsgReturn(c <= pVM->cCpus, ("%u vs %u\n", c, pVM->cCpus), VERR_TM_VIRTUAL_TICKING_IPE);
        if (c == 1)
        {
            /* The first VCPU to resume. */
            uint64_t    offTSCRawSrcOld = pVCpu->tm.s.offTSCRawSrc;

            STAM_COUNTER_INC(&pVM->tm.s.StatTSCResume);

            /* When resuming, use the TSC value of the last stopped VCPU to avoid the TSC going back. */
            switch (pVM->tm.s.enmTSCMode)
            {
                case TMTSCMODE_REAL_TSC_OFFSET:
                    pVCpu->tm.s.offTSCRawSrc = SUPReadTsc() * pVM->tm.s.u8TSCMultiplier - pVM->tm.s.u64LastPausedTSC;
                    break;
                case TMTSCMODE_VIRT_TSC_EMULATED:
                case TMTSCMODE_DYNAMIC:
                    pVCpu->tm.s.offTSCRawSrc = tmCpuTickGetRawVirtual(pVM, false /* don't check for pending timers */)
                                             - pVM->tm.s.u64LastPausedTSC;
                    break;
                case TMTSCMODE_NATIVE_API:
                {
                    int rc = NEMHCResumeCpuTickOnAll(pVM, pVCpu, pVM->tm.s.u64LastPausedTSC);
                    AssertRCReturn(rc, rc);
                    pVCpu->tm.s.offTSCRawSrc = offTSCRawSrcOld = 0;
                    break;
                }
                default:
                    AssertFailedReturn(VERR_IPE_NOT_REACHED_DEFAULT_CASE);
            }

            /* Calculate the offset addendum for other VCPUs to use. */
            pVM->tm.s.offTSCPause = pVCpu->tm.s.offTSCRawSrc - offTSCRawSrcOld;
        }
        else
        {
            /* All other VCPUs (if any). */
            pVCpu->tm.s.offTSCRawSrc += pVM->tm.s.offTSCPause;
        }
    }
    return VINF_SUCCESS;
}


/**
 * Pauses the CPU timestamp counter ticking.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @internal
 */
int tmCpuTickPause(PVMCPUCC pVCpu)
{
    if (pVCpu->tm.s.fTSCTicking)
    {
        pVCpu->tm.s.u64TSC = TMCpuTickGetNoCheck(pVCpu);
        pVCpu->tm.s.fTSCTicking = false;
        return VINF_SUCCESS;
    }
    AssertFailed();
    return VERR_TM_TSC_ALREADY_PAUSED;
}


/**
 * Pauses the CPU timestamp counter ticking.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @internal
 */
int tmCpuTickPauseLocked(PVMCC pVM, PVMCPUCC pVCpu)
{
    if (pVCpu->tm.s.fTSCTicking)
    {
        pVCpu->tm.s.u64TSC = TMCpuTickGetNoCheck(pVCpu);
        pVCpu->tm.s.fTSCTicking = false;

        uint32_t c = ASMAtomicDecU32(&pVM->tm.s.cTSCsTicking);
        AssertMsgReturn(c < pVM->cCpus, ("%u vs %u\n", c, pVM->cCpus), VERR_TM_VIRTUAL_TICKING_IPE);
        if (c == 0)
        {
            /* When the last TSC stops, remember the value. */
            STAM_COUNTER_INC(&pVM->tm.s.StatTSCPause);
            pVM->tm.s.u64LastPausedTSC = pVCpu->tm.s.u64TSC;
        }
        return VINF_SUCCESS;
    }
    AssertFailed();
    return VERR_TM_TSC_ALREADY_PAUSED;
}


#ifdef IN_RING0 /* Only used in ring-0 at present (AMD-V and VT-x). */

# ifdef VBOX_WITH_STATISTICS
/**
 * Record why we refused to use offsetted TSC.
 *
 * Used by TMCpuTickCanUseRealTSC() and TMCpuTickGetDeadlineAndTscOffset().
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 */
DECLINLINE(void) tmCpuTickRecordOffsettedTscRefusal(PVM pVM, PVMCPU pVCpu)
{
    /* Sample the reason for refusing. */
    if (pVM->tm.s.enmTSCMode != TMTSCMODE_DYNAMIC)
       STAM_COUNTER_INC(&pVM->tm.s.StatTSCNotFixed);
    else if (!pVCpu->tm.s.fTSCTicking)
       STAM_COUNTER_INC(&pVM->tm.s.StatTSCNotTicking);
    else if (pVM->tm.s.enmTSCMode != TMTSCMODE_REAL_TSC_OFFSET)
    {
        if (pVM->tm.s.fVirtualSyncCatchUp)
        {
           if (pVM->tm.s.u32VirtualSyncCatchUpPercentage <= 10)
               STAM_COUNTER_INC(&pVM->tm.s.StatTSCCatchupLE010);
           else if (pVM->tm.s.u32VirtualSyncCatchUpPercentage <= 25)
               STAM_COUNTER_INC(&pVM->tm.s.StatTSCCatchupLE025);
           else if (pVM->tm.s.u32VirtualSyncCatchUpPercentage <= 100)
               STAM_COUNTER_INC(&pVM->tm.s.StatTSCCatchupLE100);
           else
               STAM_COUNTER_INC(&pVM->tm.s.StatTSCCatchupOther);
        }
        else if (!pVM->tm.s.fVirtualSyncTicking)
           STAM_COUNTER_INC(&pVM->tm.s.StatTSCSyncNotTicking);
        else if (pVM->tm.s.fVirtualWarpDrive)
           STAM_COUNTER_INC(&pVM->tm.s.StatTSCWarp);
    }
}
# endif /* VBOX_WITH_STATISTICS */

/**
 * Checks if AMD-V / VT-x can use an offsetted hardware TSC or not.
 *
 * @returns true/false accordingly.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   poffRealTsc     The offset against the TSC of the current host CPU,
 *                          if pfOffsettedTsc is set to true.
 * @param   pfParavirtTsc   Where to return whether paravirt TSC is enabled.
 *
 * @thread  EMT(pVCpu).
 * @see     TMCpuTickGetDeadlineAndTscOffset().
 */
VMM_INT_DECL(bool) TMCpuTickCanUseRealTSC(PVMCC pVM, PVMCPUCC pVCpu, uint64_t *poffRealTsc, bool *pfParavirtTsc)
{
    Assert(pVCpu->tm.s.fTSCTicking || DBGFIsStepping(pVCpu));

    *pfParavirtTsc = pVM->tm.s.fParavirtTscEnabled;

    /*
     * In real TSC mode it's easy, we just need the delta & offTscRawSrc and
     * the CPU will add them to RDTSC and RDTSCP at runtime.
     *
     * In tmCpuTickGetInternal we do:
     *          SUPReadTsc() - pVCpu->tm.s.offTSCRawSrc;
     * Where SUPReadTsc() does:
     *          ASMReadTSC() - pGipCpu->i64TscDelta;
     * Which means tmCpuTickGetInternal actually does:
     *          ASMReadTSC() - pGipCpu->i64TscDelta - pVCpu->tm.s.offTSCRawSrc;
     * So, the offset to be ADDED to RDTSC[P] is:
     *          offRealTsc = -(pGipCpu->i64TscDelta + pVCpu->tm.s.offTSCRawSrc)
     */
    if (pVM->tm.s.enmTSCMode == TMTSCMODE_REAL_TSC_OFFSET)
    {
        /** @todo We should negate both deltas!  It's soo weird that we do the
         *        exact opposite of what the hardware implements. */
# ifdef IN_RING3
        *poffRealTsc = (uint64_t)0 - pVCpu->tm.s.offTSCRawSrc - (uint64_t)SUPGetTscDelta(g_pSUPGlobalInfoPage);
# else
        *poffRealTsc = (uint64_t)0 - pVCpu->tm.s.offTSCRawSrc - (uint64_t)SUPGetTscDeltaByCpuSetIndex(pVCpu->iHostCpuSet);
# endif
        return true;
    }

    /*
     * We require:
     *     1. A fixed TSC, this is checked at init time.
     *     2. That the TSC is ticking (we shouldn't be here if it isn't)
     *     3. Either that we're using the real TSC as time source or
     *          a) we don't have any lag to catch up, and
     *          b) the virtual sync clock hasn't been halted by an expired timer, and
     *          c) we're not using warp drive (accelerated virtual guest time).
     */
    if (   pVM->tm.s.enmTSCMode == TMTSCMODE_DYNAMIC
        && !pVM->tm.s.fVirtualSyncCatchUp
        && RT_LIKELY(pVM->tm.s.fVirtualSyncTicking)
        && !pVM->tm.s.fVirtualWarpDrive)
    {
        /* The source is the timer synchronous virtual clock. */
        uint64_t uTscNow;
        uint64_t u64Now = tmCpuTickCalcFromVirtual(pVM, TMVirtualSyncGetNoCheckWithTsc(pVM, &uTscNow))
                        - pVCpu->tm.s.offTSCRawSrc;
        /** @todo When we start collecting statistics on how much time we spend executing
         * guest code before exiting, we should check this against the next virtual sync
         * timer timeout. If it's lower than the avg. length, we should trap rdtsc to increase
         * the chance that we'll get interrupted right after the timer expired. */
        if (u64Now >= pVCpu->tm.s.u64TSCLastSeen)
        {
# ifdef IN_RING3
            *poffRealTsc = u64Now - (uTscNow + (uint64_t)SUPGetTscDelta(g_pSUPGlobalInfoPage);
# else
            *poffRealTsc = u64Now - (uTscNow + (uint64_t)SUPGetTscDeltaByCpuSetIndex(pVCpu->iHostCpuSet));
# endif
            return true;    /** @todo count this? */
        }
    }

# ifdef VBOX_WITH_STATISTICS
    tmCpuTickRecordOffsettedTscRefusal(pVM, pVCpu);
# endif
    return false;
}


/**
 * Calculates the number of host CPU ticks till the next virtual sync deadline.
 *
 * @note    To save work, this function will not bother calculating the accurate
 *          tick count for deadlines that are more than a second ahead.
 *
 * @returns The number of host cpu ticks to the next deadline.  Max one second.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   cNsToDeadline   The number of nano seconds to the next virtual
 *                          sync deadline.
 */
DECLINLINE(uint64_t) tmCpuCalcTicksToDeadline(PVMCPUCC pVCpu, uint64_t cNsToDeadline)
{
    AssertCompile(TMCLOCK_FREQ_VIRTUAL <= _4G);
# ifdef IN_RING3
    RT_NOREF_PV(pVCpu);
    PSUPGIP const pGip = g_pSUPGlobalInfoPage;
    uint64_t uCpuHz = pGip ? SUPGetCpuHzFromGip(pGip) : pVCpu->pVMR3->tm.s.cTSCTicksPerSecondHost;
# else
    uint64_t uCpuHz = SUPGetCpuHzFromGipBySetIndex(g_pSUPGlobalInfoPage, pVCpu->iHostCpuSet);
# endif
    if (RT_UNLIKELY(cNsToDeadline >= TMCLOCK_FREQ_VIRTUAL))
        return uCpuHz;
    AssertCompile(TMCLOCK_FREQ_VIRTUAL <= UINT32_MAX);
    uint64_t cTicks = ASMMultU64ByU32DivByU32(uCpuHz, (uint32_t)cNsToDeadline, TMCLOCK_FREQ_VIRTUAL);
    if (cTicks > 4000)
        cTicks -= 4000; /* fudge to account for overhead */
    else
        cTicks >>= 1;
    return cTicks;
}


/**
 * Gets the next deadline in host CPU clock ticks and the TSC offset if we can
 * use the raw TSC.
 *
 * @returns The number of host CPU clock ticks to the next timer deadline.
 * @param   pVM                 The cross context VM structure.
 * @param   pVCpu               The cross context virtual CPU structure of the calling EMT.
 * @param   poffRealTsc         The offset against the TSC of the current host CPU,
 *                              if pfOffsettedTsc is set to true.
 * @param   pfOffsettedTsc      Where to return whether TSC offsetting can be used.
 * @param   pfParavirtTsc       Where to return whether paravirt TSC is enabled.
 * @param   puTscNow            Where to return the TSC value that the return
 *                              value is relative to.   This is delta adjusted.
 * @param   puDeadlineVersion   Where to return the deadline "version" number.
 *                              Use with TMVirtualSyncIsCurrentDeadlineVersion()
 *                              to check if the absolute deadline is still up to
 *                              date and the caller can skip calling this
 *                              function.
 *
 * @thread  EMT(pVCpu).
 * @see     TMCpuTickCanUseRealTSC().
 */
VMM_INT_DECL(uint64_t) TMCpuTickGetDeadlineAndTscOffset(PVMCC pVM, PVMCPUCC pVCpu, uint64_t *poffRealTsc,
                                                        bool *pfOffsettedTsc, bool *pfParavirtTsc,
                                                        uint64_t *puTscNow, uint64_t *puDeadlineVersion)
{
    Assert(pVCpu->tm.s.fTSCTicking || DBGFIsStepping(pVCpu));

    *pfParavirtTsc = pVM->tm.s.fParavirtTscEnabled;

    /*
     * Same logic as in TMCpuTickCanUseRealTSC.
     */
    if (pVM->tm.s.enmTSCMode == TMTSCMODE_REAL_TSC_OFFSET)
    {
        /** @todo We should negate both deltas!  It's soo weird that we do the
         *        exact opposite of what the hardware implements. */
# ifdef IN_RING3
        *poffRealTsc     = (uint64_t)0 - pVCpu->tm.s.offTSCRawSrc - (uint64_t)SUPGetTscDelta(g_pSUPGlobalInfoPage);
# else
        *poffRealTsc     = (uint64_t)0 - pVCpu->tm.s.offTSCRawSrc - (uint64_t)SUPGetTscDeltaByCpuSetIndex(pVCpu->iHostCpuSet);
# endif
        *pfOffsettedTsc  = true;
        return tmCpuCalcTicksToDeadline(pVCpu, TMVirtualSyncGetNsToDeadline(pVM, puDeadlineVersion, puTscNow));
    }

    /*
     * Same logic as in TMCpuTickCanUseRealTSC.
     */
    if (   pVM->tm.s.enmTSCMode == TMTSCMODE_DYNAMIC
        && !pVM->tm.s.fVirtualSyncCatchUp
        && RT_LIKELY(pVM->tm.s.fVirtualSyncTicking)
        && !pVM->tm.s.fVirtualWarpDrive)
    {
        /* The source is the timer synchronous virtual clock. */
        uint64_t cNsToDeadline;
        uint64_t u64NowVirtSync = TMVirtualSyncGetWithDeadlineNoCheck(pVM, &cNsToDeadline, puDeadlineVersion, puTscNow);
        uint64_t u64Now = tmCpuTickCalcFromVirtual(pVM, u64NowVirtSync);
        u64Now -= pVCpu->tm.s.offTSCRawSrc;

# ifdef IN_RING3
        *poffRealTsc     = u64Now - (*puTscNow + (uint64_t)SUPGetTscDelta(g_pSUPGlobalInfoPage)); /* undoing delta */
# else
        *poffRealTsc     = u64Now - (*puTscNow + (uint64_t)SUPGetTscDeltaByCpuSetIndex(pVCpu->iHostCpuSet)); /* undoing delta */
# endif
        *pfOffsettedTsc  = u64Now >= pVCpu->tm.s.u64TSCLastSeen;
        return tmCpuCalcTicksToDeadline(pVCpu, cNsToDeadline);
    }

# ifdef VBOX_WITH_STATISTICS
    tmCpuTickRecordOffsettedTscRefusal(pVM, pVCpu);
# endif
    *pfOffsettedTsc  = false;
    *poffRealTsc     = 0;
    return tmCpuCalcTicksToDeadline(pVCpu, TMVirtualSyncGetNsToDeadline(pVM, puDeadlineVersion, puTscNow));
}

#endif /* IN_RING0 - at the moment */

/**
 * Read the current CPU timestamp counter.
 *
 * @returns Gets the CPU tsc.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   fCheckTimers    Whether to check timers.
 */
DECLINLINE(uint64_t) tmCpuTickGetInternal(PVMCPUCC pVCpu, bool fCheckTimers)
{
    uint64_t u64;

    if (RT_LIKELY(pVCpu->tm.s.fTSCTicking))
    {
        PVMCC pVM = pVCpu->CTX_SUFF(pVM);
        switch (pVM->tm.s.enmTSCMode)
        {
            case TMTSCMODE_REAL_TSC_OFFSET:
                u64 = SUPReadTsc() * pVM->tm.s.u8TSCMultiplier;
                break;
            case TMTSCMODE_VIRT_TSC_EMULATED:
            case TMTSCMODE_DYNAMIC:
                u64 = tmCpuTickGetRawVirtual(pVM, fCheckTimers);
                break;
            case TMTSCMODE_NATIVE_API:
            {
                u64 = 0;
                int rcNem = NEMHCQueryCpuTick(pVCpu, &u64, NULL);
                AssertLogRelRCReturn(rcNem, SUPReadTsc());
                break;
            }
            default:
                AssertFailedBreakStmt(u64 = SUPReadTsc());
        }
        u64 -= pVCpu->tm.s.offTSCRawSrc;

        /* Always return a value higher than what the guest has already seen. */
        if (RT_LIKELY(u64 > pVCpu->tm.s.u64TSCLastSeen))
            pVCpu->tm.s.u64TSCLastSeen = u64;
        else
        {
            STAM_COUNTER_INC(&pVM->tm.s.StatTSCUnderflow);
            pVCpu->tm.s.u64TSCLastSeen += 64;   /** @todo choose a good increment here */
            u64 = pVCpu->tm.s.u64TSCLastSeen;
        }
    }
    else
        u64 = pVCpu->tm.s.u64TSC;
    return u64;
}


/**
 * Read the current CPU timestamp counter.
 *
 * @returns Gets the CPU tsc.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMDECL(uint64_t) TMCpuTickGet(PVMCPUCC pVCpu)
{
    return tmCpuTickGetInternal(pVCpu, true /* fCheckTimers */);
}


/**
 * Read the current CPU timestamp counter, don't check for expired timers.
 *
 * @returns Gets the CPU tsc.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMM_INT_DECL(uint64_t) TMCpuTickGetNoCheck(PVMCPUCC pVCpu)
{
    return tmCpuTickGetInternal(pVCpu, false /* fCheckTimers */);
}


/**
 * Sets the current CPU timestamp counter.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   u64Tick     The new timestamp value.
 *
 * @thread  EMT which TSC is to be set.
 */
VMM_INT_DECL(int) TMCpuTickSet(PVMCC pVM, PVMCPUCC pVCpu, uint64_t u64Tick)
{
    VMCPU_ASSERT_EMT(pVCpu);
    STAM_COUNTER_INC(&pVM->tm.s.StatTSCSet);

    /*
     * This is easier to do when the TSC is paused since resume will
     * do all the calculations for us. Actually, we don't need to
     * call tmCpuTickPause here since we overwrite u64TSC anyway.
     */
    bool        fTSCTicking    = pVCpu->tm.s.fTSCTicking;
    pVCpu->tm.s.fTSCTicking    = false;
    pVCpu->tm.s.u64TSC         = u64Tick;
    pVCpu->tm.s.u64TSCLastSeen = u64Tick;
    if (fTSCTicking)
        tmCpuTickResume(pVM, pVCpu);
    /** @todo Try help synchronizing it better among the virtual CPUs? */

    return VINF_SUCCESS;
}

/**
 * Sets the last seen CPU timestamp counter.
 *
 * @returns VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   u64LastSeenTick     The last seen timestamp value.
 *
 * @thread  EMT which TSC is to be set.
 */
VMM_INT_DECL(int) TMCpuTickSetLastSeen(PVMCPUCC pVCpu, uint64_t u64LastSeenTick)
{
    VMCPU_ASSERT_EMT(pVCpu);

    LogFlow(("TMCpuTickSetLastSeen %RX64\n", u64LastSeenTick));
    /** @todo deal with wraparound!   */
    if (pVCpu->tm.s.u64TSCLastSeen < u64LastSeenTick)
        pVCpu->tm.s.u64TSCLastSeen = u64LastSeenTick;
    return VINF_SUCCESS;
}

/**
 * Gets the last seen CPU timestamp counter of the guest.
 *
 * @returns the last seen TSC.
 * @param   pVCpu               The cross context virtual CPU structure.
 *
 * @thread  EMT(pVCpu).
 */
VMM_INT_DECL(uint64_t) TMCpuTickGetLastSeen(PVMCPUCC pVCpu)
{
    VMCPU_ASSERT_EMT(pVCpu);

    return pVCpu->tm.s.u64TSCLastSeen;
}


/**
 * Get the timestamp frequency.
 *
 * @returns Number of ticks per second.
 * @param   pVM     The cross context VM structure.
 */
VMMDECL(uint64_t) TMCpuTicksPerSecond(PVMCC pVM)
{
    if (pVM->tm.s.enmTSCMode == TMTSCMODE_REAL_TSC_OFFSET)
    {
        PSUPGLOBALINFOPAGE const pGip = g_pSUPGlobalInfoPage;
        if (pGip && pGip->u32Mode != SUPGIPMODE_INVARIANT_TSC)
        {
#ifdef IN_RING3
            uint64_t cTSCTicksPerSecond = SUPGetCpuHzFromGip(pGip);
#elif defined(IN_RING0)
            uint64_t cTSCTicksPerSecond = SUPGetCpuHzFromGipBySetIndex(pGip, (uint32_t)RTMpCpuIdToSetIndex(RTMpCpuId()));
#else
            uint64_t cTSCTicksPerSecond = SUPGetCpuHzFromGipBySetIndex(pGip, VMMGetCpu(pVM)->iHostCpuSet);
#endif
            if (RT_LIKELY(cTSCTicksPerSecond != ~(uint64_t)0))
                return cTSCTicksPerSecond * pVM->tm.s.u8TSCMultiplier;
        }
    }
    return pVM->tm.s.cTSCTicksPerSecond;
}


/**
 * Whether the TSC is ticking for the VCPU.
 *
 * @returns true if ticking, false otherwise.
 * @param   pVCpu           The cross context virtual CPU structure.
 */
VMM_INT_DECL(bool) TMCpuTickIsTicking(PVMCPUCC pVCpu)
{
    return pVCpu->tm.s.fTSCTicking;
}


#if defined(VBOX_VMM_TARGET_ARMV8)
/**
 * Sets the number of nanoseconds from now when the vTiemr is supposed to expire next.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   cNanoSecs       Number of nano seconds when the timer is supposed to fire.
 *
 * @note This is required in order to kick the vCPU out of a halting state because the
 *       vTimer is supposed to fire an interrupt.
 */
VMM_INT_DECL(void) TMCpuSetVTimerNextActivation(PVMCPUCC pVCpu, uint64_t cNanoSecs)
{
    pVCpu->cNsVTimerActivate = cNanoSecs;
}


/**
 * Returns when the vTimer is supposed to expire next in number of nanoseconds.
 *
 * @returns Number of nanoseconds when the vTimer is supposed to activate.
 * @retval  UINT64_MAX if the timer is not active.
 * @param   pVCpu           The cross context virtual CPU structure.
 */
VMM_INT_DECL(uint64_t) TMCpuGetVTimerActivationNano(PVMCPUCC pVCpu)
{
    return pVCpu->cNsVTimerActivate;
}
#endif
