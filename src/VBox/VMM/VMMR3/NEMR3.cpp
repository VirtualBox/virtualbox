/* $Id: NEMR3.cpp 107891 2025-01-16 00:33:11Z knut.osmundsen@oracle.com $ */
/** @file
 * NEM - Native execution manager.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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

/** @page pg_nem NEM - Native Execution Manager.
 *
 * This is an alternative execution manage to HM and raw-mode.   On one host
 * (Windows) we're forced to use this, on the others we just do it because we
 * can.   Since this is host specific in nature, information about an
 * implementation is contained in the NEMR3Native-xxxx.cpp files.
 *
 * @ref pg_nem_win
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_NEM
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/nem.h>
#include <VBox/vmm/gim.h>
#include "NEMInternal.h"
#include <VBox/vmm/vm.h>
#include <VBox/vmm/vmcc.h>
#include <VBox/vmm/uvm.h>
#include <VBox/err.h>

#include <iprt/asm.h>
#include <iprt/string.h>



/**
 * Basic init and configuration reading.
 *
 * Always call NEMR3Term after calling this.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3_INT_DECL(int) NEMR3InitConfig(PVM pVM)
{
    LogFlow(("NEMR3Init\n"));

    /*
     * Assert alignment and sizes.
     */
    AssertCompileMemberAlignment(VM, nem.s, 64);
    AssertCompile(sizeof(pVM->nem.s) <= sizeof(pVM->nem.padding));

    /*
     * Initialize state info so NEMR3Term will always be happy.
     * No returning prior to setting magics!
     */
    pVM->nem.s.u32Magic = NEM_MAGIC;
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];
        pVCpu->nem.s.u32Magic = NEMCPU_MAGIC;
    }

    /*
     * Read configuration.
     */
    PCFGMNODE pCfgNem = CFGMR3GetChild(CFGMR3GetRoot(pVM), "NEM/");

    /*
     * Validate the NEM settings.
     */
    int rc = CFGMR3ValidateConfig(pCfgNem,
                                  "/NEM/",
                                  "Enabled"
                                  "|Allow64BitGuests"
                                  "|LovelyMesaDrvWorkaround"
#ifdef RT_OS_WINDOWS
                                  "|UseRing0Runloop"
#elif defined(RT_OS_DARWIN)
                                  "|VmxPleGap"
                                  "|VmxPleWindow"
                                  "|VmxLbr"
#endif
#if defined(VBOX_VMM_TARGET_ARMV8)
                                  "|VTimerInterrupt"
#endif
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
                                  "|IBPBOnVMExit"
                                  "|IBPBOnVMEntry"
                                  "|L1DFlushOnSched"
                                  "|L1DFlushOnVMEntry"
                                  "|MDSClearOnSched"
                                  "|MDSClearOnVMEntry"
#endif
                                  ,
                                  "" /* pszValidNodes */, "NEM" /* pszWho */, 0 /* uInstance */);
    if (RT_FAILURE(rc))
        return rc;

    /** @cfgm{/NEM/NEMEnabled, bool, true}
     * Whether NEM is enabled. */
    rc = CFGMR3QueryBoolDef(pCfgNem, "Enabled", &pVM->nem.s.fEnabled, true);
    AssertLogRelRCReturn(rc, rc);


#ifdef VBOX_WITH_64_BITS_GUESTS
    /** @cfgm{/NEM/Allow64BitGuests, bool, 32-bit:false, 64-bit:true}
     * Enables AMD64 CPU features.
     * On 32-bit hosts this isn't default and require host CPU support. 64-bit hosts
     * already have the support. */
    rc = CFGMR3QueryBoolDef(pCfgNem, "Allow64BitGuests", &pVM->nem.s.fAllow64BitGuests, HC_ARCH_BITS == 64);
    AssertLogRelRCReturn(rc, rc);
#else
    pVM->nem.s.fAllow64BitGuests = false;
