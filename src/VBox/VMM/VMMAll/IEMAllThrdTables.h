/* $Id: IEMAllThrdTables.h 107184 2024-11-26 22:32:44Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Instruction Decoding and Threaded Recompilation, Instruction Tables.
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

#ifndef VMM_INCLUDED_SRC_VMMAll_IEMAllThrdTables_h
#define VMM_INCLUDED_SRC_VMMAll_IEMAllThrdTables_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#ifndef LOG_GROUP /* defined when included by tstIEMCheckMc.cpp */
# define LOG_GROUP LOG_GROUP_IEM_RE_THREADED
#endif
#define IEM_WITH_CODE_TLB_AND_OPCODE_BUF  /* A bit hackish, but its all in IEMInline.h. */
#define VMCPU_INCL_CPUM_GST_CTX
#include <VBox/vmm/iem.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/apic.h>
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
#ifndef TST_IEM_CHECK_MC
# include "IEMInternal.h"
#endif
#include <VBox/vmm/vmcc.h>
#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/dis.h>
#include <VBox/disopcode-x86-amd64.h>
#include <iprt/asm-math.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/x86.h>

#ifndef TST_IEM_CHECK_MC
# include "IEMInline.h"
# include "IEMOpHlp.h"
# include "IEMMc.h"
#endif

#include "IEMThreadedFunctions.h"
#include "IEMN8veRecompiler.h" /* For a_fGstShwFlush and iemThreadedRecompilerMcDeferToCImpl0. */


/*
 * Narrow down configs here to avoid wasting time on unused configs here.
 */

#ifndef IEM_WITH_CODE_TLB
# error The code TLB must be enabled for the recompiler.
#endif

#ifndef IEM_WITH_DATA_TLB
# error The data TLB must be enabled for the recompiler.
#endif

#ifndef IEM_WITH_SETJMP
# error The setjmp approach must be enabled for the recompiler.
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define g_apfnOneByteMap    g_apfnIemThreadedRecompilerOneByteMap
#define g_apfnTwoByteMap    g_apfnIemThreadedRecompilerTwoByteMap
#define g_apfnThreeByte0f3a g_apfnIemThreadedRecompilerThreeByte0f3a
#define g_apfnThreeByte0f38 g_apfnIemThreadedRecompilerThreeByte0f38
#define g_apfnVexMap1       g_apfnIemThreadedRecompilerVecMap1
#define g_apfnVexMap2       g_apfnIemThreadedRecompilerVecMap2
#define g_apfnVexMap3       g_apfnIemThreadedRecompilerVecMap3


/*
 * Override IEM_MC_BEGIN to take down the IEM_CIMPL_F_XXX flags.
 */
#undef IEM_MC_BEGIN
#define IEM_MC_BEGIN(a_fMcFlags, a_fCImplFlags) \
    { \
        pVCpu->iem.s.fTbCurInstr = (a_fCImplFlags) /*| ((a_fMcFlags) << 20*/

/*
 * Override IEM_MC_CALC_RM_EFF_ADDR to use iemOpHlpCalcRmEffAddrJmpEx and produce uEffAddrInfo.
 */
#undef IEM_MC_CALC_RM_EFF_ADDR
#ifndef IEM_WITH_SETJMP
# define IEM_MC_CALC_RM_EFF_ADDR(a_GCPtrEff, a_bRm, a_cbImmAndRspOffset) \
    uint64_t uEffAddrInfo; \
    IEM_MC_RETURN_ON_FAILURE(iemOpHlpCalcRmEffAddrJmpEx(pVCpu, (a_bRm), (a_cbImmAndRspOffset), &(a_GCPtrEff), &uEffAddrInfo))
#else
# define IEM_MC_CALC_RM_EFF_ADDR(a_GCPtrEff, a_bRm, a_cbImmAndRspOffset) \
    uint64_t uEffAddrInfo; \
    ((a_GCPtrEff) = iemOpHlpCalcRmEffAddrJmpEx(pVCpu, (a_bRm), (a_cbImmAndRspOffset), &uEffAddrInfo))
#endif

/*
 * Likewise override IEM_OPCODE_SKIP_RM_EFF_ADDR_BYTES so we fetch all the opcodes.
 */
#undef IEM_OPCODE_SKIP_RM_EFF_ADDR_BYTES
#define IEM_OPCODE_SKIP_RM_EFF_ADDR_BYTES(a_bRm) do { \
        uint64_t uEffAddrInfo; \
        (void)iemOpHlpCalcRmEffAddrJmpEx(pVCpu, bRm, 0, &uEffAddrInfo); \
    } while (0)

