/* $Id: IEMInline.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - Inlined Functions.
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

#ifndef VMM_INCLUDED_SRC_include_IEMInline_h
#define VMM_INCLUDED_SRC_include_IEMInline_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif



/**
 * Makes status code addjustments (pass up from I/O and access handler)
 * as well as maintaining statistics.
 *
 * @returns Strict VBox status code to pass up.
 * @param   pVCpu       The cross context virtual CPU structure of the calling thread.
 * @param   rcStrict    The status from executing an instruction.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemExecStatusCodeFiddling(PVMCPUCC pVCpu, VBOXSTRICTRC rcStrict) RT_NOEXCEPT
{
    if (rcStrict != VINF_SUCCESS)
    {
        /* Deal with the cases that should be treated as VINF_SUCCESS first. */
        if (   rcStrict == VINF_IEM_YIELD_PENDING_FF
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX /** @todo r=bird: Why do we need TWO status codes here? */
            || rcStrict == VINF_VMX_VMEXIT
#endif
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
            || rcStrict == VINF_SVM_VMEXIT
#endif
            )
        {
            rcStrict = pVCpu->iem.s.rcPassUp;
            if (RT_LIKELY(rcStrict == VINF_SUCCESS))
            { /* likely */ }
            else
                pVCpu->iem.s.cRetPassUpStatus++;
        }
        else if (RT_SUCCESS(rcStrict))
        {
            AssertMsg(   (rcStrict >= VINF_EM_FIRST && rcStrict <= VINF_EM_LAST)
                      || rcStrict == VINF_IOM_R3_IOPORT_READ
                      || rcStrict == VINF_IOM_R3_IOPORT_WRITE
                      || rcStrict == VINF_IOM_R3_IOPORT_COMMIT_WRITE
                      || rcStrict == VINF_IOM_R3_MMIO_READ
                      || rcStrict == VINF_IOM_R3_MMIO_READ_WRITE
                      || rcStrict == VINF_IOM_R3_MMIO_WRITE
                      || rcStrict == VINF_IOM_R3_MMIO_COMMIT_WRITE
                      || rcStrict == VINF_CPUM_R3_MSR_READ
                      || rcStrict == VINF_CPUM_R3_MSR_WRITE
                      || rcStrict == VINF_EM_RAW_EMULATE_INSTR
                      || rcStrict == VINF_EM_RAW_TO_R3
                      || rcStrict == VINF_EM_TRIPLE_FAULT
                      || rcStrict == VINF_EM_EMULATE_SPLIT_LOCK
                      || rcStrict == VINF_GIM_R3_HYPERCALL
                      /* raw-mode / virt handlers only: */
                      || rcStrict == VINF_EM_RAW_EMULATE_INSTR_GDT_FAULT
                      || rcStrict == VINF_EM_RAW_EMULATE_INSTR_TSS_FAULT
                      || rcStrict == VINF_EM_RAW_EMULATE_INSTR_LDT_FAULT
                      || rcStrict == VINF_EM_RAW_EMULATE_INSTR_IDT_FAULT
                      || rcStrict == VINF_SELM_SYNC_GDT
                      || rcStrict == VINF_CSAM_PENDING_ACTION
                      || rcStrict == VINF_PATM_CHECK_PATCH_PAGE
                      /* nested hw.virt codes: */
                      || rcStrict == VINF_VMX_INTERCEPT_NOT_ACTIVE
                      || rcStrict == VINF_VMX_MODIFIES_BEHAVIOR
                      , ("rcStrict=%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
/** @todo adjust for VINF_EM_RAW_EMULATE_INSTR. */
            int32_t const rcPassUp = pVCpu->iem.s.rcPassUp;
            if (rcPassUp == VINF_SUCCESS)
                pVCpu->iem.s.cRetInfStatuses++;
            else if (   rcPassUp < VINF_EM_FIRST
                     || rcPassUp > VINF_EM_LAST
                     || rcPassUp < VBOXSTRICTRC_VAL(rcStrict))
            {
                LogEx(LOG_GROUP_IEM,("IEM: rcPassUp=%Rrc! rcStrict=%Rrc\n", rcPassUp, VBOXSTRICTRC_VAL(rcStrict)));
                pVCpu->iem.s.cRetPassUpStatus++;
                rcStrict = rcPassUp;
            }
            else
            {
                LogEx(LOG_GROUP_IEM,("IEM: rcPassUp=%Rrc  rcStrict=%Rrc!\n", rcPassUp, VBOXSTRICTRC_VAL(rcStrict)));
                pVCpu->iem.s.cRetInfStatuses++;
            }
        }
        else if (rcStrict == VERR_IEM_ASPECT_NOT_IMPLEMENTED)
            pVCpu->iem.s.cRetAspectNotImplemented++;
        else if (rcStrict == VERR_IEM_INSTR_NOT_IMPLEMENTED)
            pVCpu->iem.s.cRetInstrNotImplemented++;
        else
            pVCpu->iem.s.cRetErrStatuses++;
    }
    else
    {
        rcStrict = pVCpu->iem.s.rcPassUp;
        if (rcStrict != VINF_SUCCESS)
            pVCpu->iem.s.cRetPassUpStatus++;
    }

    /* Just clear it here as well. */
    pVCpu->iem.s.rcPassUp = VINF_SUCCESS;

    return rcStrict;
}


/**
 * Sets the pass up status.
 *
 * @returns VINF_SUCCESS.
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling thread.
 * @param   rcPassUp            The pass up status.  Must be informational.
 *                              VINF_SUCCESS is not allowed.
 */
DECLINLINE(int) iemSetPassUpStatus(PVMCPUCC pVCpu, VBOXSTRICTRC rcPassUp) RT_NOEXCEPT
{
    AssertRC(VBOXSTRICTRC_VAL(rcPassUp)); Assert(rcPassUp != VINF_SUCCESS);

    int32_t const rcOldPassUp = pVCpu->iem.s.rcPassUp;
    if (rcOldPassUp == VINF_SUCCESS)
        pVCpu->iem.s.rcPassUp = VBOXSTRICTRC_VAL(rcPassUp);
    /* If both are EM scheduling codes, use EM priority rules. */
    else if (   rcOldPassUp >= VINF_EM_FIRST && rcOldPassUp <= VINF_EM_LAST
             && rcPassUp    >= VINF_EM_FIRST && rcPassUp    <= VINF_EM_LAST)
    {
        if (rcPassUp < rcOldPassUp)
        {
            LogEx(LOG_GROUP_IEM,("IEM: rcPassUp=%Rrc! rcOldPassUp=%Rrc\n", VBOXSTRICTRC_VAL(rcPassUp), rcOldPassUp));
            pVCpu->iem.s.rcPassUp = VBOXSTRICTRC_VAL(rcPassUp);
        }
        else
            LogEx(LOG_GROUP_IEM,("IEM: rcPassUp=%Rrc  rcOldPassUp=%Rrc!\n", VBOXSTRICTRC_VAL(rcPassUp), rcOldPassUp));
    }
    /* Override EM scheduling with specific status code. */
    else if (rcOldPassUp >= VINF_EM_FIRST && rcOldPassUp <= VINF_EM_LAST)
    {
        LogEx(LOG_GROUP_IEM,("IEM: rcPassUp=%Rrc! rcOldPassUp=%Rrc\n", VBOXSTRICTRC_VAL(rcPassUp), rcOldPassUp));
        pVCpu->iem.s.rcPassUp = VBOXSTRICTRC_VAL(rcPassUp);
    }
    /* Don't override specific status code, first come first served. */
    else
        LogEx(LOG_GROUP_IEM,("IEM: rcPassUp=%Rrc  rcOldPassUp=%Rrc!\n", VBOXSTRICTRC_VAL(rcPassUp), rcOldPassUp));
    return VINF_SUCCESS;
}


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

/**
 * Calculates the the IEM_F_XXX flags.
 *
 * @returns IEM_F_XXX combination match the current CPU state.
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling thread.
 */
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


#ifndef IEM_WITH_OPAQUE_DECODER_STATE

# if defined(VBOX_INCLUDED_vmm_dbgf_h) || defined(DOXYGEN_RUNNING) /* dbgf.ro.cEnabledHwBreakpoints */

/**
 * Initializes the execution state.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling thread.
 * @param   fExecOpts           Optional execution flags:
 *                                  - IEM_F_BYPASS_HANDLERS
 *                                  - IEM_F_X86_DISREGARD_LOCK
 *
 * @remarks Callers of this must call iemUninitExec() to undo potentially fatal
 *          side-effects in strict builds.
 */
DECLINLINE(void) iemInitExec(PVMCPUCC pVCpu, uint32_t fExecOpts) RT_NOEXCEPT
{
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK);
    Assert(!VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_IEM));
    Assert(CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, &pVCpu->cpum.GstCtx.cs));
    Assert(CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, &pVCpu->cpum.GstCtx.ss));
    Assert(CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, &pVCpu->cpum.GstCtx.es));
    Assert(CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, &pVCpu->cpum.GstCtx.ds));
    Assert(CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, &pVCpu->cpum.GstCtx.fs));
    Assert(CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, &pVCpu->cpum.GstCtx.gs));
    Assert(CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, &pVCpu->cpum.GstCtx.ldtr));
    Assert(CPUMSELREG_ARE_HIDDEN_PARTS_VALID(pVCpu, &pVCpu->cpum.GstCtx.tr));

    pVCpu->iem.s.rcPassUp           = VINF_SUCCESS;
    pVCpu->iem.s.fExec              = iemCalcExecFlags(pVCpu) | fExecOpts;
    pVCpu->iem.s.cActiveMappings    = 0;
    pVCpu->iem.s.iNextMapping       = 0;

#  ifdef VBOX_STRICT
    pVCpu->iem.s.enmDefAddrMode     = (IEMMODE)0xfe;
    pVCpu->iem.s.enmEffAddrMode     = (IEMMODE)0xfe;
    pVCpu->iem.s.enmDefOpSize       = (IEMMODE)0xfe;
    pVCpu->iem.s.enmEffOpSize       = (IEMMODE)0xfe;
    pVCpu->iem.s.fPrefixes          = 0xfeedbeef;
    pVCpu->iem.s.uRexReg            = 127;
    pVCpu->iem.s.uRexB              = 127;
    pVCpu->iem.s.offModRm           = 127;
    pVCpu->iem.s.uRexIndex          = 127;
    pVCpu->iem.s.iEffSeg            = 127;
    pVCpu->iem.s.idxPrefix          = 127;
    pVCpu->iem.s.uVex3rdReg         = 127;
    pVCpu->iem.s.uVexLength         = 127;
    pVCpu->iem.s.fEvexStuff         = 127;
    pVCpu->iem.s.uFpuOpcode         = UINT16_MAX;
#   ifdef IEM_WITH_CODE_TLB
    pVCpu->iem.s.offInstrNextByte   = UINT16_MAX;
    pVCpu->iem.s.pbInstrBuf         = NULL;
    pVCpu->iem.s.cbInstrBuf         = UINT16_MAX;
    pVCpu->iem.s.cbInstrBufTotal    = UINT16_MAX;
    pVCpu->iem.s.offCurInstrStart   = INT16_MAX;
    pVCpu->iem.s.uInstrBufPc        = UINT64_C(0xc0ffc0ffcff0c0ff);
