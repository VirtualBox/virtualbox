/* $Id: IEMAllThrdFuncsBltIn.cpp 110494 2025-07-31 10:11:09Z andreas.loeffler@oracle.com $ */
/** @file
 * IEM - Instruction Decoding and Emulation, Built-in Threaded Functions.
 *
 * This is separate from IEMThreadedFunctions.cpp because it doesn't work
 * with IEM_WITH_OPAQUE_DECODER_STATE defined.
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
#define LOG_GROUP LOG_GROUP_IEM_RE_THREADED
#define VMCPU_INCL_CPUM_GST_CTX
#ifdef IN_RING0
# define VBOX_VMM_TARGET_X86
#endif
#include <VBox/vmm/iem.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/pdmapic.h>
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/iom.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/nem.h>
#include <VBox/vmm/gim.h>
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
# include <VBox/vmm/em.h>
# include <VBox/vmm/hm_svm.h>
#endif
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
# include <VBox/vmm/hmvmxinline.h>
#endif
#include <VBox/vmm/tm.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/dbgftrace.h>
#include "IEMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/dis.h>
#include <VBox/disopcode-x86-amd64.h>
#include <iprt/asm-math.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/x86.h>

#include "IEMInline.h"
#ifdef VBOX_VMM_TARGET_X86
# include "target-x86/IEMInline-x86.h"
#endif



static VBOXSTRICTRC iemThreadeFuncWorkerObsoleteTb(PVMCPUCC pVCpu)
{
    /* We set fSafeToFree to false where as we're being called in the context
       of a TB callback function, which for native TBs means we cannot release
       the executable memory till we've returned our way back to iemTbExec as
       that return path codes via the native code generated for the TB. */
    iemThreadedTbObsolete(pVCpu, pVCpu->iem.s.pCurTbR3, false /*fSafeToFree*/);
    return VINF_IEM_REEXEC_BREAK;
}


/**
 * Built-in function that does absolutely nothing - for debugging.
 *
 * This can be used for artifically increasing the number of calls generated, or
 * for triggering flushes associated with threaded calls.
 */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_Nop)
{
    RT_NOREF(pVCpu, uParam0, uParam1, uParam2);
    return VINF_SUCCESS;
}



/**
 * This is also called from iemNativeHlpAsmSafeWrapLogCpuState.
 */
DECLASM(void) iemThreadedFunc_BltIn_LogCpuStateWorker(PVMCPU pVCpu)
{
#ifdef LOG_ENABLED
    PCIEMTB const      pTb     = pVCpu->iem.s.pCurTbR3;
    PCX86FXSTATE const pFpuCtx = &pVCpu->cpum.GstCtx.XState.x87;
    Log2(("**** LG%c fExec=%x pTb=%p cUsed=%u\n"
          " eax=%08x ebx=%08x ecx=%08x edx=%08x esi=%08x edi=%08x\n"
          " eip=%08x esp=%08x ebp=%08x iopl=%d tr=%04x\n"
          " cs=%04x ss=%04x ds=%04x es=%04x fs=%04x gs=%04x efl=%08x\n"
          " fsw=%04x fcw=%04x ftw=%02x mxcsr=%04x/%04x\n"
          , pTb && (pTb->fFlags & IEMTB_F_TYPE_NATIVE) ? 'n' : 't', pVCpu->iem.s.fExec, pTb, pTb ? pTb->cUsed : 0,
          pVCpu->cpum.GstCtx.eax, pVCpu->cpum.GstCtx.ebx, pVCpu->cpum.GstCtx.ecx, pVCpu->cpum.GstCtx.edx, pVCpu->cpum.GstCtx.esi, pVCpu->cpum.GstCtx.edi,
          pVCpu->cpum.GstCtx.eip, pVCpu->cpum.GstCtx.esp, pVCpu->cpum.GstCtx.ebp, pVCpu->cpum.GstCtx.eflags.Bits.u2IOPL, pVCpu->cpum.GstCtx.tr.Sel,
          pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.ss.Sel, pVCpu->cpum.GstCtx.ds.Sel, pVCpu->cpum.GstCtx.es.Sel,
          pVCpu->cpum.GstCtx.fs.Sel, pVCpu->cpum.GstCtx.gs.Sel, pVCpu->cpum.GstCtx.eflags.u,
          pFpuCtx->FSW, pFpuCtx->FCW, pFpuCtx->FTW, pFpuCtx->MXCSR, pFpuCtx->MXCSR_MASK ));
#else
    RT_NOREF(pVCpu);
#endif
}


/**
 * Built-in function that logs the current CPU state - for debugging.
 */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_LogCpuState)
{
    iemThreadedFunc_BltIn_LogCpuStateWorker(pVCpu);
    RT_NOREF(uParam0, uParam1, uParam2);
    return VINF_SUCCESS;
}


/**
 * Built-in function that calls a C-implemention function taking zero arguments.
 */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_DeferToCImpl0)
{
    PFNIEMCIMPL0 const pfnCImpl = (PFNIEMCIMPL0)(uintptr_t)uParam0;
    uint8_t const      cbInstr  = (uint8_t)uParam1;
    RT_NOREF(uParam2);
    return pfnCImpl(pVCpu, cbInstr);
}


/**
 * Worker for iemThreadedFunc_BltIn_CheckIrq and
 * iemThreadedFunc_BltIn_CheckTimersAndIrqs that checks for pending FFs
 * and IRQs, and if it's only the latter whether we can dispatch them now.
 */
