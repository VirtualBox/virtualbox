/* $Id: IEMInline-x86.h 108590 2025-02-27 10:35:39Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - Inlined Functions, x86 target.
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

#ifndef VMM_INCLUDED_SRC_VMMAll_target_x86_IEMInline_x86_h
#define VMM_INCLUDED_SRC_VMMAll_target_x86_IEMInline_x86_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/err.h>


/**
 * Calculates the IEM_F_X86_AC flags.
 *
 * @returns IEM_F_X86_AC or zero
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling thread.
 */
DECL_FORCE_INLINE(uint32_t) iemCalcExecAcFlag(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_RFLAGS);
    Assert(CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, &pVCpu->cpum.GstCtx.ss));

    if (   !pVCpu->cpum.GstCtx.eflags.Bits.u1AC
        || (pVCpu->cpum.GstCtx.cr0 & (X86_CR0_AM | X86_CR0_PE)) != (X86_CR0_AM | X86_CR0_PE)
        || (   !pVCpu->cpum.GstCtx.eflags.Bits.u1VM
            && pVCpu->cpum.GstCtx.ss.Attr.n.u2Dpl != 3))
        return 0;
    return IEM_F_X86_AC;
}


/**
 * Calculates the IEM_F_MODE_X86_32BIT_FLAT flag.
 *
 * Checks if CS, SS, DS and SS are all wide open flat 32-bit segments. This will
 * reject expand down data segments and conforming code segments.
 *
 * ASSUMES that the CPU is in 32-bit mode.
 *
 * @note    Will return zero when if any of the segment register state is marked
 *          external, this must be factored into assertions checking fExec
 *          consistency.
 *
 * @returns IEM_F_MODE_X86_32BIT_FLAT or zero.
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling thread.
 * @sa      iemCalc32BitFlatIndicatorEsDs
 */
DECL_FORCE_INLINE(uint32_t) iemCalc32BitFlatIndicator(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    AssertCompile(X86_SEL_TYPE_DOWN == X86_SEL_TYPE_CONF);
    return (  (    pVCpu->cpum.GstCtx.es.Attr.u
                 | pVCpu->cpum.GstCtx.cs.Attr.u
                 | pVCpu->cpum.GstCtx.ss.Attr.u
                 | pVCpu->cpum.GstCtx.ds.Attr.u)
              & (X86_SEL_TYPE_ACCESSED | X86DESCATTR_G | X86DESCATTR_D | X86DESCATTR_P | X86_SEL_TYPE_DOWN | X86DESCATTR_UNUSABLE))
           ==   (X86_SEL_TYPE_ACCESSED | X86DESCATTR_G | X86DESCATTR_D | X86DESCATTR_P)
        &&    (  (pVCpu->cpum.GstCtx.es.u32Limit + 1)
               | (pVCpu->cpum.GstCtx.cs.u32Limit + 1)
               | (pVCpu->cpum.GstCtx.ss.u32Limit + 1)
               | (pVCpu->cpum.GstCtx.ds.u32Limit + 1))
           == 0
        &&    (  pVCpu->cpum.GstCtx.es.u64Base
               | pVCpu->cpum.GstCtx.cs.u64Base
               | pVCpu->cpum.GstCtx.ss.u64Base
               | pVCpu->cpum.GstCtx.ds.u64Base)
           == 0
        && !(pVCpu->cpum.GstCtx.fExtrn & (CPUMCTX_EXTRN_ES | CPUMCTX_EXTRN_CS | CPUMCTX_EXTRN_SS | CPUMCTX_EXTRN_ES))
        ? IEM_F_MODE_X86_32BIT_FLAT : 0;
}


/**
 * Calculates the IEM_F_MODE_X86_32BIT_FLAT flag, ASSUMING the CS and SS are
 * flat already.
 *
 * This is used by sysenter.
 *
 * @note    Will return zero when if any of the segment register state is marked
 *          external, this must be factored into assertions checking fExec
 *          consistency.
 *
 * @returns IEM_F_MODE_X86_32BIT_FLAT or zero.
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling thread.
 * @sa      iemCalc32BitFlatIndicator
 */
DECL_FORCE_INLINE(uint32_t) iemCalc32BitFlatIndicatorEsDs(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    AssertCompile(X86_SEL_TYPE_DOWN == X86_SEL_TYPE_CONF);
    return (  (    pVCpu->cpum.GstCtx.es.Attr.u
                 | pVCpu->cpum.GstCtx.ds.Attr.u)
              & (X86_SEL_TYPE_ACCESSED | X86DESCATTR_G | X86DESCATTR_D | X86DESCATTR_P | X86_SEL_TYPE_DOWN | X86DESCATTR_UNUSABLE))
           ==   (X86_SEL_TYPE_ACCESSED | X86DESCATTR_G | X86DESCATTR_D | X86DESCATTR_P)
        &&    (  (pVCpu->cpum.GstCtx.es.u32Limit + 1)
               | (pVCpu->cpum.GstCtx.ds.u32Limit + 1))
           == 0
        &&    (  pVCpu->cpum.GstCtx.es.u64Base
               | pVCpu->cpum.GstCtx.ds.u64Base)
           == 0
        && !(pVCpu->cpum.GstCtx.fExtrn & (CPUMCTX_EXTRN_ES | CPUMCTX_EXTRN_CS | CPUMCTX_EXTRN_SS | CPUMCTX_EXTRN_ES))
        ? IEM_F_MODE_X86_32BIT_FLAT : 0;
}


/**
 * Calculates the IEM_F_MODE_XXX, CPL and AC flags.
 *
 * @returns IEM_F_MODE_XXX, IEM_F_X86_CPL_MASK and IEM_F_X86_AC.
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling thread.
 */
DECL_FORCE_INLINE(uint32_t) iemCalcExecModeAndCplFlags(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    /*
     * We're duplicates code from CPUMGetGuestCPL and CPUMIsGuestIn64BitCodeEx
     * here to try get this done as efficiently as possible.
     */
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_EFER | CPUMCTX_EXTRN_RFLAGS | CPUMCTX_EXTRN_SS | CPUMCTX_EXTRN_CS);

    if (pVCpu->cpum.GstCtx.cr0 & X86_CR0_PE)
    {
        if (!pVCpu->cpum.GstCtx.eflags.Bits.u1VM)
        {
            Assert(CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, &pVCpu->cpum.GstCtx.ss));
            uint32_t fExec = ((uint32_t)pVCpu->cpum.GstCtx.ss.Attr.n.u2Dpl << IEM_F_X86_CPL_SHIFT);
            if (   !pVCpu->cpum.GstCtx.eflags.Bits.u1AC
                || !(pVCpu->cpum.GstCtx.cr0 & X86_CR0_AM)
                || fExec != (3U << IEM_F_X86_CPL_SHIFT))
            { /* likely */ }
            else
                fExec |= IEM_F_X86_AC;

            if (pVCpu->cpum.GstCtx.cs.Attr.n.u1DefBig)
            {
                Assert(!pVCpu->cpum.GstCtx.cs.Attr.n.u1Long || !(pVCpu->cpum.GstCtx.msrEFER & MSR_K6_EFER_LMA));
                fExec |= IEM_F_MODE_X86_32BIT_PROT | iemCalc32BitFlatIndicator(pVCpu);
            }
            else if (   pVCpu->cpum.GstCtx.cs.Attr.n.u1Long
                     && (pVCpu->cpum.GstCtx.msrEFER & MSR_K6_EFER_LMA))
                fExec |= IEM_F_MODE_X86_64BIT;
            else if (IEM_GET_TARGET_CPU(pVCpu) >= IEMTARGETCPU_386)
                fExec |= IEM_F_MODE_X86_16BIT_PROT;
            else
                fExec |= IEM_F_MODE_X86_16BIT_PROT_PRE_386;
            return fExec;
        }
        if (   !pVCpu->cpum.GstCtx.eflags.Bits.u1AC
            || !(pVCpu->cpum.GstCtx.cr0 & X86_CR0_AM))
            return IEM_F_MODE_X86_16BIT_PROT_V86 | (UINT32_C(3) << IEM_F_X86_CPL_SHIFT);
        return IEM_F_MODE_X86_16BIT_PROT_V86 | (UINT32_C(3) << IEM_F_X86_CPL_SHIFT) | IEM_F_X86_AC;
    }

    /* Real mode is zero; CPL set to 3 for VT-x real-mode emulation. */
    if (RT_LIKELY(!pVCpu->cpum.GstCtx.cs.Attr.n.u1DefBig))
    {
        if (IEM_GET_TARGET_CPU(pVCpu) >= IEMTARGETCPU_386)
            return IEM_F_MODE_X86_16BIT;
        return IEM_F_MODE_X86_16BIT_PRE_386;
    }

    /* 32-bit unreal mode. */
    return IEM_F_MODE_X86_32BIT | iemCalc32BitFlatIndicator(pVCpu);
}


/**
 * Calculates the AMD-V and VT-x related context flags.
 *
 * @returns 0 or a combination of IEM_F_X86_CTX_IN_GUEST, IEM_F_X86_CTX_SVM and
 *          IEM_F_X86_CTX_VMX.
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling thread.
 */
DECL_FORCE_INLINE(uint32_t) iemCalcExecHwVirtFlags(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    /*
     * This duplicates code from CPUMIsGuestVmxEnabled, CPUMIsGuestSvmEnabled
     * and CPUMIsGuestInNestedHwvirtMode to some extent.
     */
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR4 | CPUMCTX_EXTRN_EFER);

    AssertCompile(X86_CR4_VMXE != MSR_K6_EFER_SVME);
    uint64_t const fTmp = (pVCpu->cpum.GstCtx.cr4     & X86_CR4_VMXE)
                        | (pVCpu->cpum.GstCtx.msrEFER & MSR_K6_EFER_SVME);
    if (RT_LIKELY(!fTmp))
        return 0; /* likely */

    if (fTmp & X86_CR4_VMXE)
    {
        Assert(pVCpu->cpum.GstCtx.hwvirt.enmHwvirt == CPUMHWVIRT_VMX);
        if (pVCpu->cpum.GstCtx.hwvirt.vmx.fInVmxNonRootMode)
            return IEM_F_X86_CTX_VMX | IEM_F_X86_CTX_IN_GUEST;
        return IEM_F_X86_CTX_VMX;
    }

    Assert(pVCpu->cpum.GstCtx.hwvirt.enmHwvirt == CPUMHWVIRT_SVM);
    if (pVCpu->cpum.GstCtx.hwvirt.svm.Vmcb.ctrl.u64InterceptCtrl & SVM_CTRL_INTERCEPT_VMRUN)
        return IEM_F_X86_CTX_SVM | IEM_F_X86_CTX_IN_GUEST;
    return IEM_F_X86_CTX_SVM;
}

#ifdef VBOX_INCLUDED_vmm_dbgf_h /* VM::dbgf.ro.cEnabledHwBreakpoints is only accessible if VBox/vmm/dbgf.h is included. */

/**
 * Calculates IEM_F_BRK_PENDING_XXX (IEM_F_PENDING_BRK_MASK) flags.
 *
 * @returns IEM_F_BRK_PENDING_XXX or zero.
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling thread.
 */
DECL_FORCE_INLINE(uint32_t) iemCalcExecDbgFlags(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_DR7);

    if (RT_LIKELY(   !(pVCpu->cpum.GstCtx.dr[7] & X86_DR7_ENABLED_MASK)
                  && pVCpu->CTX_SUFF(pVM)->dbgf.ro.cEnabledHwBreakpoints == 0))
        return 0;
    return iemCalcExecDbgFlagsSlow(pVCpu);
}


DECL_FORCE_INLINE(uint32_t) iemCalcExecFlags(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    return iemCalcExecModeAndCplFlags(pVCpu)
         | iemCalcExecHwVirtFlags(pVCpu)
         /* SMM is not yet implemented */
         | iemCalcExecDbgFlags(pVCpu)
         ;
}


/**
 * Re-calculates the MODE and CPL parts of IEMCPU::fExec.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling thread.
 */
DECL_FORCE_INLINE(void) iemRecalcExecModeAndCplAndAcFlags(PVMCPUCC pVCpu)
{
    pVCpu->iem.s.fExec = (pVCpu->iem.s.fExec & ~(IEM_F_MODE_MASK | IEM_F_X86_CPL_MASK | IEM_F_X86_AC))
                       | iemCalcExecModeAndCplFlags(pVCpu);
}


/**
 * Re-calculates the IEM_F_PENDING_BRK_MASK part of IEMCPU::fExec.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling thread.
 */
DECL_FORCE_INLINE(void) iemRecalcExecDbgFlags(PVMCPUCC pVCpu)
{
    pVCpu->iem.s.fExec = (pVCpu->iem.s.fExec & ~IEM_F_PENDING_BRK_MASK)
                       | iemCalcExecDbgFlags(pVCpu);
}

#endif /* VBOX_INCLUDED_vmm_dbgf_h */

/**
 * Macro used by the IEMExec* method to check the given instruction length.
 *
 * Will return on failure!
 *
 * @param   a_cbInstr   The given instruction length.
 * @param   a_cbMin     The minimum length.
 */
#define IEMEXEC_ASSERT_INSTR_LEN_RETURN(a_cbInstr, a_cbMin) \
    AssertMsgReturn((unsigned)(a_cbInstr) - (unsigned)(a_cbMin) <= (unsigned)15 - (unsigned)(a_cbMin), \
                    ("cbInstr=%u cbMin=%u\n", (a_cbInstr), (a_cbMin)), VERR_IEM_INVALID_INSTR_LENGTH)


/** @name  Misc Worker Functions.
 * @{
 */

/**
 * Gets the correct EFLAGS regardless of whether PATM stores parts of them or
 * not (kind of obsolete now).
 *
 * @param   a_pVCpu The cross context virtual CPU structure of the calling thread.
 */
#define IEMMISC_GET_EFL(a_pVCpu)            ( (a_pVCpu)->cpum.GstCtx.eflags.u  )

/**
 * Updates the EFLAGS in the correct manner wrt. PATM (kind of obsolete).
 *
 * @param   a_pVCpu The cross context virtual CPU structure of the calling thread.
 * @param   a_fEfl  The new EFLAGS.
 */
#define IEMMISC_SET_EFL(a_pVCpu, a_fEfl)    do { (a_pVCpu)->cpum.GstCtx.eflags.u = (a_fEfl); } while (0)


/**
 * Loads a NULL data selector into a selector register, both the hidden and
 * visible parts, in protected mode.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pSReg               Pointer to the segment register.
 * @param   uRpl                The RPL.
 */