#    ifdef IEM_WITH_CODE_TLB_AND_OPCODE_BUF
    pVCpu->iem.s.offOpcode          = 127;
#    endif
#   else
    pVCpu->iem.s.offOpcode          = 127;
    pVCpu->iem.s.cbOpcode           = 127;
#   endif
#  endif /* VBOX_STRICT */
}


#  if defined(VBOX_WITH_NESTED_HWVIRT_SVM) || defined(VBOX_WITH_NESTED_HWVIRT_VMX)
/**
 * Performs a minimal reinitialization of the execution state.
 *
 * This is intended to be used by VM-exits, SMM, LOADALL and other similar
 * 'world-switch' types operations on the CPU. Currently only nested
 * hardware-virtualization uses it.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling EMT.
 * @param   cbInstr             The instruction length (for flushing).
 */
DECLINLINE(void) iemReInitExec(PVMCPUCC pVCpu, uint8_t cbInstr) RT_NOEXCEPT
{
    pVCpu->iem.s.fExec = iemCalcExecFlags(pVCpu) | (pVCpu->iem.s.fExec & IEM_F_USER_OPTS);
    iemOpcodeFlushHeavy(pVCpu, cbInstr);
}
#  endif

# endif /* VBOX_INCLUDED_vmm_dbgf_h || DOXYGEN_RUNNING */

/**
 * Counterpart to #iemInitExec that undoes evil strict-build stuff.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling thread.
 */
DECLINLINE(void) iemUninitExec(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    /* Note! do not touch fInPatchCode here! (see iemUninitExecAndFiddleStatusAndMaybeReenter) */
# ifdef VBOX_STRICT
#  ifdef IEM_WITH_CODE_TLB
    NOREF(pVCpu);
#  else
    pVCpu->iem.s.cbOpcode = 0;
#  endif
# else
    NOREF(pVCpu);
# endif
}


/**
 * Calls iemUninitExec, iemExecStatusCodeFiddling and iemRCRawMaybeReenter.
 *
 * Only calling iemRCRawMaybeReenter in raw-mode, obviously.
 *
 * @returns Fiddled strict vbox status code, ready to return to non-IEM caller.
 * @param   pVCpu       The cross context virtual CPU structure of the calling thread.
 * @param   rcStrict    The status code to fiddle.
 */
DECLINLINE(VBOXSTRICTRC) iemUninitExecAndFiddleStatusAndMaybeReenter(PVMCPUCC pVCpu, VBOXSTRICTRC rcStrict) RT_NOEXCEPT
{
    iemUninitExec(pVCpu);
    return iemExecStatusCodeFiddling(pVCpu, rcStrict);
}


/**
 * Macro used by the IEMExec* method to check the given instruction length.
 *
 * Will return on failure!
 *
 * @param   a_cbInstr   The given instruction length.
 * @param   a_cbMin     The minimum length.
 */
# define IEMEXEC_ASSERT_INSTR_LEN_RETURN(a_cbInstr, a_cbMin) \
    AssertMsgReturn((unsigned)(a_cbInstr) - (unsigned)(a_cbMin) <= (unsigned)15 - (unsigned)(a_cbMin), \
                    ("cbInstr=%u cbMin=%u\n", (a_cbInstr), (a_cbMin)), VERR_IEM_INVALID_INSTR_LENGTH)


# ifndef IEM_WITH_SETJMP

/**
 * Fetches the first opcode byte.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling thread.
 * @param   pu8                 Where to return the opcode byte.
 */
DECLINLINE(VBOXSTRICTRC) iemOpcodeGetFirstU8(PVMCPUCC pVCpu, uint8_t *pu8) RT_NOEXCEPT
{
    /*
     * Check for hardware instruction breakpoints.
     * Note! Guest breakpoints are only checked after POP SS or MOV SS on AMD CPUs.
     */
    if (RT_LIKELY(!(pVCpu->iem.s.fExec & IEM_F_PENDING_BRK_INSTR)))
    { /* likely */ }
    else
    {
        VBOXSTRICTRC rcStrict = DBGFBpCheckInstruction(pVCpu->CTX_SUFF(pVM), pVCpu,
                                                       pVCpu->cpum.GstCtx.rip + pVCpu->cpum.GstCtx.cs.u64Base,
                                                          !(pVCpu->cpum.GstCtx.rflags.uBoth & CPUMCTX_INHIBIT_SHADOW_SS)
                                                       || IEM_IS_GUEST_CPU_AMD(pVCpu));
        if (RT_LIKELY(rcStrict == VINF_SUCCESS))
        { /* likely */ }
        else
        {
            *pu8 = 0xff; /* shut up gcc. sigh */
            if (rcStrict == VINF_EM_RAW_GUEST_TRAP)
                return iemRaiseDebugException(pVCpu);
            return rcStrict;
        }
    }

    /*
     * Fetch the first opcode byte.
     */
    uintptr_t const offOpcode = pVCpu->iem.s.offOpcode;
    if (RT_LIKELY((uint8_t)offOpcode < pVCpu->iem.s.cbOpcode))
    {
        pVCpu->iem.s.offOpcode = (uint8_t)offOpcode + 1;
        *pu8 = pVCpu->iem.s.abOpcode[offOpcode];
        return VINF_SUCCESS;
    }
    return iemOpcodeGetNextU8Slow(pVCpu, pu8);
}

# else  /* IEM_WITH_SETJMP */

/**
 * Fetches the first opcode byte, longjmp on error.
 *
 * @returns The opcode byte.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
DECL_INLINE_THROW(uint8_t) iemOpcodeGetFirstU8Jmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
{
    /*
     * Check for hardware instruction breakpoints.
    * Note! Guest breakpoints are only checked after POP SS or MOV SS on AMD CPUs.
     */
    if (RT_LIKELY(!(pVCpu->iem.s.fExec & IEM_F_PENDING_BRK_INSTR)))
    { /* likely */ }
    else
    {
        VBOXSTRICTRC rcStrict = DBGFBpCheckInstruction(pVCpu->CTX_SUFF(pVM), pVCpu,
                                                       pVCpu->cpum.GstCtx.rip + pVCpu->cpum.GstCtx.cs.u64Base,
                                                          !(pVCpu->cpum.GstCtx.rflags.uBoth & CPUMCTX_INHIBIT_SHADOW_SS)
                                                       || IEM_IS_GUEST_CPU_AMD(pVCpu));
        if (RT_LIKELY(rcStrict == VINF_SUCCESS))
        { /* likely */ }
        else
        {
            if (rcStrict == VINF_EM_RAW_GUEST_TRAP)
                rcStrict = iemRaiseDebugException(pVCpu);
            IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict));
        }
    }

    /*
     * Fetch the first opcode byte.
     */
#  ifdef IEM_WITH_CODE_TLB
    uint8_t         bRet;
    uintptr_t       offBuf = pVCpu->iem.s.offInstrNextByte;
    uint8_t const  *pbBuf  = pVCpu->iem.s.pbInstrBuf;
    if (RT_LIKELY(   pbBuf != NULL
                  && offBuf < pVCpu->iem.s.cbInstrBuf))
    {
        pVCpu->iem.s.offInstrNextByte = (uint32_t)offBuf + 1;
        bRet = pbBuf[offBuf];
    }
    else
        bRet = iemOpcodeGetNextU8SlowJmp(pVCpu);
#   ifdef IEM_WITH_CODE_TLB_AND_OPCODE_BUF
    Assert(pVCpu->iem.s.offOpcode == 0);
    pVCpu->iem.s.abOpcode[pVCpu->iem.s.offOpcode++] = bRet;
#   endif
    return bRet;

#  else /* !IEM_WITH_CODE_TLB */
    uintptr_t offOpcode = pVCpu->iem.s.offOpcode;
    if (RT_LIKELY((uint8_t)offOpcode < pVCpu->iem.s.cbOpcode))
    {
        pVCpu->iem.s.offOpcode = (uint8_t)offOpcode + 1;
        return pVCpu->iem.s.abOpcode[offOpcode];
    }
    return iemOpcodeGetNextU8SlowJmp(pVCpu);
#  endif
}

# endif /* IEM_WITH_SETJMP */

/**
 * Fetches the first opcode byte, returns/throws automatically on failure.
 *
 * @param   a_pu8               Where to return the opcode byte.
 * @remark Implicitly references pVCpu.
 */
# ifndef IEM_WITH_SETJMP
#  define IEM_OPCODE_GET_FIRST_U8(a_pu8) \
    do \
    { \
        VBOXSTRICTRC rcStrict2 = iemOpcodeGetFirstU8(pVCpu, (a_pu8)); \
        if (rcStrict2 == VINF_SUCCESS) \
        { /* likely */ } \
        else \
            return rcStrict2; \
    } while (0)
# else
#  define IEM_OPCODE_GET_FIRST_U8(a_pu8) (*(a_pu8) = iemOpcodeGetFirstU8Jmp(pVCpu))
# endif /* IEM_WITH_SETJMP */


# ifndef IEM_WITH_SETJMP

/**
 * Fetches the next opcode byte.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling thread.
 * @param   pu8                 Where to return the opcode byte.
 */
DECLINLINE(VBOXSTRICTRC) iemOpcodeGetNextU8(PVMCPUCC pVCpu, uint8_t *pu8) RT_NOEXCEPT
{
    uintptr_t const offOpcode = pVCpu->iem.s.offOpcode;
    if (RT_LIKELY((uint8_t)offOpcode < pVCpu->iem.s.cbOpcode))
    {
        pVCpu->iem.s.offOpcode = (uint8_t)offOpcode + 1;
        *pu8 = pVCpu->iem.s.abOpcode[offOpcode];
        return VINF_SUCCESS;
    }
    return iemOpcodeGetNextU8Slow(pVCpu, pu8);
}

# else  /* IEM_WITH_SETJMP */

/**
 * Fetches the next opcode byte, longjmp on error.
 *
 * @returns The opcode byte.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
DECL_INLINE_THROW(uint8_t) iemOpcodeGetNextU8Jmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
{
#  ifdef IEM_WITH_CODE_TLB
    uint8_t         bRet;
    uintptr_t       offBuf = pVCpu->iem.s.offInstrNextByte;
    uint8_t const  *pbBuf  = pVCpu->iem.s.pbInstrBuf;
    if (RT_LIKELY(   pbBuf != NULL
                  && offBuf < pVCpu->iem.s.cbInstrBuf))
    {
        pVCpu->iem.s.offInstrNextByte = (uint32_t)offBuf + 1;
        bRet = pbBuf[offBuf];
    }
    else
        bRet = iemOpcodeGetNextU8SlowJmp(pVCpu);
#   ifdef IEM_WITH_CODE_TLB_AND_OPCODE_BUF
    Assert(pVCpu->iem.s.offOpcode < sizeof(pVCpu->iem.s.abOpcode));
    pVCpu->iem.s.abOpcode[pVCpu->iem.s.offOpcode++] = bRet;
#   endif
    return bRet;

#  else /* !IEM_WITH_CODE_TLB */
    uintptr_t offOpcode = pVCpu->iem.s.offOpcode;
    if (RT_LIKELY((uint8_t)offOpcode < pVCpu->iem.s.cbOpcode))
    {
        pVCpu->iem.s.offOpcode = (uint8_t)offOpcode + 1;
        return pVCpu->iem.s.abOpcode[offOpcode];
    }
    return iemOpcodeGetNextU8SlowJmp(pVCpu);