DECL_FORCE_INLINE(int) iemThreadedFunc_BltIn_CheckIrqCommon(PVMCPUCC pVCpu)
{
    /* Get and mask the per-CPU FFs.*/
    uint64_t const fCpuRaw = pVCpu->fLocalForcedActions;
    uint64_t       fFlags  = fCpuRaw & (VMCPU_FF_ALL_MASK & ~(  VMCPU_FF_PGM_SYNC_CR3
                                                              | VMCPU_FF_PGM_SYNC_CR3_NON_GLOBAL
                                                              | VMCPU_FF_TLB_FLUSH
                                                              | VMCPU_FF_UNHALT ));

    /* OR in VM-wide FFs and check them together. */
    uint32_t const fVmRaw = pVCpu->CTX_SUFF(pVM)->fGlobalForcedActions;
    fFlags |= fVmRaw;
    if (RT_LIKELY(!fFlags))
        return VINF_SUCCESS;

    /* Since the VMCPU_FF_INTERUPT_XXX flags was once upon a time in fVm and
       we haven't reused the bits yet, we can still reliably check whether
       we're only here for reasons of pending interrupts and whether these
       are suppressed by EFLAGS.IF=0 or interrupt shadowing. */
    Assert(!(fVmRaw & (VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_INTERRUPT_PIC)));
    AssertCompile((VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_INTERRUPT_PIC) == 3);
    if (   fFlags <= (VMCPU_FF_INTERRUPT_APIC | VMCPU_FF_INTERRUPT_PIC)
        && (   !pVCpu->cpum.GstCtx.rflags.Bits.u1IF
            || CPUMIsInInterruptShadow(&pVCpu->cpum.GstCtx)))
        return VINF_SUCCESS;

    Log(("%04x:%08RX32: Pending IRQ and/or FF: fCpu=%#RX64 fVm=%#RX32 IF=%d\n",
         pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.eip, fCpuRaw, fVmRaw, pVCpu->cpum.GstCtx.rflags.Bits.u1IF));
    STAM_REL_COUNTER_INC(&pVCpu->iem.s.StatCheckIrqBreaks);
    return VINF_IEM_REEXEC_BREAK;
}


/**
 * Built-in function that checks for pending interrupts that can be delivered or
 * forced action flags.
 *
 * This triggers after the completion of an instruction, so EIP is already at
 * the next instruction.  If an IRQ or important FF is pending, this will return
 * a non-zero status that stops TB execution.
 */
/** @todo add VMX / SVM variants of this. */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_CheckIrq)
{
    RT_NOREF(uParam0, uParam1, uParam2);
    return iemThreadedFunc_BltIn_CheckIrqCommon(pVCpu);
}


