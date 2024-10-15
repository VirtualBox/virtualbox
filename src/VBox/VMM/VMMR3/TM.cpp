/* $Id: TM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * TM - Time Manager.
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

/** @page pg_tm        TM - The Time Manager
 *
 * The Time Manager abstracts the CPU clocks and manages timers used by the VMM,
 * device and drivers.
 *
 * @see grp_tm
 *
 *
 * @section sec_tm_clocks   Clocks
 *
 * There are currently 4 clocks:
 *   - Virtual (guest).
 *   - Synchronous virtual (guest).
 *   - CPU Tick (TSC) (guest).  Only current use is rdtsc emulation. Usually a
 *     function of the virtual clock.
 *   - Real (host).  This is only used for display updates atm.
 *
 * The most important clocks are the three first ones and of these the second is
 * the most interesting.
 *
 *
 * The synchronous virtual clock is tied to the virtual clock except that it
 * will take into account timer delivery lag caused by host scheduling.  It will
 * normally never advance beyond the head timer, and when lagging too far behind
 * it will gradually speed up to catch up with the virtual clock.  All devices
 * implementing time sources accessible to and used by the guest is using this
 * clock (for timers and other things).  This ensures consistency between the
 * time sources.
 *
 * The virtual clock is implemented as an offset to a monotonic, high
 * resolution, wall clock.  The current time source is using the RTTimeNanoTS()
 * machinery based upon the Global Info Pages (GIP), that is, we're using TSC
 * deltas (usually 10 ms) to fill the gaps between GIP updates.  The result is
 * a fairly high res clock that works in all contexts and on all hosts.  The
 * virtual clock is paused when the VM isn't in the running state.
 *
 * The CPU tick (TSC) is normally virtualized as a function of the synchronous
 * virtual clock, where the frequency defaults to the host cpu frequency (as we
 * measure it). In this mode it is possible to configure the frequency. Another
 * (non-default) option is to use the raw unmodified host TSC values. And yet
 * another, to tie it to time spent executing guest code.  All these things are
 * configurable should non-default behavior be desirable.
 *
 * The real clock is a monotonic clock (when available) with relatively low
 * resolution, though this a bit host specific.  Note that we're currently not
 * servicing timers using the real clock when the VM is not running, this is
 * simply because it has not been needed yet therefore not implemented.
 *
 *
 * @subsection subsec_tm_timesync Guest Time Sync / UTC time
 *
 * Guest time syncing is primarily taken care of by the VMM device.  The
 * principle is very simple, the guest additions periodically asks the VMM
 * device what the current UTC time is and makes adjustments accordingly.
 *
 * A complicating factor is that the synchronous virtual clock might be doing
 * catchups and the guest perception is currently a little bit behind the world
 * but it will (hopefully) be catching up soon as we're feeding timer interrupts
 * at a slightly higher rate.  Adjusting the guest clock to the current wall
 * time in the real world would be a bad idea then because the guest will be
 * advancing too fast and run ahead of world time (if the catchup works out).
 * To solve this problem TM provides the VMM device with an UTC time source that
 * gets adjusted with the current lag, so that when the guest eventually catches
 * up the lag it will be showing correct real world time.
 *
 *
 * @section sec_tm_timers   Timers
 *
 * The timers can use any of the TM clocks described in the previous section.
 * Each clock has its own scheduling facility, or timer queue if you like.
 * There are a few factors which makes it a bit complex.  First, there is the
 * usual R0 vs R3 vs. RC thing.  Then there are multiple threads, and then there
 * is the timer thread that periodically checks whether any timers has expired
 * without EMT noticing.  On the API level, all but the create and save APIs
 * must be multithreaded.  EMT will always run the timers.
 *
 * The design is using a doubly linked list of active timers which is ordered
 * by expire date.  This list is only modified by the EMT thread.  Updates to
 * the list are batched in a singly linked list, which is then processed by the
 * EMT thread at the first opportunity (immediately, next time EMT modifies a
 * timer on that clock, or next timer timeout).  Both lists are offset based and
 * all the elements are therefore allocated from the hyper heap.
 *
 * For figuring out when there is need to schedule and run timers TM will:
 *    - Poll whenever somebody queries the virtual clock.
 *    - Poll the virtual clocks from the EM and REM loops.
 *    - Poll the virtual clocks from trap exit path.
 *    - Poll the virtual clocks and calculate first timeout from the halt loop.
 *    - Employ a thread which periodically (100Hz) polls all the timer queues.
 *
 *
 * @image html TMTIMER-Statechart-Diagram.gif
 *
 * @section sec_tm_timer    Logging
 *
 * Level 2: Logs a most of the timer state transitions and queue servicing.
 * Level 3: Logs a few oddments.
 * Level 4: Logs TMCLOCK_VIRTUAL_SYNC catch-up events.
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_TM
#ifdef DEBUG_bird
# define DBGFTRACE_DISABLED /* annoying */
#endif
#include <VBox/vmm/tm.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/nem.h>
#include <VBox/vmm/gim.h>
#include <VBox/vmm/ssm.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/dbgftrace.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/iom.h>
#include "TMInternal.h"
#include <VBox/vmm/vm.h>
#include <VBox/vmm/uvm.h>

#include <VBox/vmm/pdmdev.h>
#include <VBox/log.h>
#include <VBox/param.h>
#include <VBox/err.h>

#include <iprt/asm.h>
#include <iprt/asm-math.h>
#include <iprt/assert.h>
#include <iprt/env.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/mem.h>
#include <iprt/rand.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/time.h>
#include <iprt/timer.h>