#  endif
}

# endif /* IEM_WITH_SETJMP */

/**
 * Fetches the next opcode byte, returns automatically on failure.
 *
 * @param   a_pu8               Where to return the opcode byte.
 * @remark Implicitly references pVCpu.
 */
# ifndef IEM_WITH_SETJMP
#  define IEM_OPCODE_GET_NEXT_U8(a_pu8) \
    do \
    { \
        VBOXSTRICTRC rcStrict2 = iemOpcodeGetNextU8(pVCpu, (a_pu8)); \
        if (rcStrict2 == VINF_SUCCESS) \
        { /* likely */ } \
        else \
            return rcStrict2; \
    } while (0)
# else
#  define IEM_OPCODE_GET_NEXT_U8(a_pu8) (*(a_pu8) = iemOpcodeGetNextU8Jmp(pVCpu))
# endif /* IEM_WITH_SETJMP */


# ifndef IEM_WITH_SETJMP
/**
 * Fetches the next signed byte from the opcode stream.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pi8                 Where to return the signed byte.
 */
DECLINLINE(VBOXSTRICTRC) iemOpcodeGetNextS8(PVMCPUCC pVCpu, int8_t *pi8) RT_NOEXCEPT
{
    return iemOpcodeGetNextU8(pVCpu, (uint8_t *)pi8);
}
# endif /* !IEM_WITH_SETJMP */


/**
 * Fetches the next signed byte from the opcode stream, returning automatically
 * on failure.
 *
 * @param   a_pi8               Where to return the signed byte.
 * @remark Implicitly references pVCpu.
 */
# ifndef IEM_WITH_SETJMP
#  define IEM_OPCODE_GET_NEXT_S8(a_pi8) \
    do \
    { \
        VBOXSTRICTRC rcStrict2 = iemOpcodeGetNextS8(pVCpu, (a_pi8)); \
        if (rcStrict2 != VINF_SUCCESS) \
            return rcStrict2; \
    } while (0)
# else /* IEM_WITH_SETJMP */
#  define IEM_OPCODE_GET_NEXT_S8(a_pi8) (*(a_pi8) = (int8_t)iemOpcodeGetNextU8Jmp(pVCpu))

# endif /* IEM_WITH_SETJMP */


# ifndef IEM_WITH_SETJMP
/**
 * Fetches the next signed byte from the opcode stream, extending it to
 * unsigned 16-bit.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pu16                Where to return the unsigned word.
 */
DECLINLINE(VBOXSTRICTRC) iemOpcodeGetNextS8SxU16(PVMCPUCC pVCpu, uint16_t *pu16) RT_NOEXCEPT
{
    uint8_t const offOpcode = pVCpu->iem.s.offOpcode;
    if (RT_UNLIKELY(offOpcode >= pVCpu->iem.s.cbOpcode))
        return iemOpcodeGetNextS8SxU16Slow(pVCpu, pu16);

    *pu16 = (uint16_t)(int16_t)(int8_t)pVCpu->iem.s.abOpcode[offOpcode];
    pVCpu->iem.s.offOpcode = offOpcode + 1;
    return VINF_SUCCESS;
}
# endif /* !IEM_WITH_SETJMP */

/**
 * Fetches the next signed byte from the opcode stream and sign-extending it to
 * a word, returning automatically on failure.
 *
 * @param   a_pu16              Where to return the word.
 * @remark Implicitly references pVCpu.
 */
# ifndef IEM_WITH_SETJMP
#  define IEM_OPCODE_GET_NEXT_S8_SX_U16(a_pu16) \
    do \
    { \
        VBOXSTRICTRC rcStrict2 = iemOpcodeGetNextS8SxU16(pVCpu, (a_pu16)); \
        if (rcStrict2 != VINF_SUCCESS) \
            return rcStrict2; \
    } while (0)
# else
#  define IEM_OPCODE_GET_NEXT_S8_SX_U16(a_pu16) (*(a_pu16) = (uint16_t)(int16_t)(int8_t)iemOpcodeGetNextU8Jmp(pVCpu))
# endif

# ifndef IEM_WITH_SETJMP
/**
 * Fetches the next signed byte from the opcode stream, extending it to
 * unsigned 32-bit.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pu32                Where to return the unsigned dword.
 */
DECLINLINE(VBOXSTRICTRC) iemOpcodeGetNextS8SxU32(PVMCPUCC pVCpu, uint32_t *pu32) RT_NOEXCEPT
{
    uint8_t const offOpcode = pVCpu->iem.s.offOpcode;
    if (RT_UNLIKELY(offOpcode >= pVCpu->iem.s.cbOpcode))
        return iemOpcodeGetNextS8SxU32Slow(pVCpu, pu32);

    *pu32 = (uint32_t)(int32_t)(int8_t)pVCpu->iem.s.abOpcode[offOpcode];
    pVCpu->iem.s.offOpcode = offOpcode + 1;
    return VINF_SUCCESS;
}
# endif /* !IEM_WITH_SETJMP */

/**
 * Fetches the next signed byte from the opcode stream and sign-extending it to
 * a word, returning automatically on failure.
 *
 * @param   a_pu32              Where to return the word.
 * @remark Implicitly references pVCpu.
 */
# ifndef IEM_WITH_SETJMP
#  define IEM_OPCODE_GET_NEXT_S8_SX_U32(a_pu32) \
    do \
    { \
        VBOXSTRICTRC rcStrict2 = iemOpcodeGetNextS8SxU32(pVCpu, (a_pu32)); \
        if (rcStrict2 != VINF_SUCCESS) \
            return rcStrict2; \
    } while (0)
# else
#  define IEM_OPCODE_GET_NEXT_S8_SX_U32(a_pu32) (*(a_pu32) = (uint32_t)(int32_t)(int8_t)iemOpcodeGetNextU8Jmp(pVCpu))
# endif


# ifndef IEM_WITH_SETJMP
/**
 * Fetches the next signed byte from the opcode stream, extending it to
 * unsigned 64-bit.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pu64                Where to return the unsigned qword.
 */
DECLINLINE(VBOXSTRICTRC) iemOpcodeGetNextS8SxU64(PVMCPUCC pVCpu, uint64_t *pu64) RT_NOEXCEPT
{
    uint8_t const offOpcode = pVCpu->iem.s.offOpcode;
    if (RT_UNLIKELY(offOpcode >= pVCpu->iem.s.cbOpcode))
        return iemOpcodeGetNextS8SxU64Slow(pVCpu, pu64);

    *pu64 = (uint64_t)(int64_t)(int8_t)pVCpu->iem.s.abOpcode[offOpcode];
    pVCpu->iem.s.offOpcode = offOpcode + 1;
    return VINF_SUCCESS;
}
# endif /* !IEM_WITH_SETJMP */

/**
 * Fetches the next signed byte from the opcode stream and sign-extending it to
 * a word, returning automatically on failure.
 *
 * @param   a_pu64              Where to return the word.
 * @remark Implicitly references pVCpu.
 */
# ifndef IEM_WITH_SETJMP
#  define IEM_OPCODE_GET_NEXT_S8_SX_U64(a_pu64) \
    do \
    { \
        VBOXSTRICTRC rcStrict2 = iemOpcodeGetNextS8SxU64(pVCpu, (a_pu64)); \
        if (rcStrict2 != VINF_SUCCESS) \
            return rcStrict2; \
    } while (0)
# else
#  define IEM_OPCODE_GET_NEXT_S8_SX_U64(a_pu64) (*(a_pu64) = (uint64_t)(int64_t)(int8_t)iemOpcodeGetNextU8Jmp(pVCpu))
# endif


# ifndef IEM_WITH_SETJMP

/**
 * Fetches the next opcode word.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pu16                Where to return the opcode word.
 */
DECLINLINE(VBOXSTRICTRC) iemOpcodeGetNextU16(PVMCPUCC pVCpu, uint16_t *pu16) RT_NOEXCEPT
{
    uintptr_t const offOpcode = pVCpu->iem.s.offOpcode;
    if (RT_LIKELY((uint8_t)offOpcode + 2 <= pVCpu->iem.s.cbOpcode))
    {
        pVCpu->iem.s.offOpcode = (uint8_t)offOpcode + 2;
#  ifdef IEM_USE_UNALIGNED_DATA_ACCESS
        *pu16 = *(uint16_t const *)&pVCpu->iem.s.abOpcode[offOpcode];
#  else
        *pu16 = RT_MAKE_U16(pVCpu->iem.s.abOpcode[offOpcode], pVCpu->iem.s.abOpcode[offOpcode + 1]);
#  endif
        return VINF_SUCCESS;
    }
    return iemOpcodeGetNextU16Slow(pVCpu, pu16);
}

# else  /* IEM_WITH_SETJMP */

/**
 * Fetches the next opcode word, longjmp on error.
 *
 * @returns The opcode word.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
DECL_INLINE_THROW(uint16_t) iemOpcodeGetNextU16Jmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
{
#  ifdef IEM_WITH_CODE_TLB
    uint16_t        u16Ret;
    uintptr_t       offBuf = pVCpu->iem.s.offInstrNextByte;
    uint8_t const  *pbBuf  = pVCpu->iem.s.pbInstrBuf;
    if (RT_LIKELY(   pbBuf != NULL
                  && offBuf + 2 <= pVCpu->iem.s.cbInstrBuf))
    {
        pVCpu->iem.s.offInstrNextByte = (uint32_t)offBuf + 2;
#   ifdef IEM_USE_UNALIGNED_DATA_ACCESS
        u16Ret = *(uint16_t const *)&pbBuf[offBuf];
#   else
        u16Ret = RT_MAKE_U16(pbBuf[offBuf], pbBuf[offBuf + 1]);
#   endif
    }
    else
        u16Ret = iemOpcodeGetNextU16SlowJmp(pVCpu);

#   ifdef IEM_WITH_CODE_TLB_AND_OPCODE_BUF
    uintptr_t const offOpcode = pVCpu->iem.s.offOpcode;
    Assert(offOpcode + 1 < sizeof(pVCpu->iem.s.abOpcode));
#    ifdef IEM_USE_UNALIGNED_DATA_ACCESS
    *(uint16_t *)&pVCpu->iem.s.abOpcode[offOpcode] = u16Ret;
#    else
    pVCpu->iem.s.abOpcode[offOpcode]     = RT_LO_U8(u16Ret);
    pVCpu->iem.s.abOpcode[offOpcode + 1] = RT_HI_U8(u16Ret);
#    endif
    pVCpu->iem.s.offOpcode = (uint8_t)offOpcode + (uint8_t)2;
#   endif

    return u16Ret;

#  else /* !IEM_WITH_CODE_TLB */
    uintptr_t const offOpcode = pVCpu->iem.s.offOpcode;
    if (RT_LIKELY((uint8_t)offOpcode + 2 <= pVCpu->iem.s.cbOpcode))
    {
        pVCpu->iem.s.offOpcode = (uint8_t)offOpcode + 2;
#   ifdef IEM_USE_UNALIGNED_DATA_ACCESS
        return *(uint16_t const *)&pVCpu->iem.s.abOpcode[offOpcode];
#   else
        return RT_MAKE_U16(pVCpu->iem.s.abOpcode[offOpcode], pVCpu->iem.s.abOpcode[offOpcode + 1]);
#   endif
    }
    return iemOpcodeGetNextU16SlowJmp(pVCpu);
