/* $Id: EM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * EM - Execution Monitor / Manager.
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

/** @page pg_em         EM - The Execution Monitor / Manager
 *
 * The Execution Monitor/Manager is responsible for running the VM, scheduling
 * the right kind of execution (Raw-mode, Hardware Assisted, Recompiled or
 * Interpreted), and keeping the CPU states in sync. The function
 * EMR3ExecuteVM() is the 'main-loop' of the VM, while each of the execution
 * modes has different inner loops (emR3RawExecute, emR3HmExecute, and
 * emR3RmExecute).
 *
 * The interpreted execution is only used to avoid switching between
 * raw-mode/hm and the recompiler when fielding virtualization traps/faults.
 * The interpretation is thus implemented as part of EM.
 *
 * @see grp_em
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_EM
#define VMCPU_INCL_CPUM_GST_CTX /* for CPUM_IMPORT_GUEST_STATE_RET & interrupt injection */
#include <VBox/vmm/em.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/selm.h>
#include <VBox/vmm/trpm.h>
#include <VBox/vmm/iem.h>
#include <VBox/vmm/nem.h>
#include <VBox/vmm/iom.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/apic.h>
#include <VBox/vmm/tm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/ssm.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/pdmcritsect.h>
#include <VBox/vmm/pdmqueue.h>
#include <VBox/vmm/hm.h>
#include "EMInternal.h"
#include <VBox/vmm/vm.h>
#include <VBox/vmm/uvm.h>
#include <VBox/vmm/cpumdis.h>
#include <VBox/dis.h>
#include <VBox/err.h>
#include "VMMTracing.h"

#include <iprt/asm.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/thread.h>

#include "EMInline.h"


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int) emR3Save(PVM pVM, PSSMHANDLE pSSM);
static DECLCALLBACK(int) emR3Load(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass);
#if defined(LOG_ENABLED) || defined(VBOX_STRICT)
static const char *emR3GetStateName(EMSTATE enmState);
#endif
static VBOXSTRICTRC emR3Debug(PVM pVM, PVMCPU pVCpu, VBOXSTRICTRC rc);


/**
 * Initializes the EM.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3_INT_DECL(int) EMR3Init(PVM pVM)
{
    LogFlow(("EMR3Init\n"));
    /*
     * Assert alignment and sizes.
     */
    AssertCompileMemberAlignment(VM, em.s, 32);
    AssertCompile(sizeof(pVM->em.s) <= sizeof(pVM->em.padding));
    AssertCompile(RT_SIZEOFMEMB(VMCPU, em.s.u.FatalLongJump) <= RT_SIZEOFMEMB(VMCPU, em.s.u.achPaddingFatalLongJump));
    AssertCompile(RT_SIZEOFMEMB(VMCPU, em.s) <= RT_SIZEOFMEMB(VMCPU, em.padding));

    /*
     * Init the structure.
     */
    PCFGMNODE pCfgRoot = CFGMR3GetRoot(pVM);
    PCFGMNODE pCfgEM = CFGMR3GetChild(pCfgRoot, "EM");

    int rc = CFGMR3QueryBoolDef(pCfgEM, "IemExecutesAll", &pVM->em.s.fIemExecutesAll,
#if defined(RT_ARCH_ARM64) && defined(RT_OS_DARWIN) && !defined(VBOX_VMM_TARGET_ARMV8)
                                true
#else
                                false
#endif
                                );
    AssertLogRelRCReturn(rc, rc);

    bool fEnabled;
    rc = CFGMR3QueryBoolDef(pCfgEM, "TripleFaultReset", &fEnabled, false);
    AssertLogRelRCReturn(rc, rc);
    pVM->em.s.fGuruOnTripleFault = !fEnabled;
    if (!pVM->em.s.fGuruOnTripleFault && pVM->cCpus > 1)
    {
        LogRel(("EM: Overriding /EM/TripleFaultReset, must be false on SMP.\n"));
        pVM->em.s.fGuruOnTripleFault = true;
    }

    LogRel(("EMR3Init: fIemExecutesAll=%RTbool fGuruOnTripleFault=%RTbool\n", pVM->em.s.fIemExecutesAll, pVM->em.s.fGuruOnTripleFault));

    /** @cfgm{/EM/ExitOptimizationEnabled, bool, true}
     * Whether to try correlate exit history in any context, detect hot spots and
     * try optimize these using IEM if there are other exits close by.  This
     * overrides the context specific settings. */
    bool fExitOptimizationEnabled = true;
    rc = CFGMR3QueryBoolDef(pCfgEM, "ExitOptimizationEnabled", &fExitOptimizationEnabled, true);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/EM/ExitOptimizationEnabledR0, bool, true}
     * Whether to optimize exits in ring-0.  Setting this to false will also disable
     * the /EM/ExitOptimizationEnabledR0PreemptDisabled setting.  Depending on preemption
     * capabilities of the host kernel, this optimization may be unavailable. */
    bool fExitOptimizationEnabledR0 = true;
    rc = CFGMR3QueryBoolDef(pCfgEM, "ExitOptimizationEnabledR0", &fExitOptimizationEnabledR0, true);
    AssertLogRelRCReturn(rc, rc);
    fExitOptimizationEnabledR0 &= fExitOptimizationEnabled;

    /** @cfgm{/EM/ExitOptimizationEnabledR0PreemptDisabled, bool, false}
     * Whether to optimize exits in ring-0 when preemption is disable (or preemption
     * hooks are in effect). */
    /** @todo change the default to true here */
    bool fExitOptimizationEnabledR0PreemptDisabled = true;
    rc = CFGMR3QueryBoolDef(pCfgEM, "ExitOptimizationEnabledR0PreemptDisabled", &fExitOptimizationEnabledR0PreemptDisabled, false);
    AssertLogRelRCReturn(rc, rc);
    fExitOptimizationEnabledR0PreemptDisabled &= fExitOptimizationEnabledR0;

    /** @cfgm{/EM/HistoryExecMaxInstructions, integer, 16, 65535, 8192}
     * Maximum number of instruction to let EMHistoryExec execute in one go. */
    uint16_t cHistoryExecMaxInstructions = 8192;
    rc = CFGMR3QueryU16Def(pCfgEM, "HistoryExecMaxInstructions", &cHistoryExecMaxInstructions, cHistoryExecMaxInstructions);
    AssertLogRelRCReturn(rc, rc);
    if (cHistoryExecMaxInstructions < 16)
        return VMSetError(pVM, VERR_OUT_OF_RANGE, RT_SRC_POS, "/EM/HistoryExecMaxInstructions value is too small, min 16");

    /** @cfgm{/EM/HistoryProbeMaxInstructionsWithoutExit, integer, 2, 65535, 24 for HM, 32 for NEM}
     * Maximum number of instruction between exits during probing. */
    uint16_t cHistoryProbeMaxInstructionsWithoutExit = 24;
#ifdef RT_OS_WINDOWS
    if (VM_IS_NEM_ENABLED(pVM))
        cHistoryProbeMaxInstructionsWithoutExit = 32;
#endif
    rc = CFGMR3QueryU16Def(pCfgEM, "HistoryProbeMaxInstructionsWithoutExit", &cHistoryProbeMaxInstructionsWithoutExit,
                           cHistoryProbeMaxInstructionsWithoutExit);
    AssertLogRelRCReturn(rc, rc);
    if (cHistoryProbeMaxInstructionsWithoutExit < 2)
        return VMSetError(pVM, VERR_OUT_OF_RANGE, RT_SRC_POS,
                          "/EM/HistoryProbeMaxInstructionsWithoutExit value is too small, min 16");

    /** @cfgm{/EM/HistoryProbMinInstructions, integer, 0, 65535, depends}
     * The default is (/EM/HistoryProbeMaxInstructionsWithoutExit + 1) * 3. */
    uint16_t cHistoryProbeMinInstructions = cHistoryProbeMaxInstructionsWithoutExit < 0x5554
                                          ? (cHistoryProbeMaxInstructionsWithoutExit + 1) * 3 : 0xffff;
    rc = CFGMR3QueryU16Def(pCfgEM, "HistoryProbMinInstructions", &cHistoryProbeMinInstructions,
                           cHistoryProbeMinInstructions);
    AssertLogRelRCReturn(rc, rc);

    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];
        pVCpu->em.s.fExitOptimizationEnabled                  = fExitOptimizationEnabled;
        pVCpu->em.s.fExitOptimizationEnabledR0                = fExitOptimizationEnabledR0;
        pVCpu->em.s.fExitOptimizationEnabledR0PreemptDisabled = fExitOptimizationEnabledR0PreemptDisabled;
        pVCpu->em.s.cHistoryExecMaxInstructions               = cHistoryExecMaxInstructions;
        pVCpu->em.s.cHistoryProbeMinInstructions              = cHistoryProbeMinInstructions;
        pVCpu->em.s.cHistoryProbeMaxInstructionsWithoutExit   = cHistoryProbeMaxInstructionsWithoutExit;
    }

#ifdef VBOX_WITH_IEM_RECOMPILER
    /** @cfgm{/EM/IemRecompiled, bool, true}
     * Whether IEM bulk execution is recompiled or interpreted. */
    rc = CFGMR3QueryBoolDef(pCfgEM, "IemRecompiled", &pVM->em.s.fIemRecompiled, true);
    AssertLogRelRCReturn(rc, rc);
#endif

    /*
     * Saved state.
     */
    rc = SSMR3RegisterInternal(pVM, "em", 0, EM_SAVED_STATE_VERSION, 16,
                               NULL, NULL, NULL,
                               NULL, emR3Save, NULL,
                               NULL, emR3Load, NULL);
    if (RT_FAILURE(rc))
        return rc;

    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];

        pVCpu->em.s.enmState            = idCpu == 0 ? EMSTATE_NONE : EMSTATE_WAIT_SIPI;
        pVCpu->em.s.enmPrevState        = EMSTATE_NONE;
        pVCpu->em.s.msTimeSliceStart    = 0; /* paranoia */
        pVCpu->em.s.idxContinueExitRec  = UINT16_MAX;

# define EM_REG_COUNTER(a, b, c) \
        rc = STAMR3RegisterF(pVM, a, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES, c, b, idCpu); \
        AssertRC(rc);

# define EM_REG_COUNTER_USED(a, b, c) \
        rc = STAMR3RegisterF(pVM, a, STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES, c, b, idCpu); \
        AssertRC(rc);

# define EM_REG_PROFILE(a, b, c) \
        rc = STAMR3RegisterF(pVM, a, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, c, b, idCpu); \
        AssertRC(rc);

# define EM_REG_PROFILE_ADV(a, b, c) \
        rc = STAMR3RegisterF(pVM, a, STAMTYPE_PROFILE_ADV, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, c, b, idCpu); \
        AssertRC(rc);

        /*
         * Statistics.
         */
#ifdef VBOX_WITH_STATISTICS
        EM_REG_COUNTER_USED(&pVCpu->em.s.StatIoRestarted,   "/EM/CPU%u/R3/PrivInst/IoRestarted",        "I/O instructions restarted in ring-3.");
        EM_REG_COUNTER_USED(&pVCpu->em.s.StatIoIem,         "/EM/CPU%u/R3/PrivInst/IoIem",              "I/O instructions end to IEM in ring-3.");

        /* these should be considered for release statistics. */
        EM_REG_COUNTER(&pVCpu->em.s.StatIOEmu,              "/PROF/CPU%u/EM/Emulation/IO",      "Profiling of emR3RawExecuteIOInstruction.");
        EM_REG_COUNTER(&pVCpu->em.s.StatPrivEmu,            "/PROF/CPU%u/EM/Emulation/Priv",    "Profiling of emR3RawPrivileged.");
        EM_REG_PROFILE(&pVCpu->em.s.StatHMEntry,            "/PROF/CPU%u/EM/HMEnter",           "Profiling Hardware Accelerated Mode entry overhead.");
#endif
        EM_REG_PROFILE(&pVCpu->em.s.StatHMExec,             "/PROF/CPU%u/EM/HMExec",            "Profiling Hardware Accelerated Mode execution.");
        EM_REG_COUNTER(&pVCpu->em.s.StatHMExecuteCalled,    "/PROF/CPU%u/EM/HMExecuteCalled",   "Number of times enmR3HMExecute is called.");
#ifdef VBOX_WITH_STATISTICS
        EM_REG_PROFILE(&pVCpu->em.s.StatIEMEmu,             "/PROF/CPU%u/EM/IEMEmuSingle",      "Profiling single instruction IEM execution.");
        EM_REG_PROFILE(&pVCpu->em.s.StatIEMThenREM,         "/PROF/CPU%u/EM/IEMThenRem",        "Profiling IEM-then-REM instruction execution (by IEM).");
        EM_REG_PROFILE(&pVCpu->em.s.StatNEMEntry,           "/PROF/CPU%u/EM/NEMEnter",          "Profiling NEM entry overhead.");
#endif
        EM_REG_PROFILE(&pVCpu->em.s.StatNEMExec,            "/PROF/CPU%u/EM/NEMExec",           "Profiling NEM execution.");
        EM_REG_COUNTER(&pVCpu->em.s.StatNEMExecuteCalled,   "/PROF/CPU%u/EM/NEMExecuteCalled",  "Number of times enmR3NEMExecute is called.");
#ifdef VBOX_WITH_STATISTICS
        EM_REG_PROFILE(&pVCpu->em.s.StatREMExec,            "/PROF/CPU%u/EM/REMExec",           "Profiling REM execution.");
#endif

        EM_REG_COUNTER(&pVCpu->em.s.StatForcedActions,      "/PROF/CPU%u/EM/ForcedActions",     "Profiling forced action execution.");
        EM_REG_COUNTER(&pVCpu->em.s.StatHalted,             "/PROF/CPU%u/EM/Halted",            "Profiling halted state (VMR3WaitHalted).");
        EM_REG_PROFILE_ADV(&pVCpu->em.s.StatCapped,         "/PROF/CPU%u/EM/Capped",            "Profiling capped state (sleep).");
        EM_REG_COUNTER(&pVCpu->em.s.StatREMTotal,           "/PROF/CPU%u/EM/REMTotal",          "Profiling emR3RecompilerExecute (excluding FFs).");

        EM_REG_PROFILE_ADV(&pVCpu->em.s.StatTotal,          "/PROF/CPU%u/EM/Total",             "Profiling EMR3ExecuteVM.");

        rc = STAMR3RegisterF(pVM, &pVCpu->em.s.iNextExit, STAMTYPE_U64, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                             "Number of recorded exits.", "/PROF/CPU%u/EM/RecordedExits", idCpu);
        AssertRC(rc);

        /* History record statistics */
        rc = STAMR3RegisterF(pVM, &pVCpu->em.s.cExitRecordUsed, STAMTYPE_U32, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                             "Number of used hash table entries.", "/EM/CPU%u/ExitHashing/Used", idCpu);
        AssertRC(rc);

        for (uint32_t iStep = 0; iStep < RT_ELEMENTS(pVCpu->em.s.aStatHistoryRecHits); iStep++)
        {
            rc = STAMR3RegisterF(pVM, &pVCpu->em.s.aStatHistoryRecHits[iStep], STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                                 "Number of hits at this step.",             "/EM/CPU%u/ExitHashing/Step%02u-Hits", idCpu, iStep);
            AssertRC(rc);
            rc = STAMR3RegisterF(pVM, &pVCpu->em.s.aStatHistoryRecTypeChanged[iStep], STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                                 "Number of type changes at this step.",     "/EM/CPU%u/ExitHashing/Step%02u-TypeChanges", idCpu, iStep);
            AssertRC(rc);
            rc = STAMR3RegisterF(pVM, &pVCpu->em.s.aStatHistoryRecTypeChanged[iStep], STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                                 "Number of replacments at this step.",     "/EM/CPU%u/ExitHashing/Step%02u-Replacments", idCpu, iStep);
            AssertRC(rc);
            rc = STAMR3RegisterF(pVM, &pVCpu->em.s.aStatHistoryRecNew[iStep], STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                                 "Number of new inserts at this step.",     "/EM/CPU%u/ExitHashing/Step%02u-NewInserts", idCpu, iStep);
            AssertRC(rc);
        }

        EM_REG_PROFILE(&pVCpu->em.s.StatHistoryExec,              "/EM/CPU%u/ExitOpt/Exec",              "Profiling normal EMHistoryExec operation.");
        EM_REG_COUNTER(&pVCpu->em.s.StatHistoryExecSavedExits,    "/EM/CPU%u/ExitOpt/ExecSavedExit",     "Net number of saved exits.");
        EM_REG_COUNTER(&pVCpu->em.s.StatHistoryExecInstructions,  "/EM/CPU%u/ExitOpt/ExecInstructions",  "Number of instructions executed during normal operation.");
        EM_REG_PROFILE(&pVCpu->em.s.StatHistoryProbe,             "/EM/CPU%u/ExitOpt/Probe",             "Profiling EMHistoryExec when probing.");
        EM_REG_COUNTER(&pVCpu->em.s.StatHistoryProbeInstructions, "/EM/CPU%u/ExitOpt/ProbeInstructions", "Number of instructions executed during probing.");
        EM_REG_COUNTER(&pVCpu->em.s.StatHistoryProbedNormal,      "/EM/CPU%u/ExitOpt/ProbedNormal",      "Number of EMEXITACTION_NORMAL_PROBED results.");
        EM_REG_COUNTER(&pVCpu->em.s.StatHistoryProbedExecWithMax, "/EM/CPU%u/ExitOpt/ProbedExecWithMax", "Number of EMEXITACTION_EXEC_WITH_MAX results.");
        EM_REG_COUNTER(&pVCpu->em.s.StatHistoryProbedToRing3,     "/EM/CPU%u/ExitOpt/ProbedToRing3",     "Number of ring-3 probe continuations.");
    }

    emR3InitDbg(pVM);
    return VINF_SUCCESS;
}


