/* $Id: HMSVMAll.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * HM SVM (AMD-V) - All contexts.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_HM
#define VMCPU_INCL_CPUM_GST_CTX
#include "HMInternal.h"
#include <VBox/vmm/apic.h>
#include <VBox/vmm/gim.h>
#include <VBox/vmm/iem.h>
#include <VBox/vmm/vmcc.h>

#include <VBox/err.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h> /* ASMCpuId */
#endif



/**
 * Emulates a simple MOV TPR (CR8) instruction.
 *
 * Used for TPR patching on 32-bit guests. This simply looks up the patch record
 * at EIP and does the required.
 *
 * This VMMCALL is used a fallback mechanism when mov to/from cr8 isn't exactly
 * like how we want it to be (e.g. not followed by shr 4 as is usually done for
 * TPR). See hmR3ReplaceTprInstr() for the details.
 *
 * @returns VBox status code.
 * @retval VINF_SUCCESS if the access was handled successfully, RIP + RFLAGS updated.
 * @retval VERR_NOT_FOUND if no patch record for this RIP could be found.
 * @retval VERR_SVM_UNEXPECTED_PATCH_TYPE if the found patch type is invalid.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   pVCpu               The cross context virtual CPU structure.
 */
VMM_INT_DECL(int) hmEmulateSvmMovTpr(PVMCC pVM, PVMCPUCC pVCpu)
{
    PCPUMCTX pCtx = &pVCpu->cpum.GstCtx;
    Log4(("Emulated VMMCall TPR access replacement at RIP=%RGv\n", pCtx->rip));

    AssertCompile(DISGREG_EAX  == X86_GREG_xAX);
    AssertCompile(DISGREG_ECX  == X86_GREG_xCX);
    AssertCompile(DISGREG_EDX  == X86_GREG_xDX);
    AssertCompile(DISGREG_EBX  == X86_GREG_xBX);
    AssertCompile(DISGREG_ESP  == X86_GREG_xSP);
    AssertCompile(DISGREG_EBP  == X86_GREG_xBP);
    AssertCompile(DISGREG_ESI  == X86_GREG_xSI);
    AssertCompile(DISGREG_EDI  == X86_GREG_xDI);
    AssertCompile(DISGREG_R8D  == X86_GREG_x8);
    AssertCompile(DISGREG_R9D  == X86_GREG_x9);
    AssertCompile(DISGREG_R10D == X86_GREG_x10);
    AssertCompile(DISGREG_R11D == X86_GREG_x11);
    AssertCompile(DISGREG_R12D == X86_GREG_x12);
    AssertCompile(DISGREG_R13D == X86_GREG_x13);
    AssertCompile(DISGREG_R14D == X86_GREG_x14);
    AssertCompile(DISGREG_R15D == X86_GREG_x15);

    /*
     * We do this in a loop as we increment the RIP after a successful emulation
     * and the new RIP may be a patched instruction which needs emulation as well.
     */
    bool fPatchFound = false;
    for (;;)
    {
        PHMTPRPATCH pPatch = (PHMTPRPATCH)RTAvloU32Get(&pVM->hm.s.PatchTree, (AVLOU32KEY)pCtx->eip);
        if (!pPatch)
            break;
        fPatchFound = true;

        uint8_t u8Tpr;
        switch (pPatch->enmType)
        {
            case HMTPRINSTR_READ:
            {
                bool fPending;
                int  rc = APICGetTpr(pVCpu, &u8Tpr, &fPending, NULL /* pu8PendingIrq */);
                AssertRC(rc);

                uint8_t idxReg = pPatch->uDstOperand;
                AssertStmt(idxReg < RT_ELEMENTS(pCtx->aGRegs), idxReg = RT_ELEMENTS(pCtx->aGRegs) - 1);
                pCtx->aGRegs[idxReg].u64 = u8Tpr;
                pCtx->rip += pPatch->cbOp;
                pCtx->eflags.Bits.u1RF = 0;
                break;
            }

            case HMTPRINSTR_WRITE_REG:
            case HMTPRINSTR_WRITE_IMM:
            {
                if (pPatch->enmType == HMTPRINSTR_WRITE_REG)
                {
                    uint8_t idxReg = pPatch->uSrcOperand;
                    AssertStmt(idxReg < RT_ELEMENTS(pCtx->aGRegs), idxReg = RT_ELEMENTS(pCtx->aGRegs) - 1);
                    u8Tpr = pCtx->aGRegs[idxReg].u8;
                }
                else
                    u8Tpr = (uint8_t)pPatch->uSrcOperand;

                int rc2 = APICSetTpr(pVCpu, u8Tpr);
                AssertRC(rc2);
                pCtx->rip += pPatch->cbOp;
                pCtx->eflags.Bits.u1RF = 0;
                ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_GUEST_APIC_TPR
                                                         | HM_CHANGED_GUEST_RIP
                                                         | HM_CHANGED_GUEST_RFLAGS);
                break;
            }

            default:
            {
                AssertMsgFailed(("Unexpected patch type %d\n", pPatch->enmType));
                pVCpu->hm.s.u32HMError = pPatch->enmType;
                return VERR_SVM_UNEXPECTED_PATCH_TYPE;
            }
        }
    }

    return fPatchFound ? VINF_SUCCESS : VERR_NOT_FOUND;
}