DECLINLINE(void) iemHlpLoadNullDataSelectorProt(PVMCPUCC pVCpu, PCPUMSELREG pSReg, RTSEL uRpl) RT_NOEXCEPT
{
    /** @todo Testcase: write a testcase checking what happends when loading a NULL
     *        data selector in protected mode. */
    pSReg->Sel      = uRpl;
    pSReg->ValidSel = uRpl;
    pSReg->fFlags   = CPUMSELREG_FLAGS_VALID;
    if (IEM_IS_GUEST_CPU_INTEL(pVCpu))
    {
        /* VT-x (Intel 3960x) observed doing something like this. */
        pSReg->Attr.u   = X86DESCATTR_UNUSABLE | X86DESCATTR_G | X86DESCATTR_D | (IEM_GET_CPL(pVCpu) << X86DESCATTR_DPL_SHIFT);
        pSReg->u32Limit = UINT32_MAX;
        pSReg->u64Base  = 0;
    }
    else
    {
        pSReg->Attr.u   = X86DESCATTR_UNUSABLE;
        pSReg->u32Limit = 0;
        pSReg->u64Base  = 0;
    }
}

/** @} */


/** @name   Register Access.
 * @{
 */

/**
 * Gets a reference (pointer) to the specified hidden segment register.
 *
 * @returns Hidden register reference.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iSegReg             The segment register.
 */
DECL_FORCE_INLINE(PCPUMSELREG) iemSRegGetHid(PVMCPUCC pVCpu, uint8_t iSegReg) RT_NOEXCEPT
{
    Assert(iSegReg < X86_SREG_COUNT);
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_SREG_FROM_IDX(iSegReg));
    PCPUMSELREG pSReg = &pVCpu->cpum.GstCtx.aSRegs[iSegReg];

    Assert(CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, pSReg));
    return pSReg;
}


/**
 * Ensures that the given hidden segment register is up to date.
 *
 * @returns Hidden register reference.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pSReg               The segment register.
 */
DECL_FORCE_INLINE(PCPUMSELREG) iemSRegUpdateHid(PVMCPUCC pVCpu, PCPUMSELREG pSReg) RT_NOEXCEPT
{
    Assert(CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, pSReg));
    NOREF(pVCpu);
    return pSReg;
}


/**
 * Gets a reference (pointer) to the specified segment register (the selector
 * value).
 *
 * @returns Pointer to the selector variable.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iSegReg             The segment register.
 */
DECL_FORCE_INLINE(uint16_t *) iemSRegRef(PVMCPUCC pVCpu, uint8_t iSegReg) RT_NOEXCEPT
{
    Assert(iSegReg < X86_SREG_COUNT);
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_SREG_FROM_IDX(iSegReg));
    return &pVCpu->cpum.GstCtx.aSRegs[iSegReg].Sel;
}


/**
 * Fetches the selector value of a segment register.
 *
 * @returns The selector value.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iSegReg             The segment register.
 */
DECL_FORCE_INLINE(uint16_t) iemSRegFetchU16(PVMCPUCC pVCpu, uint8_t iSegReg) RT_NOEXCEPT
{
    Assert(iSegReg < X86_SREG_COUNT);
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_SREG_FROM_IDX(iSegReg));
    return pVCpu->cpum.GstCtx.aSRegs[iSegReg].Sel;
}


/**
 * Fetches the base address value of a segment register.
 *
 * @returns The selector value.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iSegReg             The segment register.
 */
DECL_FORCE_INLINE(uint64_t) iemSRegBaseFetchU64(PVMCPUCC pVCpu, uint8_t iSegReg) RT_NOEXCEPT
{
    Assert(iSegReg < X86_SREG_COUNT);
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_SREG_FROM_IDX(iSegReg));
    return pVCpu->cpum.GstCtx.aSRegs[iSegReg].u64Base;
}


/**
 * Gets a reference (pointer) to the specified general purpose register.
 *
 * @returns Register reference.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iReg                The general purpose register.
 */
DECL_FORCE_INLINE(void *) iemGRegRef(PVMCPUCC pVCpu, uint8_t iReg) RT_NOEXCEPT
{
    Assert(iReg < 16);
    return &pVCpu->cpum.GstCtx.aGRegs[iReg];
}


#ifndef IEM_WITH_OPAQUE_DECODER_STATE
/**
 * Gets a reference (pointer) to the specified 8-bit general purpose register.
 *
 * Because of AH, CH, DH and BH we cannot use iemGRegRef directly here.
 *
 * @returns Register reference.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iReg                The register.
 */
DECL_FORCE_INLINE(uint8_t *) iemGRegRefU8(PVMCPUCC pVCpu, uint8_t iReg) RT_NOEXCEPT
{
    if (iReg < 4 || (pVCpu->iem.s.fPrefixes & (IEM_OP_PRF_REX | IEM_OP_PRF_VEX)))
    {
        Assert(iReg < 16);
        return &pVCpu->cpum.GstCtx.aGRegs[iReg].u8;
    }
    /* high 8-bit register. */
    Assert(iReg < 8);
    return &pVCpu->cpum.GstCtx.aGRegs[iReg & 3].bHi;
}
#endif


/**
 * Gets a reference (pointer) to the specified 8-bit general purpose register,
 * alternative version with extended (20) register index.
 *
 * @returns Register reference.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iRegEx              The register.  The 16 first are regular ones,
 *                              whereas 16 thru 19 maps to AH, CH, DH and BH.
 */
DECL_FORCE_INLINE(uint8_t *) iemGRegRefU8Ex(PVMCPUCC pVCpu, uint8_t iRegEx) RT_NOEXCEPT
{
    /** @todo This could be done by double indexing on little endian hosts:
     *  return &pVCpu->cpum.GstCtx.aGRegs[iRegEx & 15].ab[iRegEx >> 4]; */
    if (iRegEx < 16)
        return &pVCpu->cpum.GstCtx.aGRegs[iRegEx].u8;

    /* high 8-bit register. */
    Assert(iRegEx < 20);
    return &pVCpu->cpum.GstCtx.aGRegs[iRegEx & 3].bHi;
}


/**
 * Gets a reference (pointer) to the specified 16-bit general purpose register.
 *
 * @returns Register reference.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iReg                The register.
 */
DECL_FORCE_INLINE(uint16_t *) iemGRegRefU16(PVMCPUCC pVCpu, uint8_t iReg) RT_NOEXCEPT
{
    Assert(iReg < 16);
    return &pVCpu->cpum.GstCtx.aGRegs[iReg].u16;
}


/**
 * Gets a reference (pointer) to the specified 32-bit general purpose register.
 *
 * @returns Register reference.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iReg                The register.
 */
DECL_FORCE_INLINE(uint32_t *) iemGRegRefU32(PVMCPUCC pVCpu, uint8_t iReg) RT_NOEXCEPT
{
    Assert(iReg < 16);
    return &pVCpu->cpum.GstCtx.aGRegs[iReg].u32;
}


/**
 * Gets a reference (pointer) to the specified signed 32-bit general purpose register.
 *
 * @returns Register reference.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iReg                The register.
 */
DECL_FORCE_INLINE(int32_t *) iemGRegRefI32(PVMCPUCC pVCpu, uint8_t iReg) RT_NOEXCEPT
{
    Assert(iReg < 16);
    return (int32_t *)&pVCpu->cpum.GstCtx.aGRegs[iReg].u32;
}


/**
 * Gets a reference (pointer) to the specified 64-bit general purpose register.
 *
 * @returns Register reference.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iReg                The register.
 */
DECL_FORCE_INLINE(uint64_t *) iemGRegRefU64(PVMCPUCC pVCpu, uint8_t iReg) RT_NOEXCEPT
{
    Assert(iReg < 64);
    return &pVCpu->cpum.GstCtx.aGRegs[iReg].u64;
}


/**
 * Gets a reference (pointer) to the specified signed 64-bit general purpose register.
 *
 * @returns Register reference.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iReg                The register.
 */
DECL_FORCE_INLINE(int64_t *) iemGRegRefI64(PVMCPUCC pVCpu, uint8_t iReg) RT_NOEXCEPT
{
    Assert(iReg < 16);
    return (int64_t *)&pVCpu->cpum.GstCtx.aGRegs[iReg].u64;
}


/**
 * Gets a reference (pointer) to the specified segment register's base address.
 *
 * @returns Segment register base address reference.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iSegReg             The segment selector.
 */
DECL_FORCE_INLINE(uint64_t *) iemSRegBaseRefU64(PVMCPUCC pVCpu, uint8_t iSegReg) RT_NOEXCEPT
{
    Assert(iSegReg < X86_SREG_COUNT);
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_SREG_FROM_IDX(iSegReg));
    return &pVCpu->cpum.GstCtx.aSRegs[iSegReg].u64Base;
}


#ifndef IEM_WITH_OPAQUE_DECODER_STATE
/**
 * Fetches the value of a 8-bit general purpose register.
 *
 * @returns The register value.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iReg                The register.
 */
DECL_FORCE_INLINE(uint8_t) iemGRegFetchU8(PVMCPUCC pVCpu, uint8_t iReg) RT_NOEXCEPT
{
    return *iemGRegRefU8(pVCpu, iReg);
}
#endif


/**
 * Fetches the value of a 8-bit general purpose register, alternative version
 * with extended (20) register index.

 * @returns The register value.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iRegEx              The register.  The 16 first are regular ones,
 *                              whereas 16 thru 19 maps to AH, CH, DH and BH.
 */
DECL_FORCE_INLINE(uint8_t) iemGRegFetchU8Ex(PVMCPUCC pVCpu, uint8_t iRegEx) RT_NOEXCEPT
{
    return *iemGRegRefU8Ex(pVCpu, iRegEx);
}


/**
 * Fetches the value of a 16-bit general purpose register.
 *
 * @returns The register value.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iReg                The register.
 */
DECL_FORCE_INLINE(uint16_t) iemGRegFetchU16(PVMCPUCC pVCpu, uint8_t iReg) RT_NOEXCEPT
{
    Assert(iReg < 16);
    return pVCpu->cpum.GstCtx.aGRegs[iReg].u16;
}


/**
 * Fetches the value of a 32-bit general purpose register.
 *
 * @returns The register value.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iReg                The register.
 */
DECL_FORCE_INLINE(uint32_t) iemGRegFetchU32(PVMCPUCC pVCpu, uint8_t iReg) RT_NOEXCEPT
{
    Assert(iReg < 16);
    return pVCpu->cpum.GstCtx.aGRegs[iReg].u32;
}


/**
 * Fetches the value of a 64-bit general purpose register.
 *
 * @returns The register value.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iReg                The register.
 */
DECL_FORCE_INLINE(uint64_t) iemGRegFetchU64(PVMCPUCC pVCpu, uint8_t iReg) RT_NOEXCEPT
{
    Assert(iReg < 16);
    return pVCpu->cpum.GstCtx.aGRegs[iReg].u64;
}


/**
 * Stores a 16-bit value to a general purpose register.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iReg                The register.
 * @param   uValue              The value to store.
 */
DECL_FORCE_INLINE(void) iemGRegStoreU16(PVMCPUCC pVCpu, uint8_t iReg, uint16_t uValue) RT_NOEXCEPT
{
    Assert(iReg < 16);
    pVCpu->cpum.GstCtx.aGRegs[iReg].u16 = uValue;
}


/**
 * Stores a 32-bit value to a general purpose register, implicitly clearing high
 * values.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iReg                The register.
 * @param   uValue              The value to store.
 */
DECL_FORCE_INLINE(void) iemGRegStoreU32(PVMCPUCC pVCpu, uint8_t iReg, uint32_t uValue) RT_NOEXCEPT
{
    Assert(iReg < 16);
    pVCpu->cpum.GstCtx.aGRegs[iReg].u64 = uValue;
}


/**
 * Stores a 64-bit value to a general purpose register.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iReg                The register.
 * @param   uValue              The value to store.
 */
DECL_FORCE_INLINE(void) iemGRegStoreU64(PVMCPUCC pVCpu, uint8_t iReg, uint64_t uValue) RT_NOEXCEPT
{
    Assert(iReg < 16);
    pVCpu->cpum.GstCtx.aGRegs[iReg].u64 = uValue;
}


/**
 * Get the address of the top of the stack.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
DECL_FORCE_INLINE(RTGCPTR) iemRegGetEffRsp(PCVMCPU pVCpu) RT_NOEXCEPT
{
    if (IEM_IS_64BIT_CODE(pVCpu))
        return pVCpu->cpum.GstCtx.rsp;
    if (pVCpu->cpum.GstCtx.ss.Attr.n.u1DefBig)
        return pVCpu->cpum.GstCtx.esp;
    return pVCpu->cpum.GstCtx.sp;
}


/**
 * Updates the RIP/EIP/IP to point to the next instruction.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             The number of bytes to add.
 */
DECL_FORCE_INLINE(void) iemRegAddToRip(PVMCPUCC pVCpu, uint8_t cbInstr) RT_NOEXCEPT
{
    /*
     * Advance RIP.
     *
     * When we're targetting 8086/8, 80186/8 or 80286 mode the updates are 16-bit,
     * while in all other modes except LM64 the updates are 32-bit.  This means
     * we need to watch for both 32-bit and 16-bit "carry" situations, i.e.
     * 4GB and 64KB rollovers, and decide whether anything needs masking.
     *
     * See PC wrap around tests in bs3-cpu-weird-1.
     */
    uint64_t const uRipPrev = pVCpu->cpum.GstCtx.rip;
    uint64_t const uRipNext = uRipPrev + cbInstr;
    if (RT_LIKELY(   !((uRipNext ^ uRipPrev) & (RT_BIT_64(32) | RT_BIT_64(16)))
                  || IEM_IS_64BIT_CODE(pVCpu)))
        pVCpu->cpum.GstCtx.rip = uRipNext;
    else if (IEM_GET_TARGET_CPU(pVCpu) >= IEMTARGETCPU_386)
        pVCpu->cpum.GstCtx.rip = (uint32_t)uRipNext;
    else
        pVCpu->cpum.GstCtx.rip = (uint16_t)uRipNext;
}


/**
 * Called by iemRegAddToRipAndFinishingClearingRF and others when any of the
 * following EFLAGS bits are set:
 *      - X86_EFL_RF - clear it.
 *      - CPUMCTX_INHIBIT_SHADOW (_SS/_STI) - clear them.
 *      - X86_EFL_TF - generate single step \#DB trap.
 *      - CPUMCTX_DBG_HIT_DR0/1/2/3 - generate \#DB trap (data or I/O, not
 *        instruction).
 *
 * According to @sdmv3{077,200,Table 6-2,Priority Among Concurrent Events},
 * a \#DB due to TF (single stepping) or a DRx non-instruction breakpoint
 * takes priority over both NMIs and hardware interrupts.  So, neither is
 * considered here.  (The RESET, \#MC, SMI, INIT, STOPCLK and FLUSH events are
 * either unsupported will be triggered on-top of any \#DB raised here.)
 *
 * The RF flag only needs to be cleared here as it only suppresses instruction
 * breakpoints which are not raised here (happens synchronously during
 * instruction fetching).
 *
 * The CPUMCTX_INHIBIT_SHADOW_SS flag will be cleared by this function, so its
 * status has no bearing on whether \#DB exceptions are raised.
 *
 * @note This must *NOT* be called by the two instructions setting the
 *       CPUMCTX_INHIBIT_SHADOW_SS flag.
 *
 * @see  @sdmv3{077,200,Table 6-2,Priority Among Concurrent Events}
 * @see  @sdmv3{077,200,6.8.3,Masking Exceptions and Interrupts When Switching
 *              Stacks}
 */