#  endif /* !IEM_WITH_CODE_TLB */
}

# endif /* IEM_WITH_SETJMP */

/**
 * Fetches the next opcode word, returns automatically on failure.
 *
 * @param   a_pu16              Where to return the opcode word.
 * @remark Implicitly references pVCpu.
 */
# ifndef IEM_WITH_SETJMP
#  define IEM_OPCODE_GET_NEXT_U16(a_pu16) \
    do \
    { \
        VBOXSTRICTRC rcStrict2 = iemOpcodeGetNextU16(pVCpu, (a_pu16)); \
        if (rcStrict2 != VINF_SUCCESS) \
            return rcStrict2; \
    } while (0)
# else
#  define IEM_OPCODE_GET_NEXT_U16(a_pu16) (*(a_pu16) = iemOpcodeGetNextU16Jmp(pVCpu))
# endif

# ifndef IEM_WITH_SETJMP
/**
 * Fetches the next opcode word, zero extending it to a double word.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pu32                Where to return the opcode double word.
 */
DECLINLINE(VBOXSTRICTRC) iemOpcodeGetNextU16ZxU32(PVMCPUCC pVCpu, uint32_t *pu32) RT_NOEXCEPT
{
    uint8_t const offOpcode = pVCpu->iem.s.offOpcode;
    if (RT_UNLIKELY(offOpcode + 2 > pVCpu->iem.s.cbOpcode))
        return iemOpcodeGetNextU16ZxU32Slow(pVCpu, pu32);

    *pu32 = RT_MAKE_U16(pVCpu->iem.s.abOpcode[offOpcode], pVCpu->iem.s.abOpcode[offOpcode + 1]);
    pVCpu->iem.s.offOpcode = offOpcode + 2;
    return VINF_SUCCESS;
}
# endif /* !IEM_WITH_SETJMP */

/**
 * Fetches the next opcode word and zero extends it to a double word, returns
 * automatically on failure.
 *
 * @param   a_pu32              Where to return the opcode double word.
 * @remark Implicitly references pVCpu.
 */
# ifndef IEM_WITH_SETJMP
#  define IEM_OPCODE_GET_NEXT_U16_ZX_U32(a_pu32) \
    do \
    { \
        VBOXSTRICTRC rcStrict2 = iemOpcodeGetNextU16ZxU32(pVCpu, (a_pu32)); \
        if (rcStrict2 != VINF_SUCCESS) \
            return rcStrict2; \
    } while (0)
# else
#  define IEM_OPCODE_GET_NEXT_U16_ZX_U32(a_pu32) (*(a_pu32) = iemOpcodeGetNextU16Jmp(pVCpu))
# endif

# ifndef IEM_WITH_SETJMP
/**
 * Fetches the next opcode word, zero extending it to a quad word.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pu64                Where to return the opcode quad word.
 */
DECLINLINE(VBOXSTRICTRC) iemOpcodeGetNextU16ZxU64(PVMCPUCC pVCpu, uint64_t *pu64) RT_NOEXCEPT
{
    uint8_t const offOpcode = pVCpu->iem.s.offOpcode;
    if (RT_UNLIKELY(offOpcode + 2 > pVCpu->iem.s.cbOpcode))
        return iemOpcodeGetNextU16ZxU64Slow(pVCpu, pu64);

    *pu64 = RT_MAKE_U16(pVCpu->iem.s.abOpcode[offOpcode], pVCpu->iem.s.abOpcode[offOpcode + 1]);
    pVCpu->iem.s.offOpcode = offOpcode + 2;
    return VINF_SUCCESS;
}
# endif /* !IEM_WITH_SETJMP */

/**
 * Fetches the next opcode word and zero extends it to a quad word, returns
 * automatically on failure.
 *
 * @param   a_pu64              Where to return the opcode quad word.
 * @remark Implicitly references pVCpu.
 */
# ifndef IEM_WITH_SETJMP
#  define IEM_OPCODE_GET_NEXT_U16_ZX_U64(a_pu64) \
    do \
    { \
        VBOXSTRICTRC rcStrict2 = iemOpcodeGetNextU16ZxU64(pVCpu, (a_pu64)); \
        if (rcStrict2 != VINF_SUCCESS) \
            return rcStrict2; \
    } while (0)
# else
#  define IEM_OPCODE_GET_NEXT_U16_ZX_U64(a_pu64)  (*(a_pu64) = iemOpcodeGetNextU16Jmp(pVCpu))
# endif


# ifndef IEM_WITH_SETJMP
/**
 * Fetches the next signed word from the opcode stream.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pi16                Where to return the signed word.
 */
DECLINLINE(VBOXSTRICTRC) iemOpcodeGetNextS16(PVMCPUCC pVCpu, int16_t *pi16) RT_NOEXCEPT
{
    return iemOpcodeGetNextU16(pVCpu, (uint16_t *)pi16);
}
# endif /* !IEM_WITH_SETJMP */


/**
 * Fetches the next signed word from the opcode stream, returning automatically
 * on failure.
 *
 * @param   a_pi16              Where to return the signed word.
 * @remark Implicitly references pVCpu.
 */
# ifndef IEM_WITH_SETJMP
#  define IEM_OPCODE_GET_NEXT_S16(a_pi16) \
    do \
    { \
        VBOXSTRICTRC rcStrict2 = iemOpcodeGetNextS16(pVCpu, (a_pi16)); \
        if (rcStrict2 != VINF_SUCCESS) \
            return rcStrict2; \
    } while (0)
# else
#  define IEM_OPCODE_GET_NEXT_S16(a_pi16) (*(a_pi16) = (int16_t)iemOpcodeGetNextU16Jmp(pVCpu))
# endif

# ifndef IEM_WITH_SETJMP

/**
 * Fetches the next opcode dword.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pu32                Where to return the opcode double word.
 */
DECLINLINE(VBOXSTRICTRC) iemOpcodeGetNextU32(PVMCPUCC pVCpu, uint32_t *pu32) RT_NOEXCEPT
{
    uintptr_t const offOpcode = pVCpu->iem.s.offOpcode;
    if (RT_LIKELY((uint8_t)offOpcode + 4 <= pVCpu->iem.s.cbOpcode))
    {
        pVCpu->iem.s.offOpcode = (uint8_t)offOpcode + 4;
#  ifdef IEM_USE_UNALIGNED_DATA_ACCESS
        *pu32 = *(uint32_t const *)&pVCpu->iem.s.abOpcode[offOpcode];
#  else
        *pu32 = RT_MAKE_U32_FROM_U8(pVCpu->iem.s.abOpcode[offOpcode],
                                    pVCpu->iem.s.abOpcode[offOpcode + 1],
                                    pVCpu->iem.s.abOpcode[offOpcode + 2],
                                    pVCpu->iem.s.abOpcode[offOpcode + 3]);
#  endif
        return VINF_SUCCESS;
    }
    return iemOpcodeGetNextU32Slow(pVCpu, pu32);
}

# else  /* IEM_WITH_SETJMP */

/**
 * Fetches the next opcode dword, longjmp on error.
 *
 * @returns The opcode dword.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
DECL_INLINE_THROW(uint32_t) iemOpcodeGetNextU32Jmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
{
#  ifdef IEM_WITH_CODE_TLB
    uint32_t u32Ret;
    uintptr_t       offBuf = pVCpu->iem.s.offInstrNextByte;
    uint8_t const  *pbBuf  = pVCpu->iem.s.pbInstrBuf;
    if (RT_LIKELY(   pbBuf != NULL
                  && offBuf + 4 <= pVCpu->iem.s.cbInstrBuf))
    {
        pVCpu->iem.s.offInstrNextByte = (uint32_t)offBuf + 4;
#   ifdef IEM_USE_UNALIGNED_DATA_ACCESS
        u32Ret = *(uint32_t const *)&pbBuf[offBuf];
#   else
        u32Ret = RT_MAKE_U32_FROM_U8(pbBuf[offBuf],
                                     pbBuf[offBuf + 1],
                                     pbBuf[offBuf + 2],
                                     pbBuf[offBuf + 3]);
#   endif
    }
    else
        u32Ret = iemOpcodeGetNextU32SlowJmp(pVCpu);

#   ifdef IEM_WITH_CODE_TLB_AND_OPCODE_BUF
    uintptr_t const offOpcode = pVCpu->iem.s.offOpcode;
    Assert(offOpcode + 3 < sizeof(pVCpu->iem.s.abOpcode));
#    ifdef IEM_USE_UNALIGNED_DATA_ACCESS
    *(uint32_t *)&pVCpu->iem.s.abOpcode[offOpcode] = u32Ret;
#    else
    pVCpu->iem.s.abOpcode[offOpcode]     = RT_BYTE1(u32Ret);
    pVCpu->iem.s.abOpcode[offOpcode + 1] = RT_BYTE2(u32Ret);
    pVCpu->iem.s.abOpcode[offOpcode + 2] = RT_BYTE3(u32Ret);
    pVCpu->iem.s.abOpcode[offOpcode + 3] = RT_BYTE4(u32Ret);
#    endif
    pVCpu->iem.s.offOpcode = (uint8_t)offOpcode + (uint8_t)4;
#   endif /* IEM_WITH_CODE_TLB_AND_OPCODE_BUF */

    return u32Ret;

#  else  /* !IEM_WITH_CODE_TLB */
    uintptr_t const offOpcode = pVCpu->iem.s.offOpcode;
    if (RT_LIKELY((uint8_t)offOpcode + 4 <= pVCpu->iem.s.cbOpcode))
    {
        pVCpu->iem.s.offOpcode = (uint8_t)offOpcode + 4;
#   ifdef IEM_USE_UNALIGNED_DATA_ACCESS
        return *(uint32_t const *)&pVCpu->iem.s.abOpcode[offOpcode];
#   else
        return RT_MAKE_U32_FROM_U8(pVCpu->iem.s.abOpcode[offOpcode],
                                   pVCpu->iem.s.abOpcode[offOpcode + 1],
                                   pVCpu->iem.s.abOpcode[offOpcode + 2],
                                   pVCpu->iem.s.abOpcode[offOpcode + 3]);
#   endif
    }
    return iemOpcodeGetNextU32SlowJmp(pVCpu);
#  endif
}

# endif /* IEM_WITH_SETJMP */

/**
 * Fetches the next opcode dword, returns automatically on failure.
 *
 * @param   a_pu32              Where to return the opcode dword.
 * @remark Implicitly references pVCpu.
 */
# ifndef IEM_WITH_SETJMP
#  define IEM_OPCODE_GET_NEXT_U32(a_pu32) \
    do \
    { \
        VBOXSTRICTRC rcStrict2 = iemOpcodeGetNextU32(pVCpu, (a_pu32)); \
        if (rcStrict2 != VINF_SUCCESS) \
            return rcStrict2; \
    } while (0)