#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
/**
 * Notification callback for when a \#VMEXIT happens outside SVM R0 code (e.g.
 * in IEM).
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pCtx            Pointer to the guest-CPU context.
 *
 * @sa      hmR0SvmVmRunCacheVmcb.
 */
VMM_INT_DECL(void) HMNotifySvmNstGstVmexit(PVMCPUCC pVCpu, PCPUMCTX pCtx)
{
    PSVMNESTEDVMCBCACHE pVmcbNstGstCache = &pVCpu->hm.s.svm.NstGstVmcbCache;
    if (pVmcbNstGstCache->fCacheValid)
    {
        /*
         * Restore fields as our own code might look at the VMCB controls as part
         * of the #VMEXIT handling in IEM. Otherwise, strictly speaking we don't need to
         * restore these fields because currently none of them are written back to memory
         * by a physical CPU on #VMEXIT.
         */
        PSVMVMCBCTRL pVmcbNstGstCtrl = &pCtx->hwvirt.svm.Vmcb.ctrl;
        pVmcbNstGstCtrl->u16InterceptRdCRx                 = pVmcbNstGstCache->u16InterceptRdCRx;
        pVmcbNstGstCtrl->u16InterceptWrCRx                 = pVmcbNstGstCache->u16InterceptWrCRx;
        pVmcbNstGstCtrl->u16InterceptRdDRx                 = pVmcbNstGstCache->u16InterceptRdDRx;
        pVmcbNstGstCtrl->u16InterceptWrDRx                 = pVmcbNstGstCache->u16InterceptWrDRx;
        pVmcbNstGstCtrl->u16PauseFilterThreshold           = pVmcbNstGstCache->u16PauseFilterThreshold;
        pVmcbNstGstCtrl->u16PauseFilterCount               = pVmcbNstGstCache->u16PauseFilterCount;
        pVmcbNstGstCtrl->u32InterceptXcpt                  = pVmcbNstGstCache->u32InterceptXcpt;
        pVmcbNstGstCtrl->u64InterceptCtrl                  = pVmcbNstGstCache->u64InterceptCtrl;
        pVmcbNstGstCtrl->u64TSCOffset                      = pVmcbNstGstCache->u64TSCOffset;
        pVmcbNstGstCtrl->IntCtrl.n.u1VIntrMasking          = pVmcbNstGstCache->fVIntrMasking;
        pVmcbNstGstCtrl->NestedPagingCtrl.n.u1NestedPaging = pVmcbNstGstCache->fNestedPaging;
        pVmcbNstGstCtrl->LbrVirt.n.u1LbrVirt               = pVmcbNstGstCache->fLbrVirt;
        pVmcbNstGstCache->fCacheValid = false;
    }

    /*
     * Transitions to ring-3 flag a full CPU-state change except if we transition to ring-3
     * in response to a physical CPU interrupt as no changes to the guest-CPU state are
     * expected (see VINF_EM_RAW_INTERRUPT handling in hmR0SvmExitToRing3).
     *
     * However, with nested-guests, the state -can- change on trips to ring-3 for we might
     * try to inject a nested-guest physical interrupt and cause a SVM_EXIT_INTR #VMEXIT for
     * the nested-guest from ring-3. Import the complete state here as we will be swapping
     * to the guest VMCB after the #VMEXIT.
     */
    CPUMImportGuestStateOnDemand(pVCpu, CPUMCTX_EXTRN_ALL);
    CPUM_ASSERT_NOT_EXTRN(pVCpu, CPUMCTX_EXTRN_ALL);
    ASMAtomicUoOrU64(&pVCpu->hm.s.fCtxChanged, HM_CHANGED_ALL_GUEST);
}
#endif