#include "TMInline.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The current saved state version.*/
#define TM_SAVED_STATE_VERSION  3


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static bool                 tmR3HasFixedTSC(PVM pVM);
static uint64_t             tmR3CalibrateTSC(void);
static DECLCALLBACK(int)    tmR3Save(PVM pVM, PSSMHANDLE pSSM);
static DECLCALLBACK(int)    tmR3Load(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass);
static DECLCALLBACK(int)    tmR3LoadDone(PVM pVM, PSSMHANDLE pSSM);
#ifdef VBOX_WITH_STATISTICS
static void                 tmR3TimerQueueRegisterStats(PVM pVM, PTMTIMERQUEUE pQueue, uint32_t cTimers);
#endif
static DECLCALLBACK(void)   tmR3TimerCallback(PRTTIMER pTimer, void *pvUser, uint64_t iTick);
static DECLCALLBACK(int)    tmR3SetWarpDrive(PUVM pUVM, uint32_t u32Percent);
#ifndef VBOX_WITHOUT_NS_ACCOUNTING
static DECLCALLBACK(void)   tmR3CpuLoadTimer(PVM pVM, TMTIMERHANDLE hTimer, void *pvUser);
#endif
static DECLCALLBACK(void)   tmR3TimerInfo(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
static DECLCALLBACK(void)   tmR3TimerInfoActive(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
static DECLCALLBACK(void)   tmR3InfoClocks(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
static DECLCALLBACK(void)   tmR3InfoCpuLoad(PVM pVM, PCDBGFINFOHLP pHlp, int cArgs, char **papszArgs);
static DECLCALLBACK(VBOXSTRICTRC) tmR3CpuTickParavirtDisable(PVM pVM, PVMCPU pVCpu, void *pvData);
static const char          *tmR3GetTSCModeName(PVM pVM);
static const char          *tmR3GetTSCModeNameEx(TMTSCMODE enmMode);
static int                  tmR3TimerQueueGrow(PVM pVM, PTMTIMERQUEUE pQueue, uint32_t cNewTimers);


/**
 * Initializes the TM.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMM_INT_DECL(int) TMR3Init(PVM pVM)
{
    LogFlow(("TMR3Init:\n"));

    /*
     * Assert alignment and sizes.
     */
    AssertCompileMemberAlignment(VM, tm.s, 32);
    AssertCompile(sizeof(pVM->tm.s) <= sizeof(pVM->tm.padding));
    AssertCompileMemberAlignment(TM, VirtualSyncLock, 8);

    /*
     * Init the structure.
     */
    pVM->tm.s.idTimerCpu = pVM->cCpus - 1; /* The last CPU. */

    int rc = PDMR3CritSectInit(pVM, &pVM->tm.s.VirtualSyncLock, RT_SRC_POS, "TM VirtualSync Lock");
    AssertLogRelRCReturn(rc, rc);

    strcpy(pVM->tm.s.aTimerQueues[TMCLOCK_VIRTUAL].szName,      "virtual");
    strcpy(pVM->tm.s.aTimerQueues[TMCLOCK_VIRTUAL_SYNC].szName, "virtual_sync"); /* Underscore is for STAM ordering issue. */
    strcpy(pVM->tm.s.aTimerQueues[TMCLOCK_REAL].szName,         "real");
    strcpy(pVM->tm.s.aTimerQueues[TMCLOCK_TSC].szName,          "tsc");

    for (uint32_t i = 0; i < RT_ELEMENTS(pVM->tm.s.aTimerQueues); i++)
    {
        Assert(pVM->tm.s.aTimerQueues[i].szName[0] != '\0');
        pVM->tm.s.aTimerQueues[i].enmClock          = (TMCLOCK)i;
        pVM->tm.s.aTimerQueues[i].u64Expire         = INT64_MAX;
        pVM->tm.s.aTimerQueues[i].idxActive         = UINT32_MAX;
        pVM->tm.s.aTimerQueues[i].idxSchedule       = UINT32_MAX;
        pVM->tm.s.aTimerQueues[i].idxFreeHint       = 1;
        pVM->tm.s.aTimerQueues[i].fBeingProcessed   = false;
        pVM->tm.s.aTimerQueues[i].fCannotGrow       = false;
        pVM->tm.s.aTimerQueues[i].hThread           = NIL_RTTHREAD;
        pVM->tm.s.aTimerQueues[i].hWorkerEvt        = NIL_SUPSEMEVENT;

        rc = PDMR3CritSectInit(pVM, &pVM->tm.s.aTimerQueues[i].TimerLock, RT_SRC_POS,
                               "TM %s queue timer lock", pVM->tm.s.aTimerQueues[i].szName);
        AssertLogRelRCReturn(rc, rc);

        rc = PDMR3CritSectRwInit(pVM, &pVM->tm.s.aTimerQueues[i].AllocLock, RT_SRC_POS,
                                 "TM %s queue alloc lock", pVM->tm.s.aTimerQueues[i].szName);
        AssertLogRelRCReturn(rc, rc);
    }

    /*
     * We directly use the GIP to calculate the virtual time. We map the
     * the GIP into the guest context so we can do this calculation there
     * as well and save costly world switches.
     */
    PSUPGLOBALINFOPAGE pGip = g_pSUPGlobalInfoPage;
    if (pGip || !SUPR3IsDriverless())
    {
        pVM->tm.s.pvGIPR3 = (void *)pGip;
        AssertMsgReturn(pVM->tm.s.pvGIPR3, ("GIP support is now required!\n"), VERR_TM_GIP_REQUIRED);
        AssertMsgReturn((pGip->u32Version >> 16) == (SUPGLOBALINFOPAGE_VERSION >> 16),
                        ("Unsupported GIP version %#x! (expected=%#x)\n", pGip->u32Version, SUPGLOBALINFOPAGE_VERSION),
                        VERR_TM_GIP_VERSION);

        /* Check assumptions made in TMAllVirtual.cpp about the GIP update interval. */
        if (    pGip->u32Magic == SUPGLOBALINFOPAGE_MAGIC
            &&  pGip->u32UpdateIntervalNS >= 250000000 /* 0.25s */)
            return VMSetError(pVM, VERR_TM_GIP_UPDATE_INTERVAL_TOO_BIG, RT_SRC_POS,
                              N_("The GIP update interval is too big. u32UpdateIntervalNS=%RU32 (u32UpdateHz=%RU32)"),
                              pGip->u32UpdateIntervalNS, pGip->u32UpdateHz);

        /* Log GIP info that may come in handy. */
        LogRel(("TM: GIP - u32Mode=%d (%s) u32UpdateHz=%u u32UpdateIntervalNS=%u enmUseTscDelta=%d (%s) fGetGipCpu=%#x cCpus=%d\n",
                pGip->u32Mode, SUPGetGIPModeName(pGip), pGip->u32UpdateHz, pGip->u32UpdateIntervalNS,
                pGip->enmUseTscDelta, SUPGetGIPTscDeltaModeName(pGip), pGip->fGetGipCpu, pGip->cCpus));
        LogRel(("TM: GIP - u64CpuHz=%'RU64 (%#RX64)  SUPGetCpuHzFromGip => %'RU64\n",
                pGip->u64CpuHz, pGip->u64CpuHz, SUPGetCpuHzFromGip(pGip)));
        for (uint32_t iCpuSet = 0; iCpuSet < RT_ELEMENTS(pGip->aiCpuFromCpuSetIdx); iCpuSet++)
        {
            uint16_t iGipCpu = pGip->aiCpuFromCpuSetIdx[iCpuSet];
            if (iGipCpu != UINT16_MAX)
                LogRel(("TM: GIP - CPU: iCpuSet=%#x idCpu=%#x idApic=%#x iGipCpu=%#x i64TSCDelta=%RI64 enmState=%d u64CpuHz=%RU64(*) cErrors=%u\n",
                        iCpuSet, pGip->aCPUs[iGipCpu].idCpu, pGip->aCPUs[iGipCpu].idApic, iGipCpu, pGip->aCPUs[iGipCpu].i64TSCDelta,
                        pGip->aCPUs[iGipCpu].enmState, pGip->aCPUs[iGipCpu].u64CpuHz, pGip->aCPUs[iGipCpu].cErrors));
        }
    }

    /*
     * Setup the VirtualGetRaw backend.
     */
    pVM->tm.s.pfnVirtualGetRaw                 = tmVirtualNanoTSRediscover;
    pVM->tm.s.VirtualGetRawData.pfnRediscover  = tmVirtualNanoTSRediscover;
    pVM->tm.s.VirtualGetRawData.pfnBad         = tmVirtualNanoTSBad;
    pVM->tm.s.VirtualGetRawData.pfnBadCpuIndex = tmVirtualNanoTSBadCpuIndex;
    pVM->tm.s.VirtualGetRawData.pu64Prev       = &pVM->tm.s.u64VirtualRawPrev;

    /*
     * Get our CFGM node, create it if necessary.
     */
    PCFGMNODE pCfgHandle = CFGMR3GetChild(CFGMR3GetRoot(pVM), "TM");
    if (!pCfgHandle)
    {
        rc = CFGMR3InsertNode(CFGMR3GetRoot(pVM), "TM", &pCfgHandle);
        AssertRCReturn(rc, rc);
    }

    /*
     * Specific errors about some obsolete TM settings (remove after 2015-12-03).
     */
    if (CFGMR3Exists(pCfgHandle, "TSCVirtualized"))
        return VMSetError(pVM, VERR_CFGM_CONFIG_UNKNOWN_VALUE, RT_SRC_POS,
                          N_("Configuration error: TM setting \"TSCVirtualized\" is no longer supported. Use the \"TSCMode\" setting instead."));
    if (CFGMR3Exists(pCfgHandle, "UseRealTSC"))
        return VMSetError(pVM, VERR_CFGM_CONFIG_UNKNOWN_VALUE, RT_SRC_POS,
                          N_("Configuration error: TM setting \"UseRealTSC\" is no longer supported. Use the \"TSCMode\" setting instead."));

    if (CFGMR3Exists(pCfgHandle, "MaybeUseOffsettedHostTSC"))
        return VMSetError(pVM, VERR_CFGM_CONFIG_UNKNOWN_VALUE, RT_SRC_POS,
                          N_("Configuration error: TM setting \"MaybeUseOffsettedHostTSC\" is no longer supported. Use the \"TSCMode\" setting instead."));

    /*
     * Validate the rest of the TM settings.
     */
    rc = CFGMR3ValidateConfig(pCfgHandle, "/TM/",
                              "TSCMode|"
                              "TSCModeSwitchAllowed|"
                              "TSCTicksPerSecond|"
                              "TSCTiedToExecution|"
                              "TSCNotTiedToHalt|"
                              "TSCMultiplier|"
                              "ScheduleSlack|"
                              "CatchUpStopThreshold|"
                              "CatchUpGiveUpThreshold|"
                              "CatchUpStartThreshold0|CatchUpStartThreshold1|CatchUpStartThreshold2|CatchUpStartThreshold3|"
                              "CatchUpStartThreshold4|CatchUpStartThreshold5|CatchUpStartThreshold6|CatchUpStartThreshold7|"
                              "CatchUpStartThreshold8|CatchUpStartThreshold9|"
                              "CatchUpPrecentage0|CatchUpPrecentage1|CatchUpPrecentage2|CatchUpPrecentage3|"
                              "CatchUpPrecentage4|CatchUpPrecentage5|CatchUpPrecentage6|CatchUpPrecentage7|"
                              "CatchUpPrecentage8|CatchUpPrecentage9|"
                              "UTCOffset|"
                              "UTCTouchFileOnJump|"
                              "WarpDrivePercentage|"
                              "HostHzMax|"
                              "HostHzFudgeFactorTimerCpu|"
                              "HostHzFudgeFactorOtherCpu|"
                              "HostHzFudgeFactorCatchUp100|"
                              "HostHzFudgeFactorCatchUp200|"
                              "HostHzFudgeFactorCatchUp400|"
                              "TimerMillies"
                              ,
                              "",
                              "TM", 0);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Determine the TSC configuration and frequency.
     */
    /** @cfgm{/TM/TSCMode, string, Depends on the CPU and VM config}
     * The name of the TSC mode to use: VirtTSCEmulated, RealTSCOffset or Dynamic.
     * The default depends on the VM configuration and the capabilities of the
     * host CPU.  Other config options or runtime changes may override the TSC
     * mode specified here.
     */
    char szTSCMode[32];
    rc = CFGMR3QueryString(pCfgHandle, "TSCMode", szTSCMode, sizeof(szTSCMode));
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
    {
        /** @todo Rainy-day/never: Dynamic mode isn't currently suitable for SMP VMs, so
         * fall back on the more expensive emulated mode. With the current TSC handling
         * (frequent switching between offsetted mode and taking VM exits, on all VCPUs
         * without any kind of coordination) will lead to inconsistent TSC behavior with
         * guest SMP, including TSC going backwards. */
        pVM->tm.s.enmTSCMode = NEMR3NeedSpecialTscMode(pVM) ? TMTSCMODE_NATIVE_API
                             : pVM->cCpus == 1 && tmR3HasFixedTSC(pVM) ? TMTSCMODE_DYNAMIC : TMTSCMODE_VIRT_TSC_EMULATED;
    }
    else if (RT_FAILURE(rc))
        return VMSetError(pVM, rc, RT_SRC_POS, N_("Configuration error: Failed to querying string value \"TSCMode\""));
    else
    {
        if (!RTStrCmp(szTSCMode, "VirtTSCEmulated"))
            pVM->tm.s.enmTSCMode = TMTSCMODE_VIRT_TSC_EMULATED;
        else if (!RTStrCmp(szTSCMode, "RealTSCOffset"))
            pVM->tm.s.enmTSCMode = TMTSCMODE_REAL_TSC_OFFSET;
        else if (!RTStrCmp(szTSCMode, "Dynamic"))
            pVM->tm.s.enmTSCMode = TMTSCMODE_DYNAMIC;
        else
            return VMSetError(pVM, rc, RT_SRC_POS, N_("Configuration error: Unrecognized TM TSC mode value \"%s\""), szTSCMode);
        if (NEMR3NeedSpecialTscMode(pVM))
        {
            LogRel(("TM: NEM overrides the /TM/TSCMode=%s settings.\n", szTSCMode));
            pVM->tm.s.enmTSCMode = TMTSCMODE_NATIVE_API;
        }
    }

    /**
     * @cfgm{/TM/TSCModeSwitchAllowed, bool, Whether TM TSC mode switch is allowed
     *      at runtime}
     * When using paravirtualized guests, we dynamically switch TSC modes to a more
     * optimal one for performance. This setting allows overriding this behaviour.
     */
    rc = CFGMR3QueryBool(pCfgHandle, "TSCModeSwitchAllowed", &pVM->tm.s.fTSCModeSwitchAllowed);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
    {
        /* This is finally determined in TMR3InitFinalize() as GIM isn't initialized yet. */
        pVM->tm.s.fTSCModeSwitchAllowed = true;
    }
    else if (RT_FAILURE(rc))
        return VMSetError(pVM, rc, RT_SRC_POS, N_("Configuration error: Failed to querying bool value \"TSCModeSwitchAllowed\""));
    if (pVM->tm.s.fTSCModeSwitchAllowed && pVM->tm.s.enmTSCMode == TMTSCMODE_NATIVE_API)
    {
        LogRel(("TM: NEM overrides the /TM/TSCModeSwitchAllowed setting.\n"));
        pVM->tm.s.fTSCModeSwitchAllowed = false;
    }

    /** @cfgm{/TM/TSCMultiplier, uint8_t}
     * This is a multiplier to apply to the host TSC while calculating the guest
     * TSC.  It's recommended to avoid using a power-of-two value to reduce number
     * of zeros in least-significant-bits of the scaled TSC.  Defaults to 43 on
     * ARM64 and 1 on all other hosts. */
#ifdef RT_ARCH_ARM64
    pVM->tm.s.u8TSCMultiplier = 43; /* 125/3 + some fudge to get us >= 1GHz from 24MHz */
#else
    pVM->tm.s.u8TSCMultiplier = 1;
#endif
    rc = CFGMR3QueryU8Def(pCfgHandle, "TSCMultiplier", &pVM->tm.s.u8TSCMultiplier, pVM->tm.s.u8TSCMultiplier);
    if (RT_FAILURE(rc))
        return VMSetError(pVM, rc, RT_SRC_POS,
                          N_("Configuration error: Failed to query 8-bit value \"TSCMultiplier\""));
    if (pVM->tm.s.u8TSCMultiplier == 0)
        return VMSetError(pVM, rc, RT_SRC_POS, N_("Configuration error: \"TSCMultiplier\" must not be zero!"));

    /** @cfgm{/TM/TSCTicksPerSecond, uint32_t, Current TSC frequency from GIP}
     * The number of TSC ticks per second (i.e. the TSC frequency). This will
     * override enmTSCMode.
     */
    pVM->tm.s.cTSCTicksPerSecondHost = tmR3CalibrateTSC();
    rc = CFGMR3QueryU64(pCfgHandle, "TSCTicksPerSecond", &pVM->tm.s.cTSCTicksPerSecond);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
    {
        pVM->tm.s.cTSCTicksPerSecond = pVM->tm.s.cTSCTicksPerSecondHost * pVM->tm.s.u8TSCMultiplier;
        if (   (   pVM->tm.s.enmTSCMode == TMTSCMODE_DYNAMIC
                || pVM->tm.s.enmTSCMode == TMTSCMODE_VIRT_TSC_EMULATED)
            && pVM->tm.s.cTSCTicksPerSecond >= _4G)
        {
            pVM->tm.s.cTSCTicksPerSecond = _4G - 1; /* (A limitation of our math code) */
            pVM->tm.s.enmTSCMode = TMTSCMODE_VIRT_TSC_EMULATED;
        }
    }
    else if (RT_FAILURE(rc))
        return VMSetError(pVM, rc, RT_SRC_POS,
                          N_("Configuration error: Failed to querying uint64_t value \"TSCTicksPerSecond\""));
    else if (   pVM->tm.s.cTSCTicksPerSecond < _1M
             || pVM->tm.s.cTSCTicksPerSecond >= _4G)
        return VMSetError(pVM, VERR_INVALID_PARAMETER, RT_SRC_POS,
                          N_("Configuration error: \"TSCTicksPerSecond\" = %RI64 is not in the range 1MHz..4GHz-1"),
                          pVM->tm.s.cTSCTicksPerSecond);
    else if (pVM->tm.s.enmTSCMode != TMTSCMODE_NATIVE_API)
        pVM->tm.s.enmTSCMode = TMTSCMODE_VIRT_TSC_EMULATED;
    else
    {
        LogRel(("TM: NEM overrides the /TM/TSCTicksPerSecond=%RU64 setting.\n", pVM->tm.s.cTSCTicksPerSecond));
        pVM->tm.s.cTSCTicksPerSecond = pVM->tm.s.cTSCTicksPerSecondHost * pVM->tm.s.u8TSCMultiplier;
    }

    /** @cfgm{/TM/TSCTiedToExecution, bool, false}
     * Whether the TSC should be tied to execution. This will exclude most of the
     * virtualization overhead, but will by default include the time spent in the
     * halt state (see TM/TSCNotTiedToHalt). This setting will override all other
     * TSC settings except for TSCTicksPerSecond and TSCNotTiedToHalt, which should
     * be used avoided or used with great care. Note that this will only work right
     * together with VT-x or AMD-V, and with a single virtual CPU. */
    rc = CFGMR3QueryBoolDef(pCfgHandle, "TSCTiedToExecution", &pVM->tm.s.fTSCTiedToExecution, false);
    if (RT_FAILURE(rc))
        return VMSetError(pVM, rc, RT_SRC_POS,
                          N_("Configuration error: Failed to querying bool value \"TSCTiedToExecution\""));
    if (pVM->tm.s.fTSCTiedToExecution && pVM->tm.s.enmTSCMode == TMTSCMODE_NATIVE_API)
        return VMSetError(pVM, VERR_INVALID_PARAMETER, RT_SRC_POS, N_("/TM/TSCTiedToExecution is not supported in NEM mode!"));
    if (pVM->tm.s.fTSCTiedToExecution)
        pVM->tm.s.enmTSCMode = TMTSCMODE_VIRT_TSC_EMULATED;


    /** @cfgm{/TM/TSCNotTiedToHalt, bool, false}
     * This is used with /TM/TSCTiedToExecution to control how TSC operates
     * accross HLT instructions.  When true HLT is considered execution time and
     * TSC continues to run, while when false (default) TSC stops during halt. */
    rc = CFGMR3QueryBoolDef(pCfgHandle, "TSCNotTiedToHalt", &pVM->tm.s.fTSCNotTiedToHalt, false);
    if (RT_FAILURE(rc))
        return VMSetError(pVM, rc, RT_SRC_POS,
                          N_("Configuration error: Failed to querying bool value \"TSCNotTiedToHalt\""));

    /*
     * Configure the timer synchronous virtual time.
     */
    /** @cfgm{/TM/ScheduleSlack, uint32_t, ns, 0, UINT32_MAX, 100000}
     * Scheduling slack when processing timers. */
    rc = CFGMR3QueryU32(pCfgHandle, "ScheduleSlack", &pVM->tm.s.u32VirtualSyncScheduleSlack);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        pVM->tm.s.u32VirtualSyncScheduleSlack           =   100000; /* 0.100ms (ASSUMES virtual time is nanoseconds) */
    else if (RT_FAILURE(rc))
        return VMSetError(pVM, rc, RT_SRC_POS,
                          N_("Configuration error: Failed to querying 32-bit integer value \"ScheduleSlack\""));

    /** @cfgm{/TM/CatchUpStopThreshold, uint64_t, ns, 0, UINT64_MAX, 500000}
     * When to stop a catch-up, considering it successful. */
    rc = CFGMR3QueryU64(pCfgHandle, "CatchUpStopThreshold", &pVM->tm.s.u64VirtualSyncCatchUpStopThreshold);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        pVM->tm.s.u64VirtualSyncCatchUpStopThreshold    =   500000; /* 0.5ms */
    else if (RT_FAILURE(rc))
        return VMSetError(pVM, rc, RT_SRC_POS,
                          N_("Configuration error: Failed to querying 64-bit integer value \"CatchUpStopThreshold\""));

    /** @cfgm{/TM/CatchUpGiveUpThreshold, uint64_t, ns, 0, UINT64_MAX, 60000000000}
     * When to give up a catch-up attempt. */
    rc = CFGMR3QueryU64(pCfgHandle, "CatchUpGiveUpThreshold", &pVM->tm.s.u64VirtualSyncCatchUpGiveUpThreshold);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        pVM->tm.s.u64VirtualSyncCatchUpGiveUpThreshold  = UINT64_C(60000000000); /* 60 sec */
    else if (RT_FAILURE(rc))
        return VMSetError(pVM, rc, RT_SRC_POS,
                          N_("Configuration error: Failed to querying 64-bit integer value \"CatchUpGiveUpThreshold\""));


    /** @cfgm{/TM/CatchUpPrecentage[0..9], uint32_t, %, 1, 2000, various}
     * The catch-up percent for a given period.  */
    /** @cfgm{/TM/CatchUpStartThreshold[0..9], uint64_t, ns, 0, UINT64_MAX}
     * The catch-up period threshold, or if you like, when a period starts.  */
#define TM_CFG_PERIOD(iPeriod, DefStart, DefPct) \
    do \
    { \
        uint64_t u64; \
        rc = CFGMR3QueryU64(pCfgHandle, "CatchUpStartThreshold" #iPeriod, &u64); \
        if (rc == VERR_CFGM_VALUE_NOT_FOUND) \
            u64 = UINT64_C(DefStart); \
        else if (RT_FAILURE(rc)) \
            return VMSetError(pVM, rc, RT_SRC_POS, N_("Configuration error: Failed to querying 64-bit integer value \"CatchUpThreshold" #iPeriod "\"")); \
        if (    (iPeriod > 0 && u64 <= pVM->tm.s.aVirtualSyncCatchUpPeriods[iPeriod - 1].u64Start) \
            ||  u64 >= pVM->tm.s.u64VirtualSyncCatchUpGiveUpThreshold) \
            return VMSetError(pVM, VERR_INVALID_PARAMETER, RT_SRC_POS, N_("Configuration error: Invalid start of period #" #iPeriod ": %'RU64"), u64); \
        pVM->tm.s.aVirtualSyncCatchUpPeriods[iPeriod].u64Start = u64; \
        rc = CFGMR3QueryU32(pCfgHandle, "CatchUpPrecentage" #iPeriod, &pVM->tm.s.aVirtualSyncCatchUpPeriods[iPeriod].u32Percentage); \
        if (rc == VERR_CFGM_VALUE_NOT_FOUND) \
            pVM->tm.s.aVirtualSyncCatchUpPeriods[iPeriod].u32Percentage = (DefPct); \
        else if (RT_FAILURE(rc)) \
            return VMSetError(pVM, rc, RT_SRC_POS, N_("Configuration error: Failed to querying 32-bit integer value \"CatchUpPrecentage" #iPeriod "\"")); \
    } while (0)
    /* This needs more tuning. Not sure if we really need so many period and be so gentle. */
    TM_CFG_PERIOD(0,     750000,   5); /* 0.75ms at 1.05x */
    TM_CFG_PERIOD(1,    1500000,  10); /* 1.50ms at 1.10x */
    TM_CFG_PERIOD(2,    8000000,  25); /*    8ms at 1.25x */
    TM_CFG_PERIOD(3,   30000000,  50); /*   30ms at 1.50x */
    TM_CFG_PERIOD(4,   75000000,  75); /*   75ms at 1.75x */
    TM_CFG_PERIOD(5,  175000000, 100); /*  175ms at 2x */
    TM_CFG_PERIOD(6,  500000000, 200); /*  500ms at 3x */
    TM_CFG_PERIOD(7, 3000000000, 300); /*    3s  at 4x */
    TM_CFG_PERIOD(8,30000000000, 400); /*   30s  at 5x */
    TM_CFG_PERIOD(9,55000000000, 500); /*   55s  at 6x */
    AssertCompile(RT_ELEMENTS(pVM->tm.s.aVirtualSyncCatchUpPeriods) == 10);
#undef TM_CFG_PERIOD

    /*
     * Configure real world time (UTC).
     */
    /** @cfgm{/TM/UTCOffset, int64_t, ns, INT64_MIN, INT64_MAX, 0}
     * The UTC offset. This is used to put the guest back or forwards in time.  */
    rc = CFGMR3QueryS64(pCfgHandle, "UTCOffset", &pVM->tm.s.offUTC);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        pVM->tm.s.offUTC = 0; /* ns */
    else if (RT_FAILURE(rc))
        return VMSetError(pVM, rc, RT_SRC_POS,
                          N_("Configuration error: Failed to querying 64-bit integer value \"UTCOffset\""));

    /** @cfgm{/TM/UTCTouchFileOnJump, string, none}
     * File to be written to everytime the host time jumps.  */
    rc = CFGMR3QueryStringAlloc(pCfgHandle, "UTCTouchFileOnJump", &pVM->tm.s.pszUtcTouchFileOnJump);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        pVM->tm.s.pszUtcTouchFileOnJump = NULL;
    else if (RT_FAILURE(rc))
        return VMSetError(pVM, rc, RT_SRC_POS,
                          N_("Configuration error: Failed to querying string value \"UTCTouchFileOnJump\""));

    /*
     * Setup the warp drive.
     */
    /** @cfgm{/TM/WarpDrivePercentage, uint32_t, %, 0, 20000, 100}
     * The warp drive percentage, 100% is normal speed.  This is used to speed up
     * or slow down the virtual clock, which can be useful for fast forwarding
     * borring periods during tests. */
    rc = CFGMR3QueryU32(pCfgHandle, "WarpDrivePercentage", &pVM->tm.s.u32VirtualWarpDrivePercentage);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        rc = CFGMR3QueryU32(CFGMR3GetRoot(pVM), "WarpDrivePercentage", &pVM->tm.s.u32VirtualWarpDrivePercentage); /* legacy */
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        pVM->tm.s.u32VirtualWarpDrivePercentage = 100;
    else if (RT_FAILURE(rc))
        return VMSetError(pVM, rc, RT_SRC_POS,
                          N_("Configuration error: Failed to querying uint32_t value \"WarpDrivePercent\""));
    else if (   pVM->tm.s.u32VirtualWarpDrivePercentage < 2
             || pVM->tm.s.u32VirtualWarpDrivePercentage > 20000)
        return VMSetError(pVM, VERR_INVALID_PARAMETER, RT_SRC_POS,
                          N_("Configuration error: \"WarpDrivePercent\" = %RI32 is not in the range 2..20000"),
                          pVM->tm.s.u32VirtualWarpDrivePercentage);
    pVM->tm.s.fVirtualWarpDrive = pVM->tm.s.u32VirtualWarpDrivePercentage != 100;
    if (pVM->tm.s.fVirtualWarpDrive)
    {
        if (pVM->tm.s.enmTSCMode == TMTSCMODE_NATIVE_API)
            LogRel(("TM: Warp-drive active, escept for TSC which is in NEM mode. u32VirtualWarpDrivePercentage=%RI32\n",
                    pVM->tm.s.u32VirtualWarpDrivePercentage));
        else
        {
            pVM->tm.s.enmTSCMode = TMTSCMODE_VIRT_TSC_EMULATED;
            LogRel(("TM: Warp-drive active. u32VirtualWarpDrivePercentage=%RI32\n", pVM->tm.s.u32VirtualWarpDrivePercentage));
        }
    }

    /*
     * Gather the Host Hz configuration values.
     */
    rc = CFGMR3QueryU32Def(pCfgHandle, "HostHzMax", &pVM->tm.s.cHostHzMax, 20000);
    if (RT_FAILURE(rc))
        return VMSetError(pVM, rc, RT_SRC_POS,
                          N_("Configuration error: Failed to querying uint32_t value \"HostHzMax\""));

    rc = CFGMR3QueryU32Def(pCfgHandle, "HostHzFudgeFactorTimerCpu", &pVM->tm.s.cPctHostHzFudgeFactorTimerCpu, 111);
    if (RT_FAILURE(rc))
        return VMSetError(pVM, rc, RT_SRC_POS,
                          N_("Configuration error: Failed to querying uint32_t value \"HostHzFudgeFactorTimerCpu\""));

    rc = CFGMR3QueryU32Def(pCfgHandle, "HostHzFudgeFactorOtherCpu", &pVM->tm.s.cPctHostHzFudgeFactorOtherCpu, 110);
    if (RT_FAILURE(rc))
        return VMSetError(pVM, rc, RT_SRC_POS,
                          N_("Configuration error: Failed to querying uint32_t value \"HostHzFudgeFactorOtherCpu\""));

    rc = CFGMR3QueryU32Def(pCfgHandle, "HostHzFudgeFactorCatchUp100", &pVM->tm.s.cPctHostHzFudgeFactorCatchUp100, 300);
    if (RT_FAILURE(rc))
        return VMSetError(pVM, rc, RT_SRC_POS,
                          N_("Configuration error: Failed to querying uint32_t value \"HostHzFudgeFactorCatchUp100\""));

    rc = CFGMR3QueryU32Def(pCfgHandle, "HostHzFudgeFactorCatchUp200", &pVM->tm.s.cPctHostHzFudgeFactorCatchUp200, 250);
    if (RT_FAILURE(rc))
        return VMSetError(pVM, rc, RT_SRC_POS,
                          N_("Configuration error: Failed to querying uint32_t value \"HostHzFudgeFactorCatchUp200\""));

    rc = CFGMR3QueryU32Def(pCfgHandle, "HostHzFudgeFactorCatchUp400", &pVM->tm.s.cPctHostHzFudgeFactorCatchUp400, 200);
    if (RT_FAILURE(rc))
        return VMSetError(pVM, rc, RT_SRC_POS,
                          N_("Configuration error: Failed to querying uint32_t value \"HostHzFudgeFactorCatchUp400\""));

    /*
     * Finally, setup and report.
     */
    pVM->tm.s.enmOriginalTSCMode = pVM->tm.s.enmTSCMode;
#if !defined(VBOX_VMM_TARGET_ARMV8)
    CPUMR3SetCR4Feature(pVM, X86_CR4_TSD, ~X86_CR4_TSD);
#endif
    LogRel(("TM:     cTSCTicksPerSecond=%'RU64 (%#RX64) enmTSCMode=%d (%s) TSCMultiplier=%u\n"
            "TM: cTSCTicksPerSecondHost=%'RU64 (%#RX64)\n"
            "TM: TSCTiedToExecution=%RTbool TSCNotTiedToHalt=%RTbool\n",
            pVM->tm.s.cTSCTicksPerSecond, pVM->tm.s.cTSCTicksPerSecond, pVM->tm.s.enmTSCMode, tmR3GetTSCModeName(pVM), pVM->tm.s.u8TSCMultiplier,
            pVM->tm.s.cTSCTicksPerSecondHost, pVM->tm.s.cTSCTicksPerSecondHost,
            pVM->tm.s.fTSCTiedToExecution, pVM->tm.s.fTSCNotTiedToHalt));

    /*
     * Start the timer (guard against REM not yielding).
     */
    /** @cfgm{/TM/TimerMillies, uint32_t, ms, 1, 1000, 10}
     * The watchdog timer interval.  */
    uint32_t u32Millies;
    rc = CFGMR3QueryU32(pCfgHandle, "TimerMillies", &u32Millies);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        u32Millies = VM_IS_HM_ENABLED(pVM) ? 1000 : 10;
    else if (RT_FAILURE(rc))
        return VMSetError(pVM, rc, RT_SRC_POS,
                          N_("Configuration error: Failed to query uint32_t value \"TimerMillies\""));
    rc = RTTimerCreate(&pVM->tm.s.pTimer, u32Millies, tmR3TimerCallback, pVM);
    if (RT_FAILURE(rc))
    {
        AssertMsgFailed(("Failed to create timer, u32Millies=%d rc=%Rrc.\n", u32Millies, rc));
        return rc;
    }
    Log(("TM: Created timer %p firing every %d milliseconds\n", pVM->tm.s.pTimer, u32Millies));
    pVM->tm.s.u32TimerMillies = u32Millies;

    /*
     * Register saved state.
     */
    rc = SSMR3RegisterInternal(pVM, "tm", 1, TM_SAVED_STATE_VERSION, sizeof(uint64_t) * 8,
                               NULL, NULL, NULL,
                               NULL, tmR3Save, NULL,
                               NULL, tmR3Load, tmR3LoadDone);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Register statistics.
     */
    STAM_REL_REG_USED(pVM,(void*)&pVM->tm.s.VirtualGetRawData.c1nsSteps,STAMTYPE_U32, "/TM/R3/1nsSteps",                     STAMUNIT_OCCURENCES, "Virtual time 1ns steps (due to TSC / GIP variations).");
    STAM_REL_REG_USED(pVM,(void*)&pVM->tm.s.VirtualGetRawData.cBadPrev, STAMTYPE_U32, "/TM/R3/cBadPrev",                     STAMUNIT_OCCURENCES, "Times the previous virtual time was considered erratic (shouldn't ever happen).");
#if 0 /** @todo retreive from ring-0 */
    STAM_REL_REG_USED(pVM,(void*)&pVM->tm.s.VirtualGetRawDataR0.c1nsSteps,STAMTYPE_U32, "/TM/R0/1nsSteps",                     STAMUNIT_OCCURENCES, "Virtual time 1ns steps (due to TSC / GIP variations).");
    STAM_REL_REG_USED(pVM,(void*)&pVM->tm.s.VirtualGetRawDataR0.cBadPrev, STAMTYPE_U32, "/TM/R0/cBadPrev",                     STAMUNIT_OCCURENCES, "Times the previous virtual time was considered erratic (shouldn't ever happen).");
#endif
    STAM_REL_REG(     pVM,(void*)&pVM->tm.s.offVirtualSync,               STAMTYPE_U64, "/TM/VirtualSync/CurrentOffset",               STAMUNIT_NS, "The current offset. (subtract GivenUp to get the lag)");
    STAM_REL_REG_USED(pVM,(void*)&pVM->tm.s.offVirtualSyncGivenUp,        STAMTYPE_U64, "/TM/VirtualSync/GivenUp",                     STAMUNIT_NS, "Nanoseconds of the 'CurrentOffset' that's been given up and won't ever be attempted caught up with.");
    STAM_REL_REG(     pVM,(void*)&pVM->tm.s.HzHint.s.uMax,                STAMTYPE_U32, "/TM/MaxHzHint",                               STAMUNIT_HZ, "Max guest timer frequency hint.");
    for (uint32_t i = 0; i < RT_ELEMENTS(pVM->tm.s.aTimerQueues); i++)
    {
        rc = STAMR3RegisterF(pVM, (void *)&pVM->tm.s.aTimerQueues[i].uMaxHzHint, STAMTYPE_U32, STAMVISIBILITY_ALWAYS, STAMUNIT_HZ,
                             "", "/TM/MaxHzHint/%s", pVM->tm.s.aTimerQueues[i].szName);
        AssertRC(rc);
    }

#ifdef VBOX_WITH_STATISTICS
    STAM_REG_USED(pVM,(void *)&pVM->tm.s.VirtualGetRawData.cExpired,    STAMTYPE_U32, "/TM/R3/cExpired",                     STAMUNIT_OCCURENCES, "Times the TSC interval expired (overlaps 1ns steps).");
    STAM_REG_USED(pVM,(void *)&pVM->tm.s.VirtualGetRawData.cUpdateRaces,STAMTYPE_U32, "/TM/R3/cUpdateRaces",                 STAMUNIT_OCCURENCES, "Thread races when updating the previous timestamp.");
# if 0 /** @todo retreive from ring-0 */
    STAM_REG_USED(pVM,(void *)&pVM->tm.s.VirtualGetRawDataR0.cExpired,    STAMTYPE_U32, "/TM/R0/cExpired",                     STAMUNIT_OCCURENCES, "Times the TSC interval expired (overlaps 1ns steps).");
    STAM_REG_USED(pVM,(void *)&pVM->tm.s.VirtualGetRawDataR0.cUpdateRaces,STAMTYPE_U32, "/TM/R0/cUpdateRaces",                 STAMUNIT_OCCURENCES, "Thread races when updating the previous timestamp.");
# endif
    STAM_REG(pVM, &pVM->tm.s.StatDoQueues,                            STAMTYPE_PROFILE, "/TM/DoQueues",                    STAMUNIT_TICKS_PER_CALL, "Profiling timer TMR3TimerQueuesDo.");
    STAM_REG(pVM, &pVM->tm.s.aTimerQueues[TMCLOCK_VIRTUAL].StatDo,    STAMTYPE_PROFILE, "/TM/DoQueues/Virtual",            STAMUNIT_TICKS_PER_CALL, "Time spent on the virtual clock queue.");
    STAM_REG(pVM, &pVM->tm.s.aTimerQueues[TMCLOCK_VIRTUAL_SYNC].StatDo,STAMTYPE_PROFILE,"/TM/DoQueues/VirtualSync",        STAMUNIT_TICKS_PER_CALL, "Time spent on the virtual sync clock queue.");
    STAM_REG(pVM, &pVM->tm.s.aTimerQueues[TMCLOCK_REAL].StatDo,       STAMTYPE_PROFILE, "/TM/DoQueues/Real",               STAMUNIT_TICKS_PER_CALL, "Time spent on the real clock queue.");

    STAM_REG(pVM, &pVM->tm.s.StatPoll,                                STAMTYPE_COUNTER, "/TM/Poll",                            STAMUNIT_OCCURENCES, "TMTimerPoll calls.");
    STAM_REG(pVM, &pVM->tm.s.StatPollAlreadySet,                      STAMTYPE_COUNTER, "/TM/Poll/AlreadySet",                 STAMUNIT_OCCURENCES, "TMTimerPoll calls where the FF was already set.");
    STAM_REG(pVM, &pVM->tm.s.StatPollELoop,                           STAMTYPE_COUNTER, "/TM/Poll/ELoop",                      STAMUNIT_OCCURENCES, "Times TMTimerPoll has given up getting a consistent virtual sync data set.");
    STAM_REG(pVM, &pVM->tm.s.StatPollMiss,                            STAMTYPE_COUNTER, "/TM/Poll/Miss",                       STAMUNIT_OCCURENCES, "TMTimerPoll calls where nothing had expired.");
    STAM_REG(pVM, &pVM->tm.s.StatPollRunning,                         STAMTYPE_COUNTER, "/TM/Poll/Running",                    STAMUNIT_OCCURENCES, "TMTimerPoll calls where the queues were being run.");
    STAM_REG(pVM, &pVM->tm.s.StatPollSimple,                          STAMTYPE_COUNTER, "/TM/Poll/Simple",                     STAMUNIT_OCCURENCES, "TMTimerPoll calls where we could take the simple path.");
    STAM_REG(pVM, &pVM->tm.s.StatPollVirtual,                         STAMTYPE_COUNTER, "/TM/Poll/HitsVirtual",                STAMUNIT_OCCURENCES, "The number of times TMTimerPoll found an expired TMCLOCK_VIRTUAL queue.");
    STAM_REG(pVM, &pVM->tm.s.StatPollVirtualSync,                     STAMTYPE_COUNTER, "/TM/Poll/HitsVirtualSync",            STAMUNIT_OCCURENCES, "The number of times TMTimerPoll found an expired TMCLOCK_VIRTUAL_SYNC queue.");

    STAM_REG(pVM, &pVM->tm.s.StatPostponedR3,                         STAMTYPE_COUNTER, "/TM/PostponedR3",                     STAMUNIT_OCCURENCES, "Postponed due to unschedulable state, in ring-3.");
    STAM_REG(pVM, &pVM->tm.s.StatPostponedRZ,                         STAMTYPE_COUNTER, "/TM/PostponedRZ",                     STAMUNIT_OCCURENCES, "Postponed due to unschedulable state, in ring-0 / RC.");

    STAM_REG(pVM, &pVM->tm.s.StatScheduleOneR3,                       STAMTYPE_PROFILE, "/TM/ScheduleOneR3",               STAMUNIT_TICKS_PER_CALL, "Profiling the scheduling of one queue during a TMTimer* call in EMT.");
    STAM_REG(pVM, &pVM->tm.s.StatScheduleOneRZ,                       STAMTYPE_PROFILE, "/TM/ScheduleOneRZ",               STAMUNIT_TICKS_PER_CALL, "Profiling the scheduling of one queue during a TMTimer* call in EMT.");
    STAM_REG(pVM, &pVM->tm.s.StatScheduleSetFF,                       STAMTYPE_COUNTER, "/TM/ScheduleSetFF",                   STAMUNIT_OCCURENCES, "The number of times the timer FF was set instead of doing scheduling.");

    STAM_REG(pVM, &pVM->tm.s.StatTimerSet,                            STAMTYPE_COUNTER, "/TM/TimerSet",                        STAMUNIT_OCCURENCES, "Calls, except virtual sync timers");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetOpt,                         STAMTYPE_COUNTER, "/TM/TimerSet/Opt",                    STAMUNIT_OCCURENCES, "Optimized path taken.");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetR3,                          STAMTYPE_PROFILE, "/TM/TimerSet/R3",                 STAMUNIT_TICKS_PER_CALL, "Profiling TMTimerSet calls made in ring-3.");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetRZ,                          STAMTYPE_PROFILE, "/TM/TimerSet/RZ",                 STAMUNIT_TICKS_PER_CALL, "Profiling TMTimerSet calls made in ring-0 / RC.");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetStActive,                    STAMTYPE_COUNTER, "/TM/TimerSet/StActive",               STAMUNIT_OCCURENCES, "ACTIVE");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetStExpDeliver,                STAMTYPE_COUNTER, "/TM/TimerSet/StExpDeliver",           STAMUNIT_OCCURENCES, "EXPIRED_DELIVER");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetStOther,                     STAMTYPE_COUNTER, "/TM/TimerSet/StOther",                STAMUNIT_OCCURENCES, "Other states");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetStPendStop,                  STAMTYPE_COUNTER, "/TM/TimerSet/StPendStop",             STAMUNIT_OCCURENCES, "PENDING_STOP");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetStPendStopSched,             STAMTYPE_COUNTER, "/TM/TimerSet/StPendStopSched",        STAMUNIT_OCCURENCES, "PENDING_STOP_SCHEDULE");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetStPendSched,                 STAMTYPE_COUNTER, "/TM/TimerSet/StPendSched",            STAMUNIT_OCCURENCES, "PENDING_SCHEDULE");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetStPendResched,               STAMTYPE_COUNTER, "/TM/TimerSet/StPendResched",          STAMUNIT_OCCURENCES, "PENDING_RESCHEDULE");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetStStopped,                   STAMTYPE_COUNTER, "/TM/TimerSet/StStopped",              STAMUNIT_OCCURENCES, "STOPPED");

    STAM_REG(pVM, &pVM->tm.s.StatTimerSetVs,                          STAMTYPE_COUNTER, "/TM/TimerSetVs",                      STAMUNIT_OCCURENCES, "TMTimerSet calls on virtual sync timers");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetVsR3,                        STAMTYPE_PROFILE, "/TM/TimerSetVs/R3",               STAMUNIT_TICKS_PER_CALL, "Profiling TMTimerSet calls made in ring-3 on virtual sync timers.");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetVsRZ,                        STAMTYPE_PROFILE, "/TM/TimerSetVs/RZ",               STAMUNIT_TICKS_PER_CALL, "Profiling TMTimerSet calls made in ring-0 / RC on virtual sync timers.");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetVsStActive,                  STAMTYPE_COUNTER, "/TM/TimerSetVs/StActive",             STAMUNIT_OCCURENCES, "ACTIVE");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetVsStExpDeliver,              STAMTYPE_COUNTER, "/TM/TimerSetVs/StExpDeliver",         STAMUNIT_OCCURENCES, "EXPIRED_DELIVER");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetVsStStopped,                 STAMTYPE_COUNTER, "/TM/TimerSetVs/StStopped",            STAMUNIT_OCCURENCES, "STOPPED");

    STAM_REG(pVM, &pVM->tm.s.StatTimerSetRelative,                    STAMTYPE_COUNTER, "/TM/TimerSetRelative",                STAMUNIT_OCCURENCES, "Calls, except virtual sync timers");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetRelativeOpt,                 STAMTYPE_COUNTER, "/TM/TimerSetRelative/Opt",            STAMUNIT_OCCURENCES, "Optimized path taken.");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetRelativeR3,                  STAMTYPE_PROFILE, "/TM/TimerSetRelative/R3",         STAMUNIT_TICKS_PER_CALL, "Profiling TMTimerSetRelative calls made in ring-3 (sans virtual sync).");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetRelativeRZ,                  STAMTYPE_PROFILE, "/TM/TimerSetRelative/RZ",         STAMUNIT_TICKS_PER_CALL, "Profiling TMTimerSetReltaive calls made in ring-0 / RC (sans virtual sync).");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetRelativeStActive,            STAMTYPE_COUNTER, "/TM/TimerSetRelative/StActive",       STAMUNIT_OCCURENCES, "ACTIVE");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetRelativeStExpDeliver,        STAMTYPE_COUNTER, "/TM/TimerSetRelative/StExpDeliver",   STAMUNIT_OCCURENCES, "EXPIRED_DELIVER");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetRelativeStOther,             STAMTYPE_COUNTER, "/TM/TimerSetRelative/StOther",        STAMUNIT_OCCURENCES, "Other states");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetRelativeStPendStop,          STAMTYPE_COUNTER, "/TM/TimerSetRelative/StPendStop",     STAMUNIT_OCCURENCES, "PENDING_STOP");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetRelativeStPendStopSched,     STAMTYPE_COUNTER, "/TM/TimerSetRelative/StPendStopSched",STAMUNIT_OCCURENCES, "PENDING_STOP_SCHEDULE");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetRelativeStPendSched,         STAMTYPE_COUNTER, "/TM/TimerSetRelative/StPendSched",    STAMUNIT_OCCURENCES, "PENDING_SCHEDULE");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetRelativeStPendResched,       STAMTYPE_COUNTER, "/TM/TimerSetRelative/StPendResched",  STAMUNIT_OCCURENCES, "PENDING_RESCHEDULE");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetRelativeStStopped,           STAMTYPE_COUNTER, "/TM/TimerSetRelative/StStopped",      STAMUNIT_OCCURENCES, "STOPPED");

    STAM_REG(pVM, &pVM->tm.s.StatTimerSetRelativeVs,                  STAMTYPE_COUNTER, "/TM/TimerSetRelativeVs",              STAMUNIT_OCCURENCES, "TMTimerSetRelative calls on virtual sync timers");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetRelativeVsR3,                STAMTYPE_PROFILE, "/TM/TimerSetRelativeVs/R3",       STAMUNIT_TICKS_PER_CALL, "Profiling TMTimerSetRelative calls made in ring-3 on virtual sync timers.");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetRelativeVsRZ,                STAMTYPE_PROFILE, "/TM/TimerSetRelativeVs/RZ",       STAMUNIT_TICKS_PER_CALL, "Profiling TMTimerSetReltaive calls made in ring-0 / RC on virtual sync timers.");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetRelativeVsStActive,          STAMTYPE_COUNTER, "/TM/TimerSetRelativeVs/StActive",     STAMUNIT_OCCURENCES, "ACTIVE");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetRelativeVsStExpDeliver,      STAMTYPE_COUNTER, "/TM/TimerSetRelativeVs/StExpDeliver", STAMUNIT_OCCURENCES, "EXPIRED_DELIVER");
    STAM_REG(pVM, &pVM->tm.s.StatTimerSetRelativeVsStStopped,         STAMTYPE_COUNTER, "/TM/TimerSetRelativeVs/StStopped",    STAMUNIT_OCCURENCES, "STOPPED");

    STAM_REG(pVM, &pVM->tm.s.StatTimerStopR3,                         STAMTYPE_PROFILE, "/TM/TimerStopR3",                 STAMUNIT_TICKS_PER_CALL, "Profiling TMTimerStop calls made in ring-3.");
    STAM_REG(pVM, &pVM->tm.s.StatTimerStopRZ,                         STAMTYPE_PROFILE, "/TM/TimerStopRZ",                 STAMUNIT_TICKS_PER_CALL, "Profiling TMTimerStop calls made in ring-0 / RC.");

    STAM_REG(pVM, &pVM->tm.s.StatVirtualGet,                          STAMTYPE_COUNTER, "/TM/VirtualGet",                      STAMUNIT_OCCURENCES, "The number of times TMTimerGet was called when the clock was running.");
    STAM_REG(pVM, &pVM->tm.s.StatVirtualGetSetFF,                     STAMTYPE_COUNTER, "/TM/VirtualGetSetFF",                 STAMUNIT_OCCURENCES, "Times we set the FF when calling TMTimerGet.");
    STAM_REG(pVM, &pVM->tm.s.StatVirtualSyncGet,                      STAMTYPE_COUNTER, "/TM/VirtualSyncGet",                  STAMUNIT_OCCURENCES, "The number of times tmVirtualSyncGetEx was called.");
    STAM_REG(pVM, &pVM->tm.s.StatVirtualSyncGetAdjLast,               STAMTYPE_COUNTER, "/TM/VirtualSyncGet/AdjLast",          STAMUNIT_OCCURENCES, "Times we've adjusted against the last returned time stamp .");
    STAM_REG(pVM, &pVM->tm.s.StatVirtualSyncGetELoop,                 STAMTYPE_COUNTER, "/TM/VirtualSyncGet/ELoop",            STAMUNIT_OCCURENCES, "Times tmVirtualSyncGetEx has given up getting a consistent virtual sync data set.");
    STAM_REG(pVM, &pVM->tm.s.StatVirtualSyncGetExpired,               STAMTYPE_COUNTER, "/TM/VirtualSyncGet/Expired",          STAMUNIT_OCCURENCES, "Times tmVirtualSyncGetEx encountered an expired timer stopping the clock.");
    STAM_REG(pVM, &pVM->tm.s.StatVirtualSyncGetLocked,                STAMTYPE_COUNTER, "/TM/VirtualSyncGet/Locked",           STAMUNIT_OCCURENCES, "Times we successfully acquired the lock in tmVirtualSyncGetEx.");
    STAM_REG(pVM, &pVM->tm.s.StatVirtualSyncGetLockless,              STAMTYPE_COUNTER, "/TM/VirtualSyncGet/Lockless",         STAMUNIT_OCCURENCES, "Times tmVirtualSyncGetEx returned without needing to take the lock.");
    STAM_REG(pVM, &pVM->tm.s.StatVirtualSyncGetSetFF,                 STAMTYPE_COUNTER, "/TM/VirtualSyncGet/SetFF",            STAMUNIT_OCCURENCES, "Times we set the FF when calling tmVirtualSyncGetEx.");
    STAM_REG(pVM, &pVM->tm.s.StatVirtualPause,                        STAMTYPE_COUNTER, "/TM/VirtualPause",                    STAMUNIT_OCCURENCES, "The number of times TMR3TimerPause was called.");
    STAM_REG(pVM, &pVM->tm.s.StatVirtualResume,                       STAMTYPE_COUNTER, "/TM/VirtualResume",                   STAMUNIT_OCCURENCES, "The number of times TMR3TimerResume was called.");

    STAM_REG(pVM, &pVM->tm.s.StatTimerCallbackSetFF,                  STAMTYPE_COUNTER, "/TM/CallbackSetFF",                   STAMUNIT_OCCURENCES, "The number of times the timer callback set FF.");
    STAM_REG(pVM, &pVM->tm.s.StatTimerCallback,                       STAMTYPE_COUNTER, "/TM/Callback",                        STAMUNIT_OCCURENCES, "The number of times the timer callback is invoked.");

    STAM_REG(pVM, &pVM->tm.s.StatTSCCatchupLE010,                     STAMTYPE_COUNTER, "/TM/TSC/Intercept/CatchupLE010",      STAMUNIT_OCCURENCES, "In catch-up mode, 10% or lower.");
    STAM_REG(pVM, &pVM->tm.s.StatTSCCatchupLE025,                     STAMTYPE_COUNTER, "/TM/TSC/Intercept/CatchupLE025",      STAMUNIT_OCCURENCES, "In catch-up mode, 25%-11%.");
    STAM_REG(pVM, &pVM->tm.s.StatTSCCatchupLE100,                     STAMTYPE_COUNTER, "/TM/TSC/Intercept/CatchupLE100",      STAMUNIT_OCCURENCES, "In catch-up mode, 100%-26%.");
    STAM_REG(pVM, &pVM->tm.s.StatTSCCatchupOther,                     STAMTYPE_COUNTER, "/TM/TSC/Intercept/CatchupOther",      STAMUNIT_OCCURENCES, "In catch-up mode, > 100%.");
    STAM_REG(pVM, &pVM->tm.s.StatTSCNotFixed,                         STAMTYPE_COUNTER, "/TM/TSC/Intercept/NotFixed",          STAMUNIT_OCCURENCES, "TSC is not fixed, it may run at variable speed.");
    STAM_REG(pVM, &pVM->tm.s.StatTSCNotTicking,                       STAMTYPE_COUNTER, "/TM/TSC/Intercept/NotTicking",        STAMUNIT_OCCURENCES, "TSC is not ticking.");
    STAM_REG(pVM, &pVM->tm.s.StatTSCSyncNotTicking,                   STAMTYPE_COUNTER, "/TM/TSC/Intercept/SyncNotTicking",    STAMUNIT_OCCURENCES, "VirtualSync isn't ticking.");
    STAM_REG(pVM, &pVM->tm.s.StatTSCWarp,                             STAMTYPE_COUNTER, "/TM/TSC/Intercept/Warp",              STAMUNIT_OCCURENCES, "Warpdrive is active.");
    STAM_REG(pVM, &pVM->tm.s.StatTSCSet,                              STAMTYPE_COUNTER, "/TM/TSC/Sets",                        STAMUNIT_OCCURENCES, "Calls to TMCpuTickSet.");
    STAM_REG(pVM, &pVM->tm.s.StatTSCUnderflow,                        STAMTYPE_COUNTER, "/TM/TSC/Underflow",                   STAMUNIT_OCCURENCES, "TSC underflow; corrected with last seen value .");
    STAM_REG(pVM, &pVM->tm.s.StatVirtualPause,                        STAMTYPE_COUNTER, "/TM/TSC/Pause",                       STAMUNIT_OCCURENCES, "The number of times the TSC was paused.");
    STAM_REG(pVM, &pVM->tm.s.StatVirtualResume,                       STAMTYPE_COUNTER, "/TM/TSC/Resume",                      STAMUNIT_OCCURENCES, "The number of times the TSC was resumed.");