# else
#  define IEM_OPCODE_GET_NEXT_U32(a_pu32) (*(a_pu32) = iemOpcodeGetNextU32Jmp(pVCpu))
# endif

# ifndef IEM_WITH_SETJMP
/**
 * Fetches the next opcode dword, zero extending it to a quad word.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pu64                Where to return the opcode quad word.
 */
DECLINLINE(VBOXSTRICTRC) iemOpcodeGetNextU32ZxU64(PVMCPUCC pVCpu, uint64_t *pu64) RT_NOEXCEPT
{
    uint8_t const offOpcode = pVCpu->iem.s.offOpcode;
    if (RT_UNLIKELY(offOpcode + 4 > pVCpu->iem.s.cbOpcode))
        return iemOpcodeGetNextU32ZxU64Slow(pVCpu, pu64);

    *pu64 = RT_MAKE_U32_FROM_U8(pVCpu->iem.s.abOpcode[offOpcode],
                                pVCpu->iem.s.abOpcode[offOpcode + 1],
                                pVCpu->iem.s.abOpcode[offOpcode + 2],
                                pVCpu->iem.s.abOpcode[offOpcode + 3]);
    pVCpu->iem.s.offOpcode = offOpcode + 4;
    return VINF_SUCCESS;
}
# endif /* !IEM_WITH_SETJMP */

/**
 * Fetches the next opcode dword and zero extends it to a quad word, returns
 * automatically on failure.
 *
 * @param   a_pu64              Where to return the opcode quad word.
 * @remark Implicitly references pVCpu.
 */
# ifndef IEM_WITH_SETJMP
#  define IEM_OPCODE_GET_NEXT_U32_ZX_U64(a_pu64) \
    do \
    { \
        VBOXSTRICTRC rcStrict2 = iemOpcodeGetNextU32ZxU64(pVCpu, (a_pu64)); \
        if (rcStrict2 != VINF_SUCCESS) \
            return rcStrict2; \
    } while (0)
# else
#  define IEM_OPCODE_GET_NEXT_U32_ZX_U64(a_pu64) (*(a_pu64) = iemOpcodeGetNextU32Jmp(pVCpu))
# endif


# ifndef IEM_WITH_SETJMP
/**
 * Fetches the next signed double word from the opcode stream.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pi32                Where to return the signed double word.
 */
DECLINLINE(VBOXSTRICTRC) iemOpcodeGetNextS32(PVMCPUCC pVCpu, int32_t *pi32) RT_NOEXCEPT
{
    return iemOpcodeGetNextU32(pVCpu, (uint32_t *)pi32);
}
# endif

/**
 * Fetches the next signed double word from the opcode stream, returning
 * automatically on failure.
 *
 * @param   a_pi32              Where to return the signed double word.
 * @remark Implicitly references pVCpu.
 */
# ifndef IEM_WITH_SETJMP
#  define IEM_OPCODE_GET_NEXT_S32(a_pi32) \
    do \
    { \
        VBOXSTRICTRC rcStrict2 = iemOpcodeGetNextS32(pVCpu, (a_pi32)); \
        if (rcStrict2 != VINF_SUCCESS) \
            return rcStrict2; \
    } while (0)
# else
#  define IEM_OPCODE_GET_NEXT_S32(a_pi32)    (*(a_pi32) = (int32_t)iemOpcodeGetNextU32Jmp(pVCpu))
# endif

# ifndef IEM_WITH_SETJMP
/**
 * Fetches the next opcode dword, sign extending it into a quad word.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pu64                Where to return the opcode quad word.
 */
DECLINLINE(VBOXSTRICTRC) iemOpcodeGetNextS32SxU64(PVMCPUCC pVCpu, uint64_t *pu64) RT_NOEXCEPT
{
    uint8_t const offOpcode = pVCpu->iem.s.offOpcode;
    if (RT_UNLIKELY(offOpcode + 4 > pVCpu->iem.s.cbOpcode))
        return iemOpcodeGetNextS32SxU64Slow(pVCpu, pu64);

    int32_t i32 = RT_MAKE_U32_FROM_U8(pVCpu->iem.s.abOpcode[offOpcode],
                                      pVCpu->iem.s.abOpcode[offOpcode + 1],
                                      pVCpu->iem.s.abOpcode[offOpcode + 2],
                                      pVCpu->iem.s.abOpcode[offOpcode + 3]);
    *pu64 = (uint64_t)(int64_t)i32;
    pVCpu->iem.s.offOpcode = offOpcode + 4;
    return VINF_SUCCESS;
}
# endif /* !IEM_WITH_SETJMP */

/**
 * Fetches the next opcode double word and sign extends it to a quad word,
 * returns automatically on failure.
 *
 * @param   a_pu64              Where to return the opcode quad word.
 * @remark Implicitly references pVCpu.
 */
# ifndef IEM_WITH_SETJMP
#  define IEM_OPCODE_GET_NEXT_S32_SX_U64(a_pu64) \
    do \
    { \
        VBOXSTRICTRC rcStrict2 = iemOpcodeGetNextS32SxU64(pVCpu, (a_pu64)); \
        if (rcStrict2 != VINF_SUCCESS) \
            return rcStrict2; \
    } while (0)
# else
#  define IEM_OPCODE_GET_NEXT_S32_SX_U64(a_pu64) (*(a_pu64) = (uint64_t)(int64_t)(int32_t)iemOpcodeGetNextU32Jmp(pVCpu))
# endif

# ifndef IEM_WITH_SETJMP

/**
 * Fetches the next opcode qword.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pu64                Where to return the opcode qword.
 */
DECLINLINE(VBOXSTRICTRC) iemOpcodeGetNextU64(PVMCPUCC pVCpu, uint64_t *pu64) RT_NOEXCEPT
{
    uintptr_t const offOpcode = pVCpu->iem.s.offOpcode;
    if (RT_LIKELY((uint8_t)offOpcode + 8 <= pVCpu->iem.s.cbOpcode))
    {
#  ifdef IEM_USE_UNALIGNED_DATA_ACCESS
        *pu64 = *(uint64_t const *)&pVCpu->iem.s.abOpcode[offOpcode];
#  else
        *pu64 = RT_MAKE_U64_FROM_U8(pVCpu->iem.s.abOpcode[offOpcode],
                                    pVCpu->iem.s.abOpcode[offOpcode + 1],
                                    pVCpu->iem.s.abOpcode[offOpcode + 2],
                                    pVCpu->iem.s.abOpcode[offOpcode + 3],
                                    pVCpu->iem.s.abOpcode[offOpcode + 4],
                                    pVCpu->iem.s.abOpcode[offOpcode + 5],
                                    pVCpu->iem.s.abOpcode[offOpcode + 6],
                                    pVCpu->iem.s.abOpcode[offOpcode + 7]);
#  endif
        pVCpu->iem.s.offOpcode = (uint8_t)offOpcode + 8;
        return VINF_SUCCESS;
    }
    return iemOpcodeGetNextU64Slow(pVCpu, pu64);
}

# else  /* IEM_WITH_SETJMP */

/**
 * Fetches the next opcode qword, longjmp on error.
 *
 * @returns The opcode qword.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
DECL_INLINE_THROW(uint64_t) iemOpcodeGetNextU64Jmp(PVMCPUCC pVCpu) IEM_NOEXCEPT_MAY_LONGJMP
{
#  ifdef IEM_WITH_CODE_TLB
    uint64_t        u64Ret;
    uintptr_t       offBuf = pVCpu->iem.s.offInstrNextByte;
    uint8_t const  *pbBuf  = pVCpu->iem.s.pbInstrBuf;
    if (RT_LIKELY(   pbBuf != NULL
                  && offBuf + 8 <= pVCpu->iem.s.cbInstrBuf))
    {
        pVCpu->iem.s.offInstrNextByte = (uint32_t)offBuf + 8;
#   ifdef IEM_USE_UNALIGNED_DATA_ACCESS
        u64Ret = *(uint64_t const *)&pbBuf[offBuf];
#   else
        u64Ret = RT_MAKE_U64_FROM_U8(pbBuf[offBuf],
                                     pbBuf[offBuf + 1],
                                     pbBuf[offBuf + 2],
                                     pbBuf[offBuf + 3],
                                     pbBuf[offBuf + 4],
                                     pbBuf[offBuf + 5],
                                     pbBuf[offBuf + 6],
                                     pbBuf[offBuf + 7]);
#   endif
    }
    else
        u64Ret = iemOpcodeGetNextU64SlowJmp(pVCpu);

#   ifdef IEM_WITH_CODE_TLB_AND_OPCODE_BUF
    uintptr_t const offOpcode = pVCpu->iem.s.offOpcode;
    Assert(offOpcode + 7 < sizeof(pVCpu->iem.s.abOpcode));
#    ifdef IEM_USE_UNALIGNED_DATA_ACCESS
    *(uint64_t *)&pVCpu->iem.s.abOpcode[offOpcode] = u64Ret;
#    else
    pVCpu->iem.s.abOpcode[offOpcode]     = RT_BYTE1(u64Ret);
    pVCpu->iem.s.abOpcode[offOpcode + 1] = RT_BYTE2(u64Ret);
    pVCpu->iem.s.abOpcode[offOpcode + 2] = RT_BYTE3(u64Ret);
    pVCpu->iem.s.abOpcode[offOpcode + 3] = RT_BYTE4(u64Ret);
    pVCpu->iem.s.abOpcode[offOpcode + 4] = RT_BYTE5(u64Ret);
    pVCpu->iem.s.abOpcode[offOpcode + 5] = RT_BYTE6(u64Ret);
    pVCpu->iem.s.abOpcode[offOpcode + 6] = RT_BYTE7(u64Ret);
    pVCpu->iem.s.abOpcode[offOpcode + 7] = RT_BYTE8(u64Ret);
#    endif
    pVCpu->iem.s.offOpcode = (uint8_t)offOpcode + (uint8_t)8;
#   endif /* IEM_WITH_CODE_TLB_AND_OPCODE_BUF */

    return u64Ret;

#  else /* !IEM_WITH_CODE_TLB */
    uintptr_t const offOpcode = pVCpu->iem.s.offOpcode;
    if (RT_LIKELY((uint8_t)offOpcode + 8 <= pVCpu->iem.s.cbOpcode))
    {
        pVCpu->iem.s.offOpcode = (uint8_t)offOpcode + 8;
#   ifdef IEM_USE_UNALIGNED_DATA_ACCESS
        return *(uint64_t const *)&pVCpu->iem.s.abOpcode[offOpcode];
#   else
        return RT_MAKE_U64_FROM_U8(pVCpu->iem.s.abOpcode[offOpcode],
                                   pVCpu->iem.s.abOpcode[offOpcode + 1],
                                   pVCpu->iem.s.abOpcode[offOpcode + 2],
                                   pVCpu->iem.s.abOpcode[offOpcode + 3],
                                   pVCpu->iem.s.abOpcode[offOpcode + 4],
                                   pVCpu->iem.s.abOpcode[offOpcode + 5],
                                   pVCpu->iem.s.abOpcode[offOpcode + 6],
                                   pVCpu->iem.s.abOpcode[offOpcode + 7]);
#   endif
    }
    return iemOpcodeGetNextU64SlowJmp(pVCpu);