#endif

    /** @cfgm{/NEM/LovelyMesaDrvWorkaround, bool, false}
     * Workaround for mesa vmsvga 3d driver making incorrect assumptions about
     * the hypervisor it is running under. */
    bool f;
    rc = CFGMR3QueryBoolDef(pCfgNem, "LovelyMesaDrvWorkaround", &f, false);
    AssertLogRelRCReturn(rc, rc);
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];
        pVCpu->nem.s.fTrapXcptGpForLovelyMesaDrv = f;
    }

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    /** @cfgm{/NEM/IBPBOnVMExit, bool}
     * Costly paranoia setting. */
    rc = CFGMR3QueryBoolDef(pCfgNem, "IBPBOnVMExit", &f, false);
    AssertLogRelRCReturn(rc, rc);
    VMCC_FOR_EACH_VMCPU_STMT(pVM, pVCpu->nem.s.fIbpbOnVmExit = f);

    /** @cfgm{/NEM/IBPBOnVMEntry, bool}
     * Costly paranoia setting. */
    rc = CFGMR3QueryBoolDef(pCfgNem, "IBPBOnVMEntry", &f, false);
    AssertLogRelRCReturn(rc, rc);
    VMCC_FOR_EACH_VMCPU_STMT(pVM, pVCpu->nem.s.fIbpbOnVmEntry = f);

    /** @cfgm{/NEM/L1DFlushOnSched, bool, true}
     * CVE-2018-3646 workaround, ignored on CPUs that aren't affected. */
    rc = CFGMR3QueryBoolDef(pCfgNem, "L1DFlushOnSched", &f, true);
    AssertLogRelRCReturn(rc, rc);
    VMCC_FOR_EACH_VMCPU_STMT(pVM, pVCpu->nem.s.fL1dFlushOnSched = f);

    /** @cfgm{/NEM/L1DFlushOnVMEntry, bool}
     * CVE-2018-3646 workaround, ignored on CPUs that aren't affected. */
    rc = CFGMR3QueryBoolDef(pCfgNem, "L1DFlushOnVMEntry", &f, false);
    AssertLogRelRCReturn(rc, rc);
    VMCC_FOR_EACH_VMCPU_STMT(pVM, pVCpu->nem.s.fL1dFlushOnVmEntry = f);

    /* Disable L1DFlushOnSched if L1DFlushOnVMEntry is enabled. */
    PVMCPU const pVCpu0 = pVM->apCpusR3[0];
    if (pVCpu0->nem.s.fL1dFlushOnVmEntry)
        VMCC_FOR_EACH_VMCPU_STMT(pVM, pVCpu->nem.s.fL1dFlushOnSched = false);

    /** @cfgm{/NEM/MDSClearOnSched, bool, true}
     * CVE-2018-12126, CVE-2018-12130, CVE-2018-12127, CVE-2019-11091 workaround,
     * ignored on CPUs that aren't affected. */
    rc = CFGMR3QueryBoolDef(pCfgNem, "MDSClearOnSched", &f, true);
    AssertLogRelRCReturn(rc, rc);
    VMCC_FOR_EACH_VMCPU_STMT(pVM, pVCpu->nem.s.fMdsClearOnSched = f);

    /** @cfgm{/NEM/MDSClearOnVmEntry, bool, false}
     * CVE-2018-12126, CVE-2018-12130, CVE-2018-12127, CVE-2019-11091 workaround,
     * ignored on CPUs that aren't affected. */
    rc = CFGMR3QueryBoolDef(pCfgNem, "MDSClearOnVmEntry", &f, false);
    AssertLogRelRCReturn(rc, rc);
    VMCC_FOR_EACH_VMCPU_STMT(pVM, pVCpu->nem.s.fMdsClearOnVmEntry = f);

    /* Disable MDSClearOnSched if MDSClearOnVmEntry is enabled. */
    if (pVCpu0->nem.s.fMdsClearOnVmEntry)
        VMCC_FOR_EACH_VMCPU_STMT(pVM, pVCpu->nem.s.fMdsClearOnSched = false);
#endif /* VBOX_VMM_TARGET_X86 */

#if defined(VBOX_VMM_TARGET_ARMV8)
    /** @cfgm{/NEM/VTimerInterrupt, uint32_t}
     * Specifies the interrupt identifier for the VTimer. */
    rc = CFGMR3QueryU32(pCfgNem, "VTimerInterrupt", &pVM->nem.s.u32GicPpiVTimer);
    AssertLogRelRCReturn(rc, rc);
#endif

    return VINF_SUCCESS;
}