/**
 * Built-in function that works the cTbsTillNextTimerPoll counter on direct TB
 * linking, like loop-jumps.
 */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_CheckTimers)
{
    if (RT_LIKELY(--pVCpu->iem.s.cTbsTillNextTimerPoll > 0))
        return VINF_SUCCESS;

    Log(("%04x:%08RX32: Check timers\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.eip));
    STAM_REL_COUNTER_INC(&pVCpu->iem.s.StatCheckTimersBreaks);
    RT_NOREF(uParam0, uParam1, uParam2);
    return VINF_IEM_REEXEC_BREAK;
}


/**
 * Combined BltIn_CheckTimers + BltIn_CheckIrq for direct linking.
 */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_CheckTimersAndIrq)
{
    if (RT_LIKELY(--pVCpu->iem.s.cTbsTillNextTimerPoll > 0))
        return iemThreadedFunc_BltIn_CheckIrqCommon(pVCpu);

    Log(("%04x:%08RX32: Check timers\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.eip));
    STAM_REL_COUNTER_INC(&pVCpu->iem.s.StatCheckTimersBreaks);
    RT_NOREF(uParam0, uParam1, uParam2);
    return VINF_IEM_REEXEC_BREAK;
}


/**
 * Built-in function that compares the fExec mask against uParam0.
 *
 * This is used both for IEM_CIMPL_F_MODE and IEM_CIMPL_F_VMEXIT after executing
 * an instruction.
 */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_CheckMode)
{
    uint32_t const fExpectedExec = (uint32_t)uParam0;
    if ((pVCpu->iem.s.fExec & IEMTB_F_KEY_MASK) == (fExpectedExec & IEMTB_F_KEY_MASK))
        return VINF_SUCCESS;
    LogFlow(("Mode changed at %04x:%08RX64: %#x -> %#x (xor: %#x, xor-key: %#x)\n",
             pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, fExpectedExec,
             pVCpu->iem.s.fExec, fExpectedExec ^ pVCpu->iem.s.fExec, (fExpectedExec ^ pVCpu->iem.s.fExec) & IEMTB_F_KEY_MASK));
    RT_NOREF(uParam1, uParam2);
    STAM_REL_COUNTER_INC(&pVCpu->iem.s.StatCheckModeBreaks);
    return VINF_IEM_REEXEC_BREAK;
}


/**
 * Built-in function that checks for hardware instruction breakpoints.
 */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_CheckHwInstrBps)
{
    VBOXSTRICTRC rcStrict = DBGFBpCheckInstruction(pVCpu->CTX_SUFF(pVM), pVCpu,
                                                   pVCpu->cpum.GstCtx.rip + pVCpu->cpum.GstCtx.cs.u64Base,
                                                      !(pVCpu->cpum.GstCtx.rflags.uBoth & CPUMCTX_INHIBIT_SHADOW_SS)
                                                   || IEM_IS_GUEST_CPU_AMD(pVCpu));
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
        return VINF_SUCCESS;

    if (rcStrict == VINF_EM_RAW_GUEST_TRAP)
    {
        LogFlow(("Guest HW bp at %04x:%08RX64\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip));
        rcStrict = iemRaiseDebugException(pVCpu);
        Assert(rcStrict != VINF_SUCCESS);
    }
    else
        LogFlow(("VBoxDbg HW bp at %04x:%08RX64: %Rrc\n",
                 pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, VBOXSTRICTRC_VAL(rcStrict) ));
    RT_NOREF(uParam0, uParam1, uParam2);
    return rcStrict;
}


/**
 * Macro that implements the 16/32-bit CS.LIM check, as this is done by a
 * number of functions.
 */
/** @todo consider 32-bit EIP mid-instruction wrap-around... Difficult to
 *        test, since it would require replacing the default firmware. */
#define BODY_CHECK_CS_LIM(a_cbInstr) do { \
        if (RT_LIKELY((uint32_t)(pVCpu->cpum.GstCtx.eip + cbInstr - 1U) <= pVCpu->cpum.GstCtx.cs.u32Limit)) \
        { /* likely */ } \
        else \
        { \
            Log7(("EIP out of bounds at %04x:%08RX32 LB %u - CS.LIM=%#RX32\n", \
                  pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.eip, (a_cbInstr), pVCpu->cpum.GstCtx.cs.u32Limit)); \
            return iemRaiseGeneralProtectionFault0(pVCpu); \
        } \
    } while(0)

/**
 * Macro that considers whether we need CS.LIM checking after a branch or
 * crossing over to a new page.
 */
#define BODY_CONSIDER_CS_LIM_CHECKING(a_pTb, a_cbInstr) do { \
        int64_t const offFromLim = (int64_t)pVCpu->cpum.GstCtx.cs.u32Limit - (int64_t)pVCpu->cpum.GstCtx.eip; \
        if (offFromLim >= GUEST_PAGE_SIZE + 16 - (int32_t)(pVCpu->cpum.GstCtx.cs.u64Base & GUEST_PAGE_OFFSET_MASK)) \
        { /* likely */ } \
        else \
        { \
            Log7(("TB need CS.LIM: %p at %04x:%08RX64 LB %u; #%u offFromLim=%#RX64 CS.LIM=%#RX32 CS.BASE=%#RX64\n", \
                  (a_pTb), pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, (a_cbInstr), offFromLim, \
                  pVCpu->cpum.GstCtx.cs.u32Limit, pVCpu->cpum.GstCtx.cs.u64Base, __LINE__)); \
            RT_NOREF(a_pTb, a_cbInstr); \
            STAM_REL_COUNTER_INC(&pVCpu->iem.s.StatCheckNeedCsLimChecking); \
            return VINF_IEM_REEXEC_BREAK; \
        } \
    } while(0)

/**
 * Macro that implements opcode (re-)checking.
 */
#define BODY_CHECK_OPCODES(a_pTb, a_idxRange, a_offRange, a_cbInstr) do { \
        Assert((a_idxRange) < (a_pTb)->cRanges && (a_pTb)->cRanges <= RT_ELEMENTS((a_pTb)->aRanges)); \
        Assert((a_offRange) < (a_pTb)->aRanges[(a_idxRange)].cbOpcodes); \
        /* We can use pbInstrBuf here as it will be updated when branching (and prior to executing a TB). */ \
        if (RT_LIKELY(memcmp(&pVCpu->iem.s.pbInstrBuf[(a_pTb)->aRanges[(a_idxRange)].offPhysPage + (a_offRange)], \
                             &(a_pTb)->pabOpcodes[    (a_pTb)->aRanges[(a_idxRange)].offOpcodes  + (a_offRange)], \
                                                      (a_pTb)->aRanges[(a_idxRange)].cbOpcodes   - (a_offRange)) == 0)) \
        { /* likely */ } \
        else \
        { \
            Log7(("TB obsolete: %p at %04x:%08RX64 LB %u; range %u, off %#x LB %#x + %#x; #%u\n", (a_pTb), \
                  pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, (a_cbInstr), (a_idxRange), \
                  (a_pTb)->aRanges[(a_idxRange)].offOpcodes, (a_pTb)->aRanges[(a_idxRange)].cbOpcodes, (a_offRange), __LINE__)); \
            RT_NOREF(a_cbInstr); \
            return iemThreadeFuncWorkerObsoleteTb(pVCpu); \
        } \
    } while(0)

/**
 * Macro that implements TLB loading and updating pbInstrBuf updating for an
 * instruction crossing into a new page.
 *
 * This may long jump if we're raising a \#PF, \#GP or similar trouble.
 */
#define BODY_LOAD_TLB_FOR_NEW_PAGE(a_pTb, a_offInstr, a_idxRange, a_cbInstr) do { \
        pVCpu->iem.s.pbInstrBuf       = NULL; \
        pVCpu->iem.s.offCurInstrStart = GUEST_PAGE_SIZE - (a_offInstr); \
        pVCpu->iem.s.offInstrNextByte = GUEST_PAGE_SIZE; \
        iemOpcodeFetchBytesJmp(pVCpu, 0, NULL); \
        \
        RTGCPHYS const GCPhysNewPage = iemTbGetRangePhysPageAddr(a_pTb, a_idxRange); \
        if (RT_LIKELY(   pVCpu->iem.s.GCPhysInstrBuf == GCPhysNewPage \
                      && pVCpu->iem.s.pbInstrBuf)) \
        { /* likely */ } \
        else \
        { \
            Log7(("TB obsolete: %p at %04x:%08RX64 LB %u; crossing at %#x; GCPhys=%RGp expected %RGp, pbInstrBuf=%p - #%u\n", \
                  (a_pTb), pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, (a_cbInstr), (a_offInstr), \
                  pVCpu->iem.s.GCPhysInstrBuf, GCPhysNewPage, pVCpu->iem.s.pbInstrBuf, __LINE__)); \
            RT_NOREF(a_cbInstr); \
            return iemThreadeFuncWorkerObsoleteTb(pVCpu); \
        } \
    } while(0)