/*
 * Override the IEM_MC_REL_JMP_S*_AND_FINISH macros to check for zero byte jumps.
 */
#undef IEM_MC_REL_JMP_S8_AND_FINISH
#define IEM_MC_REL_JMP_S8_AND_FINISH(a_i8) do { \
        Assert(pVCpu->iem.s.fTbBranched != 0); \
        if ((a_i8) == 0) \
            pVCpu->iem.s.fTbBranched |= IEMBRANCHED_F_ZERO; \
        return iemRegRipRelativeJumpS8AndFinishClearingRF(pVCpu, IEM_GET_INSTR_LEN(pVCpu), (a_i8), pVCpu->iem.s.enmEffOpSize); \
    } while (0)

#undef IEM_MC_REL_JMP_S16_AND_FINISH
#define IEM_MC_REL_JMP_S16_AND_FINISH(a_i16) do { \
        Assert(pVCpu->iem.s.fTbBranched != 0); \
        if ((a_i16) == 0) \
            pVCpu->iem.s.fTbBranched |= IEMBRANCHED_F_ZERO; \
        return iemRegRipRelativeJumpS16AndFinishClearingRF(pVCpu, IEM_GET_INSTR_LEN(pVCpu), (a_i16)); \
    } while (0)

#undef IEM_MC_REL_JMP_S32_AND_FINISH
#define IEM_MC_REL_JMP_S32_AND_FINISH(a_i32) do { \
        Assert(pVCpu->iem.s.fTbBranched != 0); \
        if ((a_i32) == 0) \
            pVCpu->iem.s.fTbBranched |= IEMBRANCHED_F_ZERO; \
        return iemRegRipRelativeJumpS32AndFinishClearingRF(pVCpu, IEM_GET_INSTR_LEN(pVCpu), (a_i32), pVCpu->iem.s.enmEffOpSize); \
    } while (0)


#ifndef IEM_WITH_INTRA_TB_JUMPS
/**
 * Stub for a no-jumps config, see IEMAllThrdRecompiler.cpp for the real thing.
 */
DECL_FORCE_INLINE(int) iemThreadedCompileBackAtFirstInstruction(PVMCPU pVCpu, PIEMTB pTb)
{
    RT_NOREF(pTb);
    STAM_REL_COUNTER_INC(&pVCpu->iem.s.StatTbLoopFullTbDetected2);
    return VINF_IEM_RECOMPILE_END_TB;
}
#endif


/*
 * Emit call macros.
 */
