/* $Id: TRPM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * TRPM - The Trap Monitor.
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

/** @page pg_trpm   TRPM - The Trap Monitor
 *
 * The Trap Monitor (TRPM) is responsible for all trap and interrupt handling in
 * the VMM.  It plays a major role in raw-mode execution and a lesser one in the
 * hardware assisted mode.
 *
 * Note first, the following will use trap as a collective term for faults,
 * aborts and traps.
 *
 * @see grp_trpm
 *
 *
 * @section sec_trpm_rc     Raw-Mode Context
 *
 * When executing in the raw-mode context, TRPM will be managing the IDT and
 * processing all traps and interrupts.  It will also monitor the guest IDT
 * because CSAM wishes to know about changes to it (trap/interrupt/syscall
 * handler patching) and TRPM needs to keep the \#BP gate in sync (ring-3
 * considerations).  See TRPMR3SyncIDT and CSAMR3CheckGates.
 *
 * External interrupts will be forwarded to the host context by the quickest
 * possible route where they will be reasserted.  The other events will be
 * categorized into virtualization traps, genuine guest traps and hypervisor
 * traps.  The latter group may be recoverable depending on when they happen and
 * whether there is a handler for it, otherwise it will cause a guru meditation.
 *
 * TRPM distinguishes the between the first two (virt and guest traps) and the
 * latter (hyper) by checking the CPL of the trapping code, if CPL == 0 then
 * it's a hyper trap otherwise it's a virt/guest trap.  There are three trap
 * dispatcher tables, one ad-hoc for one time traps registered via
 * TRPMGCSetTempHandler(), one for hyper traps and one for virt/guest traps.
 * The latter two live in TRPMGCHandlersA.asm, the former in the VM structure.
 *
 * The raw-mode context trap handlers found in TRPMGCHandlers.cpp (for the most
 * part), will call up the other VMM sub-systems depending on what it things
 * happens.  The two most busy traps are page faults (\#PF) and general
 * protection fault/trap (\#GP).
 *
 * Before resuming guest code after having taken a virtualization trap or
 * injected a guest trap, TRPM will check for pending forced action and
 * every now and again let TM check for timed out timers.  This allows code that
 * is being executed as part of virtualization traps to signal ring-3 exits,
 * page table resyncs and similar without necessarily using the status code.  It
 * also make sure we're more responsive to timers and requests from other
 * threads (necessarily running on some different core/cpu in most cases).
 *
 *
 * @section sec_trpm_all    All Contexts
 *
 * TRPM will also dispatch / inject interrupts and traps to the guest, both when
 * in raw-mode and when in hardware assisted mode.  See TRPMInject().
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_TRPM
#include <VBox/vmm/trpm.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/selm.h>
#include <VBox/vmm/ssm.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/iem.h>
#include "TRPMInternal.h"
#include <VBox/vmm/vm.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/hm.h>

#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/string.h>
#include <iprt/alloc.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** TRPM saved state version. */
#define TRPM_SAVED_STATE_VERSION                11
#define TRPM_SAVED_STATE_VERSION_PRE_NMI        10  /* NMI TRPM event type bumped the version */
#define TRPM_SAVED_STATE_VERSION_PRE_ICEBP      9   /* INT1/ICEBP support bumped the version */
#define TRPM_SAVED_STATE_VERSION_UNI            8   /* SMP support bumped the version */


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int) trpmR3Save(PVM pVM, PSSMHANDLE pSSM);
static DECLCALLBACK(int) trpmR3Load(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass);
static DECLCALLBACK(void) trpmR3InfoEvent(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);


/**
 * Initializes the Trap Manager
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3DECL(int) TRPMR3Init(PVM pVM)
{
    LogFlow(("TRPMR3Init\n"));
    int rc;

    /*
     * Assert sizes and alignments.
     */
    AssertRelease(sizeof(pVM->trpm.s) <= sizeof(pVM->trpm.padding));

    /*
     * Initialize members.
     */
    for (VMCPUID i = 0; i < pVM->cCpus; i++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[i];
        pVCpu->trpm.s.uActiveVector = ~0U;
    }

    /*
     * Register the saved state data unit.
     */
    rc = SSMR3RegisterInternal(pVM, "trpm", 1, TRPM_SAVED_STATE_VERSION, sizeof(TRPM),
                               NULL, NULL, NULL,
                               NULL, trpmR3Save, NULL,
                               NULL, trpmR3Load, NULL);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Register info handlers.
     */
    rc = DBGFR3InfoRegisterInternalEx(pVM, "trpmevent", "Dumps TRPM pending event.", trpmR3InfoEvent,
                                      DBGFINFO_FLAGS_ALL_EMTS);
    AssertRCReturn(rc, rc);

    /*
     * Statistics.
     */
    for (unsigned i = 0; i < 256; i++)
        STAMR3RegisterF(pVM, &pVM->trpm.s.aStatForwardedIRQ[i], STAMTYPE_COUNTER, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                        "Forwarded interrupts.", i < 0x20 ? "/TRPM/ForwardRaw/TRAP/%02X" : "/TRPM/ForwardRaw/IRQ/%02X", i);

    return 0;
}


