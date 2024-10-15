/* $Id: IEMAllN8veRecompFuncs.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEM - Native Recompiler - Inlined Bits.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_IEM_RE_NATIVE
#define IEM_WITH_OPAQUE_DECODER_STATE
#define VMCPU_INCL_CPUM_GST_CTX
#define VMM_INCLUDED_SRC_include_IEMMc_h /* block IEMMc.h inclusion. */
#define IEMNATIVE_INCL_TABLE_FUNCTION_PROTOTYPES
#include <VBox/vmm/iem.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/dbgf.h>
#include "IEMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/dis.h>
#include <VBox/param.h>
#include <iprt/assert.h>
#include <iprt/heap.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#if   defined(RT_ARCH_AMD64)
# include <iprt/x86.h>
#elif defined(RT_ARCH_ARM64)
# include <iprt/armv8.h>
#endif

#include "IEMInline.h"
#include "IEMThreadedFunctions.h"
#include "IEMN8veRecompiler.h"
#include "IEMN8veRecompilerEmit.h"
#include "IEMN8veRecompilerTlbLookup.h"
#include "IEMNativeFunctions.h"
#include "target-x86/IEMAllN8veEmit-x86.h"


/*
 * Narrow down configs here to avoid wasting time on unused configs here.
 * Note! Same checks in IEMAllThrdRecompiler.cpp.
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

#if defined(IEMNATIVE_WITH_SIMD_FP_NATIVE_EMITTERS) && !defined(IEMNATIVE_WITH_SIMD_REG_ALLOCATOR)
# error "IEMNATIVE_WITH_SIMD_FP_NATIVE_EMITTERS requires IEMNATIVE_WITH_SIMD_REG_ALLOCATOR"
#endif


/*********************************************************************************************************************************
*   Code emitters for flushing pending guest register writes and sanity checks                                                   *
*********************************************************************************************************************************/

#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING

# ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING_DEBUG
/**
 * Updates IEMCPU::uPcUpdatingDebug.
 */
DECL_INLINE_THROW(uint32_t) iemNativeEmitPcDebugAdd(PIEMRECOMPILERSTATE pReNative, uint32_t off, int64_t offDisp, uint8_t cBits)
{
# ifdef RT_ARCH_AMD64
    if (pReNative->Core.fDebugPcInitialized && cBits >= 32)
    {
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 32);
        if ((int32_t)offDisp == offDisp || cBits != 64)
        {
            /* add [q]word [pVCpu->iem.s.uPcUpdatingDebug], imm32/imm8 */
            if (cBits == 64)
                pCodeBuf[off++] = X86_OP_REX_W;
            pCodeBuf[off++] = (int8_t)offDisp == offDisp ? 0x83 : 0x81;
            off = iemNativeEmitGprByVCpuDisp(pCodeBuf, off, 0, RT_UOFFSETOF(VMCPU, iem.s.uPcUpdatingDebug));
            if ((int8_t)offDisp == offDisp)
                pCodeBuf[off++] = (int8_t)offDisp;
            else
            {
                *(int32_t *)&pCodeBuf[off] = (int32_t)offDisp;
                off += sizeof(int32_t);
            }
        }
        else
        {
            /* mov tmp0, imm64 */
            off = iemNativeEmitLoadGprImmEx(pCodeBuf, off, IEMNATIVE_REG_FIXED_TMP0, offDisp);

            /* add [pVCpu->iem.s.uPcUpdatingDebug], tmp0 */
            if (cBits == 64)
                pCodeBuf[off++] = X86_OP_REX_W | (IEMNATIVE_REG_FIXED_TMP0 >= 8 ? X86_OP_REX_R : 0);
            else if (IEMNATIVE_REG_FIXED_TMP0 >= 8)
                pCodeBuf[off++] = X86_OP_REX_R;
            pCodeBuf[off++] = 0x01;
            off = iemNativeEmitGprByVCpuDisp(pCodeBuf, off, IEMNATIVE_REG_FIXED_TMP0 & 7,
                                             RT_UOFFSETOF(VMCPU, iem.s.uPcUpdatingDebug));
        }
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
        return off;
    }
# endif

    uint8_t const         idxTmpReg = iemNativeRegAllocTmp(pReNative, &off);
    PIEMNATIVEINSTR const pCodeBuf  = iemNativeInstrBufEnsure(pReNative, off, RT_ARCH_VAL == RT_ARCH_VAL_AMD64 ? 32 : 12);

    if (pReNative->Core.fDebugPcInitialized)
    {
        Log4(("uPcUpdatingDebug+=%ld cBits=%d off=%#x\n", offDisp, cBits, off));
        off = iemNativeEmitLoadGprFromVCpuU64Ex(pCodeBuf, off, idxTmpReg, RT_UOFFSETOF(VMCPU, iem.s.uPcUpdatingDebug));
    }
    else
    {
        Log4(("uPcUpdatingDebug=rip+%ld cBits=%d off=%#x\n", offDisp, cBits, off));
        pReNative->Core.fDebugPcInitialized = true;
        off = iemNativeEmitLoadGprFromVCpuU64Ex(pCodeBuf, off, idxTmpReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rip));
    }

    if (cBits == 64)
        off = iemNativeEmitAddGprImmEx(pCodeBuf, off, idxTmpReg, offDisp, IEMNATIVE_REG_FIXED_TMP0);
    else
    {
        off = iemNativeEmitAddGpr32ImmEx(pCodeBuf, off, idxTmpReg, (int32_t)offDisp, IEMNATIVE_REG_FIXED_TMP0);
        if (cBits == 16)
            off = iemNativeEmitAndGpr32ByImmEx(pCodeBuf, off, idxTmpReg, UINT16_MAX);
    }

    off = iemNativeEmitStoreGprToVCpuU64Ex(pCodeBuf, off, idxTmpReg, RT_UOFFSETOF(VMCPU, iem.s.uPcUpdatingDebug),
                                           IEMNATIVE_REG_FIXED_TMP0);

    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    iemNativeRegFreeTmp(pReNative, idxTmpReg);
    return off;
}


# elif defined(IEMNATIVE_REG_FIXED_PC_DBG)
DECL_INLINE_THROW(uint32_t) iemNativePcAdjustCheck(PIEMRECOMPILERSTATE pReNative, uint32_t off)
{
    /* Compare the shadow with the context value, they should match. */
    off = iemNativeEmitLoadGprFromGpr(pReNative, off, IEMNATIVE_REG_FIXED_TMP1, IEMNATIVE_REG_FIXED_PC_DBG);
    off = iemNativeEmitAddGprImm(pReNative, off, IEMNATIVE_REG_FIXED_TMP1, pReNative->Core.offPc);
    off = iemNativeEmitGuestRegValueCheck(pReNative, off, IEMNATIVE_REG_FIXED_TMP1, kIemNativeGstReg_Pc);
    return off;
}
# endif

#endif /* IEMNATIVE_WITH_DELAYED_PC_UPDATING  */

/**
 * Flushes delayed write of a specific guest register.
 *
 * This must be called prior to calling CImpl functions and any helpers that use
 * the guest state (like raising exceptions) and such.
 *
 * This optimization has not yet been implemented.  The first target would be
 * RIP updates, since these are the most common ones.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeRegFlushPendingSpecificWrite(PIEMRECOMPILERSTATE pReNative, uint32_t off, IEMNATIVEGSTREGREF enmClass, uint8_t idxReg)
{
#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
    /* If for whatever reason it is possible to reference the PC register at some point we need to do the writeback here first. */
#endif

#ifdef IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK
#if 0 /** @todo r=aeichner EFLAGS writeback delay. */
    if (   enmClass == kIemNativeGstRegRef_EFlags
        && pReNative->Core.bmGstRegShadowDirty & RT_BIT_64(kIemNativeGstReg_EFlags))
        off = iemNativeRegFlushPendingWrite(pReNative, off, kIemNativeGstReg_EFlags);
#else
    Assert(!(pReNative->Core.bmGstRegShadowDirty & RT_BIT_64(kIemNativeGstReg_EFlags)));
#endif

    if (   enmClass == kIemNativeGstRegRef_Gpr
        && pReNative->Core.bmGstRegShadowDirty & RT_BIT_64(idxReg))
        off = iemNativeRegFlushPendingWrite(pReNative, off, IEMNATIVEGSTREG_GPR(idxReg));
#endif

#ifdef IEMNATIVE_WITH_SIMD_REG_ALLOCATOR
    if (   enmClass == kIemNativeGstRegRef_XReg
        && pReNative->Core.bmGstSimdRegShadows & RT_BIT_64(idxReg))
    {
        off = iemNativeSimdRegFlushPendingWrite(pReNative, off, IEMNATIVEGSTSIMDREG_SIMD(idxReg));
        /* Flush the shadows as the register needs to be reloaded (there is no guarantee right now, that the referenced register doesn't change). */
        uint8_t const idxHstSimdReg = pReNative->Core.aidxGstSimdRegShadows[idxReg];

        iemNativeSimdRegClearGstSimdRegShadowing(pReNative, idxHstSimdReg, off);
        iemNativeSimdRegFlushGuestShadows(pReNative, RT_BIT_64(IEMNATIVEGSTSIMDREG_SIMD(idxReg)));
    }
#endif
    RT_NOREF(pReNative, enmClass, idxReg);
    return off;
}



/*********************************************************************************************************************************
*   Emitters for IEM_MC_BEGIN_EX and IEM_MC_END.                                                                                    *
*********************************************************************************************************************************/

#undef  IEM_MC_BEGIN /* unused */
#define IEM_MC_BEGIN_EX(a_fMcFlags, a_fCImplFlags, a_cArgsIncludingHidden) \
    { \
        Assert(pReNative->Core.bmVars     == 0); \
        Assert(pReNative->Core.u64ArgVars == UINT64_MAX); \
        Assert(pReNative->Core.bmStack    == 0); \
        pReNative->fMc    = (a_fMcFlags); \
        pReNative->fCImpl = (a_fCImplFlags); \
        pReNative->cArgsX = (a_cArgsIncludingHidden)

/** We have to get to the end in recompilation mode, as otherwise we won't
 * generate code for all the IEM_MC_IF_XXX branches. */
#define IEM_MC_END() \
        iemNativeVarFreeAll(pReNative); \
    } return off



/*********************************************************************************************************************************
*   Liveness Stubs                                                                                                               *
*********************************************************************************************************************************/

#define IEM_MC_LIVENESS_GREG_INPUT(a_iGReg)     ((void)0)
#define IEM_MC_LIVENESS_GREG_CLOBBER(a_iGReg)   ((void)0)
#define IEM_MC_LIVENESS_GREG_MODIFY(a_iGReg)    ((void)0)

#define IEM_MC_LIVENESS_MREG_INPUT(a_iMReg)     ((void)0)
#define IEM_MC_LIVENESS_MREG_CLOBBER(a_iMReg)   ((void)0)
#define IEM_MC_LIVENESS_MREG_MODIFY(a_iMReg)    ((void)0)

#define IEM_MC_LIVENESS_XREG_INPUT(a_iXReg)     ((void)0)
#define IEM_MC_LIVENESS_XREG_CLOBBER(a_iXReg)   ((void)0)
#define IEM_MC_LIVENESS_XREG_MODIFY(a_iXReg)    ((void)0)

#define IEM_MC_LIVENESS_MXCSR_INPUT()           ((void)0)
#define IEM_MC_LIVENESS_MXCSR_CLOBBER()         ((void)0)
#define IEM_MC_LIVENESS_MXCSR_MODIFY()          ((void)0)


/*********************************************************************************************************************************
*   Native Emitter Support.                                                                                                      *
*********************************************************************************************************************************/

#define IEM_MC_NATIVE_IF(a_fSupportedHosts)     if (RT_ARCH_VAL & (a_fSupportedHosts)) {

#define IEM_MC_NATIVE_ELSE()                    } else {

#define IEM_MC_NATIVE_ENDIF()                   } ((void)0)


#define IEM_MC_NATIVE_EMIT_0(a_fnEmitter) \
    off = a_fnEmitter(pReNative, off)

#define IEM_MC_NATIVE_EMIT_1(a_fnEmitter, a0) \
    off = a_fnEmitter(pReNative, off, (a0))

#define IEM_MC_NATIVE_EMIT_2(a_fnEmitter, a0, a1) \
    off = a_fnEmitter(pReNative, off, (a0), (a1))

#define IEM_MC_NATIVE_EMIT_2_EX(a_fnEmitter, a0, a1) \
    off = a_fnEmitter(pReNative, off, pCallEntry->idxInstr, (a0), (a1))

#define IEM_MC_NATIVE_EMIT_3(a_fnEmitter, a0, a1, a2) \
    off = a_fnEmitter(pReNative, off, (a0), (a1), (a2))

#define IEM_MC_NATIVE_EMIT_4(a_fnEmitter, a0, a1, a2, a3) \
    off = a_fnEmitter(pReNative, off, (a0), (a1), (a2), (a3))

#define IEM_MC_NATIVE_EMIT_5(a_fnEmitter, a0, a1, a2, a3, a4) \
    off = a_fnEmitter(pReNative, off, (a0), (a1), (a2), (a3), (a4))

#define IEM_MC_NATIVE_EMIT_6(a_fnEmitter, a0, a1, a2, a3, a4, a5) \
    off = a_fnEmitter(pReNative, off, (a0), (a1), (a2), (a3), (a4), (a5))

#define IEM_MC_NATIVE_EMIT_7(a_fnEmitter, a0, a1, a2, a3, a4, a5, a6) \
    off = a_fnEmitter(pReNative, off, (a0), (a1), (a2), (a3), (a4), (a5), (a6))

#define IEM_MC_NATIVE_EMIT_8(a_fnEmitter, a0, a1, a2, a3, a4, a5, a6, a7) \
    off = a_fnEmitter(pReNative, off, (a0), (a1), (a2), (a3), (a4), (a5), (a6), (a7))


#ifndef RT_ARCH_AMD64
# define IEM_MC_NATIVE_SET_AMD64_HOST_REG_FOR_LOCAL(a_VarNm, a_idxHostReg) ((void)0)
#else
/** @note This is a naive approach that ASSUMES that the register isn't
 *        allocated, so it only works safely for the first allocation(s) in
 *        a MC block. */
# define IEM_MC_NATIVE_SET_AMD64_HOST_REG_FOR_LOCAL(a_VarNm, a_idxHostReg) \
    off = iemNativeVarSetAmd64HostRegisterForLocal(pReNative, off, a_VarNm, a_idxHostReg)

DECL_INLINE_THROW(uint8_t) iemNativeVarRegisterSet(PIEMRECOMPILERSTATE pReNative, uint8_t idxVar, uint8_t idxReg,
                                                   uint32_t off, bool fAllocated);

DECL_INLINE_THROW(uint32_t)
iemNativeVarSetAmd64HostRegisterForLocal(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVar, uint8_t idxHstReg)
{
    Log12(("iemNativeVarSetAmd64HostRegisterForLocal: idxVar=%#x idxHstReg=%s (%#x) off=%#x\n", idxVar, g_apszIemNativeHstRegNames[idxHstReg], idxHstReg, off));
    Assert(idxHstReg < RT_ELEMENTS(pReNative->Core.aHstRegs));
    Assert(!(pReNative->Core.bmHstRegs & RT_BIT_32(idxHstReg))); /* iemNativeVarRegisterSet does a throw/longjmp on this */

# ifdef IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK
    /* Must flush the register if it hold pending writes. */
    if (   (pReNative->Core.bmHstRegsWithGstShadow & RT_BIT_32(idxHstReg))
        && (pReNative->Core.bmGstRegShadowDirty & pReNative->Core.aHstRegs[idxHstReg].fGstRegShadows) )
        off = iemNativeRegFlushDirtyGuest(pReNative, off, pReNative->Core.aHstRegs[idxHstReg].fGstRegShadows);
# endif

    iemNativeVarRegisterSet(pReNative, idxVar, idxHstReg, off, false /*fAllocated*/);
    return off;
}

#endif /* RT_ARCH_AMD64 */



/*********************************************************************************************************************************
*   Emitters for standalone C-implementation deferals (IEM_MC_DEFER_TO_CIMPL_XXXX)                                               *
*********************************************************************************************************************************/

#define IEM_MC_DEFER_TO_CIMPL_0_RET_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl) \
    pReNative->fMc    = 0; \
    pReNative->fCImpl = (a_fFlags); \
    return iemNativeEmitCImplCall0(pReNative, off, pCallEntry->idxInstr, a_fGstShwFlush, (uintptr_t)a_pfnCImpl, \
                                   a_cbInstr) /** @todo not used ... */


#define IEM_MC_DEFER_TO_CIMPL_1_RET_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0) \
    pReNative->fMc    = 0; \
    pReNative->fCImpl = (a_fFlags); \
    return iemNativeEmitCImplCall1(pReNative, off, pCallEntry->idxInstr, a_fGstShwFlush, (uintptr_t)a_pfnCImpl, a_cbInstr, a0)

DECL_INLINE_THROW(uint32_t) iemNativeEmitCImplCall1(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                    uint8_t idxInstr, uint64_t a_fGstShwFlush,
                                                    uintptr_t pfnCImpl, uint8_t cbInstr, uint64_t uArg0)
{
    return iemNativeEmitCImplCall(pReNative, off, idxInstr, a_fGstShwFlush, pfnCImpl, cbInstr, 1, uArg0, 0, 0);
}


#define IEM_MC_DEFER_TO_CIMPL_2_RET_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1) \
    pReNative->fMc    = 0; \
    pReNative->fCImpl = (a_fFlags); \
    return iemNativeEmitCImplCall2(pReNative, off, pCallEntry->idxInstr, a_fGstShwFlush, \
                                   (uintptr_t)a_pfnCImpl, a_cbInstr, a0, a1)

DECL_INLINE_THROW(uint32_t) iemNativeEmitCImplCall2(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                    uint8_t idxInstr, uint64_t a_fGstShwFlush,
                                                    uintptr_t pfnCImpl, uint8_t cbInstr, uint64_t uArg0, uint64_t uArg1)
{
    return iemNativeEmitCImplCall(pReNative, off, idxInstr, a_fGstShwFlush, pfnCImpl, cbInstr, 2, uArg0, uArg1, 0);
}


#define IEM_MC_DEFER_TO_CIMPL_3_RET_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2) \
    pReNative->fMc    = 0; \
    pReNative->fCImpl = (a_fFlags); \
    return iemNativeEmitCImplCall3(pReNative, off, pCallEntry->idxInstr, a_fGstShwFlush, \
                                   (uintptr_t)a_pfnCImpl, a_cbInstr, a0, a1, a2)

DECL_INLINE_THROW(uint32_t) iemNativeEmitCImplCall3(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                    uint8_t idxInstr, uint64_t a_fGstShwFlush,
                                                    uintptr_t pfnCImpl, uint8_t cbInstr, uint64_t uArg0, uint64_t uArg1,
                                                    uint64_t uArg2)
{
    return iemNativeEmitCImplCall(pReNative, off, idxInstr, a_fGstShwFlush, pfnCImpl, cbInstr, 3, uArg0, uArg1, uArg2);
}



/*********************************************************************************************************************************
*   Emitters for advancing PC/RIP/EIP/IP (IEM_MC_ADVANCE_RIP_AND_FINISH_XXX)                                                     *
*********************************************************************************************************************************/

/** Emits the flags check for IEM_MC_ADVANCE_RIP_AND_FINISH_THREADED_PC64_WITH_FLAGS
 *  and the other _WITH_FLAGS MCs, see iemRegFinishClearingRF. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitFinishInstructionFlagsCheck(PIEMRECOMPILERSTATE pReNative, uint32_t off)
{
    /*
     * If its not just X86_EFL_RF and CPUMCTX_INHIBIT_SHADOW that are set, we
     * return with special status code and make the execution loop deal with
     * this.  If TF or CPUMCTX_DBG_HIT_DRX_MASK triggers, we have to raise an
     * exception and won't continue execution.  While CPUMCTX_DBG_DBGF_MASK
     * could continue w/o interruption, it probably will drop into the
     * debugger, so not worth the effort of trying to services it here and we
     * just lump it in with the handling of the others.
     *
     * To simplify the code and the register state management even more (wrt
     * immediate in AND operation), we always update the flags and skip the
     * extra check associated conditional jump.
     */
    AssertCompile(   (X86_EFL_TF | X86_EFL_RF | CPUMCTX_INHIBIT_SHADOW | CPUMCTX_DBG_HIT_DRX_MASK | CPUMCTX_DBG_DBGF_MASK)
                  <= UINT32_MAX);
#ifdef IEMNATIVE_WITH_LIVENESS_ANALYSIS
    AssertMsg(   pReNative->idxCurCall == 0
              || IEMLIVENESS_STATE_IS_INPUT_EXPECTED(iemNativeLivenessGetStateByGstRegEx(&pReNative->paLivenessEntries[pReNative->idxCurCall - 1],
                                                                                         IEMLIVENESSBIT_IDX_EFL_OTHER)),
              ("Efl_Other - %u\n", iemNativeLivenessGetStateByGstRegEx(&pReNative->paLivenessEntries[pReNative->idxCurCall - 1],
                                                                       IEMLIVENESSBIT_IDX_EFL_OTHER)));
#endif

    /*
     * As this code can break out of the execution loop when jumping to the ReturnWithFlags label
     * any pending register writes must be flushed.
     */
    off = iemNativeRegFlushPendingWrites(pReNative, off);

    uint8_t const idxEflReg = iemNativeRegAllocTmpForGuestEFlagsForUpdate(pReNative, &off,
                                                                          RT_BIT_64(IEMLIVENESSBIT_IDX_EFL_OTHER),
                                                                          RT_BIT_64(IEMLIVENESSBIT_IDX_EFL_OTHER));
    off = iemNativeEmitTbExitIfAnyBitsSetInGpr<kIemNativeLabelType_ReturnWithFlags>(pReNative, off, idxEflReg,
                                                                                      X86_EFL_TF
                                                                                    | CPUMCTX_DBG_HIT_DRX_MASK
                                                                                    | CPUMCTX_DBG_DBGF_MASK);
    off = iemNativeEmitAndGpr32ByImm(pReNative, off, idxEflReg, ~(uint32_t)(X86_EFL_RF | CPUMCTX_INHIBIT_SHADOW));
    off = iemNativeEmitStoreGprToVCpuU32(pReNative, off, idxEflReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.eflags));

    /* Free but don't flush the EFLAGS register. */
    iemNativeRegFreeTmp(pReNative, idxEflReg);

    return off;
}


/** Helper for iemNativeEmitFinishInstructionWithStatus. */
DECLINLINE(RTGCPHYS) iemNativeCallEntryToGCPhysPc(PCIEMTB pTb, PCIEMTHRDEDCALLENTRY pCallEntry)
{
    unsigned const offOpcodes = pCallEntry->offOpcode;
    unsigned const cRanges    = RT_MIN(pTb->cRanges, RT_ELEMENTS(pTb->aRanges));
    for (unsigned  idxRange   = 0; idxRange < cRanges; idxRange++)
    {
        unsigned const offRange = offOpcodes - (unsigned)pTb->aRanges[idxRange].offOpcodes;
        if (offRange < (unsigned)pTb->aRanges[idxRange].cbOpcodes)
            return iemTbGetRangePhysPageAddr(pTb, idxRange) + offRange + pTb->aRanges[idxRange].offPhysPage;
    }
    AssertFailedReturn(NIL_RTGCPHYS);
}


/** The VINF_SUCCESS dummy. */
template<int const a_rcNormal, bool const a_fIsJump>
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitFinishInstructionWithStatus(PIEMRECOMPILERSTATE pReNative, uint32_t off, PCIEMTHRDEDCALLENTRY pCallEntry,
                                         int32_t const offJump)
{
    AssertCompile(a_rcNormal == VINF_SUCCESS || a_rcNormal == VINF_IEM_REEXEC_BREAK);
    if (a_rcNormal != VINF_SUCCESS)
    {
#ifdef IEMNATIVE_WITH_INSTRUCTION_COUNTING
        off = iemNativeEmitStoreImmToVCpuU8(pReNative, off, pCallEntry->idxInstr, RT_UOFFSETOF(VMCPUCC, iem.s.idxTbCurInstr));
#else
        RT_NOREF_PV(pCallEntry);
#endif

        /* As this code returns from the TB any pending register writes must be flushed. */
        off = iemNativeRegFlushPendingWrites(pReNative, off);

        /*
         * If we're in a conditional, mark the current branch as exiting so we
         * can disregard its state when we hit the IEM_MC_ENDIF.
         */
        iemNativeMarkCurCondBranchAsExiting(pReNative);

        /*
         * Use the lookup table for getting to the next TB quickly.
         * Note! In this code path there can only be one entry at present.
         */
        uint8_t const  idxTbLookupFirst = IEM_TB_LOOKUP_TAB_GET_IDX(pCallEntry->uTbLookup);
        PCIEMTB const  pTbOrg           = pReNative->pTbOrg;
        Assert(idxTbLookupFirst < pTbOrg->cTbLookupEntries);
        Assert(IEM_TB_LOOKUP_TAB_GET_SIZE(pCallEntry->uTbLookup) == 1);

#if 0
        /* Update IEMCPU::ppTbLookupEntryR3 to get the best lookup effect. */
        PIEMTB * const ppTbLookupFirst  = IEMTB_GET_TB_LOOKUP_TAB_ENTRY(pTbOrg, idxTbLookupFirst);
        Assert(IEM_TB_LOOKUP_TAB_GET_SIZE(pCallEntry->uTbLookup) == 1); /* large stuff later/never */
        off = iemNativeEmitStoreImmToVCpuU64(pReNative, off, (uintptr_t)ppTbLookupFirst,
                                             RT_UOFFSETOF(VMCPU, iem.s.ppTbLookupEntryR3));

        return iemNativeEmitTbExit(pReNative, off, kIemNativeLabelType_ReturnBreak);

#else
        /* Load the index as argument #1 for the helper call at the given label. */
        off = iemNativeEmitLoadGpr8Imm(pReNative, off, IEMNATIVE_CALL_ARG1_GREG, idxTbLookupFirst);

        /*
         * Figure out the physical address of the current instruction and see
         * whether the next instruction we're about to execute is in the same
         * page so we by can optimistically skip TLB loading.
         *
         * - This is safe for all cases in FLAT mode.
         * - In segmentmented modes it is complicated, given that a negative
         *   jump may underflow EIP and a forward jump may overflow or run into
         *   CS.LIM and triggering a #GP.  The only thing we can get away with
         *   now at compile time is forward jumps w/o CS.LIM checks, since the
         *   lack of CS.LIM checks means we're good for the entire physical page
         *   we're executing on and another 15 bytes before we run into CS.LIM.
         */
        if (   IEM_F_MODE_X86_IS_FLAT(pReNative->fExec)
# if 0 /** @todo breaks on IP/EIP/RIP wraparound tests in bs3-cpu-weird-1. See also iemNativeHlpReturnBreakViaLookup. */
            || !(pTbOrg->fFlags & IEMTB_F_CS_LIM_CHECKS)
# endif
           )
        {
            RTGCPHYS const GCPhysPcCurrent = iemNativeCallEntryToGCPhysPc(pTbOrg, pCallEntry);
            RTGCPHYS const GCPhysPcNext    = GCPhysPcCurrent + pCallEntry->cbOpcode + (int64_t)(a_fIsJump ? offJump : 0);
            if (   (GCPhysPcNext >> GUEST_PAGE_SHIFT) == (GCPhysPcCurrent >> GUEST_PAGE_SHIFT)
                && GUEST_PAGE_SIZE - (GCPhysPcCurrent & GUEST_PAGE_OFFSET_MASK) >= pCallEntry->cbOpcode /* 0xfff: je -56h */ )

            {
                /* Load the next GCPhysPc into the 3rd argument for the helper call. */
                off = iemNativeEmitLoadGprImm64(pReNative, off, IEMNATIVE_CALL_ARG3_GREG, GCPhysPcNext);

                /* Load the key lookup flags into the 2nd argument for the helper call.
                   - This is safe wrt CS limit checking since we're only here for FLAT modes.
                   - ASSUMING that this isn't a STI or POPF instruction, we can exclude any
                     interrupt shadow.
                   - The NMI inhibiting is more questionable, though... */
                /** @todo We don't implement NMI blocking atm, except via VT-x/AMD-V.
                 *        Should we copy it into fExec to simplify this? OTOH, it's just a
                 *        couple of extra instructions if EFLAGS are already in a register. */
                off = iemNativeEmitLoadGprImm64(pReNative, off, IEMNATIVE_CALL_ARG2_GREG,
                                                (pReNative->fExec & IEMTB_F_KEY_MASK) | IEMTB_F_TYPE_NATIVE);

                if (pReNative->idxLastCheckIrqCallNo != UINT32_MAX)
                    return iemNativeEmitTbExit<kIemNativeLabelType_ReturnBreakViaLookup>(pReNative, off);
                return iemNativeEmitTbExit<kIemNativeLabelType_ReturnBreakViaLookupWithIrq>(pReNative, off);
            }
        }
        if (pReNative->idxLastCheckIrqCallNo != UINT32_MAX)
            return iemNativeEmitTbExit<kIemNativeLabelType_ReturnBreakViaLookupWithTlb>(pReNative, off);
        return iemNativeEmitTbExit<kIemNativeLabelType_ReturnBreakViaLookupWithTlbAndIrq>(pReNative, off);
#endif
    }
    return off;
}


#define IEM_MC_ADVANCE_RIP_AND_FINISH_THREADED_PC64(a_cbInstr, a_rcNormal) \
    off = iemNativeEmitAddToRip64AndFinishingNoFlags(pReNative, off, (a_cbInstr)); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, false /*a_fIsJump*/>(pReNative, off, pCallEntry, 0)

#define IEM_MC_ADVANCE_RIP_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_cbInstr, a_rcNormal) \
    off = iemNativeEmitAddToRip64AndFinishingNoFlags(pReNative, off, (a_cbInstr)); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, false /*a_fIsJump*/>(pReNative, off, pCallEntry, 0)

/** Same as iemRegAddToRip64AndFinishingNoFlags. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitAddToRip64AndFinishingNoFlags(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t cbInstr)
{
#if !defined(IEMNATIVE_WITH_DELAYED_PC_UPDATING) || defined(IEMNATIVE_REG_FIXED_PC_DBG)
# if defined(IEMNATIVE_REG_FIXED_PC_DBG)
    if (!pReNative->Core.offPc)
        off = iemNativeEmitLoadGprFromVCpuU64(pReNative, off, IEMNATIVE_REG_FIXED_PC_DBG, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rip));
# endif

    /* Allocate a temporary PC register. */
    uint8_t const idxPcReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_Pc, kIemNativeGstRegUse_ForUpdate);

    /* Perform the addition and store the result. */
    off = iemNativeEmitAddGprImm8(pReNative, off, idxPcReg, cbInstr);
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxPcReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rip));

    /* Free but don't flush the PC register. */
    iemNativeRegFreeTmp(pReNative, idxPcReg);
#endif

#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
    pReNative->Core.offPc += cbInstr;
    Log4(("offPc=%#RX64 cbInstr=%#x off=%#x\n", pReNative->Core.offPc, cbInstr, off));
# ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING_DEBUG
    off = iemNativeEmitPcDebugAdd(pReNative, off, cbInstr, 64);
    off = iemNativeEmitPcDebugCheck(pReNative, off);
# elif defined(IEMNATIVE_REG_FIXED_PC_DBG)
    off = iemNativePcAdjustCheck(pReNative, off);
# endif
    STAM_COUNTER_INC(&pReNative->pVCpu->iem.s.StatNativePcUpdateTotal);
#endif

    return off;
}


#define IEM_MC_ADVANCE_RIP_AND_FINISH_THREADED_PC32(a_cbInstr, a_rcNormal) \
    off = iemNativeEmitAddToEip32AndFinishingNoFlags(pReNative, off, (a_cbInstr)); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, false /*a_fIsJump*/>(pReNative, off, pCallEntry, 0)

#define IEM_MC_ADVANCE_RIP_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_cbInstr, a_rcNormal) \
    off = iemNativeEmitAddToEip32AndFinishingNoFlags(pReNative, off, (a_cbInstr)); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, false /*a_fIsJump*/>(pReNative, off, pCallEntry, 0)

/** Same as iemRegAddToEip32AndFinishingNoFlags. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitAddToEip32AndFinishingNoFlags(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t cbInstr)
{
#if !defined(IEMNATIVE_WITH_DELAYED_PC_UPDATING) || defined(IEMNATIVE_REG_FIXED_PC_DBG)
# ifdef IEMNATIVE_REG_FIXED_PC_DBG
    if (!pReNative->Core.offPc)
        off = iemNativeEmitLoadGprFromVCpuU64(pReNative, off, IEMNATIVE_REG_FIXED_PC_DBG, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rip));
# endif

    /* Allocate a temporary PC register. */
    uint8_t const idxPcReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_Pc, kIemNativeGstRegUse_ForUpdate);

    /* Perform the addition and store the result. */
    off = iemNativeEmitAddGpr32Imm8(pReNative, off, idxPcReg, cbInstr);
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxPcReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rip));

    /* Free but don't flush the PC register. */
    iemNativeRegFreeTmp(pReNative, idxPcReg);
#endif

#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
    pReNative->Core.offPc += cbInstr;
    Log4(("offPc=%#RX64 cbInstr=%#x off=%#x\n", pReNative->Core.offPc, cbInstr, off));
# ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING_DEBUG
    off = iemNativeEmitPcDebugAdd(pReNative, off, cbInstr, 32);
    off = iemNativeEmitPcDebugCheck(pReNative, off);
# elif defined(IEMNATIVE_REG_FIXED_PC_DBG)
    off = iemNativePcAdjustCheck(pReNative, off);
# endif
    STAM_COUNTER_INC(&pReNative->pVCpu->iem.s.StatNativePcUpdateTotal);
#endif

    return off;
}


#define IEM_MC_ADVANCE_RIP_AND_FINISH_THREADED_PC16(a_cbInstr, a_rcNormal) \
    off = iemNativeEmitAddToIp16AndFinishingNoFlags(pReNative, off, (a_cbInstr)); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, false /*a_fIsJump*/>(pReNative, off, pCallEntry, 0)

#define IEM_MC_ADVANCE_RIP_AND_FINISH_THREADED_PC16_WITH_FLAGS(a_cbInstr, a_rcNormal) \
    off = iemNativeEmitAddToIp16AndFinishingNoFlags(pReNative, off, (a_cbInstr)); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, false /*a_fIsJump*/>(pReNative, off, pCallEntry, 0)

/** Same as iemRegAddToIp16AndFinishingNoFlags. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitAddToIp16AndFinishingNoFlags(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t cbInstr)
{
#if !defined(IEMNATIVE_WITH_DELAYED_PC_UPDATING) || defined(IEMNATIVE_REG_FIXED_PC_DBG)
# if defined(IEMNATIVE_REG_FIXED_PC_DBG)
    if (!pReNative->Core.offPc)
        off = iemNativeEmitLoadGprFromVCpuU64(pReNative, off, IEMNATIVE_REG_FIXED_PC_DBG, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rip));
# endif

    /* Allocate a temporary PC register. */
    uint8_t const idxPcReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_Pc, kIemNativeGstRegUse_ForUpdate);

    /* Perform the addition and store the result. */
    off = iemNativeEmitAddGpr32Imm8(pReNative, off, idxPcReg, cbInstr);
    off = iemNativeEmitClear16UpGpr(pReNative, off, idxPcReg);
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxPcReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rip));

    /* Free but don't flush the PC register. */
    iemNativeRegFreeTmp(pReNative, idxPcReg);
#endif

#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
    pReNative->Core.offPc += cbInstr;
    Log4(("offPc=%#RX64 cbInstr=%#x off=%#x\n", pReNative->Core.offPc, cbInstr, off));
# ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING_DEBUG
    off = iemNativeEmitPcDebugAdd(pReNative, off, cbInstr, 16);
    off = iemNativeEmitPcDebugCheck(pReNative, off);
# elif defined(IEMNATIVE_REG_FIXED_PC_DBG)
    off = iemNativePcAdjustCheck(pReNative, off);
# endif
    STAM_COUNTER_INC(&pReNative->pVCpu->iem.s.StatNativePcUpdateTotal);
#endif

    return off;
}


/*********************************************************************************************************************************
*   Common code for changing PC/RIP/EIP/IP.                                                                                      *
*********************************************************************************************************************************/

/**
 * Emits code to check if the content of @a idxAddrReg is a canonical address,
 * raising a \#GP(0) if it isn't.
 *
 * @returns New code buffer offset, UINT32_MAX on failure.
 * @param   pReNative       The native recompile state.
 * @param   off             The code buffer offset.
 * @param   idxAddrReg      The host register with the address to check.
 * @param   idxInstr        The current instruction.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitCheckGprCanonicalMaybeRaiseGp0(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxAddrReg, uint8_t idxInstr)
{
    /*
     * Make sure we don't have any outstanding guest register writes as we may
     * raise an #GP(0) and all guest register must be up to date in CPUMCTX.
     */
    off = iemNativeRegFlushPendingWrites(pReNative, off);

#ifdef IEMNATIVE_WITH_INSTRUCTION_COUNTING
    off = iemNativeEmitStoreImmToVCpuU8(pReNative, off, idxInstr, RT_UOFFSETOF(VMCPUCC, iem.s.idxTbCurInstr));
#else
    RT_NOREF(idxInstr);
#endif

#ifdef RT_ARCH_AMD64
    /*
     * if ((((uint32_t)(a_u64Addr >> 32) + UINT32_C(0x8000)) >> 16) != 0)
     *     return raisexcpt();
     * ---- this variant avoid loading a 64-bit immediate, but is an instruction longer.
     */
    uint8_t const iTmpReg = iemNativeRegAllocTmp(pReNative, &off);

    off = iemNativeEmitLoadGprFromGpr(pReNative, off, iTmpReg, idxAddrReg);
    off = iemNativeEmitShiftGprRight(pReNative, off, iTmpReg, 32);
    off = iemNativeEmitAddGpr32Imm(pReNative, off, iTmpReg, (int32_t)0x8000);
    off = iemNativeEmitShiftGprRight(pReNative, off, iTmpReg, 16);
    off = iemNativeEmitTbExitJnz<kIemNativeLabelType_RaiseGp0>(pReNative, off);

    iemNativeRegFreeTmp(pReNative, iTmpReg);

#elif defined(RT_ARCH_ARM64)
    /*
     * if ((((uint64_t)(a_u64Addr) + UINT64_C(0x800000000000)) >> 48) != 0)
     *     return raisexcpt();
     * ----
     *     mov     x1, 0x800000000000
     *     add     x1, x0, x1
     *     cmp     xzr, x1, lsr 48
     *     b.ne    .Lraisexcpt
     */
    uint8_t const iTmpReg = iemNativeRegAllocTmp(pReNative, &off);

    off = iemNativeEmitLoadGprImm64(pReNative, off, iTmpReg, UINT64_C(0x800000000000));
    off = iemNativeEmitAddTwoGprs(pReNative, off, iTmpReg, idxAddrReg);
    off = iemNativeEmitCmpArm64(pReNative, off, ARMV8_A64_REG_XZR, iTmpReg, true /*f64Bit*/, 48 /*cShift*/, kArmv8A64InstrShift_Lsr);
    off = iemNativeEmitTbExitJnz<kIemNativeLabelType_RaiseGp0>(pReNative, off);

    iemNativeRegFreeTmp(pReNative, iTmpReg);

#else
# error "Port me"
#endif
    return off;
}


/**
 * Emits code to check if the content of @a idxAddrReg is a canonical address,
 * raising a \#GP(0) if it isn't.
 *
 * Caller makes sure everything is flushed, except maybe PC.
 *
 * @returns New code buffer offset, UINT32_MAX on failure.
 * @param   pReNative       The native recompile state.
 * @param   off             The code buffer offset.
 * @param   idxAddrReg      The host register with the address to check.
 * @param   offDisp         The relative displacement that has already been
 *                          added to idxAddrReg and must be subtracted if
 *                          raising a \#GP(0).
 * @param   idxInstr        The current instruction.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitCheckGprCanonicalMaybeRaiseGp0WithDisp(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                    uint8_t idxAddrReg, int64_t offDisp, uint8_t idxInstr)
{
#ifdef IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK
    Assert(pReNative->Core.bmGstRegShadowDirty == 0);
#endif

#ifdef RT_ARCH_AMD64
    /*
     * if ((((uint32_t)(a_u64Addr >> 32) + UINT32_C(0x8000)) >> 16) != 0)
     *     return raisexcpt();
     * ---- this variant avoid loading a 64-bit immediate, but is an instruction longer.
     */
    uint8_t const iTmpReg = iemNativeRegAllocTmp(pReNative, &off);

    off = iemNativeEmitLoadGprFromGpr(pReNative, off, iTmpReg, idxAddrReg);
    off = iemNativeEmitShiftGprRight(pReNative, off, iTmpReg, 32);
    off = iemNativeEmitAddGpr32Imm(pReNative, off, iTmpReg, (int32_t)0x8000);
    off = iemNativeEmitShiftGprRight(pReNative, off, iTmpReg, 16);

#elif defined(RT_ARCH_ARM64)
    /*
     * if ((((uint64_t)(a_u64Addr) + UINT64_C(0x800000000000)) >> 48) != 0)
     *     return raisexcpt();
     * ----
     *     mov     x1, 0x800000000000
     *     add     x1, x0, x1
     *     cmp     xzr, x1, lsr 48
     *     b.ne    .Lraisexcpt
     */
    uint8_t const iTmpReg = iemNativeRegAllocTmp(pReNative, &off);

    off = iemNativeEmitLoadGprImm64(pReNative, off, iTmpReg, UINT64_C(0x800000000000));
    off = iemNativeEmitAddTwoGprs(pReNative, off, iTmpReg, idxAddrReg);
    off = iemNativeEmitCmpArm64(pReNative, off, ARMV8_A64_REG_XZR, iTmpReg, true /*f64Bit*/, 48 /*cShift*/, kArmv8A64InstrShift_Lsr);
#else
# error "Port me"
#endif

    /* Jump to the #GP code (hoping static prediction considers forward branches as not-taken). */
    uint32_t const offFixup1 = off;
    off = iemNativeEmitJnzToFixed(pReNative, off, off /*8-bit jump suffices*/);

    /* jump .Lnoexcept;  Skip the #GP code.  */
    uint32_t const offFixup2 = off;
    off = iemNativeEmitJmpToFixed(pReNative, off, off /*8-bit jump suffices*/);

    /* .Lraisexcpt: */
    iemNativeFixupFixedJump(pReNative, offFixup1, off);
#ifdef IEMNATIVE_WITH_INSTRUCTION_COUNTING
    off = iemNativeEmitStoreImmToVCpuU8(pReNative, off, idxInstr, RT_UOFFSETOF(VMCPUCC, iem.s.idxTbCurInstr), iTmpReg);
#else
    RT_NOREF(idxInstr);
#endif

    /* Undo the PC adjustment and store the old PC value. */
    off = iemNativeEmitSubGprImm(pReNative, off, idxAddrReg, offDisp, iTmpReg);
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxAddrReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rip));

    off = iemNativeEmitTbExit<kIemNativeLabelType_RaiseGp0, false /*a_fActuallyExitingTb*/>(pReNative, off);

    /* .Lnoexcept: */
    iemNativeFixupFixedJump(pReNative, offFixup2, off);

    iemNativeRegFreeTmp(pReNative, iTmpReg);
    return off;
}


/**
 * Emits code to check if the content of @a idxAddrReg is a canonical address,
 * raising a \#GP(0) if it isn't.
 *
 * Caller makes sure everything is flushed, except maybe PC.
 *
 * @returns New code buffer offset, UINT32_MAX on failure.
 * @param   pReNative       The native recompile state.
 * @param   off             The code buffer offset.
 * @param   idxAddrReg      The host register with the address to check.
 * @param   idxOldPcReg     Register holding the old PC that offPc is relative
 *                          to if available, otherwise UINT8_MAX.
 * @param   idxInstr        The current instruction.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitCheckGprCanonicalMaybeRaiseGp0WithOldPc(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                     uint8_t idxAddrReg, uint8_t idxOldPcReg, uint8_t idxInstr)
{
#ifdef IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK
    Assert(pReNative->Core.bmGstRegShadowDirty == 0);
#endif

#ifdef IEMNATIVE_WITH_INSTRUCTION_COUNTING
# ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
    if (!pReNative->Core.offPc)
# endif
        off = iemNativeEmitStoreImmToVCpuU8(pReNative, off, idxInstr, RT_UOFFSETOF(VMCPUCC, iem.s.idxTbCurInstr));
#else
    RT_NOREF(idxInstr);
#endif

#ifdef RT_ARCH_AMD64
    /*
     * if ((((uint32_t)(a_u64Addr >> 32) + UINT32_C(0x8000)) >> 16) != 0)
     *     return raisexcpt();
     * ---- this variant avoid loading a 64-bit immediate, but is an instruction longer.
     */
    uint8_t const iTmpReg = iemNativeRegAllocTmp(pReNative, &off);

    off = iemNativeEmitLoadGprFromGpr(pReNative, off, iTmpReg, idxAddrReg);
    off = iemNativeEmitShiftGprRight(pReNative, off, iTmpReg, 32);
    off = iemNativeEmitAddGpr32Imm(pReNative, off, iTmpReg, (int32_t)0x8000);
    off = iemNativeEmitShiftGprRight(pReNative, off, iTmpReg, 16);

#elif defined(RT_ARCH_ARM64)
    /*
     * if ((((uint64_t)(a_u64Addr) + UINT64_C(0x800000000000)) >> 48) != 0)
     *     return raisexcpt();
     * ----
     *     mov     x1, 0x800000000000
     *     add     x1, x0, x1
     *     cmp     xzr, x1, lsr 48
     *     b.ne    .Lraisexcpt
     */
    uint8_t const iTmpReg = iemNativeRegAllocTmp(pReNative, &off);

    off = iemNativeEmitLoadGprImm64(pReNative, off, iTmpReg, UINT64_C(0x800000000000));
    off = iemNativeEmitAddTwoGprs(pReNative, off, iTmpReg, idxAddrReg);
    off = iemNativeEmitCmpArm64(pReNative, off, ARMV8_A64_REG_XZR, iTmpReg, true /*f64Bit*/, 48 /*cShift*/, kArmv8A64InstrShift_Lsr);
#else
# error "Port me"
#endif

#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
    if (pReNative->Core.offPc)
    {
        /** @todo On x86, it is said that conditional jumps forward are statically
         *        predicited as not taken, so this isn't a very good construct.
         *        Investigate whether it makes sense to invert it and add another
         *        jump.  Also, find out wtf the static predictor does here on arm! */
        uint32_t const offFixup = off;
        off = iemNativeEmitJzToFixed(pReNative, off, off + 16 /*8-bit suffices*/);

        /* .Lraisexcpt: */
# ifdef IEMNATIVE_WITH_INSTRUCTION_COUNTING
        off = iemNativeEmitStoreImmToVCpuU8(pReNative, off, idxInstr, RT_UOFFSETOF(VMCPUCC, iem.s.idxTbCurInstr), iTmpReg);
# endif
        /* We need to update cpum.GstCtx.rip. */
        if (idxOldPcReg == UINT8_MAX)
        {
            idxOldPcReg = iTmpReg;
            off = iemNativeEmitLoadGprFromVCpuU64(pReNative, off, idxOldPcReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rip));
        }
        off = iemNativeEmitAddGprImm(pReNative, off, idxOldPcReg, pReNative->Core.offPc);
        off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxOldPcReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rip));

        off = iemNativeEmitTbExit<kIemNativeLabelType_RaiseGp0, false /*a_fActuallyExitingTb*/>(pReNative, off);
        iemNativeFixupFixedJump(pReNative, offFixup, off);
    }
    else
#endif
        off = iemNativeEmitTbExitJnz<kIemNativeLabelType_RaiseGp0>(pReNative, off);

    iemNativeRegFreeTmp(pReNative, iTmpReg);

    return off;
}


/**
 * Emits code to check if that the content of @a idxAddrReg is within the limit
 * of CS, raising a \#GP(0) if it isn't.
 *
 * @returns New code buffer offset; throws VBox status code on error.
 * @param   pReNative       The native recompile state.
 * @param   off             The code buffer offset.
 * @param   idxAddrReg      The host register (32-bit) with the address to
 *                          check.
 * @param   idxInstr        The current instruction.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitCheckGpr32AgainstCsSegLimitMaybeRaiseGp0(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                      uint8_t idxAddrReg, uint8_t idxInstr)
{
    /*
     * Make sure we don't have any outstanding guest register writes as we may
     * raise an #GP(0) and all guest register must be up to date in CPUMCTX.
     */
    off = iemNativeRegFlushPendingWrites(pReNative, off);

#ifdef IEMNATIVE_WITH_INSTRUCTION_COUNTING
    off = iemNativeEmitStoreImmToVCpuU8(pReNative, off, idxInstr, RT_UOFFSETOF(VMCPUCC, iem.s.idxTbCurInstr));
#else
    RT_NOREF(idxInstr);
#endif

    uint8_t const idxRegCsLim = iemNativeRegAllocTmpForGuestReg(pReNative, &off,
                                                                (IEMNATIVEGSTREG)(kIemNativeGstReg_SegLimitFirst + X86_SREG_CS),
                                                                kIemNativeGstRegUse_ReadOnly);

    off = iemNativeEmitCmpGpr32WithGpr(pReNative, off, idxAddrReg, idxRegCsLim);
    off = iemNativeEmitTbExitJa<kIemNativeLabelType_RaiseGp0>(pReNative, off);

    iemNativeRegFreeTmp(pReNative, idxRegCsLim);
    return off;
}




/**
 * Emits code to check if that the content of @a idxAddrReg is within the limit
 * of CS, raising a \#GP(0) if it isn't.
 *
 * Caller makes sure everything is flushed, except maybe PC.
 *
 * @returns New code buffer offset; throws VBox status code on error.
 * @param   pReNative       The native recompile state.
 * @param   off             The code buffer offset.
 * @param   idxAddrReg      The host register (32-bit) with the address to
 *                          check.
 * @param   idxOldPcReg     Register holding the old PC that offPc is relative
 *                          to if available, otherwise UINT8_MAX.
 * @param   idxInstr        The current instruction.
 */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitCheckGpr32AgainstCsSegLimitMaybeRaiseGp0WithOldPc(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                               uint8_t idxAddrReg, uint8_t idxOldPcReg, uint8_t idxInstr)
{
#ifdef IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK
    Assert(pReNative->Core.bmGstRegShadowDirty == 0);
#endif

#ifdef IEMNATIVE_WITH_INSTRUCTION_COUNTING
# ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
    if (!pReNative->Core.offPc)
# endif
        off = iemNativeEmitStoreImmToVCpuU8(pReNative, off, idxInstr, RT_UOFFSETOF(VMCPUCC, iem.s.idxTbCurInstr));
#else
    RT_NOREF(idxInstr);
#endif

    uint8_t const idxRegCsLim = iemNativeRegAllocTmpForGuestReg(pReNative, &off,
                                                                (IEMNATIVEGSTREG)(kIemNativeGstReg_SegLimitFirst + X86_SREG_CS),
                                                                kIemNativeGstRegUse_ReadOnly);

    off = iemNativeEmitCmpGpr32WithGpr(pReNative, off, idxAddrReg, idxRegCsLim);
#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
    if (pReNative->Core.offPc)
    {
        uint32_t const offFixup = off;
        off = iemNativeEmitJbeToFixed(pReNative, off, off + 16 /*8-bit suffices*/);

        /* Raising a GP(0), but first we need to update cpum.GstCtx.rip. */
        if (idxOldPcReg == UINT8_MAX)
        {
            idxOldPcReg = idxAddrReg;
            off = iemNativeEmitLoadGprFromVCpuU64(pReNative, off, idxOldPcReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rip));
        }
        off = iemNativeEmitAddGprImm(pReNative, off, idxOldPcReg, pReNative->Core.offPc);
        off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxOldPcReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rip));
# ifdef IEMNATIVE_WITH_INSTRUCTION_COUNTING
        off = iemNativeEmitStoreImmToVCpuU8(pReNative, off, idxInstr, RT_UOFFSETOF(VMCPUCC, iem.s.idxTbCurInstr));
# endif
        off = iemNativeEmitTbExit<kIemNativeLabelType_RaiseGp0, false /*a_fActuallyExitingTb*/>(pReNative, off);
        iemNativeFixupFixedJump(pReNative, offFixup, off);
    }
    else
#endif
        off = iemNativeEmitTbExitJa<kIemNativeLabelType_RaiseGp0>(pReNative, off);

    iemNativeRegFreeTmp(pReNative, idxRegCsLim);
    return off;
}


/*********************************************************************************************************************************
*   Emitters for changing PC/RIP/EIP/IP with a relative jump (IEM_MC_REL_JMP_XXX_AND_FINISH_XXX).                                *
*********************************************************************************************************************************/

#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC64(a_i8, a_cbInstr, a_enmEffOpSize, a_rcNormal) \
    off = iemNativeEmitRip64RelativeJumpAndFinishingNoFlags<false>(pReNative, off, (a_cbInstr), (int8_t)(a_i8), \
                                                                   (a_enmEffOpSize), pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (int8_t)(a_i8))

#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_i8, a_cbInstr, a_enmEffOpSize, a_rcNormal) \
    off = iemNativeEmitRip64RelativeJumpAndFinishingNoFlags<false>(pReNative, off, (a_cbInstr), (int8_t)(a_i8), \
                                                                   (a_enmEffOpSize), pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (int8_t)(a_i8))

#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC64(a_i16, a_cbInstr, a_rcNormal) \
    off = iemNativeEmitRip64RelativeJumpAndFinishingNoFlags<false>(pReNative, off, (a_cbInstr), (int16_t)(a_i16), \
                                                                   IEMMODE_16BIT, pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (int16_t)(a_i16))

#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_i16, a_cbInstr, a_rcNormal) \
        off = iemNativeEmitRip64RelativeJumpAndFinishingNoFlags<false>(pReNative, off, (a_cbInstr), (int16_t)(a_i16), \
                                                                       IEMMODE_16BIT, pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (int16_t)(a_i16))

#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC64(a_i32, a_cbInstr, a_rcNormal) \
    off = iemNativeEmitRip64RelativeJumpAndFinishingNoFlags<false>(pReNative, off, (a_cbInstr), (a_i32), \
                                                                   IEMMODE_64BIT, pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (a_i32))

#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_i32, a_cbInstr, a_rcNormal) \
    off = iemNativeEmitRip64RelativeJumpAndFinishingNoFlags<false>(pReNative, off, (a_cbInstr), (a_i32), \
                                                                   IEMMODE_64BIT, pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (a_i32))


#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC64_INTRAPG(a_i8, a_cbInstr, a_enmEffOpSize, a_rcNormal) \
    off = iemNativeEmitRip64RelativeJumpAndFinishingNoFlags<true>(pReNative, off, (a_cbInstr), (int8_t)(a_i8), \
                                                                  (a_enmEffOpSize), pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (int8_t)(a_i8))

#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC64_INTRAPG_WITH_FLAGS(a_i8, a_cbInstr, a_enmEffOpSize, a_rcNormal) \
    off = iemNativeEmitRip64RelativeJumpAndFinishingNoFlags<true>(pReNative, off, (a_cbInstr), (int8_t)(a_i8), \
                                                                  (a_enmEffOpSize), pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (int8_t)(a_i8))

#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC64_INTRAPG(a_i16, a_cbInstr, a_rcNormal) \
    off = iemNativeEmitRip64RelativeJumpAndFinishingNoFlags<true>(pReNative, off, (a_cbInstr), (int16_t)(a_i16), \
                                                                  IEMMODE_16BIT, pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (int16_t)(a_i16))

#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC64_INTRAPG_WITH_FLAGS(a_i16, a_cbInstr, a_rcNormal) \
    off = iemNativeEmitRip64RelativeJumpAndFinishingNoFlags<true>(pReNative, off, (a_cbInstr), (int16_t)(a_i16), \
                                                                  IEMMODE_16BIT, pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (int16_t)(a_i16))

#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC64_INTRAPG(a_i32, a_cbInstr, a_rcNormal) \
    off = iemNativeEmitRip64RelativeJumpAndFinishingNoFlags<true>(pReNative, off, (a_cbInstr), (a_i32), \
                                                                  IEMMODE_64BIT, pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (a_i32))

#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC64_INTRAPG_WITH_FLAGS(a_i32, a_cbInstr, a_rcNormal) \
    off = iemNativeEmitRip64RelativeJumpAndFinishingNoFlags<true>(pReNative, off, (a_cbInstr), (a_i32), \
                                                                  IEMMODE_64BIT, pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (a_i32))

/** Same as iemRegRip64RelativeJumpS8AndFinishNoFlags,
 *  iemRegRip64RelativeJumpS16AndFinishNoFlags and
 *  iemRegRip64RelativeJumpS32AndFinishNoFlags. */
template<bool const a_fWithinPage>
DECL_INLINE_THROW(uint32_t)
iemNativeEmitRip64RelativeJumpAndFinishingNoFlags(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t cbInstr,
                                                  int32_t offDisp, IEMMODE enmEffOpSize, uint8_t idxInstr)
{
    Assert(enmEffOpSize == IEMMODE_64BIT || enmEffOpSize == IEMMODE_16BIT);
#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
    STAM_COUNTER_INC(&pReNative->pVCpu->iem.s.StatNativePcUpdateTotal);
    if (a_fWithinPage && enmEffOpSize == IEMMODE_64BIT)
    {
        /* No #GP checking required, just update offPc and get on with it. */
        pReNative->Core.offPc += (int64_t)offDisp + cbInstr;
# ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING_DEBUG
        off = iemNativeEmitPcDebugAdd(pReNative, off, (int64_t)offDisp + cbInstr, enmEffOpSize == IEMMODE_64BIT ? 64 : 16);
# endif
    }
    else
#endif
    {
        /* Flush all but PC iff we're doing a 64-bit update here and this isn't within a page.. */
        if (RT_LIKELY(enmEffOpSize == IEMMODE_64BIT && !a_fWithinPage))
            off = iemNativeRegFlushPendingWrites(pReNative, off, RT_BIT_64(kIemNativeGstReg_Pc) /*fGstShwExcept*/);

        /* Allocate a temporary PC register. */
        uint8_t const idxPcReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_Pc,
                                                                 kIemNativeGstRegUse_ForUpdate);

        /* Perform the addition. */
        off = iemNativeEmitAddGprImm(pReNative, off, idxPcReg, (int64_t)offDisp + cbInstr + pReNative->Core.offPc);

        if (RT_LIKELY(enmEffOpSize == IEMMODE_64BIT))
        {
            /* Check that the address is canonical, raising #GP(0) + exit TB if it isn't.
               We can skip this if the target is within the same page. */
            if (!a_fWithinPage)
                off = iemNativeEmitCheckGprCanonicalMaybeRaiseGp0WithDisp(pReNative, off, idxPcReg,
                                                                          (int64_t)offDisp + cbInstr, idxInstr);
        }
        else
        {
            /* Just truncate the result to 16-bit IP. */
            Assert(enmEffOpSize == IEMMODE_16BIT);
            off = iemNativeEmitClear16UpGpr(pReNative, off, idxPcReg);
        }

#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
# ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING_DEBUG
        off = iemNativeEmitPcDebugAdd(pReNative, off, (int64_t)offDisp + cbInstr, enmEffOpSize == IEMMODE_64BIT ? 64 : 16);
        off = iemNativeEmitPcDebugCheckWithReg(pReNative, off, idxPcReg);
# endif
        /* Since we've already got the new PC value in idxPcReg, we can just as
           well write it out and reset offPc to zero.  Otherwise, we'd need to use
           a copy the shadow PC, which will cost another move instruction here. */
# if defined(IEMNATIVE_WITH_TB_DEBUG_INFO) || defined(LOG_ENABLED) || defined(VBOX_WITH_STATISTICS)
        uint8_t const idxOldInstrPlusOne = pReNative->idxInstrPlusOneOfLastPcUpdate;
        pReNative->idxInstrPlusOneOfLastPcUpdate = RT_MAX(idxInstr + 1, idxOldInstrPlusOne);
        uint8_t const cInstrsSkipped     = idxInstr <= idxOldInstrPlusOne ? 0 : idxInstr - idxOldInstrPlusOne;
        Log4(("iemNativeEmitRip64RelativeJumpAndFinishingNoFlags: offPc=%#RX64 -> 0; off=%#x; idxInstr=%u cInstrsSkipped=%u cCondDepth=%d\n",
              pReNative->Core.offPc, off, idxInstr, cInstrsSkipped, pReNative->cCondDepth));
        STAM_COUNTER_ADD(&pReNative->pVCpu->iem.s.StatNativePcUpdateDelayed, cInstrsSkipped);
#  ifdef IEMNATIVE_WITH_TB_DEBUG_INFO
        iemNativeDbgInfoAddNativeOffset(pReNative, off);
        iemNativeDbgInfoAddDelayedPcUpdate(pReNative, pReNative->Core.offPc, cInstrsSkipped);
#  endif
# endif
        pReNative->Core.offPc = 0;
#endif

        off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxPcReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rip));

        /* Free but don't flush the PC register. */
        iemNativeRegFreeTmp(pReNative, idxPcReg);
    }
    return off;
}


#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC32(a_i8, a_cbInstr, a_enmEffOpSize, a_rcNormal) \
    off = iemNativeEmitEip32RelativeJumpAndFinishingNoFlags<false>(pReNative, off, (a_cbInstr), (int8_t)(a_i8), \
                                                                   (a_enmEffOpSize), pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (int8_t)(a_i8))

#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_i8, a_cbInstr, a_enmEffOpSize, a_rcNormal) \
    off = iemNativeEmitEip32RelativeJumpAndFinishingNoFlags<false>(pReNative, off, (a_cbInstr), (int8_t)(a_i8), \
                                                                   (a_enmEffOpSize), pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (int8_t)(a_i8))

#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC32(a_i16, a_cbInstr, a_rcNormal) \
    off = iemNativeEmitEip32RelativeJumpAndFinishingNoFlags<false>(pReNative, off, (a_cbInstr), (int16_t)(a_i16), \
                                                                   IEMMODE_16BIT, pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (int16_t)(a_i16))

#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_i16, a_cbInstr, a_rcNormal) \
    off = iemNativeEmitEip32RelativeJumpAndFinishingNoFlags<false>(pReNative, off, (a_cbInstr), (int16_t)(a_i16), \
                                                                   IEMMODE_16BIT, pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (int16_t)(a_i16))

#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC32(a_i32, a_cbInstr, a_rcNormal) \
    off = iemNativeEmitEip32RelativeJumpAndFinishingNoFlags<false>(pReNative, off, (a_cbInstr), (a_i32), \
                                                                   IEMMODE_32BIT, pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (a_i32))

#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_i32, a_cbInstr, a_rcNormal) \
    off = iemNativeEmitEip32RelativeJumpAndFinishingNoFlags<false>(pReNative, off, (a_cbInstr), (a_i32), \
                                                                   IEMMODE_32BIT, pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (a_i32))


#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC32_FLAT(a_i8, a_cbInstr, a_enmEffOpSize, a_rcNormal) \
    off = iemNativeEmitEip32RelativeJumpAndFinishingNoFlags<true>(pReNative, off, (a_cbInstr), (int8_t)(a_i8), \
                                                                  (a_enmEffOpSize), pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (int8_t)(a_i8))

#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC32_FLAT_WITH_FLAGS(a_i8, a_cbInstr, a_enmEffOpSize, a_rcNormal) \
    off = iemNativeEmitEip32RelativeJumpAndFinishingNoFlags<true>(pReNative, off, (a_cbInstr), (int8_t)(a_i8), \
                                                                  (a_enmEffOpSize), pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (int8_t)(a_i8))

#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC32_FLAT(a_i16, a_cbInstr, a_rcNormal) \
    off = iemNativeEmitEip32RelativeJumpAndFinishingNoFlags<true>(pReNative, off, (a_cbInstr), (int16_t)(a_i16), \
                                                                  IEMMODE_16BIT, pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (int16_t)(a_i16))

#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC32_FLAT_WITH_FLAGS(a_i16, a_cbInstr, a_rcNormal) \
    off = iemNativeEmitEip32RelativeJumpAndFinishingNoFlags<true>(pReNative, off, (a_cbInstr), (int16_t)(a_i16), \
                                                                  IEMMODE_16BIT, pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (int16_t)(a_i16))

#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC32_FLAT(a_i32, a_cbInstr, a_rcNormal) \
    off = iemNativeEmitEip32RelativeJumpAndFinishingNoFlags<true>(pReNative, off, (a_cbInstr), (a_i32), \
                                                                  IEMMODE_32BIT, pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (a_i32))

#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC32_FLAT_WITH_FLAGS(a_i32, a_cbInstr, a_rcNormal) \
    off = iemNativeEmitEip32RelativeJumpAndFinishingNoFlags<true>(pReNative, off, (a_cbInstr), (a_i32), \
                                                                  IEMMODE_32BIT, pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (a_i32))

/** Same as iemRegEip32RelativeJumpS8AndFinishNoFlags,
 *  iemRegEip32RelativeJumpS16AndFinishNoFlags and
 *  iemRegEip32RelativeJumpS32AndFinishNoFlags. */
template<bool const a_fFlat>
DECL_INLINE_THROW(uint32_t)
iemNativeEmitEip32RelativeJumpAndFinishingNoFlags(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t cbInstr,
                                                  int32_t offDisp, IEMMODE enmEffOpSize, uint8_t idxInstr)
{
    Assert(enmEffOpSize == IEMMODE_32BIT || enmEffOpSize == IEMMODE_16BIT);
#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
    STAM_COUNTER_INC(&pReNative->pVCpu->iem.s.StatNativePcUpdateTotal);
#endif

    /* We speculatively modify PC and may raise #GP(0), so make sure the right values are in CPUMCTX. */
    if (!a_fFlat || enmEffOpSize == IEMMODE_16BIT)
    {
        off = iemNativeRegFlushPendingWrites(pReNative, off);
#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
        Assert(pReNative->Core.offPc == 0);
#endif
    }

    /* Allocate a temporary PC register. */
    uint8_t const idxPcReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_Pc, kIemNativeGstRegUse_ForUpdate);

    /* Perform the addition. */
#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
    off = iemNativeEmitAddGpr32Imm(pReNative, off, idxPcReg, offDisp + cbInstr + (int32_t)pReNative->Core.offPc);
#else
    off = iemNativeEmitAddGpr32Imm(pReNative, off, idxPcReg, offDisp + cbInstr + (int32_t)pReNative->Core.offPc);
#endif

    /* Truncate the result to 16-bit IP if the operand size is 16-bit. */
    if (enmEffOpSize == IEMMODE_16BIT)
        off = iemNativeEmitClear16UpGpr(pReNative, off, idxPcReg);

    /* Perform limit checking, potentially raising #GP(0) and exit the TB. */
    if (!a_fFlat)
        off = iemNativeEmitCheckGpr32AgainstCsSegLimitMaybeRaiseGp0(pReNative, off, idxPcReg, idxInstr);

    /* Commit it. */
#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING_DEBUG
    off = iemNativeEmitPcDebugAdd(pReNative, off, offDisp + cbInstr, enmEffOpSize == IEMMODE_32BIT ? 32 : 16);
    off = iemNativeEmitPcDebugCheckWithReg(pReNative, off, idxPcReg);
#endif

    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxPcReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rip));
#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
    pReNative->Core.offPc = 0;
#endif

    /* Free but don't flush the PC register. */
    iemNativeRegFreeTmp(pReNative, idxPcReg);

    return off;
}


#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC16(a_i8, a_cbInstr, a_rcNormal) \
    off = iemNativeEmitIp16RelativeJumpAndFinishingNoFlags(pReNative, off, (a_cbInstr), (int8_t)(a_i8), pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (int8_t)(a_i8))

#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC16_WITH_FLAGS(a_i8, a_cbInstr, a_rcNormal) \
    off = iemNativeEmitIp16RelativeJumpAndFinishingNoFlags(pReNative, off, (a_cbInstr), (int8_t)(a_i8), pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (int8_t)(a_i8))

#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC16(a_i16, a_cbInstr, a_rcNormal) \
    off = iemNativeEmitIp16RelativeJumpAndFinishingNoFlags(pReNative, off, (a_cbInstr), (int16_t)(a_i16), pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (int16_t)(a_i16))

#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC16_WITH_FLAGS(a_i16, a_cbInstr, a_rcNormal) \
    off = iemNativeEmitIp16RelativeJumpAndFinishingNoFlags(pReNative, off, (a_cbInstr), (int16_t)(a_i16), pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, (int16_t)(a_i16))

#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC16(a_i32, a_cbInstr, a_rcNormal) \
    off = iemNativeEmitIp16RelativeJumpAndFinishingNoFlags(pReNative, off, (a_cbInstr), (a_i32), pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, a_i32)

#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC16_WITH_FLAGS(a_i32, a_cbInstr, a_rcNormal) \
    off = iemNativeEmitIp16RelativeJumpAndFinishingNoFlags(pReNative, off, (a_cbInstr), (a_i32), pCallEntry->idxInstr); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off); \
    off = iemNativeEmitFinishInstructionWithStatus<a_rcNormal, true /*a_fIsJump*/>(pReNative, off, pCallEntry, a_i32)

/** Same as iemRegIp16RelativeJumpS8AndFinishNoFlags. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitIp16RelativeJumpAndFinishingNoFlags(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                                 uint8_t cbInstr, int32_t offDisp, uint8_t idxInstr)
{
    /* We speculatively modify PC and may raise #GP(0), so make sure the right values are in CPUMCTX. */
    off = iemNativeRegFlushPendingWrites(pReNative, off);

#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
    Assert(pReNative->Core.offPc == 0);
    STAM_COUNTER_INC(&pReNative->pVCpu->iem.s.StatNativePcUpdateTotal);
#endif

    /* Allocate a temporary PC register. */
    uint8_t const idxPcReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_Pc, kIemNativeGstRegUse_ForUpdate);

    /* Perform the addition, clamp the result, check limit (may #GP(0) + exit TB) and store the result. */
    off = iemNativeEmitAddGpr32Imm(pReNative, off, idxPcReg, offDisp + cbInstr);
    off = iemNativeEmitClear16UpGpr(pReNative, off, idxPcReg);
    off = iemNativeEmitCheckGpr32AgainstCsSegLimitMaybeRaiseGp0(pReNative, off, idxPcReg, idxInstr);
#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING_DEBUG
    off = iemNativeEmitPcDebugAdd(pReNative, off, offDisp + cbInstr, 16);
    off = iemNativeEmitPcDebugCheckWithReg(pReNative, off, idxPcReg);
#endif
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxPcReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rip));

    /* Free but don't flush the PC register. */
    iemNativeRegFreeTmp(pReNative, idxPcReg);

    return off;
}



/*********************************************************************************************************************************
*   Emitters for changing PC/RIP/EIP/IP with a indirect jump (IEM_MC_SET_RIP_UXX_AND_FINISH).                                    *
*********************************************************************************************************************************/

/** Variant of IEM_MC_SET_RIP_U16_AND_FINISH for pre-386 targets. */
#define IEM_MC_SET_RIP_U16_AND_FINISH_THREADED_PC16(a_u16NewIP) \
    off = iemNativeEmitRipJumpNoFlags(pReNative, off, (a_u16NewIP), false /*f64Bit*/, pCallEntry->idxInstr, sizeof(uint16_t))

/** Variant of IEM_MC_SET_RIP_U16_AND_FINISH for 386+ targets. */
#define IEM_MC_SET_RIP_U16_AND_FINISH_THREADED_PC32(a_u16NewIP) \
    off = iemNativeEmitRipJumpNoFlags(pReNative, off, (a_u16NewIP), false /*f64Bit*/, pCallEntry->idxInstr, sizeof(uint16_t))

/** Variant of IEM_MC_SET_RIP_U16_AND_FINISH for use in 64-bit code. */
#define IEM_MC_SET_RIP_U16_AND_FINISH_THREADED_PC64(a_u16NewIP) \
    off = iemNativeEmitRipJumpNoFlags(pReNative, off, (a_u16NewIP),  true /*f64Bit*/, pCallEntry->idxInstr, sizeof(uint16_t))

/** Variant of IEM_MC_SET_RIP_U16_AND_FINISH for pre-386 targets that checks and
 *  clears flags. */
#define IEM_MC_SET_RIP_U16_AND_FINISH_THREADED_PC16_WITH_FLAGS(a_u16NewIP) \
    IEM_MC_SET_RIP_U16_AND_FINISH_THREADED_PC16(a_u16NewIP); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off)

/** Variant of IEM_MC_SET_RIP_U16_AND_FINISH for 386+ targets that checks and
 *  clears flags. */
#define IEM_MC_SET_RIP_U16_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_u16NewIP) \
    IEM_MC_SET_RIP_U16_AND_FINISH_THREADED_PC32(a_u16NewIP); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off)

/** Variant of IEM_MC_SET_RIP_U16_AND_FINISH for use in 64-bit code that checks and
 *  clears flags. */
#define IEM_MC_SET_RIP_U16_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_u16NewIP) \
    IEM_MC_SET_RIP_U16_AND_FINISH_THREADED_PC64(a_u16NewIP); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off)

#undef IEM_MC_SET_RIP_U16_AND_FINISH


/** Variant of IEM_MC_SET_RIP_U32_AND_FINISH for 386+ targets. */
#define IEM_MC_SET_RIP_U32_AND_FINISH_THREADED_PC32(a_u32NewEIP) \
    off = iemNativeEmitRipJumpNoFlags(pReNative, off, (a_u32NewEIP), false /*f64Bit*/, pCallEntry->idxInstr, sizeof(uint32_t))

/** Variant of IEM_MC_SET_RIP_U32_AND_FINISH for use in 64-bit code. */
#define IEM_MC_SET_RIP_U32_AND_FINISH_THREADED_PC64(a_u32NewEIP) \
    off = iemNativeEmitRipJumpNoFlags(pReNative, off, (a_u32NewEIP),  true /*f64Bit*/, pCallEntry->idxInstr, sizeof(uint32_t))

/** Variant of IEM_MC_SET_RIP_U32_AND_FINISH for 386+ targets that checks and
 *  clears flags. */
#define IEM_MC_SET_RIP_U32_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_u32NewEIP) \
    IEM_MC_SET_RIP_U32_AND_FINISH_THREADED_PC32(a_u32NewEIP); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off)

/** Variant of IEM_MC_SET_RIP_U32_AND_FINISH for use in 64-bit code that checks
 *  and clears flags. */
#define IEM_MC_SET_RIP_U32_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_u32NewEIP) \
    IEM_MC_SET_RIP_U32_AND_FINISH_THREADED_PC64(a_u32NewEIP); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off)

#undef IEM_MC_SET_RIP_U32_AND_FINISH


/** Variant of IEM_MC_SET_RIP_U64_AND_FINISH for use in 64-bit code. */
#define IEM_MC_SET_RIP_U64_AND_FINISH_THREADED_PC64(a_u64NewEIP) \
    off = iemNativeEmitRipJumpNoFlags(pReNative, off, (a_u64NewEIP),  true /*f64Bit*/, pCallEntry->idxInstr, sizeof(uint64_t))

/** Variant of IEM_MC_SET_RIP_U64_AND_FINISH for use in 64-bit code that checks
 *  and clears flags. */
#define IEM_MC_SET_RIP_U64_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_u64NewEIP) \
    IEM_MC_SET_RIP_U64_AND_FINISH_THREADED_PC64(a_u64NewEIP); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off)

#undef IEM_MC_SET_RIP_U64_AND_FINISH


/** Same as iemRegRipJumpU16AndFinishNoFlags,
 *  iemRegRipJumpU32AndFinishNoFlags and iemRegRipJumpU64AndFinishNoFlags. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitRipJumpNoFlags(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarPc, bool f64Bit,
                            uint8_t idxInstr, uint8_t cbVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVarPc);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxVarPc, cbVar);

    /* If we can't rule out a #GP(0) below, flush all dirty register except for
       PC which will be handled specially by the two workers below if they raise a GP. */
    bool const    fMayRaiseGp0 = (f64Bit && cbVar > sizeof(uint32_t)) || (!f64Bit && !IEM_F_MODE_X86_IS_FLAT(pReNative->fExec));
    uint8_t const idxOldPcReg  = fMayRaiseGp0
                               ? iemNativeRegAllocTmpForGuestRegIfAlreadyPresent(pReNative, &off, kIemNativeGstReg_Pc)
                               : UINT8_MAX;
    if (fMayRaiseGp0)
        off = iemNativeRegFlushPendingWrites(pReNative, off, RT_BIT_64(kIemNativeGstReg_Pc) /*fGstShwExcept*/);

    /* Get a register with the new PC loaded from idxVarPc.
       Note! This ASSUMES that the high bits of the GPR is zeroed. */
    uint8_t const idxPcReg = iemNativeVarRegisterAcquireForGuestReg(pReNative, idxVarPc, kIemNativeGstReg_Pc, &off);

    /* Check that the target is within CS.LIM / is canonical (may #GP(0) + exit TB). */
    if (fMayRaiseGp0)
    {
        if (f64Bit)
            off = iemNativeEmitCheckGprCanonicalMaybeRaiseGp0WithOldPc(pReNative, off, idxPcReg, idxOldPcReg, idxInstr);
        else
            off = iemNativeEmitCheckGpr32AgainstCsSegLimitMaybeRaiseGp0WithOldPc(pReNative, off, idxPcReg, idxOldPcReg, idxInstr);
    }

    /* Store the result. */
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxPcReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rip));

#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
    pReNative->Core.offPc = 0;
    STAM_COUNTER_INC(&pReNative->pVCpu->iem.s.StatNativePcUpdateTotal);
# ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING_DEBUG
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxPcReg, RT_UOFFSETOF(VMCPU, iem.s.uPcUpdatingDebug));
    pReNative->Core.fDebugPcInitialized = true;
    Log4(("uPcUpdatingDebug=rip off=%#x\n", off));
# endif
#endif

    if (idxOldPcReg != UINT8_MAX)
        iemNativeRegFreeTmp(pReNative, idxOldPcReg);
    iemNativeVarRegisterRelease(pReNative, idxVarPc);
    /** @todo implictly free the variable? */

    return off;
}



/*********************************************************************************************************************************
*   Emitters for changing PC/RIP/EIP/IP with a relative call jump (IEM_MC_IND_CALL_UXX_AND_FINISH) (requires stack emmiters).    *
*********************************************************************************************************************************/

/** @todo These helpers belong to the stack push API naturally but we already need them up here (we could of course move
 *        this below the stack emitters but then this is not close to the rest of the PC/RIP handling...). */
DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitStackPushUse16Sp(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t idxRegRsp, uint8_t idxRegEffSp, uint8_t cbMem)
{
    /* Use16BitSp: */
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitSubGpr16ImmEx(pCodeBuf, off, idxRegRsp, cbMem); /* ASSUMES this does NOT modify bits [63:16]! */
    off = iemNativeEmitLoadGprFromGpr16Ex(pCodeBuf, off, idxRegEffSp, idxRegRsp);
#else
    /* sub regeff, regrsp, #cbMem */
    pCodeBuf[off++] = Armv8A64MkInstrSubUImm12(idxRegEffSp, idxRegRsp, cbMem, false /*f64Bit*/);
    /* and regeff, regeff, #0xffff */
    Assert(Armv8A64ConvertImmRImmS2Mask32(15, 0) == 0xffff);
    pCodeBuf[off++] = Armv8A64MkInstrAndImm(idxRegEffSp, idxRegEffSp, 15, 0,  false /*f64Bit*/);
    /* bfi regrsp, regeff, #0, #16 - moves bits 15:0 from idxVarReg to idxGstTmpReg bits 15:0. */
    pCodeBuf[off++] = Armv8A64MkInstrBfi(idxRegRsp, idxRegEffSp, 0, 16, false /*f64Bit*/);
#endif
    return off;
}


DECL_FORCE_INLINE(uint32_t)
iemNativeEmitStackPushUse32Sp(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t idxRegRsp, uint8_t idxRegEffSp, uint8_t cbMem)
{
    /* Use32BitSp: */
    off = iemNativeEmitSubGpr32ImmEx(pCodeBuf, off, idxRegRsp, cbMem);
    off = iemNativeEmitLoadGprFromGpr32Ex(pCodeBuf, off, idxRegEffSp, idxRegRsp);
    return off;
}


DECL_INLINE_THROW(uint32_t)
iemNativeEmitStackPushRip(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t const idxRegPc,
                          uint32_t cBitsVarAndFlat, uintptr_t pfnFunction, uint8_t idxInstr)
{
    /*
     * Assert sanity.
     */
#ifdef VBOX_STRICT
    if (RT_BYTE2(cBitsVarAndFlat) != 0)
    {
        Assert(   (pReNative->fExec & IEM_F_MODE_MASK) == IEM_F_MODE_X86_64BIT
               || (pReNative->fExec & IEM_F_MODE_MASK) == IEM_F_MODE_X86_32BIT_PROT_FLAT
               || (pReNative->fExec & IEM_F_MODE_MASK) == IEM_F_MODE_X86_32BIT_FLAT);
        Assert(   pfnFunction
               == (  cBitsVarAndFlat == RT_MAKE_U32_FROM_U8(16, 32, 0, 0) ? (uintptr_t)iemNativeHlpStackFlatStoreU16
                   : cBitsVarAndFlat == RT_MAKE_U32_FROM_U8(32, 32, 0, 0) ? (uintptr_t)iemNativeHlpStackFlatStoreU32
                   : cBitsVarAndFlat == RT_MAKE_U32_FROM_U8(16, 64, 0, 0) ? (uintptr_t)iemNativeHlpStackFlatStoreU16
                   : cBitsVarAndFlat == RT_MAKE_U32_FROM_U8(64, 64, 0, 0) ? (uintptr_t)iemNativeHlpStackFlatStoreU64
                   : UINT64_C(0xc000b000a0009000) ));
    }
    else
        Assert(   pfnFunction
               == (  cBitsVarAndFlat == RT_MAKE_U32_FROM_U8(16, 0, 0, 0) ? (uintptr_t)iemNativeHlpStackStoreU16
                   : cBitsVarAndFlat == RT_MAKE_U32_FROM_U8(32, 0, 0, 0) ? (uintptr_t)iemNativeHlpStackStoreU32
                   : cBitsVarAndFlat == RT_MAKE_U32_FROM_U8(64, 0, 0, 0) ? (uintptr_t)iemNativeHlpStackStoreU64
                   : UINT64_C(0xc000b000a0009000) ));
#endif

#ifdef VBOX_STRICT
    /*
     * Check that the fExec flags we've got make sense.
     */
    off = iemNativeEmitExecFlagsCheck(pReNative, off, pReNative->fExec);
#endif

    /*
     * To keep things simple we have to commit any pending writes first as we
     * may end up making calls.
     */
    /** @todo we could postpone this till we make the call and reload the
     * registers after returning from the call. Not sure if that's sensible or
     * not, though. */
    off = iemNativeRegFlushPendingWrites(pReNative, off);

    /*
     * First we calculate the new RSP and the effective stack pointer value.
     * For 64-bit mode and flat 32-bit these two are the same.
     * (Code structure is very similar to that of PUSH)
     */
    uint8_t const cbMem       = RT_BYTE1(cBitsVarAndFlat) / 8;
    bool const    fIsSegReg   = RT_BYTE3(cBitsVarAndFlat) != 0;
    bool const    fIsIntelSeg = fIsSegReg && IEM_IS_GUEST_CPU_INTEL(pReNative->pVCpu);
    uint8_t const cbMemAccess = !fIsIntelSeg || (pReNative->fExec & IEM_F_MODE_MASK) == IEM_F_MODE_X86_16BIT
                              ? cbMem : sizeof(uint16_t);
    uint8_t const cBitsFlat   = RT_BYTE2(cBitsVarAndFlat);      RT_NOREF(cBitsFlat);
    uint8_t const idxRegRsp   = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(X86_GREG_xSP),
                                                                kIemNativeGstRegUse_ForUpdate, true /*fNoVolatileRegs*/);
    uint8_t const idxRegEffSp = cBitsFlat != 0 ? idxRegRsp : iemNativeRegAllocTmp(pReNative, &off);
    uint32_t      offFixupJumpToUseOtherBitSp = UINT32_MAX;
    if (cBitsFlat != 0)
    {
        Assert(idxRegEffSp == idxRegRsp);
        Assert(cBitsFlat == 32 || cBitsFlat == 64);
        Assert(IEM_F_MODE_X86_IS_FLAT(pReNative->fExec));
        if (cBitsFlat == 64)
            off = iemNativeEmitSubGprImm(pReNative, off, idxRegRsp, cbMem);
        else
            off = iemNativeEmitSubGpr32Imm(pReNative, off, idxRegRsp, cbMem);
    }
    else /** @todo We can skip the test if we're targeting pre-386 CPUs. */
    {
        Assert(idxRegEffSp != idxRegRsp);
        uint8_t const idxRegSsAttr = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_SEG_ATTRIB(X86_SREG_SS),
                                                                     kIemNativeGstRegUse_ReadOnly);
#ifdef RT_ARCH_AMD64
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 32);
#else
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 10);
#endif
        off = iemNativeEmitTestAnyBitsInGpr32Ex(pCodeBuf, off, idxRegSsAttr, X86DESCATTR_D);
        iemNativeRegFreeTmp(pReNative, idxRegSsAttr);
        offFixupJumpToUseOtherBitSp = off;
        if ((pReNative->fExec & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_32BIT)
        {
            off = iemNativeEmitJccToFixedEx(pCodeBuf, off, off /*8-bit suffices*/, kIemNativeInstrCond_e); /* jump if zero */
            off = iemNativeEmitStackPushUse32Sp(pCodeBuf, off, idxRegRsp, idxRegEffSp, cbMem);
        }
        else
        {
            off = iemNativeEmitJccToFixedEx(pCodeBuf, off, off /*8-bit suffices*/, kIemNativeInstrCond_ne); /* jump if not zero */
            off = iemNativeEmitStackPushUse16Sp(pCodeBuf, off, idxRegRsp, idxRegEffSp, cbMem);
        }
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    }
    /* SpUpdateEnd: */
    uint32_t const offLabelSpUpdateEnd = off;

    /*
     * Okay, now prepare for TLB lookup and jump to code (or the TlbMiss if
     * we're skipping lookup).
     */
    uint8_t const  iSegReg           = cBitsFlat != 0 ? UINT8_MAX : X86_SREG_SS;
    IEMNATIVEEMITTLBSTATE const TlbState(pReNative, idxRegEffSp, &off, iSegReg, cbMemAccess);
    uint16_t const uTlbSeqNo         = pReNative->uTlbSeqNo++;
    uint32_t const idxLabelTlbMiss   = iemNativeLabelCreate(pReNative, kIemNativeLabelType_TlbMiss, UINT32_MAX, uTlbSeqNo);
    uint32_t const idxLabelTlbLookup = !TlbState.fSkip
                                     ? iemNativeLabelCreate(pReNative, kIemNativeLabelType_TlbLookup, UINT32_MAX, uTlbSeqNo)
                                     : UINT32_MAX;
    uint8_t const  idxRegMemResult   = !TlbState.fSkip ? iemNativeRegAllocTmp(pReNative, &off) : UINT8_MAX;


    if (!TlbState.fSkip)
        off = iemNativeEmitJmpToLabel(pReNative, off, idxLabelTlbLookup); /** @todo short jump */
    else
        off = iemNativeEmitJmpToLabel(pReNative, off, idxLabelTlbMiss); /** @todo short jump */

    /*
     * Use16BitSp:
     */
    if (cBitsFlat == 0)
    {
#ifdef RT_ARCH_AMD64
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 32);
#else
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 10);
#endif
        iemNativeFixupFixedJump(pReNative, offFixupJumpToUseOtherBitSp, off);
        if ((pReNative->fExec & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_32BIT)
            off = iemNativeEmitStackPushUse16Sp(pCodeBuf, off, idxRegRsp, idxRegEffSp, cbMem);
        else
            off = iemNativeEmitStackPushUse32Sp(pCodeBuf, off, idxRegRsp, idxRegEffSp, cbMem);
        off = iemNativeEmitJmpToFixedEx(pCodeBuf, off, offLabelSpUpdateEnd);
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    }

    /*
     * TlbMiss:
     *
     * Call helper to do the pushing.
     */
    iemNativeLabelDefine(pReNative, idxLabelTlbMiss, off);

#ifdef IEMNATIVE_WITH_INSTRUCTION_COUNTING
    off = iemNativeEmitStoreImmToVCpuU8(pReNative, off, idxInstr, RT_UOFFSETOF(VMCPUCC, iem.s.idxTbCurInstr));
#else
    RT_NOREF(idxInstr);
#endif

    /* Save variables in volatile registers. */
    uint32_t const fHstRegsNotToSave = TlbState.getRegsNotToSave()
                                     | (idxRegMemResult < RT_ELEMENTS(pReNative->Core.aHstRegs) ? RT_BIT_32(idxRegMemResult) : 0)
                                     | (idxRegEffSp != idxRegRsp ? RT_BIT_32(idxRegEffSp) : 0)
                                     | (RT_BIT_32(idxRegPc));
    off = iemNativeVarSaveVolatileRegsPreHlpCall(pReNative, off, fHstRegsNotToSave);

    if (   idxRegPc == IEMNATIVE_CALL_ARG1_GREG
        && idxRegEffSp == IEMNATIVE_CALL_ARG2_GREG)
    {
        /* Swap them using ARG0 as temp register: */
        off = iemNativeEmitLoadGprFromGpr(pReNative, off, IEMNATIVE_CALL_ARG0_GREG, IEMNATIVE_CALL_ARG1_GREG);
        off = iemNativeEmitLoadGprFromGpr(pReNative, off, IEMNATIVE_CALL_ARG1_GREG, IEMNATIVE_CALL_ARG2_GREG);
        off = iemNativeEmitLoadGprFromGpr(pReNative, off, IEMNATIVE_CALL_ARG2_GREG, IEMNATIVE_CALL_ARG0_GREG);
    }
    else if (idxRegEffSp != IEMNATIVE_CALL_ARG2_GREG)
    {
        /* IEMNATIVE_CALL_ARG2_GREG = idxRegPc (first!) */
        off = iemNativeEmitLoadGprFromGpr(pReNative, off, IEMNATIVE_CALL_ARG2_GREG, idxRegPc);

        /* IEMNATIVE_CALL_ARG1_GREG = idxRegEffSp */
        if (idxRegEffSp != IEMNATIVE_CALL_ARG1_GREG)
            off = iemNativeEmitLoadGprFromGpr(pReNative, off, IEMNATIVE_CALL_ARG1_GREG, idxRegEffSp);
    }
    else
    {
        /* IEMNATIVE_CALL_ARG1_GREG = idxRegEffSp (first!) */
        off = iemNativeEmitLoadGprFromGpr(pReNative, off, IEMNATIVE_CALL_ARG1_GREG, idxRegEffSp);

        /* IEMNATIVE_CALL_ARG2_GREG = idxRegPc */
        off = iemNativeEmitLoadGprFromGpr(pReNative, off, IEMNATIVE_CALL_ARG2_GREG, idxRegPc);
    }

#ifdef IEMNATIVE_WITH_EFLAGS_POSTPONING
    /* Do delayed EFLAGS calculations. */
    off = iemNativeDoPostponedEFlagsAtTlbMiss<  RT_BIT_32(IEMNATIVE_CALL_ARG1_GREG)
                                              | RT_BIT_32(IEMNATIVE_CALL_ARG2_GREG)>(pReNative, off, &TlbState, fHstRegsNotToSave);
#endif

    /* IEMNATIVE_CALL_ARG0_GREG = pVCpu */
    off = iemNativeEmitLoadGprFromGpr(pReNative, off, IEMNATIVE_CALL_ARG0_GREG, IEMNATIVE_REG_FIXED_PVMCPU);

    /* Done setting up parameters, make the call. */
    off = iemNativeEmitCallImm<true /*a_fSkipEflChecks*/>(pReNative, off, pfnFunction);

    /* Restore variables and guest shadow registers to volatile registers. */
    off = iemNativeVarRestoreVolatileRegsPostHlpCall(pReNative, off, fHstRegsNotToSave);
    off = iemNativeRegRestoreGuestShadowsInVolatileRegs(pReNative, off, TlbState.getActiveRegsWithShadows());

#ifdef IEMNATIVE_WITH_TLB_LOOKUP
    if (!TlbState.fSkip)
    {
        /* end of TlbMiss - Jump to the done label. */
        uint32_t const idxLabelTlbDone = iemNativeLabelCreate(pReNative, kIemNativeLabelType_TlbDone, UINT32_MAX, uTlbSeqNo);
        off = iemNativeEmitJmpToLabel(pReNative, off, idxLabelTlbDone);

        /*
         * TlbLookup:
         */
        off = iemNativeEmitTlbLookup<true>(pReNative, off, &TlbState, iSegReg, cbMemAccess, cbMemAccess - 1,
                                           IEM_ACCESS_TYPE_WRITE, idxLabelTlbLookup, idxLabelTlbMiss, idxRegMemResult);

        /*
         * Emit code to do the actual storing / fetching.
         */
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 64);
# ifdef IEM_WITH_TLB_STATISTICS
        off = iemNativeEmitIncStamCounterInVCpuEx(pCodeBuf, off, TlbState.idxReg1, TlbState.idxReg2,
                                                  RT_UOFFSETOF(VMCPUCC, iem.s.StatNativeTlbHitsForStack));
# endif
        switch (cbMemAccess)
        {
            case 2:
                off = iemNativeEmitStoreGpr16ByGprEx(pCodeBuf, off, idxRegPc, idxRegMemResult);
                break;
            case 4:
                if (!fIsIntelSeg)
                    off = iemNativeEmitStoreGpr32ByGprEx(pCodeBuf, off, idxRegPc, idxRegMemResult);
                else
                {
                    /* intel real mode segment push. 10890XE adds the 2nd of half EFLAGS to a
                       PUSH FS in real mode, so we have to try emulate that here.
                       We borrow the now unused idxReg1 from the TLB lookup code here. */
                    uint8_t const idxRegEfl = iemNativeRegAllocTmpForGuestRegIfAlreadyPresent(pReNative, &off,
                                                                                              kIemNativeGstReg_EFlags);
                    if (idxRegEfl != UINT8_MAX)
                    {
#ifdef ARCH_AMD64
                        off = iemNativeEmitLoadGprFromGpr32(pReNative, off, TlbState.idxReg1, idxRegEfl);
                        off = iemNativeEmitAndGpr32ByImm(pReNative, off, TlbState.idxReg1,
                                                         UINT32_C(0xffff0000) & ~X86_EFL_RAZ_MASK);
#else
                        off = iemNativeEmitGpr32EqGprAndImmEx(iemNativeInstrBufEnsure(pReNative, off, 3),
                                                              off, TlbState.idxReg1, idxRegEfl,
                                                              UINT32_C(0xffff0000) & ~X86_EFL_RAZ_MASK);
#endif
                        iemNativeRegFreeTmp(pReNative, idxRegEfl);
                    }
                    else
                    {
                        off = iemNativeEmitLoadGprFromVCpuU32(pReNative, off, TlbState.idxReg1,
                                                              RT_UOFFSETOF(VMCPUCC, cpum.GstCtx.eflags));
                        off = iemNativeEmitAndGpr32ByImm(pReNative, off, TlbState.idxReg1,
                                                         UINT32_C(0xffff0000) & ~X86_EFL_RAZ_MASK);
                    }
                    /* ASSUMES the upper half of idxRegPc is ZERO. */
                    off = iemNativeEmitOrGpr32ByGpr(pReNative, off, TlbState.idxReg1, idxRegPc);
                    off = iemNativeEmitStoreGpr32ByGprEx(pCodeBuf, off, TlbState.idxReg1, idxRegMemResult);
                }
                break;
            case 8:
                off = iemNativeEmitStoreGpr64ByGprEx(pCodeBuf, off, idxRegPc, idxRegMemResult);
                break;
            default:
                AssertFailed();
        }

        iemNativeRegFreeTmp(pReNative, idxRegMemResult);
        TlbState.freeRegsAndReleaseVars(pReNative);

        /*
         * TlbDone:
         *
         * Commit the new RSP value.
         */
        iemNativeLabelDefine(pReNative, idxLabelTlbDone, off);
    }
#endif /* IEMNATIVE_WITH_TLB_LOOKUP */

#if !defined(IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK)
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxRegRsp, RT_UOFFSETOF_DYN(VMCPU, cpum.GstCtx.rsp));
#endif
    iemNativeRegFreeTmp(pReNative, idxRegRsp);
    if (idxRegEffSp != idxRegRsp)
        iemNativeRegFreeTmp(pReNative, idxRegEffSp);

    return off;
}


/** Variant of IEM_MC_IND_CALL_U16_AND_FINISH for pre-386 targets. */
#define IEM_MC_IND_CALL_U16_AND_FINISH_THREADED_PC16(a_u16NewIP, a_cbInstr) \
    off = iemNativeEmitRipIndirectCallNoFlags(pReNative, off, a_cbInstr, (a_u16NewIP), false /*f64Bit*/, pCallEntry->idxInstr, sizeof(uint16_t))

/** Variant of IEM_MC_IND_CALL_U16_AND_FINISH for pre-386 targets that checks and
 *  clears flags. */
#define IEM_MC_IND_CALL_U16_AND_FINISH_THREADED_PC16_WITH_FLAGS(a_u16NewIP, a_cbInstr) \
    IEM_MC_IND_CALL_U16_AND_FINISH_THREADED_PC16(a_u16NewIP, a_cbInstr); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off)

/** Variant of IEM_MC_IND_CALL_U16_AND_FINISH for 386+ targets. */
#define IEM_MC_IND_CALL_U16_AND_FINISH_THREADED_PC32(a_u16NewIP, a_cbInstr) \
    off = iemNativeEmitRipIndirectCallNoFlags(pReNative, off, a_cbInstr, (a_u16NewIP), false /*f64Bit*/, pCallEntry->idxInstr, sizeof(uint16_t))

/** Variant of IEM_MC_IND_CALL_U16_AND_FINISH for 386+ targets that checks and
 *  clears flags. */
#define IEM_MC_IND_CALL_U16_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_u16NewIP, a_cbInstr) \
    IEM_MC_IND_CALL_U16_AND_FINISH_THREADED_PC32(a_u16NewIP, a_cbInstr); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off)

#undef  IEM_MC_IND_CALL_U16_AND_FINISH


/** Variant of IEM_MC_IND_CALL_U32_AND_FINISH for 386+ targets. */
#define IEM_MC_IND_CALL_U32_AND_FINISH_THREADED_PC32(a_u32NewEIP, a_cbInstr) \
    off = iemNativeEmitRipIndirectCallNoFlags(pReNative, off, a_cbInstr, (a_u32NewEIP), false /*f64Bit*/, pCallEntry->idxInstr, sizeof(uint32_t))

/** Variant of IEM_MC_IND_CALL_U32_AND_FINISH for 386+ targets that checks and
 *  clears flags. */
#define IEM_MC_IND_CALL_U32_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_u32NewEIP, a_cbInstr) \
    IEM_MC_IND_CALL_U32_AND_FINISH_THREADED_PC32(a_u32NewEIP, a_cbInstr); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off)

#undef  IEM_MC_IND_CALL_U32_AND_FINISH


/** Variant of IEM_MC_IND_CALL_U64_AND_FINISH with instruction length as
 *  an extra parameter, for use in 64-bit code. */
#define IEM_MC_IND_CALL_U64_AND_FINISH_THREADED_PC64(a_u64NewIP, a_cbInstr) \
    off = iemNativeEmitRipIndirectCallNoFlags(pReNative, off, a_cbInstr, (a_u64NewIP), true /*f64Bit*/, pCallEntry->idxInstr, sizeof(uint64_t))


/** Variant of IEM_MC_IND_CALL_U64_AND_FINISH with instruction length as
 *  an extra parameter, for use in 64-bit code and we need to check and clear
 *  flags. */
#define IEM_MC_IND_CALL_U64_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_u64NewIP, a_cbInstr) \
    IEM_MC_IND_CALL_U64_AND_FINISH_THREADED_PC64(a_u64NewIP, a_cbInstr); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off)

#undef  IEM_MC_IND_CALL_U64_AND_FINISH

/** Same as iemRegIp16RelativeCallS16AndFinishNoFlags,
 *  iemRegEip32RelativeCallS32AndFinishNoFlags and iemRegRip64RelativeCallS64AndFinishNoFlags. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitRipIndirectCallNoFlags(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t cbInstr, uint8_t idxVarPc, bool f64Bit,
                                    uint8_t idxInstr, uint8_t cbVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVarPc);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxVarPc, cbVar);

    /* We speculatively modify PC and may raise #GP(0), so make sure the right values are in CPUMCTX. */
    off = iemNativeRegFlushPendingWrites(pReNative, off);

#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
    Assert(pReNative->Core.offPc == 0);
    STAM_COUNTER_INC(&pReNative->pVCpu->iem.s.StatNativePcUpdateTotal);
#endif

    /* Get a register with the new PC loaded from idxVarPc.
       Note! This ASSUMES that the high bits of the GPR is zeroed. */
    uint8_t const idxNewPcReg = iemNativeVarRegisterAcquire(pReNative, idxVarPc, &off);

    /* Check limit (may #GP(0) + exit TB). */
    if (!f64Bit)
/** @todo we can skip this test in FLAT 32-bit mode. */
        off = iemNativeEmitCheckGpr32AgainstCsSegLimitMaybeRaiseGp0(pReNative, off, idxNewPcReg, idxInstr);
    /* Check that the address is canonical, raising #GP(0) + exit TB if it isn't. */
    else if (cbVar > sizeof(uint32_t))
        off = iemNativeEmitCheckGprCanonicalMaybeRaiseGp0(pReNative, off, idxNewPcReg, idxInstr);

#if 1
    /* Allocate a temporary PC register, we don't want it shadowed. */
    uint8_t const idxPcReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_Pc,
                                                             kIemNativeGstRegUse_Calculation, true /*fNoVolatileRegs*/);
#else
    /* Allocate a temporary PC register. */
    uint8_t const idxPcReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_Pc, kIemNativeGstRegUse_ForUpdate,
                                                             true /*fNoVolatileRegs*/);
#endif

    /* Perform the addition and push the variable to the guest stack. */
    /** @todo Flat variants for PC32 variants. */
    switch (cbVar)
    {
        case sizeof(uint16_t):
            off = iemNativeEmitAddGpr32Imm8(pReNative, off, idxPcReg, cbInstr);
            /* Truncate the result to 16-bit IP. */
            off = iemNativeEmitClear16UpGpr(pReNative, off, idxPcReg);
            off = iemNativeEmitStackPushRip(pReNative, off, idxPcReg, RT_MAKE_U32_FROM_U8(16,  0, 0, 0),
                                            (uintptr_t)iemNativeHlpStackStoreU16, idxInstr);
            break;
        case sizeof(uint32_t):
            off = iemNativeEmitAddGpr32Imm8(pReNative, off, idxPcReg, cbInstr);
            /** @todo In FLAT mode we can use the flat variant. */
            off = iemNativeEmitStackPushRip(pReNative, off, idxPcReg, RT_MAKE_U32_FROM_U8(32,  0, 0, 0),
                                            (uintptr_t)iemNativeHlpStackStoreU32, idxInstr);
            break;
        case sizeof(uint64_t):
            off = iemNativeEmitAddGprImm8(pReNative, off, idxPcReg, cbInstr);
            off = iemNativeEmitStackPushRip(pReNative, off, idxPcReg, RT_MAKE_U32_FROM_U8(64,  64, 0, 0),
                                            (uintptr_t)iemNativeHlpStackFlatStoreU64, idxInstr);
            break;
        default:
            AssertFailed();
    }

    /* RSP got changed, so do this again. */
    off = iemNativeRegFlushPendingWrites(pReNative, off);

    /* Store the result. */
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxNewPcReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rip));
#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING_DEBUG
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxNewPcReg, RT_UOFFSETOF(VMCPU, iem.s.uPcUpdatingDebug));
    pReNative->Core.fDebugPcInitialized = true;
    Log4(("uPcUpdatingDebug=rip/indirect-call off=%#x\n", off));
#endif

#if 1
    /* Need to transfer the shadow information to the new RIP register. */
    iemNativeRegClearAndMarkAsGstRegShadow(pReNative, idxNewPcReg, kIemNativeGstReg_Pc, off);
#else
    /* Sync the new PC. */
    off = iemNativeEmitLoadGprFromGpr(pReNative, off, idxPcReg, idxNewPcReg);
#endif
    iemNativeVarRegisterRelease(pReNative, idxVarPc);
    iemNativeRegFreeTmp(pReNative, idxPcReg);
    /** @todo implictly free the variable? */

    return off;
}


/** Variant of IEM_MC_REL_CALL_S16_AND_FINISH with instruction length as
 *  an extra parameter, for use in 16-bit code on a pre-386 CPU. */
#define IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC16(a_i16, a_cbInstr) \
    off = iemNativeEmitRipRelativeCallS16NoFlags(pReNative, off, a_cbInstr, (a_i16), pCallEntry->idxInstr)

/** Variant of IEM_MC_REL_CALL_S16_AND_FINISH with instruction length as
 *  an extra parameter, for use in 16-bit code on a pre-386 CPU and we need to check and clear
 *  flags. */
#define IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC16_WITH_FLAGS(a_i16, a_cbInstr) \
    IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC16(a_i16, a_cbInstr); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off)

/** Variant of IEM_MC_REL_CALL_S16_AND_FINISH with instruction length as
 *  an extra parameter, for use in 16-bit and 32-bit code on 386+. */
#define IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC32(a_i16, a_cbInstr) \
    off = iemNativeEmitRipRelativeCallS16NoFlags(pReNative, off, a_cbInstr, (a_i16), pCallEntry->idxInstr)

/** Variant of IEM_MC_REL_CALL_S16_AND_FINISH with instruction length as
 *  an extra parameter, for use in 16-bit and 32-bit code on 386+ and we need to check and clear
 *  flags. */
#define IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_i16, a_cbInstr) \
    IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC32(a_i16, a_cbInstr); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off)

/** Variant of IEM_MC_REL_CALL_S16_AND_FINISH with instruction length as
 *  an extra parameter, for use in 16-bit and 32-bit code on 386+. */
#define IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC64(a_i16, a_cbInstr) \
    off = iemNativeEmitRipRelativeCallS16NoFlags(pReNative, off, a_cbInstr, (a_i16), pCallEntry->idxInstr)

/** Variant of IEM_MC_REL_CALL_S16_AND_FINISH with instruction length as
 *  an extra parameter, for use in 16-bit and 32-bit code on 386+ and we need to check and clear
 *  flags. */
#define IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_i16, a_cbInstr) \
    IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC64(a_i16, a_cbInstr); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off)

#undef  IEM_MC_REL_CALL_S16_AND_FINISH

/** Same as iemRegIp16RelativeCallS16AndFinishNoFlags,
 *  iemRegEip32RelativeCallS32AndFinishNoFlags and iemRegRip64RelativeCallS64AndFinishNoFlags. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitRipRelativeCallS16NoFlags(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t cbInstr, int16_t offDisp,
                                       uint8_t idxInstr)
{
    /* We speculatively modify PC and may raise #GP(0), so make sure the right values are in CPUMCTX. */
    off = iemNativeRegFlushPendingWrites(pReNative, off);

#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
    Assert(pReNative->Core.offPc == 0);
    STAM_COUNTER_INC(&pReNative->pVCpu->iem.s.StatNativePcUpdateTotal);
#endif

    /* Allocate a temporary PC register. */
    uint8_t const idxPcRegOld = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_Pc,
                                                                kIemNativeGstRegUse_Calculation, true /*fNoVolatileRegs*/);
    uint8_t const idxPcRegNew = iemNativeRegAllocTmpPreferNonVolatile(pReNative, &off);

    /* Calculate the new RIP. */
    off = iemNativeEmitAddGpr32Imm8(pReNative, off, idxPcRegOld, cbInstr);
    /* Truncate the result to 16-bit IP. */
    off = iemNativeEmitClear16UpGpr(pReNative, off, idxPcRegOld);
    off = iemNativeEmitLoadGprFromGpr32(pReNative, off, idxPcRegNew, idxPcRegOld);
    off = iemNativeEmitAddGpr32Imm(pReNative, off, idxPcRegNew, offDisp);

    /* Truncate the result to 16-bit IP. */
    off = iemNativeEmitClear16UpGpr(pReNative, off, idxPcRegNew);

    /* Check limit (may #GP(0) + exit TB). */
    off = iemNativeEmitCheckGpr32AgainstCsSegLimitMaybeRaiseGp0(pReNative, off, idxPcRegNew, idxInstr);

    /* Perform the addition and push the variable to the guest stack. */
    off = iemNativeEmitStackPushRip(pReNative, off, idxPcRegOld, RT_MAKE_U32_FROM_U8(16,  0, 0, 0),
                                    (uintptr_t)iemNativeHlpStackStoreU16, idxInstr);

    /* RSP got changed, so flush again. */
    off = iemNativeRegFlushPendingWrites(pReNative, off);

    /* Store the result. */
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxPcRegNew, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rip));
#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING_DEBUG
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxPcRegNew, RT_UOFFSETOF(VMCPU, iem.s.uPcUpdatingDebug));
    pReNative->Core.fDebugPcInitialized = true;
    Log4(("uPcUpdatingDebug=rip/rel-call-16 off=%#x offDisp=%d\n", off, offDisp));
#endif

    /* Need to transfer the shadow information to the new RIP register. */
    iemNativeRegClearAndMarkAsGstRegShadow(pReNative, idxPcRegNew, kIemNativeGstReg_Pc, off);
    iemNativeRegFreeTmp(pReNative, idxPcRegOld);
    iemNativeRegFreeTmp(pReNative, idxPcRegNew);

    return off;
}


/** Variant of IEM_MC_REL_CALL_S32_AND_FINISH with instruction length as
 *  an extra parameter, for use in 16-bit and 32-bit code on 386+. */
#define IEM_MC_REL_CALL_S32_AND_FINISH_THREADED_PC32(a_i32, a_cbInstr) \
    off = iemNativeEmitEip32RelativeCallNoFlags(pReNative, off, a_cbInstr, (a_i32), pCallEntry->idxInstr)

/** Variant of IEM_MC_REL_CALL_S32_AND_FINISH with instruction length as
 *  an extra parameter, for use in 16-bit and 32-bit code on 386+ and we need to check and clear
 *  flags. */
#define IEM_MC_REL_CALL_S32_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_i32, a_cbInstr) \
    IEM_MC_REL_CALL_S32_AND_FINISH_THREADED_PC32(a_i32, a_cbInstr); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off)

#undef  IEM_MC_REL_CALL_S32_AND_FINISH

/** Same as iemRegIp16RelativeCallS16AndFinishNoFlags,
 *  iemRegEip32RelativeCallS32AndFinishNoFlags and iemRegRip64RelativeCallS64AndFinishNoFlags. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitEip32RelativeCallNoFlags(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t cbInstr, int32_t offDisp,
                                      uint8_t idxInstr)
{
    /* We speculatively modify PC and may raise #GP(0), so make sure the right values are in CPUMCTX. */
    off = iemNativeRegFlushPendingWrites(pReNative, off);

#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
    Assert(pReNative->Core.offPc == 0);
    STAM_COUNTER_INC(&pReNative->pVCpu->iem.s.StatNativePcUpdateTotal);
#endif

    /* Allocate a temporary PC register. */
    uint8_t const idxPcRegOld = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_Pc,
                                                                kIemNativeGstRegUse_ReadOnly, true /*fNoVolatileRegs*/);
    uint8_t const idxPcRegNew = iemNativeRegAllocTmpPreferNonVolatile(pReNative, &off);

    /* Update the EIP to get the return address. */
    off = iemNativeEmitAddGpr32Imm8(pReNative, off, idxPcRegOld, cbInstr);

    /* Load address, add the displacement and check that the address is canonical, raising #GP(0) + exit TB if it isn't. */
    off = iemNativeEmitLoadGprFromGpr32(pReNative, off, idxPcRegNew, idxPcRegOld);
    off = iemNativeEmitAddGpr32Imm(pReNative, off, idxPcRegNew, offDisp);
    /** @todo we can skip this test in FLAT 32-bit mode. */
    off = iemNativeEmitCheckGpr32AgainstCsSegLimitMaybeRaiseGp0(pReNative, off, idxPcRegNew, idxInstr);

    /* Perform Perform the return address to the guest stack. */
    /** @todo Can avoid the stack limit checks in FLAT 32-bit mode. */
    off = iemNativeEmitStackPushRip(pReNative, off, idxPcRegOld, RT_MAKE_U32_FROM_U8(32,  0, 0, 0),
                                    (uintptr_t)iemNativeHlpStackStoreU32, idxInstr);

    /* RSP got changed, so do this again. */
    off = iemNativeRegFlushPendingWrites(pReNative, off);

    /* Store the result. */
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxPcRegNew, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rip));
#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING_DEBUG
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxPcRegNew, RT_UOFFSETOF(VMCPU, iem.s.uPcUpdatingDebug));
    pReNative->Core.fDebugPcInitialized = true;
    Log4(("uPcUpdatingDebug=eip/rel-call-32 off=%#x offDisp=%d\n", off, offDisp));
#endif

    /* Need to transfer the shadow information to the new RIP register. */
    iemNativeRegClearAndMarkAsGstRegShadow(pReNative, idxPcRegNew, kIemNativeGstReg_Pc, off);
    iemNativeRegFreeTmp(pReNative, idxPcRegNew);
    iemNativeRegFreeTmp(pReNative, idxPcRegOld);

    return off;
}


/** Variant of IEM_MC_REL_CALL_S64_AND_FINISH with instruction length as
 *  an extra parameter, for use in 64-bit code. */
#define IEM_MC_REL_CALL_S64_AND_FINISH_THREADED_PC64(a_i64, a_cbInstr) \
    off = iemNativeEmitRip64RelativeCallNoFlags(pReNative, off, a_cbInstr, (a_i64), pCallEntry->idxInstr)

/** Variant of IEM_MC_REL_CALL_S64_AND_FINISH with instruction length as
 *  an extra parameter, for use in 64-bit code and we need to check and clear
 *  flags. */
#define IEM_MC_REL_CALL_S64_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_i64, a_cbInstr) \
    IEM_MC_REL_CALL_S64_AND_FINISH_THREADED_PC64(a_i64, a_cbInstr); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off)

#undef  IEM_MC_REL_CALL_S64_AND_FINISH

/** Same as iemRegIp16RelativeCallS16AndFinishNoFlags,
 *  iemRegEip32RelativeCallS32AndFinishNoFlags and iemRegRip64RelativeCallS64AndFinishNoFlags. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitRip64RelativeCallNoFlags(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t cbInstr, int64_t offDisp,
                                      uint8_t idxInstr)
{
    /* We speculatively modify PC and may raise #GP(0), so make sure the right values are in CPUMCTX. */
    off = iemNativeRegFlushPendingWrites(pReNative, off);

#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
    Assert(pReNative->Core.offPc == 0);
    STAM_COUNTER_INC(&pReNative->pVCpu->iem.s.StatNativePcUpdateTotal);
#endif

    /* Allocate a temporary PC register. */
    uint8_t const idxPcRegOld = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_Pc,
                                                                kIemNativeGstRegUse_ReadOnly, true /*fNoVolatileRegs*/);
    uint8_t const idxPcRegNew = iemNativeRegAllocTmpPreferNonVolatile(pReNative, &off);

    /* Update the RIP to get the return address. */
    off = iemNativeEmitAddGprImm8(pReNative, off, idxPcRegOld, cbInstr);

    /* Load address, add the displacement and check that the address is canonical, raising #GP(0) + exit TB if it isn't. */
    off = iemNativeEmitLoadGprFromGpr(pReNative, off, idxPcRegNew, idxPcRegOld);
    off = iemNativeEmitAddGprImm(pReNative, off, idxPcRegNew, offDisp);
    off = iemNativeEmitCheckGprCanonicalMaybeRaiseGp0(pReNative, off, idxPcRegNew, idxInstr);

    /* Perform Perform the return address to the guest stack. */
    off = iemNativeEmitStackPushRip(pReNative, off, idxPcRegOld, RT_MAKE_U32_FROM_U8(64,  64, 0, 0),
                                    (uintptr_t)iemNativeHlpStackFlatStoreU64, idxInstr);

    /* RSP got changed, so do this again. */
    off = iemNativeRegFlushPendingWrites(pReNative, off);

    /* Store the result. */
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxPcRegNew, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rip));
#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING_DEBUG
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxPcRegNew, RT_UOFFSETOF(VMCPU, iem.s.uPcUpdatingDebug));
    pReNative->Core.fDebugPcInitialized = true;
    Log4(("uPcUpdatingDebug=rip/rel-call-64 off=%#x offDisp=%ld\n", off, offDisp));
#endif

    /* Need to transfer the shadow information to the new RIP register. */
    iemNativeRegClearAndMarkAsGstRegShadow(pReNative, idxPcRegNew, kIemNativeGstReg_Pc, off);
    iemNativeRegFreeTmp(pReNative, idxPcRegNew);
    iemNativeRegFreeTmp(pReNative, idxPcRegOld);

    return off;
}


/*********************************************************************************************************************************
*   Emitters for changing PC/RIP/EIP/IP with a RETN (Iw) instruction (IEM_MC_RETN_AND_FINISH) (requires stack emmiters).         *
*********************************************************************************************************************************/

DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitStackPopForRetnUse16Sp(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t idxRegRsp, uint8_t idxRegEffSp, uint8_t cbMem,
                                    uint16_t cbPopAdd, uint8_t idxRegTmp)
{
    /* Use16BitSp: */
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitLoadGprFromGpr16Ex(pCodeBuf, off, idxRegEffSp, idxRegRsp);
    off = iemNativeEmitAddGpr16ImmEx(pCodeBuf, off, idxRegRsp, cbMem); /* ASSUMES this does NOT modify bits [63:16]! */
    off = iemNativeEmitAddGpr16ImmEx(pCodeBuf, off, idxRegRsp, cbPopAdd); /* ASSUMES this does NOT modify bits [63:16]! */
    RT_NOREF(idxRegTmp);

#elif defined(RT_ARCH_ARM64)
    /* ubfiz regeff, regrsp, #0, #16 - copies bits 15:0 from RSP to EffSp bits 15:0, zeroing bits 63:16. */
    pCodeBuf[off++] = Armv8A64MkInstrUbfiz(idxRegEffSp, idxRegRsp, 0, 16, false /*f64Bit*/);
    /* add tmp, regrsp, #cbMem */
    uint16_t const cbCombined = cbMem + cbPopAdd;
    pCodeBuf[off++] = Armv8A64MkInstrAddUImm12(idxRegTmp, idxRegRsp, cbCombined & (RT_BIT_32(12) - 1U), false /*f64Bit*/);
    if (cbCombined >= RT_BIT_32(12))
        pCodeBuf[off++] = Armv8A64MkInstrAddUImm12(idxRegTmp, idxRegTmp, cbCombined >> 12,
                                                   false /*f64Bit*/, false /*fSetFlags*/,  true /*fShift12*/);
    /* and tmp, tmp, #0xffff */
    Assert(Armv8A64ConvertImmRImmS2Mask32(15, 0) == 0xffff);
    pCodeBuf[off++] = Armv8A64MkInstrAndImm(idxRegTmp, idxRegTmp, 15, 0, false /*f64Bit*/);
    /* bfi regrsp, regeff, #0, #16 - moves bits 15:0 from tmp to RSP bits 15:0, keeping the other RSP bits as is. */
    pCodeBuf[off++] = Armv8A64MkInstrBfi(idxRegRsp, idxRegTmp, 0, 16, false /*f64Bit*/);

#else
# error "Port me"
#endif
    return off;
}


DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitStackPopForRetnUse32Sp(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t idxRegRsp, uint8_t idxRegEffSp, uint8_t cbMem,
                                    uint16_t cbPopAdd)
{
    /* Use32BitSp: */
    off = iemNativeEmitLoadGprFromGpr32Ex(pCodeBuf, off, idxRegEffSp, idxRegRsp);
    off = iemNativeEmitAddGpr32ImmEx(pCodeBuf, off, idxRegRsp, cbMem + cbPopAdd);
    return off;
}


/** Variant of IEM_MC_RETN_AND_FINISH for pre-386 targets. */
#define IEM_MC_RETN_AND_FINISH_THREADED_PC16(a_u16Pop, a_cbInstr) \
    off = iemNativeEmitRetn(pReNative, off, (a_cbInstr), (a_u16Pop), false /*f64Bit*/, IEMMODE_16BIT, pCallEntry->idxInstr)

/** Variant of IEM_MC_RETN_AND_FINISH for 386+ targets. */
#define IEM_MC_RETN_AND_FINISH_THREADED_PC32(a_u16Pop, a_cbInstr, a_enmEffOpSize) \
    off = iemNativeEmitRetn(pReNative, off, (a_cbInstr), (a_u16Pop), false /*f64Bit*/, (a_enmEffOpSize), pCallEntry->idxInstr)

/** Variant of IEM_MC_RETN_AND_FINISH for use in 64-bit code. */
#define IEM_MC_RETN_AND_FINISH_THREADED_PC64(a_u16Pop, a_cbInstr, a_enmEffOpSize) \
    off = iemNativeEmitRetn(pReNative, off, (a_cbInstr), (a_u16Pop), true /*f64Bit*/, (a_enmEffOpSize), pCallEntry->idxInstr)

/** Variant of IEM_MC_RETN_AND_FINISH for pre-386 targets that checks and
 *  clears flags. */
#define IEM_MC_RETN_AND_FINISH_THREADED_PC16_WITH_FLAGS(a_u16Pop, a_cbInstr) \
    IEM_MC_RETN_AND_FINISH_THREADED_PC16(a_u16Pop, a_cbInstr); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off)

/** Variant of IEM_MC_RETN_AND_FINISH for 386+ targets that checks and
 *  clears flags. */
#define IEM_MC_RETN_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_u16Pop, a_cbInstr, a_enmEffOpSize) \
    IEM_MC_RETN_AND_FINISH_THREADED_PC32(a_u16Pop, a_cbInstr, a_enmEffOpSize); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off)

/** Variant of IEM_MC_RETN_AND_FINISH for use in 64-bit code that checks and
 *  clears flags. */
#define IEM_MC_RETN_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_u16Pop, a_cbInstr, a_enmEffOpSize) \
    IEM_MC_RETN_AND_FINISH_THREADED_PC64(a_u16Pop, a_cbInstr, a_enmEffOpSize); \
    off = iemNativeEmitFinishInstructionFlagsCheck(pReNative, off)

/** IEM_MC[|_FLAT32|_FLAT64]_RETN_AND_FINISH */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitRetn(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t cbInstr, uint16_t cbPop, bool f64Bit,
                  IEMMODE enmEffOpSize, uint8_t idxInstr)
{
    RT_NOREF(cbInstr);

#ifdef VBOX_STRICT
    /*
     * Check that the fExec flags we've got make sense.
     */
    off = iemNativeEmitExecFlagsCheck(pReNative, off, pReNative->fExec);
#endif

    /*
     * To keep things simple we have to commit any pending writes first as we
     * may end up making calls.
     */
    off = iemNativeRegFlushPendingWrites(pReNative, off);

    /*
     * Determine the effective stack pointer, for non-FLAT modes we also update RSP.
     * For FLAT modes we'll do this in TlbDone as we'll be using the incoming RSP
     * directly as the effective stack pointer.
     * (Code structure is very similar to that of PUSH)
     *
     * Note! As a simplification, we treat opsize overridden returns (o16 ret)
     *       in FLAT 32-bit mode as if we weren't in FLAT mode since these
     *       aren't commonly used (or useful) and thus not in need of optimizing.
     *
     * Note! For non flat modes the guest RSP is not allocated for update but rather for calculation
     *       as the shadowed register would remain modified even if the return address throws a \#GP(0)
     *       due to being outside the CS limit causing a wrong stack pointer value in the guest (see
     *       the near return testcase in bs3-cpu-basic-2). If no exception is thrown the shadowing is transfered
     *       to the new register returned by iemNativeRegAllocTmpForGuestReg() at the end.
     */
    uint8_t   const cbMem           =   enmEffOpSize == IEMMODE_64BIT
                                      ? sizeof(uint64_t)
                                      : enmEffOpSize == IEMMODE_32BIT
                                      ? sizeof(uint32_t)
                                      : sizeof(uint16_t);
    bool      const fFlat           = IEM_F_MODE_X86_IS_FLAT(pReNative->fExec) && enmEffOpSize != IEMMODE_16BIT; /* see note */
    uintptr_t const pfnFunction     = fFlat
                                      ?   enmEffOpSize == IEMMODE_64BIT
                                        ? (uintptr_t)iemNativeHlpStackFlatFetchU64
                                        : (uintptr_t)iemNativeHlpStackFlatFetchU32
                                      :   enmEffOpSize == IEMMODE_32BIT
                                        ? (uintptr_t)iemNativeHlpStackFetchU32
                                        : (uintptr_t)iemNativeHlpStackFetchU16;
    uint8_t   const idxRegRsp       = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(X86_GREG_xSP),
                                                                      fFlat ? kIemNativeGstRegUse_ForUpdate
                                                                            : kIemNativeGstRegUse_Calculation,
                                                                      true /*fNoVolatileRegs*/);
    uint8_t   const idxRegEffSp     = fFlat ? idxRegRsp : iemNativeRegAllocTmp(pReNative, &off);
    /** @todo can do a better job picking the register here. For cbMem >= 4 this
     *        will be the resulting register value. */
    uint8_t   const idxRegMemResult = iemNativeRegAllocTmp(pReNative, &off); /* pointer then value; arm64 SP += 2/4 helper too. */

    uint32_t        offFixupJumpToUseOtherBitSp = UINT32_MAX;
    if (fFlat)
        Assert(idxRegEffSp == idxRegRsp);
    else /** @todo We can skip the test if we're targeting pre-386 CPUs. */
    {
        Assert(idxRegEffSp != idxRegRsp);
        uint8_t const idxRegSsAttr = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_SEG_ATTRIB(X86_SREG_SS),
                                                                     kIemNativeGstRegUse_ReadOnly);
#ifdef RT_ARCH_AMD64
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 32);
#else
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 12);
#endif
        off = iemNativeEmitTestAnyBitsInGpr32Ex(pCodeBuf, off, idxRegSsAttr, X86DESCATTR_D);
        iemNativeRegFreeTmp(pReNative, idxRegSsAttr);
        offFixupJumpToUseOtherBitSp = off;
        if (enmEffOpSize == IEMMODE_32BIT)
        {
            off = iemNativeEmitJccToFixedEx(pCodeBuf, off, off /*8-bit suffices*/, kIemNativeInstrCond_e); /* jump if zero */
            off = iemNativeEmitStackPopForRetnUse32Sp(pCodeBuf, off, idxRegRsp, idxRegEffSp, cbMem, cbPop);
        }
        else
        {
            Assert(enmEffOpSize == IEMMODE_16BIT);
            off = iemNativeEmitJccToFixedEx(pCodeBuf, off, off /*8-bit suffices*/, kIemNativeInstrCond_ne); /* jump if not zero */
            off = iemNativeEmitStackPopForRetnUse16Sp(pCodeBuf, off, idxRegRsp, idxRegEffSp, cbMem, cbPop,
                                                      idxRegMemResult);
        }
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    }
    /* SpUpdateEnd: */
    uint32_t const offLabelSpUpdateEnd = off;

    /*
     * Okay, now prepare for TLB lookup and jump to code (or the TlbMiss if
     * we're skipping lookup).
     */
    uint8_t const  iSegReg           = fFlat ? UINT8_MAX : X86_SREG_SS;
    IEMNATIVEEMITTLBSTATE const TlbState(pReNative, idxRegEffSp, &off, iSegReg, cbMem);
    uint16_t const uTlbSeqNo         = pReNative->uTlbSeqNo++;
    uint32_t const idxLabelTlbMiss   = iemNativeLabelCreate(pReNative, kIemNativeLabelType_TlbMiss, UINT32_MAX, uTlbSeqNo);
    uint32_t const idxLabelTlbLookup = !TlbState.fSkip
                                     ? iemNativeLabelCreate(pReNative, kIemNativeLabelType_TlbLookup, UINT32_MAX, uTlbSeqNo)
                                     : UINT32_MAX;

    if (!TlbState.fSkip)
        off = iemNativeEmitJmpToLabel(pReNative, off, idxLabelTlbLookup); /** @todo short jump */
    else
        off = iemNativeEmitJmpToLabel(pReNative, off, idxLabelTlbMiss); /** @todo short jump */

    /*
     * Use16BitSp:
     */
    if (!fFlat)
    {
#ifdef RT_ARCH_AMD64
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 32);
#else
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 12);
#endif
        iemNativeFixupFixedJump(pReNative, offFixupJumpToUseOtherBitSp, off);
        if ((pReNative->fExec & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_32BIT)
            off = iemNativeEmitStackPopForRetnUse16Sp(pCodeBuf, off, idxRegRsp, idxRegEffSp, cbMem, cbPop,
                                                      idxRegMemResult);
        else
            off = iemNativeEmitStackPopForRetnUse32Sp(pCodeBuf, off, idxRegRsp, idxRegEffSp, cbMem, cbPop);
        off = iemNativeEmitJmpToFixedEx(pCodeBuf, off, offLabelSpUpdateEnd);
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    }

    /*
     * TlbMiss:
     *
     * Call helper to do the pushing.
     */
    iemNativeLabelDefine(pReNative, idxLabelTlbMiss, off);

#ifdef IEMNATIVE_WITH_INSTRUCTION_COUNTING
    off = iemNativeEmitStoreImmToVCpuU8(pReNative, off, idxInstr, RT_UOFFSETOF(VMCPUCC, iem.s.idxTbCurInstr));
#else
    RT_NOREF(idxInstr);
#endif

    uint32_t const fHstRegsNotToSave = TlbState.getRegsNotToSave()
                                     | (idxRegMemResult < RT_ELEMENTS(pReNative->Core.aHstRegs) ? RT_BIT_32(idxRegMemResult) : 0)
                                     | (idxRegEffSp != idxRegRsp ? RT_BIT_32(idxRegEffSp) : 0);
    off = iemNativeVarSaveVolatileRegsPreHlpCall(pReNative, off, fHstRegsNotToSave);


    /* IEMNATIVE_CALL_ARG1_GREG = EffSp/RSP */
    if (idxRegEffSp != IEMNATIVE_CALL_ARG1_GREG)
        off = iemNativeEmitLoadGprFromGpr(pReNative, off, IEMNATIVE_CALL_ARG1_GREG, idxRegEffSp);

#ifdef IEMNATIVE_WITH_EFLAGS_POSTPONING
    /* Do delayed EFLAGS calculations. */
    off = iemNativeDoPostponedEFlagsAtTlbMiss<RT_BIT_32(IEMNATIVE_CALL_ARG1_GREG)>(pReNative, off, &TlbState, fHstRegsNotToSave);
#endif

    /* IEMNATIVE_CALL_ARG0_GREG = pVCpu */
    off = iemNativeEmitLoadGprFromGpr(pReNative, off, IEMNATIVE_CALL_ARG0_GREG, IEMNATIVE_REG_FIXED_PVMCPU);

    /* Done setting up parameters, make the call. */
    off = iemNativeEmitCallImm<true /*a_fSkipEflChecks*/>(pReNative, off, pfnFunction);

    /* Move the return register content to idxRegMemResult. */
    if (idxRegMemResult != IEMNATIVE_CALL_RET_GREG)
        off = iemNativeEmitLoadGprFromGpr(pReNative, off, idxRegMemResult, IEMNATIVE_CALL_RET_GREG);

    /* Restore variables and guest shadow registers to volatile registers. */
    off = iemNativeVarRestoreVolatileRegsPostHlpCall(pReNative, off, fHstRegsNotToSave);
    off = iemNativeRegRestoreGuestShadowsInVolatileRegs(pReNative, off, TlbState.getActiveRegsWithShadows());

#ifdef IEMNATIVE_WITH_TLB_LOOKUP
    if (!TlbState.fSkip)
    {
        /* end of TlbMiss - Jump to the done label. */
        uint32_t const idxLabelTlbDone = iemNativeLabelCreate(pReNative, kIemNativeLabelType_TlbDone, UINT32_MAX, uTlbSeqNo);
        off = iemNativeEmitJmpToLabel(pReNative, off, idxLabelTlbDone);

        /*
         * TlbLookup:
         */
        off = iemNativeEmitTlbLookup<true>(pReNative, off, &TlbState, iSegReg, cbMem, cbMem - 1, IEM_ACCESS_TYPE_READ,
                                           idxLabelTlbLookup, idxLabelTlbMiss, idxRegMemResult);

        /*
         * Emit code to load the value (from idxRegMemResult into idxRegMemResult).
         */
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 32);
# ifdef IEM_WITH_TLB_STATISTICS
        off = iemNativeEmitIncStamCounterInVCpuEx(pCodeBuf, off, TlbState.idxReg1, TlbState.idxReg2,
                                                  RT_UOFFSETOF(VMCPUCC, iem.s.StatNativeTlbHitsForStack));
# endif
        switch (cbMem)
        {
            case 2:
                off = iemNativeEmitLoadGprByGprU16Ex(pCodeBuf, off, idxRegMemResult, idxRegMemResult);
                break;
            case 4:
                off = iemNativeEmitLoadGprByGprU32Ex(pCodeBuf, off, idxRegMemResult, idxRegMemResult);
                break;
            case 8:
                off = iemNativeEmitLoadGprByGprU64Ex(pCodeBuf, off, idxRegMemResult, idxRegMemResult);
                break;
            default:
                AssertFailed();
        }

        TlbState.freeRegsAndReleaseVars(pReNative);

        /*
         * TlbDone:
         *
         * Set the new RSP value (FLAT accesses needs to calculate it first) and
         * commit the popped register value.
         */
        iemNativeLabelDefine(pReNative, idxLabelTlbDone, off);
    }
#endif /* IEMNATIVE_WITH_TLB_LOOKUP */

    /* Check limit before committing RIP and RSP (may #GP(0) + exit TB). */
    if (!f64Bit)
/** @todo we can skip this test in FLAT 32-bit mode. */
        off = iemNativeEmitCheckGpr32AgainstCsSegLimitMaybeRaiseGp0(pReNative, off, idxRegMemResult, idxInstr);
    /* Check that the address is canonical, raising #GP(0) + exit TB if it isn't. */
    else if (enmEffOpSize == IEMMODE_64BIT)
        off = iemNativeEmitCheckGprCanonicalMaybeRaiseGp0(pReNative, off, idxRegMemResult, idxInstr);

    /* Complete RSP calculation for FLAT mode. */
    if (idxRegEffSp == idxRegRsp)
    {
        if (enmEffOpSize == IEMMODE_64BIT)
            off = iemNativeEmitAddGprImm(pReNative, off, idxRegRsp, sizeof(uint64_t) + cbPop);
        else
        {
            Assert(enmEffOpSize == IEMMODE_32BIT);
            off = iemNativeEmitAddGpr32Imm(pReNative, off, idxRegRsp, sizeof(uint32_t) + cbPop);
        }
    }

    /* Commit the result and clear any current guest shadows for RIP. */
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxRegRsp,       RT_UOFFSETOF(VMCPU, cpum.GstCtx.rsp));
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxRegMemResult, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rip));
    iemNativeRegClearAndMarkAsGstRegShadow(pReNative, idxRegMemResult, kIemNativeGstReg_Pc, off);
#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING_DEBUG
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxRegMemResult, RT_UOFFSETOF(VMCPU, iem.s.uPcUpdatingDebug));
    pReNative->Core.fDebugPcInitialized = true;
    Log4(("uPcUpdatingDebug=rip/ret off=%#x\n", off));
#endif

    /* Need to transfer the shadowing information to the host register containing the updated value now. */
    if (!fFlat)
        iemNativeRegClearAndMarkAsGstRegShadow(pReNative, idxRegRsp, IEMNATIVEGSTREG_GPR(X86_GREG_xSP), off);

    iemNativeRegFreeTmp(pReNative, idxRegRsp);
    if (idxRegEffSp != idxRegRsp)
        iemNativeRegFreeTmp(pReNative, idxRegEffSp);
    iemNativeRegFreeTmp(pReNative, idxRegMemResult);
    return off;
}


/*********************************************************************************************************************************
*   Emitters for raising exceptions (IEM_MC_MAYBE_RAISE_XXX)                                                                     *
*********************************************************************************************************************************/

#define IEM_MC_MAYBE_RAISE_DEVICE_NOT_AVAILABLE() \
    off = iemNativeEmitMaybeRaiseDeviceNotAvailable(pReNative, off, pCallEntry->idxInstr)

/**
 * Emits code to check if a \#NM exception should be raised.
 *
 * @returns New code buffer offset, UINT32_MAX on failure.
 * @param   pReNative       The native recompile state.
 * @param   off             The code buffer offset.
 * @param   idxInstr        The current instruction.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitMaybeRaiseDeviceNotAvailable(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxInstr)
{
#ifdef IEMNATIVE_WITH_SIMD_REG_ALLOCATOR
    STAM_COUNTER_INC(&pReNative->pVCpu->iem.s.StatNativeMaybeDeviceNotAvailXcptCheckPotential);

    if (!(pReNative->fSimdRaiseXcptChecksEmitted & IEMNATIVE_SIMD_RAISE_XCPT_CHECKS_EMITTED_MAYBE_DEVICE_NOT_AVAILABLE))
    {
#endif
        /*
         * Make sure we don't have any outstanding guest register writes as we may
         * raise an #NM and all guest register must be up to date in CPUMCTX.
         */
        /** @todo r=aeichner Can we postpone this to the RaiseNm path? */
        off = iemNativeRegFlushPendingWrites(pReNative, off);

#ifdef IEMNATIVE_WITH_INSTRUCTION_COUNTING
        off = iemNativeEmitStoreImmToVCpuU8(pReNative, off, idxInstr, RT_UOFFSETOF(VMCPUCC, iem.s.idxTbCurInstr));
#else
        RT_NOREF(idxInstr);
#endif

        /* Allocate a temporary CR0 register. */
        uint8_t const idxCr0Reg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_Cr0,
                                                                  kIemNativeGstRegUse_ReadOnly);

        /*
         * if (cr0 & (X86_CR0_EM | X86_CR0_TS) != 0)
         *     return raisexcpt();
         */
        /* Test and jump. */
        off = iemNativeEmitTbExitIfAnyBitsSetInGpr<kIemNativeLabelType_RaiseNm>(pReNative, off, idxCr0Reg,
                                                                                X86_CR0_EM | X86_CR0_TS);

        /* Free but don't flush the CR0 register. */
        iemNativeRegFreeTmp(pReNative, idxCr0Reg);

#ifdef IEMNATIVE_WITH_SIMD_REG_ALLOCATOR
        pReNative->fSimdRaiseXcptChecksEmitted |= IEMNATIVE_SIMD_RAISE_XCPT_CHECKS_EMITTED_MAYBE_DEVICE_NOT_AVAILABLE;
    }
    else
        STAM_COUNTER_INC(&pReNative->pVCpu->iem.s.StatNativeMaybeDeviceNotAvailXcptCheckOmitted);
#endif

    return off;
}


#define IEM_MC_MAYBE_RAISE_WAIT_DEVICE_NOT_AVAILABLE() \
    off = iemNativeEmitMaybeRaiseWaitDeviceNotAvailable(pReNative, off, pCallEntry->idxInstr)

/**
 * Emits code to check if a \#NM exception should be raised.
 *
 * @returns New code buffer offset, UINT32_MAX on failure.
 * @param   pReNative       The native recompile state.
 * @param   off             The code buffer offset.
 * @param   idxInstr        The current instruction.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitMaybeRaiseWaitDeviceNotAvailable(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxInstr)
{
#ifdef IEMNATIVE_WITH_SIMD_REG_ALLOCATOR
    STAM_COUNTER_INC(&pReNative->pVCpu->iem.s.StatNativeMaybeWaitDeviceNotAvailXcptCheckPotential);

    if (!(pReNative->fSimdRaiseXcptChecksEmitted & IEMNATIVE_SIMD_RAISE_XCPT_CHECKS_EMITTED_MAYBE_WAIT_DEVICE_NOT_AVAILABLE))
    {
#endif
        /*
         * Make sure we don't have any outstanding guest register writes as we may
         * raise an #NM and all guest register must be up to date in CPUMCTX.
         */
        /** @todo r=aeichner Can we postpone this to the RaiseNm path? */
        off = iemNativeRegFlushPendingWrites(pReNative, off);

#ifdef IEMNATIVE_WITH_INSTRUCTION_COUNTING
        off = iemNativeEmitStoreImmToVCpuU8(pReNative, off, idxInstr, RT_UOFFSETOF(VMCPUCC, iem.s.idxTbCurInstr));
#else
        RT_NOREF(idxInstr);
#endif

        /* Allocate a temporary CR0 register. */
        uint8_t const idxCr0Reg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_Cr0,
                                                                  kIemNativeGstRegUse_Calculation);

        /*
         * if (cr0 & (X86_CR0_MP | X86_CR0_TS) == (X86_CR0_MP | X86_CR0_TS))
         *     return raisexcpt();
         */
        off = iemNativeEmitAndGpr32ByImm(pReNative, off, idxCr0Reg, X86_CR0_MP | X86_CR0_TS);
        /* Test and jump. */
        off = iemNativeEmitTbExitIfGpr32EqualsImm<kIemNativeLabelType_RaiseNm>(pReNative, off, idxCr0Reg, X86_CR0_MP | X86_CR0_TS);

        /* Free the CR0 register. */
        iemNativeRegFreeTmp(pReNative, idxCr0Reg);

#ifdef IEMNATIVE_WITH_SIMD_REG_ALLOCATOR
        pReNative->fSimdRaiseXcptChecksEmitted |= IEMNATIVE_SIMD_RAISE_XCPT_CHECKS_EMITTED_MAYBE_WAIT_DEVICE_NOT_AVAILABLE;
    }
    else
        STAM_COUNTER_INC(&pReNative->pVCpu->iem.s.StatNativeMaybeWaitDeviceNotAvailXcptCheckOmitted);
#endif

    return off;
}


#define IEM_MC_MAYBE_RAISE_FPU_XCPT() \
    off = iemNativeEmitMaybeRaiseFpuException(pReNative, off, pCallEntry->idxInstr)

/**
 * Emits code to check if a \#MF exception should be raised.
 *
 * @returns New code buffer offset, UINT32_MAX on failure.
 * @param   pReNative       The native recompile state.
 * @param   off             The code buffer offset.
 * @param   idxInstr        The current instruction.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitMaybeRaiseFpuException(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxInstr)
{
    /*
     * Make sure we don't have any outstanding guest register writes as we may
     * raise an #MF and all guest register must be up to date in CPUMCTX.
     */
    /** @todo r=aeichner Can we postpone this to the RaiseMf path? */
    off = iemNativeRegFlushPendingWrites(pReNative, off);

#ifdef IEMNATIVE_WITH_INSTRUCTION_COUNTING
    off = iemNativeEmitStoreImmToVCpuU8(pReNative, off, idxInstr, RT_UOFFSETOF(VMCPUCC, iem.s.idxTbCurInstr));
#else
    RT_NOREF(idxInstr);
#endif

    /* Allocate a temporary FSW register. */
    uint8_t const idxFpuFswReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_FpuFsw,
                                                                 kIemNativeGstRegUse_ReadOnly);

    /*
     * if (FSW & X86_FSW_ES != 0)
     *     return raisexcpt();
     */
    /* Test and jump. */
    off = iemNativeEmitTbExitIfBitSetInGpr<kIemNativeLabelType_RaiseMf>(pReNative, off, idxFpuFswReg, X86_FSW_ES_BIT);

    /* Free but don't flush the FSW register. */
    iemNativeRegFreeTmp(pReNative, idxFpuFswReg);

    return off;
}


#define IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT() \
    off = iemNativeEmitMaybeRaiseSseRelatedXcpt(pReNative, off, pCallEntry->idxInstr)

/**
 * Emits code to check if a SSE exception (either \#UD or \#NM) should be raised.
 *
 * @returns New code buffer offset, UINT32_MAX on failure.
 * @param   pReNative       The native recompile state.
 * @param   off             The code buffer offset.
 * @param   idxInstr        The current instruction.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitMaybeRaiseSseRelatedXcpt(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxInstr)
{
#ifdef IEMNATIVE_WITH_SIMD_REG_ALLOCATOR
    STAM_COUNTER_INC(&pReNative->pVCpu->iem.s.StatNativeMaybeSseXcptCheckPotential);

    if (!(pReNative->fSimdRaiseXcptChecksEmitted & IEMNATIVE_SIMD_RAISE_XCPT_CHECKS_EMITTED_MAYBE_SSE))
    {
#endif
        /*
         * Make sure we don't have any outstanding guest register writes as we may
         * raise an \#UD or \#NM and all guest register must be up to date in CPUMCTX.
         */
        /** @todo r=aeichner Can we postpone this to the RaiseNm/RaiseUd path? */
        off = iemNativeRegFlushPendingWrites(pReNative, off);

#ifdef IEMNATIVE_WITH_INSTRUCTION_COUNTING
        off = iemNativeEmitStoreImmToVCpuU8(pReNative, off, idxInstr, RT_UOFFSETOF(VMCPUCC, iem.s.idxTbCurInstr));
#else
        RT_NOREF(idxInstr);
#endif

        /* Allocate a temporary CR0 and CR4 register. */
        uint8_t const idxCr0Reg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_Cr0);
        uint8_t const idxCr4Reg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_Cr4);
        uint8_t const idxTmpReg = iemNativeRegAllocTmp(pReNative, &off);

        AssertCompile(!((X86_CR0_EM | X86_CR0_TS) & X86_CR4_OSFXSR));
#ifdef RT_ARCH_AMD64
        /*
         * We do a modified test here:
         *  if (!(((cr4 & X86_CR4_OSFXSR) | cr0) ^ X86_CR4_OSFXSR)) { likely }
         *  else                                                    { goto RaiseSseRelated; }
         * This ASSUMES that CR0[bit 9] is always zero.  This is the case on
         * all targets except the 386, which doesn't support SSE, this should
         * be a safe assumption.
         */
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off,
                                                                 1+6+3+3+7+7+6 + IEMNATIVE_MAX_POSTPONED_EFLAGS_INSTRUCTIONS);
        //pCodeBuf[off++] = 0xcc;
        off = iemNativeEmitLoadGpr32ImmEx(pCodeBuf, off,    idxTmpReg, X86_CR4_OSFXSR); /* Isolate CR4.OSFXSR as CR4.TSD and */
        off = iemNativeEmitAndGpr32ByGpr32Ex(pCodeBuf, off, idxTmpReg, idxCr4Reg);      /* CR4.DE would overlap the CR0 bits. */
        off = iemNativeEmitOrGpr32ByGprEx(pCodeBuf, off,    idxTmpReg, idxCr0Reg);
        off = iemNativeEmitAndGpr32ByImmEx(pCodeBuf, off,   idxTmpReg, X86_CR0_EM | X86_CR0_TS | X86_CR4_OSFXSR);
        off = iemNativeEmitXorGpr32ByImmEx(pCodeBuf, off,   idxTmpReg, X86_CR4_OSFXSR);
        off = iemNativeEmitTbExitJccEx<kIemNativeLabelType_RaiseSseRelated>(pReNative, pCodeBuf, off, kIemNativeInstrCond_ne);

#elif defined(RT_ARCH_ARM64)
        /*
         * We do a modified test here:
         *  if (!((cr0 & (X86_CR0_EM | X86_CR0_TS)) | (((cr4 >> X86_CR4_OSFXSR_BIT) & 1) ^ 1))) { likely }
         *  else                                                                                { goto RaiseSseRelated; }
         */
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off,
                                                                 1+5 + IEMNATIVE_MAX_POSTPONED_EFLAGS_INSTRUCTIONS);
        //pCodeBuf[off++] = Armv8A64MkInstrBrk(0x1111);
        Assert(Armv8A64ConvertImmRImmS2Mask32(1, 32 - X86_CR0_EM_BIT) == (X86_CR0_EM | X86_CR0_TS));
        pCodeBuf[off++] = Armv8A64MkInstrAndImm(idxTmpReg, idxCr0Reg, 1, 32 - X86_CR0_EM_BIT, false /*f64Bit*/);
        pCodeBuf[off++] = Armv8A64MkInstrBfxil(idxTmpReg, idxCr4Reg, X86_CR4_OSFXSR_BIT, 1, false /*f64Bit*/);
        /* -> idxTmpReg[0]=OSFXSR;  idxTmpReg[2]=EM; idxTmpReg[3]=TS; (the rest is zero) */
        Assert(Armv8A64ConvertImmRImmS2Mask32(0, 0) == 1);
        pCodeBuf[off++] = Armv8A64MkInstrEorImm(idxTmpReg, idxTmpReg, 0, 0, false /*f64Bit*/);
        /* -> idxTmpReg[0]=~OSFXSR; idxTmpReg[2]=EM; idxTmpReg[3]=TS; (the rest is zero) */
        off = iemNativeEmitTbExitIfGprIsNotZeroEx<kIemNativeLabelType_RaiseSseRelated>(pReNative, pCodeBuf, off,
                                                                                       idxTmpReg, false /*f64Bit*/);

#else
# error "Port me!"
#endif

        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
        iemNativeRegFreeTmp(pReNative, idxTmpReg);
        iemNativeRegFreeTmp(pReNative, idxCr0Reg);
        iemNativeRegFreeTmp(pReNative, idxCr4Reg);

#ifdef IEMNATIVE_WITH_SIMD_REG_ALLOCATOR
        pReNative->fSimdRaiseXcptChecksEmitted |= IEMNATIVE_SIMD_RAISE_XCPT_CHECKS_EMITTED_MAYBE_SSE;
    }
    else
        STAM_COUNTER_INC(&pReNative->pVCpu->iem.s.StatNativeMaybeSseXcptCheckOmitted);
#endif

    return off;
}


#define IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT() \
    off = iemNativeEmitMaybeRaiseAvxRelatedXcpt(pReNative, off, pCallEntry->idxInstr)

/**
 * Emits code to check if a AVX exception (either \#UD or \#NM) should be raised.
 *
 * @returns New code buffer offset, UINT32_MAX on failure.
 * @param   pReNative       The native recompile state.
 * @param   off             The code buffer offset.
 * @param   idxInstr        The current instruction.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitMaybeRaiseAvxRelatedXcpt(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxInstr)
{
#ifdef IEMNATIVE_WITH_SIMD_REG_ALLOCATOR
    STAM_COUNTER_INC(&pReNative->pVCpu->iem.s.StatNativeMaybeAvxXcptCheckPotential);

    if (!(pReNative->fSimdRaiseXcptChecksEmitted & IEMNATIVE_SIMD_RAISE_XCPT_CHECKS_EMITTED_MAYBE_AVX))
    {
#endif
        /*
         * Make sure we don't have any outstanding guest register writes as we may
         * raise an \#UD or \#NM and all guest register must be up to date in CPUMCTX.
         */
        /** @todo r=aeichner Can we postpone this to the RaiseNm/RaiseUd path? */
        off = iemNativeRegFlushPendingWrites(pReNative, off);

#ifdef IEMNATIVE_WITH_INSTRUCTION_COUNTING
        off = iemNativeEmitStoreImmToVCpuU8(pReNative, off, idxInstr, RT_UOFFSETOF(VMCPUCC, iem.s.idxTbCurInstr));
#else
        RT_NOREF(idxInstr);
#endif

        /* Allocate a temporary CR0, CR4 and XCR0 register. */
        uint8_t const idxCr0Reg  = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_Cr0);
        uint8_t const idxCr4Reg  = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_Cr4);
        uint8_t const idxXcr0Reg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_Xcr0);
        uint8_t const idxTmpReg  = iemNativeRegAllocTmp(pReNative, &off);

        /*
         * We have the following in IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT:
         *  if (RT_LIKELY(   (  (pVCpu->cpum.GstCtx.aXcr[0] & (XSAVE_C_YMM | XSAVE_C_SSE))
         *                    | (pVCpu->cpum.GstCtx.cr4     & X86_CR4_OSXSAVE)
         *                    | (pVCpu->cpum.GstCtx.cr0     & X86_CR0_TS))
         *                == (XSAVE_C_YMM | XSAVE_C_SSE | X86_CR4_OSXSAVE)))
         *       { likely }
         *  else { goto RaiseAvxRelated; }
         */
#ifdef RT_ARCH_AMD64
        /*  if (!(  (  ((xcr0 & (XSAVE_C_YMM | XSAVE_C_SSE)) << 2)
                     | (((cr4 >> X86_CR4_OSFXSR_BIT) & 1)    << 1)
                     | ((cr0 >> X86_CR0_TS_BIT)      & 1)         )
                  ^ 0x1a) ) { likely }
            else            { goto RaiseAvxRelated; } */
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off,
                                                                 1+6+3+5+3+5+3+7+6 + IEMNATIVE_MAX_POSTPONED_EFLAGS_INSTRUCTIONS);
        //pCodeBuf[off++] = 0xcc;
        off = iemNativeEmitLoadGpr32ImmEx(pCodeBuf, off,                 idxTmpReg, XSAVE_C_YMM | XSAVE_C_SSE);
        off = iemNativeEmitAndGpr32ByGpr32Ex(pCodeBuf, off,              idxTmpReg, idxXcr0Reg);
        off = iemNativeEmitAmd64TestBitInGprEx(pCodeBuf, off,            idxCr4Reg, X86_CR4_OSXSAVE_BIT);
        off = iemNativeEmitAmd64RotateGpr32LeftViaCarryEx(pCodeBuf, off, idxTmpReg, 1);
        /* -> idxTmpReg[0]=CR4.OSXSAVE;  idxTmpReg[1]=0; idxTmpReg[2]=SSE;  idxTmpReg[3]=YMM; (the rest is zero) */
        off = iemNativeEmitAmd64TestBitInGprEx(pCodeBuf, off,            idxCr0Reg, X86_CR0_TS_BIT);
        off = iemNativeEmitAmd64RotateGpr32LeftViaCarryEx(pCodeBuf, off, idxTmpReg, 1);
        /* -> idxTmpReg[0]=CR0.TS idxTmpReg[1]=CR4.OSXSAVE; idxTmpReg[2]=0; idxTmpReg[3]=SSE; idxTmpReg[4]=YMM; */
        off = iemNativeEmitXorGpr32ByImmEx(pCodeBuf, off,                idxTmpReg, ((XSAVE_C_YMM | XSAVE_C_SSE) << 2) | 2);
        /* -> idxTmpReg[0]=CR0.TS idxTmpReg[1]=~CR4.OSXSAVE; idxTmpReg[2]=0; idxTmpReg[3]=~SSE; idxTmpReg[4]=~YMM; */
        off = iemNativeEmitTbExitJccEx<kIemNativeLabelType_RaiseAvxRelated>(pReNative, pCodeBuf, off, kIemNativeInstrCond_ne);

#elif defined(RT_ARCH_ARM64)
        /*  if (!(  (((xcr0 & (XSAVE_C_YMM | XSAVE_C_SSE)) | ((cr4 >> X86_CR4_OSFXSR_BIT) & 1)) ^ 7) << 1)
                  | ((cr0 >> X86_CR0_TS_BIT) & 1) ) { likely }
            else                                    { goto RaiseAvxRelated; } */
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off,
                                                                 1+6 + IEMNATIVE_MAX_POSTPONED_EFLAGS_INSTRUCTIONS);
        //pCodeBuf[off++] = Armv8A64MkInstrBrk(0x1111);
        Assert(Armv8A64ConvertImmRImmS2Mask32(1, 32 - XSAVE_C_SSE_BIT) == (XSAVE_C_YMM | XSAVE_C_SSE));
        pCodeBuf[off++] = Armv8A64MkInstrAndImm(idxTmpReg, idxXcr0Reg, 1, 32 - XSAVE_C_SSE_BIT, false /*f64Bit*/);
        pCodeBuf[off++] = Armv8A64MkInstrBfxil(idxTmpReg, idxCr4Reg, X86_CR4_OSXSAVE_BIT, 1, false /*f64Bit*/);
        /* -> idxTmpReg[0]=CR4.OSXSAVE;  idxTmpReg[1]=SSE;  idxTmpReg[2]=YMM; (the rest is zero) */
        Assert(Armv8A64ConvertImmRImmS2Mask32(2, 0) == 7);
        pCodeBuf[off++] = Armv8A64MkInstrEorImm(idxTmpReg, idxTmpReg, 2, 0, false /*f64Bit*/);
        /* -> idxTmpReg[0]=~CR4.OSXSAVE; idxTmpReg[1]=~SSE; idxTmpReg[2]=~YMM; (the rest is zero) */
        pCodeBuf[off++] = Armv8A64MkInstrLslImm(idxTmpReg, idxTmpReg, 1, false /*f64Bit*/);
        pCodeBuf[off++] = Armv8A64MkInstrBfxil(idxTmpReg, idxCr0Reg, X86_CR0_TS_BIT, 1, false /*f64Bit*/);
        /* -> idxTmpReg[0]=CR0.TS; idxTmpReg[1]=~CR4.OSXSAVE; idxTmpReg[2]=~SSE; idxTmpReg[3]=~YMM; (the rest is zero) */
        off = iemNativeEmitTbExitIfGprIsNotZeroEx<kIemNativeLabelType_RaiseAvxRelated>(pReNative, pCodeBuf, off,
                                                                                       idxTmpReg, false /*f64Bit*/);

#else
# error "Port me!"
#endif

        iemNativeRegFreeTmp(pReNative, idxTmpReg);
        iemNativeRegFreeTmp(pReNative, idxCr0Reg);
        iemNativeRegFreeTmp(pReNative, idxCr4Reg);
        iemNativeRegFreeTmp(pReNative, idxXcr0Reg);
#ifdef IEMNATIVE_WITH_SIMD_REG_ALLOCATOR
        pReNative->fSimdRaiseXcptChecksEmitted |= IEMNATIVE_SIMD_RAISE_XCPT_CHECKS_EMITTED_MAYBE_AVX;
    }
    else
        STAM_COUNTER_INC(&pReNative->pVCpu->iem.s.StatNativeMaybeAvxXcptCheckOmitted);
#endif

    return off;
}


#define IEM_MC_RAISE_DIVIDE_ERROR_IF_LOCAL_IS_ZERO(a_uVar) \
    off = iemNativeEmitRaiseDivideErrorIfLocalIsZero(pReNative, off, a_uVar, pCallEntry->idxInstr)

/**
 * Emits code to raise a \#DE if a local variable is zero.
 *
 * @returns New code buffer offset, UINT32_MAX on failure.
 * @param   pReNative       The native recompile state.
 * @param   off             The code buffer offset.
 * @param   idxVar          The variable to check. This must be 32-bit (EFLAGS).
 * @param   idxInstr        The current instruction.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitRaiseDivideErrorIfLocalIsZero(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVar, uint8_t idxInstr)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxVar, sizeof(uint32_t));

    /* Make sure we don't have any outstanding guest register writes as we may. */
    off = iemNativeRegFlushPendingWrites(pReNative, off);

    /* Set the instruction number if we're counting. */
#ifdef IEMNATIVE_WITH_INSTRUCTION_COUNTING
    off = iemNativeEmitStoreImmToVCpuU8(pReNative, off, idxInstr, RT_UOFFSETOF(VMCPUCC, iem.s.idxTbCurInstr));
#else
    RT_NOREF(idxInstr);
#endif

    /* Do the job we're here for. */
    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxVar, &off);
    off = iemNativeEmitTbExitIfGprIsZero<kIemNativeLabelType_RaiseDe>(pReNative, off, idxVarReg, false /*f64Bit*/);
    iemNativeVarRegisterRelease(pReNative, idxVar);

    return off;
}


#define IEM_MC_RAISE_GP0_IF_EFF_ADDR_UNALIGNED(a_EffAddr, a_cbAlign) \
    off = iemNativeEmitRaiseGp0IfEffAddrUnaligned(pReNative, off, pCallEntry->idxInstr, a_EffAddr, a_cbAlign)

/**
 * Emits code to raise a \#GP(0) if the given variable contains an unaligned address.
 *
 * @returns New code buffer offset, UINT32_MAX on failure.
 * @param   pReNative       The native recompile state.
 * @param   off             The code buffer offset.
 * @param   idxInstr        The current instruction.
 * @param   idxVarEffAddr   Index of the variable containing the effective address to check.
 * @param   cbAlign         The alignment in bytes to check against.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitRaiseGp0IfEffAddrUnaligned(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxInstr,
                                        uint8_t idxVarEffAddr, uint8_t cbAlign)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVarEffAddr);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxVarEffAddr, sizeof(RTGCPTR));

    /*
     * Make sure we don't have any outstanding guest register writes as we may throw an exception.
     */
    off = iemNativeRegFlushPendingWrites(pReNative, off);

#ifdef IEMNATIVE_WITH_INSTRUCTION_COUNTING
    off = iemNativeEmitStoreImmToVCpuU8(pReNative, off, idxInstr, RT_UOFFSETOF(VMCPUCC, iem.s.idxTbCurInstr));
#else
    RT_NOREF(idxInstr);
#endif

    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxVarEffAddr, &off);
    off = iemNativeEmitTbExitIfAnyBitsSetInGpr<kIemNativeLabelType_RaiseGp0>(pReNative, off, idxVarReg, cbAlign - 1);
    iemNativeVarRegisterRelease(pReNative, idxVarEffAddr);

    return off;
}


/*********************************************************************************************************************************
*   Emitters for conditionals (IEM_MC_IF_XXX, IEM_MC_ELSE, IEM_MC_ENDIF)                                                         *
*********************************************************************************************************************************/

/**
 * Pushes an IEM_MC_IF_XXX onto the condition stack.
 *
 * @returns Pointer to the condition stack entry on success, NULL on failure
 *          (too many nestings)
 */
DECL_INLINE_THROW(PIEMNATIVECOND) iemNativeCondPushIf(PIEMRECOMPILERSTATE pReNative)
{
    uint32_t const idxStack = pReNative->cCondDepth;
    AssertStmt(idxStack < RT_ELEMENTS(pReNative->aCondStack), IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_COND_TOO_DEEPLY_NESTED));

    PIEMNATIVECOND const pEntry = &pReNative->aCondStack[idxStack];
    pReNative->cCondDepth = (uint8_t)(idxStack + 1);

    uint16_t const uCondSeqNo = ++pReNative->uCondSeqNo;
    pEntry->fInElse       = false;
    pEntry->fIfExitTb     = false;
    pEntry->fElseExitTb   = false;
    pEntry->idxLabelElse  = iemNativeLabelCreate(pReNative, kIemNativeLabelType_Else, UINT32_MAX /*offWhere*/, uCondSeqNo);
    pEntry->idxLabelEndIf = iemNativeLabelCreate(pReNative, kIemNativeLabelType_Endif, UINT32_MAX /*offWhere*/, uCondSeqNo);

    return pEntry;
}


/**
 * Start of the if-block, snapshotting the register and variable state.
 */
DECL_INLINE_THROW(void)
iemNativeCondStartIfBlock(PIEMRECOMPILERSTATE pReNative, uint32_t offIfBlock, uint32_t idxLabelIf = UINT32_MAX)
{
    Assert(offIfBlock != UINT32_MAX);
    Assert(pReNative->cCondDepth > 0 && pReNative->cCondDepth <= RT_ELEMENTS(pReNative->aCondStack));
    PIEMNATIVECOND const pEntry = &pReNative->aCondStack[pReNative->cCondDepth - 1];
    Assert(!pEntry->fInElse);

    /* Define the start of the IF block if request or for disassembly purposes. */
    if (idxLabelIf != UINT32_MAX)
        iemNativeLabelDefine(pReNative, idxLabelIf, offIfBlock);
#ifdef IEMNATIVE_WITH_TB_DEBUG_INFO
    else
        iemNativeLabelCreate(pReNative, kIemNativeLabelType_If, offIfBlock, pReNative->paLabels[pEntry->idxLabelElse].uData);
#else
    RT_NOREF(offIfBlock);
#endif

    /* Copy the initial state so we can restore it in the 'else' block. */
    pEntry->InitialState = pReNative->Core;
}


#define IEM_MC_ELSE() } while (0); \
        off = iemNativeEmitElse(pReNative, off); \
        do {

/** Emits code related to IEM_MC_ELSE. */
DECL_INLINE_THROW(uint32_t) iemNativeEmitElse(PIEMRECOMPILERSTATE pReNative, uint32_t off)
{
    /* Check sanity and get the conditional stack entry. */
    Assert(off != UINT32_MAX);
    Assert(pReNative->cCondDepth > 0 && pReNative->cCondDepth <= RT_ELEMENTS(pReNative->aCondStack));
    PIEMNATIVECOND const pEntry = &pReNative->aCondStack[pReNative->cCondDepth - 1];
    Assert(!pEntry->fInElse);

    /* We can skip dirty register flushing and the dirty register flushing if
       the branch already jumped to a TB exit. */
    if (!pEntry->fIfExitTb)
    {
#if defined(IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK) && 0
        /* Writeback any dirty shadow registers. */
        /** @todo r=aeichner Possible optimization is to only writeback guest registers which became dirty
         *                   in one of the branches and leave guest registers already dirty before the start of the if
         *                   block alone. */
        off = iemNativeRegFlushDirtyGuest(pReNative, off);
#endif

        /* Jump to the endif. */
        off = iemNativeEmitJmpToLabel(pReNative, off, pEntry->idxLabelEndIf);
    }
# ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
    else
        Assert(pReNative->Core.offPc == 0);
# endif

    /* Define the else label and enter the else part of the condition. */
    iemNativeLabelDefine(pReNative, pEntry->idxLabelElse, off);
    pEntry->fInElse = true;

    /* Snapshot the core state so we can do a merge at the endif and restore
       the snapshot we took at the start of the if-block. */
    pEntry->IfFinalState = pReNative->Core;
    pReNative->Core = pEntry->InitialState;

    return off;
}


#define IEM_MC_ENDIF() } while (0); \
        off = iemNativeEmitEndIf(pReNative, off)

/** Emits code related to IEM_MC_ENDIF. */
DECL_INLINE_THROW(uint32_t) iemNativeEmitEndIf(PIEMRECOMPILERSTATE pReNative, uint32_t off)
{
    /* Check sanity and get the conditional stack entry. */
    Assert(off != UINT32_MAX);
    Assert(pReNative->cCondDepth > 0 && pReNative->cCondDepth <= RT_ELEMENTS(pReNative->aCondStack));
    PIEMNATIVECOND const pEntry = &pReNative->aCondStack[pReNative->cCondDepth - 1];

#if defined(IEMNATIVE_WITH_DELAYED_PC_UPDATING) && 0
    off = iemNativeRegFlushDirtyGuest(pReNative, off);
#endif

    /*
     * If either of the branches exited the TB, we can take the state from the
     * other branch and skip all the merging headache.
     */
    bool fDefinedLabels = false;
    if (pEntry->fElseExitTb || pEntry->fIfExitTb)
    {
#ifdef VBOX_STRICT
        Assert(pReNative->cCondDepth == 1);                 /* Assuming this only happens in simple conditional structures.  */
        Assert(pEntry->fElseExitTb != pEntry->fIfExitTb);   /* Assuming we don't have any code where both branches exits. */
        PCIEMNATIVECORESTATE const pExitCoreState = pEntry->fIfExitTb && pEntry->fInElse
                                                  ? &pEntry->IfFinalState : &pReNative->Core;
# ifdef IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK
        Assert(pExitCoreState->bmGstRegShadowDirty == 0);
# endif
# ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
        Assert(pExitCoreState->offPc == 0);
# endif
        RT_NOREF(pExitCoreState);
#endif

        if (!pEntry->fIfExitTb)
        {
            Assert(pEntry->fInElse);
            pReNative->Core = pEntry->IfFinalState;
        }
    }
    else
    {
        /*
         * Now we have find common group with the core state at the end of the
         * if-final.  Use the smallest common denominator and just drop anything
         * that isn't the same in both states.
         */
        /** @todo We could, maybe, shuffle registers around if we thought it helpful,
         *        which is why we're doing this at the end of the else-block.
         *        But we'd need more info about future for that to be worth the effort. */
        PCIEMNATIVECORESTATE const pOther = pEntry->fInElse ? &pEntry->IfFinalState : &pEntry->InitialState;
#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
        AssertMsgStmt(pReNative->Core.offPc == pOther->offPc,
                      ("Core.offPc=%#RX64 pOther->offPc=%#RX64\n", pReNative->Core.offPc, pOther->offPc),
                      IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_COND_ENDIF_RECONCILIATION_FAILED));
#endif

        if (memcmp(&pReNative->Core, pOther, sizeof(*pOther)) != 0)
        {
#ifdef IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK
            /*
             * If the branch has differences in dirty shadow registers, we will flush
             * the register only dirty in the current branch and dirty any that's only
             * dirty in the other one.
             */
            uint64_t const fGstRegDirtyOther = pOther->bmGstRegShadowDirty;
            uint64_t const fGstRegDirtyThis  = pReNative->Core.bmGstRegShadowDirty;
            uint64_t const fGstRegDirtyDiff  = fGstRegDirtyOther ^ fGstRegDirtyThis;
            uint64_t const fGstRegDirtyHead  = fGstRegDirtyThis  & fGstRegDirtyDiff;
            uint64_t       fGstRegDirtyTail  = fGstRegDirtyOther & fGstRegDirtyDiff;
            if (!fGstRegDirtyDiff)
            { /* likely */ }
            else
            {
                //uint64_t const fGstRegDirtyHead = pReNative->Core.bmGstRegShadowDirty & fGstRegDirtyDiff;
                if (fGstRegDirtyHead)
                {
                    Log12(("iemNativeEmitEndIf: flushing dirty guest registers in current branch: %RX64\n", fGstRegDirtyHead));
                    off = iemNativeRegFlushDirtyGuest(pReNative, off, fGstRegDirtyHead);
                }
            }
#endif

            /*
             * Shadowed guest registers.
             *
             * We drop any shadows where the two states disagree about where
             * things are kept.  We may end up flushing dirty more registers
             * here, if the two branches keeps things in different registers.
             */
            uint64_t fGstRegs = pReNative->Core.bmGstRegShadows;
            if (fGstRegs)
            {
                Assert(pReNative->Core.bmHstRegsWithGstShadow != 0);
                do
                {
                    unsigned idxGstReg = ASMBitFirstSetU64(fGstRegs) - 1;
                    fGstRegs &= ~RT_BIT_64(idxGstReg);

                    uint8_t const idxCurHstReg   = pReNative->Core.aidxGstRegShadows[idxGstReg];
                    uint8_t const idxOtherHstReg = pOther->aidxGstRegShadows[idxGstReg];
                    if (   idxCurHstReg != idxOtherHstReg
                        || !(pOther->bmGstRegShadows & RT_BIT_64(idxGstReg)))
                    {
#ifndef IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK
                        Log12(("iemNativeEmitEndIf: dropping gst %s (%d) from hst %s (other %d/%#RX64)\n",
                               g_aGstShadowInfo[idxGstReg].pszName, idxGstReg, g_apszIemNativeHstRegNames[idxCurHstReg],
                               idxOtherHstReg, pOther->bmGstRegShadows));
#else
                        Log12(("iemNativeEmitEndIf: dropping %s gst %s (%d) from hst %s (other %d/%#RX64/%s)\n",
                               pReNative->Core.bmGstRegShadowDirty & RT_BIT_64(idxGstReg) ? "_dirty_" : "clean",
                               g_aGstShadowInfo[idxGstReg].pszName, idxGstReg, g_apszIemNativeHstRegNames[idxCurHstReg],
                               idxOtherHstReg, pOther->bmGstRegShadows,
                               pOther->bmGstRegShadowDirty & RT_BIT_64(idxGstReg) ? "dirty" : "clean"));
                        if (pOther->bmGstRegShadowDirty & RT_BIT_64(idxGstReg))
                            fGstRegDirtyTail |= RT_BIT_64(idxGstReg);
                        if (pReNative->Core.bmGstRegShadowDirty & RT_BIT_64(idxGstReg))
                            off = iemNativeRegFlushPendingWrite(pReNative, off, (IEMNATIVEGSTREG)idxGstReg);
#endif
                        iemNativeRegClearGstRegShadowingOne(pReNative, idxCurHstReg, (IEMNATIVEGSTREG)idxGstReg, off);
                    }
                } while (fGstRegs);
            }
            else
                Assert(pReNative->Core.bmHstRegsWithGstShadow == 0);

#ifdef IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK
            /*
             * Generate jumpy code for flushing dirty registers from the other
             * branch that aren't dirty in the current one.
             */
            if (!fGstRegDirtyTail)
            { /* likely */ }
            else
            {
                STAM_REL_COUNTER_INC(&pReNative->pVCpu->iem.s.StatNativeEndIfOtherBranchDirty);
                Log12(("iemNativeEmitEndIf: Dirty register only in the other branch: %#RX64 - BAD!\n", fGstRegDirtyTail));

                /* First the current branch has to jump over the dirty flushing from the other branch. */
                uint32_t const offFixup1 = off;
                off = iemNativeEmitJmpToFixed(pReNative, off, off + 10);

                /* Put the endif and maybe else label here so the other branch ends up here. */
                if (!pEntry->fInElse)
                    iemNativeLabelDefine(pReNative, pEntry->idxLabelElse, off);
                else
                    Assert(pReNative->paLabels[pEntry->idxLabelElse].off <= off);
                iemNativeLabelDefine(pReNative, pEntry->idxLabelEndIf, off);
                fDefinedLabels = true;

                /* Flush the dirty guest registers from the other branch. */
                while (fGstRegDirtyTail)
                {
                    unsigned idxGstReg = ASMBitFirstSetU64(fGstRegDirtyTail) - 1;
                    fGstRegDirtyTail &= ~RT_BIT_64(idxGstReg);
                    Log12(("iemNativeEmitEndIf: tail flushing %s (%d) from other branch %d (cur %d/%#RX64)\n",
                           g_aGstShadowInfo[idxGstReg].pszName, idxGstReg, pOther->aidxGstRegShadows[idxGstReg],
                           pReNative->Core.aidxGstRegShadows[idxGstReg], pReNative->Core.bmGstRegShadows));

                    off = iemNativeRegFlushPendingWriteEx(pReNative, off, (PIEMNATIVECORESTATE)pOther, (IEMNATIVEGSTREG)idxGstReg);

                    /* Mismatching shadowing should've been dropped in the previous step already. */
                    Assert(   !(pReNative->Core.bmGstRegShadows & RT_BIT_64(idxGstReg))
                           || pReNative->Core.aidxGstRegShadows[idxGstReg] == pOther->aidxGstRegShadows[idxGstReg]);
                }

                /* Here is the actual endif label, fixup the above jump to land here. */
                iemNativeFixupFixedJump(pReNative, offFixup1, off);
            }
#endif

            /*
             * Check variables next. For now we must require them to be identical
             * or stuff we can recreate. (No code is emitted here.)
             */
            Assert(pReNative->Core.u64ArgVars == pOther->u64ArgVars);
#ifdef VBOX_STRICT
            uint32_t const offAssert = off;
#endif
            uint32_t       fVars     = pReNative->Core.bmVars | pOther->bmVars;
            if (fVars)
            {
                uint32_t const fVarsMustRemove = pReNative->Core.bmVars ^ pOther->bmVars;
                do
                {
                    unsigned idxVar = ASMBitFirstSetU32(fVars) - 1;
                    fVars &= ~RT_BIT_32(idxVar);

                    if (!(fVarsMustRemove & RT_BIT_32(idxVar)))
                    {
                        if (pReNative->Core.aVars[idxVar].idxReg == pOther->aVars[idxVar].idxReg)
                            continue;
                        if (pReNative->Core.aVars[idxVar].enmKind != kIemNativeVarKind_Stack)
                        {
                            uint8_t const idxHstReg = pReNative->Core.aVars[idxVar].idxReg;
                            if (idxHstReg != UINT8_MAX)
                            {
                                pReNative->Core.bmHstRegs &= ~RT_BIT_32(idxHstReg);
                                pReNative->Core.aVars[idxVar].idxReg = UINT8_MAX;
                                Log12(("iemNativeEmitEndIf: Dropping hst reg %s for var #%u/%#x\n",
                                       g_apszIemNativeHstRegNames[idxHstReg], idxVar, IEMNATIVE_VAR_IDX_PACK(idxVar)));
                            }
                            continue;
                        }
                    }
                    else if (!(pReNative->Core.bmVars & RT_BIT_32(idxVar)))
                        continue;

                    /* Irreconcilable, so drop it. */
                    uint8_t const idxHstReg = pReNative->Core.aVars[idxVar].idxReg;
                    if (idxHstReg != UINT8_MAX)
                    {
                        pReNative->Core.bmHstRegs &= ~RT_BIT_32(idxHstReg);
                        pReNative->Core.aVars[idxVar].idxReg = UINT8_MAX;
                        Log12(("iemNativeEmitEndIf: Dropping hst reg %s for var #%u/%#x (also dropped)\n",
                               g_apszIemNativeHstRegNames[idxHstReg], idxVar, IEMNATIVE_VAR_IDX_PACK(idxVar)));
                    }
                    Log11(("iemNativeEmitEndIf: Freeing variable #%u/%#x\n", idxVar, IEMNATIVE_VAR_IDX_PACK(idxVar)));
                    pReNative->Core.bmVars &= ~RT_BIT_32(idxVar);
                } while (fVars);
            }
            Assert(off == offAssert);

            /*
             * Finally, check that the host register allocations matches.
             */
            AssertMsgStmt((pReNative->Core.bmHstRegs & (pReNative->Core.bmHstRegs ^ pOther->bmHstRegs)) == 0,
                          ("Core.bmHstRegs=%#x pOther->bmHstRegs=%#x - %#x\n",
                           pReNative->Core.bmHstRegs, pOther->bmHstRegs, pReNative->Core.bmHstRegs ^ pOther->bmHstRegs),
                          IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_COND_ENDIF_RECONCILIATION_FAILED));
        }
    }

    /*
     * Define the endif label and maybe the else one if we're still in the 'if' part.
     */
    if (!fDefinedLabels)
    {
        if (!pEntry->fInElse)
            iemNativeLabelDefine(pReNative, pEntry->idxLabelElse, off);
        else
            Assert(pReNative->paLabels[pEntry->idxLabelElse].off <= off);
        iemNativeLabelDefine(pReNative, pEntry->idxLabelEndIf, off);
    }

    /* Pop the conditional stack.*/
    pReNative->cCondDepth -= 1;

    return off;
}


/**
 * Helper function to convert X86_EFL_xxx masks to liveness masks.
 *
 * The compiler should be able to figure this out at compile time, so sprinkling
 * constexpr where ever possible here to nudge it along.
 */
template<uint32_t const a_fEfl>
RT_CONSTEXPR uint64_t iemNativeEflagsToLivenessMask(void)
{
    return (a_fEfl & ~X86_EFL_STATUS_BITS ? RT_BIT_64(IEMLIVENESSBIT_IDX_EFL_OTHER) : 0)
         | (a_fEfl & X86_EFL_CF           ? RT_BIT_64(IEMLIVENESSBIT_IDX_EFL_CF)    : 0)
         | (a_fEfl & X86_EFL_PF           ? RT_BIT_64(IEMLIVENESSBIT_IDX_EFL_PF)    : 0)
         | (a_fEfl & X86_EFL_AF           ? RT_BIT_64(IEMLIVENESSBIT_IDX_EFL_AF)    : 0)
         | (a_fEfl & X86_EFL_ZF           ? RT_BIT_64(IEMLIVENESSBIT_IDX_EFL_ZF)    : 0)
         | (a_fEfl & X86_EFL_SF           ? RT_BIT_64(IEMLIVENESSBIT_IDX_EFL_SF)    : 0)
         | (a_fEfl & X86_EFL_OF           ? RT_BIT_64(IEMLIVENESSBIT_IDX_EFL_OF)    : 0);
}


/**
 * Helper function to convert a single X86_EFL_xxxx value to bit number.
 *
 * The compiler should be able to figure this out at compile time, so sprinkling
 * constexpr where ever possible here to nudge it along.
 */
template<uint32_t const a_fEfl>
RT_CONSTEXPR unsigned iemNativeEflagsToSingleBitNo(void)
{
    AssertCompile(   a_fEfl == X86_EFL_CF
                  || a_fEfl == X86_EFL_PF
                  || a_fEfl == X86_EFL_AF
                  || a_fEfl == X86_EFL_ZF
                  || a_fEfl == X86_EFL_SF
                  || a_fEfl == X86_EFL_OF
                  || a_fEfl == X86_EFL_DF);
    return a_fEfl == X86_EFL_CF ? X86_EFL_CF_BIT
         : a_fEfl == X86_EFL_PF ? X86_EFL_PF_BIT
         : a_fEfl == X86_EFL_AF ? X86_EFL_AF_BIT
         : a_fEfl == X86_EFL_ZF ? X86_EFL_ZF_BIT
         : a_fEfl == X86_EFL_SF ? X86_EFL_SF_BIT
         : a_fEfl == X86_EFL_OF ? X86_EFL_OF_BIT
         :                        X86_EFL_DF_BIT;
}


#define IEM_MC_IF_EFL_ANY_BITS_SET(a_fBits) \
        off = iemNativeEmitIfEflagAnysBitsSet(pReNative, off, (a_fBits), iemNativeEflagsToLivenessMask<a_fBits>()); \
        do {

/** Emits code for IEM_MC_IF_EFL_ANY_BITS_SET. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitIfEflagAnysBitsSet(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint32_t fBitsInEfl, uint64_t fLivenessEflBits)
{
    IEMNATIVE_ASSERT_EFLAGS_SKIPPING_AND_POSTPONING(pReNative,  fBitsInEfl);
    IEMNATIVE_STRICT_EFLAGS_SKIPPING_EMIT_CHECK(pReNative, off, fBitsInEfl);
    PIEMNATIVECOND const pEntry = iemNativeCondPushIf(pReNative);

    /* Get the eflags. */
    uint8_t const idxEflReg = iemNativeRegAllocTmpForGuestEFlagsReadOnly(pReNative, &off, fLivenessEflBits);

    /* Test and jump. */
    off = iemNativeEmitTestAnyBitsInGprAndJmpToLabelIfNoneSet(pReNative, off, idxEflReg, fBitsInEfl, pEntry->idxLabelElse);

    /* Free but don't flush the EFlags register. */
    iemNativeRegFreeTmp(pReNative, idxEflReg);

    /* Make a copy of the core state now as we start the if-block. */
    iemNativeCondStartIfBlock(pReNative, off);

    return off;
}


#define IEM_MC_IF_EFL_NO_BITS_SET(a_fBits) \
        off = iemNativeEmitIfEflagNoBitsSet(pReNative, off, (a_fBits), iemNativeEflagsToLivenessMask<a_fBits>()); \
        do {

/** Emits code for IEM_MC_IF_EFL_NO_BITS_SET. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitIfEflagNoBitsSet(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint32_t fBitsInEfl, uint64_t fLivenessEflBits)
{
    IEMNATIVE_ASSERT_EFLAGS_SKIPPING_AND_POSTPONING(pReNative,  fBitsInEfl);
    IEMNATIVE_STRICT_EFLAGS_SKIPPING_EMIT_CHECK(pReNative, off, fBitsInEfl);
    PIEMNATIVECOND const pEntry = iemNativeCondPushIf(pReNative);

    /* Get the eflags. */
    uint8_t const idxEflReg = iemNativeRegAllocTmpForGuestEFlagsReadOnly(pReNative, &off, fLivenessEflBits);

    /* Test and jump. */
    off = iemNativeEmitTestAnyBitsInGprAndJmpToLabelIfAnySet(pReNative, off, idxEflReg, fBitsInEfl, pEntry->idxLabelElse);

    /* Free but don't flush the EFlags register. */
    iemNativeRegFreeTmp(pReNative, idxEflReg);

    /* Make a copy of the core state now as we start the if-block. */
    iemNativeCondStartIfBlock(pReNative, off);

    return off;
}


#define IEM_MC_IF_EFL_BIT_SET(a_fBit) \
        off = iemNativeEmitIfEflagsBitSet(pReNative, off, iemNativeEflagsToSingleBitNo<a_fBit>(), \
                                          iemNativeEflagsToLivenessMask<a_fBit>()); \
        do {

/** Emits code for IEM_MC_IF_EFL_BIT_SET. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitIfEflagsBitSet(PIEMRECOMPILERSTATE pReNative, uint32_t off, unsigned iBitNo, uint64_t fLivenessEflBit)
{
    IEMNATIVE_ASSERT_EFLAGS_SKIPPING_AND_POSTPONING(pReNative,  RT_BIT_32(iBitNo));
    IEMNATIVE_STRICT_EFLAGS_SKIPPING_EMIT_CHECK(pReNative, off, RT_BIT_32(iBitNo));
    PIEMNATIVECOND const pEntry = iemNativeCondPushIf(pReNative);

    /* Get the eflags. */
    uint8_t const idxEflReg = iemNativeRegAllocTmpForGuestEFlagsReadOnly(pReNative, &off, fLivenessEflBit);

    /* Test and jump. */
    off = iemNativeEmitTestBitInGprAndJmpToLabelIfNotSet(pReNative, off, idxEflReg, iBitNo, pEntry->idxLabelElse);

    /* Free but don't flush the EFlags register. */
    iemNativeRegFreeTmp(pReNative, idxEflReg);

    /* Make a copy of the core state now as we start the if-block. */
    iemNativeCondStartIfBlock(pReNative, off);

    return off;
}


#define IEM_MC_IF_EFL_BIT_NOT_SET(a_fBit) \
        off = iemNativeEmitIfEflagsBitNotSet(pReNative, off, iemNativeEflagsToSingleBitNo<a_fBit>(), \
                                             iemNativeEflagsToLivenessMask<a_fBit>()); \
        do {

/** Emits code for IEM_MC_IF_EFL_BIT_NOT_SET. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitIfEflagsBitNotSet(PIEMRECOMPILERSTATE pReNative, uint32_t off, unsigned iBitNo, uint64_t fLivenessEflBit)
{
    IEMNATIVE_ASSERT_EFLAGS_SKIPPING_AND_POSTPONING(pReNative,  RT_BIT_32(iBitNo));
    IEMNATIVE_STRICT_EFLAGS_SKIPPING_EMIT_CHECK(pReNative, off, RT_BIT_32(iBitNo));
    PIEMNATIVECOND const pEntry = iemNativeCondPushIf(pReNative);

    /* Get the eflags. */
    uint8_t const idxEflReg = iemNativeRegAllocTmpForGuestEFlagsReadOnly(pReNative, &off, fLivenessEflBit);

    /* Test and jump. */
    off = iemNativeEmitTestBitInGprAndJmpToLabelIfSet(pReNative, off, idxEflReg, iBitNo, pEntry->idxLabelElse);

    /* Free but don't flush the EFlags register. */
    iemNativeRegFreeTmp(pReNative, idxEflReg);

    /* Make a copy of the core state now as we start the if-block. */
    iemNativeCondStartIfBlock(pReNative, off);

    return off;
}


#define IEM_MC_IF_EFL_BITS_EQ(a_fBit1, a_fBit2) \
    off = iemNativeEmitIfEflagsTwoBitsEqual(pReNative, off, false /*fInverted*/, \
                                            iemNativeEflagsToSingleBitNo<a_fBit1>(), \
                                            iemNativeEflagsToSingleBitNo<a_fBit2>(), \
                                            iemNativeEflagsToLivenessMask<a_fBit1 | a_fBit2>()); \
    do {

#define IEM_MC_IF_EFL_BITS_NE(a_fBit1, a_fBit2) \
    off = iemNativeEmitIfEflagsTwoBitsEqual(pReNative, off, true /*fInverted*/, \
                                            iemNativeEflagsToSingleBitNo<a_fBit1>(), \
                                            iemNativeEflagsToSingleBitNo<a_fBit2>(), \
                                            iemNativeEflagsToLivenessMask<a_fBit1 | a_fBit2>()); \
    do {

/** Emits code for IEM_MC_IF_EFL_BITS_EQ and IEM_MC_IF_EFL_BITS_NE. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitIfEflagsTwoBitsEqual(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                  bool fInverted, unsigned iBitNo1, unsigned iBitNo2, uint64_t fLivenessEflBits)
{
    Assert(iBitNo1 != iBitNo2);
    IEMNATIVE_ASSERT_EFLAGS_SKIPPING_AND_POSTPONING(pReNative,  RT_BIT_32(iBitNo1) | RT_BIT_32(iBitNo2));
    IEMNATIVE_STRICT_EFLAGS_SKIPPING_EMIT_CHECK(pReNative, off, RT_BIT_32(iBitNo1) | RT_BIT_32(iBitNo2));
    PIEMNATIVECOND const pEntry = iemNativeCondPushIf(pReNative);

    /* Get the eflags. */
    uint8_t const idxEflReg = iemNativeRegAllocTmpForGuestEFlagsReadOnly(pReNative, &off, fLivenessEflBits);

#ifdef RT_ARCH_AMD64
    uint8_t const idxTmpReg = iemNativeRegAllocTmpImm(pReNative, &off, RT_BIT_64(iBitNo1));

    off = iemNativeEmitAndGpr32ByGpr32(pReNative, off, idxTmpReg, idxEflReg);
    if (iBitNo1 > iBitNo2)
        off = iemNativeEmitShiftGpr32Right(pReNative, off, idxTmpReg, iBitNo1 - iBitNo2);
    else
        off = iemNativeEmitShiftGpr32Left(pReNative, off, idxTmpReg, iBitNo2 - iBitNo1);
    off = iemNativeEmitXorGpr32ByGpr32(pReNative, off, idxTmpReg, idxEflReg);

#elif defined(RT_ARCH_ARM64)
    uint8_t const    idxTmpReg   = iemNativeRegAllocTmp(pReNative, &off);
    uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 2);

    /* and tmpreg, eflreg, #1<<iBitNo1 */
    pu32CodeBuf[off++] = Armv8A64MkInstrAndImm(idxTmpReg, idxEflReg, 0 /*uImm7SizeLen -> 32*/, 32 - iBitNo1, false /*f64Bit*/);

    /* eeyore tmpreg, eflreg, tmpreg, LSL/LSR, #abs(iBitNo2 - iBitNo1) */
    if (iBitNo1 > iBitNo2)
        pu32CodeBuf[off++] = Armv8A64MkInstrEor(idxTmpReg, idxEflReg, idxTmpReg, false /*64bit*/,
                                                iBitNo1 - iBitNo2, kArmv8A64InstrShift_Lsr);
    else
        pu32CodeBuf[off++] = Armv8A64MkInstrEor(idxTmpReg, idxEflReg, idxTmpReg, false /*64bit*/,
                                                iBitNo2 - iBitNo1, kArmv8A64InstrShift_Lsl);

    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#else
# error "Port me"
#endif

    /* Test (bit #2 is set in tmpreg if not-equal) and jump. */
    off = iemNativeEmitTestBitInGprAndJmpToLabelIfCc(pReNative, off, idxTmpReg, iBitNo2,
                                                     pEntry->idxLabelElse, !fInverted /*fJmpIfSet*/);

    /* Free but don't flush the EFlags and tmp registers. */
    iemNativeRegFreeTmp(pReNative, idxTmpReg);
    iemNativeRegFreeTmp(pReNative, idxEflReg);

    /* Make a copy of the core state now as we start the if-block. */
    iemNativeCondStartIfBlock(pReNative, off);

    return off;
}


#define IEM_MC_IF_EFL_BIT_NOT_SET_AND_BITS_EQ(a_fBit, a_fBit1, a_fBit2) \
    off = iemNativeEmitIfEflagsBitNotSetAndTwoBitsEqual(pReNative, off, false /*fInverted*/, \
                                                        iemNativeEflagsToSingleBitNo<a_fBit>(), \
                                                        iemNativeEflagsToSingleBitNo<a_fBit1>(), \
                                                        iemNativeEflagsToSingleBitNo<a_fBit2>(), \
                                                        iemNativeEflagsToLivenessMask<a_fBit | a_fBit1 | a_fBit2>()); \
    do {

#define IEM_MC_IF_EFL_BIT_SET_OR_BITS_NE(a_fBit, a_fBit1, a_fBit2) \
    off = iemNativeEmitIfEflagsBitNotSetAndTwoBitsEqual(pReNative, off, true /*fInverted*/, \
                                                        iemNativeEflagsToSingleBitNo<a_fBit>(), \
                                                        iemNativeEflagsToSingleBitNo<a_fBit1>(), \
                                                        iemNativeEflagsToSingleBitNo<a_fBit2>(), \
                                                        iemNativeEflagsToLivenessMask<a_fBit | a_fBit1 | a_fBit2>()); \
    do {

/** Emits code for IEM_MC_IF_EFL_BIT_NOT_SET_AND_BITS_EQ and
 *  IEM_MC_IF_EFL_BIT_SET_OR_BITS_NE. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitIfEflagsBitNotSetAndTwoBitsEqual(PIEMRECOMPILERSTATE pReNative, uint32_t off, bool fInverted,
                                              unsigned iBitNo, unsigned iBitNo1, unsigned iBitNo2, uint64_t fLivenessEflBits)
{
    Assert(iBitNo1 != iBitNo);
    Assert(iBitNo2 != iBitNo);
    Assert(iBitNo2 != iBitNo1);
    IEMNATIVE_ASSERT_EFLAGS_SKIPPING_AND_POSTPONING(pReNative,  RT_BIT_32(iBitNo) | RT_BIT_32(iBitNo1) | RT_BIT_32(iBitNo2));
    IEMNATIVE_STRICT_EFLAGS_SKIPPING_EMIT_CHECK(pReNative, off, RT_BIT_32(iBitNo) | RT_BIT_32(iBitNo1) | RT_BIT_32(iBitNo2));
    PIEMNATIVECOND const pEntry = iemNativeCondPushIf(pReNative);

    /* We need an if-block label for the non-inverted variant. */
    uint32_t const idxLabelIf = fInverted ? iemNativeLabelCreate(pReNative, kIemNativeLabelType_If, UINT32_MAX,
                                                                 pReNative->paLabels[pEntry->idxLabelElse].uData) : UINT32_MAX;

    /* Get the eflags. */
    uint8_t const idxEflReg = iemNativeRegAllocTmpForGuestEFlagsReadOnly(pReNative, &off, fLivenessEflBits);

#ifdef RT_ARCH_AMD64
    uint8_t const idxTmpReg = iemNativeRegAllocTmpImm(pReNative, &off, RT_BIT_64(iBitNo1)); /* This must come before we jump anywhere! */
#elif defined(RT_ARCH_ARM64)
    uint8_t const idxTmpReg = iemNativeRegAllocTmp(pReNative, &off);
#endif

    /* Check for the lone bit first. */
    if (!fInverted)
        off = iemNativeEmitTestBitInGprAndJmpToLabelIfSet(pReNative, off, idxEflReg, iBitNo, pEntry->idxLabelElse);
    else
        off = iemNativeEmitTestBitInGprAndJmpToLabelIfSet(pReNative, off, idxEflReg, iBitNo, idxLabelIf);

    /* Then extract and compare the other two bits. */
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitAndGpr32ByGpr32(pReNative, off, idxTmpReg, idxEflReg);
    if (iBitNo1 > iBitNo2)
        off = iemNativeEmitShiftGpr32Right(pReNative, off, idxTmpReg, iBitNo1 - iBitNo2);
    else
        off = iemNativeEmitShiftGpr32Left(pReNative, off, idxTmpReg, iBitNo2 - iBitNo1);
    off = iemNativeEmitXorGpr32ByGpr32(pReNative, off, idxTmpReg, idxEflReg);

#elif defined(RT_ARCH_ARM64)
    uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 2);

    /* and tmpreg, eflreg, #1<<iBitNo1 */
    pu32CodeBuf[off++] = Armv8A64MkInstrAndImm(idxTmpReg, idxEflReg, 0 /*uImm7SizeLen -> 32*/, 32 - iBitNo1, false /*f64Bit*/);

    /* eeyore tmpreg, eflreg, tmpreg, LSL/LSR, #abs(iBitNo2 - iBitNo1) */
    if (iBitNo1 > iBitNo2)
        pu32CodeBuf[off++] = Armv8A64MkInstrEor(idxTmpReg, idxEflReg, idxTmpReg, false /*64bit*/,
                                                iBitNo1 - iBitNo2, kArmv8A64InstrShift_Lsr);
    else
        pu32CodeBuf[off++] = Armv8A64MkInstrEor(idxTmpReg, idxEflReg, idxTmpReg, false /*64bit*/,
                                                iBitNo2 - iBitNo1, kArmv8A64InstrShift_Lsl);

    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#else
# error "Port me"
#endif

    /* Test (bit #2 is set in tmpreg if not-equal) and jump. */
    off = iemNativeEmitTestBitInGprAndJmpToLabelIfCc(pReNative, off, idxTmpReg, iBitNo2,
                                                     pEntry->idxLabelElse, !fInverted /*fJmpIfSet*/);

    /* Free but don't flush the EFlags and tmp registers. */
    iemNativeRegFreeTmp(pReNative, idxTmpReg);
    iemNativeRegFreeTmp(pReNative, idxEflReg);

    /* Make a copy of the core state now as we start the if-block. */
    iemNativeCondStartIfBlock(pReNative, off, idxLabelIf);

    return off;
}


#define IEM_MC_IF_CX_IS_NZ() \
    off = iemNativeEmitIfCxIsNotZero(pReNative, off); \
    do {

/** Emits code for IEM_MC_IF_CX_IS_NZ. */
DECL_INLINE_THROW(uint32_t) iemNativeEmitIfCxIsNotZero(PIEMRECOMPILERSTATE pReNative, uint32_t off)
{
    PIEMNATIVECOND const pEntry = iemNativeCondPushIf(pReNative);

    uint8_t const idxGstRcxReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(X86_GREG_xCX),
                                                                 kIemNativeGstRegUse_ReadOnly);
    off = iemNativeEmitTestAnyBitsInGprAndJmpToLabelIfNoneSet(pReNative, off, idxGstRcxReg, UINT16_MAX, pEntry->idxLabelElse);
    iemNativeRegFreeTmp(pReNative, idxGstRcxReg);

    iemNativeCondStartIfBlock(pReNative, off);
    return off;
}


#define IEM_MC_IF_ECX_IS_NZ() \
    off = iemNativeEmitIfRcxEcxIsNotZero(pReNative, off, false /*f64Bit*/); \
    do {

#define IEM_MC_IF_RCX_IS_NZ() \
    off = iemNativeEmitIfRcxEcxIsNotZero(pReNative, off, true /*f64Bit*/); \
    do {

/** Emits code for IEM_MC_IF_ECX_IS_NZ and IEM_MC_IF_RCX_IS_NZ. */
DECL_INLINE_THROW(uint32_t) iemNativeEmitIfRcxEcxIsNotZero(PIEMRECOMPILERSTATE pReNative, uint32_t off, bool f64Bit)
{
    PIEMNATIVECOND const pEntry = iemNativeCondPushIf(pReNative);

    uint8_t const idxGstRcxReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(X86_GREG_xCX),
                                                                 kIemNativeGstRegUse_ReadOnly);
    off = iemNativeEmitTestIfGprIsZeroAndJmpToLabel(pReNative, off, idxGstRcxReg, f64Bit, pEntry->idxLabelElse);
    iemNativeRegFreeTmp(pReNative, idxGstRcxReg);

    iemNativeCondStartIfBlock(pReNative, off);
    return off;
}


#define IEM_MC_IF_CX_IS_NOT_ONE() \
    off = iemNativeEmitIfCxIsNotOne(pReNative, off); \
    do {

/** Emits code for IEM_MC_IF_CX_IS_NOT_ONE. */
DECL_INLINE_THROW(uint32_t) iemNativeEmitIfCxIsNotOne(PIEMRECOMPILERSTATE pReNative, uint32_t off)
{
    PIEMNATIVECOND const pEntry = iemNativeCondPushIf(pReNative);

    uint8_t const idxGstRcxReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(X86_GREG_xCX),
                                                                 kIemNativeGstRegUse_ReadOnly);
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitTestIfGpr16EqualsImmAndJmpToLabel(pReNative, off, idxGstRcxReg, 1, pEntry->idxLabelElse);
#else
    uint8_t const idxTmpReg = iemNativeRegAllocTmp(pReNative, &off);
    off = iemNativeEmitTestIfGpr16EqualsImmAndJmpToLabel(pReNative, off, idxGstRcxReg, 1, pEntry->idxLabelElse, idxTmpReg);
    iemNativeRegFreeTmp(pReNative, idxTmpReg);
#endif
    iemNativeRegFreeTmp(pReNative, idxGstRcxReg);

    iemNativeCondStartIfBlock(pReNative, off);
    return off;
}


#define IEM_MC_IF_ECX_IS_NOT_ONE() \
    off = iemNativeEmitIfRcxEcxIsNotOne(pReNative, off, false /*f64Bit*/); \
    do {

#define IEM_MC_IF_RCX_IS_NOT_ONE() \
    off = iemNativeEmitIfRcxEcxIsNotOne(pReNative, off, true /*f64Bit*/); \
    do {

/** Emits code for IEM_MC_IF_ECX_IS_NOT_ONE and IEM_MC_IF_RCX_IS_NOT_ONE. */
DECL_INLINE_THROW(uint32_t) iemNativeEmitIfRcxEcxIsNotOne(PIEMRECOMPILERSTATE pReNative, uint32_t off, bool f64Bit)
{
    PIEMNATIVECOND const pEntry = iemNativeCondPushIf(pReNative);

    uint8_t const idxGstRcxReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(X86_GREG_xCX),
                                                                 kIemNativeGstRegUse_ReadOnly);
    if (f64Bit)
        off = iemNativeEmitTestIfGprEqualsImmAndJmpToLabel(pReNative, off, idxGstRcxReg, 1, pEntry->idxLabelElse);
    else
        off = iemNativeEmitTestIfGpr32EqualsImmAndJmpToLabel(pReNative, off, idxGstRcxReg, 1, pEntry->idxLabelElse);
    iemNativeRegFreeTmp(pReNative, idxGstRcxReg);

    iemNativeCondStartIfBlock(pReNative, off);
    return off;
}


#define IEM_MC_IF_CX_IS_NOT_ONE_AND_EFL_BIT_SET(a_fBit) \
    off = iemNativeEmitIfCxIsNotOneAndTestEflagsBit(pReNative, off, true /*fCheckIfSet*/, \
                                                    iemNativeEflagsToSingleBitNo<a_fBit>(), \
                                                    iemNativeEflagsToLivenessMask<a_fBit>()); \
    do {

#define IEM_MC_IF_CX_IS_NOT_ONE_AND_EFL_BIT_NOT_SET(a_fBit) \
    off = iemNativeEmitIfCxIsNotOneAndTestEflagsBit(pReNative, off, false /*fCheckIfSet*/, \
                                                    iemNativeEflagsToSingleBitNo<a_fBit>(), \
                                                    iemNativeEflagsToLivenessMask<a_fBit>()); \
    do {

/** Emits code for IEM_MC_IF_CX_IS_NOT_ONE_AND_EFL_BIT_SET and
 *  IEM_MC_IF_CX_IS_NOT_ONE_AND_EFL_BIT_NOT_SET. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitIfCxIsNotOneAndTestEflagsBit(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                          bool fCheckIfSet, unsigned iBitNo, uint64_t fLivenessEflBit)
{
    IEMNATIVE_ASSERT_EFLAGS_SKIPPING_AND_POSTPONING(pReNative,  RT_BIT_32(iBitNo));
    IEMNATIVE_STRICT_EFLAGS_SKIPPING_EMIT_CHECK(pReNative, off, RT_BIT_32(iBitNo));
    PIEMNATIVECOND const pEntry = iemNativeCondPushIf(pReNative);

    /* We have to load both RCX and EFLAGS before we can start branching,
       otherwise we'll end up in the else-block with an inconsistent
       register allocator state.
       Doing EFLAGS first as it's more likely to be loaded, right? */
    uint8_t const idxEflReg = iemNativeRegAllocTmpForGuestEFlagsReadOnly(pReNative, &off, fLivenessEflBit);
    uint8_t const idxGstRcxReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(X86_GREG_xCX),
                                                                 kIemNativeGstRegUse_ReadOnly);

    /** @todo we could reduce this to a single branch instruction by spending a
     *        temporary register and some setnz stuff.  Not sure if loops are
     *        worth it. */
    /* Check CX. */
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitTestIfGpr16EqualsImmAndJmpToLabel(pReNative, off, idxGstRcxReg, 1, pEntry->idxLabelElse);
#else
    uint8_t const idxTmpReg = iemNativeRegAllocTmp(pReNative, &off);
    off = iemNativeEmitTestIfGpr16EqualsImmAndJmpToLabel(pReNative, off, idxGstRcxReg, 1, pEntry->idxLabelElse, idxTmpReg);
    iemNativeRegFreeTmp(pReNative, idxTmpReg);
#endif

    /* Check the EFlags bit. */
    off = iemNativeEmitTestBitInGprAndJmpToLabelIfCc(pReNative, off, idxEflReg, iBitNo, pEntry->idxLabelElse,
                                                     !fCheckIfSet /*fJmpIfSet*/);

    iemNativeRegFreeTmp(pReNative, idxGstRcxReg);
    iemNativeRegFreeTmp(pReNative, idxEflReg);

    iemNativeCondStartIfBlock(pReNative, off);
    return off;
}


#define IEM_MC_IF_ECX_IS_NOT_ONE_AND_EFL_BIT_SET(a_fBit) \
    off = iemNativeEmitIfRcxEcxIsNotOneAndTestEflagsBit(pReNative, off, true /*fCheckIfSet*/, false /*f64Bit*/, \
                                                        iemNativeEflagsToSingleBitNo<a_fBit>(), \
                                                        iemNativeEflagsToLivenessMask<a_fBit>()); \
    do {

#define IEM_MC_IF_ECX_IS_NOT_ONE_AND_EFL_BIT_NOT_SET(a_fBit) \
    off = iemNativeEmitIfRcxEcxIsNotOneAndTestEflagsBit(pReNative, off, false /*fCheckIfSet*/, false /*f64Bit*/, \
                                                        iemNativeEflagsToSingleBitNo<a_fBit>(), \
                                                        iemNativeEflagsToLivenessMask<a_fBit>()); \
    do {

#define IEM_MC_IF_RCX_IS_NOT_ONE_AND_EFL_BIT_SET(a_fBit) \
    off = iemNativeEmitIfRcxEcxIsNotOneAndTestEflagsBit(pReNative, off, true /*fCheckIfSet*/, true /*f64Bit*/, \
                                                        iemNativeEflagsToSingleBitNo<a_fBit>(), \
                                                        iemNativeEflagsToLivenessMask<a_fBit>()); \
    do {

#define IEM_MC_IF_RCX_IS_NOT_ONE_AND_EFL_BIT_NOT_SET(a_fBit) \
    off = iemNativeEmitIfRcxEcxIsNotOneAndTestEflagsBit(pReNative, off, false /*fCheckIfSet*/, true /*f64Bit*/, \
                                                        iemNativeEflagsToSingleBitNo<a_fBit>(), \
                                                        iemNativeEflagsToLivenessMask<a_fBit>()); \
    do {

/** Emits code for IEM_MC_IF_ECX_IS_NOT_ONE_AND_EFL_BIT_SET,
 *  IEM_MC_IF_ECX_IS_NOT_ONE_AND_EFL_BIT_NOT_SET,
 *  IEM_MC_IF_RCX_IS_NOT_ONE_AND_EFL_BIT_SET and
 *  IEM_MC_IF_RCX_IS_NOT_ONE_AND_EFL_BIT_NOT_SET. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitIfRcxEcxIsNotOneAndTestEflagsBit(PIEMRECOMPILERSTATE pReNative, uint32_t off, bool fCheckIfSet, bool f64Bit,
                                              unsigned iBitNo, uint64_t fLivenessEFlBit)

{
    IEMNATIVE_ASSERT_EFLAGS_SKIPPING_AND_POSTPONING(pReNative,  RT_BIT_32(iBitNo));
    IEMNATIVE_STRICT_EFLAGS_SKIPPING_EMIT_CHECK(pReNative, off, RT_BIT_32(iBitNo));
    PIEMNATIVECOND const pEntry = iemNativeCondPushIf(pReNative);

    /* We have to load both RCX and EFLAGS before we can start branching,
       otherwise we'll end up in the else-block with an inconsistent
       register allocator state.
       Doing EFLAGS first as it's more likely to be loaded, right? */
    uint8_t const idxEflReg = iemNativeRegAllocTmpForGuestEFlagsReadOnly(pReNative, &off, fLivenessEFlBit);
    uint8_t const idxGstRcxReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(X86_GREG_xCX),
                                                                 kIemNativeGstRegUse_ReadOnly);

    /** @todo we could reduce this to a single branch instruction by spending a
     *        temporary register and some setnz stuff.  Not sure if loops are
     *        worth it. */
    /* Check RCX/ECX. */
    if (f64Bit)
        off = iemNativeEmitTestIfGprEqualsImmAndJmpToLabel(pReNative, off, idxGstRcxReg, 1, pEntry->idxLabelElse);
    else
        off = iemNativeEmitTestIfGpr32EqualsImmAndJmpToLabel(pReNative, off, idxGstRcxReg, 1, pEntry->idxLabelElse);

    /* Check the EFlags bit. */
    off = iemNativeEmitTestBitInGprAndJmpToLabelIfCc(pReNative, off, idxEflReg, iBitNo, pEntry->idxLabelElse,
                                                     !fCheckIfSet /*fJmpIfSet*/);

    iemNativeRegFreeTmp(pReNative, idxGstRcxReg);
    iemNativeRegFreeTmp(pReNative, idxEflReg);

    iemNativeCondStartIfBlock(pReNative, off);
    return off;
}


#define IEM_MC_IF_LOCAL_IS_Z(a_Local) \
    off = iemNativeEmitIfLocalIsZ(pReNative, off, a_Local); \
    do {

/** Emits code for IEM_MC_IF_LOCAL_IS_Z. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitIfLocalIsZ(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarLocal)
{
    PIEMNATIVECOND const pEntry = iemNativeCondPushIf(pReNative);

    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVarLocal);
    PIEMNATIVEVAR const pVarRc = &pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVarLocal)];
    AssertStmt(pVarRc->uArgNo == UINT8_MAX,       IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_VAR_IPE_8));
    AssertStmt(pVarRc->cbVar == sizeof(int32_t), IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_VAR_IPE_9));

    uint8_t const idxReg = iemNativeVarRegisterAcquire(pReNative, idxVarLocal, &off);

    off = iemNativeEmitTestIfGprIsNotZeroAndJmpToLabel(pReNative, off, idxReg, false /*f64Bit*/, pEntry->idxLabelElse);

    iemNativeVarRegisterRelease(pReNative, idxVarLocal);

    iemNativeCondStartIfBlock(pReNative, off);
    return off;
}


#define IEM_MC_IF_GREG_BIT_SET(a_iGReg, a_iBitNo) \
    off = iemNativeEmitIfGregBitSet(pReNative, off, a_iGReg, a_iBitNo); \
    do {

/** Emits code for IEM_MC_IF_GREG_BIT_SET. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitIfGregBitSet(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGReg, uint8_t iBitNo)
{
    PIEMNATIVECOND const pEntry = iemNativeCondPushIf(pReNative);
    Assert(iGReg < 16);

    uint8_t const idxGstFullReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(iGReg),
                                                                  kIemNativeGstRegUse_ReadOnly);

    off = iemNativeEmitTestBitInGprAndJmpToLabelIfNotSet(pReNative, off, idxGstFullReg, iBitNo, pEntry->idxLabelElse);

    iemNativeRegFreeTmp(pReNative, idxGstFullReg);

    iemNativeCondStartIfBlock(pReNative, off);
    return off;
}



/*********************************************************************************************************************************
*   Emitters for IEM_MC_ARG_XXX, IEM_MC_LOCAL, IEM_MC_LOCAL_CONST, ++                                                            *
*********************************************************************************************************************************/

#define IEM_MC_NOREF(a_Name) \
    RT_NOREF_PV(a_Name)

#define IEM_MC_ARG(a_Type, a_Name, a_iArg) \
    uint8_t const a_Name = iemNativeArgAlloc(pReNative, (a_iArg), sizeof(a_Type))

#define IEM_MC_ARG_CONST(a_Type, a_Name, a_Value, a_iArg) \
    uint8_t const a_Name = iemNativeArgAllocConst(pReNative, (a_iArg), sizeof(a_Type), (a_Value))

#define IEM_MC_ARG_LOCAL_REF(a_Type, a_Name, a_Local, a_iArg) \
    uint8_t const a_Name = iemNativeArgAllocLocalRef(pReNative, (a_iArg), (a_Local))

#define IEM_MC_LOCAL(a_Type, a_Name) \
    uint8_t const a_Name = iemNativeVarAlloc(pReNative, sizeof(a_Type))

#define IEM_MC_LOCAL_CONST(a_Type, a_Name, a_Value) \
    uint8_t const a_Name = iemNativeVarAllocConst(pReNative, sizeof(a_Type), (a_Value))

#define IEM_MC_LOCAL_ASSIGN(a_Type, a_Name, a_Value) \
    uint8_t const a_Name = iemNativeVarAllocAssign(pReNative, &off, sizeof(a_Type), (a_Value))


/**
 * Sets the host register for @a idxVarRc to @a idxReg.
 *
 * Any guest register shadowing will be implictly dropped by this call.
 *
 * The variable must not have any register associated with it (causes
 * VERR_IEM_VAR_IPE_10 to be raised).  Conversion to a stack variable is
 * implied.
 *
 * @returns idxReg
 * @param   pReNative   The recompiler state.
 * @param   idxVar      The variable.
 * @param   idxReg      The host register (typically IEMNATIVE_CALL_RET_GREG).
 * @param   off         For recording in debug info.
 * @param   fAllocated  Set if the register is already allocated, false if not.
 *
 * @throws  VERR_IEM_VAR_IPE_10, VERR_IEM_VAR_IPE_11
 */
DECL_INLINE_THROW(uint8_t)
iemNativeVarRegisterSet(PIEMRECOMPILERSTATE pReNative, uint8_t idxVar, uint8_t idxReg, uint32_t off, bool fAllocated)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVar);
    PIEMNATIVEVAR const pVar = &pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVar)];
    Assert(!pVar->fRegAcquired);
    Assert(idxReg < RT_ELEMENTS(pReNative->Core.aHstRegs));
    AssertStmt(pVar->idxReg == UINT8_MAX, IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_VAR_IPE_10));
    AssertStmt(RT_BOOL(pReNative->Core.bmHstRegs & RT_BIT_32(idxReg)) == fAllocated,
               IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_VAR_IPE_11));

    iemNativeRegClearGstRegShadowing(pReNative, idxReg, off);
    iemNativeRegMarkAllocated(pReNative, idxReg, kIemNativeWhat_Var, idxVar);

    iemNativeVarSetKindToStack(pReNative, idxVar);
    pVar->idxReg = idxReg;

    return idxReg;
}


/**
 * A convenient helper function.
 */
DECL_INLINE_THROW(uint8_t) iemNativeVarRegisterSetAndAcquire(PIEMRECOMPILERSTATE pReNative, uint8_t idxVar,
                                                             uint8_t idxReg, uint32_t *poff)
{
    idxReg = iemNativeVarRegisterSet(pReNative, idxVar, idxReg, *poff, false /*fAllocated*/);
    pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVar)].fRegAcquired = true;
    return idxReg;
}


/**
 * This is called by IEM_MC_END() to clean up all variables.
 */
DECL_FORCE_INLINE(void) iemNativeVarFreeAll(PIEMRECOMPILERSTATE pReNative)
{
    uint32_t const bmVars = pReNative->Core.bmVars;
    if (bmVars != 0)
        iemNativeVarFreeAllSlow(pReNative, bmVars);
    Assert(pReNative->Core.u64ArgVars == UINT64_MAX);
    Assert(pReNative->Core.bmStack    == 0);
}


#define IEM_MC_FREE_LOCAL(a_Name)   iemNativeVarFreeLocal(pReNative, a_Name)

/**
 * This is called by IEM_MC_FREE_LOCAL.
 */
DECLINLINE(void) iemNativeVarFreeLocal(PIEMRECOMPILERSTATE pReNative, uint8_t idxVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVar);
    Assert(pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVar)].uArgNo == UINT8_MAX);
    iemNativeVarFreeOneWorker(pReNative, IEMNATIVE_VAR_IDX_UNPACK(idxVar));
}


#define IEM_MC_FREE_ARG(a_Name)     iemNativeVarFreeArg(pReNative, a_Name)

/**
 * This is called by IEM_MC_FREE_ARG.
 */
DECLINLINE(void) iemNativeVarFreeArg(PIEMRECOMPILERSTATE pReNative, uint8_t idxVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVar);
    Assert(pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVar)].uArgNo < RT_ELEMENTS(pReNative->Core.aidxArgVars));
    iemNativeVarFreeOneWorker(pReNative, IEMNATIVE_VAR_IDX_UNPACK(idxVar));
}


#define IEM_MC_ASSIGN_TO_SMALLER(a_VarDst, a_VarSrcEol) off = iemNativeVarAssignToSmaller(pReNative, off, a_VarDst, a_VarSrcEol)

/**
 * This is called by IEM_MC_ASSIGN_TO_SMALLER.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeVarAssignToSmaller(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarDst, uint8_t idxVarSrc)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVarDst);
    PIEMNATIVEVAR const pVarDst = &pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVarDst)];
    AssertStmt(pVarDst->enmKind == kIemNativeVarKind_Invalid, IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_VAR_UNEXPECTED_KIND));
    Assert(   pVarDst->cbVar == sizeof(uint16_t)
           || pVarDst->cbVar == sizeof(uint32_t));

    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVarSrc);
    PIEMNATIVEVAR const pVarSrc = &pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVarSrc)];
    AssertStmt(   pVarSrc->enmKind == kIemNativeVarKind_Stack
               || pVarSrc->enmKind == kIemNativeVarKind_Immediate,
               IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_VAR_UNEXPECTED_KIND));

    Assert(pVarDst->cbVar < pVarSrc->cbVar);

    /*
     * Special case for immediates.
     */
    if (pVarSrc->enmKind == kIemNativeVarKind_Immediate)
    {
        switch (pVarDst->cbVar)
        {
            case sizeof(uint16_t):
                iemNativeVarSetKindToConst(pReNative, idxVarDst, (uint16_t)pVarSrc->u.uValue);
                break;
            case sizeof(uint32_t):
                iemNativeVarSetKindToConst(pReNative, idxVarDst, (uint32_t)pVarSrc->u.uValue);
                break;
            default: AssertFailed(); break;
        }
    }
    else
    {
        /*
         * The generic solution for now.
         */
        /** @todo optimize this by having the python script make sure the source
         *        variable passed to IEM_MC_ASSIGN_TO_SMALLER is not used after the
         *        statement.   Then we could just transfer the register assignments. */
        uint8_t const idxRegDst = iemNativeVarRegisterAcquire(pReNative, idxVarDst, &off);
        uint8_t const idxRegSrc = iemNativeVarRegisterAcquire(pReNative, idxVarSrc, &off);
        switch (pVarDst->cbVar)
        {
            case sizeof(uint16_t):
                off = iemNativeEmitLoadGprFromGpr16(pReNative, off, idxRegDst, idxRegSrc);
                break;
            case sizeof(uint32_t):
                off = iemNativeEmitLoadGprFromGpr32(pReNative, off, idxRegDst, idxRegSrc);
                break;
            default: AssertFailed(); break;
        }
        iemNativeVarRegisterRelease(pReNative, idxVarSrc);
        iemNativeVarRegisterRelease(pReNative, idxVarDst);
    }
    return off;
}



/*********************************************************************************************************************************
*   Emitters for IEM_MC_CALL_CIMPL_XXX                                                                                           *
*********************************************************************************************************************************/

/** Common emit function for IEM_MC_CALL_CIMPL_XXXX. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCallCImplCommon(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t cbInstr, uint8_t idxInstr,
                             uint64_t fGstShwFlush, uintptr_t pfnCImpl, uint8_t cArgs)

{
    IEMNATIVE_ASSERT_EFLAGS_SKIPPING_AND_POSTPONING(pReNative,  X86_EFL_STATUS_BITS);
    IEMNATIVE_STRICT_EFLAGS_SKIPPING_EMIT_CHECK(pReNative, off, X86_EFL_STATUS_BITS);

#ifdef IEMNATIVE_WITH_SIMD_REG_ALLOCATOR
    /* Clear the appropriate IEMNATIVE_SIMD_RAISE_XCPT_CHECKS_EMITTED_XXX flags
       when a calls clobber any of the relevant control registers. */
# if 1
    if (!(fGstShwFlush & (RT_BIT_64(kIemNativeGstReg_Cr0) | RT_BIT_64(kIemNativeGstReg_Cr4) | RT_BIT_64(kIemNativeGstReg_Xcr0))))
    {
        /* Likely as long as call+ret are done via cimpl. */
        Assert(   /*pfnCImpl != (uintptr_t)iemCImpl_mov_Cd_Rd && pfnCImpl != (uintptr_t)iemCImpl_xsetbv
               &&*/ pfnCImpl != (uintptr_t)iemCImpl_lmsw      && pfnCImpl != (uintptr_t)iemCImpl_clts);
    }
    else if (fGstShwFlush & RT_BIT_64(kIemNativeGstReg_Xcr0))
        pReNative->fSimdRaiseXcptChecksEmitted &= ~IEMNATIVE_SIMD_RAISE_XCPT_CHECKS_EMITTED_MAYBE_AVX;
    else if (fGstShwFlush & RT_BIT_64(kIemNativeGstReg_Cr4))
        pReNative->fSimdRaiseXcptChecksEmitted &= ~(  IEMNATIVE_SIMD_RAISE_XCPT_CHECKS_EMITTED_MAYBE_AVX
                                                    | IEMNATIVE_SIMD_RAISE_XCPT_CHECKS_EMITTED_MAYBE_SSE);
    else
        pReNative->fSimdRaiseXcptChecksEmitted &= ~(  IEMNATIVE_SIMD_RAISE_XCPT_CHECKS_EMITTED_MAYBE_AVX
                                                    | IEMNATIVE_SIMD_RAISE_XCPT_CHECKS_EMITTED_MAYBE_SSE
                                                    | IEMNATIVE_SIMD_RAISE_XCPT_CHECKS_EMITTED_MAYBE_DEVICE_NOT_AVAILABLE);

# else
    if (pfnCImpl == (uintptr_t)iemCImpl_xsetbv) /* Modifies xcr0 which only the AVX check uses. */
        pReNative->fSimdRaiseXcptChecksEmitted &= ~IEMNATIVE_SIMD_RAISE_XCPT_CHECKS_EMITTED_MAYBE_AVX;
    else if (pfnCImpl == (uintptr_t)iemCImpl_mov_Cd_Rd) /* Can modify cr4 which all checks use. */
        pReNative->fSimdRaiseXcptChecksEmitted = 0;
    else if (   pfnCImpl == (uintptr_t)iemCImpl_FarJmp
             || pfnCImpl == (uintptr_t)iemCImpl_callf
             || pfnCImpl == (uintptr_t)iemCImpl_lmsw
             || pfnCImpl == (uintptr_t)iemCImpl_clts) /* Will only modify cr0 */
        pReNative->fSimdRaiseXcptChecksEmitted &= ~(  IEMNATIVE_SIMD_RAISE_XCPT_CHECKS_EMITTED_MAYBE_AVX
                                                    | IEMNATIVE_SIMD_RAISE_XCPT_CHECKS_EMITTED_MAYBE_SSE
                                                    | IEMNATIVE_SIMD_RAISE_XCPT_CHECKS_EMITTED_MAYBE_DEVICE_NOT_AVAILABLE);
# endif

# ifdef IEMNATIVE_WITH_SIMD_FP_NATIVE_EMITTERS
    /* Mark the host floating point control register as not synced if MXCSR is modified. */
    if (fGstShwFlush & RT_BIT_64(kIemNativeGstReg_MxCsr))
        pReNative->fSimdRaiseXcptChecksEmitted &= ~IEMNATIVE_SIMD_HOST_FP_CTRL_REG_SYNCED;
# endif
#endif

    /*
     * Do all the call setup and cleanup.
     */
    off = iemNativeEmitCallCommon(pReNative, off, cArgs + IEM_CIMPL_HIDDEN_ARGS, IEM_CIMPL_HIDDEN_ARGS);

    /*
     * Load the two or three hidden arguments.
     */
#if defined(VBOXSTRICTRC_STRICT_ENABLED) && defined(RT_OS_WINDOWS) && defined(RT_ARCH_AMD64)
    off = iemNativeEmitLeaGprByBp(pReNative, off, IEMNATIVE_CALL_ARG0_GREG, IEMNATIVE_FP_OFF_IN_SHADOW_ARG0); /* rcStrict */
    off = iemNativeEmitLoadGprFromGpr(pReNative, off, IEMNATIVE_CALL_ARG1_GREG, IEMNATIVE_REG_FIXED_PVMCPU);
    off = iemNativeEmitLoadGpr8Imm(pReNative, off, IEMNATIVE_CALL_ARG2_GREG, cbInstr);
#else
    off = iemNativeEmitLoadGprFromGpr(pReNative, off, IEMNATIVE_CALL_ARG0_GREG, IEMNATIVE_REG_FIXED_PVMCPU);
    off = iemNativeEmitLoadGpr8Imm(pReNative, off, IEMNATIVE_CALL_ARG1_GREG, cbInstr);
#endif

    /*
     * Make the call and check the return code.
     *
     * Shadow PC copies are always flushed here, other stuff depends on flags.
     * Segment and general purpose registers are explictily flushed via the
     * IEM_MC_HINT_FLUSH_GUEST_SHADOW_GREG and IEM_MC_HINT_FLUSH_GUEST_SHADOW_SREG
     * macros.
     */
    off = iemNativeEmitCallImm(pReNative, off, (uintptr_t)pfnCImpl);
#if defined(VBOXSTRICTRC_STRICT_ENABLED) && defined(RT_OS_WINDOWS) && defined(RT_ARCH_AMD64)
    off = iemNativeEmitLoadGprByBpU32(pReNative, off, X86_GREG_xAX, IEMNATIVE_FP_OFF_IN_SHADOW_ARG0); /* rcStrict (see above) */
#endif
    fGstShwFlush = iemNativeCImplFlagsToGuestShadowFlushMask(pReNative->fCImpl, fGstShwFlush | RT_BIT_64(kIemNativeGstReg_Pc));
    if (!(pReNative->fMc & IEM_MC_F_WITHOUT_FLAGS)) /** @todo We don't emit with-flags/without-flags variations for CIMPL calls. */
        fGstShwFlush |= RT_BIT_64(kIemNativeGstReg_EFlags);
    iemNativeRegFlushGuestShadows(pReNative, fGstShwFlush);

#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING_DEBUG
    pReNative->Core.fDebugPcInitialized = false;
    Log4(("fDebugPcInitialized=false cimpl off=%#x (v1)\n", off));
#endif

    return iemNativeEmitCheckCallRetAndPassUp(pReNative, off, idxInstr);
}


#define IEM_MC_CALL_CIMPL_1_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0) \
    off = iemNativeEmitCallCImpl1(pReNative, off, a_cbInstr, pCallEntry->idxInstr, a_fGstShwFlush, (uintptr_t)a_pfnCImpl, a0)

/** Emits code for IEM_MC_CALL_CIMPL_1. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCallCImpl1(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t cbInstr, uint8_t idxInstr, uint64_t fGstShwFlush,
                        uintptr_t pfnCImpl, uint8_t idxArg0)
{
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg0, 0 + IEM_CIMPL_HIDDEN_ARGS);
    return iemNativeEmitCallCImplCommon(pReNative, off, cbInstr, idxInstr, fGstShwFlush, pfnCImpl, 1);
}


#define IEM_MC_CALL_CIMPL_2_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1) \
    off = iemNativeEmitCallCImpl2(pReNative, off, a_cbInstr, pCallEntry->idxInstr, a_fGstShwFlush, (uintptr_t)a_pfnCImpl, a0, a1)

/** Emits code for IEM_MC_CALL_CIMPL_2. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCallCImpl2(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t cbInstr, uint8_t idxInstr, uint64_t fGstShwFlush,
                        uintptr_t pfnCImpl, uint8_t idxArg0, uint8_t idxArg1)
{
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg0, 0 + IEM_CIMPL_HIDDEN_ARGS);
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg1, 1 + IEM_CIMPL_HIDDEN_ARGS);
    return iemNativeEmitCallCImplCommon(pReNative, off, cbInstr, idxInstr, fGstShwFlush, pfnCImpl, 2);
}


#define IEM_MC_CALL_CIMPL_3_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2) \
    off = iemNativeEmitCallCImpl3(pReNative, off, a_cbInstr, pCallEntry->idxInstr, a_fGstShwFlush, \
                                  (uintptr_t)a_pfnCImpl, a0, a1, a2)

/** Emits code for IEM_MC_CALL_CIMPL_3. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCallCImpl3(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t cbInstr, uint8_t idxInstr, uint64_t fGstShwFlush,
                        uintptr_t pfnCImpl, uint8_t idxArg0, uint8_t idxArg1, uint8_t idxArg2)
{
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg0, 0 + IEM_CIMPL_HIDDEN_ARGS);
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg1, 1 + IEM_CIMPL_HIDDEN_ARGS);
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg2, 2 + IEM_CIMPL_HIDDEN_ARGS);
    return iemNativeEmitCallCImplCommon(pReNative, off, cbInstr, idxInstr, fGstShwFlush, pfnCImpl, 3);
}


#define IEM_MC_CALL_CIMPL_4_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2, a3) \
    off = iemNativeEmitCallCImpl4(pReNative, off, a_cbInstr, pCallEntry->idxInstr, a_fGstShwFlush, \
                                  (uintptr_t)a_pfnCImpl, a0, a1, a2, a3)

/** Emits code for IEM_MC_CALL_CIMPL_4. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCallCImpl4(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t cbInstr, uint8_t idxInstr, uint64_t fGstShwFlush,
                        uintptr_t pfnCImpl, uint8_t idxArg0, uint8_t idxArg1, uint8_t idxArg2, uint8_t idxArg3)
{
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg0, 0 + IEM_CIMPL_HIDDEN_ARGS);
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg1, 1 + IEM_CIMPL_HIDDEN_ARGS);
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg2, 2 + IEM_CIMPL_HIDDEN_ARGS);
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg3, 3 + IEM_CIMPL_HIDDEN_ARGS);
    return iemNativeEmitCallCImplCommon(pReNative, off, cbInstr, idxInstr, fGstShwFlush, pfnCImpl, 4);
}


#define IEM_MC_CALL_CIMPL_5_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2, a3, a4) \
    off = iemNativeEmitCallCImpl5(pReNative, off, a_cbInstr, pCallEntry->idxInstr, a_fGstShwFlush, \
                                  (uintptr_t)a_pfnCImpl, a0, a1, a2, a3, a4)

/** Emits code for IEM_MC_CALL_CIMPL_4. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCallCImpl5(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t cbInstr, uint8_t idxInstr, uint64_t fGstShwFlush,
                        uintptr_t pfnCImpl, uint8_t idxArg0, uint8_t idxArg1, uint8_t idxArg2, uint8_t idxArg3, uint8_t idxArg4)
{
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg0, 0 + IEM_CIMPL_HIDDEN_ARGS);
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg1, 1 + IEM_CIMPL_HIDDEN_ARGS);
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg2, 2 + IEM_CIMPL_HIDDEN_ARGS);
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg3, 3 + IEM_CIMPL_HIDDEN_ARGS);
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg4, 4 + IEM_CIMPL_HIDDEN_ARGS);
    return iemNativeEmitCallCImplCommon(pReNative, off, cbInstr, idxInstr, fGstShwFlush, pfnCImpl, 5);
}


/** Recompiler debugging: Flush guest register shadow copies. */
#define IEM_MC_HINT_FLUSH_GUEST_SHADOW(g_fGstShwFlush) iemNativeRegFlushGuestShadows(pReNative, g_fGstShwFlush)



/*********************************************************************************************************************************
*   Emitters for IEM_MC_CALL_VOID_AIMPL_XXX and IEM_MC_CALL_AIMPL_XXX                                                            *
*********************************************************************************************************************************/

/**
 * Common worker for IEM_MC_CALL_VOID_AIMPL_XXX and IEM_MC_CALL_AIMPL_XXX.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCallAImplCommon(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarRc,
                             uintptr_t pfnAImpl, uint8_t cArgs)
{
    if (idxVarRc != UINT8_MAX)
    {
        IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVarRc);
        PIEMNATIVEVAR const pVarRc = &pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVarRc)];
        AssertStmt(pVarRc->uArgNo == UINT8_MAX,       IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_VAR_IPE_8));
        AssertStmt(pVarRc->cbVar <= sizeof(uint64_t), IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_VAR_IPE_9));
    }

    /*
     * Do all the call setup and cleanup.
     *
     * It is only required to flush pending guest register writes in call volatile registers as
     * assembly helpers can't throw and don't access anything living in CPUMCTX, they only
     * access parameters. The flushing of call volatile registers is always done in iemNativeEmitCallCommon()
     * no matter the fFlushPendingWrites parameter.
     */
    off = iemNativeEmitCallCommon(pReNative, off, cArgs, 0 /*cHiddenArgs*/, false /*fFlushPendingWrites*/);

    /*
     * Make the call and update the return code variable if we've got one.
     */
    off = iemNativeEmitCallImm<true /*a_fSkipEflChecks*/>(pReNative, off, pfnAImpl);
    if (idxVarRc != UINT8_MAX)
        iemNativeVarRegisterSet(pReNative, idxVarRc, IEMNATIVE_CALL_RET_GREG, off, false /*fAllocated*/);

    return off;
}



#define IEM_MC_CALL_VOID_AIMPL_0(a_pfn) \
    off = iemNativeEmitCallAImpl0(pReNative, off, UINT8_MAX /*idxVarRc*/, (uintptr_t)(a_pfn))

#define IEM_MC_CALL_AIMPL_0(a_rc, a_pfn) \
    off = iemNativeEmitCallAImpl0(pReNative, off, a_rc,                   (uintptr_t)(a_pfn))

/** Emits code for IEM_MC_CALL_VOID_AIMPL_0 and IEM_MC_CALL_AIMPL_0. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCallAImpl0(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarRc, uintptr_t pfnAImpl)
{
    return iemNativeEmitCallAImplCommon(pReNative, off, idxVarRc, pfnAImpl, 0);
}


#define IEM_MC_CALL_VOID_AIMPL_1(a_pfn, a0) \
    off = iemNativeEmitCallAImpl1(pReNative, off, UINT8_MAX /*idxVarRc*/, (uintptr_t)(a_pfn), a0)

#define IEM_MC_CALL_AIMPL_1(a_rc, a_pfn, a0) \
    off = iemNativeEmitCallAImpl1(pReNative, off, a_rc,                   (uintptr_t)(a_pfn), a0)

/** Emits code for IEM_MC_CALL_VOID_AIMPL_1 and IEM_MC_CALL_AIMPL_1. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCallAImpl1(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarRc, uintptr_t pfnAImpl, uint8_t idxArg0)
{
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg0, 0);
    return iemNativeEmitCallAImplCommon(pReNative, off, idxVarRc, pfnAImpl, 1);
}


#define IEM_MC_CALL_VOID_AIMPL_2(a_pfn, a0, a1) \
    off = iemNativeEmitCallAImpl2(pReNative, off, UINT8_MAX /*idxVarRc*/, (uintptr_t)(a_pfn), a0, a1)

#define IEM_MC_CALL_AIMPL_2(a_rc, a_pfn, a0, a1) \
    off = iemNativeEmitCallAImpl2(pReNative, off, a_rc,                   (uintptr_t)(a_pfn), a0, a1)

/** Emits code for IEM_MC_CALL_VOID_AIMPL_2 and IEM_MC_CALL_AIMPL_2. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCallAImpl2(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarRc,
                        uintptr_t pfnAImpl, uint8_t idxArg0, uint8_t idxArg1)
{
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg0, 0);
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg1, 1);
    return iemNativeEmitCallAImplCommon(pReNative, off, idxVarRc, pfnAImpl, 2);
}


#define IEM_MC_CALL_VOID_AIMPL_3(a_pfn, a0, a1, a2) \
    off = iemNativeEmitCallAImpl3(pReNative, off, UINT8_MAX /*idxVarRc*/, (uintptr_t)(a_pfn), a0, a1, a2)

#define IEM_MC_CALL_AIMPL_3(a_rcType, a_rc, a_pfn, a0, a1, a2) \
    IEM_MC_LOCAL(a_rcType, a_rc); \
    off = iemNativeEmitCallAImpl3(pReNative, off, a_rc,                   (uintptr_t)(a_pfn), a0, a1, a2)

/** Emits code for IEM_MC_CALL_VOID_AIMPL_3 and IEM_MC_CALL_AIMPL_3. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCallAImpl3(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarRc,
                        uintptr_t pfnAImpl, uint8_t idxArg0, uint8_t idxArg1, uint8_t idxArg2)
{
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg0, 0);
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg1, 1);
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg2, 2);
    return iemNativeEmitCallAImplCommon(pReNative, off, idxVarRc, pfnAImpl, 3);
}


#define IEM_MC_CALL_VOID_AIMPL_4(a_pfn, a0, a1, a2, a3) \
    off = iemNativeEmitCallAImpl4(pReNative, off, UINT8_MAX /*idxVarRc*/, (uintptr_t)(a_pfn), a0, a1, a2, a3)

#define IEM_MC_CALL_AIMPL_4(a_rcType, a_rc, a_pfn, a0, a1, a2, a3) \
    IEM_MC_LOCAL(a_rcType, a_rc); \
    off = iemNativeEmitCallAImpl4(pReNative, off, a_rc,                   (uintptr_t)(a_pfn), a0, a1, a2, a3)

/** Emits code for IEM_MC_CALL_VOID_AIMPL_4 and IEM_MC_CALL_AIMPL_4. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCallAImpl4(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarRc,
                        uintptr_t pfnAImpl, uint8_t idxArg0, uint8_t idxArg1, uint8_t idxArg2, uint8_t idxArg3)
{
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg0, 0);
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg1, 1);
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg2, 2);
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg3, 3);
    return iemNativeEmitCallAImplCommon(pReNative, off, idxVarRc, pfnAImpl, 4);
}



/*********************************************************************************************************************************
*   Emitters for general purpose register fetches (IEM_MC_FETCH_GREG_XXX).                                                       *
*********************************************************************************************************************************/

#define IEM_MC_FETCH_GREG_U8_THREADED(a_u8Dst, a_iGRegEx) \
    off = iemNativeEmitFetchGregU8(pReNative, off, a_u8Dst,  a_iGRegEx, sizeof(uint8_t) /*cbZeroExtended*/)

#define IEM_MC_FETCH_GREG_U8_ZX_U16_THREADED(a_u16Dst, a_iGRegEx) \
    off = iemNativeEmitFetchGregU8(pReNative, off, a_u16Dst, a_iGRegEx, sizeof(uint16_t) /*cbZeroExtended*/)

#define IEM_MC_FETCH_GREG_U8_ZX_U32_THREADED(a_u32Dst, a_iGRegEx) \
    off = iemNativeEmitFetchGregU8(pReNative, off, a_u32Dst, a_iGRegEx, sizeof(uint32_t) /*cbZeroExtended*/)

#define IEM_MC_FETCH_GREG_U8_ZX_U64_THREADED(a_u64Dst, a_iGRegEx) \
    off = iemNativeEmitFetchGregU8(pReNative, off, a_u64Dst, a_iGRegEx, sizeof(uint64_t) /*cbZeroExtended*/)


/** Emits code for IEM_MC_FETCH_GREG_U8_THREADED and
 *  IEM_MC_FETCH_GREG_U8_ZX_U16/32/64_THREADED. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitFetchGregU8(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxDstVar, uint8_t iGRegEx, int8_t cbZeroExtended)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxDstVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxDstVar, cbZeroExtended); RT_NOREF(cbZeroExtended);
    Assert(iGRegEx < 20);

    /* Same discussion as in iemNativeEmitFetchGregU16 */
    uint8_t const idxGstFullReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(iGRegEx & 15),
                                                                  kIemNativeGstRegUse_ReadOnly);

    iemNativeVarSetKindToStack(pReNative, idxDstVar);
    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxDstVar, &off);

    /* The value is zero-extended to the full 64-bit host register width. */
    if (iGRegEx < 16)
        off = iemNativeEmitLoadGprFromGpr8(pReNative, off, idxVarReg, idxGstFullReg);
    else
        off = iemNativeEmitLoadGprFromGpr8Hi(pReNative, off, idxVarReg, idxGstFullReg);

    iemNativeVarRegisterRelease(pReNative, idxDstVar);
    iemNativeRegFreeTmp(pReNative, idxGstFullReg);
    return off;
}


#define IEM_MC_FETCH_GREG_U8_SX_U16_THREADED(a_u16Dst, a_iGRegEx) \
    off = iemNativeEmitFetchGregU8Sx(pReNative, off, a_u16Dst, a_iGRegEx, sizeof(uint16_t))

#define IEM_MC_FETCH_GREG_U8_SX_U32_THREADED(a_u32Dst, a_iGRegEx) \
    off = iemNativeEmitFetchGregU8Sx(pReNative, off, a_u32Dst, a_iGRegEx, sizeof(uint32_t))

#define IEM_MC_FETCH_GREG_U8_SX_U64_THREADED(a_u64Dst, a_iGRegEx) \
    off = iemNativeEmitFetchGregU8Sx(pReNative, off, a_u64Dst, a_iGRegEx, sizeof(uint64_t))

/** Emits code for IEM_MC_FETCH_GREG_U8_SX_U16/32/64_THREADED. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitFetchGregU8Sx(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxDstVar, uint8_t iGRegEx, uint8_t cbSignExtended)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxDstVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxDstVar, cbSignExtended);
    Assert(iGRegEx < 20);

    /* Same discussion as in iemNativeEmitFetchGregU16 */
    uint8_t const idxGstFullReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(iGRegEx & 15),
                                                                  kIemNativeGstRegUse_ReadOnly);

    iemNativeVarSetKindToStack(pReNative, idxDstVar);
    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxDstVar, &off);

    if (iGRegEx < 16)
    {
        switch (cbSignExtended)
        {
            case sizeof(uint16_t):
                off = iemNativeEmitLoadGpr16SignExtendedFromGpr8(pReNative, off, idxVarReg, idxGstFullReg);
                break;
            case sizeof(uint32_t):
                off = iemNativeEmitLoadGpr32SignExtendedFromGpr8(pReNative, off, idxVarReg, idxGstFullReg);
                break;
            case sizeof(uint64_t):
                off = iemNativeEmitLoadGprSignExtendedFromGpr8(pReNative, off, idxVarReg, idxGstFullReg);
                break;
            default: AssertFailed(); break;
        }
    }
    else
    {
        off = iemNativeEmitLoadGprFromGpr8Hi(pReNative, off, idxVarReg, idxGstFullReg);
        switch (cbSignExtended)
        {
            case sizeof(uint16_t):
                off = iemNativeEmitLoadGpr16SignExtendedFromGpr8(pReNative, off, idxVarReg, idxVarReg);
                break;
            case sizeof(uint32_t):
                off = iemNativeEmitLoadGpr32SignExtendedFromGpr8(pReNative, off, idxVarReg, idxVarReg);
                break;
            case sizeof(uint64_t):
                off = iemNativeEmitLoadGprSignExtendedFromGpr8(pReNative, off, idxVarReg, idxVarReg);
                break;
            default: AssertFailed(); break;
        }
    }

    iemNativeVarRegisterRelease(pReNative, idxDstVar);
    iemNativeRegFreeTmp(pReNative, idxGstFullReg);
    return off;
}



#define IEM_MC_FETCH_GREG_U16(a_u16Dst, a_iGReg) \
    off = iemNativeEmitFetchGregU16(pReNative, off, a_u16Dst, a_iGReg, sizeof(uint16_t))

#define IEM_MC_FETCH_GREG_U16_ZX_U32(a_u16Dst, a_iGReg) \
    off = iemNativeEmitFetchGregU16(pReNative, off, a_u16Dst, a_iGReg, sizeof(uint32_t))

#define IEM_MC_FETCH_GREG_U16_ZX_U64(a_u16Dst, a_iGReg) \
    off = iemNativeEmitFetchGregU16(pReNative, off, a_u16Dst, a_iGReg, sizeof(uint64_t))

/** Emits code for IEM_MC_FETCH_GREG_U16 and IEM_MC_FETCH_GREG_U16_ZX_U32/64. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitFetchGregU16(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxDstVar, uint8_t iGReg, uint8_t cbZeroExtended)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxDstVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxDstVar, cbZeroExtended); RT_NOREF(cbZeroExtended);
    Assert(iGReg < 16);

    /*
     * We can either just load the low 16-bit of the GPR into a host register
     * for the variable, or we can do so via a shadow copy host register. The
     * latter will avoid having to reload it if it's being stored later, but
     * will waste a host register if it isn't touched again.  Since we don't
     * know what going to happen, we choose the latter for now.
     */
    uint8_t const idxGstFullReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(iGReg),
                                                                  kIemNativeGstRegUse_ReadOnly);

    iemNativeVarSetKindToStack(pReNative, idxDstVar);
    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxDstVar, &off);
    off = iemNativeEmitLoadGprFromGpr16(pReNative, off, idxVarReg, idxGstFullReg);
    iemNativeVarRegisterRelease(pReNative, idxDstVar);

    iemNativeRegFreeTmp(pReNative, idxGstFullReg);
    return off;
}

#define IEM_MC_FETCH_GREG_I16(a_i16Dst, a_iGReg) \
    off = iemNativeEmitFetchGregI16(pReNative, off, a_i16Dst, a_iGReg)

/** Emits code for IEM_MC_FETCH_GREG_I16. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitFetchGregI16(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxDstVar, uint8_t iGReg)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxDstVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxDstVar, sizeof(int16_t));
    Assert(iGReg < 16);

    /*
     * We can either just load the low 16-bit of the GPR into a host register
     * for the variable, or we can do so via a shadow copy host register. The
     * latter will avoid having to reload it if it's being stored later, but
     * will waste a host register if it isn't touched again.  Since we don't
     * know what going to happen, we choose the latter for now.
     */
    uint8_t const idxGstFullReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(iGReg),
                                                                  kIemNativeGstRegUse_ReadOnly);

    iemNativeVarSetKindToStack(pReNative, idxDstVar);
    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxDstVar, &off);
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitLoadGprFromGpr16(pReNative, off, idxVarReg, idxGstFullReg);
#elif defined(RT_ARCH_ARM64) /* Note! There are no 16-bit registers on ARM, we emulate that through 32-bit registers which requires sign extension. */
    off = iemNativeEmitLoadGpr32SignExtendedFromGpr16(pReNative, off, idxVarReg, idxGstFullReg);
#endif
    iemNativeVarRegisterRelease(pReNative, idxDstVar);

    iemNativeRegFreeTmp(pReNative, idxGstFullReg);
    return off;
}


#define IEM_MC_FETCH_GREG_U16_SX_U32(a_u16Dst, a_iGReg) \
    off = iemNativeEmitFetchGregU16Sx(pReNative, off, a_u16Dst, a_iGReg, sizeof(uint32_t))

#define IEM_MC_FETCH_GREG_U16_SX_U64(a_u16Dst, a_iGReg) \
    off = iemNativeEmitFetchGregU16Sx(pReNative, off, a_u16Dst, a_iGReg, sizeof(uint64_t))

/** Emits code for IEM_MC_FETCH_GREG_U16_SX_U32/64. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitFetchGregU16Sx(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxDstVar, uint8_t iGReg, uint8_t cbSignExtended)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxDstVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxDstVar, cbSignExtended);
    Assert(iGReg < 16);

    /*
     * We can either just load the low 16-bit of the GPR into a host register
     * for the variable, or we can do so via a shadow copy host register. The
     * latter will avoid having to reload it if it's being stored later, but
     * will waste a host register if it isn't touched again.  Since we don't
     * know what going to happen, we choose the latter for now.
     */
    uint8_t const idxGstFullReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(iGReg),
                                                                  kIemNativeGstRegUse_ReadOnly);

    iemNativeVarSetKindToStack(pReNative, idxDstVar);
    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxDstVar, &off);
    if (cbSignExtended == sizeof(uint32_t))
        off = iemNativeEmitLoadGpr32SignExtendedFromGpr16(pReNative, off, idxVarReg, idxGstFullReg);
    else
    {
        Assert(cbSignExtended == sizeof(uint64_t));
        off = iemNativeEmitLoadGprSignExtendedFromGpr16(pReNative, off, idxVarReg, idxGstFullReg);
    }
    iemNativeVarRegisterRelease(pReNative, idxDstVar);

    iemNativeRegFreeTmp(pReNative, idxGstFullReg);
    return off;
}


#define IEM_MC_FETCH_GREG_I32(a_i32Dst, a_iGReg) \
    off = iemNativeEmitFetchGregU32(pReNative, off, a_i32Dst, a_iGReg, sizeof(uint32_t))

#define IEM_MC_FETCH_GREG_U32(a_u32Dst, a_iGReg) \
    off = iemNativeEmitFetchGregU32(pReNative, off, a_u32Dst, a_iGReg, sizeof(uint32_t))

#define IEM_MC_FETCH_GREG_U32_ZX_U64(a_u32Dst, a_iGReg) \
    off = iemNativeEmitFetchGregU32(pReNative, off, a_u32Dst, a_iGReg, sizeof(uint64_t))

/** Emits code for IEM_MC_FETCH_GREG_U32. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitFetchGregU32(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxDstVar, uint8_t iGReg, uint8_t cbZeroExtended)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxDstVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxDstVar, cbZeroExtended); RT_NOREF(cbZeroExtended);
    Assert(iGReg < 16);

    /*
     * We can either just load the low 16-bit of the GPR into a host register
     * for the variable, or we can do so via a shadow copy host register. The
     * latter will avoid having to reload it if it's being stored later, but
     * will waste a host register if it isn't touched again.  Since we don't
     * know what going to happen, we choose the latter for now.
     */
    uint8_t const idxGstFullReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(iGReg),
                                                                  kIemNativeGstRegUse_ReadOnly);

    iemNativeVarSetKindToStack(pReNative, idxDstVar);
    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxDstVar, &off);
    off = iemNativeEmitLoadGprFromGpr32(pReNative, off, idxVarReg, idxGstFullReg);
    iemNativeVarRegisterRelease(pReNative, idxDstVar);

    iemNativeRegFreeTmp(pReNative, idxGstFullReg);
    return off;
}


#define IEM_MC_FETCH_GREG_U32_SX_U64(a_u32Dst, a_iGReg) \
    off = iemNativeEmitFetchGregU32SxU64(pReNative, off, a_u32Dst, a_iGReg)

/** Emits code for IEM_MC_FETCH_GREG_U32. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitFetchGregU32SxU64(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxDstVar, uint8_t iGReg)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxDstVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxDstVar, sizeof(uint64_t));
    Assert(iGReg < 16);

    /*
     * We can either just load the low 32-bit of the GPR into a host register
     * for the variable, or we can do so via a shadow copy host register. The
     * latter will avoid having to reload it if it's being stored later, but
     * will waste a host register if it isn't touched again.  Since we don't
     * know what going to happen, we choose the latter for now.
     */
    uint8_t const idxGstFullReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(iGReg),
                                                                  kIemNativeGstRegUse_ReadOnly);

    iemNativeVarSetKindToStack(pReNative, idxDstVar);
    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxDstVar, &off);
    off = iemNativeEmitLoadGprSignExtendedFromGpr32(pReNative, off, idxVarReg, idxGstFullReg);
    iemNativeVarRegisterRelease(pReNative, idxDstVar);

    iemNativeRegFreeTmp(pReNative, idxGstFullReg);
    return off;
}


#define IEM_MC_FETCH_GREG_U64(a_u64Dst, a_iGReg) \
    off = iemNativeEmitFetchGregU64(pReNative, off, a_u64Dst, a_iGReg)

#define IEM_MC_FETCH_GREG_U64_ZX_U64(a_u64Dst, a_iGReg) \
    off = iemNativeEmitFetchGregU64(pReNative, off, a_u64Dst, a_iGReg)

/** Emits code for IEM_MC_FETCH_GREG_U64 (and the
 *  IEM_MC_FETCH_GREG_U64_ZX_U64 alias). */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitFetchGregU64(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxDstVar, uint8_t iGReg)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxDstVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxDstVar, sizeof(uint64_t));
    Assert(iGReg < 16);

    uint8_t const idxGstFullReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(iGReg),
                                                                  kIemNativeGstRegUse_ReadOnly);

    iemNativeVarSetKindToStack(pReNative, idxDstVar);
    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxDstVar, &off);
    off = iemNativeEmitLoadGprFromGpr(pReNative, off, idxVarReg, idxGstFullReg);
    /** @todo name the register a shadow one already? */
    iemNativeVarRegisterRelease(pReNative, idxDstVar);

    iemNativeRegFreeTmp(pReNative, idxGstFullReg);
    return off;
}


#ifdef IEMNATIVE_WITH_SIMD_REG_ALLOCATOR
#define IEM_MC_FETCH_GREG_PAIR_U64(a_u128Dst, a_iGRegLo, a_iGRegHi) \
    off = iemNativeEmitFetchGregPairU64(pReNative, off, a_u128Dst, a_iGRegLo, a_iGRegHi)

/** Emits code for IEM_MC_FETCH_GREG_PAIR_U64. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitFetchGregPairU64(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxDstVar, uint8_t iGRegLo, uint8_t iGRegHi)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxDstVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxDstVar, sizeof(RTUINT128U));
    Assert(iGRegLo < 16 && iGRegHi < 16);

    uint8_t const idxGstFullRegLo = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(iGRegLo),
                                                                    kIemNativeGstRegUse_ReadOnly);
    uint8_t const idxGstFullRegHi = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(iGRegHi),
                                                                    kIemNativeGstRegUse_ReadOnly);

    iemNativeVarSetKindToStack(pReNative, idxDstVar);
    uint8_t const idxVarReg = iemNativeVarSimdRegisterAcquire(pReNative, idxDstVar, &off);
    off = iemNativeEmitSimdStoreGprToVecRegU64(pReNative, off, idxVarReg, idxGstFullRegLo, 0);
    off = iemNativeEmitSimdStoreGprToVecRegU64(pReNative, off, idxVarReg, idxGstFullRegHi, 1);

    iemNativeVarSimdRegisterRelease(pReNative, idxDstVar);
    iemNativeRegFreeTmp(pReNative, idxGstFullRegLo);
    iemNativeRegFreeTmp(pReNative, idxGstFullRegHi);
    return off;
}
#endif


/*********************************************************************************************************************************
*   Emitters for general purpose register stores (IEM_MC_STORE_GREG_XXX).                                                        *
*********************************************************************************************************************************/

#define IEM_MC_STORE_GREG_U8_CONST_THREADED(a_iGRegEx, a_u8Value) \
    off = iemNativeEmitStoreGregU8Const(pReNative, off, a_iGRegEx, a_u8Value)

/** Emits code for IEM_MC_STORE_GREG_U8_CONST_THREADED. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitStoreGregU8Const(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGRegEx, uint8_t u8Value)
{
    Assert(iGRegEx < 20);
    uint8_t const idxGstTmpReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(iGRegEx & 15),
                                                                 kIemNativeGstRegUse_ForUpdate);
#ifdef RT_ARCH_AMD64
    uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 12);

    /* To the lowest byte of the register: mov r8, imm8 */
    if (iGRegEx < 16)
    {
        if (idxGstTmpReg >= 8)
            pbCodeBuf[off++] = X86_OP_REX_B;
        else if (idxGstTmpReg >= 4)
            pbCodeBuf[off++] = X86_OP_REX;
        pbCodeBuf[off++] = 0xb0 + (idxGstTmpReg & 7);
        pbCodeBuf[off++] = u8Value;
    }
    /* Otherwise it's to ah, ch, dh or bh: use mov r8, imm8 if we can, otherwise, we rotate. */
    else if (idxGstTmpReg < 4)
    {
        pbCodeBuf[off++] = 0xb4 + idxGstTmpReg;
        pbCodeBuf[off++] = u8Value;
    }
    else
    {
        /* ror reg64, 8 */
        pbCodeBuf[off++] = X86_OP_REX_W | (idxGstTmpReg < 8 ? 0 : X86_OP_REX_B);
        pbCodeBuf[off++] = 0xc1;
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 1, idxGstTmpReg & 7);
        pbCodeBuf[off++] = 8;

        /* mov reg8, imm8  */
        if (idxGstTmpReg >= 8)
            pbCodeBuf[off++] = X86_OP_REX_B;
        else if (idxGstTmpReg >= 4)
            pbCodeBuf[off++] = X86_OP_REX;
        pbCodeBuf[off++] = 0xb0 + (idxGstTmpReg & 7);
        pbCodeBuf[off++] = u8Value;

        /* rol reg64, 8 */
        pbCodeBuf[off++] = X86_OP_REX_W | (idxGstTmpReg < 8 ? 0 : X86_OP_REX_B);
        pbCodeBuf[off++] = 0xc1;
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, idxGstTmpReg & 7);
        pbCodeBuf[off++] = 8;
    }

#elif defined(RT_ARCH_ARM64)
    uint8_t const    idxImmReg   = iemNativeRegAllocTmpImm(pReNative, &off, u8Value);
    uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 2);
    if (iGRegEx < 16)
        /* bfi w1, w2, 0, 8 - moves bits 7:0 from idxImmReg to idxGstTmpReg bits 7:0. */
        pu32CodeBuf[off++] = Armv8A64MkInstrBfi(idxGstTmpReg, idxImmReg, 0, 8);
    else
        /* bfi w1, w2, 8, 8 - moves bits 7:0 from idxImmReg to idxGstTmpReg bits 15:8. */
        pu32CodeBuf[off++] = Armv8A64MkInstrBfi(idxGstTmpReg, idxImmReg, 8, 8);
    iemNativeRegFreeTmp(pReNative, idxImmReg);

#else
# error "Port me!"
#endif

    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#if !defined(IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK)
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxGstTmpReg, RT_UOFFSETOF_DYN(VMCPU, cpum.GstCtx.aGRegs[iGRegEx & 15]));
#endif

    iemNativeRegFreeTmp(pReNative, idxGstTmpReg);
    return off;
}


#define IEM_MC_STORE_GREG_U8_THREADED(a_iGRegEx, a_u8Value) \
    off = iemNativeEmitStoreGregU8(pReNative, off, a_iGRegEx, a_u8Value)

/** Emits code for IEM_MC_STORE_GREG_U8_THREADED. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitStoreGregU8(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGRegEx, uint8_t idxValueVar)
{
    Assert(iGRegEx < 20);
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxValueVar);

    /*
     * If it's a constant value (unlikely) we treat this as a
     * IEM_MC_STORE_GREG_U8_CONST statement.
     */
    PIEMNATIVEVAR const pValueVar = &pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxValueVar)];
    if (pValueVar->enmKind == kIemNativeVarKind_Stack)
    { /* likely */ }
    else
    {
        AssertStmt(pValueVar->enmKind == kIemNativeVarKind_Immediate,
                   IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_VAR_UNEXPECTED_KIND));
        return iemNativeEmitStoreGregU8Const(pReNative, off, iGRegEx, (uint8_t)pValueVar->u.uValue);
    }

    uint8_t const idxGstTmpReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(iGRegEx & 15),
                                                                 kIemNativeGstRegUse_ForUpdate);
    uint8_t const    idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxValueVar, &off, true /*fInitialized*/);

#ifdef RT_ARCH_AMD64
    /* To the lowest byte of the register: mov reg8, reg8(r/m) */
    if (iGRegEx < 16)
    {
        uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 3);
        if (idxGstTmpReg >= 8 || idxVarReg >= 8)
            pbCodeBuf[off++] = (idxGstTmpReg >= 8 ? X86_OP_REX_R : 0) | (idxVarReg >= 8 ? X86_OP_REX_B : 0);
        else if (idxGstTmpReg >= 4 || idxVarReg >= 4)
            pbCodeBuf[off++] = X86_OP_REX;
        pbCodeBuf[off++] = 0x8a;
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, idxGstTmpReg & 7, idxVarReg & 7);
    }
    /* Otherwise it's to ah, ch, dh or bh from al, cl, dl or bl: use mov r8, r8 if we can, otherwise, we rotate. */
    else if (idxGstTmpReg < 4 && idxVarReg < 4)
    {
        uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 2+1);
        pbCodeBuf[off++] = 0x8a;
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, idxGstTmpReg + 4, idxVarReg);
    }
    else
    {
        uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 15);

        /* ror reg64, 8 */
        pbCodeBuf[off++] = X86_OP_REX_W | (idxGstTmpReg < 8 ? 0 : X86_OP_REX_B);
        pbCodeBuf[off++] = 0xc1;
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 1, idxGstTmpReg & 7);
        pbCodeBuf[off++] = 8;

        /* mov reg8, reg8(r/m)  */
        if (idxGstTmpReg >= 8 || idxVarReg >= 8)
            pbCodeBuf[off++] = (idxGstTmpReg >= 8 ? X86_OP_REX_R : 0) | (idxVarReg >= 8 ? X86_OP_REX_B : 0);
        else if (idxGstTmpReg >= 4 || idxVarReg >= 4)
            pbCodeBuf[off++] = X86_OP_REX;
        pbCodeBuf[off++] = 0x8a;
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, idxGstTmpReg & 7, idxVarReg & 7);

        /* rol reg64, 8 */
        pbCodeBuf[off++] = X86_OP_REX_W | (idxGstTmpReg < 8 ? 0 : X86_OP_REX_B);
        pbCodeBuf[off++] = 0xc1;
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, idxGstTmpReg & 7);
        pbCodeBuf[off++] = 8;
    }

#elif defined(RT_ARCH_ARM64)
    /* bfi w1, w2, 0, 8 - moves bits 7:0 from idxVarReg to idxGstTmpReg bits 7:0.
            or
       bfi w1, w2, 8, 8 - moves bits 7:0 from idxVarReg to idxGstTmpReg bits 15:8. */
    uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
    if (iGRegEx < 16)
        pu32CodeBuf[off++] = Armv8A64MkInstrBfi(idxGstTmpReg, idxVarReg, 0, 8);
    else
        pu32CodeBuf[off++] = Armv8A64MkInstrBfi(idxGstTmpReg, idxVarReg, 8, 8);

#else
# error "Port me!"
#endif
    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

    iemNativeVarRegisterRelease(pReNative, idxValueVar);

#if !defined(IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK)
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxGstTmpReg, RT_UOFFSETOF_DYN(VMCPU, cpum.GstCtx.aGRegs[iGRegEx & 15]));
#endif
    iemNativeRegFreeTmp(pReNative, idxGstTmpReg);
    return off;
}



#define IEM_MC_STORE_GREG_U16_CONST(a_iGReg, a_u16Const) \
    off = iemNativeEmitStoreGregU16Const(pReNative, off, a_iGReg, a_u16Const)

/** Emits code for IEM_MC_STORE_GREG_U16. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitStoreGregU16Const(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGReg, uint16_t uValue)
{
    Assert(iGReg < 16);
    uint8_t const idxGstTmpReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(iGReg),
                                                                 kIemNativeGstRegUse_ForUpdate);
#ifdef RT_ARCH_AMD64
    /* mov reg16, imm16 */
    uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 5);
    pbCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
    if (idxGstTmpReg >= 8)
        pbCodeBuf[off++] = X86_OP_REX_B;
    pbCodeBuf[off++] = 0xb8 + (idxGstTmpReg & 7);
    pbCodeBuf[off++] = RT_BYTE1(uValue);
    pbCodeBuf[off++] = RT_BYTE2(uValue);

#elif defined(RT_ARCH_ARM64)
    /* movk xdst, #uValue, lsl #0 */
    uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
    pu32CodeBuf[off++] = Armv8A64MkInstrMovK(idxGstTmpReg, uValue);

#else
# error "Port me!"
#endif

    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#if !defined(IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK)
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxGstTmpReg, RT_UOFFSETOF_DYN(VMCPU, cpum.GstCtx.aGRegs[iGReg]));
#endif
    iemNativeRegFreeTmp(pReNative, idxGstTmpReg);
    return off;
}


#define IEM_MC_STORE_GREG_U16(a_iGReg, a_u16Value) \
    off = iemNativeEmitStoreGregU16(pReNative, off, a_iGReg, a_u16Value)

/** Emits code for IEM_MC_STORE_GREG_U16. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitStoreGregU16(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGReg, uint8_t idxValueVar)
{
    Assert(iGReg < 16);
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxValueVar);

    /*
     * If it's a constant value (unlikely) we treat this as a
     * IEM_MC_STORE_GREG_U16_CONST statement.
     */
    PIEMNATIVEVAR const pValueVar = &pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxValueVar)];
    if (pValueVar->enmKind == kIemNativeVarKind_Stack)
    { /* likely */ }
    else
    {
        AssertStmt(pValueVar->enmKind == kIemNativeVarKind_Immediate,
                   IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_VAR_UNEXPECTED_KIND));
        return iemNativeEmitStoreGregU16Const(pReNative, off, iGReg, (uint16_t)pValueVar->u.uValue);
    }

    uint8_t const idxGstTmpReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(iGReg),
                                                                 kIemNativeGstRegUse_ForUpdate);

#ifdef RT_ARCH_AMD64
    /* mov reg16, reg16 or [mem16] */
    uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 12);
    pbCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
    if (pValueVar->idxReg < RT_ELEMENTS(pReNative->Core.aHstRegs))
    {
        if (idxGstTmpReg >= 8 || pValueVar->idxReg >= 8)
            pbCodeBuf[off++] = (idxGstTmpReg      >= 8 ? X86_OP_REX_R : 0)
                             | (pValueVar->idxReg >= 8 ? X86_OP_REX_B : 0);
        pbCodeBuf[off++] = 0x8b;
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, idxGstTmpReg & 7, pValueVar->idxReg & 7);
    }
    else
    {
        uint8_t const idxStackSlot = pValueVar->idxStackSlot;
        AssertStmt(idxStackSlot != UINT8_MAX, IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_VAR_NOT_INITIALIZED));
        if (idxGstTmpReg >= 8)
            pbCodeBuf[off++] = X86_OP_REX_R;
        pbCodeBuf[off++] = 0x8b;
        off = iemNativeEmitGprByBpDisp(pbCodeBuf, off, idxGstTmpReg, iemNativeStackCalcBpDisp(idxStackSlot), pReNative);
    }

#elif defined(RT_ARCH_ARM64)
    /* bfi w1, w2, 0, 16 - moves bits 15:0 from idxVarReg to idxGstTmpReg bits 15:0. */
    uint8_t const    idxVarReg   = iemNativeVarRegisterAcquire(pReNative, idxValueVar, &off, true /*fInitialized*/);
    uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
    pu32CodeBuf[off++] = Armv8A64MkInstrBfi(idxGstTmpReg, idxVarReg, 0, 16);
    iemNativeVarRegisterRelease(pReNative, idxValueVar);

#else
# error "Port me!"
#endif

    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#if !defined(IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK)
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxGstTmpReg, RT_UOFFSETOF_DYN(VMCPU, cpum.GstCtx.aGRegs[iGReg]));
#endif
    iemNativeRegFreeTmp(pReNative, idxGstTmpReg);
    return off;
}


#define IEM_MC_STORE_GREG_U32_CONST(a_iGReg, a_u32Const) \
    off = iemNativeEmitStoreGregU32Const(pReNative, off, a_iGReg, a_u32Const)

/** Emits code for IEM_MC_STORE_GREG_U32_CONST. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitStoreGregU32Const(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGReg, uint32_t uValue)
{
    Assert(iGReg < 16);
    uint8_t const idxGstTmpReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(iGReg),
                                                                 kIemNativeGstRegUse_ForFullWrite);
    off = iemNativeEmitLoadGprImm64(pReNative, off, idxGstTmpReg, uValue);
#if !defined(IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK)
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxGstTmpReg, RT_UOFFSETOF_DYN(VMCPU, cpum.GstCtx.aGRegs[iGReg]));
#endif
    iemNativeRegFreeTmp(pReNative, idxGstTmpReg);
    return off;
}


#define IEM_MC_STORE_GREG_U32(a_iGReg, a_u32Value) \
    off = iemNativeEmitStoreGregU32(pReNative, off, a_iGReg, a_u32Value)

#define IEM_MC_STORE_GREG_I32(a_iGReg, a_i32Value) \
    off = iemNativeEmitStoreGregU32(pReNative, off, a_iGReg, a_i32Value)

/** Emits code for IEM_MC_STORE_GREG_U32/IEM_MC_STORE_GREG_I32. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitStoreGregU32(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGReg, uint8_t idxValueVar)
{
    Assert(iGReg < 16);
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxValueVar);

    /*
     * If it's a constant value (unlikely) we treat this as a
     * IEM_MC_STORE_GREG_U32_CONST statement.
     */
    PIEMNATIVEVAR const pValueVar = &pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxValueVar)];
    if (pValueVar->enmKind == kIemNativeVarKind_Stack)
    { /* likely */ }
    else
    {
        AssertStmt(pValueVar->enmKind == kIemNativeVarKind_Immediate,
                   IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_VAR_UNEXPECTED_KIND));
        return iemNativeEmitStoreGregU32Const(pReNative, off, iGReg, (uint32_t)pValueVar->u.uValue);
    }

    /*
     * For the rest we allocate a guest register for the variable and writes
     * it to the CPUMCTX structure.
     */
    uint8_t const idxVarReg = iemNativeVarRegisterAcquireForGuestReg(pReNative, idxValueVar, IEMNATIVEGSTREG_GPR(iGReg), &off);
#if !defined(IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK)
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxVarReg, RT_UOFFSETOF_DYN(VMCPU, cpum.GstCtx.aGRegs[iGReg]));
#else
    RT_NOREF(idxVarReg);
#endif
#ifdef VBOX_STRICT
    off = iemNativeEmitTop32BitsClearCheck(pReNative, off, idxVarReg);
#endif
    iemNativeVarRegisterRelease(pReNative, idxValueVar);
    return off;
}


#define IEM_MC_STORE_GREG_U64_CONST(a_iGReg, a_u64Const) \
    off = iemNativeEmitStoreGregU64Const(pReNative, off, a_iGReg, a_u64Const)

/** Emits code for IEM_MC_STORE_GREG_U64_CONST. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitStoreGregU64Const(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGReg, uint64_t uValue)
{
    Assert(iGReg < 16);
    uint8_t const idxGstTmpReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(iGReg),
                                                                 kIemNativeGstRegUse_ForFullWrite);
    off = iemNativeEmitLoadGprImm64(pReNative, off, idxGstTmpReg, uValue);
#if !defined(IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK)
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxGstTmpReg, RT_UOFFSETOF_DYN(VMCPU, cpum.GstCtx.aGRegs[iGReg]));
#endif
    iemNativeRegFreeTmp(pReNative, idxGstTmpReg);
    return off;
}


#define IEM_MC_STORE_GREG_U64(a_iGReg, a_u64Value) \
    off = iemNativeEmitStoreGregU64(pReNative, off, a_iGReg, a_u64Value)

#define IEM_MC_STORE_GREG_I64(a_iGReg, a_i64Value) \
    off = iemNativeEmitStoreGregU64(pReNative, off, a_iGReg, a_i64Value)

/** Emits code for IEM_MC_STORE_GREG_U64. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitStoreGregU64(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGReg, uint8_t idxValueVar)
{
    Assert(iGReg < 16);
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxValueVar);

    /*
     * If it's a constant value (unlikely) we treat this as a
     * IEM_MC_STORE_GREG_U64_CONST statement.
     */
    PIEMNATIVEVAR const pValueVar = &pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxValueVar)];
    if (pValueVar->enmKind == kIemNativeVarKind_Stack)
    { /* likely */ }
    else
    {
        AssertStmt(pValueVar->enmKind == kIemNativeVarKind_Immediate,
                   IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_VAR_UNEXPECTED_KIND));
        return iemNativeEmitStoreGregU64Const(pReNative, off, iGReg, pValueVar->u.uValue);
    }

    /*
     * For the rest we allocate a guest register for the variable and writes
     * it to the CPUMCTX structure.
     */
    uint8_t const idxVarReg = iemNativeVarRegisterAcquireForGuestReg(pReNative, idxValueVar, IEMNATIVEGSTREG_GPR(iGReg), &off);
#if !defined(IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK)
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxVarReg, RT_UOFFSETOF_DYN(VMCPU, cpum.GstCtx.aGRegs[iGReg]));
#else
    RT_NOREF(idxVarReg);
#endif
    iemNativeVarRegisterRelease(pReNative, idxValueVar);
    return off;
}


#define IEM_MC_CLEAR_HIGH_GREG_U64(a_iGReg) \
    off = iemNativeEmitClearHighGregU64(pReNative, off, a_iGReg)

/** Emits code for IEM_MC_CLEAR_HIGH_GREG_U64. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitClearHighGregU64(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGReg)
{
    Assert(iGReg < 16);
    uint8_t const idxGstTmpReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(iGReg),
                                                                 kIemNativeGstRegUse_ForUpdate);
    off = iemNativeEmitLoadGprFromGpr32(pReNative, off, idxGstTmpReg, idxGstTmpReg);
#if !defined(IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK)
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxGstTmpReg, RT_UOFFSETOF_DYN(VMCPU, cpum.GstCtx.aGRegs[iGReg]));
#endif
    iemNativeRegFreeTmp(pReNative, idxGstTmpReg);
    return off;
}


#ifdef IEMNATIVE_WITH_SIMD_REG_ALLOCATOR
#define IEM_MC_STORE_GREG_PAIR_U64(a_iGRegLo, a_iGRegHi, a_u128Value) \
    off = iemNativeEmitStoreGregPairU64(pReNative, off, a_iGRegLo, a_iGRegHi, a_u128Value)

/** Emits code for IEM_MC_FETCH_GREG_PAIR_U64. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitStoreGregPairU64(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGRegLo, uint8_t iGRegHi, uint8_t idxDstVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxDstVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxDstVar, sizeof(RTUINT128U));
    Assert(iGRegLo < 16 && iGRegHi < 16);

    uint8_t const idxGstFullRegLo = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(iGRegLo),
                                                                    kIemNativeGstRegUse_ForFullWrite);
    uint8_t const idxGstFullRegHi = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(iGRegHi),
                                                                    kIemNativeGstRegUse_ForFullWrite);

    iemNativeVarSetKindToStack(pReNative, idxDstVar);
    uint8_t const idxVarReg = iemNativeVarSimdRegisterAcquire(pReNative, idxDstVar, &off, true /*fInitialized*/);
    off = iemNativeEmitSimdLoadGprFromVecRegU64(pReNative, off, idxGstFullRegLo, idxVarReg, 0);
    off = iemNativeEmitSimdLoadGprFromVecRegU64(pReNative, off, idxGstFullRegHi, idxVarReg, 1);

    iemNativeVarSimdRegisterRelease(pReNative, idxDstVar);
    iemNativeRegFreeTmp(pReNative, idxGstFullRegLo);
    iemNativeRegFreeTmp(pReNative, idxGstFullRegHi);
    return off;
}
#endif


/*********************************************************************************************************************************
*   General purpose register manipulation (add, sub).                                                                            *
*********************************************************************************************************************************/

#define IEM_MC_ADD_GREG_U16(a_iGReg, a_u8SubtrahendConst) \
    off = iemNativeEmitAddGregU16(pReNative, off, a_iGReg, a_u8SubtrahendConst)

/** Emits code for IEM_MC_ADD_GREG_U16. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitAddGregU16(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGReg, uint8_t uAddend)
{
    uint8_t const idxGstTmpReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(iGReg),
                                                                 kIemNativeGstRegUse_ForUpdate);

#ifdef RT_ARCH_AMD64
    uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 6);
    pbCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
    if (idxGstTmpReg >= 8)
        pbCodeBuf[off++] = X86_OP_REX_B;
    if (uAddend == 1)
    {
        pbCodeBuf[off++] = 0xff; /* inc */
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, idxGstTmpReg & 7);
    }
    else
    {
        pbCodeBuf[off++] = 0x81;
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, idxGstTmpReg & 7);
        pbCodeBuf[off++] = uAddend;
        pbCodeBuf[off++] = 0;
    }

#else
    uint8_t const    idxTmpReg   = iemNativeRegAllocTmp(pReNative, &off);
    uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 2);

    /* sub tmp, gstgrp, uAddend */
    pu32CodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(false /*fSub*/, idxTmpReg, idxGstTmpReg, uAddend, false /*f64Bit*/);

    /* bfi w1, w2, 0, 16 - moves bits 15:0 from tmpreg2 to tmpreg. */
    pu32CodeBuf[off++] = Armv8A64MkInstrBfi(idxGstTmpReg, idxTmpReg, 0, 16);

    iemNativeRegFreeTmp(pReNative, idxTmpReg);
#endif

    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#if !defined(IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK)
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxGstTmpReg, RT_UOFFSETOF_DYN(VMCPU, cpum.GstCtx.aGRegs[iGReg]));
#endif

    iemNativeRegFreeTmp(pReNative, idxGstTmpReg);
    return off;
}


#define IEM_MC_ADD_GREG_U32(a_iGReg, a_u8Const) \
    off = iemNativeEmitAddGregU32U64(pReNative, off, a_iGReg, a_u8Const, false /*f64Bit*/)

#define IEM_MC_ADD_GREG_U64(a_iGReg, a_u8Const) \
    off = iemNativeEmitAddGregU32U64(pReNative, off, a_iGReg, a_u8Const, true /*f64Bit*/)

/** Emits code for IEM_MC_ADD_GREG_U32 and IEM_MC_ADD_GREG_U64. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitAddGregU32U64(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGReg, uint8_t uAddend, bool f64Bit)
{
    uint8_t const idxGstTmpReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(iGReg),
                                                                 kIemNativeGstRegUse_ForUpdate);

#ifdef RT_ARCH_AMD64
    uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 7);
    if (f64Bit)
        pbCodeBuf[off++] = X86_OP_REX_W | (idxGstTmpReg >= 8 ? X86_OP_REX_B : 0);
    else if (idxGstTmpReg >= 8)
        pbCodeBuf[off++] = X86_OP_REX_B;
    if (uAddend == 1)
    {
        pbCodeBuf[off++] = 0xff; /* inc */
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, idxGstTmpReg & 7);
    }
    else if (uAddend < 128)
    {
        pbCodeBuf[off++] = 0x83; /* add */
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, idxGstTmpReg & 7);
        pbCodeBuf[off++] = RT_BYTE1(uAddend);
    }
    else
    {
        pbCodeBuf[off++] = 0x81; /* add */
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 0, idxGstTmpReg & 7);
        pbCodeBuf[off++] = RT_BYTE1(uAddend);
        pbCodeBuf[off++] = 0;
        pbCodeBuf[off++] = 0;
        pbCodeBuf[off++] = 0;
    }

#else
    /* sub tmp, gstgrp, uAddend */
    uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
    pu32CodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(false /*fSub*/, idxGstTmpReg, idxGstTmpReg, uAddend, f64Bit);

#endif

    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#if !defined(IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK)
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxGstTmpReg, RT_UOFFSETOF_DYN(VMCPU, cpum.GstCtx.aGRegs[iGReg]));
#endif

    iemNativeRegFreeTmp(pReNative, idxGstTmpReg);
    return off;
}



#define IEM_MC_SUB_GREG_U16(a_iGReg, a_u8SubtrahendConst) \
    off = iemNativeEmitSubGregU16(pReNative, off, a_iGReg, a_u8SubtrahendConst)

/** Emits code for IEM_MC_SUB_GREG_U16. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSubGregU16(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGReg, uint8_t uSubtrahend)
{
    uint8_t const idxGstTmpReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(iGReg),
                                                                 kIemNativeGstRegUse_ForUpdate);

#ifdef RT_ARCH_AMD64
    uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 6);
    pbCodeBuf[off++] = X86_OP_PRF_SIZE_OP;
    if (idxGstTmpReg >= 8)
        pbCodeBuf[off++] = X86_OP_REX_B;
    if (uSubtrahend == 1)
    {
        pbCodeBuf[off++] = 0xff; /* dec */
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 1, idxGstTmpReg & 7);
    }
    else
    {
        pbCodeBuf[off++] = 0x81;
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 5, idxGstTmpReg & 7);
        pbCodeBuf[off++] = uSubtrahend;
        pbCodeBuf[off++] = 0;
    }

#else
    uint8_t const    idxTmpReg   = iemNativeRegAllocTmp(pReNative, &off);
    uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 2);

    /* sub tmp, gstgrp, uSubtrahend */
    pu32CodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(true /*fSub*/, idxTmpReg, idxGstTmpReg, uSubtrahend, false /*f64Bit*/);

    /* bfi w1, w2, 0, 16 - moves bits 15:0 from tmpreg2 to tmpreg. */
    pu32CodeBuf[off++] = Armv8A64MkInstrBfi(idxGstTmpReg, idxTmpReg, 0, 16);

    iemNativeRegFreeTmp(pReNative, idxTmpReg);
#endif

    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#if !defined(IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK)
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxGstTmpReg, RT_UOFFSETOF_DYN(VMCPU, cpum.GstCtx.aGRegs[iGReg]));
#endif

    iemNativeRegFreeTmp(pReNative, idxGstTmpReg);
    return off;
}


#define IEM_MC_SUB_GREG_U32(a_iGReg, a_u8Const) \
    off = iemNativeEmitSubGregU32U64(pReNative, off, a_iGReg, a_u8Const, false /*f64Bit*/)

#define IEM_MC_SUB_GREG_U64(a_iGReg, a_u8Const) \
    off = iemNativeEmitSubGregU32U64(pReNative, off, a_iGReg, a_u8Const, true /*f64Bit*/)

/** Emits code for IEM_MC_SUB_GREG_U32 and IEM_MC_SUB_GREG_U64. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSubGregU32U64(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGReg, uint8_t uSubtrahend, bool f64Bit)
{
    uint8_t const idxGstTmpReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(iGReg),
                                                                 kIemNativeGstRegUse_ForUpdate);

#ifdef RT_ARCH_AMD64
    uint8_t *pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 7);
    if (f64Bit)
        pbCodeBuf[off++] = X86_OP_REX_W | (idxGstTmpReg >= 8 ? X86_OP_REX_B : 0);
    else if (idxGstTmpReg >= 8)
        pbCodeBuf[off++] = X86_OP_REX_B;
    if (uSubtrahend == 1)
    {
        pbCodeBuf[off++] = 0xff; /* dec */
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 1, idxGstTmpReg & 7);
    }
    else if (uSubtrahend < 128)
    {
        pbCodeBuf[off++] = 0x83; /* sub */
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 5, idxGstTmpReg & 7);
        pbCodeBuf[off++] = RT_BYTE1(uSubtrahend);
    }
    else
    {
        pbCodeBuf[off++] = 0x81; /* sub */
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_REG, 5, idxGstTmpReg & 7);
        pbCodeBuf[off++] = RT_BYTE1(uSubtrahend);
        pbCodeBuf[off++] = 0;
        pbCodeBuf[off++] = 0;
        pbCodeBuf[off++] = 0;
    }

#else
    /* sub tmp, gstgrp, uSubtrahend */
    uint32_t *pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
    pu32CodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(true /*fSub*/, idxGstTmpReg, idxGstTmpReg, uSubtrahend, f64Bit);

#endif

    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#if !defined(IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK)
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxGstTmpReg, RT_UOFFSETOF_DYN(VMCPU, cpum.GstCtx.aGRegs[iGReg]));
#endif

    iemNativeRegFreeTmp(pReNative, idxGstTmpReg);
    return off;
}


#define IEM_MC_AND_GREG_U8(a_iGReg, a_u8Mask) \
    off = iemNativeEmitAndGReg(pReNative, off, a_iGReg, a_u8Mask, sizeof(uint8_t))

#define IEM_MC_AND_GREG_U16(a_iGReg, a_u16Mask) \
    off = iemNativeEmitAndGReg(pReNative, off, a_iGReg, a_u16Mask, sizeof(uint16_t))

#define IEM_MC_AND_GREG_U32(a_iGReg, a_u32Mask) \
    off = iemNativeEmitAndGReg(pReNative, off, a_iGReg, a_u32Mask, sizeof(uint32_t))

#define IEM_MC_AND_GREG_U64(a_iGReg, a_u64Mask) \
    off = iemNativeEmitAndGReg(pReNative, off, a_iGReg, a_u64Mask, sizeof(uint64_t))

/** Emits code for IEM_MC_AND_GREG_U8, IEM_MC_AND_GREG_U16, IEM_MC_AND_GREG_U32 and IEM_MC_AND_GREG_U64. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitAndGReg(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGReg, uint64_t uMask, uint8_t cbMask)
{
#ifdef VBOX_STRICT
    switch (cbMask)
    {
        case sizeof(uint8_t):  Assert((uint8_t)uMask  == uMask); break;
        case sizeof(uint16_t): Assert((uint16_t)uMask == uMask); break;
        case sizeof(uint32_t): Assert((uint32_t)uMask == uMask); break;
        case sizeof(uint64_t): break;
        default: AssertFailedBreak();
    }
#endif

    uint8_t const idxGstTmpReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(iGReg),
                                                                 kIemNativeGstRegUse_ForUpdate);

    switch (cbMask)
    {
        case sizeof(uint8_t): /* Leaves the higher bits untouched. */
            off = iemNativeEmitAndGprByImm(pReNative, off, idxGstTmpReg, uMask | UINT64_C(0xffffffffffffff00));
            break;
        case sizeof(uint16_t): /* Leaves the higher bits untouched. */
            off = iemNativeEmitAndGprByImm(pReNative, off, idxGstTmpReg, uMask | UINT64_C(0xffffffffffff0000));
            break;
        case sizeof(uint32_t): /* Zeroes the high 32 bits of the guest register. */
                off = iemNativeEmitAndGpr32ByImm(pReNative, off, idxGstTmpReg, uMask);
                break;
        case sizeof(uint64_t):
                off = iemNativeEmitAndGprByImm(pReNative, off, idxGstTmpReg, uMask);
                break;
        default: AssertFailedBreak();
    }

    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#if !defined(IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK)
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxGstTmpReg, RT_UOFFSETOF_DYN(VMCPU, cpum.GstCtx.aGRegs[iGReg]));
#endif

    iemNativeRegFreeTmp(pReNative, idxGstTmpReg);
    return off;
}


#define IEM_MC_OR_GREG_U8(a_iGReg, a_u8Mask) \
    off = iemNativeEmitOrGReg(pReNative, off, a_iGReg, a_u8Mask, sizeof(uint8_t))

#define IEM_MC_OR_GREG_U16(a_iGReg, a_u16Mask) \
    off = iemNativeEmitOrGReg(pReNative, off, a_iGReg, a_u16Mask, sizeof(uint16_t))

#define IEM_MC_OR_GREG_U32(a_iGReg, a_u32Mask) \
    off = iemNativeEmitOrGReg(pReNative, off, a_iGReg, a_u32Mask, sizeof(uint32_t))

#define IEM_MC_OR_GREG_U64(a_iGReg, a_u64Mask) \
    off = iemNativeEmitOrGReg(pReNative, off, a_iGReg, a_u64Mask, sizeof(uint64_t))

/** Emits code for IEM_MC_OR_GREG_U8, IEM_MC_OR_GREG_U16, IEM_MC_OR_GREG_U32 and IEM_MC_OR_GREG_U64. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitOrGReg(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iGReg, uint64_t uMask, uint8_t cbMask)
{
#ifdef VBOX_STRICT
    switch (cbMask)
    {
        case sizeof(uint8_t):  Assert((uint8_t)uMask  == uMask); break;
        case sizeof(uint16_t): Assert((uint16_t)uMask == uMask); break;
        case sizeof(uint32_t): Assert((uint32_t)uMask == uMask); break;
        case sizeof(uint64_t): break;
        default: AssertFailedBreak();
    }
#endif

    uint8_t const idxGstTmpReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(iGReg),
                                                                 kIemNativeGstRegUse_ForUpdate);

    switch (cbMask)
    {
        case sizeof(uint8_t): /* Leaves the higher bits untouched. */
        case sizeof(uint16_t):
        case sizeof(uint64_t):
            off = iemNativeEmitOrGprByImm(pReNative, off, idxGstTmpReg, uMask);
            break;
        case sizeof(uint32_t): /* Zeroes the high 32 bits of the guest register. */
            off = iemNativeEmitOrGpr32ByImm(pReNative, off, idxGstTmpReg, uMask);
            break;
        default: AssertFailedBreak();
    }

    IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

#if !defined(IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK)
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxGstTmpReg, RT_UOFFSETOF_DYN(VMCPU, cpum.GstCtx.aGRegs[iGReg]));
#endif

    iemNativeRegFreeTmp(pReNative, idxGstTmpReg);
    return off;
}


/*********************************************************************************************************************************
*   Local/Argument variable manipulation (add, sub, and, or).                                                                             *
*********************************************************************************************************************************/

#define IEM_MC_AND_LOCAL_U8(a_u8Local, a_u8Mask) \
    off = iemNativeEmitAndLocal(pReNative, off, a_u8Local, a_u8Mask, sizeof(uint8_t))

#define IEM_MC_AND_LOCAL_U16(a_u16Local, a_u16Mask) \
    off = iemNativeEmitAndLocal(pReNative, off, a_u16Local, a_u16Mask, sizeof(uint16_t))

#define IEM_MC_AND_LOCAL_U32(a_u32Local, a_u32Mask) \
    off = iemNativeEmitAndLocal(pReNative, off, a_u32Local, a_u32Mask, sizeof(uint32_t))

#define IEM_MC_AND_LOCAL_U64(a_u64Local, a_u64Mask) \
    off = iemNativeEmitAndLocal(pReNative, off, a_u64Local, a_u64Mask, sizeof(uint64_t))


#define IEM_MC_AND_ARG_U16(a_u16Arg, a_u16Mask) \
    off = iemNativeEmitAndLocal(pReNative, off, a_u16Arg, a_u16Mask, sizeof(uint16_t))

#define IEM_MC_AND_ARG_U32(a_u32Arg, a_u32Mask) \
    off = iemNativeEmitAndLocal(pReNative, off, a_u32Arg, a_u32Mask, sizeof(uint32_t))

#define IEM_MC_AND_ARG_U64(a_u64Arg, a_u64Mask) \
    off = iemNativeEmitAndLocal(pReNative, off, a_u64Arg, a_u64Mask, sizeof(uint64_t))

/** Emits code for AND'ing a local and a constant value.   */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitAndLocal(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVar, uint64_t uMask, uint8_t cbMask)
{
#ifdef VBOX_STRICT
    switch (cbMask)
    {
        case sizeof(uint8_t):  Assert((uint8_t)uMask  == uMask); break;
        case sizeof(uint16_t): Assert((uint16_t)uMask == uMask); break;
        case sizeof(uint32_t): Assert((uint32_t)uMask == uMask); break;
        case sizeof(uint64_t): break;
        default: AssertFailedBreak();
    }
#endif

    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxVar, &off, true /*fInitialized*/);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxVar, cbMask);

    if (cbMask <= sizeof(uint32_t))
        off = iemNativeEmitAndGpr32ByImm(pReNative, off, idxVarReg, uMask);
    else
        off = iemNativeEmitAndGprByImm(pReNative, off, idxVarReg, uMask);

    iemNativeVarRegisterRelease(pReNative, idxVar);
    return off;
}


#define IEM_MC_OR_LOCAL_U8(a_u8Local, a_u8Mask) \
    off = iemNativeEmitOrLocal(pReNative, off, a_u8Local, a_u8Mask, sizeof(uint8_t))

#define IEM_MC_OR_LOCAL_U16(a_u16Local, a_u16Mask) \
    off = iemNativeEmitOrLocal(pReNative, off, a_u16Local, a_u16Mask, sizeof(uint16_t))

#define IEM_MC_OR_LOCAL_U32(a_u32Local, a_u32Mask) \
    off = iemNativeEmitOrLocal(pReNative, off, a_u32Local, a_u32Mask, sizeof(uint32_t))

#define IEM_MC_OR_LOCAL_U64(a_u64Local, a_u64Mask) \
    off = iemNativeEmitOrLocal(pReNative, off, a_u64Local, a_u64Mask, sizeof(uint64_t))

/** Emits code for OR'ing a local and a constant value.   */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitOrLocal(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVar, uint64_t uMask, uint8_t cbMask)
{
#ifdef VBOX_STRICT
    switch (cbMask)
    {
        case sizeof(uint8_t):  Assert((uint8_t)uMask  == uMask); break;
        case sizeof(uint16_t): Assert((uint16_t)uMask == uMask); break;
        case sizeof(uint32_t): Assert((uint32_t)uMask == uMask); break;
        case sizeof(uint64_t): break;
        default: AssertFailedBreak();
    }
#endif

    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxVar, &off, true /*fInitialized*/);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxVar, cbMask);

    if (cbMask <= sizeof(uint32_t))
        off = iemNativeEmitOrGpr32ByImm(pReNative, off, idxVarReg, uMask);
    else
        off = iemNativeEmitOrGprByImm(pReNative, off, idxVarReg, uMask);

    iemNativeVarRegisterRelease(pReNative, idxVar);
    return off;
}


#define IEM_MC_BSWAP_LOCAL_U16(a_u16Local) \
    off = iemNativeEmitBswapLocal(pReNative, off, a_u16Local, sizeof(uint16_t))

#define IEM_MC_BSWAP_LOCAL_U32(a_u32Local) \
    off = iemNativeEmitBswapLocal(pReNative, off, a_u32Local, sizeof(uint32_t))

#define IEM_MC_BSWAP_LOCAL_U64(a_u64Local) \
    off = iemNativeEmitBswapLocal(pReNative, off, a_u64Local, sizeof(uint64_t))

/** Emits code for reversing the byte order in a local value.   */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitBswapLocal(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVar, uint8_t cbLocal)
{
    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxVar, &off, true /*fInitialized*/);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxVar, cbLocal);

    switch (cbLocal)
    {
        case sizeof(uint16_t): off = iemNativeEmitBswapGpr16(pReNative, off, idxVarReg); break;
        case sizeof(uint32_t): off = iemNativeEmitBswapGpr32(pReNative, off, idxVarReg); break;
        case sizeof(uint64_t): off = iemNativeEmitBswapGpr(pReNative, off, idxVarReg);   break;
        default: AssertFailedBreak();
    }

    iemNativeVarRegisterRelease(pReNative, idxVar);
    return off;
}


#define IEM_MC_SHL_LOCAL_S16(a_i16Local, a_cShift) \
    off = iemNativeEmitShlLocal(pReNative, off, a_i16Local, sizeof(int16_t), a_cShift)

#define IEM_MC_SHL_LOCAL_S32(a_i32Local, a_cShift) \
    off = iemNativeEmitShlLocal(pReNative, off, a_i32Local, sizeof(int32_t), a_cShift)

#define IEM_MC_SHL_LOCAL_S64(a_i64Local, a_cShift) \
    off = iemNativeEmitShlLocal(pReNative, off, a_i64Local, sizeof(int64_t), a_cShift)

/** Emits code for shifting left a local value.   */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitShlLocal(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVar, uint8_t cbLocal, uint8_t cShift)
{
#ifdef VBOX_STRICT
    switch (cbLocal)
    {
        case sizeof(uint8_t):  Assert(cShift < 8); break;
        case sizeof(uint16_t): Assert(cShift < 16); break;
        case sizeof(uint32_t): Assert(cShift < 32); break;
        case sizeof(uint64_t): Assert(cShift < 64); break;
        default: AssertFailedBreak();
    }
#endif

    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxVar, &off, true /*fInitialized*/);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxVar, cbLocal);

    if (cbLocal <= sizeof(uint32_t))
    {
        off = iemNativeEmitShiftGpr32Left(pReNative, off, idxVarReg, cShift);
        if (cbLocal < sizeof(uint32_t))
            off = iemNativeEmitAndGpr32ByImm(pReNative, off, idxVarReg,
                                               cbLocal == sizeof(uint16_t)
                                             ? UINT32_C(0xffff)
                                             : UINT32_C(0xff));
    }
    else
        off = iemNativeEmitShiftGprLeft(pReNative, off, idxVarReg, cShift);

    iemNativeVarRegisterRelease(pReNative, idxVar);
    return off;
}


#define IEM_MC_SAR_LOCAL_S16(a_i16Local, a_cShift) \
    off = iemNativeEmitSarLocal(pReNative, off, a_i16Local, sizeof(int16_t), a_cShift)

#define IEM_MC_SAR_LOCAL_S32(a_i32Local, a_cShift) \
    off = iemNativeEmitSarLocal(pReNative, off, a_i32Local, sizeof(int32_t), a_cShift)

#define IEM_MC_SAR_LOCAL_S64(a_i64Local, a_cShift) \
    off = iemNativeEmitSarLocal(pReNative, off, a_i64Local, sizeof(int64_t), a_cShift)

/** Emits code for shifting left a local value.   */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSarLocal(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVar, uint8_t cbLocal, uint8_t cShift)
{
#ifdef VBOX_STRICT
    switch (cbLocal)
    {
        case sizeof(int8_t):  Assert(cShift < 8); break;
        case sizeof(int16_t): Assert(cShift < 16); break;
        case sizeof(int32_t): Assert(cShift < 32); break;
        case sizeof(int64_t): Assert(cShift < 64); break;
        default: AssertFailedBreak();
    }
#endif

    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxVar, &off, true /*fInitialized*/);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxVar, cbLocal);

    /* Need to sign extend the value first to make sure the sign is correct in the following arithmetic shift. */
    if (cbLocal == sizeof(uint8_t))
        off = iemNativeEmitLoadGpr32SignExtendedFromGpr8(pReNative, off, idxVarReg, idxVarReg);
    else if (cbLocal == sizeof(uint16_t))
        off = iemNativeEmitLoadGpr32SignExtendedFromGpr16(pReNative, off, idxVarReg, idxVarReg);

    if (cbLocal <= sizeof(uint32_t))
        off = iemNativeEmitArithShiftGpr32Right(pReNative, off, idxVarReg, cShift);
    else
        off = iemNativeEmitArithShiftGprRight(pReNative, off, idxVarReg, cShift);

    iemNativeVarRegisterRelease(pReNative, idxVar);
    return off;
}


#define IEM_MC_ADD_LOCAL_S16_TO_EFF_ADDR(a_EffAddr, a_i16) \
    off = iemNativeEmitAddLocalToEffAddr(pReNative, off, a_EffAddr, a_i16, sizeof(int16_t))

#define IEM_MC_ADD_LOCAL_S32_TO_EFF_ADDR(a_EffAddr, a_i32) \
    off = iemNativeEmitAddLocalToEffAddr(pReNative, off, a_EffAddr, a_i32, sizeof(int32_t))

#define IEM_MC_ADD_LOCAL_S64_TO_EFF_ADDR(a_EffAddr, a_i64) \
    off = iemNativeEmitAddLocalToEffAddr(pReNative, off, a_EffAddr, a_i64, sizeof(int64_t))

/** Emits code for IEM_MC_ADD_LOCAL_S16_TO_EFF_ADDR/IEM_MC_ADD_LOCAL_S32_TO_EFF_ADDR/IEM_MC_ADD_LOCAL_S64_TO_EFF_ADDR.   */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitAddLocalToEffAddr(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarEffAddr, uint8_t idxVar, uint8_t cbLocal)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVarEffAddr);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxVarEffAddr, sizeof(RTGCPTR));
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxVar, cbLocal);

    uint8_t const idxVarReg        = iemNativeVarRegisterAcquire(pReNative, idxVar, &off, true /*fInitialized*/);
    uint8_t const idxVarRegEffAddr = iemNativeVarRegisterAcquire(pReNative, idxVarEffAddr, &off, true /*fInitialized*/);

    /* Need to sign extend the value. */
    if (cbLocal <= sizeof(uint32_t))
    {
/** @todo ARM64: In case of boredone, the extended add instruction can do the
 * conversion directly: ADD idxVarRegEffAddr, idxVarRegEffAddr, [w]idxVarReg, SXTH/SXTW */
        uint8_t const idxRegTmp = iemNativeRegAllocTmp(pReNative, &off);

        switch (cbLocal)
        {
            case sizeof(int16_t): off = iemNativeEmitLoadGprSignExtendedFromGpr16(pReNative, off, idxRegTmp, idxVarReg); break;
            case sizeof(int32_t): off = iemNativeEmitLoadGprSignExtendedFromGpr32(pReNative, off, idxRegTmp, idxVarReg); break;
            default: AssertFailed();
        }

        off = iemNativeEmitAddTwoGprs(pReNative, off, idxVarRegEffAddr, idxRegTmp);
        iemNativeRegFreeTmp(pReNative, idxRegTmp);
    }
    else
        off = iemNativeEmitAddTwoGprs(pReNative, off, idxVarRegEffAddr, idxVarReg);

    iemNativeVarRegisterRelease(pReNative, idxVarEffAddr);
    iemNativeVarRegisterRelease(pReNative, idxVar);
    return off;
}



/*********************************************************************************************************************************
*   EFLAGS                                                                                                                       *
*********************************************************************************************************************************/

#if !defined(VBOX_WITH_STATISTICS) || !defined(IEMNATIVE_WITH_LIVENESS_ANALYSIS)
# define IEMNATIVE_EFLAGS_OPTIMIZATION_STATS(a_fEflInput, a_fEflOutput)     ((void)0)
#else
# define IEMNATIVE_EFLAGS_OPTIMIZATION_STATS(a_fEflInput, a_fEflOutput) \
    iemNativeEFlagsOptimizationStats(pReNative, a_fEflInput, a_fEflOutput)

DECLINLINE(void) iemNativeEFlagsOptimizationStats(PIEMRECOMPILERSTATE pReNative, uint32_t fEflInput, uint32_t fEflOutput)
{
    if (fEflOutput)
    {
        PVMCPUCC const pVCpu = pReNative->pVCpu;
# ifndef IEMLIVENESS_EXTENDED_LAYOUT
        IEMLIVENESSBIT const LivenessBit0 = pReNative->paLivenessEntries[pReNative->idxCurCall].Bit0;
        IEMLIVENESSBIT const LivenessBit1 = pReNative->paLivenessEntries[pReNative->idxCurCall].Bit1;
        AssertCompile(IEMLIVENESS_STATE_CLOBBERED == 0);
#  define CHECK_FLAG_AND_UPDATE_STATS(a_fEfl, a_fLivenessMember, a_CoreStatName) \
            if (fEflOutput & (a_fEfl)) \
            { \
                if (LivenessBit0.a_fLivenessMember | LivenessBit1.a_fLivenessMember) \
                    STAM_COUNTER_INC(&pVCpu->iem.s.a_CoreStatName ## Required); \
                else \
                    STAM_COUNTER_INC(&pVCpu->iem.s.a_CoreStatName ## Skippable); \
            } else do { } while (0)
# else
        PCIEMLIVENESSENTRY const pLivenessEntry       = &pReNative->paLivenessEntries[pReNative->idxCurCall];
        IEMLIVENESSBIT const     LivenessClobbered    = { IEMLIVENESS_STATE_GET_WILL_BE_CLOBBERED_SET(pLivenessEntry) };
        IEMLIVENESSBIT const     LivenessDelayable    = { IEMLIVENESS_STATE_GET_CAN_BE_POSTPONED_SET(pLivenessEntry)  };
#  define CHECK_FLAG_AND_UPDATE_STATS(a_fEfl, a_fLivenessMember, a_CoreStatName) \
            if (fEflOutput & (a_fEfl)) \
            { \
                if (LivenessClobbered.a_fLivenessMember) \
                    STAM_COUNTER_INC(&pVCpu->iem.s.a_CoreStatName ## Skippable); \
                else if (LivenessDelayable.a_fLivenessMember) \
                    STAM_COUNTER_INC(&pVCpu->iem.s.a_CoreStatName ## Delayable); \
                else \
                    STAM_COUNTER_INC(&pVCpu->iem.s.a_CoreStatName ## Required); \
            } else do { } while (0)
# endif
        CHECK_FLAG_AND_UPDATE_STATS(X86_EFL_CF, fEflCf, StatNativeLivenessEflCf);
        CHECK_FLAG_AND_UPDATE_STATS(X86_EFL_PF, fEflPf, StatNativeLivenessEflPf);
        CHECK_FLAG_AND_UPDATE_STATS(X86_EFL_AF, fEflAf, StatNativeLivenessEflAf);
        CHECK_FLAG_AND_UPDATE_STATS(X86_EFL_ZF, fEflZf, StatNativeLivenessEflZf);
        CHECK_FLAG_AND_UPDATE_STATS(X86_EFL_SF, fEflSf, StatNativeLivenessEflSf);
        CHECK_FLAG_AND_UPDATE_STATS(X86_EFL_OF, fEflOf, StatNativeLivenessEflOf);
        //CHECK_FLAG_AND_UPDATE_STATS(~X86_EFL_STATUS_BITS, fEflOther, StatNativeLivenessEflOther);
# undef CHECK_FLAG_AND_UPDATE_STATS
    }
    RT_NOREF(fEflInput);
}
#endif /* VBOX_WITH_STATISTICS */

#undef  IEM_MC_FETCH_EFLAGS /* should not be used */
#define IEM_MC_FETCH_EFLAGS_EX(a_EFlags, a_fEflInput, a_fEflOutput) \
    off = iemNativeEmitFetchEFlags<a_fEflInput,  iemNativeEflagsToLivenessMask<a_fEflInput>(),\
                                   a_fEflOutput, iemNativeEflagsToLivenessMask<a_fEflOutput>()>(pReNative, off, a_EFlags)

/** Handles IEM_MC_FETCH_EFLAGS_EX. */
template<uint32_t const a_fEflInput,  uint64_t const a_fLivenessEflInput,
         uint32_t const a_fEflOutput, uint64_t const a_fLivenessEflOutput>
DECL_INLINE_THROW(uint32_t)
iemNativeEmitFetchEFlags(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarEFlags)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVarEFlags);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxVarEFlags, sizeof(uint32_t));
    /** @todo fix NOT AssertCompile(a_fEflInput != 0 || a_fEflOutput != 0); */

#ifdef IEMNATIVE_WITH_LIVENESS_ANALYSIS
# ifdef VBOX_STRICT
    if (   pReNative->idxCurCall != 0
        && (a_fEflInput != 0 || a_fEflOutput != 0) /* for NOT these are both zero for now. */)
    {
        PCIEMLIVENESSENTRY const    pLivenessEntry = &pReNative->paLivenessEntries[pReNative->idxCurCall - 1];
        RT_CONSTEXPR uint32_t const fBoth          = a_fEflInput | a_fEflOutput;
# define ASSERT_ONE_EFL(a_fElfConst, a_idxField) \
            AssertMsg(   !(fBoth & (a_fElfConst)) \
                      || (!(a_fEflInput & (a_fElfConst)) \
                          ? IEMLIVENESS_STATE_IS_CLOBBER_EXPECTED(iemNativeLivenessGetStateByGstRegEx(pLivenessEntry, a_idxField)) \
                          : !(a_fEflOutput & (a_fElfConst)) \
                          ? IEMLIVENESS_STATE_IS_INPUT_EXPECTED(  iemNativeLivenessGetStateByGstRegEx(pLivenessEntry, a_idxField)) \
                          : IEMLIVENESS_STATE_IS_MODIFY_EXPECTED( iemNativeLivenessGetStateByGstRegEx(pLivenessEntry, a_idxField)) ), \
                      ("%s - %u\n", #a_fElfConst, iemNativeLivenessGetStateByGstRegEx(pLivenessEntry, a_idxField)))
        ASSERT_ONE_EFL(~(uint32_t)X86_EFL_STATUS_BITS, IEMLIVENESSBIT_IDX_EFL_OTHER);
        ASSERT_ONE_EFL(X86_EFL_CF, IEMLIVENESSBIT_IDX_EFL_CF);
        ASSERT_ONE_EFL(X86_EFL_PF, IEMLIVENESSBIT_IDX_EFL_PF);
        ASSERT_ONE_EFL(X86_EFL_AF, IEMLIVENESSBIT_IDX_EFL_AF);
        ASSERT_ONE_EFL(X86_EFL_ZF, IEMLIVENESSBIT_IDX_EFL_ZF);
        ASSERT_ONE_EFL(X86_EFL_SF, IEMLIVENESSBIT_IDX_EFL_SF);
        ASSERT_ONE_EFL(X86_EFL_OF, IEMLIVENESSBIT_IDX_EFL_OF);
# undef ASSERT_ONE_EFL
    }
# endif
#endif

    IEMNATIVE_ASSERT_EFLAGS_SKIPPING_AND_POSTPONING(pReNative,  a_fEflInput);
    IEMNATIVE_STRICT_EFLAGS_SKIPPING_EMIT_CHECK(pReNative, off, a_fEflInput);

    /** @todo This could be prettier...*/
    /** @todo Also, the shadowing+liveness handling of EFlags is currently
     *        problematic, but I'll try tackle that soon (@bugref{10720}). */
    PCIEMNATIVEVAR const pVar = &pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVarEFlags)];
    Assert(pVar->enmKind == kIemNativeVarKind_Invalid || pVar->enmKind == kIemNativeVarKind_Stack);
    Assert(pVar->idxReg  == UINT8_MAX);
    if (pVar->uArgNo >= IEMNATIVE_CALL_ARG_GREG_COUNT)
    {
        /** @todo We could use kIemNativeGstRegUse_ReadOnly here when fOutput is
         *        zero, but since iemNativeVarRegisterSet clears the shadowing,
         *        that's counter productive... */
        uint8_t const idxGstReg = iemNativeRegAllocTmpForGuestEFlagsForUpdate(pReNative, &off,
                                                                              a_fLivenessEflInput, a_fLivenessEflOutput);
        iemNativeVarRegisterSet(pReNative, idxVarEFlags, idxGstReg, off, true /*fAllocated*/);
    }
    else
    {
        /* Register argument variable: Avoid assertions in generic call code and load it the traditional way. */
        uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxVarEFlags, &off, false /*fInitialized*/);
        uint8_t const idxGstReg = iemNativeRegAllocTmpForGuestEFlagsIfAlreadyPresent(pReNative, &off,
                                                                                     a_fLivenessEflInput, a_fLivenessEflOutput);
        if (idxGstReg != UINT8_MAX)
        {
            off = iemNativeEmitLoadGprFromGpr32(pReNative, off, idxVarReg, idxGstReg);
            iemNativeRegFreeTmp(pReNative, idxGstReg);
        }
        else
            off = iemNativeEmitLoadGprFromVCpuU32(pReNative, off, idxVarReg, RT_UOFFSETOF(VMCPUCC, cpum.GstCtx.eflags));
        iemNativeVarRegisterRelease(pReNative, idxVarEFlags);
    }
    return off;
}



/** @todo emit strict build assertions for IEM_MC_COMMIT_EFLAGS_EX when we
 * start using it with custom native code emission (inlining assembly
 * instruction helpers). */
#undef  IEM_MC_COMMIT_EFLAGS /* should not be used */
#define IEM_MC_COMMIT_EFLAGS_EX(a_EFlags, a_fEflInput, a_fEflOutput) \
    IEMNATIVE_EFLAGS_OPTIMIZATION_STATS(a_fEflInput, a_fEflOutput); \
    off = iemNativeEmitCommitEFlags<true /*a_fUpdateSkippingAndPostponing*/, a_fEflOutput, \
                                    iemNativeEflagsToLivenessMask<a_fEflInput>(), \
                                    iemNativeEflagsToLivenessMask<a_fEflOutput>()>(pReNative, off, a_EFlags, a_fEflInput)

#undef IEM_MC_COMMIT_EFLAGS_OPT /* should not be used */
#define IEM_MC_COMMIT_EFLAGS_OPT_EX(a_EFlags, a_fEflInput, a_fEflOutput) \
    IEMNATIVE_EFLAGS_OPTIMIZATION_STATS(a_fEflInput, a_fEflOutput); \
    off = iemNativeEmitCommitEFlags<false /*a_fUpdateSkippingAndPostponing*/, a_fEflOutput, \
                                    iemNativeEflagsToLivenessMask<a_fEflInput>(), \
                                    iemNativeEflagsToLivenessMask<a_fEflOutput>()>(pReNative, off, a_EFlags, a_fEflInput)

/** Handles IEM_MC_COMMIT_EFLAGS_EX. */
template<bool const a_fUpdateSkippingAndPostponing, uint32_t const a_fEflOutput,
         uint64_t const a_fLivenessEflInputBits, uint64_t const a_fLivenessEflOutputBits>
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCommitEFlags(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarEFlags, uint32_t fElfInput)
{
    uint8_t const idxReg = iemNativeVarRegisterAcquire(pReNative, idxVarEFlags, &off, true /*fInitialized*/);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxVarEFlags, sizeof(uint32_t));

#ifdef IEMNATIVE_WITH_LIVENESS_ANALYSIS
# ifdef VBOX_STRICT
    if (   pReNative->idxCurCall != 0
        && (a_fLivenessEflInputBits != 0 || a_fLivenessEflOutputBits != 0) /* for NOT these are both zero for now. */)
    {
        PCIEMLIVENESSENTRY const pLivenessEntry = &pReNative->paLivenessEntries[pReNative->idxCurCall - 1];
# define ASSERT_ONE_EFL(a_idxField) \
            if RT_CONSTEXPR_IF(((a_fLivenessEflInputBits | a_fLivenessEflOutputBits) & RT_BIT_64(a_idxField)) != 0) \
                AssertMsg(!(a_fLivenessEflInputBits & RT_BIT_64(a_idxField)) \
                          ? IEMLIVENESS_STATE_IS_CLOBBER_EXPECTED(iemNativeLivenessGetStateByGstRegEx(pLivenessEntry, a_idxField)) \
                          : !(a_fLivenessEflOutputBits & RT_BIT_64(a_idxField)) \
                          ? IEMLIVENESS_STATE_IS_INPUT_EXPECTED(  iemNativeLivenessGetStateByGstRegEx(pLivenessEntry, a_idxField)) \
                          : IEMLIVENESS_STATE_IS_MODIFY_EXPECTED( iemNativeLivenessGetStateByGstRegEx(pLivenessEntry, a_idxField)), \
                          ("%s - %u\n", #a_idxField, iemNativeLivenessGetStateByGstRegEx(pLivenessEntry, a_idxField)))
        ASSERT_ONE_EFL(IEMLIVENESSBIT_IDX_EFL_OTHER);
        ASSERT_ONE_EFL(IEMLIVENESSBIT_IDX_EFL_CF);
        ASSERT_ONE_EFL(IEMLIVENESSBIT_IDX_EFL_PF);
        ASSERT_ONE_EFL(IEMLIVENESSBIT_IDX_EFL_AF);
        ASSERT_ONE_EFL(IEMLIVENESSBIT_IDX_EFL_ZF);
        ASSERT_ONE_EFL(IEMLIVENESSBIT_IDX_EFL_SF);
        ASSERT_ONE_EFL(IEMLIVENESSBIT_IDX_EFL_OF);
# undef ASSERT_ONE_EFL
    }
# endif
#endif

#ifdef VBOX_STRICT
    off = iemNativeEmitTestAnyBitsInGpr(pReNative, off, idxReg, X86_EFL_RA1_MASK);
    uint32_t offFixup = off;
    off = iemNativeEmitJnzToFixed(pReNative, off, off);
    off = iemNativeEmitBrk(pReNative, off, UINT32_C(0x2001));
    iemNativeFixupFixedJump(pReNative, offFixup, off);

    off = iemNativeEmitTestAnyBitsInGpr(pReNative, off, idxReg, X86_EFL_RAZ_MASK & CPUMX86EFLAGS_HW_MASK_32);
    offFixup = off;
    off = iemNativeEmitJzToFixed(pReNative, off, off);
    off = iemNativeEmitBrk(pReNative, off, UINT32_C(0x2002));
    iemNativeFixupFixedJump(pReNative, offFixup, off);

    /** @todo validate that only bits in the a_fEflOutput mask changed. */
#endif

#ifdef IEMNATIVE_WITH_EFLAGS_SKIPPING
    if RT_CONSTEXPR_IF(a_fUpdateSkippingAndPostponing)
    {
        Assert(!(pReNative->fSkippingEFlags & fElfInput)); RT_NOREF(fElfInput);
        if (pReNative->fSkippingEFlags)
            Log5(("EFLAGS: fSkippingEFlags %#x -> %#x (iemNativeEmitCommitEFlags)\n",
                  pReNative->fSkippingEFlags, pReNative->fSkippingEFlags & ~(a_fEflOutput & X86_EFL_STATUS_BITS) ));
        if RT_CONSTEXPR_IF((a_fEflOutput & X86_EFL_STATUS_BITS) == X86_EFL_STATUS_BITS)
            pReNative->fSkippingEFlags  = 0;
        else
            pReNative->fSkippingEFlags &= ~(a_fEflOutput & X86_EFL_STATUS_BITS);
# ifdef IEMNATIVE_STRICT_EFLAGS_SKIPPING
        if RT_CONSTEXPR_IF((a_fEflOutput & X86_EFL_STATUS_BITS) == X86_EFL_STATUS_BITS)
            off = iemNativeEmitStoreImmToVCpuU32(pReNative, off, 0, RT_UOFFSETOF(VMCPU, iem.s.fSkippingEFlags));
        else
            off = iemNativeEmitAndImmIntoVCpuU32(pReNative, off, ~(a_fEflOutput & X86_EFL_STATUS_BITS),
                                                 RT_UOFFSETOF(VMCPU, iem.s.fSkippingEFlags));
# endif
        IEMNATIVE_CLEAR_POSTPONED_EFLAGS(pReNative, a_fEflOutput);
    }
#endif

    iemNativeRegClearAndMarkAsGstRegShadow(pReNative, idxReg, kIemNativeGstReg_EFlags, off);
    off = iemNativeEmitStoreGprToVCpuU32(pReNative, off, idxReg, RT_UOFFSETOF_DYN(VMCPUCC, cpum.GstCtx.eflags));
    iemNativeVarRegisterRelease(pReNative, idxVarEFlags);
    return off;
}


typedef enum IEMNATIVEMITEFLOP
{
    kIemNativeEmitEflOp_Set,
    kIemNativeEmitEflOp_Clear,
    kIemNativeEmitEflOp_Flip
} IEMNATIVEMITEFLOP;

#define IEM_MC_SET_EFL_BIT(a_fBit) \
    off = iemNativeEmitModifyEFlagsBit<kIemNativeEmitEflOp_Set,   a_fBit, iemNativeEflagsToLivenessMask<a_fBit>()>(pReNative, off)

#define IEM_MC_CLEAR_EFL_BIT(a_fBit) \
    off = iemNativeEmitModifyEFlagsBit<kIemNativeEmitEflOp_Clear, a_fBit, iemNativeEflagsToLivenessMask<a_fBit>()>(pReNative, off)

#define IEM_MC_FLIP_EFL_BIT(a_fBit) \
    off = iemNativeEmitModifyEFlagsBit<kIemNativeEmitEflOp_Flip,  a_fBit, iemNativeEflagsToLivenessMask<a_fBit>()>(pReNative, off)

/** Handles IEM_MC_SET_EFL_BIT/IEM_MC_CLEAR_EFL_BIT/IEM_MC_FLIP_EFL_BIT. */
template<IEMNATIVEMITEFLOP const a_enmOp, uint32_t const a_fEflBit, uint64_t const a_fLivenessEflBit>
DECL_INLINE_THROW(uint32_t) iemNativeEmitModifyEFlagsBit(PIEMRECOMPILERSTATE pReNative, uint32_t off)
{
    uint8_t const idxEflReg = iemNativeRegAllocTmpForGuestEFlagsForUpdate(pReNative, &off,
                                                                            a_enmOp == kIemNativeEmitEflOp_Flip
                                                                          ? a_fLivenessEflBit : 0,
                                                                          a_fLivenessEflBit);

    /* Using 'if constexpr' forces code elimination in debug builds with VC. */
    if RT_CONSTEXPR_IF(a_enmOp == kIemNativeEmitEflOp_Set)
        off = iemNativeEmitOrGpr32ByImm(pReNative, off, idxEflReg, a_fEflBit);
    else if RT_CONSTEXPR_IF(a_enmOp == kIemNativeEmitEflOp_Clear)
        off = iemNativeEmitAndGpr32ByImm(pReNative, off, idxEflReg, ~a_fEflBit);
    else if RT_CONSTEXPR_IF(a_enmOp == kIemNativeEmitEflOp_Flip)
        off = iemNativeEmitXorGpr32ByImm(pReNative, off, idxEflReg, a_fEflBit);
    else
        AssertCompile(   a_enmOp == kIemNativeEmitEflOp_Set /* AssertCompile(false) works with VC 2019 but not clang 15. */
                      || a_enmOp == kIemNativeEmitEflOp_Clear
                      || a_enmOp == kIemNativeEmitEflOp_Flip);

    /** @todo No delayed writeback for EFLAGS right now. */
    off = iemNativeEmitStoreGprToVCpuU32(pReNative, off, idxEflReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.eflags));

    /* Free but don't flush the EFLAGS register. */
    iemNativeRegFreeTmp(pReNative, idxEflReg);

#ifdef IEMNATIVE_WITH_EFLAGS_SKIPPING
    /* Clear the bit in the skipped mask if we're clobbering and it's a status bit. */
    if RT_CONSTEXPR_IF(   (a_enmOp == kIemNativeEmitEflOp_Set || a_enmOp == kIemNativeEmitEflOp_Clear)
                       && (a_fEflBit & X86_EFL_STATUS_BITS))
    {
        if (pReNative->fSkippingEFlags)
            Log5(("EFLAGS: fSkippingEFlags %#x -> %#x (iemNativeEmitModifyEFlagsBit)\n",
                  pReNative->fSkippingEFlags, pReNative->fSkippingEFlags & ~a_fEflBit ));
        pReNative->fSkippingEFlags &= ~a_fEflBit;
# ifdef IEMNATIVE_STRICT_EFLAGS_SKIPPING
        off = iemNativeEmitAndImmIntoVCpuU32(pReNative, off, ~a_fEflBit, RT_UOFFSETOF(VMCPU, iem.s.fSkippingEFlags));
# endif
    }
#endif

    return off;
}


/*********************************************************************************************************************************
*   Emitters for segment register fetches (IEM_MC_FETCH_SREG_XXX).
*********************************************************************************************************************************/

#define IEM_MC_FETCH_SREG_U16(a_u16Dst, a_iSReg) \
    off = iemNativeEmitFetchSReg(pReNative, off, a_u16Dst, a_iSReg, sizeof(uint16_t))

#define IEM_MC_FETCH_SREG_ZX_U32(a_u32Dst, a_iSReg) \
    off = iemNativeEmitFetchSReg(pReNative, off, a_u32Dst, a_iSReg, sizeof(uint32_t))

#define IEM_MC_FETCH_SREG_ZX_U64(a_u64Dst, a_iSReg) \
    off = iemNativeEmitFetchSReg(pReNative, off, a_u64Dst, a_iSReg, sizeof(uint64_t))


/** Emits code for IEM_MC_FETCH_SREG_U16, IEM_MC_FETCH_SREG_ZX_U32 and
 *  IEM_MC_FETCH_SREG_ZX_U64. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitFetchSReg(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxDstVar, uint8_t iSReg, int8_t cbVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxDstVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxDstVar, cbVar); RT_NOREF(cbVar);
    Assert(iSReg < X86_SREG_COUNT);

    /*
     * For now, we will not create a shadow copy of a selector.  The rational
     * is that since we do not recompile the popping and loading of segment
     * registers and that the the IEM_MC_FETCH_SREG_U* MCs are only used for
     * pushing and moving to registers, there is only a small chance that the
     * shadow copy will be accessed again before the register is reloaded.  One
     * scenario would be nested called in 16-bit code, but I doubt it's worth
     * the extra register pressure atm.
     *
     * What we really need first, though, is to combine iemNativeRegAllocTmpForGuestReg
     * and iemNativeVarRegisterAcquire for a load scenario. We only got the
     * store scencario covered at present (r160730).
     */
    iemNativeVarSetKindToStack(pReNative, idxDstVar);
    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxDstVar, &off);
    off = iemNativeEmitLoadGprFromVCpuU16(pReNative, off, idxVarReg, RT_UOFFSETOF_DYN(VMCPU, cpum.GstCtx.aSRegs[iSReg].Sel));
    iemNativeVarRegisterRelease(pReNative, idxDstVar);
    return off;
}



/*********************************************************************************************************************************
*   Register references.                                                                                                         *
*********************************************************************************************************************************/

#define IEM_MC_REF_GREG_U8_THREADED(a_pu8Dst, a_iGRegEx) \
    off = iemNativeEmitRefGregU8(pReNative, off, a_pu8Dst, a_iGRegEx, false /*fConst*/)

#define IEM_MC_REF_GREG_U8_CONST_THREADED(a_pu8Dst, a_iGRegEx) \
    off = iemNativeEmitRefGregU8(pReNative, off, a_pu8Dst, a_iGRegEx, true /*fConst*/)

/** Handles IEM_MC_REF_GREG_U8[_CONST]. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitRefGregU8(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarRef, uint8_t iGRegEx, bool fConst)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVarRef);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxVarRef, sizeof(void *));
    Assert(iGRegEx < 20);

    if (iGRegEx < 16)
        iemNativeVarSetKindToGstRegRef(pReNative, idxVarRef, kIemNativeGstRegRef_Gpr, iGRegEx & 15);
    else
        iemNativeVarSetKindToGstRegRef(pReNative, idxVarRef, kIemNativeGstRegRef_GprHighByte, iGRegEx & 15);

    /* If we've delayed writing back the register value, flush it now. */
    off = iemNativeRegFlushPendingSpecificWrite(pReNative, off, kIemNativeGstRegRef_Gpr, iGRegEx & 15);

    /* If it's not a const reference we need to flush the shadow copy of the register now. */
    if (!fConst)
        iemNativeRegFlushGuestShadows(pReNative, RT_BIT_64(IEMNATIVEGSTREG_GPR(iGRegEx & 15)));

    return off;
}

#define IEM_MC_REF_GREG_U16(a_pu16Dst, a_iGReg) \
    off = iemNativeEmitRefGregUxx(pReNative, off, a_pu16Dst, a_iGReg, false /*fConst*/)

#define IEM_MC_REF_GREG_U16_CONST(a_pu16Dst, a_iGReg) \
    off = iemNativeEmitRefGregUxx(pReNative, off, a_pu16Dst, a_iGReg, true /*fConst*/)

#define IEM_MC_REF_GREG_U32(a_pu32Dst, a_iGReg) \
    off = iemNativeEmitRefGregUxx(pReNative, off, a_pu32Dst, a_iGReg, false /*fConst*/)

#define IEM_MC_REF_GREG_U32_CONST(a_pu32Dst, a_iGReg) \
    off = iemNativeEmitRefGregUxx(pReNative, off, a_pu32Dst, a_iGReg, true /*fConst*/)

#define IEM_MC_REF_GREG_I32(a_pi32Dst, a_iGReg) \
    off = iemNativeEmitRefGregUxx(pReNative, off, a_pi32Dst, a_iGReg, false /*fConst*/)

#define IEM_MC_REF_GREG_I32_CONST(a_pi32Dst, a_iGReg) \
    off = iemNativeEmitRefGregUxx(pReNative, off, a_pi32Dst, a_iGReg, true /*fConst*/)

#define IEM_MC_REF_GREG_U64(a_pu64Dst, a_iGReg) \
    off = iemNativeEmitRefGregUxx(pReNative, off, a_pu64Dst, a_iGReg, false /*fConst*/)

#define IEM_MC_REF_GREG_U64_CONST(a_pu64Dst, a_iGReg) \
    off = iemNativeEmitRefGregUxx(pReNative, off, a_pu64Dst, a_iGReg, true /*fConst*/)

#define IEM_MC_REF_GREG_I64(a_pi64Dst, a_iGReg) \
    off = iemNativeEmitRefGregUxx(pReNative, off, a_pi64Dst, a_iGReg, false /*fConst*/)

#define IEM_MC_REF_GREG_I64_CONST(a_pi64Dst, a_iGReg) \
    off = iemNativeEmitRefGregUxx(pReNative, off, a_pi64Dst, a_iGReg, true /*fConst*/)

/** Handles IEM_MC_REF_GREG_Uxx[_CONST] and IEM_MC_REF_GREG_Ixx[_CONST]. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitRefGregUxx(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarRef, uint8_t iGReg, bool fConst)
{
    Assert(iGReg < 16);
    iemNativeVarSetKindToGstRegRef(pReNative, idxVarRef, kIemNativeGstRegRef_Gpr, iGReg);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxVarRef, sizeof(void *));

    /* If we've delayed writing back the register value, flush it now. */
    off = iemNativeRegFlushPendingSpecificWrite(pReNative, off, kIemNativeGstRegRef_Gpr, iGReg);

    /* If it's not a const reference we need to flush the shadow copy of the register now. */
    if (!fConst)
        iemNativeRegFlushGuestShadows(pReNative, RT_BIT_64(IEMNATIVEGSTREG_GPR(iGReg)));

    return off;
}


#undef  IEM_MC_REF_EFLAGS /* should not be used. */
#define IEM_MC_REF_EFLAGS_EX(a_pEFlags, a_fEflInput, a_fEflOutput) \
    IEMNATIVE_EFLAGS_OPTIMIZATION_STATS(a_fEflInput, a_fEflOutput); \
    off = iemNativeEmitRefEFlags<a_fEflOutput>(pReNative, off, a_pEFlags, a_fEflInput)

/** Handles IEM_MC_REF_EFLAGS. */
template<uint32_t const a_fEflOutput>
DECL_INLINE_THROW(uint32_t)
iemNativeEmitRefEFlags(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarRef, uint32_t fEflInput)
{
    iemNativeVarSetKindToGstRegRef(pReNative, idxVarRef, kIemNativeGstRegRef_EFlags, 0);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxVarRef, sizeof(void *));

#ifdef IEMNATIVE_WITH_EFLAGS_SKIPPING
    IEMNATIVE_ASSERT_EFLAGS_SKIPPING_AND_POSTPONING(pReNative,  fEflInput);
    IEMNATIVE_STRICT_EFLAGS_SKIPPING_EMIT_CHECK(pReNative, off, fEflInput);
    if (pReNative->fSkippingEFlags)
        Log5(("EFLAGS: fSkippingEFlags %#x -> %#x (iemNativeEmitRefEFlags)\n",
              pReNative->fSkippingEFlags, pReNative->fSkippingEFlags & ~a_fEflOutput ));
    pReNative->fSkippingEFlags &= ~a_fEflOutput;
# ifdef IEMNATIVE_STRICT_EFLAGS_SKIPPING

    /* Updating the skipping according to the outputs is a little early, but
       we don't have any other hooks for references atm. */
    if RT_CONSTEXPR_IF((a_fEflOutput & X86_EFL_STATUS_BITS) == X86_EFL_STATUS_BITS)
        off = iemNativeEmitStoreImmToVCpuU32(pReNative, off, 0, RT_UOFFSETOF(VMCPU, iem.s.fSkippingEFlags));
    else if RT_CONSTEXPR_IF((a_fEflOutput & X86_EFL_STATUS_BITS) != 0)
        off = iemNativeEmitAndImmIntoVCpuU32(pReNative, off, ~(a_fEflOutput & X86_EFL_STATUS_BITS),
                                             RT_UOFFSETOF(VMCPU, iem.s.fSkippingEFlags));
# endif

    /* This ASSUMES that EFLAGS references are not taken before use. */
    IEMNATIVE_CLEAR_POSTPONED_EFLAGS(pReNative, a_fEflOutput);

#endif
    RT_NOREF(fEflInput);

    /* If we've delayed writing back the register value, flush it now. */
    off = iemNativeRegFlushPendingSpecificWrite(pReNative, off, kIemNativeGstRegRef_EFlags, 0);

    /* If there is a shadow copy of guest EFLAGS, flush it now. */
    iemNativeRegFlushGuestShadows(pReNative, RT_BIT_64(kIemNativeGstReg_EFlags));

    return off;
}


/** @todo Emit code for IEM_MC_ASSERT_EFLAGS in strict builds?  Once we emit
 * different code from threaded recompiler, maybe it would be helpful. For now
 * we assume the threaded recompiler catches any incorrect EFLAGS delcarations. */
#define IEM_MC_ASSERT_EFLAGS(a_fEflInput, a_fEflOutput) ((void)0)


#define IEM_MC_REF_XREG_U128(a_pu128Dst, a_iXReg) \
    off = iemNativeEmitRefXregXxx(pReNative, off, a_pu128Dst, a_iXReg, false /*fConst*/)

#define IEM_MC_REF_XREG_XMM(a_puXmmDst, a_iXReg) \
    off = iemNativeEmitRefXregXxx(pReNative, off, a_puXmmDst, a_iXReg, false /*fConst*/)

#define IEM_MC_REF_XREG_U128_CONST(a_pu128Dst, a_iXReg) \
    off = iemNativeEmitRefXregXxx(pReNative, off, a_pu128Dst, a_iXReg, true /*fConst*/)

#define IEM_MC_REF_XREG_XMM_CONST(a_pXmmDst, a_iXReg) \
    off = iemNativeEmitRefXregXxx(pReNative, off, a_pXmmDst, a_iXReg, true /*fConst*/)

#ifdef IEMNATIVE_WITH_SIMD_REG_ALLOCATOR
/* Just being paranoid here. */
# ifndef _MSC_VER /* MSC can't compile this, doesn't like [0]. Added reduced version afterwards. */
AssertCompile2MemberOffsets(CPUMCTX, XState.x87.aXMM[0], XState.x87.aXMM[0].au64[0]);
AssertCompile2MemberOffsets(CPUMCTX, XState.x87.aXMM[0], XState.x87.aXMM[0].au32[0]);
AssertCompile2MemberOffsets(CPUMCTX, XState.x87.aXMM[0], XState.x87.aXMM[0].ar64[0]);
AssertCompile2MemberOffsets(CPUMCTX, XState.x87.aXMM[0], XState.x87.aXMM[0].ar32[0]);
# endif
AssertCompileMemberOffset(X86XMMREG, au64, 0);
AssertCompileMemberOffset(X86XMMREG, au32, 0);
AssertCompileMemberOffset(X86XMMREG, ar64, 0);
AssertCompileMemberOffset(X86XMMREG, ar32, 0);

# define IEM_MC_REF_XREG_U32_CONST(a_pu32Dst, a_iXReg) \
    off = iemNativeEmitRefXregXxx(pReNative, off, a_pu32Dst, a_iXReg, true /*fConst*/)
# define IEM_MC_REF_XREG_U64_CONST(a_pu64Dst, a_iXReg) \
    off = iemNativeEmitRefXregXxx(pReNative, off, a_pu64Dst, a_iXReg, true /*fConst*/)
# define IEM_MC_REF_XREG_R32_CONST(a_pr32Dst, a_iXReg) \
    off = iemNativeEmitRefXregXxx(pReNative, off, a_pr32Dst, a_iXReg, true /*fConst*/)
# define IEM_MC_REF_XREG_R64_CONST(a_pr64Dst, a_iXReg) \
    off = iemNativeEmitRefXregXxx(pReNative, off, a_pr64Dst, a_iXReg, true /*fConst*/)
#endif

/** Handles IEM_MC_REF_XREG_xxx[_CONST]. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitRefXregXxx(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarRef, uint8_t iXReg, bool fConst)
{
    Assert(iXReg < 16);
    iemNativeVarSetKindToGstRegRef(pReNative, idxVarRef, kIemNativeGstRegRef_XReg, iXReg);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxVarRef, sizeof(void *));

    /* If we've delayed writing back the register value, flush it now. */
    off = iemNativeRegFlushPendingSpecificWrite(pReNative, off, kIemNativeGstRegRef_XReg, iXReg);

#ifdef IEMNATIVE_WITH_SIMD_REG_ALLOCATOR
    /* If it's not a const reference we need to flush the shadow copy of the register now. */
    if (!fConst)
        iemNativeSimdRegFlushGuestShadows(pReNative, RT_BIT_64(IEMNATIVEGSTSIMDREG_SIMD(iXReg)));
#else
    RT_NOREF(fConst);
#endif

    return off;
}



/*********************************************************************************************************************************
*   Effective Address Calculation                                                                                                *
*********************************************************************************************************************************/
#define IEM_MC_CALC_RM_EFF_ADDR_THREADED_16(a_GCPtrEff, a_bRm, a_u16Disp) \
    off = iemNativeEmitCalcRmEffAddrThreadedAddr16(pReNative, off, a_bRm, a_u16Disp, a_GCPtrEff)

/** Emit code for IEM_MC_CALC_RM_EFF_ADDR_THREADED_16.
 * @sa iemOpHlpCalcRmEffAddrThreadedAddr16  */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCalcRmEffAddrThreadedAddr16(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                         uint8_t bRm, uint16_t u16Disp, uint8_t idxVarRet)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVarRet);

    /*
     * Handle the disp16 form with no registers first.
     *
     * Convert to an immediate value, as that'll delay the register allocation
     * and assignment till the memory access / call / whatever and we can use
     * a more appropriate register (or none at all).
     */
    if ((bRm & (X86_MODRM_MOD_MASK | X86_MODRM_RM_MASK)) == 6)
    {
        iemNativeVarSetKindToConst(pReNative, idxVarRet, u16Disp);
        return off;
    }

    /* Determin the displacment. */
    uint16_t u16EffAddr;
    switch ((bRm >> X86_MODRM_MOD_SHIFT) & X86_MODRM_MOD_SMASK)
    {
        case 0:  u16EffAddr = 0;                        break;
        case 1:  u16EffAddr = (int16_t)(int8_t)u16Disp; break;
        case 2:  u16EffAddr = u16Disp;                  break;
        default: AssertFailedStmt(u16EffAddr = 0);
    }

    /* Determine the registers involved. */
    uint8_t idxGstRegBase;
    uint8_t idxGstRegIndex;
    switch (bRm & X86_MODRM_RM_MASK)
    {
        case 0:
            idxGstRegBase  = X86_GREG_xBX;
            idxGstRegIndex = X86_GREG_xSI;
            break;
        case 1:
            idxGstRegBase  = X86_GREG_xBX;
            idxGstRegIndex = X86_GREG_xDI;
            break;
        case 2:
            idxGstRegBase  = X86_GREG_xBP;
            idxGstRegIndex = X86_GREG_xSI;
            break;
        case 3:
            idxGstRegBase  = X86_GREG_xBP;
            idxGstRegIndex = X86_GREG_xDI;
            break;
        case 4:
            idxGstRegBase  = X86_GREG_xSI;
            idxGstRegIndex = UINT8_MAX;
            break;
        case 5:
            idxGstRegBase  = X86_GREG_xDI;
            idxGstRegIndex = UINT8_MAX;
            break;
        case 6:
            idxGstRegBase  = X86_GREG_xBP;
            idxGstRegIndex = UINT8_MAX;
            break;
#ifdef _MSC_VER  /* lazy compiler, thinks idxGstRegBase and idxGstRegIndex may otherwise be used uninitialized. */
        default:
#endif
        case 7:
            idxGstRegBase  = X86_GREG_xBX;
            idxGstRegIndex = UINT8_MAX;
            break;
    }

    /*
     * Now emit code that calculates: idxRegRet = (uint16_t)(u16EffAddr + idxGstRegBase [+ idxGstRegIndex])
     */
    uint8_t const idxRegRet   = iemNativeVarRegisterAcquire(pReNative, idxVarRet, &off);
    uint8_t const idxRegBase  = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(idxGstRegBase),
                                                               kIemNativeGstRegUse_ReadOnly);
    uint8_t const idxRegIndex = idxGstRegIndex != UINT8_MAX
                              ? iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(idxGstRegIndex),
                                                               kIemNativeGstRegUse_ReadOnly)
                              : UINT8_MAX;
#ifdef RT_ARCH_AMD64
    if (idxRegIndex == UINT8_MAX)
    {
        if (u16EffAddr == 0)
        {
            /* movxz ret, base */
            off = iemNativeEmitLoadGprFromGpr16(pReNative, off, idxRegRet, idxRegBase);
        }
        else
        {
            /* lea ret32, [base64 + disp32] */
            Assert(idxRegBase != X86_GREG_xSP /*SIB*/);
            uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 8);
            if (idxRegRet >= 8 || idxRegBase >= 8)
                pbCodeBuf[off++] = (idxRegRet >= 8 ? X86_OP_REX_R : 0) | (idxRegBase >= 8 ? X86_OP_REX_B : 0);
            pbCodeBuf[off++] = 0x8d;
            if (idxRegBase != X86_GREG_x12 /*SIB*/)
                pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_MEM4, idxRegRet & 7, idxRegBase & 7);
            else
            {
                pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_MEM4, idxRegRet & 7, 4 /*SIB*/);
                pbCodeBuf[off++] = X86_SIB_MAKE(X86_GREG_x12 & 7, 4 /*no index*/, 0);
            }
            pbCodeBuf[off++] = RT_BYTE1(u16EffAddr);
            pbCodeBuf[off++] = RT_BYTE2(u16EffAddr);
            pbCodeBuf[off++] = 0;
            pbCodeBuf[off++] = 0;
            IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

            off = iemNativeEmitClear16UpGpr(pReNative, off, idxRegRet);
        }
    }
    else
    {
        /* lea ret32, [index64 + base64 (+ disp32)] */
        Assert(idxRegIndex != X86_GREG_xSP /*no-index*/);
        uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 8);
        if (idxRegRet >= 8 || idxRegBase >= 8 || idxRegIndex >= 8)
            pbCodeBuf[off++] = (idxRegRet   >= 8 ? X86_OP_REX_R : 0)
                             | (idxRegBase  >= 8 ? X86_OP_REX_B : 0)
                             | (idxRegIndex >= 8 ? X86_OP_REX_X : 0);
        pbCodeBuf[off++] = 0x8d;
        uint8_t const bMod = u16EffAddr == 0 && (idxRegBase & 7) != X86_GREG_xBP ? X86_MOD_MEM0 : X86_MOD_MEM4;
        pbCodeBuf[off++] = X86_MODRM_MAKE(bMod, idxRegRet & 7, 4 /*SIB*/);
        pbCodeBuf[off++] = X86_SIB_MAKE(idxRegBase & 7, idxRegIndex & 7, 0);
        if (bMod == X86_MOD_MEM4)
        {
            pbCodeBuf[off++] = RT_BYTE1(u16EffAddr);
            pbCodeBuf[off++] = RT_BYTE2(u16EffAddr);
            pbCodeBuf[off++] = 0;
            pbCodeBuf[off++] = 0;
        }
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
        off = iemNativeEmitClear16UpGpr(pReNative, off, idxRegRet);
    }

#elif defined(RT_ARCH_ARM64)
    uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 5);
    if (u16EffAddr == 0)
    {
        if (idxRegIndex == UINT8_MAX)
            pu32CodeBuf[off++] = Armv8A64MkInstrUxth(idxRegRet, idxRegBase);
        else
        {
            pu32CodeBuf[off++] = Armv8A64MkInstrAddSubReg(false /*fSub*/, idxRegRet, idxRegBase, idxRegIndex, false /*f64Bit*/);
            pu32CodeBuf[off++] = Armv8A64MkInstrUxth(idxRegRet, idxRegRet);
        }
    }
    else
    {
        if ((int16_t)u16EffAddr < 4096 && (int16_t)u16EffAddr >= 0)
            pu32CodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(false /*fSub*/, idxRegRet, idxRegBase, u16EffAddr, false /*f64Bit*/);
        else if ((int16_t)u16EffAddr > -4096 && (int16_t)u16EffAddr < 0)
            pu32CodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(true /*fSub*/, idxRegRet, idxRegBase,
                                                             (uint16_t)-(int16_t)u16EffAddr, false /*f64Bit*/);
        else
        {
            pu32CodeBuf[off++] = Armv8A64MkInstrMovZ(idxRegRet, u16EffAddr);
            pu32CodeBuf[off++] = Armv8A64MkInstrAddSubReg(false /*fSub*/, idxRegRet, idxRegRet, idxRegBase, false /*f64Bit*/);
        }
        if (idxRegIndex != UINT8_MAX)
            pu32CodeBuf[off++] = Armv8A64MkInstrAddSubReg(false /*fSub*/, idxRegRet, idxRegRet, idxRegIndex, false /*f64Bit*/);
        pu32CodeBuf[off++] = Armv8A64MkInstrUxth(idxRegRet, idxRegRet);
    }

#else
# error "port me"
#endif

    if (idxRegIndex != UINT8_MAX)
        iemNativeRegFreeTmp(pReNative, idxRegIndex);
    iemNativeRegFreeTmp(pReNative, idxRegBase);
    iemNativeVarRegisterRelease(pReNative, idxVarRet);
    return off;
}


#define IEM_MC_CALC_RM_EFF_ADDR_THREADED_32(a_GCPtrEff, a_bRm, a_uSibAndRspOffset, a_u32Disp) \
    off = iemNativeEmitCalcRmEffAddrThreadedAddr32(pReNative, off, a_bRm, a_uSibAndRspOffset, a_u32Disp, a_GCPtrEff)

/** Emit code for IEM_MC_CALC_RM_EFF_ADDR_THREADED_32.
 * @see iemOpHlpCalcRmEffAddrThreadedAddr32  */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCalcRmEffAddrThreadedAddr32(PIEMRECOMPILERSTATE pReNative, uint32_t off,
                                         uint8_t bRm, uint32_t uSibAndRspOffset, uint32_t u32Disp, uint8_t idxVarRet)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVarRet);

    /*
     * Handle the disp32 form with no registers first.
     *
     * Convert to an immediate value, as that'll delay the register allocation
     * and assignment till the memory access / call / whatever and we can use
     * a more appropriate register (or none at all).
     */
    if ((bRm & (X86_MODRM_MOD_MASK | X86_MODRM_RM_MASK)) == 5)
    {
        iemNativeVarSetKindToConst(pReNative, idxVarRet, u32Disp);
        return off;
    }

    /* Calculate the fixed displacement (more down in SIB.B=4 and SIB.B=5 on this). */
    uint32_t u32EffAddr = 0;
    switch ((bRm >> X86_MODRM_MOD_SHIFT) & X86_MODRM_MOD_SMASK)
    {
        case 0: break;
        case 1: u32EffAddr = (int8_t)u32Disp; break;
        case 2: u32EffAddr = u32Disp; break;
        default: AssertFailed();
    }

    /* Get the register (or SIB) value. */
    uint8_t idxGstRegBase  = UINT8_MAX;
    uint8_t idxGstRegIndex = UINT8_MAX;
    uint8_t cShiftIndex    = 0;
    switch (bRm & X86_MODRM_RM_MASK)
    {
        case 0: idxGstRegBase = X86_GREG_xAX; break;
        case 1: idxGstRegBase = X86_GREG_xCX; break;
        case 2: idxGstRegBase = X86_GREG_xDX; break;
        case 3: idxGstRegBase = X86_GREG_xBX; break;
        case 4: /* SIB */
        {
            /* index /w scaling . */
            cShiftIndex = (uSibAndRspOffset >> X86_SIB_SCALE_SHIFT) & X86_SIB_SCALE_SMASK;
            switch ((uSibAndRspOffset >> X86_SIB_INDEX_SHIFT) & X86_SIB_INDEX_SMASK)
            {
                case 0: idxGstRegIndex = X86_GREG_xAX; break;
                case 1: idxGstRegIndex = X86_GREG_xCX; break;
                case 2: idxGstRegIndex = X86_GREG_xDX; break;
                case 3: idxGstRegIndex = X86_GREG_xBX; break;
                case 4: cShiftIndex    = 0; /*no index*/ break;
                case 5: idxGstRegIndex = X86_GREG_xBP; break;
                case 6: idxGstRegIndex = X86_GREG_xSI; break;
                case 7: idxGstRegIndex = X86_GREG_xDI; break;
            }

            /* base */
            switch (uSibAndRspOffset & X86_SIB_BASE_MASK)
            {
                case 0: idxGstRegBase = X86_GREG_xAX; break;
                case 1: idxGstRegBase = X86_GREG_xCX; break;
                case 2: idxGstRegBase = X86_GREG_xDX; break;
                case 3: idxGstRegBase = X86_GREG_xBX; break;
                case 4:
                    idxGstRegBase     = X86_GREG_xSP;
                    u32EffAddr       += uSibAndRspOffset >> 8;
                    break;
                case 5:
                    if ((bRm & X86_MODRM_MOD_MASK) != 0)
                        idxGstRegBase = X86_GREG_xBP;
                    else
                    {
                        Assert(u32EffAddr == 0);
                        u32EffAddr    = u32Disp;
                    }
                    break;
                case 6: idxGstRegBase = X86_GREG_xSI; break;
                case 7: idxGstRegBase = X86_GREG_xDI; break;
            }
            break;
        }
        case 5: idxGstRegBase = X86_GREG_xBP; break;
        case 6: idxGstRegBase = X86_GREG_xSI; break;
        case 7: idxGstRegBase = X86_GREG_xDI; break;
    }

    /*
     * If no registers are involved (SIB.B=5, SIB.X=4) repeat what we did at
     * the start of the function.
     */
    if (idxGstRegBase == UINT8_MAX && idxGstRegIndex == UINT8_MAX)
    {
        iemNativeVarSetKindToConst(pReNative, idxVarRet, u32EffAddr);
        return off;
    }

    /*
     * Now emit code that calculates: idxRegRet = (uint32_t)(u32EffAddr [+ idxGstRegBase] [+ (idxGstRegIndex << cShiftIndex)])
     */
    uint8_t const idxRegRet   = iemNativeVarRegisterAcquire(pReNative, idxVarRet, &off);
    uint8_t       idxRegBase  = idxGstRegBase == UINT8_MAX ? UINT8_MAX
                              : iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(idxGstRegBase),
                                                                kIemNativeGstRegUse_ReadOnly);
    uint8_t       idxRegIndex = idxGstRegIndex == UINT8_MAX ? UINT8_MAX
                              : iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(idxGstRegIndex),
                                                               kIemNativeGstRegUse_ReadOnly);

    /* If base is not given and there is no shifting, swap the registers to avoid code duplication. */
    if (idxRegBase == UINT8_MAX && cShiftIndex == 0)
    {
        idxRegBase  = idxRegIndex;
        idxRegIndex = UINT8_MAX;
    }

#ifdef RT_ARCH_AMD64
    if (idxRegIndex == UINT8_MAX)
    {
        if (u32EffAddr == 0)
        {
            /* mov ret, base */
            off = iemNativeEmitLoadGprFromGpr32(pReNative, off, idxRegRet, idxRegBase);
        }
        else
        {
            /* lea ret32, [base64 + disp32] */
            Assert(idxRegBase != X86_GREG_xSP /*SIB*/);
            uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 8);
            if (idxRegRet >= 8 || idxRegBase >= 8)
                pbCodeBuf[off++] = (idxRegRet >= 8 ? X86_OP_REX_R : 0) | (idxRegBase >= 8 ? X86_OP_REX_B : 0);
            pbCodeBuf[off++] = 0x8d;
            uint8_t const bMod = (int8_t)u32EffAddr == (int32_t)u32EffAddr ? X86_MOD_MEM1 : X86_MOD_MEM4;
            if (idxRegBase != X86_GREG_x12 /*SIB*/)
                pbCodeBuf[off++] = X86_MODRM_MAKE(bMod, idxRegRet & 7, idxRegBase & 7);
            else
            {
                pbCodeBuf[off++] = X86_MODRM_MAKE(bMod, idxRegRet & 7, 4 /*SIB*/);
                pbCodeBuf[off++] = X86_SIB_MAKE(X86_GREG_x12 & 7, 4 /*no index*/, 0);
            }
            pbCodeBuf[off++] = RT_BYTE1(u32EffAddr);
            if (bMod == X86_MOD_MEM4)
            {
                pbCodeBuf[off++] = RT_BYTE2(u32EffAddr);
                pbCodeBuf[off++] = RT_BYTE3(u32EffAddr);
                pbCodeBuf[off++] = RT_BYTE4(u32EffAddr);
            }
            IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
        }
    }
    else
    {
        Assert(idxRegIndex != X86_GREG_xSP /*no-index*/);
        uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 8);
        if (idxRegBase == UINT8_MAX)
        {
            /* lea ret32, [(index64 << cShiftIndex) + disp32] */
            if (idxRegRet >= 8 || idxRegIndex >= 8)
                pbCodeBuf[off++] = (idxRegRet   >= 8 ? X86_OP_REX_R : 0)
                                 | (idxRegIndex >= 8 ? X86_OP_REX_X : 0);
            pbCodeBuf[off++] = 0x8d;
            pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_MEM0, idxRegRet & 7, 4 /*SIB*/);
            pbCodeBuf[off++] = X86_SIB_MAKE(5 /*nobase/bp*/, idxRegIndex & 7, cShiftIndex);
            pbCodeBuf[off++] = RT_BYTE1(u32EffAddr);
            pbCodeBuf[off++] = RT_BYTE2(u32EffAddr);
            pbCodeBuf[off++] = RT_BYTE3(u32EffAddr);
            pbCodeBuf[off++] = RT_BYTE4(u32EffAddr);
        }
        else
        {
            /* lea ret32, [(index64 << cShiftIndex) + base64 (+ disp32)] */
            if (idxRegRet >= 8 || idxRegBase >= 8 || idxRegIndex >= 8)
                pbCodeBuf[off++] = (idxRegRet   >= 8 ? X86_OP_REX_R : 0)
                                 | (idxRegBase  >= 8 ? X86_OP_REX_B : 0)
                                 | (idxRegIndex >= 8 ? X86_OP_REX_X : 0);
            pbCodeBuf[off++] = 0x8d;
            uint8_t const bMod = u32EffAddr == 0 && (idxRegBase & 7) != X86_GREG_xBP ? X86_MOD_MEM0
                               : (int8_t)u32EffAddr == (int32_t)u32EffAddr           ? X86_MOD_MEM1 : X86_MOD_MEM4;
            pbCodeBuf[off++] = X86_MODRM_MAKE(bMod, idxRegRet & 7, 4 /*SIB*/);
            pbCodeBuf[off++] = X86_SIB_MAKE(idxRegBase & 7, idxRegIndex & 7, cShiftIndex);
            if (bMod != X86_MOD_MEM0)
            {
                pbCodeBuf[off++] = RT_BYTE1(u32EffAddr);
                if (bMod == X86_MOD_MEM4)
                {
                    pbCodeBuf[off++] = RT_BYTE2(u32EffAddr);
                    pbCodeBuf[off++] = RT_BYTE3(u32EffAddr);
                    pbCodeBuf[off++] = RT_BYTE4(u32EffAddr);
                }
            }
        }
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    }

#elif defined(RT_ARCH_ARM64)
    if (u32EffAddr == 0)
    {
        if (idxRegIndex == UINT8_MAX)
            off = iemNativeEmitLoadGprFromGpr32(pReNative, off, idxRegRet, idxRegBase);
        else if (idxRegBase == UINT8_MAX)
        {
            if (cShiftIndex == 0)
                off = iemNativeEmitLoadGprFromGpr32(pReNative, off, idxRegRet, idxRegIndex);
            else
            {
                uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
                pu32CodeBuf[off++] = Armv8A64MkInstrLslImm(idxRegRet, idxRegIndex, cShiftIndex, false /*f64Bit*/);
            }
        }
        else
        {
            uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
            pu32CodeBuf[off++] = Armv8A64MkInstrAddSubReg(false /*fSub*/, idxRegRet, idxRegBase, idxRegIndex,
                                                          false /*f64Bit*/, false /*fSetFlags*/, cShiftIndex);
        }
    }
    else
    {
        if ((int32_t)u32EffAddr < 4096 && (int32_t)u32EffAddr >= 0 && idxRegBase != UINT8_MAX)
        {
            uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
            pu32CodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(false /*fSub*/, idxRegRet, idxRegBase, u32EffAddr, false /*f64Bit*/);
        }
        else if ((int32_t)u32EffAddr > -4096 && (int32_t)u32EffAddr < 0 && idxRegBase != UINT8_MAX)
        {
            uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
            pu32CodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(true /*fSub*/, idxRegRet, idxRegBase,
                                                             (uint32_t)-(int32_t)u32EffAddr, false /*f64Bit*/);
        }
        else
        {
            off = iemNativeEmitLoadGprImm64(pReNative, off, idxRegRet, u32EffAddr);
            if (idxRegBase != UINT8_MAX)
            {
                uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
                pu32CodeBuf[off++] = Armv8A64MkInstrAddSubReg(false /*fSub*/, idxRegRet, idxRegRet, idxRegBase, false /*f64Bit*/);
            }
        }
        if (idxRegIndex != UINT8_MAX)
        {
            uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
            pu32CodeBuf[off++] = Armv8A64MkInstrAddSubReg(false /*fSub*/, idxRegRet, idxRegRet, idxRegIndex,
                                                          false /*f64Bit*/, false /*fSetFlags*/, cShiftIndex);
        }
    }

#else
# error "port me"
#endif

    if (idxRegIndex != UINT8_MAX)
        iemNativeRegFreeTmp(pReNative, idxRegIndex);
    if (idxRegBase != UINT8_MAX)
        iemNativeRegFreeTmp(pReNative, idxRegBase);
    iemNativeVarRegisterRelease(pReNative, idxVarRet);
    return off;
}


#define IEM_MC_CALC_RM_EFF_ADDR_THREADED_64(a_GCPtrEff, a_bRmEx, a_uSibAndRspOffset, a_u32Disp, a_cbImm) \
    off = iemNativeEmitCalcRmEffAddrThreadedAddr64(pReNative, off, a_bRmEx, a_uSibAndRspOffset, \
                                                   a_u32Disp, a_cbImm, a_GCPtrEff, true /*f64Bit*/)

#define IEM_MC_CALC_RM_EFF_ADDR_THREADED_64_FSGS(a_GCPtrEff, a_bRmEx, a_uSibAndRspOffset, a_u32Disp, a_cbImm) \
    off = iemNativeEmitCalcRmEffAddrThreadedAddr64(pReNative, off, a_bRmEx, a_uSibAndRspOffset, \
                                                   a_u32Disp, a_cbImm, a_GCPtrEff, true /*f64Bit*/)

#define IEM_MC_CALC_RM_EFF_ADDR_THREADED_64_ADDR32(a_GCPtrEff, a_bRmEx, a_uSibAndRspOffset, a_u32Disp, a_cbImm) \
    off = iemNativeEmitCalcRmEffAddrThreadedAddr64(pReNative, off, a_bRmEx, a_uSibAndRspOffset, \
                                                   a_u32Disp, a_cbImm, a_GCPtrEff, false /*f64Bit*/)

/**
 * Emit code for IEM_MC_CALC_RM_EFF_ADDR_THREADED_64*.
 *
 * @returns New off.
 * @param   pReNative           .
 * @param   off                 .
 * @param   bRmEx               The ModRM byte but with bit 3 set to REX.B and
 *                              bit 4 to REX.X.  The two bits are part of the
 *                              REG sub-field, which isn't needed in this
 *                              function.
 * @param   uSibAndRspOffset    Two parts:
 *                                - The first 8 bits make up the SIB byte.
 *                                - The next 8 bits are the fixed RSP/ESP offset
 *                                  in case of a pop [xSP].
 * @param   u32Disp             The displacement byte/word/dword, if any.
 * @param   cbInstr             The size of the fully decoded instruction. Used
 *                              for RIP relative addressing.
 * @param   idxVarRet           The result variable number.
 * @param   f64Bit              Whether to use a 64-bit or 32-bit address size
 *                              when calculating the address.
 *
 * @see iemOpHlpCalcRmEffAddrThreadedAddr64
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCalcRmEffAddrThreadedAddr64(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t bRmEx, uint32_t uSibAndRspOffset,
                                         uint32_t u32Disp, uint8_t cbInstr, uint8_t idxVarRet, bool f64Bit)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVarRet);

    /*
     * Special case the rip + disp32 form first.
     */
    if ((bRmEx & (X86_MODRM_MOD_MASK | X86_MODRM_RM_MASK)) == 5)
    {
        uint8_t const idxRegRet = iemNativeVarRegisterAcquire(pReNative, idxVarRet, &off);
        uint8_t const idxRegPc  = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_Pc,
                                                                  kIemNativeGstRegUse_ReadOnly);
        if (f64Bit)
        {
#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
            int64_t const offFinalDisp = (int64_t)(int32_t)u32Disp + cbInstr + (int64_t)pReNative->Core.offPc;
#else
            int64_t const offFinalDisp = (int64_t)(int32_t)u32Disp + cbInstr;
#endif
#ifdef RT_ARCH_AMD64
            if ((int32_t)offFinalDisp == offFinalDisp)
                off = iemNativeEmitLoadGprFromGprWithAddendMaybeZero(pReNative, off, idxRegRet, idxRegPc, (int32_t)offFinalDisp);
            else
            {
                off = iemNativeEmitLoadGprFromGprWithAddend(pReNative, off, idxRegRet, idxRegPc, (int32_t)u32Disp);
                off = iemNativeEmitAddGprImm8(pReNative, off, idxRegRet, cbInstr);
            }
#else
            off = iemNativeEmitLoadGprFromGprWithAddendMaybeZero(pReNative, off, idxRegRet, idxRegPc, offFinalDisp);
#endif
        }
        else
        {
# ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
            int32_t const offFinalDisp = (int32_t)u32Disp + cbInstr + (int32_t)pReNative->Core.offPc;
# else
            int32_t const offFinalDisp = (int32_t)u32Disp + cbInstr;
# endif
            off = iemNativeEmitLoadGprFromGpr32WithAddendMaybeZero(pReNative, off, idxRegRet, idxRegPc, offFinalDisp);
        }
        iemNativeRegFreeTmp(pReNative, idxRegPc);
        iemNativeVarRegisterRelease(pReNative, idxVarRet);
        return off;
    }

    /* Calculate the fixed displacement (more down in SIB.B=4 and SIB.B=5 on this). */
    int64_t i64EffAddr = 0;
    switch ((bRmEx >> X86_MODRM_MOD_SHIFT) & X86_MODRM_MOD_SMASK)
    {
        case 0: break;
        case 1: i64EffAddr = (int8_t)u32Disp; break;
        case 2: i64EffAddr = (int32_t)u32Disp; break;
        default: AssertFailed();
    }

    /* Get the register (or SIB) value. */
    uint8_t idxGstRegBase  = UINT8_MAX;
    uint8_t idxGstRegIndex = UINT8_MAX;
    uint8_t cShiftIndex    = 0;
    if ((bRmEx & X86_MODRM_RM_MASK) != 4)
        idxGstRegBase = bRmEx & (X86_MODRM_RM_MASK | 0x8); /* bRmEx[bit 3] = REX.B */
    else /* SIB: */
    {
        /* index /w scaling . */
        cShiftIndex    = (uSibAndRspOffset >> X86_SIB_SCALE_SHIFT) & X86_SIB_SCALE_SMASK;
        idxGstRegIndex = ((uSibAndRspOffset >> X86_SIB_INDEX_SHIFT) & X86_SIB_INDEX_SMASK)
                       | ((bRmEx & 0x10) >> 1); /* bRmEx[bit 4] = REX.X */
        if (idxGstRegIndex == 4)
        {
            /* no index */
            cShiftIndex    = 0;
            idxGstRegIndex = UINT8_MAX;
        }

        /* base */
        idxGstRegBase = (uSibAndRspOffset & X86_SIB_BASE_MASK) | (bRmEx & 0x8); /* bRmEx[bit 3] = REX.B */
        if (idxGstRegBase == 4)
        {
            /* pop [rsp] hack */
            i64EffAddr += uSibAndRspOffset >> 8; /* (this is why i64EffAddr must be 64-bit) */
        }
        else if (   (idxGstRegBase & X86_SIB_BASE_MASK) == 5
                 && (bRmEx & X86_MODRM_MOD_MASK) == 0)
        {
            /* mod=0 and base=5 -> disp32, no base reg. */
            Assert(i64EffAddr == 0);
            i64EffAddr    = (int32_t)u32Disp;
            idxGstRegBase = UINT8_MAX;
        }
    }

    /*
     * If no registers are involved (SIB.B=5, SIB.X=4) repeat what we did at
     * the start of the function.
     */
    if (idxGstRegBase == UINT8_MAX && idxGstRegIndex == UINT8_MAX)
    {
        if (f64Bit)
            iemNativeVarSetKindToConst(pReNative, idxVarRet, (uint64_t)i64EffAddr);
        else
            iemNativeVarSetKindToConst(pReNative, idxVarRet, (uint32_t)i64EffAddr);
        return off;
    }

    /*
     * Now emit code that calculates:
     *      idxRegRet = (uint64_t)(i64EffAddr [+ idxGstRegBase] [+ (idxGstRegIndex << cShiftIndex)])
     * or if !f64Bit:
     *      idxRegRet = (uint32_t)(i64EffAddr [+ idxGstRegBase] [+ (idxGstRegIndex << cShiftIndex)])
     */
    uint8_t const idxRegRet   = iemNativeVarRegisterAcquire(pReNative, idxVarRet, &off);
    uint8_t       idxRegBase  = idxGstRegBase == UINT8_MAX ? UINT8_MAX
                              : iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(idxGstRegBase),
                                                                kIemNativeGstRegUse_ReadOnly);
    uint8_t       idxRegIndex = idxGstRegIndex == UINT8_MAX ? UINT8_MAX
                              : iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(idxGstRegIndex),
                                                               kIemNativeGstRegUse_ReadOnly);

    /* If base is not given and there is no shifting, swap the registers to avoid code duplication. */
    if (idxRegBase == UINT8_MAX && cShiftIndex == 0)
    {
        idxRegBase  = idxRegIndex;
        idxRegIndex = UINT8_MAX;
    }

#ifdef RT_ARCH_AMD64
    uint8_t bFinalAdj;
    if (!f64Bit || (int32_t)i64EffAddr == i64EffAddr)
        bFinalAdj = 0; /* likely */
    else
    {
        /* pop [rsp] with a problematic disp32 value.  Split out the
           RSP offset and add it separately afterwards (bFinalAdj). */
        /** @todo testcase: pop [rsp] with problematic disp32 (mod4).   */
        Assert(idxGstRegBase == X86_GREG_xSP);
        Assert(((bRmEx >> X86_MODRM_MOD_SHIFT) & X86_MODRM_MOD_SMASK) == X86_MOD_MEM4);
        bFinalAdj   = (uint8_t)(uSibAndRspOffset >> 8);
        Assert(bFinalAdj != 0);
        i64EffAddr -= bFinalAdj;
        Assert((int32_t)i64EffAddr == i64EffAddr);
    }
    uint32_t const u32EffAddr = (uint32_t)i64EffAddr;
//pReNative->pInstrBuf[off++] = 0xcc;

    if (idxRegIndex == UINT8_MAX)
    {
        if (u32EffAddr == 0)
        {
            /* mov ret, base */
            if (f64Bit)
                off = iemNativeEmitLoadGprFromGpr(pReNative, off, idxRegRet, idxRegBase);
            else
                off = iemNativeEmitLoadGprFromGpr32(pReNative, off, idxRegRet, idxRegBase);
        }
        else
        {
            /* lea ret, [base + disp32] */
            Assert(idxRegBase != X86_GREG_xSP /*SIB*/);
            uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 8);
            if (f64Bit || idxRegRet >= 8 || idxRegBase >= 8)
                pbCodeBuf[off++] = (idxRegRet  >= 8 ? X86_OP_REX_R : 0)
                                 | (idxRegBase >= 8 ? X86_OP_REX_B : 0)
                                 | (f64Bit          ? X86_OP_REX_W : 0);
            pbCodeBuf[off++] = 0x8d;
            uint8_t const bMod = (int8_t)u32EffAddr == (int32_t)u32EffAddr ? X86_MOD_MEM1 : X86_MOD_MEM4;
            if (idxRegBase != X86_GREG_x12 /*SIB*/)
                pbCodeBuf[off++] = X86_MODRM_MAKE(bMod, idxRegRet & 7, idxRegBase & 7);
            else
            {
                pbCodeBuf[off++] = X86_MODRM_MAKE(bMod, idxRegRet & 7, 4 /*SIB*/);
                pbCodeBuf[off++] = X86_SIB_MAKE(X86_GREG_x12 & 7, 4 /*no index*/, 0);
            }
            pbCodeBuf[off++] = RT_BYTE1(u32EffAddr);
            if (bMod == X86_MOD_MEM4)
            {
                pbCodeBuf[off++] = RT_BYTE2(u32EffAddr);
                pbCodeBuf[off++] = RT_BYTE3(u32EffAddr);
                pbCodeBuf[off++] = RT_BYTE4(u32EffAddr);
            }
            IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
        }
    }
    else
    {
        Assert(idxRegIndex != X86_GREG_xSP /*no-index*/);
        uint8_t * const pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 8);
        if (idxRegBase == UINT8_MAX)
        {
            /* lea ret, [(index64 << cShiftIndex) + disp32] */
            if (f64Bit || idxRegRet >= 8 || idxRegIndex >= 8)
                pbCodeBuf[off++] = (idxRegRet   >= 8 ? X86_OP_REX_R : 0)
                                 | (idxRegIndex >= 8 ? X86_OP_REX_X : 0)
                                 | (f64Bit           ? X86_OP_REX_W : 0);
            pbCodeBuf[off++] = 0x8d;
            pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_MEM0, idxRegRet & 7, 4 /*SIB*/);
            pbCodeBuf[off++] = X86_SIB_MAKE(5 /*nobase/bp*/, idxRegIndex & 7, cShiftIndex);
            pbCodeBuf[off++] = RT_BYTE1(u32EffAddr);
            pbCodeBuf[off++] = RT_BYTE2(u32EffAddr);
            pbCodeBuf[off++] = RT_BYTE3(u32EffAddr);
            pbCodeBuf[off++] = RT_BYTE4(u32EffAddr);
        }
        else
        {
            /* lea ret, [(index64 << cShiftIndex) + base64 (+ disp32)] */
            if (f64Bit || idxRegRet >= 8 || idxRegBase >= 8 || idxRegIndex >= 8)
                pbCodeBuf[off++] = (idxRegRet   >= 8 ? X86_OP_REX_R : 0)
                                 | (idxRegBase  >= 8 ? X86_OP_REX_B : 0)
                                 | (idxRegIndex >= 8 ? X86_OP_REX_X : 0)
                                 | (f64Bit           ? X86_OP_REX_W : 0);
            pbCodeBuf[off++] = 0x8d;
            uint8_t const bMod = u32EffAddr == 0 && (idxRegBase & 7) != X86_GREG_xBP ? X86_MOD_MEM0
                               : (int8_t)u32EffAddr == (int32_t)u32EffAddr           ? X86_MOD_MEM1 : X86_MOD_MEM4;
            pbCodeBuf[off++] = X86_MODRM_MAKE(bMod, idxRegRet & 7, 4 /*SIB*/);
            pbCodeBuf[off++] = X86_SIB_MAKE(idxRegBase & 7, idxRegIndex & 7, cShiftIndex);
            if (bMod != X86_MOD_MEM0)
            {
                pbCodeBuf[off++] = RT_BYTE1(u32EffAddr);
                if (bMod == X86_MOD_MEM4)
                {
                    pbCodeBuf[off++] = RT_BYTE2(u32EffAddr);
                    pbCodeBuf[off++] = RT_BYTE3(u32EffAddr);
                    pbCodeBuf[off++] = RT_BYTE4(u32EffAddr);
                }
            }
        }
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    }

    if (!bFinalAdj)
    { /* likely */ }
    else
    {
        Assert(f64Bit);
        off = iemNativeEmitAddGprImm8(pReNative, off, idxRegRet, bFinalAdj);
    }

#elif defined(RT_ARCH_ARM64)
    if (i64EffAddr == 0)
    {
        uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
        if (idxRegIndex == UINT8_MAX)
            pu32CodeBuf[off++] = Armv8A64MkInstrMov(idxRegRet, idxRegBase, f64Bit);
        else if (idxRegBase != UINT8_MAX)
            pu32CodeBuf[off++] = Armv8A64MkInstrAddSubReg(false /*fSub*/, idxRegRet, idxRegBase, idxRegIndex,
                                                          f64Bit, false /*fSetFlags*/, cShiftIndex);
        else
        {
            Assert(cShiftIndex != 0); /* See base = index swap above when shift is 0 and we have no base reg. */
            pu32CodeBuf[off++] = Armv8A64MkInstrLslImm(idxRegRet, idxRegIndex, cShiftIndex, f64Bit);
        }
    }
    else
    {
        if (f64Bit)
        { /* likely */ }
        else
            i64EffAddr = (int32_t)i64EffAddr;

        if (i64EffAddr < 4096 && i64EffAddr >= 0 && idxRegBase != UINT8_MAX)
        {
            uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
            pu32CodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(false /*fSub*/, idxRegRet, idxRegBase, i64EffAddr, f64Bit);
        }
        else if (i64EffAddr > -4096 && i64EffAddr < 0 && idxRegBase != UINT8_MAX)
        {
            uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
            pu32CodeBuf[off++] = Armv8A64MkInstrAddSubUImm12(true /*fSub*/, idxRegRet, idxRegBase, (uint32_t)-i64EffAddr, f64Bit);
        }
        else
        {
            if (f64Bit)
                off = iemNativeEmitLoadGprImm64(pReNative, off, idxRegRet, i64EffAddr);
            else
                off = iemNativeEmitLoadGprImm64(pReNative, off, idxRegRet, (uint32_t)i64EffAddr);
            if (idxRegBase != UINT8_MAX)
            {
                uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
                pu32CodeBuf[off++] = Armv8A64MkInstrAddSubReg(false /*fSub*/, idxRegRet, idxRegRet, idxRegBase, f64Bit);
            }
        }
        if (idxRegIndex != UINT8_MAX)
        {
            uint32_t * const pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 1);
            pu32CodeBuf[off++] = Armv8A64MkInstrAddSubReg(false /*fSub*/, idxRegRet, idxRegRet, idxRegIndex,
                                                          f64Bit, false /*fSetFlags*/, cShiftIndex);
        }
    }

#else
# error "port me"
#endif

    if (idxRegIndex != UINT8_MAX)
        iemNativeRegFreeTmp(pReNative, idxRegIndex);
    if (idxRegBase != UINT8_MAX)
        iemNativeRegFreeTmp(pReNative, idxRegBase);
    iemNativeVarRegisterRelease(pReNative, idxVarRet);
    return off;
}


/*********************************************************************************************************************************
*   Memory fetches and stores common                                                                                             *
*********************************************************************************************************************************/

typedef enum IEMNATIVEMITMEMOP
{
    kIemNativeEmitMemOp_Store = 0,
    kIemNativeEmitMemOp_Fetch,
    kIemNativeEmitMemOp_Fetch_Zx_U16,
    kIemNativeEmitMemOp_Fetch_Zx_U32,
    kIemNativeEmitMemOp_Fetch_Zx_U64,
    kIemNativeEmitMemOp_Fetch_Sx_U16,
    kIemNativeEmitMemOp_Fetch_Sx_U32,
    kIemNativeEmitMemOp_Fetch_Sx_U64
} IEMNATIVEMITMEMOP;

/** Emits code for IEM_MC_FETCH_MEM_U8/16/32/64 and IEM_MC_STORE_MEM_U8/16/32/64,
 * and IEM_MC_FETCH_MEM_FLAT_U8/16/32/64 and IEM_MC_STORE_MEM_FLAT_U8/16/32/64
 * (with iSegReg = UINT8_MAX). */
/** @todo Pass enmOp, cbMem, fAlignMaskAndClt and a iSegReg == UINT8_MAX
 *        indicator as template parameters. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitMemFetchStoreDataCommon(PIEMRECOMPILERSTATE pReNative, uint32_t off,  uint8_t idxVarValue, uint8_t iSegReg,
                                     uint8_t idxVarGCPtrMem, uint8_t cbMem, uint32_t fAlignMaskAndCtl, IEMNATIVEMITMEMOP enmOp,
                                     uintptr_t pfnFunction, uint8_t idxInstr, uint8_t offDisp = 0)
{
    /*
     * Assert sanity.
     */
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVarValue);
    PIEMNATIVEVAR const pVarValue = &pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVarValue)];
    Assert(   enmOp != kIemNativeEmitMemOp_Store
           || pVarValue->enmKind == kIemNativeVarKind_Immediate
           || pVarValue->enmKind == kIemNativeVarKind_Stack);
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVarGCPtrMem);
    PIEMNATIVEVAR const pVarGCPtrMem = &pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVarGCPtrMem)];
    AssertStmt(   pVarGCPtrMem->enmKind == kIemNativeVarKind_Immediate
               || pVarGCPtrMem->enmKind == kIemNativeVarKind_Stack,
               IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_VAR_UNEXPECTED_KIND));
    Assert(iSegReg < 6 || iSegReg == UINT8_MAX);
#ifdef IEMNATIVE_WITH_SIMD_REG_ALLOCATOR
    Assert(   cbMem == 1 || cbMem == 2 || cbMem == 4 || cbMem == 8
           || cbMem == sizeof(RTUINT128U) || cbMem == sizeof(RTUINT256U));
#else
    Assert(cbMem == 1 || cbMem == 2 || cbMem == 4 || cbMem == 8);
#endif
    Assert(!(fAlignMaskAndCtl & ~(UINT32_C(0xff) | IEM_MEMMAP_F_ALIGN_GP | IEM_MEMMAP_F_ALIGN_SSE)));
    AssertCompile(IEMNATIVE_CALL_ARG_GREG_COUNT >= 4);
#ifdef VBOX_STRICT
    if (iSegReg == UINT8_MAX)
    {
        Assert(   (pReNative->fExec & IEM_F_MODE_MASK) == IEM_F_MODE_X86_64BIT
               || (pReNative->fExec & IEM_F_MODE_MASK) == IEM_F_MODE_X86_32BIT_PROT_FLAT
               || (pReNative->fExec & IEM_F_MODE_MASK) == IEM_F_MODE_X86_32BIT_FLAT);
        switch (cbMem)
        {
            case 1:
                Assert(   pfnFunction
                       == (  enmOp == kIemNativeEmitMemOp_Store        ? (uintptr_t)iemNativeHlpMemFlatStoreDataU8
                           : enmOp == kIemNativeEmitMemOp_Fetch        ? (uintptr_t)iemNativeHlpMemFlatFetchDataU8
                           : enmOp == kIemNativeEmitMemOp_Fetch_Zx_U16 ? (uintptr_t)iemNativeHlpMemFlatFetchDataU8
                           : enmOp == kIemNativeEmitMemOp_Fetch_Zx_U32 ? (uintptr_t)iemNativeHlpMemFlatFetchDataU8
                           : enmOp == kIemNativeEmitMemOp_Fetch_Zx_U64 ? (uintptr_t)iemNativeHlpMemFlatFetchDataU8
                           : enmOp == kIemNativeEmitMemOp_Fetch_Sx_U16 ? (uintptr_t)iemNativeHlpMemFlatFetchDataU8_Sx_U16
                           : enmOp == kIemNativeEmitMemOp_Fetch_Sx_U32 ? (uintptr_t)iemNativeHlpMemFlatFetchDataU8_Sx_U32
                           : enmOp == kIemNativeEmitMemOp_Fetch_Sx_U64 ? (uintptr_t)iemNativeHlpMemFlatFetchDataU8_Sx_U64
                           : UINT64_C(0xc000b000a0009000) ));
                Assert(!fAlignMaskAndCtl);
                break;
            case 2:
                Assert(   pfnFunction
                       == (  enmOp == kIemNativeEmitMemOp_Store        ? (uintptr_t)iemNativeHlpMemFlatStoreDataU16
                           : enmOp == kIemNativeEmitMemOp_Fetch        ? (uintptr_t)iemNativeHlpMemFlatFetchDataU16
                           : enmOp == kIemNativeEmitMemOp_Fetch_Zx_U32 ? (uintptr_t)iemNativeHlpMemFlatFetchDataU16
                           : enmOp == kIemNativeEmitMemOp_Fetch_Zx_U64 ? (uintptr_t)iemNativeHlpMemFlatFetchDataU16
                           : enmOp == kIemNativeEmitMemOp_Fetch_Sx_U32 ? (uintptr_t)iemNativeHlpMemFlatFetchDataU16_Sx_U32
                           : enmOp == kIemNativeEmitMemOp_Fetch_Sx_U64 ? (uintptr_t)iemNativeHlpMemFlatFetchDataU16_Sx_U64
                           : UINT64_C(0xc000b000a0009000) ));
                Assert(fAlignMaskAndCtl <= 1);
                break;
            case 4:
                Assert(   pfnFunction
                       == (  enmOp == kIemNativeEmitMemOp_Store        ? (uintptr_t)iemNativeHlpMemFlatStoreDataU32
                           : enmOp == kIemNativeEmitMemOp_Fetch        ? (uintptr_t)iemNativeHlpMemFlatFetchDataU32
                           : enmOp == kIemNativeEmitMemOp_Fetch_Zx_U64 ? (uintptr_t)iemNativeHlpMemFlatFetchDataU32
                           : enmOp == kIemNativeEmitMemOp_Fetch_Sx_U64 ? (uintptr_t)iemNativeHlpMemFlatFetchDataU32_Sx_U64
                           : UINT64_C(0xc000b000a0009000) ));
                Assert(fAlignMaskAndCtl <= 3);
                break;
            case 8:
                Assert(    pfnFunction
                       == (  enmOp == kIemNativeEmitMemOp_Store        ? (uintptr_t)iemNativeHlpMemFlatStoreDataU64
                           : enmOp == kIemNativeEmitMemOp_Fetch        ? (uintptr_t)iemNativeHlpMemFlatFetchDataU64
                           : UINT64_C(0xc000b000a0009000) ));
                Assert(fAlignMaskAndCtl <= 7);
                break;
#ifdef IEMNATIVE_WITH_SIMD_REG_ALLOCATOR
            case sizeof(RTUINT128U):
                Assert(   (   enmOp == kIemNativeEmitMemOp_Fetch
                           && (   pfnFunction == (uintptr_t)iemNativeHlpMemFlatFetchDataU128
                               || pfnFunction == (uintptr_t)iemNativeHlpMemFlatFetchDataU128AlignedSse
                               || pfnFunction == (uintptr_t)iemNativeHlpMemFlatFetchDataU128NoAc))
                       || (   enmOp == kIemNativeEmitMemOp_Store
                           && (   pfnFunction == (uintptr_t)iemNativeHlpMemFlatStoreDataU128AlignedSse
                               || pfnFunction == (uintptr_t)iemNativeHlpMemFlatStoreDataU128NoAc)));
                Assert(   pfnFunction == (uintptr_t)iemNativeHlpMemFlatFetchDataU128AlignedSse
                       || pfnFunction == (uintptr_t)iemNativeHlpMemFlatStoreDataU128AlignedSse
                       ? (fAlignMaskAndCtl & (IEM_MEMMAP_F_ALIGN_GP | IEM_MEMMAP_F_ALIGN_SSE)) && (uint8_t)fAlignMaskAndCtl == 15
                       : fAlignMaskAndCtl <= 15);
                break;
            case sizeof(RTUINT256U):
                Assert(   (   enmOp == kIemNativeEmitMemOp_Fetch
                           && (   pfnFunction == (uintptr_t)iemNativeHlpMemFlatFetchDataU256NoAc
                               || pfnFunction == (uintptr_t)iemNativeHlpMemFlatFetchDataU256AlignedAvx))
                       || (   enmOp == kIemNativeEmitMemOp_Store
                           && (   pfnFunction == (uintptr_t)iemNativeHlpMemFlatStoreDataU256NoAc
                               || pfnFunction == (uintptr_t)iemNativeHlpMemFlatStoreDataU256AlignedAvx)));
                Assert(   pfnFunction == (uintptr_t)iemNativeHlpMemFlatFetchDataU256AlignedAvx
                       || pfnFunction == (uintptr_t)iemNativeHlpMemFlatStoreDataU256AlignedAvx
                       ? (fAlignMaskAndCtl & IEM_MEMMAP_F_ALIGN_GP) && (uint8_t)fAlignMaskAndCtl == 31
                       : fAlignMaskAndCtl <= 31);
                break;
#endif
        }
    }
    else
    {
        Assert(iSegReg < 6);
        switch (cbMem)
        {
            case 1:
                Assert(   pfnFunction
                       == (  enmOp == kIemNativeEmitMemOp_Store        ? (uintptr_t)iemNativeHlpMemStoreDataU8
                           : enmOp == kIemNativeEmitMemOp_Fetch        ? (uintptr_t)iemNativeHlpMemFetchDataU8
                           : enmOp == kIemNativeEmitMemOp_Fetch_Zx_U16 ? (uintptr_t)iemNativeHlpMemFetchDataU8
                           : enmOp == kIemNativeEmitMemOp_Fetch_Zx_U32 ? (uintptr_t)iemNativeHlpMemFetchDataU8
                           : enmOp == kIemNativeEmitMemOp_Fetch_Zx_U64 ? (uintptr_t)iemNativeHlpMemFetchDataU8
                           : enmOp == kIemNativeEmitMemOp_Fetch_Sx_U16 ? (uintptr_t)iemNativeHlpMemFetchDataU8_Sx_U16
                           : enmOp == kIemNativeEmitMemOp_Fetch_Sx_U32 ? (uintptr_t)iemNativeHlpMemFetchDataU8_Sx_U32
                           : enmOp == kIemNativeEmitMemOp_Fetch_Sx_U64 ? (uintptr_t)iemNativeHlpMemFetchDataU8_Sx_U64
                           : UINT64_C(0xc000b000a0009000) ));
                Assert(!fAlignMaskAndCtl);
                break;
            case 2:
                Assert(   pfnFunction
                       == (  enmOp == kIemNativeEmitMemOp_Store        ? (uintptr_t)iemNativeHlpMemStoreDataU16
                           : enmOp == kIemNativeEmitMemOp_Fetch        ? (uintptr_t)iemNativeHlpMemFetchDataU16
                           : enmOp == kIemNativeEmitMemOp_Fetch_Zx_U32 ? (uintptr_t)iemNativeHlpMemFetchDataU16
                           : enmOp == kIemNativeEmitMemOp_Fetch_Zx_U64 ? (uintptr_t)iemNativeHlpMemFetchDataU16
                           : enmOp == kIemNativeEmitMemOp_Fetch_Sx_U32 ? (uintptr_t)iemNativeHlpMemFetchDataU16_Sx_U32
                           : enmOp == kIemNativeEmitMemOp_Fetch_Sx_U64 ? (uintptr_t)iemNativeHlpMemFetchDataU16_Sx_U64
                           : UINT64_C(0xc000b000a0009000) ));
                Assert(fAlignMaskAndCtl <= 1);
                break;
            case 4:
                Assert(   pfnFunction
                       == (  enmOp == kIemNativeEmitMemOp_Store        ? (uintptr_t)iemNativeHlpMemStoreDataU32
                           : enmOp == kIemNativeEmitMemOp_Fetch        ? (uintptr_t)iemNativeHlpMemFetchDataU32
                           : enmOp == kIemNativeEmitMemOp_Fetch_Zx_U64 ? (uintptr_t)iemNativeHlpMemFetchDataU32
                           : enmOp == kIemNativeEmitMemOp_Fetch_Sx_U64 ? (uintptr_t)iemNativeHlpMemFetchDataU32_Sx_U64
                           : UINT64_C(0xc000b000a0009000) ));
                Assert(fAlignMaskAndCtl <= 3);
                break;
            case 8:
                Assert(    pfnFunction
                       == (  enmOp == kIemNativeEmitMemOp_Store        ? (uintptr_t)iemNativeHlpMemStoreDataU64
                           : enmOp == kIemNativeEmitMemOp_Fetch        ? (uintptr_t)iemNativeHlpMemFetchDataU64
                           : UINT64_C(0xc000b000a0009000) ));
                Assert(fAlignMaskAndCtl <= 7);
                break;
#ifdef IEMNATIVE_WITH_SIMD_REG_ALLOCATOR
            case sizeof(RTUINT128U):
                Assert(   (   enmOp == kIemNativeEmitMemOp_Fetch
                           && (   pfnFunction == (uintptr_t)iemNativeHlpMemFetchDataU128
                               || pfnFunction == (uintptr_t)iemNativeHlpMemFetchDataU128AlignedSse
                               || pfnFunction == (uintptr_t)iemNativeHlpMemFetchDataU128NoAc))
                       || (   enmOp == kIemNativeEmitMemOp_Store
                           && (   pfnFunction == (uintptr_t)iemNativeHlpMemStoreDataU128AlignedSse
                               || pfnFunction == (uintptr_t)iemNativeHlpMemStoreDataU128NoAc)));
                Assert(   pfnFunction == (uintptr_t)iemNativeHlpMemFetchDataU128AlignedSse
                       || pfnFunction == (uintptr_t)iemNativeHlpMemStoreDataU128AlignedSse
                       ? (fAlignMaskAndCtl & (IEM_MEMMAP_F_ALIGN_GP | IEM_MEMMAP_F_ALIGN_SSE)) && (uint8_t)fAlignMaskAndCtl == 15
                       : fAlignMaskAndCtl <= 15);
                break;
            case sizeof(RTUINT256U):
                Assert(   (   enmOp == kIemNativeEmitMemOp_Fetch
                           && (   pfnFunction == (uintptr_t)iemNativeHlpMemFetchDataU256NoAc
                               || pfnFunction == (uintptr_t)iemNativeHlpMemFetchDataU256AlignedAvx))
                       || (   enmOp == kIemNativeEmitMemOp_Store
                           && (   pfnFunction == (uintptr_t)iemNativeHlpMemStoreDataU256NoAc
                               || pfnFunction == (uintptr_t)iemNativeHlpMemStoreDataU256AlignedAvx)));
                Assert(   pfnFunction == (uintptr_t)iemNativeHlpMemFetchDataU256AlignedAvx
                       || pfnFunction == (uintptr_t)iemNativeHlpMemStoreDataU256AlignedAvx
                       ? (fAlignMaskAndCtl & IEM_MEMMAP_F_ALIGN_GP) && (uint8_t)fAlignMaskAndCtl == 31
                       : fAlignMaskAndCtl <= 31);
                break;
#endif
        }
    }
#endif

#ifdef VBOX_STRICT
    /*
     * Check that the fExec flags we've got make sense.
     */
    off = iemNativeEmitExecFlagsCheck(pReNative, off, pReNative->fExec);
#endif

    /*
     * To keep things simple we have to commit any pending writes first as we
     * may end up making calls.
     */
    /** @todo we could postpone this till we make the call and reload the
     * registers after returning from the call. Not sure if that's sensible or
     * not, though. */
#ifndef IEMNATIVE_WITH_DELAYED_PC_UPDATING
    off = iemNativeRegFlushPendingWrites(pReNative, off);
#else
    /* The program counter is treated differently for now. */
    off = iemNativeRegFlushPendingWrites(pReNative, off, RT_BIT_64(kIemNativeGstReg_Pc));
#endif

#ifdef IEMNATIVE_WITH_FREE_AND_FLUSH_VOLATILE_REGS_AT_TLB_LOOKUP
    /*
     * Move/spill/flush stuff out of call-volatile registers.
     * This is the easy way out. We could contain this to the tlb-miss branch
     * by saving and restoring active stuff here.
     */
    off = iemNativeRegMoveAndFreeAndFlushAtCall(pReNative, off, 0 /* vacate all non-volatile regs */);
#endif

    /*
     * Define labels and allocate the result register (trying for the return
     * register if we can).
     */
    uint16_t const uTlbSeqNo         = pReNative->uTlbSeqNo++;
#ifdef IEMNATIVE_WITH_SIMD_REG_ALLOCATOR
    uint8_t  idxRegValueFetch = UINT8_MAX;

    if (cbMem == sizeof(RTUINT128U) || cbMem == sizeof(RTUINT256U))
        idxRegValueFetch = enmOp == kIemNativeEmitMemOp_Store ? UINT8_MAX
                         : iemNativeVarSimdRegisterAcquire(pReNative, idxVarValue, &off);
    else
        idxRegValueFetch = enmOp == kIemNativeEmitMemOp_Store ? UINT8_MAX
                         : !(pReNative->Core.bmHstRegs & RT_BIT_32(IEMNATIVE_CALL_RET_GREG))
                         ? iemNativeVarRegisterSetAndAcquire(pReNative, idxVarValue, IEMNATIVE_CALL_RET_GREG, &off)
                         : iemNativeVarRegisterAcquire(pReNative, idxVarValue, &off);
#else
    uint8_t  const idxRegValueFetch  = enmOp == kIemNativeEmitMemOp_Store ? UINT8_MAX
                                     : !(pReNative->Core.bmHstRegs & RT_BIT_32(IEMNATIVE_CALL_RET_GREG))
                                     ? iemNativeVarRegisterSetAndAcquire(pReNative, idxVarValue, IEMNATIVE_CALL_RET_GREG, &off)
                                     : iemNativeVarRegisterAcquire(pReNative, idxVarValue, &off);
#endif
    IEMNATIVEEMITTLBSTATE const TlbState(pReNative, &off, idxVarGCPtrMem, iSegReg, cbMem, offDisp);

#ifdef IEMNATIVE_WITH_SIMD_REG_ALLOCATOR
    uint8_t  idxRegValueStore = UINT8_MAX;

    if (cbMem == sizeof(RTUINT128U) || cbMem == sizeof(RTUINT256U))
        idxRegValueStore =     !TlbState.fSkip
                            && enmOp == kIemNativeEmitMemOp_Store
                            && pVarValue->enmKind != kIemNativeVarKind_Immediate
                          ? iemNativeVarSimdRegisterAcquire(pReNative, idxVarValue, &off, true /*fInitialized*/)
                          : UINT8_MAX;
    else
        idxRegValueStore  =    !TlbState.fSkip
                            && enmOp == kIemNativeEmitMemOp_Store
                            && pVarValue->enmKind != kIemNativeVarKind_Immediate
                          ? iemNativeVarRegisterAcquire(pReNative, idxVarValue, &off, true /*fInitialized*/)
                          : UINT8_MAX;

#else
    uint8_t  const idxRegValueStore  =    !TlbState.fSkip
                                       && enmOp == kIemNativeEmitMemOp_Store
                                       && pVarValue->enmKind != kIemNativeVarKind_Immediate
                                     ? iemNativeVarRegisterAcquire(pReNative, idxVarValue, &off, true /*fInitialized*/)
                                     : UINT8_MAX;
#endif
    uint32_t const idxRegMemResult   = !TlbState.fSkip ? iemNativeRegAllocTmp(pReNative, &off) : UINT8_MAX;
    uint32_t const idxLabelTlbLookup = !TlbState.fSkip
                                     ? iemNativeLabelCreate(pReNative, kIemNativeLabelType_TlbLookup, UINT32_MAX, uTlbSeqNo)
                                     : UINT32_MAX;

    /*
     * Jump to the TLB lookup code.
     */
    if (!TlbState.fSkip)
        off = iemNativeEmitJmpToLabel(pReNative, off, idxLabelTlbLookup); /** @todo short jump */

    /*
     * TlbMiss:
     *
     * Call helper to do the fetching.
     * We flush all guest register shadow copies here.
     */
    uint32_t const idxLabelTlbMiss = iemNativeLabelCreate(pReNative, kIemNativeLabelType_TlbMiss, off, uTlbSeqNo);

#ifdef IEMNATIVE_WITH_INSTRUCTION_COUNTING
    off = iemNativeEmitStoreImmToVCpuU8(pReNative, off, idxInstr, RT_UOFFSETOF(VMCPUCC, iem.s.idxTbCurInstr));
#else
    RT_NOREF(idxInstr);
#endif

#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
    if (pReNative->Core.offPc)
    {
        /*
         * Update the program counter but restore it at the end of the TlbMiss branch.
         * This should allow delaying more program counter updates for the TlbLookup and hit paths
         * which are hopefully much more frequent, reducing the amount of memory accesses.
         */
        /* Allocate a temporary PC register. */
/** @todo r=bird: This would technically need to be done up front as it's a register allocation. */
        uint8_t const idxPcReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_Pc,
                                                                 kIemNativeGstRegUse_ForUpdate);

        /* Perform the addition and store the result. */
        off = iemNativeEmitAddGprImm(pReNative, off, idxPcReg, pReNative->Core.offPc);
        off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxPcReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rip));
# ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING_DEBUG
        off = iemNativeEmitPcDebugCheckWithReg(pReNative, off, idxPcReg);
# endif

        /* Free and flush the PC register. */
        iemNativeRegFreeTmp(pReNative, idxPcReg);
        iemNativeRegFlushGuestShadowsByHostMask(pReNative, RT_BIT_32(idxPcReg));
    }
#endif

#ifndef IEMNATIVE_WITH_FREE_AND_FLUSH_VOLATILE_REGS_AT_TLB_LOOKUP
    /* Save variables in volatile registers. */
    uint32_t const fHstRegsNotToSave = TlbState.getRegsNotToSave()
                                     | (idxRegMemResult  != UINT8_MAX ? RT_BIT_32(idxRegMemResult)  : 0)
                                     | (idxRegValueFetch != UINT8_MAX ? RT_BIT_32(idxRegValueFetch) : 0);
    off = iemNativeVarSaveVolatileRegsPreHlpCall(pReNative, off, fHstRegsNotToSave);
#endif

    /* IEMNATIVE_CALL_ARG2/3_GREG = uValue (idxVarValue) - if store */
    uint32_t fVolGregMask = IEMNATIVE_CALL_VOLATILE_GREG_MASK;
#ifdef IEMNATIVE_WITH_SIMD_REG_ALLOCATOR
    if (cbMem == sizeof(RTUINT128U) || cbMem == sizeof(RTUINT256U))
    {
        /*
         * For SIMD based variables we pass the reference on the stack for both fetches and stores.
         *
         * Note! There was a register variable assigned to the variable for the TlbLookup case above
         *       which must not be freed or the value loaded into the register will not be synced into the register
         *       further down the road because the variable doesn't know it had a variable assigned.
         *
         * Note! For loads it is not required to sync what is in the assigned register with the stack slot
         *       as it will be overwritten anyway.
         */
        uint8_t const idxRegArgValue = iSegReg == UINT8_MAX ? IEMNATIVE_CALL_ARG2_GREG : IEMNATIVE_CALL_ARG3_GREG;
        off = iemNativeEmitLoadArgGregWithSimdVarAddrForMemAccess(pReNative, off, idxRegArgValue, idxVarValue,
                                                                  enmOp == kIemNativeEmitMemOp_Store /*fSyncRegWithStack*/);
        fVolGregMask &= ~RT_BIT_32(idxRegArgValue);
    }
    else
#endif
    if (enmOp == kIemNativeEmitMemOp_Store)
    {
        uint8_t const idxRegArgValue = iSegReg == UINT8_MAX ? IEMNATIVE_CALL_ARG2_GREG : IEMNATIVE_CALL_ARG3_GREG;
        off = iemNativeEmitLoadArgGregFromImmOrStackVar(pReNative, off, idxRegArgValue, idxVarValue, 0 /*cbAppend*/,
#ifdef IEMNATIVE_WITH_FREE_AND_FLUSH_VOLATILE_REGS_AT_TLB_LOOKUP
                                                        IEMNATIVE_CALL_VOLATILE_GREG_MASK);
#else
                                                        IEMNATIVE_CALL_VOLATILE_GREG_MASK, true /*fSpilledVarsInvolatileRegs*/);
        fVolGregMask &= ~RT_BIT_32(idxRegArgValue);
#endif
    }

    /* IEMNATIVE_CALL_ARG1_GREG = GCPtrMem */
    off = iemNativeEmitLoadArgGregFromImmOrStackVar(pReNative, off, IEMNATIVE_CALL_ARG1_GREG, idxVarGCPtrMem, offDisp /*cbAppend*/,
#ifdef IEMNATIVE_WITH_FREE_AND_FLUSH_VOLATILE_REGS_AT_TLB_LOOKUP
                                                    fVolGregMask);
#else
                                                    fVolGregMask, true /*fSpilledVarsInvolatileRegs*/);
#endif

    if (iSegReg != UINT8_MAX)
    {
        /* IEMNATIVE_CALL_ARG2_GREG = iSegReg */
        AssertStmt(iSegReg < 6, IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_EMIT_BAD_SEG_REG_NO));
        off = iemNativeEmitLoadGpr8Imm(pReNative, off, IEMNATIVE_CALL_ARG2_GREG, iSegReg);
    }

#ifdef IEMNATIVE_WITH_EFLAGS_POSTPONING
    /* Do delayed EFLAGS calculations. */
    if (enmOp == kIemNativeEmitMemOp_Store || cbMem == sizeof(RTUINT128U) || cbMem == sizeof(RTUINT256U))
    {
        if (iSegReg == UINT8_MAX)
            off = iemNativeDoPostponedEFlagsAtTlbMiss<  RT_BIT_32(IEMNATIVE_CALL_ARG1_GREG)
                                                      | RT_BIT_32(IEMNATIVE_CALL_ARG2_GREG)>(pReNative, off, &TlbState,
                                                                                             fHstRegsNotToSave);
        else
            off = iemNativeDoPostponedEFlagsAtTlbMiss<  RT_BIT_32(IEMNATIVE_CALL_ARG1_GREG)
                                                      | RT_BIT_32(IEMNATIVE_CALL_ARG2_GREG)
                                                      | RT_BIT_32(IEMNATIVE_CALL_ARG3_GREG)>(pReNative, off, &TlbState,
                                                                                             fHstRegsNotToSave);
    }
    else if (iSegReg == UINT8_MAX)
        off = iemNativeDoPostponedEFlagsAtTlbMiss<  RT_BIT_32(IEMNATIVE_CALL_ARG1_GREG)>(pReNative, off, &TlbState,
                                                                                         fHstRegsNotToSave);
    else
        off = iemNativeDoPostponedEFlagsAtTlbMiss<  RT_BIT_32(IEMNATIVE_CALL_ARG1_GREG)
                                                  | RT_BIT_32(IEMNATIVE_CALL_ARG2_GREG)>(pReNative, off, &TlbState,
                                                                                         fHstRegsNotToSave);
#endif

    /* IEMNATIVE_CALL_ARG0_GREG = pVCpu */
    off = iemNativeEmitLoadGprFromGpr(pReNative, off, IEMNATIVE_CALL_ARG0_GREG, IEMNATIVE_REG_FIXED_PVMCPU);

    /* Done setting up parameters, make the call. */
    off = iemNativeEmitCallImm<true /*a_fSkipEflChecks*/>(pReNative, off, pfnFunction);

    /*
     * Put the result in the right register if this is a fetch.
     */
    if (enmOp != kIemNativeEmitMemOp_Store)
    {
#ifdef IEMNATIVE_WITH_SIMD_REG_ALLOCATOR
        if (   cbMem == sizeof(RTUINT128U)
            || cbMem == sizeof(RTUINT256U))
        {
            Assert(enmOp == kIemNativeEmitMemOp_Fetch);

            /* Sync the value on the stack with the host register assigned to the variable. */
            off = iemNativeEmitSimdVarSyncStackToRegister(pReNative, off, idxVarValue);
        }
        else
#endif
        {
            Assert(idxRegValueFetch == pVarValue->idxReg);
            if (idxRegValueFetch != IEMNATIVE_CALL_RET_GREG)
                off = iemNativeEmitLoadGprFromGpr(pReNative, off, idxRegValueFetch, IEMNATIVE_CALL_RET_GREG);
        }
    }

#ifndef IEMNATIVE_WITH_FREE_AND_FLUSH_VOLATILE_REGS_AT_TLB_LOOKUP
    /* Restore variables and guest shadow registers to volatile registers. */
    off = iemNativeVarRestoreVolatileRegsPostHlpCall(pReNative, off, fHstRegsNotToSave);
    off = iemNativeRegRestoreGuestShadowsInVolatileRegs(pReNative, off, TlbState.getActiveRegsWithShadows());
#endif

#ifdef IEMNATIVE_WITH_DELAYED_PC_UPDATING
    if (pReNative->Core.offPc)
    {
        /*
         * Time to restore the program counter to its original value.
         */
        /* Allocate a temporary PC register. */
        uint8_t const idxPcReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_Pc,
                                                                 kIemNativeGstRegUse_ForUpdate);

        /* Restore the original value. */
        off = iemNativeEmitSubGprImm(pReNative, off, idxPcReg, pReNative->Core.offPc);
        off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxPcReg, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rip));

        /* Free and flush the PC register. */
        iemNativeRegFreeTmp(pReNative, idxPcReg);
        iemNativeRegFlushGuestShadowsByHostMask(pReNative, RT_BIT_32(idxPcReg));
    }
#endif

#ifdef IEMNATIVE_WITH_TLB_LOOKUP
    if (!TlbState.fSkip)
    {
        /* end of TlbMiss - Jump to the done label. */
        uint32_t const idxLabelTlbDone = iemNativeLabelCreate(pReNative, kIemNativeLabelType_TlbDone, UINT32_MAX, uTlbSeqNo);
        off = iemNativeEmitJmpToLabel(pReNative, off, idxLabelTlbDone);

        /*
         * TlbLookup:
         */
        off = iemNativeEmitTlbLookup<true>(pReNative, off, &TlbState, iSegReg, cbMem, fAlignMaskAndCtl,
                                           enmOp == kIemNativeEmitMemOp_Store ? IEM_ACCESS_TYPE_WRITE : IEM_ACCESS_TYPE_READ,
                                           idxLabelTlbLookup, idxLabelTlbMiss, idxRegMemResult, offDisp);

        /*
         * Emit code to do the actual storing / fetching.
         */
        PIEMNATIVEINSTR pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 64);
# ifdef IEM_WITH_TLB_STATISTICS
        off = iemNativeEmitIncStamCounterInVCpuEx(pCodeBuf, off, TlbState.idxReg1, TlbState.idxReg2,
                                                  enmOp == kIemNativeEmitMemOp_Store
                                                  ? RT_UOFFSETOF(VMCPUCC, iem.s.StatNativeTlbHitsForFetch)
                                                  : RT_UOFFSETOF(VMCPUCC, iem.s.StatNativeTlbHitsForStore));
# endif
        switch (enmOp)
        {
            case kIemNativeEmitMemOp_Store:
                if (pVarValue->enmKind != kIemNativeVarKind_Immediate)
                {
                    switch (cbMem)
                    {
                        case 1:
                            off = iemNativeEmitStoreGpr8ByGprEx(pCodeBuf, off, idxRegValueStore, idxRegMemResult);
                            break;
                        case 2:
                            off = iemNativeEmitStoreGpr16ByGprEx(pCodeBuf, off, idxRegValueStore, idxRegMemResult);
                            break;
                        case 4:
                            off = iemNativeEmitStoreGpr32ByGprEx(pCodeBuf, off, idxRegValueStore, idxRegMemResult);
                            break;
                        case 8:
                            off = iemNativeEmitStoreGpr64ByGprEx(pCodeBuf, off, idxRegValueStore, idxRegMemResult);
                            break;
#ifdef IEMNATIVE_WITH_SIMD_REG_ALLOCATOR
                        case sizeof(RTUINT128U):
                            off = iemNativeEmitStoreVecRegByGprU128Ex(pCodeBuf, off, idxRegValueStore, idxRegMemResult);
                            break;
                        case sizeof(RTUINT256U):
                            off = iemNativeEmitStoreVecRegByGprU256Ex(pCodeBuf, off, idxRegValueStore, idxRegMemResult);
                            break;
#endif
                        default:
                            AssertFailed();
                    }
                }
                else
                {
                    switch (cbMem)
                    {
                        case 1:
                            off = iemNativeEmitStoreImm8ByGprEx(pCodeBuf, off, (uint8_t)pVarValue->u.uValue,
                                                                idxRegMemResult, TlbState.idxReg1);
                            break;
                        case 2:
                            off = iemNativeEmitStoreImm16ByGprEx(pCodeBuf, off, (uint16_t)pVarValue->u.uValue,
                                                                 idxRegMemResult, TlbState.idxReg1);
                            break;
                        case 4:
                            off = iemNativeEmitStoreImm32ByGprEx(pCodeBuf, off, (uint32_t)pVarValue->u.uValue,
                                                                 idxRegMemResult, TlbState.idxReg1);
                            break;
                        case 8:
                            off = iemNativeEmitStoreImm64ByGprEx(pCodeBuf, off, pVarValue->u.uValue,
                                                                 idxRegMemResult, TlbState.idxReg1);
                            break;
                        default:
                            AssertFailed();
                    }
                }
                break;

            case kIemNativeEmitMemOp_Fetch:
            case kIemNativeEmitMemOp_Fetch_Zx_U16:
            case kIemNativeEmitMemOp_Fetch_Zx_U32:
            case kIemNativeEmitMemOp_Fetch_Zx_U64:
                switch (cbMem)
                {
                    case 1:
                        off = iemNativeEmitLoadGprByGprU8Ex(pCodeBuf, off, idxRegValueFetch, idxRegMemResult);
                        break;
                    case 2:
                        off = iemNativeEmitLoadGprByGprU16Ex(pCodeBuf, off, idxRegValueFetch, idxRegMemResult);
                        break;
                    case 4:
                        off = iemNativeEmitLoadGprByGprU32Ex(pCodeBuf, off, idxRegValueFetch, idxRegMemResult);
                        break;
                    case 8:
                        off = iemNativeEmitLoadGprByGprU64Ex(pCodeBuf, off, idxRegValueFetch, idxRegMemResult);
                        break;
#ifdef IEMNATIVE_WITH_SIMD_REG_ALLOCATOR
                    case sizeof(RTUINT128U):
                        /*
                         * No need to sync back the register with the stack, this is done by the generic variable handling
                         * code if there is a register assigned to a variable and the stack must be accessed.
                         */
                        off = iemNativeEmitLoadVecRegByGprU128Ex(pCodeBuf, off, idxRegValueFetch, idxRegMemResult);
                        break;
                    case sizeof(RTUINT256U):
                        /*
                         * No need to sync back the register with the stack, this is done by the generic variable handling
                         * code if there is a register assigned to a variable and the stack must be accessed.
                         */
                        off = iemNativeEmitLoadVecRegByGprU256Ex(pCodeBuf, off, idxRegValueFetch, idxRegMemResult);
                        break;
#endif
                    default:
                        AssertFailed();
                }
                break;

            case kIemNativeEmitMemOp_Fetch_Sx_U16:
                Assert(cbMem == 1);
                off = iemNativeEmitLoadGprByGprU16SignExtendedFromS8Ex(pCodeBuf, off, idxRegValueFetch, idxRegMemResult);
                break;

            case kIemNativeEmitMemOp_Fetch_Sx_U32:
                Assert(cbMem == 1 || cbMem == 2);
                if (cbMem == 1)
                    off = iemNativeEmitLoadGprByGprU32SignExtendedFromS8Ex(pCodeBuf, off, idxRegValueFetch, idxRegMemResult);
                else
                    off = iemNativeEmitLoadGprByGprU32SignExtendedFromS16Ex(pCodeBuf, off, idxRegValueFetch, idxRegMemResult);
                break;

            case kIemNativeEmitMemOp_Fetch_Sx_U64:
                switch (cbMem)
                {
                    case 1:
                        off = iemNativeEmitLoadGprByGprU64SignExtendedFromS8Ex(pCodeBuf, off, idxRegValueFetch, idxRegMemResult);
                        break;
                    case 2:
                        off = iemNativeEmitLoadGprByGprU64SignExtendedFromS16Ex(pCodeBuf, off, idxRegValueFetch, idxRegMemResult);
                        break;
                    case 4:
                        off = iemNativeEmitLoadGprByGprU64SignExtendedFromS32Ex(pCodeBuf, off, idxRegValueFetch, idxRegMemResult);
                        break;
                    default:
                        AssertFailed();
                }
                break;

            default:
                AssertFailed();
        }

        iemNativeRegFreeTmp(pReNative, idxRegMemResult);

        /*
         * TlbDone:
         */
        iemNativeLabelDefine(pReNative, idxLabelTlbDone, off);

        TlbState.freeRegsAndReleaseVars(pReNative, idxVarGCPtrMem);

# ifndef IEMNATIVE_WITH_FREE_AND_FLUSH_VOLATILE_REGS_AT_TLB_LOOKUP
        /* Temp Hack: Flush all guest shadows in volatile registers in case of TLB miss. */
        iemNativeRegFlushGuestShadowsByHostMask(pReNative, IEMNATIVE_CALL_VOLATILE_GREG_MASK);
# endif
    }
#else
    RT_NOREF(fAlignMaskAndCtl, idxLabelTlbMiss);
#endif

    if (idxRegValueFetch != UINT8_MAX || idxRegValueStore != UINT8_MAX)
        iemNativeVarRegisterRelease(pReNative, idxVarValue);
    return off;
}



/*********************************************************************************************************************************
*   Memory fetches (IEM_MEM_FETCH_XXX).                                                                                          *
*********************************************************************************************************************************/

/* 8-bit segmented: */
#define IEM_MC_FETCH_MEM_U8(a_u8Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u8Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(uint8_t), 0 /*fAlignMaskAndCtl*/, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU8, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_U8_ZX_U16(a_u16Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u16Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(uint8_t), 0 /*fAlignMaskAndCtl*/, kIemNativeEmitMemOp_Fetch_Zx_U16, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU8, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_U8_ZX_U32(a_u32Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u32Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(uint8_t), 0 /*fAlignMaskAndCtl*/, kIemNativeEmitMemOp_Fetch_Zx_U32, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU8, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_U8_ZX_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u64Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(uint8_t), 0 /*fAlignMaskAndCtl*/, kIemNativeEmitMemOp_Fetch_Zx_U64, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU8, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_U8_SX_U16(a_u16Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u16Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(uint8_t), 0 /*fAlignMaskAndCtl*/, kIemNativeEmitMemOp_Fetch_Sx_U16, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU8_Sx_U16, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_U8_SX_U32(a_u32Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u32Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(uint8_t), 0 /*fAlignMaskAndCtl*/, kIemNativeEmitMemOp_Fetch_Sx_U32, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU8_Sx_U32, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_U8_SX_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u64Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(uint8_t), 0 /*fAlignMaskAndCtl*/, kIemNativeEmitMemOp_Fetch_Sx_U64, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU8_Sx_U64, pCallEntry->idxInstr)

/* 16-bit segmented: */
#define IEM_MC_FETCH_MEM_U16(a_u16Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u16Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(uint16_t), sizeof(uint16_t) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU16, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_U16_DISP(a_u16Dst, a_iSeg, a_GCPtrMem, a_offDisp) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u16Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(uint16_t), sizeof(uint16_t) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU16, pCallEntry->idxInstr, a_offDisp)

#define IEM_MC_FETCH_MEM_U16_ZX_U32(a_u32Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u32Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(uint16_t), sizeof(uint16_t) - 1, kIemNativeEmitMemOp_Fetch_Zx_U32, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU16, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_U16_ZX_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u64Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(uint16_t), sizeof(uint16_t) - 1, kIemNativeEmitMemOp_Fetch_Zx_U64, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU16, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_U16_SX_U32(a_u32Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u32Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(uint16_t), sizeof(uint16_t) - 1, kIemNativeEmitMemOp_Fetch_Sx_U32, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU16_Sx_U32, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_U16_SX_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u64Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(uint16_t), sizeof(uint16_t) - 1, kIemNativeEmitMemOp_Fetch_Sx_U64, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU16_Sx_U64, pCallEntry->idxInstr)


/* 32-bit segmented: */
#define IEM_MC_FETCH_MEM_U32(a_u32Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u32Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(uint32_t), sizeof(uint32_t) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU32, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_U32_DISP(a_u32Dst, a_iSeg, a_GCPtrMem, a_offDisp) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u32Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(uint32_t), sizeof(uint32_t) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU32, pCallEntry->idxInstr, a_offDisp)

#define IEM_MC_FETCH_MEM_U32_ZX_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u64Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(uint32_t), sizeof(uint32_t) - 1, kIemNativeEmitMemOp_Fetch_Zx_U64, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU32, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_U32_SX_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u64Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(uint32_t), sizeof(uint32_t) - 1, kIemNativeEmitMemOp_Fetch_Sx_U64, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU32_Sx_U64, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_I16(a_i16Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_i16Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(int16_t), sizeof(int16_t) - 1, kIemNativeEmitMemOp_Fetch_Sx_U32, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU16_Sx_U32, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_I16_DISP(a_i16Dst, a_iSeg, a_GCPtrMem, a_offDisp) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_i16Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(int16_t), sizeof(int16_t) - 1, kIemNativeEmitMemOp_Fetch_Sx_U32, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU16_Sx_U32, pCallEntry->idxInstr, a_offDisp)

#define IEM_MC_FETCH_MEM_I32(a_i32Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_i32Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(int32_t), sizeof(int32_t) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU32, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_I32_DISP(a_i32Dst, a_iSeg, a_GCPtrMem, a_offDisp) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_i32Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(int32_t), sizeof(int32_t) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU32, pCallEntry->idxInstr, a_offDisp)

#define IEM_MC_FETCH_MEM_I64(a_i64Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_i64Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(int64_t), sizeof(int64_t) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU64, pCallEntry->idxInstr)

AssertCompileSize(RTFLOAT32U, sizeof(uint32_t));
#define IEM_MC_FETCH_MEM_R32(a_r32Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_r32Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(RTFLOAT32U), sizeof(RTFLOAT32U) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU32, pCallEntry->idxInstr)


/* 64-bit segmented: */
#define IEM_MC_FETCH_MEM_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u64Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(uint64_t), sizeof(uint64_t) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU64, pCallEntry->idxInstr)

AssertCompileSize(RTFLOAT64U, sizeof(uint64_t));
#define IEM_MC_FETCH_MEM_R64(a_r64Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_r64Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(RTFLOAT64U), sizeof(RTFLOAT64U) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU64, pCallEntry->idxInstr)


/* 8-bit flat: */
#define IEM_MC_FETCH_MEM_FLAT_U8(a_u8Dst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u8Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(uint8_t), 0 /*fAlignMaskAndCtl*/, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU8, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_FLAT_U8_ZX_U16(a_u16Dst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u16Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(uint8_t), 0 /*fAlignMaskAndCtl*/, kIemNativeEmitMemOp_Fetch_Zx_U16, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU8, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_FLAT_U8_ZX_U32(a_u32Dst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u32Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(uint8_t), 0 /*fAlignMaskAndCtl*/, kIemNativeEmitMemOp_Fetch_Zx_U32, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU8, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_FLAT_U8_ZX_U64(a_u64Dst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u64Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(uint8_t), 0 /*fAlignMaskAndCtl*/, kIemNativeEmitMemOp_Fetch_Zx_U64, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU8, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_FLAT_U8_SX_U16(a_u16Dst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u16Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(uint8_t), 0 /*fAlignMaskAndCtl*/, kIemNativeEmitMemOp_Fetch_Sx_U16, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU8_Sx_U16, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_FLAT_U8_SX_U32(a_u32Dst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u32Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(uint8_t), 0 /*fAlignMaskAndCtl*/, kIemNativeEmitMemOp_Fetch_Sx_U32, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU8_Sx_U32, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_FLAT_U8_SX_U64(a_u64Dst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u64Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(uint8_t), 0 /*fAlignMaskAndCtl*/, kIemNativeEmitMemOp_Fetch_Sx_U64, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU8_Sx_U64, pCallEntry->idxInstr)


/* 16-bit flat: */
#define IEM_MC_FETCH_MEM_FLAT_U16(a_u16Dst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u16Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(uint16_t), sizeof(uint16_t) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU16, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_FLAT_U16_DISP(a_u16Dst, a_GCPtrMem, a_offDisp) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u16Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(uint16_t), sizeof(uint16_t) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU16, pCallEntry->idxInstr, a_offDisp)

#define IEM_MC_FETCH_MEM_FLAT_U16_ZX_U32(a_u32Dst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u32Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(uint16_t), sizeof(uint16_t) - 1, kIemNativeEmitMemOp_Fetch_Zx_U32, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU16, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_FLAT_U16_ZX_U64(a_u64Dst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u64Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(uint16_t), sizeof(uint16_t) - 1, kIemNativeEmitMemOp_Fetch_Zx_U64, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU16, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_FLAT_U16_SX_U32(a_u32Dst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u32Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(uint16_t), sizeof(uint16_t) - 1, kIemNativeEmitMemOp_Fetch_Sx_U32, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU16_Sx_U32, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_FLAT_U16_SX_U64(a_u64Dst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u64Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(uint16_t), sizeof(uint16_t) - 1, kIemNativeEmitMemOp_Fetch_Sx_U64, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU16_Sx_U64, pCallEntry->idxInstr)

/* 32-bit flat: */
#define IEM_MC_FETCH_MEM_FLAT_U32(a_u32Dst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u32Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(uint32_t), sizeof(uint32_t) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU32, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_FLAT_U32_DISP(a_u32Dst, a_GCPtrMem, a_offDisp) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u32Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(uint32_t), sizeof(uint32_t) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU32, pCallEntry->idxInstr, a_offDisp)

#define IEM_MC_FETCH_MEM_FLAT_U32_ZX_U64(a_u64Dst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u64Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(uint32_t), sizeof(uint32_t) - 1, kIemNativeEmitMemOp_Fetch_Zx_U64, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU32, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_FLAT_U32_SX_U64(a_u64Dst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u64Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(uint32_t), sizeof(uint32_t) - 1, kIemNativeEmitMemOp_Fetch_Sx_U64, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU32_Sx_U64, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_FLAT_I16(a_i16Dst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_i16Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(int16_t), sizeof(int16_t) - 1, kIemNativeEmitMemOp_Fetch_Sx_U32, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU16_Sx_U32, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_FLAT_I16_DISP(a_i16Dst, a_GCPtrMem, a_offDisp) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_i16Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(int16_t), sizeof(int16_t) - 1, kIemNativeEmitMemOp_Fetch_Sx_U32, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU16_Sx_U32, pCallEntry->idxInstr, a_offDisp)

#define IEM_MC_FETCH_MEM_FLAT_I32(a_i32Dst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_i32Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(int32_t), sizeof(int32_t) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU32, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_FLAT_I32_DISP(a_i32Dst, a_GCPtrMem, a_offDisp) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_i32Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(int32_t), sizeof(int32_t) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU32, pCallEntry->idxInstr, a_offDisp)

#define IEM_MC_FETCH_MEM_FLAT_I64(a_i64Dst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_i64Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(int64_t), sizeof(int64_t) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU64, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_FLAT_R32(a_r32Dst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_r32Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(RTFLOAT32U), sizeof(RTFLOAT32U) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU32, pCallEntry->idxInstr)


/* 64-bit flat: */
#define IEM_MC_FETCH_MEM_FLAT_U64(a_u64Dst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u64Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(uint64_t), sizeof(uint64_t) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU64, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_FLAT_R64(a_r64Dst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_r64Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(RTFLOAT64U), sizeof(RTFLOAT64U) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU64, pCallEntry->idxInstr)

#ifdef IEMNATIVE_WITH_SIMD_REG_ALLOCATOR
/* 128-bit segmented: */
#define IEM_MC_FETCH_MEM_U128(a_u128Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u128Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(RTUINT128U), sizeof(RTUINT128U) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU128, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_U128_ALIGN_SSE(a_u128Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u128Dst, a_iSeg, a_GCPtrMem, sizeof(RTUINT128U), \
                                               (sizeof(RTUINT128U) - 1U) | IEM_MEMMAP_F_ALIGN_GP | IEM_MEMMAP_F_ALIGN_SSE, \
                                               kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU128AlignedSse, pCallEntry->idxInstr)

AssertCompileSize(X86XMMREG, sizeof(RTUINT128U));
#define IEM_MC_FETCH_MEM_XMM_ALIGN_SSE(a_uXmmDst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_uXmmDst, a_iSeg, a_GCPtrMem, sizeof(X86XMMREG), \
                                               (sizeof(X86XMMREG) - 1U) | IEM_MEMMAP_F_ALIGN_GP | IEM_MEMMAP_F_ALIGN_SSE, \
                                               kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU128AlignedSse, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_U128_NO_AC(a_u128Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u128Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(RTUINT128U), sizeof(RTUINT128U) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU128NoAc, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_XMM_NO_AC(a_u128Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u128Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(X86XMMREG), sizeof(X86XMMREG) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU128NoAc, pCallEntry->idxInstr)


/* 128-bit flat: */
#define IEM_MC_FETCH_MEM_FLAT_U128(a_u128Dst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u128Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(RTUINT128U), sizeof(RTUINT128U) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU128, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_FLAT_U128_ALIGN_SSE(a_u128Dst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u128Dst, UINT8_MAX, a_GCPtrMem, sizeof(RTUINT128U), \
                                               (sizeof(RTUINT128U) - 1U) | IEM_MEMMAP_F_ALIGN_GP | IEM_MEMMAP_F_ALIGN_SSE, \
                                               kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU128AlignedSse, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_FLAT_XMM_ALIGN_SSE(a_uXmmDst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_uXmmDst, UINT8_MAX, a_GCPtrMem, sizeof(X86XMMREG), \
                                               (sizeof(X86XMMREG) - 1U) | IEM_MEMMAP_F_ALIGN_GP | IEM_MEMMAP_F_ALIGN_SSE, \
                                               kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU128AlignedSse, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_FLAT_U128_NO_AC(a_u128Dst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u128Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(RTUINT128U), sizeof(RTUINT128U) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU128NoAc, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_FLAT_XMM_NO_AC(a_uXmmDst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_uXmmDst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(X86XMMREG), sizeof(X86XMMREG) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU128NoAc, pCallEntry->idxInstr)

/* 256-bit segmented: */
#define IEM_MC_FETCH_MEM_U256(a_u256Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u256Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(RTUINT256U), sizeof(RTUINT256U) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU256NoAc, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_U256_NO_AC(a_u256Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u256Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(RTUINT256U), sizeof(RTUINT256U) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU256NoAc, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_U256_ALIGN_AVX(a_u256Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u256Dst, a_iSeg, a_GCPtrMem, sizeof(RTUINT256U), \
                                               (sizeof(RTUINT256U) - 1U) | IEM_MEMMAP_F_ALIGN_GP, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU256AlignedAvx, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_YMM_NO_AC(a_u256Dst, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u256Dst, a_iSeg, a_GCPtrMem, \
                                               sizeof(X86YMMREG), sizeof(X86YMMREG) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFetchDataU256NoAc, pCallEntry->idxInstr)


/* 256-bit flat: */
#define IEM_MC_FETCH_MEM_FLAT_U256(a_u256Dst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u256Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(RTUINT256U), sizeof(RTUINT256U) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU256NoAc, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_FLAT_U256_NO_AC(a_u256Dst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u256Dst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(RTUINT256U), sizeof(RTUINT256U) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU256NoAc, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_FLAT_U256_ALIGN_AVX(a_u256Dst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u256Dst, UINT8_MAX, a_GCPtrMem, sizeof(RTUINT256U), \
                                               (sizeof(RTUINT256U) - 1U) | IEM_MEMMAP_F_ALIGN_GP, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU256AlignedAvx, pCallEntry->idxInstr)

#define IEM_MC_FETCH_MEM_FLAT_YMM_NO_AC(a_uYmmDst, a_GCPtrMem) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_uYmmDst, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(X86YMMREG), sizeof(X86YMMREG) - 1, kIemNativeEmitMemOp_Fetch, \
                                               (uintptr_t)iemNativeHlpMemFlatFetchDataU256NoAc, pCallEntry->idxInstr)

#endif


/*********************************************************************************************************************************
*   Memory stores (IEM_MEM_STORE_XXX).                                                                                           *
*********************************************************************************************************************************/

#define IEM_MC_STORE_MEM_U8(a_iSeg, a_GCPtrMem, a_u8Value) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u8Value, a_iSeg, a_GCPtrMem, \
                                               sizeof(uint8_t), 0 /*fAlignMaskAndCtl*/, kIemNativeEmitMemOp_Store, \
                                               (uintptr_t)iemNativeHlpMemStoreDataU8, pCallEntry->idxInstr)

#define IEM_MC_STORE_MEM_U16(a_iSeg, a_GCPtrMem, a_u16Value) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u16Value, a_iSeg, a_GCPtrMem, \
                                               sizeof(uint16_t), sizeof(uint16_t) - 1, kIemNativeEmitMemOp_Store, \
                                               (uintptr_t)iemNativeHlpMemStoreDataU16, pCallEntry->idxInstr)

#define IEM_MC_STORE_MEM_U32(a_iSeg, a_GCPtrMem, a_u32Value) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u32Value, a_iSeg, a_GCPtrMem, \
                                               sizeof(uint32_t), sizeof(uint32_t) - 1, kIemNativeEmitMemOp_Store, \
                                               (uintptr_t)iemNativeHlpMemStoreDataU32, pCallEntry->idxInstr)

#define IEM_MC_STORE_MEM_U64(a_iSeg, a_GCPtrMem, a_u64Value) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u64Value, a_iSeg, a_GCPtrMem, \
                                               sizeof(uint64_t), sizeof(uint64_t) - 1, kIemNativeEmitMemOp_Store, \
                                               (uintptr_t)iemNativeHlpMemStoreDataU64, pCallEntry->idxInstr)


#define IEM_MC_STORE_MEM_FLAT_U8(a_GCPtrMem, a_u8Value) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u8Value, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(uint8_t), 0 /*fAlignMaskAndCtl*/, kIemNativeEmitMemOp_Store, \
                                               (uintptr_t)iemNativeHlpMemFlatStoreDataU8, pCallEntry->idxInstr)

#define IEM_MC_STORE_MEM_FLAT_U16(a_GCPtrMem, a_u16Value) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u16Value, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(uint16_t), sizeof(uint16_t) - 1, kIemNativeEmitMemOp_Store, \
                                               (uintptr_t)iemNativeHlpMemFlatStoreDataU16, pCallEntry->idxInstr)

#define IEM_MC_STORE_MEM_FLAT_U32(a_GCPtrMem, a_u32Value) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u32Value, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(uint32_t), sizeof(uint32_t) - 1, kIemNativeEmitMemOp_Store, \
                                               (uintptr_t)iemNativeHlpMemFlatStoreDataU32, pCallEntry->idxInstr)

#define IEM_MC_STORE_MEM_FLAT_U64(a_GCPtrMem, a_u64Value) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u64Value, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(uint64_t), sizeof(uint64_t) - 1, kIemNativeEmitMemOp_Store, \
                                               (uintptr_t)iemNativeHlpMemFlatStoreDataU64, pCallEntry->idxInstr)


#define IEM_MC_STORE_MEM_U8_CONST(a_iSeg, a_GCPtrMem, a_u8ConstValue) \
    off = iemNativeEmitMemStoreConstDataCommon(pReNative, off, a_u8ConstValue, a_iSeg, a_GCPtrMem, sizeof(uint8_t), \
                                               (uintptr_t)iemNativeHlpMemStoreDataU8, pCallEntry->idxInstr)

#define IEM_MC_STORE_MEM_U16_CONST(a_iSeg, a_GCPtrMem, a_u16ConstValue) \
    off = iemNativeEmitMemStoreConstDataCommon(pReNative, off, a_u16ConstValue, a_iSeg, a_GCPtrMem, sizeof(uint16_t), \
                                               (uintptr_t)iemNativeHlpMemStoreDataU16, pCallEntry->idxInstr)

#define IEM_MC_STORE_MEM_U32_CONST(a_iSeg, a_GCPtrMem, a_u32ConstValue) \
    off = iemNativeEmitMemStoreConstDataCommon(pReNative, off, a_u32ConstValue, a_iSeg, a_GCPtrMem, sizeof(uint32_t), \
                                               (uintptr_t)iemNativeHlpMemStoreDataU32, pCallEntry->idxInstr)

#define IEM_MC_STORE_MEM_U64_CONST(a_iSeg, a_GCPtrMem, a_u64ConstValue) \
    off = iemNativeEmitMemStoreConstDataCommon(pReNative, off, a_u64ConstValue, a_iSeg, a_GCPtrMem, sizeof(uint64_t), \
                                               (uintptr_t)iemNativeHlpMemStoreDataU64, pCallEntry->idxInstr)


#define IEM_MC_STORE_MEM_FLAT_U8_CONST(a_GCPtrMem, a_u8ConstValue) \
    off = iemNativeEmitMemStoreConstDataCommon(pReNative, off, a_u8ConstValue, UINT8_MAX, a_GCPtrMem, sizeof(uint8_t), \
                                               (uintptr_t)iemNativeHlpMemFlatStoreDataU8, pCallEntry->idxInstr)

#define IEM_MC_STORE_MEM_FLAT_U16_CONST(a_GCPtrMem, a_u16ConstValue) \
    off = iemNativeEmitMemStoreConstDataCommon(pReNative, off, a_u16ConstValue, UINT8_MAX, a_GCPtrMem, sizeof(uint16_t), \
                                               (uintptr_t)iemNativeHlpMemFlatStoreDataU16, pCallEntry->idxInstr)

#define IEM_MC_STORE_MEM_FLAT_U32_CONST(a_GCPtrMem, a_u32ConstValue) \
    off = iemNativeEmitMemStoreConstDataCommon(pReNative, off, a_u32ConstValue, UINT8_MAX, a_GCPtrMem, sizeof(uint32_t), \
                                               (uintptr_t)iemNativeHlpMemFlatStoreDataU32, pCallEntry->idxInstr)

#define IEM_MC_STORE_MEM_FLAT_U64_CONST(a_GCPtrMem, a_u64ConstValue) \
    off = iemNativeEmitMemStoreConstDataCommon(pReNative, off, a_u64ConstValue, UINT8_MAX, a_GCPtrMem, sizeof(uint64_t), \
                                               (uintptr_t)iemNativeHlpMemFlatStoreDataU64, pCallEntry->idxInstr)

/** Emits code for IEM_MC_STORE_MEM_U8/16/32/64_CONST and
 *  IEM_MC_STORE_MEM_FLAT_U8/16/32/64_CONST (with iSegReg = UINT8_MAX). */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitMemStoreConstDataCommon(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint64_t uValueConst, uint8_t iSegReg,
                                    uint8_t idxVarGCPtrMem, uint8_t cbMem, uintptr_t pfnFunction, uint8_t idxInstr)
{
    /*
     * Create a temporary const variable and call iemNativeEmitMemFetchStoreDataCommon
     * to do the grunt work.
     */
    uint8_t const idxVarConstValue = iemNativeVarAllocConst(pReNative, cbMem, uValueConst);
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, idxVarConstValue, iSegReg, idxVarGCPtrMem,
                                               cbMem, cbMem - 1, kIemNativeEmitMemOp_Store,
                                               pfnFunction, idxInstr);
    iemNativeVarFreeLocal(pReNative, idxVarConstValue);
    return off;
}


#ifdef IEMNATIVE_WITH_SIMD_REG_ALLOCATOR
# define IEM_MC_STORE_MEM_U128_ALIGN_SSE(a_iSeg, a_GCPtrMem, a_u128Value) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u128Value, a_iSeg, a_GCPtrMem, sizeof(RTUINT128U), \
                                               (sizeof(RTUINT128U) - 1U) | IEM_MEMMAP_F_ALIGN_GP | IEM_MEMMAP_F_ALIGN_SSE, \
                                               kIemNativeEmitMemOp_Store, \
                                               (uintptr_t)iemNativeHlpMemStoreDataU128AlignedSse, pCallEntry->idxInstr)

# define IEM_MC_STORE_MEM_U128_NO_AC(a_iSeg, a_GCPtrMem, a_u128Value) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u128Value, a_iSeg, a_GCPtrMem, \
                                               sizeof(RTUINT128U), sizeof(RTUINT128U) - 1, kIemNativeEmitMemOp_Store, \
                                               (uintptr_t)iemNativeHlpMemStoreDataU128NoAc, pCallEntry->idxInstr)

# define IEM_MC_STORE_MEM_U256_NO_AC(a_iSeg, a_GCPtrMem, a_u256Value) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u256Value, a_iSeg, a_GCPtrMem, \
                                               sizeof(RTUINT256U), sizeof(RTUINT256U) - 1, kIemNativeEmitMemOp_Store, \
                                               (uintptr_t)iemNativeHlpMemStoreDataU256NoAc, pCallEntry->idxInstr)

# define IEM_MC_STORE_MEM_U256_ALIGN_AVX(a_iSeg, a_GCPtrMem, a_u256Value) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u256Value, a_iSeg, a_GCPtrMem, sizeof(RTUINT256U), \
                                               (sizeof(RTUINT256U) - 1U) | IEM_MEMMAP_F_ALIGN_GP, kIemNativeEmitMemOp_Store, \
                                               (uintptr_t)iemNativeHlpMemStoreDataU256AlignedAvx, pCallEntry->idxInstr)


# define IEM_MC_STORE_MEM_FLAT_U128_ALIGN_SSE(a_GCPtrMem, a_u128Value) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u128Value, UINT8_MAX, a_GCPtrMem, sizeof(RTUINT128U), \
                                               (sizeof(RTUINT128U) - 1U) | IEM_MEMMAP_F_ALIGN_GP | IEM_MEMMAP_F_ALIGN_SSE, \
                                               kIemNativeEmitMemOp_Store, \
                                               (uintptr_t)iemNativeHlpMemFlatStoreDataU128AlignedSse, pCallEntry->idxInstr)

# define IEM_MC_STORE_MEM_FLAT_U128_NO_AC(a_GCPtrMem, a_u128Value) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u128Value, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(RTUINT128U), sizeof(RTUINT128U) - 1, kIemNativeEmitMemOp_Store, \
                                               (uintptr_t)iemNativeHlpMemFlatStoreDataU128NoAc, pCallEntry->idxInstr)

# define IEM_MC_STORE_MEM_FLAT_U256_NO_AC(a_GCPtrMem, a_u256Value) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u256Value, UINT8_MAX, a_GCPtrMem, \
                                               sizeof(RTUINT256U), sizeof(RTUINT256U) - 1, kIemNativeEmitMemOp_Store, \
                                               (uintptr_t)iemNativeHlpMemFlatStoreDataU256NoAc, pCallEntry->idxInstr)

# define IEM_MC_STORE_MEM_FLAT_U256_ALIGN_AVX(a_GCPtrMem, a_u256Value) \
    off = iemNativeEmitMemFetchStoreDataCommon(pReNative, off, a_u256Value, UINT8_MAX, a_GCPtrMem, sizeof(RTUINT256U), \
                                               (sizeof(RTUINT256U) - 1U) | IEM_MEMMAP_F_ALIGN_GP, kIemNativeEmitMemOp_Store, \
                                               (uintptr_t)iemNativeHlpMemFlatStoreDataU256AlignedAvx, pCallEntry->idxInstr)
#endif



/*********************************************************************************************************************************
*   Stack Accesses.                                                                                                              *
*********************************************************************************************************************************/
/*                                                     RT_MAKE_U32_FROM_U8(cBitsVar, cBitsFlat, fSReg, 0) */
#define IEM_MC_PUSH_U16(a_u16Value) \
    off = iemNativeEmitStackPush(pReNative, off, a_u16Value, RT_MAKE_U32_FROM_U8(16,  0, 0, 0), \
                                 (uintptr_t)iemNativeHlpStackStoreU16, pCallEntry->idxInstr)
#define IEM_MC_PUSH_U32(a_u32Value) \
    off = iemNativeEmitStackPush(pReNative, off, a_u32Value, RT_MAKE_U32_FROM_U8(32,  0, 0, 0), \
                                 (uintptr_t)iemNativeHlpStackStoreU32, pCallEntry->idxInstr)
#define IEM_MC_PUSH_U32_SREG(a_uSegVal) \
    off = iemNativeEmitStackPush(pReNative, off, a_uSegVal,  RT_MAKE_U32_FROM_U8(32,  0, 1, 0), \
                                 (uintptr_t)iemNativeHlpStackStoreU32SReg, pCallEntry->idxInstr)
#define IEM_MC_PUSH_U64(a_u64Value) \
    off = iemNativeEmitStackPush(pReNative, off, a_u64Value, RT_MAKE_U32_FROM_U8(64,  0, 0, 0), \
                                 (uintptr_t)iemNativeHlpStackStoreU64, pCallEntry->idxInstr)

#define IEM_MC_FLAT32_PUSH_U16(a_u16Value) \
    off = iemNativeEmitStackPush(pReNative, off, a_u16Value, RT_MAKE_U32_FROM_U8(16, 32, 0, 0), \
                                 (uintptr_t)iemNativeHlpStackFlatStoreU16, pCallEntry->idxInstr)
#define IEM_MC_FLAT32_PUSH_U32(a_u32Value) \
    off = iemNativeEmitStackPush(pReNative, off, a_u32Value, RT_MAKE_U32_FROM_U8(32, 32, 0, 0), \
                                 (uintptr_t)iemNativeHlpStackFlatStoreU32, pCallEntry->idxInstr)
#define IEM_MC_FLAT32_PUSH_U32_SREG(a_u32Value) \
    off = iemNativeEmitStackPush(pReNative, off, a_u32Value, RT_MAKE_U32_FROM_U8(32, 32, 1, 0), \
                                 (uintptr_t)iemNativeHlpStackFlatStoreU32SReg, pCallEntry->idxInstr)

#define IEM_MC_FLAT64_PUSH_U16(a_u16Value) \
    off = iemNativeEmitStackPush(pReNative, off, a_u16Value, RT_MAKE_U32_FROM_U8(16, 64, 0, 0), \
                                 (uintptr_t)iemNativeHlpStackFlatStoreU16, pCallEntry->idxInstr)
#define IEM_MC_FLAT64_PUSH_U64(a_u64Value) \
    off = iemNativeEmitStackPush(pReNative, off, a_u64Value, RT_MAKE_U32_FROM_U8(64, 64, 0, 0), \
                                 (uintptr_t)iemNativeHlpStackFlatStoreU64, pCallEntry->idxInstr)


/** IEM_MC[|_FLAT32|_FLAT64]_PUSH_U16/32/32_SREG/64 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitStackPush(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarValue,
                       uint32_t cBitsVarAndFlat, uintptr_t pfnFunction, uint8_t idxInstr)
{
    /*
     * Assert sanity.
     */
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVarValue);
    PIEMNATIVEVAR const pVarValue = &pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVarValue)];
#ifdef VBOX_STRICT
    if (RT_BYTE2(cBitsVarAndFlat) != 0)
    {
        Assert(   (pReNative->fExec & IEM_F_MODE_MASK) == IEM_F_MODE_X86_64BIT
               || (pReNative->fExec & IEM_F_MODE_MASK) == IEM_F_MODE_X86_32BIT_PROT_FLAT
               || (pReNative->fExec & IEM_F_MODE_MASK) == IEM_F_MODE_X86_32BIT_FLAT);
        Assert(   pfnFunction
               == (  cBitsVarAndFlat == RT_MAKE_U32_FROM_U8(16, 32, 0, 0) ? (uintptr_t)iemNativeHlpStackFlatStoreU16
                   : cBitsVarAndFlat == RT_MAKE_U32_FROM_U8(32, 32, 0, 0) ? (uintptr_t)iemNativeHlpStackFlatStoreU32
                   : cBitsVarAndFlat == RT_MAKE_U32_FROM_U8(32, 32, 1, 0) ? (uintptr_t)iemNativeHlpStackFlatStoreU32SReg
                   : cBitsVarAndFlat == RT_MAKE_U32_FROM_U8(16, 64, 0, 0) ? (uintptr_t)iemNativeHlpStackFlatStoreU16
                   : cBitsVarAndFlat == RT_MAKE_U32_FROM_U8(64, 64, 0, 0) ? (uintptr_t)iemNativeHlpStackFlatStoreU64
                   : UINT64_C(0xc000b000a0009000) ));
    }
    else
        Assert(   pfnFunction
               == (  cBitsVarAndFlat == RT_MAKE_U32_FROM_U8(16, 0, 0, 0) ? (uintptr_t)iemNativeHlpStackStoreU16
                   : cBitsVarAndFlat == RT_MAKE_U32_FROM_U8(32, 0, 0, 0) ? (uintptr_t)iemNativeHlpStackStoreU32
                   : cBitsVarAndFlat == RT_MAKE_U32_FROM_U8(32, 0, 1, 0) ? (uintptr_t)iemNativeHlpStackStoreU32SReg
                   : cBitsVarAndFlat == RT_MAKE_U32_FROM_U8(64, 0, 0, 0) ? (uintptr_t)iemNativeHlpStackStoreU64
                   : UINT64_C(0xc000b000a0009000) ));
#endif

#ifdef VBOX_STRICT
    /*
     * Check that the fExec flags we've got make sense.
     */
    off = iemNativeEmitExecFlagsCheck(pReNative, off, pReNative->fExec);
#endif

    /*
     * To keep things simple we have to commit any pending writes first as we
     * may end up making calls.
     */
    /** @todo we could postpone this till we make the call and reload the
     * registers after returning from the call. Not sure if that's sensible or
     * not, though. */
    off = iemNativeRegFlushPendingWrites(pReNative, off);

    /*
     * First we calculate the new RSP and the effective stack pointer value.
     * For 64-bit mode and flat 32-bit these two are the same.
     * (Code structure is very similar to that of PUSH)
     */
    uint8_t const cbMem       = RT_BYTE1(cBitsVarAndFlat) / 8;
    bool const    fIsSegReg   = RT_BYTE3(cBitsVarAndFlat) != 0;
    bool const    fIsIntelSeg = fIsSegReg && IEM_IS_GUEST_CPU_INTEL(pReNative->pVCpu);
    uint8_t const cbMemAccess = !fIsIntelSeg || (pReNative->fExec & IEM_F_MODE_MASK) == IEM_F_MODE_X86_16BIT
                              ? cbMem : sizeof(uint16_t);
    uint8_t const cBitsFlat   = RT_BYTE2(cBitsVarAndFlat);      RT_NOREF(cBitsFlat);
    uint8_t const idxRegRsp   = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(X86_GREG_xSP),
                                                                kIemNativeGstRegUse_ForUpdate, true /*fNoVolatileRegs*/);
    uint8_t const idxRegEffSp = cBitsFlat != 0 ? idxRegRsp : iemNativeRegAllocTmp(pReNative, &off);
    uint32_t      offFixupJumpToUseOtherBitSp = UINT32_MAX;
    if (cBitsFlat != 0)
    {
        Assert(idxRegEffSp == idxRegRsp);
        Assert(cBitsFlat == 32 || cBitsFlat == 64);
        Assert(IEM_F_MODE_X86_IS_FLAT(pReNative->fExec));
        if (cBitsFlat == 64)
            off = iemNativeEmitSubGprImm(pReNative, off, idxRegRsp, cbMem);
        else
            off = iemNativeEmitSubGpr32Imm(pReNative, off, idxRegRsp, cbMem);
    }
    else /** @todo We can skip the test if we're targeting pre-386 CPUs. */
    {
        Assert(idxRegEffSp != idxRegRsp);
        uint8_t const idxRegSsAttr = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_SEG_ATTRIB(X86_SREG_SS),
                                                                     kIemNativeGstRegUse_ReadOnly);
#ifdef RT_ARCH_AMD64
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 32);
#else
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 10);
#endif
        off = iemNativeEmitTestAnyBitsInGpr32Ex(pCodeBuf, off, idxRegSsAttr, X86DESCATTR_D);
        iemNativeRegFreeTmp(pReNative, idxRegSsAttr);
        offFixupJumpToUseOtherBitSp = off;
        if ((pReNative->fExec & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_32BIT)
        {
            off = iemNativeEmitJccToFixedEx(pCodeBuf, off, off /*8-bit suffices*/, kIemNativeInstrCond_e); /* jump if zero */
            off = iemNativeEmitStackPushUse32Sp(pCodeBuf, off, idxRegRsp, idxRegEffSp, cbMem);
        }
        else
        {
            off = iemNativeEmitJccToFixedEx(pCodeBuf, off, off /*8-bit suffices*/, kIemNativeInstrCond_ne); /* jump if not zero */
            off = iemNativeEmitStackPushUse16Sp(pCodeBuf, off, idxRegRsp, idxRegEffSp, cbMem);
        }
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    }
    /* SpUpdateEnd: */
    uint32_t const offLabelSpUpdateEnd = off;

    /*
     * Okay, now prepare for TLB lookup and jump to code (or the TlbMiss if
     * we're skipping lookup).
     */
    uint8_t const  iSegReg           = cBitsFlat != 0 ? UINT8_MAX : X86_SREG_SS;
    IEMNATIVEEMITTLBSTATE const TlbState(pReNative, idxRegEffSp, &off, iSegReg, cbMemAccess);
    uint16_t const uTlbSeqNo         = pReNative->uTlbSeqNo++;
    uint32_t const idxLabelTlbMiss   = iemNativeLabelCreate(pReNative, kIemNativeLabelType_TlbMiss, UINT32_MAX, uTlbSeqNo);
    uint32_t const idxLabelTlbLookup = !TlbState.fSkip
                                     ? iemNativeLabelCreate(pReNative, kIemNativeLabelType_TlbLookup, UINT32_MAX, uTlbSeqNo)
                                     : UINT32_MAX;
    uint8_t const  idxRegValue       =    !TlbState.fSkip
                                       && pVarValue->enmKind != kIemNativeVarKind_Immediate
                                     ? iemNativeVarRegisterAcquire(pReNative, idxVarValue, &off, true /*fInitialized*/,
                                                                   IEMNATIVE_CALL_ARG2_GREG /*idxRegPref*/)
                                     : UINT8_MAX;
    uint8_t const  idxRegMemResult   = !TlbState.fSkip ? iemNativeRegAllocTmp(pReNative, &off) : UINT8_MAX;


    if (!TlbState.fSkip)
        off = iemNativeEmitJmpToLabel(pReNative, off, idxLabelTlbLookup); /** @todo short jump */
    else
        off = iemNativeEmitJmpToLabel(pReNative, off, idxLabelTlbMiss); /** @todo short jump */

    /*
     * Use16BitSp:
     */
    if (cBitsFlat == 0)
    {
#ifdef RT_ARCH_AMD64
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 32);
#else
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 10);
#endif
        iemNativeFixupFixedJump(pReNative, offFixupJumpToUseOtherBitSp, off);
        if ((pReNative->fExec & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_32BIT)
            off = iemNativeEmitStackPushUse16Sp(pCodeBuf, off, idxRegRsp, idxRegEffSp, cbMem);
        else
            off = iemNativeEmitStackPushUse32Sp(pCodeBuf, off, idxRegRsp, idxRegEffSp, cbMem);
        off = iemNativeEmitJmpToFixedEx(pCodeBuf, off, offLabelSpUpdateEnd);
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    }

    /*
     * TlbMiss:
     *
     * Call helper to do the pushing.
     */
    iemNativeLabelDefine(pReNative, idxLabelTlbMiss, off);

#ifdef IEMNATIVE_WITH_INSTRUCTION_COUNTING
    off = iemNativeEmitStoreImmToVCpuU8(pReNative, off, idxInstr, RT_UOFFSETOF(VMCPUCC, iem.s.idxTbCurInstr));
#else
    RT_NOREF(idxInstr);
#endif

    /* Save variables in volatile registers. */
    uint32_t const fHstRegsNotToSave = TlbState.getRegsNotToSave()
                                     | (idxRegMemResult < RT_ELEMENTS(pReNative->Core.aHstRegs) ? RT_BIT_32(idxRegMemResult) : 0)
                                     | (idxRegEffSp != idxRegRsp ? RT_BIT_32(idxRegEffSp) : 0)
                                     | (idxRegValue < RT_ELEMENTS(pReNative->Core.aHstRegs) ? RT_BIT_32(idxRegValue) : 0);
    off = iemNativeVarSaveVolatileRegsPreHlpCall(pReNative, off, fHstRegsNotToSave);

    if (   idxRegValue == IEMNATIVE_CALL_ARG1_GREG
        && idxRegEffSp == IEMNATIVE_CALL_ARG2_GREG)
    {
        /* Swap them using ARG0 as temp register: */
        off = iemNativeEmitLoadGprFromGpr(pReNative, off, IEMNATIVE_CALL_ARG0_GREG, IEMNATIVE_CALL_ARG1_GREG);
        off = iemNativeEmitLoadGprFromGpr(pReNative, off, IEMNATIVE_CALL_ARG1_GREG, IEMNATIVE_CALL_ARG2_GREG);
        off = iemNativeEmitLoadGprFromGpr(pReNative, off, IEMNATIVE_CALL_ARG2_GREG, IEMNATIVE_CALL_ARG0_GREG);
    }
    else if (idxRegEffSp != IEMNATIVE_CALL_ARG2_GREG)
    {
        /* IEMNATIVE_CALL_ARG2_GREG = idxVarValue (first!) */
        off = iemNativeEmitLoadArgGregFromImmOrStackVar(pReNative, off, IEMNATIVE_CALL_ARG2_GREG, idxVarValue,
                                                        0 /*offAddend*/, IEMNATIVE_CALL_VOLATILE_GREG_MASK);

        /* IEMNATIVE_CALL_ARG1_GREG = idxRegEffSp */
        if (idxRegEffSp != IEMNATIVE_CALL_ARG1_GREG)
            off = iemNativeEmitLoadGprFromGpr(pReNative, off, IEMNATIVE_CALL_ARG1_GREG, idxRegEffSp);
    }
    else
    {
        /* IEMNATIVE_CALL_ARG1_GREG = idxRegEffSp (first!) */
        off = iemNativeEmitLoadGprFromGpr(pReNative, off, IEMNATIVE_CALL_ARG1_GREG, idxRegEffSp);

        /* IEMNATIVE_CALL_ARG2_GREG = idxVarValue */
        off = iemNativeEmitLoadArgGregFromImmOrStackVar(pReNative, off, IEMNATIVE_CALL_ARG2_GREG, idxVarValue, 0 /*offAddend*/,
                                                        IEMNATIVE_CALL_VOLATILE_GREG_MASK & ~RT_BIT_32(IEMNATIVE_CALL_ARG1_GREG));
    }

#ifdef IEMNATIVE_WITH_EFLAGS_POSTPONING
    /* Do delayed EFLAGS calculations. */
    off = iemNativeDoPostponedEFlagsAtTlbMiss<  RT_BIT_32(IEMNATIVE_CALL_ARG1_GREG)
                                              | RT_BIT_32(IEMNATIVE_CALL_ARG2_GREG)>(pReNative, off, &TlbState, fHstRegsNotToSave);
#endif

    /* IEMNATIVE_CALL_ARG0_GREG = pVCpu */
    off = iemNativeEmitLoadGprFromGpr(pReNative, off, IEMNATIVE_CALL_ARG0_GREG, IEMNATIVE_REG_FIXED_PVMCPU);

    /* Done setting up parameters, make the call. */
    off = iemNativeEmitCallImm<true /*a_fSkipEflChecks*/>(pReNative, off, pfnFunction);

    /* Restore variables and guest shadow registers to volatile registers. */
    off = iemNativeVarRestoreVolatileRegsPostHlpCall(pReNative, off, fHstRegsNotToSave);
    off = iemNativeRegRestoreGuestShadowsInVolatileRegs(pReNative, off, TlbState.getActiveRegsWithShadows());

#ifdef IEMNATIVE_WITH_TLB_LOOKUP
    if (!TlbState.fSkip)
    {
        /* end of TlbMiss - Jump to the done label. */
        uint32_t const idxLabelTlbDone = iemNativeLabelCreate(pReNative, kIemNativeLabelType_TlbDone, UINT32_MAX, uTlbSeqNo);
        off = iemNativeEmitJmpToLabel(pReNative, off, idxLabelTlbDone);

        /*
         * TlbLookup:
         */
        off = iemNativeEmitTlbLookup<true>(pReNative, off, &TlbState, iSegReg, cbMemAccess, cbMemAccess - 1,
                                           IEM_ACCESS_TYPE_WRITE, idxLabelTlbLookup, idxLabelTlbMiss, idxRegMemResult);

        /*
         * Emit code to do the actual storing / fetching.
         */
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 64);
# ifdef IEM_WITH_TLB_STATISTICS
        off = iemNativeEmitIncStamCounterInVCpuEx(pCodeBuf, off, TlbState.idxReg1, TlbState.idxReg2,
                                                  RT_UOFFSETOF(VMCPUCC, iem.s.StatNativeTlbHitsForStack));
# endif
        if (idxRegValue != UINT8_MAX)
        {
            switch (cbMemAccess)
            {
                case 2:
                    off = iemNativeEmitStoreGpr16ByGprEx(pCodeBuf, off, idxRegValue, idxRegMemResult);
                    break;
                case 4:
                    if (!fIsIntelSeg)
                        off = iemNativeEmitStoreGpr32ByGprEx(pCodeBuf, off, idxRegValue, idxRegMemResult);
                    else
                    {
                        /* intel real mode segment push. 10890XE adds the 2nd of half EFLAGS to a
                           PUSH FS in real mode, so we have to try emulate that here.
                           We borrow the now unused idxReg1 from the TLB lookup code here. */
                        uint8_t idxRegEfl = iemNativeRegAllocTmpForGuestRegIfAlreadyPresent(pReNative, &off,
                                                                                            kIemNativeGstReg_EFlags);
                        if (idxRegEfl != UINT8_MAX)
                        {
#ifdef ARCH_AMD64
                            off = iemNativeEmitLoadGprFromGpr32(pReNative, off, TlbState.idxReg1, idxRegEfl);
                            off = iemNativeEmitAndGpr32ByImm(pReNative, off, TlbState.idxReg1,
                                                             UINT32_C(0xffff0000) & ~X86_EFL_RAZ_MASK);
#else
                            off = iemNativeEmitGpr32EqGprAndImmEx(iemNativeInstrBufEnsure(pReNative, off, 3),
                                                                  off, TlbState.idxReg1, idxRegEfl,
                                                                  UINT32_C(0xffff0000) & ~X86_EFL_RAZ_MASK);
#endif
                            iemNativeRegFreeTmp(pReNative, idxRegEfl);
                        }
                        else
                        {
                            off = iemNativeEmitLoadGprFromVCpuU32(pReNative, off, TlbState.idxReg1,
                                                                  RT_UOFFSETOF(VMCPUCC, cpum.GstCtx.eflags));
                            off = iemNativeEmitAndGpr32ByImm(pReNative, off, TlbState.idxReg1,
                                                             UINT32_C(0xffff0000) & ~X86_EFL_RAZ_MASK);
                        }
                        /* ASSUMES the upper half of idxRegValue is ZERO. */
                        off = iemNativeEmitOrGpr32ByGpr(pReNative, off, TlbState.idxReg1, idxRegValue);
                        off = iemNativeEmitStoreGpr32ByGprEx(pCodeBuf, off, TlbState.idxReg1, idxRegMemResult);
                    }
                    break;
                case 8:
                    off = iemNativeEmitStoreGpr64ByGprEx(pCodeBuf, off, idxRegValue, idxRegMemResult);
                    break;
                default:
                    AssertFailed();
            }
        }
        else
        {
            switch (cbMemAccess)
            {
                case 2:
                    off = iemNativeEmitStoreImm16ByGprEx(pCodeBuf, off, (uint16_t)pVarValue->u.uValue,
                                                         idxRegMemResult, TlbState.idxReg1);
                    break;
                case 4:
                    Assert(!fIsSegReg);
                    off = iemNativeEmitStoreImm32ByGprEx(pCodeBuf, off, (uint32_t)pVarValue->u.uValue,
                                                         idxRegMemResult, TlbState.idxReg1);
                    break;
                case 8:
                    off = iemNativeEmitStoreImm64ByGprEx(pCodeBuf, off, pVarValue->u.uValue, idxRegMemResult, TlbState.idxReg1);
                    break;
                default:
                    AssertFailed();
            }
        }

        iemNativeRegFreeTmp(pReNative, idxRegMemResult);
        TlbState.freeRegsAndReleaseVars(pReNative);

        /*
         * TlbDone:
         *
         * Commit the new RSP value.
         */
        iemNativeLabelDefine(pReNative, idxLabelTlbDone, off);
    }
#endif /* IEMNATIVE_WITH_TLB_LOOKUP */

#if !defined(IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK)
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxRegRsp, RT_UOFFSETOF_DYN(VMCPU, cpum.GstCtx.rsp));
#endif
    iemNativeRegFreeTmp(pReNative, idxRegRsp);
    if (idxRegEffSp != idxRegRsp)
        iemNativeRegFreeTmp(pReNative, idxRegEffSp);

    /* The value variable is implictly flushed. */
    if (idxRegValue != UINT8_MAX)
        iemNativeVarRegisterRelease(pReNative, idxVarValue);
    iemNativeVarFreeLocal(pReNative, idxVarValue);

    return off;
}



/*                                                     RT_MAKE_U32_FROM_U8(cBitsVar, cBitsFlat, 0, 0) */
#define IEM_MC_POP_GREG_U16(a_iGReg) \
    off = iemNativeEmitStackPopGReg(pReNative, off, a_iGReg, RT_MAKE_U32_FROM_U8(16,  0, 0, 0), \
                                    (uintptr_t)iemNativeHlpStackFetchU16, pCallEntry->idxInstr)
#define IEM_MC_POP_GREG_U32(a_iGReg) \
    off = iemNativeEmitStackPopGReg(pReNative, off, a_iGReg, RT_MAKE_U32_FROM_U8(32,  0, 0, 0), \
                                    (uintptr_t)iemNativeHlpStackFetchU32, pCallEntry->idxInstr)
#define IEM_MC_POP_GREG_U64(a_iGReg) \
    off = iemNativeEmitStackPopGReg(pReNative, off, a_iGReg, RT_MAKE_U32_FROM_U8(64,  0, 0, 0), \
                                    (uintptr_t)iemNativeHlpStackFetchU64, pCallEntry->idxInstr)

#define IEM_MC_FLAT32_POP_GREG_U16(a_iGReg) \
    off = iemNativeEmitStackPopGReg(pReNative, off, a_iGReg, RT_MAKE_U32_FROM_U8(16, 32, 0, 0), \
                                    (uintptr_t)iemNativeHlpStackFlatFetchU16, pCallEntry->idxInstr)
#define IEM_MC_FLAT32_POP_GREG_U32(a_iGReg) \
    off = iemNativeEmitStackPopGReg(pReNative, off, a_iGReg, RT_MAKE_U32_FROM_U8(32, 32, 0, 0), \
                                    (uintptr_t)iemNativeHlpStackFlatFetchU32, pCallEntry->idxInstr)

#define IEM_MC_FLAT64_POP_GREG_U16(a_iGReg) \
    off = iemNativeEmitStackPopGReg(pReNative, off, a_iGReg, RT_MAKE_U32_FROM_U8(16, 64, 0, 0), \
                                    (uintptr_t)iemNativeHlpStackFlatFetchU16, pCallEntry->idxInstr)
#define IEM_MC_FLAT64_POP_GREG_U64(a_iGReg) \
    off = iemNativeEmitStackPopGReg(pReNative, off, a_iGReg, RT_MAKE_U32_FROM_U8(64, 64, 0, 0), \
                                    (uintptr_t)iemNativeHlpStackFlatFetchU64, pCallEntry->idxInstr)


DECL_FORCE_INLINE_THROW(uint32_t)
iemNativeEmitStackPopUse16Sp(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t idxRegRsp, uint8_t idxRegEffSp, uint8_t cbMem,
                             uint8_t idxRegTmp)
{
    /* Use16BitSp: */
#ifdef RT_ARCH_AMD64
    off = iemNativeEmitLoadGprFromGpr16Ex(pCodeBuf, off, idxRegEffSp, idxRegRsp);
    off = iemNativeEmitAddGpr16ImmEx(pCodeBuf, off, idxRegRsp, cbMem); /* ASSUMES this does NOT modify bits [63:16]! */
    RT_NOREF(idxRegTmp);
#else
    /* ubfiz regeff, regrsp, #0, #16 - copies bits 15:0 from RSP to EffSp bits 15:0, zeroing bits 63:16. */
    pCodeBuf[off++] = Armv8A64MkInstrUbfiz(idxRegEffSp, idxRegRsp, 0, 16, false /*f64Bit*/);
    /* add tmp, regrsp, #cbMem */
    pCodeBuf[off++] = Armv8A64MkInstrAddUImm12(idxRegTmp, idxRegRsp, cbMem, false /*f64Bit*/);
    /* and tmp, tmp, #0xffff */
    Assert(Armv8A64ConvertImmRImmS2Mask32(15, 0) == 0xffff);
    pCodeBuf[off++] = Armv8A64MkInstrAndImm(idxRegTmp, idxRegTmp, 15, 0,  false /*f64Bit*/);
    /* bfi regrsp, regeff, #0, #16 - moves bits 15:0 from tmp to RSP bits 15:0, keeping the other RSP bits as is. */
    pCodeBuf[off++] = Armv8A64MkInstrBfi(idxRegRsp, idxRegTmp, 0, 16, false /*f64Bit*/);
#endif
    return off;
}


DECL_FORCE_INLINE(uint32_t)
iemNativeEmitStackPopUse32Sp(PIEMNATIVEINSTR pCodeBuf, uint32_t off, uint8_t idxRegRsp, uint8_t idxRegEffSp, uint8_t cbMem)
{
    /* Use32BitSp: */
    off = iemNativeEmitLoadGprFromGpr32Ex(pCodeBuf, off, idxRegEffSp, idxRegRsp);
    off = iemNativeEmitAddGpr32ImmEx(pCodeBuf, off, idxRegRsp, cbMem);
    return off;
}


/** IEM_MC[|_FLAT32|_FLAT64]_POP_GREG_U16/32/64 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitStackPopGReg(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxGReg,
                          uint32_t cBitsVarAndFlat, uintptr_t pfnFunction, uint8_t idxInstr)
{
    /*
     * Assert sanity.
     */
    Assert(idxGReg < 16);
#ifdef VBOX_STRICT
    if (RT_BYTE2(cBitsVarAndFlat) != 0)
    {
        Assert(   (pReNative->fExec & IEM_F_MODE_MASK) == IEM_F_MODE_X86_64BIT
               || (pReNative->fExec & IEM_F_MODE_MASK) == IEM_F_MODE_X86_32BIT_PROT_FLAT
               || (pReNative->fExec & IEM_F_MODE_MASK) == IEM_F_MODE_X86_32BIT_FLAT);
        Assert(   pfnFunction
               == (  cBitsVarAndFlat == RT_MAKE_U32_FROM_U8(16, 32, 0, 0) ? (uintptr_t)iemNativeHlpStackFlatFetchU16
                   : cBitsVarAndFlat == RT_MAKE_U32_FROM_U8(32, 32, 0, 0) ? (uintptr_t)iemNativeHlpStackFlatFetchU32
                   : cBitsVarAndFlat == RT_MAKE_U32_FROM_U8(16, 64, 0, 0) ? (uintptr_t)iemNativeHlpStackFlatFetchU16
                   : cBitsVarAndFlat == RT_MAKE_U32_FROM_U8(64, 64, 0, 0) ? (uintptr_t)iemNativeHlpStackFlatFetchU64
                   : UINT64_C(0xc000b000a0009000) ));
    }
    else
        Assert(   pfnFunction
               == (  cBitsVarAndFlat == RT_MAKE_U32_FROM_U8(16, 0, 0, 0) ? (uintptr_t)iemNativeHlpStackFetchU16
                   : cBitsVarAndFlat == RT_MAKE_U32_FROM_U8(32, 0, 0, 0) ? (uintptr_t)iemNativeHlpStackFetchU32
                   : cBitsVarAndFlat == RT_MAKE_U32_FROM_U8(64, 0, 0, 0) ? (uintptr_t)iemNativeHlpStackFetchU64
                   : UINT64_C(0xc000b000a0009000) ));
#endif

#ifdef VBOX_STRICT
    /*
     * Check that the fExec flags we've got make sense.
     */
    off = iemNativeEmitExecFlagsCheck(pReNative, off, pReNative->fExec);
#endif

    /*
     * To keep things simple we have to commit any pending writes first as we
     * may end up making calls.
     */
    off = iemNativeRegFlushPendingWrites(pReNative, off);

    /*
     * Determine the effective stack pointer, for non-FLAT modes we also update RSP.
     * For FLAT modes we'll do this in TlbDone as we'll be using the incoming RSP
     * directly as the effective stack pointer.
     * (Code structure is very similar to that of PUSH)
     */
    uint8_t const cbMem           = RT_BYTE1(cBitsVarAndFlat) / 8;
    uint8_t const cBitsFlat       = RT_BYTE2(cBitsVarAndFlat);      RT_NOREF(cBitsFlat);
    uint8_t const idxRegRsp       = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(X86_GREG_xSP),
                                                                    kIemNativeGstRegUse_ForUpdate, true /*fNoVolatileRegs*/);
    uint8_t const idxRegEffSp     = cBitsFlat != 0 ? idxRegRsp : iemNativeRegAllocTmp(pReNative, &off);
    /** @todo can do a better job picking the register here. For cbMem >= 4 this
     *        will be the resulting register value. */
    uint8_t const idxRegMemResult = iemNativeRegAllocTmp(pReNative, &off); /* pointer then value; arm64 SP += 2/4 helper too.  */

    uint32_t      offFixupJumpToUseOtherBitSp = UINT32_MAX;
    if (cBitsFlat != 0)
    {
        Assert(idxRegEffSp == idxRegRsp);
        Assert(cBitsFlat == 32 || cBitsFlat == 64);
        Assert(IEM_F_MODE_X86_IS_FLAT(pReNative->fExec));
    }
    else /** @todo We can skip the test if we're targeting pre-386 CPUs. */
    {
        Assert(idxRegEffSp != idxRegRsp);
        uint8_t const idxRegSsAttr = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_SEG_ATTRIB(X86_SREG_SS),
                                                                     kIemNativeGstRegUse_ReadOnly);
#ifdef RT_ARCH_AMD64
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 32);
#else
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 10);
#endif
        off = iemNativeEmitTestAnyBitsInGpr32Ex(pCodeBuf, off, idxRegSsAttr, X86DESCATTR_D);
        iemNativeRegFreeTmp(pReNative, idxRegSsAttr);
        offFixupJumpToUseOtherBitSp = off;
        if ((pReNative->fExec & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_32BIT)
        {
/** @todo can skip idxRegRsp updating when popping ESP.   */
            off = iemNativeEmitJccToFixedEx(pCodeBuf, off, off /*8-bit suffices*/, kIemNativeInstrCond_e); /* jump if zero */
            off = iemNativeEmitStackPopUse32Sp(pCodeBuf, off, idxRegRsp, idxRegEffSp, cbMem);
        }
        else
        {
            off = iemNativeEmitJccToFixedEx(pCodeBuf, off, off /*8-bit suffices*/, kIemNativeInstrCond_ne); /* jump if not zero */
            off = iemNativeEmitStackPopUse16Sp(pCodeBuf, off, idxRegRsp, idxRegEffSp, cbMem, idxRegMemResult);
        }
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    }
    /* SpUpdateEnd: */
    uint32_t const offLabelSpUpdateEnd = off;

    /*
     * Okay, now prepare for TLB lookup and jump to code (or the TlbMiss if
     * we're skipping lookup).
     */
    uint8_t const  iSegReg           = cBitsFlat != 0 ? UINT8_MAX : X86_SREG_SS;
    IEMNATIVEEMITTLBSTATE const TlbState(pReNative, idxRegEffSp, &off, iSegReg, cbMem);
    uint16_t const uTlbSeqNo         = pReNative->uTlbSeqNo++;
    uint32_t const idxLabelTlbMiss   = iemNativeLabelCreate(pReNative, kIemNativeLabelType_TlbMiss, UINT32_MAX, uTlbSeqNo);
    uint32_t const idxLabelTlbLookup = !TlbState.fSkip
                                     ? iemNativeLabelCreate(pReNative, kIemNativeLabelType_TlbLookup, UINT32_MAX, uTlbSeqNo)
                                     : UINT32_MAX;

    if (!TlbState.fSkip)
        off = iemNativeEmitJmpToLabel(pReNative, off, idxLabelTlbLookup); /** @todo short jump */
    else
        off = iemNativeEmitJmpToLabel(pReNative, off, idxLabelTlbMiss); /** @todo short jump */

    /*
     * Use16BitSp:
     */
    if (cBitsFlat == 0)
    {
#ifdef RT_ARCH_AMD64
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 32);
#else
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 10);
#endif
        iemNativeFixupFixedJump(pReNative, offFixupJumpToUseOtherBitSp, off);
        if ((pReNative->fExec & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_32BIT)
            off = iemNativeEmitStackPopUse16Sp(pCodeBuf, off, idxRegRsp, idxRegEffSp, cbMem, idxRegMemResult);
        else
            off = iemNativeEmitStackPopUse32Sp(pCodeBuf, off, idxRegRsp, idxRegEffSp, cbMem);
        off = iemNativeEmitJmpToFixedEx(pCodeBuf, off, offLabelSpUpdateEnd);
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    }

    /*
     * TlbMiss:
     *
     * Call helper to do the pushing.
     */
    iemNativeLabelDefine(pReNative, idxLabelTlbMiss, off);

#ifdef IEMNATIVE_WITH_INSTRUCTION_COUNTING
    off = iemNativeEmitStoreImmToVCpuU8(pReNative, off, idxInstr, RT_UOFFSETOF(VMCPUCC, iem.s.idxTbCurInstr));
#else
    RT_NOREF(idxInstr);
#endif

    uint32_t const fHstRegsNotToSave = TlbState.getRegsNotToSave()
                                     | (idxRegMemResult < RT_ELEMENTS(pReNative->Core.aHstRegs) ? RT_BIT_32(idxRegMemResult) : 0)
                                     | (idxRegEffSp != idxRegRsp ? RT_BIT_32(idxRegEffSp) : 0);
    off = iemNativeVarSaveVolatileRegsPreHlpCall(pReNative, off, fHstRegsNotToSave);


    /* IEMNATIVE_CALL_ARG1_GREG = EffSp/RSP */
    if (idxRegEffSp != IEMNATIVE_CALL_ARG1_GREG)
        off = iemNativeEmitLoadGprFromGpr(pReNative, off, IEMNATIVE_CALL_ARG1_GREG, idxRegEffSp);

#ifdef IEMNATIVE_WITH_EFLAGS_POSTPONING
    /* Do delayed EFLAGS calculations. */
    off = iemNativeDoPostponedEFlagsAtTlbMiss<RT_BIT_32(IEMNATIVE_CALL_ARG1_GREG)>(pReNative, off, &TlbState, fHstRegsNotToSave);
#endif

    /* IEMNATIVE_CALL_ARG0_GREG = pVCpu */
    off = iemNativeEmitLoadGprFromGpr(pReNative, off, IEMNATIVE_CALL_ARG0_GREG, IEMNATIVE_REG_FIXED_PVMCPU);

    /* Done setting up parameters, make the call. */
    off = iemNativeEmitCallImm<true /*a_fSkipEflChecks*/>(pReNative, off, pfnFunction);

    /* Move the return register content to idxRegMemResult. */
    if (idxRegMemResult != IEMNATIVE_CALL_RET_GREG)
        off = iemNativeEmitLoadGprFromGpr(pReNative, off, idxRegMemResult, IEMNATIVE_CALL_RET_GREG);

    /* Restore variables and guest shadow registers to volatile registers. */
    off = iemNativeVarRestoreVolatileRegsPostHlpCall(pReNative, off, fHstRegsNotToSave);
    off = iemNativeRegRestoreGuestShadowsInVolatileRegs(pReNative, off, TlbState.getActiveRegsWithShadows());

#ifdef IEMNATIVE_WITH_TLB_LOOKUP
    if (!TlbState.fSkip)
    {
        /* end of TlbMiss - Jump to the done label. */
        uint32_t const idxLabelTlbDone = iemNativeLabelCreate(pReNative, kIemNativeLabelType_TlbDone, UINT32_MAX, uTlbSeqNo);
        off = iemNativeEmitJmpToLabel(pReNative, off, idxLabelTlbDone);

        /*
         * TlbLookup:
         */
        off = iemNativeEmitTlbLookup<true>(pReNative, off, &TlbState, iSegReg, cbMem, cbMem - 1, IEM_ACCESS_TYPE_READ,
                                           idxLabelTlbLookup, idxLabelTlbMiss, idxRegMemResult);

        /*
         * Emit code to load the value (from idxRegMemResult into idxRegMemResult).
         */
        PIEMNATIVEINSTR const pCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 32);
# ifdef IEM_WITH_TLB_STATISTICS
        off = iemNativeEmitIncStamCounterInVCpuEx(pCodeBuf, off, TlbState.idxReg1, TlbState.idxReg2,
                                                  RT_UOFFSETOF(VMCPUCC, iem.s.StatNativeTlbHitsForStack));
# endif
        switch (cbMem)
        {
            case 2:
                off = iemNativeEmitLoadGprByGprU16Ex(pCodeBuf, off, idxRegMemResult, idxRegMemResult);
                break;
            case 4:
                off = iemNativeEmitLoadGprByGprU32Ex(pCodeBuf, off, idxRegMemResult, idxRegMemResult);
                break;
            case 8:
                off = iemNativeEmitLoadGprByGprU64Ex(pCodeBuf, off, idxRegMemResult, idxRegMemResult);
                break;
            default:
                AssertFailed();
        }

        TlbState.freeRegsAndReleaseVars(pReNative);

        /*
         * TlbDone:
         *
         * Set the new RSP value (FLAT accesses needs to calculate it first) and
         * commit the popped register value.
         */
        iemNativeLabelDefine(pReNative, idxLabelTlbDone, off);
    }
#endif /* IEMNATIVE_WITH_TLB_LOOKUP */

    if (idxGReg != X86_GREG_xSP)
    {
        /* Set the register. */
        if (cbMem >= sizeof(uint32_t))
        {
#ifdef IEMNATIVE_WITH_LIVENESS_ANALYSIS
            AssertMsg(   pReNative->idxCurCall == 0
                      || IEMLIVENESS_STATE_IS_CLOBBER_EXPECTED(iemNativeLivenessGetPrevStateByGstReg(pReNative, IEMNATIVEGSTREG_GPR(idxGReg))),
                      ("%s - %u\n", g_aGstShadowInfo[idxGReg].pszName,
                       iemNativeLivenessGetPrevStateByGstReg(pReNative, IEMNATIVEGSTREG_GPR(idxGReg))));
#endif
            iemNativeRegClearAndMarkAsGstRegShadow(pReNative, idxRegMemResult,  IEMNATIVEGSTREG_GPR(idxGReg), off);
#if defined(IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK)
            pReNative->Core.bmGstRegShadowDirty |= RT_BIT_64(idxGReg);
#endif
#if !defined(IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK)
            off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxRegMemResult,
                                                 RT_UOFFSETOF_DYN(VMCPU, cpum.GstCtx.aGRegs[idxGReg]));
#endif
        }
        else
        {
            Assert(cbMem == sizeof(uint16_t));
            uint8_t const idxRegDst = iemNativeRegAllocTmpForGuestReg(pReNative, &off, IEMNATIVEGSTREG_GPR(idxGReg),
                                                                      kIemNativeGstRegUse_ForUpdate);
            off = iemNativeEmitGprMergeInGpr16(pReNative, off, idxRegDst, idxRegMemResult);
#if !defined(IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK)
            off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxRegDst, RT_UOFFSETOF_DYN(VMCPU, cpum.GstCtx.aGRegs[idxGReg]));
#endif
            iemNativeRegFreeTmp(pReNative, idxRegDst);
        }

        /* Complete RSP calculation for FLAT mode. */
        if (idxRegEffSp == idxRegRsp)
        {
            if (cBitsFlat == 64)
                off = iemNativeEmitAddGprImm8(pReNative, off, idxRegRsp, cbMem);
            else
                off = iemNativeEmitAddGpr32Imm8(pReNative, off, idxRegRsp, cbMem);
        }
    }
    else
    {
        /* We're popping RSP, ESP or SP. Only the is a bit extra work, of course. */
        if (cbMem == sizeof(uint64_t))
            off = iemNativeEmitLoadGprFromGpr(pReNative, off, idxRegRsp, idxRegMemResult);
        else if (cbMem == sizeof(uint32_t))
            off = iemNativeEmitLoadGprFromGpr32(pReNative, off, idxRegRsp, idxRegMemResult);
        else
        {
            if (idxRegEffSp == idxRegRsp)
            {
                if (cBitsFlat == 64)
                    off = iemNativeEmitAddGprImm8(pReNative, off, idxRegRsp, cbMem);
                else
                    off = iemNativeEmitAddGpr32Imm8(pReNative, off, idxRegRsp, cbMem);
            }
            off = iemNativeEmitGprMergeInGpr16(pReNative, off, idxRegRsp, idxRegMemResult);
        }
    }

#if !defined(IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK)
    off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxRegRsp, RT_UOFFSETOF(VMCPU, cpum.GstCtx.rsp));
#endif

    iemNativeRegFreeTmp(pReNative, idxRegRsp);
    if (idxRegEffSp != idxRegRsp)
        iemNativeRegFreeTmp(pReNative, idxRegEffSp);
    iemNativeRegFreeTmp(pReNative, idxRegMemResult);

    return off;
}



/*********************************************************************************************************************************
*   Memory mapping (IEM_MEM_MAP_XXX, IEM_MEM_FLAT_MAP_XXX).                                                                      *
*********************************************************************************************************************************/

#define IEM_MC_MEM_MAP_U8_ATOMIC(a_pu8Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu8Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem, sizeof(uint8_t), \
                                    IEM_ACCESS_DATA_ATOMIC,  0 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemMapDataU8Atomic, pCallEntry->idxInstr)

#define IEM_MC_MEM_MAP_U8_RW(a_pu8Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu8Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem, sizeof(uint8_t), \
                                    IEM_ACCESS_DATA_RW,  0 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemMapDataU8Rw, pCallEntry->idxInstr)

#define IEM_MC_MEM_MAP_U8_WO(a_pu8Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu8Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem, sizeof(uint8_t), \
                                    IEM_ACCESS_DATA_W,  0 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemMapDataU8Wo, pCallEntry->idxInstr) \

#define IEM_MC_MEM_MAP_U8_RO(a_pu8Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu8Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem, sizeof(uint8_t), \
                                    IEM_ACCESS_DATA_R,  0 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemMapDataU8Ro, pCallEntry->idxInstr)


#define IEM_MC_MEM_MAP_U16_ATOMIC(a_pu16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem, sizeof(uint16_t), \
                                    IEM_ACCESS_DATA_ATOMIC,  sizeof(uint16_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemMapDataU16Atomic, pCallEntry->idxInstr)

#define IEM_MC_MEM_MAP_U16_RW(a_pu16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem, sizeof(uint16_t), \
                                    IEM_ACCESS_DATA_RW,  sizeof(uint16_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemMapDataU16Rw, pCallEntry->idxInstr)

#define IEM_MC_MEM_MAP_U16_WO(a_pu16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem, sizeof(uint16_t), \
                                    IEM_ACCESS_DATA_W, sizeof(uint16_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemMapDataU16Wo, pCallEntry->idxInstr) \

#define IEM_MC_MEM_MAP_U16_RO(a_pu16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem, sizeof(uint16_t), \
                                    IEM_ACCESS_DATA_R,  sizeof(uint16_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemMapDataU16Ro, pCallEntry->idxInstr)

#define IEM_MC_MEM_MAP_I16_WO(a_pi16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pi16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem, sizeof(int16_t), \
                                    IEM_ACCESS_DATA_W, sizeof(uint16_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemMapDataU16Wo, pCallEntry->idxInstr) \


#define IEM_MC_MEM_MAP_U32_ATOMIC(a_pu32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem, sizeof(uint32_t), \
                                    IEM_ACCESS_DATA_ATOMIC, sizeof(uint32_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemMapDataU32Atomic, pCallEntry->idxInstr)

#define IEM_MC_MEM_MAP_U32_RW(a_pu32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem, sizeof(uint32_t), \
                                    IEM_ACCESS_DATA_RW, sizeof(uint32_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemMapDataU32Rw, pCallEntry->idxInstr)

#define IEM_MC_MEM_MAP_U32_WO(a_pu32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem, sizeof(uint32_t), \
                                    IEM_ACCESS_DATA_W, sizeof(uint32_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemMapDataU32Wo, pCallEntry->idxInstr) \

#define IEM_MC_MEM_MAP_U32_RO(a_pu32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem, sizeof(uint32_t), \
                                    IEM_ACCESS_DATA_R,  sizeof(uint32_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemMapDataU32Ro, pCallEntry->idxInstr)

#define IEM_MC_MEM_MAP_I32_WO(a_pi32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pi32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem, sizeof(int32_t), \
                                    IEM_ACCESS_DATA_W, sizeof(uint32_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemMapDataU32Wo, pCallEntry->idxInstr) \


#define IEM_MC_MEM_MAP_U64_ATOMIC(a_pu64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem, sizeof(uint64_t), \
                                    IEM_ACCESS_DATA_ATOMIC, sizeof(uint64_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemMapDataU64Atomic, pCallEntry->idxInstr)

#define IEM_MC_MEM_MAP_U64_RW(a_pu64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem, sizeof(uint64_t), \
                                    IEM_ACCESS_DATA_RW, sizeof(uint64_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemMapDataU64Rw, pCallEntry->idxInstr)
#define IEM_MC_MEM_MAP_U64_WO(a_pu64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem, sizeof(uint64_t), \
                                    IEM_ACCESS_DATA_W, sizeof(uint64_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemMapDataU64Wo, pCallEntry->idxInstr) \

#define IEM_MC_MEM_MAP_U64_RO(a_pu64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem, sizeof(uint64_t), \
                                    IEM_ACCESS_DATA_R,  sizeof(uint64_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemMapDataU64Ro, pCallEntry->idxInstr)

#define IEM_MC_MEM_MAP_I64_WO(a_pi64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pi64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem, sizeof(int64_t), \
                                    IEM_ACCESS_DATA_W, sizeof(uint64_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemMapDataU64Wo, pCallEntry->idxInstr) \


#define IEM_MC_MEM_MAP_R80_WO(a_pr80Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pr80Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem, sizeof(RTFLOAT80U), \
                                    IEM_ACCESS_DATA_W, sizeof(uint64_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemMapDataR80Wo, pCallEntry->idxInstr) \

#define IEM_MC_MEM_MAP_D80_WO(a_pd80Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pd80Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem, sizeof(RTFLOAT80U), \
                                    IEM_ACCESS_DATA_W, sizeof(uint64_t) - 1 /*fAlignMaskAndCtl*/, /** @todo check BCD align */ \
                                    (uintptr_t)iemNativeHlpMemMapDataD80Wo, pCallEntry->idxInstr) \


#define IEM_MC_MEM_MAP_U128_ATOMIC(a_pu128Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu128Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem, sizeof(RTUINT128U), \
                                    IEM_ACCESS_DATA_ATOMIC, sizeof(RTUINT128U) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemMapDataU128Atomic, pCallEntry->idxInstr)

#define IEM_MC_MEM_MAP_U128_RW(a_pu128Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu128Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem, sizeof(RTUINT128U), \
                                    IEM_ACCESS_DATA_RW, sizeof(RTUINT128U) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemMapDataU128Rw, pCallEntry->idxInstr)

#define IEM_MC_MEM_MAP_U128_WO(a_pu128Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu128Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem, sizeof(RTUINT128U), \
                                    IEM_ACCESS_DATA_W, sizeof(RTUINT128U) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemMapDataU128Wo, pCallEntry->idxInstr) \

#define IEM_MC_MEM_MAP_U128_RO(a_pu128Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu128Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem, sizeof(RTUINT128U), \
                                    IEM_ACCESS_DATA_R,  sizeof(RTUINT128U) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemMapDataU128Ro, pCallEntry->idxInstr)



#define IEM_MC_MEM_FLAT_MAP_U8_ATOMIC(a_pu8Mem, a_bUnmapInfo, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu8Mem, a_bUnmapInfo, UINT8_MAX, a_GCPtrMem, sizeof(uint8_t), \
                                    IEM_ACCESS_DATA_ATOMIC,  0 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemFlatMapDataU8Atomic, pCallEntry->idxInstr)

#define IEM_MC_MEM_FLAT_MAP_U8_RW(a_pu8Mem, a_bUnmapInfo, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu8Mem, a_bUnmapInfo, UINT8_MAX, a_GCPtrMem, sizeof(uint8_t), \
                                    IEM_ACCESS_DATA_RW,  0 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemFlatMapDataU8Rw, pCallEntry->idxInstr)

#define IEM_MC_MEM_FLAT_MAP_U8_WO(a_pu8Mem, a_bUnmapInfo, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu8Mem, a_bUnmapInfo, UINT8_MAX, a_GCPtrMem, sizeof(uint8_t), \
                                    IEM_ACCESS_DATA_W,  0 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemFlatMapDataU8Wo, pCallEntry->idxInstr) \

#define IEM_MC_MEM_FLAT_MAP_U8_RO(a_pu8Mem, a_bUnmapInfo, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu8Mem, a_bUnmapInfo, UINT8_MAX, a_GCPtrMem, sizeof(uint8_t), \
                                    IEM_ACCESS_DATA_R,  0 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemFlatMapDataU8Ro, pCallEntry->idxInstr)


#define IEM_MC_MEM_FLAT_MAP_U16_ATOMIC(a_pu16Mem, a_bUnmapInfo, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu16Mem, a_bUnmapInfo, UINT8_MAX, a_GCPtrMem, sizeof(uint16_t), \
                                    IEM_ACCESS_DATA_ATOMIC,  sizeof(uint16_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemFlatMapDataU16Atomic, pCallEntry->idxInstr)

#define IEM_MC_MEM_FLAT_MAP_U16_RW(a_pu16Mem, a_bUnmapInfo, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu16Mem, a_bUnmapInfo, UINT8_MAX, a_GCPtrMem, sizeof(uint16_t), \
                                    IEM_ACCESS_DATA_RW,  sizeof(uint16_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemFlatMapDataU16Rw, pCallEntry->idxInstr)

#define IEM_MC_MEM_FLAT_MAP_U16_WO(a_pu16Mem, a_bUnmapInfo, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu16Mem, a_bUnmapInfo, UINT8_MAX, a_GCPtrMem, sizeof(uint16_t), \
                                    IEM_ACCESS_DATA_W, sizeof(uint16_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemFlatMapDataU16Wo, pCallEntry->idxInstr) \

#define IEM_MC_MEM_FLAT_MAP_U16_RO(a_pu16Mem, a_bUnmapInfo, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu16Mem, a_bUnmapInfo, UINT8_MAX, a_GCPtrMem, sizeof(uint16_t), \
                                    IEM_ACCESS_DATA_R,  sizeof(uint16_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemFlatMapDataU16Ro, pCallEntry->idxInstr)

#define IEM_MC_MEM_FLAT_MAP_I16_WO(a_pi16Mem, a_bUnmapInfo, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pi16Mem, a_bUnmapInfo, UINT8_MAX, a_GCPtrMem, sizeof(int16_t), \
                                    IEM_ACCESS_DATA_W, sizeof(uint16_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemFlatMapDataU16Wo, pCallEntry->idxInstr) \


#define IEM_MC_MEM_FLAT_MAP_U32_ATOMIC(a_pu32Mem, a_bUnmapInfo, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu32Mem, a_bUnmapInfo, UINT8_MAX, a_GCPtrMem, sizeof(uint32_t), \
                                    IEM_ACCESS_DATA_ATOMIC, sizeof(uint32_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemFlatMapDataU32Atomic, pCallEntry->idxInstr)

#define IEM_MC_MEM_FLAT_MAP_U32_RW(a_pu32Mem, a_bUnmapInfo, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu32Mem, a_bUnmapInfo, UINT8_MAX, a_GCPtrMem, sizeof(uint32_t), \
                                    IEM_ACCESS_DATA_RW, sizeof(uint32_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemFlatMapDataU32Rw, pCallEntry->idxInstr)

#define IEM_MC_MEM_FLAT_MAP_U32_WO(a_pu32Mem, a_bUnmapInfo, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu32Mem, a_bUnmapInfo, UINT8_MAX, a_GCPtrMem, sizeof(uint32_t), \
                                    IEM_ACCESS_DATA_W, sizeof(uint32_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemFlatMapDataU32Wo, pCallEntry->idxInstr) \

#define IEM_MC_MEM_FLAT_MAP_U32_RO(a_pu32Mem, a_bUnmapInfo, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu32Mem, a_bUnmapInfo, UINT8_MAX, a_GCPtrMem, sizeof(uint32_t), \
                                    IEM_ACCESS_DATA_R,  sizeof(uint32_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemFlatMapDataU32Ro, pCallEntry->idxInstr)

#define IEM_MC_MEM_FLAT_MAP_I32_WO(a_pi32Mem, a_bUnmapInfo, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pi32Mem, a_bUnmapInfo, UINT8_MAX, a_GCPtrMem, sizeof(int32_t), \
                                    IEM_ACCESS_DATA_W, sizeof(uint32_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemFlatMapDataU32Wo, pCallEntry->idxInstr) \


#define IEM_MC_MEM_FLAT_MAP_U64_ATOMIC(a_pu64Mem, a_bUnmapInfo, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu64Mem, a_bUnmapInfo, UINT8_MAX, a_GCPtrMem, sizeof(uint64_t), \
                                    IEM_ACCESS_DATA_ATOMIC, sizeof(uint64_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemFlatMapDataU64Atomic, pCallEntry->idxInstr)

#define IEM_MC_MEM_FLAT_MAP_U64_RW(a_pu64Mem, a_bUnmapInfo, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu64Mem, a_bUnmapInfo, UINT8_MAX, a_GCPtrMem, sizeof(uint64_t), \
                                    IEM_ACCESS_DATA_RW, sizeof(uint64_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemFlatMapDataU64Rw, pCallEntry->idxInstr)

#define IEM_MC_MEM_FLAT_MAP_U64_WO(a_pu64Mem, a_bUnmapInfo, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu64Mem, a_bUnmapInfo, UINT8_MAX, a_GCPtrMem, sizeof(uint64_t), \
                                    IEM_ACCESS_DATA_W, sizeof(uint64_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemFlatMapDataU64Wo, pCallEntry->idxInstr) \

#define IEM_MC_MEM_FLAT_MAP_U64_RO(a_pu64Mem, a_bUnmapInfo, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu64Mem, a_bUnmapInfo, UINT8_MAX, a_GCPtrMem, sizeof(uint64_t), \
                                    IEM_ACCESS_DATA_R,  sizeof(uint64_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemFlatMapDataU64Ro, pCallEntry->idxInstr)

#define IEM_MC_MEM_FLAT_MAP_I64_WO(a_pi64Mem, a_bUnmapInfo, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pi64Mem, a_bUnmapInfo, UINT8_MAX, a_GCPtrMem, sizeof(int64_t), \
                                    IEM_ACCESS_DATA_W, sizeof(uint64_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemFlatMapDataU64Wo, pCallEntry->idxInstr) \


#define IEM_MC_MEM_FLAT_MAP_R80_WO(a_pr80Mem, a_bUnmapInfo, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pr80Mem, a_bUnmapInfo, UINT8_MAX, a_GCPtrMem, sizeof(RTFLOAT80U), \
                                    IEM_ACCESS_DATA_W, sizeof(uint64_t) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemFlatMapDataR80Wo, pCallEntry->idxInstr) \

#define IEM_MC_MEM_FLAT_MAP_D80_WO(a_pd80Mem, a_bUnmapInfo, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pd80Mem, a_bUnmapInfo, UINT8_MAX, a_GCPtrMem, sizeof(RTFLOAT80U), \
                                    IEM_ACCESS_DATA_W, sizeof(uint64_t) - 1 /*fAlignMaskAndCtl*/, /** @todo check BCD align */ \
                                    (uintptr_t)iemNativeHlpMemFlatMapDataD80Wo, pCallEntry->idxInstr) \


#define IEM_MC_MEM_FLAT_MAP_U128_ATOMIC(a_pu128Mem, a_bUnmapInfo, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu128Mem, a_bUnmapInfo, UINT8_MAX, a_GCPtrMem, sizeof(RTUINT128U), \
                                    IEM_ACCESS_DATA_ATOMIC, sizeof(RTUINT128U) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemFlatMapDataU128Atomic, pCallEntry->idxInstr)

#define IEM_MC_MEM_FLAT_MAP_U128_RW(a_pu128Mem, a_bUnmapInfo, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu128Mem, a_bUnmapInfo, UINT8_MAX, a_GCPtrMem, sizeof(RTUINT128U), \
                                    IEM_ACCESS_DATA_RW, sizeof(RTUINT128U) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemFlatMapDataU128Rw, pCallEntry->idxInstr)

#define IEM_MC_MEM_FLAT_MAP_U128_WO(a_pu128Mem, a_bUnmapInfo, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu128Mem, a_bUnmapInfo, UINT8_MAX, a_GCPtrMem, sizeof(RTUINT128U), \
                                    IEM_ACCESS_DATA_W, sizeof(RTUINT128U) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemFlatMapDataU128Wo, pCallEntry->idxInstr) \

#define IEM_MC_MEM_FLAT_MAP_U128_RO(a_pu128Mem, a_bUnmapInfo, a_GCPtrMem) \
    off = iemNativeEmitMemMapCommon(pReNative, off, a_pu128Mem, a_bUnmapInfo, UINT8_MAX, a_GCPtrMem, sizeof(RTUINT128U), \
                                    IEM_ACCESS_DATA_R,  sizeof(RTUINT128U) - 1 /*fAlignMaskAndCtl*/, \
                                    (uintptr_t)iemNativeHlpMemFlatMapDataU128Ro, pCallEntry->idxInstr)


DECL_INLINE_THROW(uint32_t)
iemNativeEmitMemMapCommon(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarMem, uint8_t idxVarUnmapInfo,
                          uint8_t iSegReg, uint8_t idxVarGCPtrMem, uint8_t cbMem, uint32_t fAccess, uint32_t fAlignMaskAndCtl,
                          uintptr_t pfnFunction, uint8_t idxInstr)
{
    /*
     * Assert sanity.
     */
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVarMem);
    PIEMNATIVEVAR const pVarMem = &pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVarMem)];
    AssertStmt(   pVarMem->enmKind == kIemNativeVarKind_Invalid
               && pVarMem->cbVar   == sizeof(void *),
               IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_VAR_UNEXPECTED_KIND));

    PIEMNATIVEVAR const pVarUnmapInfo = &pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVarUnmapInfo)];
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVarUnmapInfo);
    AssertStmt(   pVarUnmapInfo->enmKind == kIemNativeVarKind_Invalid
               && pVarUnmapInfo->cbVar   == sizeof(uint8_t),
               IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_VAR_UNEXPECTED_KIND));

    PIEMNATIVEVAR const pVarGCPtrMem = &pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVarGCPtrMem)];
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVarGCPtrMem);
    AssertStmt(   pVarGCPtrMem->enmKind == kIemNativeVarKind_Immediate
               || pVarGCPtrMem->enmKind == kIemNativeVarKind_Stack,
               IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_VAR_UNEXPECTED_KIND));

    Assert(iSegReg < 6 || iSegReg == UINT8_MAX);

    AssertCompile(IEMNATIVE_CALL_ARG_GREG_COUNT >= 4);

#ifdef VBOX_STRICT
# define IEM_MAP_HLP_FN_NO_AT(a_fAccess, a_fnBase) \
        (  ((a_fAccess) & (IEM_ACCESS_TYPE_MASK | IEM_ACCESS_ATOMIC)) == (IEM_ACCESS_TYPE_WRITE | IEM_ACCESS_TYPE_READ) \
         ? (uintptr_t)RT_CONCAT(a_fnBase,Rw) \
         : ((a_fAccess) & (IEM_ACCESS_TYPE_MASK | IEM_ACCESS_ATOMIC)) == IEM_ACCESS_TYPE_READ \
         ? (uintptr_t)RT_CONCAT(a_fnBase,Ro) : (uintptr_t)RT_CONCAT(a_fnBase,Wo) )
# define IEM_MAP_HLP_FN(a_fAccess, a_fnBase) \
        (  ((a_fAccess) & (IEM_ACCESS_TYPE_MASK | IEM_ACCESS_ATOMIC)) == (IEM_ACCESS_TYPE_WRITE | IEM_ACCESS_TYPE_READ | IEM_ACCESS_ATOMIC) \
         ? (uintptr_t)RT_CONCAT(a_fnBase,Atomic) \
         : IEM_MAP_HLP_FN_NO_AT(a_fAccess, a_fnBase) )

    if (iSegReg == UINT8_MAX)
    {
        Assert(   (pReNative->fExec & IEM_F_MODE_MASK) == IEM_F_MODE_X86_64BIT
               || (pReNative->fExec & IEM_F_MODE_MASK) == IEM_F_MODE_X86_32BIT_PROT_FLAT
               || (pReNative->fExec & IEM_F_MODE_MASK) == IEM_F_MODE_X86_32BIT_FLAT);
        switch (cbMem)
        {
            case 1:
                Assert(pfnFunction == IEM_MAP_HLP_FN(fAccess, iemNativeHlpMemFlatMapDataU8));
                Assert(!fAlignMaskAndCtl);
                break;
            case 2:
                Assert(pfnFunction == IEM_MAP_HLP_FN(fAccess, iemNativeHlpMemFlatMapDataU16));
                Assert(fAlignMaskAndCtl < 2);
                break;
            case 4:
                Assert(pfnFunction == IEM_MAP_HLP_FN(fAccess, iemNativeHlpMemFlatMapDataU32));
                Assert(fAlignMaskAndCtl < 4);
                break;
            case 8:
                Assert(pfnFunction == IEM_MAP_HLP_FN(fAccess, iemNativeHlpMemFlatMapDataU64));
                Assert(fAlignMaskAndCtl < 8);
                break;
            case 10:
                Assert(   pfnFunction == (uintptr_t)iemNativeHlpMemFlatMapDataR80Wo
                       || pfnFunction == (uintptr_t)iemNativeHlpMemFlatMapDataD80Wo);
                Assert((fAccess & IEM_ACCESS_TYPE_MASK) == IEM_ACCESS_TYPE_WRITE);
                Assert(fAlignMaskAndCtl < 8);
                break;
            case 16:
                Assert(pfnFunction == IEM_MAP_HLP_FN(fAccess, iemNativeHlpMemFlatMapDataU128));
                Assert(fAlignMaskAndCtl < 16);
                break;
# if 0
            case 32:
                Assert(pfnFunction == IEM_MAP_HLP_FN_NO_AT(fAccess, iemNativeHlpMemFlatMapDataU256));
                Assert(fAlignMaskAndCtl < 32);
                break;
            case 64:
                Assert(pfnFunction == IEM_MAP_HLP_FN_NO_AT(fAccess, iemNativeHlpMemFlatMapDataU512));
                Assert(fAlignMaskAndCtl < 64);
                break;
# endif
            default: AssertFailed(); break;
        }
    }
    else
    {
        Assert(iSegReg < 6);
        switch (cbMem)
        {
            case 1:
                Assert(pfnFunction == IEM_MAP_HLP_FN(fAccess, iemNativeHlpMemMapDataU8));
                Assert(!fAlignMaskAndCtl);
                break;
            case 2:
                Assert(pfnFunction == IEM_MAP_HLP_FN(fAccess, iemNativeHlpMemMapDataU16));
                Assert(fAlignMaskAndCtl < 2);
                break;
            case 4:
                Assert(pfnFunction == IEM_MAP_HLP_FN(fAccess, iemNativeHlpMemMapDataU32));
                Assert(fAlignMaskAndCtl < 4);
                break;
            case 8:
                Assert(pfnFunction == IEM_MAP_HLP_FN(fAccess, iemNativeHlpMemMapDataU64));
                Assert(fAlignMaskAndCtl < 8);
                break;
            case 10:
                Assert(   pfnFunction == (uintptr_t)iemNativeHlpMemMapDataR80Wo
                       || pfnFunction == (uintptr_t)iemNativeHlpMemMapDataD80Wo);
                Assert((fAccess & IEM_ACCESS_TYPE_MASK) == IEM_ACCESS_TYPE_WRITE);
                Assert(fAlignMaskAndCtl < 8);
                break;
            case 16:
                Assert(pfnFunction == IEM_MAP_HLP_FN(fAccess, iemNativeHlpMemMapDataU128));
                Assert(fAlignMaskAndCtl < 16);
                break;
# if 0
            case 32:
                Assert(pfnFunction == IEM_MAP_HLP_FN_NO_AT(fAccess, iemNativeHlpMemMapDataU256));
                Assert(fAlignMaskAndCtl < 32);
                break;
            case 64:
                Assert(pfnFunction == IEM_MAP_HLP_FN_NO_AT(fAccess, iemNativeHlpMemMapDataU512));
                Assert(fAlignMaskAndCtl < 64);
                break;
# endif
            default: AssertFailed(); break;
        }
    }
# undef IEM_MAP_HLP_FN
# undef IEM_MAP_HLP_FN_NO_AT
#endif

#ifdef VBOX_STRICT
    /*
     * Check that the fExec flags we've got make sense.
     */
    off = iemNativeEmitExecFlagsCheck(pReNative, off, pReNative->fExec);
#endif

    /*
     * To keep things simple we have to commit any pending writes first as we
     * may end up making calls.
     */
    off = iemNativeRegFlushPendingWrites(pReNative, off);

#ifdef IEMNATIVE_WITH_FREE_AND_FLUSH_VOLATILE_REGS_AT_TLB_LOOKUP
    /*
     * Move/spill/flush stuff out of call-volatile registers.
     * This is the easy way out. We could contain this to the tlb-miss branch
     * by saving and restoring active stuff here.
     */
    /** @todo save+restore active registers and maybe guest shadows in tlb-miss.  */
    off = iemNativeRegMoveAndFreeAndFlushAtCall(pReNative, off, 0 /* vacate all non-volatile regs */);
#endif

    /* The bUnmapInfo variable will get a register in the tlb-hit code path,
       while the tlb-miss codepath will temporarily put it on the stack.
       Set the the type to stack here so we don't need to do it twice below. */
    iemNativeVarSetKindToStack(pReNative, idxVarUnmapInfo);
    uint8_t const idxRegUnmapInfo   = iemNativeVarRegisterAcquire(pReNative, idxVarUnmapInfo, &off);
    /** @todo use a tmp register from TlbState, since they'll be free after tlb
     *        lookup is done. */

    /*
     * Define labels and allocate the result register (trying for the return
     * register if we can).
     */
    uint16_t const uTlbSeqNo         = pReNative->uTlbSeqNo++;
    uint8_t  const idxRegMemResult   = !(pReNative->Core.bmHstRegs & RT_BIT_32(IEMNATIVE_CALL_RET_GREG))
                                     ? iemNativeVarRegisterSetAndAcquire(pReNative, idxVarMem, IEMNATIVE_CALL_RET_GREG, &off)
                                     : iemNativeVarRegisterAcquire(pReNative, idxVarMem, &off);
    IEMNATIVEEMITTLBSTATE const TlbState(pReNative, &off, idxVarGCPtrMem, iSegReg, cbMem);
    uint32_t const idxLabelTlbLookup = !TlbState.fSkip
                                     ? iemNativeLabelCreate(pReNative, kIemNativeLabelType_TlbLookup, UINT32_MAX, uTlbSeqNo)
                                     : UINT32_MAX;

    /*
     * Jump to the TLB lookup code.
     */
    if (!TlbState.fSkip)
        off = iemNativeEmitJmpToLabel(pReNative, off, idxLabelTlbLookup); /** @todo short jump */

    /*
     * TlbMiss:
     *
     * Call helper to do the fetching.
     * We flush all guest register shadow copies here.
     */
    uint32_t const idxLabelTlbMiss = iemNativeLabelCreate(pReNative, kIemNativeLabelType_TlbMiss, off, uTlbSeqNo);

#ifdef IEMNATIVE_WITH_INSTRUCTION_COUNTING
    off = iemNativeEmitStoreImmToVCpuU8(pReNative, off, idxInstr, RT_UOFFSETOF(VMCPUCC, iem.s.idxTbCurInstr));
#else
    RT_NOREF(idxInstr);
#endif

#ifndef IEMNATIVE_WITH_FREE_AND_FLUSH_VOLATILE_REGS_AT_TLB_LOOKUP
    /* Save variables in volatile registers. */
    uint32_t const fHstRegsNotToSave = TlbState.getRegsNotToSave() | RT_BIT_32(idxRegMemResult) | RT_BIT_32(idxRegUnmapInfo);
    off = iemNativeVarSaveVolatileRegsPreHlpCall(pReNative, off, fHstRegsNotToSave);
#endif

    /* IEMNATIVE_CALL_ARG2_GREG = GCPtrMem - load first as it is from a variable. */
    off = iemNativeEmitLoadArgGregFromImmOrStackVar(pReNative, off, IEMNATIVE_CALL_ARG2_GREG, idxVarGCPtrMem, 0 /*cbAppend*/,
#ifndef IEMNATIVE_WITH_FREE_AND_FLUSH_VOLATILE_REGS_AT_TLB_LOOKUP
                                                    IEMNATIVE_CALL_VOLATILE_GREG_MASK, true /*fSpilledVarsInvolatileRegs*/);
#else
                                                    IEMNATIVE_CALL_VOLATILE_GREG_MASK);
#endif

    /* IEMNATIVE_CALL_ARG3_GREG = iSegReg */
    if (iSegReg != UINT8_MAX)
    {
        AssertStmt(iSegReg < 6, IEMNATIVE_DO_LONGJMP(pReNative, VERR_IEM_EMIT_BAD_SEG_REG_NO));
        off = iemNativeEmitLoadGpr8Imm(pReNative, off, IEMNATIVE_CALL_ARG3_GREG, iSegReg);
    }

#ifdef IEMNATIVE_WITH_EFLAGS_POSTPONING
    /* Do delayed EFLAGS calculations. */
    if (iSegReg == UINT8_MAX)
        off = iemNativeDoPostponedEFlagsAtTlbMiss<RT_BIT_32(IEMNATIVE_CALL_ARG2_GREG)>(pReNative, off, &TlbState,
                                                                                       fHstRegsNotToSave);
    else
        off = iemNativeDoPostponedEFlagsAtTlbMiss<  RT_BIT_32(IEMNATIVE_CALL_ARG2_GREG)
                                                  | RT_BIT_32(IEMNATIVE_CALL_ARG3_GREG)>(pReNative, off, &TlbState,
                                                                                         fHstRegsNotToSave);
#endif

    /* IEMNATIVE_CALL_ARG1_GREG = &idxVarUnmapInfo; stackslot address, load any register with result after the call. */
    int32_t const offBpDispVarUnmapInfo = iemNativeStackCalcBpDisp(iemNativeVarGetStackSlot(pReNative, idxVarUnmapInfo));
    off = iemNativeEmitLeaGprByBp(pReNative, off, IEMNATIVE_CALL_ARG1_GREG, offBpDispVarUnmapInfo);

    /* IEMNATIVE_CALL_ARG0_GREG = pVCpu */
    off = iemNativeEmitLoadGprFromGpr(pReNative, off, IEMNATIVE_CALL_ARG0_GREG, IEMNATIVE_REG_FIXED_PVMCPU);

    /* Done setting up parameters, make the call. */
    off = iemNativeEmitCallImm<true /*a_fSkipEflChecks*/>(pReNative, off, pfnFunction);

    /*
     * Put the output in the right registers.
     */
    Assert(idxRegMemResult == pVarMem->idxReg);
    if (idxRegMemResult != IEMNATIVE_CALL_RET_GREG)
        off = iemNativeEmitLoadGprFromGpr(pReNative, off, idxRegMemResult, IEMNATIVE_CALL_RET_GREG);

#ifndef IEMNATIVE_WITH_FREE_AND_FLUSH_VOLATILE_REGS_AT_TLB_LOOKUP
    /* Restore variables and guest shadow registers to volatile registers. */
    off = iemNativeVarRestoreVolatileRegsPostHlpCall(pReNative, off, fHstRegsNotToSave);
    off = iemNativeRegRestoreGuestShadowsInVolatileRegs(pReNative, off, TlbState.getActiveRegsWithShadows());
#endif

    Assert(pVarUnmapInfo->idxReg == idxRegUnmapInfo);
    off = iemNativeEmitLoadGprByBpU8(pReNative, off, idxRegUnmapInfo, offBpDispVarUnmapInfo);

#ifdef IEMNATIVE_WITH_TLB_LOOKUP
    if (!TlbState.fSkip)
    {
        /* end of tlbsmiss - Jump to the done label. */
        uint32_t const idxLabelTlbDone = iemNativeLabelCreate(pReNative, kIemNativeLabelType_TlbDone, UINT32_MAX, uTlbSeqNo);
        off = iemNativeEmitJmpToLabel(pReNative, off, idxLabelTlbDone);

        /*
         * TlbLookup:
         */
        off = iemNativeEmitTlbLookup<true>(pReNative, off, &TlbState, iSegReg, cbMem, fAlignMaskAndCtl, fAccess,
                                           idxLabelTlbLookup, idxLabelTlbMiss, idxRegMemResult);
# ifdef IEM_WITH_TLB_STATISTICS
        off = iemNativeEmitIncStamCounterInVCpu(pReNative, off, TlbState.idxReg1, TlbState.idxReg2,
                                                RT_UOFFSETOF(VMCPUCC,  iem.s.StatNativeTlbHitsForMapped));
# endif

        /* [idxVarUnmapInfo] = 0; */
        off = iemNativeEmitLoadGprImm32(pReNative, off, idxRegUnmapInfo, 0);

        /*
         * TlbDone:
         */
        iemNativeLabelDefine(pReNative, idxLabelTlbDone, off);

        TlbState.freeRegsAndReleaseVars(pReNative, idxVarGCPtrMem);

# ifndef IEMNATIVE_WITH_FREE_AND_FLUSH_VOLATILE_REGS_AT_TLB_LOOKUP
        /* Temp Hack: Flush all guest shadows in volatile registers in case of TLB miss. */
        iemNativeRegFlushGuestShadowsByHostMask(pReNative, IEMNATIVE_CALL_VOLATILE_GREG_MASK);
# endif
    }
#else
    RT_NOREF(fAccess, fAlignMaskAndCtl, idxLabelTlbMiss);
#endif

    iemNativeVarRegisterRelease(pReNative, idxVarUnmapInfo);
    iemNativeVarRegisterRelease(pReNative, idxVarMem);

    return off;
}


#define IEM_MC_MEM_COMMIT_AND_UNMAP_ATOMIC(a_bMapInfo) \
    off = iemNativeEmitMemCommitAndUnmap(pReNative, off, (a_bMapInfo), IEM_ACCESS_DATA_ATOMIC, \
                                         (uintptr_t)iemNativeHlpMemCommitAndUnmapAtomic, pCallEntry->idxInstr)

#define IEM_MC_MEM_COMMIT_AND_UNMAP_RW(a_bMapInfo) \
    off = iemNativeEmitMemCommitAndUnmap(pReNative, off, (a_bMapInfo), IEM_ACCESS_DATA_RW, \
                                         (uintptr_t)iemNativeHlpMemCommitAndUnmapRw, pCallEntry->idxInstr)

#define IEM_MC_MEM_COMMIT_AND_UNMAP_WO(a_bMapInfo) \
    off = iemNativeEmitMemCommitAndUnmap(pReNative, off, (a_bMapInfo), IEM_ACCESS_DATA_W, \
                                         (uintptr_t)iemNativeHlpMemCommitAndUnmapWo, pCallEntry->idxInstr)

#define IEM_MC_MEM_COMMIT_AND_UNMAP_RO(a_bMapInfo) \
    off = iemNativeEmitMemCommitAndUnmap(pReNative, off, (a_bMapInfo), IEM_ACCESS_DATA_R, \
                                         (uintptr_t)iemNativeHlpMemCommitAndUnmapRo, pCallEntry->idxInstr)

DECL_INLINE_THROW(uint32_t)
iemNativeEmitMemCommitAndUnmap(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxVarUnmapInfo,
                               uint32_t fAccess, uintptr_t pfnFunction, uint8_t idxInstr)
{
    /*
     * Assert sanity.
     */
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxVarUnmapInfo);
#if defined(VBOX_STRICT) || defined(RT_ARCH_AMD64)
    PIEMNATIVEVAR const pVarUnmapInfo = &pReNative->Core.aVars[IEMNATIVE_VAR_IDX_UNPACK(idxVarUnmapInfo)];
#endif
    Assert(pVarUnmapInfo->enmKind == kIemNativeVarKind_Stack);
    Assert(   pVarUnmapInfo->idxReg       < RT_ELEMENTS(pReNative->Core.aHstRegs)
           || pVarUnmapInfo->idxStackSlot < IEMNATIVE_FRAME_VAR_SLOTS); /* must be initialized */
#ifdef VBOX_STRICT
    switch (fAccess & (IEM_ACCESS_TYPE_MASK | IEM_ACCESS_ATOMIC))
    {
        case IEM_ACCESS_TYPE_READ | IEM_ACCESS_TYPE_WRITE | IEM_ACCESS_ATOMIC:
            Assert(pfnFunction == (uintptr_t)iemNativeHlpMemCommitAndUnmapAtomic); break;
        case IEM_ACCESS_TYPE_READ | IEM_ACCESS_TYPE_WRITE:
            Assert(pfnFunction == (uintptr_t)iemNativeHlpMemCommitAndUnmapRw); break;
        case IEM_ACCESS_TYPE_WRITE:
            Assert(pfnFunction == (uintptr_t)iemNativeHlpMemCommitAndUnmapWo); break;
        case IEM_ACCESS_TYPE_READ:
            Assert(pfnFunction == (uintptr_t)iemNativeHlpMemCommitAndUnmapRo); break;
        default: AssertFailed();
    }
#else
    RT_NOREF(fAccess);
#endif

    /*
     * To keep things simple we have to commit any pending writes first as we
     * may end up making calls (there shouldn't be any at this point, so this
     * is just for consistency).
     */
    /** @todo we could postpone this till we make the call and reload the
     * registers after returning from the call. Not sure if that's sensible or
     * not, though. */
    off = iemNativeRegFlushPendingWrites(pReNative, off);

    /*
     * Move/spill/flush stuff out of call-volatile registers.
     *
     * We exclude any register holding the bUnmapInfo variable, as we'll be
     * checking it after returning from the call and will free it afterwards.
     */
    /** @todo save+restore active registers and maybe guest shadows in miss
     *        scenario. */
    off = iemNativeRegMoveAndFreeAndFlushAtCall(pReNative, off, 0 /* vacate all non-volatile regs */,
                                                RT_BIT_32(IEMNATIVE_VAR_IDX_UNPACK(idxVarUnmapInfo)));

    /*
     * If idxVarUnmapInfo is zero, we can skip all this. Otherwise we'll have
     * to call the unmap helper function.
     *
     * The likelyhood of it being zero is higher than for the TLB hit when doing
     * the mapping, as a TLB miss for an well aligned and unproblematic memory
     * access should also end up with a mapping that won't need special unmapping.
     */
    /** @todo Go over iemMemMapJmp and implement the no-unmap-needed case!  That
     *        should speed up things for the pure interpreter as well when TLBs
     *        are enabled. */
#ifdef RT_ARCH_AMD64
    if (pVarUnmapInfo->idxReg == UINT8_MAX)
    {
        /* test byte [rbp - xxx], 0ffh  */
        uint8_t * const pbCodeBuf    = iemNativeInstrBufEnsure(pReNative, off, 7);
        pbCodeBuf[off++] = 0xf6;
        uint8_t const   idxStackSlot = pVarUnmapInfo->idxStackSlot;
        off = iemNativeEmitGprByBpDisp(pbCodeBuf, off, 0, iemNativeStackCalcBpDisp(idxStackSlot), pReNative);
        pbCodeBuf[off++] = 0xff;
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
    }
    else
#endif
    {
        uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxVarUnmapInfo, &off,
                                                              true /*fInitialized*/, IEMNATIVE_CALL_ARG1_GREG /*idxRegPref*/);
        off = iemNativeEmitTestAnyBitsInGpr8(pReNative, off, idxVarReg, 0xff);
        iemNativeVarRegisterRelease(pReNative, idxVarUnmapInfo);
    }
    uint32_t const offJmpFixup = off;
    off = iemNativeEmitJzToFixed(pReNative, off, off /* ASSUME jz rel8 suffices*/);

    /*
     * Call the unmap helper function.
     */
#ifdef IEMNATIVE_WITH_INSTRUCTION_COUNTING /** @todo This should be unnecessary, the mapping call will already have set it! */
    off = iemNativeEmitStoreImmToVCpuU8(pReNative, off, idxInstr, RT_UOFFSETOF(VMCPUCC, iem.s.idxTbCurInstr));
#else
    RT_NOREF(idxInstr);
#endif

    /* IEMNATIVE_CALL_ARG1_GREG = idxVarUnmapInfo (first!) */
    off = iemNativeEmitLoadArgGregFromStackVar(pReNative, off, IEMNATIVE_CALL_ARG1_GREG, idxVarUnmapInfo,
                                               0 /*offAddend*/, IEMNATIVE_CALL_VOLATILE_GREG_MASK);

    /* IEMNATIVE_CALL_ARG0_GREG = pVCpu */
    off = iemNativeEmitLoadGprFromGpr(pReNative, off, IEMNATIVE_CALL_ARG0_GREG, IEMNATIVE_REG_FIXED_PVMCPU);

    /* Done setting up parameters, make the call.
       Note! Since we can only end up here if we took a TLB miss, any postponed EFLAGS
             calculations has been done there already. Thus, a_fSkipEflChecks = true. */
    off = iemNativeEmitCallImm<true /*a_fSkipEflChecks*/>(pReNative, off, pfnFunction);

    /* The bUnmapInfo variable is implictly free by these MCs. */
    iemNativeVarFreeLocal(pReNative, idxVarUnmapInfo);

    /*
     * Done, just fixup the jump for the non-call case.
     */
    iemNativeFixupFixedJump(pReNative, offJmpFixup, off);

    return off;
}



/*********************************************************************************************************************************
*   State and Exceptions                                                                                                         *
*********************************************************************************************************************************/

#define IEM_MC_ACTUALIZE_FPU_STATE_FOR_CHANGE()     off = iemNativeEmitPrepareFpuForUse(pReNative, off, true /*fForChange*/)
#define IEM_MC_ACTUALIZE_FPU_STATE_FOR_READ()       off = iemNativeEmitPrepareFpuForUse(pReNative, off, false /*fForChange*/)

#define IEM_MC_PREPARE_SSE_USAGE()                  off = iemNativeEmitPrepareFpuForUse(pReNative, off, true /*fForChange*/)
#define IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE()     off = iemNativeEmitPrepareFpuForUse(pReNative, off, true /*fForChange*/)
#define IEM_MC_ACTUALIZE_SSE_STATE_FOR_READ()       off = iemNativeEmitPrepareFpuForUse(pReNative, off, false /*fForChange*/)

#define IEM_MC_PREPARE_AVX_USAGE()                  off = iemNativeEmitPrepareFpuForUse(pReNative, off, true /*fForChange*/)
#define IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE()     off = iemNativeEmitPrepareFpuForUse(pReNative, off, true /*fForChange*/)
#define IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ()       off = iemNativeEmitPrepareFpuForUse(pReNative, off, false /*fForChange*/)


DECL_INLINE_THROW(uint32_t) iemNativeEmitPrepareFpuForUse(PIEMRECOMPILERSTATE pReNative, uint32_t off, bool fForChange)
{
#ifndef IEMNATIVE_WITH_SIMD_FP_NATIVE_EMITTERS
    RT_NOREF(pReNative, fForChange);
#else
    if (   !(pReNative->fSimdRaiseXcptChecksEmitted & IEMNATIVE_SIMD_HOST_FP_CTRL_REG_SYNCED)
        && fForChange)
    {
# ifdef RT_ARCH_AMD64

        /* Need to save the host MXCSR the first time, and clear the exception flags. */
        if (!(pReNative->fSimdRaiseXcptChecksEmitted & IEMNATIVE_SIMD_HOST_FP_CTRL_REG_SAVED))
        {
            PIEMNATIVEINSTR pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 8);

            /* stmxcsr */
            if (IEMNATIVE_REG_FIXED_PVMCPU >= 8)
                pbCodeBuf[off++] = X86_OP_REX_B;
            pbCodeBuf[off++] = 0x0f;
            pbCodeBuf[off++] = 0xae;
            pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_MEM4, 3, IEMNATIVE_REG_FIXED_PVMCPU & 7);
            pbCodeBuf[off++] = RT_BYTE1(RT_UOFFSETOF(VMCPU, iem.s.uRegFpCtrl));
            pbCodeBuf[off++] = RT_BYTE2(RT_UOFFSETOF(VMCPU, iem.s.uRegFpCtrl));
            pbCodeBuf[off++] = RT_BYTE3(RT_UOFFSETOF(VMCPU, iem.s.uRegFpCtrl));
            pbCodeBuf[off++] = RT_BYTE4(RT_UOFFSETOF(VMCPU, iem.s.uRegFpCtrl));
            IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

            pReNative->fSimdRaiseXcptChecksEmitted |= IEMNATIVE_SIMD_HOST_FP_CTRL_REG_SAVED;
        }

        uint8_t const idxRegTmp = iemNativeRegAllocTmpPreferNonVolatile(pReNative, &off);
        uint8_t const idxRegMxCsr = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_MxCsr,
                                                                    kIemNativeGstRegUse_ReadOnly);

        /*
         * Mask any exceptions and clear the exception status and save into MXCSR,
         * taking a detour through memory here because ldmxcsr/stmxcsr don't support
         * a register source/target (sigh).
         */
        off = iemNativeEmitLoadGprFromGpr32(pReNative, off, IEMNATIVE_REG_FIXED_TMP0, idxRegMxCsr);
        off = iemNativeEmitOrGpr32ByImm(pReNative, off, IEMNATIVE_REG_FIXED_TMP0, X86_MXCSR_XCPT_MASK);
        off = iemNativeEmitAndGpr32ByImm(pReNative, off, IEMNATIVE_REG_FIXED_TMP0, ~X86_MXCSR_XCPT_FLAGS);
        off = iemNativeEmitStoreGprToVCpuU32(pReNative, off, IEMNATIVE_REG_FIXED_TMP0, RT_UOFFSETOF(VMCPU, iem.s.uRegMxcsrTmp));

        PIEMNATIVEINSTR pbCodeBuf = iemNativeInstrBufEnsure(pReNative, off, 8);

        /* ldmxcsr */
        if (IEMNATIVE_REG_FIXED_PVMCPU >= 8)
            pbCodeBuf[off++] = X86_OP_REX_B;
        pbCodeBuf[off++] = 0x0f;
        pbCodeBuf[off++] = 0xae;
        pbCodeBuf[off++] = X86_MODRM_MAKE(X86_MOD_MEM4, 2, IEMNATIVE_REG_FIXED_PVMCPU & 7);
        pbCodeBuf[off++] = RT_BYTE1(RT_UOFFSETOF(VMCPU, iem.s.uRegMxcsrTmp));
        pbCodeBuf[off++] = RT_BYTE2(RT_UOFFSETOF(VMCPU, iem.s.uRegMxcsrTmp));
        pbCodeBuf[off++] = RT_BYTE3(RT_UOFFSETOF(VMCPU, iem.s.uRegMxcsrTmp));
        pbCodeBuf[off++] = RT_BYTE4(RT_UOFFSETOF(VMCPU, iem.s.uRegMxcsrTmp));
        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);

        iemNativeRegFreeTmp(pReNative, idxRegMxCsr);
        iemNativeRegFreeTmp(pReNative, idxRegTmp);

# elif defined(RT_ARCH_ARM64)
        uint8_t const idxRegTmp = iemNativeRegAllocTmpPreferNonVolatile(pReNative, &off);

        /* Need to save the host floating point control register the first time, clear FPSR. */
        if (!(pReNative->fSimdRaiseXcptChecksEmitted & IEMNATIVE_SIMD_HOST_FP_CTRL_REG_SAVED))
        {
            PIEMNATIVEINSTR pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 2);
            pu32CodeBuf[off++] = Armv8A64MkInstrMsr(ARMV8_A64_REG_XZR, ARMV8_AARCH64_SYSREG_FPSR);
            pu32CodeBuf[off++] = Armv8A64MkInstrMrs(idxRegTmp, ARMV8_AARCH64_SYSREG_FPCR);
            off = iemNativeEmitStoreGprToVCpuU64(pReNative, off, idxRegTmp, RT_UOFFSETOF(VMCPU, iem.s.uRegFpCtrl));
            pReNative->fSimdRaiseXcptChecksEmitted |= IEMNATIVE_SIMD_HOST_FP_CTRL_REG_SAVED;
        }

        /*
         * Translate MXCSR to FPCR.
         *
         * Unfortunately we can't emulate the exact behavior of MXCSR as we can't take
         * FEAT_AFP on arm64 for granted (My M2 Macbook doesn't has it). So we can't map
         * MXCSR.DAZ to FPCR.FIZ and MXCSR.FZ to FPCR.FZ with FPCR.AH being set.
         * We can only use FPCR.FZ which will flush inputs _and_ output de-normals to zero.
         */
        /** @todo Check the host supported flags (needs additional work to get the host features from CPUM)
         *        and implement alternate handling if FEAT_AFP is present. */
        uint8_t const idxRegMxCsr = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_MxCsr,
                                                                    kIemNativeGstRegUse_ReadOnly);

        PIEMNATIVEINSTR pu32CodeBuf = iemNativeInstrBufEnsure(pReNative, off, 10);

        /* First make sure that there is nothing set for the upper 16-bits (X86_MXCSR_MM, which we don't emulate right now). */
        pu32CodeBuf[off++] = Armv8A64MkInstrUxth(idxRegTmp, idxRegMxCsr);

        /* If either MXCSR.FZ or MXCSR.DAZ is set FPCR.FZ will be set. */
        pu32CodeBuf[off++] = Armv8A64MkInstrUbfx(IEMNATIVE_REG_FIXED_TMP0, idxRegTmp, X86_MXCSR_DAZ_BIT, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrLsrImm(idxRegTmp,              idxRegTmp, X86_MXCSR_FZ_BIT);
        pu32CodeBuf[off++] = Armv8A64MkInstrOrr(idxRegTmp, idxRegTmp, IEMNATIVE_REG_FIXED_TMP0);
        pu32CodeBuf[off++] = Armv8A64MkInstrLslImm(idxRegTmp, idxRegTmp, ARMV8_FPCR_FZ_BIT);

        /*
         * Init the rounding mode, the layout differs between MXCSR.RM[14:13] and FPCR.RMode[23:22]:
         *
         * Value    MXCSR   FPCR
         *   0       RN      RN
         *   1       R-      R+
         *   2       R+      R-
         *   3       RZ      RZ
         *
         * Conversion can be achieved by switching bit positions
         */
        pu32CodeBuf[off++] = Armv8A64MkInstrLsrImm(IEMNATIVE_REG_FIXED_TMP0, idxRegMxCsr, X86_MXCSR_RC_SHIFT);
        pu32CodeBuf[off++] = Armv8A64MkInstrBfi(idxRegTmp, IEMNATIVE_REG_FIXED_TMP0, 14, 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrLsrImm(IEMNATIVE_REG_FIXED_TMP0, idxRegMxCsr, X86_MXCSR_RC_SHIFT + 1);
        pu32CodeBuf[off++] = Armv8A64MkInstrBfi(idxRegTmp, IEMNATIVE_REG_FIXED_TMP0, 13, 1);

        /* Write the value to FPCR. */
        pu32CodeBuf[off++] = Armv8A64MkInstrMsr(idxRegTmp, ARMV8_AARCH64_SYSREG_FPCR);

        IEMNATIVE_ASSERT_INSTR_BUF_ENSURE(pReNative, off);
        iemNativeRegFreeTmp(pReNative, idxRegMxCsr);
        iemNativeRegFreeTmp(pReNative, idxRegTmp);
# else
#  error "Port me"
# endif
        pReNative->fSimdRaiseXcptChecksEmitted |= IEMNATIVE_SIMD_HOST_FP_CTRL_REG_SYNCED;
    }
#endif
    return off;
}



/*********************************************************************************************************************************
*   Emitters for FPU related operations.                                                                                         *
*********************************************************************************************************************************/

#define IEM_MC_FETCH_FCW(a_u16Fcw) \
    off = iemNativeEmitFetchFpuFcw(pReNative, off, a_u16Fcw)

/** Emits code for IEM_MC_FETCH_FCW. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitFetchFpuFcw(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxDstVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxDstVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxDstVar, sizeof(uint16_t));

    uint8_t const idxReg = iemNativeVarRegisterAcquire(pReNative, idxDstVar, &off);

    /* Allocate a temporary FCW register. */
    /** @todo eliminate extra register   */
    uint8_t const idxFcwReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_FpuFcw,
                                                              kIemNativeGstRegUse_ReadOnly);

    off = iemNativeEmitLoadGprFromGpr16(pReNative, off, idxReg, idxFcwReg);

    /* Free but don't flush the FCW register. */
    iemNativeRegFreeTmp(pReNative, idxFcwReg);
    iemNativeVarRegisterRelease(pReNative, idxDstVar);

    return off;
}


#define IEM_MC_FETCH_FSW(a_u16Fsw) \
    off = iemNativeEmitFetchFpuFsw(pReNative, off, a_u16Fsw)

/** Emits code for IEM_MC_FETCH_FSW. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitFetchFpuFsw(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxDstVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxDstVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxDstVar, sizeof(uint16_t));

    uint8_t const idxReg = iemNativeVarRegisterAcquire(pReNative, idxDstVar, &off, false /*fInitialized*/);
    /* Allocate a temporary FSW register. */
    /** @todo eliminate extra register   */
    uint8_t const idxFswReg = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_FpuFsw,
                                                              kIemNativeGstRegUse_ReadOnly);

    off = iemNativeEmitLoadGprFromGpr16(pReNative, off, idxReg, idxFswReg);

    /* Free but don't flush the FSW register. */
    iemNativeRegFreeTmp(pReNative, idxFswReg);
    iemNativeVarRegisterRelease(pReNative, idxDstVar);

    return off;
}



#ifdef IEMNATIVE_WITH_SIMD_REG_ALLOCATOR


/*********************************************************************************************************************************
*   Emitters for SSE/AVX specific operations.                                                                                    *
*********************************************************************************************************************************/

#define IEM_MC_COPY_XREG_U128(a_iXRegDst, a_iXRegSrc) \
    off = iemNativeEmitSimdCopyXregU128(pReNative, off, a_iXRegDst, a_iXRegSrc)

/** Emits code for IEM_MC_COPY_XREG_U128. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdCopyXregU128(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iXRegDst, uint8_t iXRegSrc)
{
    /* This is a nop if the source and destination register are the same. */
    if (iXRegDst != iXRegSrc)
    {
        /* Allocate destination and source register. */
        uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iXRegDst),
                                                                              kIemNativeGstSimdRegLdStSz_Low128,
                                                                              kIemNativeGstRegUse_ForFullWrite);
        uint8_t const idxSimdRegSrc = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iXRegSrc),
                                                                              kIemNativeGstSimdRegLdStSz_Low128,
                                                                              kIemNativeGstRegUse_ReadOnly);

        off = iemNativeEmitSimdLoadVecRegFromVecRegU128(pReNative, off, idxSimdRegDst, idxSimdRegSrc);

        /* Free but don't flush the source and destination register. */
        iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
        iemNativeSimdRegFreeTmp(pReNative, idxSimdRegSrc);
    }

    return off;
}


#define IEM_MC_FETCH_XREG_U128(a_u128Value, a_iXReg) \
    off = iemNativeEmitSimdFetchXregU128(pReNative, off, a_u128Value, a_iXReg)

/** Emits code for IEM_MC_FETCH_XREG_U128. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdFetchXregU128(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxDstVar, uint8_t iXReg)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxDstVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxDstVar, sizeof(RTUINT128U));

    uint8_t const idxSimdRegSrc = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iXReg),
                                                                          kIemNativeGstSimdRegLdStSz_Low128, kIemNativeGstRegUse_ReadOnly);

    uint8_t const idxVarReg = iemNativeVarSimdRegisterAcquire(pReNative, idxDstVar, &off);

    off = iemNativeEmitSimdLoadVecRegFromVecRegU128(pReNative, off, idxVarReg, idxSimdRegSrc);

    /* Free but don't flush the source register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegSrc);
    iemNativeVarSimdRegisterRelease(pReNative, idxDstVar);

    return off;
}


#define IEM_MC_FETCH_XREG_U64(a_u64Value, a_iXReg, a_iQWord) \
    off = iemNativeEmitSimdFetchXregU64(pReNative, off, a_u64Value, a_iXReg, a_iQWord)

#define IEM_MC_FETCH_XREG_R64(a_r64Value, a_iXReg, a_iQWord) \
    off = iemNativeEmitSimdFetchXregU64(pReNative, off, a_r64Value, a_iXReg, a_iQWord)

/** Emits code for IEM_MC_FETCH_XREG_U64. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdFetchXregU64(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxDstVar, uint8_t iXReg, uint8_t iQWord)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxDstVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxDstVar, sizeof(uint64_t));

    uint8_t const idxSimdRegSrc = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iXReg),
                                                                          kIemNativeGstSimdRegLdStSz_Low128,
                                                                          kIemNativeGstRegUse_ReadOnly);

    iemNativeVarSetKindToStack(pReNative, idxDstVar);
    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxDstVar, &off);

    off = iemNativeEmitSimdLoadGprFromVecRegU64(pReNative, off, idxVarReg, idxSimdRegSrc, iQWord);

    /* Free but don't flush the source register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegSrc);
    iemNativeVarRegisterRelease(pReNative, idxDstVar);

    return off;
}


#define IEM_MC_FETCH_XREG_U32(a_u32Value, a_iXReg, a_iDWord) \
    off = iemNativeEmitSimdFetchXregU32(pReNative, off, a_u32Value, a_iXReg, a_iDWord)

#define IEM_MC_FETCH_XREG_R32(a_r32Value, a_iXReg, a_iDWord) \
    off = iemNativeEmitSimdFetchXregU32(pReNative, off, a_r32Value, a_iXReg, a_iDWord)

/** Emits code for IEM_MC_FETCH_XREG_U32/IEM_MC_FETCH_XREG_R32. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdFetchXregU32(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxDstVar, uint8_t iXReg, uint8_t iDWord)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxDstVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxDstVar, sizeof(uint32_t));

    uint8_t const idxSimdRegSrc = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iXReg),
                                                                          kIemNativeGstSimdRegLdStSz_Low128,
                                                                          kIemNativeGstRegUse_ReadOnly);

    iemNativeVarSetKindToStack(pReNative, idxDstVar);
    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxDstVar, &off);

    off = iemNativeEmitSimdLoadGprFromVecRegU32(pReNative, off, idxVarReg, idxSimdRegSrc, iDWord);

    /* Free but don't flush the source register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegSrc);
    iemNativeVarRegisterRelease(pReNative, idxDstVar);

    return off;
}


#define IEM_MC_FETCH_XREG_U16(a_u64Value, a_iXReg, a_iWord) \
    off = iemNativeEmitSimdFetchXregU16(pReNative, off, a_u64Value, a_iXReg, a_iWord)

/** Emits code for IEM_MC_FETCH_XREG_U16. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdFetchXregU16(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxDstVar, uint8_t iXReg, uint8_t iWord)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxDstVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxDstVar, sizeof(uint16_t));

    uint8_t const idxSimdRegSrc = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iXReg),
                                                                          kIemNativeGstSimdRegLdStSz_Low128,
                                                                          kIemNativeGstRegUse_ReadOnly);

    iemNativeVarSetKindToStack(pReNative, idxDstVar);
    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxDstVar, &off);

    off = iemNativeEmitSimdLoadGprFromVecRegU16(pReNative, off, idxVarReg, idxSimdRegSrc, iWord);

    /* Free but don't flush the source register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegSrc);
    iemNativeVarRegisterRelease(pReNative, idxDstVar);

    return off;
}


#define IEM_MC_FETCH_XREG_U8(a_u64Value, a_iXReg, a_iByte) \
    off = iemNativeEmitSimdFetchXregU8(pReNative, off, a_u64Value, a_iXReg, a_iByte)

/** Emits code for IEM_MC_FETCH_XREG_U8. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdFetchXregU8(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxDstVar, uint8_t iXReg, uint8_t iByte)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxDstVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxDstVar, sizeof(uint8_t));

    uint8_t const idxSimdRegSrc = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iXReg),
                                                                          kIemNativeGstSimdRegLdStSz_Low128,
                                                                          kIemNativeGstRegUse_ReadOnly);

    iemNativeVarSetKindToStack(pReNative, idxDstVar);
    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxDstVar, &off);

    off = iemNativeEmitSimdLoadGprFromVecRegU8(pReNative, off, idxVarReg, idxSimdRegSrc, iByte);

    /* Free but don't flush the source register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegSrc);
    iemNativeVarRegisterRelease(pReNative, idxDstVar);

    return off;
}


#define IEM_MC_STORE_XREG_U128(a_iXReg, a_u128Value) \
    off = iemNativeEmitSimdStoreXregU128(pReNative, off, a_iXReg, a_u128Value)

AssertCompileSize(X86XMMREG, sizeof(RTUINT128U));
#define IEM_MC_STORE_XREG_XMM(a_iXReg, a_XmmValue) \
    off = iemNativeEmitSimdStoreXregU128(pReNative, off, a_iXReg, a_XmmValue)


/** Emits code for IEM_MC_STORE_XREG_U128/IEM_MC_STORE_XREG_XMM. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdStoreXregU128(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iXReg, uint8_t idxSrcVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxSrcVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxSrcVar, sizeof(RTUINT128U));

    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iXReg),
                                                                          kIemNativeGstSimdRegLdStSz_Low128,
                                                                          kIemNativeGstRegUse_ForFullWrite);
    uint8_t const idxVarReg     = iemNativeVarSimdRegisterAcquire(pReNative, idxSrcVar, &off, true /*fInitialized*/);

    off = iemNativeEmitSimdLoadVecRegFromVecRegU128(pReNative, off, idxSimdRegDst, idxVarReg);

    /* Free but don't flush the source register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeVarSimdRegisterRelease(pReNative, idxSrcVar);

    return off;
}


#define IEM_MC_STORE_XREG_U64(a_iXReg, a_iQWord, a_u64Value) \
    off = iemNativeEmitSimdStoreXregUxx(pReNative, off, a_iXReg, a_u64Value, sizeof(uint64_t), a_iQWord)

#define IEM_MC_STORE_XREG_U32(a_iXReg, a_iDWord, a_u32Value) \
    off = iemNativeEmitSimdStoreXregUxx(pReNative, off, a_iXReg, a_u32Value, sizeof(uint32_t), a_iDWord)

#define IEM_MC_STORE_XREG_U16(a_iXReg, a_iWord, a_u32Value) \
    off = iemNativeEmitSimdStoreXregUxx(pReNative, off, a_iXReg, a_u32Value, sizeof(uint16_t), a_iWord)

#define IEM_MC_STORE_XREG_U8(a_iXReg, a_iByte, a_u32Value) \
    off = iemNativeEmitSimdStoreXregUxx(pReNative, off, a_iXReg, a_u32Value, sizeof(uint8_t), a_iByte)

#define IEM_MC_STORE_XREG_R32(a_iXReg, a_r32Value) \
    off = iemNativeEmitSimdStoreXregUxx(pReNative, off, a_iXReg, a_r32Value, sizeof(RTFLOAT32U), 0 /*iElem*/)

#define IEM_MC_STORE_XREG_R64(a_iXReg, a_r64Value) \
    off = iemNativeEmitSimdStoreXregUxx(pReNative, off, a_iXReg, a_r64Value, sizeof(RTFLOAT64U), 0 /*iElem*/)

/** Emits code for IEM_MC_STORE_XREG_U64/IEM_MC_STORE_XREG_U32/IEM_MC_STORE_XREG_U16/IEM_MC_STORE_XREG_U8. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdStoreXregUxx(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iXReg, uint8_t idxDstVar,
                              uint8_t cbLocal, uint8_t iElem)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxDstVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxDstVar, cbLocal);

#ifdef VBOX_STRICT
    switch (cbLocal)
    {
        case sizeof(uint64_t): Assert(iElem <  2); break;
        case sizeof(uint32_t): Assert(iElem <  4); break;
        case sizeof(uint16_t): Assert(iElem <  8); break;
        case sizeof(uint8_t):  Assert(iElem < 16); break;
        default: AssertFailed();
    }
#endif

    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iXReg),
                                                                          kIemNativeGstSimdRegLdStSz_Low128,
                                                                          kIemNativeGstRegUse_ForUpdate);
    uint8_t const idxVarReg     = iemNativeVarRegisterAcquire(pReNative, idxDstVar, &off, true /*fInitialized*/);

    switch (cbLocal)
    {
        case sizeof(uint64_t): off = iemNativeEmitSimdStoreGprToVecRegU64(pReNative, off, idxSimdRegDst, idxVarReg, iElem); break;
        case sizeof(uint32_t): off = iemNativeEmitSimdStoreGprToVecRegU32(pReNative, off, idxSimdRegDst, idxVarReg, iElem); break;
        case sizeof(uint16_t): off = iemNativeEmitSimdStoreGprToVecRegU16(pReNative, off, idxSimdRegDst, idxVarReg, iElem); break;
        case sizeof(uint8_t):  off = iemNativeEmitSimdStoreGprToVecRegU8(pReNative, off, idxSimdRegDst, idxVarReg, iElem); break;
        default: AssertFailed();
    }

    /* Free but don't flush the source register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeVarRegisterRelease(pReNative, idxDstVar);

    return off;
}


#define IEM_MC_STORE_XREG_U64_ZX_U128(a_iXReg, a_u64Value) \
    off = iemNativeEmitSimdStoreXregU64ZxU128(pReNative, off, a_iXReg, a_u64Value)

/** Emits code for IEM_MC_STORE_XREG_U64_ZX_U128. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdStoreXregU64ZxU128(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iXReg, uint8_t idxDstVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxDstVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxDstVar, sizeof(uint64_t));

    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iXReg),
                                                                          kIemNativeGstSimdRegLdStSz_Low128,
                                                                          kIemNativeGstRegUse_ForUpdate);
    uint8_t const idxVarReg     = iemNativeVarRegisterAcquire(pReNative, idxDstVar, &off, true /*fInitialized*/);

    /* Zero the vector register first, then store the 64-bit value to the lower 64-bit. */
    off = iemNativeEmitSimdZeroVecRegLowU128(pReNative, off, idxSimdRegDst);
    off = iemNativeEmitSimdStoreGprToVecRegU64(pReNative, off, idxSimdRegDst, idxVarReg, 0);

    /* Free but don't flush the source register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeVarRegisterRelease(pReNative, idxDstVar);

    return off;
}


#define IEM_MC_STORE_XREG_U32_ZX_U128(a_iXReg, a_u32Value) \
    off = iemNativeEmitSimdStoreXregU32ZxU128(pReNative, off, a_iXReg, a_u32Value)

/** Emits code for IEM_MC_STORE_XREG_U32_ZX_U128. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdStoreXregU32ZxU128(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iXReg, uint8_t idxDstVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxDstVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxDstVar, sizeof(uint32_t));

    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iXReg),
                                                                          kIemNativeGstSimdRegLdStSz_Low128,
                                                                          kIemNativeGstRegUse_ForUpdate);
    uint8_t const idxVarReg     = iemNativeVarRegisterAcquire(pReNative, idxDstVar, &off, true /*fInitialized*/);

    /* Zero the vector register first, then store the 32-bit value to the lowest 32-bit element. */
    off = iemNativeEmitSimdZeroVecRegLowU128(pReNative, off, idxSimdRegDst);
    off = iemNativeEmitSimdStoreGprToVecRegU32(pReNative, off, idxSimdRegDst, idxVarReg, 0);

    /* Free but don't flush the source register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeVarRegisterRelease(pReNative, idxDstVar);

    return off;
}


#define IEM_MC_STORE_XREG_U32_U128(a_iXReg, a_iDwDst, a_u128Value, a_iDwSrc) \
    off = iemNativeEmitSimdStoreXregU32U128(pReNative, off, a_iXReg, a_iDwDst, a_u128Value, a_iDwSrc)

/** Emits code for IEM_MC_STORE_XREG_U32_U128. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdStoreXregU32U128(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iXReg, uint8_t iDwDst,
                                  uint8_t idxSrcVar, uint8_t iDwSrc)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxSrcVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxSrcVar, sizeof(RTUINT128U));

    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iXReg),
                                                                          kIemNativeGstSimdRegLdStSz_Low128,
                                                                          kIemNativeGstRegUse_ForUpdate);
    uint8_t const idxVarReg     = iemNativeVarSimdRegisterAcquire(pReNative, idxSrcVar, &off, true /*fInitialized*/);

    off = iemNativeEmitSimdLoadGprFromVecRegU32(pReNative, off, IEMNATIVE_REG_FIXED_TMP0, idxVarReg, iDwSrc);
    off = iemNativeEmitSimdStoreGprToVecRegU32(pReNative,  off, idxSimdRegDst, IEMNATIVE_REG_FIXED_TMP0, iDwDst);

    /* Free but don't flush the destination register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeVarSimdRegisterRelease(pReNative, idxSrcVar);

    return off;
}


#define IEM_MC_COPY_YREG_U128_ZX_VLMAX(a_iYRegDst, a_iYRegSrc) \
    off = iemNativeEmitSimdCopyYregU128ZxVlmax(pReNative, off, a_iYRegDst, a_iYRegSrc)

/** Emits code for IEM_MC_COPY_YREG_U128_ZX_VLMAX. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdCopyYregU128ZxVlmax(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iYRegDst, uint8_t iYRegSrc)
{
    /*
     * The iYRegSrc == iYRegDst case needs to be treated differently here, because
     * if iYRegDst gets allocated first for the full write  it won't load the
     * actual value from CPUMCTX.  When allocating iYRegSrc afterwards it will get
     * duplicated from the already allocated host register for iYRegDst containing
     * garbage.  This will be catched by the guest register value checking in debug
     * builds.
     */
    if (iYRegDst != iYRegSrc)
    {
        /* Allocate destination and source register. */
        uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iYRegDst),
                                                                              kIemNativeGstSimdRegLdStSz_256,
                                                                              kIemNativeGstRegUse_ForFullWrite);
        uint8_t const idxSimdRegSrc =  iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iYRegSrc),
                                                                               kIemNativeGstSimdRegLdStSz_Low128,
                                                                               kIemNativeGstRegUse_ReadOnly);

        off = iemNativeEmitSimdLoadVecRegFromVecRegU128(pReNative, off, idxSimdRegDst, idxSimdRegSrc);
        off = iemNativeEmitSimdZeroVecRegHighU128(pReNative, off, idxSimdRegDst);

        /* Free but don't flush the source and destination register. */
        iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
        iemNativeSimdRegFreeTmp(pReNative, idxSimdRegSrc);
    }
    else
    {
        /* This effectively only clears the upper 128-bits of the register. */
        uint8_t const idxSimdReg = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iYRegDst),
                                                                           kIemNativeGstSimdRegLdStSz_High128,
                                                                           kIemNativeGstRegUse_ForFullWrite);

        off = iemNativeEmitSimdZeroVecRegHighU128(pReNative, off, idxSimdReg);

        /* Free but don't flush the destination register. */
        iemNativeSimdRegFreeTmp(pReNative, idxSimdReg);
    }

    return off;
}


#define IEM_MC_COPY_YREG_U256_ZX_VLMAX(a_iYRegDst, a_iYRegSrc) \
    off = iemNativeEmitSimdCopyYregU256ZxVlmax(pReNative, off, a_iYRegDst, a_iYRegSrc)

/** Emits code for IEM_MC_COPY_YREG_U256_ZX_VLMAX. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdCopyYregU256ZxVlmax(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iYRegDst, uint8_t iYRegSrc)
{
    /*
     * The iYRegSrc == iYRegDst case needs to be treated differently here, because
     * if iYRegDst gets allocated first for the full write it won't load the
     * actual value from CPUMCTX.  When allocating iYRegSrc afterwards it will get
     * duplicated from the already allocated host register for iYRegDst containing
     * garbage. This will be catched by the guest register value checking in debug
     * builds. iYRegSrc == iYRegDst would effectively only clear any upper 256-bits
     * for a zmm register we don't support yet, so this is just a nop.
     */
    if (iYRegDst != iYRegSrc)
    {
        /* Allocate destination and source register. */
        uint8_t const idxSimdRegSrc = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iYRegSrc),
                                                                              kIemNativeGstSimdRegLdStSz_256,
                                                                              kIemNativeGstRegUse_ReadOnly);
        uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iYRegDst),
                                                                              kIemNativeGstSimdRegLdStSz_256,
                                                                              kIemNativeGstRegUse_ForFullWrite);

        off = iemNativeEmitSimdLoadVecRegFromVecRegU256(pReNative, off, idxSimdRegDst, idxSimdRegSrc);

        /* Free but don't flush the source and destination register. */
        iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
        iemNativeSimdRegFreeTmp(pReNative, idxSimdRegSrc);
    }

    return off;
}


#define IEM_MC_FETCH_YREG_U128(a_u128Dst, a_iYRegSrc, a_iDQWord) \
    off = iemNativeEmitSimdFetchYregU128(pReNative, off, a_u128Dst, a_iYRegSrc, a_iDQWord)

/** Emits code for IEM_MC_FETCH_YREG_U128. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdFetchYregU128(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxDstVar, uint8_t iYReg, uint8_t iDQWord)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxDstVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxDstVar, sizeof(RTUINT128U));

    Assert(iDQWord <= 1);
    uint8_t const idxSimdRegSrc = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iYReg),
                                                                            iDQWord == 1
                                                                          ? kIemNativeGstSimdRegLdStSz_High128
                                                                          : kIemNativeGstSimdRegLdStSz_Low128,
                                                                          kIemNativeGstRegUse_ReadOnly);

    iemNativeVarSetKindToStack(pReNative, idxDstVar);
    uint8_t const idxVarReg = iemNativeVarSimdRegisterAcquire(pReNative, idxDstVar, &off);

    if (iDQWord == 1)
        off = iemNativeEmitSimdLoadVecRegLowU128FromVecRegHighU128(pReNative, off, idxVarReg, idxSimdRegSrc);
    else
        off = iemNativeEmitSimdLoadVecRegFromVecRegU128(pReNative, off, idxVarReg, idxSimdRegSrc);

    /* Free but don't flush the source register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegSrc);
    iemNativeVarSimdRegisterRelease(pReNative, idxDstVar);

    return off;
}


#define IEM_MC_FETCH_YREG_U64(a_u64Dst, a_iYRegSrc, a_iQWord) \
    off = iemNativeEmitSimdFetchYregU64(pReNative, off, a_u64Dst, a_iYRegSrc, a_iQWord)

/** Emits code for IEM_MC_FETCH_YREG_U64. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdFetchYregU64(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxDstVar, uint8_t iYReg, uint8_t iQWord)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxDstVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxDstVar, sizeof(uint64_t));

    uint8_t const idxSimdRegSrc = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iYReg),
                                                                            iQWord >= 2
                                                                          ? kIemNativeGstSimdRegLdStSz_High128
                                                                          : kIemNativeGstSimdRegLdStSz_Low128,
                                                                          kIemNativeGstRegUse_ReadOnly);

    iemNativeVarSetKindToStack(pReNative, idxDstVar);
    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxDstVar, &off);

    off = iemNativeEmitSimdLoadGprFromVecRegU64(pReNative, off, idxVarReg, idxSimdRegSrc, iQWord);

    /* Free but don't flush the source register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegSrc);
    iemNativeVarRegisterRelease(pReNative, idxDstVar);

    return off;
}


#define IEM_MC_FETCH_YREG_U32(a_u32Dst, a_iYRegSrc) \
    off = iemNativeEmitSimdFetchYregU32(pReNative, off, a_u32Dst, a_iYRegSrc, 0)

/** Emits code for IEM_MC_FETCH_YREG_U32. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdFetchYregU32(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxDstVar, uint8_t iYReg, uint8_t iDWord)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxDstVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxDstVar, sizeof(uint32_t));

    uint8_t const idxSimdRegSrc = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iYReg),
                                                                            iDWord >= 4
                                                                          ? kIemNativeGstSimdRegLdStSz_High128
                                                                          : kIemNativeGstSimdRegLdStSz_Low128,
                                                                          kIemNativeGstRegUse_ReadOnly);

    iemNativeVarSetKindToStack(pReNative, idxDstVar);
    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxDstVar, &off);

    off = iemNativeEmitSimdLoadGprFromVecRegU32(pReNative, off, idxVarReg, idxSimdRegSrc, iDWord);

    /* Free but don't flush the source register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegSrc);
    iemNativeVarRegisterRelease(pReNative, idxDstVar);

    return off;
}


#define IEM_MC_CLEAR_YREG_128_UP(a_iYReg) \
    off = iemNativeEmitSimdClearYregHighU128(pReNative, off, a_iYReg)

/** Emits code for IEM_MC_CLEAR_YREG_128_UP. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdClearYregHighU128(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iYReg)
{
    uint8_t const idxSimdReg = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iYReg),
                                                                       kIemNativeGstSimdRegLdStSz_High128,
                                                                       kIemNativeGstRegUse_ForFullWrite);

    off = iemNativeEmitSimdZeroVecRegHighU128(pReNative, off, idxSimdReg);

    /* Free but don't flush the register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdReg);

    return off;
}


#define IEM_MC_STORE_YREG_U128(a_iYRegDst, a_iDQword, a_u128Value) \
    off = iemNativeEmitSimdStoreYregU128(pReNative, off, a_iYRegDst, a_iDQword, a_u128Value)

/** Emits code for IEM_MC_STORE_YREG_U128. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdStoreYregU128(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iYReg, uint8_t iDQword, uint8_t idxSrcVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxSrcVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxSrcVar, sizeof(RTUINT128U));

    Assert(iDQword <= 1);
    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iYReg),
                                                                            iDQword == 0
                                                                          ? kIemNativeGstSimdRegLdStSz_Low128
                                                                          : kIemNativeGstSimdRegLdStSz_High128,
                                                                          kIemNativeGstRegUse_ForFullWrite);

    uint8_t const idxVarReg = iemNativeVarSimdRegisterAcquire(pReNative, idxSrcVar, &off);

    if (iDQword == 0)
        off = iemNativeEmitSimdLoadVecRegFromVecRegU128(pReNative, off, idxSimdRegDst, idxVarReg);
    else
        off = iemNativeEmitSimdLoadVecRegHighU128FromVecRegLowU128(pReNative, off, idxSimdRegDst, idxVarReg);

    /* Free but don't flush the source register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeVarSimdRegisterRelease(pReNative, idxSrcVar);

    return off;
}


#define IEM_MC_STORE_YREG_U128_ZX_VLMAX(a_iYRegDst, a_u128Src) \
    off = iemNativeEmitSimdStoreYregU128ZxVlmax(pReNative, off, a_iYRegDst, a_u128Src)

/** Emits code for IEM_MC_STORE_YREG_U128_ZX_VLMAX. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdStoreYregU128ZxVlmax(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iYReg, uint8_t idxSrcVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxSrcVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxSrcVar, sizeof(RTUINT128U));

    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iYReg),
                                                                          kIemNativeGstSimdRegLdStSz_256,
                                                                          kIemNativeGstRegUse_ForFullWrite);

    uint8_t const idxVarReg = iemNativeVarSimdRegisterAcquire(pReNative, idxSrcVar, &off);

    off = iemNativeEmitSimdLoadVecRegFromVecRegU128(pReNative, off, idxSimdRegDst, idxVarReg);
    off = iemNativeEmitSimdZeroVecRegHighU128(pReNative, off, idxSimdRegDst);

    /* Free but don't flush the source register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeVarSimdRegisterRelease(pReNative, idxSrcVar);

    return off;
}


#define IEM_MC_BROADCAST_XREG_U8_ZX_VLMAX(a_iXRegDst, a_u8Src) \
    off = iemNativeEmitSimdBroadcastXregU8ZxVlmax(pReNative, off, a_iXRegDst, a_u8Src)

/** Emits code for IEM_MC_BROADCAST_XREG_U8_ZX_VLMAX. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdBroadcastXregU8ZxVlmax(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iXReg, uint8_t idxSrcVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxSrcVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxSrcVar, sizeof(uint8_t));

    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iXReg),
                                                                          kIemNativeGstSimdRegLdStSz_256,
                                                                          kIemNativeGstRegUse_ForFullWrite);

    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxSrcVar, &off);

    off = iemNativeEmitSimdBroadcastGprToVecRegU8(pReNative, off, idxSimdRegDst, idxVarReg, false /*f256Bit*/);
    off = iemNativeEmitSimdZeroVecRegHighU128(pReNative, off, idxSimdRegDst);

    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeVarRegisterRelease(pReNative, idxSrcVar);

    return off;
}


#define IEM_MC_BROADCAST_XREG_U16_ZX_VLMAX(a_iXRegDst, a_u16Src) \
    off = iemNativeEmitSimdBroadcastXregU16ZxVlmax(pReNative, off, a_iXRegDst, a_u16Src)

/** Emits code for IEM_MC_BROADCAST_XREG_U16_ZX_VLMAX. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdBroadcastXregU16ZxVlmax(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iXReg, uint8_t idxSrcVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxSrcVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxSrcVar, sizeof(uint16_t));

    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iXReg),
                                                                          kIemNativeGstSimdRegLdStSz_256,
                                                                          kIemNativeGstRegUse_ForFullWrite);

    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxSrcVar, &off);

    off = iemNativeEmitSimdBroadcastGprToVecRegU16(pReNative, off, idxSimdRegDst, idxVarReg, false /*f256Bit*/);
    off = iemNativeEmitSimdZeroVecRegHighU128(pReNative, off, idxSimdRegDst);

    /* Free but don't flush the source register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeVarRegisterRelease(pReNative, idxSrcVar);

    return off;
}


#define IEM_MC_BROADCAST_XREG_U32_ZX_VLMAX(a_iXRegDst, a_u32Src) \
    off = iemNativeEmitSimdBroadcastXregU32ZxVlmax(pReNative, off, a_iXRegDst, a_u32Src)

/** Emits code for IEM_MC_BROADCAST_XREG_U32_ZX_VLMAX. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdBroadcastXregU32ZxVlmax(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iXReg, uint8_t idxSrcVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxSrcVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxSrcVar, sizeof(uint32_t));

    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iXReg),
                                                                          kIemNativeGstSimdRegLdStSz_256,
                                                                          kIemNativeGstRegUse_ForFullWrite);

    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxSrcVar, &off);

    off = iemNativeEmitSimdBroadcastGprToVecRegU32(pReNative, off, idxSimdRegDst, idxVarReg, false /*f256Bit*/);
    off = iemNativeEmitSimdZeroVecRegHighU128(pReNative, off, idxSimdRegDst);

    /* Free but don't flush the source register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeVarRegisterRelease(pReNative, idxSrcVar);

    return off;
}


#define IEM_MC_BROADCAST_XREG_U64_ZX_VLMAX(a_iXRegDst, a_u64Src) \
    off = iemNativeEmitSimdBroadcastXregU64ZxVlmax(pReNative, off, a_iXRegDst, a_u64Src)

/** Emits code for IEM_MC_BROADCAST_XREG_U64_ZX_VLMAX. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdBroadcastXregU64ZxVlmax(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iXReg, uint8_t idxSrcVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxSrcVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxSrcVar, sizeof(uint64_t));

    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iXReg),
                                                                          kIemNativeGstSimdRegLdStSz_256,
                                                                          kIemNativeGstRegUse_ForFullWrite);

    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxSrcVar, &off);

    off = iemNativeEmitSimdBroadcastGprToVecRegU64(pReNative, off, idxSimdRegDst, idxVarReg, false /*f256Bit*/);
    off = iemNativeEmitSimdZeroVecRegHighU128(pReNative, off, idxSimdRegDst);

    /* Free but don't flush the source register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeVarRegisterRelease(pReNative, idxSrcVar);

    return off;
}


#define IEM_MC_BROADCAST_YREG_U8_ZX_VLMAX(a_iYRegDst, a_u8Src) \
    off = iemNativeEmitSimdBroadcastYregU8ZxVlmax(pReNative, off, a_iYRegDst, a_u8Src)

/** Emits code for IEM_MC_BROADCAST_YREG_U8_ZX_VLMAX. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdBroadcastYregU8ZxVlmax(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iYReg, uint8_t idxSrcVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxSrcVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxSrcVar, sizeof(uint8_t));

    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iYReg),
                                                                          kIemNativeGstSimdRegLdStSz_256,
                                                                          kIemNativeGstRegUse_ForFullWrite);

    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxSrcVar, &off);

    off = iemNativeEmitSimdBroadcastGprToVecRegU8(pReNative, off, idxSimdRegDst, idxVarReg, true /*f256Bit*/);

    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeVarRegisterRelease(pReNative, idxSrcVar);

    return off;
}


#define IEM_MC_BROADCAST_YREG_U16_ZX_VLMAX(a_iYRegDst, a_u16Src) \
    off = iemNativeEmitSimdBroadcastYregU16ZxVlmax(pReNative, off, a_iYRegDst, a_u16Src)

/** Emits code for IEM_MC_BROADCAST_YREG_U16_ZX_VLMAX. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdBroadcastYregU16ZxVlmax(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iYReg, uint8_t idxSrcVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxSrcVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxSrcVar, sizeof(uint16_t));

    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iYReg),
                                                                          kIemNativeGstSimdRegLdStSz_256,
                                                                          kIemNativeGstRegUse_ForFullWrite);

    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxSrcVar, &off);

    off = iemNativeEmitSimdBroadcastGprToVecRegU16(pReNative, off, idxSimdRegDst, idxVarReg, true /*f256Bit*/);

    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeVarRegisterRelease(pReNative, idxSrcVar);

    return off;
}


#define IEM_MC_BROADCAST_YREG_U32_ZX_VLMAX(a_iYRegDst, a_u32Src) \
    off = iemNativeEmitSimdBroadcastYregU32ZxVlmax(pReNative, off, a_iYRegDst, a_u32Src)

/** Emits code for IEM_MC_BROADCAST_YREG_U32_ZX_VLMAX. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdBroadcastYregU32ZxVlmax(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iYReg, uint8_t idxSrcVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxSrcVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxSrcVar, sizeof(uint32_t));

    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iYReg),
                                                                          kIemNativeGstSimdRegLdStSz_256,
                                                                          kIemNativeGstRegUse_ForFullWrite);

    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxSrcVar, &off);

    off = iemNativeEmitSimdBroadcastGprToVecRegU32(pReNative, off, idxSimdRegDst, idxVarReg, true /*f256Bit*/);

    /* Free but don't flush the source register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeVarRegisterRelease(pReNative, idxSrcVar);

    return off;
}


#define IEM_MC_BROADCAST_YREG_U64_ZX_VLMAX(a_iYRegDst, a_u64Src) \
    off = iemNativeEmitSimdBroadcastYregU64ZxVlmax(pReNative, off, a_iYRegDst, a_u64Src)

/** Emits code for IEM_MC_BROADCAST_YREG_U64_ZX_VLMAX. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdBroadcastYregU64ZxVlmax(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iYReg, uint8_t idxSrcVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxSrcVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxSrcVar, sizeof(uint64_t));

    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iYReg),
                                                                          kIemNativeGstSimdRegLdStSz_256,
                                                                          kIemNativeGstRegUse_ForFullWrite);

    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxSrcVar, &off);

    off = iemNativeEmitSimdBroadcastGprToVecRegU64(pReNative, off, idxSimdRegDst, idxVarReg, true /*f256Bit*/);

    /* Free but don't flush the source register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeVarRegisterRelease(pReNative, idxSrcVar);

    return off;
}


#define IEM_MC_BROADCAST_YREG_U128_ZX_VLMAX(a_iYRegDst, a_u128Src) \
    off = iemNativeEmitSimdBroadcastYregU128ZxVlmax(pReNative, off, a_iYRegDst, a_u128Src)

/** Emits code for IEM_MC_BROADCAST_YREG_U128_ZX_VLMAX. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdBroadcastYregU128ZxVlmax(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iYReg, uint8_t idxSrcVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxSrcVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxSrcVar, sizeof(RTUINT128U));

    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iYReg),
                                                                          kIemNativeGstSimdRegLdStSz_256,
                                                                          kIemNativeGstRegUse_ForFullWrite);

    uint8_t const idxVarReg = iemNativeVarSimdRegisterAcquire(pReNative, idxSrcVar, &off);

    off = iemNativeEmitSimdBroadcastVecRegU128ToVecReg(pReNative, off, idxSimdRegDst, idxVarReg);

    /* Free but don't flush the source register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeVarSimdRegisterRelease(pReNative, idxSrcVar);

    return off;
}


#define IEM_MC_STORE_YREG_U32_ZX_VLMAX(a_iYRegDst, a_u32Src) \
    off = iemNativeEmitSimdStoreYregU32ZxVlmax(pReNative, off, a_iYRegDst, a_u32Src)

/** Emits code for IEM_MC_STORE_YREG_U32_ZX_VLMAX. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdStoreYregU32ZxVlmax(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iYReg, uint8_t idxSrcVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxSrcVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxSrcVar, sizeof(uint32_t));

    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iYReg),
                                                                          kIemNativeGstSimdRegLdStSz_256,
                                                                          kIemNativeGstRegUse_ForFullWrite);

    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxSrcVar, &off);

    off = iemNativeEmitSimdZeroVecRegU256(pReNative, off, idxSimdRegDst);
    off = iemNativeEmitSimdStoreGprToVecRegU32(pReNative, off, idxSimdRegDst, idxVarReg, 0 /*iDWord*/);

    /* Free but don't flush the source register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeVarRegisterRelease(pReNative, idxSrcVar);

    return off;
}


#define IEM_MC_STORE_YREG_U64_ZX_VLMAX(a_iYRegDst, a_u64Src) \
    off = iemNativeEmitSimdStoreYregU64ZxVlmax(pReNative, off, a_iYRegDst, a_u64Src)

/** Emits code for IEM_MC_STORE_YREG_U64_ZX_VLMAX. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdStoreYregU64ZxVlmax(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iYReg, uint8_t idxSrcVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxSrcVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxSrcVar, sizeof(uint64_t));

    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iYReg),
                                                                          kIemNativeGstSimdRegLdStSz_256,
                                                                          kIemNativeGstRegUse_ForFullWrite);

    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxSrcVar, &off);

    off = iemNativeEmitSimdZeroVecRegU256(pReNative, off, idxSimdRegDst);
    off = iemNativeEmitSimdStoreGprToVecRegU64(pReNative, off, idxSimdRegDst, idxVarReg, 0 /*iQWord*/);

    /* Free but don't flush the source register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeVarRegisterRelease(pReNative, idxSrcVar);

    return off;
}


#define IEM_MC_MERGE_YREG_U64LOCAL_U64HI_ZX_VLMAX(a_iYRegDst, a_u64Local, a_iYRegSrcHx) \
    off = iemNativeEmitSimdMergeYregU64LocalU64HiZxVlmax(pReNative, off, a_iYRegDst, a_u64Local, a_iYRegSrcHx)

/** Emits code for IEM_MC_MERGE_YREG_U64LOCAL_U64HI_ZX_VLMAX. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdMergeYregU64LocalU64HiZxVlmax(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iYRegDst, uint8_t idxSrcVar, uint8_t iYRegSrcHx)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxSrcVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxSrcVar, sizeof(uint64_t));

    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iYRegDst),
                                                                          kIemNativeGstSimdRegLdStSz_256,
                                                                          kIemNativeGstRegUse_ForFullWrite);
    uint8_t const idxSimdRegSrcHx = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iYRegSrcHx),
                                                                            kIemNativeGstSimdRegLdStSz_Low128,
                                                                            kIemNativeGstRegUse_ReadOnly);
    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxSrcVar, &off);

    off = iemNativeEmitSimdLoadVecRegFromVecRegU128(pReNative, off, idxSimdRegDst, idxSimdRegSrcHx);
    off = iemNativeEmitSimdStoreGprToVecRegU64(pReNative, off, idxSimdRegDst, idxVarReg, 0 /*iQWord*/);
    off = iemNativeEmitSimdZeroVecRegHighU128(pReNative, off, idxSimdRegDst);

    /* Free but don't flush the source and destination registers. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegSrcHx);
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeVarRegisterRelease(pReNative, idxSrcVar);

    return off;
}


#define IEM_MC_MERGE_YREG_U64LO_U64LOCAL_ZX_VLMAX(a_iYRegDst, a_iYRegSrcHx, a_u64Local) \
    off = iemNativeEmitSimdMergeYregU64LoU64LocalZxVlmax(pReNative, off, a_iYRegDst, a_iYRegSrcHx, a_u64Local)

/** Emits code for IEM_MC_MERGE_YREG_U64LO_U64LOCAL_ZX_VLMAX. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdMergeYregU64LoU64LocalZxVlmax(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iYRegDst, uint8_t iYRegSrcHx, uint8_t idxSrcVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxSrcVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxSrcVar, sizeof(uint64_t));

    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iYRegDst),
                                                                          kIemNativeGstSimdRegLdStSz_256,
                                                                          kIemNativeGstRegUse_ForFullWrite);
    uint8_t const idxSimdRegSrcHx = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iYRegSrcHx),
                                                                            kIemNativeGstSimdRegLdStSz_Low128,
                                                                            kIemNativeGstRegUse_ReadOnly);
    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxSrcVar, &off);

    off = iemNativeEmitSimdLoadVecRegFromVecRegU128(pReNative, off, idxSimdRegDst, idxSimdRegSrcHx);
    off = iemNativeEmitSimdStoreGprToVecRegU64(pReNative, off, idxSimdRegDst, idxVarReg, 1 /*iQWord*/);
    off = iemNativeEmitSimdZeroVecRegHighU128(pReNative, off, idxSimdRegDst);

    /* Free but don't flush the source and destination registers. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegSrcHx);
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeVarRegisterRelease(pReNative, idxSrcVar);

    return off;
}


#define IEM_MC_CLEAR_XREG_U32_MASK(a_iXReg, a_bMask) \
    off = iemNativeEmitSimdClearXregU32Mask(pReNative, off, a_iXReg, a_bMask)


/** Emits code for IEM_MC_CLEAR_XREG_U32_MASK. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdClearXregU32Mask(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iXReg, uint8_t bImm8Mask)
{
    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iXReg),
                                                                          kIemNativeGstSimdRegLdStSz_Low128,
                                                                          kIemNativeGstRegUse_ForUpdate);

    /** @todo r=aeichner For certain bit combinations we could reduce the number of emitted instructions. */
    if (bImm8Mask & RT_BIT(0))
        off = iemNativeEmitSimdZeroVecRegElemU32(pReNative, off, idxSimdRegDst, 0 /*iDWord*/);
    if (bImm8Mask & RT_BIT(1))
        off = iemNativeEmitSimdZeroVecRegElemU32(pReNative, off, idxSimdRegDst, 1 /*iDWord*/);
    if (bImm8Mask & RT_BIT(2))
        off = iemNativeEmitSimdZeroVecRegElemU32(pReNative, off, idxSimdRegDst, 2 /*iDWord*/);
    if (bImm8Mask & RT_BIT(3))
        off = iemNativeEmitSimdZeroVecRegElemU32(pReNative, off, idxSimdRegDst, 3 /*iDWord*/);

    /* Free but don't flush the destination register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);

    return off;
}


#define IEM_MC_FETCH_YREG_U256(a_u256Dst, a_iYRegSrc) \
    off = iemNativeEmitSimdFetchYregU256(pReNative, off, a_u256Dst, a_iYRegSrc)

#define IEM_MC_FETCH_YREG_YMM(a_uYmmDst, a_iYRegSrc) \
    off = iemNativeEmitSimdFetchYregU256(pReNative, off, a_uYmmDst, a_iYRegSrc)

/** Emits code for IEM_MC_FETCH_YREG_U256/IEM_MC_FETCH_YREG_YMM. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdFetchYregU256(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxDstVar, uint8_t iYRegSrc)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxDstVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxDstVar, sizeof(RTUINT256U));

    uint8_t const idxSimdRegSrc = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iYRegSrc),
                                                                          kIemNativeGstSimdRegLdStSz_256,
                                                                          kIemNativeGstRegUse_ReadOnly);
    uint8_t const idxVarReg = iemNativeVarSimdRegisterAcquire(pReNative, idxDstVar, &off);

    off = iemNativeEmitSimdLoadVecRegFromVecRegU256(pReNative, off, idxVarReg, idxSimdRegSrc);

    /* Free but don't flush the source register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegSrc);
    iemNativeVarSimdRegisterRelease(pReNative, idxDstVar);

    return off;
}


#define IEM_MC_STORE_YREG_U256_ZX_VLMAX(a_iYRegDst, a_u256Src) \
    off = iemNativeEmitSimdStoreYregU256ZxVlmax(pReNative, off, a_iYRegDst, a_u256Src)

#define IEM_MC_STORE_YREG_YMM_ZX_VLMAX(a_iYRegDst, a_uYmmSrc) \
    off = iemNativeEmitSimdStoreYregU256ZxVlmax(pReNative, off, a_iYRegDst, a_uYmmSrc)

/** Emits code for IEM_MC_STORE_YREG_U256_ZX_VLMAX/IEM_MC_STORE_YREG_YMM_ZX_VLMAX. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdStoreYregU256ZxVlmax(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iYRegDst, uint8_t idxSrcVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxSrcVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxSrcVar, sizeof(RTUINT256U));

    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iYRegDst),
                                                                          kIemNativeGstSimdRegLdStSz_256,
                                                                          kIemNativeGstRegUse_ForFullWrite);
    uint8_t const idxVarRegSrc  = iemNativeVarSimdRegisterAcquire(pReNative, idxSrcVar, &off, true /*fInitalized*/);

    off = iemNativeEmitSimdLoadVecRegFromVecRegU256(pReNative, off, idxSimdRegDst, idxVarRegSrc);

    /* Free but don't flush the source register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeVarSimdRegisterRelease(pReNative, idxSrcVar);

    return off;
}


#define IEM_MC_STORE_YREG_U32_U256(a_iYRegDst, a_iDwDst, a_u256Value, a_iDwSrc) \
    off = iemNativeEmitSimdStoreYregU32FromU256(pReNative, off, a_iYRegDst, a_iDwDst, a_u256Value, a_iDwSrc)


/** Emits code for IEM_MC_STORE_YREG_U32_U256. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdStoreYregU32FromU256(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iYRegDst, uint8_t iDwDst,
                                      uint8_t idxSrcVar, uint8_t iDwSrc)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxSrcVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxSrcVar, sizeof(RTUINT256U));

    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iYRegDst),
                                                                            iDwDst < 4
                                                                          ? kIemNativeGstSimdRegLdStSz_Low128
                                                                          : kIemNativeGstSimdRegLdStSz_High128,
                                                                          kIemNativeGstRegUse_ForUpdate);
    uint8_t const idxVarRegSrc  = iemNativeVarSimdRegisterAcquire(pReNative, idxSrcVar, &off, true /*fInitalized*/);
    uint8_t const idxRegTmp     = iemNativeRegAllocTmp(pReNative, &off);

    off = iemNativeEmitSimdLoadGprFromVecRegU32(pReNative, off, idxRegTmp, idxVarRegSrc, iDwSrc);
    off = iemNativeEmitSimdStoreGprToVecRegU32(pReNative, off, idxSimdRegDst, idxRegTmp, iDwDst);

    /* Free but don't flush the source register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeRegFreeTmp(pReNative, idxRegTmp);
    iemNativeVarSimdRegisterRelease(pReNative, idxSrcVar);

    return off;
}


#define IEM_MC_STORE_YREG_U64_U256(a_iYRegDst, a_iQwDst, a_u256Value, a_iQwSrc) \
    off = iemNativeEmitSimdStoreYregU64FromU256(pReNative, off, a_iYRegDst, a_iQwDst, a_u256Value, a_iQwSrc)


/** Emits code for IEM_MC_STORE_YREG_U64_U256. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdStoreYregU64FromU256(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iYRegDst, uint8_t iQwDst,
                                      uint8_t idxSrcVar, uint8_t iQwSrc)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxSrcVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxSrcVar, sizeof(RTUINT256U));

    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iYRegDst),
                                                                            iQwDst < 2
                                                                          ? kIemNativeGstSimdRegLdStSz_Low128
                                                                          : kIemNativeGstSimdRegLdStSz_High128,
                                                                          kIemNativeGstRegUse_ForUpdate);
    uint8_t const idxVarRegSrc  = iemNativeVarSimdRegisterAcquire(pReNative, idxSrcVar, &off, true /*fInitalized*/);
    uint8_t const idxRegTmp     = iemNativeRegAllocTmp(pReNative, &off);

    off = iemNativeEmitSimdLoadGprFromVecRegU64(pReNative, off, idxRegTmp, idxVarRegSrc, iQwSrc);
    off = iemNativeEmitSimdStoreGprToVecRegU64(pReNative, off, idxSimdRegDst, idxRegTmp, iQwDst);

    /* Free but don't flush the source register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeRegFreeTmp(pReNative, idxRegTmp);
    iemNativeVarSimdRegisterRelease(pReNative, idxSrcVar);

    return off;
}


#define IEM_MC_STORE_YREG_U64(a_iYRegDst, a_iQword, a_u64Value) \
    off = iemNativeEmitSimdStoreYregU64(pReNative, off, a_iYRegDst, a_iQword, a_u64Value)


/** Emits code for IEM_MC_STORE_YREG_U64. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdStoreYregU64(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iYRegDst, uint8_t iQwDst, uint8_t idxSrcVar)
{
    IEMNATIVE_ASSERT_VAR_IDX(pReNative, idxSrcVar);
    IEMNATIVE_ASSERT_VAR_SIZE(pReNative, idxSrcVar, sizeof(uint64_t));

    uint8_t const idxSimdRegDst = iemNativeSimdRegAllocTmpForGuestSimdReg(pReNative, &off, IEMNATIVEGSTSIMDREG_SIMD(iYRegDst),
                                                                            iQwDst < 2
                                                                          ? kIemNativeGstSimdRegLdStSz_Low128
                                                                          : kIemNativeGstSimdRegLdStSz_High128,
                                                                          kIemNativeGstRegUse_ForUpdate);

    uint8_t const idxVarReg = iemNativeVarRegisterAcquire(pReNative, idxSrcVar, &off);

    off = iemNativeEmitSimdStoreGprToVecRegU64(pReNative, off, idxSimdRegDst, idxVarReg, iQwDst);

    /* Free but don't flush the source register. */
    iemNativeSimdRegFreeTmp(pReNative, idxSimdRegDst);
    iemNativeVarRegisterRelease(pReNative, idxSrcVar);

    return off;
}


#define IEM_MC_CLEAR_ZREG_256_UP(a_iYReg) \
    off = iemNativeEmitSimdClearZregU256Vlmax(pReNative, off, a_iYReg)

/** Emits code for IEM_MC_CLEAR_ZREG_256_UP. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitSimdClearZregU256Vlmax(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t iYReg)
{
    RT_NOREF(pReNative, iYReg);
    /** @todo Needs to be implemented when support for AVX-512 is added. */
    return off;
}



/*********************************************************************************************************************************
*   Emitters for IEM_MC_CALL_SSE_AIMPL_XXX                                                                                       *
*********************************************************************************************************************************/

/**
 * Common worker for IEM_MC_CALL_SSE_AIMPL_XXX/IEM_MC_CALL_AVX_AIMPL_XXX.
 */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCallSseAvxAImplCommon(PIEMRECOMPILERSTATE pReNative, uint32_t off, uintptr_t pfnAImpl, uint8_t cArgs, uint8_t idxInstr)
{
    /* Grab the MXCSR register, it must not be call volatile or we end up freeing it when setting up the call below. */
    uint8_t const idxRegMxCsr = iemNativeRegAllocTmpForGuestReg(pReNative, &off, kIemNativeGstReg_MxCsr,
                                                                kIemNativeGstRegUse_ForUpdate, true /*fNoVolatileRegs*/);
    AssertRelease(!(RT_BIT_32(idxRegMxCsr) & IEMNATIVE_CALL_VOLATILE_GREG_MASK));

#if 0 /* This is not required right now as the called helper will set up the SSE/AVX state if it is an assembly one. */
    /*
     * Need to do the FPU preparation.
     */
    off = iemNativeEmitPrepareFpuForUse(pReNative, off, true /*fForChange*/);
#endif

    /*
     * Do all the call setup and cleanup.
     */
    off = iemNativeEmitCallCommon(pReNative, off, cArgs + IEM_SSE_AIMPL_HIDDEN_ARGS, IEM_SSE_AIMPL_HIDDEN_ARGS,
                                  false /*fFlushPendingWrites*/);

    /*
     * Load the MXCSR register into the first argument and mask out the current exception flags.
     */
    off = iemNativeEmitLoadGprFromGpr32(pReNative, off, IEMNATIVE_CALL_ARG0_GREG, idxRegMxCsr);
    off = iemNativeEmitAndGpr32ByImm(pReNative, off, IEMNATIVE_CALL_ARG0_GREG, ~X86_MXCSR_XCPT_FLAGS);

    /*
     * Make the call.
     */
    off = iemNativeEmitCallImm<true /*a_fSkipEflChecks*/>(pReNative, off, pfnAImpl);

    /*
     * The updated MXCSR is in the return register, update exception status flags.
     *
     * The return register is marked allocated as a temporary because it is required for the
     * exception generation check below.
     */
    Assert(!(pReNative->Core.bmHstRegs & RT_BIT_32(IEMNATIVE_CALL_RET_GREG)));
    uint8_t const idxRegTmp = iemNativeRegMarkAllocated(pReNative, IEMNATIVE_CALL_RET_GREG, kIemNativeWhat_Tmp);
    off = iemNativeEmitOrGpr32ByGpr(pReNative, off, idxRegMxCsr, idxRegTmp);

#ifndef IEMNATIVE_WITH_DELAYED_REGISTER_WRITEBACK
    /* Writeback the MXCSR register value (there is no delayed writeback for such registers at the moment). */
    off = iemNativeEmitStoreGprToVCpuU32(pReNative, off, idxRegMxCsr, RT_UOFFSETOF_DYN(VMCPU, cpum.GstCtx.XState.x87.MXCSR));
#endif

    /*
     * Make sure we don't have any outstanding guest register writes as we may
     * raise an \#UD or \#XF and all guest register must be up to date in CPUMCTX.
     */
    off = iemNativeRegFlushPendingWrites(pReNative, off);

#ifdef IEMNATIVE_WITH_INSTRUCTION_COUNTING
    off = iemNativeEmitStoreImmToVCpuU8(pReNative, off, idxInstr, RT_UOFFSETOF(VMCPUCC, iem.s.idxTbCurInstr));
#else
    RT_NOREF(idxInstr);
#endif

    /** @todo r=aeichner ANDN from BMI1 would save us a temporary and additional instruction here but I don't
     * want to assume the existence for this instruction at the moment. */
    uint8_t const idxRegTmp2 = iemNativeRegAllocTmp(pReNative, &off);

    off = iemNativeEmitLoadGprFromGpr(pReNative, off, idxRegTmp2, idxRegTmp);
    /* tmp &= X86_MXCSR_XCPT_MASK */
    off = iemNativeEmitAndGpr32ByImm(pReNative, off, idxRegTmp, X86_MXCSR_XCPT_MASK);
    /* tmp >>= X86_MXCSR_XCPT_MASK_SHIFT */
    off = iemNativeEmitShiftGprRight(pReNative, off, idxRegTmp, X86_MXCSR_XCPT_MASK_SHIFT);
    /* tmp = ~tmp */
    off = iemNativeEmitInvBitsGpr(pReNative, off, idxRegTmp, idxRegTmp, false /*f64Bit*/);
    /* tmp &= mxcsr */
    off = iemNativeEmitAndGpr32ByGpr32(pReNative, off, idxRegTmp, idxRegTmp2);
    off = iemNativeEmitTbExitIfAnyBitsSetInGpr<kIemNativeLabelType_RaiseSseAvxFpRelated>(pReNative, off, idxRegTmp,
                                                                                         X86_MXCSR_XCPT_FLAGS);

    iemNativeRegFreeTmp(pReNative, idxRegTmp2);
    iemNativeRegFreeTmp(pReNative, idxRegTmp);
    iemNativeRegFreeTmp(pReNative, idxRegMxCsr);

    return off;
}


#define IEM_MC_CALL_SSE_AIMPL_2(a_pfnAImpl, a0, a1) \
    off = iemNativeEmitCallSseAImpl2(pReNative, off, pCallEntry->idxInstr, (uintptr_t)(a_pfnAImpl), (a0), (a1))

/** Emits code for IEM_MC_CALL_SSE_AIMPL_2. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCallSseAImpl2(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxInstr, uintptr_t pfnAImpl, uint8_t idxArg0, uint8_t idxArg1)
{
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg0, 0 + IEM_SSE_AIMPL_HIDDEN_ARGS);
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg1, 1 + IEM_SSE_AIMPL_HIDDEN_ARGS);
    return iemNativeEmitCallSseAvxAImplCommon(pReNative, off, pfnAImpl, 2, idxInstr);
}


#define IEM_MC_CALL_SSE_AIMPL_3(a_pfnAImpl, a0, a1, a2) \
    off = iemNativeEmitCallSseAImpl3(pReNative, off, pCallEntry->idxInstr, (uintptr_t)(a_pfnAImpl), (a0), (a1), (a2))

/** Emits code for IEM_MC_CALL_SSE_AIMPL_3. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCallSseAImpl3(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxInstr, uintptr_t pfnAImpl,
                           uint8_t idxArg0, uint8_t idxArg1, uint8_t idxArg2)
{
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg0, 0 + IEM_SSE_AIMPL_HIDDEN_ARGS);
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg1, 1 + IEM_SSE_AIMPL_HIDDEN_ARGS);
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg2, 2 + IEM_SSE_AIMPL_HIDDEN_ARGS);
    return iemNativeEmitCallSseAvxAImplCommon(pReNative, off, pfnAImpl, 3, idxInstr);
}


/*********************************************************************************************************************************
*   Emitters for IEM_MC_CALL_AVX_AIMPL_XXX                                                                                       *
*********************************************************************************************************************************/

#define IEM_MC_CALL_AVX_AIMPL_2(a_pfnAImpl, a0, a1) \
    off = iemNativeEmitCallAvxAImpl2(pReNative, off, pCallEntry->idxInstr, (uintptr_t)(a_pfnAImpl), (a0), (a1))

/** Emits code for IEM_MC_CALL_AVX_AIMPL_2. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCallAvxAImpl2(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxInstr, uintptr_t pfnAImpl, uint8_t idxArg0, uint8_t idxArg1)
{
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg0, 0 + IEM_AVX_AIMPL_HIDDEN_ARGS);
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg1, 1 + IEM_AVX_AIMPL_HIDDEN_ARGS);
    return iemNativeEmitCallSseAvxAImplCommon(pReNative, off, pfnAImpl, 2, idxInstr);
}


#define IEM_MC_CALL_AVX_AIMPL_3(a_pfnAImpl, a0, a1, a2) \
    off = iemNativeEmitCallAvxAImpl3(pReNative, off, pCallEntry->idxInstr, (uintptr_t)(a_pfnAImpl), (a0), (a1), (a2))

/** Emits code for IEM_MC_CALL_AVX_AIMPL_3. */
DECL_INLINE_THROW(uint32_t)
iemNativeEmitCallAvxAImpl3(PIEMRECOMPILERSTATE pReNative, uint32_t off, uint8_t idxInstr, uintptr_t pfnAImpl,
                           uint8_t idxArg0, uint8_t idxArg1, uint8_t idxArg2)
{
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg0, 0 + IEM_AVX_AIMPL_HIDDEN_ARGS);
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg1, 1 + IEM_AVX_AIMPL_HIDDEN_ARGS);
    IEMNATIVE_ASSERT_ARG_VAR_IDX(pReNative, idxArg2, 2 + IEM_AVX_AIMPL_HIDDEN_ARGS);
    return iemNativeEmitCallSseAvxAImplCommon(pReNative, off, pfnAImpl, 3, idxInstr);
}


#endif /* IEMNATIVE_WITH_SIMD_REG_ALLOCATOR */


/*********************************************************************************************************************************
*   Include instruction emitters.                                                                                                *
*********************************************************************************************************************************/
#include "target-x86/IEMAllN8veEmit-x86.h"