#  endif /* !IEM_WITH_CODE_TLB */
}

# endif /* IEM_WITH_SETJMP */

/**
 * Fetches the next opcode quad word, returns automatically on failure.
 *
 * @param   a_pu64              Where to return the opcode quad word.
 * @remark Implicitly references pVCpu.
 */
# ifndef IEM_WITH_SETJMP
#  define IEM_OPCODE_GET_NEXT_U64(a_pu64) \
    do \
    { \
        VBOXSTRICTRC rcStrict2 = iemOpcodeGetNextU64(pVCpu, (a_pu64)); \
        if (rcStrict2 != VINF_SUCCESS) \
            return rcStrict2; \
    } while (0)
# else
#  define IEM_OPCODE_GET_NEXT_U64(a_pu64)    ( *(a_pu64) = iemOpcodeGetNextU64Jmp(pVCpu) )
# endif

/**
 * For fetching the opcode bytes for an ModR/M effective address, but throw
 * away the result.
 *
 * This is used when decoding undefined opcodes and such where we want to avoid
 * unnecessary MC blocks.
 *
 * @note The recompiler code overrides this one so iemOpHlpCalcRmEffAddrJmpEx is
 *       used instead.  At least for now...
 */
# ifndef IEM_WITH_SETJMP
#  define IEM_OPCODE_SKIP_RM_EFF_ADDR_BYTES(a_bRm) do { \
        RTGCPTR      GCPtrEff; \
        VBOXSTRICTRC rcStrict = iemOpHlpCalcRmEffAddr(pVCpu, bRm, 0, &GCPtrEff); \
        if (rcStrict != VINF_SUCCESS) \
            return rcStrict; \
    } while (0)
# else
#  define IEM_OPCODE_SKIP_RM_EFF_ADDR_BYTES(a_bRm) do { \
        (void)iemOpHlpCalcRmEffAddrJmp(pVCpu, bRm, 0); \
    } while (0)
# endif

#endif /* !IEM_WITH_OPAQUE_DECODER_STATE */


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


/*
 *
 * Helpers routines.
 * Helpers routines.
 * Helpers routines.
 *
 */

#ifndef IEM_WITH_OPAQUE_DECODER_STATE

/**
 * Recalculates the effective operand size.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
DECLINLINE(void) iemRecalEffOpSize(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    switch (IEM_GET_CPU_MODE(pVCpu))
    {
        case IEMMODE_16BIT:
            pVCpu->iem.s.enmEffOpSize = pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_OP ? IEMMODE_32BIT : IEMMODE_16BIT;
            break;
        case IEMMODE_32BIT:
            pVCpu->iem.s.enmEffOpSize = pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_OP ? IEMMODE_16BIT : IEMMODE_32BIT;
            break;
        case IEMMODE_64BIT:
            switch (pVCpu->iem.s.fPrefixes & (IEM_OP_PRF_SIZE_REX_W | IEM_OP_PRF_SIZE_OP))
            {
                case 0:
                    pVCpu->iem.s.enmEffOpSize = pVCpu->iem.s.enmDefOpSize;
                    break;
                case IEM_OP_PRF_SIZE_OP:
                    pVCpu->iem.s.enmEffOpSize = IEMMODE_16BIT;
                    break;
                case IEM_OP_PRF_SIZE_REX_W:
                case IEM_OP_PRF_SIZE_REX_W | IEM_OP_PRF_SIZE_OP:
                    pVCpu->iem.s.enmEffOpSize = IEMMODE_64BIT;
                    break;
            }
            break;
        default:
            AssertFailed();
    }
}


/**
 * Sets the default operand size to 64-bit and recalculates the effective
 * operand size.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
DECLINLINE(void) iemRecalEffOpSize64Default(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    Assert(IEM_IS_64BIT_CODE(pVCpu));
    pVCpu->iem.s.enmDefOpSize = IEMMODE_64BIT;
    if ((pVCpu->iem.s.fPrefixes & (IEM_OP_PRF_SIZE_REX_W | IEM_OP_PRF_SIZE_OP)) != IEM_OP_PRF_SIZE_OP)
        pVCpu->iem.s.enmEffOpSize = IEMMODE_64BIT;
    else
        pVCpu->iem.s.enmEffOpSize = IEMMODE_16BIT;
}


/**
 * Sets the default operand size to 64-bit and recalculates the effective
 * operand size, with intel ignoring any operand size prefix (AMD respects it).
 *
 * This is for the relative jumps.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
DECLINLINE(void) iemRecalEffOpSize64DefaultAndIntelIgnoresOpSizePrefix(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    Assert(IEM_IS_64BIT_CODE(pVCpu));
    pVCpu->iem.s.enmDefOpSize = IEMMODE_64BIT;
    if (   (pVCpu->iem.s.fPrefixes & (IEM_OP_PRF_SIZE_REX_W | IEM_OP_PRF_SIZE_OP)) != IEM_OP_PRF_SIZE_OP
        || pVCpu->iem.s.enmCpuVendor == CPUMCPUVENDOR_INTEL)
        pVCpu->iem.s.enmEffOpSize = IEMMODE_64BIT;
    else
        pVCpu->iem.s.enmEffOpSize = IEMMODE_16BIT;
}

#endif /* !IEM_WITH_OPAQUE_DECODER_STATE */



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
    /** @todo x87.CS and FPUIP needs to be kept seperately. */
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


/** @name   Memory access.
 *
 * @{
 */


/**
 * Checks whether alignment checks are enabled or not.
 *
 * @returns true if enabled, false if not.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 */
DECLINLINE(bool) iemMemAreAlignmentChecksEnabled(PVMCPUCC pVCpu) RT_NOEXCEPT
{
#if 0
    AssertCompile(X86_CR0_AM == X86_EFL_AC);
    return IEM_GET_CPL(pVCpu) == 3
        && (((uint32_t)pVCpu->cpum.GstCtx.cr0 & pVCpu->cpum.GstCtx.eflags.u) & X86_CR0_AM);
#else
    return RT_BOOL(pVCpu->iem.s.fExec & IEM_F_X86_AC);
#endif
}

/**
 * Checks if the given segment can be written to, raise the appropriate
 * exception if not.
 *
 * @returns VBox strict status code.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pHid                Pointer to the hidden register.
 * @param   iSegReg             The register number.
 * @param   pu64BaseAddr        Where to return the base address to use for the
 *                              segment. (In 64-bit code it may differ from the
 *                              base in the hidden segment.)
 */
DECLINLINE(VBOXSTRICTRC) iemMemSegCheckWriteAccessEx(PVMCPUCC pVCpu, PCCPUMSELREGHID pHid,
                                                     uint8_t iSegReg, uint64_t *pu64BaseAddr) RT_NOEXCEPT
{
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_SREG_FROM_IDX(iSegReg));

    if (IEM_IS_64BIT_CODE(pVCpu))
        *pu64BaseAddr = iSegReg < X86_SREG_FS ? 0 : pHid->u64Base;
    else
    {
        if (!pHid->Attr.n.u1Present)
        {
            uint16_t    uSel = iemSRegFetchU16(pVCpu, iSegReg);
            AssertRelease(uSel == 0);
            LogEx(LOG_GROUP_IEM,("iemMemSegCheckWriteAccessEx: %#x (index %u) - bad selector -> #GP\n", uSel, iSegReg));
            return iemRaiseGeneralProtectionFault0(pVCpu);
        }

        if (   (   (pHid->Attr.n.u4Type & X86_SEL_TYPE_CODE)
                || !(pHid->Attr.n.u4Type & X86_SEL_TYPE_WRITE) )
            && !IEM_IS_64BIT_CODE(pVCpu) )
            return iemRaiseSelectorInvalidAccess(pVCpu, iSegReg, IEM_ACCESS_DATA_W);
        *pu64BaseAddr = pHid->u64Base;
    }
    return VINF_SUCCESS;
}


/**
 * Checks if the given segment can be read from, raise the appropriate
 * exception if not.
 *
 * @returns VBox strict status code.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pHid                Pointer to the hidden register.
 * @param   iSegReg             The register number.
 * @param   pu64BaseAddr        Where to return the base address to use for the
 *                              segment. (In 64-bit code it may differ from the
 *                              base in the hidden segment.)
 */
DECLINLINE(VBOXSTRICTRC) iemMemSegCheckReadAccessEx(PVMCPUCC pVCpu, PCCPUMSELREGHID pHid,
                                                    uint8_t iSegReg, uint64_t *pu64BaseAddr) RT_NOEXCEPT
{
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_SREG_FROM_IDX(iSegReg));

    if (IEM_IS_64BIT_CODE(pVCpu))
        *pu64BaseAddr = iSegReg < X86_SREG_FS ? 0 : pHid->u64Base;
    else
    {
        if (!pHid->Attr.n.u1Present)
        {
            uint16_t    uSel = iemSRegFetchU16(pVCpu, iSegReg);
            AssertRelease(uSel == 0);
            LogEx(LOG_GROUP_IEM,("iemMemSegCheckReadAccessEx: %#x (index %u) - bad selector -> #GP\n", uSel, iSegReg));
            return iemRaiseGeneralProtectionFault0(pVCpu);
        }

        if ((pHid->Attr.n.u4Type & (X86_SEL_TYPE_CODE | X86_SEL_TYPE_READ)) == X86_SEL_TYPE_CODE)
            return iemRaiseSelectorInvalidAccess(pVCpu, iSegReg, IEM_ACCESS_DATA_R);
        *pu64BaseAddr = pHid->u64Base;
    }
    return VINF_SUCCESS;
}


/**
 * Maps a physical page.
 *
 * @returns VBox status code (see PGMR3PhysTlbGCPhys2Ptr).
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   GCPhysMem           The physical address.
 * @param   fAccess             The intended access.
 * @param   ppvMem              Where to return the mapping address.
 * @param   pLock               The PGM lock.
 */
DECLINLINE(int) iemMemPageMap(PVMCPUCC pVCpu, RTGCPHYS GCPhysMem, uint32_t fAccess,
                              void **ppvMem, PPGMPAGEMAPLOCK pLock) RT_NOEXCEPT
{
#ifdef IEM_LOG_MEMORY_WRITES
    if (fAccess & IEM_ACCESS_TYPE_WRITE)
        return VERR_PGM_PHYS_TLB_CATCH_ALL;
#endif

    /** @todo This API may require some improving later.  A private deal with PGM
     *        regarding locking and unlocking needs to be struct.  A couple of TLBs
     *        living in PGM, but with publicly accessible inlined access methods
     *        could perhaps be an even better solution. */
    int rc = PGMPhysIemGCPhys2Ptr(pVCpu->CTX_SUFF(pVM), pVCpu,
                                  GCPhysMem,
                                  RT_BOOL(fAccess & IEM_ACCESS_TYPE_WRITE),
                                  RT_BOOL(pVCpu->iem.s.fExec & IEM_F_BYPASS_HANDLERS),
                                  ppvMem,
                                  pLock);
    /*Log(("PGMPhysIemGCPhys2Ptr %Rrc pLock=%.*Rhxs\n", rc, sizeof(*pLock), pLock));*/
    AssertMsg(rc == VINF_SUCCESS || RT_FAILURE_NP(rc), ("%Rrc\n", rc));

    return rc;
}