/**
 * This is called by HMR3Init() when HM cannot be used.
 *
 * Sets VM::bMainExecutionEngine to VM_EXEC_ENGINE_NATIVE_API if we can use a
 * native hypervisor API to execute the VM.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   fFallback   Whether this is a fallback call.  Cleared if the VM is
 *                      configured to use NEM instead of HM.
 * @param   fForced     Whether /HM/HMForced was set.  If set and we fail to
 *                      enable NEM, we'll return a failure status code.
 *                      Otherwise we'll assume HMR3Init falls back on raw-mode.
 */
VMMR3_INT_DECL(int) NEMR3Init(PVM pVM, bool fFallback, bool fForced)
{
    Assert(pVM->bMainExecutionEngine != VM_EXEC_ENGINE_NATIVE_API);
    int rc;
    if (pVM->nem.s.fEnabled)
    {
#ifdef VBOX_WITH_NATIVE_NEM
        rc = nemR3NativeInit(pVM, fFallback, fForced);
        ASMCompilerBarrier(); /* May have changed bMainExecutionEngine. */
#else
        RT_NOREF(fFallback);
        rc = VINF_SUCCESS;
#endif
        if (RT_SUCCESS(rc))
        {
            if (pVM->bMainExecutionEngine == VM_EXEC_ENGINE_NATIVE_API)
            {
#ifdef RT_OS_WINDOWS /* The WHv* API is extremely slow at handling VM exits. The AppleHv and
                        KVM APIs are much faster, thus the different mode name. :-) */
                LogRel(("NEM:\n"
                        "NEM: NEMR3Init: Snail execution mode is active!\n"
                        "NEM: Note! VirtualBox is not able to run at its full potential in this execution mode.\n"
                        "NEM:       To see VirtualBox run at max speed you need to disable all Windows features\n"
                        "NEM:       making use of Hyper-V.  That is a moving target, so google how and carefully\n"
                        "NEM:       consider the consequences of disabling these features.\n"
                        "NEM:\n"));
#else
                LogRel(("NEM:\n"
                        "NEM: NEMR3Init: Turtle execution mode is active!\n"
                        "NEM: Note! VirtualBox is not able to run at its full potential in this execution mode.\n"
                        "NEM:\n"));
#endif
            }
            else
            {
                LogRel(("NEM: NEMR3Init: Not available.\n"));
                if (fForced)
                    rc = VERR_NEM_NOT_AVAILABLE;
            }
        }
        else
            LogRel(("NEM: NEMR3Init: Native init failed: %Rrc.\n", rc));
    }
    else
    {
        LogRel(("NEM: NEMR3Init: Disabled.\n"));
        rc = fForced ? VERR_NEM_NOT_ENABLED : VINF_SUCCESS;
    }
    return rc;
}


#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
/**
 * Finalize configuration related to spectre and such.
 *
 * This can be called explicitly by the native code after it has seeded the
 * necessary host information to CPUM, or/and it will be called by
 * NEMR3InitAfterCPUM().
 *
 * @note This code is also duplicated in hmR3InitFinalizeR3().
 */
DECLHIDDEN(void) nemR3InitFinalizeSpecCtrl(PVM pVM)
{
    /*
     * Check if L1D flush is needed/possible.
     */
    if (   !g_CpumHostFeatures.s.fFlushCmd
        || g_CpumHostFeatures.s.enmMicroarch <  kCpumMicroarch_Intel_Core7_Nehalem
        || g_CpumHostFeatures.s.enmMicroarch >= kCpumMicroarch_Intel_Core7_End
        || g_CpumHostFeatures.s.fArchVmmNeedNotFlushL1d
        || g_CpumHostFeatures.s.fArchRdclNo)
        VMCC_FOR_EACH_VMCPU_STMT(pVM, pVCpu->nem.s.fL1dFlushOnSched = pVCpu->nem.s.fL1dFlushOnVmEntry = false);

    /*
     * Check if MDS flush is needed/possible.
     * On atoms and knight family CPUs, we will only allow clearing on scheduling.
     */
    if (   !g_CpumHostFeatures.s.fMdsClear
        || g_CpumHostFeatures.s.fArchMdsNo)
        VMCC_FOR_EACH_VMCPU_STMT(pVM, pVCpu->nem.s.fMdsClearOnSched = pVCpu->nem.s.fMdsClearOnVmEntry = false);
    else if (   (   g_CpumHostFeatures.s.enmMicroarch >=  kCpumMicroarch_Intel_Atom_Airmount
                 && g_CpumHostFeatures.s.enmMicroarch <   kCpumMicroarch_Intel_Atom_End)
             || (   g_CpumHostFeatures.s.enmMicroarch >=  kCpumMicroarch_Intel_Phi_KnightsLanding
                 && g_CpumHostFeatures.s.enmMicroarch <   kCpumMicroarch_Intel_Phi_End))
    {
        PVMCPU const pVCpu0 = pVM->apCpusR3[0];
        if (!pVCpu0->nem.s.fMdsClearOnSched)
            VMCC_FOR_EACH_VMCPU_STMT(pVM, pVCpu->nem.s.fMdsClearOnSched = pVCpu->nem.s.fMdsClearOnVmEntry);
        VMCC_FOR_EACH_VMCPU_STMT(pVM, pVCpu->nem.s.fMdsClearOnVmEntry = false);
    }
    else if (   g_CpumHostFeatures.s.enmMicroarch <  kCpumMicroarch_Intel_Core7_Nehalem
             || g_CpumHostFeatures.s.enmMicroarch >= kCpumMicroarch_Intel_Core7_End)
        VMCC_FOR_EACH_VMCPU_STMT(pVM, pVCpu->nem.s.fMdsClearOnSched = pVCpu->nem.s.fMdsClearOnVmEntry = false);
}
#endif