/**
 * Called when a VM initialization stage is completed.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   enmWhat         The initialization state that was completed.
 */
VMMR3_INT_DECL(int) EMR3InitCompleted(PVM pVM, VMINITCOMPLETED enmWhat)
{
    if (enmWhat == VMINITCOMPLETED_RING0)
        LogRel(("EM: Exit history optimizations: enabled=%RTbool enabled-r0=%RTbool enabled-r0-no-preemption=%RTbool\n",
                pVM->apCpusR3[0]->em.s.fExitOptimizationEnabled, pVM->apCpusR3[0]->em.s.fExitOptimizationEnabledR0,
                pVM->apCpusR3[0]->em.s.fExitOptimizationEnabledR0PreemptDisabled));
    return VINF_SUCCESS;
}


/**
 * Applies relocations to data and code managed by this
 * component. This function will be called at init and
 * whenever the VMM need to relocate it self inside the GC.
 *
 * @param   pVM     The cross context VM structure.
 */
VMMR3_INT_DECL(void) EMR3Relocate(PVM pVM)
{
    LogFlow(("EMR3Relocate\n"));
    RT_NOREF(pVM);
}


/**
 * Reset the EM state for a CPU.
 *
 * Called by EMR3Reset and hot plugging.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
VMMR3_INT_DECL(void) EMR3ResetCpu(PVMCPU pVCpu)
{
    /* Reset scheduling state. */
    VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_UNHALT);

    /* VMR3ResetFF may return VINF_EM_RESET or VINF_EM_SUSPEND, so transition
       out of the HALTED state here so that enmPrevState doesn't end up as
       HALTED when EMR3Execute returns. */
    if (pVCpu->em.s.enmState == EMSTATE_HALTED)
    {
        Log(("EMR3ResetCpu: Cpu#%u %s -> %s\n", pVCpu->idCpu, emR3GetStateName(pVCpu->em.s.enmState), pVCpu->idCpu == 0 ? "EMSTATE_NONE" : "EMSTATE_WAIT_SIPI"));
        pVCpu->em.s.enmState = pVCpu->idCpu == 0 ? EMSTATE_NONE : EMSTATE_WAIT_SIPI;
    }
}


/**
 * Reset notification.
 *
 * @param   pVM         The cross context VM structure.
 */
VMMR3_INT_DECL(void) EMR3Reset(PVM pVM)
{
    Log(("EMR3Reset: \n"));
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
        EMR3ResetCpu(pVM->apCpusR3[idCpu]);
}


/**
 * Terminates the EM.
 *
 * Termination means cleaning up and freeing all resources,
 * the VM it self is at this point powered off or suspended.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3_INT_DECL(int) EMR3Term(PVM pVM)
{
    RT_NOREF(pVM);
    return VINF_SUCCESS;
}


/**
 * Execute state save operation.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pSSM            SSM operation handle.
 */
static DECLCALLBACK(int) emR3Save(PVM pVM, PSSMHANDLE pSSM)
{
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];

        SSMR3PutBool(pSSM, false /*fForceRAW*/);

        Assert(pVCpu->em.s.enmState     == EMSTATE_SUSPENDED);
        Assert(pVCpu->em.s.enmPrevState != EMSTATE_SUSPENDED);
        SSMR3PutU32(pSSM,
                       pVCpu->em.s.enmPrevState == EMSTATE_NONE
                    || pVCpu->em.s.enmPrevState == EMSTATE_HALTED
                    || pVCpu->em.s.enmPrevState == EMSTATE_WAIT_SIPI
                    ? pVCpu->em.s.enmPrevState : EMSTATE_NONE);

        /* Save mwait state. */
        SSMR3PutU32(pSSM, pVCpu->em.s.MWait.fWait);
        SSMR3PutGCPtr(pSSM, pVCpu->em.s.MWait.uMWaitRAX);
        SSMR3PutGCPtr(pSSM, pVCpu->em.s.MWait.uMWaitRCX);
        SSMR3PutGCPtr(pSSM, pVCpu->em.s.MWait.uMonitorRAX);
        SSMR3PutGCPtr(pSSM, pVCpu->em.s.MWait.uMonitorRCX);
        int rc = SSMR3PutGCPtr(pSSM, pVCpu->em.s.MWait.uMonitorRDX);
        AssertRCReturn(rc, rc);
    }
    return VINF_SUCCESS;
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
static DECLCALLBACK(int) emR3Load(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    /*
     * Validate version.
     */
    if (   uVersion > EM_SAVED_STATE_VERSION
        || uVersion < EM_SAVED_STATE_VERSION_PRE_SMP)
    {
        AssertMsgFailed(("emR3Load: Invalid version uVersion=%d (current %d)!\n", uVersion, EM_SAVED_STATE_VERSION));
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    }
    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);

    /*
     * Load the saved state.
     */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];

        bool fForceRAWIgnored;
        int rc = SSMR3GetBool(pSSM, &fForceRAWIgnored);
        AssertRCReturn(rc, rc);

        if (uVersion > EM_SAVED_STATE_VERSION_PRE_SMP)
        {
            /* We are only intereseted in two enmPrevState values for use when
               EMR3ExecuteVM is called.
               Since ~r157540. only these two and EMSTATE_NONE are saved. */
            SSM_GET_ENUM32_RET(pSSM, pVCpu->em.s.enmPrevState, EMSTATE);
            Assert(pVCpu->em.s.enmPrevState != EMSTATE_SUSPENDED);
            if (   pVCpu->em.s.enmPrevState != EMSTATE_WAIT_SIPI
                && pVCpu->em.s.enmPrevState != EMSTATE_HALTED)
                pVCpu->em.s.enmPrevState = EMSTATE_NONE;

            pVCpu->em.s.enmState = EMSTATE_SUSPENDED;
        }
        if (uVersion > EM_SAVED_STATE_VERSION_PRE_MWAIT)
        {
            /* Load mwait state. */
            rc = SSMR3GetU32(pSSM, &pVCpu->em.s.MWait.fWait);
            AssertRCReturn(rc, rc);
            rc = SSMR3GetGCPtr(pSSM, &pVCpu->em.s.MWait.uMWaitRAX);
            AssertRCReturn(rc, rc);
            rc = SSMR3GetGCPtr(pSSM, &pVCpu->em.s.MWait.uMWaitRCX);
            AssertRCReturn(rc, rc);
            rc = SSMR3GetGCPtr(pSSM, &pVCpu->em.s.MWait.uMonitorRAX);
            AssertRCReturn(rc, rc);
            rc = SSMR3GetGCPtr(pSSM, &pVCpu->em.s.MWait.uMonitorRCX);
            AssertRCReturn(rc, rc);
            rc = SSMR3GetGCPtr(pSSM, &pVCpu->em.s.MWait.uMonitorRDX);
            AssertRCReturn(rc, rc);
        }
    }
    return VINF_SUCCESS;
}


/**
 * Argument packet for emR3SetExecutionPolicy.
 */
struct EMR3SETEXECPOLICYARGS
{
    EMEXECPOLICY    enmPolicy;
    bool            fEnforce;
};


/**
 * @callback_method_impl{FNVMMEMTRENDEZVOUS, Rendezvous callback for EMR3SetExecutionPolicy.}
 */
static DECLCALLBACK(VBOXSTRICTRC) emR3SetExecutionPolicy(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    /*
     * Only the first CPU changes the variables.
     */
    if (pVCpu->idCpu == 0)
    {
        struct EMR3SETEXECPOLICYARGS *pArgs = (struct EMR3SETEXECPOLICYARGS *)pvUser;
        switch (pArgs->enmPolicy)
        {
            case EMEXECPOLICY_IEM_ALL:
                pVM->em.s.fIemExecutesAll = pArgs->fEnforce;

                /* For making '.alliem 1' useful during debugging, transition the
                   EMSTATE_DEBUG_GUEST_XXX to EMSTATE_DEBUG_GUEST_IEM.  */
                for (VMCPUID i = 0; i < pVM->cCpus; i++)
                {
                    PVMCPU pVCpuX = pVM->apCpusR3[i];
                    switch (pVCpuX->em.s.enmState)
                    {
                        case EMSTATE_DEBUG_GUEST_RECOMPILER:
                            if (pVM->em.s.fIemRecompiled)
                                break;
                            RT_FALL_THROUGH();
                        case EMSTATE_DEBUG_GUEST_RAW:
                        case EMSTATE_DEBUG_GUEST_HM:
                        case EMSTATE_DEBUG_GUEST_NEM:
                            Log(("EM: idCpu=%u: %s -> EMSTATE_DEBUG_GUEST_IEM\n", i, emR3GetStateName(pVCpuX->em.s.enmState) ));
                            pVCpuX->em.s.enmState = EMSTATE_DEBUG_GUEST_IEM;
                            break;
                        case EMSTATE_DEBUG_GUEST_IEM:
                        default:
                            break;
                    }
                }
                break;

            case EMEXECPOLICY_IEM_RECOMPILED:
                pVM->em.s.fIemRecompiled = pArgs->fEnforce;
                break;

            default:
                AssertFailedReturn(VERR_INVALID_PARAMETER);
        }
        Log(("EM: Set execution policy: fIemExecutesAll=%RTbool fIemRecompiled=%RTbool\n",
             pVM->em.s.fIemExecutesAll, pVM->em.s.fIemRecompiled));
    }

    /*
     * Force rescheduling if in HM, NEM, IEM/interpreter or IEM/recompiler.
     */
    Assert(pVCpu->em.s.enmState != EMSTATE_RAW_OBSOLETE);
    return    pVCpu->em.s.enmState == EMSTATE_HM
           || pVCpu->em.s.enmState == EMSTATE_NEM
           || pVCpu->em.s.enmState == EMSTATE_IEM
           || pVCpu->em.s.enmState == EMSTATE_RECOMPILER
         ? VINF_EM_RESCHEDULE
         : VINF_SUCCESS;
}


/**
 * Changes an execution scheduling policy parameter.
 *
 * This is used to enable or disable raw-mode / hardware-virtualization
 * execution of user and supervisor code.
 *
 * @returns VINF_SUCCESS on success.
 * @returns VINF_RESCHEDULE if a rescheduling might be required.
 * @returns VERR_INVALID_PARAMETER on an invalid enmMode value.
 *
 * @param   pUVM            The user mode VM handle.
 * @param   enmPolicy       The scheduling policy to change.
 * @param   fEnforce        Whether to enforce the policy or not.
 */
VMMR3DECL(int) EMR3SetExecutionPolicy(PUVM pUVM, EMEXECPOLICY enmPolicy, bool fEnforce)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    VM_ASSERT_VALID_EXT_RETURN(pUVM->pVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(enmPolicy > EMEXECPOLICY_INVALID && enmPolicy < EMEXECPOLICY_END, VERR_INVALID_PARAMETER);

    struct EMR3SETEXECPOLICYARGS Args = { enmPolicy, fEnforce };
    return VMMR3EmtRendezvous(pUVM->pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_DESCENDING, emR3SetExecutionPolicy, &Args);
}


/**
 * Queries an execution scheduling policy parameter.
 *
 * @returns VBox status code
 * @param   pUVM            The user mode VM handle.
 * @param   enmPolicy       The scheduling policy to query.
 * @param   pfEnforced      Where to return the current value.
 */
VMMR3DECL(int) EMR3QueryExecutionPolicy(PUVM pUVM, EMEXECPOLICY enmPolicy, bool *pfEnforced)
{
    AssertReturn(enmPolicy > EMEXECPOLICY_INVALID && enmPolicy < EMEXECPOLICY_END, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pfEnforced, VERR_INVALID_POINTER);
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    /* No need to bother EMTs with a query. */
    switch (enmPolicy)
    {
        case EMEXECPOLICY_IEM_ALL:
            *pfEnforced = pVM->em.s.fIemExecutesAll;
            break;
        case EMEXECPOLICY_IEM_RECOMPILED:
            *pfEnforced = pVM->em.s.fIemRecompiled;
            break;
        default:
            AssertFailedReturn(VERR_INTERNAL_ERROR_2);
    }

    return VINF_SUCCESS;
}


/**
 * Queries the main execution engine of the VM.
 *
 * @returns VBox status code
 * @param   pUVM                    The user mode VM handle.
 * @param   pbMainExecutionEngine   Where to return the result, VM_EXEC_ENGINE_XXX.
 */
VMMR3DECL(int) EMR3QueryMainExecutionEngine(PUVM pUVM, uint8_t *pbMainExecutionEngine)
{
    AssertPtrReturn(pbMainExecutionEngine, VERR_INVALID_POINTER);
    *pbMainExecutionEngine = VM_EXEC_ENGINE_NOT_SET;

    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    *pbMainExecutionEngine = pVM->bMainExecutionEngine;
    return VINF_SUCCESS;
}


/**
 * Raise a fatal error.
 *
 * Safely terminate the VM with full state report and stuff. This function
 * will naturally never return.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   rc          VBox status code.
 */
VMMR3DECL(void) EMR3FatalError(PVMCPU pVCpu, int rc)
{
    pVCpu->em.s.enmState = EMSTATE_GURU_MEDITATION;
    longjmp(pVCpu->em.s.u.FatalLongJump, rc);
}


#if defined(LOG_ENABLED) || defined(VBOX_STRICT)
/**
 * Gets the EM state name.
 *
 * @returns pointer to read only state name,
 * @param   enmState    The state.
 */
static const char *emR3GetStateName(EMSTATE enmState)
{
    switch (enmState)
    {
        case EMSTATE_NONE:                      return "EMSTATE_NONE";
        case EMSTATE_RAW_OBSOLETE:              return "EMSTATE_RAW_OBSOLETE";
        case EMSTATE_HM:                        return "EMSTATE_HM";
        case EMSTATE_IEM:                       return "EMSTATE_IEM";
        case EMSTATE_RECOMPILER:                return "EMSTATE_RECOMPILER";
        case EMSTATE_HALTED:                    return "EMSTATE_HALTED";
        case EMSTATE_WAIT_SIPI:                 return "EMSTATE_WAIT_SIPI";
        case EMSTATE_SUSPENDED:                 return "EMSTATE_SUSPENDED";
        case EMSTATE_TERMINATING:               return "EMSTATE_TERMINATING";
        case EMSTATE_DEBUG_GUEST_RAW:           return "EMSTATE_DEBUG_GUEST_RAW";
        case EMSTATE_DEBUG_GUEST_HM:            return "EMSTATE_DEBUG_GUEST_HM";
        case EMSTATE_DEBUG_GUEST_IEM:           return "EMSTATE_DEBUG_GUEST_IEM";
        case EMSTATE_DEBUG_GUEST_RECOMPILER:    return "EMSTATE_DEBUG_GUEST_RECOMPILER";
        case EMSTATE_DEBUG_HYPER:               return "EMSTATE_DEBUG_HYPER";
        case EMSTATE_GURU_MEDITATION:           return "EMSTATE_GURU_MEDITATION";
        case EMSTATE_IEM_THEN_REM_OBSOLETE:     return "EMSTATE_IEM_THEN_REM_OBSOLETE";
        case EMSTATE_NEM:                       return "EMSTATE_NEM";
        case EMSTATE_DEBUG_GUEST_NEM:           return "EMSTATE_DEBUG_GUEST_NEM";
        default:                                return "Unknown!";
    }
}
#endif /* LOG_ENABLED || VBOX_STRICT */