/**
 * Macro that implements TLB loading and updating pbInstrBuf updating when
 * branching or when crossing a page on an instruction boundrary.
 *
 * This differs from BODY_LOAD_TLB_FOR_NEW_PAGE in that it will first check if
 * it is an inter-page branch and also check the page offset.
 *
 * This may long jump if we're raising a \#PF, \#GP or similar trouble.
 */
#define BODY_LOAD_TLB_AFTER_BRANCH(a_pTb, a_idxRange, a_cbInstr) do { \
        /* Is RIP within the current code page? */ \
        Assert(pVCpu->cpum.GstCtx.cs.u64Base == 0 || !IEM_IS_64BIT_CODE(pVCpu)); \
        uint64_t const uPc = pVCpu->cpum.GstCtx.rip + pVCpu->cpum.GstCtx.cs.u64Base; \
        uint64_t const off = uPc - pVCpu->iem.s.uInstrBufPc; \
        if (off < pVCpu->iem.s.cbInstrBufTotal) \
        { \
            Assert(!(pVCpu->iem.s.GCPhysInstrBuf & GUEST_PAGE_OFFSET_MASK)); \
            Assert(pVCpu->iem.s.pbInstrBuf); \
            RTGCPHYS const GCPhysRangePageWithOffset = iemTbGetRangePhysPageAddr(a_pTb, a_idxRange) \
                                                     | pTb->aRanges[(a_idxRange)].offPhysPage; \
            if (GCPhysRangePageWithOffset == pVCpu->iem.s.GCPhysInstrBuf + off) \
            { /* we're good */ } \
            /** @todo r=bird: Not sure if we need the TB obsolete complication here. \
             * If we're preceded by an indirect jump, there is no reason why the TB \
             * would be 'obsolete' just because this time around the indirect jump ends \
             * up at the same offset in a different page.  This would be real bad for \
             * indirect trampolines/validators. */ \
            else if (pTb->aRanges[(a_idxRange)].offPhysPage != off) \
            { \
                Log7(("TB jmp miss: %p at %04x:%08RX64 LB %u; branching/1; GCPhysWithOffset=%RGp expected %RGp, pbInstrBuf=%p - #%u\n", \
                      (a_pTb), pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, (a_cbInstr), \
                      pVCpu->iem.s.GCPhysInstrBuf + off, GCPhysRangePageWithOffset, pVCpu->iem.s.pbInstrBuf, __LINE__)); \
                RT_NOREF(a_cbInstr); \
                STAM_REL_COUNTER_INC(&pVCpu->iem.s.StatCheckBranchMisses); \
                return VINF_IEM_REEXEC_BREAK; \
            } \
            else \
            { \
                Log7(("TB obsolete: %p at %04x:%08RX64 LB %u; branching/1; GCPhysWithOffset=%RGp expected %RGp, pbInstrBuf=%p - #%u\n", \
                      (a_pTb), pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, (a_cbInstr), \
                      pVCpu->iem.s.GCPhysInstrBuf + off, GCPhysRangePageWithOffset, pVCpu->iem.s.pbInstrBuf, __LINE__)); \
                RT_NOREF(a_cbInstr); \
                return iemThreadeFuncWorkerObsoleteTb(pVCpu); \
            } \
        } \
        else \
        { \
            /* Must translate new RIP. */ \
            pVCpu->iem.s.pbInstrBuf       = NULL; \
            pVCpu->iem.s.offCurInstrStart = 0; \
            pVCpu->iem.s.offInstrNextByte = 0; \
            iemOpcodeFetchBytesJmp(pVCpu, 0, NULL); \
            Assert(!(pVCpu->iem.s.GCPhysInstrBuf & GUEST_PAGE_OFFSET_MASK) || !pVCpu->iem.s.pbInstrBuf); \
            \
            RTGCPHYS const GCPhysRangePageWithOffset = iemTbGetRangePhysPageAddr(a_pTb, a_idxRange) \
                                                     | pTb->aRanges[(a_idxRange)].offPhysPage; \
            uint64_t const offNew                    = uPc - pVCpu->iem.s.uInstrBufPc; \
            if (   GCPhysRangePageWithOffset == pVCpu->iem.s.GCPhysInstrBuf + offNew \
                && pVCpu->iem.s.pbInstrBuf) \
            { /* likely */ } \
            else if (   pTb->aRanges[(a_idxRange)].offPhysPage != offNew \
                     && pVCpu->iem.s.pbInstrBuf) \
            { \
                Log7(("TB jmp miss: %p at %04x:%08RX64 LB %u; branching/2; GCPhysWithOffset=%RGp expected %RGp, pbInstrBuf=%p - #%u\n", \
                      (a_pTb), pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, (a_cbInstr), \
                      pVCpu->iem.s.GCPhysInstrBuf + offNew, GCPhysRangePageWithOffset, pVCpu->iem.s.pbInstrBuf, __LINE__)); \
                RT_NOREF(a_cbInstr); \
                STAM_REL_COUNTER_INC(&pVCpu->iem.s.StatCheckBranchMisses); \
                return VINF_IEM_REEXEC_BREAK; \
            } \
            else \
            { \
                Log7(("TB obsolete: %p at %04x:%08RX64 LB %u; branching/2; GCPhysWithOffset=%RGp expected %RGp, pbInstrBuf=%p - #%u\n", \
                      (a_pTb), pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, (a_cbInstr), \
                      pVCpu->iem.s.GCPhysInstrBuf + offNew, GCPhysRangePageWithOffset, pVCpu->iem.s.pbInstrBuf, __LINE__)); \
                RT_NOREF(a_cbInstr); \
                return iemThreadeFuncWorkerObsoleteTb(pVCpu); \
            } \
        } \
    } while(0)