/**
 * Checks if the Virtual GIF (Global Interrupt Flag) feature is supported and
 * enabled for the VM.
 *
 * @returns @c true if VGIF is enabled, @c false otherwise.
 * @param   pVM         The cross context VM structure.
 *
 * @remarks This value returned by this functions is expected by the callers not
 *          to change throughout the lifetime of the VM.
 */
VMM_INT_DECL(bool) HMIsSvmVGifActive(PCVMCC pVM)
{
#ifdef IN_RING0
    bool const fVGif    = RT_BOOL(g_fHmSvmFeatures & X86_CPUID_SVM_FEATURE_EDX_VGIF);
#else
    bool const fVGif    = RT_BOOL(pVM->hm.s.ForR3.svm.fFeatures & X86_CPUID_SVM_FEATURE_EDX_VGIF);
#endif
    return fVGif && pVM->hm.s.svm.fVGif;
}


/**
 * Interface used by IEM to handle patched TPR accesses.
 *
 * @returns VBox status code
 * @retval  VINF_SUCCESS if hypercall was handled, RIP + RFLAGS all dealt with.
 * @retval  VERR_NOT_FOUND if hypercall was _not_ handled.
 * @retval  VERR_SVM_UNEXPECTED_PATCH_TYPE on IPE.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   pVCpu               The cross context virtual CPU structure.
 */
VMM_INT_DECL(int) HMHCMaybeMovTprSvmHypercall(PVMCC pVM, PVMCPUCC pVCpu)
{
    if (pVM->hm.s.fTprPatchingAllowed)
    {
        int rc = hmEmulateSvmMovTpr(pVM, pVCpu);
        if (RT_SUCCESS(rc))
            return VINF_SUCCESS;
        return rc;
    }
    return VERR_NOT_FOUND;
}


#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
/**
 * Checks if the current AMD CPU is subject to erratum 170 "In SVM mode,
 * incorrect code bytes may be fetched after a world-switch".
 *
 * @param   pu32Family      Where to store the CPU family (can be NULL).
 * @param   pu32Model       Where to store the CPU model (can be NULL).
 * @param   pu32Stepping    Where to store the CPU stepping (can be NULL).
 * @returns true if the erratum applies, false otherwise.
 */