#if !defined(VBOX_VMM_TARGET_ARMV8)

/**
 * Handle pending ring-3 I/O port write.
 *
 * This is in response to a VINF_EM_PENDING_R3_IOPORT_WRITE status code returned
 * by EMRZSetPendingIoPortWrite() in ring-0 or raw-mode context.
 *
 * @returns Strict VBox status code.
 * @param   pVM     The cross context VM structure.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
VBOXSTRICTRC emR3ExecutePendingIoPortWrite(PVM pVM, PVMCPU pVCpu)
{
    CPUM_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_RFLAGS);

    /* Get and clear the pending data. */
    RTIOPORT const uPort   = pVCpu->em.s.PendingIoPortAccess.uPort;
    uint32_t const uValue  = pVCpu->em.s.PendingIoPortAccess.uValue;
    uint8_t  const cbValue = pVCpu->em.s.PendingIoPortAccess.cbValue;
    uint8_t  const cbInstr = pVCpu->em.s.PendingIoPortAccess.cbInstr;
    pVCpu->em.s.PendingIoPortAccess.cbValue = 0;

    /* Assert sanity. */
    switch (cbValue)
    {
        case 1:     Assert(!(cbValue & UINT32_C(0xffffff00))); break;
        case 2:     Assert(!(cbValue & UINT32_C(0xffff0000))); break;
        case 4:     break;
        default:    AssertMsgFailedReturn(("cbValue=%#x\n", cbValue), VERR_EM_INTERNAL_ERROR);
    }
    AssertReturn(cbInstr <= 15 && cbInstr >= 1, VERR_EM_INTERNAL_ERROR);

    /* Do the work.*/
    VBOXSTRICTRC rcStrict = IOMIOPortWrite(pVM, pVCpu, uPort, uValue, cbValue);
    LogFlow(("EM/OUT: %#x, %#x LB %u -> %Rrc\n", uPort, uValue, cbValue, VBOXSTRICTRC_VAL(rcStrict) ));
    if (IOM_SUCCESS(rcStrict))
    {
        pVCpu->cpum.GstCtx.rip += cbInstr;
        pVCpu->cpum.GstCtx.rflags.Bits.u1RF = 0;
    }
    return rcStrict;
}


/**
 * Handle pending ring-3 I/O port write.
 *
 * This is in response to a VINF_EM_PENDING_R3_IOPORT_WRITE status code returned
 * by EMRZSetPendingIoPortRead() in ring-0 or raw-mode context.
 *
 * @returns Strict VBox status code.
 * @param   pVM     The cross context VM structure.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
VBOXSTRICTRC emR3ExecutePendingIoPortRead(PVM pVM, PVMCPU pVCpu)
{
    CPUM_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_RFLAGS | CPUMCTX_EXTRN_RAX);

    /* Get and clear the pending data. */
    RTIOPORT const uPort   = pVCpu->em.s.PendingIoPortAccess.uPort;
    uint8_t  const cbValue = pVCpu->em.s.PendingIoPortAccess.cbValue;
    uint8_t  const cbInstr = pVCpu->em.s.PendingIoPortAccess.cbInstr;
    pVCpu->em.s.PendingIoPortAccess.cbValue = 0;

    /* Assert sanity. */
    switch (cbValue)
    {
        case 1:     break;
        case 2:     break;
        case 4:     break;
        default:    AssertMsgFailedReturn(("cbValue=%#x\n", cbValue), VERR_EM_INTERNAL_ERROR);
    }
    AssertReturn(pVCpu->em.s.PendingIoPortAccess.uValue == UINT32_C(0x52454144) /* READ*/, VERR_EM_INTERNAL_ERROR);
    AssertReturn(cbInstr <= 15 && cbInstr >= 1, VERR_EM_INTERNAL_ERROR);

    /* Do the work.*/
    uint32_t uValue = 0;
    VBOXSTRICTRC rcStrict = IOMIOPortRead(pVM, pVCpu, uPort, &uValue, cbValue);
    LogFlow(("EM/IN: %#x LB %u -> %Rrc, %#x\n", uPort, cbValue, VBOXSTRICTRC_VAL(rcStrict), uValue ));
    if (IOM_SUCCESS(rcStrict))
    {
        if (cbValue == 4)
            pVCpu->cpum.GstCtx.rax = uValue;
        else if (cbValue == 2)
            pVCpu->cpum.GstCtx.ax = (uint16_t)uValue;
        else
            pVCpu->cpum.GstCtx.al = (uint8_t)uValue;
        pVCpu->cpum.GstCtx.rip += cbInstr;
        pVCpu->cpum.GstCtx.rflags.Bits.u1RF = 0;
    }
    return rcStrict;
}


/**
 * @callback_method_impl{FNVMMEMTRENDEZVOUS,
 * Worker for emR3ExecuteSplitLockInstruction}
 */
static DECLCALLBACK(VBOXSTRICTRC) emR3ExecuteSplitLockInstructionRendezvous(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    /* Only execute on the specified EMT. */
    if (pVCpu == (PVMCPU)pvUser)
    {
        LogFunc(("\n"));
        VBOXSTRICTRC rcStrict = IEMExecOneIgnoreLock(pVCpu);
        LogFunc(("rcStrict=%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
        if (rcStrict == VINF_IEM_RAISED_XCPT)
            rcStrict = VINF_SUCCESS;
        return rcStrict;
    }
    RT_NOREF(pVM);
    return VINF_SUCCESS;
}


/**
 * Handle an instruction causing a split cacheline lock access in SMP VMs.
 *
 * Generally we only get here if the host has split-lock detection enabled and
 * this caused an \#AC because of something the guest did.  If we interpret the
 * instruction as-is, we'll likely just repeat the split-lock access and
 * possibly be killed, get a SIGBUS, or trigger a warning followed by extra MSR
 * changes on context switching (costs a tiny bit).  Assuming these \#ACs are
 * rare to non-existing, we'll do a rendezvous of all EMTs and tell IEM to
 * disregard the lock prefix when emulating the instruction.
 *
 * Yes, we could probably modify the MSR (or MSRs) controlling the detection
 * feature when entering guest context, but the support for the feature isn't a
 * 100% given and we'll need the debug-only supdrvOSMsrProberRead and
 * supdrvOSMsrProberWrite functionality from SUPDrv.cpp to safely detect it.
 * Thus the approach is to just deal with the spurious \#ACs first and maybe add
 * propert detection to SUPDrv later if we find it necessary.
 *
 * @see     @bugref{10052}
 *
 * @returns Strict VBox status code.
 * @param   pVM     The cross context VM structure.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
VBOXSTRICTRC emR3ExecuteSplitLockInstruction(PVM pVM, PVMCPU pVCpu)
{
    LogFunc(("\n"));
    return VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ALL_AT_ONCE, emR3ExecuteSplitLockInstructionRendezvous, pVCpu);
}

#endif /* VBOX_VMM_TARGET_ARMV8 */

/**
 * Debug loop.
 *
 * @returns VBox status code for EM.
 * @param   pVM     The cross context VM structure.
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   rc      Current EM VBox status code.
 */
static VBOXSTRICTRC emR3Debug(PVM pVM, PVMCPU pVCpu, VBOXSTRICTRC rc)
{
    for (;;)
    {
        Log(("emR3Debug: rc=%Rrc\n", VBOXSTRICTRC_VAL(rc)));
        const VBOXSTRICTRC rcLast = rc;

        /*
         * Debug related RC.
         */
        switch (VBOXSTRICTRC_VAL(rc))
        {
            /*
             * Single step an instruction.
             */
            case VINF_EM_DBG_STEP:
                if (   pVCpu->em.s.enmState == EMSTATE_DEBUG_GUEST_RAW
                    || pVCpu->em.s.enmState == EMSTATE_DEBUG_HYPER)
                    AssertLogRelMsgFailedStmt(("Bad EM state."), rc = VERR_EM_INTERNAL_ERROR);
#if !defined(VBOX_VMM_TARGET_ARMV8)
                else if (pVCpu->em.s.enmState == EMSTATE_DEBUG_GUEST_HM)
                    rc = EMR3HmSingleInstruction(pVM, pVCpu, 0 /*fFlags*/);
#endif
                else if (pVCpu->em.s.enmState == EMSTATE_DEBUG_GUEST_NEM)
                    rc = VBOXSTRICTRC_TODO(emR3NemSingleInstruction(pVM, pVCpu, 0 /*fFlags*/));
                else
                {
                    rc = IEMExecOne(pVCpu); /** @todo add dedicated interface... */
                    if (rc == VINF_SUCCESS || rc == VINF_EM_RESCHEDULE)
                        rc = VINF_EM_DBG_STEPPED;
                }
#ifndef VBOX_VMM_TARGET_ARMV8
                if (rc != VINF_EM_EMULATE_SPLIT_LOCK)
                { /* likely */ }
                else
                {
                    rc = emR3ExecuteSplitLockInstruction(pVM, pVCpu);
                    if (rc == VINF_SUCCESS || rc == VINF_EM_RESCHEDULE)
                        rc = VINF_EM_DBG_STEPPED;
                }
#endif
                break;

            /*
             * Simple events: stepped, breakpoint, stop/assertion.
             */
            case VINF_EM_DBG_STEPPED:
                rc = DBGFR3Event(pVM, DBGFEVENT_STEPPED);
                break;

            case VINF_EM_DBG_BREAKPOINT:
                rc = DBGFR3BpHit(pVM, pVCpu);
                break;

            case VINF_EM_DBG_STOP:
                rc = DBGFR3EventSrc(pVM, DBGFEVENT_DEV_STOP, NULL, 0, NULL, NULL);
                break;

            case VINF_EM_DBG_EVENT:
                rc = DBGFR3EventHandlePending(pVM, pVCpu);
                break;

            case VINF_EM_DBG_HYPER_STEPPED:
                rc = DBGFR3Event(pVM, DBGFEVENT_STEPPED_HYPER);
                break;

            case VINF_EM_DBG_HYPER_BREAKPOINT:
                rc = DBGFR3EventBreakpoint(pVM, DBGFEVENT_BREAKPOINT_HYPER);
                break;

            case VINF_EM_DBG_HYPER_ASSERTION:
                RTPrintf("\nVINF_EM_DBG_HYPER_ASSERTION:\n%s%s\n", VMMR3GetRZAssertMsg1(pVM), VMMR3GetRZAssertMsg2(pVM));
                RTLogFlush(NULL);
                rc = DBGFR3EventAssertion(pVM, DBGFEVENT_ASSERTION_HYPER, VMMR3GetRZAssertMsg1(pVM), VMMR3GetRZAssertMsg2(pVM));
                break;

            /*
             * Guru meditation.
             */
            case VERR_VMM_RING0_ASSERTION: /** @todo Make a guru meditation event! */
                rc = DBGFR3EventSrc(pVM, DBGFEVENT_FATAL_ERROR, "VERR_VMM_RING0_ASSERTION", 0, NULL, NULL);
                break;
            case VINF_EM_TRIPLE_FAULT:    /** @todo Make a guru meditation event! */
                rc = DBGFR3EventSrc(pVM, DBGFEVENT_DEV_STOP, "VINF_EM_TRIPLE_FAULT", 0, NULL, NULL);
                break;

            default: /** @todo don't use default for guru, but make special errors code! */
            {
                LogRel(("emR3Debug: rc=%Rrc\n", VBOXSTRICTRC_VAL(rc)));
                rc = DBGFR3Event(pVM, DBGFEVENT_FATAL_ERROR);
                break;
            }
        }

        /*
         * Process the result.
         */
        switch (VBOXSTRICTRC_VAL(rc))
        {
            /*
             * Continue the debugging loop.
             */
            case VINF_EM_DBG_STEP:
            case VINF_EM_DBG_STOP:
            case VINF_EM_DBG_EVENT:
            case VINF_EM_DBG_STEPPED:
            case VINF_EM_DBG_BREAKPOINT:
            case VINF_EM_DBG_HYPER_STEPPED:
            case VINF_EM_DBG_HYPER_BREAKPOINT:
            case VINF_EM_DBG_HYPER_ASSERTION:
                break;

            /*
             * Resuming execution (in some form) has to be done here if we got
             * a hypervisor debug event.
             */
            case VINF_SUCCESS:
            case VINF_EM_RESUME:
            case VINF_EM_SUSPEND:
            case VINF_EM_RESCHEDULE:
            case VINF_EM_RESCHEDULE_REM:
            case VINF_EM_HALT:
                if (pVCpu->em.s.enmState == EMSTATE_DEBUG_HYPER)
                    AssertLogRelMsgFailedReturn(("Not implemented\n"), VERR_EM_INTERNAL_ERROR);
                if (rc == VINF_SUCCESS)
                    rc = VINF_EM_RESCHEDULE;
                return rc;

            /*
             * The debugger isn't attached.
             * We'll simply turn the thing off since that's the easiest thing to do.
             */
            case VERR_DBGF_NOT_ATTACHED:
                switch (VBOXSTRICTRC_VAL(rcLast))
                {
                    case VINF_EM_DBG_HYPER_STEPPED:
                    case VINF_EM_DBG_HYPER_BREAKPOINT:
                    case VINF_EM_DBG_HYPER_ASSERTION:
                    case VERR_TRPM_PANIC:
                    case VERR_TRPM_DONT_PANIC:
                    case VERR_VMM_RING0_ASSERTION:
                    case VERR_VMM_HYPER_CR3_MISMATCH:
                    case VERR_VMM_RING3_CALL_DISABLED:
                        return rcLast;
                }
                return VINF_EM_OFF;

            /*
             * Status codes terminating the VM in one or another sense.
             */
            case VINF_EM_TERMINATE:
            case VINF_EM_OFF:
            case VINF_EM_RESET:
            case VINF_EM_NO_MEMORY:
            case VINF_EM_RAW_STALE_SELECTOR:
            case VINF_EM_RAW_IRET_TRAP:
            case VERR_TRPM_PANIC:
            case VERR_TRPM_DONT_PANIC:
            case VERR_IEM_INSTR_NOT_IMPLEMENTED:
            case VERR_IEM_ASPECT_NOT_IMPLEMENTED:
            case VERR_VMM_RING0_ASSERTION:
            case VERR_VMM_HYPER_CR3_MISMATCH:
            case VERR_VMM_RING3_CALL_DISABLED:
            case VERR_INTERNAL_ERROR:
            case VERR_INTERNAL_ERROR_2:
            case VERR_INTERNAL_ERROR_3:
            case VERR_INTERNAL_ERROR_4:
            case VERR_INTERNAL_ERROR_5:
            case VERR_IPE_UNEXPECTED_STATUS:
            case VERR_IPE_UNEXPECTED_INFO_STATUS:
            case VERR_IPE_UNEXPECTED_ERROR_STATUS:
                return rc;

            /*
             * The rest is unexpected, and will keep us here.
             */
            default:
                AssertMsgFailed(("Unexpected rc %Rrc!\n", VBOXSTRICTRC_VAL(rc)));
                break;
        }
    } /* debug for ever */
}


/**
 * Executes recompiled code.
 *
 * This function contains the recompiler version of the inner
 * execution loop (the outer loop being in EMR3ExecuteVM()).
 *
 * @returns VBox status code. The most important ones are: VINF_EM_RESCHEDULE,
 *          VINF_EM_SUSPEND, VINF_EM_RESET and VINF_EM_TERMINATE.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   fWasHalted  Set if we're comming out of a CPU HALT state.
 * @param   pfFFDone    Where to store an indicator telling whether or not
 *                      FFs were done before returning.
 *
 */
static VBOXSTRICTRC emR3RecompilerExecute(PVM pVM, PVMCPU pVCpu, bool fWasHalted, bool *pfFFDone)
{
    STAM_REL_PROFILE_START(&pVCpu->em.s.StatREMTotal, a);
#ifdef VBOX_VMM_TARGET_ARMV8
    LogFlow(("emR3RecompilerExecute/%u: (pc=%RGv)\n", pVCpu->idCpu, (RTGCPTR)pVCpu->cpum.GstCtx.Pc.u64));
#else
    LogFlow(("emR3RecompilerExecute/%u: (cs:eip=%04x:%RGv)\n", pVCpu->idCpu, pVCpu->cpum.GstCtx.cs.Sel, (RTGCPTR)pVCpu->cpum.GstCtx.rip));
#endif

    /*
     * Loop till we get a forced action which returns anything but VINF_SUCCESS.
     */
    *pfFFDone = false;
    VBOXSTRICTRC rcStrict = VINF_SUCCESS;
    for (;;)
    {
#ifdef LOG_ENABLED
# if defined(VBOX_VMM_TARGET_ARMV8)
        Log3(("EM: pc=%08x\n", CPUMGetGuestFlatPC(pVCpu)));
# else
        if (!pVCpu->cpum.GstCtx.eflags.Bits.u1VM)
            Log(("EMR%d: %04X:%08RX64 RSP=%08RX64 IF=%d CR0=%x eflags=%x\n", CPUMGetGuestCPL(pVCpu), pVCpu->cpum.GstCtx.cs.Sel,
                 pVCpu->cpum.GstCtx.rip, pVCpu->cpum.GstCtx.rsp, pVCpu->cpum.GstCtx.eflags.Bits.u1IF,
                 (uint32_t)pVCpu->cpum.GstCtx.cr0, pVCpu->cpum.GstCtx.eflags.u));
        else
            Log(("EMV86: %04X:%08X IF=%d\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.eip, pVCpu->cpum.GstCtx.eflags.Bits.u1IF));
# endif
#endif

        /*
         * Execute.
         */
        if (RT_LIKELY(emR3IsExecutionAllowed(pVM, pVCpu)))
        {
            STAM_PROFILE_START(&pVCpu->em.s.StatREMExec, c);
#ifdef VBOX_WITH_IEM_RECOMPILER
            if (pVM->em.s.fIemRecompiled)
                rcStrict = IEMExecRecompiler(pVM, pVCpu, fWasHalted);
            else
#endif
                rcStrict = IEMExecLots(pVCpu, 8192 /*cMaxInstructions*/, 4095 /*cPollRate*/, NULL /*pcInstructions*/);
            STAM_PROFILE_STOP(&pVCpu->em.s.StatREMExec, c);
        }
        else
        {
            /* Give up this time slice; virtual time continues */
            STAM_REL_PROFILE_ADV_START(&pVCpu->em.s.StatCapped, u);
            RTThreadSleep(5);
            STAM_REL_PROFILE_ADV_STOP(&pVCpu->em.s.StatCapped, u);
            rcStrict = VINF_SUCCESS;
        }

        /*
         * Deal with high priority post execution FFs before doing anything
         * else.  Sync back the state and leave the lock to be on the safe side.
         */
        if (    VM_FF_IS_ANY_SET(pVM, VM_FF_HIGH_PRIORITY_POST_MASK)
            ||  VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_HIGH_PRIORITY_POST_MASK))
            rcStrict = emR3HighPriorityPostForcedActions(pVM, pVCpu, rcStrict);

        /*
         * Process the returned status code.
         */
        if (rcStrict != VINF_SUCCESS)
        {
#ifndef VBOX_VMM_TARGET_ARMV8
            if (rcStrict == VINF_EM_EMULATE_SPLIT_LOCK)
                rcStrict = emR3ExecuteSplitLockInstruction(pVM, pVCpu);
#endif
            if (rcStrict != VINF_SUCCESS)
            {
#if 0
                if (RT_LIKELY(rcStrict >= VINF_EM_FIRST && rcStrict <= VINF_EM_LAST))
                    break;
                /* Fatal error: */
#endif
                break;
            }
        }


        /*
         * Check and execute forced actions.
         *
         * Sync back the VM state and leave the lock  before calling any of
         * these, you never know what's going to happen here.
         */
#ifdef VBOX_HIGH_RES_TIMERS_HACK
        TMTimerPollVoid(pVM, pVCpu);
#endif
        AssertCompile(VMCPU_FF_ALL_REM_MASK & VMCPU_FF_TIMER);
        if (   VM_FF_IS_ANY_SET(pVM, VM_FF_ALL_REM_MASK)
            || VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_ALL_REM_MASK) )
        {
            rcStrict = emR3ForcedActions(pVM, pVCpu, VBOXSTRICTRC_TODO(rcStrict));
            VBOXVMM_EM_FF_ALL_RET(pVCpu, VBOXSTRICTRC_VAL(rcStrict));
            if (   rcStrict != VINF_SUCCESS
                && rcStrict != VINF_EM_RESCHEDULE_REM)
            {
                *pfFFDone = true;
                break;
            }
        }

        /*
         * Check if we can switch back to the main execution engine now.
         */
#if !defined(VBOX_VMM_TARGET_ARMV8)
        if (VM_IS_HM_ENABLED(pVM))
        {
            if (HMCanExecuteGuest(pVM, pVCpu, &pVCpu->cpum.GstCtx))
            {
                *pfFFDone = true;
                rcStrict  = VINF_EM_RESCHEDULE_EXEC_ENGINE;
                break;
            }
        }
        else
#endif
        if (VM_IS_NEM_ENABLED(pVM))
        {
            if (NEMR3CanExecuteGuest(pVM, pVCpu))
            {
                *pfFFDone = true;
                rcStrict  = VINF_EM_RESCHEDULE_EXEC_ENGINE;
                break;
            }
        }

#ifdef VBOX_WITH_IEM_RECOMPILER
        fWasHalted = false;
#else
        RT_NOREF(fWasHalted);
#endif
    } /* The Inner Loop, recompiled execution mode version. */

    STAM_REL_PROFILE_STOP(&pVCpu->em.s.StatREMTotal, a);
    return rcStrict;
}


/**
 * Decides whether to execute HM, NEM, IEM/interpreter or IEM/recompiler.
 *
 * @returns new EM state
 * @param   pVM     The cross context VM structure.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
EMSTATE emR3Reschedule(PVM pVM, PVMCPU pVCpu)
{
    /*
     * We stay in the wait for SIPI state unless explicitly told otherwise.
     */
    if (pVCpu->em.s.enmState == EMSTATE_WAIT_SIPI)
        return EMSTATE_WAIT_SIPI;

    /*
     * Execute everything in IEM?
     */
    if (   pVM->em.s.fIemExecutesAll
        || VM_IS_EXEC_ENGINE_IEM(pVM))
#ifdef VBOX_WITH_IEM_RECOMPILER
        return pVM->em.s.fIemRecompiled ? EMSTATE_RECOMPILER : EMSTATE_IEM;
#else
        return EMSTATE_IEM;
#endif

#if !defined(VBOX_VMM_TARGET_ARMV8)
    if (VM_IS_HM_ENABLED(pVM))
    {
        if (HMCanExecuteGuest(pVM, pVCpu, &pVCpu->cpum.GstCtx))
            return EMSTATE_HM;
    }
    else
#endif
    if (NEMR3CanExecuteGuest(pVM, pVCpu))
        return EMSTATE_NEM;

    /*
     * Note! Raw mode and hw accelerated mode are incompatible. The latter
     *       turns off monitoring features essential for raw mode!
     */
#ifdef VBOX_WITH_IEM_RECOMPILER
    return pVM->em.s.fIemRecompiled ? EMSTATE_RECOMPILER : EMSTATE_IEM;
#else
    return EMSTATE_IEM;
#endif
}


/**
 * Executes all high priority post execution force actions.
 *
 * @returns Strict VBox status code.  Typically @a rc, but may be upgraded to
 *          fatal error status code.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   rc          The current strict VBox status code rc.
 */
VBOXSTRICTRC emR3HighPriorityPostForcedActions(PVM pVM, PVMCPU pVCpu, VBOXSTRICTRC rc)
{
    VBOXVMM_EM_FF_HIGH(pVCpu, pVM->fGlobalForcedActions, pVCpu->fLocalForcedActions, VBOXSTRICTRC_VAL(rc));

    if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_PDM_CRITSECT))
        PDMCritSectBothFF(pVM, pVCpu);