/**
 * Macro that implements PC check after a conditional branch.
 */
#define BODY_CHECK_PC_AFTER_BRANCH(a_pTb, a_idxRange, a_offRange, a_cbInstr) do { \
        /* Is RIP within the current code page? */ \
        Assert(pVCpu->cpum.GstCtx.cs.u64Base == 0 || !IEM_IS_64BIT_CODE(pVCpu)); \
        uint64_t const uPc = pVCpu->cpum.GstCtx.rip + pVCpu->cpum.GstCtx.cs.u64Base; \
        uint64_t const off = uPc - pVCpu->iem.s.uInstrBufPc; \
        Assert(!(pVCpu->iem.s.GCPhysInstrBuf & GUEST_PAGE_OFFSET_MASK)); \
        RTGCPHYS const GCPhysRangePageWithOffset = (  iemTbGetRangePhysPageAddr(a_pTb, a_idxRange) \
                                                    | (a_pTb)->aRanges[(a_idxRange)].offPhysPage) \
                                                 + (a_offRange); \
        if (   GCPhysRangePageWithOffset == pVCpu->iem.s.GCPhysInstrBuf + off \
            && off < /*pVCpu->iem.s.cbInstrBufTotal - ignore flushes and CS.LIM is check elsewhere*/ X86_PAGE_SIZE) \
        { /* we're good */ } \
        else \
        { \
            Log7(("TB jmp miss: %p at %04x:%08RX64 LB %u; GCPhysWithOffset=%RGp hoped for %RGp, pbInstrBuf=%p - #%u\n", \
                  (a_pTb), pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, (a_cbInstr), \
                  pVCpu->iem.s.GCPhysInstrBuf + off, GCPhysRangePageWithOffset, pVCpu->iem.s.pbInstrBuf, __LINE__)); \
            RT_NOREF(a_cbInstr); \
            STAM_REL_COUNTER_INC(&pVCpu->iem.s.StatCheckBranchMisses); \
            return VINF_IEM_REEXEC_BREAK; \
        } \
    } while(0)



/**
 * Built-in function that checks the EIP/IP + uParam0 is within CS.LIM,
 * raising a \#GP(0) if this isn't the case.
 */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_CheckCsLim)
{
    uint32_t const cbInstr = (uint8_t)uParam0;
    RT_NOREF(uParam1, uParam2);
    BODY_CHECK_CS_LIM(cbInstr);
    return VINF_SUCCESS;
}


/**
 * Built-in function for re-checking opcodes and CS.LIM after an instruction
 * that may have modified them.
 */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_CheckCsLimAndOpcodes)
{
    PCIEMTB const  pTb      = pVCpu->iem.s.pCurTbR3;
    uint32_t const cbInstr  = (uint8_t)uParam0;
    uint32_t const idxRange = (uint32_t)uParam1;
    uint32_t const offRange = (uint32_t)uParam2;
    BODY_CHECK_CS_LIM(cbInstr);
    BODY_CHECK_OPCODES(pTb, idxRange, offRange, cbInstr);
    return VINF_SUCCESS;
}


/**
 * Built-in function for re-checking opcodes after an instruction that may have
 * modified them.
 */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_CheckOpcodes)
{
    PCIEMTB const  pTb      = pVCpu->iem.s.pCurTbR3;
    uint32_t const cbInstr  = (uint8_t)uParam0;
    uint32_t const idxRange = (uint32_t)uParam1;
    uint32_t const offRange = (uint32_t)uParam2;
    BODY_CHECK_OPCODES(pTb, idxRange, offRange, cbInstr);
    return VINF_SUCCESS;
}


/**
 * Built-in function for re-checking opcodes and considering the need for CS.LIM
 * checking after an instruction that may have modified them.
 */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_CheckOpcodesConsiderCsLim)
{
    PCIEMTB const  pTb      = pVCpu->iem.s.pCurTbR3;
    uint32_t const cbInstr  = (uint8_t)uParam0;
    uint32_t const idxRange = (uint32_t)uParam1;
    uint32_t const offRange = (uint32_t)uParam2;
    BODY_CONSIDER_CS_LIM_CHECKING(pTb, cbInstr);
    BODY_CHECK_OPCODES(pTb, idxRange, offRange, cbInstr);
    return VINF_SUCCESS;
}


/*
 * Post-branching checkers.
 */

/**
 * Built-in function for checking CS.LIM, checking the PC and checking opcodes
 * after conditional branching within the same page.
 *
 * @see iemThreadedFunc_BltIn_CheckPcAndOpcodes
 */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_CheckCsLimAndPcAndOpcodes)
{
    PCIEMTB const  pTb      = pVCpu->iem.s.pCurTbR3;
    uint32_t const cbInstr  = (uint8_t)uParam0;
    uint32_t const idxRange = (uint32_t)uParam1;
    uint32_t const offRange = (uint32_t)uParam2;
    //LogFunc(("idxRange=%u @ %#x LB %#x: offPhysPage=%#x LB %#x\n", idxRange, offRange, cbInstr, pTb->aRanges[idxRange].offPhysPage, pTb->aRanges[idxRange].cbOpcodes));
    BODY_CHECK_CS_LIM(cbInstr);
    BODY_CHECK_PC_AFTER_BRANCH(pTb, idxRange, offRange, cbInstr);
    BODY_CHECK_OPCODES(pTb, idxRange, offRange, cbInstr);
    //LogFunc(("okay\n"));
    return VINF_SUCCESS;
}