#endif /* VBOX_WITH_STATISTICS */

    for (VMCPUID i = 0; i < pVM->cCpus; i++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[i];
        STAMR3RegisterF(pVM, &pVCpu->tm.s.offTSCRawSrc,          STAMTYPE_U64, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS, "TSC offset relative the raw source",           "/TM/TSC/offCPU%u", i);
#ifndef VBOX_WITHOUT_NS_ACCOUNTING
# if defined(VBOX_WITH_STATISTICS) || defined(VBOX_WITH_NS_ACCOUNTING_STATS)
        STAMR3RegisterF(pVM, &pVCpu->tm.s.StatNsTotal,       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_NS,               "Resettable: Total CPU run time.",                                 "/TM/CPU/%02u", i);
        STAMR3RegisterF(pVM, &pVCpu->tm.s.StatNsExecuting,   STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_NS_PER_OCCURENCE, "Resettable: Time spent executing guest code.",                    "/TM/CPU/%02u/PrfExecuting", i);
        STAMR3RegisterF(pVM, &pVCpu->tm.s.StatNsExecLong,    STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_NS_PER_OCCURENCE, "Resettable: Time spent executing guest code - long hauls.",       "/TM/CPU/%02u/PrfExecLong", i);
        STAMR3RegisterF(pVM, &pVCpu->tm.s.StatNsExecShort,   STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_NS_PER_OCCURENCE, "Resettable: Time spent executing guest code - short stretches.",  "/TM/CPU/%02u/PrfExecShort", i);
        STAMR3RegisterF(pVM, &pVCpu->tm.s.StatNsExecTiny,    STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_NS_PER_OCCURENCE, "Resettable: Time spent executing guest code - tiny bits.",        "/TM/CPU/%02u/PrfExecTiny", i);
        STAMR3RegisterF(pVM, &pVCpu->tm.s.StatNsHalted,      STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_NS_PER_OCCURENCE, "Resettable: Time spent halted.",                                  "/TM/CPU/%02u/PrfHalted", i);
        STAMR3RegisterF(pVM, &pVCpu->tm.s.StatNsOther,       STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_NS_PER_OCCURENCE, "Resettable: Time spent in the VMM or preempted.",                 "/TM/CPU/%02u/PrfOther", i);
# endif
        STAMR3RegisterF(pVM, &pVCpu->tm.s.cNsTotalStat,          STAMTYPE_U64, STAMVISIBILITY_ALWAYS, STAMUNIT_NS,    "Total CPU run time.",                          "/TM/CPU/%02u/cNsTotal", i);
        STAMR3RegisterF(pVM, &pVCpu->tm.s.cNsExecuting,          STAMTYPE_U64, STAMVISIBILITY_ALWAYS, STAMUNIT_NS,    "Time spent executing guest code.",             "/TM/CPU/%02u/cNsExecuting", i);
        STAMR3RegisterF(pVM, &pVCpu->tm.s.cNsHalted,             STAMTYPE_U64, STAMVISIBILITY_ALWAYS, STAMUNIT_NS,    "Time spent halted.",                           "/TM/CPU/%02u/cNsHalted", i);
        STAMR3RegisterF(pVM, &pVCpu->tm.s.cNsOtherStat,          STAMTYPE_U64, STAMVISIBILITY_ALWAYS, STAMUNIT_NS,    "Time spent in the VMM or preempted.",          "/TM/CPU/%02u/cNsOther", i);
        STAMR3RegisterF(pVM, &pVCpu->tm.s.cPeriodsExecuting,     STAMTYPE_U64, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "Times executed guest code.",                   "/TM/CPU/%02u/cPeriodsExecuting", i);
        STAMR3RegisterF(pVM, &pVCpu->tm.s.cPeriodsHalted,        STAMTYPE_U64, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT, "Times halted.",                                "/TM/CPU/%02u/cPeriodsHalted", i);
        STAMR3RegisterF(pVM, &pVCpu->tm.s.CpuLoad.cPctExecuting, STAMTYPE_U8,  STAMVISIBILITY_ALWAYS, STAMUNIT_PCT,   "Time spent executing guest code recently.",    "/TM/CPU/%02u/pctExecuting", i);
        STAMR3RegisterF(pVM, &pVCpu->tm.s.CpuLoad.cPctHalted,    STAMTYPE_U8,  STAMVISIBILITY_ALWAYS, STAMUNIT_PCT,   "Time spent halted recently.",                  "/TM/CPU/%02u/pctHalted", i);
        STAMR3RegisterF(pVM, &pVCpu->tm.s.CpuLoad.cPctOther,     STAMTYPE_U8,  STAMVISIBILITY_ALWAYS, STAMUNIT_PCT,   "Time spent in the VMM or preempted recently.", "/TM/CPU/%02u/pctOther", i);
#endif
    }
#ifndef VBOX_WITHOUT_NS_ACCOUNTING
    STAMR3RegisterF(pVM, &pVM->tm.s.CpuLoad.cPctExecuting, STAMTYPE_U8,  STAMVISIBILITY_ALWAYS, STAMUNIT_PCT,   "Time spent executing guest code recently.",    "/TM/CPU/pctExecuting");
    STAMR3RegisterF(pVM, &pVM->tm.s.CpuLoad.cPctHalted,    STAMTYPE_U8,  STAMVISIBILITY_ALWAYS, STAMUNIT_PCT,   "Time spent halted recently.",                  "/TM/CPU/pctHalted");
    STAMR3RegisterF(pVM, &pVM->tm.s.CpuLoad.cPctOther,     STAMTYPE_U8,  STAMVISIBILITY_ALWAYS, STAMUNIT_PCT,   "Time spent in the VMM or preempted recently.", "/TM/CPU/pctOther");
#endif

#ifdef VBOX_WITH_STATISTICS
    STAM_REG(pVM, &pVM->tm.s.StatVirtualSyncCatchup,              STAMTYPE_PROFILE_ADV, "/TM/VirtualSync/CatchUp",    STAMUNIT_TICKS_PER_OCCURENCE, "Counting and measuring the times spent catching up.");
    STAM_REG(pVM, (void *)&pVM->tm.s.fVirtualSyncCatchUp,                  STAMTYPE_U8, "/TM/VirtualSync/CatchUpActive",             STAMUNIT_NONE, "Catch-Up active indicator.");
    STAM_REG(pVM, (void *)&pVM->tm.s.u32VirtualSyncCatchUpPercentage,     STAMTYPE_U32, "/TM/VirtualSync/CatchUpPercentage",          STAMUNIT_PCT, "The catch-up percentage. (+100/100 to get clock multiplier)");
    STAM_REG(pVM, &pVM->tm.s.StatVirtualSyncFF,                       STAMTYPE_PROFILE, "/TM/VirtualSync/FF",         STAMUNIT_TICKS_PER_OCCURENCE, "Time spent in TMR3VirtualSyncFF by all but the dedicate timer EMT.");
    STAM_REG(pVM, &pVM->tm.s.StatVirtualSyncGiveUp,                   STAMTYPE_COUNTER, "/TM/VirtualSync/GiveUp",              STAMUNIT_OCCURENCES, "Times the catch-up was abandoned.");
    STAM_REG(pVM, &pVM->tm.s.StatVirtualSyncGiveUpBeforeStarting,     STAMTYPE_COUNTER, "/TM/VirtualSync/GiveUpBeforeStarting",STAMUNIT_OCCURENCES, "Times the catch-up was abandoned before even starting. (Typically debugging++.)");
    STAM_REG(pVM, &pVM->tm.s.StatVirtualSyncRun,                      STAMTYPE_COUNTER, "/TM/VirtualSync/Run",                 STAMUNIT_OCCURENCES, "Times the virtual sync timer queue was considered.");
    STAM_REG(pVM, &pVM->tm.s.StatVirtualSyncRunRestart,               STAMTYPE_COUNTER, "/TM/VirtualSync/Run/Restarts",        STAMUNIT_OCCURENCES, "Times the clock was restarted after a run.");
    STAM_REG(pVM, &pVM->tm.s.StatVirtualSyncRunStop,                  STAMTYPE_COUNTER, "/TM/VirtualSync/Run/Stop",            STAMUNIT_OCCURENCES, "Times the clock was stopped when calculating the current time before examining the timers.");
    STAM_REG(pVM, &pVM->tm.s.StatVirtualSyncRunStoppedAlready,        STAMTYPE_COUNTER, "/TM/VirtualSync/Run/StoppedAlready",  STAMUNIT_OCCURENCES, "Times the clock was already stopped elsewhere (TMVirtualSyncGet).");
    STAM_REG(pVM, &pVM->tm.s.StatVirtualSyncRunSlack,                 STAMTYPE_PROFILE, "/TM/VirtualSync/Run/Slack",     STAMUNIT_NS_PER_OCCURENCE, "The scheduling slack. (Catch-up handed out when running timers.)");
    for (unsigned i = 0; i < RT_ELEMENTS(pVM->tm.s.aVirtualSyncCatchUpPeriods); i++)
    {
        STAMR3RegisterF(pVM, &pVM->tm.s.aVirtualSyncCatchUpPeriods[i].u32Percentage,    STAMTYPE_U32, STAMVISIBILITY_ALWAYS,          STAMUNIT_PCT, "The catch-up percentage.",         "/TM/VirtualSync/Periods/%u", i);
        STAMR3RegisterF(pVM, &pVM->tm.s.aStatVirtualSyncCatchupAdjust[i],           STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS,   STAMUNIT_OCCURENCES, "Times adjusted to this period.",   "/TM/VirtualSync/Periods/%u/Adjust", i);
        STAMR3RegisterF(pVM, &pVM->tm.s.aStatVirtualSyncCatchupInitial[i],          STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS,   STAMUNIT_OCCURENCES, "Times started in this period.",    "/TM/VirtualSync/Periods/%u/Initial", i);
        STAMR3RegisterF(pVM, &pVM->tm.s.aVirtualSyncCatchUpPeriods[i].u64Start,         STAMTYPE_U64, STAMVISIBILITY_ALWAYS,           STAMUNIT_NS, "Start of this period (lag).",      "/TM/VirtualSync/Periods/%u/Start", i);
    }
#endif /* VBOX_WITH_STATISTICS */

    /*
     * Register info handlers.
     */
    DBGFR3InfoRegisterInternalEx(pVM, "timers",       "Dumps all timers. No arguments.",          tmR3TimerInfo,        DBGFINFO_FLAGS_RUN_ON_EMT);
    DBGFR3InfoRegisterInternalEx(pVM, "activetimers", "Dumps active all timers. No arguments.",   tmR3TimerInfoActive,  DBGFINFO_FLAGS_RUN_ON_EMT);
    DBGFR3InfoRegisterInternalEx(pVM, "clocks",       "Display the time of the various clocks.",  tmR3InfoClocks,       DBGFINFO_FLAGS_RUN_ON_EMT);
    DBGFR3InfoRegisterInternalArgv(pVM, "cpuload",    "Display the CPU load stats (--help for details).", tmR3InfoCpuLoad, 0);

    return VINF_SUCCESS;
}


/**
 * Checks if the host CPU has a fixed TSC frequency.
 *
 * @returns true if it has, false if it hasn't.
 *
 * @remarks This test doesn't bother with very old CPUs that don't do power
 *          management or any other stuff that might influence the TSC rate.
 *          This isn't currently relevant.
 */
static bool tmR3HasFixedTSC(PVM pVM)
{
    /*
     * ASSUME that if the GIP is in invariant TSC mode, it's because the CPU
     * actually has invariant TSC.
     *
     * In driverless mode we just assume sync TSC for now regardless of what
     * the case actually is.
     */
    PSUPGLOBALINFOPAGE const pGip       = g_pSUPGlobalInfoPage;
    SUPGIPMODE const         enmGipMode = pGip ? (SUPGIPMODE)pGip->u32Mode : SUPGIPMODE_INVARIANT_TSC;
    if (enmGipMode == SUPGIPMODE_INVARIANT_TSC)
        return true;

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    /*
     * Go by features and model info from the CPUID instruction.
     */
    if (ASMHasCpuId())
    {
        uint32_t uEAX, uEBX, uECX, uEDX;

        /*
         * By feature. (Used to be AMD specific, intel seems to have picked it up.)
         */
        ASMCpuId(0x80000000, &uEAX, &uEBX, &uECX, &uEDX);
        if (uEAX >= 0x80000007 && RTX86IsValidExtRange(uEAX))
        {
            ASMCpuId(0x80000007, &uEAX, &uEBX, &uECX, &uEDX);
            if (   (uEDX & X86_CPUID_AMD_ADVPOWER_EDX_TSCINVAR) /* TscInvariant */
                && enmGipMode != SUPGIPMODE_ASYNC_TSC)       /* No fixed tsc if the gip timer is in async mode. */
                return true;
        }

        /*
         * By model.
         */
        if (CPUMGetHostCpuVendor(pVM) == CPUMCPUVENDOR_AMD)
        {
            /*
             * AuthenticAMD - Check for APM support and that TscInvariant is set.
             *
             * This test isn't correct with respect to fixed/non-fixed TSC and
             * older models, but this isn't relevant since the result is currently
             * only used for making a decision on AMD-V models.
             */
# if 0 /* Promoted to generic */
            ASMCpuId(0x80000000, &uEAX, &uEBX, &uECX, &uEDX);
            if (uEAX >= 0x80000007)
            {
                ASMCpuId(0x80000007, &uEAX, &uEBX, &uECX, &uEDX);
                if (   (uEDX & X86_CPUID_AMD_ADVPOWER_EDX_TSCINVAR) /* TscInvariant */
                    && (   enmGipMode == SUPGIPMODE_SYNC_TSC     /* No fixed tsc if the gip timer is in async mode. */
                        || enmGipMode == SUPGIPMODE_INVARIANT_TSC))
                    return true;
            }
# endif
        }
        else if (CPUMGetHostCpuVendor(pVM) == CPUMCPUVENDOR_INTEL)
        {
            /*
             * GenuineIntel - Check the model number.
             *
             * This test is lacking in the same way and for the same reasons
             * as the AMD test above.
             */
            /** @todo use RTX86GetCpuFamily() and RTX86GetCpuModel() here. */
            ASMCpuId(1, &uEAX, &uEBX, &uECX, &uEDX);
            unsigned uModel  = (uEAX >> 4) & 0x0f;
            unsigned uFamily = (uEAX >> 8) & 0x0f;
            if (uFamily == 0x0f)
                uFamily += (uEAX >> 20) & 0xff;
            if (uFamily >= 0x06)
                uModel += ((uEAX >> 16) & 0x0f) << 4;
            if (    (uFamily == 0x0f /*P4*/     && uModel >= 0x03)
                ||  (uFamily == 0x06 /*P2/P3*/  && uModel >= 0x0e))
                return true;
        }
        else if (CPUMGetHostCpuVendor(pVM) == CPUMCPUVENDOR_VIA)
        {
            /*
             * CentaurHauls - Check the model, family and stepping.
             *
             * This only checks for VIA CPU models Nano X2, Nano X3,
             * Eden X2 and QuadCore.
             */
            /** @todo use RTX86GetCpuFamily() and RTX86GetCpuModel() here. */
            ASMCpuId(1, &uEAX, &uEBX, &uECX, &uEDX);
            unsigned uStepping = (uEAX & 0x0f);
            unsigned uModel    = (uEAX >> 4) & 0x0f;
            unsigned uFamily   = (uEAX >> 8) & 0x0f;
            if (   uFamily   == 0x06
                && uModel    == 0x0f
                && uStepping >= 0x0c
                && uStepping <= 0x0f)
                return true;
        }
        else if (CPUMGetHostCpuVendor(pVM) == CPUMCPUVENDOR_SHANGHAI)
        {
            /*
             * Shanghai - Check the model, family and stepping.
             */
            /** @todo use RTX86GetCpuFamily() and RTX86GetCpuModel() here. */
            ASMCpuId(1, &uEAX, &uEBX, &uECX, &uEDX);
            unsigned uFamily   = (uEAX >> 8) & 0x0f;
            if (   uFamily == 0x06
                || uFamily == 0x07)
            {
                return true;
            }
        }
    }

# else  /* !X86 && !AMD64 */
    RT_NOREF_PV(pVM);
# endif /* !X86 && !AMD64 */
    return false;
}


/**
 * Calibrate the CPU tick.
 *
 * @returns Number of ticks per second.
 */
static uint64_t tmR3CalibrateTSC(void)
{
    uint64_t u64Hz;

    /*
     * Use GIP when available. Prefere the nominal one, no need to wait for it.
     */
    PSUPGLOBALINFOPAGE pGip = g_pSUPGlobalInfoPage;
    if (pGip)
    {
        u64Hz = pGip->u64CpuHz;
        if (u64Hz < _1T && u64Hz > _1M)
            return u64Hz;
        AssertFailed(); /* This shouldn't happen. */

        u64Hz = SUPGetCpuHzFromGip(pGip);
        if (u64Hz < _1T && u64Hz > _1M)
            return u64Hz;

        AssertFailed(); /* This shouldn't happen. */
    }
    else
        Assert(SUPR3IsDriverless());

    /* Call this once first to make sure it's initialized. */
    RTTimeNanoTS();

    /*
     * Yield the CPU to increase our chances of getting a correct value.
     */
    RTThreadYield();                    /* Try avoid interruptions between TSC and NanoTS samplings. */
    static const unsigned   s_auSleep[5] = { 50, 30, 30, 40, 40 };
    uint64_t                au64Samples[5];
    unsigned                i;
    for (i = 0; i < RT_ELEMENTS(au64Samples); i++)
    {
        RTMSINTERVAL    cMillies;
        int             cTries   = 5;
        uint64_t        u64Start = ASMReadTSC();
        uint64_t        u64End;
        uint64_t        StartTS  = RTTimeNanoTS();
        uint64_t        EndTS;
        do
        {
            RTThreadSleep(s_auSleep[i]);
            u64End = ASMReadTSC();
            EndTS  = RTTimeNanoTS();
            cMillies = (RTMSINTERVAL)((EndTS - StartTS + 500000) / 1000000);
        } while (   cMillies == 0       /* the sleep may be interrupted... */
                 || (cMillies < 20 && --cTries > 0));
        uint64_t    u64Diff = u64End - u64Start;

        au64Samples[i] = (u64Diff * 1000) / cMillies;
        AssertMsg(cTries > 0, ("cMillies=%d i=%d\n", cMillies, i));
    }

    /*
     * Discard the highest and lowest results and calculate the average.
     */
    unsigned iHigh = 0;
    unsigned iLow = 0;
    for (i = 1; i < RT_ELEMENTS(au64Samples); i++)
    {
        if (au64Samples[i] < au64Samples[iLow])
            iLow = i;
        if (au64Samples[i] > au64Samples[iHigh])
            iHigh = i;
    }
    au64Samples[iLow] = 0;
    au64Samples[iHigh] = 0;

    u64Hz = au64Samples[0];
    for (i = 1; i < RT_ELEMENTS(au64Samples); i++)
        u64Hz += au64Samples[i];
    u64Hz /= RT_ELEMENTS(au64Samples) - 2;

    return u64Hz;
}

#ifdef TM_SECONDS_TO_AUTOMATIC_POWER_OFF
# include <iprt/message.h>
/** @callback_method_impl{FNTMTIMERINT} */
static DECLCALLBACK(void) tmR3AutoPowerOffTimer(PVM pVM, TMTIMERHANDLE hTimer, void *pvUser)
{
    RT_NOREF(hTimer, pvUser);
    RTMsgInfo("The automatic power off timer fired...\n");
    LogRel(("The automatic power off timer fired...\n"));
    int rc = VMR3ReqCallNoWait(pVM, VMCPUID_ANY_QUEUE, (PFNRT)VMR3PowerOff, 1, pVM->pUVM);
    AssertLogRelRC(rc);
}
#endif


/**
 * Finalizes the TM initialization.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMM_INT_DECL(int) TMR3InitFinalize(PVM pVM)
{
    int rc;

#ifndef VBOX_WITHOUT_NS_ACCOUNTING
    /*
     * Create a timer for refreshing the CPU load stats.
     */
    TMTIMERHANDLE hTimer;
    rc = TMR3TimerCreate(pVM, TMCLOCK_REAL, tmR3CpuLoadTimer, NULL, TMTIMER_FLAGS_NO_RING0, "CPU Load Timer", &hTimer);
    if (RT_SUCCESS(rc))
        rc = TMTimerSetMillies(pVM, hTimer, 1000);
#endif

    /*
     * GIM is now initialized. Determine if TSC mode switching is allowed (respecting CFGM override).
     */
    pVM->tm.s.fTSCModeSwitchAllowed &= tmR3HasFixedTSC(pVM) && GIMIsEnabled(pVM);
    LogRel(("TM: TMR3InitFinalize: fTSCModeSwitchAllowed=%RTbool\n", pVM->tm.s.fTSCModeSwitchAllowed));

    /*
     * Grow the virtual & real timer tables so we've got sufficient
     * space for dynamically created timers.  We cannot allocate more
     * after ring-0 init completes.
     */
    static struct { uint32_t idxQueue, cExtra; } s_aExtra[] = { {TMCLOCK_VIRTUAL, 128}, {TMCLOCK_REAL, 32} };
    for (uint32_t i = 0; i < RT_ELEMENTS(s_aExtra); i++)
    {
        PTMTIMERQUEUE pQueue = &pVM->tm.s.aTimerQueues[s_aExtra[i].idxQueue];
        PDMCritSectRwEnterExcl(pVM, &pQueue->AllocLock, VERR_IGNORED);
        if (s_aExtra[i].cExtra > pQueue->cTimersFree)
        {
            uint32_t cTimersAlloc = pQueue->cTimersAlloc + s_aExtra[i].cExtra - pQueue->cTimersFree;
            rc = tmR3TimerQueueGrow(pVM, pQueue, cTimersAlloc);
            AssertLogRelMsgReturn(RT_SUCCESS(rc), ("rc=%Rrc cTimersAlloc=%u %s\n", rc, cTimersAlloc, pQueue->szName), rc);
        }
        PDMCritSectRwLeaveExcl(pVM, &pQueue->AllocLock);
    }