template<uint32_t const a_fTF = X86_EFL_TF>
static VBOXSTRICTRC iemFinishInstructionWithFlagsSet(PVMCPUCC pVCpu, int rcNormal) RT_NOEXCEPT
{
    /*
     * Normally we're just here to clear RF and/or interrupt shadow bits.
     */
    if (RT_LIKELY((pVCpu->cpum.GstCtx.eflags.uBoth & (a_fTF | CPUMCTX_DBG_HIT_DRX_MASK | CPUMCTX_DBG_DBGF_MASK)) == 0))
        pVCpu->cpum.GstCtx.eflags.uBoth &= ~(X86_EFL_RF | CPUMCTX_INHIBIT_SHADOW);
    else
    {
        /*
         * Raise a #DB or/and DBGF event.
         */
        VBOXSTRICTRC rcStrict;
        if (pVCpu->cpum.GstCtx.eflags.uBoth & (a_fTF | CPUMCTX_DBG_HIT_DRX_MASK))
        {
            IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_DR6);
            pVCpu->cpum.GstCtx.dr[6] &= ~X86_DR6_B_MASK;
            if (pVCpu->cpum.GstCtx.eflags.uBoth & a_fTF)
                pVCpu->cpum.GstCtx.dr[6] |= X86_DR6_BS;
            pVCpu->cpum.GstCtx.dr[6] |= (pVCpu->cpum.GstCtx.eflags.uBoth & CPUMCTX_DBG_HIT_DRX_MASK_NONSILENT)
                                     >> CPUMCTX_DBG_HIT_DRX_SHIFT;
            LogFlowFunc(("Guest #DB fired at %04X:%016llX: DR6=%08X, RFLAGS=%16RX64\n",
                         pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, (unsigned)pVCpu->cpum.GstCtx.dr[6],
                         pVCpu->cpum.GstCtx.rflags.uBoth));

            pVCpu->cpum.GstCtx.eflags.uBoth &= ~(X86_EFL_RF | CPUMCTX_INHIBIT_SHADOW | CPUMCTX_DBG_HIT_DRX_MASK);
            rcStrict = iemRaiseDebugException(pVCpu);

            /* A DBGF event/breakpoint trumps the iemRaiseDebugException informational status code. */
            if ((pVCpu->cpum.GstCtx.eflags.uBoth & CPUMCTX_DBG_DBGF_MASK) && RT_FAILURE(rcStrict))
            {
                rcStrict = pVCpu->cpum.GstCtx.eflags.uBoth & CPUMCTX_DBG_DBGF_BP ? VINF_EM_DBG_BREAKPOINT : VINF_EM_DBG_EVENT;
                LogFlowFunc(("dbgf at %04X:%016llX: %Rrc\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, VBOXSTRICTRC_VAL(rcStrict)));
            }
        }
        else
        {
            Assert(pVCpu->cpum.GstCtx.eflags.uBoth & CPUMCTX_DBG_DBGF_MASK);
            rcStrict = pVCpu->cpum.GstCtx.eflags.uBoth & CPUMCTX_DBG_DBGF_BP ? VINF_EM_DBG_BREAKPOINT : VINF_EM_DBG_EVENT;
            LogFlowFunc(("dbgf at %04X:%016llX: %Rrc\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, VBOXSTRICTRC_VAL(rcStrict)));
        }
        pVCpu->cpum.GstCtx.eflags.uBoth &= ~CPUMCTX_DBG_DBGF_MASK;
        Assert(rcStrict != VINF_SUCCESS);
        return rcStrict;
    }
    return rcNormal;
}


/**
 * Clears the RF and CPUMCTX_INHIBIT_SHADOW, triggering \#DB if pending.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemRegFinishClearingRF(PVMCPUCC pVCpu, int rcNormal) RT_NOEXCEPT
{
    /*
     * We assume that most of the time nothing actually needs doing here.
     */
    AssertCompile(CPUMCTX_INHIBIT_SHADOW < UINT32_MAX);
    if (RT_LIKELY(!(  pVCpu->cpum.GstCtx.eflags.uBoth
                    & (X86_EFL_TF | X86_EFL_RF | CPUMCTX_INHIBIT_SHADOW | CPUMCTX_DBG_HIT_DRX_MASK | CPUMCTX_DBG_DBGF_MASK)) ))
        return rcNormal;
    return iemFinishInstructionWithFlagsSet(pVCpu, rcNormal);
}


/**
 * Updates the RIP/EIP/IP to point to the next instruction and clears EFLAGS.RF
 * and CPUMCTX_INHIBIT_SHADOW.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             The number of bytes to add.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemRegAddToRipAndFinishingClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr) RT_NOEXCEPT
{
    iemRegAddToRip(pVCpu, cbInstr);
    return iemRegFinishClearingRF(pVCpu, VINF_SUCCESS);
}


/**
 * Updates the RIP to point to the next instruction and clears EFLAGS.RF
 * and CPUMCTX_INHIBIT_SHADOW.
 *
 * Only called from 64-bit code.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             The number of bytes to add.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemRegAddToRip64AndFinishingClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr, int rcNormal) RT_NOEXCEPT
{
    pVCpu->cpum.GstCtx.rip = pVCpu->cpum.GstCtx.rip + cbInstr;
    return iemRegFinishClearingRF(pVCpu, rcNormal);
}


/**
 * Updates the EIP to point to the next instruction and clears EFLAGS.RF and
 * CPUMCTX_INHIBIT_SHADOW.
 *
 * This is never from 64-bit code.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             The number of bytes to add.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemRegAddToEip32AndFinishingClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr, int rcNormal) RT_NOEXCEPT
{
    pVCpu->cpum.GstCtx.rip = (uint32_t)(pVCpu->cpum.GstCtx.eip + cbInstr);
    return iemRegFinishClearingRF(pVCpu, rcNormal);
}


/**
 * Updates the IP to point to the next instruction and clears EFLAGS.RF and
 * CPUMCTX_INHIBIT_SHADOW.
 *
 * This is only ever used from 16-bit code on a pre-386 CPU.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             The number of bytes to add.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemRegAddToIp16AndFinishingClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr, int rcNormal) RT_NOEXCEPT
{
    pVCpu->cpum.GstCtx.rip = (uint16_t)(pVCpu->cpum.GstCtx.ip + cbInstr);
    return iemRegFinishClearingRF(pVCpu, rcNormal);
}


/**
 * Tail method for a finish function that does't clear flags or raise \#DB.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemRegFinishNoFlags(PVMCPUCC pVCpu, int rcNormal) RT_NOEXCEPT
{
    AssertCompile(CPUMCTX_INHIBIT_SHADOW < UINT32_MAX);
    Assert(!(  pVCpu->cpum.GstCtx.eflags.uBoth
             & (X86_EFL_TF | X86_EFL_RF | CPUMCTX_INHIBIT_SHADOW | CPUMCTX_DBG_HIT_DRX_MASK | CPUMCTX_DBG_DBGF_MASK)) );
    RT_NOREF(pVCpu);
    return rcNormal;
}


/**
 * Updates the RIP to point to the next instruction, but does not need to clear
 * EFLAGS.RF or CPUMCTX_INHIBIT_SHADOW nor check for debug flags.
 *
 * Only called from 64-bit code.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             The number of bytes to add.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemRegAddToRip64AndFinishingNoFlags(PVMCPUCC pVCpu, uint8_t cbInstr, int rcNormal) RT_NOEXCEPT
{
    pVCpu->cpum.GstCtx.rip = pVCpu->cpum.GstCtx.rip + cbInstr;
    return iemRegFinishNoFlags(pVCpu, rcNormal);
}


/**
 * Updates the EIP to point to the next instruction, but does not need to clear
 * EFLAGS.RF or CPUMCTX_INHIBIT_SHADOW nor check for debug flags.
 *
 * This is never from 64-bit code.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             The number of bytes to add.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemRegAddToEip32AndFinishingNoFlags(PVMCPUCC pVCpu, uint8_t cbInstr, int rcNormal) RT_NOEXCEPT
{
    pVCpu->cpum.GstCtx.rip = (uint32_t)(pVCpu->cpum.GstCtx.eip + cbInstr);
    return iemRegFinishNoFlags(pVCpu, rcNormal);
}


/**
 * Updates the IP to point to the next instruction, but does not need to clear
 * EFLAGS.RF or CPUMCTX_INHIBIT_SHADOW nor check for debug flags.
 *
 * This is only ever used from 16-bit code on a pre-386 CPU.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             The number of bytes to add.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 *
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemRegAddToIp16AndFinishingNoFlags(PVMCPUCC pVCpu, uint8_t cbInstr, int rcNormal) RT_NOEXCEPT
{
    pVCpu->cpum.GstCtx.rip = (uint16_t)(pVCpu->cpum.GstCtx.ip + cbInstr);
    return iemRegFinishNoFlags(pVCpu, rcNormal);
}


/**
 * Adds a 8-bit signed jump offset to RIP from 64-bit code.
 *
 * May raise a \#GP(0) if the new RIP is non-canonical or outside the code
 * segment limit.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             Instruction size.
 * @param   offNextInstr        The offset of the next instruction.
 * @param   enmEffOpSize        Effective operand size.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemRegRip64RelativeJumpS8AndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr, int8_t offNextInstr,
                                                                             IEMMODE enmEffOpSize, int rcNormal) RT_NOEXCEPT
{
    Assert(IEM_IS_64BIT_CODE(pVCpu));
    Assert(enmEffOpSize == IEMMODE_64BIT || enmEffOpSize == IEMMODE_16BIT);

    uint64_t uNewRip = pVCpu->cpum.GstCtx.rip + cbInstr + (int64_t)offNextInstr;
    if (enmEffOpSize == IEMMODE_16BIT)
        uNewRip &= UINT16_MAX;

    if (RT_LIKELY(IEM_IS_CANONICAL(uNewRip)))
        pVCpu->cpum.GstCtx.rip = uNewRip;
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);

#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif

    /*
     * Clear RF and finish the instruction (maybe raise #DB).
     */
    return iemRegFinishClearingRF(pVCpu, rcNormal);
}


/**
 * Adds a 8-bit signed jump offset to RIP from 64-bit code when the caller is
 * sure it stays within the same page.
 *
 * May raise a \#GP(0) if the new RIP is non-canonical or outside the code
 * segment limit.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             Instruction size.
 * @param   offNextInstr        The offset of the next instruction.
 * @param   enmEffOpSize        Effective operand size.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC)
iemRegRip64RelativeJumpS8IntraPgAndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr, int8_t offNextInstr,
                                                    IEMMODE enmEffOpSize, int rcNormal) RT_NOEXCEPT
{
    Assert(IEM_IS_64BIT_CODE(pVCpu));
    Assert(enmEffOpSize == IEMMODE_64BIT); RT_NOREF(enmEffOpSize);

    uint64_t const uNewRip = pVCpu->cpum.GstCtx.rip + cbInstr + (int64_t)offNextInstr;
    Assert((pVCpu->cpum.GstCtx.rip >> GUEST_PAGE_SHIFT) == (uNewRip >> GUEST_PAGE_SHIFT));
    pVCpu->cpum.GstCtx.rip = uNewRip;

#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif

    /*
     * Clear RF and finish the instruction (maybe raise #DB).
     */
    return iemRegFinishClearingRF(pVCpu, rcNormal);
}


/**
 * Adds a 8-bit signed jump offset to EIP, on 386 or later from 16-bit or 32-bit
 * code (never 64-bit).
 *
 * May raise a \#GP(0) if the new RIP is non-canonical or outside the code
 * segment limit.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             Instruction size.
 * @param   offNextInstr        The offset of the next instruction.
 * @param   enmEffOpSize        Effective operand size.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemRegEip32RelativeJumpS8AndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr, int8_t offNextInstr,
                                                                             IEMMODE enmEffOpSize, int rcNormal) RT_NOEXCEPT
{
    Assert(!IEM_IS_64BIT_CODE(pVCpu));
    Assert(enmEffOpSize == IEMMODE_32BIT || enmEffOpSize == IEMMODE_16BIT);

    uint32_t uNewEip = pVCpu->cpum.GstCtx.eip + cbInstr + (int32_t)offNextInstr;
    if (enmEffOpSize == IEMMODE_16BIT)
        uNewEip &= UINT16_MAX;
    if (RT_LIKELY(uNewEip <= pVCpu->cpum.GstCtx.cs.u32Limit))
        pVCpu->cpum.GstCtx.rip = uNewEip;
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);

#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif

    /*
     * Clear RF and finish the instruction (maybe raise #DB).
     */
    return iemRegFinishClearingRF(pVCpu, rcNormal);
}


/**
 * Adds a 8-bit signed jump offset to EIP, on 386 or later from FLAT 32-bit code
 * (never 64-bit).
 *
 * May raise a \#GP(0) if the new RIP is non-canonical or outside the code
 * segment limit.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             Instruction size.
 * @param   offNextInstr        The offset of the next instruction.
 * @param   enmEffOpSize        Effective operand size.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC)
 iemRegEip32RelativeJumpS8FlatAndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr, int8_t offNextInstr,
                                                  IEMMODE enmEffOpSize, int rcNormal) RT_NOEXCEPT
{
    Assert(!IEM_IS_64BIT_CODE(pVCpu));
    Assert(enmEffOpSize == IEMMODE_32BIT || enmEffOpSize == IEMMODE_16BIT);

    uint32_t uNewEip = pVCpu->cpum.GstCtx.eip + cbInstr + (int32_t)offNextInstr;
    if (enmEffOpSize == IEMMODE_16BIT)
        uNewEip &= UINT16_MAX;
    pVCpu->cpum.GstCtx.rip = uNewEip;

#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif

    /*
     * Clear RF and finish the instruction (maybe raise #DB).
     */
    return iemRegFinishClearingRF(pVCpu, rcNormal);
}


/**
 * Adds a 8-bit signed jump offset to IP, on a pre-386 CPU.
 *
 * May raise a \#GP(0) if the new RIP is non-canonical or outside the code
 * segment limit.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             Instruction size.
 * @param   offNextInstr        The offset of the next instruction.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemRegIp16RelativeJumpS8AndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr,
                                                                            int8_t offNextInstr, int rcNormal) RT_NOEXCEPT
{
    Assert(!IEM_IS_64BIT_CODE(pVCpu));

    uint16_t const uNewIp = pVCpu->cpum.GstCtx.ip + cbInstr + (int16_t)offNextInstr;
    if (RT_LIKELY(uNewIp <= pVCpu->cpum.GstCtx.cs.u32Limit))
        pVCpu->cpum.GstCtx.rip = uNewIp;
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);

#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif

    /*
     * Clear RF and finish the instruction (maybe raise #DB).
     */
    return iemRegFinishClearingRF(pVCpu, rcNormal);
}