#if !defined(VBOX_VMM_TARGET_ARMV8)
    /* Update CR3 (Nested Paging case for HM). */
    if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_HM_UPDATE_CR3))
    {
        CPUM_IMPORT_EXTRN_RCSTRICT(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_CR3 | CPUMCTX_EXTRN_CR4 | CPUMCTX_EXTRN_EFER, rc);
        int const rc2 = PGMUpdateCR3(pVCpu, CPUMGetGuestCR3(pVCpu));
        if (RT_FAILURE(rc2))
            return rc2;
        Assert(!VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_HM_UPDATE_CR3));
    }
#endif

    /* IEM has pending work (typically memory write after INS instruction). */
    if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_IEM))
        rc = IEMR3ProcessForceFlag(pVM, pVCpu, rc);

    /* IOM has pending work (comitting an I/O or MMIO write). */
    if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_IOM))
    {
        rc = IOMR3ProcessForceFlag(pVM, pVCpu, rc);
        if (pVCpu->em.s.idxContinueExitRec >= RT_ELEMENTS(pVCpu->em.s.aExitRecords))
        { /* half likely, or at least it's a line shorter. */ }
        else if (rc == VINF_SUCCESS)
            rc = VINF_EM_RESUME_R3_HISTORY_EXEC;
        else
            pVCpu->em.s.idxContinueExitRec = UINT16_MAX;
    }

    if (VM_FF_IS_SET(pVM, VM_FF_PGM_NO_MEMORY))
    {
        if (    rc > VINF_EM_NO_MEMORY
            &&  rc <= VINF_EM_LAST)
            rc = VINF_EM_NO_MEMORY;
    }

    return rc;
}


#if !defined(VBOX_VMM_TARGET_ARMV8)
/**
 * Helper for emR3ForcedActions() for VMX external interrupt VM-exit.
 *
 * @returns VBox status code.
 * @retval  VINF_NO_CHANGE if the VMX external interrupt intercept was not active.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
static int emR3VmxNstGstIntrIntercept(PVMCPU pVCpu)
{
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    /* Handle the "external interrupt" VM-exit intercept. */
    if (    CPUMIsGuestVmxPinCtlsSet(&pVCpu->cpum.GstCtx, VMX_PIN_CTLS_EXT_INT_EXIT)
        && !CPUMIsGuestVmxExitCtlsSet(&pVCpu->cpum.GstCtx, VMX_EXIT_CTLS_ACK_EXT_INT))
    {
        VBOXSTRICTRC rcStrict = IEMExecVmxVmexitExtInt(pVCpu, 0 /* uVector */, true /* fIntPending */);
        AssertMsg(   rcStrict != VINF_VMX_VMEXIT    /* VM-exit should have been converted to VINF_SUCCESS. */
                  && rcStrict != VINF_NO_CHANGE
                  && rcStrict != VINF_VMX_INTERCEPT_NOT_ACTIVE, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
        return VBOXSTRICTRC_VAL(rcStrict);
    }
#else
    RT_NOREF(pVCpu);
#endif
    return VINF_NO_CHANGE;
}