/**
 * Applies relocations to data and code managed by this component.
 *
 * This function will be called at init and whenever the VMM need
 * to relocate itself inside the GC.
 *
 * @param   pVM         The cross context VM structure.
 * @param   offDelta    Relocation delta relative to old location.
 */
VMMR3DECL(void) TRPMR3Relocate(PVM pVM, RTGCINTPTR offDelta)
{
    RT_NOREF(pVM, offDelta);
}


/**
 * Terminates the Trap Manager
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3DECL(int) TRPMR3Term(PVM pVM)
{
    NOREF(pVM);
    return VINF_SUCCESS;
}


/**
 * Resets a virtual CPU.
 *
 * Used by TRPMR3Reset and CPU hot plugging.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 */
VMMR3DECL(void) TRPMR3ResetCpu(PVMCPU pVCpu)
{
    pVCpu->trpm.s.uActiveVector = ~0U;
}


/**
 * The VM is being reset.
 *
 * For the TRPM component this means that any IDT write monitors
 * needs to be removed, any pending trap cleared, and the IDT reset.
 *
 * @param   pVM     The cross context VM structure.
 */
VMMR3DECL(void) TRPMR3Reset(PVM pVM)
{
    /*
     * Reinitialize other members calling the relocator to get things right.
     */
    for (VMCPUID i = 0; i < pVM->cCpus; i++)
        TRPMR3ResetCpu(pVM->apCpusR3[i]);
    TRPMR3Relocate(pVM, 0);
}


/**
 * Execute state save operation.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pSSM            SSM operation handle.
 */