/**
 * Adds a 8-bit signed jump offset to RIP from 64-bit code, no checking or
 * clearing of flags.
 *
 * May raise a \#GP(0) if the new RIP is non-canonical or outside the code
 * segment limit.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             Instruction size.
 * @param   offNextInstr        The offset of the next instruction.
 * @param   enmEffOpSize        Effective operand size.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemRegRip64RelativeJumpS8AndFinishNoFlags(PVMCPUCC pVCpu, uint8_t cbInstr, int8_t offNextInstr,
                                                                          IEMMODE enmEffOpSize, int rcNormal) RT_NOEXCEPT
{
    Assert(IEM_IS_64BIT_CODE(pVCpu));
    Assert(enmEffOpSize == IEMMODE_64BIT || enmEffOpSize == IEMMODE_16BIT);

    uint64_t uNewRip = pVCpu->cpum.GstCtx.rip + cbInstr + (int64_t)offNextInstr;
    if (enmEffOpSize == IEMMODE_16BIT)
        uNewRip &= UINT16_MAX;

    if (RT_LIKELY(IEM_IS_CANONICAL(uNewRip)))
        pVCpu->cpum.GstCtx.rip = uNewRip;
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);

#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif
    return iemRegFinishNoFlags(pVCpu, rcNormal);
}


/**
 * Adds a 8-bit signed jump offset to RIP from 64-bit code when caller is sure
 * it stays within the same page, no checking or clearing of flags.
 *
 * May raise a \#GP(0) if the new RIP is non-canonical or outside the code
 * segment limit.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             Instruction size.
 * @param   offNextInstr        The offset of the next instruction.
 * @param   enmEffOpSize        Effective operand size.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC)
iemRegRip64RelativeJumpS8IntraPgAndFinishNoFlags(PVMCPUCC pVCpu, uint8_t cbInstr, int8_t offNextInstr,
                                                 IEMMODE enmEffOpSize, int rcNormal) RT_NOEXCEPT
{
    Assert(IEM_IS_64BIT_CODE(pVCpu));
    Assert(enmEffOpSize == IEMMODE_64BIT); RT_NOREF(enmEffOpSize);

    uint64_t const uNewRip = pVCpu->cpum.GstCtx.rip + cbInstr + (int64_t)offNextInstr;
    Assert((pVCpu->cpum.GstCtx.rip >> GUEST_PAGE_SHIFT) == (uNewRip >> GUEST_PAGE_SHIFT));
    pVCpu->cpum.GstCtx.rip = uNewRip;

#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif
    return iemRegFinishNoFlags(pVCpu, rcNormal);
}


/**
 * Adds a 8-bit signed jump offset to EIP, on 386 or later from 16-bit or 32-bit
 * code (never 64-bit), no checking or clearing of flags.
 *
 * May raise a \#GP(0) if the new RIP is non-canonical or outside the code
 * segment limit.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             Instruction size.
 * @param   offNextInstr        The offset of the next instruction.
 * @param   enmEffOpSize        Effective operand size.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemRegEip32RelativeJumpS8AndFinishNoFlags(PVMCPUCC pVCpu, uint8_t cbInstr, int8_t offNextInstr,
                                                                          IEMMODE enmEffOpSize, int rcNormal) RT_NOEXCEPT
{
    Assert(!IEM_IS_64BIT_CODE(pVCpu));
    Assert(enmEffOpSize == IEMMODE_32BIT || enmEffOpSize == IEMMODE_16BIT);

    uint32_t uNewEip = pVCpu->cpum.GstCtx.eip + cbInstr + (int32_t)offNextInstr;
    if (enmEffOpSize == IEMMODE_16BIT)
        uNewEip &= UINT16_MAX;
    if (RT_LIKELY(uNewEip <= pVCpu->cpum.GstCtx.cs.u32Limit))
        pVCpu->cpum.GstCtx.rip = uNewEip;
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);

#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif
    return iemRegFinishNoFlags(pVCpu, rcNormal);
}


/**
 * Adds a 8-bit signed jump offset to EIP, on 386 or later from flat 32-bit code
 * (never 64-bit), no checking or clearing of flags.
 *
 * May raise a \#GP(0) if the new RIP is non-canonical or outside the code
 * segment limit.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             Instruction size.
 * @param   offNextInstr        The offset of the next instruction.
 * @param   enmEffOpSize        Effective operand size.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC)
iemRegEip32RelativeJumpS8FlatAndFinishNoFlags(PVMCPUCC pVCpu, uint8_t cbInstr, int8_t offNextInstr,
                                              IEMMODE enmEffOpSize, int rcNormal) RT_NOEXCEPT
{
    Assert(!IEM_IS_64BIT_CODE(pVCpu));
    Assert(enmEffOpSize == IEMMODE_32BIT || enmEffOpSize == IEMMODE_16BIT);

    uint32_t uNewEip = pVCpu->cpum.GstCtx.eip + cbInstr + (int32_t)offNextInstr;
    if (enmEffOpSize == IEMMODE_16BIT)
        uNewEip &= UINT16_MAX;
    pVCpu->cpum.GstCtx.rip = uNewEip;

#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif
    return iemRegFinishNoFlags(pVCpu, rcNormal);
}


/**
 * Adds a 8-bit signed jump offset to IP, on a pre-386 CPU, no checking or
 * clearing of flags.
 *
 * May raise a \#GP(0) if the new RIP is non-canonical or outside the code
 * segment limit.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             Instruction size.
 * @param   offNextInstr        The offset of the next instruction.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemRegIp16RelativeJumpS8AndFinishNoFlags(PVMCPUCC pVCpu, uint8_t cbInstr,
                                                                         int8_t offNextInstr, int rcNormal) RT_NOEXCEPT
{
    Assert(!IEM_IS_64BIT_CODE(pVCpu));

    uint16_t const uNewIp = pVCpu->cpum.GstCtx.ip + cbInstr + (int16_t)offNextInstr;
    if (RT_LIKELY(uNewIp <= pVCpu->cpum.GstCtx.cs.u32Limit))
        pVCpu->cpum.GstCtx.rip = uNewIp;
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);

#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif
    return iemRegFinishNoFlags(pVCpu, rcNormal);
}


/**
 * Adds a 16-bit signed jump offset to RIP from 64-bit code.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             Instruction size.
 * @param   offNextInstr        The offset of the next instruction.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemRegRip64RelativeJumpS16AndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr,
                                                                              int16_t offNextInstr, int rcNormal) RT_NOEXCEPT
{
    Assert(IEM_IS_64BIT_CODE(pVCpu));

    pVCpu->cpum.GstCtx.rip = (uint16_t)(pVCpu->cpum.GstCtx.ip + cbInstr + offNextInstr);

#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif

    /*
     * Clear RF and finish the instruction (maybe raise #DB).
     */
    return iemRegFinishClearingRF(pVCpu, rcNormal);
}


/**
 * Adds a 16-bit signed jump offset to EIP from 16-bit or 32-bit code.
 *
 * May raise a \#GP(0) if the new RIP is non-canonical or outside the code
 * segment limit.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             Instruction size.
 * @param   offNextInstr        The offset of the next instruction.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 *
 * @note    This is also used by 16-bit code in pre-386 mode, as the code is
 *          identical.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemRegEip32RelativeJumpS16AndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr,
                                                                              int16_t offNextInstr, int rcNormal) RT_NOEXCEPT
{
    Assert(!IEM_IS_64BIT_CODE(pVCpu));

    uint16_t const uNewIp = pVCpu->cpum.GstCtx.ip + cbInstr + offNextInstr;
    if (RT_LIKELY(uNewIp <= pVCpu->cpum.GstCtx.cs.u32Limit))
        pVCpu->cpum.GstCtx.rip = uNewIp;
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);

#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif

    /*
     * Clear RF and finish the instruction (maybe raise #DB).
     */
    return iemRegFinishClearingRF(pVCpu, rcNormal);
}


/**
 * Adds a 16-bit signed jump offset to EIP from FLAT 32-bit code.
 *
 * May raise a \#GP(0) if the new RIP is non-canonical or outside the code
 * segment limit.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             Instruction size.
 * @param   offNextInstr        The offset of the next instruction.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 *
 * @note    This is also used by 16-bit code in pre-386 mode, as the code is
 *          identical.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemRegEip32RelativeJumpS16FlatAndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr,
                                                                                  int16_t offNextInstr, int rcNormal) RT_NOEXCEPT
{
    Assert(!IEM_IS_64BIT_CODE(pVCpu));

    uint16_t const uNewIp = pVCpu->cpum.GstCtx.ip + cbInstr + offNextInstr;
    pVCpu->cpum.GstCtx.rip = uNewIp;

#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif

    /*
     * Clear RF and finish the instruction (maybe raise #DB).
     */
    return iemRegFinishClearingRF(pVCpu, rcNormal);
}


/**
 * Adds a 16-bit signed jump offset to RIP from 64-bit code, no checking or
 * clearing of flags.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             Instruction size.
 * @param   offNextInstr        The offset of the next instruction.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemRegRip64RelativeJumpS16AndFinishNoFlags(PVMCPUCC pVCpu, uint8_t cbInstr,
                                                                           int16_t offNextInstr, int rcNormal) RT_NOEXCEPT
{
    Assert(IEM_IS_64BIT_CODE(pVCpu));

    pVCpu->cpum.GstCtx.rip = (uint16_t)(pVCpu->cpum.GstCtx.ip + cbInstr + offNextInstr);

#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif
    return iemRegFinishNoFlags(pVCpu, rcNormal);
}


/**
 * Adds a 16-bit signed jump offset to EIP from 16-bit or 32-bit code,
 * no checking or clearing of flags.
 *
 * May raise a \#GP(0) if the new RIP is non-canonical or outside the code
 * segment limit.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             Instruction size.
 * @param   offNextInstr        The offset of the next instruction.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 *
 * @note    This is also used by 16-bit code in pre-386 mode, as the code is
 *          identical.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemRegEip32RelativeJumpS16AndFinishNoFlags(PVMCPUCC pVCpu, uint8_t cbInstr,
                                                                           int16_t offNextInstr, int rcNormal) RT_NOEXCEPT
{
    Assert(!IEM_IS_64BIT_CODE(pVCpu));

    uint16_t const uNewIp = pVCpu->cpum.GstCtx.ip + cbInstr + offNextInstr;
    if (RT_LIKELY(uNewIp <= pVCpu->cpum.GstCtx.cs.u32Limit))
        pVCpu->cpum.GstCtx.rip = uNewIp;
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);

#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif
    return iemRegFinishNoFlags(pVCpu, rcNormal);
}


/**
 * Adds a 16-bit signed jump offset to EIP from FLAT 32-bit code, no checking or
 * clearing of flags.
 *
 * May raise a \#GP(0) if the new RIP is non-canonical or outside the code
 * segment limit.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             Instruction size.
 * @param   offNextInstr        The offset of the next instruction.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 *
 * @note    This is also used by 16-bit code in pre-386 mode, as the code is
 *          identical.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemRegEip32RelativeJumpS16FlatAndFinishNoFlags(PVMCPUCC pVCpu, uint8_t cbInstr,
                                                                               int16_t offNextInstr, int rcNormal) RT_NOEXCEPT
{
    Assert(!IEM_IS_64BIT_CODE(pVCpu));

    uint16_t const uNewIp = pVCpu->cpum.GstCtx.ip + cbInstr + offNextInstr;
    pVCpu->cpum.GstCtx.rip = uNewIp;

#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif
    return iemRegFinishNoFlags(pVCpu, rcNormal);
}


/**
 * Adds a 32-bit signed jump offset to RIP from 64-bit code.
 *
 * May raise a \#GP(0) if the new RIP is non-canonical or outside the code
 * segment limit.
 *
 * We ASSUME that the effective operand size is 64-bit here, as 16-bit is the
 * only alternative for relative jumps in 64-bit code and that is already
 * handled in the decoder stage.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             Instruction size.
 * @param   offNextInstr        The offset of the next instruction.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemRegRip64RelativeJumpS32AndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr,
                                                                              int32_t offNextInstr, int rcNormal) RT_NOEXCEPT
{
    Assert(IEM_IS_64BIT_CODE(pVCpu));

    uint64_t const uNewRip = pVCpu->cpum.GstCtx.rip + cbInstr + (int64_t)offNextInstr;
    if (RT_LIKELY(IEM_IS_CANONICAL(uNewRip)))
        pVCpu->cpum.GstCtx.rip = uNewRip;
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);

#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif

    /*
     * Clear RF and finish the instruction (maybe raise #DB).
     */
    return iemRegFinishClearingRF(pVCpu, rcNormal);
}


/**
 * Adds a 32-bit signed jump offset to RIP from 64-bit code when the caller is
 * sure the target is in the same page.
 *
 * May raise a \#GP(0) if the new RIP is non-canonical or outside the code
 * segment limit.
 *
 * We ASSUME that the effective operand size is 64-bit here, as 16-bit is the
 * only alternative for relative jumps in 64-bit code and that is already
 * handled in the decoder stage.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             Instruction size.
 * @param   offNextInstr        The offset of the next instruction.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC)
iemRegRip64RelativeJumpS32IntraPgAndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr,
                                                     int32_t offNextInstr, int rcNormal) RT_NOEXCEPT
{
    Assert(IEM_IS_64BIT_CODE(pVCpu));

    uint64_t const uNewRip = pVCpu->cpum.GstCtx.rip + cbInstr + (int64_t)offNextInstr;
    Assert((pVCpu->cpum.GstCtx.rip >> GUEST_PAGE_SHIFT) == (uNewRip >> GUEST_PAGE_SHIFT));
    pVCpu->cpum.GstCtx.rip = uNewRip;

#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif

    /*
     * Clear RF and finish the instruction (maybe raise #DB).
     */
    return iemRegFinishClearingRF(pVCpu, rcNormal);
}


/**
 * Adds a 32-bit signed jump offset to RIP from 64-bit code.
 *
 * May raise a \#GP(0) if the new RIP is non-canonical or outside the code
 * segment limit.
 *
 * We ASSUME that the effective operand size is 32-bit here, as 16-bit is the
 * only alternative for relative jumps in 32-bit code and that is already
 * handled in the decoder stage.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             Instruction size.
 * @param   offNextInstr        The offset of the next instruction.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemRegEip32RelativeJumpS32AndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr,
                                                                              int32_t offNextInstr, int rcNormal) RT_NOEXCEPT
{
    Assert(!IEM_IS_64BIT_CODE(pVCpu));
    Assert(pVCpu->cpum.GstCtx.rip <= UINT32_MAX);

    uint32_t const uNewEip = pVCpu->cpum.GstCtx.eip + cbInstr + offNextInstr;
    if (RT_LIKELY(uNewEip <= pVCpu->cpum.GstCtx.cs.u32Limit))
        pVCpu->cpum.GstCtx.rip = uNewEip;
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);

#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif

    /*
     * Clear RF and finish the instruction (maybe raise #DB).
     */
    return iemRegFinishClearingRF(pVCpu, rcNormal);
}