/**
 * Perform initialization that depends on CPUM working.
 *
 * This is a noop if NEM wasn't activated by a previous NEMR3Init() call.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3_INT_DECL(int) NEMR3InitAfterCPUM(PVM pVM)
{
    int rc = VINF_SUCCESS;
    if (pVM->bMainExecutionEngine == VM_EXEC_ENGINE_NATIVE_API)
    {
        /*
         * Do native after-CPUM init.
         */
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
        nemR3InitFinalizeSpecCtrl(pVM);
#endif
#ifdef VBOX_WITH_NATIVE_NEM
        rc = nemR3NativeInitAfterCPUM(pVM);
#else
        RT_NOREF(pVM);
#endif
    }
    return rc;
}


/**
 * Called when a init phase has completed.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   enmWhat     The phase that completed.
 */
VMMR3_INT_DECL(int) NEMR3InitCompleted(PVM pVM, VMINITCOMPLETED enmWhat)
{
    /*
     * Check if GIM needs #UD, since that applies to everyone.
     */
    if (enmWhat == VMINITCOMPLETED_RING3)
        for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
        {
            PVMCPU pVCpu = pVM->apCpusR3[idCpu];
            pVCpu->nem.s.fGIMTrapXcptUD = GIMShouldTrapXcptUD(pVCpu);
        }

    /*
     * Call native code.
     */
    int rc = VINF_SUCCESS;
#ifdef VBOX_WITH_NATIVE_NEM
    if (pVM->bMainExecutionEngine == VM_EXEC_ENGINE_NATIVE_API)
        rc = nemR3NativeInitCompleted(pVM, enmWhat);
#else
    RT_NOREF(pVM, enmWhat);
#endif
    return rc;
}


/**
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3_INT_DECL(int) NEMR3Term(PVM pVM)
{
    AssertReturn(pVM->nem.s.u32Magic == NEM_MAGIC, VERR_WRONG_ORDER);
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
        AssertReturn(pVM->apCpusR3[idCpu]->nem.s.u32Magic == NEMCPU_MAGIC, VERR_WRONG_ORDER);

    /* Do native termination. */
    int rc = VINF_SUCCESS;
#ifdef VBOX_WITH_NATIVE_NEM
    if (pVM->bMainExecutionEngine == VM_EXEC_ENGINE_NATIVE_API)
        rc = nemR3NativeTerm(pVM);
#endif

    /* Mark it as terminated. */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];
        pVCpu->nem.s.u32Magic = NEMCPU_MAGIC_DEAD;
    }
    pVM->nem.s.u32Magic = NEM_MAGIC_DEAD;
    return rc;
}

/**
 * External interface for querying whether native execution API is used.
 *
 * @returns true if NEM is being used, otherwise false.
 * @param   pUVM        The user mode VM handle.
 * @sa      HMR3IsEnabled
 */
VMMR3DECL(bool) NEMR3IsEnabled(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, false);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, false);
    return VM_IS_NEM_ENABLED(pVM);
}