#ifdef VBOX_WITH_STATISTICS
    /*
     * Register timer statistics now that we've fixed the timer table sizes.
     */
    for (uint32_t idxQueue = 0; idxQueue < RT_ELEMENTS(pVM->tm.s.aTimerQueues); idxQueue++)
    {
        pVM->tm.s.aTimerQueues[idxQueue].fCannotGrow = true;
        tmR3TimerQueueRegisterStats(pVM, &pVM->tm.s.aTimerQueues[idxQueue], UINT32_MAX);
    }
#endif

#ifdef TM_SECONDS_TO_AUTOMATIC_POWER_OFF
    /*
     * Automatic VM shutdown timer.
     */
    rc = TMR3TimerCreate(pVM, TMCLOCK_VIRTUAL, tmR3AutoPowerOffTimer, NULL, TMTIMER_FLAGS_NO_RING0,
                         "Auto power off after " RT_XSTR(TM_SECONDS_TO_AUTOMATIC_POWER_OFF) " sec", &hTimer);
    AssertLogRelRCReturn(rc, rc);
    TMTimerSetMillies(pVM, hTimer, TM_SECONDS_TO_AUTOMATIC_POWER_OFF * RT_MS_1SEC);
    pVM->tm.s.hAutoPowerOff = hTimer;
#endif

    return rc;
}


/**
 * Applies relocations to data and code managed by this
 * component. This function will be called at init and
 * whenever the VMM need to relocate it self inside the GC.
 *
 * @param   pVM     The cross context VM structure.
 * @param   offDelta    Relocation delta relative to old location.
 */
VMM_INT_DECL(void) TMR3Relocate(PVM pVM, RTGCINTPTR offDelta)
{
    LogFlow(("TMR3Relocate\n"));
    RT_NOREF(pVM, offDelta);
}


/**
 * Terminates the TM.
 *
 * Termination means cleaning up and freeing all resources,
 * the VM it self is at this point powered off or suspended.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMM_INT_DECL(int) TMR3Term(PVM pVM)
{
    if (pVM->tm.s.pTimer)
    {
        int rc = RTTimerDestroy(pVM->tm.s.pTimer);
        AssertRC(rc);
        pVM->tm.s.pTimer = NULL;
    }

    return VINF_SUCCESS;
}


/**
 * The VM is being reset.
 *
 * For the TM component this means that a rescheduling is preformed,
 * the FF is cleared and but without running the queues. We'll have to
 * check if this makes sense or not, but it seems like a good idea now....
 *
 * @param   pVM     The cross context VM structure.
 */
VMM_INT_DECL(void) TMR3Reset(PVM pVM)
{
    LogFlow(("TMR3Reset:\n"));
    VM_ASSERT_EMT(pVM);

    /*
     * Abort any pending catch up.
     * This isn't perfect...
     */
    if (pVM->tm.s.fVirtualSyncCatchUp)
    {
        const uint64_t offVirtualNow = TMVirtualGetNoCheck(pVM);
        const uint64_t offVirtualSyncNow = TMVirtualSyncGetNoCheck(pVM);
        if (pVM->tm.s.fVirtualSyncCatchUp)
        {
            STAM_PROFILE_ADV_STOP(&pVM->tm.s.StatVirtualSyncCatchup, c);

            const uint64_t offOld = pVM->tm.s.offVirtualSyncGivenUp;
            const uint64_t offNew = offVirtualNow - offVirtualSyncNow;
            Assert(offOld <= offNew);
            ASMAtomicWriteU64((uint64_t volatile *)&pVM->tm.s.offVirtualSyncGivenUp, offNew);
            ASMAtomicWriteU64((uint64_t volatile *)&pVM->tm.s.offVirtualSync, offNew);
            ASMAtomicWriteBool(&pVM->tm.s.fVirtualSyncCatchUp, false);
            LogRel(("TM: Aborting catch-up attempt on reset with a %'RU64 ns lag on reset; new total: %'RU64 ns\n", offNew - offOld, offNew));
        }
    }

    /*
     * Process the queues.
     */
    for (uint32_t idxQueue = 0; idxQueue < RT_ELEMENTS(pVM->tm.s.aTimerQueues); idxQueue++)
    {
        PTMTIMERQUEUE pQueue = &pVM->tm.s.aTimerQueues[idxQueue];
        PDMCritSectEnter(pVM, &pQueue->TimerLock, VERR_IGNORED);
        tmTimerQueueSchedule(pVM, pQueue, pQueue);
        PDMCritSectLeave(pVM, &pQueue->TimerLock);
    }
#ifdef VBOX_STRICT
    tmTimerQueuesSanityChecks(pVM, "TMR3Reset");
#endif

    PVMCPU pVCpuDst = pVM->apCpusR3[pVM->tm.s.idTimerCpu];
    VMCPU_FF_CLEAR(pVCpuDst, VMCPU_FF_TIMER); /** @todo FIXME: this isn't right. */

    /*
     * Switch TM TSC mode back to the original mode after a reset for
     * paravirtualized guests that alter the TM TSC mode during operation.
     * We're already in an EMT rendezvous at this point.
     */
    if (   pVM->tm.s.fTSCModeSwitchAllowed
        && pVM->tm.s.enmTSCMode != pVM->tm.s.enmOriginalTSCMode)
    {
        VM_ASSERT_EMT0(pVM);
        tmR3CpuTickParavirtDisable(pVM, pVM->apCpusR3[0], NULL /* pvData */);
    }
    Assert(!GIMIsParavirtTscEnabled(pVM));
    pVM->tm.s.fParavirtTscEnabled = false;

    /*
     * Reset TSC to avoid a Windows 8+ bug (see @bugref{8926}). If Windows
     * sees TSC value beyond 0x40000000000 at startup, it will reset the
     * TSC on boot-up CPU only, causing confusion and mayhem with SMP.
     */
    VM_ASSERT_EMT0(pVM);
    uint64_t offTscRawSrc;
    switch (pVM->tm.s.enmTSCMode)
    {
        case TMTSCMODE_REAL_TSC_OFFSET:
            offTscRawSrc = SUPReadTsc() * pVM->tm.s.u8TSCMultiplier;
            break;
        case TMTSCMODE_DYNAMIC:
        case TMTSCMODE_VIRT_TSC_EMULATED:
            offTscRawSrc = TMVirtualSyncGetNoCheck(pVM);
            offTscRawSrc = ASMMultU64ByU32DivByU32(offTscRawSrc, pVM->tm.s.cTSCTicksPerSecond, TMCLOCK_FREQ_VIRTUAL);
            break;
        case TMTSCMODE_NATIVE_API:
            /** @todo NEM TSC reset on reset for Windows8+ bug workaround. */
            offTscRawSrc = 0;
            break;
        default:
            AssertFailedBreakStmt(offTscRawSrc = 0);
    }
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];
        pVCpu->tm.s.offTSCRawSrc   = offTscRawSrc;
        pVCpu->tm.s.u64TSC         = 0;
        pVCpu->tm.s.u64TSCLastSeen = 0;
#if defined(VBOX_VMM_TARGET_ARMV8)
        pVCpu->cNsVTimerActivate   = UINT64_MAX;
#endif
    }
}


/**
 * Execute state save operation.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pSSM            SSM operation handle.
 */
static DECLCALLBACK(int) tmR3Save(PVM pVM, PSSMHANDLE pSSM)
{
    LogFlow(("tmR3Save:\n"));
#ifdef VBOX_STRICT
    for (VMCPUID i = 0; i < pVM->cCpus; i++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[i];
        Assert(!pVCpu->tm.s.fTSCTicking);
    }
    Assert(!pVM->tm.s.cVirtualTicking);
    Assert(!pVM->tm.s.fVirtualSyncTicking);
    Assert(!pVM->tm.s.cTSCsTicking);
#endif

    /*
     * Save the virtual clocks.
     */
    /* the virtual clock. */
    SSMR3PutU64(pSSM, TMCLOCK_FREQ_VIRTUAL);
    SSMR3PutU64(pSSM, pVM->tm.s.u64Virtual);

    /* the virtual timer synchronous clock. */
    SSMR3PutU64(pSSM, pVM->tm.s.u64VirtualSync);
    SSMR3PutU64(pSSM, pVM->tm.s.offVirtualSync);
    SSMR3PutU64(pSSM, pVM->tm.s.offVirtualSyncGivenUp);
    SSMR3PutU64(pSSM, pVM->tm.s.u64VirtualSyncCatchUpPrev);
    SSMR3PutBool(pSSM, pVM->tm.s.fVirtualSyncCatchUp);

    /* real time clock */
    SSMR3PutU64(pSSM, TMCLOCK_FREQ_REAL);

    /* the cpu tick clock. */
    for (VMCPUID i = 0; i < pVM->cCpus; i++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[i];
        SSMR3PutU64(pSSM, TMCpuTickGet(pVCpu));
    }
    return SSMR3PutU64(pSSM, pVM->tm.s.cTSCTicksPerSecond);
}


/**
 * Execute state load operation.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pSSM            SSM operation handle.
 * @param   uVersion        Data layout version.
 * @param   uPass           The data pass.
 */
static DECLCALLBACK(int) tmR3Load(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    LogFlow(("tmR3Load:\n"));

    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);
#ifdef VBOX_STRICT
    for (VMCPUID i = 0; i < pVM->cCpus; i++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[i];
        Assert(!pVCpu->tm.s.fTSCTicking);
    }
    Assert(!pVM->tm.s.cVirtualTicking);
    Assert(!pVM->tm.s.fVirtualSyncTicking);
    Assert(!pVM->tm.s.cTSCsTicking);
#endif

    /*
     * Validate version.
     */
    if (uVersion != TM_SAVED_STATE_VERSION)
    {
        AssertMsgFailed(("tmR3Load: Invalid version uVersion=%d!\n", uVersion));
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    }

    /*
     * Load the virtual clock.
     */
    pVM->tm.s.cVirtualTicking = 0;
    /* the virtual clock. */
    uint64_t u64Hz;
    int rc = SSMR3GetU64(pSSM, &u64Hz);
    if (RT_FAILURE(rc))
        return rc;
    if (u64Hz != TMCLOCK_FREQ_VIRTUAL)
    {
        AssertMsgFailed(("The virtual clock frequency differs! Saved: %'RU64 Binary: %'RU64\n",
                         u64Hz, TMCLOCK_FREQ_VIRTUAL));
        return VERR_SSM_VIRTUAL_CLOCK_HZ;
    }
    SSMR3GetU64(pSSM, &pVM->tm.s.u64Virtual);
    pVM->tm.s.u64VirtualOffset = 0;

    /* the virtual timer synchronous clock. */
    pVM->tm.s.fVirtualSyncTicking = false;
    uint64_t u64;
    SSMR3GetU64(pSSM, &u64);
    pVM->tm.s.u64VirtualSync = u64;
    SSMR3GetU64(pSSM, &u64);
    pVM->tm.s.offVirtualSync = u64;
    SSMR3GetU64(pSSM, &u64);
    pVM->tm.s.offVirtualSyncGivenUp = u64;
    SSMR3GetU64(pSSM, &u64);
    pVM->tm.s.u64VirtualSyncCatchUpPrev = u64;
    bool f;
    SSMR3GetBool(pSSM, &f);
    pVM->tm.s.fVirtualSyncCatchUp = f;

    /* the real clock  */
    rc = SSMR3GetU64(pSSM, &u64Hz);
    if (RT_FAILURE(rc))
        return rc;
    if (u64Hz != TMCLOCK_FREQ_REAL)
    {
        AssertMsgFailed(("The real clock frequency differs! Saved: %'RU64 Binary: %'RU64\n",
                         u64Hz, TMCLOCK_FREQ_REAL));
        return VERR_SSM_VIRTUAL_CLOCK_HZ; /* misleading... */
    }

    /* the cpu tick clock. */
    pVM->tm.s.cTSCsTicking = 0;
    pVM->tm.s.offTSCPause = 0;
    pVM->tm.s.u64LastPausedTSC = 0;
    for (VMCPUID i = 0; i < pVM->cCpus; i++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[i];

        pVCpu->tm.s.fTSCTicking = false;
        SSMR3GetU64(pSSM, &pVCpu->tm.s.u64TSC);
        if (pVM->tm.s.u64LastPausedTSC < pVCpu->tm.s.u64TSC)
            pVM->tm.s.u64LastPausedTSC = pVCpu->tm.s.u64TSC;

        if (pVM->tm.s.enmTSCMode == TMTSCMODE_REAL_TSC_OFFSET)
            pVCpu->tm.s.offTSCRawSrc = 0; /** @todo TSC restore stuff and HWACC. */
    }

    rc = SSMR3GetU64(pSSM, &u64Hz);
    if (RT_FAILURE(rc))
        return rc;
    if (pVM->tm.s.enmTSCMode != TMTSCMODE_REAL_TSC_OFFSET)
        pVM->tm.s.cTSCTicksPerSecond = u64Hz;

    LogRel(("TM: cTSCTicksPerSecond=%#RX64 (%'RU64) enmTSCMode=%d (%s) (state load)\n",
            pVM->tm.s.cTSCTicksPerSecond, pVM->tm.s.cTSCTicksPerSecond, pVM->tm.s.enmTSCMode, tmR3GetTSCModeName(pVM)));

    /* Disabled as this isn't tested, also should this apply only if GIM is enabled etc. */
#if 0
    /*
     * If the current host TSC frequency is incompatible with what is in the
     * saved state of the VM, fall back to emulating TSC and disallow TSC mode
     * switches during VM runtime (e.g. by GIM).
     */
    if (   GIMIsEnabled(pVM)
        || pVM->tm.s.enmTSCMode == TMTSCMODE_REAL_TSC_OFFSET)
    {
        uint64_t uGipCpuHz;
        bool fRelax  = RTSystemIsInsideVM();
        bool fCompat = SUPIsTscFreqCompatible(pVM->tm.s.cTSCTicksPerSecond, &uGipCpuHz, fRelax);
        if (!fCompat)
        {
            pVM->tm.s.enmTSCMode = TMTSCMODE_VIRT_TSC_EMULATED;
            pVM->tm.s.fTSCModeSwitchAllowed = false;
            if (g_pSUPGlobalInfoPage->u32Mode != SUPGIPMODE_ASYNC_TSC)
            {
                LogRel(("TM: TSC frequency incompatible! uGipCpuHz=%#RX64 (%'RU64) enmTSCMode=%d (%s) fTSCModeSwitchAllowed=%RTbool (state load)\n",
                        uGipCpuHz, uGipCpuHz, pVM->tm.s.enmTSCMode, tmR3GetTSCModeName(pVM), pVM->tm.s.fTSCModeSwitchAllowed));
            }
            else
            {
                LogRel(("TM: GIP is async, enmTSCMode=%d (%s) fTSCModeSwitchAllowed=%RTbool (state load)\n",
                        uGipCpuHz, uGipCpuHz, pVM->tm.s.enmTSCMode, tmR3GetTSCModeName(pVM), pVM->tm.s.fTSCModeSwitchAllowed));
            }
        }
    }
#endif

    /*
     * Make sure timers get rescheduled immediately.
     */
    PVMCPU pVCpuDst = pVM->apCpusR3[pVM->tm.s.idTimerCpu];
    VMCPU_FF_SET(pVCpuDst, VMCPU_FF_TIMER);

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMINTLOADDONE, For rearming autopoweroff}
 */
static DECLCALLBACK(int) tmR3LoadDone(PVM pVM, PSSMHANDLE pSSM)
{
    RT_NOREF(pVM, pSSM);
#ifdef TM_SECONDS_TO_AUTOMATIC_POWER_OFF
    TMTimerSetMillies(pVM, pVM->tm.s.hAutoPowerOff, TM_SECONDS_TO_AUTOMATIC_POWER_OFF * RT_MS_1SEC);
#endif
    return VINF_SUCCESS;
}


#ifdef VBOX_WITH_STATISTICS

/**
 * Register statistics for a timer.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pQueue      The queue the timer belongs to.
 * @param   pTimer      The timer to register statistics for.
 */
static void tmR3TimerRegisterStats(PVM pVM, PTMTIMERQUEUE pQueue, PTMTIMER pTimer)
{
    STAMR3RegisterF(pVM, &pTimer->StatTimer,            STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,
                    pQueue->szName, "/TM/Timers/%s", pTimer->szName);
    STAMR3RegisterF(pVM, &pTimer->StatCritSectEnter,    STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,
                    "",                         "/TM/Timers/%s/CritSectEnter", pTimer->szName);
    STAMR3RegisterF(pVM, &pTimer->StatGet,              STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_CALLS,
                    "",                         "/TM/Timers/%s/Get", pTimer->szName);
    STAMR3RegisterF(pVM, &pTimer->StatSetAbsolute,      STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_CALLS,
                    "",                         "/TM/Timers/%s/SetAbsolute", pTimer->szName);
    STAMR3RegisterF(pVM, &pTimer->StatSetRelative,      STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_CALLS,
                    "",                         "/TM/Timers/%s/SetRelative", pTimer->szName);
    STAMR3RegisterF(pVM, &pTimer->StatStop,             STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_CALLS,
                    "",                         "/TM/Timers/%s/Stop", pTimer->szName);
}


/**
 * Deregister the statistics for a timer.
 */
static void tmR3TimerDeregisterStats(PVM pVM, PTMTIMER pTimer)
{
    char szPrefix[128];
    size_t cchPrefix = RTStrPrintf(szPrefix, sizeof(szPrefix), "/TM/Timers/%s/", pTimer->szName);
    STAMR3DeregisterByPrefix(pVM->pUVM, szPrefix);
    szPrefix[cchPrefix - 1] = '\0';
    STAMR3Deregister(pVM->pUVM, szPrefix);
}


/**
 * Register statistics for all allocated timers in a queue.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pQueue      The queue to register statistics for.
 * @param   cTimers     Number of timers to consider (in growth scenario).
 */
static void tmR3TimerQueueRegisterStats(PVM pVM, PTMTIMERQUEUE pQueue, uint32_t cTimers)
{
    uint32_t idxTimer = RT_MIN(cTimers, pQueue->cTimersAlloc);
    while (idxTimer-- > 0)
    {
        PTMTIMER     pTimer   = &pQueue->paTimers[idxTimer];
        TMTIMERSTATE enmState = pTimer->enmState;
        if (enmState > TMTIMERSTATE_INVALID && enmState < TMTIMERSTATE_DESTROY)
            tmR3TimerRegisterStats(pVM, pQueue, pTimer);
    }
}

#endif /* VBOX_WITH_STATISTICS */


/**
 * Grows a timer queue.
 *
 * @returns VBox status code (errors are LogRel'ed already).
 * @param   pVM         The cross context VM structure.
 * @param   pQueue      The timer queue to grow.
 * @param   cNewTimers  The minimum number of timers after growing.
 * @note    Caller owns the queue's allocation lock.
 */
static int tmR3TimerQueueGrow(PVM pVM, PTMTIMERQUEUE pQueue, uint32_t cNewTimers)
{
    /*
     * Validate input and state.
     */
    VM_ASSERT_EMT0_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);
    VM_ASSERT_STATE_RETURN(pVM, VMSTATE_CREATING, VERR_VM_INVALID_VM_STATE); /** @todo must do better than this! */
    AssertReturn(!pQueue->fCannotGrow, VERR_TM_TIMER_QUEUE_CANNOT_GROW);

    uint32_t const cOldEntries = pQueue->cTimersAlloc;
    AssertReturn(cNewTimers > cOldEntries, VERR_TM_IPE_1);
    AssertReturn(cNewTimers < _32K, VERR_TM_IPE_1);

    /*
     * Do the growing.
     */
    int rc;
    if (!SUPR3IsDriverless())
    {
        rc = VMMR3CallR0Emt(pVM, VMMGetCpu(pVM), VMMR0_DO_TM_GROW_TIMER_QUEUE,
                            RT_MAKE_U64(cNewTimers, (uint64_t)(pQueue - &pVM->tm.s.aTimerQueues[0])), NULL);
        AssertLogRelRCReturn(rc, rc);
        AssertReturn(pQueue->cTimersAlloc >= cNewTimers, VERR_TM_IPE_3);
    }
    else
    {
        AssertReturn(cNewTimers <= _32K && cOldEntries <= _32K, VERR_TM_TOO_MANY_TIMERS);
        ASMCompilerBarrier();

        /*
         * Round up the request to the nearest page and do the allocation.
         */
        size_t cbNew = sizeof(TMTIMER) * cNewTimers;
        cbNew = RT_ALIGN_Z(cbNew, HOST_PAGE_SIZE);
        cNewTimers = (uint32_t)(cbNew / sizeof(TMTIMER));

        PTMTIMER paTimers = (PTMTIMER)RTMemPageAllocZ(cbNew);
        if (paTimers)
        {
            /*
             * Copy over the old timer, init the new free ones, then switch over
             * and free the old ones.
             */
            PTMTIMER const paOldTimers = pQueue->paTimers;
            tmHCTimerQueueGrowInit(paTimers, paOldTimers, cNewTimers, cOldEntries);

            pQueue->paTimers      = paTimers;
            pQueue->cTimersAlloc  = cNewTimers;
            pQueue->cTimersFree  += cNewTimers - (cOldEntries ? cOldEntries : 1);

            RTMemPageFree(paOldTimers, RT_ALIGN_Z(sizeof(TMTIMER) * cOldEntries, HOST_PAGE_SIZE));
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_NO_PAGE_MEMORY;
    }
    return rc;
}


/**
 * Internal TMR3TimerCreate worker.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   enmClock    The timer clock.
 * @param   fFlags      TMTIMER_FLAGS_XXX.
 * @param   pszName     The timer name.
 * @param   ppTimer     Where to store the timer pointer on success.
 */
static int tmr3TimerCreate(PVM pVM, TMCLOCK enmClock, uint32_t fFlags, const char *pszName, PPTMTIMERR3 ppTimer)
{
    PTMTIMER pTimer;

    /*
     * Validate input.
     */
    VM_ASSERT_EMT(pVM);

    AssertReturn((fFlags & (TMTIMER_FLAGS_RING0 | TMTIMER_FLAGS_NO_RING0)) != (TMTIMER_FLAGS_RING0 | TMTIMER_FLAGS_NO_RING0),
                 VERR_INVALID_FLAGS);

    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    size_t const cchName = strlen(pszName);
    AssertMsgReturn(cchName < sizeof(pTimer->szName), ("timer name too long: %s\n", pszName), VERR_INVALID_NAME);
    AssertMsgReturn(cchName > 2,  ("Too short timer name: %s\n", pszName), VERR_INVALID_NAME);

    AssertMsgReturn(enmClock >= TMCLOCK_REAL && enmClock < TMCLOCK_MAX,
                    ("%d\n", enmClock), VERR_INVALID_PARAMETER);
    AssertReturn(enmClock != TMCLOCK_TSC, VERR_NOT_SUPPORTED);
    if (enmClock == TMCLOCK_VIRTUAL_SYNC)
        VM_ASSERT_STATE_RETURN(pVM, VMSTATE_CREATING, VERR_WRONG_ORDER);

    /*
     * Exclusively lock the queue.
     *
     * Note! This means that it is not possible to allocate timers from a timer callback.
     */
    PTMTIMERQUEUE pQueue = &pVM->tm.s.aTimerQueues[enmClock];
    int rc = PDMCritSectRwEnterExcl(pVM, &pQueue->AllocLock, VERR_IGNORED);
    AssertRCReturn(rc, rc);

    /*
     * Allocate the timer.
     */
    if (!pQueue->cTimersFree)
    {
        rc = tmR3TimerQueueGrow(pVM, pQueue, pQueue->cTimersAlloc + 64);
        AssertRCReturnStmt(rc, PDMCritSectRwLeaveExcl(pVM, &pQueue->AllocLock), rc);
    }

    /* Scan the array for free timers. */
    pTimer = NULL;
    PTMTIMER const paTimers     = pQueue->paTimers;
    uint32_t const cTimersAlloc = pQueue->cTimersAlloc;
    uint32_t       idxTimer     = pQueue->idxFreeHint;
    for (uint32_t iScan = 0; iScan < 2; iScan++)
    {
        while (idxTimer < cTimersAlloc)
        {
            if (paTimers[idxTimer].enmState == TMTIMERSTATE_FREE)
            {
                pTimer = &paTimers[idxTimer];
                pQueue->idxFreeHint = idxTimer + 1;
                break;
            }
            idxTimer++;
        }
        if (pTimer != NULL)
            break;
        idxTimer = 1;
    }
    AssertLogRelMsgReturnStmt(pTimer != NULL, ("cTimersFree=%u cTimersAlloc=%u enmClock=%s\n", pQueue->cTimersFree,
                                               pQueue->cTimersAlloc, pQueue->szName),
                              PDMCritSectRwLeaveExcl(pVM, &pQueue->AllocLock), VERR_INTERNAL_ERROR_3);
    pQueue->cTimersFree -= 1;

    /*
     * Initialize it.
     */
    Assert(idxTimer != 0);
    Assert(idxTimer <= TMTIMERHANDLE_TIMER_IDX_MASK);
    pTimer->hSelf           = idxTimer
                            | ((uintptr_t)(pQueue - &pVM->tm.s.aTimerQueues[0]) << TMTIMERHANDLE_QUEUE_IDX_SHIFT);
    Assert(!(pTimer->hSelf & TMTIMERHANDLE_RANDOM_MASK));
    pTimer->hSelf          |= (RTRandU64() & TMTIMERHANDLE_RANDOM_MASK);

    pTimer->u64Expire       = 0;
    pTimer->enmState        = TMTIMERSTATE_STOPPED;
    pTimer->idxScheduleNext = UINT32_MAX;
    pTimer->idxNext         = UINT32_MAX;
    pTimer->idxPrev         = UINT32_MAX;
    pTimer->fFlags          = fFlags;
    pTimer->uHzHint         = 0;
    pTimer->pvUser          = NULL;
    pTimer->pCritSect       = NULL;
    memcpy(pTimer->szName, pszName, cchName);
    pTimer->szName[cchName] = '\0';

#ifdef VBOX_STRICT
    tmTimerQueuesSanityChecks(pVM, "tmR3TimerCreate");
#endif

    PDMCritSectRwLeaveExcl(pVM, &pQueue->AllocLock);

#ifdef VBOX_WITH_STATISTICS
    /*
     * Only register statistics if we're passed the no-realloc point.
     */
    if (pQueue->fCannotGrow)
        tmR3TimerRegisterStats(pVM, pQueue, pTimer);
#endif

    *ppTimer = pTimer;
    return VINF_SUCCESS;
}


/**
 * Creates a device timer.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pDevIns         Device instance.
 * @param   enmClock        The clock to use on this timer.
 * @param   pfnCallback     Callback function.
 * @param   pvUser          The user argument to the callback.
 * @param   fFlags          Timer creation flags, see grp_tm_timer_flags.
 * @param   pszName         Timer name (will be copied).  Max 31 chars.
 * @param   phTimer         Where to store the timer handle on success.
 */
VMM_INT_DECL(int) TMR3TimerCreateDevice(PVM pVM, PPDMDEVINS pDevIns, TMCLOCK enmClock,
                                        PFNTMTIMERDEV pfnCallback, void *pvUser,
                                        uint32_t fFlags, const char *pszName, PTMTIMERHANDLE phTimer)
{
    AssertReturn(!(fFlags & ~(TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_RING0 | TMTIMER_FLAGS_NO_RING0)),
                 VERR_INVALID_FLAGS);

    /*
     * Allocate and init stuff.
     */
    PTMTIMER pTimer;
    int rc = tmr3TimerCreate(pVM, enmClock, fFlags, pszName, &pTimer);
    if (RT_SUCCESS(rc))
    {
        pTimer->enmType         = TMTIMERTYPE_DEV;
        pTimer->u.Dev.pfnTimer  = pfnCallback;
        pTimer->u.Dev.pDevIns   = pDevIns;
        pTimer->pvUser          = pvUser;
        if (!(fFlags & TMTIMER_FLAGS_NO_CRIT_SECT))
            pTimer->pCritSect = PDMR3DevGetCritSect(pVM, pDevIns);
        *phTimer = pTimer->hSelf;
        Log(("TM: Created device timer %p clock %d callback %p '%s'\n", phTimer, enmClock, pfnCallback, pszName));
    }

    return rc;
}




