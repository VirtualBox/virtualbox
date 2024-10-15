/* $Id: HM-armv8.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * HM - VM Hardware Support Manager, ARMv8 shim.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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

/** @page pg_hm_armv8     HM - Hardware Assisted Virtualization Manager
 *
 * This is just a stub for ARMv8 which is bound to use NEM exclusively for the time being.
 * But to not mess up the upper layers too much for now this is really tiny shim which takes
 * of initializing NEM like it is done on AMD64.
 *
 * @sa @ref grp_hm
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_HM
#define VMCPU_INCL_CPUM_GST_CTX
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/ssm.h>
#include <VBox/vmm/gim.h>
#include <VBox/vmm/gcm.h>
#include <VBox/vmm/trpm.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/iom.h>
#include <VBox/vmm/iem.h>
#include <VBox/vmm/selm.h>
#include <VBox/vmm/nem.h>
#include <VBox/vmm/hm_vmx.h>
#include <VBox/vmm/hm_svm.h>
#include "HMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/err.h>
#include <VBox/param.h>

#include <iprt/assert.h>
#include <VBox/log.h>
#include <iprt/asm.h>
#include <iprt/env.h>
#include <iprt/thread.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int                hmR3InitFinalizeR3(PVM pVM);
static int                hmR3TermCPU(PVM pVM);


/**
 * Initializes the HM.
 *
 * This is the very first component to really do init after CFGM so that we can
 * establish the predominant execution engine for the VM prior to initializing
 * other modules.  It takes care of NEM initialization if needed (HM disabled or
 * not available in HW).
 *
 * If VT-x or AMD-V hardware isn't available, HM will try fall back on a native
 * hypervisor API via NEM, and then back on raw-mode if that isn't available
 * either.  The fallback to raw-mode will not happen if /HM/HMForced is set
 * (like for guest using SMP or 64-bit as well as for complicated guest like OS
 * X, OS/2 and others).
 *
 * Note that a lot of the set up work is done in ring-0 and thus postponed till
 * the ring-3 and ring-0 callback to HMR3InitCompleted.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 *
 * @remarks Be careful with what we call here, since most of the VMM components
 *          are uninitialized.
 */
VMMR3_INT_DECL(int) HMR3Init(PVM pVM)
{
    LogFlowFunc(("\n"));

    /*
     * Assert alignment and sizes.
     */
    AssertCompileMemberAlignment(VM, hm.s, 32);
    AssertCompile(sizeof(pVM->hm.s) <= sizeof(pVM->hm.padding));

    /*
     * Read configuration.
     */
    PCFGMNODE pCfgHm = CFGMR3GetChild(CFGMR3GetRoot(pVM), "HM/");

    /*
     * Validate the HM settings.
     */
#if 0
    int rc = CFGMR3ValidateConfig(pCfgHm, "/HM/",
                                  "|FallbackToIEM"
                                  , "" /* pszValidNodes */, "HM" /* pszWho */, 0 /* uInstance */);
    if (RT_FAILURE(rc))
        return rc;
#else
    int rc;
#endif

    AssertRelease(!pVM->fHMEnabled);

    /** @cfgm{/HM/FallbackToIEM, bool, false on AMD64 else true }
     * Enables fallback on NEM. */
    bool fFallbackToIEM = true;
    rc = CFGMR3QueryBoolDef(pCfgHm, "FallbackToIEM", &fFallbackToIEM, true);
    AssertRCReturn(rc, rc);

    /*
     * Disabled HM mean raw-mode, unless NEM is supposed to be used.
     */
    rc = NEMR3Init(pVM, false /*fFallback*/, true);
    ASMCompilerBarrier(); /* NEMR3Init may have changed bMainExecutionEngine. */
    if (RT_SUCCESS(rc))
    {
        /* For some reason, HM is in charge or large pages. Make sure to enable them: */
        //PGMSetLargePageUsage(pVM, pVM->hm.s.fLargePages);
    }
    else if (!fFallbackToIEM || rc != VERR_NEM_NOT_AVAILABLE)
        return rc;

    if (fFallbackToIEM && rc == VERR_NEM_NOT_AVAILABLE)
    {
        LogRel(("HM: HMR3Init: Falling back on IEM: NEM not available"));
        VM_SET_MAIN_EXECUTION_ENGINE(pVM, VM_EXEC_ENGINE_IEM);
#ifdef VBOX_WITH_PGM_NEM_MODE
        PGMR3EnableNemMode(pVM);
#endif
    }

    if (   pVM->bMainExecutionEngine == VM_EXEC_ENGINE_NOT_SET
        || pVM->bMainExecutionEngine == VM_EXEC_ENGINE_HW_VIRT /* paranoia */)
        return VM_SET_ERROR(pVM, rc, "Misconfigured VM: No guest execution engine available!");

    Assert(pVM->bMainExecutionEngine != VM_EXEC_ENGINE_NOT_SET);
    return VINF_SUCCESS;
}