/**
 * The VM is being reset.
 *
 * @param   pVM     The cross context VM structure.
 */
VMMR3_INT_DECL(void) NEMR3Reset(PVM pVM)
{
#ifdef VBOX_WITH_NATIVE_NEM
    if (pVM->bMainExecutionEngine == VM_EXEC_ENGINE_NATIVE_API)
        nemR3NativeReset(pVM);
#else
    RT_NOREF(pVM);
#endif
}


/**
 * Resets a virtual CPU.
 *
 * Used to bring up secondary CPUs on SMP as well as CPU hot plugging.
 *
 * @param   pVCpu       The cross context virtual CPU structure to reset.
 * @param   fInitIpi    Set if being reset due to INIT IPI.
 */
VMMR3_INT_DECL(void) NEMR3ResetCpu(PVMCPU pVCpu, bool fInitIpi)
{
#ifdef VBOX_WITH_NATIVE_NEM
    if (pVCpu->pVMR3->bMainExecutionEngine == VM_EXEC_ENGINE_NATIVE_API)
        nemR3NativeResetCpu(pVCpu, fInitIpi);
#else
    RT_NOREF(pVCpu, fInitIpi);
#endif
}


/**
 * Indicates to TM that TMTSCMODE_NATIVE_API should be used for TSC.
 *
 * @returns true if TMTSCMODE_NATIVE_API must be used, otherwise @c false.
 * @param   pVM     The cross context VM structure.
 */
VMMR3_INT_DECL(bool) NEMR3NeedSpecialTscMode(PVM pVM)
{
#ifdef VBOX_WITH_NATIVE_NEM
    if (VM_IS_NEM_ENABLED(pVM))
        return true;
#else
    RT_NOREF(pVM);
#endif
    return false;
}


/**
 * Gets the name of a generic NEM exit code.
 *
 * @returns Pointer to read only string if @a uExit is known, otherwise NULL.
 * @param   uExit               The NEM exit to name.
 */
VMMR3DECL(const char *) NEMR3GetExitName(uint32_t uExit)
{
    switch ((NEMEXITTYPE)uExit)
    {
        case NEMEXITTYPE_INTTERRUPT_WINDOW:             return "NEM interrupt window";
        case NEMEXITTYPE_HALT:                          return "NEM halt";

        case NEMEXITTYPE_UNRECOVERABLE_EXCEPTION:       return "NEM unrecoverable exception";
        case NEMEXITTYPE_INVALID_VP_REGISTER_VALUE:     return "NEM invalid vp register value";
        case NEMEXITTYPE_XCPT_UD:                       return "NEM #UD";
        case NEMEXITTYPE_XCPT_DB:                       return "NEM #DB";
        case NEMEXITTYPE_XCPT_BP:                       return "NEM #BP";
        case NEMEXITTYPE_CANCELED:                      return "NEM canceled";
        case NEMEXITTYPE_MEMORY_ACCESS:                 return "NEM memory access";

        case NEMEXITTYPE_INTERNAL_ERROR_EMULATION:      return "NEM emulation IPE";
        case NEMEXITTYPE_INTERNAL_ERROR_FATAL:          return "NEM fatal IPE";
        case NEMEXITTYPE_INTERRUPTED:                   return "NEM interrupted";
        case NEMEXITTYPE_FAILED_ENTRY:                  return "NEM failed VT-x/AMD-V entry";

        case NEMEXITTYPE_INVALID:
        case NEMEXITTYPE_END:
            break;
    }

    return NULL;
}


VMMR3_INT_DECL(VBOXSTRICTRC) NEMR3RunGC(PVM pVM, PVMCPU pVCpu)
{
    Assert(VM_IS_NEM_ENABLED(pVM));
#ifdef VBOX_WITH_NATIVE_NEM
    return nemR3NativeRunGC(pVM, pVCpu);
#else
    NOREF(pVM); NOREF(pVCpu);
    return VERR_INTERNAL_ERROR_3;
#endif
}


#ifndef VBOX_WITH_NATIVE_NEM
VMMR3_INT_DECL(bool) NEMR3CanExecuteGuest(PVM pVM, PVMCPU pVCpu)
{
    RT_NOREF(pVM, pVCpu);
    return false;
}
#endif