/**
 * Unmap a page previously mapped by iemMemPageMap.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   GCPhysMem           The physical address.
 * @param   fAccess             The intended access.
 * @param   pvMem               What iemMemPageMap returned.
 * @param   pLock               The PGM lock.
 */
DECLINLINE(void) iemMemPageUnmap(PVMCPUCC pVCpu, RTGCPHYS GCPhysMem, uint32_t fAccess,
                                 const void *pvMem, PPGMPAGEMAPLOCK pLock) RT_NOEXCEPT
{
    NOREF(pVCpu);
    NOREF(GCPhysMem);
    NOREF(fAccess);
    NOREF(pvMem);
    PGMPhysReleasePageMappingLock(pVCpu->CTX_SUFF(pVM), pLock);
}

#ifdef IEM_WITH_SETJMP

/** @todo slim this down   */
DECL_INLINE_THROW(RTGCPTR) iemMemApplySegmentToReadJmp(PVMCPUCC pVCpu, uint8_t iSegReg,
                                                       size_t cbMem, RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
    Assert(cbMem >= 1);
    Assert(iSegReg < X86_SREG_COUNT);

    /*
     * 64-bit mode is simpler.
     */
    if (IEM_IS_64BIT_CODE(pVCpu))
    {
        if (iSegReg >= X86_SREG_FS && iSegReg != UINT8_MAX)
        {
            IEM_CTX_IMPORT_JMP(pVCpu, CPUMCTX_EXTRN_SREG_FROM_IDX(iSegReg));
            PCPUMSELREGHID const pSel = iemSRegGetHid(pVCpu, iSegReg);
            GCPtrMem += pSel->u64Base;
        }

        if (RT_LIKELY(X86_IS_CANONICAL(GCPtrMem) && X86_IS_CANONICAL(GCPtrMem + cbMem - 1)))
            return GCPtrMem;
        iemRaiseGeneralProtectionFault0Jmp(pVCpu);
    }
    /*
     * 16-bit and 32-bit segmentation.
     */
    else if (iSegReg != UINT8_MAX)
    {
        /** @todo Does this apply to segments with 4G-1 limit? */
        uint32_t const GCPtrLast32 = (uint32_t)GCPtrMem + (uint32_t)cbMem - 1;
        if (RT_LIKELY(GCPtrLast32 >= (uint32_t)GCPtrMem))
        {
            IEM_CTX_IMPORT_JMP(pVCpu, CPUMCTX_EXTRN_SREG_FROM_IDX(iSegReg));
            PCPUMSELREGHID const pSel = iemSRegGetHid(pVCpu, iSegReg);
            switch (pSel->Attr.u & (  X86DESCATTR_P     | X86DESCATTR_UNUSABLE
                                    | X86_SEL_TYPE_READ | X86_SEL_TYPE_WRITE /* same as read */
                                    | X86_SEL_TYPE_DOWN | X86_SEL_TYPE_CONF  /* same as down */
                                    | X86_SEL_TYPE_CODE))
            {
                case X86DESCATTR_P:                                         /* readonly data, expand up */
                case X86DESCATTR_P | X86_SEL_TYPE_WRITE:                    /* writable data, expand up */
                case X86DESCATTR_P | X86_SEL_TYPE_CODE | X86_SEL_TYPE_READ: /* code, read-only */
                case X86DESCATTR_P | X86_SEL_TYPE_CODE | X86_SEL_TYPE_READ | X86_SEL_TYPE_CONF: /* conforming code, read-only */
                    /* expand up */
                    if (RT_LIKELY(GCPtrLast32 <= pSel->u32Limit))
                        return (uint32_t)GCPtrMem + (uint32_t)pSel->u64Base;
                    Log10(("iemMemApplySegmentToReadJmp: out of bounds %#x..%#x vs %#x\n",
                           (uint32_t)GCPtrMem, GCPtrLast32, pSel->u32Limit));
                    break;

                case X86DESCATTR_P | X86_SEL_TYPE_DOWN:                         /* readonly data, expand down */
                case X86DESCATTR_P | X86_SEL_TYPE_DOWN | X86_SEL_TYPE_WRITE:    /* writable data, expand down */
                    /* expand down */
                    if (RT_LIKELY(   (uint32_t)GCPtrMem > pSel->u32Limit
                                  && (   pSel->Attr.n.u1DefBig
                                      || GCPtrLast32 <= UINT32_C(0xffff)) ))
                        return (uint32_t)GCPtrMem + (uint32_t)pSel->u64Base;
                    Log10(("iemMemApplySegmentToReadJmp: expand down out of bounds %#x..%#x vs %#x..%#x\n",
                           (uint32_t)GCPtrMem, GCPtrLast32, pSel->u32Limit, pSel->Attr.n.u1DefBig ? UINT32_MAX : UINT16_MAX));
                    break;

                default:
                    Log10(("iemMemApplySegmentToReadJmp: bad selector %#x\n", pSel->Attr.u));
                    iemRaiseSelectorInvalidAccessJmp(pVCpu, iSegReg, IEM_ACCESS_DATA_R);
                    break;
            }
        }
        Log10(("iemMemApplySegmentToReadJmp: out of bounds %#x..%#x\n",(uint32_t)GCPtrMem, GCPtrLast32));
        iemRaiseSelectorBoundsJmp(pVCpu, iSegReg, IEM_ACCESS_DATA_R);
    }
    /*
     * 32-bit flat address.
     */
    else
        return GCPtrMem;
}


/** @todo slim this down   */
DECL_INLINE_THROW(RTGCPTR) iemMemApplySegmentToWriteJmp(PVMCPUCC pVCpu, uint8_t iSegReg, size_t cbMem,
                                                        RTGCPTR GCPtrMem) IEM_NOEXCEPT_MAY_LONGJMP
{
    Assert(cbMem >= 1);
    Assert(iSegReg < X86_SREG_COUNT);

    /*
     * 64-bit mode is simpler.
     */
    if (IEM_IS_64BIT_CODE(pVCpu))
    {
        if (iSegReg >= X86_SREG_FS)
        {
            IEM_CTX_IMPORT_JMP(pVCpu, CPUMCTX_EXTRN_SREG_FROM_IDX(iSegReg));
            PCPUMSELREGHID pSel = iemSRegGetHid(pVCpu, iSegReg);
            GCPtrMem += pSel->u64Base;
        }

        if (RT_LIKELY(X86_IS_CANONICAL(GCPtrMem) && X86_IS_CANONICAL(GCPtrMem + cbMem - 1)))
            return GCPtrMem;
    }
    /*
     * 16-bit and 32-bit segmentation.
     */
    else
    {
        Assert(GCPtrMem <= UINT32_MAX);
        IEM_CTX_IMPORT_JMP(pVCpu, CPUMCTX_EXTRN_SREG_FROM_IDX(iSegReg));
        PCPUMSELREGHID pSel           = iemSRegGetHid(pVCpu, iSegReg);
        uint32_t const fRelevantAttrs = pSel->Attr.u & (  X86DESCATTR_P     | X86DESCATTR_UNUSABLE
                                                        | X86_SEL_TYPE_CODE | X86_SEL_TYPE_WRITE | X86_SEL_TYPE_DOWN);
        if (   fRelevantAttrs == (X86DESCATTR_P | X86_SEL_TYPE_WRITE) /* data, expand up */
               /** @todo explore exactly how the CS stuff works in real mode. See also
                *        http://www.rcollins.org/Productivity/DescriptorCache.html and
                *        http://www.rcollins.org/ddj/Aug98/Aug98.html for some insight. */
            || (iSegReg == X86_SREG_CS && IEM_IS_REAL_OR_V86_MODE(pVCpu)) ) /* Ignored for CS. */ /** @todo testcase! */
        {
            /* expand up */
            uint32_t const GCPtrLast32 = (uint32_t)GCPtrMem + (uint32_t)cbMem - 1;
            if (RT_LIKELY(   GCPtrLast32 <= pSel->u32Limit
                          && GCPtrLast32 >= (uint32_t)GCPtrMem))
                return (uint32_t)GCPtrMem + (uint32_t)pSel->u64Base;
            iemRaiseSelectorBoundsJmp(pVCpu, iSegReg, IEM_ACCESS_DATA_W);
        }
        else if (fRelevantAttrs == (X86DESCATTR_P | X86_SEL_TYPE_WRITE | X86_SEL_TYPE_DOWN)) /* data, expand up */
        {
            /* expand down - the uppger boundary is defined by the B bit, not G. */
            uint32_t GCPtrLast32 = (uint32_t)GCPtrMem + (uint32_t)cbMem - 1;
            if (RT_LIKELY(   (uint32_t)GCPtrMem >= pSel->u32Limit
                          && (pSel->Attr.n.u1DefBig || GCPtrLast32 <= UINT32_C(0xffff))
                          && GCPtrLast32 >= (uint32_t)GCPtrMem))
                return (uint32_t)GCPtrMem + (uint32_t)pSel->u64Base;
            iemRaiseSelectorBoundsJmp(pVCpu, iSegReg, IEM_ACCESS_DATA_W);
        }
        else
            iemRaiseSelectorInvalidAccessJmp(pVCpu, iSegReg, IEM_ACCESS_DATA_W);
    }
    iemRaiseGeneralProtectionFault0Jmp(pVCpu);
}

#endif /* IEM_WITH_SETJMP */

/**
 * Fakes a long mode stack selector for SS = 0.
 *
 * @param   pDescSs             Where to return the fake stack descriptor.
 * @param   uDpl                The DPL we want.
 */
DECLINLINE(void) iemMemFakeStackSelDesc(PIEMSELDESC pDescSs, uint32_t uDpl) RT_NOEXCEPT
{
    pDescSs->Long.au64[0] = 0;
    pDescSs->Long.au64[1] = 0;
    pDescSs->Long.Gen.u4Type     = X86_SEL_TYPE_RW_ACC;
    pDescSs->Long.Gen.u1DescType = 1; /* 1 = code / data, 0 = system. */
    pDescSs->Long.Gen.u2Dpl      = uDpl;
    pDescSs->Long.Gen.u1Present  = 1;
    pDescSs->Long.Gen.u1Long     = 1;
}


/*
 * Unmap helpers.
 */

#ifdef IEM_WITH_SETJMP

DECL_INLINE_THROW(void) iemMemCommitAndUnmapRwJmp(PVMCPUCC pVCpu, uint8_t bMapInfo) IEM_NOEXCEPT_MAY_LONGJMP
{
# if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    if (RT_LIKELY(bMapInfo == 0))
        return;
# endif
    iemMemCommitAndUnmapRwSafeJmp(pVCpu, bMapInfo);
}


DECL_INLINE_THROW(void) iemMemCommitAndUnmapAtJmp(PVMCPUCC pVCpu, uint8_t bMapInfo) IEM_NOEXCEPT_MAY_LONGJMP
{
# if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    if (RT_LIKELY(bMapInfo == 0))
        return;
# endif
    iemMemCommitAndUnmapAtSafeJmp(pVCpu, bMapInfo);
}