/**
 * Helper for emR3ForcedActions() for SVM interrupt intercept.
 *
 * @returns VBox status code.
 * @retval  VINF_NO_CHANGE if the SVM external interrupt intercept was not active.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
static int emR3SvmNstGstIntrIntercept(PVMCPU pVCpu)
{
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
    /* Handle the physical interrupt intercept (can be masked by the nested hypervisor). */
    if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, &pVCpu->cpum.GstCtx, SVM_CTRL_INTERCEPT_INTR))
    {
        CPUM_ASSERT_NOT_EXTRN(pVCpu, IEM_CPUMCTX_EXTRN_SVM_VMEXIT_MASK);
        VBOXSTRICTRC rcStrict = IEMExecSvmVmexit(pVCpu, SVM_EXIT_INTR, 0, 0);
        if (RT_SUCCESS(rcStrict))
        {
            AssertMsg(   rcStrict != VINF_SVM_VMEXIT
                      && rcStrict != VINF_NO_CHANGE, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
            return VBOXSTRICTRC_VAL(rcStrict);
        }

        AssertMsgFailed(("INTR #VMEXIT failed! rc=%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
        return VINF_EM_TRIPLE_FAULT;
    }
#else
    NOREF(pVCpu);
#endif
    return VINF_NO_CHANGE;
}


/**
 * Helper for emR3ForcedActions() for SVM virtual interrupt intercept.
 *
 * @returns VBox status code.
 * @retval  VINF_NO_CHANGE if the SVM virtual interrupt intercept was not active.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
static int emR3SvmNstGstVirtIntrIntercept(PVMCPU pVCpu)
{
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
    if (CPUMIsGuestSvmCtrlInterceptSet(pVCpu, &pVCpu->cpum.GstCtx, SVM_CTRL_INTERCEPT_VINTR))
    {
        CPUM_ASSERT_NOT_EXTRN(pVCpu, IEM_CPUMCTX_EXTRN_SVM_VMEXIT_MASK);
        VBOXSTRICTRC rcStrict = IEMExecSvmVmexit(pVCpu, SVM_EXIT_VINTR, 0, 0);
        if (RT_SUCCESS(rcStrict))
        {
            Assert(rcStrict != VINF_SVM_VMEXIT);
            return VBOXSTRICTRC_VAL(rcStrict);
        }
        AssertMsgFailed(("VINTR #VMEXIT failed! rc=%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
        return VINF_EM_TRIPLE_FAULT;
    }
#else
    NOREF(pVCpu);
#endif
    return VINF_NO_CHANGE;
}
#endif


/**
 * Executes all pending forced actions.
 *
 * Forced actions can cause execution delays and execution
 * rescheduling. The first we deal with using action priority, so
 * that for instance pending timers aren't scheduled and ran until
 * right before execution. The rescheduling we deal with using
 * return codes. The same goes for VM termination, only in that case
 * we exit everything.
 *
 * @returns VBox status code of equal or greater importance/severity than rc.
 *          The most important ones are: VINF_EM_RESCHEDULE,
 *          VINF_EM_SUSPEND, VINF_EM_RESET and VINF_EM_TERMINATE.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   rc          The current rc.
 *
 */
int emR3ForcedActions(PVM pVM, PVMCPU pVCpu, int rc)
{
    STAM_REL_PROFILE_START(&pVCpu->em.s.StatForcedActions, a);
#ifdef VBOX_STRICT
    int rcIrq = VINF_SUCCESS;
#endif
    int rc2;
#define UPDATE_RC() \
        do { \
            AssertMsg(rc2 <= 0 || (rc2 >= VINF_EM_FIRST && rc2 <= VINF_EM_LAST), ("Invalid FF return code: %Rra\n", rc2)); \
            if (rc2 == VINF_SUCCESS || rc < VINF_SUCCESS) \
                break; \
            if (!rc || rc2 < rc) \
                rc = rc2; \
        } while (0)
    VBOXVMM_EM_FF_ALL(pVCpu, pVM->fGlobalForcedActions, pVCpu->fLocalForcedActions, rc);

    /*
     * Post execution chunk first.
     */
    if (    VM_FF_IS_ANY_SET(pVM, VM_FF_NORMAL_PRIORITY_POST_MASK)
        ||  (VMCPU_FF_NORMAL_PRIORITY_POST_MASK && VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_NORMAL_PRIORITY_POST_MASK)) )
    {
        /*
         * EMT Rendezvous (must be serviced before termination).
         */
        if (VM_FF_IS_SET(pVM, VM_FF_EMT_RENDEZVOUS))
        {
            CPUM_IMPORT_EXTRN_RCSTRICT(pVCpu, ~CPUMCTX_EXTRN_KEEPER_MASK, rc);
            rc2 = VMMR3EmtRendezvousFF(pVM, pVCpu);
            UPDATE_RC();
            /** @todo HACK ALERT! The following test is to make sure EM+TM
             * thinks the VM is stopped/reset before the next VM state change
             * is made. We need a better solution for this, or at least make it
             * possible to do: (rc >= VINF_EM_FIRST && rc <=
             * VINF_EM_SUSPEND). */
            if (RT_UNLIKELY(rc == VINF_EM_SUSPEND || rc == VINF_EM_RESET || rc == VINF_EM_OFF))
            {
                Log2(("emR3ForcedActions: returns %Rrc\n", rc));
                STAM_REL_PROFILE_STOP(&pVCpu->em.s.StatForcedActions, a);
                return rc;
            }
        }

        /*
         * State change request (cleared by vmR3SetStateLocked).
         */
        if (VM_FF_IS_SET(pVM, VM_FF_CHECK_VM_STATE))
        {
            VMSTATE enmState = VMR3GetState(pVM);
            switch (enmState)
            {
                case VMSTATE_FATAL_ERROR:
                case VMSTATE_FATAL_ERROR_LS:
                case VMSTATE_GURU_MEDITATION:
                case VMSTATE_GURU_MEDITATION_LS:
                    Log2(("emR3ForcedActions: %s -> VINF_EM_SUSPEND\n", VMGetStateName(enmState) ));
                    STAM_REL_PROFILE_STOP(&pVCpu->em.s.StatForcedActions, a);
                    return VINF_EM_SUSPEND;

                case VMSTATE_DESTROYING:
                    Log2(("emR3ForcedActions: %s -> VINF_EM_TERMINATE\n", VMGetStateName(enmState) ));
                    STAM_REL_PROFILE_STOP(&pVCpu->em.s.StatForcedActions, a);
                    return VINF_EM_TERMINATE;

                default:
                    AssertMsgFailed(("%s\n", VMGetStateName(enmState)));
            }
        }

        /*
         * Debugger Facility polling.
         */
        if (   VM_FF_IS_SET(pVM, VM_FF_DBGF)
            || VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_DBGF) )
        {
            CPUM_IMPORT_EXTRN_RCSTRICT(pVCpu, ~CPUMCTX_EXTRN_KEEPER_MASK, rc);
            rc2 = DBGFR3VMMForcedAction(pVM, pVCpu);
            /** @todo why that VINF_EM_DBG_EVENT here? Duplicate info, should be handled
             *        somewhere before we get here, I would think. */
            if (rc == VINF_EM_DBG_EVENT) /* HACK! We should've handled pending debug event. */
                rc = rc2;
            else
                UPDATE_RC();
        }

        /*
         * Postponed reset request.
         */
        if (VM_FF_TEST_AND_CLEAR(pVM, VM_FF_RESET))
        {
            CPUM_IMPORT_EXTRN_RCSTRICT(pVCpu, ~CPUMCTX_EXTRN_KEEPER_MASK, rc);
            rc2 = VBOXSTRICTRC_TODO(VMR3ResetFF(pVM));
            UPDATE_RC();
        }

        /*
         * Out of memory? Putting this after CSAM as it may in theory cause us to run out of memory.
         */
        if (VM_FF_IS_SET(pVM, VM_FF_PGM_NO_MEMORY))
        {
            rc2 = PGMR3PhysAllocateHandyPages(pVM);
            UPDATE_RC();
            if (rc == VINF_EM_NO_MEMORY)
                return rc;
        }

        /* check that we got them all  */
        AssertCompile(VM_FF_NORMAL_PRIORITY_POST_MASK == (VM_FF_CHECK_VM_STATE | VM_FF_DBGF | VM_FF_RESET | VM_FF_PGM_NO_MEMORY | VM_FF_EMT_RENDEZVOUS));
        AssertCompile(VMCPU_FF_NORMAL_PRIORITY_POST_MASK == VMCPU_FF_DBGF);
    }

    /*
     * Normal priority then.
     * (Executed in no particular order.)
     */
    if (VM_FF_IS_PENDING_EXCEPT(pVM, VM_FF_NORMAL_PRIORITY_MASK, VM_FF_PGM_NO_MEMORY))
    {
        /*
         * PDM Queues are pending.
         */
        if (VM_FF_IS_PENDING_EXCEPT(pVM, VM_FF_PDM_QUEUES, VM_FF_PGM_NO_MEMORY))
            PDMR3QueueFlushAll(pVM);

        /*
         * PDM DMA transfers are pending.
         */
        if (VM_FF_IS_PENDING_EXCEPT(pVM, VM_FF_PDM_DMA, VM_FF_PGM_NO_MEMORY))
            PDMR3DmaRun(pVM);

        /*
         * EMT Rendezvous (make sure they are handled before the requests).
         */
        if (VM_FF_IS_SET(pVM, VM_FF_EMT_RENDEZVOUS))
        {
            CPUM_IMPORT_EXTRN_RCSTRICT(pVCpu, ~CPUMCTX_EXTRN_KEEPER_MASK, rc);
            rc2 = VMMR3EmtRendezvousFF(pVM, pVCpu);
            UPDATE_RC();
            /** @todo HACK ALERT! The following test is to make sure EM+TM
             * thinks the VM is stopped/reset before the next VM state change
             * is made. We need a better solution for this, or at least make it
             * possible to do: (rc >= VINF_EM_FIRST && rc <=
             * VINF_EM_SUSPEND). */
            if (RT_UNLIKELY(rc == VINF_EM_SUSPEND || rc == VINF_EM_RESET || rc == VINF_EM_OFF))
            {
                Log2(("emR3ForcedActions: returns %Rrc\n", rc));
                STAM_REL_PROFILE_STOP(&pVCpu->em.s.StatForcedActions, a);
                return rc;
            }
        }

        /*
         * Requests from other threads.
         */
        if (VM_FF_IS_PENDING_EXCEPT(pVM, VM_FF_REQUEST, VM_FF_PGM_NO_MEMORY))
        {
            CPUM_IMPORT_EXTRN_RCSTRICT(pVCpu, ~CPUMCTX_EXTRN_KEEPER_MASK, rc);
            rc2 = VMR3ReqProcessU(pVM->pUVM, VMCPUID_ANY, false /*fPriorityOnly*/);
            if (rc2 == VINF_EM_OFF || rc2 == VINF_EM_TERMINATE) /** @todo this shouldn't be necessary */
            {
                Log2(("emR3ForcedActions: returns %Rrc\n", rc2));
                STAM_REL_PROFILE_STOP(&pVCpu->em.s.StatForcedActions, a);
                return rc2;
            }
            UPDATE_RC();
            /** @todo HACK ALERT! The following test is to make sure EM+TM
             * thinks the VM is stopped/reset before the next VM state change
             * is made. We need a better solution for this, or at least make it
             * possible to do: (rc >= VINF_EM_FIRST && rc <=
             * VINF_EM_SUSPEND). */
            if (RT_UNLIKELY(rc == VINF_EM_SUSPEND || rc == VINF_EM_RESET || rc == VINF_EM_OFF))
            {
                Log2(("emR3ForcedActions: returns %Rrc\n", rc));
                STAM_REL_PROFILE_STOP(&pVCpu->em.s.StatForcedActions, a);
                return rc;
            }
        }

        /* check that we got them all  */
        AssertCompile(VM_FF_NORMAL_PRIORITY_MASK == (VM_FF_REQUEST | VM_FF_PDM_QUEUES | VM_FF_PDM_DMA | VM_FF_EMT_RENDEZVOUS));
    }

    /*
     * Normal priority then. (per-VCPU)
     * (Executed in no particular order.)
     */
    if (    !VM_FF_IS_SET(pVM, VM_FF_PGM_NO_MEMORY)
        &&  VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_NORMAL_PRIORITY_MASK))
    {
        /*
         * Requests from other threads.
         */
        if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_REQUEST))
        {
            CPUM_IMPORT_EXTRN_RCSTRICT(pVCpu, ~CPUMCTX_EXTRN_KEEPER_MASK, rc);
            rc2 = VMR3ReqProcessU(pVM->pUVM, pVCpu->idCpu, false /*fPriorityOnly*/);
            if (rc2 == VINF_EM_OFF || rc2 == VINF_EM_TERMINATE || rc2 == VINF_EM_RESET)
            {
                Log2(("emR3ForcedActions: returns %Rrc\n", rc2));
                STAM_REL_PROFILE_STOP(&pVCpu->em.s.StatForcedActions, a);
                return rc2;
            }
            UPDATE_RC();
            /** @todo HACK ALERT! The following test is to make sure EM+TM
             * thinks the VM is stopped/reset before the next VM state change
             * is made. We need a better solution for this, or at least make it
             * possible to do: (rc >= VINF_EM_FIRST && rc <=
             * VINF_EM_SUSPEND). */
            if (RT_UNLIKELY(rc == VINF_EM_SUSPEND || rc == VINF_EM_RESET || rc == VINF_EM_OFF))
            {
                Log2(("emR3ForcedActions: returns %Rrc\n", rc));
                STAM_REL_PROFILE_STOP(&pVCpu->em.s.StatForcedActions, a);
                return rc;
            }
        }

        /* check that we got them all  */
        Assert(!(VMCPU_FF_NORMAL_PRIORITY_MASK & ~VMCPU_FF_REQUEST));
    }

    /*
     * High priority pre execution chunk last.
     * (Executed in ascending priority order.)
     */
    if (    VM_FF_IS_ANY_SET(pVM, VM_FF_HIGH_PRIORITY_PRE_MASK)
        ||  VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_HIGH_PRIORITY_PRE_MASK))
    {
        /*
         * Timers before interrupts.
         */
        if (   VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_TIMER)
            && !VM_FF_IS_SET(pVM, VM_FF_PGM_NO_MEMORY))
            TMR3TimerQueuesDo(pVM);

#if !defined(VBOX_VMM_TARGET_ARMV8)
        /*
         * Pick up asynchronously posted interrupts into the APIC.
         */
        if (VMCPU_FF_TEST_AND_CLEAR(pVCpu, VMCPU_FF_UPDATE_APIC))
            APICUpdatePendingInterrupts(pVCpu);

        /*
         * The instruction following an emulated STI should *always* be executed!
         *
         * Note! We intentionally don't clear CPUMCTX_INHIBIT_INT here if
         *       the eip is the same as the inhibited instr address.  Before we
         *       are able to execute this instruction in raw mode (iret to
         *       guest code) an external interrupt might force a world switch
         *       again.  Possibly allowing a guest interrupt to be dispatched
         *       in the process.  This could break the guest.  Sounds very
         *       unlikely, but such timing sensitive problem are not as rare as
         *       you might think.
         *
         * Note! This used to be a force action flag. Can probably ditch this code.
         */
        /** @todo r=bird: the clearing case will *never* be taken here as
         *        CPUMIsInInterruptShadow already makes sure the RIPs matches. */
        if (   CPUMIsInInterruptShadow(&pVCpu->cpum.GstCtx)
            && !VM_FF_IS_SET(pVM, VM_FF_PGM_NO_MEMORY))
        {
            CPUM_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_INHIBIT_INT);
            if (CPUMGetGuestRIP(pVCpu) != pVCpu->cpum.GstCtx.uRipInhibitInt)
            {
                CPUMClearInterruptShadow(&pVCpu->cpum.GstCtx);
                Log(("Clearing CPUMCTX_INHIBIT_INT at %RGv - successor %RGv\n",
                     (RTGCPTR)CPUMGetGuestRIP(pVCpu), (RTGCPTR)pVCpu->cpum.GstCtx.uRipInhibitInt));
            }
            else
                Log(("Leaving CPUMCTX_INHIBIT_INT set at %RGv\n", (RTGCPTR)CPUMGetGuestRIP(pVCpu)));
        }

        /** @todo SMIs. If we implement SMIs, this is where they will have to be
         *        delivered. */

# ifdef VBOX_WITH_NESTED_HWVIRT_VMX
        if (VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_VMX_APIC_WRITE | VMCPU_FF_VMX_MTF | VMCPU_FF_VMX_PREEMPT_TIMER
                                     | VMCPU_FF_VMX_NMI_WINDOW | VMCPU_FF_VMX_INT_WINDOW))
        {
            /*
             * VMX Nested-guest APIC-write pending (can cause VM-exits).
             * Takes priority over even SMI and INIT signals.
             * See Intel spec. 29.4.3.2 "APIC-Write Emulation".
             */
            if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_VMX_APIC_WRITE))
            {
                rc2 = VBOXSTRICTRC_VAL(IEMExecVmxVmexitApicWrite(pVCpu));
                if (rc2 != VINF_VMX_INTERCEPT_NOT_ACTIVE)
                    UPDATE_RC();
            }

            /*
             * APIC write emulation MAY have a caused a VM-exit.
             * If it did cause a VM-exit, there's no point checking the other VMX non-root mode FFs here.
             */
            if (CPUMIsGuestInVmxNonRootMode(&pVCpu->cpum.GstCtx))
            {
                /*
                 * VMX Nested-guest monitor-trap flag (MTF) VM-exit.
                 * Takes priority over "Traps on the previous instruction".
                 * See Intel spec. 6.9 "Priority Among Simultaneous Exceptions And Interrupts".
                 */
                if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_VMX_MTF))
                {
                    rc2 = VBOXSTRICTRC_VAL(IEMExecVmxVmexit(pVCpu, VMX_EXIT_MTF, 0 /* uExitQual */));
                    Assert(rc2 != VINF_VMX_INTERCEPT_NOT_ACTIVE);
                    UPDATE_RC();
                }
                /*
                 * VMX Nested-guest preemption timer VM-exit.
                 * Takes priority over NMI-window VM-exits.
                 */
                else if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_VMX_PREEMPT_TIMER))
                {
                    rc2 = VBOXSTRICTRC_VAL(IEMExecVmxVmexitPreemptTimer(pVCpu));
                    Assert(rc2 != VINF_VMX_INTERCEPT_NOT_ACTIVE);
                    UPDATE_RC();
                }
                /*
                 * VMX interrupt-window and NMI-window VM-exits.
                 * Takes priority over non-maskable interrupts (NMIs) and external interrupts respectively.
                 * If we are in an interrupt shadow or if we already in the process of delivering
                 * an event then these VM-exits cannot occur.
                 *
                 * Interrupt shadows block NMI-window VM-exits.
                 * Any event that is already in TRPM (e.g. injected during VM-entry) takes priority.
                 *
                 * See Intel spec. 25.2 "Other Causes Of VM Exits".
                 * See Intel spec. 26.7.6 "NMI-Window Exiting".
                 * See Intel spec. 6.7 "Nonmaskable Interrupt (NMI)".
                 */
                else if (   !CPUMIsInInterruptShadow(&pVCpu->cpum.GstCtx)
                         && !CPUMAreInterruptsInhibitedByNmi(&pVCpu->cpum.GstCtx)
                         && !TRPMHasTrap(pVCpu))
                {
                    /*
                     * VMX NMI-window VM-exit.
                     */
                    if (    VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_VMX_NMI_WINDOW)
                        && !CPUMIsGuestVmxVirtNmiBlocking(&pVCpu->cpum.GstCtx))
                    {
                        Assert(CPUMIsGuestVmxProcCtlsSet(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS_NMI_WINDOW_EXIT));
                        Assert(CPUMIsGuestVmxInterceptEvents(&pVCpu->cpum.GstCtx));
                        rc2 = VBOXSTRICTRC_VAL(IEMExecVmxVmexit(pVCpu, VMX_EXIT_NMI_WINDOW, 0 /* uExitQual */));
                        AssertMsg(   rc2 != VINF_VMX_INTERCEPT_NOT_ACTIVE
                                  && rc2 != VINF_VMX_VMEXIT
                                  && rc2 != VINF_NO_CHANGE, ("%Rrc\n", rc2));
                        UPDATE_RC();
                    }
                    /*
                     * VMX interrupt-window VM-exit.
                     * This is a bit messy with the way the code below is currently structured,
                     * but checking VMCPU_FF_INTERRUPT_NMI here (combined with CPUMAreInterruptsInhibitedByNmi
                     * already checked at this point) should allow a pending NMI to be delivered prior to
                     * causing an interrupt-window VM-exit.
                     */
                    /** @todo Restructure this later to happen after injecting NMI/causing NMI-exit, see
                     *        code in VMX R0 event delivery. */
                    else if (    VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_VMX_INT_WINDOW)
                             && !VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_NMI)
                             &&  CPUMIsGuestVmxVirtIntrEnabled(&pVCpu->cpum.GstCtx))
                    {
                        Assert(CPUMIsGuestVmxProcCtlsSet(&pVCpu->cpum.GstCtx, VMX_PROC_CTLS_INT_WINDOW_EXIT));
                        Assert(CPUMIsGuestVmxInterceptEvents(&pVCpu->cpum.GstCtx));
                        rc2 = VBOXSTRICTRC_VAL(IEMExecVmxVmexit(pVCpu, VMX_EXIT_INT_WINDOW, 0 /* uExitQual */));
                        AssertMsg(   rc2 != VINF_VMX_INTERCEPT_NOT_ACTIVE
                                  && rc2 != VINF_VMX_VMEXIT
                                  && rc2 != VINF_NO_CHANGE, ("%Rrc\n", rc2));
                        UPDATE_RC();
                    }
                }
            }

            /*
             * Interrupt-window and NMI-window force flags might still be pending if we didn't actually cause
             * a VM-exit above. They will get cleared eventually when ANY nested-guest VM-exit occurs.
             * However, the force flags asserted below MUST have been cleared at this point.
             */
            Assert(!VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_VMX_APIC_WRITE | VMCPU_FF_VMX_MTF | VMCPU_FF_VMX_PREEMPT_TIMER));
        }
