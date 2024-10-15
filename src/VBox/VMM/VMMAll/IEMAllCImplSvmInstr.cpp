/* $Id: IEMAllCImplSvmInstr.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEM - AMD-V (Secure Virtual Machine) instruction implementation.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP   LOG_GROUP_IEM_SVM
#define VMCPU_INCL_CPUM_GST_CTX
#include <VBox/vmm/iem.h>
#include <VBox/vmm/apic.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/pgm.h>
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
# include <VBox/vmm/hm_svm.h>
#endif
#include <VBox/vmm/gim.h>
#include <VBox/vmm/tm.h>
#include "IEMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/log.h>
#include <VBox/disopcode-x86-amd64.h> /* for OP_VMMCALL */
#include <VBox/err.h>
#include <VBox/param.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/x86.h>

#include "IEMInline.h"

#ifdef VBOX_WITH_NESTED_HWVIRT_SVM /* Almost the whole file. */


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/**
 * Check the common SVM instruction preconditions.
 */
# define IEM_SVM_INSTR_COMMON_CHECKS(a_pVCpu, a_Instr) \
    do { \
        if (!CPUMIsGuestSvmEnabled(IEM_GET_CTX(a_pVCpu))) \
        { \
            Log((RT_STR(a_Instr) ": EFER.SVME not enabled -> #UD\n")); \
            return iemRaiseUndefinedOpcode(a_pVCpu); \
        } \
        if (IEM_IS_REAL_OR_V86_MODE(a_pVCpu)) \
        { \
            Log((RT_STR(a_Instr) ": Real or v8086 mode -> #UD\n")); \
            return iemRaiseUndefinedOpcode(a_pVCpu); \
        } \
        if (IEM_GET_CPL(a_pVCpu) != 0) \
        { \
            Log((RT_STR(a_Instr) ": CPL != 0 -> #GP(0)\n")); \
            return iemRaiseGeneralProtectionFault0(a_pVCpu); \
        } \
    } while (0)


/**
 * Converts an IEM exception event type to an SVM event type.
 *
 * @returns The SVM event type.
 * @retval  UINT8_MAX if the specified type of event isn't among the set
 *          of recognized IEM event types.
 *
 * @param   uVector         The vector of the event.
 * @param   fIemXcptFlags   The IEM exception / interrupt flags.
 */
IEM_STATIC uint8_t iemGetSvmEventType(uint32_t uVector, uint32_t fIemXcptFlags)
{
    if (fIemXcptFlags & IEM_XCPT_FLAGS_T_CPU_XCPT)
    {
        if (uVector != X86_XCPT_NMI)
            return SVM_EVENT_EXCEPTION;
        return SVM_EVENT_NMI;
    }

    /* See AMD spec. Table 15-1. "Guest Exception or Interrupt Types". */
    if (fIemXcptFlags & (IEM_XCPT_FLAGS_BP_INSTR | IEM_XCPT_FLAGS_ICEBP_INSTR | IEM_XCPT_FLAGS_OF_INSTR))
        return SVM_EVENT_EXCEPTION;

    if (fIemXcptFlags & IEM_XCPT_FLAGS_T_EXT_INT)
        return SVM_EVENT_EXTERNAL_IRQ;

    if (fIemXcptFlags & IEM_XCPT_FLAGS_T_SOFT_INT)
        return SVM_EVENT_SOFTWARE_INT;

    AssertMsgFailed(("iemGetSvmEventType: Invalid IEM xcpt/int. type %#x, uVector=%#x\n", fIemXcptFlags, uVector));
    return UINT8_MAX;
}


/**
 * Performs an SVM world-switch (VMRUN, \#VMEXIT) updating PGM and IEM internals.
 *
 * @returns Strict VBox status code from PGMChangeMode.
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   cbInstr The length of the current instruction.
 */
DECLINLINE(VBOXSTRICTRC) iemSvmWorldSwitch(PVMCPUCC pVCpu, uint8_t cbInstr)
{
    /*
     * Inform PGM about paging mode changes.
     * We include X86_CR0_PE because PGM doesn't handle paged-real mode yet,
     * see comment in iemMemPageTranslateAndCheckAccess().
     */
    int rc = PGMChangeMode(pVCpu, pVCpu->cpum.GstCtx.cr0 | X86_CR0_PE, pVCpu->cpum.GstCtx.cr4, pVCpu->cpum.GstCtx.msrEFER,
                           true /* fForce */);
    AssertRCReturn(rc, rc);

    /* Invalidate IEM TLBs now that we've forced a PGM mode change. */
    IEMTlbInvalidateAllGlobal(pVCpu);

    /* Inform CPUM (recompiler), can later be removed. */
    CPUMSetChangedFlags(pVCpu, CPUM_CHANGED_ALL);

    /* Re-initialize IEM cache/state after the drastic mode switch. */
    iemReInitExec(pVCpu, cbInstr);
    return rc;
}


/**
 * SVM \#VMEXIT handler.
 *
 * @returns Strict VBox status code.
 * @retval VINF_SVM_VMEXIT when the \#VMEXIT is successful.
 * @retval VERR_SVM_VMEXIT_FAILED when the \#VMEXIT failed restoring the guest's
 *         "host state" and a shutdown is required.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   uExitCode   The exit code.
 * @param   uExitInfo1  The exit info. 1 field.
 * @param   uExitInfo2  The exit info. 2 field.
 */