VMM_INT_DECL(int) HMIsSubjectToSvmErratum170(uint32_t *pu32Family, uint32_t *pu32Model, uint32_t *pu32Stepping)
{
    /*
     * Erratum 170 which requires a forced TLB flush for each world switch:
     * See AMD spec. "Revision Guide for AMD NPT Family 0Fh Processors".
     *
     * All BH-G1/2 and DH-G1/2 models include a fix:
     * Athlon X2:   0x6b 1/2
     *              0x68 1/2
     * Athlon 64:   0x7f 1
     *              0x6f 2
     * Sempron:     0x7f 1/2
     *              0x6f 2
     *              0x6c 2
     *              0x7c 2
     * Turion 64:   0x68 2
     */
    uint32_t u32Dummy;
    uint32_t u32Version, u32Family, u32Model, u32Stepping, u32BaseFamily;
    ASMCpuId(1, &u32Version, &u32Dummy, &u32Dummy, &u32Dummy);
    u32BaseFamily = (u32Version >> 8) & 0xf;
    u32Family     = u32BaseFamily + (u32BaseFamily == 0xf ? ((u32Version >> 20) & 0x7f) : 0);
    u32Model      = ((u32Version >> 4) & 0xf);
    u32Model      = u32Model | ((u32BaseFamily == 0xf ? (u32Version >> 16) & 0x0f : 0) << 4);
    u32Stepping   = u32Version & 0xf;

    bool fErratumApplies = false;
    if (   u32Family == 0xf
        && !((u32Model == 0x68 || u32Model == 0x6b || u32Model == 0x7f) && u32Stepping >= 1)
        && !((u32Model == 0x6f || u32Model == 0x6c || u32Model == 0x7c) && u32Stepping >= 2))
        fErratumApplies = true;

    if (pu32Family)
        *pu32Family   = u32Family;
    if (pu32Model)
        *pu32Model    = u32Model;
    if (pu32Stepping)
        *pu32Stepping = u32Stepping;

    return fErratumApplies;
}
#endif


/**
 * Converts an SVM event type to a TRPM event type.
 *
 * @returns The TRPM event type.
 * @retval  TRPM_32BIT_HACK if the specified type of event isn't among the set
 *          of recognized trap types.
 *
 * @param   pEvent       Pointer to the SVM event.
 * @param   uVector      The vector associated with the event.
 */
VMM_INT_DECL(TRPMEVENT) HMSvmEventToTrpmEventType(PCSVMEVENT pEvent, uint8_t uVector)
{
    uint8_t const uType = pEvent->n.u3Type;
    switch (uType)
    {
        case SVM_EVENT_EXTERNAL_IRQ:    return TRPM_HARDWARE_INT;
        case SVM_EVENT_SOFTWARE_INT:    return TRPM_SOFTWARE_INT;
        case SVM_EVENT_NMI:             return TRPM_NMI;
        case SVM_EVENT_EXCEPTION:
        {
            if (   uVector == X86_XCPT_BP
                || uVector == X86_XCPT_OF)
                return TRPM_SOFTWARE_INT;
            return TRPM_TRAP;
        }
        default:
            break;
    }
    AssertMsgFailed(("HMSvmEventToTrpmEvent: Invalid pending-event type %#x\n", uType));
    return TRPM_32BIT_HACK;
}


/**
 * Gets the SVM nested-guest control intercepts if cached by HM.
 *
 * @returns @c true on success, @c false otherwise.
 * @param   pVCpu           The cross context virtual CPU structure of the calling
 *                          EMT.
 * @param   pu64Intercepts  Where to store the control intercepts. Only updated when
 *                          @c true is returned.
 */
VMM_INT_DECL(bool) HMGetGuestSvmCtrlIntercepts(PCVMCPU pVCpu, uint64_t *pu64Intercepts)
{
    Assert(pu64Intercepts);
    PCSVMNESTEDVMCBCACHE pVmcbNstGstCache = &pVCpu->hm.s.svm.NstGstVmcbCache;
    if (pVmcbNstGstCache->fCacheValid)
    {
        *pu64Intercepts = pVmcbNstGstCache->u64InterceptCtrl;
        return true;
    }
    return false;
}


/**
 * Gets the SVM nested-guest CRx-read intercepts if cached by HM.
 *
 * @returns @c true on success, @c false otherwise.
 * @param   pVCpu           The cross context virtual CPU structure of the calling
 *                          EMT.
 * @param   pu16Intercepts  Where to store the CRx-read intercepts. Only updated
 *                          when @c true is returned.
 */