# endif

        /*
         * Guest event injection.
         */
        Assert(!(pVCpu->cpum.GstCtx.fExtrn & (CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI)));
        bool fWakeupPending = false;
        if (    VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_VMX_NMI_WINDOW | VMCPU_FF_VMX_INT_WINDOW
                                         | VMCPU_FF_INTERRUPT_NMI  | VMCPU_FF_INTERRUPT_NESTED_GUEST
                                         | VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_INTERRUPT_PIC)
            && !VM_FF_IS_SET(pVM, VM_FF_PGM_NO_MEMORY)
            && (!rc || rc >= VINF_EM_RESCHEDULE_EXEC_ENGINE)
            && !CPUMIsInInterruptShadow(&pVCpu->cpum.GstCtx)             /* Interrupt shadows block both NMIs and interrupts. */
            && !TRPMHasTrap(pVCpu))                                      /* An event could already be scheduled for dispatching. */
        {
            if (CPUMGetGuestGif(&pVCpu->cpum.GstCtx))
            {
                bool fInVmxNonRootMode;
                bool fInSvmHwvirtMode;
                if (!CPUMIsGuestInNestedHwvirtMode(&pVCpu->cpum.GstCtx))
                {
                    fInVmxNonRootMode = false;
                    fInSvmHwvirtMode  = false;
                }
                else
                {
                    fInVmxNonRootMode = CPUMIsGuestInVmxNonRootMode(&pVCpu->cpum.GstCtx);
                    fInSvmHwvirtMode  = CPUMIsGuestInSvmNestedHwVirtMode(&pVCpu->cpum.GstCtx);
                }

                /*
                 * NMIs (take priority over external interrupts).
                 */
                if (   VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_NMI)
                    && !CPUMAreInterruptsInhibitedByNmi(&pVCpu->cpum.GstCtx))
                {
# ifdef VBOX_WITH_NESTED_HWVIRT_VMX
                    if (   fInVmxNonRootMode
                        && CPUMIsGuestVmxPinCtlsSet(&pVCpu->cpum.GstCtx, VMX_PIN_CTLS_NMI_EXIT))
                    {
                        /* We MUST clear the NMI force-flag here, see @bugref{10318#c19}. */
                        VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_INTERRUPT_NMI);
                        rc2 = VBOXSTRICTRC_VAL(IEMExecVmxVmexitXcptNmi(pVCpu));
                        Assert(rc2 != VINF_VMX_INTERCEPT_NOT_ACTIVE);
                        UPDATE_RC();
                    }
                    else
# endif
# ifdef VBOX_WITH_NESTED_HWVIRT_SVM
                    if (   fInSvmHwvirtMode
                        && CPUMIsGuestSvmCtrlInterceptSet(pVCpu, &pVCpu->cpum.GstCtx, SVM_CTRL_INTERCEPT_NMI))
                    {
                        rc2 = VBOXSTRICTRC_VAL(IEMExecSvmVmexit(pVCpu, SVM_EXIT_NMI, 0 /* uExitInfo1 */,  0 /* uExitInfo2 */));
                        AssertMsg(   rc2 != VINF_SVM_VMEXIT
                                  && rc2 != VINF_NO_CHANGE, ("%Rrc\n", rc2));
                        UPDATE_RC();
                    }
                    else
# endif
                    {
                        rc2 = TRPMAssertTrap(pVCpu, X86_XCPT_NMI, TRPM_NMI);
                        if (rc2 == VINF_SUCCESS)
                        {
                            VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_INTERRUPT_NMI);
                            fWakeupPending = true;
# if 0 /* HMR3IsActive is not reliable (esp. after restore), just return VINF_EM_RESCHEDULE. */
                            if (pVM->em.s.fIemExecutesAll)
                                rc2 = VINF_EM_RESCHEDULE;
                            else
                            {
                                rc2 = HMR3IsActive(pVCpu)    ? VINF_EM_RESCHEDULE_HM
                                    : VM_IS_NEM_ENABLED(pVM) ? VINF_EM_RESCHEDULE
                                    :                          VINF_EM_RESCHEDULE_REM;
                            }
# else
                            rc2 = VINF_EM_RESCHEDULE;
# endif
                        }
                        UPDATE_RC();
                    }
                }
# ifdef VBOX_WITH_NESTED_HWVIRT_SVM
                /** @todo NSTSVM: Handle this for SVM here too later not when an interrupt is
                 *        actually pending like we currently do. */
# endif
                /*
                 * External interrupts.
                 */
                else
                {
                    /*
                     * VMX: virtual interrupts takes priority over physical interrupts.
                     * SVM: physical interrupts takes priority over virtual interrupts.
                     */
                    if (   fInVmxNonRootMode
                        && VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_NESTED_GUEST)
                        && CPUMIsGuestVmxVirtIntrEnabled(&pVCpu->cpum.GstCtx))
                    {
                        /** @todo NSTVMX: virtual-interrupt delivery. */
                        rc2 = VINF_SUCCESS;
                    }
                    else if (   VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_INTERRUPT_PIC)
                             && CPUMIsGuestPhysIntrEnabled(pVCpu))
                    {
                        Assert(pVCpu->em.s.enmState != EMSTATE_WAIT_SIPI);
                        if (fInVmxNonRootMode)
                            rc2 = emR3VmxNstGstIntrIntercept(pVCpu);
                        else if (fInSvmHwvirtMode)
                            rc2 = emR3SvmNstGstIntrIntercept(pVCpu);
                        else
                            rc2 = VINF_NO_CHANGE;

                        if (rc2 == VINF_NO_CHANGE)
                        {
                            bool fInjected = false;
                            CPUM_IMPORT_EXTRN_RET(pVCpu, IEM_CPUMCTX_EXTRN_XCPT_MASK);
                            /** @todo this really isn't nice, should properly handle this */
                            /* Note! This can still cause a VM-exit (on Intel). */
                            LogFlow(("Calling TRPMR3InjectEvent: %04x:%08RX64 efl=%#x\n",
                                     pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pVCpu->cpum.GstCtx.eflags));
                            rc2 = TRPMR3InjectEvent(pVM, pVCpu, TRPM_HARDWARE_INT, &fInjected);
                            fWakeupPending = true;
                            if (   pVM->em.s.fIemExecutesAll
                                && (   rc2 == VINF_EM_RESCHEDULE_REM
                                    || rc2 == VINF_EM_RESCHEDULE_EXEC_ENGINE))
                                rc2 = VINF_EM_RESCHEDULE;
# ifdef VBOX_STRICT
                            if (fInjected)
                                rcIrq = rc2;
# endif
                        }
                        UPDATE_RC();
                    }
                    else if (   fInSvmHwvirtMode
                             && VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_NESTED_GUEST)
                             && CPUMIsGuestSvmVirtIntrEnabled(pVCpu,  &pVCpu->cpum.GstCtx))
                    {
                        rc2 = emR3SvmNstGstVirtIntrIntercept(pVCpu);
                        if (rc2 == VINF_NO_CHANGE)
                        {
                            VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_INTERRUPT_NESTED_GUEST);
                            uint8_t const uNstGstVector = CPUMGetGuestSvmVirtIntrVector(&pVCpu->cpum.GstCtx);
                            AssertMsg(uNstGstVector > 0 && uNstGstVector <= X86_XCPT_LAST, ("Invalid VINTR %#x\n", uNstGstVector));
                            TRPMAssertTrap(pVCpu, uNstGstVector, TRPM_HARDWARE_INT);
                            Log(("EM: Asserting nested-guest virt. hardware intr: %#x\n", uNstGstVector));
                            rc2 = VINF_EM_RESCHEDULE;
# ifdef VBOX_STRICT
                            rcIrq = rc2;
# endif
                        }
                        UPDATE_RC();
                    }
                }
            } /* CPUMGetGuestGif */
        }

#else /* VBOX_VMM_TARGET_ARMV8 */
        bool fWakeupPending = false;

        if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_VTIMER_ACTIVATED))
        {
            VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_VTIMER_ACTIVATED);

            fWakeupPending = true;
            rc2 = VINF_EM_RESCHEDULE;
            UPDATE_RC();
        }
#endif /* VBOX_VMM_TARGET_ARMV8 */

        /*
         * Allocate handy pages.
         */
        if (VM_FF_IS_PENDING_EXCEPT(pVM, VM_FF_PGM_NEED_HANDY_PAGES, VM_FF_PGM_NO_MEMORY))
        {
            rc2 = PGMR3PhysAllocateHandyPages(pVM);
            UPDATE_RC();
        }

        /*
         * Debugger Facility request.
         */
        if (   (   VM_FF_IS_SET(pVM, VM_FF_DBGF)
                || VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_DBGF) )
            && !VM_FF_IS_SET(pVM, VM_FF_PGM_NO_MEMORY) )
        {
            CPUM_IMPORT_EXTRN_RCSTRICT(pVCpu, ~CPUMCTX_EXTRN_KEEPER_MASK, rc);
            rc2 = DBGFR3VMMForcedAction(pVM, pVCpu);
            UPDATE_RC();
        }

        /*
         * EMT Rendezvous (must be serviced before termination).
         */
        if (   !fWakeupPending /* don't miss the wakeup from EMSTATE_HALTED! */
            && VM_FF_IS_SET(pVM, VM_FF_EMT_RENDEZVOUS))
        {
            CPUM_IMPORT_EXTRN_RCSTRICT(pVCpu, ~CPUMCTX_EXTRN_KEEPER_MASK, rc);
            rc2 = VMMR3EmtRendezvousFF(pVM, pVCpu);
            UPDATE_RC();
            /** @todo HACK ALERT! The following test is to make sure EM+TM thinks the VM is
             * stopped/reset before the next VM state change is made. We need a better
             * solution for this, or at least make it possible to do: (rc >= VINF_EM_FIRST
             * && rc >= VINF_EM_SUSPEND). */
            if (RT_UNLIKELY(rc == VINF_EM_SUSPEND || rc == VINF_EM_RESET || rc == VINF_EM_OFF))
            {
                Log2(("emR3ForcedActions: returns %Rrc\n", rc));
                STAM_REL_PROFILE_STOP(&pVCpu->em.s.StatForcedActions, a);
                return rc;
            }
        }

        /*
         * State change request (cleared by vmR3SetStateLocked).
         */
        if (   !fWakeupPending /* don't miss the wakeup from EMSTATE_HALTED! */
            && VM_FF_IS_SET(pVM, VM_FF_CHECK_VM_STATE))
        {
            VMSTATE enmState = VMR3GetState(pVM);
            switch (enmState)
            {
                case VMSTATE_FATAL_ERROR:
                case VMSTATE_FATAL_ERROR_LS:
                case VMSTATE_GURU_MEDITATION:
                case VMSTATE_GURU_MEDITATION_LS:
                    Log2(("emR3ForcedActions: %s -> VINF_EM_SUSPEND\n", VMGetStateName(enmState) ));
                    STAM_REL_PROFILE_STOP(&pVCpu->em.s.StatForcedActions, a);
                    return VINF_EM_SUSPEND;

                case VMSTATE_DESTROYING:
                    Log2(("emR3ForcedActions: %s -> VINF_EM_TERMINATE\n", VMGetStateName(enmState) ));
                    STAM_REL_PROFILE_STOP(&pVCpu->em.s.StatForcedActions, a);
                    return VINF_EM_TERMINATE;

                default:
                    AssertMsgFailed(("%s\n", VMGetStateName(enmState)));
            }
        }

        /*
         * Out of memory? Since most of our fellow high priority actions may cause us
         * to run out of memory, we're employing VM_FF_IS_PENDING_EXCEPT and putting this
         * at the end rather than the start. Also, VM_FF_TERMINATE has higher priority
         * than us since we can terminate without allocating more memory.
         */
        if (VM_FF_IS_SET(pVM, VM_FF_PGM_NO_MEMORY))
        {
            rc2 = PGMR3PhysAllocateHandyPages(pVM);
            UPDATE_RC();
            if (rc == VINF_EM_NO_MEMORY)
                return rc;
        }

        /*
         * If the virtual sync clock is still stopped, make TM restart it.
         */
        if (VM_FF_IS_SET(pVM, VM_FF_TM_VIRTUAL_SYNC))
            TMR3VirtualSyncFF(pVM, pVCpu);

#ifdef DEBUG
        /*
         * Debug, pause the VM.
         */
        if (VM_FF_IS_SET(pVM, VM_FF_DEBUG_SUSPEND))
        {
            VM_FF_CLEAR(pVM, VM_FF_DEBUG_SUSPEND);
            Log(("emR3ForcedActions: returns VINF_EM_SUSPEND\n"));
            return VINF_EM_SUSPEND;
        }
#endif

        /* check that we got them all  */
        AssertCompile(VM_FF_HIGH_PRIORITY_PRE_MASK == (VM_FF_TM_VIRTUAL_SYNC | VM_FF_DBGF | VM_FF_CHECK_VM_STATE | VM_FF_DEBUG_SUSPEND | VM_FF_PGM_NEED_HANDY_PAGES | VM_FF_PGM_NO_MEMORY | VM_FF_EMT_RENDEZVOUS));
#if defined(VBOX_VMM_TARGET_ARMV8)
        AssertCompile(VMCPU_FF_HIGH_PRIORITY_PRE_MASK == (VMCPU_FF_TIMER | VMCPU_FF_INTERRUPT_IRQ | VMCPU_FF_INTERRUPT_FIQ | VMCPU_FF_DBGF));
#else
        AssertCompile(VMCPU_FF_HIGH_PRIORITY_PRE_MASK == (VMCPU_FF_TIMER | VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_UPDATE_APIC | VMCPU_FF_INTERRUPT_PIC | VMCPU_FF_PGM_SYNC_CR3 | VMCPU_FF_PGM_SYNC_CR3_NON_GLOBAL | VMCPU_FF_DBGF | VMCPU_FF_INTERRUPT_NESTED_GUEST | VMCPU_FF_VMX_MTF | VMCPU_FF_VMX_APIC_WRITE | VMCPU_FF_VMX_PREEMPT_TIMER | VMCPU_FF_VMX_INT_WINDOW | VMCPU_FF_VMX_NMI_WINDOW));
#endif
    }

#undef UPDATE_RC
    Log2(("emR3ForcedActions: returns %Rrc\n", rc));
    STAM_REL_PROFILE_STOP(&pVCpu->em.s.StatForcedActions, a);
    Assert(rcIrq == VINF_SUCCESS || rcIrq == rc);
    return rc;
}


/**
 * Check if the preset execution time cap restricts guest execution scheduling.
 *
 * @returns true if allowed, false otherwise
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
bool emR3IsExecutionAllowedSlow(PVM pVM, PVMCPU pVCpu)
{
    Assert(pVM->uCpuExecutionCap != 100);
    uint64_t cMsUserTime;
    uint64_t cMsKernelTime;
    if (RT_SUCCESS(RTThreadGetExecutionTimeMilli(&cMsKernelTime, &cMsUserTime)))
    {
        uint64_t const msTimeNow = RTTimeMilliTS();
        if (pVCpu->em.s.msTimeSliceStart + EM_TIME_SLICE < msTimeNow)
        {
            /* New time slice. */
            pVCpu->em.s.msTimeSliceStart      = msTimeNow;
            pVCpu->em.s.cMsTimeSliceStartExec = cMsKernelTime + cMsUserTime;
            pVCpu->em.s.cMsTimeSliceExec      = 0;
        }
        pVCpu->em.s.cMsTimeSliceExec = cMsKernelTime + cMsUserTime - pVCpu->em.s.cMsTimeSliceStartExec;

        bool const fRet = pVCpu->em.s.cMsTimeSliceExec < (EM_TIME_SLICE * pVM->uCpuExecutionCap) / 100;
        Log2(("emR3IsExecutionAllowed: start=%RX64 startexec=%RX64 exec=%RX64 (cap=%x)\n", pVCpu->em.s.msTimeSliceStart,
              pVCpu->em.s.cMsTimeSliceStartExec, pVCpu->em.s.cMsTimeSliceExec, (EM_TIME_SLICE * pVM->uCpuExecutionCap) / 100));
        return fRet;
    }
    return true;
}