/**
 * Adds a 32-bit signed jump offset to RIP from FLAT 32-bit code.
 *
 * May raise a \#GP(0) if the new RIP is non-canonical or outside the code
 * segment limit.
 *
 * We ASSUME that the effective operand size is 32-bit here, as 16-bit is the
 * only alternative for relative jumps in 32-bit code and that is already
 * handled in the decoder stage.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             Instruction size.
 * @param   offNextInstr        The offset of the next instruction.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemRegEip32RelativeJumpS32FlatAndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr,
                                                                                  int32_t offNextInstr, int rcNormal) RT_NOEXCEPT
{
    Assert(!IEM_IS_64BIT_CODE(pVCpu));
    Assert(pVCpu->cpum.GstCtx.rip <= UINT32_MAX);

    uint32_t const uNewEip = pVCpu->cpum.GstCtx.eip + cbInstr + offNextInstr;
    pVCpu->cpum.GstCtx.rip = uNewEip;

#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif

    /*
     * Clear RF and finish the instruction (maybe raise #DB).
     */
    return iemRegFinishClearingRF(pVCpu, rcNormal);
}



/**
 * Adds a 32-bit signed jump offset to RIP from 64-bit code, no checking or
 * clearing of flags.
 *
 * May raise a \#GP(0) if the new RIP is non-canonical or outside the code
 * segment limit.
 *
 * We ASSUME that the effective operand size is 64-bit here, as 16-bit is the
 * only alternative for relative jumps in 64-bit code and that is already
 * handled in the decoder stage.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             Instruction size.
 * @param   offNextInstr        The offset of the next instruction.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemRegRip64RelativeJumpS32AndFinishNoFlags(PVMCPUCC pVCpu, uint8_t cbInstr,
                                                                           int32_t offNextInstr, int rcNormal) RT_NOEXCEPT
{
    Assert(IEM_IS_64BIT_CODE(pVCpu));

    uint64_t const uNewRip = pVCpu->cpum.GstCtx.rip + cbInstr + (int64_t)offNextInstr;
    if (RT_LIKELY(IEM_IS_CANONICAL(uNewRip)))
        pVCpu->cpum.GstCtx.rip = uNewRip;
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);

#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif
    return iemRegFinishNoFlags(pVCpu, rcNormal);
}


/**
 * Adds a 32-bit signed jump offset to RIP from 64-bit code when the caller is
 * sure it stays within the same page, no checking or clearing of flags.
 *
 * May raise a \#GP(0) if the new RIP is non-canonical or outside the code
 * segment limit.
 *
 * We ASSUME that the effective operand size is 64-bit here, as 16-bit is the
 * only alternative for relative jumps in 64-bit code and that is already
 * handled in the decoder stage.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             Instruction size.
 * @param   offNextInstr        The offset of the next instruction.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC)
iemRegRip64RelativeJumpS32IntraPgAndFinishNoFlags(PVMCPUCC pVCpu, uint8_t cbInstr, int32_t offNextInstr, int rcNormal) RT_NOEXCEPT
{
    Assert(IEM_IS_64BIT_CODE(pVCpu));

    uint64_t const uNewRip = pVCpu->cpum.GstCtx.rip + cbInstr + (int64_t)offNextInstr;
    Assert((pVCpu->cpum.GstCtx.rip >> GUEST_PAGE_SHIFT) == (uNewRip >> GUEST_PAGE_SHIFT));
    pVCpu->cpum.GstCtx.rip = uNewRip;

#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif
    return iemRegFinishNoFlags(pVCpu, rcNormal);
}


/**
 * Adds a 32-bit signed jump offset to RIP from 32-bit code, no checking or
 * clearing of flags.
 *
 * May raise a \#GP(0) if the new RIP is non-canonical or outside the code
 * segment limit.
 *
 * We ASSUME that the effective operand size is 32-bit here, as 16-bit is the
 * only alternative for relative jumps in 32-bit code and that is already
 * handled in the decoder stage.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             Instruction size.
 * @param   offNextInstr        The offset of the next instruction.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemRegEip32RelativeJumpS32AndFinishNoFlags(PVMCPUCC pVCpu, uint8_t cbInstr,
                                                                           int32_t offNextInstr, int rcNormal) RT_NOEXCEPT
{
    Assert(!IEM_IS_64BIT_CODE(pVCpu));
    Assert(pVCpu->cpum.GstCtx.rip <= UINT32_MAX);

    uint32_t const uNewEip = pVCpu->cpum.GstCtx.eip + cbInstr + offNextInstr;
    if (RT_LIKELY(uNewEip <= pVCpu->cpum.GstCtx.cs.u32Limit))
        pVCpu->cpum.GstCtx.rip = uNewEip;
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);

#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif
    return iemRegFinishNoFlags(pVCpu, rcNormal);
}


/**
 * Adds a 32-bit signed jump offset to RIP from FLAT 32-bit code, no checking or
 * clearing of flags.
 *
 * May raise a \#GP(0) if the new RIP is non-canonical or outside the code
 * segment limit.
 *
 * We ASSUME that the effective operand size is 32-bit here, as 16-bit is the
 * only alternative for relative jumps in 32-bit code and that is already
 * handled in the decoder stage.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             Instruction size.
 * @param   offNextInstr        The offset of the next instruction.
 * @param   rcNormal            VINF_SUCCESS to continue TB.
 *                              VINF_IEM_REEXEC_BREAK to force TB exit when
 *                              taking the wrong conditional branhc.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemRegEip32RelativeJumpS32FlatAndFinishNoFlags(PVMCPUCC pVCpu, uint8_t cbInstr,
                                                                               int32_t offNextInstr, int rcNormal) RT_NOEXCEPT
{
    Assert(!IEM_IS_64BIT_CODE(pVCpu));
    Assert(pVCpu->cpum.GstCtx.rip <= UINT32_MAX);

    uint32_t const uNewEip = pVCpu->cpum.GstCtx.eip + cbInstr + offNextInstr;
    pVCpu->cpum.GstCtx.rip = uNewEip;

#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif
    return iemRegFinishNoFlags(pVCpu, rcNormal);
}


/**
 * Extended version of iemFinishInstructionWithFlagsSet that goes with
 * iemRegAddToRipAndFinishingClearingRfEx.
 *
 * See iemFinishInstructionWithFlagsSet() for details.
 */
static VBOXSTRICTRC iemFinishInstructionWithTfSet(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    /*
     * Raise a #DB.
     */
    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_DR6);
    pVCpu->cpum.GstCtx.dr[6] &= ~X86_DR6_B_MASK;
    pVCpu->cpum.GstCtx.dr[6] |= X86_DR6_BS
                             | (   (pVCpu->cpum.GstCtx.eflags.uBoth & CPUMCTX_DBG_HIT_DRX_MASK_NONSILENT)
                                >> CPUMCTX_DBG_HIT_DRX_SHIFT);
    /** @todo Do we set all pending \#DB events, or just one? */
    LogFlowFunc(("Guest #DB fired at %04X:%016llX: DR6=%08X, RFLAGS=%16RX64 (popf)\n",
                 pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, (unsigned)pVCpu->cpum.GstCtx.dr[6],
                 pVCpu->cpum.GstCtx.rflags.uBoth));
    pVCpu->cpum.GstCtx.eflags.uBoth &= ~(X86_EFL_RF | CPUMCTX_INHIBIT_SHADOW | CPUMCTX_DBG_HIT_DRX_MASK | CPUMCTX_DBG_DBGF_MASK);
    return iemRaiseDebugException(pVCpu);
}


/**
 * Extended version of iemRegAddToRipAndFinishingClearingRF for use by POPF and
 * others potentially updating EFLAGS.TF.
 *
 * The single step event must be generated using the TF value at the start of
 * the instruction, not the new value set by it.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             The number of bytes to add.
 * @param   fEflOld             The EFLAGS at the start of the instruction
 *                              execution.
 */
DECLINLINE(VBOXSTRICTRC) iemRegAddToRipAndFinishingClearingRfEx(PVMCPUCC pVCpu, uint8_t cbInstr, uint32_t fEflOld) RT_NOEXCEPT
{
    iemRegAddToRip(pVCpu, cbInstr);
    if (!(fEflOld & X86_EFL_TF))
    {
        /* Specialized iemRegFinishClearingRF edition here that doesn't check X86_EFL_TF. */
        AssertCompile(CPUMCTX_INHIBIT_SHADOW < UINT32_MAX);
        if (RT_LIKELY(!(  pVCpu->cpum.GstCtx.eflags.uBoth
                        & (X86_EFL_RF | CPUMCTX_INHIBIT_SHADOW | CPUMCTX_DBG_HIT_DRX_MASK | CPUMCTX_DBG_DBGF_MASK)) ))
            return VINF_SUCCESS;
        return iemFinishInstructionWithFlagsSet<0 /*a_fTF*/>(pVCpu, VINF_SUCCESS); /* TF=0, so ignore it.  */
    }
    return iemFinishInstructionWithTfSet(pVCpu);
}


#ifndef IEM_WITH_OPAQUE_DECODER_STATE
/**
 * Updates the RIP/EIP/IP to point to the next instruction and clears EFLAGS.RF.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
DECLINLINE(VBOXSTRICTRC) iemRegUpdateRipAndFinishClearingRF(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, IEM_GET_INSTR_LEN(pVCpu));
}
#endif


#ifdef IEM_WITH_CODE_TLB

/**
 * Performs a near jump to the specified address, no checking or clearing of
 * flags
 *
 * May raise a \#GP(0) if the new IP outside the code segment limit.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   uNewIp              The new IP value.
 */
DECLINLINE(VBOXSTRICTRC) iemRegRipJumpU16AndFinishNoFlags(PVMCPUCC pVCpu, uint16_t uNewIp) RT_NOEXCEPT
{
    if (RT_LIKELY(   uNewIp <= pVCpu->cpum.GstCtx.cs.u32Limit
                  || IEM_IS_64BIT_CODE(pVCpu) /* no limit checks in 64-bit mode */))
        pVCpu->cpum.GstCtx.rip = uNewIp;
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);
    return iemRegFinishNoFlags(pVCpu, VINF_SUCCESS);
}


/**
 * Performs a near jump to the specified address, no checking or clearing of
 * flags
 *
 * May raise a \#GP(0) if the new RIP is outside the code segment limit.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   uNewEip             The new EIP value.
 */
DECLINLINE(VBOXSTRICTRC) iemRegRipJumpU32AndFinishNoFlags(PVMCPUCC pVCpu, uint32_t uNewEip) RT_NOEXCEPT
{
    Assert(pVCpu->cpum.GstCtx.rip <= UINT32_MAX);
    Assert(!IEM_IS_64BIT_CODE(pVCpu));
    if (RT_LIKELY(uNewEip <= pVCpu->cpum.GstCtx.cs.u32Limit))
        pVCpu->cpum.GstCtx.rip = uNewEip;
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);
    return iemRegFinishNoFlags(pVCpu, VINF_SUCCESS);
}


/**
 * Performs a near jump to the specified address, no checking or clearing of
 * flags.
 *
 * May raise a \#GP(0) if the new RIP is non-canonical or outside the code
 * segment limit.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   uNewRip             The new RIP value.
 */
DECLINLINE(VBOXSTRICTRC) iemRegRipJumpU64AndFinishNoFlags(PVMCPUCC pVCpu, uint64_t uNewRip) RT_NOEXCEPT
{
    Assert(IEM_IS_64BIT_CODE(pVCpu));
    if (RT_LIKELY(IEM_IS_CANONICAL(uNewRip)))
        pVCpu->cpum.GstCtx.rip = uNewRip;
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);
    return iemRegFinishNoFlags(pVCpu, VINF_SUCCESS);
}

#endif /* IEM_WITH_CODE_TLB */

/**
 * Performs a near jump to the specified address.
 *
 * May raise a \#GP(0) if the new IP outside the code segment limit.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   uNewIp              The new IP value.
 * @param   cbInstr             The instruction length, for flushing in the non-TLB case.
 */
DECLINLINE(VBOXSTRICTRC) iemRegRipJumpU16AndFinishClearingRF(PVMCPUCC pVCpu, uint16_t uNewIp, uint8_t cbInstr) RT_NOEXCEPT
{
    if (RT_LIKELY(   uNewIp <= pVCpu->cpum.GstCtx.cs.u32Limit
                  || IEM_IS_64BIT_CODE(pVCpu) /* no limit checks in 64-bit mode */))
        pVCpu->cpum.GstCtx.rip = uNewIp;
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);
#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#else
    RT_NOREF_PV(cbInstr);
#endif
    return iemRegFinishClearingRF(pVCpu, VINF_SUCCESS);
}


/**
 * Performs a near jump to the specified address.
 *
 * May raise a \#GP(0) if the new RIP is outside the code segment limit.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   uNewEip             The new EIP value.
 * @param   cbInstr             The instruction length, for flushing in the non-TLB case.
 */
DECLINLINE(VBOXSTRICTRC) iemRegRipJumpU32AndFinishClearingRF(PVMCPUCC pVCpu, uint32_t uNewEip, uint8_t cbInstr) RT_NOEXCEPT
{
    Assert(pVCpu->cpum.GstCtx.rip <= UINT32_MAX);
    Assert(!IEM_IS_64BIT_CODE(pVCpu));
    if (RT_LIKELY(uNewEip <= pVCpu->cpum.GstCtx.cs.u32Limit))
        pVCpu->cpum.GstCtx.rip = uNewEip;
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);
#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#else
    RT_NOREF_PV(cbInstr);
#endif
    return iemRegFinishClearingRF(pVCpu, VINF_SUCCESS);
}


/**
 * Performs a near jump to the specified address.
 *
 * May raise a \#GP(0) if the new RIP is non-canonical or outside the code
 * segment limit.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   uNewRip             The new RIP value.
 * @param   cbInstr             The instruction length, for flushing in the non-TLB case.
 */
DECLINLINE(VBOXSTRICTRC) iemRegRipJumpU64AndFinishClearingRF(PVMCPUCC pVCpu, uint64_t uNewRip, uint8_t cbInstr) RT_NOEXCEPT
{
    Assert(IEM_IS_64BIT_CODE(pVCpu));
    if (RT_LIKELY(IEM_IS_CANONICAL(uNewRip)))
        pVCpu->cpum.GstCtx.rip = uNewRip;
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);
#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#else
    RT_NOREF_PV(cbInstr);
#endif
    return iemRegFinishClearingRF(pVCpu, VINF_SUCCESS);
}


/**
 * Implements a 16-bit relative call, no checking or clearing of
 * flags.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             The instruction length.
 * @param   offDisp             The 16-bit displacement.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC)
iemRegRipRelativeCallS16AndFinishNoFlags(PVMCPUCC pVCpu, uint8_t cbInstr, int16_t offDisp) RT_NOEXCEPT
{
    uint16_t const uOldIp = pVCpu->cpum.GstCtx.ip + cbInstr;
    uint16_t const uNewIp = uOldIp + offDisp;
    if (   uNewIp <= pVCpu->cpum.GstCtx.cs.u32Limit
        || IEM_IS_64BIT_CODE(pVCpu) /* no CS limit checks in 64-bit mode */)
    { /* likely */ }
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);

    VBOXSTRICTRC rcStrict = iemMemStackPushU16(pVCpu, uOldIp);
    if (rcStrict == VINF_SUCCESS)
    { /* likely */ }
    else
        return rcStrict;

    pVCpu->cpum.GstCtx.rip = uNewIp;