/**
 * Initializes HM components after ring-3 phase has been fully initialized.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
static int hmR3InitFinalizeR3(PVM pVM)
{
    LogFlowFunc(("\n"));
    RT_NOREF(pVM);
    return VINF_SUCCESS;
}


/**
 * Called when a init phase has completed.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   enmWhat             The phase that completed.
 */
VMMR3_INT_DECL(int) HMR3InitCompleted(PVM pVM, VMINITCOMPLETED enmWhat)
{
    switch (enmWhat)
    {
        case VMINITCOMPLETED_RING3:
            return hmR3InitFinalizeR3(pVM);
        default:
            return VINF_SUCCESS;
    }
}


/**
 * Applies relocations to data and code managed by this
 * component. This function will be called at init and
 * whenever the VMM need to relocate it self inside the GC.
 *
 * @param   pVM     The cross context VM structure.
 */
VMMR3_INT_DECL(void) HMR3Relocate(PVM pVM)
{
    RT_NOREF(pVM);
}


/**
 * Terminates the HM.
 *
 * Termination means cleaning up and freeing all resources,
 * the VM itself is, at this point, powered off or suspended.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3_INT_DECL(int) HMR3Term(PVM pVM)
{
    hmR3TermCPU(pVM);
    return VINF_SUCCESS;
}


/**
 * Terminates the per-VCPU HM.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
static int hmR3TermCPU(PVM pVM)
{
    RT_NOREF(pVM);
    return VINF_SUCCESS;
}


/**
 * Resets a virtual CPU.
 *
 * Used by HMR3Reset and CPU hot plugging.
 *
 * @param   pVCpu   The cross context virtual CPU structure to reset.
 */
VMMR3_INT_DECL(void) HMR3ResetCpu(PVMCPU pVCpu)
{
    RT_NOREF(pVCpu);
}


/**
 * The VM is being reset.
 *
 * For the HM component this means that any GDT/LDT/TSS monitors
 * needs to be removed.
 *
 * @param   pVM     The cross context VM structure.
 */
VMMR3_INT_DECL(void) HMR3Reset(PVM pVM)
{
    LogFlow(("HMR3Reset:\n"));
    RT_NOREF(pVM);
}


/**
 * Enable patching in a VT-x/AMD-V guest
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pPatchMem   Patch memory range.
 * @param   cbPatchMem  Size of the memory range.
 */
VMMR3_INT_DECL(int)  HMR3EnablePatching(PVM pVM, RTGCPTR pPatchMem, unsigned cbPatchMem)
{
    AssertReleaseFailed();
    RT_NOREF(pVM, pPatchMem, cbPatchMem);
    return VERR_NOT_SUPPORTED;
}


/**
 * Disable patching in a VT-x/AMD-V guest.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pPatchMem   Patch memory range.
 * @param   cbPatchMem  Size of the memory range.
 */
VMMR3_INT_DECL(int)  HMR3DisablePatching(PVM pVM, RTGCPTR pPatchMem, unsigned cbPatchMem)
{
    AssertReleaseFailed();
    RT_NOREF(pVM, pPatchMem, cbPatchMem);
    return VERR_NOT_SUPPORTED;
}


/**
 * Noticiation callback from DBGF when interrupt breakpoints or generic debug
 * event settings changes.
 *
 * DBGF will call HMR3NotifyDebugEventChangedPerCpu on each CPU afterwards, this
 * function is just updating the VM globals.
 *
 * @param   pVM         The VM cross context VM structure.
 * @thread  EMT(0)
 */
VMMR3_INT_DECL(void) HMR3NotifyDebugEventChanged(PVM pVM)
{
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

    /* Vendor specific VM exits. */
    if (HMR3IsVmxEnabled(pVM->pUVM))
        for (DBGFEVENTTYPE enmEvent = DBGFEVENT_EXIT_VMX_FIRST;
             !fUseDebugLoop && enmEvent <= DBGFEVENT_EXIT_VMX_LAST;
             enmEvent = (DBGFEVENTTYPE)(enmEvent + 1))
            fUseDebugLoop = DBGF_IS_EVENT_ENABLED(pVM, enmEvent);
    else
        for (DBGFEVENTTYPE enmEvent = DBGFEVENT_EXIT_SVM_FIRST;
             !fUseDebugLoop && enmEvent <= DBGFEVENT_EXIT_SVM_LAST;
             enmEvent = (DBGFEVENTTYPE)(enmEvent + 1))
            fUseDebugLoop = DBGF_IS_EVENT_ENABLED(pVM, enmEvent);

    /* Done. */
    pVM->hm.s.fUseDebugLoop = fUseDebugLoop;
}