/**
 * Execute VM.
 *
 * This function is the main loop of the VM. The emulation thread
 * calls this function when the VM has been successfully constructed
 * and we're ready for executing the VM.
 *
 * Returning from this function means that the VM is turned off or
 * suspended (state already saved) and deconstruction is next in line.
 *
 * All interaction from other thread are done using forced actions
 * and signalling of the wait object.
 *
 * @returns VBox status code, informational status codes may indicate failure.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMR3_INT_DECL(int) EMR3ExecuteVM(PVM pVM, PVMCPU pVCpu)
{
    Log(("EMR3ExecuteVM: pVM=%p enmVMState=%d (%s)  enmState=%d (%s) enmPrevState=%d (%s)\n",
         pVM,
         pVM->enmVMState,          VMR3GetStateName(pVM->enmVMState),
         pVCpu->em.s.enmState,     emR3GetStateName(pVCpu->em.s.enmState),
         pVCpu->em.s.enmPrevState, emR3GetStateName(pVCpu->em.s.enmPrevState) ));
    VM_ASSERT_EMT(pVM);
    AssertMsg(   pVCpu->em.s.enmState == EMSTATE_NONE
              || pVCpu->em.s.enmState == EMSTATE_WAIT_SIPI
              || pVCpu->em.s.enmState == EMSTATE_SUSPENDED,
              ("%s\n", emR3GetStateName(pVCpu->em.s.enmState)));

    int rc = setjmp(pVCpu->em.s.u.FatalLongJump);
    if (rc == 0)
    {
        /*
         * Start the virtual time.
         */
        TMR3NotifyResume(pVM, pVCpu);

        /*
         * The Outer Main Loop.
         */
        bool fFFDone = false;

        /* Reschedule right away to start in the right state. */
        rc = VINF_SUCCESS;

        /* If resuming after a pause or a state load, restore the previous
           state or else we'll start executing code. Else, just reschedule. */
        if (    pVCpu->em.s.enmState == EMSTATE_SUSPENDED
            &&  (   pVCpu->em.s.enmPrevState == EMSTATE_WAIT_SIPI
                 || pVCpu->em.s.enmPrevState == EMSTATE_HALTED))
            pVCpu->em.s.enmState = pVCpu->em.s.enmPrevState;
        else
            pVCpu->em.s.enmState = emR3Reschedule(pVM, pVCpu);
        Log(("EMR3ExecuteVM: enmState=%s\n", emR3GetStateName(pVCpu->em.s.enmState)));

        STAM_REL_PROFILE_ADV_START(&pVCpu->em.s.StatTotal, x);
        for (;;)
        {
            /*
             * Before we can schedule anything (we're here because
             * scheduling is required) we must service any pending
             * forced actions to avoid any pending action causing
             * immediate rescheduling upon entering an inner loop
             *
             * Do forced actions.
             */
            if (   !fFFDone
                && RT_SUCCESS(rc)
                && rc != VINF_EM_TERMINATE
                && rc != VINF_EM_OFF
                && (   VM_FF_IS_ANY_SET(pVM, VM_FF_ALL_REM_MASK)
                    || VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_ALL_REM_MASK & ~VMCPU_FF_UNHALT)))
            {
                rc = emR3ForcedActions(pVM, pVCpu, rc);
                VBOXVMM_EM_FF_ALL_RET(pVCpu, rc);
            }
            else if (fFFDone)
                fFFDone = false;

#if defined(VBOX_STRICT) && !defined(VBOX_VMM_TARGET_ARMV8)
            CPUMAssertGuestRFlagsCookie(pVM, pVCpu);