#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif
    return iemRegFinishNoFlags(pVCpu, VINF_SUCCESS);
}


/**
 * Implements a 16-bit relative call.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             The instruction length.
 * @param   offDisp             The 16-bit displacement.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC)
iemRegRipRelativeCallS16AndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr, int16_t offDisp) RT_NOEXCEPT
{
    uint16_t const uOldIp = pVCpu->cpum.GstCtx.ip + cbInstr;
    uint16_t const uNewIp = uOldIp + offDisp;
    if (   uNewIp <= pVCpu->cpum.GstCtx.cs.u32Limit
        || IEM_IS_64BIT_CODE(pVCpu) /* no CS limit checks in 64-bit mode */)
    { /* likely */ }
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);

    VBOXSTRICTRC rcStrict = iemMemStackPushU16(pVCpu, uOldIp);
    if (rcStrict == VINF_SUCCESS)
    { /* likely */ }
    else
        return rcStrict;

    pVCpu->cpum.GstCtx.rip = uNewIp;
#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif
    return iemRegFinishClearingRF(pVCpu, VINF_SUCCESS);
}


/**
 * Implements a 32-bit relative call, no checking or clearing of flags.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             The instruction length.
 * @param   offDisp             The 32-bit displacement.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC)
iemRegEip32RelativeCallS32AndFinishNoFlags(PVMCPUCC pVCpu, uint8_t cbInstr, int32_t offDisp) RT_NOEXCEPT
{
    Assert(pVCpu->cpum.GstCtx.rip <= UINT32_MAX); Assert(!IEM_IS_64BIT_CODE(pVCpu));

    uint32_t const uOldRip = pVCpu->cpum.GstCtx.eip + cbInstr;
    uint32_t const uNewRip = uOldRip + offDisp;
    if (uNewRip <= pVCpu->cpum.GstCtx.cs.u32Limit)
    { /* likely */ }
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);

    VBOXSTRICTRC rcStrict = iemMemStackPushU32(pVCpu, uOldRip);
    if (rcStrict == VINF_SUCCESS)
    { /* likely */ }
    else
        return rcStrict;

    pVCpu->cpum.GstCtx.rip = uNewRip;
#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif
    return iemRegFinishNoFlags(pVCpu, VINF_SUCCESS);
}


/**
 * Implements a 32-bit relative call.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             The instruction length.
 * @param   offDisp             The 32-bit displacement.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC)
iemRegEip32RelativeCallS32AndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr, int32_t offDisp) RT_NOEXCEPT
{
    Assert(pVCpu->cpum.GstCtx.rip <= UINT32_MAX); Assert(!IEM_IS_64BIT_CODE(pVCpu));

    uint32_t const uOldRip = pVCpu->cpum.GstCtx.eip + cbInstr;
    uint32_t const uNewRip = uOldRip + offDisp;
    if (uNewRip <= pVCpu->cpum.GstCtx.cs.u32Limit)
    { /* likely */ }
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);

    VBOXSTRICTRC rcStrict = iemMemStackPushU32(pVCpu, uOldRip);
    if (rcStrict == VINF_SUCCESS)
    { /* likely */ }
    else
        return rcStrict;

    pVCpu->cpum.GstCtx.rip = uNewRip;
#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif
    return iemRegFinishClearingRF(pVCpu, VINF_SUCCESS);
}


/**
 * Implements a 64-bit relative call, no checking or clearing of flags.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             The instruction length.
 * @param   offDisp             The 64-bit displacement.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC)
iemRegRip64RelativeCallS64AndFinishNoFlags(PVMCPUCC pVCpu, uint8_t cbInstr, int64_t offDisp) RT_NOEXCEPT
{
    uint64_t const uOldRip = pVCpu->cpum.GstCtx.rip + cbInstr;
    uint64_t const uNewRip = uOldRip + (int64_t)offDisp;
    if (IEM_IS_CANONICAL(uNewRip))
    { /* likely */ }
    else
        return iemRaiseNotCanonical(pVCpu);

    VBOXSTRICTRC rcStrict = iemMemStackPushU64(pVCpu, uOldRip);
    if (rcStrict == VINF_SUCCESS)
    { /* likely */ }
    else
        return rcStrict;

    pVCpu->cpum.GstCtx.rip = uNewRip;
#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif
    return iemRegFinishNoFlags(pVCpu, VINF_SUCCESS);
}


/**
 * Implements a 64-bit relative call.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             The instruction length.
 * @param   offDisp             The 64-bit displacement.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC)
iemRegRip64RelativeCallS64AndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr, int64_t offDisp) RT_NOEXCEPT
{
    uint64_t const uOldRip = pVCpu->cpum.GstCtx.rip + cbInstr;
    uint64_t const uNewRip = uOldRip + (int64_t)offDisp;
    if (IEM_IS_CANONICAL(uNewRip))
    { /* likely */ }
    else
        return iemRaiseNotCanonical(pVCpu);

    VBOXSTRICTRC rcStrict = iemMemStackPushU64(pVCpu, uOldRip);
    if (rcStrict == VINF_SUCCESS)
    { /* likely */ }
    else
        return rcStrict;

    pVCpu->cpum.GstCtx.rip = uNewRip;
#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif
    return iemRegFinishClearingRF(pVCpu, VINF_SUCCESS);
}


/**
 * Implements an 16-bit indirect call, no checking or clearing of
 * flags.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             The instruction length.
 * @param   uNewRip             The new RIP value.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC)
iemRegIp16IndirectCallU16AndFinishNoFlags(PVMCPUCC pVCpu, uint8_t cbInstr, uint16_t uNewRip) RT_NOEXCEPT
{
    uint16_t const uOldRip = pVCpu->cpum.GstCtx.ip + cbInstr;
    if (uNewRip <= pVCpu->cpum.GstCtx.cs.u32Limit)
    { /* likely */ }
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);

    VBOXSTRICTRC rcStrict = iemMemStackPushU16(pVCpu, uOldRip);
    if (rcStrict == VINF_SUCCESS)
    { /* likely */ }
    else
        return rcStrict;

    pVCpu->cpum.GstCtx.rip = uNewRip;
#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif
    return iemRegFinishNoFlags(pVCpu, VINF_SUCCESS);
}


/**
 * Implements an 16-bit indirect call, no checking or clearing of
 * flags.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             The instruction length.
 * @param   uNewRip             The new RIP value.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC)
iemRegEip32IndirectCallU16AndFinishNoFlags(PVMCPUCC pVCpu, uint8_t cbInstr, uint16_t uNewRip) RT_NOEXCEPT
{
    uint16_t const uOldRip = pVCpu->cpum.GstCtx.ip + cbInstr;
    if (uNewRip <= pVCpu->cpum.GstCtx.cs.u32Limit)
    { /* likely */ }
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);

    VBOXSTRICTRC rcStrict = iemMemStackPushU16(pVCpu, uOldRip);
    if (rcStrict == VINF_SUCCESS)
    { /* likely */ }
    else
        return rcStrict;

    pVCpu->cpum.GstCtx.rip = uNewRip;
#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif
    return iemRegFinishNoFlags(pVCpu, VINF_SUCCESS);
}


/**
 * Implements an 16-bit indirect call.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             The instruction length.
 * @param   uNewRip             The new RIP value.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC)
iemRegIp16IndirectCallU16AndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr, uint16_t uNewRip) RT_NOEXCEPT
{
    uint16_t const uOldRip = pVCpu->cpum.GstCtx.ip + cbInstr;
    if (uNewRip <= pVCpu->cpum.GstCtx.cs.u32Limit)
    { /* likely */ }
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);

    VBOXSTRICTRC rcStrict = iemMemStackPushU16(pVCpu, uOldRip);
    if (rcStrict == VINF_SUCCESS)
    { /* likely */ }
    else
        return rcStrict;

    pVCpu->cpum.GstCtx.rip = uNewRip;
#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif
    return iemRegFinishClearingRF(pVCpu, VINF_SUCCESS);
}


/**
 * Implements an 16-bit indirect call.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             The instruction length.
 * @param   uNewRip             The new RIP value.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC)
iemRegEip32IndirectCallU16AndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr, uint16_t uNewRip) RT_NOEXCEPT
{
    uint16_t const uOldRip = pVCpu->cpum.GstCtx.ip + cbInstr;
    if (uNewRip <= pVCpu->cpum.GstCtx.cs.u32Limit)
    { /* likely */ }
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);

    VBOXSTRICTRC rcStrict = iemMemStackPushU16(pVCpu, uOldRip);
    if (rcStrict == VINF_SUCCESS)
    { /* likely */ }
    else
        return rcStrict;

    pVCpu->cpum.GstCtx.rip = uNewRip;
#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif
    return iemRegFinishClearingRF(pVCpu, VINF_SUCCESS);
}


/**
 * Implements an 32-bit indirect call, no checking or clearing of
 * flags.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             The instruction length.
 * @param   uNewRip             The new RIP value.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC)
iemRegEip32IndirectCallU32AndFinishNoFlags(PVMCPUCC pVCpu, uint8_t cbInstr, uint32_t uNewRip) RT_NOEXCEPT
{
    uint32_t const uOldRip = pVCpu->cpum.GstCtx.eip + cbInstr;
    if (uNewRip <= pVCpu->cpum.GstCtx.cs.u32Limit)
    { /* likely */ }
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);

    VBOXSTRICTRC rcStrict = iemMemStackPushU32(pVCpu, uOldRip);
    if (rcStrict == VINF_SUCCESS)
    { /* likely */ }
    else
        return rcStrict;

    pVCpu->cpum.GstCtx.rip = uNewRip;
#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif
    return iemRegFinishNoFlags(pVCpu, VINF_SUCCESS);
}


/**
 * Implements an 32-bit indirect call.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             The instruction length.
 * @param   uNewRip             The new RIP value.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC)
iemRegEip32IndirectCallU32AndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr, uint32_t uNewRip) RT_NOEXCEPT
{
    uint32_t const uOldRip = pVCpu->cpum.GstCtx.eip + cbInstr;
    if (uNewRip <= pVCpu->cpum.GstCtx.cs.u32Limit)
    { /* likely */ }
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);

    VBOXSTRICTRC rcStrict = iemMemStackPushU32(pVCpu, uOldRip);
    if (rcStrict == VINF_SUCCESS)
    { /* likely */ }
    else
        return rcStrict;

    pVCpu->cpum.GstCtx.rip = uNewRip;
#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif
    return iemRegFinishClearingRF(pVCpu, VINF_SUCCESS);
}


/**
 * Implements an 64-bit indirect call, no checking or clearing of
 * flags.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             The instruction length.
 * @param   uNewRip             The new RIP value.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC)
iemRegRip64IndirectCallU64AndFinishNoFlags(PVMCPUCC pVCpu, uint8_t cbInstr, uint64_t uNewRip) RT_NOEXCEPT
{
    uint64_t const uOldRip = pVCpu->cpum.GstCtx.rip + cbInstr;
    if (IEM_IS_CANONICAL(uNewRip))
    { /* likely */ }
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);

    VBOXSTRICTRC rcStrict = iemMemStackPushU64(pVCpu, uOldRip);
    if (rcStrict == VINF_SUCCESS)
    { /* likely */ }
    else
        return rcStrict;

    pVCpu->cpum.GstCtx.rip = uNewRip;
#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif
    return iemRegFinishNoFlags(pVCpu, VINF_SUCCESS);
}


/**
 * Implements an 64-bit indirect call.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbInstr             The instruction length.
 * @param   uNewRip             The new RIP value.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC)
iemRegRip64IndirectCallU64AndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr, uint64_t uNewRip) RT_NOEXCEPT
{
    uint64_t const uOldRip = pVCpu->cpum.GstCtx.rip + cbInstr;
    if (IEM_IS_CANONICAL(uNewRip))
    { /* likely */ }
    else
        return iemRaiseGeneralProtectionFault0(pVCpu);

    VBOXSTRICTRC rcStrict = iemMemStackPushU64(pVCpu, uOldRip);
    if (rcStrict == VINF_SUCCESS)
    { /* likely */ }
    else
        return rcStrict;

    pVCpu->cpum.GstCtx.rip = uNewRip;
#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif
    return iemRegFinishClearingRF(pVCpu, VINF_SUCCESS);
}



/**
 * Adds to the stack pointer.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbToAdd             The number of bytes to add (8-bit!).
 */
DECLINLINE(void) iemRegAddToRsp(PVMCPUCC pVCpu, uint8_t cbToAdd) RT_NOEXCEPT
{
    if (IEM_IS_64BIT_CODE(pVCpu))
        pVCpu->cpum.GstCtx.rsp += cbToAdd;
    else if (pVCpu->cpum.GstCtx.ss.Attr.n.u1DefBig)
        pVCpu->cpum.GstCtx.esp += cbToAdd;
    else
        pVCpu->cpum.GstCtx.sp  += cbToAdd;
}


/**
 * Subtracts from the stack pointer.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbToSub             The number of bytes to subtract (8-bit!).
 */
DECLINLINE(void) iemRegSubFromRsp(PVMCPUCC pVCpu, uint8_t cbToSub) RT_NOEXCEPT
{
    if (IEM_IS_64BIT_CODE(pVCpu))
        pVCpu->cpum.GstCtx.rsp -= cbToSub;
    else if (pVCpu->cpum.GstCtx.ss.Attr.n.u1DefBig)
        pVCpu->cpum.GstCtx.esp -= cbToSub;
    else
        pVCpu->cpum.GstCtx.sp  -= cbToSub;
}


/**
 * Adds to the temporary stack pointer.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pTmpRsp             The temporary SP/ESP/RSP to update.
 * @param   cbToAdd             The number of bytes to add (16-bit).
 */
DECLINLINE(void) iemRegAddToRspEx(PCVMCPU pVCpu, PRTUINT64U pTmpRsp, uint16_t cbToAdd) RT_NOEXCEPT
{
    if (IEM_IS_64BIT_CODE(pVCpu))
        pTmpRsp->u           += cbToAdd;
    else if (pVCpu->cpum.GstCtx.ss.Attr.n.u1DefBig)
        pTmpRsp->DWords.dw0  += cbToAdd;
    else
        pTmpRsp->Words.w0    += cbToAdd;
}


/**
 * Subtracts from the temporary stack pointer.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pTmpRsp             The temporary SP/ESP/RSP to update.
 * @param   cbToSub             The number of bytes to subtract.
 * @remarks The @a cbToSub argument *MUST* be 16-bit, iemCImpl_enter is
 *          expecting that.
 */