VMM_INT_DECL(bool) HMGetGuestSvmReadCRxIntercepts(PCVMCPU pVCpu, uint16_t *pu16Intercepts)
{
    Assert(pu16Intercepts);
    PCSVMNESTEDVMCBCACHE pVmcbNstGstCache = &pVCpu->hm.s.svm.NstGstVmcbCache;
    if (pVmcbNstGstCache->fCacheValid)
    {
        *pu16Intercepts = pVmcbNstGstCache->u16InterceptRdCRx;
        return true;
    }
    return false;
}


/**
 * Gets the SVM nested-guest CRx-write intercepts if cached by HM.
 *
 * @returns @c true on success, @c false otherwise.
 * @param   pVCpu           The cross context virtual CPU structure of the calling
 *                          EMT.
 * @param   pu16Intercepts  Where to store the CRx-write intercepts. Only updated
 *                          when @c true is returned.
 */
VMM_INT_DECL(bool) HMGetGuestSvmWriteCRxIntercepts(PCVMCPU pVCpu, uint16_t *pu16Intercepts)
{
    Assert(pu16Intercepts);
    PCSVMNESTEDVMCBCACHE pVmcbNstGstCache = &pVCpu->hm.s.svm.NstGstVmcbCache;
    if (pVmcbNstGstCache->fCacheValid)
    {
        *pu16Intercepts = pVmcbNstGstCache->u16InterceptWrCRx;
        return true;
    }
    return false;
}


/**
 * Gets the SVM nested-guest DRx-read intercepts if cached by HM.
 *
 * @returns @c true on success, @c false otherwise.
 * @param   pVCpu           The cross context virtual CPU structure of the calling
 *                          EMT.
 * @param   pu16Intercepts  Where to store the DRx-read intercepts. Only updated
 *                          when @c true is returned.
 */
VMM_INT_DECL(bool) HMGetGuestSvmReadDRxIntercepts(PCVMCPU pVCpu, uint16_t *pu16Intercepts)
{
    Assert(pu16Intercepts);
    PCSVMNESTEDVMCBCACHE pVmcbNstGstCache = &pVCpu->hm.s.svm.NstGstVmcbCache;
    if (pVmcbNstGstCache->fCacheValid)
    {
        *pu16Intercepts = pVmcbNstGstCache->u16InterceptRdDRx;
        return true;
    }
    return false;
}


/**
 * Gets the SVM nested-guest DRx-write intercepts if cached by HM.
 *
 * @returns @c true on success, @c false otherwise.
 * @param   pVCpu           The cross context virtual CPU structure of the calling
 *                          EMT.
 * @param   pu16Intercepts  Where to store the DRx-write intercepts. Only updated
 *                          when @c true is returned.
 */
VMM_INT_DECL(bool) HMGetGuestSvmWriteDRxIntercepts(PCVMCPU pVCpu, uint16_t *pu16Intercepts)
{
    Assert(pu16Intercepts);
    PCSVMNESTEDVMCBCACHE pVmcbNstGstCache = &pVCpu->hm.s.svm.NstGstVmcbCache;
    if (pVmcbNstGstCache->fCacheValid)
    {
        *pu16Intercepts = pVmcbNstGstCache->u16InterceptWrDRx;
        return true;
    }
    return false;
}


/**
 * Gets the SVM nested-guest exception intercepts if cached by HM.
 *
 * @returns @c true on success, @c false otherwise.
 * @param   pVCpu           The cross context virtual CPU structure of the calling
 *                          EMT.
 * @param   pu32Intercepts  Where to store the exception intercepts. Only updated
 *                          when @c true is returned.
 */