/**
 * Creates a USB device timer.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pUsbIns         The USB device instance.
 * @param   enmClock        The clock to use on this timer.
 * @param   pfnCallback     Callback function.
 * @param   pvUser          The user argument to the callback.
 * @param   fFlags          Timer creation flags, see grp_tm_timer_flags.
 * @param   pszName         Timer name (will be copied).  Max 31 chars.
 * @param   phTimer         Where to store the timer handle on success.
 */
VMM_INT_DECL(int) TMR3TimerCreateUsb(PVM pVM, PPDMUSBINS pUsbIns, TMCLOCK enmClock,
                                     PFNTMTIMERUSB pfnCallback, void *pvUser,
                                     uint32_t fFlags, const char *pszName, PTMTIMERHANDLE phTimer)
{
    AssertReturn(!(fFlags & ~(TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_NO_RING0)), VERR_INVALID_PARAMETER);

    /*
     * Allocate and init stuff.
     */
    PTMTIMER pTimer;
    int rc = tmr3TimerCreate(pVM, enmClock, fFlags, pszName, &pTimer);
    if (RT_SUCCESS(rc))
    {
        pTimer->enmType         = TMTIMERTYPE_USB;
        pTimer->u.Usb.pfnTimer  = pfnCallback;
        pTimer->u.Usb.pUsbIns   = pUsbIns;
        pTimer->pvUser          = pvUser;
        //if (!(fFlags & TMTIMER_FLAGS_NO_CRIT_SECT))
        //{
        //    if (pDevIns->pCritSectR3)
        //        pTimer->pCritSect = pUsbIns->pCritSectR3;
        //    else
        //        pTimer->pCritSect = IOMR3GetCritSect(pVM);
        //}
        *phTimer = pTimer->hSelf;
        Log(("TM: Created USB device timer %p clock %d callback %p '%s'\n", *phTimer, enmClock, pfnCallback, pszName));
    }

    return rc;
}


/**
 * Creates a driver timer.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pDrvIns         Driver instance.
 * @param   enmClock        The clock to use on this timer.
 * @param   pfnCallback     Callback function.
 * @param   pvUser          The user argument to the callback.
 * @param   fFlags          Timer creation flags, see grp_tm_timer_flags.
 * @param   pszName         Timer name (will be copied).  Max 31 chars.
 * @param   phTimer         Where to store the timer handle on success.
 */
VMM_INT_DECL(int) TMR3TimerCreateDriver(PVM pVM, PPDMDRVINS pDrvIns, TMCLOCK enmClock, PFNTMTIMERDRV pfnCallback, void *pvUser,
                                        uint32_t fFlags, const char *pszName, PTMTIMERHANDLE phTimer)
{
    AssertReturn(!(fFlags & ~(TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_RING0 | TMTIMER_FLAGS_NO_RING0)),
                 VERR_INVALID_FLAGS);

    /*
     * Allocate and init stuff.
     */
    PTMTIMER pTimer;
    int rc = tmr3TimerCreate(pVM, enmClock, fFlags, pszName, &pTimer);
    if (RT_SUCCESS(rc))
    {
        pTimer->enmType         = TMTIMERTYPE_DRV;
        pTimer->u.Drv.pfnTimer  = pfnCallback;
        pTimer->u.Drv.pDrvIns   = pDrvIns;
        pTimer->pvUser          = pvUser;
        *phTimer = pTimer->hSelf;
        Log(("TM: Created device timer %p clock %d callback %p '%s'\n", *phTimer, enmClock, pfnCallback, pszName));
    }

    return rc;
}


/**
 * Creates an internal timer.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   enmClock        The clock to use on this timer.
 * @param   pfnCallback     Callback function.
 * @param   pvUser          User argument to be passed to the callback.
 * @param   fFlags          Timer creation flags, see grp_tm_timer_flags.
 * @param   pszName         Timer name (will be copied).  Max 31 chars.
 * @param   phTimer         Where to store the timer handle on success.
 */
VMMR3DECL(int) TMR3TimerCreate(PVM pVM, TMCLOCK enmClock, PFNTMTIMERINT pfnCallback, void *pvUser,
                               uint32_t fFlags, const char *pszName, PTMTIMERHANDLE phTimer)
{
    AssertReturn(fFlags & (TMTIMER_FLAGS_RING0 | TMTIMER_FLAGS_NO_RING0), VERR_INVALID_FLAGS);
    AssertReturn((fFlags & (TMTIMER_FLAGS_RING0 | TMTIMER_FLAGS_NO_RING0)) != (TMTIMER_FLAGS_RING0 | TMTIMER_FLAGS_NO_RING0),
                 VERR_INVALID_FLAGS);

    /*
     * Allocate and init  stuff.
     */
    PTMTIMER pTimer;
    int rc = tmr3TimerCreate(pVM, enmClock, fFlags, pszName, &pTimer);
    if (RT_SUCCESS(rc))
    {
        pTimer->enmType             = TMTIMERTYPE_INTERNAL;
        pTimer->u.Internal.pfnTimer = pfnCallback;
        pTimer->pvUser              = pvUser;
        *phTimer = pTimer->hSelf;
        Log(("TM: Created internal timer %p clock %d callback %p '%s'\n", pTimer, enmClock, pfnCallback, pszName));
    }

    return rc;
}


/**
 * Destroy a timer
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pQueue          The queue the timer is on.
 * @param   pTimer          Timer handle as returned by one of the create functions.
 */
static int tmR3TimerDestroy(PVMCC pVM, PTMTIMERQUEUE pQueue, PTMTIMER pTimer)
{
    bool fActive  = false;
    bool fPending = false;

    AssertMsg(   !pTimer->pCritSect
              || VMR3GetState(pVM) != VMSTATE_RUNNING
              || PDMCritSectIsOwner(pVM, pTimer->pCritSect), ("%s\n", pTimer->szName));

    /*
     * The rest of the game happens behind the lock, just
     * like create does. All the work is done here.
     */
    PDMCritSectRwEnterExcl(pVM, &pQueue->AllocLock, VERR_IGNORED);
    PDMCritSectEnter(pVM, &pQueue->TimerLock, VERR_IGNORED);

    for (int cRetries = 1000;; cRetries--)
    {
        /*
         * Change to the DESTROY state.
         */
        TMTIMERSTATE const enmState = pTimer->enmState;
        Log2(("TMTimerDestroy: %p:{.enmState=%s, .szName='%s'} cRetries=%d\n",
              pTimer, tmTimerState(enmState), pTimer->szName, cRetries));
        switch (enmState)
        {
            case TMTIMERSTATE_STOPPED:
            case TMTIMERSTATE_EXPIRED_DELIVER:
                break;

            case TMTIMERSTATE_ACTIVE:
                fActive     = true;
                break;

            case TMTIMERSTATE_PENDING_STOP:
            case TMTIMERSTATE_PENDING_STOP_SCHEDULE:
            case TMTIMERSTATE_PENDING_RESCHEDULE:
                fActive     = true;
                fPending    = true;
                break;

            case TMTIMERSTATE_PENDING_SCHEDULE:
                fPending    = true;
                break;

            /*
             * This shouldn't happen as the caller should make sure there are no races.
             */
            case TMTIMERSTATE_EXPIRED_GET_UNLINK:
            case TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE:
            case TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE:
                AssertMsgFailed(("%p:.enmState=%s %s\n", pTimer, tmTimerState(enmState), pTimer->szName));
                PDMCritSectLeave(pVM, &pQueue->TimerLock);
                PDMCritSectRwLeaveExcl(pVM, &pQueue->AllocLock);

                AssertMsgReturn(cRetries > 0, ("Failed waiting for stable state. state=%d (%s)\n", pTimer->enmState, pTimer->szName),
                                VERR_TM_UNSTABLE_STATE);
                if (!RTThreadYield())
                    RTThreadSleep(1);

                PDMCritSectRwEnterExcl(pVM, &pQueue->AllocLock, VERR_IGNORED);
                PDMCritSectEnter(pVM, &pQueue->TimerLock, VERR_IGNORED);
                continue;

            /*
             * Invalid states.
             */
            case TMTIMERSTATE_FREE:
            case TMTIMERSTATE_DESTROY:
                PDMCritSectLeave(pVM, &pQueue->TimerLock);
                PDMCritSectRwLeaveExcl(pVM, &pQueue->AllocLock);
                AssertLogRelMsgFailedReturn(("pTimer=%p %s\n", pTimer, tmTimerState(enmState)), VERR_TM_INVALID_STATE);

            default:
                AssertMsgFailed(("Unknown timer state %d (%s)\n", enmState, pTimer->szName));
                PDMCritSectLeave(pVM, &pQueue->TimerLock);
                PDMCritSectRwLeaveExcl(pVM, &pQueue->AllocLock);
                return VERR_TM_UNKNOWN_STATE;
        }

        /*
         * Try switch to the destroy state.
         * This should always succeed as the caller should make sure there are no race.
         */
        bool fRc;
        TM_TRY_SET_STATE(pTimer, TMTIMERSTATE_DESTROY, enmState, fRc);
        if (fRc)
            break;
        AssertMsgFailed(("%p:.enmState=%s %s\n", pTimer, tmTimerState(enmState), pTimer->szName));
        PDMCritSectLeave(pVM, &pQueue->TimerLock);
        PDMCritSectRwLeaveExcl(pVM, &pQueue->AllocLock);

        AssertMsgReturn(cRetries > 0, ("Failed waiting for stable state. state=%d (%s)\n", pTimer->enmState, pTimer->szName),
                        VERR_TM_UNSTABLE_STATE);

        PDMCritSectRwEnterExcl(pVM, &pQueue->AllocLock, VERR_IGNORED);
        PDMCritSectEnter(pVM, &pQueue->TimerLock, VERR_IGNORED);
    }

    /*
     * Unlink from the active list.
     */
    if (fActive)
    {
        const PTMTIMER pPrev = tmTimerGetPrev(pQueue, pTimer);
        const PTMTIMER pNext = tmTimerGetNext(pQueue, pTimer);
        if (pPrev)
            tmTimerSetNext(pQueue, pPrev, pNext);
        else
        {
            tmTimerQueueSetHead(pQueue, pQueue, pNext);
            pQueue->u64Expire = pNext ? pNext->u64Expire : INT64_MAX;
        }
        if (pNext)
            tmTimerSetPrev(pQueue, pNext, pPrev);
        pTimer->idxNext = UINT32_MAX;
        pTimer->idxPrev = UINT32_MAX;
    }

    /*
     * Unlink from the schedule list by running it.
     */
    if (fPending)
    {
        Log3(("TMR3TimerDestroy: tmTimerQueueSchedule\n"));
        STAM_PROFILE_START(&pVM->tm.s.CTX_SUFF_Z(StatScheduleOne), a);
        Assert(pQueue->idxSchedule < pQueue->cTimersAlloc);
        tmTimerQueueSchedule(pVM, pQueue, pQueue);
        STAM_PROFILE_STOP(&pVM->tm.s.CTX_SUFF_Z(StatScheduleOne), a);
    }

#ifdef VBOX_WITH_STATISTICS
    /*
     * Deregister statistics.
     */
    tmR3TimerDeregisterStats(pVM, pTimer);
#endif

    /*
     * Change it to free state and update the queue accordingly.
     */
    Assert(pTimer->idxNext == UINT32_MAX); Assert(pTimer->idxPrev == UINT32_MAX); Assert(pTimer->idxScheduleNext == UINT32_MAX);

    TM_SET_STATE(pTimer, TMTIMERSTATE_FREE);

    pQueue->cTimersFree += 1;
    uint32_t idxTimer = (uint32_t)(pTimer - pQueue->paTimers);
    if (idxTimer < pQueue->idxFreeHint)
        pQueue->idxFreeHint = idxTimer;

#ifdef VBOX_STRICT
    tmTimerQueuesSanityChecks(pVM, "TMR3TimerDestroy");
#endif
    PDMCritSectLeave(pVM, &pQueue->TimerLock);
    PDMCritSectRwLeaveExcl(pVM, &pQueue->AllocLock);
    return VINF_SUCCESS;
}


/**
 * Destroy a timer
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   hTimer      Timer handle as returned by one of the create functions.
 */
VMMR3DECL(int) TMR3TimerDestroy(PVM pVM, TMTIMERHANDLE hTimer)
{
    /* We ignore NILs here. */
    if (hTimer == NIL_TMTIMERHANDLE)
        return VINF_SUCCESS;
    TMTIMER_HANDLE_TO_VARS_RETURN(pVM, hTimer); /* => pTimer, pQueueCC, pQueue, idxTimer, idxQueue */
    return tmR3TimerDestroy(pVM, pQueue, pTimer);
}


/**
 * Destroy all timers owned by a device.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pDevIns         Device which timers should be destroyed.
 */
VMM_INT_DECL(int) TMR3TimerDestroyDevice(PVM pVM, PPDMDEVINS pDevIns)
{
    LogFlow(("TMR3TimerDestroyDevice: pDevIns=%p\n", pDevIns));
    if (!pDevIns)
        return VERR_INVALID_PARAMETER;

    for (uint32_t idxQueue = 0; idxQueue < RT_ELEMENTS(pVM->tm.s.aTimerQueues); idxQueue++)
    {
        PTMTIMERQUEUE pQueue = &pVM->tm.s.aTimerQueues[idxQueue];
        PDMCritSectRwEnterShared(pVM, &pQueue->AllocLock, VERR_IGNORED);
        uint32_t idxTimer = pQueue->cTimersAlloc;
        while (idxTimer-- > 0)
        {
            PTMTIMER pTimer = &pQueue->paTimers[idxTimer];
            if (   pTimer->enmType == TMTIMERTYPE_DEV
                && pTimer->u.Dev.pDevIns == pDevIns
                && pTimer->enmState < TMTIMERSTATE_DESTROY)
            {
                PDMCritSectRwLeaveShared(pVM, &pQueue->AllocLock);

                int rc = tmR3TimerDestroy(pVM, pQueue, pTimer);
                AssertRC(rc);

                PDMCritSectRwEnterShared(pVM, &pQueue->AllocLock, VERR_IGNORED);
            }
        }
        PDMCritSectRwLeaveShared(pVM, &pQueue->AllocLock);
    }

    LogFlow(("TMR3TimerDestroyDevice: returns VINF_SUCCESS\n"));
    return VINF_SUCCESS;
}


/**
 * Destroy all timers owned by a USB device.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pUsbIns         USB device which timers should be destroyed.
 */
VMM_INT_DECL(int) TMR3TimerDestroyUsb(PVM pVM, PPDMUSBINS pUsbIns)
{
    LogFlow(("TMR3TimerDestroyUsb: pUsbIns=%p\n", pUsbIns));
    if (!pUsbIns)
        return VERR_INVALID_PARAMETER;

    for (uint32_t idxQueue = 0; idxQueue < RT_ELEMENTS(pVM->tm.s.aTimerQueues); idxQueue++)
    {
        PTMTIMERQUEUE pQueue = &pVM->tm.s.aTimerQueues[idxQueue];
        PDMCritSectRwEnterShared(pVM, &pQueue->AllocLock, VERR_IGNORED);
        uint32_t idxTimer = pQueue->cTimersAlloc;
        while (idxTimer-- > 0)
        {
            PTMTIMER pTimer = &pQueue->paTimers[idxTimer];
            if (   pTimer->enmType == TMTIMERTYPE_USB
                && pTimer->u.Usb.pUsbIns == pUsbIns
                && pTimer->enmState < TMTIMERSTATE_DESTROY)
            {
                PDMCritSectRwLeaveShared(pVM, &pQueue->AllocLock);

                int rc = tmR3TimerDestroy(pVM, pQueue, pTimer);
                AssertRC(rc);

                PDMCritSectRwEnterShared(pVM, &pQueue->AllocLock, VERR_IGNORED);
            }
        }
        PDMCritSectRwLeaveShared(pVM, &pQueue->AllocLock);
    }

    LogFlow(("TMR3TimerDestroyUsb: returns VINF_SUCCESS\n"));
    return VINF_SUCCESS;
}


/**
 * Destroy all timers owned by a driver.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pDrvIns         Driver which timers should be destroyed.
 */
VMM_INT_DECL(int) TMR3TimerDestroyDriver(PVM pVM, PPDMDRVINS pDrvIns)
{
    LogFlow(("TMR3TimerDestroyDriver: pDrvIns=%p\n", pDrvIns));
    if (!pDrvIns)
        return VERR_INVALID_PARAMETER;

    for (uint32_t idxQueue = 0; idxQueue < RT_ELEMENTS(pVM->tm.s.aTimerQueues); idxQueue++)
    {
        PTMTIMERQUEUE pQueue = &pVM->tm.s.aTimerQueues[idxQueue];
        PDMCritSectRwEnterShared(pVM, &pQueue->AllocLock, VERR_IGNORED);
        uint32_t idxTimer = pQueue->cTimersAlloc;
        while (idxTimer-- > 0)
        {
            PTMTIMER pTimer = &pQueue->paTimers[idxTimer];
            if (   pTimer->enmType == TMTIMERTYPE_DRV
                && pTimer->u.Drv.pDrvIns == pDrvIns
                && pTimer->enmState < TMTIMERSTATE_DESTROY)
            {
                PDMCritSectRwLeaveShared(pVM, &pQueue->AllocLock);

                int rc = tmR3TimerDestroy(pVM, pQueue, pTimer);
                AssertRC(rc);

                PDMCritSectRwEnterShared(pVM, &pQueue->AllocLock, VERR_IGNORED);
            }
        }
        PDMCritSectRwLeaveShared(pVM, &pQueue->AllocLock);
    }

    LogFlow(("TMR3TimerDestroyDriver: returns VINF_SUCCESS\n"));
    return VINF_SUCCESS;
}


/**
 * Internal function for getting the clock time.
 *
 * @returns clock time.
 * @param   pVM         The cross context VM structure.
 * @param   enmClock    The clock.
 */
DECLINLINE(uint64_t) tmClock(PVM pVM, TMCLOCK enmClock)
{
    switch (enmClock)
    {
        case TMCLOCK_VIRTUAL:       return TMVirtualGet(pVM);
        case TMCLOCK_VIRTUAL_SYNC:  return TMVirtualSyncGet(pVM);
        case TMCLOCK_REAL:          return TMRealGet(pVM);
        case TMCLOCK_TSC:           return TMCpuTickGet(pVM->apCpusR3[0] /* just take VCPU 0 */);
        default:
            AssertMsgFailed(("enmClock=%d\n", enmClock));
            return ~(uint64_t)0;
    }
}


/**
 * Checks if the sync queue has one or more expired timers.
 *
 * @returns true / false.
 *
 * @param   pVM         The cross context VM structure.
 * @param   enmClock    The queue.
 */
DECLINLINE(bool) tmR3HasExpiredTimer(PVM pVM, TMCLOCK enmClock)
{
    const uint64_t u64Expire = pVM->tm.s.aTimerQueues[enmClock].u64Expire;
    return u64Expire != INT64_MAX && u64Expire <= tmClock(pVM, enmClock);
}


/**
 * Checks for expired timers in all the queues.
 *
 * @returns true / false.
 * @param   pVM         The cross context VM structure.
 */
DECLINLINE(bool) tmR3AnyExpiredTimers(PVM pVM)
{
    /*
     * Combine the time calculation for the first two since we're not on EMT
     * TMVirtualSyncGet only permits EMT.
     */
    uint64_t u64Now = TMVirtualGetNoCheck(pVM);
    if (pVM->tm.s.aTimerQueues[TMCLOCK_VIRTUAL].u64Expire <= u64Now)
        return true;
    u64Now = pVM->tm.s.fVirtualSyncTicking
           ? u64Now - pVM->tm.s.offVirtualSync
           : pVM->tm.s.u64VirtualSync;
    if (pVM->tm.s.aTimerQueues[TMCLOCK_VIRTUAL_SYNC].u64Expire <= u64Now)
        return true;

    /*
     * The remaining timers.
     */
    if (tmR3HasExpiredTimer(pVM, TMCLOCK_REAL))
        return true;
    if (tmR3HasExpiredTimer(pVM, TMCLOCK_TSC))
        return true;
    return false;
}


/**
 * Schedule timer callback.
 *
 * @param   pTimer      Timer handle.
 * @param   pvUser      Pointer to the VM.
 * @thread  Timer thread.
 *
 * @remark  We cannot do the scheduling and queues running from a timer handler
 *          since it's not executing in EMT, and even if it was it would be async
 *          and we wouldn't know the state of the affairs.
 *          So, we'll just raise the timer FF and force any REM execution to exit.
 */
static DECLCALLBACK(void) tmR3TimerCallback(PRTTIMER pTimer, void *pvUser, uint64_t /*iTick*/)
{
    PVM     pVM      = (PVM)pvUser;
    PVMCPU  pVCpuDst = pVM->apCpusR3[pVM->tm.s.idTimerCpu];
    NOREF(pTimer);

    AssertCompile(TMCLOCK_MAX == 4);
    STAM_COUNTER_INC(&pVM->tm.s.StatTimerCallback);

#ifdef DEBUG_Sander /* very annoying, keep it private. */
    if (VMCPU_FF_IS_SET(pVCpuDst, VMCPU_FF_TIMER))
        Log(("tmR3TimerCallback: timer event still pending!!\n"));
#endif
    if (    !VMCPU_FF_IS_SET(pVCpuDst, VMCPU_FF_TIMER)
        &&  (   pVM->tm.s.aTimerQueues[TMCLOCK_VIRTUAL_SYNC].idxSchedule != UINT32_MAX /** @todo FIXME - reconsider offSchedule as a reason for running the timer queues. */
            ||  pVM->tm.s.aTimerQueues[TMCLOCK_VIRTUAL].idxSchedule != UINT32_MAX
            ||  pVM->tm.s.aTimerQueues[TMCLOCK_REAL].idxSchedule != UINT32_MAX
            ||  pVM->tm.s.aTimerQueues[TMCLOCK_TSC].idxSchedule != UINT32_MAX
            ||  tmR3AnyExpiredTimers(pVM)
            )
        && !VMCPU_FF_IS_SET(pVCpuDst, VMCPU_FF_TIMER)
        && !pVM->tm.s.fRunningQueues
       )
    {
        Log5(("TM(%u): FF: 0 -> 1\n", __LINE__));
        VMCPU_FF_SET(pVCpuDst, VMCPU_FF_TIMER);
        VMR3NotifyCpuFFU(pVCpuDst->pUVCpu, VMNOTIFYFF_FLAGS_DONE_REM | VMNOTIFYFF_FLAGS_POKE);
        STAM_COUNTER_INC(&pVM->tm.s.StatTimerCallbackSetFF);
    }
}


/**
 * Worker for tmR3TimerQueueDoOne that runs pending timers on the specified
 * non-empty timer queue.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pQueue          The queue to run.
 * @param   pTimer          The head timer.  Caller already check that this is
 *                          not NULL.
 */
static void tmR3TimerQueueRun(PVM pVM, PTMTIMERQUEUE pQueue, PTMTIMER pTimer)
{
    VM_ASSERT_EMT(pVM); /** @todo relax this */

    /*
     * Run timers.
     *
     * We check the clock once and run all timers which are ACTIVE
     * and have an expire time less or equal to the time we read.
     *
     * N.B. A generic unlink must be applied since other threads
     *      are allowed to mess with any active timer at any time.
     *
     *      However, we only allow EMT to handle EXPIRED_PENDING
     *      timers, thus enabling the timer handler function to
     *      arm the timer again.
     */
/** @todo the above 'however' is outdated.   */
    const uint64_t u64Now = tmClock(pVM, pQueue->enmClock);
    while (pTimer->u64Expire <= u64Now)
    {
        PTMTIMER const  pNext = tmTimerGetNext(pQueue, pTimer);
        PPDMCRITSECT    pCritSect = pTimer->pCritSect;
        if (pCritSect)
        {
            STAM_PROFILE_START(&pTimer->StatCritSectEnter, Locking);
            PDMCritSectEnter(pVM, pCritSect, VERR_IGNORED);
            STAM_PROFILE_STOP(&pTimer->StatCritSectEnter, Locking);
        }
        Log2(("tmR3TimerQueueRun: %p:{.enmState=%s, .enmClock=%d, .enmType=%d, u64Expire=%llx (now=%llx) .szName='%s'}\n",
              pTimer, tmTimerState(pTimer->enmState), pQueue->enmClock, pTimer->enmType, pTimer->u64Expire, u64Now, pTimer->szName));
        bool fRc;
        TM_TRY_SET_STATE(pTimer, TMTIMERSTATE_EXPIRED_GET_UNLINK, TMTIMERSTATE_ACTIVE, fRc);
        if (fRc)
        {
            Assert(pTimer->idxScheduleNext == UINT32_MAX); /* this can trigger falsely */

            /* unlink */
            const PTMTIMER pPrev = tmTimerGetPrev(pQueue, pTimer);
            if (pPrev)
                tmTimerSetNext(pQueue, pPrev, pNext);
            else
            {
                tmTimerQueueSetHead(pQueue, pQueue, pNext);
                pQueue->u64Expire = pNext ? pNext->u64Expire : INT64_MAX;
            }
            if (pNext)
                tmTimerSetPrev(pQueue, pNext, pPrev);
            pTimer->idxNext = UINT32_MAX;
            pTimer->idxPrev = UINT32_MAX;

            /* fire */
            TM_SET_STATE(pTimer, TMTIMERSTATE_EXPIRED_DELIVER);
            STAM_PROFILE_START(&pTimer->StatTimer, PrfTimer);
            switch (pTimer->enmType)
            {
                case TMTIMERTYPE_DEV:       pTimer->u.Dev.pfnTimer(pTimer->u.Dev.pDevIns, pTimer->hSelf, pTimer->pvUser); break;
                case TMTIMERTYPE_USB:       pTimer->u.Usb.pfnTimer(pTimer->u.Usb.pUsbIns, pTimer->hSelf, pTimer->pvUser); break;
                case TMTIMERTYPE_DRV:       pTimer->u.Drv.pfnTimer(pTimer->u.Drv.pDrvIns, pTimer->hSelf, pTimer->pvUser); break;
                case TMTIMERTYPE_INTERNAL:  pTimer->u.Internal.pfnTimer(pVM, pTimer->hSelf, pTimer->pvUser); break;
                default:
                    AssertMsgFailed(("Invalid timer type %d (%s)\n", pTimer->enmType, pTimer->szName));
                    break;
            }
            STAM_PROFILE_STOP(&pTimer->StatTimer, PrfTimer);

            /* change the state if it wasn't changed already in the handler. */
            TM_TRY_SET_STATE(pTimer, TMTIMERSTATE_STOPPED, TMTIMERSTATE_EXPIRED_DELIVER, fRc);
            Log2(("tmR3TimerQueueRun: new state %s\n", tmTimerState(pTimer->enmState)));
        }
        if (pCritSect)
            PDMCritSectLeave(pVM, pCritSect);

        /* Advance? */
        pTimer = pNext;
        if (!pTimer)
            break;
    } /* run loop */
}


/**
 * Service one regular timer queue.
 *
 * @param   pVM     The cross context VM structure.
 * @param   pQueue  The queue.
 */