DECLINLINE(void) iemRegSubFromRspEx(PCVMCPU pVCpu, PRTUINT64U pTmpRsp, uint16_t cbToSub) RT_NOEXCEPT
{
    if (IEM_IS_64BIT_CODE(pVCpu))
        pTmpRsp->u          -= cbToSub;
    else if (pVCpu->cpum.GstCtx.ss.Attr.n.u1DefBig)
        pTmpRsp->DWords.dw0 -= cbToSub;
    else
        pTmpRsp->Words.w0   -= cbToSub;
}


/**
 * Calculates the effective stack address for a push of the specified size as
 * well as the new RSP value (upper bits may be masked).
 *
 * @returns Effective stack addressf for the push.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbItem              The size of the stack item to pop.
 * @param   puNewRsp            Where to return the new RSP value.
 */
DECLINLINE(RTGCPTR) iemRegGetRspForPush(PCVMCPU pVCpu, uint8_t cbItem, uint64_t *puNewRsp) RT_NOEXCEPT
{
    RTUINT64U   uTmpRsp;
    RTGCPTR     GCPtrTop;
    uTmpRsp.u = pVCpu->cpum.GstCtx.rsp;

    if (IEM_IS_64BIT_CODE(pVCpu))
        GCPtrTop = uTmpRsp.u            -= cbItem;
    else if (pVCpu->cpum.GstCtx.ss.Attr.n.u1DefBig)
        GCPtrTop = uTmpRsp.DWords.dw0   -= cbItem;
    else
        GCPtrTop = uTmpRsp.Words.w0     -= cbItem;
    *puNewRsp = uTmpRsp.u;
    return GCPtrTop;
}


/**
 * Gets the current stack pointer and calculates the value after a pop of the
 * specified size.
 *
 * @returns Current stack pointer.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   cbItem              The size of the stack item to pop.
 * @param   puNewRsp            Where to return the new RSP value.
 */
DECLINLINE(RTGCPTR) iemRegGetRspForPop(PCVMCPU pVCpu, uint8_t cbItem, uint64_t *puNewRsp) RT_NOEXCEPT
{
    RTUINT64U   uTmpRsp;
    RTGCPTR     GCPtrTop;
    uTmpRsp.u = pVCpu->cpum.GstCtx.rsp;

    if (IEM_IS_64BIT_CODE(pVCpu))
    {
        GCPtrTop = uTmpRsp.u;
        uTmpRsp.u += cbItem;
    }
    else if (pVCpu->cpum.GstCtx.ss.Attr.n.u1DefBig)
    {
        GCPtrTop = uTmpRsp.DWords.dw0;
        uTmpRsp.DWords.dw0 += cbItem;
    }
    else
    {
        GCPtrTop = uTmpRsp.Words.w0;
        uTmpRsp.Words.w0 += cbItem;
    }
    *puNewRsp = uTmpRsp.u;
    return GCPtrTop;
}


/**
 * Calculates the effective stack address for a push of the specified size as
 * well as the new temporary RSP value (upper bits may be masked).
 *
 * @returns Effective stack addressf for the push.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pTmpRsp             The temporary stack pointer.  This is updated.
 * @param   cbItem              The size of the stack item to pop.
 */
DECLINLINE(RTGCPTR) iemRegGetRspForPushEx(PCVMCPU pVCpu, PRTUINT64U pTmpRsp, uint8_t cbItem) RT_NOEXCEPT
{
    RTGCPTR GCPtrTop;

    if (IEM_IS_64BIT_CODE(pVCpu))
        GCPtrTop = pTmpRsp->u          -= cbItem;
    else if (pVCpu->cpum.GstCtx.ss.Attr.n.u1DefBig)
        GCPtrTop = pTmpRsp->DWords.dw0 -= cbItem;
    else
        GCPtrTop = pTmpRsp->Words.w0   -= cbItem;
    return GCPtrTop;
}


/**
 * Gets the effective stack address for a pop of the specified size and
 * calculates and updates the temporary RSP.
 *
 * @returns Current stack pointer.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pTmpRsp             The temporary stack pointer.  This is updated.
 * @param   cbItem              The size of the stack item to pop.
 */
DECLINLINE(RTGCPTR) iemRegGetRspForPopEx(PCVMCPU pVCpu, PRTUINT64U pTmpRsp, uint8_t cbItem) RT_NOEXCEPT
{
    RTGCPTR GCPtrTop;
    if (IEM_IS_64BIT_CODE(pVCpu))
    {
        GCPtrTop = pTmpRsp->u;
        pTmpRsp->u          += cbItem;
    }
    else if (pVCpu->cpum.GstCtx.ss.Attr.n.u1DefBig)
    {
        GCPtrTop = pTmpRsp->DWords.dw0;
        pTmpRsp->DWords.dw0 += cbItem;
    }
    else
    {
        GCPtrTop = pTmpRsp->Words.w0;
        pTmpRsp->Words.w0   += cbItem;
    }
    return GCPtrTop;
}


/** Common body for iemRegRipNearReturnAndFinishClearingRF()
 * and iemRegRipNearReturnAndFinishNoFlags(). */
template<bool a_fWithFlags>
DECL_FORCE_INLINE(VBOXSTRICTRC)
iemRegRipNearReturnCommon(PVMCPUCC pVCpu, uint8_t cbInstr, uint16_t cbPop, IEMMODE enmEffOpSize) RT_NOEXCEPT
{
    /* Fetch the new RIP from the stack. */
    VBOXSTRICTRC    rcStrict;
    RTUINT64U       NewRip;
    RTUINT64U       NewRsp;
    NewRsp.u = pVCpu->cpum.GstCtx.rsp;
    switch (enmEffOpSize)
    {
        case IEMMODE_16BIT:
            NewRip.u = 0;
            rcStrict = iemMemStackPopU16Ex(pVCpu, &NewRip.Words.w0, &NewRsp);
            break;
        case IEMMODE_32BIT:
            NewRip.u = 0;
            rcStrict = iemMemStackPopU32Ex(pVCpu, &NewRip.DWords.dw0, &NewRsp);
            break;
        case IEMMODE_64BIT:
            rcStrict = iemMemStackPopU64Ex(pVCpu, &NewRip.u, &NewRsp);
            break;
        IEM_NOT_REACHED_DEFAULT_CASE_RET();
    }
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;

    /* Check the new ew RIP before loading it. */
    /** @todo Should test this as the intel+amd pseudo code doesn't mention half
     *        of it.  The canonical test is performed here and for call. */
    if (enmEffOpSize != IEMMODE_64BIT)
    {
        if (RT_LIKELY(NewRip.DWords.dw0 <= pVCpu->cpum.GstCtx.cs.u32Limit))
        { /* likely */ }
        else
        {
            Log(("retn newrip=%llx - out of bounds (%x) -> #GP\n", NewRip.u, pVCpu->cpum.GstCtx.cs.u32Limit));
            return iemRaiseSelectorBounds(pVCpu, X86_SREG_CS, IEM_ACCESS_INSTRUCTION);
        }
    }
    else
    {
        if (RT_LIKELY(IEM_IS_CANONICAL(NewRip.u)))
        { /* likely */ }
        else
        {
            Log(("retn newrip=%llx - not canonical -> #GP\n", NewRip.u));
            return iemRaiseNotCanonical(pVCpu);
        }
    }

    /* Apply cbPop */
    if (cbPop)
        iemRegAddToRspEx(pVCpu, &NewRsp, cbPop);

    /* Commit it. */
    pVCpu->cpum.GstCtx.rip = NewRip.u;
    pVCpu->cpum.GstCtx.rsp = NewRsp.u;

    /* Flush the prefetch buffer. */
#ifndef IEM_WITH_CODE_TLB
    iemOpcodeFlushLight(pVCpu, cbInstr);
#endif
    RT_NOREF(cbInstr);


    if (a_fWithFlags)
        return iemRegFinishClearingRF(pVCpu, VINF_SUCCESS);
    return iemRegFinishNoFlags(pVCpu, VINF_SUCCESS);
}


/**
 * Implements retn and retn imm16.
 *
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling thread.
 * @param   cbInstr         The current instruction length.
 * @param   enmEffOpSize    The effective operand size.  This is constant.
 * @param   cbPop           The amount of arguments to pop from the stack
 *                          (bytes).  This can be constant (zero).
 */
DECL_FORCE_INLINE(VBOXSTRICTRC)
iemRegRipNearReturnAndFinishClearingRF(PVMCPUCC pVCpu, uint8_t cbInstr, uint16_t cbPop, IEMMODE enmEffOpSize) RT_NOEXCEPT
{
    return iemRegRipNearReturnCommon<true /*a_fWithFlags*/>(pVCpu, cbInstr, cbPop, enmEffOpSize);
}


/**
 * Implements retn and retn imm16, no checking or clearing of
 * flags.
 *
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling thread.
 * @param   cbInstr         The current instruction length.
 * @param   enmEffOpSize    The effective operand size.  This is constant.
 * @param   cbPop           The amount of arguments to pop from the stack
 *                          (bytes).  This can be constant (zero).
 */
DECL_FORCE_INLINE(VBOXSTRICTRC)
iemRegRipNearReturnAndFinishNoFlags(PVMCPUCC pVCpu, uint8_t cbInstr, uint16_t cbPop, IEMMODE enmEffOpSize) RT_NOEXCEPT
{
    return iemRegRipNearReturnCommon<false /*a_fWithFlags*/>(pVCpu, cbInstr, cbPop, enmEffOpSize);
}

/** @}  */


/** @name   FPU access and helpers.
 *
 * @{
 */


/**
 * Hook for preparing to use the host FPU.
 *
 * This is necessary in ring-0 and raw-mode context (nop in ring-3).
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
DECLINLINE(void) iemFpuPrepareUsage(PVMCPUCC pVCpu) RT_NOEXCEPT
{
#ifdef IN_RING3
    CPUMSetChangedFlags(pVCpu, CPUM_CHANGED_FPU_REM);
#else
    CPUMRZFpuStatePrepareHostCpuForUse(pVCpu);
#endif
    IEM_CTX_IMPORT_NORET(pVCpu, CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX | CPUMCTX_EXTRN_OTHER_XSAVE | CPUMCTX_EXTRN_XCRx);
}


/**
 * Hook for preparing to use the host FPU for SSE.
 *
 * This is necessary in ring-0 and raw-mode context (nop in ring-3).
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
DECLINLINE(void) iemFpuPrepareUsageSse(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    iemFpuPrepareUsage(pVCpu);
}


/**
 * Hook for preparing to use the host FPU for AVX.
 *
 * This is necessary in ring-0 and raw-mode context (nop in ring-3).
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
DECLINLINE(void) iemFpuPrepareUsageAvx(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    iemFpuPrepareUsage(pVCpu);
}


/**
 * Hook for actualizing the guest FPU state before the interpreter reads it.
 *
 * This is necessary in ring-0 and raw-mode context (nop in ring-3).
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
DECLINLINE(void) iemFpuActualizeStateForRead(PVMCPUCC pVCpu) RT_NOEXCEPT
{
#ifdef IN_RING3
    NOREF(pVCpu);
#else
    CPUMRZFpuStateActualizeForRead(pVCpu);
#endif
    IEM_CTX_IMPORT_NORET(pVCpu, CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX | CPUMCTX_EXTRN_OTHER_XSAVE | CPUMCTX_EXTRN_XCRx);
}


/**
 * Hook for actualizing the guest FPU state before the interpreter changes it.
 *
 * This is necessary in ring-0 and raw-mode context (nop in ring-3).
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
DECLINLINE(void) iemFpuActualizeStateForChange(PVMCPUCC pVCpu) RT_NOEXCEPT
{
#ifdef IN_RING3
    CPUMSetChangedFlags(pVCpu, CPUM_CHANGED_FPU_REM);
#else
    CPUMRZFpuStateActualizeForChange(pVCpu);
#endif
    IEM_CTX_IMPORT_NORET(pVCpu, CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX | CPUMCTX_EXTRN_OTHER_XSAVE | CPUMCTX_EXTRN_XCRx);
}


/**
 * Hook for actualizing the guest XMM0..15 and MXCSR register state for read
 * only.
 *
 * This is necessary in ring-0 and raw-mode context (nop in ring-3).
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
DECLINLINE(void) iemFpuActualizeSseStateForRead(PVMCPUCC pVCpu) RT_NOEXCEPT
{
#if defined(IN_RING3) || defined(VBOX_WITH_KERNEL_USING_XMM)
    NOREF(pVCpu);
#else
    CPUMRZFpuStateActualizeSseForRead(pVCpu);
#endif
    IEM_CTX_IMPORT_NORET(pVCpu, CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX | CPUMCTX_EXTRN_OTHER_XSAVE | CPUMCTX_EXTRN_XCRx);
}


/**
 * Hook for actualizing the guest XMM0..15 and MXCSR register state for
 * read+write.
 *
 * This is necessary in ring-0 and raw-mode context (nop in ring-3).
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
DECLINLINE(void) iemFpuActualizeSseStateForChange(PVMCPUCC pVCpu) RT_NOEXCEPT
{
#if defined(IN_RING3) || defined(VBOX_WITH_KERNEL_USING_XMM)
    CPUMSetChangedFlags(pVCpu, CPUM_CHANGED_FPU_REM);
#else
    CPUMRZFpuStateActualizeForChange(pVCpu);
#endif
    IEM_CTX_IMPORT_NORET(pVCpu, CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX | CPUMCTX_EXTRN_OTHER_XSAVE | CPUMCTX_EXTRN_XCRx);

    /* Make sure any changes are loaded the next time around. */
    pVCpu->cpum.GstCtx.XState.Hdr.bmXState |= XSAVE_C_SSE;
}


/**
 * Hook for actualizing the guest YMM0..15 and MXCSR register state for read
 * only.
 *
 * This is necessary in ring-0 and raw-mode context (nop in ring-3).
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
DECLINLINE(void) iemFpuActualizeAvxStateForRead(PVMCPUCC pVCpu) RT_NOEXCEPT
{
#ifdef IN_RING3
    NOREF(pVCpu);
#else
    CPUMRZFpuStateActualizeAvxForRead(pVCpu);
#endif
    IEM_CTX_IMPORT_NORET(pVCpu, CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX | CPUMCTX_EXTRN_OTHER_XSAVE | CPUMCTX_EXTRN_XCRx);
}


/**
 * Hook for actualizing the guest YMM0..15 and MXCSR register state for
 * read+write.
 *
 * This is necessary in ring-0 and raw-mode context (nop in ring-3).
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
DECLINLINE(void) iemFpuActualizeAvxStateForChange(PVMCPUCC pVCpu) RT_NOEXCEPT
{
#ifdef IN_RING3
    CPUMSetChangedFlags(pVCpu, CPUM_CHANGED_FPU_REM);
#else
    CPUMRZFpuStateActualizeForChange(pVCpu);
#endif
    IEM_CTX_IMPORT_NORET(pVCpu, CPUMCTX_EXTRN_X87 | CPUMCTX_EXTRN_SSE_AVX | CPUMCTX_EXTRN_OTHER_XSAVE | CPUMCTX_EXTRN_XCRx);

    /* Just assume we're going to make changes to the SSE and YMM_HI parts. */
    pVCpu->cpum.GstCtx.XState.Hdr.bmXState |= XSAVE_C_YMM | XSAVE_C_SSE;
}