VMM_INT_DECL(bool) HMGetGuestSvmXcptIntercepts(PCVMCPU pVCpu, uint32_t *pu32Intercepts)
{
    Assert(pu32Intercepts);
    PCSVMNESTEDVMCBCACHE pVmcbNstGstCache = &pVCpu->hm.s.svm.NstGstVmcbCache;
    if (pVmcbNstGstCache->fCacheValid)
    {
        *pu32Intercepts = pVmcbNstGstCache->u32InterceptXcpt;
        return true;
    }
    return false;
}


/**
 * Checks if the nested-guest VMCB has virtual-interrupts masking enabled.
 *
 * @returns @c true on success, @c false otherwise.
 * @param   pVCpu           The cross context virtual CPU structure of the calling
 *                          EMT.
 * @param   pfVIntrMasking  Where to store the virtual-interrupt masking bit.
 *                          Updated only when @c true is returned.
 */
VMM_INT_DECL(bool) HMGetGuestSvmVirtIntrMasking(PCVMCPU pVCpu, bool *pfVIntrMasking)
{
    Assert(pfVIntrMasking);
    PCSVMNESTEDVMCBCACHE pVmcbNstGstCache = &pVCpu->hm.s.svm.NstGstVmcbCache;
    if (pVmcbNstGstCache->fCacheValid)
    {
        *pfVIntrMasking = pVmcbNstGstCache->fVIntrMasking;
        return true;
    }
    return false;
}


/**
 * Gets the SVM nested-guest nested-paging bit if cached by HM.
 *
 * @returns @c true on success, @c false otherwise.
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling EMT.
 * @param   pfNestedPaging      Where to store the nested-paging bit. Updated only
 *                              when @c true is returned.
 */
VMM_INT_DECL(bool) HMGetGuestSvmNestedPaging(PCVMCPU pVCpu, bool *pfNestedPaging)
{
    Assert(pfNestedPaging);
    PCSVMNESTEDVMCBCACHE pVmcbNstGstCache = &pVCpu->hm.s.svm.NstGstVmcbCache;
    if (pVmcbNstGstCache->fCacheValid)
    {
        *pfNestedPaging = pVmcbNstGstCache->fNestedPaging;
        return true;
    }
    return false;
}


/**
 * Returns the nested-guest VMCB pause-filter count.
 *
 * @returns @c true on success, @c false otherwise.
 * @param   pVCpu                   The cross context virtual CPU structure of the
 *                                  calling EMT.
 * @param   pu16PauseFilterCount    Where to store the pause-filter count. Only
 *                                  updated @c true is returned.
 */
VMM_INT_DECL(bool) HMGetGuestSvmPauseFilterCount(PCVMCPU pVCpu, uint16_t *pu16PauseFilterCount)
{
    Assert(pu16PauseFilterCount);
    PCSVMNESTEDVMCBCACHE pVmcbNstGstCache = &pVCpu->hm.s.svm.NstGstVmcbCache;
    if (pVmcbNstGstCache->fCacheValid)
    {
        *pu16PauseFilterCount = pVmcbNstGstCache->u16PauseFilterCount;
        return true;
    }
    return false;
}


/**
 * Returns the SVM nested-guest TSC offset if cached by HM.
 *
 * @returns The TSC offset after applying any nested-guest TSC offset.
 * @param   pVCpu           The cross context virtual CPU structure of the calling
 *                          EMT.
 * @param   pu64TscOffset   Where to store the TSC offset. Only updated when @c
 *                          true is returned.
 */
VMM_INT_DECL(bool) HMGetGuestSvmTscOffset(PCVMCPU pVCpu, uint64_t *pu64TscOffset)
{
    Assert(pu64TscOffset);
    PCSVMNESTEDVMCBCACHE pVmcbNstGstCache = &pVCpu->hm.s.svm.NstGstVmcbCache;
    if (pVmcbNstGstCache->fCacheValid)
    {
        *pu64TscOffset = pVmcbNstGstCache->u64TSCOffset;
        return true;
    }
    return false;
}