static void tmR3TimerQueueDoOne(PVM pVM, PTMTIMERQUEUE pQueue)
{
    Assert(pQueue->enmClock != TMCLOCK_VIRTUAL_SYNC);

    /*
     * Only one thread should be "doing" the queue.
     */
    if (ASMAtomicCmpXchgBool(&pQueue->fBeingProcessed, true, false))
    {
        STAM_PROFILE_START(&pQueue->StatDo, s);
        PDMCritSectEnter(pVM, &pQueue->TimerLock, VERR_IGNORED);

        if (pQueue->idxSchedule != UINT32_MAX)
            tmTimerQueueSchedule(pVM, pQueue, pQueue);

        PTMTIMER pHead = tmTimerQueueGetHead(pQueue, pQueue);
        if (pHead)
            tmR3TimerQueueRun(pVM, pQueue, pHead);

        PDMCritSectLeave(pVM, &pQueue->TimerLock);
        STAM_PROFILE_STOP(&pQueue->StatDo, s);
        ASMAtomicWriteBool(&pQueue->fBeingProcessed, false);
    }
}


/**
 * Schedules and runs any pending times in the timer queue for the
 * synchronous virtual clock.
 *
 * This scheduling is a bit different from the other queues as it need
 * to implement the special requirements of the timer synchronous virtual
 * clock, thus this 2nd queue run function.
 *
 * @param   pVM             The cross context VM structure.
 *
 * @remarks The caller must the Virtual Sync lock.  Owning the TM lock is no
 *          longer important.
 */
static void tmR3TimerQueueRunVirtualSync(PVM pVM)
{
    PTMTIMERQUEUE const pQueue = &pVM->tm.s.aTimerQueues[TMCLOCK_VIRTUAL_SYNC];
    VM_ASSERT_EMT(pVM);
    Assert(PDMCritSectIsOwner(pVM, &pVM->tm.s.VirtualSyncLock));

    /*
     * Any timers?
     */
    PTMTIMER pNext = tmTimerQueueGetHead(pQueue, pQueue);
    if (RT_UNLIKELY(!pNext))
    {
        Assert(pVM->tm.s.fVirtualSyncTicking || !pVM->tm.s.cVirtualTicking);
        return;
    }
    STAM_COUNTER_INC(&pVM->tm.s.StatVirtualSyncRun);

    /*
     * Calculate the time frame for which we will dispatch timers.
     *
     * We use a time frame ranging from the current sync time (which is most likely the
     * same as the head timer) and some configurable period (100000ns) up towards the
     * current virtual time. This period might also need to be restricted by the catch-up
     * rate so frequent calls to this function won't accelerate the time too much, however
     * this will be implemented at a later point if necessary.
     *
     * Without this frame we would 1) having to run timers much more frequently
     * and 2) lag behind at a steady rate.
     */
    const uint64_t  u64VirtualNow  = TMVirtualGetNoCheck(pVM);
    uint64_t const  offSyncGivenUp = pVM->tm.s.offVirtualSyncGivenUp;
    uint64_t        u64Now;
    if (!pVM->tm.s.fVirtualSyncTicking)
    {
        STAM_COUNTER_INC(&pVM->tm.s.StatVirtualSyncRunStoppedAlready);
        u64Now = pVM->tm.s.u64VirtualSync;
        Assert(u64Now <= pNext->u64Expire);
    }
    else
    {
        /* Calc 'now'. */
        bool        fStopCatchup   = false;
        bool        fUpdateStuff   = false;
        uint64_t    off            = pVM->tm.s.offVirtualSync;
        if (pVM->tm.s.fVirtualSyncCatchUp)
        {
            uint64_t u64Delta = u64VirtualNow - pVM->tm.s.u64VirtualSyncCatchUpPrev;
            if (RT_LIKELY(!(u64Delta >> 32)))
            {
                uint64_t u64Sub = ASMMultU64ByU32DivByU32(u64Delta, pVM->tm.s.u32VirtualSyncCatchUpPercentage, 100);
                if (off > u64Sub + offSyncGivenUp)
                {
                    off -= u64Sub;
                    Log4(("TM: %'RU64/-%'8RU64: sub %'RU64 [tmR3TimerQueueRunVirtualSync]\n", u64VirtualNow - off, off - offSyncGivenUp, u64Sub));
                }
                else
                {
                    STAM_PROFILE_ADV_STOP(&pVM->tm.s.StatVirtualSyncCatchup, c);
                    fStopCatchup = true;
                    off = offSyncGivenUp;
                }
                fUpdateStuff = true;
            }
        }
        u64Now = u64VirtualNow - off;

        /* Adjust against last returned time. */
        uint64_t u64Last = ASMAtomicUoReadU64(&pVM->tm.s.u64VirtualSync);
        if (u64Last > u64Now)
        {
            u64Now = u64Last + 1;
            STAM_COUNTER_INC(&pVM->tm.s.StatVirtualSyncGetAdjLast);
        }

        /* Check if stopped by expired timer. */
        uint64_t const u64Expire = pNext->u64Expire;
        if (u64Now >= u64Expire)
        {
            STAM_COUNTER_INC(&pVM->tm.s.StatVirtualSyncRunStop);
            u64Now = u64Expire;
            ASMAtomicWriteU64(&pVM->tm.s.u64VirtualSync, u64Now);
            ASMAtomicWriteBool(&pVM->tm.s.fVirtualSyncTicking, false);
            Log4(("TM: %'RU64/-%'8RU64: exp tmr [tmR3TimerQueueRunVirtualSync]\n", u64Now, u64VirtualNow - u64Now - offSyncGivenUp));
        }
        else
        {
            ASMAtomicWriteU64(&pVM->tm.s.u64VirtualSync, u64Now);
            if (fUpdateStuff)
            {
                ASMAtomicWriteU64(&pVM->tm.s.offVirtualSync, off);
                ASMAtomicWriteU64(&pVM->tm.s.u64VirtualSyncCatchUpPrev, u64VirtualNow);
                ASMAtomicWriteU64(&pVM->tm.s.u64VirtualSync, u64Now);
                if (fStopCatchup)
                {
                    ASMAtomicWriteBool(&pVM->tm.s.fVirtualSyncCatchUp, false);
                    Log4(("TM: %'RU64/0: caught up [tmR3TimerQueueRunVirtualSync]\n", u64VirtualNow));
                }
            }
        }
    }

    /* calc end of frame. */
    uint64_t u64Max = u64Now + pVM->tm.s.u32VirtualSyncScheduleSlack;
    if (u64Max > u64VirtualNow - offSyncGivenUp)
        u64Max = u64VirtualNow - offSyncGivenUp;

    /* assert sanity */
    Assert(u64Now <= u64VirtualNow - offSyncGivenUp);
    Assert(u64Max <= u64VirtualNow - offSyncGivenUp);
    Assert(u64Now <= u64Max);
    Assert(offSyncGivenUp == pVM->tm.s.offVirtualSyncGivenUp);

    /*
     * Process the expired timers moving the clock along as we progress.
     */
#ifdef VBOX_STRICT
    uint64_t u64Prev = u64Now; NOREF(u64Prev);
#endif
    while (pNext && pNext->u64Expire <= u64Max)
    {
        /* Advance */
        PTMTIMER pTimer = pNext;
        pNext = tmTimerGetNext(pQueue, pTimer);

        /* Take the associated lock. */
        PPDMCRITSECT pCritSect = pTimer->pCritSect;
        if (pCritSect)
        {
            STAM_PROFILE_START(&pTimer->StatCritSectEnter, Locking);
            PDMCritSectEnter(pVM, pCritSect, VERR_IGNORED);
            STAM_PROFILE_STOP(&pTimer->StatCritSectEnter, Locking);
        }

        Log2(("tmR3TimerQueueRunVirtualSync: %p:{.enmState=%s, .enmClock=%d, .enmType=%d, u64Expire=%llx (now=%llx) .szName='%s'}\n",
              pTimer, tmTimerState(pTimer->enmState), pQueue->enmClock, pTimer->enmType, pTimer->u64Expire, u64Now, pTimer->szName));

        /* Advance the clock - don't permit timers to be out of order or armed
           in the 'past'. */
#ifdef VBOX_STRICT
        AssertMsg(pTimer->u64Expire >= u64Prev, ("%'RU64 < %'RU64 %s\n", pTimer->u64Expire, u64Prev, pTimer->szName));
        u64Prev = pTimer->u64Expire;
#endif
        ASMAtomicWriteU64(&pVM->tm.s.u64VirtualSync, pTimer->u64Expire);
        ASMAtomicWriteBool(&pVM->tm.s.fVirtualSyncTicking, false);

        /* Unlink it, change the state and do the callout. */
        tmTimerQueueUnlinkActive(pVM, pQueue, pQueue, pTimer);
        TM_SET_STATE(pTimer, TMTIMERSTATE_EXPIRED_DELIVER);
        STAM_PROFILE_START(&pTimer->StatTimer, PrfTimer);
        switch (pTimer->enmType)
        {
            case TMTIMERTYPE_DEV:       pTimer->u.Dev.pfnTimer(pTimer->u.Dev.pDevIns, pTimer->hSelf, pTimer->pvUser); break;
            case TMTIMERTYPE_USB:       pTimer->u.Usb.pfnTimer(pTimer->u.Usb.pUsbIns, pTimer->hSelf, pTimer->pvUser); break;
            case TMTIMERTYPE_DRV:       pTimer->u.Drv.pfnTimer(pTimer->u.Drv.pDrvIns, pTimer->hSelf, pTimer->pvUser); break;
            case TMTIMERTYPE_INTERNAL:  pTimer->u.Internal.pfnTimer(pVM, pTimer->hSelf, pTimer->pvUser); break;
            default:
                AssertMsgFailed(("Invalid timer type %d (%s)\n", pTimer->enmType, pTimer->szName));
                break;
        }
        STAM_PROFILE_STOP(&pTimer->StatTimer, PrfTimer);

        /* Change the state if it wasn't changed already in the handler.
           Reset the Hz hint too since this is the same as TMTimerStop. */
        bool fRc;
        TM_TRY_SET_STATE(pTimer, TMTIMERSTATE_STOPPED, TMTIMERSTATE_EXPIRED_DELIVER, fRc);
        if (fRc && pTimer->uHzHint)
        {
            if (pTimer->uHzHint >= pQueue->uMaxHzHint)
                ASMAtomicOrU64(&pVM->tm.s.HzHint.u64Combined, RT_BIT_32(TMCLOCK_VIRTUAL_SYNC) | RT_BIT_32(TMCLOCK_VIRTUAL_SYNC + 16));
            pTimer->uHzHint = 0;
        }
        Log2(("tmR3TimerQueueRunVirtualSync: new state %s\n", tmTimerState(pTimer->enmState)));

        /* Leave the associated lock. */
        if (pCritSect)
            PDMCritSectLeave(pVM, pCritSect);
    } /* run loop */


    /*
     * Restart the clock if it was stopped to serve any timers,
     * and start/adjust catch-up if necessary.
     */
    if (    !pVM->tm.s.fVirtualSyncTicking
        &&  pVM->tm.s.cVirtualTicking)
    {
        STAM_COUNTER_INC(&pVM->tm.s.StatVirtualSyncRunRestart);

        /* calc the slack we've handed out. */
        const uint64_t u64VirtualNow2 = TMVirtualGetNoCheck(pVM);
        Assert(u64VirtualNow2 >= u64VirtualNow);
        AssertMsg(pVM->tm.s.u64VirtualSync >= u64Now, ("%'RU64 < %'RU64\n", pVM->tm.s.u64VirtualSync, u64Now));
        const uint64_t offSlack = pVM->tm.s.u64VirtualSync - u64Now;
        STAM_STATS({
            if (offSlack)
            {
                PSTAMPROFILE p = &pVM->tm.s.StatVirtualSyncRunSlack;
                p->cPeriods++;
                p->cTicks += offSlack;
                if (p->cTicksMax < offSlack) p->cTicksMax = offSlack;
                if (p->cTicksMin > offSlack) p->cTicksMin = offSlack;
            }
        });

        /* Let the time run a little bit while we were busy running timers(?). */
        uint64_t u64Elapsed;
#define MAX_ELAPSED 30000U /* ns */
        if (offSlack > MAX_ELAPSED)
            u64Elapsed = 0;
        else
        {
            u64Elapsed = u64VirtualNow2 - u64VirtualNow;
            if (u64Elapsed > MAX_ELAPSED)
                u64Elapsed = MAX_ELAPSED;
            u64Elapsed = u64Elapsed > offSlack ? u64Elapsed - offSlack : 0;
        }
#undef MAX_ELAPSED

        /* Calc the current offset. */
        uint64_t offNew = u64VirtualNow2 - pVM->tm.s.u64VirtualSync - u64Elapsed;
        Assert(!(offNew & RT_BIT_64(63)));
        uint64_t offLag = offNew - pVM->tm.s.offVirtualSyncGivenUp;
        Assert(!(offLag & RT_BIT_64(63)));

        /*
         * Deal with starting, adjusting and stopping catchup.
         */
        if (pVM->tm.s.fVirtualSyncCatchUp)
        {
            if (offLag <= pVM->tm.s.u64VirtualSyncCatchUpStopThreshold)
            {
                /* stop */
                STAM_PROFILE_ADV_STOP(&pVM->tm.s.StatVirtualSyncCatchup, c);
                ASMAtomicWriteBool(&pVM->tm.s.fVirtualSyncCatchUp, false);
                Log4(("TM: %'RU64/-%'8RU64: caught up [pt]\n", u64VirtualNow2 - offNew, offLag));
            }
            else if (offLag <= pVM->tm.s.u64VirtualSyncCatchUpGiveUpThreshold)
            {
                /* adjust */
                unsigned i = 0;
                while (     i + 1 < RT_ELEMENTS(pVM->tm.s.aVirtualSyncCatchUpPeriods)
                       &&   offLag >= pVM->tm.s.aVirtualSyncCatchUpPeriods[i + 1].u64Start)
                    i++;
                if (pVM->tm.s.u32VirtualSyncCatchUpPercentage < pVM->tm.s.aVirtualSyncCatchUpPeriods[i].u32Percentage)
                {
                    STAM_COUNTER_INC(&pVM->tm.s.aStatVirtualSyncCatchupAdjust[i]);
                    ASMAtomicWriteU32(&pVM->tm.s.u32VirtualSyncCatchUpPercentage, pVM->tm.s.aVirtualSyncCatchUpPeriods[i].u32Percentage);
                    Log4(("TM: %'RU64/%'8RU64: adj %u%%\n", u64VirtualNow2 - offNew, offLag, pVM->tm.s.u32VirtualSyncCatchUpPercentage));
                }
                pVM->tm.s.u64VirtualSyncCatchUpPrev = u64VirtualNow2;
            }
            else
            {
                /* give up */
                STAM_COUNTER_INC(&pVM->tm.s.StatVirtualSyncGiveUp);
                STAM_PROFILE_ADV_STOP(&pVM->tm.s.StatVirtualSyncCatchup, c);
                ASMAtomicWriteU64((uint64_t volatile *)&pVM->tm.s.offVirtualSyncGivenUp, offNew);
                ASMAtomicWriteBool(&pVM->tm.s.fVirtualSyncCatchUp, false);
                Log4(("TM: %'RU64/%'8RU64: give up %u%%\n", u64VirtualNow2 - offNew, offLag, pVM->tm.s.u32VirtualSyncCatchUpPercentage));
                LogRel(("TM: Giving up catch-up attempt at a %'RU64 ns lag; new total: %'RU64 ns\n", offLag, offNew));
            }
        }
        else if (offLag >= pVM->tm.s.aVirtualSyncCatchUpPeriods[0].u64Start)
        {
            if (offLag <= pVM->tm.s.u64VirtualSyncCatchUpGiveUpThreshold)
            {
                /* start */
                STAM_PROFILE_ADV_START(&pVM->tm.s.StatVirtualSyncCatchup, c);
                unsigned i = 0;
                while (     i + 1 < RT_ELEMENTS(pVM->tm.s.aVirtualSyncCatchUpPeriods)
                       &&   offLag >= pVM->tm.s.aVirtualSyncCatchUpPeriods[i + 1].u64Start)
                    i++;
                STAM_COUNTER_INC(&pVM->tm.s.aStatVirtualSyncCatchupInitial[i]);
                ASMAtomicWriteU32(&pVM->tm.s.u32VirtualSyncCatchUpPercentage, pVM->tm.s.aVirtualSyncCatchUpPeriods[i].u32Percentage);
                ASMAtomicWriteBool(&pVM->tm.s.fVirtualSyncCatchUp, true);
                Log4(("TM: %'RU64/%'8RU64: catch-up %u%%\n", u64VirtualNow2 - offNew, offLag, pVM->tm.s.u32VirtualSyncCatchUpPercentage));
            }
            else
            {
                /* don't bother */
                STAM_COUNTER_INC(&pVM->tm.s.StatVirtualSyncGiveUpBeforeStarting);
                ASMAtomicWriteU64((uint64_t volatile *)&pVM->tm.s.offVirtualSyncGivenUp, offNew);
                Log4(("TM: %'RU64/%'8RU64: give up\n", u64VirtualNow2 - offNew, offLag));
                LogRel(("TM: Not bothering to attempt catching up a %'RU64 ns lag; new total: %'RU64\n", offLag, offNew));
            }
        }

        /*
         * Update the offset and restart the clock.
         */
        Assert(!(offNew & RT_BIT_64(63)));
        ASMAtomicWriteU64(&pVM->tm.s.offVirtualSync, offNew);
        ASMAtomicWriteBool(&pVM->tm.s.fVirtualSyncTicking, true);
    }
}


/**
 * Deals with stopped Virtual Sync clock.
 *
 * This is called by the forced action flag handling code in EM when it
 * encounters the VM_FF_TM_VIRTUAL_SYNC flag. It is called by all VCPUs and they
 * will block on the VirtualSyncLock until the pending timers has been executed
 * and the clock restarted.
 *
 * @param   pVM     The cross context VM structure.
 * @param   pVCpu   The cross context virtual CPU structure of the calling EMT.
 *
 * @thread  EMTs
 */
VMMR3_INT_DECL(void) TMR3VirtualSyncFF(PVM pVM, PVMCPU pVCpu)
{
    Log2(("TMR3VirtualSyncFF:\n"));

    /*
     * The EMT doing the timers is diverted to them.
     */
    if (pVCpu->idCpu == pVM->tm.s.idTimerCpu)
        TMR3TimerQueuesDo(pVM);
    /*
     * The other EMTs will block on the virtual sync lock and the first owner
     * will run the queue and thus restarting the clock.
     *
     * Note! This is very suboptimal code wrt to resuming execution when there
     *       are more than two Virtual CPUs, since they will all have to enter
     *       the critical section one by one. But it's a very simple solution
     *       which will have to do the job for now.
     */
    else
    {
/** @todo Optimize for SMP   */
        STAM_PROFILE_START(&pVM->tm.s.StatVirtualSyncFF, a);
        PDMCritSectEnter(pVM, &pVM->tm.s.VirtualSyncLock, VERR_IGNORED);
        if (pVM->tm.s.fVirtualSyncTicking)
        {
            STAM_PROFILE_STOP(&pVM->tm.s.StatVirtualSyncFF, a); /* before the unlock! */
            PDMCritSectLeave(pVM, &pVM->tm.s.VirtualSyncLock);
            Log2(("TMR3VirtualSyncFF: ticking\n"));
        }
        else
        {
            PDMCritSectLeave(pVM, &pVM->tm.s.VirtualSyncLock);

            /* try run it. */
            PDMCritSectEnter(pVM, &pVM->tm.s.aTimerQueues[TMCLOCK_VIRTUAL].TimerLock, VERR_IGNORED);
            PDMCritSectEnter(pVM, &pVM->tm.s.VirtualSyncLock, VERR_IGNORED);
            if (pVM->tm.s.fVirtualSyncTicking)
                Log2(("TMR3VirtualSyncFF: ticking (2)\n"));
            else
            {
                ASMAtomicWriteBool(&pVM->tm.s.fRunningVirtualSyncQueue, true);
                Log2(("TMR3VirtualSyncFF: running queue\n"));

                Assert(pVM->tm.s.aTimerQueues[TMCLOCK_VIRTUAL_SYNC].idxSchedule == UINT32_MAX);
                tmR3TimerQueueRunVirtualSync(pVM);
                if (pVM->tm.s.fVirtualSyncTicking) /** @todo move into tmR3TimerQueueRunVirtualSync - FIXME */
                    VM_FF_CLEAR(pVM, VM_FF_TM_VIRTUAL_SYNC);

                ASMAtomicWriteBool(&pVM->tm.s.fRunningVirtualSyncQueue, false);
            }
            PDMCritSectLeave(pVM, &pVM->tm.s.VirtualSyncLock);
            STAM_PROFILE_STOP(&pVM->tm.s.StatVirtualSyncFF, a); /* before the unlock! */
            PDMCritSectLeave(pVM, &pVM->tm.s.aTimerQueues[TMCLOCK_VIRTUAL].TimerLock);
        }
    }
}


/**
 * Service the special virtual sync timer queue.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpuDst    The destination VCpu.
 */
static void tmR3TimerQueueDoVirtualSync(PVM pVM, PVMCPU pVCpuDst)
{
    PTMTIMERQUEUE pQueue = &pVM->tm.s.aTimerQueues[TMCLOCK_VIRTUAL_SYNC];
    if (ASMAtomicCmpXchgBool(&pQueue->fBeingProcessed, true, false))
    {
        STAM_PROFILE_START(&pQueue->StatDo, s1);
        PDMCritSectEnter(pVM, &pQueue->TimerLock, VERR_IGNORED);
        PDMCritSectEnter(pVM, &pVM->tm.s.VirtualSyncLock, VERR_IGNORED);
        ASMAtomicWriteBool(&pVM->tm.s.fRunningVirtualSyncQueue, true);
        VMCPU_FF_CLEAR(pVCpuDst, VMCPU_FF_TIMER);   /* Clear the FF once we started working for real. */

        Assert(pQueue->idxSchedule == UINT32_MAX);
        tmR3TimerQueueRunVirtualSync(pVM);
        if (pVM->tm.s.fVirtualSyncTicking) /** @todo move into tmR3TimerQueueRunVirtualSync - FIXME */
            VM_FF_CLEAR(pVM, VM_FF_TM_VIRTUAL_SYNC);

        ASMAtomicWriteBool(&pVM->tm.s.fRunningVirtualSyncQueue, false);
        PDMCritSectLeave(pVM, &pVM->tm.s.VirtualSyncLock);
        PDMCritSectLeave(pVM, &pQueue->TimerLock);
        STAM_PROFILE_STOP(&pQueue->StatDo, s1);
        ASMAtomicWriteBool(&pQueue->fBeingProcessed, false);
    }
}


/**
 * Schedules and runs any pending timers.
 *
 * This is normally called from a forced action handler in EMT.
 *
 * @param   pVM     The cross context VM structure.
 *
 * @thread  EMT (actually EMT0, but we fend off the others)
 */
VMMR3DECL(void) TMR3TimerQueuesDo(PVM pVM)
{
    /*
     * Only the dedicated timer EMT should do stuff here.
     * (fRunningQueues is only used as an indicator.)
     */
    Assert(pVM->tm.s.idTimerCpu < pVM->cCpus);
    PVMCPU pVCpuDst = pVM->apCpusR3[pVM->tm.s.idTimerCpu];
    if (VMMGetCpu(pVM) != pVCpuDst)
    {
        Assert(pVM->cCpus > 1);
        return;
    }
    STAM_PROFILE_START(&pVM->tm.s.StatDoQueues, a);
    Log2(("TMR3TimerQueuesDo:\n"));
    Assert(!pVM->tm.s.fRunningQueues);
    ASMAtomicWriteBool(&pVM->tm.s.fRunningQueues, true);

    /*
     * Process the queues.
     */
    AssertCompile(TMCLOCK_MAX == 4);

    /*
     * TMCLOCK_VIRTUAL_SYNC (see also TMR3VirtualSyncFF)
     */
    tmR3TimerQueueDoVirtualSync(pVM, pVCpuDst);

    /*
     * TMCLOCK_VIRTUAL
     */
    tmR3TimerQueueDoOne(pVM, &pVM->tm.s.aTimerQueues[TMCLOCK_VIRTUAL]);

    /*
     * TMCLOCK_TSC
     */
    Assert(pVM->tm.s.aTimerQueues[TMCLOCK_TSC].idxActive == UINT32_MAX); /* not used */

    /*
     * TMCLOCK_REAL
     */
    tmR3TimerQueueDoOne(pVM, &pVM->tm.s.aTimerQueues[TMCLOCK_REAL]);

#ifdef VBOX_STRICT
    /* check that we didn't screw up. */
    tmTimerQueuesSanityChecks(pVM, "TMR3TimerQueuesDo");
#endif

    /* done */
    Log2(("TMR3TimerQueuesDo: returns void\n"));
    ASMAtomicWriteBool(&pVM->tm.s.fRunningQueues, false);
    STAM_PROFILE_STOP(&pVM->tm.s.StatDoQueues, a);
}



/** @name Saved state values
 * @{ */
#define TMTIMERSTATE_SAVED_PENDING_STOP         4
#define TMTIMERSTATE_SAVED_PENDING_SCHEDULE     7
/** @}  */


/**
 * Saves the state of a timer to a saved state.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   hTimer          Timer to save.
 * @param   pSSM            Save State Manager handle.
 */
VMMR3DECL(int) TMR3TimerSave(PVM pVM, TMTIMERHANDLE hTimer, PSSMHANDLE pSSM)
{
    VM_ASSERT_EMT(pVM);
    TMTIMER_HANDLE_TO_VARS_RETURN(pVM, hTimer); /* => pTimer, pQueueCC, pQueue, idxTimer, idxQueue */
    LogFlow(("TMR3TimerSave: %p:{enmState=%s, .szName='%s'} pSSM=%p\n", pTimer, tmTimerState(pTimer->enmState), pTimer->szName, pSSM));

    switch (pTimer->enmState)
    {
        case TMTIMERSTATE_STOPPED:
        case TMTIMERSTATE_PENDING_STOP:
        case TMTIMERSTATE_PENDING_STOP_SCHEDULE:
            return SSMR3PutU8(pSSM, TMTIMERSTATE_SAVED_PENDING_STOP);

        case TMTIMERSTATE_PENDING_SCHEDULE_SET_EXPIRE:
        case TMTIMERSTATE_PENDING_RESCHEDULE_SET_EXPIRE:
            AssertMsgFailed(("u64Expire is being updated! (%s)\n", pTimer->szName));
            if (!RTThreadYield())
                RTThreadSleep(1);
            RT_FALL_THRU();
        case TMTIMERSTATE_ACTIVE:
        case TMTIMERSTATE_PENDING_SCHEDULE:
        case TMTIMERSTATE_PENDING_RESCHEDULE:
            SSMR3PutU8(pSSM, TMTIMERSTATE_SAVED_PENDING_SCHEDULE);
            return SSMR3PutU64(pSSM, pTimer->u64Expire);

        case TMTIMERSTATE_EXPIRED_GET_UNLINK:
        case TMTIMERSTATE_EXPIRED_DELIVER:
        case TMTIMERSTATE_DESTROY:
        case TMTIMERSTATE_FREE:
        case TMTIMERSTATE_INVALID:
            AssertMsgFailed(("Invalid timer state %d %s (%s)\n", pTimer->enmState, tmTimerState(pTimer->enmState), pTimer->szName));
            return SSMR3HandleSetStatus(pSSM, VERR_TM_INVALID_STATE);
    }

    AssertMsgFailed(("Unknown timer state %d (%s)\n", pTimer->enmState, pTimer->szName));
    return SSMR3HandleSetStatus(pSSM, VERR_TM_UNKNOWN_STATE);
}


/**
 * Loads the state of a timer from a saved state.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   hTimer          Handle of Timer to restore.
 * @param   pSSM            Save State Manager handle.
 */