#endif

            /*
             * Now what to do?
             */
            Log2(("EMR3ExecuteVM: rc=%Rrc\n", rc));
            EMSTATE const enmOldState = pVCpu->em.s.enmState;
            switch (rc)
            {
                /*
                 * Keep doing what we're currently doing.
                 */
                case VINF_SUCCESS:
                    break;

                /*
                 * Reschedule - to main execution engine (HM, NEM, IEM/REM).
                 */
                case VINF_EM_RESCHEDULE_EXEC_ENGINE:
                    Assert(!pVM->em.s.fIemExecutesAll || pVCpu->em.s.enmState != EMSTATE_IEM);
                    if (!pVM->em.s.fIemExecutesAll)
                    {
#if !defined(VBOX_VMM_TARGET_ARMV8)
                        if (VM_IS_HM_ENABLED(pVM))
                        {
                            if (HMCanExecuteGuest(pVM, pVCpu, &pVCpu->cpum.GstCtx))
                            {
                                Log2(("EMR3ExecuteVM: VINF_EM_RESCHEDULE_EXEC_ENGINE: %d -> %d (EMSTATE_HM)\n", enmOldState, EMSTATE_HM));
                                pVCpu->em.s.enmState = EMSTATE_HM;
                                break;
                            }
                        }
                        else
#endif
                        if (VM_IS_NEM_ENABLED(pVM) && NEMR3CanExecuteGuest(pVM, pVCpu))
                        {
                            Log2(("EMR3ExecuteVM: VINF_EM_RESCHEDULE_EXEC_ENGINE: %d -> %d (EMSTATE_NEM)\n", enmOldState, EMSTATE_NEM));
                            pVCpu->em.s.enmState = EMSTATE_NEM;
                            break;
                        }
                    }

                    Log2(("EMR3ExecuteVM: VINF_EM_RESCHEDULE_EXEC_ENGINE: %d -> %d (EMSTATE_RECOMPILER)\n", enmOldState, EMSTATE_RECOMPILER));
                    pVCpu->em.s.enmState = EMSTATE_RECOMPILER;
                    break;

                /*
                 * Reschedule - to recompiled execution.
                 */
                case VINF_EM_RESCHEDULE_REM:
                    Assert(!pVM->em.s.fIemExecutesAll || pVCpu->em.s.enmState != EMSTATE_IEM);
                    Log2(("EMR3ExecuteVM: VINF_EM_RESCHEDULE_REM: %d -> %d (EMSTATE_REM)\n",
                          enmOldState, EMSTATE_RECOMPILER));
                    pVCpu->em.s.enmState = EMSTATE_RECOMPILER;
                    break;

                /*
                 * Resume.
                 */
                case VINF_EM_RESUME:
                    Log2(("EMR3ExecuteVM: VINF_EM_RESUME: %d -> VINF_EM_RESCHEDULE\n", enmOldState));
                    /* Don't reschedule in the halted or wait-for-SIPI cases. */
                    if (    pVCpu->em.s.enmPrevState == EMSTATE_WAIT_SIPI
                        ||  pVCpu->em.s.enmPrevState == EMSTATE_HALTED)
                    {
                        pVCpu->em.s.enmState = pVCpu->em.s.enmPrevState;
                        break;
                    }
                    /* fall through and get scheduled. */
                    RT_FALL_THRU();

                /*
                 * Reschedule.
                 */
                case VINF_EM_RESCHEDULE:
                {
                    EMSTATE enmState = emR3Reschedule(pVM, pVCpu);
                    Log2(("EMR3ExecuteVM: VINF_EM_RESCHEDULE: %d -> %d (%s)\n", enmOldState, enmState, emR3GetStateName(enmState)));
                    pVCpu->em.s.enmState = enmState;
                    break;
                }

                /*
                 * Halted.
                 */
                case VINF_EM_HALT:
                    Log2(("EMR3ExecuteVM: VINF_EM_HALT: %d -> %d\n", enmOldState, EMSTATE_HALTED));
                    pVCpu->em.s.enmState = EMSTATE_HALTED;
                    break;

                /*
                 * Switch to the wait for SIPI state (application processor only)
                 */
                case VINF_EM_WAIT_SIPI:
                    Assert(pVCpu->idCpu != 0);
                    Log2(("EMR3ExecuteVM: VINF_EM_WAIT_SIPI: %d -> %d\n", enmOldState, EMSTATE_WAIT_SIPI));
                    pVCpu->em.s.enmState = EMSTATE_WAIT_SIPI;
                    break;


                /*
                 * Suspend.
                 */
                case VINF_EM_SUSPEND:
                    Log2(("EMR3ExecuteVM: VINF_EM_SUSPEND: %d -> %d\n", enmOldState, EMSTATE_SUSPENDED));
                    Assert(enmOldState != EMSTATE_SUSPENDED);
                    pVCpu->em.s.enmPrevState = enmOldState;
                    pVCpu->em.s.enmState     = EMSTATE_SUSPENDED;
                    break;

                /*
                 * Reset.
                 * We might end up doing a double reset for now, we'll have to clean up the mess later.
                 */
                case VINF_EM_RESET:
                {
                    if (pVCpu->idCpu == 0)
                    {
                        EMSTATE enmState = emR3Reschedule(pVM, pVCpu);
                        Log2(("EMR3ExecuteVM: VINF_EM_RESET: %d -> %d (%s)\n", enmOldState, enmState, emR3GetStateName(enmState)));
                        pVCpu->em.s.enmState = enmState;
                    }
                    else
                    {
                        /* All other VCPUs go into the wait for SIPI state. */
                        pVCpu->em.s.enmState = EMSTATE_WAIT_SIPI;
                    }
                    break;
                }

                /*
                 * Power Off.
                 */
                case VINF_EM_OFF:
                    pVCpu->em.s.enmState = EMSTATE_TERMINATING;
                    Log2(("EMR3ExecuteVM: returns VINF_EM_OFF (%d -> %d)\n", enmOldState, EMSTATE_TERMINATING));
                    TMR3NotifySuspend(pVM, pVCpu);
                    STAM_REL_PROFILE_ADV_STOP(&pVCpu->em.s.StatTotal, x);
                    return rc;

                /*
                 * Terminate the VM.
                 */
                case VINF_EM_TERMINATE:
                    pVCpu->em.s.enmState = EMSTATE_TERMINATING;
                    Log(("EMR3ExecuteVM returns VINF_EM_TERMINATE (%d -> %d)\n", enmOldState, EMSTATE_TERMINATING));
                    if (pVM->enmVMState < VMSTATE_DESTROYING) /* ugly */
                        TMR3NotifySuspend(pVM, pVCpu);
                    STAM_REL_PROFILE_ADV_STOP(&pVCpu->em.s.StatTotal, x);
                    return rc;


                /*
                 * Out of memory, suspend the VM and stuff.
                 */
                case VINF_EM_NO_MEMORY:
                    Log2(("EMR3ExecuteVM: VINF_EM_NO_MEMORY: %d -> %d\n", enmOldState, EMSTATE_SUSPENDED));
                    Assert(enmOldState != EMSTATE_SUSPENDED);
                    pVCpu->em.s.enmPrevState = enmOldState;
                    pVCpu->em.s.enmState = EMSTATE_SUSPENDED;
                    TMR3NotifySuspend(pVM, pVCpu);
                    STAM_REL_PROFILE_ADV_STOP(&pVCpu->em.s.StatTotal, x);

                    rc = VMSetRuntimeError(pVM, VMSETRTERR_FLAGS_SUSPEND, "HostMemoryLow",
                                           N_("Unable to allocate and lock memory. The virtual machine will be paused. Please close applications to free up memory or close the VM"));
                    if (rc != VINF_EM_SUSPEND)
                    {
                        if (RT_SUCCESS_NP(rc))
                        {
                            AssertLogRelMsgFailed(("%Rrc\n", rc));
                            rc = VERR_EM_INTERNAL_ERROR;
                        }
                        pVCpu->em.s.enmState = EMSTATE_GURU_MEDITATION;
                    }
                    return rc;

                /*
                 * Guest debug events.
                 */
                case VINF_EM_DBG_STEPPED:
                case VINF_EM_DBG_STOP:
                case VINF_EM_DBG_EVENT:
                case VINF_EM_DBG_BREAKPOINT:
                case VINF_EM_DBG_STEP:
                    if (enmOldState == EMSTATE_HM)
                    {
                        Log2(("EMR3ExecuteVM: %Rrc: %d -> %d\n", rc, enmOldState, EMSTATE_DEBUG_GUEST_HM));
                        pVCpu->em.s.enmState = EMSTATE_DEBUG_GUEST_HM;
                    }
                    else if (enmOldState == EMSTATE_NEM)
                    {
                        Log2(("EMR3ExecuteVM: %Rrc: %d -> %d\n", rc, enmOldState, EMSTATE_DEBUG_GUEST_NEM));
                        pVCpu->em.s.enmState = EMSTATE_DEBUG_GUEST_NEM;
                    }
                    else if (enmOldState == EMSTATE_RECOMPILER)
                    {
                        Log2(("EMR3ExecuteVM: %Rrc: %d -> %d\n", rc, enmOldState, EMSTATE_DEBUG_GUEST_RECOMPILER));
                        pVCpu->em.s.enmState = EMSTATE_DEBUG_GUEST_RECOMPILER;
                    }
                    else
                    {
                        Log2(("EMR3ExecuteVM: %Rrc: %d -> %d\n", rc, enmOldState, EMSTATE_DEBUG_GUEST_IEM));
                        pVCpu->em.s.enmState = EMSTATE_DEBUG_GUEST_IEM;
                    }
                    break;

                /*
                 * Hypervisor debug events.
                 */
                case VINF_EM_DBG_HYPER_STEPPED:
                case VINF_EM_DBG_HYPER_BREAKPOINT:
                case VINF_EM_DBG_HYPER_ASSERTION:
                    Log2(("EMR3ExecuteVM: %Rrc: %d -> %d\n", rc, enmOldState, EMSTATE_DEBUG_HYPER));
                    pVCpu->em.s.enmState = EMSTATE_DEBUG_HYPER;
                    break;

                /*
                 * Triple fault.
                 */
                case VINF_EM_TRIPLE_FAULT:
                    if (!pVM->em.s.fGuruOnTripleFault)
                    {
                        Log(("EMR3ExecuteVM: VINF_EM_TRIPLE_FAULT: CPU reset...\n"));
                        rc = VBOXSTRICTRC_TODO(VMR3ResetTripleFault(pVM));
                        Log2(("EMR3ExecuteVM: VINF_EM_TRIPLE_FAULT: %d -> %d (rc=%Rrc)\n", enmOldState, pVCpu->em.s.enmState, rc));
                        continue;
                    }
                    /* Else fall through and trigger a guru. */
                    RT_FALL_THRU();

                case VERR_VMM_RING0_ASSERTION:
                    Log(("EMR3ExecuteVM: %Rrc: %d -> %d (EMSTATE_GURU_MEDITATION)\n", rc, enmOldState, EMSTATE_GURU_MEDITATION));
                    pVCpu->em.s.enmState = EMSTATE_GURU_MEDITATION;
                    break;

                /*
                 * Any error code showing up here other than the ones we
                 * know and process above are considered to be FATAL.
                 *
                 * Unknown warnings and informational status codes are also
                 * included in this.
                 */
                default:
                    if (RT_SUCCESS_NP(rc))
                    {
                        AssertMsgFailed(("Unexpected warning or informational status code %Rra!\n", rc));
                        rc = VERR_EM_INTERNAL_ERROR;
                    }
                    Log(("EMR3ExecuteVM: %Rrc: %d -> %d (EMSTATE_GURU_MEDITATION)\n", rc, enmOldState, EMSTATE_GURU_MEDITATION));
                    pVCpu->em.s.enmState = EMSTATE_GURU_MEDITATION;
                    break;
            }

            /*
             * Act on state transition.
             */
            EMSTATE const enmNewState = pVCpu->em.s.enmState;
            if (enmOldState != enmNewState)
            {
                VBOXVMM_EM_STATE_CHANGED(pVCpu, enmOldState, enmNewState, rc);

                /* Clear MWait flags and the unhalt FF. */
                if (   enmOldState == EMSTATE_HALTED
                    && (   (pVCpu->em.s.MWait.fWait & EMMWAIT_FLAG_ACTIVE)
                        || VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_UNHALT))
                    && (   enmNewState == EMSTATE_HM
                        || enmNewState == EMSTATE_NEM
                        || enmNewState == EMSTATE_RECOMPILER
                        || enmNewState == EMSTATE_DEBUG_GUEST_HM
                        || enmNewState == EMSTATE_DEBUG_GUEST_NEM
                        || enmNewState == EMSTATE_DEBUG_GUEST_IEM
                        || enmNewState == EMSTATE_DEBUG_GUEST_RECOMPILER) )
                {
                    if (pVCpu->em.s.MWait.fWait & EMMWAIT_FLAG_ACTIVE)
                    {
                        LogFlow(("EMR3ExecuteVM: Clearing MWAIT\n"));
                        pVCpu->em.s.MWait.fWait &= ~(EMMWAIT_FLAG_ACTIVE | EMMWAIT_FLAG_BREAKIRQIF0);
                    }
                    if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_UNHALT))
                    {
                        LogFlow(("EMR3ExecuteVM: Clearing UNHALT\n"));
                        VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_UNHALT);
                    }
                }
            }
            else
                VBOXVMM_EM_STATE_UNCHANGED(pVCpu, enmNewState, rc);

            STAM_PROFILE_ADV_STOP(&pVCpu->em.s.StatTotal, x); /* (skip this in release) */
            STAM_PROFILE_ADV_START(&pVCpu->em.s.StatTotal, x);

            /*
             * Act on the new state.
             */
            switch (enmNewState)
            {
                /*
                 * Execute hardware accelerated raw.
                 */
                case EMSTATE_HM:
#if defined(VBOX_VMM_TARGET_ARMV8)
                    AssertReleaseFailed(); /* Should never get here. */
#else
                    rc = emR3HmExecute(pVM, pVCpu, &fFFDone);
#endif
                    break;

                /*
                 * Execute hardware accelerated raw.
                 */
                case EMSTATE_NEM:
                    rc = VBOXSTRICTRC_TODO(emR3NemExecute(pVM, pVCpu, &fFFDone));
                    break;

                /*
                 * Execute recompiled.
                 */
                case EMSTATE_RECOMPILER:
                    rc = VBOXSTRICTRC_TODO(emR3RecompilerExecute(pVM, pVCpu, enmOldState == EMSTATE_HALTED, &fFFDone));
                    Log2(("EMR3ExecuteVM: emR3RecompilerExecute -> %Rrc\n", rc));
                    break;

                /*
                 * Execute in the interpreter.
                 */
                case EMSTATE_IEM:
                {
#if 0 /* For comparing HM and IEM (@bugref{10464}). */
                    PCPUMCTX const pCtx = &pVCpu->cpum.GstCtx;
                    PCX86FXSTATE const pX87 = &pCtx->XState.x87;
                    Log11(("eax=%08x ebx=%08x ecx=%08x edx=%08x esi=%08x edi=%08x\n"
                           "eip=%08x esp=%08x ebp=%08x eflags=%08x\n"
                           "cs=%04x ss=%04x ds=%04x es=%04x fs=%04x gs=%04x\n"
                           "fsw=%04x fcw=%04x ftw=%02x top=%u%s%s%s%s%s%s%s%s%s\n"
                           "st0=%.10Rhxs st1=%.10Rhxs st2=%.10Rhxs st3=%.10Rhxs\n"
                           "st4=%.10Rhxs st5=%.10Rhxs st6=%.10Rhxs st7=%.10Rhxs\n",
                           pCtx->eax, pCtx->ebx, pCtx->ecx, pCtx->edx, pCtx->edi, pCtx->edi,
                           pCtx->eip, pCtx->esp, pCtx->ebp, pCtx->eflags.u,
                           pCtx->cs.Sel, pCtx->ss.Sel, pCtx->ds.Sel, pCtx->es.Sel, pCtx->fs.Sel, pCtx->gs.Sel,
                           pX87->FSW, pX87->FCW, pX87->FTW, X86_FSW_TOP_GET(pX87->FSW),
                           pX87->FSW & X86_FSW_ES ? " ES!" : "",
                           pX87->FSW & X86_FSW_IE ? " IE" : "",
                           pX87->FSW & X86_FSW_DE ? " DE" : "",
                           pX87->FSW & X86_FSW_SF ? " SF" : "",
                           pX87->FSW & X86_FSW_B  ? " B!" : "",
                           pX87->FSW & X86_FSW_C0 ? " C0" : "",
                           pX87->FSW & X86_FSW_C1 ? " C1" : "",
                           pX87->FSW & X86_FSW_C2 ? " C2" : "",
                           pX87->FSW & X86_FSW_C3 ? " C3" : "",
                           &pX87->aRegs[/*X86_FSW_TOP_GET_ST(pVCpu->cpum.GstCtx.XState.x87.FSW,*/(0)],
                           &pX87->aRegs[/*X86_FSW_TOP_GET_ST(pVCpu->cpum.GstCtx.XState.x87.FSW,*/(1)],
                           &pX87->aRegs[/*X86_FSW_TOP_GET_ST(pVCpu->cpum.GstCtx.XState.x87.FSW,*/(2)],
                           &pX87->aRegs[/*X86_FSW_TOP_GET_ST(pVCpu->cpum.GstCtx.XState.x87.FSW,*/(3)],
                           &pX87->aRegs[/*X86_FSW_TOP_GET_ST(pVCpu->cpum.GstCtx.XState.x87.FSW,*/(4)],
                           &pX87->aRegs[/*X86_FSW_TOP_GET_ST(pVCpu->cpum.GstCtx.XState.x87.FSW,*/(5)],
                           &pX87->aRegs[/*X86_FSW_TOP_GET_ST(pVCpu->cpum.GstCtx.XState.x87.FSW,*/(6)],
                           &pX87->aRegs[/*X86_FSW_TOP_GET_ST(pVCpu->cpum.GstCtx.XState.x87.FSW,*/(7)]));
                    DBGFR3DisasInstrCurrentLogInternal(pVCpu, NULL);
#endif

                    uint32_t cInstructions = 0;
#if 0 /* For testing purposes. */
                    //STAM_PROFILE_START(&pVCpu->em.s.StatHmExec, x1);
                    rc = VBOXSTRICTRC_TODO(EMR3HmSingleInstruction(pVM, pVCpu, EM_ONE_INS_FLAGS_RIP_CHANGE));
                    //STAM_PROFILE_STOP(&pVCpu->em.s.StatHmExec, x1);
                    if (rc == VINF_EM_DBG_STEPPED || rc == VINF_EM_RESCHEDULE_EXEC_ENGINE || rc == VINF_EM_RESCHEDULE_REM)
                        rc = VINF_SUCCESS;
                    else if (rc == VERR_EM_CANNOT_EXEC_GUEST)
#endif
                        rc = VBOXSTRICTRC_TODO(IEMExecLots(pVCpu, 4096 /*cMaxInstructions*/, 2047 /*cPollRate*/, &cInstructions));
                    if (pVM->em.s.fIemExecutesAll)
                    {
                        Assert(rc != VINF_EM_RESCHEDULE_REM);
                        Assert(rc != VINF_EM_RESCHEDULE_EXEC_ENGINE);
#ifdef VBOX_HIGH_RES_TIMERS_HACK
                        if (cInstructions < 2048)
                            TMTimerPollVoid(pVM, pVCpu);
#endif
                    }
                    else if (rc == VINF_SUCCESS)
                        rc = VINF_EM_RESCHEDULE; /* Need to check whether we can run in HM or NEM again. */
#ifndef VBOX_VMM_TARGET_ARMV8
                    if (rc != VINF_EM_EMULATE_SPLIT_LOCK)
                    { /* likely */ }
                    else
                        rc = VBOXSTRICTRC_TODO(emR3ExecuteSplitLockInstruction(pVM, pVCpu));
#endif
                    fFFDone = false;
                    break;
                }

                /*
                 * Application processor execution halted until SIPI.
                 */
                case EMSTATE_WAIT_SIPI:
                    /* no break */
                /*
                 * hlt - execution halted until interrupt.
                 */
                case EMSTATE_HALTED:
                {
                    STAM_REL_PROFILE_START(&pVCpu->em.s.StatHalted, y);
                    /* If HM (or someone else) store a pending interrupt in
                       TRPM, it must be dispatched ASAP without any halting.
                       Anything pending in TRPM has been accepted and the CPU
                       should already be the right state to receive it. */
                    if (TRPMHasTrap(pVCpu))
                        rc = VINF_EM_RESCHEDULE;
#if !defined(VBOX_VMM_TARGET_ARMV8)
                    /* MWAIT has a special extension where it's woken up when
                       an interrupt is pending even when IF=0. */
                    else if (   (pVCpu->em.s.MWait.fWait & (EMMWAIT_FLAG_ACTIVE | EMMWAIT_FLAG_BREAKIRQIF0))
                             ==                            (EMMWAIT_FLAG_ACTIVE | EMMWAIT_FLAG_BREAKIRQIF0))
                    {
                        rc = VMR3WaitHalted(pVM, pVCpu, 0 /*fFlags*/);
                        if (rc == VINF_SUCCESS)
                        {
                            if (VMCPU_FF_TEST_AND_CLEAR(pVCpu, VMCPU_FF_UPDATE_APIC))
                                APICUpdatePendingInterrupts(pVCpu);

                            if (VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_INTERRUPT_PIC
                                                         | VMCPU_FF_INTERRUPT_NESTED_GUEST
                                                         | VMCPU_FF_INTERRUPT_NMI  | VMCPU_FF_INTERRUPT_SMI | VMCPU_FF_UNHALT))
                            {
                                Log(("EMR3ExecuteVM: Triggering reschedule on pending IRQ after MWAIT\n"));
                                rc = VINF_EM_RESCHEDULE;
                            }

                        }
                    }
#endif
                    else
                    {
#if defined(VBOX_VMM_TARGET_ARMV8)
                        const uint32_t fWaitHalted = 0; /* WFI/WFE always return when an interrupt happens. */
#else
                        const uint32_t fWaitHalted = (CPUMGetGuestEFlags(pVCpu) & X86_EFL_IF) ? 0 : VMWAITHALTED_F_IGNORE_IRQS;
#endif
                        rc = VMR3WaitHalted(pVM, pVCpu, fWaitHalted);
                        /* We're only interested in NMI/SMIs here which have their own FFs, so we don't need to
                           check VMCPU_FF_UPDATE_APIC here. */
                        if (   rc == VINF_SUCCESS
#if defined(VBOX_VMM_TARGET_ARMV8)
                            && VMCPU_FF_IS_ANY_SET(pVCpu,   VMCPU_FF_INTERRUPT_NMI | VMCPU_FF_INTERRUPT_SMI | VMCPU_FF_VTIMER_ACTIVATED
                                                          | VMCPU_FF_INTERRUPT_FIQ | VMCPU_FF_INTERRUPT_IRQ)
#else
                            && VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_INTERRUPT_NMI | VMCPU_FF_INTERRUPT_SMI | VMCPU_FF_UNHALT)
#endif
                            )
                        {
                            Log(("EMR3ExecuteVM: Triggering reschedule on pending NMI/SMI/UNHALT after HLT\n"));
                            rc = VINF_EM_RESCHEDULE;
                        }
                    }

                    STAM_REL_PROFILE_STOP(&pVCpu->em.s.StatHalted, y);
                    break;
                }

                /*
                 * Suspended - return to VM.cpp.
                 */
                case EMSTATE_SUSPENDED:
                    TMR3NotifySuspend(pVM, pVCpu);
                    STAM_REL_PROFILE_ADV_STOP(&pVCpu->em.s.StatTotal, x);
                    Log(("EMR3ExecuteVM: actually returns %Rrc (state %s / %s)\n", rc, emR3GetStateName(pVCpu->em.s.enmState), emR3GetStateName(enmOldState)));
                    return VINF_EM_SUSPEND;

                /*
                 * Debugging in the guest.
                 */
                case EMSTATE_DEBUG_GUEST_RAW:
                case EMSTATE_DEBUG_GUEST_HM:
                case EMSTATE_DEBUG_GUEST_NEM:
                case EMSTATE_DEBUG_GUEST_IEM:
                case EMSTATE_DEBUG_GUEST_RECOMPILER:
                    TMR3NotifySuspend(pVM, pVCpu);
                    rc = VBOXSTRICTRC_TODO(emR3Debug(pVM, pVCpu, rc));
                    TMR3NotifyResume(pVM, pVCpu);
                    Log2(("EMR3ExecuteVM: emR3Debug -> %Rrc (state %d)\n", rc, pVCpu->em.s.enmState));
                    break;

                /*
                 * Debugging in the hypervisor.
                 */
                case EMSTATE_DEBUG_HYPER:
                {
                    TMR3NotifySuspend(pVM, pVCpu);
                    STAM_REL_PROFILE_ADV_STOP(&pVCpu->em.s.StatTotal, x);

                    rc = VBOXSTRICTRC_TODO(emR3Debug(pVM, pVCpu, rc));
                    Log2(("EMR3ExecuteVM: emR3Debug -> %Rrc (state %d)\n", rc, pVCpu->em.s.enmState));
                    if (rc != VINF_SUCCESS)
                    {
                        if (rc == VINF_EM_OFF || rc == VINF_EM_TERMINATE)
                            pVCpu->em.s.enmState = EMSTATE_TERMINATING;
                        else
                        {
                            /* switch to guru meditation mode */
                            pVCpu->em.s.enmState = EMSTATE_GURU_MEDITATION;
                            VMR3SetGuruMeditation(pVM); /* This notifies the other EMTs. */
                            VMMR3FatalDump(pVM, pVCpu, rc);
                        }
                        Log(("EMR3ExecuteVM: actually returns %Rrc (state %s / %s)\n", rc, emR3GetStateName(pVCpu->em.s.enmState), emR3GetStateName(enmOldState)));
                        return rc;
                    }

                    STAM_REL_PROFILE_ADV_START(&pVCpu->em.s.StatTotal, x);
                    TMR3NotifyResume(pVM, pVCpu);
                    break;
                }

                /*
                 * Guru meditation takes place in the debugger.
                 */
                case EMSTATE_GURU_MEDITATION:
                {
                    TMR3NotifySuspend(pVM, pVCpu);
                    VMR3SetGuruMeditation(pVM); /* This notifies the other EMTs. */
                    VMMR3FatalDump(pVM, pVCpu, rc);
                    emR3Debug(pVM, pVCpu, rc);
                    STAM_REL_PROFILE_ADV_STOP(&pVCpu->em.s.StatTotal, x);
                    Log(("EMR3ExecuteVM: actually returns %Rrc (state %s / %s)\n", rc, emR3GetStateName(pVCpu->em.s.enmState), emR3GetStateName(enmOldState)));
                    return rc;
                }

                /*
                 * The states we don't expect here.
                 */
                case EMSTATE_NONE:
                case EMSTATE_RAW_OBSOLETE:
                case EMSTATE_IEM_THEN_REM_OBSOLETE:
                case EMSTATE_TERMINATING:
                default:
                    AssertMsgFailed(("EMR3ExecuteVM: Invalid state %d!\n", pVCpu->em.s.enmState));
                    pVCpu->em.s.enmState = EMSTATE_GURU_MEDITATION;
                    TMR3NotifySuspend(pVM, pVCpu);
                    STAM_REL_PROFILE_ADV_STOP(&pVCpu->em.s.StatTotal, x);
                    Log(("EMR3ExecuteVM: actually returns %Rrc (state %s / %s)\n", rc, emR3GetStateName(pVCpu->em.s.enmState), emR3GetStateName(enmOldState)));
                    return VERR_EM_INTERNAL_ERROR;
            }
        } /* The Outer Main Loop */
    }
    else
    {
        /*
         * Fatal error.
         */
        Log(("EMR3ExecuteVM: returns %Rrc because of longjmp / fatal error; (state %s / %s)\n", rc, emR3GetStateName(pVCpu->em.s.enmState), emR3GetStateName(pVCpu->em.s.enmPrevState)));
        TMR3NotifySuspend(pVM, pVCpu);
        VMR3SetGuruMeditation(pVM); /* This notifies the other EMTs. */
        VMMR3FatalDump(pVM, pVCpu, rc);
        emR3Debug(pVM, pVCpu, rc);
        STAM_REL_PROFILE_ADV_STOP(&pVCpu->em.s.StatTotal, x);
        /** @todo change the VM state! */
        return rc;
    }

    /* not reached */
}