/**
 * Built-in function for checking the PC and checking opcodes after conditional
 * branching within the same page.
 *
 * @see iemThreadedFunc_BltIn_CheckCsLimAndPcAndOpcodes
 */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_CheckPcAndOpcodes)
{
    PCIEMTB const  pTb      = pVCpu->iem.s.pCurTbR3;
    uint32_t const cbInstr  = (uint8_t)uParam0;
    uint32_t const idxRange = (uint32_t)uParam1;
    uint32_t const offRange = (uint32_t)uParam2;
    //LogFunc(("idxRange=%u @ %#x LB %#x: offPhysPage=%#x LB %#x\n", idxRange, offRange, cbInstr, pTb->aRanges[idxRange].offPhysPage, pTb->aRanges[idxRange].cbOpcodes));
    BODY_CHECK_PC_AFTER_BRANCH(pTb, idxRange, offRange, cbInstr);
    BODY_CHECK_OPCODES(pTb, idxRange, offRange, cbInstr);
    //LogFunc(("okay\n"));
    return VINF_SUCCESS;
}


/**
 * Built-in function for checking the PC and checking opcodes and considering
 * the need for CS.LIM checking after conditional branching within the same
 * page.
 *
 * @see iemThreadedFunc_BltIn_CheckCsLimAndPcAndOpcodes
 */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_CheckPcAndOpcodesConsiderCsLim)
{
    PCIEMTB const  pTb      = pVCpu->iem.s.pCurTbR3;
    uint32_t const cbInstr  = (uint8_t)uParam0;
    uint32_t const idxRange = (uint32_t)uParam1;
    uint32_t const offRange = (uint32_t)uParam2;
    //LogFunc(("idxRange=%u @ %#x LB %#x: offPhysPage=%#x LB %#x\n", idxRange, offRange, cbInstr, pTb->aRanges[idxRange].offPhysPage, pTb->aRanges[idxRange].cbOpcodes));
    BODY_CONSIDER_CS_LIM_CHECKING(pTb, cbInstr);
    BODY_CHECK_PC_AFTER_BRANCH(pTb, idxRange, offRange, cbInstr);
    BODY_CHECK_OPCODES(pTb, idxRange, offRange, cbInstr);
    //LogFunc(("okay\n"));
    return VINF_SUCCESS;
}


/**
 * Built-in function for checking CS.LIM, loading TLB and checking opcodes when
 * transitioning to a different code page.
 *
 * The code page transition can either be natural over onto the next page (with
 * the instruction starting at page offset zero) or by means of branching.
 *
 * @see iemThreadedFunc_BltIn_CheckOpcodesLoadingTlb
 */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_CheckCsLimAndOpcodesLoadingTlb)
{
    PCIEMTB const  pTb      = pVCpu->iem.s.pCurTbR3;
    uint32_t const cbInstr  = (uint8_t)uParam0;
    uint32_t const idxRange = (uint32_t)uParam1;
    uint32_t const offRange = (uint32_t)uParam2;
    //LogFunc(("idxRange=%u @ %#x LB %#x: offPhysPage=%#x LB %#x\n", idxRange, offRange, cbInstr, pTb->aRanges[idxRange].offPhysPage, pTb->aRanges[idxRange].cbOpcodes));
    BODY_CHECK_CS_LIM(cbInstr);
    BODY_LOAD_TLB_AFTER_BRANCH(pTb, idxRange, cbInstr);
    BODY_CHECK_OPCODES(pTb, idxRange, offRange, cbInstr);
    //LogFunc(("okay\n"));
    return VINF_SUCCESS;
}


/**
 * Built-in function for loading TLB and checking opcodes when transitioning to
 * a different code page.
 *
 * The code page transition can either be natural over onto the next page (with
 * the instruction starting at page offset zero) or by means of branching.
 *
 * @see iemThreadedFunc_BltIn_CheckCsLimAndOpcodesLoadingTlb
 */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_CheckOpcodesLoadingTlb)
{
    PCIEMTB const  pTb      = pVCpu->iem.s.pCurTbR3;
    uint32_t const cbInstr  = (uint8_t)uParam0;
    uint32_t const idxRange = (uint32_t)uParam1;
    uint32_t const offRange = (uint32_t)uParam2;
    //LogFunc(("idxRange=%u @ %#x LB %#x: offPhysPage=%#x LB %#x\n", idxRange, offRange, cbInstr, pTb->aRanges[idxRange].offPhysPage, pTb->aRanges[idxRange].cbOpcodes));
    BODY_LOAD_TLB_AFTER_BRANCH(pTb, idxRange, cbInstr);
    BODY_CHECK_OPCODES(pTb, idxRange, offRange, cbInstr);
    //LogFunc(("okay\n"));
    return VINF_SUCCESS;
}


/**
 * Built-in function for loading TLB and checking opcodes and considering the
 * need for CS.LIM checking when transitioning to a different code page.
 *
 * The code page transition can either be natural over onto the next page (with
 * the instruction starting at page offset zero) or by means of branching.
 *
 * @see iemThreadedFunc_BltIn_CheckCsLimAndOpcodesLoadingTlb
 */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_CheckOpcodesLoadingTlbConsiderCsLim)
{
    PCIEMTB const  pTb      = pVCpu->iem.s.pCurTbR3;
    uint32_t const cbInstr  = (uint8_t)uParam0;
    uint32_t const idxRange = (uint32_t)uParam1;
    uint32_t const offRange = (uint32_t)uParam2;
    //LogFunc(("idxRange=%u @ %#x LB %#x: offPhysPage=%#x LB %#x\n", idxRange, offRange, cbInstr, pTb->aRanges[idxRange].offPhysPage, pTb->aRanges[idxRange].cbOpcodes));
    BODY_CONSIDER_CS_LIM_CHECKING(pTb, cbInstr);
    BODY_LOAD_TLB_AFTER_BRANCH(pTb, idxRange, cbInstr);
    BODY_CHECK_OPCODES(pTb, idxRange, offRange, cbInstr);
    //LogFunc(("okay\n"));
    return VINF_SUCCESS;
}