DECL_INLINE_THROW(void) iemMemCommitAndUnmapWoJmp(PVMCPUCC pVCpu, uint8_t bMapInfo) IEM_NOEXCEPT_MAY_LONGJMP
{
# if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    if (RT_LIKELY(bMapInfo == 0))
        return;
# endif
    iemMemCommitAndUnmapWoSafeJmp(pVCpu, bMapInfo);
}


DECL_INLINE_THROW(void) iemMemCommitAndUnmapRoJmp(PVMCPUCC pVCpu, uint8_t bMapInfo) IEM_NOEXCEPT_MAY_LONGJMP
{
# if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    if (RT_LIKELY(bMapInfo == 0))
        return;
# endif
    iemMemCommitAndUnmapRoSafeJmp(pVCpu, bMapInfo);
}

DECLINLINE(void) iemMemRollbackAndUnmapWo(PVMCPUCC pVCpu, uint8_t bMapInfo) RT_NOEXCEPT
{
# if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    if (RT_LIKELY(bMapInfo == 0))
        return;
# endif
    iemMemRollbackAndUnmapWoSafe(pVCpu, bMapInfo);
}

#endif /* IEM_WITH_SETJMP */


/*
 * Instantiate R/W inline templates.
 */

/** @def TMPL_MEM_CHECK_UNALIGNED_WITHIN_PAGE_OK
 * Used to check if an unaligned access is if within the page and won't
 * trigger an \#AC.
 *
 * This can also be used to deal with misaligned accesses on platforms that are
 * senstive to such if desires.
 */
#if 1
# define TMPL_MEM_CHECK_UNALIGNED_WITHIN_PAGE_OK(a_pVCpu, a_GCPtrEff, a_TmplMemType) \
    (   ((a_GCPtrEff) & GUEST_PAGE_OFFSET_MASK) <= GUEST_PAGE_SIZE - sizeof(a_TmplMemType) \
     && !((a_pVCpu)->iem.s.fExec & IEM_F_X86_AC) )
#else
# define TMPL_MEM_CHECK_UNALIGNED_WITHIN_PAGE_OK(a_pVCpu, a_GCPtrEff, a_TmplMemType) 0
#endif

#define TMPL_MEM_WITH_ATOMIC_MAPPING

#define TMPL_MEM_TYPE       uint8_t
#define TMPL_MEM_TYPE_ALIGN 0
#define TMPL_MEM_TYPE_SIZE  1
#define TMPL_MEM_FN_SUFF    U8
#define TMPL_MEM_FMT_TYPE   "%#04x"
#define TMPL_MEM_FMT_DESC   "byte"
#include "../VMMAll/IEMAllMemRWTmplInline.cpp.h"

#define TMPL_MEM_WITH_STACK

#define TMPL_MEM_TYPE       uint16_t
#define TMPL_MEM_TYPE_ALIGN 1
#define TMPL_MEM_TYPE_SIZE  2
#define TMPL_MEM_FN_SUFF    U16
#define TMPL_MEM_FMT_TYPE   "%#06x"
#define TMPL_MEM_FMT_DESC   "word"
#include "../VMMAll/IEMAllMemRWTmplInline.cpp.h"

#define TMPL_WITH_PUSH_SREG
#define TMPL_MEM_TYPE       uint32_t
#define TMPL_MEM_TYPE_ALIGN 3
#define TMPL_MEM_TYPE_SIZE  4
#define TMPL_MEM_FN_SUFF    U32
#define TMPL_MEM_FMT_TYPE   "%#010x"
#define TMPL_MEM_FMT_DESC   "dword"
#include "../VMMAll/IEMAllMemRWTmplInline.cpp.h"
#undef  TMPL_WITH_PUSH_SREG

#define TMPL_MEM_TYPE       uint64_t
#define TMPL_MEM_TYPE_ALIGN 7
#define TMPL_MEM_TYPE_SIZE  8
#define TMPL_MEM_FN_SUFF    U64
#define TMPL_MEM_FMT_TYPE   "%#018RX64"
#define TMPL_MEM_FMT_DESC   "qword"
#include "../VMMAll/IEMAllMemRWTmplInline.cpp.h"

#undef TMPL_MEM_WITH_STACK
#undef TMPL_MEM_WITH_ATOMIC_MAPPING

#define TMPL_MEM_NO_MAPPING /* currently sticky */

#define TMPL_MEM_NO_STORE
#define TMPL_MEM_TYPE       uint32_t
#define TMPL_MEM_TYPE_ALIGN 0
#define TMPL_MEM_TYPE_SIZE  4
#define TMPL_MEM_FN_SUFF    U32NoAc
#define TMPL_MEM_FMT_TYPE   "%#010x"
#define TMPL_MEM_FMT_DESC   "dword"
#include "../VMMAll/IEMAllMemRWTmplInline.cpp.h"

#define TMPL_MEM_NO_STORE
#define TMPL_MEM_TYPE       uint64_t
#define TMPL_MEM_TYPE_ALIGN 0
#define TMPL_MEM_TYPE_SIZE  8
#define TMPL_MEM_FN_SUFF    U64NoAc
#define TMPL_MEM_FMT_TYPE   "%#018RX64"
#define TMPL_MEM_FMT_DESC   "qword"
#include "../VMMAll/IEMAllMemRWTmplInline.cpp.h"

#define TMPL_MEM_NO_STORE
#define TMPL_MEM_TYPE       uint64_t
#define TMPL_MEM_TYPE_ALIGN 15
#define TMPL_MEM_TYPE_SIZE  8
#define TMPL_MEM_FN_SUFF    U64AlignedU128
#define TMPL_MEM_FMT_TYPE   "%#018RX64"
#define TMPL_MEM_FMT_DESC   "qword"
#include "../VMMAll/IEMAllMemRWTmplInline.cpp.h"

#undef TMPL_MEM_NO_MAPPING

#define TMPL_MEM_TYPE       RTFLOAT80U
#define TMPL_MEM_TYPE_ALIGN 7
#define TMPL_MEM_TYPE_SIZE  10
#define TMPL_MEM_FN_SUFF    R80
#define TMPL_MEM_FMT_TYPE   "%.10Rhxs"
#define TMPL_MEM_FMT_DESC   "tword"
#include "../VMMAll/IEMAllMemRWTmplInline.cpp.h"

#define TMPL_MEM_TYPE       RTPBCD80U
#define TMPL_MEM_TYPE_ALIGN 7           /** @todo RTPBCD80U alignment testcase */
#define TMPL_MEM_TYPE_SIZE  10
#define TMPL_MEM_FN_SUFF    D80
#define TMPL_MEM_FMT_TYPE   "%.10Rhxs"
#define TMPL_MEM_FMT_DESC   "tword"
#include "../VMMAll/IEMAllMemRWTmplInline.cpp.h"

#define TMPL_MEM_WITH_ATOMIC_MAPPING
#define TMPL_MEM_TYPE       RTUINT128U
#define TMPL_MEM_TYPE_ALIGN 15
#define TMPL_MEM_TYPE_SIZE  16
#define TMPL_MEM_FN_SUFF    U128
#define TMPL_MEM_FMT_TYPE   "%.16Rhxs"
#define TMPL_MEM_FMT_DESC   "dqword"
#include "../VMMAll/IEMAllMemRWTmplInline.cpp.h"
#undef  TMPL_MEM_WITH_ATOMIC_MAPPING

#define TMPL_MEM_NO_MAPPING
#define TMPL_MEM_TYPE       RTUINT128U
#define TMPL_MEM_TYPE_ALIGN 0
#define TMPL_MEM_TYPE_SIZE  16
#define TMPL_MEM_FN_SUFF    U128NoAc
#define TMPL_MEM_FMT_TYPE   "%.16Rhxs"
#define TMPL_MEM_FMT_DESC   "dqword"
#include "../VMMAll/IEMAllMemRWTmplInline.cpp.h"
#undef TMPL_MEM_NO_MAPPING


/* Every template relying on unaligned accesses inside a page not being okay should go below. */
#undef TMPL_MEM_CHECK_UNALIGNED_WITHIN_PAGE_OK
#define TMPL_MEM_CHECK_UNALIGNED_WITHIN_PAGE_OK(a_pVCpu, a_GCPtrEff, a_TmplMemType) 0

#define TMPL_MEM_NO_MAPPING
#define TMPL_MEM_TYPE       RTUINT128U
#define TMPL_MEM_TYPE_ALIGN 15
#define TMPL_MEM_TYPE_SIZE  16
#define TMPL_MEM_FN_SUFF    U128AlignedSse
#define TMPL_MEM_FMT_TYPE   "%.16Rhxs"
#define TMPL_MEM_FMT_DESC   "dqword"
#include "../VMMAll/IEMAllMemRWTmplInline.cpp.h"
#undef  TMPL_MEM_NO_MAPPING

#define TMPL_MEM_NO_MAPPING
#define TMPL_MEM_TYPE       RTUINT256U
#define TMPL_MEM_TYPE_ALIGN 0
#define TMPL_MEM_TYPE_SIZE  32
#define TMPL_MEM_FN_SUFF    U256NoAc
#define TMPL_MEM_FMT_TYPE   "%.32Rhxs"
#define TMPL_MEM_FMT_DESC   "qqword"
#include "../VMMAll/IEMAllMemRWTmplInline.cpp.h"
#undef TMPL_MEM_NO_MAPPING

#define TMPL_MEM_NO_MAPPING
#define TMPL_MEM_TYPE       RTUINT256U
#define TMPL_MEM_TYPE_ALIGN 31
#define TMPL_MEM_TYPE_SIZE  32
#define TMPL_MEM_FN_SUFF    U256AlignedAvx
#define TMPL_MEM_FMT_TYPE   "%.32Rhxs"
#define TMPL_MEM_FMT_DESC   "qqword"
#include "../VMMAll/IEMAllMemRWTmplInline.cpp.h"
#undef TMPL_MEM_NO_MAPPING

#undef TMPL_MEM_CHECK_UNALIGNED_WITHIN_PAGE_OK

/** @} */


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

#if defined(IEM_WITH_TLB_TRACE) && defined(IN_RING3)
/**
 * Adds an entry to the TLB trace buffer.
 *
 * @note Don't use directly, only via the IEMTLBTRACE_XXX macros.
 */
DECLINLINE(void) iemTlbTrace(PVMCPU pVCpu, IEMTLBTRACETYPE enmType, uint64_t u64Param, uint64_t u64Param2 = 0,
                             uint8_t bParam = 0, uint32_t u32Param = 0/*, uint16_t u16Param = 0 */)
{
    uint32_t const          fMask  = RT_BIT_32(pVCpu->iem.s.cTlbTraceEntriesShift) - 1;
    PIEMTLBTRACEENTRY const pEntry = &pVCpu->iem.s.paTlbTraceEntries[pVCpu->iem.s.idxTlbTraceEntry++ & fMask];
    pEntry->u64Param  = u64Param;
    pEntry->u64Param2 = u64Param2;
    pEntry->u16Param  = 0; //u16Param;
    pEntry->u32Param  = u32Param;
    pEntry->bParam    = bParam;
    pEntry->enmType   = enmType;
    pEntry->rip       = pVCpu->cpum.GstCtx.rip + pVCpu->cpum.GstCtx.cs.u64Base;
}
#endif

#endif /* !VMM_INCLUDED_SRC_include_IEMInline_h */