VMMR3_INT_DECL(bool) NEMR3SetSingleInstruction(PVM pVM, PVMCPU pVCpu, bool fEnable)
{
    Assert(VM_IS_NEM_ENABLED(pVM));
#ifdef VBOX_WITH_NATIVE_NEM
    return nemR3NativeSetSingleInstruction(pVM, pVCpu, fEnable);
#else
    NOREF(pVM); NOREF(pVCpu); NOREF(fEnable);
    return false;
#endif
}


VMMR3_INT_DECL(void) NEMR3NotifyFF(PVM pVM, PVMCPU pVCpu, uint32_t fFlags)
{
    AssertLogRelReturnVoid(VM_IS_NEM_ENABLED(pVM));
#ifdef VBOX_WITH_NATIVE_NEM
    nemR3NativeNotifyFF(pVM, pVCpu, fFlags);
#else
    RT_NOREF(pVM, pVCpu, fFlags);
#endif
}

#ifndef VBOX_WITH_NATIVE_NEM

VMMR3_INT_DECL(void) NEMR3NotifySetA20(PVMCPU pVCpu, bool fEnabled)
{
    RT_NOREF(pVCpu, fEnabled);
}

# ifdef VBOX_WITH_PGM_NEM_MODE

VMMR3_INT_DECL(bool) NEMR3IsMmio2DirtyPageTrackingSupported(PVM pVM)
{
    RT_NOREF(pVM);
    return false;
}


VMMR3_INT_DECL(int)  NEMR3PhysMmio2QueryAndResetDirtyBitmap(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, uint32_t uNemRange,
                                                            void *pvBitmap, size_t cbBitmap)
{
    RT_NOREF(pVM, GCPhys, cb, uNemRange, pvBitmap, cbBitmap);
    AssertFailed();
    return VERR_INTERNAL_ERROR_2;
}


VMMR3_INT_DECL(int)  NEMR3NotifyPhysMmioExMapEarly(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, uint32_t fFlags,
                                                   void *pvRam, void *pvMmio2, uint8_t *pu2State, uint32_t *puNemRange)
{
    RT_NOREF(pVM, GCPhys, cb, fFlags, pvRam, pvMmio2, pu2State, puNemRange);
    AssertFailed();
    return VERR_INTERNAL_ERROR_2;
}

# endif /* VBOX_WITH_PGM_NEM_MODE */
#endif /* !VBOX_WITH_NATIVE_NEM */

/**
 * Notification callback from DBGF when interrupt breakpoints or generic debug
 * event settings changes.
 *
 * DBGF will call NEMR3NotifyDebugEventChangedPerCpu on each CPU afterwards, this
 * function is just updating the VM globals.
 *
 * @param   pVM         The VM cross context VM structure.
 * @thread  EMT(0)
 */
VMMR3_INT_DECL(void) NEMR3NotifyDebugEventChanged(PVM pVM)
{
    AssertLogRelReturnVoid(VM_IS_NEM_ENABLED(pVM));

#ifdef VBOX_WITH_NATIVE_NEM
    /* Interrupts. */
    bool fUseDebugLoop = pVM->dbgf.ro.cSoftIntBreakpoints > 0
                      || pVM->dbgf.ro.cHardIntBreakpoints > 0;

    /* CPU Exceptions. */
    for (DBGFEVENTTYPE enmEvent = DBGFEVENT_XCPT_FIRST;
         !fUseDebugLoop && enmEvent <= DBGFEVENT_XCPT_LAST;
         enmEvent = (DBGFEVENTTYPE)(enmEvent + 1))
        fUseDebugLoop = DBGF_IS_EVENT_ENABLED(pVM, enmEvent);

    /* Common VM exits. */
    for (DBGFEVENTTYPE enmEvent = DBGFEVENT_EXIT_FIRST;
         !fUseDebugLoop && enmEvent <= DBGFEVENT_EXIT_LAST_COMMON;
         enmEvent = (DBGFEVENTTYPE)(enmEvent + 1))
        fUseDebugLoop = DBGF_IS_EVENT_ENABLED(pVM, enmEvent);

    /* Done. */
    pVM->nem.s.fUseDebugLoop = nemR3NativeNotifyDebugEventChanged(pVM, fUseDebugLoop);
#else
    RT_NOREF(pVM);
#endif
}