VMMR3DECL(int) TMR3TimerLoad(PVM pVM, TMTIMERHANDLE hTimer, PSSMHANDLE pSSM)
{
    VM_ASSERT_EMT(pVM);
    TMTIMER_HANDLE_TO_VARS_RETURN(pVM, hTimer); /* => pTimer, pQueueCC, pQueue, idxTimer, idxQueue */
    Assert(pSSM);
    LogFlow(("TMR3TimerLoad: %p:{enmState=%s, .szName='%s'} pSSM=%p\n", pTimer, tmTimerState(pTimer->enmState), pTimer->szName, pSSM));

    /*
     * Load the state and validate it.
     */
    uint8_t u8State;
    int rc = SSMR3GetU8(pSSM, &u8State);
    if (RT_FAILURE(rc))
        return rc;

    /* TMTIMERSTATE_SAVED_XXX: Workaround for accidental state shift in r47786 (2009-05-26 19:12:12). */
    if (    u8State == TMTIMERSTATE_SAVED_PENDING_STOP + 1
        ||  u8State == TMTIMERSTATE_SAVED_PENDING_SCHEDULE + 1)
        u8State--;

    if (    u8State != TMTIMERSTATE_SAVED_PENDING_STOP
        &&  u8State != TMTIMERSTATE_SAVED_PENDING_SCHEDULE)
    {
        AssertLogRelMsgFailed(("u8State=%d\n", u8State));
        return SSMR3HandleSetStatus(pSSM, VERR_TM_LOAD_STATE);
    }

    /* Enter the critical sections to make TMTimerSet/Stop happy. */
    if (pQueue->enmClock == TMCLOCK_VIRTUAL_SYNC)
        PDMCritSectEnter(pVM, &pVM->tm.s.VirtualSyncLock, VERR_IGNORED);
    PPDMCRITSECT pCritSect = pTimer->pCritSect;
    if (pCritSect)
        PDMCritSectEnter(pVM, pCritSect, VERR_IGNORED);

    if (u8State == TMTIMERSTATE_SAVED_PENDING_SCHEDULE)
    {
        /*
         * Load the expire time.
         */
        uint64_t u64Expire;
        rc = SSMR3GetU64(pSSM, &u64Expire);
        if (RT_FAILURE(rc))
            return rc;

        /*
         * Set it.
         */
        Log(("u8State=%d u64Expire=%llu\n", u8State, u64Expire));
        rc = TMTimerSet(pVM, hTimer, u64Expire);
    }
    else
    {
        /*
         * Stop it.
         */
        Log(("u8State=%d\n", u8State));
        rc = TMTimerStop(pVM, hTimer);
    }

    if (pCritSect)
        PDMCritSectLeave(pVM, pCritSect);
    if (pQueue->enmClock == TMCLOCK_VIRTUAL_SYNC)
        PDMCritSectLeave(pVM, &pVM->tm.s.VirtualSyncLock);

    /*
     * On failure set SSM status.
     */
    if (RT_FAILURE(rc))
        rc = SSMR3HandleSetStatus(pSSM, rc);
    return rc;
}


/**
 * Skips the state of a timer in a given saved state.
 *
 * @returns VBox status.
 * @param   pSSM            Save State Manager handle.
 * @param   pfActive        Where to store whether the timer was active
 *                          when the state was saved.
 */
VMMR3DECL(int) TMR3TimerSkip(PSSMHANDLE pSSM, bool *pfActive)
{
    Assert(pSSM); AssertPtr(pfActive);
    LogFlow(("TMR3TimerSkip: pSSM=%p pfActive=%p\n", pSSM, pfActive));

    /*
     * Load the state and validate it.
     */
    uint8_t u8State;
    int rc = SSMR3GetU8(pSSM, &u8State);
    if (RT_FAILURE(rc))
        return rc;

    /* TMTIMERSTATE_SAVED_XXX: Workaround for accidental state shift in r47786 (2009-05-26 19:12:12). */
    if (    u8State == TMTIMERSTATE_SAVED_PENDING_STOP + 1
        ||  u8State == TMTIMERSTATE_SAVED_PENDING_SCHEDULE + 1)
        u8State--;

    if (    u8State != TMTIMERSTATE_SAVED_PENDING_STOP
        &&  u8State != TMTIMERSTATE_SAVED_PENDING_SCHEDULE)
    {
        AssertLogRelMsgFailed(("u8State=%d\n", u8State));
        return SSMR3HandleSetStatus(pSSM, VERR_TM_LOAD_STATE);
    }

    *pfActive = (u8State == TMTIMERSTATE_SAVED_PENDING_SCHEDULE);
    if (*pfActive)
    {
        /*
         * Load the expire time.
         */
        uint64_t u64Expire;
        rc = SSMR3GetU64(pSSM, &u64Expire);
    }

    return rc;
}


/**
 * Associates a critical section with a timer.
 *
 * The critical section will be entered prior to doing the timer call back, thus
 * avoiding potential races between the timer thread and other threads trying to
 * stop or adjust the timer expiration while it's being delivered. The timer
 * thread will leave the critical section when the timer callback returns.
 *
 * In strict builds, ownership of the critical section will be asserted by
 * TMTimerSet, TMTimerStop, TMTimerGetExpire and TMTimerDestroy (when called at
 * runtime).
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_INVALID_HANDLE if the timer handle is NULL or invalid
 *          (asserted).
 * @retval  VERR_INVALID_PARAMETER if pCritSect is NULL or has an invalid magic
 *          (asserted).
 * @retval  VERR_ALREADY_EXISTS if a critical section was already associated
 *          with the timer (asserted).
 * @retval  VERR_INVALID_STATE if the timer isn't stopped.
 *
 * @param   pVM             The cross context VM structure.
 * @param   hTimer          The timer handle.
 * @param   pCritSect       The critical section. The caller must make sure this
 *                          is around for the life time of the timer.
 *
 * @thread  Any, but the caller is responsible for making sure the timer is not
 *          active.
 */
VMMR3DECL(int) TMR3TimerSetCritSect(PVM pVM, TMTIMERHANDLE hTimer, PPDMCRITSECT pCritSect)
{
    TMTIMER_HANDLE_TO_VARS_RETURN(pVM, hTimer); /* => pTimer, pQueueCC, pQueue, idxTimer, idxQueue */
    AssertPtrReturn(pCritSect, VERR_INVALID_PARAMETER);
    const char *pszName = PDMR3CritSectName(pCritSect); /* exploited for validation */
    AssertReturn(pszName, VERR_INVALID_PARAMETER);
    AssertReturn(!pTimer->pCritSect, VERR_ALREADY_EXISTS);
    AssertReturn(pTimer->enmState == TMTIMERSTATE_STOPPED, VERR_INVALID_STATE);
    AssertReturn(   pTimer->enmType == TMTIMERTYPE_DEV
                 || pTimer->enmType == TMTIMERTYPE_DRV
                 || pTimer->enmType == TMTIMERTYPE_USB,
                 VERR_NOT_SUPPORTED); /* Not supported on internal timers, see tmRZTimerGetCritSect. */
    LogFlow(("pTimer=%p (%s) pCritSect=%p (%s)\n", pTimer, pTimer->szName, pCritSect, pszName));

    pTimer->pCritSect = pCritSect;
    return VINF_SUCCESS;
}


/**
 * Get the real world UTC time adjusted for VM lag.
 *
 * @returns pTime.
 * @param   pVM             The cross context VM structure.
 * @param   pTime           Where to store the time.
 */
VMMR3_INT_DECL(PRTTIMESPEC) TMR3UtcNow(PVM pVM, PRTTIMESPEC pTime)
{
    /*
     * Get a stable set of VirtualSync parameters and calc the lag.
     */
    uint64_t offVirtualSync;
    uint64_t offVirtualSyncGivenUp;
    do
    {
        offVirtualSync        = ASMAtomicReadU64(&pVM->tm.s.offVirtualSync);
        offVirtualSyncGivenUp = ASMAtomicReadU64((uint64_t volatile *)&pVM->tm.s.offVirtualSyncGivenUp);
    } while (ASMAtomicReadU64(&pVM->tm.s.offVirtualSync) != offVirtualSync);

    Assert(offVirtualSync >= offVirtualSyncGivenUp);
    uint64_t const offLag = offVirtualSync - offVirtualSyncGivenUp;

    /*
     * Get current time and adjust for virtual sync lag and do time displacement.
     */
    RTTimeNow(pTime);
    RTTimeSpecSubNano(pTime, offLag);
    RTTimeSpecAddNano(pTime, pVM->tm.s.offUTC);

    /*
     * Log details if the time changed radically (also triggers on first call).
     */
    int64_t nsPrev = ASMAtomicXchgS64(&pVM->tm.s.nsLastUtcNow, RTTimeSpecGetNano(pTime));
    int64_t cNsDelta = RTTimeSpecGetNano(pTime) - nsPrev;
    if ((uint64_t)RT_ABS(cNsDelta) > RT_NS_1HOUR / 2)
    {
        RTTIMESPEC NowAgain;
        RTTimeNow(&NowAgain);
        LogRel(("TMR3UtcNow: nsNow=%'RI64 nsPrev=%'RI64 -> cNsDelta=%'RI64 (offLag=%'RI64 offVirtualSync=%'RU64 offVirtualSyncGivenUp=%'RU64, NowAgain=%'RI64)\n",
                RTTimeSpecGetNano(pTime), nsPrev, cNsDelta, offLag, offVirtualSync, offVirtualSyncGivenUp, RTTimeSpecGetNano(&NowAgain)));
        if (pVM->tm.s.pszUtcTouchFileOnJump && nsPrev != 0)
        {
            RTFILE hFile;
            int rc = RTFileOpen(&hFile, pVM->tm.s.pszUtcTouchFileOnJump,
                                RTFILE_O_WRITE | RTFILE_O_APPEND | RTFILE_O_OPEN_CREATE | RTFILE_O_DENY_NONE);
            if (RT_SUCCESS(rc))
            {
                char   szMsg[256];
                size_t cch;
                cch = RTStrPrintf(szMsg, sizeof(szMsg),
                                  "TMR3UtcNow: nsNow=%'RI64 nsPrev=%'RI64 -> cNsDelta=%'RI64 (offLag=%'RI64 offVirtualSync=%'RU64 offVirtualSyncGivenUp=%'RU64, NowAgain=%'RI64)\n",
                                  RTTimeSpecGetNano(pTime), nsPrev, cNsDelta, offLag, offVirtualSync, offVirtualSyncGivenUp, RTTimeSpecGetNano(&NowAgain));
                RTFileWrite(hFile, szMsg, cch, NULL);
                RTFileClose(hFile);
            }
        }
    }

    return pTime;
}


/**
 * Pauses all clocks except TMCLOCK_REAL.
 *
 * @returns VBox status code, all errors are asserted.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @thread  EMT corresponding to Pointer to the VMCPU.
 */
VMMR3DECL(int) TMR3NotifySuspend(PVM pVM, PVMCPU pVCpu)
{
    VMCPU_ASSERT_EMT(pVCpu);
    PDMCritSectEnter(pVM, &pVM->tm.s.VirtualSyncLock, VERR_IGNORED); /* Paranoia: Exploiting the virtual sync lock here. */

    /*
     * The shared virtual clock (includes virtual sync which is tied to it).
     */
    int rc = tmVirtualPauseLocked(pVM);
    AssertRCReturnStmt(rc, PDMCritSectLeave(pVM, &pVM->tm.s.VirtualSyncLock), rc);

    /*
     * Pause the TSC last since it is normally linked to the virtual
     * sync clock, so the above code may actually stop both clocks.
     */
    if (!pVM->tm.s.fTSCTiedToExecution)
    {
        rc = tmCpuTickPauseLocked(pVM, pVCpu);
        AssertRCReturnStmt(rc, PDMCritSectLeave(pVM, &pVM->tm.s.VirtualSyncLock), rc);
    }

#ifndef VBOX_WITHOUT_NS_ACCOUNTING
    /*
     * Update cNsTotal and stats.
     */
    Assert(!pVCpu->tm.s.fSuspended);
    uint64_t const cNsTotalNew = RTTimeNanoTS() - pVCpu->tm.s.nsStartTotal;
    uint64_t const cNsOtherNew = cNsTotalNew - pVCpu->tm.s.cNsExecuting - pVCpu->tm.s.cNsHalted;

# if defined(VBOX_WITH_STATISTICS) || defined(VBOX_WITH_NS_ACCOUNTING_STATS)
    STAM_REL_COUNTER_ADD(&pVCpu->tm.s.StatNsTotal, cNsTotalNew - pVCpu->tm.s.cNsTotalStat);
    int64_t const cNsOtherNewDelta = cNsOtherNew - pVCpu->tm.s.cNsOtherStat;
    if (cNsOtherNewDelta > 0)
        STAM_REL_COUNTER_ADD(&pVCpu->tm.s.StatNsOther, (uint64_t)cNsOtherNewDelta);
# endif

    uint32_t uGen = ASMAtomicIncU32(&pVCpu->tm.s.uTimesGen); Assert(uGen & 1);
    pVCpu->tm.s.nsStartTotal = cNsTotalNew;
    pVCpu->tm.s.fSuspended   = true;
    pVCpu->tm.s.cNsTotalStat = cNsTotalNew;
    pVCpu->tm.s.cNsOtherStat = cNsOtherNew;
    ASMAtomicWriteU32(&pVCpu->tm.s.uTimesGen, (uGen | 1) + 1);
#endif

    PDMCritSectLeave(pVM, &pVM->tm.s.VirtualSyncLock);
    return VINF_SUCCESS;
}


/**
 * Resumes all clocks except TMCLOCK_REAL.
 *
 * @returns VBox status code, all errors are asserted.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @thread  EMT corresponding to Pointer to the VMCPU.
 */
VMMR3DECL(int) TMR3NotifyResume(PVM pVM, PVMCPU pVCpu)
{
    VMCPU_ASSERT_EMT(pVCpu);
    PDMCritSectEnter(pVM, &pVM->tm.s.VirtualSyncLock, VERR_IGNORED); /* Paranoia: Exploiting the virtual sync lock here. */

#ifndef VBOX_WITHOUT_NS_ACCOUNTING
    /*
     * Set u64NsTsStartTotal.  There is no need to back this out if either of
     * the two calls below fail.
     */
    uint32_t uGen = ASMAtomicIncU32(&pVCpu->tm.s.uTimesGen); Assert(uGen & 1);
    pVCpu->tm.s.nsStartTotal = RTTimeNanoTS() - pVCpu->tm.s.nsStartTotal;
    pVCpu->tm.s.fSuspended   = false;
    ASMAtomicWriteU32(&pVCpu->tm.s.uTimesGen, (uGen | 1) + 1);
#endif

    /*
     * Resume the TSC first since it is normally linked to the virtual sync
     * clock, so it may actually not be resumed until we've executed the code
     * below.
     */
    if (!pVM->tm.s.fTSCTiedToExecution)
    {
        int rc = tmCpuTickResumeLocked(pVM, pVCpu);
        AssertRCReturnStmt(rc, PDMCritSectLeave(pVM, &pVM->tm.s.VirtualSyncLock), rc);
    }

    /*
     * The shared virtual clock (includes virtual sync which is tied to it).
     */
    int rc = tmVirtualResumeLocked(pVM);

    PDMCritSectLeave(pVM, &pVM->tm.s.VirtualSyncLock);
    return rc;
}


/**
 * Sets the warp drive percent of the virtual time.
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM structure.
 * @param   u32Percent  The new percentage. 100 means normal operation.
 */
VMMDECL(int) TMR3SetWarpDrive(PUVM pUVM, uint32_t u32Percent)
{
    return VMR3ReqPriorityCallWaitU(pUVM, VMCPUID_ANY, (PFNRT)tmR3SetWarpDrive, 2, pUVM, u32Percent);
}


/**
 * EMT worker for TMR3SetWarpDrive.
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM handle.
 * @param   u32Percent  See TMR3SetWarpDrive().
 * @internal
 */
static DECLCALLBACK(int) tmR3SetWarpDrive(PUVM pUVM, uint32_t u32Percent)
{
    PVM    pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    PVMCPU pVCpu = VMMGetCpu(pVM);

    /*
     * Validate it.
     */
    AssertMsgReturn(u32Percent >= 2 && u32Percent <= 20000,
                    ("%RX32 is not between 2 and 20000 (inclusive).\n", u32Percent),
                    VERR_INVALID_PARAMETER);

/** @todo This isn't a feature specific to virtual time, move the variables to
 * TM level and make it affect TMR3UTCNow as well! */

    PDMCritSectEnter(pVM, &pVM->tm.s.VirtualSyncLock, VERR_IGNORED); /* Paranoia: Exploiting the virtual sync lock here. */

    /*
     * If the time is running we'll have to pause it before we can change
     * the warp drive settings.
     */
    bool fPaused = !!pVM->tm.s.cVirtualTicking;
    if (fPaused) /** @todo this isn't really working, but wtf. */
        TMR3NotifySuspend(pVM, pVCpu);

    /** @todo Should switch TM mode to virt-tsc-emulated if it isn't already! */
    pVM->tm.s.u32VirtualWarpDrivePercentage = u32Percent;
    pVM->tm.s.fVirtualWarpDrive = u32Percent != 100;
    LogRel(("TM: u32VirtualWarpDrivePercentage=%RI32 fVirtualWarpDrive=%RTbool\n",
            pVM->tm.s.u32VirtualWarpDrivePercentage, pVM->tm.s.fVirtualWarpDrive));

    if (fPaused)
        TMR3NotifyResume(pVM, pVCpu);

    PDMCritSectLeave(pVM, &pVM->tm.s.VirtualSyncLock);
    return VINF_SUCCESS;
}


/**
 * Gets the current TMCLOCK_VIRTUAL time without checking
 * timers or anything.
 *
 * @returns The timestamp.
 * @param   pUVM        The user mode VM structure.
 *
 * @remarks See TMVirtualGetNoCheck.
 */
VMMR3DECL(uint64_t) TMR3TimeVirtGet(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, UINT64_MAX);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, UINT64_MAX);
    return TMVirtualGetNoCheck(pVM);
}


/**
 * Gets the current TMCLOCK_VIRTUAL time in milliseconds without checking
 * timers or anything.
 *
 * @returns The timestamp in milliseconds.
 * @param   pUVM        The user mode VM structure.
 *
 * @remarks See TMVirtualGetNoCheck.
 */
VMMR3DECL(uint64_t) TMR3TimeVirtGetMilli(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, UINT64_MAX);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, UINT64_MAX);
    return TMVirtualToMilli(pVM, TMVirtualGetNoCheck(pVM));
}


/**
 * Gets the current TMCLOCK_VIRTUAL time in microseconds without checking
 * timers or anything.
 *
 * @returns The timestamp in microseconds.
 * @param   pUVM        The user mode VM structure.
 *
 * @remarks See TMVirtualGetNoCheck.
 */
VMMR3DECL(uint64_t) TMR3TimeVirtGetMicro(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, UINT64_MAX);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, UINT64_MAX);
    return TMVirtualToMicro(pVM, TMVirtualGetNoCheck(pVM));
}


/**
 * Gets the current TMCLOCK_VIRTUAL time in nanoseconds without checking
 * timers or anything.
 *
 * @returns The timestamp in nanoseconds.
 * @param   pUVM        The user mode VM structure.
 *
 * @remarks See TMVirtualGetNoCheck.
 */
VMMR3DECL(uint64_t) TMR3TimeVirtGetNano(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, UINT64_MAX);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, UINT64_MAX);
    return TMVirtualToNano(pVM, TMVirtualGetNoCheck(pVM));
}


/**
 * Gets the current warp drive percent.
 *
 * @returns The warp drive percent.
 * @param   pUVM        The user mode VM structure.
 */
VMMR3DECL(uint32_t) TMR3GetWarpDrive(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, UINT32_MAX);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, UINT32_MAX);
    return pVM->tm.s.u32VirtualWarpDrivePercentage;
}


#if 0 /* unused - needs a little updating after @bugref{9941}*/
/**
 * Gets the performance information for one virtual CPU as seen by the VMM.
 *
 * The returned times covers the period where the VM is running and will be
 * reset when restoring a previous VM state (at least for the time being).
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_NOT_IMPLEMENTED if not compiled in.
 * @retval  VERR_INVALID_STATE if the VM handle is bad.
 * @retval  VERR_INVALID_CPU_ID if idCpu is out of range.
 *
 * @param   pVM             The cross context VM structure.
 * @param   idCpu           The ID of the virtual CPU which times to get.
 * @param   pcNsTotal       Where to store the total run time (nano seconds) of
 *                          the CPU, i.e. the sum of the three other returns.
 *                          Optional.
 * @param   pcNsExecuting   Where to store the time (nano seconds) spent
 *                          executing guest code.  Optional.
 * @param   pcNsHalted      Where to store the time (nano seconds) spent
 *                          halted.  Optional
 * @param   pcNsOther       Where to store the time (nano seconds) spent
 *                          preempted by the host scheduler, on virtualization
 *                          overhead and on other tasks.
 */
VMMR3DECL(int) TMR3GetCpuLoadTimes(PVM pVM, VMCPUID idCpu, uint64_t *pcNsTotal, uint64_t *pcNsExecuting,
                                   uint64_t *pcNsHalted, uint64_t *pcNsOther)
{
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_STATE);
    AssertReturn(idCpu < pVM->cCpus, VERR_INVALID_CPU_ID);

#ifndef VBOX_WITHOUT_NS_ACCOUNTING
    /*
     * Get a stable result set.
     * This should be way quicker than an EMT request.
     */
    PVMCPU      pVCpu        = pVM->apCpusR3[idCpu];
    uint32_t    uTimesGen    = ASMAtomicReadU32(&pVCpu->tm.s.uTimesGen);
    uint64_t    cNsTotal     = pVCpu->tm.s.cNsTotal;
    uint64_t    cNsExecuting = pVCpu->tm.s.cNsExecuting;
    uint64_t    cNsHalted    = pVCpu->tm.s.cNsHalted;
    uint64_t    cNsOther     = pVCpu->tm.s.cNsOther;
    while (   (uTimesGen & 1) /* update in progress */
           || uTimesGen != ASMAtomicReadU32(&pVCpu->tm.s.uTimesGen))
    {
        RTThreadYield();
        uTimesGen    = ASMAtomicReadU32(&pVCpu->tm.s.uTimesGen);
        cNsTotal     = pVCpu->tm.s.cNsTotal;
        cNsExecuting = pVCpu->tm.s.cNsExecuting;
        cNsHalted    = pVCpu->tm.s.cNsHalted;
        cNsOther     = pVCpu->tm.s.cNsOther;
    }

    /*
     * Fill in the return values.
     */
    if (pcNsTotal)
        *pcNsTotal = cNsTotal;
    if (pcNsExecuting)
        *pcNsExecuting = cNsExecuting;
    if (pcNsHalted)
        *pcNsHalted = cNsHalted;
    if (pcNsOther)
        *pcNsOther = cNsOther;

    return VINF_SUCCESS;

#else
    return VERR_NOT_IMPLEMENTED;
#endif
}
#endif /* unused */


/**
 * Gets the performance information for one virtual CPU as seen by the VMM in
 * percents.
 *
 * The returned times covers the period where the VM is running and will be
 * reset when restoring a previous VM state (at least for the time being).
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_NOT_IMPLEMENTED if not compiled in.
 * @retval  VERR_INVALID_VM_HANDLE if the VM handle is bad.
 * @retval  VERR_INVALID_CPU_ID if idCpu is out of range.
 *
 * @param   pUVM            The usermode VM structure.
 * @param   idCpu           The ID of the virtual CPU which times to get.
 * @param   pcMsInterval    Where to store the interval of the percentages in
 *                          milliseconds. Optional.
 * @param   pcPctExecuting  Where to return the percentage of time spent
 *                          executing guest code.  Optional.
 * @param   pcPctHalted     Where to return the percentage of time spent halted.
 *                          Optional
 * @param   pcPctOther      Where to return the percentage of time spent
 *                          preempted by the host scheduler, on virtualization
 *                          overhead and on other tasks.
 */
VMMR3DECL(int) TMR3GetCpuLoadPercents(PUVM pUVM, VMCPUID idCpu, uint64_t *pcMsInterval, uint8_t *pcPctExecuting,
                                      uint8_t *pcPctHalted, uint8_t *pcPctOther)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(idCpu == VMCPUID_ALL || idCpu < pVM->cCpus, VERR_INVALID_CPU_ID);

#ifndef VBOX_WITHOUT_NS_ACCOUNTING
    TMCPULOADSTATE volatile *pState;
    if (idCpu == VMCPUID_ALL)
        pState = &pVM->tm.s.CpuLoad;
    else
        pState = &pVM->apCpusR3[idCpu]->tm.s.CpuLoad;

    if (pcMsInterval)
        *pcMsInterval   = RT_MS_1SEC;
    if (pcPctExecuting)
        *pcPctExecuting = pState->cPctExecuting;
    if (pcPctHalted)
        *pcPctHalted    = pState->cPctHalted;
    if (pcPctOther)
        *pcPctOther     = pState->cPctOther;

    return VINF_SUCCESS;

#else
    RT_NOREF(pcMsInterval, pcPctExecuting, pcPctHalted, pcPctOther);
    return VERR_NOT_IMPLEMENTED;
#endif
}

#ifndef VBOX_WITHOUT_NS_ACCOUNTING

/**
 * Helper for tmR3CpuLoadTimer.
 *
 * @param   pState          The state to update.
 * @param   cNsTotal        Total time.
 * @param   cNsExecuting    Time executing.
 * @param   cNsHalted       Time halted.
 */
DECLINLINE(void) tmR3CpuLoadTimerMakeUpdate(PTMCPULOADSTATE pState, uint64_t cNsTotal, uint64_t cNsExecuting, uint64_t cNsHalted)
{
    /* Calc & update deltas */
    uint64_t cNsTotalDelta      = cNsTotal     - pState->cNsPrevTotal;
    uint64_t cNsExecutingDelta  = cNsExecuting - pState->cNsPrevExecuting;
    uint64_t cNsHaltedDelta     = cNsHalted    - pState->cNsPrevHalted;

    if (cNsExecutingDelta + cNsHaltedDelta <= cNsTotalDelta)
    { /* likely */ }
    else
    {
        /* Just adjust the executing and halted values down to match the total delta. */
        uint64_t const cNsExecAndHalted = cNsExecutingDelta + cNsHaltedDelta;
        uint64_t const cNsAdjust        = cNsExecAndHalted - cNsTotalDelta + cNsTotalDelta / 64;
        cNsExecutingDelta -= (cNsAdjust * cNsExecutingDelta + cNsExecAndHalted - 1) / cNsExecAndHalted;
        cNsHaltedDelta    -= (cNsAdjust * cNsHaltedDelta    + cNsExecAndHalted - 1) / cNsExecAndHalted;
        /*Assert(cNsExecutingDelta + cNsHaltedDelta <= cNsTotalDelta); - annoying when debugging */
    }

    pState->cNsPrevExecuting    = cNsExecuting;
    pState->cNsPrevHalted       = cNsHalted;
    pState->cNsPrevTotal        = cNsTotal;

    /* Calc pcts. */
    uint8_t cPctExecuting, cPctHalted, cPctOther;
    if (!cNsTotalDelta)
    {
        cPctExecuting = 0;
        cPctHalted    = 100;
        cPctOther     = 0;
    }
    else if (cNsTotalDelta < UINT64_MAX / 4)
    {
        cPctExecuting = (uint8_t)(cNsExecutingDelta * 100 / cNsTotalDelta);
        cPctHalted    = (uint8_t)(cNsHaltedDelta    * 100 / cNsTotalDelta);
        cPctOther     = (uint8_t)((cNsTotalDelta - cNsExecutingDelta - cNsHaltedDelta) * 100 / cNsTotalDelta);
    }
    else
    {
        cPctExecuting = 0;
        cPctHalted    = 100;
        cPctOther     = 0;
    }

    /* Update percentages: */
    size_t idxHistory = pState->idxHistory + 1;
    if (idxHistory >= RT_ELEMENTS(pState->aHistory))
        idxHistory = 0;

    pState->cPctExecuting                      = cPctExecuting;
    pState->cPctHalted                         = cPctHalted;
    pState->cPctOther                          = cPctOther;

    pState->aHistory[idxHistory].cPctExecuting = cPctExecuting;
    pState->aHistory[idxHistory].cPctHalted    = cPctHalted;
    pState->aHistory[idxHistory].cPctOther     = cPctOther;

    pState->idxHistory = (uint16_t)idxHistory;
    if (pState->cHistoryEntries < RT_ELEMENTS(pState->aHistory))
        pState->cHistoryEntries++;
}