VBOXSTRICTRC iemSvmVmexit(PVMCPUCC pVCpu, uint64_t uExitCode, uint64_t uExitInfo1, uint64_t uExitInfo2) RT_NOEXCEPT
{
    VBOXSTRICTRC rcStrict;
    if (   CPUMIsGuestInSvmNestedHwVirtMode(IEM_GET_CTX(pVCpu))
        || uExitCode == SVM_EXIT_INVALID)
    {
        Log2(("iemSvmVmexit: CS:RIP=%04x:%08RX64 uExitCode=%#RX64 uExitInfo1=%#RX64 uExitInfo2=%#RX64\n",
              pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, uExitCode, uExitInfo1, uExitInfo2));

        /*
         * Disable the global-interrupt flag to prevent interrupts during the 'atomic' world switch.
         */
        CPUMSetGuestGif(&pVCpu->cpum.GstCtx, false);

        /*
         * Map the nested-guest VMCB from its location in guest memory.
         * Write exactly what the CPU does on #VMEXIT thereby preserving most other bits in the
         * guest's VMCB in memory, see @bugref{7243#c113} and related comment on iemSvmVmrun().
         */
        PSVMVMCB       pVmcbMem;
        PGMPAGEMAPLOCK PgLockMem;
        PSVMVMCBCTRL   pVmcbCtrl = &pVCpu->cpum.GstCtx.hwvirt.svm.Vmcb.ctrl;
        rcStrict = iemMemPageMap(pVCpu, pVCpu->cpum.GstCtx.hwvirt.svm.GCPhysVmcb, IEM_ACCESS_DATA_RW, (void **)&pVmcbMem,
                                 &PgLockMem);
        if (rcStrict == VINF_SUCCESS)
        {
            /*
             * Notify HM in case the nested-guest was executed using hardware-assisted SVM (which
             * would have modified some VMCB state) that might need to be restored on #VMEXIT before
             * writing the VMCB back to guest memory.
             */
            HMNotifySvmNstGstVmexit(pVCpu, IEM_GET_CTX(pVCpu));

            Assert(CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, &pVCpu->cpum.GstCtx.es));
            Assert(CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, &pVCpu->cpum.GstCtx.cs));
            Assert(CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, &pVCpu->cpum.GstCtx.ss));
            Assert(CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, &pVCpu->cpum.GstCtx.ds));

            /*
             * Save the nested-guest state into the VMCB state-save area.
             */
            PSVMVMCBSTATESAVE pVmcbMemState = &pVmcbMem->guest;
            HMSVM_SEG_REG_COPY_TO_VMCB(IEM_GET_CTX(pVCpu), pVmcbMemState, ES, es);
            HMSVM_SEG_REG_COPY_TO_VMCB(IEM_GET_CTX(pVCpu), pVmcbMemState, CS, cs);
            HMSVM_SEG_REG_COPY_TO_VMCB(IEM_GET_CTX(pVCpu), pVmcbMemState, SS, ss);
            HMSVM_SEG_REG_COPY_TO_VMCB(IEM_GET_CTX(pVCpu), pVmcbMemState, DS, ds);
            pVmcbMemState->GDTR.u32Limit   = pVCpu->cpum.GstCtx.gdtr.cbGdt;
            pVmcbMemState->GDTR.u64Base    = pVCpu->cpum.GstCtx.gdtr.pGdt;
            pVmcbMemState->IDTR.u32Limit   = pVCpu->cpum.GstCtx.idtr.cbIdt;
            pVmcbMemState->IDTR.u64Base    = pVCpu->cpum.GstCtx.idtr.pIdt;
            pVmcbMemState->u64EFER         = pVCpu->cpum.GstCtx.msrEFER;
            pVmcbMemState->u64CR4          = pVCpu->cpum.GstCtx.cr4;
            pVmcbMemState->u64CR3          = pVCpu->cpum.GstCtx.cr3;
            pVmcbMemState->u64CR2          = pVCpu->cpum.GstCtx.cr2;
            pVmcbMemState->u64CR0          = pVCpu->cpum.GstCtx.cr0;
            /** @todo Nested paging. */
            pVmcbMemState->u64RFlags       = pVCpu->cpum.GstCtx.rflags.u;
            pVmcbMemState->u64RIP          = pVCpu->cpum.GstCtx.rip;
            pVmcbMemState->u64RSP          = pVCpu->cpum.GstCtx.rsp;
            pVmcbMemState->u64RAX          = pVCpu->cpum.GstCtx.rax;
            pVmcbMemState->u64DR7          = pVCpu->cpum.GstCtx.dr[7];
            pVmcbMemState->u64DR6          = pVCpu->cpum.GstCtx.dr[6];
            pVmcbMemState->u8CPL           = pVCpu->cpum.GstCtx.ss.Attr.n.u2Dpl;   /* See comment in CPUMGetGuestCPL(). */
            Assert(CPUMGetGuestCPL(pVCpu) == pVCpu->cpum.GstCtx.ss.Attr.n.u2Dpl);
            if (CPUMIsGuestSvmNestedPagingEnabled(pVCpu, IEM_GET_CTX(pVCpu)))
                pVmcbMemState->u64PAT = pVCpu->cpum.GstCtx.msrPAT;

            /*
             * Save additional state and intercept information.
             *
             *   - V_IRQ: Tracked using VMCPU_FF_INTERRUPT_NESTED_GUEST force-flag and updated below.
             *   - V_TPR: Updated by iemCImpl_load_CrX or by the physical CPU for hardware-assisted
             *     SVM execution.
             *   - Interrupt shadow: Tracked using VMCPU_FF_INHIBIT_INTERRUPTS and RIP.
             */
            PSVMVMCBCTRL pVmcbMemCtrl = &pVmcbMem->ctrl;
            if (!VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_NESTED_GUEST))           /* V_IRQ. */
                pVmcbMemCtrl->IntCtrl.n.u1VIrqPending = 0;
            else
            {
                Assert(pVmcbCtrl->IntCtrl.n.u1VIrqPending);
                VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_INTERRUPT_NESTED_GUEST);
            }

            pVmcbMemCtrl->IntCtrl.n.u8VTPR = pVmcbCtrl->IntCtrl.n.u8VTPR;           /* V_TPR. */

            if (!CPUMIsInInterruptShadowWithUpdate(&pVCpu->cpum.GstCtx))            /* Interrupt shadow. */
                pVmcbMemCtrl->IntShadow.n.u1IntShadow = 0;
            else
            {
                pVmcbMemCtrl->IntShadow.n.u1IntShadow = 1;
                LogFlow(("iemSvmVmexit: Interrupt shadow till %#RX64\n", pVCpu->cpum.GstCtx.rip));
                CPUMClearInterruptShadow(&pVCpu->cpum.GstCtx);
            }

            /*
             * Save nRIP, instruction length and byte fields.
             */
            pVmcbMemCtrl->u64NextRIP     = pVmcbCtrl->u64NextRIP;
            pVmcbMemCtrl->cbInstrFetched = pVmcbCtrl->cbInstrFetched;
            memcpy(&pVmcbMemCtrl->abInstr[0], &pVmcbCtrl->abInstr[0], sizeof(pVmcbMemCtrl->abInstr));

            /*
             * Save exit information.
             */
            pVmcbMemCtrl->u64ExitCode  = uExitCode;
            pVmcbMemCtrl->u64ExitInfo1 = uExitInfo1;
            pVmcbMemCtrl->u64ExitInfo2 = uExitInfo2;

            /*
             * Update the exit interrupt-information field if this #VMEXIT happened as a result
             * of delivering an event through IEM.
             *
             * Don't update the exit interrupt-information field if the event wasn't being injected
             * through IEM, as it would have been updated by real hardware if the nested-guest was
             * executed using hardware-assisted SVM.
             */
            {
                uint8_t  uExitIntVector;
                uint32_t uExitIntErr;
                uint32_t fExitIntFlags;
                bool const fRaisingEvent = IEMGetCurrentXcpt(pVCpu, &uExitIntVector, &fExitIntFlags, &uExitIntErr,
                                                             NULL /* uExitIntCr2 */);
                if (fRaisingEvent)
                {
                    pVmcbCtrl->ExitIntInfo.n.u1Valid  = 1;
                    pVmcbCtrl->ExitIntInfo.n.u8Vector = uExitIntVector;
                    pVmcbCtrl->ExitIntInfo.n.u3Type   = iemGetSvmEventType(uExitIntVector, fExitIntFlags);
                    if (fExitIntFlags & IEM_XCPT_FLAGS_ERR)
                    {
                        pVmcbCtrl->ExitIntInfo.n.u1ErrorCodeValid = true;
                        pVmcbCtrl->ExitIntInfo.n.u32ErrorCode     = uExitIntErr;
                    }
                }
            }

            /*
             * Save the exit interrupt-information field.
             *
             * We write the whole field including overwriting reserved bits as it was observed on an
             * AMD Ryzen 5 Pro 1500 that the CPU does not preserve reserved bits in EXITINTINFO.
             */
            pVmcbMemCtrl->ExitIntInfo = pVmcbCtrl->ExitIntInfo;

            /*
             * Clear event injection.
             */
            pVmcbMemCtrl->EventInject.n.u1Valid = 0;

            iemMemPageUnmap(pVCpu, pVCpu->cpum.GstCtx.hwvirt.svm.GCPhysVmcb, IEM_ACCESS_DATA_RW, pVmcbMem, &PgLockMem);
        }

        /*
         * Prepare for guest's "host mode" by clearing internal processor state bits.
         *
         * We don't need to zero out the state-save area, just the controls should be
         * sufficient because it has the critical bit of indicating whether we're inside
         * the nested-guest or not.
         */
        memset(pVmcbCtrl, 0, sizeof(*pVmcbCtrl));
        Assert(!CPUMIsGuestInSvmNestedHwVirtMode(IEM_GET_CTX(pVCpu)));

        /*
         * Restore the subset of the inhibit flags that were preserved.
         */
        pVCpu->cpum.GstCtx.eflags.uBoth |= pVCpu->cpum.GstCtx.hwvirt.fSavedInhibit;

        if (rcStrict == VINF_SUCCESS)
        {
            /** @todo Nested paging. */
            /** @todo ASID. */

            /*
             * If we are switching to PAE mode host, validate the PDPEs first.
             * Any invalid PDPEs here causes a VCPU shutdown.
             */
            PCSVMHOSTSTATE pHostState = &pVCpu->cpum.GstCtx.hwvirt.svm.HostState;
            bool const fHostInPaeMode = CPUMIsPaePagingEnabled(pHostState->uCr0, pHostState->uCr4, pHostState->uEferMsr);
            if (fHostInPaeMode)
                rcStrict = PGMGstMapPaePdpesAtCr3(pVCpu, pHostState->uCr3);
            if (RT_SUCCESS(rcStrict))
            {
                /*
                 * Reload the host state.
                 */
                CPUMSvmVmExitRestoreHostState(pVCpu, IEM_GET_CTX(pVCpu));

                /*
                 * Update PGM, IEM and others of a world-switch.
                 */
                rcStrict = iemSvmWorldSwitch(pVCpu, 0 /*cbInstr - whatever*/);
                if (rcStrict == VINF_SUCCESS)
                    rcStrict = VINF_SVM_VMEXIT;
                else if (RT_SUCCESS(rcStrict))
                {
                    LogFlow(("iemSvmVmexit: Setting passup status from iemSvmWorldSwitch %Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
                    iemSetPassUpStatus(pVCpu, rcStrict);
                    rcStrict = VINF_SVM_VMEXIT;
                }
                else
                    LogFlow(("iemSvmVmexit: iemSvmWorldSwitch unexpected failure. rc=%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
            }
            else
            {
                Log(("iemSvmVmexit: PAE PDPEs invalid while restoring host state. rc=%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
                rcStrict = VINF_EM_TRIPLE_FAULT;
            }
        }
        else
        {
            AssertMsgFailed(("iemSvmVmexit: Mapping VMCB at %#RGp failed. rc=%Rrc\n", pVCpu->cpum.GstCtx.hwvirt.svm.GCPhysVmcb, VBOXSTRICTRC_VAL(rcStrict)));
            rcStrict = VINF_EM_TRIPLE_FAULT;
        }
    }
    else
    {
        AssertMsgFailed(("iemSvmVmexit: Not in SVM guest mode! uExitCode=%#RX64 uExitInfo1=%#RX64 uExitInfo2=%#RX64\n", uExitCode, uExitInfo1, uExitInfo2));
        rcStrict = VERR_SVM_IPE_3;
    }

# if defined(VBOX_WITH_NESTED_HWVIRT_ONLY_IN_IEM) && defined(IN_RING3)
    /* CLGI/STGI may not have been intercepted and thus not executed in IEM. */
    if (   HMIsEnabled(pVCpu->CTX_SUFF(pVM))
        && HMIsSvmVGifActive(pVCpu->CTX_SUFF(pVM)))
        return EMR3SetExecutionPolicy(pVCpu->CTX_SUFF(pVM)->pUVM, EMEXECPOLICY_IEM_ALL, false);
# endif
    return rcStrict;
}


/**
 * Interface for HM and EM to emulate \#VMEXIT.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   uExitCode   The exit code.
 * @param   uExitInfo1  The exit info. 1 field.
 * @param   uExitInfo2  The exit info. 2 field.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecSvmVmexit(PVMCPUCC pVCpu, uint64_t uExitCode, uint64_t uExitInfo1, uint64_t uExitInfo2)
{
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_SVM_VMEXIT_MASK);
    VBOXSTRICTRC rcStrict = iemSvmVmexit(pVCpu, uExitCode, uExitInfo1, uExitInfo2);
    if (pVCpu->iem.s.cActiveMappings)
        iemMemRollback(pVCpu);
    return iemExecStatusCodeFiddling(pVCpu, rcStrict);
}


/**
 * Performs the operations necessary that are part of the vmrun instruction
 * execution in the guest.
 *
 * @returns Strict VBox status code (i.e. informational status codes too).
 * @retval  VINF_SUCCESS successfully executed VMRUN and entered nested-guest
 *          code execution.
 * @retval  VINF_SVM_VMEXIT when executing VMRUN causes a \#VMEXIT
 *          (SVM_EXIT_INVALID most likely).
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   cbInstr             The length of the VMRUN instruction.
 * @param   GCPhysVmcb          Guest physical address of the VMCB to run.
 */
static VBOXSTRICTRC iemSvmVmrun(PVMCPUCC pVCpu, uint8_t cbInstr, RTGCPHYS GCPhysVmcb) RT_NOEXCEPT
{
    LogFlow(("iemSvmVmrun\n"));

    /*
     * Cache the physical address of the VMCB for #VMEXIT exceptions.
     */
    pVCpu->cpum.GstCtx.hwvirt.svm.GCPhysVmcb = GCPhysVmcb;

    /*
     * Save the host state.
     */
    CPUMSvmVmRunSaveHostState(IEM_GET_CTX(pVCpu), cbInstr);

    /*
     * Read the guest VMCB.
     */
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    int rc = PGMPhysSimpleReadGCPhys(pVM, &pVCpu->cpum.GstCtx.hwvirt.svm.Vmcb, GCPhysVmcb, sizeof(SVMVMCB));
    if (RT_SUCCESS(rc))
    {
        /*
         * AMD-V seems to preserve reserved fields and only writes back selected, recognized
         * fields on #VMEXIT. However, not all reserved  bits are preserved (e.g, EXITINTINFO)
         * but in our implementation we try to preserve as much as we possibly can.
         *
         * We could read the entire page here and only write back the relevant fields on
         * #VMEXIT but since our internal VMCB is also being used by HM during hardware-assisted
         * SVM execution, it creates a potential for a nested-hypervisor to set bits that are
         * currently reserved but may be recognized as features bits in future CPUs causing
         * unexpected & undesired results. Hence, we zero out unrecognized fields here as we
         * typically enter hardware-assisted SVM soon anyway, see @bugref{7243#c113}.
         */
        PSVMVMCBCTRL      pVmcbCtrl   = &pVCpu->cpum.GstCtx.hwvirt.svm.Vmcb.ctrl;
        PSVMVMCBSTATESAVE pVmcbNstGst = &pVCpu->cpum.GstCtx.hwvirt.svm.Vmcb.guest;

        RT_ZERO(pVmcbCtrl->u8Reserved0);
        RT_ZERO(pVmcbCtrl->u8Reserved1);
        RT_ZERO(pVmcbCtrl->u8Reserved2);
        RT_ZERO(pVmcbNstGst->u8Reserved0);
        RT_ZERO(pVmcbNstGst->u8Reserved1);
        RT_ZERO(pVmcbNstGst->u8Reserved2);
        RT_ZERO(pVmcbNstGst->u8Reserved3);
        RT_ZERO(pVmcbNstGst->u8Reserved4);
        RT_ZERO(pVmcbNstGst->u8Reserved5);
        pVmcbCtrl->u32Reserved0                   = 0;
        pVmcbCtrl->TLBCtrl.n.u24Reserved          = 0;
        pVmcbCtrl->IntCtrl.n.u6Reserved           = 0;
        pVmcbCtrl->IntCtrl.n.u3Reserved           = 0;
        pVmcbCtrl->IntCtrl.n.u5Reserved           = 0;
        pVmcbCtrl->IntCtrl.n.u24Reserved          = 0;
        pVmcbCtrl->IntShadow.n.u30Reserved        = 0;
        pVmcbCtrl->ExitIntInfo.n.u19Reserved      = 0;
        pVmcbCtrl->NestedPagingCtrl.n.u29Reserved = 0;
        pVmcbCtrl->EventInject.n.u19Reserved      = 0;
        pVmcbCtrl->LbrVirt.n.u30Reserved          = 0;

        /*
         * Validate guest-state and controls.
         */
        /* VMRUN must always be intercepted. */
        if (!CPUMIsGuestSvmCtrlInterceptSet(pVCpu, IEM_GET_CTX(pVCpu), SVM_CTRL_INTERCEPT_VMRUN))
        {
            Log(("iemSvmVmrun: VMRUN instruction not intercepted -> #VMEXIT\n"));
            return iemSvmVmexit(pVCpu, SVM_EXIT_INVALID, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
        }

        /* Nested paging. */
        if (    pVmcbCtrl->NestedPagingCtrl.n.u1NestedPaging
            && !pVM->cpum.ro.GuestFeatures.fSvmNestedPaging)
        {
            Log(("iemSvmVmrun: Nested paging not supported -> Disabling\n"));
            pVmcbCtrl->NestedPagingCtrl.n.u1NestedPaging = 0;
        }

        /* AVIC. */
        if (    pVmcbCtrl->IntCtrl.n.u1AvicEnable
            && !pVM->cpum.ro.GuestFeatures.fSvmAvic)
        {
            Log(("iemSvmVmrun: AVIC not supported -> Disabling\n"));
            pVmcbCtrl->IntCtrl.n.u1AvicEnable = 0;
        }

        /* Last branch record (LBR) virtualization. */
        if (    pVmcbCtrl->LbrVirt.n.u1LbrVirt
            && !pVM->cpum.ro.GuestFeatures.fSvmLbrVirt)
        {
            Log(("iemSvmVmrun: LBR virtualization not supported -> Disabling\n"));
            pVmcbCtrl->LbrVirt.n.u1LbrVirt = 0;
        }

        /* Virtualized VMSAVE/VMLOAD. */
        if (    pVmcbCtrl->LbrVirt.n.u1VirtVmsaveVmload
            && !pVM->cpum.ro.GuestFeatures.fSvmVirtVmsaveVmload)
        {
            Log(("iemSvmVmrun: Virtualized VMSAVE/VMLOAD not supported -> Disabling\n"));
            pVmcbCtrl->LbrVirt.n.u1VirtVmsaveVmload = 0;
        }

        /* Virtual GIF. */
        if (    pVmcbCtrl->IntCtrl.n.u1VGifEnable
            && !pVM->cpum.ro.GuestFeatures.fSvmVGif)
        {
            Log(("iemSvmVmrun: Virtual GIF not supported -> Disabling\n"));
            pVmcbCtrl->IntCtrl.n.u1VGifEnable = 0;
        }

        /* Guest ASID. */
        if (!pVmcbCtrl->TLBCtrl.n.u32ASID)
        {
            Log(("iemSvmVmrun: Guest ASID is invalid -> #VMEXIT\n"));
            return iemSvmVmexit(pVCpu, SVM_EXIT_INVALID, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
        }

        /* Guest AVIC. */
        if (    pVmcbCtrl->IntCtrl.n.u1AvicEnable
            && !pVM->cpum.ro.GuestFeatures.fSvmAvic)
        {
            Log(("iemSvmVmrun: AVIC not supported -> Disabling\n"));
            pVmcbCtrl->IntCtrl.n.u1AvicEnable = 0;
        }

        /* Guest Secure Encrypted Virtualization. */
        if (  (   pVmcbCtrl->NestedPagingCtrl.n.u1Sev
               || pVmcbCtrl->NestedPagingCtrl.n.u1SevEs)
            && !pVM->cpum.ro.GuestFeatures.fSvmAvic)
        {
            Log(("iemSvmVmrun: SEV not supported -> Disabling\n"));
            pVmcbCtrl->NestedPagingCtrl.n.u1Sev = 0;
            pVmcbCtrl->NestedPagingCtrl.n.u1SevEs = 0;
        }

        /* Flush by ASID. */
        if (   !pVM->cpum.ro.GuestFeatures.fSvmFlusbByAsid
            &&  pVmcbCtrl->TLBCtrl.n.u8TLBFlush != SVM_TLB_FLUSH_NOTHING
            &&  pVmcbCtrl->TLBCtrl.n.u8TLBFlush != SVM_TLB_FLUSH_ENTIRE)
        {
            Log(("iemSvmVmrun: Flush-by-ASID not supported -> #VMEXIT\n"));
            return iemSvmVmexit(pVCpu, SVM_EXIT_INVALID, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
        }

        /* IO permission bitmap. */
        RTGCPHYS const GCPhysIOBitmap = pVmcbCtrl->u64IOPMPhysAddr;
        if (   (GCPhysIOBitmap & X86_PAGE_4K_OFFSET_MASK)
            || !PGMPhysIsGCPhysNormal(pVM, GCPhysIOBitmap)
            || !PGMPhysIsGCPhysNormal(pVM, GCPhysIOBitmap + X86_PAGE_4K_SIZE)
            || !PGMPhysIsGCPhysNormal(pVM, GCPhysIOBitmap + (X86_PAGE_4K_SIZE << 1)))
        {
            Log(("iemSvmVmrun: IO bitmap physaddr invalid. GCPhysIOBitmap=%#RX64 -> #VMEXIT\n", GCPhysIOBitmap));
            return iemSvmVmexit(pVCpu, SVM_EXIT_INVALID, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
        }

        /* MSR permission bitmap. */
        RTGCPHYS const GCPhysMsrBitmap = pVmcbCtrl->u64MSRPMPhysAddr;
        if (   (GCPhysMsrBitmap & X86_PAGE_4K_OFFSET_MASK)
            || !PGMPhysIsGCPhysNormal(pVM, GCPhysMsrBitmap)
            || !PGMPhysIsGCPhysNormal(pVM, GCPhysMsrBitmap + X86_PAGE_4K_SIZE))
        {
            Log(("iemSvmVmrun: MSR bitmap physaddr invalid. GCPhysMsrBitmap=%#RX64 -> #VMEXIT\n", GCPhysMsrBitmap));
            return iemSvmVmexit(pVCpu, SVM_EXIT_INVALID, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
        }

        /* CR0. */
        if (   !(pVmcbNstGst->u64CR0 & X86_CR0_CD)
            &&  (pVmcbNstGst->u64CR0 & X86_CR0_NW))
        {
            Log(("iemSvmVmrun: CR0 no-write through with cache disabled. CR0=%#RX64 -> #VMEXIT\n", pVmcbNstGst->u64CR0));
            return iemSvmVmexit(pVCpu, SVM_EXIT_INVALID, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
        }
        if (pVmcbNstGst->u64CR0 >> 32)
        {
            Log(("iemSvmVmrun: CR0 reserved bits set. CR0=%#RX64 -> #VMEXIT\n", pVmcbNstGst->u64CR0));
            return iemSvmVmexit(pVCpu, SVM_EXIT_INVALID, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
        }
        /** @todo Implement all reserved bits/illegal combinations for CR3, CR4. */

        /* DR6 and DR7. */
        if (   pVmcbNstGst->u64DR6 >> 32
            || pVmcbNstGst->u64DR7 >> 32)
        {
            Log(("iemSvmVmrun: DR6 and/or DR7 reserved bits set. DR6=%#RX64 DR7=%#RX64 -> #VMEXIT\n", pVmcbNstGst->u64DR6,
                 pVmcbNstGst->u64DR6));
            return iemSvmVmexit(pVCpu, SVM_EXIT_INVALID, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
        }

        /*
         * PAT (Page Attribute Table) MSR.
         *
         * The CPU only validates and loads it when nested-paging is enabled.
         * See AMD spec. "15.25.4 Nested Paging and VMRUN/#VMEXIT".
         */
        if (   pVmcbCtrl->NestedPagingCtrl.n.u1NestedPaging
            && !CPUMIsPatMsrValid(pVmcbNstGst->u64PAT))
        {
            Log(("iemSvmVmrun: PAT invalid. u64PAT=%#RX64 -> #VMEXIT\n", pVmcbNstGst->u64PAT));
            return iemSvmVmexit(pVCpu, SVM_EXIT_INVALID, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
        }

        /*
         * Copy the IO permission bitmap into the cache.
         */
        AssertCompile(sizeof(pVCpu->cpum.GstCtx.hwvirt.svm.abIoBitmap) == SVM_IOPM_PAGES * X86_PAGE_4K_SIZE);
        rc = PGMPhysSimpleReadGCPhys(pVM, pVCpu->cpum.GstCtx.hwvirt.svm.abIoBitmap, GCPhysIOBitmap,
                                     sizeof(pVCpu->cpum.GstCtx.hwvirt.svm.abIoBitmap));
        if (RT_FAILURE(rc))
        {
            Log(("iemSvmVmrun: Failed reading the IO permission bitmap at %#RGp. rc=%Rrc\n", GCPhysIOBitmap, rc));
            return iemSvmVmexit(pVCpu, SVM_EXIT_INVALID, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
        }

        /*
         * Copy the MSR permission bitmap into the cache.
         */
        AssertCompile(sizeof(pVCpu->cpum.GstCtx.hwvirt.svm.abMsrBitmap) == SVM_MSRPM_PAGES * X86_PAGE_4K_SIZE);
        rc = PGMPhysSimpleReadGCPhys(pVM, pVCpu->cpum.GstCtx.hwvirt.svm.abMsrBitmap, GCPhysMsrBitmap,
                                     sizeof(pVCpu->cpum.GstCtx.hwvirt.svm.abMsrBitmap));
        if (RT_FAILURE(rc))
        {
            Log(("iemSvmVmrun: Failed reading the MSR permission bitmap at %#RGp. rc=%Rrc\n", GCPhysMsrBitmap, rc));
            return iemSvmVmexit(pVCpu, SVM_EXIT_INVALID, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
        }

        /*
         * Copy segments from nested-guest VMCB state to the guest-CPU state.
         *
         * We do this here as we need to use the CS attributes and it's easier this way
         * then using the VMCB format selectors. It doesn't really matter where we copy
         * the state, we restore the guest-CPU context state on the \#VMEXIT anyway.
         */
        HMSVM_SEG_REG_COPY_FROM_VMCB(IEM_GET_CTX(pVCpu), pVmcbNstGst, ES, es);
        HMSVM_SEG_REG_COPY_FROM_VMCB(IEM_GET_CTX(pVCpu), pVmcbNstGst, CS, cs);
        HMSVM_SEG_REG_COPY_FROM_VMCB(IEM_GET_CTX(pVCpu), pVmcbNstGst, SS, ss);
        HMSVM_SEG_REG_COPY_FROM_VMCB(IEM_GET_CTX(pVCpu), pVmcbNstGst, DS, ds);

        /** @todo Segment attribute overrides by VMRUN. */

        /*
         * CPL adjustments and overrides.
         *
         * SS.DPL is apparently the CPU's CPL, see comment in CPUMGetGuestCPL().
         * We shall thus adjust both CS.DPL and SS.DPL here.
         */
        pVCpu->cpum.GstCtx.cs.Attr.n.u2Dpl = pVCpu->cpum.GstCtx.ss.Attr.n.u2Dpl = pVmcbNstGst->u8CPL;
        if (CPUMIsGuestInV86ModeEx(IEM_GET_CTX(pVCpu)))
            pVCpu->cpum.GstCtx.cs.Attr.n.u2Dpl = pVCpu->cpum.GstCtx.ss.Attr.n.u2Dpl = 3;
        if (CPUMIsGuestInRealModeEx(IEM_GET_CTX(pVCpu)))
            pVCpu->cpum.GstCtx.cs.Attr.n.u2Dpl = pVCpu->cpum.GstCtx.ss.Attr.n.u2Dpl = 0;
        Assert(CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, &pVCpu->cpum.GstCtx.ss));

        /*
         * Continue validating guest-state and controls.
         *
         * We pass CR0 as 0 to CPUMIsGuestEferMsrWriteValid() below to skip the illegal
         * EFER.LME bit transition check. We pass the nested-guest's EFER as both the
         * old and new EFER value to not have any guest EFER bits influence the new
         * nested-guest EFER.
         */
        uint64_t uValidEfer;
        rc = CPUMIsGuestEferMsrWriteValid(pVM, 0 /* CR0 */, pVmcbNstGst->u64EFER, pVmcbNstGst->u64EFER, &uValidEfer);
        if (RT_FAILURE(rc))
        {
            Log(("iemSvmVmrun: EFER invalid uOldEfer=%#RX64 -> #VMEXIT\n", pVmcbNstGst->u64EFER));
            return iemSvmVmexit(pVCpu, SVM_EXIT_INVALID, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
        }

        /* Validate paging and CPU mode bits. */
        bool const fSvm                     = RT_BOOL(uValidEfer & MSR_K6_EFER_SVME);
        bool const fLongModeSupported       = RT_BOOL(pVM->cpum.ro.GuestFeatures.fLongMode);
        bool const fLongModeEnabled         = RT_BOOL(uValidEfer & MSR_K6_EFER_LME);
        bool const fPaging                  = RT_BOOL(pVmcbNstGst->u64CR0 & X86_CR0_PG);
        bool const fPae                     = RT_BOOL(pVmcbNstGst->u64CR4 & X86_CR4_PAE);
        bool const fProtMode                = RT_BOOL(pVmcbNstGst->u64CR0 & X86_CR0_PE);
        bool const fLongModeWithPaging      = fLongModeEnabled && fPaging;
        bool const fLongModeConformCS       = pVCpu->cpum.GstCtx.cs.Attr.n.u1Long && pVCpu->cpum.GstCtx.cs.Attr.n.u1DefBig;
        /* Adjust EFER.LMA (this is normally done by the CPU when system software writes CR0). */
        if (fLongModeWithPaging)
            uValidEfer |= MSR_K6_EFER_LMA;
        bool const fLongModeActiveOrEnabled = RT_BOOL(uValidEfer & (MSR_K6_EFER_LME | MSR_K6_EFER_LMA));
        if (   !fSvm
            || (!fLongModeSupported && fLongModeActiveOrEnabled)
            || (fLongModeWithPaging && !fPae)
            || (fLongModeWithPaging && !fProtMode)
            || (   fLongModeEnabled
                && fPaging
                && fPae
                && fLongModeConformCS))
        {
            Log(("iemSvmVmrun: EFER invalid. uValidEfer=%#RX64 -> #VMEXIT\n", uValidEfer));
            return iemSvmVmexit(pVCpu, SVM_EXIT_INVALID, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
        }

        /*
         * Preserve the required force-flags.
         *
         * We only preserve the force-flags that would affect the execution of the
         * nested-guest (or the guest).
         *
         *   - VMCPU_FF_BLOCK_NMIS needs to be preserved as it blocks NMI until the
         *     execution of a subsequent IRET instruction in the guest.
         *
         * The remaining FFs (e.g. timers) can stay in place so that we will be able to
         * generate interrupts that should cause #VMEXITs for the nested-guest.
         *
         * VMRUN has implicit GIF (Global Interrupt Flag) handling, we don't need to
         * preserve VMCPU_FF_INHIBIT_INTERRUPTS.
         */
        pVCpu->cpum.GstCtx.hwvirt.fSavedInhibit = pVCpu->cpum.GstCtx.eflags.uBoth & CPUMCTX_INHIBIT_NMI;
        pVCpu->cpum.GstCtx.eflags.uBoth        &=                                  ~CPUMCTX_INHIBIT_NMI;

        /*
         * Pause filter.
         */
        if (pVM->cpum.ro.GuestFeatures.fSvmPauseFilter)
        {
            pVCpu->cpum.GstCtx.hwvirt.svm.cPauseFilter = pVmcbCtrl->u16PauseFilterCount;
            if (pVM->cpum.ro.GuestFeatures.fSvmPauseFilterThreshold)
                pVCpu->cpum.GstCtx.hwvirt.svm.cPauseFilterThreshold = pVmcbCtrl->u16PauseFilterCount;
        }

        /*
         * Interrupt shadow.
         */
        if (pVmcbCtrl->IntShadow.n.u1IntShadow)
        {
            LogFlow(("iemSvmVmrun: setting interrupt shadow. inhibit PC=%#RX64\n", pVmcbNstGst->u64RIP));
            /** @todo will this cause trouble if the nested-guest is 64-bit but the guest is 32-bit? */
            CPUMSetInInterruptShadowEx(&pVCpu->cpum.GstCtx, pVmcbNstGst->u64RIP);
        }

        /*
         * TLB flush control.
         * Currently disabled since it's redundant as we unconditionally flush the TLB
         * in iemSvmWorldSwitch() below.
         */
# if 0
        /** @todo @bugref{7243}: ASID based PGM TLB flushes. */
        if (   pVmcbCtrl->TLBCtrl.n.u8TLBFlush == SVM_TLB_FLUSH_ENTIRE
            || pVmcbCtrl->TLBCtrl.n.u8TLBFlush == SVM_TLB_FLUSH_SINGLE_CONTEXT
            || pVmcbCtrl->TLBCtrl.n.u8TLBFlush == SVM_TLB_FLUSH_SINGLE_CONTEXT_RETAIN_GLOBALS)
            PGMFlushTLB(pVCpu, pVmcbNstGst->u64CR3, true /* fGlobal */);
# endif

        /*
         * Validate and map PAE PDPEs if the guest will be using PAE paging.
         * Invalid PAE PDPEs here causes a #VMEXIT.
         */
        if (   !pVmcbCtrl->NestedPagingCtrl.n.u1NestedPaging
            && CPUMIsPaePagingEnabled(pVmcbNstGst->u64CR0, pVmcbNstGst->u64CR4, uValidEfer))
        {
            rc = PGMGstMapPaePdpesAtCr3(pVCpu, pVmcbNstGst->u64CR3);
            if (RT_SUCCESS(rc))
            { /* likely */ }
            else
            {
                Log(("iemSvmVmrun: PAE PDPEs invalid -> #VMEXIT\n"));
                return iemSvmVmexit(pVCpu, SVM_EXIT_INVALID, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
            }
        }

        /*
         * Copy the remaining guest state from the VMCB to the guest-CPU context.
         */
        pVCpu->cpum.GstCtx.gdtr.cbGdt = pVmcbNstGst->GDTR.u32Limit;
        pVCpu->cpum.GstCtx.gdtr.pGdt  = pVmcbNstGst->GDTR.u64Base;
        pVCpu->cpum.GstCtx.idtr.cbIdt = pVmcbNstGst->IDTR.u32Limit;
        pVCpu->cpum.GstCtx.idtr.pIdt  = pVmcbNstGst->IDTR.u64Base;
        CPUMSetGuestCR0(pVCpu, pVmcbNstGst->u64CR0);
        CPUMSetGuestCR4(pVCpu, pVmcbNstGst->u64CR4);
        pVCpu->cpum.GstCtx.cr3        = pVmcbNstGst->u64CR3;
        pVCpu->cpum.GstCtx.cr2        = pVmcbNstGst->u64CR2;
        pVCpu->cpum.GstCtx.dr[6]      = pVmcbNstGst->u64DR6;
        pVCpu->cpum.GstCtx.dr[7]      = pVmcbNstGst->u64DR7;
        pVCpu->cpum.GstCtx.rflags.u   = pVmcbNstGst->u64RFlags;
        pVCpu->cpum.GstCtx.rax        = pVmcbNstGst->u64RAX;
        pVCpu->cpum.GstCtx.rsp        = pVmcbNstGst->u64RSP;
        pVCpu->cpum.GstCtx.rip        = pVmcbNstGst->u64RIP;
        CPUMSetGuestEferMsrNoChecks(pVCpu, pVCpu->cpum.GstCtx.msrEFER, uValidEfer);
        if (pVmcbCtrl->NestedPagingCtrl.n.u1NestedPaging)
            pVCpu->cpum.GstCtx.msrPAT = pVmcbNstGst->u64PAT;

        /* Mask DR6, DR7 bits mandatory set/clear bits. */
        pVCpu->cpum.GstCtx.dr[6] &= ~(X86_DR6_RAZ_MASK | X86_DR6_MBZ_MASK);
        pVCpu->cpum.GstCtx.dr[6] |= X86_DR6_RA1_MASK;
        pVCpu->cpum.GstCtx.dr[7] &= ~(X86_DR7_RAZ_MASK | X86_DR7_MBZ_MASK);
        pVCpu->cpum.GstCtx.dr[7] |= X86_DR7_RA1_MASK;

        /*
         * Check for pending virtual interrupts.
         */
        if (pVmcbCtrl->IntCtrl.n.u1VIrqPending)
            VMCPU_FF_SET(pVCpu, VMCPU_FF_INTERRUPT_NESTED_GUEST);
        else
            Assert(!VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_NESTED_GUEST));

        /*
         * Update PGM, IEM and others of a world-switch.
         */
        VBOXSTRICTRC rcStrict = iemSvmWorldSwitch(pVCpu, cbInstr);
        if (rcStrict == VINF_SUCCESS)
        { /* likely */ }
        else if (RT_SUCCESS(rcStrict))
        {
            LogFlow(("iemSvmVmrun: iemSvmWorldSwitch returned %Rrc, setting passup status\n", VBOXSTRICTRC_VAL(rcStrict)));
            rcStrict = iemSetPassUpStatus(pVCpu, rcStrict);
        }
        else
        {
            LogFlow(("iemSvmVmrun: iemSvmWorldSwitch unexpected failure. rc=%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
            return rcStrict;
        }

        /*
         * Set the global-interrupt flag to allow interrupts in the guest.
         */
        CPUMSetGuestGif(&pVCpu->cpum.GstCtx, true);

        /*
         * Event injection.
         */
        PCSVMEVENT pEventInject = &pVmcbCtrl->EventInject;
        pVCpu->cpum.GstCtx.hwvirt.svm.fInterceptEvents = !pEventInject->n.u1Valid;
        if (pEventInject->n.u1Valid)
        {
            uint8_t   const uVector    = pEventInject->n.u8Vector;
            TRPMEVENT const enmType    = HMSvmEventToTrpmEventType(pEventInject, uVector);
            uint16_t  const uErrorCode = pEventInject->n.u1ErrorCodeValid ? pEventInject->n.u32ErrorCode : 0;

            /* Validate vectors for hardware exceptions, see AMD spec. 15.20 "Event Injection". */
            if (RT_UNLIKELY(enmType == TRPM_32BIT_HACK))
            {
                Log(("iemSvmVmrun: Invalid event type =%#x -> #VMEXIT\n", (uint8_t)pEventInject->n.u3Type));
                return iemSvmVmexit(pVCpu, SVM_EXIT_INVALID, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
            }
            if (pEventInject->n.u3Type == SVM_EVENT_EXCEPTION)
            {
                if (   uVector == X86_XCPT_NMI
                    || uVector > X86_XCPT_LAST)
                {
                    Log(("iemSvmVmrun: Invalid vector for hardware exception. uVector=%#x -> #VMEXIT\n", uVector));
                    return iemSvmVmexit(pVCpu, SVM_EXIT_INVALID, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
                }
                if (   uVector == X86_XCPT_BR
                    && CPUMIsGuestInLongModeEx(IEM_GET_CTX(pVCpu)))
                {
                    Log(("iemSvmVmrun: Cannot inject #BR when not in long mode -> #VMEXIT\n"));
                    return iemSvmVmexit(pVCpu, SVM_EXIT_INVALID, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
                }
                /** @todo any others? */
            }

            /*
             * Invalidate the exit interrupt-information field here. This field is fully updated
             * on #VMEXIT as events other than the one below can also cause intercepts during
             * their injection (e.g. exceptions).
             */
            pVmcbCtrl->ExitIntInfo.n.u1Valid = 0;

            /*
             * Clear the event injection valid bit here. While the AMD spec. mentions that the CPU
             * clears this bit from the VMCB unconditionally on #VMEXIT, internally the CPU could be
             * clearing it at any time, most likely before/after injecting the event. Since VirtualBox
             * doesn't have any virtual-CPU internal representation of this bit, we clear/update the
             * VMCB here. This also has the added benefit that we avoid the risk of injecting the event
             * twice if we fallback to executing the nested-guest using hardware-assisted SVM after
             * injecting the event through IEM here.
             */
            pVmcbCtrl->EventInject.n.u1Valid = 0;

            /** @todo NRIP: Software interrupts can only be pushed properly if we support
             *        NRIP for the nested-guest to calculate the instruction length
             *        below. */
            LogFlow(("iemSvmVmrun: Injecting event: %04x:%08RX64 vec=%#x type=%d uErr=%u cr2=%#RX64 cr3=%#RX64 efer=%#RX64\n",
                     pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, uVector, enmType, uErrorCode, pVCpu->cpum.GstCtx.cr2,
                     pVCpu->cpum.GstCtx.cr3, pVCpu->cpum.GstCtx.msrEFER));

            /*
             * We shall not inject the event here right away. There may be paging mode related updates
             * as a result of the world-switch above that are yet to be honored. Instead flag the event
             * as pending for injection.
             */
            TRPMAssertTrap(pVCpu, uVector, enmType);
            if (pEventInject->n.u1ErrorCodeValid)
                TRPMSetErrorCode(pVCpu, uErrorCode);
            if (   enmType == TRPM_TRAP
                && uVector == X86_XCPT_PF)
                TRPMSetFaultAddress(pVCpu, pVCpu->cpum.GstCtx.cr2);
        }
        else
            LogFlow(("iemSvmVmrun: Entering nested-guest: %04x:%08RX64 cr0=%#RX64 cr3=%#RX64 cr4=%#RX64 efer=%#RX64 efl=%#x\n",
                     pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pVCpu->cpum.GstCtx.cr0, pVCpu->cpum.GstCtx.cr3,
                     pVCpu->cpum.GstCtx.cr4, pVCpu->cpum.GstCtx.msrEFER, pVCpu->cpum.GstCtx.eflags.u));

        LogFlow(("iemSvmVmrun: returns %d\n", VBOXSTRICTRC_VAL(rcStrict)));

# if defined(VBOX_WITH_NESTED_HWVIRT_ONLY_IN_IEM) && defined(IN_RING3)
        /* If CLGI/STGI isn't intercepted we force IEM-only nested-guest execution here. */
        if (   HMIsEnabled(pVM)
            && HMIsSvmVGifActive(pVM))
            return EMR3SetExecutionPolicy(pVCpu->CTX_SUFF(pVM)->pUVM, EMEXECPOLICY_IEM_ALL, true);
# endif

        return rcStrict;
    }

    /* Shouldn't really happen as the caller should've validated the physical address already. */
    Log(("iemSvmVmrun: Failed to read nested-guest VMCB at %#RGp (rc=%Rrc) -> #VMEXIT\n", GCPhysVmcb, rc));
    return rc;
}


/**
 * Checks if the event intercepts and performs the \#VMEXIT if the corresponding
 * intercept is active.
 *
 * @returns Strict VBox status code.
 * @retval  VINF_HM_INTERCEPT_NOT_ACTIVE if the intercept is not active or
 *          we're not executing a nested-guest.
 * @retval  VINF_SVM_VMEXIT if the intercept is active and the \#VMEXIT occurred
 *          successfully.
 * @retval  VERR_SVM_VMEXIT_FAILED if the intercept is active and the \#VMEXIT
 *          failed and a shutdown needs to be initiated for the guest.
 *
 * @returns VBox strict status code.
 * @param   pVCpu       The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr     The length of the instruction in bytes triggering the
 *                      event.
 * @param   u8Vector    The interrupt or exception vector.
 * @param   fFlags      The exception flags (see IEM_XCPT_FLAGS_XXX).
 * @param   uErr        The error-code associated with the exception.
 * @param   uCr2        The CR2 value in case of a \#PF exception.
 */
VBOXSTRICTRC iemHandleSvmEventIntercept(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t u8Vector, uint32_t fFlags,
                                        uint32_t uErr, uint64_t uCr2) RT_NOEXCEPT
{
    Assert(CPUMIsGuestInSvmNestedHwVirtMode(IEM_GET_CTX(pVCpu)));

    /*
     * Handle SVM exception and software interrupt intercepts, see AMD spec. 15.12 "Exception Intercepts".
     *
     *   - NMI intercepts have their own exit code and do not cause SVM_EXIT_XCPT_2 #VMEXITs.
     *   - External interrupts and software interrupts (INTn instruction) do not check the exception intercepts
     *     even when they use a vector in the range 0 to 31.
     *   - ICEBP should not trigger #DB intercept, but its own intercept.
     *   - For #PF exceptions, its intercept is checked before CR2 is written by the exception.
     */
    /* Check NMI intercept */
    if (   u8Vector == X86_XCPT_NMI
        && (fFlags & IEM_XCPT_FLAGS_T_CPU_XCPT)
        && IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_NMI))
    {
        Log2(("iemHandleSvmNstGstEventIntercept: NMI intercept -> #VMEXIT\n"));
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_NMI, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }

    /* Check ICEBP intercept. */
    if (   (fFlags & IEM_XCPT_FLAGS_ICEBP_INSTR)
        && IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_ICEBP))
    {
        Log2(("iemHandleSvmNstGstEventIntercept: ICEBP intercept -> #VMEXIT\n"));
        IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_ICEBP, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }

    /* Check CPU exception intercepts. */
    if (   (fFlags & IEM_XCPT_FLAGS_T_CPU_XCPT)
        && IEM_SVM_IS_XCPT_INTERCEPT_SET(pVCpu, u8Vector))
    {
        Assert(u8Vector <= X86_XCPT_LAST);
        uint64_t const uExitInfo1 = fFlags & IEM_XCPT_FLAGS_ERR ? uErr : 0;
        uint64_t const uExitInfo2 = fFlags & IEM_XCPT_FLAGS_CR2 ? uCr2 : 0;
        if (   IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fSvmDecodeAssists
            && u8Vector == X86_XCPT_PF
            && !(uErr & X86_TRAP_PF_ID))
        {
            PSVMVMCBCTRL  pVmcbCtrl = &pVCpu->cpum.GstCtx.hwvirt.svm.Vmcb.ctrl;
# ifdef IEM_WITH_CODE_TLB
            uint8_t const *pbInstrBuf = pVCpu->iem.s.pbInstrBuf;
            uint8_t const  cbInstrBuf = pVCpu->iem.s.cbInstrBuf;
            pVmcbCtrl->cbInstrFetched = RT_MIN(cbInstrBuf, SVM_CTRL_GUEST_INSTR_BYTES_MAX);
            if (   pbInstrBuf
                && cbInstrBuf > 0)
                memcpy(&pVmcbCtrl->abInstr[0], pbInstrBuf, pVmcbCtrl->cbInstrFetched);
# else
            uint8_t const cbOpcode    = pVCpu->iem.s.cbOpcode;
            pVmcbCtrl->cbInstrFetched = RT_MIN(cbOpcode, SVM_CTRL_GUEST_INSTR_BYTES_MAX);
            if (cbOpcode > 0)
                memcpy(&pVmcbCtrl->abInstr[0], &pVCpu->iem.s.abOpcode[0], pVmcbCtrl->cbInstrFetched);
# endif
        }
        if (u8Vector == X86_XCPT_BR)
            IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
        Log2(("iemHandleSvmNstGstEventIntercept: Xcpt intercept u32InterceptXcpt=%#RX32 u8Vector=%#x "
              "uExitInfo1=%#RX64 uExitInfo2=%#RX64 -> #VMEXIT\n", pVCpu->cpum.GstCtx.hwvirt.svm.Vmcb.ctrl.u32InterceptXcpt,
              u8Vector, uExitInfo1, uExitInfo2));
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_XCPT_0 + u8Vector, uExitInfo1, uExitInfo2);
    }

    /* Check software interrupt (INTn) intercepts. */
    if (   (fFlags & (  IEM_XCPT_FLAGS_T_SOFT_INT
                      | IEM_XCPT_FLAGS_BP_INSTR
                      | IEM_XCPT_FLAGS_ICEBP_INSTR
                      | IEM_XCPT_FLAGS_OF_INSTR)) == IEM_XCPT_FLAGS_T_SOFT_INT
        && IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_INTN))
    {
        uint64_t const uExitInfo1 = IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fSvmDecodeAssists ? u8Vector : 0;
        Log2(("iemHandleSvmNstGstEventIntercept: Software INT intercept (u8Vector=%#x) -> #VMEXIT\n", u8Vector));
        IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_SWINT, uExitInfo1, 0 /* uExitInfo2 */);
    }

    return VINF_SVM_INTERCEPT_NOT_ACTIVE;
}


/**
 * Checks the SVM IO permission bitmap and performs the \#VMEXIT if the
 * corresponding intercept is active.
 *
 * @returns Strict VBox status code.
 * @retval  VINF_HM_INTERCEPT_NOT_ACTIVE if the intercept is not active or
 *          we're not executing a nested-guest.
 * @retval  VINF_SVM_VMEXIT if the intercept is active and the \#VMEXIT occurred
 *          successfully.
 * @retval  VERR_SVM_VMEXIT_FAILED if the intercept is active and the \#VMEXIT
 *          failed and a shutdown needs to be initiated for the guest.
 *
 * @returns VBox strict status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling thread.
 * @param   u16Port         The IO port being accessed.
 * @param   enmIoType       The type of IO access.
 * @param   cbReg           The IO operand size in bytes.
 * @param   cAddrSizeBits   The address size bits (for 16, 32 or 64).
 * @param   iEffSeg         The effective segment number.
 * @param   fRep            Whether this is a repeating IO instruction (REP prefix).
 * @param   fStrIo          Whether this is a string IO instruction.
 * @param   cbInstr         The length of the IO instruction in bytes.
 */
VBOXSTRICTRC iemSvmHandleIOIntercept(PVMCPUCC pVCpu, uint16_t u16Port, SVMIOIOTYPE enmIoType, uint8_t cbReg,
                                     uint8_t cAddrSizeBits, uint8_t iEffSeg, bool fRep, bool fStrIo, uint8_t cbInstr) RT_NOEXCEPT
{
    Assert(IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_IOIO_PROT));
    Assert(cAddrSizeBits == 16 || cAddrSizeBits == 32 || cAddrSizeBits == 64);
    Assert(cbReg == 1 || cbReg == 2 || cbReg == 4 || cbReg == 8);

    Log3(("iemSvmHandleIOIntercept: u16Port=%#x (%u)\n", u16Port, u16Port));

    SVMIOIOEXITINFO IoExitInfo;
    bool const fIntercept = CPUMIsSvmIoInterceptSet(pVCpu->cpum.GstCtx.hwvirt.svm.abMsrBitmap, u16Port, enmIoType, cbReg,
                                                    cAddrSizeBits, iEffSeg, fRep, fStrIo, &IoExitInfo);
    if (fIntercept)
    {
        Log3(("iemSvmHandleIOIntercept: u16Port=%#x (%u) -> #VMEXIT\n", u16Port, u16Port));
        IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
        return iemSvmVmexit(pVCpu, SVM_EXIT_IOIO, IoExitInfo.u, pVCpu->cpum.GstCtx.rip + cbInstr);
    }

    /** @todo remove later (for debugging as VirtualBox always traps all IO
     *        intercepts). */
    AssertMsgFailed(("iemSvmHandleIOIntercept: We expect an IO intercept here!\n"));
    return VINF_SVM_INTERCEPT_NOT_ACTIVE;
}


/**
 * Checks the SVM MSR permission bitmap and performs the \#VMEXIT if the
 * corresponding intercept is active.
 *
 * @returns Strict VBox status code.
 * @retval  VINF_HM_INTERCEPT_NOT_ACTIVE if the MSR permission bitmap does not
 *          specify interception of the accessed MSR @a idMsr.
 * @retval  VINF_SVM_VMEXIT if the intercept is active and the \#VMEXIT occurred
 *          successfully.
 * @retval  VERR_SVM_VMEXIT_FAILED if the intercept is active and the \#VMEXIT
 *          failed and a shutdown needs to be initiated for the guest.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idMsr       The MSR being accessed in the nested-guest.
 * @param   fWrite      Whether this is an MSR write access, @c false implies an
 *                      MSR read.
 * @param   cbInstr     The length of the MSR read/write instruction in bytes.
 */
VBOXSTRICTRC iemSvmHandleMsrIntercept(PVMCPUCC pVCpu, uint32_t idMsr, bool fWrite, uint8_t cbInstr) RT_NOEXCEPT
{
    /*
     * Check if any MSRs are being intercepted.
     */
    Assert(CPUMIsGuestSvmCtrlInterceptSet(pVCpu, IEM_GET_CTX(pVCpu), SVM_CTRL_INTERCEPT_MSR_PROT));
    Assert(CPUMIsGuestInSvmNestedHwVirtMode(IEM_GET_CTX(pVCpu)));

    uint64_t const uExitInfo1 = fWrite ? SVM_EXIT1_MSR_WRITE : SVM_EXIT1_MSR_READ;

    /*
     * Get the byte and bit offset of the permission bits corresponding to the MSR.
     */
    uint16_t offMsrpm;
    uint8_t  uMsrpmBit;
    int rc = CPUMGetSvmMsrpmOffsetAndBit(idMsr, &offMsrpm, &uMsrpmBit);
    if (RT_SUCCESS(rc))
    {
        Assert(uMsrpmBit == 0 || uMsrpmBit == 2 || uMsrpmBit == 4 || uMsrpmBit == 6);
        Assert(offMsrpm < SVM_MSRPM_PAGES << X86_PAGE_4K_SHIFT);
        if (fWrite)
            ++uMsrpmBit;

        /*
         * Check if the bit is set, if so, trigger a #VMEXIT.
         */
        if (pVCpu->cpum.GstCtx.hwvirt.svm.abMsrBitmap[offMsrpm] & RT_BIT(uMsrpmBit))
        {
            IEM_SVM_UPDATE_NRIP(pVCpu, cbInstr);
            return iemSvmVmexit(pVCpu, SVM_EXIT_MSR, uExitInfo1, 0 /* uExitInfo2 */);
        }
    }
    else
    {
        /*
         * This shouldn't happen, but if it does, cause a #VMEXIT and let the "host" (nested hypervisor) deal with it.
         */
        Log(("iemSvmHandleMsrIntercept: Invalid/out-of-range MSR %#RX32 fWrite=%RTbool -> #VMEXIT\n", idMsr, fWrite));
        return iemSvmVmexit(pVCpu, SVM_EXIT_MSR, uExitInfo1, 0 /* uExitInfo2 */);
    }
    return VINF_SVM_INTERCEPT_NOT_ACTIVE;
}



/**
 * Implements 'VMRUN'.
 */
IEM_CIMPL_DEF_0(iemCImpl_vmrun)
{
# if defined(VBOX_WITH_NESTED_HWVIRT_ONLY_IN_IEM) && !defined(IN_RING3)
    RT_NOREF2(pVCpu, cbInstr);
    return VINF_EM_RAW_EMULATE_INSTR;
# else
    LogFlow(("iemCImpl_vmrun\n"));
    IEM_SVM_INSTR_COMMON_CHECKS(pVCpu, vmrun);

    /** @todo Check effective address size using address size prefix. */
    RTGCPHYS const GCPhysVmcb = IEM_IS_64BIT_CODE(pVCpu) ? pVCpu->cpum.GstCtx.rax : pVCpu->cpum.GstCtx.eax;
    if (   (GCPhysVmcb & X86_PAGE_4K_OFFSET_MASK)
        || !PGMPhysIsGCPhysNormal(pVCpu->CTX_SUFF(pVM), GCPhysVmcb))
    {
        Log(("vmrun: VMCB physaddr (%#RGp) not valid -> #GP(0)\n", GCPhysVmcb));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_VMRUN))
    {
        Log(("vmrun: Guest intercept -> #VMEXIT\n"));
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_VMRUN, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }

    VBOXSTRICTRC rcStrict = iemSvmVmrun(pVCpu, cbInstr, GCPhysVmcb);
    if (rcStrict == VERR_SVM_VMEXIT_FAILED)
    {
        Assert(!CPUMIsGuestInSvmNestedHwVirtMode(IEM_GET_CTX(pVCpu)));
        rcStrict = VINF_EM_TRIPLE_FAULT;
    }
    return rcStrict;
# endif
}


/**
 * Interface for HM and EM to emulate the VMRUN instruction.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   cbInstr     The instruction length in bytes.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedVmrun(PVMCPUCC pVCpu, uint8_t cbInstr)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 3);
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_SVM_VMRUN_MASK);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_0(iemCImpl_vmrun);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Implements 'VMLOAD'.
 */
IEM_CIMPL_DEF_0(iemCImpl_vmload)
{
# if defined(VBOX_WITH_NESTED_HWVIRT_ONLY_IN_IEM) && !defined(IN_RING3)
    RT_NOREF2(pVCpu, cbInstr);
    return VINF_EM_RAW_EMULATE_INSTR;
# else
    LogFlow(("iemCImpl_vmload\n"));
    IEM_SVM_INSTR_COMMON_CHECKS(pVCpu, vmload);

    /** @todo Check effective address size using address size prefix. */
    RTGCPHYS const GCPhysVmcb = IEM_IS_64BIT_CODE(pVCpu) ? pVCpu->cpum.GstCtx.rax : pVCpu->cpum.GstCtx.eax;
    if (   (GCPhysVmcb & X86_PAGE_4K_OFFSET_MASK)
        || !PGMPhysIsGCPhysNormal(pVCpu->CTX_SUFF(pVM), GCPhysVmcb))
    {
        Log(("vmload: VMCB physaddr (%#RGp) not valid -> #GP(0)\n", GCPhysVmcb));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_VMLOAD))
    {
        Log(("vmload: Guest intercept -> #VMEXIT\n"));
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_VMLOAD, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }

    SVMVMCBSTATESAVE VmcbNstGst;
    VBOXSTRICTRC rcStrict = PGMPhysSimpleReadGCPhys(pVCpu->CTX_SUFF(pVM), &VmcbNstGst, GCPhysVmcb + RT_UOFFSETOF(SVMVMCB, guest),
                                                    sizeof(SVMVMCBSTATESAVE));
    if (rcStrict == VINF_SUCCESS)
    {
        LogFlow(("vmload: Loading VMCB at %#RGp enmEffAddrMode=%d\n", GCPhysVmcb, pVCpu->iem.s.enmEffAddrMode));
        HMSVM_SEG_REG_COPY_FROM_VMCB(IEM_GET_CTX(pVCpu), &VmcbNstGst, FS, fs);
        HMSVM_SEG_REG_COPY_FROM_VMCB(IEM_GET_CTX(pVCpu), &VmcbNstGst, GS, gs);
        HMSVM_SEG_REG_COPY_FROM_VMCB(IEM_GET_CTX(pVCpu), &VmcbNstGst, TR, tr);
        HMSVM_SEG_REG_COPY_FROM_VMCB(IEM_GET_CTX(pVCpu), &VmcbNstGst, LDTR, ldtr);

        pVCpu->cpum.GstCtx.msrKERNELGSBASE = VmcbNstGst.u64KernelGSBase;
        pVCpu->cpum.GstCtx.msrSTAR         = VmcbNstGst.u64STAR;
        pVCpu->cpum.GstCtx.msrLSTAR        = VmcbNstGst.u64LSTAR;
        pVCpu->cpum.GstCtx.msrCSTAR        = VmcbNstGst.u64CSTAR;
        pVCpu->cpum.GstCtx.msrSFMASK       = VmcbNstGst.u64SFMASK;

        pVCpu->cpum.GstCtx.SysEnter.cs     = VmcbNstGst.u64SysEnterCS;
        pVCpu->cpum.GstCtx.SysEnter.esp    = VmcbNstGst.u64SysEnterESP;
        pVCpu->cpum.GstCtx.SysEnter.eip    = VmcbNstGst.u64SysEnterEIP;

        rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }
    return rcStrict;
# endif
}


/**
 * Interface for HM and EM to emulate the VMLOAD instruction.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   cbInstr     The instruction length in bytes.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedVmload(PVMCPUCC pVCpu, uint8_t cbInstr)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 3);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_0(iemCImpl_vmload);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Implements 'VMSAVE'.
 */
IEM_CIMPL_DEF_0(iemCImpl_vmsave)
{
# if defined(VBOX_WITH_NESTED_HWVIRT_ONLY_IN_IEM) && !defined(IN_RING3)
    RT_NOREF2(pVCpu, cbInstr);
    return VINF_EM_RAW_EMULATE_INSTR;
# else
    LogFlow(("iemCImpl_vmsave\n"));
    IEM_SVM_INSTR_COMMON_CHECKS(pVCpu, vmsave);

    /** @todo Check effective address size using address size prefix. */
    RTGCPHYS const GCPhysVmcb = IEM_IS_64BIT_CODE(pVCpu) ? pVCpu->cpum.GstCtx.rax : pVCpu->cpum.GstCtx.eax;
    if (   (GCPhysVmcb & X86_PAGE_4K_OFFSET_MASK)
        || !PGMPhysIsGCPhysNormal(pVCpu->CTX_SUFF(pVM), GCPhysVmcb))
    {
        Log(("vmsave: VMCB physaddr (%#RGp) not valid -> #GP(0)\n", GCPhysVmcb));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_VMSAVE))
    {
        Log(("vmsave: Guest intercept -> #VMEXIT\n"));
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_VMSAVE, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }

    SVMVMCBSTATESAVE VmcbNstGst;
    VBOXSTRICTRC rcStrict = PGMPhysSimpleReadGCPhys(pVCpu->CTX_SUFF(pVM), &VmcbNstGst, GCPhysVmcb + RT_UOFFSETOF(SVMVMCB, guest),
                                                    sizeof(SVMVMCBSTATESAVE));
    if (rcStrict == VINF_SUCCESS)
    {
        LogFlow(("vmsave: Saving VMCB at %#RGp enmEffAddrMode=%d\n", GCPhysVmcb, pVCpu->iem.s.enmEffAddrMode));
        IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_FS | CPUMCTX_EXTRN_GS | CPUMCTX_EXTRN_TR | CPUMCTX_EXTRN_LDTR
                                | CPUMCTX_EXTRN_KERNEL_GS_BASE | CPUMCTX_EXTRN_SYSCALL_MSRS | CPUMCTX_EXTRN_SYSENTER_MSRS);

        HMSVM_SEG_REG_COPY_TO_VMCB(IEM_GET_CTX(pVCpu), &VmcbNstGst, FS, fs);
        HMSVM_SEG_REG_COPY_TO_VMCB(IEM_GET_CTX(pVCpu), &VmcbNstGst, GS, gs);
        HMSVM_SEG_REG_COPY_TO_VMCB(IEM_GET_CTX(pVCpu), &VmcbNstGst, TR, tr);
        HMSVM_SEG_REG_COPY_TO_VMCB(IEM_GET_CTX(pVCpu), &VmcbNstGst, LDTR, ldtr);

        VmcbNstGst.u64KernelGSBase  = pVCpu->cpum.GstCtx.msrKERNELGSBASE;
        VmcbNstGst.u64STAR          = pVCpu->cpum.GstCtx.msrSTAR;
        VmcbNstGst.u64LSTAR         = pVCpu->cpum.GstCtx.msrLSTAR;
        VmcbNstGst.u64CSTAR         = pVCpu->cpum.GstCtx.msrCSTAR;
        VmcbNstGst.u64SFMASK        = pVCpu->cpum.GstCtx.msrSFMASK;

        VmcbNstGst.u64SysEnterCS    = pVCpu->cpum.GstCtx.SysEnter.cs;
        VmcbNstGst.u64SysEnterESP   = pVCpu->cpum.GstCtx.SysEnter.esp;
        VmcbNstGst.u64SysEnterEIP   = pVCpu->cpum.GstCtx.SysEnter.eip;

        rcStrict = PGMPhysSimpleWriteGCPhys(pVCpu->CTX_SUFF(pVM), GCPhysVmcb + RT_UOFFSETOF(SVMVMCB, guest), &VmcbNstGst,
                                            sizeof(SVMVMCBSTATESAVE));
        if (rcStrict == VINF_SUCCESS)
            rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }
    return rcStrict;
# endif
}


/**
 * Interface for HM and EM to emulate the VMSAVE instruction.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   cbInstr     The instruction length in bytes.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedVmsave(PVMCPUCC pVCpu, uint8_t cbInstr)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 3);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_0(iemCImpl_vmsave);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Implements 'CLGI'.
 */
IEM_CIMPL_DEF_0(iemCImpl_clgi)
{
# if defined(VBOX_WITH_NESTED_HWVIRT_ONLY_IN_IEM) && !defined(IN_RING3)
    RT_NOREF2(pVCpu, cbInstr);
    return VINF_EM_RAW_EMULATE_INSTR;
# else
    LogFlow(("iemCImpl_clgi\n"));
    IEM_SVM_INSTR_COMMON_CHECKS(pVCpu, clgi);
    if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_CLGI))
    {
        Log(("clgi: Guest intercept -> #VMEXIT\n"));
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_CLGI, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }

    CPUMSetGuestGif(&pVCpu->cpum.GstCtx, false);

#  if defined(VBOX_WITH_NESTED_HWVIRT_ONLY_IN_IEM) && defined(IN_RING3)
    iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    return EMR3SetExecutionPolicy(pVCpu->CTX_SUFF(pVM)->pUVM, EMEXECPOLICY_IEM_ALL, true);
#  else
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
#  endif
# endif
}


/**
 * Interface for HM and EM to emulate the CLGI instruction.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   cbInstr     The instruction length in bytes.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedClgi(PVMCPUCC pVCpu, uint8_t cbInstr)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 3);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_0(iemCImpl_clgi);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Implements 'STGI'.
 */
IEM_CIMPL_DEF_0(iemCImpl_stgi)
{
# if defined(VBOX_WITH_NESTED_HWVIRT_ONLY_IN_IEM) && !defined(IN_RING3)
    RT_NOREF2(pVCpu, cbInstr);
    return VINF_EM_RAW_EMULATE_INSTR;
# else
    LogFlow(("iemCImpl_stgi\n"));
    IEM_SVM_INSTR_COMMON_CHECKS(pVCpu, stgi);
    if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_STGI))
    {
        Log2(("stgi: Guest intercept -> #VMEXIT\n"));
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_STGI, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }

    CPUMSetGuestGif(&pVCpu->cpum.GstCtx, true);

#  if defined(VBOX_WITH_NESTED_HWVIRT_ONLY_IN_IEM) && defined(IN_RING3)
    iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    return EMR3SetExecutionPolicy(pVCpu->CTX_SUFF(pVM)->pUVM, EMEXECPOLICY_IEM_ALL, false);
#  else
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
#  endif
# endif
}


/**
 * Interface for HM and EM to emulate the STGI instruction.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   cbInstr     The instruction length in bytes.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedStgi(PVMCPUCC pVCpu, uint8_t cbInstr)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 3);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_0(iemCImpl_stgi);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Implements 'INVLPGA'.
 */
IEM_CIMPL_DEF_0(iemCImpl_invlpga)
{
    /** @todo Check effective address size using address size prefix. */
    RTGCPTR  const GCPtrPage = IEM_IS_64BIT_CODE(pVCpu) ? pVCpu->cpum.GstCtx.rax : pVCpu->cpum.GstCtx.eax;
    /** @todo PGM needs virtual ASID support. */
# if 0
    uint32_t const uAsid     = pVCpu->cpum.GstCtx.ecx;
# endif

    IEM_SVM_INSTR_COMMON_CHECKS(pVCpu, invlpga);
    if (!IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_INVLPGA))
    { /* probable */ }
    else
    {
        Log2(("invlpga: Guest intercept (%RGp) -> #VMEXIT\n", GCPtrPage));
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_INVLPGA, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }

    PGMInvalidatePage(pVCpu, GCPtrPage);
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Interface for HM and EM to emulate the INVLPGA instruction.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   cbInstr     The instruction length in bytes.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedInvlpga(PVMCPUCC pVCpu, uint8_t cbInstr)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 3);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_0(iemCImpl_invlpga);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Implements 'SKINIT'.
 */
IEM_CIMPL_DEF_0(iemCImpl_skinit)
{
    IEM_SVM_INSTR_COMMON_CHECKS(pVCpu, invlpga);

    uint32_t uIgnore;
    uint32_t fFeaturesECX;
    CPUMGetGuestCpuId(pVCpu, 0x80000001, 0 /* iSubLeaf */, -1 /*f64BitMode*/, &uIgnore, &uIgnore, &fFeaturesECX, &uIgnore);
    if (!(fFeaturesECX & X86_CPUID_AMD_FEATURE_ECX_SKINIT))
        return iemRaiseUndefinedOpcode(pVCpu);

    if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_SKINIT))
    {
        Log2(("skinit: Guest intercept -> #VMEXIT\n"));
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_SKINIT, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }

    RT_NOREF(cbInstr);
    return VERR_IEM_INSTR_NOT_IMPLEMENTED;
}


/**
 * Implements SVM's implementation of PAUSE.
 */
IEM_CIMPL_DEF_0(iemCImpl_svm_pause)
{
    bool fCheckIntercept = true;
    if (IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fSvmPauseFilter)
    {
        IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_HWVIRT);

        /* TSC based pause-filter thresholding. */
        if (   IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fSvmPauseFilterThreshold
            && pVCpu->cpum.GstCtx.hwvirt.svm.cPauseFilterThreshold > 0)
        {
            uint64_t const uTick = TMCpuTickGet(pVCpu);
            if (uTick - pVCpu->cpum.GstCtx.hwvirt.svm.uPrevPauseTick > pVCpu->cpum.GstCtx.hwvirt.svm.cPauseFilterThreshold)
                pVCpu->cpum.GstCtx.hwvirt.svm.cPauseFilter = CPUMGetGuestSvmPauseFilterCount(pVCpu, IEM_GET_CTX(pVCpu));
            pVCpu->cpum.GstCtx.hwvirt.svm.uPrevPauseTick = uTick;
        }

        /* Simple pause-filter counter. */
        if (pVCpu->cpum.GstCtx.hwvirt.svm.cPauseFilter > 0)
        {
            --pVCpu->cpum.GstCtx.hwvirt.svm.cPauseFilter;
            fCheckIntercept = false;
        }
    }

    if (fCheckIntercept)
        IEM_SVM_CHECK_INSTR_INTERCEPT(pVCpu, SVM_CTRL_INTERCEPT_PAUSE, SVM_EXIT_PAUSE, 0, 0, cbInstr);

    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}

#endif /* VBOX_WITH_NESTED_HWVIRT_SVM */

/**
 * Common code for iemCImpl_vmmcall and iemCImpl_vmcall (latter in IEMAllCImplVmxInstr.cpp.h).
 */
IEM_CIMPL_DEF_1(iemCImpl_Hypercall, uint16_t, uDisOpcode)
{
    if (EMAreHypercallInstructionsEnabled(pVCpu))
    {
        NOREF(uDisOpcode);
        VBOXSTRICTRC rcStrict = GIMHypercallEx(pVCpu, IEM_GET_CTX(pVCpu), uDisOpcode, cbInstr);
        if (RT_SUCCESS(rcStrict))
        {
            /** @todo finish: Sort out assertion here when iemRegAddToRipAndFinishingClearingRF
             * starts returning non-VINF_SUCCESS statuses. */
            if (rcStrict == VINF_SUCCESS)
                rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
            if (   rcStrict == VINF_SUCCESS
                || rcStrict == VINF_GIM_HYPERCALL_CONTINUING)
                return VINF_SUCCESS;
            AssertMsgReturn(rcStrict == VINF_GIM_R3_HYPERCALL, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)), VERR_IEM_IPE_4);
            return rcStrict;
        }
        AssertMsgReturn(   rcStrict == VERR_GIM_HYPERCALL_ACCESS_DENIED
                        || rcStrict == VERR_GIM_HYPERCALLS_NOT_AVAILABLE
                        || rcStrict == VERR_GIM_NOT_ENABLED
                        || rcStrict == VERR_GIM_HYPERCALL_MEMORY_READ_FAILED
                        || rcStrict == VERR_GIM_HYPERCALL_MEMORY_WRITE_FAILED,
                        ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)), VERR_IEM_IPE_4);

        /* Raise #UD on all failures. */
    }
    return iemRaiseUndefinedOpcode(pVCpu);
}


/**
 * Implements 'VMMCALL'.
 */
IEM_CIMPL_DEF_0(iemCImpl_vmmcall)
{
    if (IEM_SVM_IS_CTRL_INTERCEPT_SET(pVCpu, SVM_CTRL_INTERCEPT_VMMCALL))
    {
        Log(("vmmcall: Guest intercept -> #VMEXIT\n"));
        IEM_SVM_VMEXIT_RET(pVCpu, SVM_EXIT_VMMCALL, 0 /* uExitInfo1 */, 0 /* uExitInfo2 */);
    }

    /* This is a little bit more complicated than the VT-x version because HM/SVM may
       patch MOV CR8 instructions to speed up APIC.TPR access for 32-bit windows guests. */
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    if (VM_IS_HM_ENABLED(pVM))
    {
        int rc = HMHCMaybeMovTprSvmHypercall(pVM, pVCpu);
        if (RT_SUCCESS(rc))
        {
            Log(("vmmcall: MovTpr\n"));
            return VINF_SUCCESS;
        }
    }

    /* Join forces with vmcall. */
    return IEM_CIMPL_CALL_1(iemCImpl_Hypercall, OP_VMMCALL);
}