/**
 * Follow up notification callback to HMR3NotifyDebugEventChanged for each CPU.
 *
 * HM uses this to combine the decision made by HMR3NotifyDebugEventChanged with
 * per CPU settings.
 *
 * @param   pVM         The VM cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 */
VMMR3_INT_DECL(void) HMR3NotifyDebugEventChangedPerCpu(PVM pVM, PVMCPU pVCpu)
{
    pVCpu->hm.s.fUseDebugLoop = pVCpu->hm.s.fSingleInstruction | pVM->hm.s.fUseDebugLoop;
}


#if 0 /* evil */
/**
 * Checks if we are currently using hardware acceleration.
 *
 * @returns true if hardware acceleration is being used, otherwise false.
 * @param   pVCpu        The cross context virtual CPU structure.
 */
VMMR3_INT_DECL(bool) HMR3IsActive(PCVMCPU pVCpu)
{
    return pVCpu->hm.s.fActive;
}
#endif


/**
 * External interface for querying whether hardware acceleration is enabled.
 *
 * @returns true if VT-x or AMD-V is being used, otherwise false.
 * @param   pUVM        The user mode VM handle.
 * @sa      HMIsEnabled, HMIsEnabledNotMacro.
 */
VMMR3DECL(bool) HMR3IsEnabled(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, false);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, false);
    return false;
}


/**
 * External interface for querying whether VT-x is being used.
 *
 * @returns true if VT-x is being used, otherwise false.
 * @param   pUVM        The user mode VM handle.
 * @sa      HMR3IsSvmEnabled, HMIsEnabled
 */
VMMR3DECL(bool) HMR3IsVmxEnabled(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, false);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, false);
    return false;
}


/**
 * External interface for querying whether AMD-V is being used.
 *
 * @returns true if VT-x is being used, otherwise false.
 * @param   pUVM        The user mode VM handle.
 * @sa      HMR3IsVmxEnabled, HMIsEnabled
 */
VMMR3DECL(bool) HMR3IsSvmEnabled(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, false);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, false);
    return false;
}


/**
 * Checks if we are currently using nested paging.
 *
 * @returns true if nested paging is being used, otherwise false.
 * @param   pUVM        The user mode VM handle.
 */
VMMR3DECL(bool) HMR3IsNestedPagingActive(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, false);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, false);
    return true; /* NEM requires nested paging always. */
}


/**
 * Checks if virtualized APIC registers are enabled.
 *
 * When enabled this feature allows the hardware to access most of the
 * APIC registers in the virtual-APIC page without causing VM-exits. See
 * Intel spec. 29.1.1 "Virtualized APIC Registers".
 *
 * @returns true if virtualized APIC registers is enabled, otherwise
 *          false.
 * @param   pUVM        The user mode VM handle.
 */
VMMR3DECL(bool) HMR3AreVirtApicRegsEnabled(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, false);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, false);
    return false;
}


/**
 * Checks if APIC posted-interrupt processing is enabled.
 *
 * This returns whether we can deliver interrupts to the guest without
 * leaving guest-context by updating APIC state from host-context.
 *
 * @returns true if APIC posted-interrupt processing is enabled,
 *          otherwise false.
 * @param   pUVM        The user mode VM handle.
 */
VMMR3DECL(bool) HMR3IsPostedIntrsEnabled(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, false);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, false);
    return false;
}


/**
 * Checks if we are currently using VPID in VT-x mode.
 *
 * @returns true if VPID is being used, otherwise false.
 * @param   pUVM        The user mode VM handle.
 */
VMMR3DECL(bool) HMR3IsVpidActive(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, false);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, false);
    return false;
}


/**
 * Checks if we are currently using VT-x unrestricted execution,
 * aka UX.
 *
 * @returns true if UX is being used, otherwise false.
 * @param   pUVM        The user mode VM handle.
 */
VMMR3DECL(bool) HMR3IsUXActive(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, false);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, false);
    return false;
}


/**
 * Check fatal VT-x/AMD-V error and produce some meaningful
 * log release message.
 *
 * @param   pVM         The cross context VM structure.
 * @param   iStatusCode VBox status code.
 */
VMMR3_INT_DECL(void) HMR3CheckError(PVM pVM, int iStatusCode)
{
    AssertReleaseFailed();
    RT_NOREF(pVM, iStatusCode);
}


/**
 * Checks whether HM (VT-x/AMD-V) is being used by this VM.
 *
 * @retval  true if used.
 * @retval  false if software virtualization (raw-mode) is used.
 * @param   pVM        The cross context VM structure.
 * @sa      HMIsEnabled, HMR3IsEnabled
 * @internal
 *
 * @note Doesn't belong here really but it doesn't make sense to create a new source file for a single function.
 */
VMMDECL(bool) HMIsEnabledNotMacro(PVM pVM)
{
    Assert(pVM->bMainExecutionEngine != VM_EXEC_ENGINE_NOT_SET);
    RT_NOREF(pVM);
    return false;
}