/**
 * @callback_method_impl{FNTMTIMERINT,
 *      Timer callback that calculates the CPU load since the last
 *      time it was called.}
 */
static DECLCALLBACK(void) tmR3CpuLoadTimer(PVM pVM, TMTIMERHANDLE hTimer, void *pvUser)
{
    /*
     * Re-arm the timer first.
     */
    int rc = TMTimerSetMillies(pVM, hTimer, 1000);
    AssertLogRelRC(rc);
    NOREF(pvUser);

    /*
     * Update the values for each CPU.
     */
    uint64_t cNsTotalAll       = 0;
    uint64_t cNsExecutingAll   = 0;
    uint64_t cNsHaltedAll      = 0;
    for (VMCPUID iCpu = 0; iCpu < pVM->cCpus; iCpu++)
    {
        PVMCPU      pVCpu = pVM->apCpusR3[iCpu];

        /* Try get a stable data set. */
        uint32_t    cTries       = 3;
        uint64_t    nsNow        = RTTimeNanoTS();
        uint32_t    uTimesGen    = ASMAtomicReadU32(&pVCpu->tm.s.uTimesGen);
        bool        fSuspended   = pVCpu->tm.s.fSuspended;
        uint64_t    nsStartTotal = pVCpu->tm.s.nsStartTotal;
        uint64_t    cNsExecuting = pVCpu->tm.s.cNsExecuting;
        uint64_t    cNsHalted    = pVCpu->tm.s.cNsHalted;
        while (RT_UNLIKELY(   (uTimesGen & 1) /* update in progress */
                           || uTimesGen != ASMAtomicReadU32(&pVCpu->tm.s.uTimesGen)))
        {
            if (!--cTries)
                break;
            ASMNopPause();
            nsNow        = RTTimeNanoTS();
            uTimesGen    = ASMAtomicReadU32(&pVCpu->tm.s.uTimesGen);
            fSuspended   = pVCpu->tm.s.fSuspended;
            nsStartTotal = pVCpu->tm.s.nsStartTotal;
            cNsExecuting = pVCpu->tm.s.cNsExecuting;
            cNsHalted    = pVCpu->tm.s.cNsHalted;
        }

        /* Totals */
        uint64_t cNsTotal = fSuspended ? nsStartTotal : nsNow - nsStartTotal;
        cNsTotalAll     += cNsTotal;
        cNsExecutingAll += cNsExecuting;
        cNsHaltedAll    += cNsHalted;

        /* Calc the PCTs and update the state. */
        tmR3CpuLoadTimerMakeUpdate(&pVCpu->tm.s.CpuLoad, cNsTotal, cNsExecuting, cNsHalted);

        /* Tell the VCpu to update the other and total stat members. */
        ASMAtomicWriteBool(&pVCpu->tm.s.fUpdateStats, true);
    }

    /*
     * Update the value for all the CPUs.
     */
    tmR3CpuLoadTimerMakeUpdate(&pVM->tm.s.CpuLoad, cNsTotalAll, cNsExecutingAll, cNsHaltedAll);

}

#endif /* !VBOX_WITHOUT_NS_ACCOUNTING */


/**
 * @callback_method_impl{PFNVMMEMTRENDEZVOUS,
 *      Worker for TMR3CpuTickParavirtEnable}
 */
static DECLCALLBACK(VBOXSTRICTRC) tmR3CpuTickParavirtEnable(PVM pVM, PVMCPU pVCpuEmt, void *pvData)
{
    AssertPtr(pVM); Assert(pVM->tm.s.fTSCModeSwitchAllowed); NOREF(pVCpuEmt); NOREF(pvData);
    Assert(pVM->tm.s.enmTSCMode != TMTSCMODE_NATIVE_API); /** @todo figure out NEM/win and paravirt */
    Assert(tmR3HasFixedTSC(pVM));

    if (pVM->tm.s.enmTSCMode != TMTSCMODE_REAL_TSC_OFFSET)
    {
        /*
         * The return value of TMCpuTickGet() and the guest's TSC value for each
         * CPU must remain constant across the TM TSC mode-switch.  Thus we have
         * the following equation (new/old signifies the new/old tsc modes):
         *      uNewTsc = uOldTsc
         *
         * Where (see tmCpuTickGetInternal):
         *      uOldTsc = uRawOldTsc - offTscRawSrcOld
         *      uNewTsc = uRawNewTsc - offTscRawSrcNew
         *
         * Solve it for offTscRawSrcNew without replacing uOldTsc:
         *     uRawNewTsc - offTscRawSrcNew = uOldTsc
         *  => -offTscRawSrcNew = uOldTsc - uRawNewTsc
         *  => offTscRawSrcNew  = uRawNewTsc - uOldTsc
         */
        uint64_t uRawOldTsc = tmR3CpuTickGetRawVirtualNoCheck(pVM);
        uint64_t uRawNewTsc = SUPReadTsc() * pVM->tm.s.u8TSCMultiplier;
        uint32_t cCpus = pVM->cCpus;
        for (uint32_t i = 0; i < cCpus; i++)
        {
            PVMCPU   pVCpu   = pVM->apCpusR3[i];
            uint64_t uOldTsc = uRawOldTsc - pVCpu->tm.s.offTSCRawSrc;
            pVCpu->tm.s.offTSCRawSrc = uRawNewTsc - uOldTsc;
            Assert(uRawNewTsc - pVCpu->tm.s.offTSCRawSrc >= uOldTsc); /* paranoia^256 */
        }

        LogRel(("TM: Switching TSC mode from '%s' to '%s'\n", tmR3GetTSCModeNameEx(pVM->tm.s.enmTSCMode),
                tmR3GetTSCModeNameEx(TMTSCMODE_REAL_TSC_OFFSET)));
        pVM->tm.s.enmTSCMode = TMTSCMODE_REAL_TSC_OFFSET;
    }
    return VINF_SUCCESS;
}


/**
 * Notify TM that the guest has enabled usage of a paravirtualized TSC.
 *
 * This may perform a EMT rendezvous and change the TSC virtualization mode.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 */
VMMR3_INT_DECL(int) TMR3CpuTickParavirtEnable(PVM pVM)
{
    int rc = VINF_SUCCESS;
    if (pVM->tm.s.fTSCModeSwitchAllowed)
        rc = VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ONCE, tmR3CpuTickParavirtEnable, NULL);
    else
        LogRel(("TM: Host/VM is not suitable for using TSC mode '%s', request to change TSC mode ignored\n",
                tmR3GetTSCModeNameEx(TMTSCMODE_REAL_TSC_OFFSET)));
    pVM->tm.s.fParavirtTscEnabled = true;
    return rc;
}


/**
 * @callback_method_impl{PFNVMMEMTRENDEZVOUS,
 *      Worker for TMR3CpuTickParavirtDisable}
 */
static DECLCALLBACK(VBOXSTRICTRC) tmR3CpuTickParavirtDisable(PVM pVM, PVMCPU pVCpuEmt, void *pvData)
{
    AssertPtr(pVM); Assert(pVM->tm.s.fTSCModeSwitchAllowed); NOREF(pVCpuEmt);
    RT_NOREF1(pvData);

    if (   pVM->tm.s.enmTSCMode == TMTSCMODE_REAL_TSC_OFFSET
        && pVM->tm.s.enmTSCMode != pVM->tm.s.enmOriginalTSCMode)
    {
        /*
         * See tmR3CpuTickParavirtEnable for an explanation of the conversion math.
         */
        uint64_t uRawOldTsc = SUPReadTsc() * pVM->tm.s.u8TSCMultiplier;
        uint64_t uRawNewTsc = tmR3CpuTickGetRawVirtualNoCheck(pVM);
        uint32_t cCpus = pVM->cCpus;
        for (uint32_t i = 0; i < cCpus; i++)
        {
            PVMCPU   pVCpu   = pVM->apCpusR3[i];
            uint64_t uOldTsc = uRawOldTsc - pVCpu->tm.s.offTSCRawSrc;
            pVCpu->tm.s.offTSCRawSrc = uRawNewTsc - uOldTsc;
            Assert(uRawNewTsc - pVCpu->tm.s.offTSCRawSrc >= uOldTsc); /* paranoia^256 */

            /* Update the last-seen tick here as we havent't been updating it (as we don't
               need it) while in pure TSC-offsetting mode. */
            pVCpu->tm.s.u64TSCLastSeen = uOldTsc;
        }

        LogRel(("TM: Switching TSC mode from '%s' to '%s'\n", tmR3GetTSCModeNameEx(pVM->tm.s.enmTSCMode),
                tmR3GetTSCModeNameEx(pVM->tm.s.enmOriginalTSCMode)));
        pVM->tm.s.enmTSCMode = pVM->tm.s.enmOriginalTSCMode;
    }
    return VINF_SUCCESS;
}


/**
 * Notify TM that the guest has disabled usage of a paravirtualized TSC.
 *
 * If TMR3CpuTickParavirtEnable() changed the TSC virtualization mode, this will
 * perform an EMT  rendezvous to revert those changes.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 */
VMMR3_INT_DECL(int) TMR3CpuTickParavirtDisable(PVM pVM)
{
    int rc = VINF_SUCCESS;
    if (pVM->tm.s.fTSCModeSwitchAllowed)
        rc = VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ONCE, tmR3CpuTickParavirtDisable, NULL);
    pVM->tm.s.fParavirtTscEnabled = false;
    return rc;
}


/**
 * Check whether the guest can be presented a fixed rate & monotonic TSC.
 *
 * @returns true if TSC is stable, false otherwise.
 * @param   pVM                     The cross context VM structure.
 * @param   fWithParavirtEnabled    Whether it's fixed & monotonic when
 *                                  paravirt. TSC is enabled or not.
 *
 * @remarks Must be called only after TMR3InitFinalize().
 */
VMMR3_INT_DECL(bool) TMR3CpuTickIsFixedRateMonotonic(PVM pVM, bool fWithParavirtEnabled)
{
    /** @todo figure out what exactly we want here later. */
    NOREF(fWithParavirtEnabled);
    PSUPGLOBALINFOPAGE pGip;
    return tmR3HasFixedTSC(pVM)                             /* Host has fixed-rate TSC. */
        && (   (pGip = g_pSUPGlobalInfoPage) == NULL        /* Can be NULL in driverless mode. */
            || (pGip->u32Mode != SUPGIPMODE_ASYNC_TSC));    /* GIP thinks it's monotonic. */
}


/**
 * Gets the 5 char clock name for the info tables.
 *
 * @returns The name.
 * @param   enmClock        The clock.
 */
DECLINLINE(const char *) tmR3Get5CharClockName(TMCLOCK enmClock)
{
    switch (enmClock)
    {
        case TMCLOCK_REAL:          return "Real ";
        case TMCLOCK_VIRTUAL:       return "Virt ";
        case TMCLOCK_VIRTUAL_SYNC:  return "VrSy ";
        case TMCLOCK_TSC:           return "TSC  ";
        default:                    return "Bad  ";
    }
}


/**
 * Display all timers.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helpers.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) tmR3TimerInfo(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    NOREF(pszArgs);
    pHlp->pfnPrintf(pHlp,
                    "Timers (pVM=%p)\n"
                    "%.*s %.*s %.*s %.*s Clock %18s %18s %6s %-25s Description\n",
                    pVM,
                    sizeof(RTR3PTR) * 2,        "pTimerR3        ",
                    sizeof(int32_t) * 2,        "offNext         ",
                    sizeof(int32_t) * 2,        "offPrev         ",
                    sizeof(int32_t) * 2,        "offSched        ",
                                                "Time",
                                                "Expire",
                                                "HzHint",
                                                "State");
    for (uint32_t idxQueue = 0; idxQueue < RT_ELEMENTS(pVM->tm.s.aTimerQueues); idxQueue++)
    {
        PTMTIMERQUEUE const pQueue   = &pVM->tm.s.aTimerQueues[idxQueue];
        const char * const  pszClock = tmR3Get5CharClockName(pQueue->enmClock);
        PDMCritSectRwEnterShared(pVM, &pQueue->AllocLock, VERR_IGNORED);
        for (uint32_t idxTimer = 0; idxTimer < pQueue->cTimersAlloc; idxTimer++)
        {
            PTMTIMER     pTimer   = &pQueue->paTimers[idxTimer];
            TMTIMERSTATE enmState = pTimer->enmState;
            if (enmState < TMTIMERSTATE_DESTROY && enmState > TMTIMERSTATE_INVALID)
                pHlp->pfnPrintf(pHlp,
                                "%p %08RX32 %08RX32 %08RX32 %s %18RU64 %18RU64 %6RU32 %-25s %s\n",
                                pTimer,
                                pTimer->idxNext,
                                pTimer->idxPrev,
                                pTimer->idxScheduleNext,
                                pszClock,
                                TMTimerGet(pVM, pTimer->hSelf),
                                pTimer->u64Expire,
                                pTimer->uHzHint,
                                tmTimerState(enmState),
                                pTimer->szName);
        }
        PDMCritSectRwLeaveShared(pVM, &pQueue->AllocLock);
    }
}


/**
 * Display all active timers.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helpers.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) tmR3TimerInfoActive(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    NOREF(pszArgs);
    pHlp->pfnPrintf(pHlp,
                    "Active Timers (pVM=%p)\n"
                    "%.*s %.*s %.*s %.*s Clock %18s %18s %6s %-25s Description\n",
                    pVM,
                    sizeof(RTR3PTR) * 2,        "pTimerR3        ",
                    sizeof(int32_t) * 2,        "offNext         ",
                    sizeof(int32_t) * 2,        "offPrev         ",
                    sizeof(int32_t) * 2,        "offSched        ",
                                                "Time",
                                                "Expire",
                                                "HzHint",
                                                "State");
    for (uint32_t idxQueue = 0; idxQueue < RT_ELEMENTS(pVM->tm.s.aTimerQueues); idxQueue++)
    {
        PTMTIMERQUEUE const pQueue   = &pVM->tm.s.aTimerQueues[idxQueue];
        const char * const  pszClock = tmR3Get5CharClockName(pQueue->enmClock);
        PDMCritSectRwEnterShared(pVM, &pQueue->AllocLock, VERR_IGNORED);
        PDMCritSectEnter(pVM, &pQueue->TimerLock, VERR_IGNORED);

        for (PTMTIMERR3 pTimer = tmTimerQueueGetHead(pQueue, pQueue);
             pTimer;
             pTimer = tmTimerGetNext(pQueue, pTimer))
        {
            pHlp->pfnPrintf(pHlp,
                            "%p %08RX32 %08RX32 %08RX32 %s %18RU64 %18RU64 %6RU32 %-25s %s\n",
                            pTimer,
                            pTimer->idxNext,
                            pTimer->idxPrev,
                            pTimer->idxScheduleNext,
                            pszClock,
                            TMTimerGet(pVM, pTimer->hSelf),
                            pTimer->u64Expire,
                            pTimer->uHzHint,
                            tmTimerState(pTimer->enmState),
                            pTimer->szName);
        }

        PDMCritSectLeave(pVM, &pQueue->TimerLock);
        PDMCritSectRwLeaveShared(pVM, &pQueue->AllocLock);
    }
}


/**
 * Display all clocks.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helpers.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) tmR3InfoClocks(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    NOREF(pszArgs);

    /*
     * Read the times first to avoid more than necessary time variation.
     */
    const uint64_t u64Virtual     = TMVirtualGet(pVM);
    const uint64_t u64VirtualSync = TMVirtualSyncGet(pVM);
    const uint64_t u64Real        = TMRealGet(pVM);

    for (VMCPUID i = 0; i < pVM->cCpus; i++)
    {
        PVMCPU   pVCpu  = pVM->apCpusR3[i];
        uint64_t u64TSC = TMCpuTickGet(pVCpu);

        /*
         * TSC
         */
        pHlp->pfnPrintf(pHlp,
                        "Cpu Tick: %18RU64 (%#016RX64) %RU64Hz %s - virtualized",
                        u64TSC, u64TSC, TMCpuTicksPerSecond(pVM),
                        pVCpu->tm.s.fTSCTicking ? "ticking" : "paused");
        if (pVM->tm.s.enmTSCMode == TMTSCMODE_REAL_TSC_OFFSET)
        {
            pHlp->pfnPrintf(pHlp, " - real tsc offset");
            if (pVCpu->tm.s.offTSCRawSrc)
                pHlp->pfnPrintf(pHlp, "\n          offset %RU64", pVCpu->tm.s.offTSCRawSrc);
        }
        else if (pVM->tm.s.enmTSCMode == TMTSCMODE_NATIVE_API)
            pHlp->pfnPrintf(pHlp, " - native api");
        else
            pHlp->pfnPrintf(pHlp, " - virtual clock");
        pHlp->pfnPrintf(pHlp, "\n");
    }

    /*
     * virtual
     */
    pHlp->pfnPrintf(pHlp,
                    " Virtual: %18RU64 (%#016RX64) %RU64Hz %s",
                    u64Virtual, u64Virtual, TMVirtualGetFreq(pVM),
                    pVM->tm.s.cVirtualTicking ? "ticking" : "paused");
    if (pVM->tm.s.fVirtualWarpDrive)
        pHlp->pfnPrintf(pHlp, " WarpDrive %RU32 %%", pVM->tm.s.u32VirtualWarpDrivePercentage);
    pHlp->pfnPrintf(pHlp, "\n");

    /*
     * virtual sync
     */
    pHlp->pfnPrintf(pHlp,
                    "VirtSync: %18RU64 (%#016RX64) %s%s",
                    u64VirtualSync, u64VirtualSync,
                    pVM->tm.s.fVirtualSyncTicking ? "ticking" : "paused",
                    pVM->tm.s.fVirtualSyncCatchUp ? " - catchup" : "");
    if (pVM->tm.s.offVirtualSync)
    {
        pHlp->pfnPrintf(pHlp, "\n          offset %RU64", pVM->tm.s.offVirtualSync);
        if (pVM->tm.s.u32VirtualSyncCatchUpPercentage)
            pHlp->pfnPrintf(pHlp, "  catch-up rate %u %%", pVM->tm.s.u32VirtualSyncCatchUpPercentage);
    }
    pHlp->pfnPrintf(pHlp, "\n");

    /*
     * real
     */
    pHlp->pfnPrintf(pHlp,
                    "    Real: %18RU64 (%#016RX64) %RU64Hz\n",
                    u64Real, u64Real, TMRealGetFreq(pVM));
}


/**
 * Helper for tmR3InfoCpuLoad that adjust @a uPct to the given graph width.
 */
DECLINLINE(size_t) tmR3InfoCpuLoadAdjustWidth(size_t uPct, size_t cchWidth)
{
    if (cchWidth != 100)
        uPct = (size_t)(((double)uPct + 0.5) * ((double)cchWidth / 100.0));
    return uPct;
}


/**
 * @callback_method_impl{FNDBGFINFOARGVINT}
 */
static DECLCALLBACK(void) tmR3InfoCpuLoad(PVM pVM, PCDBGFINFOHLP pHlp, int cArgs, char **papszArgs)
{
    char szTmp[1024];

    /*
     * Parse arguments.
     */
    PTMCPULOADSTATE pState      = &pVM->tm.s.CpuLoad;
    VMCPUID         idCpu       = 0;
    bool            fAllCpus    = true;
    bool            fExpGraph   = true;
    uint32_t        cchWidth    = 80;
    uint32_t        cPeriods    = RT_ELEMENTS(pState->aHistory);
    uint32_t        cRows       = 60;

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "all",            'a',    RTGETOPT_REQ_NOTHING },
        { "cpu",            'c',    RTGETOPT_REQ_UINT32 },
        { "periods",        'p',    RTGETOPT_REQ_UINT32 },
        { "rows",           'r',    RTGETOPT_REQ_UINT32 },
        { "uni",            'u',    RTGETOPT_REQ_NOTHING },
        { "uniform",        'u',    RTGETOPT_REQ_NOTHING },
        { "width",          'w',    RTGETOPT_REQ_UINT32 },
        { "exp",            'x',    RTGETOPT_REQ_NOTHING },
        { "exponential",    'x',    RTGETOPT_REQ_NOTHING },
    };

    RTGETOPTSTATE State;
    int rc = RTGetOptInit(&State, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0 /*fFlags*/);
    AssertRC(rc);

    RTGETOPTUNION ValueUnion;
    while ((rc = RTGetOpt(&State, &ValueUnion)) != 0)
    {
        switch (rc)
        {
            case 'a':
                pState   = &pVM->apCpusR3[0]->tm.s.CpuLoad;
                idCpu    = 0;
                fAllCpus = true;
                break;
            case 'c':
                if (ValueUnion.u32 < pVM->cCpus)
                {
                    pState = &pVM->apCpusR3[ValueUnion.u32]->tm.s.CpuLoad;
                    idCpu  = ValueUnion.u32;
                }
                else
                {
                    pState = &pVM->tm.s.CpuLoad;
                    idCpu  = VMCPUID_ALL;
                }
                fAllCpus = false;
                break;
            case 'p':
                cPeriods = RT_MIN(RT_MAX(ValueUnion.u32, 1), RT_ELEMENTS(pState->aHistory));
                break;
            case 'r':
                cRows = RT_MIN(RT_MAX(ValueUnion.u32, 5), RT_ELEMENTS(pState->aHistory));
                break;
            case 'w':
                cchWidth = RT_MIN(RT_MAX(ValueUnion.u32, 10), sizeof(szTmp) - 32);
                break;
            case 'x':
                fExpGraph = true;
                break;
            case 'u':
                fExpGraph = false;
                break;
            case 'h':
                pHlp->pfnPrintf(pHlp,
                                "Usage: cpuload [parameters]\n"
                                "  all, -a\n"
                                "    Show statistics for all CPUs. (default)\n"
                                "  cpu=id, -c id\n"
                                "    Show statistics for the specified CPU ID.  Show combined stats if out of range.\n"
                                "  periods=count, -p count\n"
                                "    Number of periods to show.  Default: all\n"
                                "  rows=count, -r count\n"
                                "    Number of rows in the graphs.  Default: 60\n"
                                "  width=count, -w count\n"
                                "    Core graph width in characters. Default: 80\n"
                                "  exp, exponential, -e\n"
                                "    Do 1:1 for more recent half / 30 seconds of the graph, combine the\n"
                                "    rest into increasinly larger chunks.  Default.\n"
                                "  uniform, uni, -u\n"
                                "    Combine periods into rows in a uniform manner for the whole graph.\n");
                return;
            default:
                pHlp->pfnGetOptError(pHlp, rc, &ValueUnion, &State);
                return;
        }
    }

    /*
     * Do the job.
     */
    for (;;)
    {
        uint32_t const cMaxPeriods = pState->cHistoryEntries;
        if (cPeriods > cMaxPeriods)
            cPeriods = cMaxPeriods;
        if (cPeriods > 0)
        {
            if (fAllCpus)
            {
                if (idCpu > 0)
                    pHlp->pfnPrintf(pHlp, "\n");
                pHlp->pfnPrintf(pHlp, "    CPU load for virtual CPU %#04x\n"
                                      "    -------------------------------\n", idCpu);
            }

            /*
             * Figure number of periods per chunk.  We can either do this in a linear
             * fashion or a exponential fashion that compresses old history more.
             */
            size_t cPerRowDecrement = 0;
            size_t cPeriodsPerRow   = 1;
            if (cRows < cPeriods)
            {
                if (!fExpGraph)
                    cPeriodsPerRow   = (cPeriods + cRows / 2) / cRows;
                else
                {
                    /* The last 30 seconds or half of the rows are 1:1, the other part
                       is in increasing period counts.  Code is a little simple but seems
                       to do the job most of the time, which is all I have time now. */
                    size_t cPeriodsOneToOne = RT_MIN(30, cRows / 2);
                    size_t cRestRows        = cRows    - cPeriodsOneToOne;
                    size_t cRestPeriods     = cPeriods - cPeriodsOneToOne;

                    size_t cPeriodsInWindow = 0;
                    for (cPeriodsPerRow = 0; cPeriodsPerRow <= cRestRows && cPeriodsInWindow < cRestPeriods; cPeriodsPerRow++)
                        cPeriodsInWindow += cPeriodsPerRow + 1;

                    size_t iLower = 1;
                    while (cPeriodsInWindow < cRestPeriods)
                    {
                        cPeriodsPerRow++;
                        cPeriodsInWindow += cPeriodsPerRow;
                        cPeriodsInWindow -= iLower;
                        iLower++;
                    }

                    cPerRowDecrement = 1;
                }
            }

            /*
             * Do the work.
             */
            size_t cPctExecuting       = 0;
            size_t cPctOther           = 0;
            size_t cPeriodsAccumulated = 0;

            size_t cRowsLeft = cRows;
            size_t iHistory  = (pState->idxHistory - cPeriods) % RT_ELEMENTS(pState->aHistory);
            while (cPeriods-- > 0)
            {
                iHistory++;
                if (iHistory >= RT_ELEMENTS(pState->aHistory))
                    iHistory = 0;

                cPctExecuting        += pState->aHistory[iHistory].cPctExecuting;
                cPctOther            += pState->aHistory[iHistory].cPctOther;
                cPeriodsAccumulated  += 1;
                if (   cPeriodsAccumulated >= cPeriodsPerRow
                    || cPeriods < cRowsLeft)
                {
                    /*
                     * Format and output the line.
                     */
                    size_t offTmp = 0;
                    size_t i      = tmR3InfoCpuLoadAdjustWidth(cPctExecuting / cPeriodsAccumulated, cchWidth);
                    while (i-- > 0)
                        szTmp[offTmp++] = '#';
                    i = tmR3InfoCpuLoadAdjustWidth(cPctOther / cPeriodsAccumulated, cchWidth);
                    while (i-- > 0)
                        szTmp[offTmp++] = 'O';
                    szTmp[offTmp] = '\0';

                    cRowsLeft--;
                    pHlp->pfnPrintf(pHlp, "%3zus: %s\n", cPeriods + cPeriodsAccumulated / 2, szTmp);

                    /* Reset the state: */
                    cPctExecuting       = 0;
                    cPctOther           = 0;
                    cPeriodsAccumulated = 0;
                    if (cPeriodsPerRow > cPerRowDecrement)
                        cPeriodsPerRow -= cPerRowDecrement;
                }
            }
            pHlp->pfnPrintf(pHlp, "    (#=guest, O=VMM overhead)  idCpu=%#x\n", idCpu);

        }
        else
            pHlp->pfnPrintf(pHlp, "No load data.\n");

        /*
         * Next CPU if we're display all.
         */
        if (!fAllCpus)
            break;
        idCpu++;
        if (idCpu >= pVM->cCpus)
            break;
        pState   = &pVM->apCpusR3[idCpu]->tm.s.CpuLoad;
    }

}


/**
 * Gets the descriptive TM TSC mode name given the enum value.
 *
 * @returns The name.
 * @param   enmMode             The mode to name.
 */
static const char *tmR3GetTSCModeNameEx(TMTSCMODE enmMode)
{
    switch (enmMode)
    {
        case TMTSCMODE_REAL_TSC_OFFSET:    return "RealTSCOffset";
        case TMTSCMODE_VIRT_TSC_EMULATED:  return "VirtTSCEmulated";
        case TMTSCMODE_DYNAMIC:            return "Dynamic";
        case TMTSCMODE_NATIVE_API:         return "NativeApi";
        default:                           return "???";
    }
}


/**
 * Gets the descriptive TM TSC mode name.
 *
 * @returns The name.
 * @param   pVM      The cross context VM structure.
 */
static const char *tmR3GetTSCModeName(PVM pVM)
{
    Assert(pVM);
    return tmR3GetTSCModeNameEx(pVM->tm.s.enmTSCMode);
}