#define IEM_MC2_BEGIN_EMIT_CALLS(a_fCheckIrqBefore) \
    { \
        PIEMTB const  pTb = pVCpu->iem.s.pCurTbR3; \
        uint8_t const cbInstrMc2 = IEM_GET_INSTR_LEN(pVCpu); \
        AssertMsg(pVCpu->iem.s.offOpcode == cbInstrMc2, \
                  ("%u vs %u (%04x:%08RX64)\n", pVCpu->iem.s.offOpcode, cbInstrMc2, \
                  pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip)); \
        \
        /* If we need to check for IRQs before the instruction, we do that before \
           adding any opcodes as it may abort the instruction. \
           Note! During compilation, we may swap IRQ and #PF exceptions here \
                 in a manner that a real CPU would not do.  However it shouldn't \
                 be something that is easy (if at all possible) to observe in the \
                 guest, so fine.  The unexpected end-of-tb below have the same \
                 potential "issue". */ \
        if (!(a_fCheckIrqBefore) || iemThreadedCompileEmitIrqCheckBefore(pVCpu, pTb)) \
        { /* likely */ } \
        else \
            return VINF_IEM_RECOMPILE_END_TB; \
        \
        /* No page crossing, right? */ \
        uint16_t const offOpcodeMc2 = pTb->cbOpcodes; \
        uint8_t const  idxRangeMc2  = pTb->cRanges - 1; \
        if (   !pVCpu->iem.s.fTbCrossedPage \
            && !pVCpu->iem.s.fTbCheckOpcodes \
            && !pVCpu->iem.s.fTbBranched \
            && !(pTb->fFlags & IEMTB_F_CS_LIM_CHECKS)) \
        { \
            /* Break/loop if we're back to the first instruction in the TB again. */ \
            if (   pTb->aRanges[idxRangeMc2].idxPhysPage != 0 \
                ||    (unsigned)pTb->aRanges[idxRangeMc2].offPhysPage + (unsigned)pTb->aRanges[idxRangeMc2].cbOpcodes \
                   != (pTb->GCPhysPc & GUEST_PAGE_OFFSET_MASK) \
                || offOpcodeMc2 == 0) \
            { \
                /** @todo Custom copy function, given range is 1 thru 15 bytes. */ \
                memcpy(&pTb->pabOpcodes[offOpcodeMc2], pVCpu->iem.s.abOpcode, pVCpu->iem.s.offOpcode); \
                pTb->cbOpcodes                       = offOpcodeMc2 + pVCpu->iem.s.offOpcode; \
                pTb->aRanges[idxRangeMc2].cbOpcodes += cbInstrMc2; \
                Assert(pTb->cbOpcodes <= pVCpu->iem.s.cbOpcodesAllocated); \
            } \
            else \
                return iemThreadedCompileBackAtFirstInstruction(pVCpu, pTb); \
        } \
        else if (iemThreadedCompileBeginEmitCallsComplications(pVCpu, pTb)) \
        { /* likely */ } \
        else \
            return VINF_IEM_RECOMPILE_END_TB; \
        \
        uint8_t const idxInstrMc2 = pTb->cInstructions; \
        \
        /* Emit hardware instruction breakpoint check if enabled. */ \
        if (!(pTb->fFlags & IEM_F_PENDING_BRK_INSTR)) \
        { /* likely */ } \
        else \
            IEM_MC2_EMIT_CALL_0(kIemThreadedFunc_BltIn_CheckHwInstrBps)

#define IEM_MC2_EMIT_CALL_0(a_enmFunction) do { \
        IEMTHREADEDFUNCS const enmFunctionCheck = a_enmFunction; RT_NOREF(enmFunctionCheck); \
        \
        LogFlow(("Call #%u: " #a_enmFunction "\n", pTb->Thrd.cCalls)); \
        PIEMTHRDEDCALLENTRY const pCall = &pTb->Thrd.paCalls[pTb->Thrd.cCalls++]; \
        pCall->enmFunction = a_enmFunction; \
        pCall->idxInstr    = idxInstrMc2; \
        pCall->cbOpcode    = cbInstrMc2; \
        pCall->offOpcode   = offOpcodeMc2; \
        pCall->uTbLookup   = 0; \
        pCall->fFlags      = 0; \
        pCall->auParams[0] = 0; \
        pCall->auParams[1] = 0; \
        pCall->auParams[2] = 0; \
    } while (0)
#define IEM_MC2_EMIT_CALL_1(a_enmFunction, a_uArg0) do { \
        IEMTHREADEDFUNCS const enmFunctionCheck = a_enmFunction; RT_NOREF(enmFunctionCheck); \
        uint64_t         const uArg0Check       = (a_uArg0);     RT_NOREF(uArg0Check); \
        \
        LogFlow(("Call #%u: " #a_enmFunction " a0=%RX64\n", pTb->Thrd.cCalls, (uint64_t)a_uArg0)); \
        PIEMTHRDEDCALLENTRY const pCall = &pTb->Thrd.paCalls[pTb->Thrd.cCalls++]; \
        pCall->enmFunction = a_enmFunction; \
        pCall->idxInstr    = idxInstrMc2; \
        pCall->cbOpcode    = cbInstrMc2; \
        pCall->offOpcode   = offOpcodeMc2; \
        pCall->uTbLookup   = 0; \
        pCall->fFlags      = 0; \
        pCall->auParams[0] = a_uArg0; \
        pCall->auParams[1] = 0; \
        pCall->auParams[2] = 0; \
    } while (0)
#define IEM_MC2_EMIT_CALL_2(a_enmFunction, a_uArg0, a_uArg1) do { \
        IEMTHREADEDFUNCS const enmFunctionCheck = a_enmFunction; RT_NOREF(enmFunctionCheck); \
        uint64_t         const uArg0Check       = (a_uArg0);     RT_NOREF(uArg0Check); \
        uint64_t         const uArg1Check       = (a_uArg1);     RT_NOREF(uArg1Check); \
        \
        LogFlow(("Call #%u: " #a_enmFunction " a0=%RX64 a1=%RX64\n", pTb->Thrd.cCalls, (uint64_t)a_uArg0, (uint64_t)a_uArg1)); \
        PIEMTHRDEDCALLENTRY const pCall = &pTb->Thrd.paCalls[pTb->Thrd.cCalls++]; \
        pCall->enmFunction = a_enmFunction; \
        pCall->idxInstr    = idxInstrMc2; \
        pCall->cbOpcode    = cbInstrMc2; \
        pCall->offOpcode   = offOpcodeMc2; \
        pCall->uTbLookup   = 0; \
        pCall->fFlags      = 0; \
        pCall->auParams[0] = a_uArg0; \
        pCall->auParams[1] = a_uArg1; \
        pCall->auParams[2] = 0; \
    } while (0)
#define IEM_MC2_EMIT_CALL_3(a_enmFunction, a_uArg0, a_uArg1, a_uArg2) do { \
        IEMTHREADEDFUNCS const enmFunctionCheck = a_enmFunction; RT_NOREF(enmFunctionCheck); \
        uint64_t         const uArg0Check       = (a_uArg0);     RT_NOREF(uArg0Check); \
        uint64_t         const uArg1Check       = (a_uArg1);     RT_NOREF(uArg1Check); \
        uint64_t         const uArg2Check       = (a_uArg2);     RT_NOREF(uArg2Check); \
        \
        LogFlow(("Call #%u: " #a_enmFunction " a0=%RX64 a1=%RX64 a2=%RX64\n", pTb->Thrd.cCalls, (uint64_t)a_uArg0, (uint64_t)a_uArg1, (uint64_t)a_uArg2)); \
        PIEMTHRDEDCALLENTRY const pCall = &pTb->Thrd.paCalls[pTb->Thrd.cCalls++]; \
        pCall->enmFunction = a_enmFunction; \
        pCall->idxInstr    = idxInstrMc2; \
        pCall->offOpcode   = offOpcodeMc2; \
        pCall->cbOpcode    = cbInstrMc2; \
        pCall->uTbLookup   = 0; \
        pCall->fFlags      = 0; \
        pCall->auParams[0] = a_uArg0; \
        pCall->auParams[1] = a_uArg1; \
        pCall->auParams[2] = a_uArg2; \
    } while (0)

#define IEM_MC2_EMIT_CALL_WITH_TB_LOOKUP_0(a_fLargeTbLookup, a_enmFunction) do { \
        IEMTHREADEDFUNCS const enmFunctionCheck = a_enmFunction; RT_NOREF(enmFunctionCheck); \
        \
        LogFlow(("Call #%u: " #a_enmFunction "\n", pTb->Thrd.cCalls)); \
        PIEMTHRDEDCALLENTRY const pCall = &pTb->Thrd.paCalls[pTb->Thrd.cCalls++]; \
        pCall->enmFunction = a_enmFunction; \
        pCall->idxInstr    = idxInstrMc2; \
        pCall->cbOpcode    = cbInstrMc2; \
        pCall->offOpcode   = offOpcodeMc2; \
        pCall->uTbLookup   = IEM_TB_LOOKUP_TAB_MAKE(pTb->cTbLookupEntries, a_fLargeTbLookup); \
        pTb->cTbLookupEntries += !(a_fLargeTbLookup) ? 1 : IEM_TB_LOOKUP_TAB_LARGE_SIZE; \
        pCall->fFlags      = 0; \
        pCall->auParams[0] = 0; \
        pCall->auParams[1] = 0; \
        pCall->auParams[2] = 0; \
    } while (0)
#define IEM_MC2_EMIT_CALL_WITH_TB_LOOKUP_1(a_fLargeTbLookup, a_enmFunction, a_uArg0) do { \
        IEMTHREADEDFUNCS const enmFunctionCheck = a_enmFunction; RT_NOREF(enmFunctionCheck); \
        uint64_t         const uArg0Check       = (a_uArg0);     RT_NOREF(uArg0Check); \
        \
        LogFlow(("Call #%u: " #a_enmFunction " a0=%RX64\n", pTb->Thrd.cCalls, (uint64_t)a_uArg0)); \
        PIEMTHRDEDCALLENTRY const pCall = &pTb->Thrd.paCalls[pTb->Thrd.cCalls++]; \
        pCall->enmFunction = a_enmFunction; \
        pCall->idxInstr    = idxInstrMc2; \
        pCall->cbOpcode    = cbInstrMc2; \
        pCall->offOpcode   = offOpcodeMc2; \
        pCall->uTbLookup   = IEM_TB_LOOKUP_TAB_MAKE(pTb->cTbLookupEntries, a_fLargeTbLookup); \
        pTb->cTbLookupEntries += !(a_fLargeTbLookup) ? 1 : IEM_TB_LOOKUP_TAB_LARGE_SIZE; \
        pCall->fFlags      = 0; \
        pCall->auParams[0] = a_uArg0; \
        pCall->auParams[1] = 0; \
        pCall->auParams[2] = 0; \
    } while (0)
#define IEM_MC2_EMIT_CALL_WITH_TB_LOOKUP_2(a_fLargeTbLookup, a_enmFunction, a_uArg0, a_uArg1) do { \
        IEMTHREADEDFUNCS const enmFunctionCheck = a_enmFunction; RT_NOREF(enmFunctionCheck); \
        uint64_t         const uArg0Check       = (a_uArg0);     RT_NOREF(uArg0Check); \
        uint64_t         const uArg1Check       = (a_uArg1);     RT_NOREF(uArg1Check); \
        \
        LogFlow(("Call #%u: " #a_enmFunction " a0=%RX64 a1=%RX64\n", pTb->Thrd.cCalls, (uint64_t)a_uArg0, (uint64_t)a_uArg1)); \
        PIEMTHRDEDCALLENTRY const pCall = &pTb->Thrd.paCalls[pTb->Thrd.cCalls++]; \
        pCall->enmFunction = a_enmFunction; \
        pCall->idxInstr    = idxInstrMc2; \
        pCall->cbOpcode    = cbInstrMc2; \
        pCall->offOpcode   = offOpcodeMc2; \
        pCall->uTbLookup   = IEM_TB_LOOKUP_TAB_MAKE(pTb->cTbLookupEntries, a_fLargeTbLookup); \
        pTb->cTbLookupEntries += !(a_fLargeTbLookup) ? 1 : IEM_TB_LOOKUP_TAB_LARGE_SIZE; \
        pCall->fFlags      = 0; \
        pCall->auParams[0] = a_uArg0; \
        pCall->auParams[1] = a_uArg1; \
        pCall->auParams[2] = 0; \
    } while (0)
#define IEM_MC2_EMIT_CALL_WITH_TB_LOOKUP_3(a_fLargeTbLookup, a_enmFunction, a_uArg0, a_uArg1, a_uArg2) do { \
        IEMTHREADEDFUNCS const enmFunctionCheck = a_enmFunction; RT_NOREF(enmFunctionCheck); \
        uint64_t         const uArg0Check       = (a_uArg0);     RT_NOREF(uArg0Check); \
        uint64_t         const uArg1Check       = (a_uArg1);     RT_NOREF(uArg1Check); \
        uint64_t         const uArg2Check       = (a_uArg2);     RT_NOREF(uArg2Check); \
        \
        LogFlow(("Call #%u: " #a_enmFunction " a0=%RX64 a1=%RX64 a2=%RX64\n", pTb->Thrd.cCalls, (uint64_t)a_uArg0, (uint64_t)a_uArg1, (uint64_t)a_uArg2)); \
        PIEMTHRDEDCALLENTRY const pCall = &pTb->Thrd.paCalls[pTb->Thrd.cCalls++]; \
        pCall->enmFunction = a_enmFunction; \
        pCall->idxInstr    = idxInstrMc2; \
        pCall->cbOpcode    = cbInstrMc2; \
        pCall->offOpcode   = offOpcodeMc2; \
        pCall->uTbLookup   = IEM_TB_LOOKUP_TAB_MAKE(pTb->cTbLookupEntries, a_fLargeTbLookup); \
        pTb->cTbLookupEntries += !(a_fLargeTbLookup) ? 1 : IEM_TB_LOOKUP_TAB_LARGE_SIZE; \
        pCall->fFlags      = 0; \
        pCall->auParams[0] = a_uArg0; \
        pCall->auParams[1] = a_uArg1; \
        pCall->auParams[2] = a_uArg2; \
    } while (0)

#define IEM_MC2_END_EMIT_CALLS(a_fCImplFlags) \
        Assert(pTb->cInstructions <= pTb->Thrd.cCalls); \
        if (pTb->cInstructions < 255) \
            pTb->cInstructions++; \
        uint32_t const fCImplFlagsMc2 = (a_fCImplFlags); \
        RT_NOREF(fCImplFlagsMc2); \
    } while (0)


/*
 * IEM_MC_DEFER_TO_CIMPL_0 is easily wrapped up.
 *
 * Doing so will also take care of IEMOP_RAISE_DIVIDE_ERROR, IEMOP_RAISE_INVALID_LOCK_PREFIX,
 * IEMOP_RAISE_INVALID_OPCODE and their users.
 */
#undef IEM_MC_DEFER_TO_CIMPL_0_RET
#define IEM_MC_DEFER_TO_CIMPL_0_RET(a_fFlags, a_fGstShwFlush, a_pfnCImpl) \
    return iemThreadedRecompilerMcDeferToCImpl0(pVCpu, a_fFlags, a_fGstShwFlush, a_pfnCImpl)

IEM_DECL_MSC_GUARD_IGNORE  DECLINLINE(VBOXSTRICTRC)
iemThreadedRecompilerMcDeferToCImpl0(PVMCPUCC pVCpu, uint32_t fFlags, uint64_t fGstShwFlush, PFNIEMCIMPL0 pfnCImpl)
{
    LogFlow(("CImpl0: %04x:%08RX64 LB %#x: %#x %#RX64 %p\n",
             pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, IEM_GET_INSTR_LEN(pVCpu), fFlags, fGstShwFlush, pfnCImpl));
    pVCpu->iem.s.fTbCurInstr = fFlags;

    IEM_MC2_BEGIN_EMIT_CALLS(fFlags & IEM_CIMPL_F_CHECK_IRQ_BEFORE);
    IEM_MC2_EMIT_CALL_3(kIemThreadedFunc_BltIn_DeferToCImpl0, (uintptr_t)pfnCImpl, IEM_GET_INSTR_LEN(pVCpu), fGstShwFlush);
    if (   (fFlags & (IEM_CIMPL_F_MODE | IEM_CIMPL_F_VMEXIT))
        && !(fFlags & (IEM_CIMPL_F_END_TB | IEM_CIMPL_F_BRANCH_FAR)))
        IEM_MC2_EMIT_CALL_1(kIemThreadedFunc_BltIn_CheckMode, pVCpu->iem.s.fExec);
    IEM_MC2_END_EMIT_CALLS(fFlags);

    /*
     * We have to repeat work normally done by kdCImplFlags and
     * ThreadedFunctionVariation.emitThreadedCallStmts here.
     */
    AssertCompile(IEM_CIMPL_F_BRANCH_DIRECT      == IEMBRANCHED_F_DIRECT);
    AssertCompile(IEM_CIMPL_F_BRANCH_INDIRECT    == IEMBRANCHED_F_INDIRECT);
    AssertCompile(IEM_CIMPL_F_BRANCH_RELATIVE    == IEMBRANCHED_F_RELATIVE);
    AssertCompile(IEM_CIMPL_F_BRANCH_CONDITIONAL == IEMBRANCHED_F_CONDITIONAL);
    AssertCompile(IEM_CIMPL_F_BRANCH_FAR         == IEMBRANCHED_F_FAR);

    if (fFlags & (IEM_CIMPL_F_END_TB | IEM_CIMPL_F_BRANCH_FAR))
        pVCpu->iem.s.fEndTb = true;
    else if (fFlags & IEM_CIMPL_F_BRANCH_ANY)
        pVCpu->iem.s.fTbBranched = fFlags & (IEM_CIMPL_F_BRANCH_ANY | IEM_CIMPL_F_BRANCH_FAR | IEM_CIMPL_F_BRANCH_CONDITIONAL);

    if (fFlags & IEM_CIMPL_F_CHECK_IRQ_BEFORE)
        pVCpu->iem.s.cInstrTillIrqCheck = 0;

    return pfnCImpl(pVCpu, IEM_GET_INSTR_LEN(pVCpu));
}


/**
 * Helper for indicating that we've branched.
 */
DECL_FORCE_INLINE(void) iemThreadedSetBranched(PVMCPUCC pVCpu, uint8_t fTbBranched)
{
    pVCpu->iem.s.fTbBranched          = fTbBranched;
    //pVCpu->iem.s.GCPhysTbBranchSrcBuf = pVCpu->iem.s.GCPhysInstrBuf;
    //pVCpu->iem.s.GCVirtTbBranchSrcBuf = pVCpu->iem.s.uInstrBufPc;
}


#endif /* !VMM_INCLUDED_SRC_VMMAll_IEMAllThrdTables_h */