static DECLCALLBACK(int) trpmR3Save(PVM pVM, PSSMHANDLE pSSM)
{
    LogFlow(("trpmR3Save:\n"));

    for (VMCPUID i = 0; i < pVM->cCpus; i++)
    {
        PCTRPMCPU pTrpmCpu = &pVM->apCpusR3[i]->trpm.s;
        SSMR3PutUInt(pSSM,       pTrpmCpu->uActiveVector);
        SSMR3PutUInt(pSSM,       pTrpmCpu->enmActiveType);
        SSMR3PutU32(pSSM,        pTrpmCpu->uActiveErrorCode);
        SSMR3PutGCUIntPtr(pSSM,  pTrpmCpu->uActiveCR2);
        SSMR3PutU8(pSSM,         pTrpmCpu->cbInstr);
        SSMR3PutBool(pSSM,       pTrpmCpu->fIcebp);
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
static DECLCALLBACK(int) trpmR3Load(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    LogFlow(("trpmR3Load:\n"));
    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);

    /*
     * Validate version.
     */
    if (    uVersion != TRPM_SAVED_STATE_VERSION
        &&  uVersion != TRPM_SAVED_STATE_VERSION_PRE_NMI
        &&  uVersion != TRPM_SAVED_STATE_VERSION_PRE_ICEBP
        &&  uVersion != TRPM_SAVED_STATE_VERSION_UNI)
    {
        AssertMsgFailed(("trpmR3Load: Invalid version uVersion=%d!\n", uVersion));
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    }

    if (uVersion >= TRPM_SAVED_STATE_VERSION_PRE_NMI)
    {
        for (VMCPUID i = 0; i < pVM->cCpus; i++)
        {
            PTRPMCPU pTrpmCpu = &pVM->apCpusR3[i]->trpm.s;
            SSMR3GetU32(pSSM,      &pTrpmCpu->uActiveVector);
            SSM_GET_ENUM32_RET(pSSM, pTrpmCpu->enmActiveType, TRPMEVENT);
            SSMR3GetU32(pSSM,       &pTrpmCpu->uActiveErrorCode);
            SSMR3GetGCUIntPtr(pSSM, &pTrpmCpu->uActiveCR2);
            SSMR3GetU8(pSSM,        &pTrpmCpu->cbInstr);
            SSMR3GetBool(pSSM,      &pTrpmCpu->fIcebp);
        }
    }
    else
    {
        /*
         * Active and saved traps.
         */
        if (uVersion == TRPM_SAVED_STATE_VERSION_PRE_ICEBP)
        {
            for (VMCPUID i = 0; i < pVM->cCpus; i++)
            {
                RTGCUINT GCUIntErrCode;
                PTRPMCPU pTrpmCpu = &pVM->apCpusR3[i]->trpm.s;
                SSMR3GetU32(pSSM,      &pTrpmCpu->uActiveVector);
                SSM_GET_ENUM32_RET(pSSM,  pTrpmCpu->enmActiveType, TRPMEVENT);
                SSMR3GetGCUInt(pSSM,    &GCUIntErrCode);
                SSMR3GetGCUIntPtr(pSSM, &pTrpmCpu->uActiveCR2);
                SSMR3Skip(pSSM,          sizeof(RTGCUINT));      /* uSavedVector    - No longer used. */
                SSMR3Skip(pSSM,          sizeof(RTUINT));        /* enmSavedType    - No longer used. */
                SSMR3Skip(pSSM,          sizeof(RTGCUINT));      /* uSavedErrorCode - No longer used. */
                SSMR3Skip(pSSM,          sizeof(RTGCUINTPTR));   /* uSavedCR2       - No longer used. */
                SSMR3Skip(pSSM,          sizeof(RTGCUINT));      /* uPrevVector     - No longer used. */

                /*
                 * We lose the high 64-bits here (if RTGCUINT is 64-bit) after making the
                 * active error code as 32-bits. However, for error codes even 16-bit should
                 * be sufficient. Despite this, we decided to use and keep it at 32-bits
                 * since VMX/SVM defines these as 32-bit in their event fields and converting
                 * to/from these events are safer.
                 */
                pTrpmCpu->uActiveErrorCode = GCUIntErrCode;
            }
        }
        else
        {
            RTGCUINT GCUIntErrCode;
            PTRPMCPU pTrpmCpu = &pVM->apCpusR3[0]->trpm.s;
            SSMR3GetU32(pSSM,      &pTrpmCpu->uActiveVector);
            SSM_GET_ENUM32_RET(pSSM, pTrpmCpu->enmActiveType, TRPMEVENT);
            SSMR3GetGCUInt(pSSM,    &GCUIntErrCode);
            SSMR3GetGCUIntPtr(pSSM, &pTrpmCpu->uActiveCR2);
            pTrpmCpu->uActiveErrorCode = GCUIntErrCode;
        }

        /*
         * Skip rest of TRPM saved-state unit involving IDT and trampoline gates.
         * With the removal of raw-mode support, we no longer need these.
         */
        SSMR3SkipToEndOfUnit(pSSM);
    }

    /*
     * For saved-state verions prior to introducing NMI as a separate type, convert
     * traps with vector 2 as NMI since the rest of VirtualBox code now expects this.
     */
    if (uVersion <= TRPM_SAVED_STATE_VERSION_PRE_NMI)
    {
        for (VMCPUID i = 0; i < pVM->cCpus; i++)
        {
            PTRPMCPU pTrpmCpu = &pVM->apCpusR3[i]->trpm.s;
            AssertLogRelMsgReturn(pTrpmCpu->enmActiveType != TRPM_NMI,
                                  ("TRPM event type (%#RX32) invalid for saved-state version %u!",
                                   pTrpmCpu->enmActiveType, uVersion), VERR_SSM_ENUM_VALUE_OUT_OF_RANGE);
            if (   pTrpmCpu->uActiveVector == X86_XCPT_NMI
                && pTrpmCpu->enmActiveType == TRPM_TRAP)
                pTrpmCpu->enmActiveType = TRPM_NMI;
        }
    }

    return VINF_SUCCESS;
}


/**
 * Inject event (such as external irq or trap).
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   enmEvent    Trpm event type
 * @param   pfInjected  Where to store whether the event was injected or not.
 */
VMMR3DECL(int) TRPMR3InjectEvent(PVM pVM, PVMCPU pVCpu, TRPMEVENT enmEvent, bool *pfInjected)
{
#if defined(VBOX_VMM_TARGET_ARMV8)
    RT_NOREF(pVM, pVCpu, enmEvent, pfInjected);
    AssertReleaseFailed();
    return VERR_NOT_IMPLEMENTED;
#else
    PCPUMCTX pCtx = CPUMQueryGuestCtxPtr(pVCpu);
    Assert(!CPUMIsInInterruptShadow(pCtx));
    Assert(pfInjected);
    *pfInjected = false;

    /* Currently only useful for external hardware interrupts. */
    Assert(enmEvent == TRPM_HARDWARE_INT);

    RT_NOREF3(pVM, enmEvent, pCtx);
    uint8_t u8Interrupt = 0;
    int rc = PDMGetInterrupt(pVCpu, &u8Interrupt);
    Log(("TRPMR3InjectEvent: u8Interrupt=%d (%#x) rc=%Rrc\n", u8Interrupt, u8Interrupt, rc));
    if (RT_SUCCESS(rc))
    {
        *pfInjected = true;
# ifdef RT_OS_WINDOWS
        if (!VM_IS_NEM_ENABLED(pVM))
        {
# endif
# ifdef VBOX_WITH_NESTED_HWVIRT_VMX
            /*
             * If we are NOT calling IEMInjectTrap, we need to handle the
             * VMX nested-guest external-interrupt VM-exit here.
             */
            if (   CPUMIsGuestInVmxNonRootMode(pCtx)
                && CPUMIsGuestVmxInterceptEvents(pCtx)
                && CPUMIsGuestVmxPinCtlsSet(pCtx, VMX_PIN_CTLS_EXT_INT_EXIT))
            {
                Assert(CPUMIsGuestVmxExitCtlsSet(pCtx, VMX_EXIT_CTLS_ACK_EXT_INT));
                VBOXSTRICTRC rcStrict = IEMExecVmxVmexitExtInt(pVCpu, u8Interrupt, false /* fIntPending */);
                Assert(rcStrict != VINF_VMX_INTERCEPT_NOT_ACTIVE);
                return VBOXSTRICTRC_VAL(rcStrict);
            }
# endif
            rc = TRPMAssertTrap(pVCpu, u8Interrupt, TRPM_HARDWARE_INT);
            AssertRC(rc);
# ifdef RT_OS_WINDOWS
        }
        else
        {
            VBOXSTRICTRC rcStrict = IEMInjectTrap(pVCpu, u8Interrupt, enmEvent, 0, 0, 0);
            /** @todo NSTVMX: NSTSVM: We don't support nested VMX or nested SVM with NEM yet.
             *        If so we should handle VINF_SVM_VMEXIT and VINF_VMX_VMEXIT codes here. */
            if (rcStrict != VINF_SUCCESS)
                return VBOXSTRICTRC_TODO(rcStrict);
        }
# endif
        STAM_REL_COUNTER_INC(&pVM->trpm.s.aStatForwardedIRQ[u8Interrupt]);
    }
    else
    {
        /* Can happen if the interrupt is masked by TPR or APIC is disabled. */
        AssertMsg(rc == VERR_APIC_INTR_MASKED_BY_TPR || rc == VERR_NO_DATA, ("PDMGetInterrupt failed. rc=%Rrc\n", rc));
    }
# if 0 /* HMR3IsActive is not reliable (esp. after restore), just return VINF_EM_RESCHEDULE. */
    return HMR3IsActive(pVCpu)    ? VINF_EM_RESCHEDULE_HM
         : VM_IS_NEM_ENABLED(pVM) ? VINF_EM_RESCHEDULE
         :                          VINF_EM_RESCHEDULE_REM; /* (Heed the halted state if this is changed!) */
# else
    return VINF_EM_RESCHEDULE;
# endif
#endif
}


/**
 * Displays the pending TRPM event.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helper functions.
 * @param   pszArgs     Arguments, ignored.
 */
static DECLCALLBACK(void) trpmR3InfoEvent(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    NOREF(pszArgs);
    PVMCPU pVCpu = VMMGetCpu(pVM);
    if (!pVCpu)
        pVCpu = pVM->apCpusR3[0];

    uint8_t     uVector;
    uint8_t     cbInstr;
    TRPMEVENT   enmTrapEvent;
    uint32_t    uErrorCode;
    RTGCUINTPTR uCR2;
    bool        fIcebp;
    int rc = TRPMQueryTrapAll(pVCpu, &uVector, &enmTrapEvent, &uErrorCode, &uCR2, &cbInstr, &fIcebp);
    if (RT_SUCCESS(rc))
    {
        pHlp->pfnPrintf(pHlp, "CPU[%u]: TRPM event\n", pVCpu->idCpu);
        static const char * const s_apszTrpmEventType[] =
        {
            "Trap",
            "Hardware Int",
            "Software Int"
        };
        if (RT_LIKELY((size_t)enmTrapEvent < RT_ELEMENTS(s_apszTrpmEventType)))
        {
            pHlp->pfnPrintf(pHlp, " Type       = %s\n", s_apszTrpmEventType[enmTrapEvent]);
            pHlp->pfnPrintf(pHlp, " uVector    = %#x\n", uVector);
            pHlp->pfnPrintf(pHlp, " uErrorCode = %#x\n", uErrorCode);
            pHlp->pfnPrintf(pHlp, " uCR2       = %#RGp\n", uCR2);
            pHlp->pfnPrintf(pHlp, " cbInstr    = %u bytes\n", cbInstr);
            pHlp->pfnPrintf(pHlp, " fIcebp     = %RTbool\n", fIcebp);
        }
        else
            pHlp->pfnPrintf(pHlp, " Type       = %#x (Invalid!)\n", enmTrapEvent);
    }
    else if (rc == VERR_TRPM_NO_ACTIVE_TRAP)
        pHlp->pfnPrintf(pHlp, "CPU[%u]: TRPM event (None)\n", pVCpu->idCpu);
    else
        pHlp->pfnPrintf(pHlp, "CPU[%u]: TRPM event - Query failed! rc=%Rrc\n", pVCpu->idCpu, rc);
}