/**
 * Stores a QNaN value into a FPU register.
 *
 * @param   pReg                Pointer to the register.
 */
DECLINLINE(void) iemFpuStoreQNan(PRTFLOAT80U pReg) RT_NOEXCEPT
{
    pReg->au32[0] = UINT32_C(0x00000000);
    pReg->au32[1] = UINT32_C(0xc0000000);
    pReg->au16[4] = UINT16_C(0xffff);
}


/**
 * Updates the FOP, FPU.CS and FPUIP registers, extended version.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pFpuCtx             The FPU context.
 * @param   uFpuOpcode          The FPU opcode value (see IEMCPU::uFpuOpcode).
 */
DECLINLINE(void) iemFpuUpdateOpcodeAndIpWorkerEx(PVMCPUCC pVCpu, PX86FXSTATE pFpuCtx, uint16_t uFpuOpcode) RT_NOEXCEPT
{
    Assert(uFpuOpcode != UINT16_MAX);
    pFpuCtx->FOP = uFpuOpcode;
    /** @todo x87.CS and FPUIP needs to be kept separately. */
    if (IEM_IS_REAL_OR_V86_MODE(pVCpu))
    {
        /** @todo Testcase: making assumptions about how FPUIP and FPUDP are handled
         *        happens in real mode here based on the fnsave and fnstenv images. */
        pFpuCtx->CS    = 0;
        pFpuCtx->FPUIP = pVCpu->cpum.GstCtx.eip | ((uint32_t)pVCpu->cpum.GstCtx.cs.Sel << 4);
    }
    else if (!IEM_IS_LONG_MODE(pVCpu))
    {
        pFpuCtx->CS    = pVCpu->cpum.GstCtx.cs.Sel;
        pFpuCtx->FPUIP = pVCpu->cpum.GstCtx.rip;
    }
    else
        *(uint64_t *)&pFpuCtx->FPUIP = pVCpu->cpum.GstCtx.rip;
}


/**
 * Marks the specified stack register as free (for FFREE).
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   iStReg              The register to free.
 */
DECLINLINE(void) iemFpuStackFree(PVMCPUCC pVCpu, uint8_t iStReg) RT_NOEXCEPT
{
    Assert(iStReg < 8);
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    uint8_t     iReg    = (X86_FSW_TOP_GET(pFpuCtx->FSW) + iStReg) & X86_FSW_TOP_SMASK;
    pFpuCtx->FTW &= ~RT_BIT(iReg);
}


/**
 * Increments FSW.TOP, i.e. pops an item off the stack without freeing it.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
DECLINLINE(void) iemFpuStackIncTop(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    uint16_t    uFsw    = pFpuCtx->FSW;
    uint16_t    uTop    = uFsw & X86_FSW_TOP_MASK;
    uTop  = (uTop + (1 << X86_FSW_TOP_SHIFT)) & X86_FSW_TOP_MASK;
    uFsw &= ~X86_FSW_TOP_MASK;
    uFsw |= uTop;
    pFpuCtx->FSW = uFsw;
}


/**
 * Decrements FSW.TOP, i.e. push an item off the stack without storing anything.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
DECLINLINE(void) iemFpuStackDecTop(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    uint16_t    uFsw    = pFpuCtx->FSW;
    uint16_t    uTop    = uFsw & X86_FSW_TOP_MASK;
    uTop  = (uTop + (7 << X86_FSW_TOP_SHIFT)) & X86_FSW_TOP_MASK;
    uFsw &= ~X86_FSW_TOP_MASK;
    uFsw |= uTop;
    pFpuCtx->FSW = uFsw;
}




DECLINLINE(int) iemFpuStRegNotEmpty(PVMCPUCC pVCpu, uint8_t iStReg) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    uint16_t    iReg    = (X86_FSW_TOP_GET(pFpuCtx->FSW) + iStReg) & X86_FSW_TOP_SMASK;
    if (pFpuCtx->FTW & RT_BIT(iReg))
        return VINF_SUCCESS;
    return VERR_NOT_FOUND;
}


DECLINLINE(int) iemFpuStRegNotEmptyRef(PVMCPUCC pVCpu, uint8_t iStReg, PCRTFLOAT80U *ppRef) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    uint16_t    iReg    = (X86_FSW_TOP_GET(pFpuCtx->FSW) + iStReg) & X86_FSW_TOP_SMASK;
    if (pFpuCtx->FTW & RT_BIT(iReg))
    {
        *ppRef = &pFpuCtx->aRegs[iStReg].r80;
        return VINF_SUCCESS;
    }
    return VERR_NOT_FOUND;
}


DECLINLINE(int) iemFpu2StRegsNotEmptyRef(PVMCPUCC pVCpu, uint8_t iStReg0, PCRTFLOAT80U *ppRef0,
                                        uint8_t iStReg1, PCRTFLOAT80U *ppRef1) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    uint16_t    iTop    = X86_FSW_TOP_GET(pFpuCtx->FSW);
    uint16_t    iReg0   = (iTop + iStReg0) & X86_FSW_TOP_SMASK;
    uint16_t    iReg1   = (iTop + iStReg1) & X86_FSW_TOP_SMASK;
    if ((pFpuCtx->FTW & (RT_BIT(iReg0) | RT_BIT(iReg1))) == (RT_BIT(iReg0) | RT_BIT(iReg1)))
    {
        *ppRef0 = &pFpuCtx->aRegs[iStReg0].r80;
        *ppRef1 = &pFpuCtx->aRegs[iStReg1].r80;
        return VINF_SUCCESS;
    }
    return VERR_NOT_FOUND;
}


DECLINLINE(int) iemFpu2StRegsNotEmptyRefFirst(PVMCPUCC pVCpu, uint8_t iStReg0, PCRTFLOAT80U *ppRef0, uint8_t iStReg1) RT_NOEXCEPT
{
    PX86FXSTATE pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    uint16_t    iTop    = X86_FSW_TOP_GET(pFpuCtx->FSW);
    uint16_t    iReg0   = (iTop + iStReg0) & X86_FSW_TOP_SMASK;
    uint16_t    iReg1   = (iTop + iStReg1) & X86_FSW_TOP_SMASK;
    if ((pFpuCtx->FTW & (RT_BIT(iReg0) | RT_BIT(iReg1))) == (RT_BIT(iReg0) | RT_BIT(iReg1)))
    {
        *ppRef0 = &pFpuCtx->aRegs[iStReg0].r80;
        return VINF_SUCCESS;
    }
    return VERR_NOT_FOUND;
}


/**
 * Rotates the stack registers when setting new TOS.
 *
 * @param   pFpuCtx             The FPU context.
 * @param   iNewTop             New TOS value.
 * @remarks We only do this to speed up fxsave/fxrstor which
 *          arrange the FP registers in stack order.
 *          MUST be done before writing the new TOS (FSW).
 */
DECLINLINE(void) iemFpuRotateStackSetTop(PX86FXSTATE pFpuCtx, uint16_t iNewTop) RT_NOEXCEPT
{
    uint16_t iOldTop = X86_FSW_TOP_GET(pFpuCtx->FSW);
    RTFLOAT80U ar80Temp[8];

    if (iOldTop == iNewTop)
        return;

    /* Unscrew the stack and get it into 'native' order. */
    ar80Temp[0] = pFpuCtx->aRegs[(8 - iOldTop + 0) & X86_FSW_TOP_SMASK].r80;
    ar80Temp[1] = pFpuCtx->aRegs[(8 - iOldTop + 1) & X86_FSW_TOP_SMASK].r80;
    ar80Temp[2] = pFpuCtx->aRegs[(8 - iOldTop + 2) & X86_FSW_TOP_SMASK].r80;
    ar80Temp[3] = pFpuCtx->aRegs[(8 - iOldTop + 3) & X86_FSW_TOP_SMASK].r80;
    ar80Temp[4] = pFpuCtx->aRegs[(8 - iOldTop + 4) & X86_FSW_TOP_SMASK].r80;
    ar80Temp[5] = pFpuCtx->aRegs[(8 - iOldTop + 5) & X86_FSW_TOP_SMASK].r80;
    ar80Temp[6] = pFpuCtx->aRegs[(8 - iOldTop + 6) & X86_FSW_TOP_SMASK].r80;
    ar80Temp[7] = pFpuCtx->aRegs[(8 - iOldTop + 7) & X86_FSW_TOP_SMASK].r80;

    /* Now rotate the stack to the new position. */
    pFpuCtx->aRegs[0].r80 = ar80Temp[(iNewTop + 0) & X86_FSW_TOP_SMASK];
    pFpuCtx->aRegs[1].r80 = ar80Temp[(iNewTop + 1) & X86_FSW_TOP_SMASK];
    pFpuCtx->aRegs[2].r80 = ar80Temp[(iNewTop + 2) & X86_FSW_TOP_SMASK];
    pFpuCtx->aRegs[3].r80 = ar80Temp[(iNewTop + 3) & X86_FSW_TOP_SMASK];
    pFpuCtx->aRegs[4].r80 = ar80Temp[(iNewTop + 4) & X86_FSW_TOP_SMASK];
    pFpuCtx->aRegs[5].r80 = ar80Temp[(iNewTop + 5) & X86_FSW_TOP_SMASK];
    pFpuCtx->aRegs[6].r80 = ar80Temp[(iNewTop + 6) & X86_FSW_TOP_SMASK];
    pFpuCtx->aRegs[7].r80 = ar80Temp[(iNewTop + 7) & X86_FSW_TOP_SMASK];
}


/**
 * Updates the FPU exception status after FCW is changed.
 *
 * @param   pFpuCtx             The FPU context.
 */
DECLINLINE(void) iemFpuRecalcExceptionStatus(PX86FXSTATE pFpuCtx) RT_NOEXCEPT
{
    uint16_t u16Fsw = pFpuCtx->FSW;
    if ((u16Fsw & X86_FSW_XCPT_MASK) & ~(pFpuCtx->FCW & X86_FCW_XCPT_MASK))
        u16Fsw |= X86_FSW_ES | X86_FSW_B;
    else
        u16Fsw &= ~(X86_FSW_ES | X86_FSW_B);
    pFpuCtx->FSW = u16Fsw;
}


/**
 * Calculates the full FTW (FPU tag word) for use in FNSTENV and FNSAVE.
 *
 * @returns The full FTW.
 * @param   pFpuCtx             The FPU context.
 */
DECLINLINE(uint16_t) iemFpuCalcFullFtw(PCX86FXSTATE pFpuCtx) RT_NOEXCEPT
{
    uint8_t const   u8Ftw  = (uint8_t)pFpuCtx->FTW;
    uint16_t        u16Ftw = 0;
    unsigned const  iTop   = X86_FSW_TOP_GET(pFpuCtx->FSW);
    for (unsigned iSt = 0; iSt < 8; iSt++)
    {
        unsigned const iReg = (iSt + iTop) & 7;
        if (!(u8Ftw & RT_BIT(iReg)))
            u16Ftw |= 3 << (iReg * 2); /* empty */
        else
        {
            uint16_t uTag;
            PCRTFLOAT80U const pr80Reg = &pFpuCtx->aRegs[iSt].r80;
            if (pr80Reg->s.uExponent == 0x7fff)
                uTag = 2; /* Exponent is all 1's => Special. */
            else if (pr80Reg->s.uExponent == 0x0000)
            {
                if (pr80Reg->s.uMantissa == 0x0000)
                    uTag = 1; /* All bits are zero => Zero. */
                else
                    uTag = 2; /* Must be special. */
            }
            else if (pr80Reg->s.uMantissa & RT_BIT_64(63)) /* The J bit. */
                uTag = 0; /* Valid. */
            else
                uTag = 2; /* Must be special. */

            u16Ftw |= uTag << (iReg * 2);
        }
    }

    return u16Ftw;
}


/**
 * Converts a full FTW to a compressed one (for use in FLDENV and FRSTOR).
 *
 * @returns The compressed FTW.
 * @param   u16FullFtw      The full FTW to convert.
 */
DECLINLINE(uint16_t) iemFpuCompressFtw(uint16_t u16FullFtw) RT_NOEXCEPT
{
    uint8_t u8Ftw = 0;
    for (unsigned i = 0; i < 8; i++)
    {
        if ((u16FullFtw & 3) != 3 /*empty*/)
            u8Ftw |= RT_BIT(i);
        u16FullFtw >>= 2;
    }

    return u8Ftw;
}

/** @}  */


#ifdef VBOX_WITH_NESTED_HWVIRT_VMX

/**
 * Gets CR0 fixed-0 bits in VMX operation.
 *
 * We do this rather than fetching what we report to the guest (in
 * IA32_VMX_CR0_FIXED0 MSR) because real hardware (and so do we) report the same
 * values regardless of whether unrestricted-guest feature is available on the CPU.
 *
 * @returns CR0 fixed-0 bits.
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   fVmxNonRootMode     Whether the CR0 fixed-0 bits for VMX non-root mode
 *                              must be returned. When @c false, the CR0 fixed-0
 *                              bits for VMX root mode is returned.
 *
 */
DECLINLINE(uint64_t) iemVmxGetCr0Fixed0(PCVMCPUCC pVCpu, bool fVmxNonRootMode) RT_NOEXCEPT
{
    Assert(IEM_VMX_IS_ROOT_MODE(pVCpu));

    PCVMXMSRS  pMsrs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs;
    if (    fVmxNonRootMode
        && (pMsrs->ProcCtls2.n.allowed1 & VMX_PROC_CTLS2_UNRESTRICTED_GUEST))
        return VMX_V_CR0_FIXED0_UX;
    return VMX_V_CR0_FIXED0;
}


# ifdef XAPIC_OFF_END /* Requires VBox/apic.h to be included before IEMInline.h. */
/**
 * Sets virtual-APIC write emulation as pending.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   offApic     The offset in the virtual-APIC page that was written.
 */
DECLINLINE(void) iemVmxVirtApicSetPendingWrite(PVMCPUCC pVCpu, uint16_t offApic) RT_NOEXCEPT
{
    Assert(offApic < XAPIC_OFF_END + 4);

    /*
     * Record the currently updated APIC offset, as we need this later for figuring
     * out whether to perform TPR, EOI or self-IPI virtualization as well as well
     * as for supplying the exit qualification when causing an APIC-write VM-exit.
     */
    pVCpu->cpum.GstCtx.hwvirt.vmx.offVirtApicWrite = offApic;

    /*
     * Flag that we need to perform virtual-APIC write emulation (TPR/PPR/EOI/Self-IPI
     * virtualization or APIC-write emulation).
     */
    if (!VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_VMX_APIC_WRITE))
        VMCPU_FF_SET(pVCpu, VMCPU_FF_VMX_APIC_WRITE);
}
# endif /* XAPIC_OFF_END */

#endif /* VBOX_WITH_NESTED_HWVIRT_VMX */

#endif /* !VMM_INCLUDED_SRC_VMMAll_target_x86_IEMInline_x86_h */