/**
 * Follow up notification callback to NEMR3NotifyDebugEventChanged for each CPU.
 *
 * NEM uses this to combine the decision made NEMR3NotifyDebugEventChanged with
 * per CPU settings.
 *
 * @param   pVM         The VM cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 */
VMMR3_INT_DECL(void) NEMR3NotifyDebugEventChangedPerCpu(PVM pVM, PVMCPU pVCpu)
{
    AssertLogRelReturnVoid(VM_IS_NEM_ENABLED(pVM));

#ifdef VBOX_WITH_NATIVE_NEM
    pVCpu->nem.s.fUseDebugLoop = nemR3NativeNotifyDebugEventChangedPerCpu(pVM, pVCpu,
                                                                          pVCpu->nem.s.fSingleInstruction | pVM->nem.s.fUseDebugLoop);
#else
    RT_NOREF(pVM, pVCpu);
#endif
}


/**
 * Disables a CPU ISA extension, like MONITOR/MWAIT.
 *
 * @returns VBox status code
 * @param   pVM             The cross context VM structure.
 * @param   pszIsaExt       The ISA extension name in the config tree.
 */
int nemR3DisableCpuIsaExt(PVM pVM, const char *pszIsaExt)
{
    /*
     * Get IsaExts config node under CPUM.
     */
    PCFGMNODE pIsaExts = CFGMR3GetChild(CFGMR3GetRoot(pVM), "/CPUM/IsaExts");
    if (!pIsaExts)
    {
        int rc = CFGMR3InsertNode(CFGMR3GetRoot(pVM), "/CPUM/IsaExts", &pIsaExts);
        AssertLogRelMsgReturn(RT_SUCCESS(rc), ("CFGMR3InsertNode: rc=%Rrc pszIsaExt=%s\n", rc, pszIsaExt), rc);
    }

    /*
     * Look for a value by the given name (pszIsaExt).
     */
    /* Integer values 1 (CPUMISAEXTCFG_ENABLED_SUPPORTED) and 9 (CPUMISAEXTCFG_ENABLED_PORTABLE) will be replaced. */
    uint64_t u64Value;
    int rc = CFGMR3QueryInteger(pIsaExts, pszIsaExt, &u64Value);
    if (RT_SUCCESS(rc))
    {
        if (u64Value != 1 && u64Value != 9)
        {
            LogRel(("NEM: Not disabling IsaExt '%s', already configured with int value %lld\n", pszIsaExt, u64Value));
            return VINF_SUCCESS;
        }
        CFGMR3RemoveValue(pIsaExts, pszIsaExt);
    }
    /* String value 'default', 'enabled' and 'portable' will be replaced. */
    else if (rc == VERR_CFGM_NOT_INTEGER)
    {
        char szValue[32];
        rc = CFGMR3QueryString(pIsaExts, pszIsaExt, szValue, sizeof(szValue));
        AssertRCReturn(rc, VINF_SUCCESS);

        if (   RTStrICmpAscii(szValue, "default")  != 0
            && RTStrICmpAscii(szValue, "def")      != 0
            && RTStrICmpAscii(szValue, "enabled")  != 0
            && RTStrICmpAscii(szValue, "enable")   != 0
            && RTStrICmpAscii(szValue, "on")       != 0
            && RTStrICmpAscii(szValue, "yes")      != 0
            && RTStrICmpAscii(szValue, "portable") != 0)
        {
            LogRel(("NEM: Not disabling IsaExt '%s', already configured with string value '%s'\n", pszIsaExt, szValue));
            return VINF_SUCCESS;
        }
        CFGMR3RemoveValue(pIsaExts, pszIsaExt);
    }
    else
        AssertLogRelMsgReturn(rc == VERR_CFGM_VALUE_NOT_FOUND, ("CFGMR3QueryInteger: rc=%Rrc pszIsaExt=%s\n", rc, pszIsaExt),
                              VERR_NEM_IPE_8);

    /*
     * Insert the disabling value.
     */
    rc = CFGMR3InsertInteger(pIsaExts, pszIsaExt, 0 /* disabled */);
    AssertLogRelMsgReturn(RT_SUCCESS(rc), ("CFGMR3InsertInteger: rc=%Rrc pszIsaExt=%s\n", rc, pszIsaExt), rc);

    return VINF_SUCCESS;
}