/*
 * Natural page crossing checkers.
 */

/**
 * Built-in function for checking CS.LIM, loading TLB and checking opcodes on
 * both pages when transitioning to a different code page.
 *
 * This is used when the previous instruction requires revalidation of opcodes
 * bytes and the current instruction stries a page boundrary with opcode bytes
 * in both the old and new page.
 *
 * @see iemThreadedFunc_BltIn_CheckOpcodesAcrossPageLoadingTlb
 */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_CheckCsLimAndOpcodesAcrossPageLoadingTlb)
{
    PCIEMTB const  pTb         = pVCpu->iem.s.pCurTbR3;
    uint32_t const cbInstr     = (uint8_t)uParam0;
    uint32_t const cbStartPage = (uint32_t)(uParam0 >> 32);
    uint32_t const idxRange1   = (uint32_t)uParam1;
    uint32_t const offRange1   = (uint32_t)uParam2;
    uint32_t const idxRange2   = idxRange1 + 1;
    BODY_CHECK_CS_LIM(cbInstr);
    BODY_CHECK_OPCODES(pTb, idxRange1, offRange1, cbInstr);
    BODY_LOAD_TLB_FOR_NEW_PAGE(pTb, cbStartPage, idxRange2, cbInstr);
    BODY_CHECK_OPCODES(pTb, idxRange2, 0, cbInstr);
    return VINF_SUCCESS;
}


/**
 * Built-in function for loading TLB and checking opcodes on both pages when
 * transitioning to a different code page.
 *
 * This is used when the previous instruction requires revalidation of opcodes
 * bytes and the current instruction stries a page boundrary with opcode bytes
 * in both the old and new page.
 *
 * @see iemThreadedFunc_BltIn_CheckCsLimAndOpcodesAcrossPageLoadingTlb
 */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_CheckOpcodesAcrossPageLoadingTlb)
{
    PCIEMTB const  pTb         = pVCpu->iem.s.pCurTbR3;
    uint32_t const cbInstr     = (uint8_t)uParam0;
    uint32_t const cbStartPage = (uint32_t)(uParam0 >> 32);
    uint32_t const idxRange1   = (uint32_t)uParam1;
    uint32_t const offRange1   = (uint32_t)uParam2;
    uint32_t const idxRange2   = idxRange1 + 1;
    BODY_CHECK_OPCODES(pTb, idxRange1, offRange1, cbInstr);
    BODY_LOAD_TLB_FOR_NEW_PAGE(pTb, cbStartPage, idxRange2, cbInstr);
    BODY_CHECK_OPCODES(pTb, idxRange2, 0, cbInstr);
    return VINF_SUCCESS;
}


/**
 * Built-in function for loading TLB and checking opcodes on both pages and
 * considering the need for CS.LIM checking when transitioning to a different
 * code page.
 *
 * This is used when the previous instruction requires revalidation of opcodes
 * bytes and the current instruction stries a page boundrary with opcode bytes
 * in both the old and new page.
 *
 * @see iemThreadedFunc_BltIn_CheckCsLimAndOpcodesAcrossPageLoadingTlb
 */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_CheckOpcodesAcrossPageLoadingTlbConsiderCsLim)
{
    PCIEMTB const  pTb         = pVCpu->iem.s.pCurTbR3;
    uint32_t const cbInstr     = (uint8_t)uParam0;
    uint32_t const cbStartPage = (uint32_t)(uParam0 >> 32);
    uint32_t const idxRange1   = (uint32_t)uParam1;
    uint32_t const offRange1   = (uint32_t)uParam2;
    uint32_t const idxRange2   = idxRange1 + 1;
    BODY_CONSIDER_CS_LIM_CHECKING(pTb, cbInstr);
    BODY_CHECK_OPCODES(pTb, idxRange1, offRange1, cbInstr);
    BODY_LOAD_TLB_FOR_NEW_PAGE(pTb, cbStartPage, idxRange2, cbInstr);
    BODY_CHECK_OPCODES(pTb, idxRange2, 0, cbInstr);
    return VINF_SUCCESS;
}


/**
 * Built-in function for checking CS.LIM, loading TLB and checking opcodes when
 * advancing naturally to a different code page.
 *
 * Only opcodes on the new page is checked.
 *
 * @see iemThreadedFunc_BltIn_CheckOpcodesOnNextPageLoadingTlb
 */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_CheckCsLimAndOpcodesOnNextPageLoadingTlb)
{
    PCIEMTB const  pTb         = pVCpu->iem.s.pCurTbR3;
    uint32_t const cbInstr     = (uint8_t)uParam0;
    uint32_t const cbStartPage = (uint32_t)(uParam0 >> 32);
    uint32_t const idxRange1   = (uint32_t)uParam1;
    //uint32_t const offRange1   = (uint32_t)uParam2;
    uint32_t const idxRange2   = idxRange1 + 1;
    BODY_CHECK_CS_LIM(cbInstr);
    BODY_LOAD_TLB_FOR_NEW_PAGE(pTb, cbStartPage, idxRange2, cbInstr);
    BODY_CHECK_OPCODES(pTb, idxRange2, 0, cbInstr);
    RT_NOREF(uParam2);
    return VINF_SUCCESS;
}


/**
 * Built-in function for loading TLB and checking opcodes when advancing
 * naturally to a different code page.
 *
 * Only opcodes on the new page is checked.
 *
 * @see iemThreadedFunc_BltIn_CheckCsLimAndOpcodesOnNextPageLoadingTlb
 */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_CheckOpcodesOnNextPageLoadingTlb)
{
    PCIEMTB const  pTb         = pVCpu->iem.s.pCurTbR3;
    uint32_t const cbInstr     = (uint8_t)uParam0;
    uint32_t const cbStartPage = (uint32_t)(uParam0 >> 32);
    uint32_t const idxRange1   = (uint32_t)uParam1;
    //uint32_t const offRange1   = (uint32_t)uParam2;
    uint32_t const idxRange2   = idxRange1 + 1;
    BODY_LOAD_TLB_FOR_NEW_PAGE(pTb, cbStartPage, idxRange2, cbInstr);
    BODY_CHECK_OPCODES(pTb, idxRange2, 0, cbInstr);
    RT_NOREF(uParam2);
    return VINF_SUCCESS;
}


/**
 * Built-in function for loading TLB and checking opcodes and considering the
 * need for CS.LIM checking when advancing naturally to a different code page.
 *
 * Only opcodes on the new page is checked.
 *
 * @see iemThreadedFunc_BltIn_CheckCsLimAndOpcodesOnNextPageLoadingTlb
 */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_CheckOpcodesOnNextPageLoadingTlbConsiderCsLim)
{
    PCIEMTB const  pTb         = pVCpu->iem.s.pCurTbR3;
    uint32_t const cbInstr     = (uint8_t)uParam0;
    uint32_t const cbStartPage = (uint32_t)(uParam0 >> 32);
    uint32_t const idxRange1   = (uint32_t)uParam1;
    //uint32_t const offRange1   = (uint32_t)uParam2;
    uint32_t const idxRange2   = idxRange1 + 1;
    BODY_CONSIDER_CS_LIM_CHECKING(pTb, cbInstr);
    BODY_LOAD_TLB_FOR_NEW_PAGE(pTb, cbStartPage, idxRange2, cbInstr);
    BODY_CHECK_OPCODES(pTb, idxRange2, 0, cbInstr);
    RT_NOREF(uParam2);
    return VINF_SUCCESS;
}


/**
 * Built-in function for checking CS.LIM, loading TLB and checking opcodes when
 * advancing naturally to a different code page with first instr at byte 0.
 *
 * @see iemThreadedFunc_BltIn_CheckOpcodesOnNewPageLoadingTlb
 */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_CheckCsLimAndOpcodesOnNewPageLoadingTlb)
{
    PCIEMTB const  pTb         = pVCpu->iem.s.pCurTbR3;
    uint32_t const cbInstr     = (uint8_t)uParam0;
    uint32_t const idxRange    = (uint32_t)uParam1;
    RT_NOREF(uParam2); //Assert(uParam2 == 0 /*offRange*/);
    BODY_CHECK_CS_LIM(cbInstr);
    BODY_LOAD_TLB_FOR_NEW_PAGE(pTb, 0, idxRange, cbInstr);
    Assert(pVCpu->iem.s.offCurInstrStart == 0);
    BODY_CHECK_OPCODES(pTb, idxRange, 0, cbInstr);
    return VINF_SUCCESS;
}


/**
 * Built-in function for loading TLB and checking opcodes when advancing
 * naturally to a different code page with first instr at byte 0.
 *
 * @see iemThreadedFunc_BltIn_CheckCsLimAndOpcodesOnNewPageLoadingTlb
 */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_CheckOpcodesOnNewPageLoadingTlb)
{
    PCIEMTB const  pTb         = pVCpu->iem.s.pCurTbR3;
    uint32_t const cbInstr     = (uint8_t)uParam0;
    uint32_t const idxRange    = (uint32_t)uParam1;
    RT_NOREF(uParam2); //Assert(uParam2 == 0 /*offRange*/);
    BODY_LOAD_TLB_FOR_NEW_PAGE(pTb, 0, idxRange, cbInstr);
    Assert(pVCpu->iem.s.offCurInstrStart == 0);
    BODY_CHECK_OPCODES(pTb, idxRange, 0, cbInstr);
    return VINF_SUCCESS;
}


/**
 * Built-in function for loading TLB and checking opcodes and considering the
 * need for CS.LIM checking when advancing naturally to a different code page
 * with first instr at byte 0.
 *
 * @see iemThreadedFunc_BltIn_CheckCsLimAndOpcodesOnNewPageLoadingTlb
 */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_CheckOpcodesOnNewPageLoadingTlbConsiderCsLim)
{
    PCIEMTB const  pTb         = pVCpu->iem.s.pCurTbR3;
    uint32_t const cbInstr     = (uint8_t)uParam0;
    uint32_t const idxRange    = (uint32_t)uParam1;
    RT_NOREF(uParam2); //Assert(uParam2 == 0 /*offRange*/);
    BODY_CONSIDER_CS_LIM_CHECKING(pTb, cbInstr);
    BODY_LOAD_TLB_FOR_NEW_PAGE(pTb, 0, idxRange, cbInstr);
    Assert(pVCpu->iem.s.offCurInstrStart == 0);
    BODY_CHECK_OPCODES(pTb, idxRange, 0, cbInstr);
    return VINF_SUCCESS;
}


/**
 * Built-in function for jumping in the call sequence.
 */
IEM_DECL_IEMTHREADEDFUNC_DEF(iemThreadedFunc_BltIn_Jump)
{
    Assert(uParam1 == 0 && uParam2 == 0);
    RT_NOREF(pVCpu, uParam0, uParam1, uParam2);
    return VINF_IEM_REEXEC_JUMP;
}

