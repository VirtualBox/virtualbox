/* $Id: IEMMc.h 107184 2024-11-26 22:32:44Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - IEM_MC_XXX.
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

#ifndef VMM_INCLUDED_SRC_include_IEMMc_h
#define VMM_INCLUDED_SRC_include_IEMMc_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/** @name   "Microcode" macros.
 *
 * The idea is that we should be able to use the same code to interpret
 * instructions as well as recompiler instructions.  Thus this obfuscation.
 *
 * @{
 */

#define IEM_MC_BEGIN(a_fMcFlags, a_fCImplFlags) {
#define IEM_MC_END()                            }

/** Internal macro. */
#define IEM_MC_RETURN_ON_FAILURE(a_Expr) \
    do \
    { \
        VBOXSTRICTRC rcStrict2 = a_Expr; \
        if (rcStrict2 == VINF_SUCCESS) \
        { /* likely */ } \
        else \
            return rcStrict2; \
    } while (0)


/** Dummy MC that prevents native recompilation. */
#define IEM_MC_NO_NATIVE_RECOMPILE()                    ((void)0)

/** Advances RIP, finishes the instruction and returns.
 * This may include raising debug exceptions and such. */
#define IEM_MC_ADVANCE_RIP_AND_FINISH()                 return iemRegAddToRipAndFinishingClearingRF(pVCpu, IEM_GET_INSTR_LEN(pVCpu))
/** Sets RIP (may trigger \#GP), finishes the instruction and returns. */
#define IEM_MC_REL_JMP_S8_AND_FINISH(a_i8) \
    return iemRegRipRelativeJumpS8AndFinishClearingRF(pVCpu, IEM_GET_INSTR_LEN(pVCpu), (a_i8), pVCpu->iem.s.enmEffOpSize)
/** Sets RIP (may trigger \#GP), finishes the instruction and returns.
 * @note only usable in 16-bit op size mode.  */
#define IEM_MC_REL_JMP_S16_AND_FINISH(a_i16) \
    return iemRegRipRelativeJumpS16AndFinishClearingRF(pVCpu, IEM_GET_INSTR_LEN(pVCpu), (a_i16))
/** Sets RIP (may trigger \#GP), finishes the instruction and returns. */
#define IEM_MC_REL_JMP_S32_AND_FINISH(a_i32) \
    return iemRegRipRelativeJumpS32AndFinishClearingRF(pVCpu, IEM_GET_INSTR_LEN(pVCpu), (a_i32), pVCpu->iem.s.enmEffOpSize)
/** Sets RIP (may trigger \#GP), finishes the instruction and returns. */
#define IEM_MC_SET_RIP_U16_AND_FINISH(a_u16NewIP) \
    return iemRegRipJumpU16AndFinishClearingRF((pVCpu), (a_u16NewIP), IEM_GET_INSTR_LEN(pVCpu))
/** Sets RIP (may trigger \#GP), finishes the instruction and returns. */
#define IEM_MC_SET_RIP_U32_AND_FINISH(a_u32NewIP) \
    return iemRegRipJumpU32AndFinishClearingRF((pVCpu), (a_u32NewIP), IEM_GET_INSTR_LEN(pVCpu))
/** Sets RIP (may trigger \#GP), finishes the instruction and returns. */
#define IEM_MC_SET_RIP_U64_AND_FINISH(a_u64NewIP) \
    return iemRegRipJumpU64AndFinishClearingRF((pVCpu), (a_u64NewIP), IEM_GET_INSTR_LEN(pVCpu))

/** Sets RIP (may trigger \#GP), finishes the instruction and returns.
 * @note only usable in 16-bit op size mode.  */
#define IEM_MC_REL_CALL_S16_AND_FINISH(a_i16) \
    return iemRegRipRelativeCallS16AndFinishClearingRF(pVCpu, IEM_GET_INSTR_LEN(pVCpu), (a_i16))
/** Sets RIP (may trigger \#GP), finishes the instruction and returns. */
#define IEM_MC_REL_CALL_S32_AND_FINISH(a_i32) \
    return iemRegEip32RelativeCallS32AndFinishClearingRF(pVCpu, IEM_GET_INSTR_LEN(pVCpu), (a_i32))
/** Sets RIP (may trigger \#GP), finishes the instruction and returns. */
#define IEM_MC_REL_CALL_S64_AND_FINISH(a_i64) \
    return iemRegRip64RelativeCallS64AndFinishClearingRF(pVCpu, IEM_GET_INSTR_LEN(pVCpu), (a_i64))
/** Sets RIP (may trigger \#GP), finishes the instruction and returns. */
#define IEM_MC_IND_CALL_U16_AND_FINISH(a_u16NewIP) \
    return iemRegIp16IndirectCallU16AndFinishClearingRF((pVCpu), IEM_GET_INSTR_LEN(pVCpu), (a_u16NewIP))
/** Sets RIP (may trigger \#GP), finishes the instruction and returns. */
#define IEM_MC_IND_CALL_U32_AND_FINISH(a_u32NewIP) \
    return iemRegEip32IndirectCallU32AndFinishClearingRF((pVCpu), IEM_GET_INSTR_LEN(pVCpu), (a_u32NewIP))
/** Sets RIP (may trigger \#GP), finishes the instruction and returns. */
#define IEM_MC_IND_CALL_U64_AND_FINISH(a_u64NewIP) \
    return iemRegRip64IndirectCallU64AndFinishClearingRF((pVCpu), IEM_GET_INSTR_LEN(pVCpu), (a_u64NewIP))


/** Fetches the near return address from the stack, sets RIP and RSP (may trigger
 * \#GP or \#SS), finishes the instruction and returns. */
#define IEM_MC_RETN_AND_FINISH(a_cbPopArgs) \
    return iemRegRipNearReturnAndFinishClearingRF((pVCpu), IEM_GET_INSTR_LEN(pVCpu), (a_cbPopArgs), pVCpu->iem.s.enmEffOpSize)


#define IEM_MC_RAISE_DIVIDE_ERROR_IF_LOCAL_IS_ZERO(a_uVar) \
    do { \
        if (RT_LIKELY((a_uVar) != 0)) \
        { /* probable */ } \
        else return iemRaiseDivideError(pVCpu); \
    } while (0)
#define IEM_MC_MAYBE_RAISE_DEVICE_NOT_AVAILABLE()       \
    do { \
        if (RT_LIKELY(!(pVCpu->cpum.GstCtx.cr0 & (X86_CR0_EM | X86_CR0_TS)))) \
        { /* probable */ } \
        else return iemRaiseDeviceNotAvailable(pVCpu); \
    } while (0)
#define IEM_MC_MAYBE_RAISE_WAIT_DEVICE_NOT_AVAILABLE()  \
    do { \
        if (RT_LIKELY(!((pVCpu->cpum.GstCtx.cr0 & (X86_CR0_MP | X86_CR0_TS)) == (X86_CR0_MP | X86_CR0_TS)))) \
        { /* probable */ } \
        else return iemRaiseDeviceNotAvailable(pVCpu); \
    } while (0)
#define IEM_MC_MAYBE_RAISE_FPU_XCPT() \
    do { \
        if (RT_LIKELY(!(pVCpu->cpum.GstCtx.XState.x87.FSW & X86_FSW_ES))) \
        { /* probable */ } \
        else return iemRaiseMathFault(pVCpu); \
    } while (0)
#define IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT() \
    do { \
        /* Since none of the bits we compare from XCR0, CR4 and CR0 overlap, it can \
           be reduced to a single compare branch in the more probably code path. */ \
        if (RT_LIKELY(   (  (pVCpu->cpum.GstCtx.aXcr[0] & (XSAVE_C_YMM | XSAVE_C_SSE)) \
                          | (pVCpu->cpum.GstCtx.cr4     & X86_CR4_OSXSAVE) \
                          | (pVCpu->cpum.GstCtx.cr0     & X86_CR0_TS)) \
                      == (XSAVE_C_YMM | XSAVE_C_SSE | X86_CR4_OSXSAVE))) \
        { /* probable */ } \
        else if (   (pVCpu->cpum.GstCtx.aXcr[0] & (XSAVE_C_YMM | XSAVE_C_SSE)) != (XSAVE_C_YMM | XSAVE_C_SSE) \
                 || !(pVCpu->cpum.GstCtx.cr4 & X86_CR4_OSXSAVE)) \
            return iemRaiseUndefinedOpcode(pVCpu); \
        else \
            return iemRaiseDeviceNotAvailable(pVCpu); \
    } while (0)
AssertCompile(!((XSAVE_C_YMM | XSAVE_C_SSE) & X86_CR4_OSXSAVE));
AssertCompile(!((XSAVE_C_YMM | XSAVE_C_SSE) & X86_CR0_TS));
AssertCompile(!(X86_CR4_OSXSAVE & X86_CR0_TS));
#define IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT() \
    do { \
        /* Since the CR4 and CR0 bits doesn't overlap, it can be reduced to a
           single compare branch in the more probable code path. */ \
        if (RT_LIKELY(  (  (pVCpu->cpum.GstCtx.cr0 & (X86_CR0_EM | X86_CR0_TS)) \
                         | (pVCpu->cpum.GstCtx.cr4 & X86_CR4_OSFXSR)) \
                      ==                             X86_CR4_OSFXSR)) \
        { /* likely */ } \
        else if (   (pVCpu->cpum.GstCtx.cr0 & X86_CR0_EM) \
                 || !(pVCpu->cpum.GstCtx.cr4 & X86_CR4_OSFXSR)) \
            return iemRaiseUndefinedOpcode(pVCpu); \
        else \
            return iemRaiseDeviceNotAvailable(pVCpu); \
    } while (0)
AssertCompile(!((X86_CR0_EM | X86_CR0_TS) & X86_CR4_OSFXSR));
#define IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT() \
    do { \
        /* Since the two CR0 bits doesn't overlap with FSW.ES, this can be reduced to a
           single compare branch in the more probable code path. */ \
        if (RT_LIKELY(!(  (pVCpu->cpum.GstCtx.cr0 & (X86_CR0_EM | X86_CR0_TS)) \
                        | (pVCpu->cpum.GstCtx.XState.x87.FSW & X86_FSW_ES)))) \
        { /* probable */ } \
        else if (pVCpu->cpum.GstCtx.cr0 & X86_CR0_EM) \
            return iemRaiseUndefinedOpcode(pVCpu); \
        else if (pVCpu->cpum.GstCtx.cr0 & X86_CR0_TS) \
            return iemRaiseDeviceNotAvailable(pVCpu); \
        else \
            return iemRaiseMathFault(pVCpu); \
    } while (0)
AssertCompile(!((X86_CR0_EM | X86_CR0_TS) & X86_FSW_ES));
/** @todo recomp: this one is slightly problematic as the recompiler doesn't
 *        count the CPL into the TB key.  However it is safe enough for now, as
 *        it calls iemRaiseGeneralProtectionFault0 directly so no calls will be
 *        emitted for it. */
#define IEM_MC_RAISE_GP0_IF_CPL_NOT_ZERO() \
    do { \
        if (RT_LIKELY(IEM_GET_CPL(pVCpu) == 0)) { /* probable */ } \
        else return iemRaiseGeneralProtectionFault0(pVCpu); \
    } while (0)
#define IEM_MC_RAISE_GP0_IF_EFF_ADDR_UNALIGNED(a_EffAddr, a_cbAlign) \
    do { \
        if (!((a_EffAddr) & ((a_cbAlign) - 1))) { /* likely */ } \
        else return iemRaiseGeneralProtectionFault0(pVCpu); \
    } while (0)
#define IEM_MC_MAYBE_RAISE_FSGSBASE_XCPT() \
    do { \
        if (RT_LIKELY(   ((pVCpu->cpum.GstCtx.cr4 & X86_CR4_FSGSBASE) | IEM_GET_CPU_MODE(pVCpu)) \
                      == (X86_CR4_FSGSBASE | IEMMODE_64BIT))) \
        { /* probable */ } \
        else return iemRaiseUndefinedOpcode(pVCpu); \
    } while (0)
AssertCompile(X86_CR4_FSGSBASE > UINT8_MAX);
#define IEM_MC_MAYBE_RAISE_NON_CANONICAL_ADDR_GP0(a_u64Addr) \
    do { \
        if (RT_LIKELY(IEM_IS_CANONICAL(a_u64Addr))) { /* likely */ } \
        else return iemRaiseGeneralProtectionFault0(pVCpu); \
    } while (0)


#define IEM_MC_LOCAL(a_Type, a_Name)                    a_Type a_Name
#define IEM_MC_LOCAL_ASSIGN(a_Type, a_Name, a_Value)    a_Type a_Name = (a_Value)
#define IEM_MC_LOCAL_CONST(a_Type, a_Name, a_Value)     a_Type const a_Name = (a_Value)
#define IEM_MC_NOREF(a_Name)                            RT_NOREF_PV(a_Name) /* NOP/liveness hack */
#define IEM_MC_ARG(a_Type, a_Name, a_iArg)              a_Type a_Name
#define IEM_MC_ARG_CONST(a_Type, a_Name, a_Value, a_iArg)       a_Type const a_Name = (a_Value)
#define IEM_MC_ARG_LOCAL_REF(a_Type, a_Name, a_Local, a_iArg)   a_Type const a_Name = &(a_Local)
/** @note IEMAllInstPython.py duplicates the expansion. */
#define IEM_MC_ARG_EFLAGS(a_Name, a_iArg)               uint32_t const a_Name = pVCpu->cpum.GstCtx.eflags.u
/** @note IEMAllInstPython.py duplicates the expansion. */
#define IEM_MC_ARG_LOCAL_EFLAGS(a_pName, a_Name, a_iArg) \
    uint32_t  a_Name  = pVCpu->cpum.GstCtx.eflags.u; \
    uint32_t *a_pName = &a_Name
/** @note IEMAllInstPython.py duplicates the expansion. */
#define IEM_MC_LOCAL_EFLAGS(a_Name)                     uint32_t a_Name = pVCpu->cpum.GstCtx.eflags.u
#define IEM_MC_COMMIT_EFLAGS(a_EFlags) \
   do { pVCpu->cpum.GstCtx.eflags.u = (a_EFlags); Assert(pVCpu->cpum.GstCtx.eflags.u & X86_EFL_1); } while (0)
#define IEM_MC_COMMIT_EFLAGS_EX(a_EFlags, a_fEflInput, a_fEflOutput) do { \
        AssertMsg((pVCpu->cpum.GstCtx.eflags.u & ~(a_fEflOutput)) == ((a_EFlags) & ~(a_fEflOutput)),  \
                  ("eflags.u=%#x (%#x) vs %s=%#x (%#x) - diff %#x (a_fEflOutput=%#x)\n", \
                   pVCpu->cpum.GstCtx.eflags.u & ~(a_fEflOutput), pVCpu->cpum.GstCtx.eflags.u, #a_EFlags, \
                   (a_EFlags) & ~(a_fEflOutput), (a_EFlags), \
                   (pVCpu->cpum.GstCtx.eflags.u & ~(a_fEflOutput)) ^ ((a_EFlags) & ~(a_fEflOutput)), a_fEflOutput)); \
        pVCpu->cpum.GstCtx.eflags.u = (a_EFlags); \
        Assert(pVCpu->cpum.GstCtx.eflags.u & X86_EFL_1); \
    } while (0)
#define IEM_MC_COMMIT_EFLAGS_OPT(a_EFlags)                               IEM_MC_COMMIT_EFLAGS(a_EFlags)
#define IEM_MC_COMMIT_EFLAGS_OPT_EX(a_EFlags, a_fEflInput, a_fEflOutput) IEM_MC_COMMIT_EFLAGS_EX(a_EFlags, a_fEflInput, a_fEflOutput)

/** ASSUMES the source variable not used after this statement. */
#define IEM_MC_ASSIGN_TO_SMALLER(a_VarDst, a_VarSrcEol) (a_VarDst) = (a_VarSrcEol)

#define IEM_MC_FETCH_GREG_U8(a_u8Dst, a_iGReg)          (a_u8Dst)  = iemGRegFetchU8(pVCpu, (a_iGReg))
#define IEM_MC_FETCH_GREG_U8_ZX_U16(a_u16Dst, a_iGReg)  (a_u16Dst) = iemGRegFetchU8(pVCpu, (a_iGReg))
#define IEM_MC_FETCH_GREG_U8_ZX_U32(a_u32Dst, a_iGReg)  (a_u32Dst) = iemGRegFetchU8(pVCpu, (a_iGReg))
#define IEM_MC_FETCH_GREG_U8_ZX_U64(a_u64Dst, a_iGReg)  (a_u64Dst) = iemGRegFetchU8(pVCpu, (a_iGReg))
#define IEM_MC_FETCH_GREG_U8_SX_U16(a_u16Dst, a_iGReg)  (a_u16Dst) = (int8_t)iemGRegFetchU8(pVCpu, (a_iGReg))
#define IEM_MC_FETCH_GREG_U8_SX_U32(a_u32Dst, a_iGReg)  (a_u32Dst) = (int8_t)iemGRegFetchU8(pVCpu, (a_iGReg))
#define IEM_MC_FETCH_GREG_U8_SX_U64(a_u64Dst, a_iGReg)  (a_u64Dst) = (int8_t)iemGRegFetchU8(pVCpu, (a_iGReg))
#define IEM_MC_FETCH_GREG_I16(a_i16Dst, a_iGReg)        (a_i16Dst) = (int16_t)iemGRegFetchU16(pVCpu, (a_iGReg))
#define IEM_MC_FETCH_GREG_U16(a_u16Dst, a_iGReg)        (a_u16Dst) = iemGRegFetchU16(pVCpu, (a_iGReg))
#define IEM_MC_FETCH_GREG_U16_ZX_U32(a_u32Dst, a_iGReg) (a_u32Dst) = iemGRegFetchU16(pVCpu, (a_iGReg))
#define IEM_MC_FETCH_GREG_U16_ZX_U64(a_u64Dst, a_iGReg) (a_u64Dst) = iemGRegFetchU16(pVCpu, (a_iGReg))
#define IEM_MC_FETCH_GREG_U16_SX_U32(a_u32Dst, a_iGReg) (a_u32Dst) = (int16_t)iemGRegFetchU16(pVCpu, (a_iGReg))
#define IEM_MC_FETCH_GREG_U16_SX_U64(a_u64Dst, a_iGReg) (a_u64Dst) = (int16_t)iemGRegFetchU16(pVCpu, (a_iGReg))
#define IEM_MC_FETCH_GREG_I32(a_i32Dst, a_iGReg)        (a_i32Dst) = (int32_t)iemGRegFetchU32(pVCpu, (a_iGReg))
#define IEM_MC_FETCH_GREG_U32(a_u32Dst, a_iGReg)        (a_u32Dst) = iemGRegFetchU32(pVCpu, (a_iGReg))
#define IEM_MC_FETCH_GREG_U32_ZX_U64(a_u64Dst, a_iGReg) (a_u64Dst) = iemGRegFetchU32(pVCpu, (a_iGReg))
#define IEM_MC_FETCH_GREG_U32_SX_U64(a_u64Dst, a_iGReg) (a_u64Dst) = (int32_t)iemGRegFetchU32(pVCpu, (a_iGReg))
#define IEM_MC_FETCH_GREG_U64(a_u64Dst, a_iGReg)        (a_u64Dst) = iemGRegFetchU64(pVCpu, (a_iGReg))
#define IEM_MC_FETCH_GREG_U64_ZX_U64                    IEM_MC_FETCH_GREG_U64
#define IEM_MC_FETCH_GREG_PAIR_U32(a_u64Dst, a_iGRegLo, a_iGRegHi) do { \
        (a_u64Dst).s.Lo = iemGRegFetchU32(pVCpu, (a_iGRegLo)); \
        (a_u64Dst).s.Hi = iemGRegFetchU32(pVCpu, (a_iGRegHi)); \
    } while(0)
#define IEM_MC_FETCH_GREG_PAIR_U64(a_u128Dst, a_iGRegLo, a_iGRegHi) do { \
        (a_u128Dst).s.Lo = iemGRegFetchU64(pVCpu, (a_iGRegLo)); \
        (a_u128Dst).s.Hi = iemGRegFetchU64(pVCpu, (a_iGRegHi)); \
    } while(0)
#define IEM_MC_FETCH_SREG_U16(a_u16Dst, a_iSReg) do { \
        IEM_CTX_IMPORT_NORET(pVCpu, CPUMCTX_EXTRN_SREG_FROM_IDX(a_iSReg)); \
        (a_u16Dst) = iemSRegFetchU16(pVCpu, (a_iSReg)); \
    } while (0)
#define IEM_MC_FETCH_SREG_ZX_U32(a_u32Dst, a_iSReg) do { \
        IEM_CTX_IMPORT_NORET(pVCpu, CPUMCTX_EXTRN_SREG_FROM_IDX(a_iSReg)); \
        (a_u32Dst) = iemSRegFetchU16(pVCpu, (a_iSReg)); \
    } while (0)
#define IEM_MC_FETCH_SREG_ZX_U64(a_u64Dst, a_iSReg) do { \
        IEM_CTX_IMPORT_NORET(pVCpu, CPUMCTX_EXTRN_SREG_FROM_IDX(a_iSReg)); \
        (a_u64Dst) = iemSRegFetchU16(pVCpu, (a_iSReg)); \
    } while (0)
/** @todo IEM_MC_FETCH_SREG_BASE_U64 & IEM_MC_FETCH_SREG_BASE_U32 probably aren't worth it... */
#define IEM_MC_FETCH_SREG_BASE_U64(a_u64Dst, a_iSReg) do { \
        IEM_CTX_IMPORT_NORET(pVCpu, CPUMCTX_EXTRN_SREG_FROM_IDX(a_iSReg)); \
        (a_u64Dst) = iemSRegBaseFetchU64(pVCpu, (a_iSReg)); \
    } while (0)
#define IEM_MC_FETCH_SREG_BASE_U32(a_u32Dst, a_iSReg) do { \
        IEM_CTX_IMPORT_NORET(pVCpu, CPUMCTX_EXTRN_SREG_FROM_IDX(a_iSReg)); \
        (a_u32Dst) = iemSRegBaseFetchU64(pVCpu, (a_iSReg)); \
    } while (0)
/** @note Not for IOPL or IF testing or modification. */
#define IEM_MC_FETCH_EFLAGS(a_EFlags)                   (a_EFlags) = pVCpu->cpum.GstCtx.eflags.u
#define IEM_MC_FETCH_EFLAGS_EX(a_EFlags, a_fEflInput, a_fEflOutput) IEM_MC_FETCH_EFLAGS(a_EFlags)
#define IEM_MC_FETCH_EFLAGS_U8(a_EFlags)                (a_EFlags) = (uint8_t)pVCpu->cpum.GstCtx.eflags.u   /* (only LAHF) */
#define IEM_MC_FETCH_FSW(a_u16Fsw)                      (a_u16Fsw) = pVCpu->cpum.GstCtx.XState.x87.FSW
#define IEM_MC_FETCH_FCW(a_u16Fcw)                      (a_u16Fcw) = pVCpu->cpum.GstCtx.XState.x87.FCW

#define IEM_MC_STORE_GREG_U8(a_iGReg, a_u8Value)        *iemGRegRefU8( pVCpu, (a_iGReg)) = (a_u8Value)
#define IEM_MC_STORE_GREG_U16(a_iGReg, a_u16Value)      *iemGRegRefU16(pVCpu, (a_iGReg)) = (a_u16Value)
#define IEM_MC_STORE_GREG_U32(a_iGReg, a_u32Value)      *iemGRegRefU64(pVCpu, (a_iGReg)) = (uint32_t)(a_u32Value) /* clear high bits. */
#define IEM_MC_STORE_GREG_I32(a_iGReg, a_i32Value)      *iemGRegRefU64(pVCpu, (a_iGReg)) = (uint32_t)(a_i32Value) /* clear high bits. */
#define IEM_MC_STORE_GREG_U64(a_iGReg, a_u64Value)      *iemGRegRefU64(pVCpu, (a_iGReg)) = (a_u64Value)
#define IEM_MC_STORE_GREG_I64(a_iGReg, a_i64Value)      *iemGRegRefI64(pVCpu, (a_iGReg)) = (a_i64Value)
#define IEM_MC_STORE_GREG_U8_CONST                      IEM_MC_STORE_GREG_U8
#define IEM_MC_STORE_GREG_U16_CONST                     IEM_MC_STORE_GREG_U16
#define IEM_MC_STORE_GREG_U32_CONST                     IEM_MC_STORE_GREG_U32
#define IEM_MC_STORE_GREG_U64_CONST                     IEM_MC_STORE_GREG_U64
#define IEM_MC_STORE_GREG_PAIR_U32(a_iGRegLo, a_iGRegHi, a_u64Value) do { \
        *iemGRegRefU64(pVCpu, (a_iGRegLo)) = (uint32_t)(a_u64Value).s.Lo; \
        *iemGRegRefU64(pVCpu, (a_iGRegHi)) = (uint32_t)(a_u64Value).s.Hi; \
    } while(0)
#define IEM_MC_STORE_GREG_PAIR_U64(a_iGRegLo, a_iGRegHi, a_u128Value) do { \
        *iemGRegRefU64(pVCpu, (a_iGRegLo)) = (uint64_t)(a_u128Value).s.Lo; \
        *iemGRegRefU64(pVCpu, (a_iGRegHi)) = (uint64_t)(a_u128Value).s.Hi; \
    } while(0)
#define IEM_MC_CLEAR_HIGH_GREG_U64(a_iGReg)             *iemGRegRefU64(pVCpu, (a_iGReg)) &= UINT32_MAX

/** @todo IEM_MC_STORE_SREG_BASE_U64 & IEM_MC_STORE_SREG_BASE_U32 aren't worth it... */
#define IEM_MC_STORE_SREG_BASE_U64(a_iSReg, a_u64Value) do { \
        IEM_CTX_IMPORT_NORET(pVCpu, CPUMCTX_EXTRN_SREG_FROM_IDX(a_iSReg)); \
        *iemSRegBaseRefU64(pVCpu, (a_iSReg)) = (a_u64Value); \
    } while (0)
#define IEM_MC_STORE_SREG_BASE_U32(a_iSReg, a_u32Value) do { \
        IEM_CTX_IMPORT_NORET(pVCpu, CPUMCTX_EXTRN_SREG_FROM_IDX(a_iSReg)); \
        *iemSRegBaseRefU64(pVCpu, (a_iSReg)) = (uint32_t)(a_u32Value); /* clear high bits. */ \
    } while (0)
#define IEM_MC_STORE_FPUREG_R80_SRC_REF(a_iSt, a_pr80Src) \
    do { pVCpu->cpum.GstCtx.XState.x87.aRegs[a_iSt].r80 = *(a_pr80Src); } while (0)


#define IEM_MC_REF_GREG_U8(a_pu8Dst, a_iGReg)           (a_pu8Dst)  = iemGRegRefU8( pVCpu, (a_iGReg))
#define IEM_MC_REF_GREG_U8_CONST(a_pu8Dst, a_iGReg)     (a_pu8Dst)  = (uint8_t  const *)iemGRegRefU8( pVCpu, (a_iGReg))
#define IEM_MC_REF_GREG_U16(a_pu16Dst, a_iGReg)         (a_pu16Dst) = iemGRegRefU16(pVCpu, (a_iGReg))
#define IEM_MC_REF_GREG_U16_CONST(a_pu16Dst, a_iGReg)   (a_pu16Dst) = (uint16_t const *)iemGRegRefU16(pVCpu, (a_iGReg))
/** @todo User of IEM_MC_REF_GREG_U32 needs to clear the high bits on commit.
 *        Use IEM_MC_CLEAR_HIGH_GREG_U64! */
#define IEM_MC_REF_GREG_U32(a_pu32Dst, a_iGReg)         (a_pu32Dst) = iemGRegRefU32(pVCpu, (a_iGReg))
#define IEM_MC_REF_GREG_U32_CONST(a_pu32Dst, a_iGReg)   (a_pu32Dst) = (uint32_t const *)iemGRegRefU32(pVCpu, (a_iGReg))
#define IEM_MC_REF_GREG_I32(a_pi32Dst, a_iGReg)         (a_pi32Dst) = (int32_t        *)iemGRegRefU32(pVCpu, (a_iGReg))
#define IEM_MC_REF_GREG_I32_CONST(a_pi32Dst, a_iGReg)   (a_pi32Dst) = (int32_t  const *)iemGRegRefU32(pVCpu, (a_iGReg))
#define IEM_MC_REF_GREG_U64(a_pu64Dst, a_iGReg)         (a_pu64Dst) = iemGRegRefU64(pVCpu, (a_iGReg))
#define IEM_MC_REF_GREG_U64_CONST(a_pu64Dst, a_iGReg)   (a_pu64Dst) = (uint64_t const *)iemGRegRefU64(pVCpu, (a_iGReg))
#define IEM_MC_REF_GREG_I64(a_pi64Dst, a_iGReg)         (a_pi64Dst) = (int64_t        *)iemGRegRefU64(pVCpu, (a_iGReg))
#define IEM_MC_REF_GREG_I64_CONST(a_pi64Dst, a_iGReg)   (a_pi64Dst) = (int64_t  const *)iemGRegRefU64(pVCpu, (a_iGReg))
/** @note Not for IOPL or IF testing or modification.
 * @note Must preserve any undefined bits, see CPUMX86EFLAGS! */
#define IEM_MC_REF_EFLAGS(a_pEFlags)                    (a_pEFlags) = &pVCpu->cpum.GstCtx.eflags.uBoth
#define IEM_MC_REF_EFLAGS_EX(a_pEFlags, a_fEflInput, a_fEflOutput) IEM_MC_REF_EFLAGS(a_pEFlags)

#define IEM_MC_ADD_GREG_U16(a_iGReg, a_u16Value)        *iemGRegRefU16(pVCpu, (a_iGReg)) += (a_u16Value)
#define IEM_MC_ADD_GREG_U32(a_iGReg, a_u32Value) \
    do { \
        uint32_t *pu32Reg = iemGRegRefU32(pVCpu, (a_iGReg)); \
        *pu32Reg += (a_u32Value); \
        pu32Reg[1] = 0; /* implicitly clear the high bit. */ \
    } while (0)
#define IEM_MC_ADD_GREG_U64(a_iGReg, a_u64Value)        *iemGRegRefU64(pVCpu, (a_iGReg)) += (a_u64Value)

#define IEM_MC_SUB_GREG_U16(a_iGReg, a_u8Const)         *iemGRegRefU16(pVCpu, (a_iGReg)) -= (a_u8Const)
#define IEM_MC_SUB_GREG_U32(a_iGReg, a_u8Const) \
    do { \
        uint32_t *pu32Reg = iemGRegRefU32(pVCpu, (a_iGReg)); \
        *pu32Reg -= (a_u8Const); \
        pu32Reg[1] = 0; /* implicitly clear the high bit. */ \
    } while (0)
#define IEM_MC_SUB_GREG_U64(a_iGReg, a_u8Const)          *iemGRegRefU64(pVCpu, (a_iGReg)) -= (a_u8Const)
#define IEM_MC_SUB_LOCAL_U16(a_u16Value, a_u16Const)     do { (a_u16Value) -= a_u16Const; } while (0)

#define IEM_MC_ADD_GREG_U8_TO_LOCAL(a_u8Value, a_iGReg)    do { (a_u8Value)  += iemGRegFetchU8( pVCpu, (a_iGReg)); } while (0)
#define IEM_MC_ADD_GREG_U16_TO_LOCAL(a_u16Value, a_iGReg)  do { (a_u16Value) += iemGRegFetchU16(pVCpu, (a_iGReg)); } while (0)
#define IEM_MC_ADD_GREG_U32_TO_LOCAL(a_u32Value, a_iGReg)  do { (a_u32Value) += iemGRegFetchU32(pVCpu, (a_iGReg)); } while (0)
#define IEM_MC_ADD_GREG_U64_TO_LOCAL(a_u64Value, a_iGReg)  do { (a_u64Value) += iemGRegFetchU64(pVCpu, (a_iGReg)); } while (0)
#define IEM_MC_ADD_LOCAL_S16_TO_EFF_ADDR(a_EffAddr, a_i16) do { (a_EffAddr) += (a_i16); } while (0)
#define IEM_MC_ADD_LOCAL_S32_TO_EFF_ADDR(a_EffAddr, a_i32) do { (a_EffAddr) += (a_i32); } while (0)
#define IEM_MC_ADD_LOCAL_S64_TO_EFF_ADDR(a_EffAddr, a_i64) do { (a_EffAddr) += (a_i64); } while (0)

#define IEM_MC_AND_LOCAL_U8(a_u8Local, a_u8Mask)        do { (a_u8Local)  &= (a_u8Mask);  } while (0)
#define IEM_MC_AND_LOCAL_U16(a_u16Local, a_u16Mask)     do { (a_u16Local) &= (a_u16Mask); } while (0)
#define IEM_MC_AND_LOCAL_U32(a_u32Local, a_u32Mask)     do { (a_u32Local) &= (a_u32Mask); } while (0)
#define IEM_MC_AND_LOCAL_U64(a_u64Local, a_u64Mask)     do { (a_u64Local) &= (a_u64Mask); } while (0)

#define IEM_MC_AND_ARG_U16(a_u16Arg, a_u16Mask)         do { (a_u16Arg) &= (a_u16Mask); } while (0)
#define IEM_MC_AND_ARG_U32(a_u32Arg, a_u32Mask)         do { (a_u32Arg) &= (a_u32Mask); } while (0)
#define IEM_MC_AND_ARG_U64(a_u64Arg, a_u64Mask)         do { (a_u64Arg) &= (a_u64Mask); } while (0)

#define IEM_MC_OR_LOCAL_U8(a_u8Local, a_u8Mask)         do { (a_u8Local)  |= (a_u8Mask);  } while (0)
#define IEM_MC_OR_LOCAL_U16(a_u16Local, a_u16Mask)      do { (a_u16Local) |= (a_u16Mask); } while (0)
#define IEM_MC_OR_LOCAL_U32(a_u32Local, a_u32Mask)      do { (a_u32Local) |= (a_u32Mask); } while (0)

#define IEM_MC_SAR_LOCAL_S16(a_i16Local, a_cShift)      do { (a_i16Local) >>= (a_cShift);  } while (0)
#define IEM_MC_SAR_LOCAL_S32(a_i32Local, a_cShift)      do { (a_i32Local) >>= (a_cShift);  } while (0)
#define IEM_MC_SAR_LOCAL_S64(a_i64Local, a_cShift)      do { (a_i64Local) >>= (a_cShift);  } while (0)

#define IEM_MC_SHR_LOCAL_U8(a_u8Local, a_cShift)        do { (a_u8Local)  >>= (a_cShift);  } while (0)

#define IEM_MC_SHL_LOCAL_S16(a_i16Local, a_cShift)      do { (a_i16Local) <<= (a_cShift);  } while (0)
#define IEM_MC_SHL_LOCAL_S32(a_i32Local, a_cShift)      do { (a_i32Local) <<= (a_cShift);  } while (0)
#define IEM_MC_SHL_LOCAL_S64(a_i64Local, a_cShift)      do { (a_i64Local) <<= (a_cShift);  } while (0)

#define IEM_MC_AND_2LOCS_U32(a_u32Local, a_u32Mask)     do { (a_u32Local) &= (a_u32Mask); } while (0)

#define IEM_MC_OR_2LOCS_U32(a_u32Local, a_u32Mask)      do { (a_u32Local) |= (a_u32Mask); } while (0)

#define IEM_MC_AND_GREG_U8(a_iGReg, a_u8Value)          *iemGRegRefU8( pVCpu, (a_iGReg)) &= (a_u8Value)
#define IEM_MC_AND_GREG_U16(a_iGReg, a_u16Value)        *iemGRegRefU16(pVCpu, (a_iGReg)) &= (a_u16Value)
#define IEM_MC_AND_GREG_U32(a_iGReg, a_u32Value) \
    do { \
        uint32_t *pu32Reg = iemGRegRefU32(pVCpu, (a_iGReg)); \
        *pu32Reg &= (a_u32Value); \
        pu32Reg[1] = 0; /* implicitly clear the high bit. */ \
    } while (0)
#define IEM_MC_AND_GREG_U64(a_iGReg, a_u64Value)        *iemGRegRefU64(pVCpu, (a_iGReg)) &= (a_u64Value)

#define IEM_MC_OR_GREG_U8(a_iGReg, a_u8Value)           *iemGRegRefU8( pVCpu, (a_iGReg)) |= (a_u8Value)
#define IEM_MC_OR_GREG_U16(a_iGReg, a_u16Value)         *iemGRegRefU16(pVCpu, (a_iGReg)) |= (a_u16Value)
#define IEM_MC_OR_GREG_U32(a_iGReg, a_u32Value) \
    do { \
        uint32_t *pu32Reg = iemGRegRefU32(pVCpu, (a_iGReg)); \
        *pu32Reg |= (a_u32Value); \
        pu32Reg[1] = 0; /* implicitly clear the high bit. */ \
    } while (0)
#define IEM_MC_OR_GREG_U64(a_iGReg, a_u64Value)         *iemGRegRefU64(pVCpu, (a_iGReg)) |= (a_u64Value)

#define IEM_MC_BSWAP_LOCAL_U16(a_u16Local)              (a_u16Local) = RT_BSWAP_U16((a_u16Local));
#define IEM_MC_BSWAP_LOCAL_U32(a_u32Local)              (a_u32Local) = RT_BSWAP_U32((a_u32Local));
#define IEM_MC_BSWAP_LOCAL_U64(a_u64Local)              (a_u64Local) = RT_BSWAP_U64((a_u64Local));

/** @note Not for IOPL or IF modification. */
#define IEM_MC_SET_EFL_BIT(a_fBit)                      do { pVCpu->cpum.GstCtx.eflags.u |= (a_fBit); } while (0)
/** @note Not for IOPL or IF modification. */
#define IEM_MC_CLEAR_EFL_BIT(a_fBit)                    do { pVCpu->cpum.GstCtx.eflags.u &= ~(a_fBit); } while (0)
/** @note Not for IOPL or IF modification. */
#define IEM_MC_FLIP_EFL_BIT(a_fBit)                     do { pVCpu->cpum.GstCtx.eflags.u ^= (a_fBit); } while (0)

#define IEM_MC_CLEAR_FSW_EX()   do { pVCpu->cpum.GstCtx.XState.x87.FSW &= X86_FSW_C_MASK | X86_FSW_TOP_MASK; } while (0)

/** Switches the FPU state to MMX mode (FSW.TOS=0, FTW=0) if necessary. */
#define IEM_MC_FPU_TO_MMX_MODE() do { \
        iemFpuRotateStackSetTop(&pVCpu->cpum.GstCtx.XState.x87, 0); \
        pVCpu->cpum.GstCtx.XState.x87.FSW &= ~X86_FSW_TOP_MASK; \
        pVCpu->cpum.GstCtx.XState.x87.FTW  = 0xff; \
    } while (0)

/** Switches the FPU state from MMX mode (FSW.TOS=0, FTW=0xffff). */
#define IEM_MC_FPU_FROM_MMX_MODE() do { \
        iemFpuRotateStackSetTop(&pVCpu->cpum.GstCtx.XState.x87, 0); \
        pVCpu->cpum.GstCtx.XState.x87.FSW &= ~X86_FSW_TOP_MASK; \
        pVCpu->cpum.GstCtx.XState.x87.FTW  = 0; \
    } while (0)

#define IEM_MC_FETCH_MREG_U64(a_u64Value, a_iMReg) \
    do { (a_u64Value) = pVCpu->cpum.GstCtx.XState.x87.aRegs[(a_iMReg)].mmx; } while (0)
#define IEM_MC_FETCH_MREG_U32(a_u32Value, a_iMReg, a_iDWord) \
    do { (a_u32Value) = pVCpu->cpum.GstCtx.XState.x87.aRegs[(a_iMReg)].au32[a_iDWord]; } while (0)
#define IEM_MC_FETCH_MREG_U16(a_u16Value, a_iMReg, a_iWord) \
    do { (a_u16Value) = pVCpu->cpum.GstCtx.XState.x87.aRegs[(a_iMReg)].au16[a_iWord]; } while (0)
#define IEM_MC_FETCH_MREG_U8(a_u8Value, a_iMReg, a_iByte) \
    do { (a_u8Value)  = pVCpu->cpum.GstCtx.XState.x87.aRegs[(a_iMReg)].au8[a_iByte]; } while (0)
#define IEM_MC_STORE_MREG_U64(a_iMReg, a_u64Value) \
    do { pVCpu->cpum.GstCtx.XState.x87.aRegs[(a_iMReg)].mmx = (a_u64Value); \
         pVCpu->cpum.GstCtx.XState.x87.aRegs[(a_iMReg)].au32[2] = 0xffff; \
    } while (0)
#define IEM_MC_STORE_MREG_U32(a_iMReg, a_iDword, a_u32Value) \
    do { pVCpu->cpum.GstCtx.XState.x87.aRegs[(a_iMReg)].au32[(a_iDword)] = (a_u32Value); \
         pVCpu->cpum.GstCtx.XState.x87.aRegs[(a_iMReg)].au32[2] = 0xffff; \
    } while (0)
#define IEM_MC_STORE_MREG_U16(a_iMReg, a_iWord, a_u16Value) \
    do { pVCpu->cpum.GstCtx.XState.x87.aRegs[(a_iMReg)].au16[(a_iWord)] = (a_u16Value); \
         pVCpu->cpum.GstCtx.XState.x87.aRegs[(a_iMReg)].au32[2] = 0xffff; \
    } while (0)
#define IEM_MC_STORE_MREG_U8(a_iMReg, a_iByte, a_u8Value) \
    do { pVCpu->cpum.GstCtx.XState.x87.aRegs[(a_iMReg)].au8[(a_iByte)] = (a_u8Value); \
         pVCpu->cpum.GstCtx.XState.x87.aRegs[(a_iMReg)].au32[2] = 0xffff; \
    } while (0)
#define IEM_MC_STORE_MREG_U32_ZX_U64(a_iMReg, a_u32Value) \
    do { pVCpu->cpum.GstCtx.XState.x87.aRegs[(a_iMReg)].mmx = (uint32_t)(a_u32Value); \
         pVCpu->cpum.GstCtx.XState.x87.aRegs[(a_iMReg)].au32[2] = 0xffff; \
    } while (0)
#define IEM_MC_REF_MREG_U64(a_pu64Dst, a_iMReg) /** @todo need to set high word to 0xffff on commit (see IEM_MC_STORE_MREG_U64) */ \
        (a_pu64Dst) = (&pVCpu->cpum.GstCtx.XState.x87.aRegs[(a_iMReg)].mmx)
#define IEM_MC_REF_MREG_U64_CONST(a_pu64Dst, a_iMReg) \
        (a_pu64Dst) = ((uint64_t const *)&pVCpu->cpum.GstCtx.XState.x87.aRegs[(a_iMReg)].mmx)
#define IEM_MC_REF_MREG_U32_CONST(a_pu32Dst, a_iMReg) \
        (a_pu32Dst) = ((uint32_t const *)&pVCpu->cpum.GstCtx.XState.x87.aRegs[(a_iMReg)].mmx)
#define IEM_MC_MODIFIED_MREG(a_iMReg) \
    do { pVCpu->cpum.GstCtx.XState.x87.aRegs[(a_iMReg)].au32[2] = 0xffff; } while (0)
#define IEM_MC_MODIFIED_MREG_BY_REF(a_pu64Dst) \
    do { ((uint32_t *)(a_pu64Dst))[2] = 0xffff; } while (0)

#define IEM_MC_CLEAR_XREG_U32_MASK(a_iXReg, a_bMask) \
    do { if ((a_bMask) & (1 << 0)) pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au32[0] = 0; \
         if ((a_bMask) & (1 << 1)) pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au32[1] = 0; \
         if ((a_bMask) & (1 << 2)) pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au32[2] = 0; \
         if ((a_bMask) & (1 << 3)) pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au32[3] = 0; \
    } while (0)
#define IEM_MC_FETCH_XREG_U128(a_u128Value, a_iXReg) \
    do { (a_u128Value).au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au64[0]; \
         (a_u128Value).au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au64[1]; \
    } while (0)
#define IEM_MC_FETCH_XREG_XMM(a_XmmValue, a_iXReg) \
    do { (a_XmmValue).au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au64[0]; \
         (a_XmmValue).au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au64[1]; \
    } while (0)
#define IEM_MC_FETCH_XREG_U64(a_u64Value, a_iXReg, a_iQWord) \
    do { (a_u64Value) = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au64[(a_iQWord)]; } while (0)
#define IEM_MC_FETCH_XREG_R64(a_r64Value, a_iXReg, a_iQWord) \
    do { (a_r64Value) = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].ar64[(a_iQWord)]; } while (0)
#define IEM_MC_FETCH_XREG_U32(a_u32Value, a_iXReg, a_iDWord) \
    do { (a_u32Value) = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au32[(a_iDWord)]; } while (0)
#define IEM_MC_FETCH_XREG_R32(a_r32Value, a_iXReg, a_iDWord) \
    do { (a_r32Value) = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].ar32[(a_iDWord)]; } while (0)
#define IEM_MC_FETCH_XREG_U16(a_u16Value, a_iXReg, a_iWord) \
    do { (a_u16Value) = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au16[(a_iWord)]; } while (0)
#define IEM_MC_FETCH_XREG_U8( a_u8Value,  a_iXReg, a_iByte) \
    do { (a_u8Value)  = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au8[(a_iByte)]; } while (0)
#define IEM_MC_FETCH_XREG_PAIR_U128(a_Dst, a_iXReg1, a_iXReg2) \
    do { (a_Dst).uSrc1.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
         (a_Dst).uSrc1.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
         (a_Dst).uSrc2.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg2)].au64[0]; \
         (a_Dst).uSrc2.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg2)].au64[1]; \
    } while (0)
#define IEM_MC_FETCH_XREG_PAIR_XMM(a_Dst, a_iXReg1, a_iXReg2) \
    do { (a_Dst).uSrc1.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
         (a_Dst).uSrc1.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
         (a_Dst).uSrc2.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg2)].au64[0]; \
         (a_Dst).uSrc2.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg2)].au64[1]; \
    } while (0)
#define IEM_MC_FETCH_XREG_PAIR_U128_AND_RAX_RDX_U64(a_Dst, a_iXReg1, a_iXReg2) \
    do { (a_Dst).uSrc1.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
         (a_Dst).uSrc1.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
         (a_Dst).uSrc2.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg2)].au64[0]; \
         (a_Dst).uSrc2.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg2)].au64[1]; \
         (a_Dst).u64Rax        = pVCpu->cpum.GstCtx.rax; \
         (a_Dst).u64Rdx        = pVCpu->cpum.GstCtx.rdx; \
    } while (0)
#define IEM_MC_FETCH_XREG_PAIR_U128_AND_EAX_EDX_U32_SX_U64(a_Dst, a_iXReg1, a_iXReg2) \
    do { (a_Dst).uSrc1.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
         (a_Dst).uSrc1.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
         (a_Dst).uSrc2.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg2)].au64[0]; \
         (a_Dst).uSrc2.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg2)].au64[1]; \
         (a_Dst).u64Rax        = (int64_t)(int32_t)pVCpu->cpum.GstCtx.eax; \
         (a_Dst).u64Rdx        = (int64_t)(int32_t)pVCpu->cpum.GstCtx.edx; \
    } while (0)
#define IEM_MC_STORE_XREG_U128(a_iXReg, a_u128Value) \
    do { pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au64[0] = (a_u128Value).au64[0]; \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au64[1] = (a_u128Value).au64[1]; \
    } while (0)
#define IEM_MC_STORE_XREG_XMM(a_iXReg, a_XmmValue) \
    do { pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au64[0] = (a_XmmValue).au64[0]; \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au64[1] = (a_XmmValue).au64[1]; \
    } while (0)
#define IEM_MC_STORE_XREG_XMM_U32(a_iXReg, a_iDword, a_XmmValue) \
    do { pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au32[(a_iDword)] = (a_XmmValue).au32[(a_iDword)]; } while (0)
#define IEM_MC_STORE_XREG_XMM_U64(a_iXReg, a_iQword, a_XmmValue) \
    do { pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au64[(a_iQword)] = (a_XmmValue).au64[(a_iQword)]; } while (0)
#define IEM_MC_STORE_XREG_U64(a_iXReg, a_iQword, a_u64Value) \
    do { pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au64[(a_iQword)] = (a_u64Value); } while (0)
#define IEM_MC_STORE_XREG_U32(a_iXReg, a_iDword, a_u32Value) \
    do { pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au32[(a_iDword)] = (a_u32Value); } while (0)
#define IEM_MC_STORE_XREG_U16(a_iXReg, a_iWord, a_u16Value) \
    do { pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au16[(a_iWord)]  = (a_u16Value); } while (0)
#define IEM_MC_STORE_XREG_U8(a_iXReg,  a_iByte, a_u8Value) \
    do { pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au8[(a_iByte)]   = (a_u8Value);  } while (0)

#define IEM_MC_STORE_XREG_U64_ZX_U128(a_iXReg, a_u64Value) \
    do { pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au64[0] = (a_u64Value); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au64[1] = 0; \
    } while (0)

#define IEM_MC_STORE_XREG_U32_U128(a_iXReg, a_iDwDst, a_u128Value, a_iDwSrc) \
    do { pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au32[(a_iDwDst)] = (a_u128Value).au32[(a_iDwSrc)]; } while (0)
#define IEM_MC_STORE_XREG_R32(a_iXReg, a_r32Value) \
    do { pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].ar32[0] = (a_r32Value); } while (0)
#define IEM_MC_STORE_XREG_R64(a_iXReg, a_r64Value) \
    do { pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].ar64[0] = (a_r64Value); } while (0)
#define IEM_MC_STORE_XREG_U32_ZX_U128(a_iXReg, a_u32Value) \
    do { pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au64[0] = (uint32_t)(a_u32Value); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au64[1] = 0; \
    } while (0)

#define IEM_MC_BROADCAST_XREG_U8_ZX_VLMAX(a_iXRegDst, a_u8Src) \
    do { uintptr_t const iXRegDstTmp    = (a_iXRegDst); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au8[0]        = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au8[1]        = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au8[2]        = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au8[3]        = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au8[4]        = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au8[5]        = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au8[6]        = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au8[7]        = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au8[8]        = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au8[9]        = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au8[10]       = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au8[11]       = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au8[12]       = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au8[13]       = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au8[14]       = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au8[15]       = (a_u8Src); \
         IEM_MC_CLEAR_YREG_128_UP(iXRegDstTmp); \
    } while (0)
#define IEM_MC_BROADCAST_XREG_U16_ZX_VLMAX(a_iXRegDst, a_u16Src) \
    do { uintptr_t const iXRegDstTmp    = (a_iXRegDst); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au16[0]       = (a_u16Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au16[1]       = (a_u16Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au16[2]       = (a_u16Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au16[3]       = (a_u16Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au16[4]       = (a_u16Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au16[5]       = (a_u16Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au16[6]       = (a_u16Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au16[7]       = (a_u16Src); \
         IEM_MC_CLEAR_YREG_128_UP(iXRegDstTmp); \
    } while (0)
#define IEM_MC_BROADCAST_XREG_U32_ZX_VLMAX(a_iXRegDst, a_u32Src) \
    do { uintptr_t const iXRegDstTmp    = (a_iXRegDst); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au32[0]       = (a_u32Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au32[1]       = (a_u32Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au32[2]       = (a_u32Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au32[3]       = (a_u32Src); \
         IEM_MC_CLEAR_YREG_128_UP(iXRegDstTmp); \
    } while (0)
#define IEM_MC_BROADCAST_XREG_U64_ZX_VLMAX(a_iXRegDst, a_u64Src) \
    do { uintptr_t const iXRegDstTmp    = (a_iXRegDst); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au64[0]       = (a_u64Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iXRegDstTmp].au64[1]       = (a_u64Src); \
         IEM_MC_CLEAR_YREG_128_UP(iXRegDstTmp); \
    } while (0)

#define IEM_MC_REF_XREG_U128(a_pu128Dst, a_iXReg)       \
    (a_pu128Dst) = (&pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].uXmm)
#define IEM_MC_REF_XREG_XMM(a_pXmmDst, a_iXReg)       \
    (a_pXmmDst) = (&pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)])
#define IEM_MC_REF_XREG_U128_CONST(a_pu128Dst, a_iXReg) \
    (a_pu128Dst) = ((PCRTUINT128U)&pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].uXmm)
#define IEM_MC_REF_XREG_XMM_CONST(a_pXmmDst, a_iXReg) \
    (a_pXmmDst) = (&pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)])
#define IEM_MC_REF_XREG_U32_CONST(a_pu32Dst, a_iXReg) \
    (a_pu32Dst) = ((uint32_t const *)&pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au32[0])
#define IEM_MC_REF_XREG_U64_CONST(a_pu64Dst, a_iXReg) \
    (a_pu64Dst) = ((uint64_t const *)&pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].au64[0])
#define IEM_MC_REF_XREG_R32_CONST(a_pr32Dst, a_iXReg) \
    (a_pr32Dst) = ((RTFLOAT32U const *)&pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].ar32[0])
#define IEM_MC_REF_XREG_R64_CONST(a_pr64Dst, a_iXReg) \
    (a_pr64Dst) = ((RTFLOAT64U const *)&pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg)].ar64[0])
#define IEM_MC_COPY_XREG_U128(a_iXRegDst, a_iXRegSrc) \
    do { pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXRegDst)].au64[0] \
            = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXRegSrc)].au64[0]; \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXRegDst)].au64[1] \
            = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXRegSrc)].au64[1]; \
    } while (0)

#define IEM_MC_FETCH_YREG_U32(a_u32Dst, a_iYRegSrc) \
    do { uintptr_t const iYRegSrcTmp    = (a_iYRegSrc); \
         (a_u32Dst) = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrcTmp].au32[0]; \
    } while (0)
#define IEM_MC_FETCH_YREG_U64(a_u64Dst, a_iYRegSrc, a_iQWord) \
    do { uintptr_t const iYRegSrcTmp    = (a_iYRegSrc); \
         if ((a_iQWord) < 2) \
            (a_u64Dst) = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrcTmp].au64[(a_iQWord)]; \
         else \
            (a_u64Dst) = pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegSrcTmp].au64[(a_iQWord) - 2]; \
    } while (0)
#define IEM_MC_FETCH_YREG_U128(a_u128Dst, a_iYRegSrc, a_iDQword) \
    do { uintptr_t const iYRegSrcTmp    = (a_iYRegSrc); \
         if ((a_iDQword) == 0) \
         { \
            (a_u128Dst).au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(iYRegSrcTmp)].au64[0]; \
            (a_u128Dst).au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(iYRegSrcTmp)].au64[1]; \
         } \
         else \
         { \
            (a_u128Dst).au64[0] = pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[(iYRegSrcTmp)].au64[0]; \
            (a_u128Dst).au64[1] = pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[(iYRegSrcTmp)].au64[1]; \
         } \
    } while (0)
#define IEM_MC_FETCH_YREG_U256(a_u256Dst, a_iYRegSrc) \
    do { uintptr_t const iYRegSrcTmp    = (a_iYRegSrc); \
         (a_u256Dst).au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrcTmp].au64[0]; \
         (a_u256Dst).au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrcTmp].au64[1]; \
         (a_u256Dst).au64[2] = pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegSrcTmp].au64[0]; \
         (a_u256Dst).au64[3] = pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegSrcTmp].au64[1]; \
    } while (0)
#define IEM_MC_FETCH_YREG_YMM(a_uYmmDst, a_iYRegSrc) \
    do { uintptr_t const iYRegSrcTmp    = (a_iYRegSrc); \
         (a_uYmmDst).au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrcTmp].au64[0]; \
         (a_uYmmDst).au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrcTmp].au64[1]; \
         (a_uYmmDst).au64[2] = pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegSrcTmp].au64[0]; \
         (a_uYmmDst).au64[3] = pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegSrcTmp].au64[1]; \
    } while (0)
#define IEM_MC_FETCH_YREG_PAIR_YMM(a_uYmmDst, a_iYRegSrc1, a_iYRegSrc2) \
    do { uintptr_t const iYRegSrc1Tmp    = (a_iYRegSrc1); \
         uintptr_t const iYRegSrc2Tmp    = (a_iYRegSrc2); \
         (a_uYmmDst).uSrc1.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrc1Tmp].au64[0]; \
         (a_uYmmDst).uSrc1.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrc1Tmp].au64[1]; \
         (a_uYmmDst).uSrc1.au64[2] = pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegSrc1Tmp].au64[0]; \
         (a_uYmmDst).uSrc1.au64[3] = pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegSrc1Tmp].au64[1]; \
         (a_uYmmDst).uSrc2.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrc2Tmp].au64[0]; \
         (a_uYmmDst).uSrc2.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrc2Tmp].au64[1]; \
         (a_uYmmDst).uSrc2.au64[2] = pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegSrc2Tmp].au64[0]; \
         (a_uYmmDst).uSrc2.au64[3] = pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegSrc2Tmp].au64[1]; \
    } while (0)

#define IEM_MC_STORE_YREG_U128(a_iYRegDst, a_iDQword, a_u128Value) \
    do { uintptr_t const iYRegDstTmp    = (a_iYRegDst); \
         if ((a_iDQword) == 0) \
         { \
            pVCpu->cpum.GstCtx.XState.x87.aXMM[(iYRegDstTmp)].au64[0] = (a_u128Value).au64[0]; \
            pVCpu->cpum.GstCtx.XState.x87.aXMM[(iYRegDstTmp)].au64[1] = (a_u128Value).au64[1]; \
         } \
         else \
         { \
            pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[(iYRegDstTmp)].au64[0] = (a_u128Value).au64[0]; \
            pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[(iYRegDstTmp)].au64[1] = (a_u128Value).au64[1]; \
         } \
    } while (0)

#define IEM_MC_INT_CLEAR_ZMM_256_UP(a_iXRegDst) do { /* For AVX512 and AVX1024 support. */ } while (0)
#define IEM_MC_STORE_YREG_U32_ZX_VLMAX(a_iYRegDst, a_u32Src) \
    do { uintptr_t const iYRegDstTmp    = (a_iYRegDst); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au32[0]       = (a_u32Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au32[1]       = 0; \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[1]       = 0; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[0] = 0; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[1] = 0; \
         IEM_MC_INT_CLEAR_ZMM_256_UP(iYRegDstTmp); \
    } while (0)
#define IEM_MC_STORE_YREG_U64_ZX_VLMAX(a_iYRegDst, a_u64Src) \
    do { uintptr_t const iYRegDstTmp    = (a_iYRegDst); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[0]       = (a_u64Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[1]       = 0; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[0] = 0; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[1] = 0; \
         IEM_MC_INT_CLEAR_ZMM_256_UP(iYRegDstTmp); \
    } while (0)
#define IEM_MC_STORE_YREG_U128_ZX_VLMAX(a_iYRegDst, a_u128Src) \
    do { uintptr_t const iYRegDstTmp    = (a_iYRegDst); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[0]       = (a_u128Src).au64[0]; \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[1]       = (a_u128Src).au64[1]; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[0] = 0; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[1] = 0; \
         IEM_MC_INT_CLEAR_ZMM_256_UP(iYRegDstTmp); \
    } while (0)
#define IEM_MC_STORE_YREG_U256_ZX_VLMAX(a_iYRegDst, a_u256Src) \
    do { uintptr_t const iYRegDstTmp    = (a_iYRegDst); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[0]       = (a_u256Src).au64[0]; \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[1]       = (a_u256Src).au64[1]; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[0] = (a_u256Src).au64[2]; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[1] = (a_u256Src).au64[3]; \
         IEM_MC_INT_CLEAR_ZMM_256_UP(iYRegDstTmp); \
    } while (0)
#define IEM_MC_STORE_YREG_YMM_ZX_VLMAX(a_iYRegDst, a_uYmmSrc) \
    do { uintptr_t const iYRegDstTmp    = (a_iYRegDst); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[0]       = (a_uYmmSrc).au64[0]; \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[1]       = (a_uYmmSrc).au64[1]; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[0] = (a_uYmmSrc).au64[2]; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[1] = (a_uYmmSrc).au64[3]; \
         IEM_MC_INT_CLEAR_ZMM_256_UP(iYRegDstTmp); \
    } while (0)
#define IEM_MC_STORE_YREG_U32_U256(a_iYRegDst, a_iDwDst, a_u256Value, a_iDwSrc) \
    do { uintptr_t const iYRegDstTmp    = (a_iYRegDst); \
         if ((a_iDwDst) < 4) \
            pVCpu->cpum.GstCtx.XState.x87.aXMM[(iYRegDstTmp)].au32[(a_iDwDst)] = (a_u256Value).au32[(a_iDwSrc)]; \
         else \
            pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[(iYRegDstTmp)].au32[(a_iDwDst) - 4] = (a_u256Value).au32[(a_iDwSrc)]; \
    } while (0)
#define IEM_MC_STORE_YREG_U64_U256(a_iYRegDst, a_iQwDst, a_u256Value, a_iQwSrc) \
    do { uintptr_t const iYRegDstTmp    = (a_iYRegDst); \
         if ((a_iQwDst) < 2) \
            pVCpu->cpum.GstCtx.XState.x87.aXMM[(iYRegDstTmp)].au64[(a_iQwDst)] = (a_u256Value).au64[(a_iQwSrc)]; \
         else \
            pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[(iYRegDstTmp)].au64[(a_iQwDst) - 2] = (a_u256Value).au64[(a_iQwSrc)]; \
    } while (0)
#define IEM_MC_STORE_YREG_U64(a_iYRegDst, a_iQword, a_u64Value) \
    do { uintptr_t const iYRegDstTmp    = (a_iYRegDst); \
         if ((a_iQword) < 2) \
            pVCpu->cpum.GstCtx.XState.x87.aXMM[(iYRegDstTmp)].au64[(a_iQword)] = (a_u64Value); \
         else \
            pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[(iYRegDstTmp)].au64[(a_iQword) - 2] = (a_u64Value); \
    } while (0)

#define IEM_MC_BROADCAST_YREG_U8_ZX_VLMAX(a_iYRegDst, a_u8Src) \
    do { uintptr_t const iYRegDstTmp    = (a_iYRegDst); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au8[0]        = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au8[1]        = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au8[2]        = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au8[3]        = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au8[4]        = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au8[5]        = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au8[6]        = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au8[7]        = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au8[8]        = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au8[9]        = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au8[10]       = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au8[11]       = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au8[12]       = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au8[13]       = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au8[14]       = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au8[15]       = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au8[0]  = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au8[1]  = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au8[2]  = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au8[3]  = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au8[4]  = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au8[5]  = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au8[6]  = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au8[7]  = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au8[8]  = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au8[9]  = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au8[10] = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au8[11] = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au8[12] = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au8[13] = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au8[14] = (a_u8Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au8[15] = (a_u8Src); \
         IEM_MC_INT_CLEAR_ZMM_256_UP(iYRegDstTmp); \
    } while (0)
#define IEM_MC_BROADCAST_YREG_U16_ZX_VLMAX(a_iYRegDst, a_u16Src) \
    do { uintptr_t const iYRegDstTmp    = (a_iYRegDst); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au16[0]       = (a_u16Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au16[1]       = (a_u16Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au16[2]       = (a_u16Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au16[3]       = (a_u16Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au16[4]       = (a_u16Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au16[5]       = (a_u16Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au16[6]       = (a_u16Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au16[7]       = (a_u16Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au16[0] = (a_u16Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au16[1] = (a_u16Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au16[2] = (a_u16Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au16[3] = (a_u16Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au16[4] = (a_u16Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au16[5] = (a_u16Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au16[6] = (a_u16Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au16[7] = (a_u16Src); \
         IEM_MC_INT_CLEAR_ZMM_256_UP(iYRegDstTmp); \
    } while (0)
#define IEM_MC_BROADCAST_YREG_U32_ZX_VLMAX(a_iYRegDst, a_u32Src) \
    do { uintptr_t const iYRegDstTmp    = (a_iYRegDst); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au32[0]       = (a_u32Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au32[1]       = (a_u32Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au32[2]       = (a_u32Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au32[3]       = (a_u32Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au32[0] = (a_u32Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au32[1] = (a_u32Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au32[2] = (a_u32Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au32[3] = (a_u32Src); \
         IEM_MC_INT_CLEAR_ZMM_256_UP(iYRegDstTmp); \
    } while (0)
#define IEM_MC_BROADCAST_YREG_U64_ZX_VLMAX(a_iYRegDst, a_u64Src) \
    do { uintptr_t const iYRegDstTmp    = (a_iYRegDst); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[0]       = (a_u64Src); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[1]       = (a_u64Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[0] = (a_u64Src); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[1] = (a_u64Src); \
         IEM_MC_INT_CLEAR_ZMM_256_UP(iYRegDstTmp); \
    } while (0)
#define IEM_MC_BROADCAST_YREG_U128_ZX_VLMAX(a_iYRegDst, a_u128Src) \
    do { uintptr_t const iYRegDstTmp    = (a_iYRegDst); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[0]       = (a_u128Src).au64[0]; \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[1]       = (a_u128Src).au64[1]; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[0] = (a_u128Src).au64[0]; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[1] = (a_u128Src).au64[1]; \
         IEM_MC_INT_CLEAR_ZMM_256_UP(iYRegDstTmp); \
    } while (0)

#define IEM_MC_REF_YREG_U128(a_pu128Dst, a_iYReg)       \
    (a_pu128Dst) = (&pVCpu->cpum.GstCtx.XState.x87.aYMM[(a_iYReg)].uXmm)
#define IEM_MC_REF_YREG_U128_CONST(a_pu128Dst, a_iYReg) \
    (a_pu128Dst) = ((PCRTUINT128U)&pVCpu->cpum.GstCtx.XState.x87.aYMM[(a_iYReg)].uXmm)
#define IEM_MC_REF_YREG_U64_CONST(a_pu64Dst, a_iYReg) \
    (a_pu64Dst) = ((uint64_t const *)&pVCpu->cpum.GstCtx.XState.x87.aYMM[(a_iYReg)].au64[0])
#define IEM_MC_CLEAR_YREG_128_UP(a_iYReg) \
    do { uintptr_t const iYRegTmp   = (a_iYReg); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegTmp].au64[0] = 0; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegTmp].au64[1] = 0; \
         IEM_MC_INT_CLEAR_ZMM_256_UP(iYRegTmp); \
    } while (0)

#define IEM_MC_COPY_YREG_U256_ZX_VLMAX(a_iYRegDst, a_iYRegSrc) \
    do { uintptr_t const iYRegDstTmp    = (a_iYRegDst); \
         uintptr_t const iYRegSrcTmp    = (a_iYRegSrc); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[0]       = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrcTmp].au64[0]; \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[1]       = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrcTmp].au64[1]; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[0] = pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegSrcTmp].au64[0]; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[1] = pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegSrcTmp].au64[1]; \
         IEM_MC_INT_CLEAR_ZMM_256_UP(iYRegDstTmp); \
    } while (0)
#define IEM_MC_COPY_YREG_U128_ZX_VLMAX(a_iYRegDst, a_iYRegSrc) \
    do { uintptr_t const iYRegDstTmp    = (a_iYRegDst); \
         uintptr_t const iYRegSrcTmp    = (a_iYRegSrc); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[0]       = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrcTmp].au64[0]; \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[1]       = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrcTmp].au64[1]; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[0] = 0; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[1] = 0; \
         IEM_MC_INT_CLEAR_ZMM_256_UP(iYRegDstTmp); \
    } while (0)
#define IEM_MC_COPY_YREG_U64_ZX_VLMAX(a_iYRegDst, a_iYRegSrc) \
    do { uintptr_t const iYRegDstTmp    = (a_iYRegDst); \
         uintptr_t const iYRegSrcTmp    = (a_iYRegSrc); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[0]       = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrcTmp].au64[0]; \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[1]       = 0; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[0] = 0; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[1] = 0; \
         IEM_MC_INT_CLEAR_ZMM_256_UP(iYRegDstTmp); \
    } while (0)

#define IEM_MC_MERGE_YREG_U32_U96_ZX_VLMAX(a_iYRegDst, a_iYRegSrc32, a_iYRegSrcHx) \
    do { uintptr_t const iYRegDstTmp    = (a_iYRegDst); \
         uintptr_t const iYRegSrc32Tmp  = (a_iYRegSrc32); \
         uintptr_t const iYRegSrcHxTmp  = (a_iYRegSrcHx); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au32[0]       = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrc32Tmp].au32[0]; \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au32[1]       = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrcHxTmp].au32[1]; \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[1]       = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrcHxTmp].au64[1]; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[0] = 0; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[1] = 0; \
         IEM_MC_INT_CLEAR_ZMM_256_UP(iYRegDstTmp); \
    } while (0)
#define IEM_MC_MERGE_YREG_U64_U64_ZX_VLMAX(a_iYRegDst, a_iYRegSrc64, a_iYRegSrcHx) \
    do { uintptr_t const iYRegDstTmp    = (a_iYRegDst); \
         uintptr_t const iYRegSrc64Tmp  = (a_iYRegSrc64); \
         uintptr_t const iYRegSrcHxTmp  = (a_iYRegSrcHx); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[0]       = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrc64Tmp].au64[0]; \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[1]       = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrcHxTmp].au64[1]; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[0] = 0; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[1] = 0; \
         IEM_MC_INT_CLEAR_ZMM_256_UP(iYRegDstTmp); \
    } while (0)
#define IEM_MC_MERGE_YREG_U64LO_U64LO_ZX_VLMAX(a_iYRegDst, a_iYRegSrc64, a_iYRegSrcHx) /* for vmovhlps */ \
    do { uintptr_t const iYRegDstTmp    = (a_iYRegDst); \
         uintptr_t const iYRegSrc64Tmp  = (a_iYRegSrc64); \
         uintptr_t const iYRegSrcHxTmp  = (a_iYRegSrcHx); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[0]       = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrc64Tmp].au64[0]; \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[1]       = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrcHxTmp].au64[0]; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[0] = 0; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[1] = 0; \
         IEM_MC_INT_CLEAR_ZMM_256_UP(iYRegDstTmp); \
    } while (0)
#define IEM_MC_MERGE_YREG_U64HI_U64HI_ZX_VLMAX(a_iYRegDst, a_iYRegSrc64, a_iYRegSrcHx) /* for vmovhlps */ \
    do { uintptr_t const iYRegDstTmp    = (a_iYRegDst); \
         uintptr_t const iYRegSrc64Tmp  = (a_iYRegSrc64); \
         uintptr_t const iYRegSrcHxTmp  = (a_iYRegSrcHx); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[0]       = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrc64Tmp].au64[1]; \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[1]       = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrcHxTmp].au64[1]; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[0] = 0; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[1] = 0; \
         IEM_MC_INT_CLEAR_ZMM_256_UP(iYRegDstTmp); \
    } while (0)
#define IEM_MC_MERGE_YREG_U64LO_U64LOCAL_ZX_VLMAX(a_iYRegDst, a_iYRegSrcHx, a_u64Local) \
    do { uintptr_t const iYRegDstTmp    = (a_iYRegDst); \
         uintptr_t const iYRegSrcHxTmp  = (a_iYRegSrcHx); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[0]       = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrcHxTmp].au64[0]; \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[1]       = (a_u64Local); \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[0] = 0; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[1] = 0; \
         IEM_MC_INT_CLEAR_ZMM_256_UP(iYRegDstTmp); \
    } while (0)
#define IEM_MC_MERGE_YREG_U64LOCAL_U64HI_ZX_VLMAX(a_iYRegDst, a_u64Local, a_iYRegSrcHx) \
    do { uintptr_t const iYRegDstTmp    = (a_iYRegDst); \
         uintptr_t const iYRegSrcHxTmp  = (a_iYRegSrcHx); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[0]       = (a_u64Local); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[1]       = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrcHxTmp].au64[1]; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[0] = 0; \
         pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[iYRegDstTmp].au64[1] = 0; \
         IEM_MC_INT_CLEAR_ZMM_256_UP(iYRegDstTmp); \
    } while (0)

#define IEM_MC_CLEAR_ZREG_256_UP(a_iYReg) \
    do { IEM_MC_INT_CLEAR_ZMM_256_UP(a_iYReg); } while (0)

#ifndef IEM_WITH_SETJMP
# define IEM_MC_FETCH_MEM_U8(a_u8Dst, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU8(pVCpu, &(a_u8Dst), (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM16_U8(a_u8Dst, a_iSeg, a_GCPtrMem16) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU8(pVCpu, &(a_u8Dst), (a_iSeg), (a_GCPtrMem16)))
# define IEM_MC_FETCH_MEM32_U8(a_u8Dst, a_iSeg, a_GCPtrMem32) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU8(pVCpu, &(a_u8Dst), (a_iSeg), (a_GCPtrMem32)))
#else
# define IEM_MC_FETCH_MEM_U8(a_u8Dst, a_iSeg, a_GCPtrMem) \
    ((a_u8Dst) = iemMemFetchDataU8Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM16_U8(a_u8Dst, a_iSeg, a_GCPtrMem16) \
    ((a_u8Dst) = iemMemFetchDataU8Jmp(pVCpu, (a_iSeg), (a_GCPtrMem16)))
# define IEM_MC_FETCH_MEM32_U8(a_u8Dst, a_iSeg, a_GCPtrMem32) \
    ((a_u8Dst) = iemMemFetchDataU8Jmp(pVCpu, (a_iSeg), (a_GCPtrMem32)))

# define IEM_MC_FETCH_MEM_FLAT_U8(a_u8Dst, a_GCPtrMem) \
    ((a_u8Dst) = iemMemFlatFetchDataU8Jmp(pVCpu, (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM16_FLAT_U8(a_u8Dst, a_GCPtrMem16) \
    ((a_u8Dst) = iemMemFlatFetchDataU8Jmp(pVCpu, (a_GCPtrMem16)))
# define IEM_MC_FETCH_MEM32_FLAT_U8(a_u8Dst, a_GCPtrMem32) \
    ((a_u8Dst) = iemMemFlatFetchDataU8Jmp(pVCpu, (a_GCPtrMem32)))
#endif

#ifndef IEM_WITH_SETJMP
# define IEM_MC_FETCH_MEM_U16(a_u16Dst, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU16(pVCpu, &(a_u16Dst), (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_U16_DISP(a_u16Dst, a_iSeg, a_GCPtrMem, a_offDisp) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU16(pVCpu, &(a_u16Dst), (a_iSeg), (a_GCPtrMem) + (a_offDisp)))
# define IEM_MC_FETCH_MEM_I16(a_i16Dst, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU16(pVCpu, (uint16_t *)&(a_i16Dst), (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_I16_DISP(a_i16Dst, a_iSeg, a_GCPtrMem, a_offDisp) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU16(pVCpu, (uint16_t *)&(a_i16Dst), (a_iSeg), (a_GCPtrMem) + (a_offDisp)))
#else
# define IEM_MC_FETCH_MEM_U16(a_u16Dst, a_iSeg, a_GCPtrMem) \
    ((a_u16Dst) = iemMemFetchDataU16Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_U16_DISP(a_u16Dst, a_iSeg, a_GCPtrMem, a_offDisp) \
    ((a_u16Dst) = iemMemFetchDataU16Jmp(pVCpu, (a_iSeg), (a_GCPtrMem) + (a_offDisp)))
# define IEM_MC_FETCH_MEM_I16(a_i16Dst, a_iSeg, a_GCPtrMem) \
    ((a_i16Dst) = (int16_t)iemMemFetchDataU16Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_I16_DISP(a_i16Dst, a_iSeg, a_GCPtrMem, a_offDisp) \
    ((a_i16Dst) = (int16_t)iemMemFetchDataU16Jmp(pVCpu, (a_iSeg), (a_GCPtrMem) + (a_offDisp)))

# define IEM_MC_FETCH_MEM_FLAT_U16(a_u16Dst, a_GCPtrMem) \
    ((a_u16Dst) = iemMemFlatFetchDataU16Jmp(pVCpu, (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_FLAT_U16_DISP(a_u16Dst, a_GCPtrMem, a_offDisp) \
    ((a_u16Dst) = iemMemFlatFetchDataU16Jmp(pVCpu, (a_GCPtrMem) + (a_offDisp)))
# define IEM_MC_FETCH_MEM_FLAT_I16(a_i16Dst, a_GCPtrMem) \
    ((a_i16Dst) = (int16_t)iemMemFlatFetchDataU16Jmp(pVCpu, (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_FLAT_I16_DISP(a_i16Dst, a_GCPtrMem, a_offDisp) \
    ((a_i16Dst) = (int16_t)iemMemFlatFetchDataU16Jmp(pVCpu, (a_GCPtrMem) + (a_offDisp)))
#endif

#ifndef IEM_WITH_SETJMP
# define IEM_MC_FETCH_MEM_U32(a_u32Dst, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU32(pVCpu, &(a_u32Dst), (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_U32_DISP(a_u32Dst, a_iSeg, a_GCPtrMem, a_offDisp) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU32(pVCpu, &(a_u32Dst), (a_iSeg), (a_GCPtrMem) + (a_offDisp)))
# define IEM_MC_FETCH_MEM_I32(a_i32Dst, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU32(pVCpu, (uint32_t *)&(a_i32Dst), (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_I32_DISP(a_i32Dst, a_iSeg, a_GCPtrMem, a_offDisp) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU32(pVCpu, (uint32_t *)&(a_i32Dst), (a_iSeg), (a_GCPtrMem) + (a_offDisp)))
#else
# define IEM_MC_FETCH_MEM_U32(a_u32Dst, a_iSeg, a_GCPtrMem) \
    ((a_u32Dst) = iemMemFetchDataU32Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_U32_DISP(a_u32Dst, a_iSeg, a_GCPtrMem, a_offDisp) \
    ((a_u32Dst) = iemMemFetchDataU32Jmp(pVCpu, (a_iSeg), (a_GCPtrMem) + (a_offDisp)))
# define IEM_MC_FETCH_MEM_I32(a_i32Dst, a_iSeg, a_GCPtrMem) \
    ((a_i32Dst) = (int32_t)iemMemFetchDataU32Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_I32_DISP(a_i32Dst, a_iSeg, a_GCPtrMem, a_offDisp) \
    ((a_i32Dst) = (int32_t)iemMemFetchDataU32Jmp(pVCpu, (a_iSeg), (a_GCPtrMem) + (a_offDisp)))

# define IEM_MC_FETCH_MEM_FLAT_U32(a_u32Dst, a_GCPtrMem) \
    ((a_u32Dst) = iemMemFlatFetchDataU32Jmp(pVCpu, (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_FLAT_U32_DISP(a_u32Dst, a_GCPtrMem, a_offDisp) \
    ((a_u32Dst) = iemMemFlatFetchDataU32Jmp(pVCpu, (a_GCPtrMem) + (a_offDisp)))
# define IEM_MC_FETCH_MEM_FLAT_I32(a_i32Dst, a_GCPtrMem) \
    ((a_i32Dst) = (int32_t)iemMemFlatFetchDataU32Jmp(pVCpu, (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_FLAT_I32_DISP(a_i32Dst, a_GCPtrMem, a_offDisp) \
    ((a_i32Dst) = (int32_t)iemMemFlatFetchDataU32Jmp(pVCpu, (a_GCPtrMem) + (a_offDisp)))
#endif

#ifndef IEM_WITH_SETJMP
# define IEM_MC_FETCH_MEM_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU64(pVCpu, &(a_u64Dst), (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_U64_DISP(a_u64Dst, a_iSeg, a_GCPtrMem, a_offDisp) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU64(pVCpu, &(a_u64Dst), (a_iSeg), (a_GCPtrMem) + (a_offDisp)))
# define IEM_MC_FETCH_MEM_U64_ALIGN_U128(a_u64Dst, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU64AlignedU128(pVCpu, &(a_u64Dst), (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_I64(a_i64Dst, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU64(pVCpu, (uint64_t *)&(a_i64Dst), (a_iSeg), (a_GCPtrMem)))
#else
# define IEM_MC_FETCH_MEM_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    ((a_u64Dst) = iemMemFetchDataU64Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_U64_DISP(a_u64Dst, a_iSeg, a_GCPtrMem, a_offDisp) \
    ((a_u64Dst) = iemMemFetchDataU64Jmp(pVCpu, (a_iSeg), (a_GCPtrMem) + (a_offDisp)))
# define IEM_MC_FETCH_MEM_U64_ALIGN_U128(a_u64Dst, a_iSeg, a_GCPtrMem) \
    ((a_u64Dst) = iemMemFetchDataU64AlignedU128Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_I64(a_i64Dst, a_iSeg, a_GCPtrMem) \
    ((a_i64Dst) = (int64_t)iemMemFetchDataU64Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))

# define IEM_MC_FETCH_MEM_FLAT_U64(a_u64Dst, a_GCPtrMem) \
    ((a_u64Dst) = iemMemFlatFetchDataU64Jmp(pVCpu, (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_FLAT_U64_DISP(a_u64Dst, a_GCPtrMem, a_offDisp) \
    ((a_u64Dst) = iemMemFlatFetchDataU64Jmp(pVCpu, (a_GCPtrMem) + (a_offDisp)))
# define IEM_MC_FETCH_MEM_FLAT_U64_ALIGN_U128(a_u64Dst, a_GCPtrMem) \
    ((a_u64Dst) = iemMemFlatFetchDataU64AlignedU128Jmp(pVCpu, (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_FLAT_I64(a_i64Dst, a_GCPtrMem) \
    ((a_i64Dst) = (int64_t)iemMemFlatFetchDataU64Jmp(pVCpu, (a_GCPtrMem)))
#endif

#ifndef IEM_WITH_SETJMP
# define IEM_MC_FETCH_MEM_R32(a_r32Dst, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU32(pVCpu, &(a_r32Dst).u, (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_R64(a_r64Dst, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU64(pVCpu, &(a_r64Dst).u, (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_R80(a_r80Dst, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataR80(pVCpu, &(a_r80Dst), (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_D80(a_d80Dst, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataD80(pVCpu, &(a_d80Dst), (a_iSeg), (a_GCPtrMem)))
#else
# define IEM_MC_FETCH_MEM_R32(a_r32Dst, a_iSeg, a_GCPtrMem) \
    ((a_r32Dst).u = iemMemFetchDataU32Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_R64(a_r64Dst, a_iSeg, a_GCPtrMem) \
    ((a_r64Dst).u = iemMemFetchDataU64Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_R80(a_r80Dst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataR80Jmp(pVCpu, &(a_r80Dst), (a_iSeg), (a_GCPtrMem))
# define IEM_MC_FETCH_MEM_D80(a_d80Dst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataD80Jmp(pVCpu, &(a_d80Dst), (a_iSeg), (a_GCPtrMem))

# define IEM_MC_FETCH_MEM_FLAT_R32(a_r32Dst, a_GCPtrMem) \
    ((a_r32Dst).u = iemMemFlatFetchDataU32Jmp(pVCpu, (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_FLAT_R64(a_r64Dst, a_GCPtrMem) \
    ((a_r64Dst).u = iemMemFlatFetchDataU64Jmp(pVCpu, (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_FLAT_R80(a_r80Dst, a_GCPtrMem) \
    iemMemFlatFetchDataR80Jmp(pVCpu, &(a_r80Dst), (a_GCPtrMem))
# define IEM_MC_FETCH_MEM_FLAT_D80(a_d80Dst, a_GCPtrMem) \
    iemMemFlatFetchDataD80Jmp(pVCpu, &(a_d80Dst), (a_GCPtrMem))
#endif

#ifndef IEM_WITH_SETJMP
# define IEM_MC_FETCH_MEM_U128(a_u128Dst, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU128(pVCpu, &(a_u128Dst), (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_U128_NO_AC(a_u128Dst, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU128NoAc(pVCpu, &(a_u128Dst), (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_U128_ALIGN_SSE(a_u128Dst, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU128AlignedSse(pVCpu, &(a_u128Dst), (a_iSeg), (a_GCPtrMem)))

# define IEM_MC_FETCH_MEM_XMM_NO_AC(a_XmmDst, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU128NoAc(pVCpu, &(a_XmmDst).uXmm, (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_XMM_ALIGN_SSE(a_XmmDst, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU128AlignedSse(pVCpu, &(a_XmmDst).uXmm, (a_iSeg), (a_GCPtrMem)))

# define IEM_MC_FETCH_MEM_U128_NO_AC_AND_XREG_U128(a_u128Dst, a_iXReg1, a_iSeg2, a_GCPtrMem2) do { \
        IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU128NoAc(pVCpu, &(a_Dst).uSrc2, (a_iSeg2), (a_GCPtrMem2))); \
        (a_Dst).uSrc1.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
    } while (0)

# define IEM_MC_FETCH_MEM_XMM_ALIGN_SSE_AND_XREG_XMM(a_Dst, a_iXReg1, a_iSeg2, a_GCPtrMem2) do { \
        IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU128AlignedSse(pVCpu, &(a_Dst).uSrc2.uXmm, (a_iSeg2), (a_GCPtrMem2))); \
        (a_Dst).uSrc1.uXmm.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.uXmm.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
    } while (0)

# define IEM_MC_FETCH_MEM_XMM_U32_AND_XREG_XMM(a_Dst, a_iXReg1, a_iDWord2, a_iSeg2, a_GCPtrMem2) do { \
        (a_Dst).uSrc2.uXmm.au64[0] = 0; \
        (a_Dst).uSrc2.uXmm.au64[1] = 0; \
        IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU32(pVCpu, &(a_Dst).uSrc2.uXmm.au32[(a_iDWord2)], (a_iSeg2), (a_GCPtrMem2))); \
        (a_Dst).uSrc1.uXmm.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.uXmm.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
    } while (0)

# define IEM_MC_FETCH_MEM_XMM_U64_AND_XREG_XMM(a_Dst, a_iXReg1, a_iQWord2, a_iSeg2, a_GCPtrMem2) do { \
        (a_Dst).uSrc2.uXmm.au64[1] = 0; \
        IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU64(pVCpu, &(a_Dst).uSrc2.uXmm.au64[(a_iQWord2)], (a_iSeg2), (a_GCPtrMem2))); \
        (a_Dst).uSrc1.uXmm.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.uXmm.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
    } while (0)

# define IEM_MC_FETCH_MEM_U128_AND_XREG_U128_AND_RAX_RDX_U64(a_Dst, a_iXReg1, a_iSeg2, a_GCPtrMem2) do { \
        IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU128(pVCpu, &(a_Dst).uSrc2, (a_iSeg2), (a_GCPtrMem2))); \
        (a_Dst).uSrc1.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
        (a_Dst).u64Rax        = pVCpu->cpum.GstCtx.rax; \
        (a_Dst).u64Rdx        = pVCpu->cpum.GstCtx.rdx; \
    } while (0)
# define IEM_MC_FETCH_MEM_U128_AND_XREG_U128_AND_EAX_EDX_U32_SX_U64(a_Dst, a_iXReg1, a_iSeg2, a_GCPtrMem2) do { \
        IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU128(pVCpu, &(a_Dst).uSrc2, (a_iSeg2), (a_GCPtrMem2))); \
        (a_Dst).uSrc1.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
        (a_Dst).u64Rax        = (int64_t)(int32_t)pVCpu->cpum.GstCtx.eax; \
        (a_Dst).u64Rdx        = (int64_t)(int32_t)pVCpu->cpum.GstCtx.edx; \
    } while (0)

#else
# define IEM_MC_FETCH_MEM_U128(a_u128Dst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataU128Jmp(pVCpu, &(a_u128Dst), (a_iSeg), (a_GCPtrMem))
# define IEM_MC_FETCH_MEM_U128_NO_AC(a_u128Dst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataU128NoAcJmp(pVCpu, &(a_u128Dst), (a_iSeg), (a_GCPtrMem))
# define IEM_MC_FETCH_MEM_U128_ALIGN_SSE(a_u128Dst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataU128AlignedSseJmp(pVCpu, &(a_u128Dst), (a_iSeg), (a_GCPtrMem))

# define IEM_MC_FETCH_MEM_XMM(a_XmmDst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataU128Jmp(pVCpu, &(a_XmmDst).uXmm, (a_iSeg), (a_GCPtrMem))
# define IEM_MC_FETCH_MEM_XMM_NO_AC(a_XmmDst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataU128NoAcJmp(pVCpu, &(a_XmmDst).uXmm, (a_iSeg), (a_GCPtrMem))
# define IEM_MC_FETCH_MEM_XMM_ALIGN_SSE(a_XmmDst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataU128AlignedSseJmp(pVCpu, &(a_XmmDst).uXmm, (a_iSeg), (a_GCPtrMem))

# define IEM_MC_FETCH_MEM_FLAT_U128(a_u128Dst, a_GCPtrMem) \
    iemMemFlatFetchDataU128Jmp(pVCpu, &(a_u128Dst), (a_GCPtrMem))
# define IEM_MC_FETCH_MEM_FLAT_U128_NO_AC(a_u128Dst, a_GCPtrMem) \
    iemMemFlatFetchDataU128NoAcJmp(pVCpu, &(a_u128Dst), (a_GCPtrMem))
# define IEM_MC_FETCH_MEM_FLAT_U128_ALIGN_SSE(a_u128Dst, a_GCPtrMem) \
    iemMemFlatFetchDataU128AlignedSseJmp(pVCpu, &(a_u128Dst), (a_GCPtrMem))

# define IEM_MC_FETCH_MEM_FLAT_XMM(a_XmmDst, a_GCPtrMem) \
    iemMemFlatFetchDataU128Jmp(pVCpu, &(a_XmmDst).uXmm, (a_GCPtrMem))
# define IEM_MC_FETCH_MEM_FLAT_XMM_NO_AC(a_XmmDst, a_GCPtrMem) \
    iemMemFlatFetchDataU128NoAcJmp(pVCpu, &(a_XmmDst).uXmm, (a_GCPtrMem))
# define IEM_MC_FETCH_MEM_FLAT_XMM_ALIGN_SSE(a_XmmDst, a_GCPtrMem) \
    iemMemFlatFetchDataU128AlignedSseJmp(pVCpu, &(a_XmmDst).uXmm, (a_GCPtrMem))

# define IEM_MC_FETCH_MEM_U128_AND_XREG_U128(a_Dst, a_iXReg1, a_iSeg2, a_GCPtrMem2) do { \
        iemMemFetchDataU128Jmp(pVCpu, &(a_Dst).uSrc2, (a_iSeg2), (a_GCPtrMem2)); \
        (a_Dst).uSrc1.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
    } while (0)
# define IEM_MC_FETCH_MEM_FLAT_U128_AND_XREG_U128(a_Dst, a_iXReg1, a_GCPtrMem2) do { \
        iemMemFlatFetchDataU128Jmp(pVCpu, &(a_Dst).uSrc2, (a_GCPtrMem2)); \
        (a_Dst).uSrc1.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
    } while (0)

# define IEM_MC_FETCH_MEM_XMM_ALIGN_SSE_AND_XREG_XMM(a_Dst, a_iXReg1, a_iSeg2, a_GCPtrMem2) do { \
        iemMemFetchDataU128AlignedSseJmp(pVCpu, &(a_Dst).uSrc2.uXmm, (a_iSeg2), (a_GCPtrMem2)); \
        (a_Dst).uSrc1.uXmm.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.uXmm.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
    } while (0)
# define IEM_MC_FETCH_MEM_FLAT_XMM_ALIGN_SSE_AND_XREG_XMM(a_Dst, a_iXReg1, a_GCPtrMem2) do { \
        iemMemFlatFetchDataU128AlignedSseJmp(pVCpu, &(a_Dst).uSrc2.uXmm, (a_GCPtrMem2)); \
        (a_Dst).uSrc1.uXmm.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.uXmm.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
    } while (0)

# define IEM_MC_FETCH_MEM_XMM_U32_AND_XREG_XMM(a_Dst, a_iXReg1, a_iDWord2, a_iSeg2, a_GCPtrMem2) do {  \
        (a_Dst).uSrc2.uXmm.au64[0] = 0; \
        (a_Dst).uSrc2.uXmm.au64[1] = 0; \
        (a_Dst).uSrc2.uXmm.au32[(a_iDWord2)] = iemMemFetchDataU32Jmp(pVCpu, (a_iSeg2), (a_GCPtrMem2)); \
        (a_Dst).uSrc1.uXmm.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.uXmm.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
    } while (0)
# define IEM_MC_FETCH_MEM_FLAT_XMM_U32_AND_XREG_XMM(a_Dst, a_iXReg1, a_iDWord2, a_GCPtrMem2) do { \
        (a_Dst).uSrc2.uXmm.au64[0] = 0; \
        (a_Dst).uSrc2.uXmm.au64[1] = 0; \
        (a_Dst).uSrc2.uXmm.au32[(a_iDWord2)] = iemMemFlatFetchDataU32Jmp(pVCpu, (a_GCPtrMem2)); \
        (a_Dst).uSrc1.uXmm.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.uXmm.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
    } while (0)

# define IEM_MC_FETCH_MEM_XMM_U64_AND_XREG_XMM(a_Dst, a_iXReg1, a_iQWord2, a_iSeg2, a_GCPtrMem2) do {  \
        (a_Dst).uSrc2.uXmm.au64[!(a_iQWord2)] = 0; \
        (a_Dst).uSrc2.uXmm.au64[(a_iQWord2)]  = iemMemFetchDataU64Jmp(pVCpu, (a_iSeg2), (a_GCPtrMem2)); \
        (a_Dst).uSrc1.uXmm.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.uXmm.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
    } while (0)
# define IEM_MC_FETCH_MEM_FLAT_XMM_U64_AND_XREG_XMM(a_Dst, a_iXReg1, a_iQWord2, a_GCPtrMem2) do {  \
        (a_Dst).uSrc2.uXmm.au64[1] = 0; \
        (a_Dst).uSrc2.uXmm.au64[(a_iQWord2)]  = iemMemFlatFetchDataU64Jmp(pVCpu, (a_GCPtrMem2)); \
        (a_Dst).uSrc1.uXmm.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.uXmm.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
    } while (0)


# define IEM_MC_FETCH_MEM_U128_AND_XREG_U128_AND_RAX_RDX_U64(a_Dst, a_iXReg1, a_iSeg2, a_GCPtrMem2) do { \
        iemMemFetchDataU128Jmp(pVCpu, &(a_Dst).uSrc2, (a_iSeg2), (a_GCPtrMem2)); \
        (a_Dst).uSrc1.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
        (a_Dst).u64Rax        = pVCpu->cpum.GstCtx.rax; \
        (a_Dst).u64Rdx        = pVCpu->cpum.GstCtx.rdx; \
    } while (0)
# define IEM_MC_FETCH_MEM_U128_AND_XREG_U128_AND_EAX_EDX_U32_SX_U64(a_Dst, a_iXReg1, a_iSeg2, a_GCPtrMem2) do { \
        iemMemFetchDataU128Jmp(pVCpu, &(a_Dst).uSrc2, (a_iSeg2), (a_GCPtrMem2)); \
        (a_Dst).uSrc1.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
        (a_Dst).u64Rax        = (int64_t)(int32_t)pVCpu->cpum.GstCtx.eax; \
        (a_Dst).u64Rdx        = (int64_t)(int32_t)pVCpu->cpum.GstCtx.edx; \
    } while (0)

# define IEM_MC_FETCH_MEM_FLAT_U128_AND_XREG_U128_AND_RAX_RDX_U64(a_Dst, a_iXReg1, a_GCPtrMem2) do { \
        iemMemFlatFetchDataU128Jmp(pVCpu, &(a_Dst).uSrc2, (a_GCPtrMem2)); \
        (a_Dst).uSrc1.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
        (a_Dst).u64Rax        = pVCpu->cpum.GstCtx.rax; \
        (a_Dst).u64Rdx        = pVCpu->cpum.GstCtx.rdx; \
    } while (0)
# define IEM_MC_FETCH_MEM_FLAT_U128_AND_XREG_U128_AND_EAX_EDX_U32_SX_U64(a_Dst, a_iXReg1, a_GCPtrMem2) do { \
        iemMemFlatFetchDataU128Jmp(pVCpu, &(a_Dst).uSrc2, (a_GCPtrMem2)); \
        (a_Dst).uSrc1.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
        (a_Dst).u64Rax        = (int64_t)(int32_t)pVCpu->cpum.GstCtx.eax; \
        (a_Dst).u64Rdx        = (int64_t)(int32_t)pVCpu->cpum.GstCtx.edx; \
    } while (0)

#endif

#ifndef IEM_WITH_SETJMP
# define IEM_MC_FETCH_MEM_U256(a_u256Dst, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU256NoAc(pVCpu, &(a_u256Dst), (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_U256_NO_AC(a_u256Dst, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU256NoAc(pVCpu, &(a_u256Dst), (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_U256_ALIGN_AVX(a_u256Dst, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU256AlignedAvx(pVCpu, &(a_u256Dst), (a_iSeg), (a_GCPtrMem)))

# define IEM_MC_FETCH_MEM_YMM(a_YmmDst, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU256NoAc(pVCpu, &(a_YmmDst).ymm, (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_YMM_NO_AC(a_YmmDst, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU256NoAc(pVCpu, &(a_YmmDst).ymm, (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_YMM_ALIGN_AVX(a_YmmDst, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU256AlignedAvx(pVCpu, &(a_YmmDst).ymm, (a_iSeg), (a_GCPtrMem)))

# define IEM_MC_FETCH_MEM_YMM_ALIGN_AVX_AND_YREG_YMM(a_uYmmDst, a_iYRegSrc1, a_iSeg2, a_GCPtrMem2) do { \
        uintptr_t const a_iYRegSrc1Tmp = (a_iYRegSrc1); \
        IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU256AlignedAvx(pVCpu, &(a_uYmmDst).uSrc2.ymm, (a_iSeg2), (a_GCPtrMem2))); \
        (a_uYmmDst).uSrc1.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[a_iYRegSrc1Tmp].au64[0]; \
        (a_uYmmDst).uSrc1.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[a_iYRegSrc1Tmp].au64[1]; \
        (a_uYmmDst).uSrc1.au64[2] = pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[a_iYRegSrc1Tmp].au64[0]; \
        (a_uYmmDst).uSrc1.au64[3] = pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[a_iYRegSrc1Tmp].au64[1]; \
    } while (0)

#else
# define IEM_MC_FETCH_MEM_U256(a_u256Dst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataU256NoAcJmp(pVCpu, &(a_u256Dst), (a_iSeg), (a_GCPtrMem))
# define IEM_MC_FETCH_MEM_U256_NO_AC(a_u256Dst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataU256NoAcJmp(pVCpu, &(a_u256Dst), (a_iSeg), (a_GCPtrMem))
# define IEM_MC_FETCH_MEM_U256_ALIGN_AVX(a_u256Dst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataU256AlignedAvxJmp(pVCpu, &(a_u256Dst), (a_iSeg), (a_GCPtrMem))

# define IEM_MC_FETCH_MEM_YMM(a_YmmDst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataU256NoAcJmp(pVCpu, &(a_YmmDst).ymm, (a_iSeg), (a_GCPtrMem))
# define IEM_MC_FETCH_MEM_YMM_NO_AC(a_YmmDst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataU256NoAcJmp(pVCpu, &(a_YmmDst).ymm, (a_iSeg), (a_GCPtrMem))
# define IEM_MC_FETCH_MEM_YMM_ALIGN_AVX(a_YmmDst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataU256AlignedAvxJmp(pVCpu, &(a_YmmDst).ymm, (a_iSeg), (a_GCPtrMem))

# define IEM_MC_FETCH_MEM_YMM_ALIGN_AVX_AND_YREG_YMM(a_uYmmDst, a_iYRegSrc1, a_iSeg2, a_GCPtrMem2) do { \
        uintptr_t const a_iYRegSrc1Tmp = (a_iYRegSrc1); \
        iemMemFetchDataU256AlignedAvxJmp(pVCpu, &(a_uYmmDst).uSrc2.ymm, (a_iSeg2), (a_GCPtrMem2)); \
        (a_uYmmDst).uSrc1.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[a_iYRegSrc1Tmp].au64[0]; \
        (a_uYmmDst).uSrc1.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[a_iYRegSrc1Tmp].au64[1]; \
        (a_uYmmDst).uSrc1.au64[2] = pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[a_iYRegSrc1Tmp].au64[0]; \
        (a_uYmmDst).uSrc1.au64[3] = pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[a_iYRegSrc1Tmp].au64[1]; \
    } while (0)

# define IEM_MC_FETCH_MEM_FLAT_U256(a_u256Dst, a_GCPtrMem) \
    iemMemFlatFetchDataU256NoAcJmp(pVCpu, &(a_u256Dst), (a_GCPtrMem))
# define IEM_MC_FETCH_MEM_FLAT_U256_NO_AC(a_u256Dst, a_GCPtrMem) \
    iemMemFlatFetchDataU256NoAcJmp(pVCpu, &(a_u256Dst), (a_GCPtrMem))
# define IEM_MC_FETCH_MEM_FLAT_U256_ALIGN_AVX(a_u256Dst, a_GCPtrMem) \
    iemMemFlatFetchDataU256AlignedAvxJmp(pVCpu, &(a_u256Dst), (a_GCPtrMem))

# define IEM_MC_FETCH_MEM_FLAT_YMM(a_YmmDst, a_GCPtrMem) \
    iemMemFlatFetchDataU256NoAcJmp(pVCpu, &(a_YmmDst).ymm, (a_GCPtrMem))
# define IEM_MC_FETCH_MEM_FLAT_YMM_NO_AC(a_YmmDst, a_GCPtrMem) \
    iemMemFlatFetchDataU256NoAcJmp(pVCpu, &(a_YmmDst).ymm, (a_GCPtrMem))
# define IEM_MC_FETCH_MEM_FLAT_YMM_ALIGN_AVX(a_YmmDst, a_GCPtrMem) \
    iemMemFlatFetchDataU256AlignedAvxJmp(pVCpu, &(a_YmmDst).ymm, (a_GCPtrMem))

# define IEM_MC_FETCH_MEM_FLAT_YMM_ALIGN_AVX_AND_YREG_YMM(a_uYmmDst, a_iYRegSrc1, a_GCPtrMem2) do { \
        uintptr_t const a_iYRegSrc1Tmp = (a_iYRegSrc1); \
        iemMemFlatFetchDataU256AlignedAvxJmp(pVCpu, &(a_uYmmDst).uSrc2.ymm, (a_GCPtrMem2)); \
        (a_uYmmDst).uSrc1.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[a_iYRegSrc1Tmp].au64[0]; \
        (a_uYmmDst).uSrc1.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[a_iYRegSrc1Tmp].au64[1]; \
        (a_uYmmDst).uSrc1.au64[2] = pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[a_iYRegSrc1Tmp].au64[0]; \
        (a_uYmmDst).uSrc1.au64[3] = pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[a_iYRegSrc1Tmp].au64[1]; \
    } while (0)

#endif



#ifndef IEM_WITH_SETJMP
# define IEM_MC_FETCH_MEM_U8_ZX_U16(a_u16Dst, a_iSeg, a_GCPtrMem) \
    do { \
        uint8_t u8Tmp; \
        IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU8(pVCpu, &u8Tmp, (a_iSeg), (a_GCPtrMem))); \
        (a_u16Dst) = u8Tmp; \
    } while (0)
# define IEM_MC_FETCH_MEM_U8_ZX_U32(a_u32Dst, a_iSeg, a_GCPtrMem) \
    do { \
        uint8_t u8Tmp; \
        IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU8(pVCpu, &u8Tmp, (a_iSeg), (a_GCPtrMem))); \
        (a_u32Dst) = u8Tmp; \
    } while (0)
# define IEM_MC_FETCH_MEM_U8_ZX_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    do { \
        uint8_t u8Tmp; \
        IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU8(pVCpu, &u8Tmp, (a_iSeg), (a_GCPtrMem))); \
        (a_u64Dst) = u8Tmp; \
    } while (0)
# define IEM_MC_FETCH_MEM_U16_ZX_U32(a_u32Dst, a_iSeg, a_GCPtrMem) \
    do { \
        uint16_t u16Tmp; \
        IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU16(pVCpu, &u16Tmp, (a_iSeg), (a_GCPtrMem))); \
        (a_u32Dst) = u16Tmp; \
    } while (0)
# define IEM_MC_FETCH_MEM_U16_ZX_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    do { \
        uint16_t u16Tmp; \
        IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU16(pVCpu, &u16Tmp, (a_iSeg), (a_GCPtrMem))); \
        (a_u64Dst) = u16Tmp; \
    } while (0)
# define IEM_MC_FETCH_MEM_U32_ZX_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    do { \
        uint32_t u32Tmp; \
        IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU32(pVCpu, &u32Tmp, (a_iSeg), (a_GCPtrMem))); \
        (a_u64Dst) = u32Tmp; \
    } while (0)
#else  /* IEM_WITH_SETJMP */
# define IEM_MC_FETCH_MEM_U8_ZX_U16(a_u16Dst, a_iSeg, a_GCPtrMem) \
    ((a_u16Dst) = iemMemFetchDataU8Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_U8_ZX_U32(a_u32Dst, a_iSeg, a_GCPtrMem) \
    ((a_u32Dst) = iemMemFetchDataU8Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_U8_ZX_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    ((a_u64Dst) = iemMemFetchDataU8Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_U16_ZX_U32(a_u32Dst, a_iSeg, a_GCPtrMem) \
    ((a_u32Dst) = iemMemFetchDataU16Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_U16_ZX_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    ((a_u64Dst) = iemMemFetchDataU16Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_U32_ZX_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    ((a_u64Dst) = iemMemFetchDataU32Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))

# define IEM_MC_FETCH_MEM_FLAT_U8_ZX_U16(a_u16Dst, a_GCPtrMem) \
    ((a_u16Dst) = iemMemFlatFetchDataU8Jmp(pVCpu, (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_FLAT_U8_ZX_U32(a_u32Dst, a_GCPtrMem) \
    ((a_u32Dst) = iemMemFlatFetchDataU8Jmp(pVCpu, (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_FLAT_U8_ZX_U64(a_u64Dst, a_GCPtrMem) \
    ((a_u64Dst) = iemMemFlatFetchDataU8Jmp(pVCpu, (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_FLAT_U16_ZX_U32(a_u32Dst, a_GCPtrMem) \
    ((a_u32Dst) = iemMemFlatFetchDataU16Jmp(pVCpu, (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_FLAT_U16_ZX_U64(a_u64Dst, a_GCPtrMem) \
    ((a_u64Dst) = iemMemFlatFetchDataU16Jmp(pVCpu, (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_FLAT_U32_ZX_U64(a_u64Dst, a_GCPtrMem) \
    ((a_u64Dst) = iemMemFlatFetchDataU32Jmp(pVCpu, (a_GCPtrMem)))
#endif /* IEM_WITH_SETJMP */

#ifndef IEM_WITH_SETJMP
# define IEM_MC_FETCH_MEM_U8_SX_U16(a_u16Dst, a_iSeg, a_GCPtrMem) \
    do { \
        uint8_t u8Tmp; \
        IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU8(pVCpu, &u8Tmp, (a_iSeg), (a_GCPtrMem))); \
        (a_u16Dst) = (int8_t)u8Tmp; \
    } while (0)
# define IEM_MC_FETCH_MEM_U8_SX_U32(a_u32Dst, a_iSeg, a_GCPtrMem) \
    do { \
        uint8_t u8Tmp; \
        IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU8(pVCpu, &u8Tmp, (a_iSeg), (a_GCPtrMem))); \
        (a_u32Dst) = (int8_t)u8Tmp; \
    } while (0)
# define IEM_MC_FETCH_MEM_U8_SX_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    do { \
        uint8_t u8Tmp; \
        IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU8(pVCpu, &u8Tmp, (a_iSeg), (a_GCPtrMem))); \
        (a_u64Dst) = (int8_t)u8Tmp; \
    } while (0)
# define IEM_MC_FETCH_MEM_U16_SX_U32(a_u32Dst, a_iSeg, a_GCPtrMem) \
    do { \
        uint16_t u16Tmp; \
        IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU16(pVCpu, &u16Tmp, (a_iSeg), (a_GCPtrMem))); \
        (a_u32Dst) = (int16_t)u16Tmp; \
    } while (0)
# define IEM_MC_FETCH_MEM_U16_SX_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    do { \
        uint16_t u16Tmp; \
        IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU16(pVCpu, &u16Tmp, (a_iSeg), (a_GCPtrMem))); \
        (a_u64Dst) = (int16_t)u16Tmp; \
    } while (0)
# define IEM_MC_FETCH_MEM_U32_SX_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    do { \
        uint32_t u32Tmp; \
        IEM_MC_RETURN_ON_FAILURE(iemMemFetchDataU32(pVCpu, &u32Tmp, (a_iSeg), (a_GCPtrMem))); \
        (a_u64Dst) = (int32_t)u32Tmp; \
    } while (0)
#else  /* IEM_WITH_SETJMP */
# define IEM_MC_FETCH_MEM_U8_SX_U16(a_u16Dst, a_iSeg, a_GCPtrMem) \
    ((a_u16Dst) = (int8_t)iemMemFetchDataU8Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_U8_SX_U32(a_u32Dst, a_iSeg, a_GCPtrMem) \
    ((a_u32Dst) = (int8_t)iemMemFetchDataU8Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_U8_SX_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    ((a_u64Dst) = (int8_t)iemMemFetchDataU8Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_U16_SX_U32(a_u32Dst, a_iSeg, a_GCPtrMem) \
    ((a_u32Dst) = (int16_t)iemMemFetchDataU16Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_U16_SX_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    ((a_u64Dst) = (int16_t)iemMemFetchDataU16Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_U32_SX_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    ((a_u64Dst) = (int32_t)iemMemFetchDataU32Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))

# define IEM_MC_FETCH_MEM_FLAT_U8_SX_U16(a_u16Dst, a_GCPtrMem) \
    ((a_u16Dst) = (int8_t)iemMemFlatFetchDataU8Jmp(pVCpu, (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_FLAT_U8_SX_U32(a_u32Dst, a_GCPtrMem) \
    ((a_u32Dst) = (int8_t)iemMemFlatFetchDataU8Jmp(pVCpu, (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_FLAT_U8_SX_U64(a_u64Dst, a_GCPtrMem) \
    ((a_u64Dst) = (int8_t)iemMemFlatFetchDataU8Jmp(pVCpu, (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_FLAT_U16_SX_U32(a_u32Dst, a_GCPtrMem) \
    ((a_u32Dst) = (int16_t)iemMemFlatFetchDataU16Jmp(pVCpu, (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_FLAT_U16_SX_U64(a_u64Dst, a_GCPtrMem) \
    ((a_u64Dst) = (int16_t)iemMemFlatFetchDataU16Jmp(pVCpu, (a_GCPtrMem)))
# define IEM_MC_FETCH_MEM_FLAT_U32_SX_U64(a_u64Dst, a_GCPtrMem) \
    ((a_u64Dst) = (int32_t)iemMemFlatFetchDataU32Jmp(pVCpu, (a_GCPtrMem)))
#endif /* IEM_WITH_SETJMP */

#ifndef IEM_WITH_SETJMP
# define IEM_MC_STORE_MEM_U8(a_iSeg, a_GCPtrMem, a_u8Value) \
    IEM_MC_RETURN_ON_FAILURE(iemMemStoreDataU8(pVCpu, (a_iSeg), (a_GCPtrMem), (a_u8Value)))
# define IEM_MC_STORE_MEM_U16(a_iSeg, a_GCPtrMem, a_u16Value) \
    IEM_MC_RETURN_ON_FAILURE(iemMemStoreDataU16(pVCpu, (a_iSeg), (a_GCPtrMem), (a_u16Value)))
# define IEM_MC_STORE_MEM_U32(a_iSeg, a_GCPtrMem, a_u32Value) \
    IEM_MC_RETURN_ON_FAILURE(iemMemStoreDataU32(pVCpu, (a_iSeg), (a_GCPtrMem), (a_u32Value)))
# define IEM_MC_STORE_MEM_U64(a_iSeg, a_GCPtrMem, a_u64Value) \
    IEM_MC_RETURN_ON_FAILURE(iemMemStoreDataU64(pVCpu, (a_iSeg), (a_GCPtrMem), (a_u64Value)))
#else
# define IEM_MC_STORE_MEM_U8(a_iSeg, a_GCPtrMem, a_u8Value) \
    iemMemStoreDataU8Jmp(pVCpu, (a_iSeg), (a_GCPtrMem), (a_u8Value))
# define IEM_MC_STORE_MEM_U16(a_iSeg, a_GCPtrMem, a_u16Value) \
    iemMemStoreDataU16Jmp(pVCpu, (a_iSeg), (a_GCPtrMem), (a_u16Value))
# define IEM_MC_STORE_MEM_U32(a_iSeg, a_GCPtrMem, a_u32Value) \
    iemMemStoreDataU32Jmp(pVCpu, (a_iSeg), (a_GCPtrMem), (a_u32Value))
# define IEM_MC_STORE_MEM_U64(a_iSeg, a_GCPtrMem, a_u64Value) \
    iemMemStoreDataU64Jmp(pVCpu, (a_iSeg), (a_GCPtrMem), (a_u64Value))

# define IEM_MC_STORE_MEM_FLAT_U8(a_GCPtrMem, a_u8Value) \
    iemMemFlatStoreDataU8Jmp(pVCpu, (a_GCPtrMem), (a_u8Value))
# define IEM_MC_STORE_MEM_FLAT_U16(a_GCPtrMem, a_u16Value) \
    iemMemFlatStoreDataU16Jmp(pVCpu, (a_GCPtrMem), (a_u16Value))
# define IEM_MC_STORE_MEM_FLAT_U32(a_GCPtrMem, a_u32Value) \
    iemMemFlatStoreDataU32Jmp(pVCpu, (a_GCPtrMem), (a_u32Value))
# define IEM_MC_STORE_MEM_FLAT_U64(a_GCPtrMem, a_u64Value) \
    iemMemFlatStoreDataU64Jmp(pVCpu, (a_GCPtrMem), (a_u64Value))
#endif

#ifndef IEM_WITH_SETJMP
# define IEM_MC_STORE_MEM_U8_CONST(a_iSeg, a_GCPtrMem, a_u8C) \
    IEM_MC_RETURN_ON_FAILURE(iemMemStoreDataU8(pVCpu, (a_iSeg), (a_GCPtrMem), (a_u8C)))
# define IEM_MC_STORE_MEM_U16_CONST(a_iSeg, a_GCPtrMem, a_u16C) \
    IEM_MC_RETURN_ON_FAILURE(iemMemStoreDataU16(pVCpu, (a_iSeg), (a_GCPtrMem), (a_u16C)))
# define IEM_MC_STORE_MEM_U32_CONST(a_iSeg, a_GCPtrMem, a_u32C) \
    IEM_MC_RETURN_ON_FAILURE(iemMemStoreDataU32(pVCpu, (a_iSeg), (a_GCPtrMem), (a_u32C)))
# define IEM_MC_STORE_MEM_U64_CONST(a_iSeg, a_GCPtrMem, a_u64C) \
    IEM_MC_RETURN_ON_FAILURE(iemMemStoreDataU64(pVCpu, (a_iSeg), (a_GCPtrMem), (a_u64C)))
#else
# define IEM_MC_STORE_MEM_U8_CONST(a_iSeg, a_GCPtrMem, a_u8C) \
    iemMemStoreDataU8Jmp(pVCpu, (a_iSeg), (a_GCPtrMem), (a_u8C))
# define IEM_MC_STORE_MEM_U16_CONST(a_iSeg, a_GCPtrMem, a_u16C) \
    iemMemStoreDataU16Jmp(pVCpu, (a_iSeg), (a_GCPtrMem), (a_u16C))
# define IEM_MC_STORE_MEM_U32_CONST(a_iSeg, a_GCPtrMem, a_u32C) \
    iemMemStoreDataU32Jmp(pVCpu, (a_iSeg), (a_GCPtrMem), (a_u32C))
# define IEM_MC_STORE_MEM_U64_CONST(a_iSeg, a_GCPtrMem, a_u64C) \
    iemMemStoreDataU64Jmp(pVCpu, (a_iSeg), (a_GCPtrMem), (a_u64C))

# define IEM_MC_STORE_MEM_FLAT_U8_CONST(a_GCPtrMem, a_u8C) \
    iemMemFlatStoreDataU8Jmp(pVCpu, (a_GCPtrMem), (a_u8C))
# define IEM_MC_STORE_MEM_FLAT_U16_CONST(a_GCPtrMem, a_u16C) \
    iemMemFlatStoreDataU16Jmp(pVCpu, (a_GCPtrMem), (a_u16C))
# define IEM_MC_STORE_MEM_FLAT_U32_CONST(a_GCPtrMem, a_u32C) \
    iemMemFlatStoreDataU32Jmp(pVCpu, (a_GCPtrMem), (a_u32C))
# define IEM_MC_STORE_MEM_FLAT_U64_CONST(a_GCPtrMem, a_u64C) \
    iemMemFlatStoreDataU64Jmp(pVCpu, (a_GCPtrMem), (a_u64C))
#endif

#define IEM_MC_STORE_MEM_I8_CONST_BY_REF( a_pi8Dst,  a_i8C)     *(a_pi8Dst)  = (a_i8C)
#define IEM_MC_STORE_MEM_I16_CONST_BY_REF(a_pi16Dst, a_i16C)    *(a_pi16Dst) = (a_i16C)
#define IEM_MC_STORE_MEM_I32_CONST_BY_REF(a_pi32Dst, a_i32C)    *(a_pi32Dst) = (a_i32C)
#define IEM_MC_STORE_MEM_I64_CONST_BY_REF(a_pi64Dst, a_i64C)    *(a_pi64Dst) = (a_i64C)
#define IEM_MC_STORE_MEM_NEG_QNAN_R32_BY_REF(a_pr32Dst)         (a_pr32Dst)->u = UINT32_C(0xffc00000)
#define IEM_MC_STORE_MEM_NEG_QNAN_R64_BY_REF(a_pr64Dst)         (a_pr64Dst)->u = UINT64_C(0xfff8000000000000)
#define IEM_MC_STORE_MEM_NEG_QNAN_R80_BY_REF(a_pr80Dst) \
    do { \
        (a_pr80Dst)->au64[0] = UINT64_C(0xc000000000000000); \
        (a_pr80Dst)->au16[4] = UINT16_C(0xffff); \
    } while (0)
#define IEM_MC_STORE_MEM_INDEF_D80_BY_REF(a_pd80Dst) \
    do { \
        (a_pd80Dst)->au64[0] = UINT64_C(0xc000000000000000); \
        (a_pd80Dst)->au16[4] = UINT16_C(0xffff); \
    } while (0)

#ifndef IEM_WITH_SETJMP
# define IEM_MC_STORE_MEM_U128(a_iSeg, a_GCPtrMem, a_u128Value) \
    IEM_MC_RETURN_ON_FAILURE(iemMemStoreDataU128(pVCpu, (a_iSeg), (a_GCPtrMem), &(a_u128Value)))
# define IEM_MC_STORE_MEM_U128_NO_AC(a_iSeg, a_GCPtrMem, a_u128Value) \
    IEM_MC_RETURN_ON_FAILURE(iemMemStoreDataU128NoAc(pVCpu, (a_iSeg), (a_GCPtrMem), &(a_u128Value)))
# define IEM_MC_STORE_MEM_U128_ALIGN_SSE(a_iSeg, a_GCPtrMem, a_u128Value) \
    IEM_MC_RETURN_ON_FAILURE(iemMemStoreDataU128AlignedSse(pVCpu, (a_iSeg), (a_GCPtrMem), (a_u128Value)))
#else
# define IEM_MC_STORE_MEM_U128(a_iSeg, a_GCPtrMem, a_u128Value) \
    iemMemStoreDataU128Jmp(pVCpu, (a_iSeg), (a_GCPtrMem), &(a_u128Value))
# define IEM_MC_STORE_MEM_U128_NO_AC(a_iSeg, a_GCPtrMem, a_u128Value) \
    iemMemStoreDataU128NoAcJmp(pVCpu, (a_iSeg), (a_GCPtrMem), &(a_u128Value))
# define IEM_MC_STORE_MEM_U128_ALIGN_SSE(a_iSeg, a_GCPtrMem, a_u128Value) \
    iemMemStoreDataU128AlignedSseJmp(pVCpu, (a_iSeg), (a_GCPtrMem), (a_u128Value))

# define IEM_MC_STORE_MEM_FLAT_U128(a_GCPtrMem, a_u128Value) \
    iemMemFlatStoreDataU128Jmp(pVCpu, (a_GCPtrMem), &(a_u128Value))
# define IEM_MC_STORE_MEM_FLAT_U128_NO_AC(a_GCPtrMem, a_u128Value) \
    iemMemFlatStoreDataU128NoAcJmp(pVCpu, (a_GCPtrMem), &(a_u128Value))
# define IEM_MC_STORE_MEM_FLAT_U128_ALIGN_SSE(a_GCPtrMem, a_u128Value) \
    iemMemStoreDataU128AlignedSseJmp(pVCpu, UINT8_MAX, (a_GCPtrMem), (a_u128Value))
#endif

#ifndef IEM_WITH_SETJMP
# define IEM_MC_STORE_MEM_U256(a_iSeg, a_GCPtrMem, a_u256Value) \
    IEM_MC_RETURN_ON_FAILURE(iemMemStoreDataU256(pVCpu, (a_iSeg), (a_GCPtrMem), &(a_u256Value)))
# define IEM_MC_STORE_MEM_U256_NO_AC(a_iSeg, a_GCPtrMem, a_u256Value) \
    IEM_MC_RETURN_ON_FAILURE(iemMemStoreDataU256NoAc(pVCpu, (a_iSeg), (a_GCPtrMem), &(a_u256Value)))
# define IEM_MC_STORE_MEM_U256_ALIGN_AVX(a_iSeg, a_GCPtrMem, a_u256Value) \
    IEM_MC_RETURN_ON_FAILURE(iemMemStoreDataU256AlignedAvx(pVCpu, (a_iSeg), (a_GCPtrMem), &(a_u256Value)))
#else
# define IEM_MC_STORE_MEM_U256(a_iSeg, a_GCPtrMem, a_u256Value) \
    iemMemStoreDataU256Jmp(pVCpu, (a_iSeg), (a_GCPtrMem), &(a_u256Value))
# define IEM_MC_STORE_MEM_U256_NO_AC(a_iSeg, a_GCPtrMem, a_u256Value) \
    iemMemStoreDataU256NoAcJmp(pVCpu, (a_iSeg), (a_GCPtrMem), &(a_u256Value))
# define IEM_MC_STORE_MEM_U256_ALIGN_AVX(a_iSeg, a_GCPtrMem, a_u256Value) \
    iemMemStoreDataU256AlignedAvxJmp(pVCpu, (a_iSeg), (a_GCPtrMem), &(a_u256Value))

# define IEM_MC_STORE_MEM_FLAT_U256(a_GCPtrMem, a_u256Value) \
    iemMemFlatStoreDataU256Jmp(pVCpu, (a_GCPtrMem), &(a_u256Value))
# define IEM_MC_STORE_MEM_FLAT_U256_NO_AC(a_GCPtrMem, a_u256Value) \
    iemMemFlatStoreDataU256NoAcJmp(pVCpu, (a_GCPtrMem), &(a_u256Value))
# define IEM_MC_STORE_MEM_FLAT_U256_ALIGN_AVX(a_GCPtrMem, a_u256Value) \
    iemMemFlatStoreDataU256AlignedAvxJmp(pVCpu, (a_GCPtrMem), &(a_u256Value))
#endif

/* Regular stack push and pop: */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_PUSH_U16(a_u16Value)            IEM_MC_RETURN_ON_FAILURE(iemMemStackPushU16(pVCpu, (a_u16Value)))
# define IEM_MC_PUSH_U32(a_u32Value)            IEM_MC_RETURN_ON_FAILURE(iemMemStackPushU32(pVCpu, (a_u32Value)))
# define IEM_MC_PUSH_U32_SREG(a_uSegVal)        IEM_MC_RETURN_ON_FAILURE(iemMemStackPushU32SReg(pVCpu, (a_uSegVal)))
# define IEM_MC_PUSH_U64(a_u64Value)            IEM_MC_RETURN_ON_FAILURE(iemMemStackPushU64(pVCpu, (a_u64Value)))

# define IEM_MC_POP_GREG_U16(a_iGReg)           IEM_MC_RETURN_ON_FAILURE(iemMemStackPopGRegU16(pVCpu, (a_iGReg)))
# define IEM_MC_POP_GREG_U32(a_iGReg)           IEM_MC_RETURN_ON_FAILURE(iemMemStackPopGRegU32(pVCpu, (a_iGReg)))
# define IEM_MC_POP_GREG_U64(a_iGReg)           IEM_MC_RETURN_ON_FAILURE(iemMemStackPopGRegU64(pVCpu, (a_iGReg)))
#else
# define IEM_MC_PUSH_U16(a_u16Value)            iemMemStackPushU16Jmp(pVCpu, (a_u16Value))
# define IEM_MC_PUSH_U32(a_u32Value)            iemMemStackPushU32Jmp(pVCpu, (a_u32Value))
# define IEM_MC_PUSH_U32_SREG(a_uSegVal)        iemMemStackPushU32SRegJmp(pVCpu, (a_uSegVal))
# define IEM_MC_PUSH_U64(a_u64Value)            iemMemStackPushU64Jmp(pVCpu, (a_u64Value))

# define IEM_MC_POP_GREG_U16(a_iGReg)           iemMemStackPopGRegU16Jmp(pVCpu, (a_iGReg))
# define IEM_MC_POP_GREG_U32(a_iGReg)           iemMemStackPopGRegU32Jmp(pVCpu, (a_iGReg))
# define IEM_MC_POP_GREG_U64(a_iGReg)           iemMemStackPopGRegU64Jmp(pVCpu, (a_iGReg))
#endif

/* 32-bit flat stack push and pop: */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_FLAT32_PUSH_U16(a_u16Value)     IEM_MC_RETURN_ON_FAILURE(iemMemStackPushU16(pVCpu, (a_u16Value)))
# define IEM_MC_FLAT32_PUSH_U32(a_u32Value)     IEM_MC_RETURN_ON_FAILURE(iemMemStackPushU32(pVCpu, (a_u32Value)))
# define IEM_MC_FLAT32_PUSH_U32_SREG(a_uSegVal) IEM_MC_RETURN_ON_FAILURE(iemMemStackPushU32SReg(pVCpu, (a_uSegVal)))

# define IEM_MC_FLAT32_POP_GREG_U16(a_iGReg)    IEM_MC_RETURN_ON_FAILURE(iemMemStackPopGRegU16(pVCpu, (a_iGReg)))
# define IEM_MC_FLAT32_POP_GREG_U32(a_iGReg)    IEM_MC_RETURN_ON_FAILURE(iemMemStackPopGRegU32(pVCpu, (a_iGReg)))
#else
# define IEM_MC_FLAT32_PUSH_U16(a_u16Value)     iemMemFlat32StackPushU16Jmp(pVCpu, (a_u16Value))
# define IEM_MC_FLAT32_PUSH_U32(a_u32Value)     iemMemFlat32StackPushU32Jmp(pVCpu, (a_u32Value))
# define IEM_MC_FLAT32_PUSH_U32_SREG(a_uSegVal) iemMemFlat32StackPushU32SRegJmp(pVCpu, (a_uSegVal))

# define IEM_MC_FLAT32_POP_GREG_U16(a_iGReg)    iemMemFlat32StackPopGRegU16Jmp(pVCpu, a_iGReg))
# define IEM_MC_FLAT32_POP_GREG_U32(a_iGReg)    iemMemFlat32StackPopGRegU32Jmp(pVCpu, a_iGReg))
#endif

/* 64-bit flat stack push and pop: */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_FLAT64_PUSH_U16(a_u16Value)     IEM_MC_RETURN_ON_FAILURE(iemMemStackPushU16(pVCpu, (a_u16Value)))
# define IEM_MC_FLAT64_PUSH_U64(a_u64Value)     IEM_MC_RETURN_ON_FAILURE(iemMemStackPushU64(pVCpu, (a_u64Value)))

# define IEM_MC_FLAT64_POP_GREG_U16(a_iGReg)    IEM_MC_RETURN_ON_FAILURE(iemMemStackPopGRegU16(pVCpu, (a_iGReg)))
# define IEM_MC_FLAT64_POP_GREG_U64(a_iGReg)    IEM_MC_RETURN_ON_FAILURE(iemMemStackPopGRegU64(pVCpu, (a_iGReg)))
#else
# define IEM_MC_FLAT64_PUSH_U16(a_u16Value)     iemMemFlat64StackPushU16Jmp(pVCpu, (a_u16Value))
# define IEM_MC_FLAT64_PUSH_U64(a_u64Value)     iemMemFlat64StackPushU64Jmp(pVCpu, (a_u64Value))

# define IEM_MC_FLAT64_POP_GREG_U16(a_iGReg)    iemMemFlat64StackPopGRegU16Jmp(pVCpu, (a_iGReg))
# define IEM_MC_FLAT64_POP_GREG_U64(a_iGReg)    iemMemFlat64StackPopGRegU64Jmp(pVCpu, (a_iGReg))
#endif


/* 8-bit */

/**
 * Maps guest memory for byte atomic read+write direct (or bounce) buffer
 * acccess, for atomic operations.
 *
 * @param[out] a_pu8Mem     Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_iSeg       The segment register to access via. No UINT8_MAX!
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_ATOMIC
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_MAP_U8_ATOMIC(a_pu8Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu8Mem), &(a_bUnmapInfo), sizeof(uint8_t), (a_iSeg), \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_ATOMIC, 0))
#else
# define IEM_MC_MEM_MAP_U8_ATOMIC(a_pu8Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu8Mem) = iemMemMapDataU8AtJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for byte read+write direct (or bounce) buffer acccess.
 *
 * @param[out] a_pu8Mem     Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_iSeg       The segment register to access via. No UINT8_MAX!
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_RW
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_MAP_U8_RW(a_pu8Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu8Mem), &(a_bUnmapInfo), sizeof(uint8_t), (a_iSeg), \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_RW, 0))
#else
# define IEM_MC_MEM_MAP_U8_RW(a_pu8Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu8Mem) = iemMemMapDataU8RwJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for byte writeonly direct (or bounce) buffer acccess.
 *
 * @param[out] a_pu8Mem     Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_iSeg       The segment register to access via. No UINT8_MAX!
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_WO
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_MAP_U8_WO(a_pu8Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu8Mem), &(a_bUnmapInfo), sizeof(uint8_t), (a_iSeg), \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_W, 0))
#else
# define IEM_MC_MEM_MAP_U8_WO(a_pu8Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu8Mem) = iemMemMapDataU8WoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for byte readonly direct (or bounce) buffer acccess.
 *
 * @param[out] a_pu8Mem     Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_iSeg       The segment register to access via. No UINT8_MAX!
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_RO
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_MAP_U8_RO(a_pu8Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu8Mem), &(a_bUnmapInfo), sizeof(uint8_t), (a_iSeg), \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_R, 0))
#else
# define IEM_MC_MEM_MAP_U8_RO(a_pu8Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu8Mem) = iemMemMapDataU8RoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for byte atomic read+write direct (or bounce) buffer
 * acccess, flat address variant.
 *
 * @param[out] a_pu8Mem     Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_ATOMIC
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_FLAT_MAP_U8_ATOMIC(a_pu8Mem, a_bUnmapInfo, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu8Mem), &(a_bUnmapInfo), sizeof(uint8_t), UINT8_MAX, \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_ATOMIC, 0))
#else
# define IEM_MC_MEM_FLAT_MAP_U8_ATOMIC(a_pu8Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu8Mem) = iemMemFlatMapDataU8AtJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for byte read+write direct (or bounce) buffer acccess, flat
 * address variant.
 *
 * @param[out] a_pu8Mem     Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_RW
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_FLAT_MAP_U8_RW(a_pu8Mem, a_bUnmapInfo, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu8Mem), &(a_bUnmapInfo), sizeof(uint8_t), UINT8_MAX, \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_RW, 0))
#else
# define IEM_MC_MEM_FLAT_MAP_U8_RW(a_pu8Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu8Mem) = iemMemFlatMapDataU8RwJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for byte writeonly direct (or bounce) buffer acccess, flat
 * address variant.
 *
 * @param[out] a_pu8Mem     Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_WO
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_FLAT_MAP_U8_WO(a_pu8Mem, a_bUnmapInfo, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu8Mem), &(a_bUnmapInfo), sizeof(uint8_t), UINT8_MAX, \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_W, 0))
#else
# define IEM_MC_MEM_FLAT_MAP_U8_WO(a_pu8Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu8Mem) = iemMemFlatMapDataU8WoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for byte readonly direct (or bounce) buffer acccess, flat
 * address variant.
 *
 * @param[out] a_pu8Mem     Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_RO
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_FLAT_MAP_U8_RO(a_pu8Mem, a_bUnmapInfo, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu8Mem), &(a_bUnmapInfo), sizeof(uint8_t), UINT8_MAX, \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_R, 0))
#else
# define IEM_MC_MEM_FLAT_MAP_U8_RO(a_pu8Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu8Mem) = iemMemFlatMapDataU8RoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))
#endif


/* 16-bit */

/**
 * Maps guest memory for word atomic read+write direct (or bounce) buffer acccess.
 *
 * @param[out] a_pu16Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_iSeg       The segment register to access via. No UINT8_MAX!
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_ATOMIC
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_MAP_U16_ATOMIC(a_pu16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu16Mem), &(a_bUnmapInfo), sizeof(uint16_t), (a_iSeg), \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_ATOMIC, sizeof(uint16_t) - 1))
#else
# define IEM_MC_MEM_MAP_U16_ATOMIC(a_pu16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu16Mem) = iemMemMapDataU16AtJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for word read+write direct (or bounce) buffer acccess.
 *
 * @param[out] a_pu16Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_iSeg       The segment register to access via. No UINT8_MAX!
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_RW
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_MAP_U16_RW(a_pu16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu16Mem), &(a_bUnmapInfo), sizeof(uint16_t), (a_iSeg), \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_RW, sizeof(uint16_t) - 1))
#else
# define IEM_MC_MEM_MAP_U16_RW(a_pu16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu16Mem) = iemMemMapDataU16RwJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for word writeonly direct (or bounce) buffer acccess.
 *
 * @param[out] a_pu16Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_iSeg       The segment register to access via. No UINT8_MAX!
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_WO
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_MAP_U16_WO(a_pu16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu16Mem), &(a_bUnmapInfo), sizeof(uint16_t), (a_iSeg), \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_W, sizeof(uint16_t) - 1))
#else
# define IEM_MC_MEM_MAP_U16_WO(a_pu16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu16Mem) = iemMemMapDataU16WoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for word readonly direct (or bounce) buffer acccess.
 *
 * @param[out] a_pu16Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_iSeg       The segment register to access via. No UINT8_MAX!
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_RO
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_MAP_U16_RO(a_pu16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu16Mem), &(a_bUnmapInfo), sizeof(uint16_t), (a_iSeg), \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_R, sizeof(uint16_t) - 1))
#else
# define IEM_MC_MEM_MAP_U16_RO(a_pu16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu16Mem) = iemMemMapDataU16RoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for word atomic read+write direct (or bounce) buffer
 * acccess, flat address variant.
 *
 * @param[out] a_pu16Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_ATOMIC
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_FLAT_MAP_U16_ATOMIC(a_pu16Mem, a_bUnmapInfo, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu16Mem), &(a_bUnmapInfo), sizeof(uint16_t), UINT8_MAX, \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_ATOMIC, sizeof(uint16_t) - 1))
#else
# define IEM_MC_MEM_FLAT_MAP_U16_ATOMIC(a_pu16Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu16Mem) = iemMemFlatMapDataU16AtJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for word read+write direct (or bounce) buffer acccess, flat
 * address variant.
 *
 * @param[out] a_pu16Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_RW
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_FLAT_MAP_U16_RW(a_pu16Mem, a_bUnmapInfo, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu16Mem), &(a_bUnmapInfo), sizeof(uint16_t), UINT8_MAX, \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_RW, sizeof(uint16_t) - 1))
#else
# define IEM_MC_MEM_FLAT_MAP_U16_RW(a_pu16Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu16Mem) = iemMemFlatMapDataU16RwJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for word writeonly direct (or bounce) buffer acccess, flat
 * address variant.
 *
 * @param[out] a_pu16Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_WO
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_FLAT_MAP_U16_WO(a_pu16Mem, a_bUnmapInfo, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu16Mem), &(a_bUnmapInfo), sizeof(uint16_t), UINT8_MAX, \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_W, sizeof(uint16_t) - 1))
#else
# define IEM_MC_MEM_FLAT_MAP_U16_WO(a_pu16Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu16Mem) = iemMemFlatMapDataU16WoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for word readonly direct (or bounce) buffer acccess, flat
 * address variant.
 *
 * @param[out] a_pu16Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_RO
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_FLAT_MAP_U16_RO(a_pu16Mem, a_bUnmapInfo, a_GCPtrMem) \
   IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu16Mem), &(a_bUnmapInfo), sizeof(uint16_t), UINT8_MAX, \
                                      (a_GCPtrMem), IEM_ACCESS_DATA_R, sizeof(uint16_t) - 1))
#else
# define IEM_MC_MEM_FLAT_MAP_U16_RO(a_pu16Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu16Mem) = iemMemFlatMapDataU16RoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))
#endif

/** int16_t alias. */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_MAP_I16_WO(a_pi16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
         IEM_MC_MEM_MAP_U16_WO(a_pi16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)
#else
# define IEM_MC_MEM_MAP_I16_WO(a_pi16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pi16Mem) = (int16_t *)iemMemMapDataU16WoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))
#endif

/** Flat int16_t alias. */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_FLAT_MAP_I16_WO(a_pi16Mem, a_bUnmapInfo, a_GCPtrMem) \
         IEM_MC_MEM_FLAT_MAP_U16_WO(a_pi16Mem, a_bUnmapInfo, a_GCPtrMem)
#else
# define IEM_MC_MEM_FLAT_MAP_I16_WO(a_pi16Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pi16Mem) = (int16_t *)iemMemFlatMapDataU16WoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))
#endif


/* 32-bit */

/**
 * Maps guest memory for dword atomic read+write direct (or bounce) buffer acccess.
 *
 * @param[out] a_pu32Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_iSeg       The segment register to access via. No UINT8_MAX!
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_ATOMIC
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_MAP_U32_ATOMIC(a_pu32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu32Mem), &(a_bUnmapInfo), sizeof(uint32_t), (a_iSeg), \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_ATOMIC, sizeof(uint32_t) - 1))
#else
# define IEM_MC_MEM_MAP_U32_ATOMIC(a_pu32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu32Mem) = iemMemMapDataU32AtJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for dword read+write direct (or bounce) buffer acccess.
 *
 * @param[out] a_pu32Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_iSeg       The segment register to access via. No UINT8_MAX!
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_RW
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_MAP_U32_RW(a_pu32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu32Mem), &(a_bUnmapInfo), sizeof(uint32_t), (a_iSeg), \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_RW, sizeof(uint32_t) - 1))
#else
# define IEM_MC_MEM_MAP_U32_RW(a_pu32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu32Mem) = iemMemMapDataU32RwJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for dword writeonly direct (or bounce) buffer acccess.
 *
 * @param[out] a_pu32Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_iSeg       The segment register to access via. No UINT8_MAX!
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_WO
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_MAP_U32_WO(a_pu32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu32Mem), &(a_bUnmapInfo), sizeof(uint32_t), (a_iSeg), \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_W, sizeof(uint32_t) - 1))
#else
# define IEM_MC_MEM_MAP_U32_WO(a_pu32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu32Mem) = iemMemMapDataU32WoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for dword readonly direct (or bounce) buffer acccess.
 *
 * @param[out] a_pu32Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_iSeg       The segment register to access via. No UINT8_MAX!
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_RO
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_MAP_U32_RO(a_pu32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu32Mem), &(a_bUnmapInfo), sizeof(uint32_t), (a_iSeg), \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_R, sizeof(uint32_t) - 1))
#else
# define IEM_MC_MEM_MAP_U32_RO(a_pu32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu32Mem) = iemMemMapDataU32RoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for dword atomic read+write direct (or bounce) buffer
 * acccess, flat address variant.
 *
 * @param[out] a_pu32Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_ATOMIC
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_FLAT_MAP_U32_ATOMIC(a_pu32Mem, a_bUnmapInfo, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu32Mem), &(a_bUnmapInfo), sizeof(uint32_t), UINT8_MAX, \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_ATOMIC, sizeof(uint32_t) - 1))
#else
# define IEM_MC_MEM_FLAT_MAP_U32_ATOMIC(a_pu32Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu32Mem) = iemMemFlatMapDataU32AtJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for dword read+write direct (or bounce) buffer acccess,
 * flat address variant.
 *
 * @param[out] a_pu32Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_RW
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_FLAT_MAP_U32_RW(a_pu32Mem, a_bUnmapInfo, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu32Mem), &(a_bUnmapInfo), sizeof(uint32_t), UINT8_MAX, \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_RW, sizeof(uint32_t) - 1))
#else
# define IEM_MC_MEM_FLAT_MAP_U32_RW(a_pu32Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu32Mem) = iemMemFlatMapDataU32RwJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for dword writeonly direct (or bounce) buffer acccess, flat
 * address variant.
 *
 * @param[out] a_pu32Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_WO
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_FLAT_MAP_U32_WO(a_pu32Mem, a_bUnmapInfo, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu32Mem), &(a_bUnmapInfo), sizeof(uint32_t), UINT8_MAX, \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_W, sizeof(uint32_t) - 1))
#else
# define IEM_MC_MEM_FLAT_MAP_U32_WO(a_pu32Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu32Mem) = iemMemFlatMapDataU32WoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for dword readonly direct (or bounce) buffer acccess, flat
 * address variant.
 *
 * @param[out] a_pu32Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_RO
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_FLAT_MAP_U32_RO(a_pu32Mem, a_bUnmapInfo, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu32Mem), &(a_bUnmapInfo), sizeof(uint32_t), UINT8_MAX, \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_R, sizeof(uint32_t) - 1))
#else
# define IEM_MC_MEM_FLAT_MAP_U32_RO(a_pu32Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu32Mem) = iemMemFlatMapDataU32RoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))
#endif

/** int32_t alias. */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_MAP_I32_WO(a_pi32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
         IEM_MC_MEM_MAP_U32_WO(a_pi32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)
#else
# define IEM_MC_MEM_MAP_I32_WO(a_pi32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pi32Mem) = (int32_t *)iemMemMapDataU32WoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))
#endif

/** Flat int32_t alias. */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_FLAT_MAP_I32_WO(a_pi32Mem, a_bUnmapInfo, a_GCPtrMem) \
         IEM_MC_MEM_FLAT_MAP_U32_WO(a_pi32Mem, a_bUnmapInfo, a_GCPtrMem)
#else
# define IEM_MC_MEM_FLAT_MAP_I32_WO(a_pi32Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pi32Mem) = (int32_t *)iemMemFlatMapDataU32WoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))
#endif

/** RTFLOAT32U alias. */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_MAP_R32_WO(a_pr32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
         IEM_MC_MEM_MAP_U32_WO(a_pr32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)
#else
# define IEM_MC_MEM_MAP_R32_WO(a_pr32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pr32Mem) = (PRTFLOAT32U)iemMemMapDataU32WoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))
#endif

/** Flat RTFLOAT32U alias. */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_FLAT_MAP_R32_WO(a_pr32Mem, a_bUnmapInfo, a_GCPtrMem) \
         IEM_MC_MEM_FLAT_MAP_U32_WO(a_pr32Mem, a_bUnmapInfo, a_GCPtrMem)
#else
# define IEM_MC_MEM_FLAT_MAP_R32_WO(a_pr32Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pr32Mem) = (PRTFLOAT32U)iemMemFlatMapDataU32WoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))
#endif


/* 64-bit */

/**
 * Maps guest memory for qword atomic read+write direct (or bounce) buffer acccess.
 *
 * @param[out] a_pu64Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_iSeg       The segment register to access via. No UINT8_MAX!
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_ATOMIC
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_MAP_U64_ATOMIC(a_pu64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu64Mem), &(a_bUnmapInfo), sizeof(uint64_t), (a_iSeg), \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_ATOMIC, sizeof(uint64_t) - 1))
#else
# define IEM_MC_MEM_MAP_U64_ATOMIC(a_pu64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu64Mem) = iemMemMapDataU64AtJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for qword read+write direct (or bounce) buffer acccess.
 *
 * @param[out] a_pu64Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_iSeg       The segment register to access via. No UINT8_MAX!
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_RW
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_MAP_U64_RW(a_pu64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu64Mem), &(a_bUnmapInfo), sizeof(uint64_t), (a_iSeg), \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_RW, sizeof(uint64_t) - 1))
#else
# define IEM_MC_MEM_MAP_U64_RW(a_pu64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu64Mem) = iemMemMapDataU64RwJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for qword writeonly direct (or bounce) buffer acccess.
 *
 * @param[out] a_pu64Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_iSeg       The segment register to access via. No UINT8_MAX!
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_WO
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_MAP_U64_WO(a_pu64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu64Mem), &(a_bUnmapInfo), sizeof(uint64_t), (a_iSeg), \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_W, sizeof(uint64_t) - 1))
#else
# define IEM_MC_MEM_MAP_U64_WO(a_pu64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu64Mem) = iemMemMapDataU64WoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for qword readonly direct (or bounce) buffer acccess.
 *
 * @param[out] a_pu64Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_iSeg       The segment register to access via. No UINT8_MAX!
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_RO
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_MAP_U64_RO(a_pu64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu64Mem), &(a_bUnmapInfo), sizeof(uint64_t), (a_iSeg), \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_R, sizeof(uint64_t) - 1))
#else
# define IEM_MC_MEM_MAP_U64_RO(a_pu64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu64Mem) = iemMemMapDataU64RoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for qword atomic read+write direct (or bounce) buffer
 * acccess, flat address variant.
 *
 * @param[out] a_pu64Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_ATOMIC
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_FLAT_MAP_U64_ATOMIC(a_pu64Mem, a_bUnmapInfo, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu64Mem), &(a_bUnmapInfo), sizeof(uint64_t), UINT8_MAX, \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_ATOMIC, sizeof(uint64_t) - 1))
#else
# define IEM_MC_MEM_FLAT_MAP_U64_ATOMIC(a_pu64Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu64Mem) = iemMemFlatMapDataU64AtJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for qword read+write direct (or bounce) buffer acccess,
 * flat address variant.
 *
 * @param[out] a_pu64Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_RW
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_FLAT_MAP_U64_RW(a_pu64Mem, a_bUnmapInfo, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu64Mem), &(a_bUnmapInfo), sizeof(uint64_t), UINT8_MAX, \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_RW, sizeof(uint64_t) - 1))
#else
# define IEM_MC_MEM_FLAT_MAP_U64_RW(a_pu64Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu64Mem) = iemMemFlatMapDataU64RwJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for qword writeonly direct (or bounce) buffer acccess, flat
 * address variant.
 *
 * @param[out] a_pu64Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_WO
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_FLAT_MAP_U64_WO(a_pu64Mem, a_bUnmapInfo, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu64Mem), &(a_bUnmapInfo), sizeof(uint64_t), UINT8_MAX, \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_W, sizeof(uint64_t) - 1))
#else
# define IEM_MC_MEM_FLAT_MAP_U64_WO(a_pu64Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu64Mem) = iemMemFlatMapDataU64WoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for qword readonly direct (or bounce) buffer acccess, flat
 * address variant.
 *
 * @param[out] a_pu64Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_RO
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_FLAT_MAP_U64_RO(a_pu64Mem, a_bUnmapInfo, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu64Mem), &(a_bUnmapInfo), sizeof(uint64_t), UINT8_MAX, \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_R, sizeof(uint64_t) - 1))
#else
# define IEM_MC_MEM_FLAT_MAP_U64_RO(a_pu64Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu64Mem) = iemMemFlatMapDataU64RoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))
#endif

/** int64_t alias. */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_MAP_I64_WO(a_pi64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
         IEM_MC_MEM_MAP_U64_WO(a_pi64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)
#else
# define IEM_MC_MEM_MAP_I64_WO(a_pi64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pi64Mem) = (int64_t *)iemMemMapDataU64WoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))
#endif

/** Flat int64_t alias. */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_FLAT_MAP_I64_WO(a_pi64Mem, a_bUnmapInfo, a_GCPtrMem) \
         IEM_MC_MEM_FLAT_MAP_U64_WO(a_pi64Mem, a_bUnmapInfo, a_GCPtrMem)
#else
# define IEM_MC_MEM_FLAT_MAP_I64_WO(a_pi64Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pi64Mem) = (int64_t *)iemMemFlatMapDataU64WoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))
#endif

/** RTFLOAT64U alias. */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_MAP_R64_WO(a_pr64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
         IEM_MC_MEM_MAP_U64_WO(a_pr64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem)
#else
# define IEM_MC_MEM_MAP_R64_WO(a_pr64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pr64Mem) = (PRTFLOAT64U)iemMemMapDataU64WoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))
#endif

/** Flat RTFLOAT64U alias. */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_FLAT_MAP_R64_WO(a_pr64Mem, a_bUnmapInfo, a_GCPtrMem) \
         IEM_MC_MEM_FLAT_MAP_U64_WO(a_pr64Mem, a_bUnmapInfo, a_GCPtrMem)
#else
# define IEM_MC_MEM_FLAT_MAP_R64_WO(a_pr64Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pr64Mem) = (PRTFLOAT64U)iemMemFlatMapDataU64WoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))
#endif


/* 128-bit */

/**
 * Maps guest memory for dqword atomic read+write direct (or bounce) buffer acccess.
 *
 * @param[out] a_pu128Mem   Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_iSeg       The segment register to access via. No UINT8_MAX!
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_ATOMIC
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_MAP_U128_ATOMIC(a_pu128Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu128Mem), &(a_bUnmapInfo), sizeof(RTUINT128U), (a_iSeg), \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_ATOMIC, sizeof(RTUINT128U) - 1))
#else
# define IEM_MC_MEM_MAP_U128_ATOMIC(a_pu128Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu128Mem) = iemMemMapDataU128AtJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for dqword read+write direct (or bounce) buffer acccess.
 *
 * @param[out] a_pu128Mem   Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_iSeg       The segment register to access via. No UINT8_MAX!
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_RW
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_MAP_U128_RW(a_pu128Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu128Mem), &(a_bUnmapInfo), sizeof(RTUINT128U), (a_iSeg), \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_RW, sizeof(RTUINT128U) - 1))
#else
# define IEM_MC_MEM_MAP_U128_RW(a_pu128Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu128Mem) = iemMemMapDataU128RwJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for dqword writeonly direct (or bounce) buffer acccess.
 *
 * @param[out] a_pu128Mem   Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_iSeg       The segment register to access via. No UINT8_MAX!
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_WO
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_MAP_U128_WO(a_pu128Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu128Mem), &(a_bUnmapInfo), sizeof(RTUINT128), (a_iSeg), \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_W, sizeof(RTUINT128) - 1))
#else
# define IEM_MC_MEM_MAP_U128_WO(a_pu128Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu128Mem) = iemMemMapDataU128WoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for dqword readonly direct (or bounce) buffer acccess.
 *
 * @param[out] a_pu128Mem   Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_iSeg       The segment register to access via. No UINT8_MAX!
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_RO
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_MAP_U128_RO(a_pu128Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu128Mem), &(a_bUnmapInfo), sizeof(RTUINT128), (a_iSeg), \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_R, sizeof(RTUINT128) - 1))
#else
# define IEM_MC_MEM_MAP_U128_RO(a_pu128Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu128Mem) = iemMemMapDataU128RoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for dqword atomic read+write direct (or bounce) buffer
 * access, flat address variant.
 *
 * @param[out] a_pu128Mem   Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_ATOMIC
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_FLAT_MAP_U128_ATOMIC(a_pu128Mem, a_bUnmapInfo, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu128Mem), &(a_bUnmapInfo), sizeof(RTUINT128), UINT8_MAX, \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_ATOMIC, sizeof(RTUINT128) - 1))
#else
# define IEM_MC_MEM_FLAT_MAP_U128_ATOMIC(a_pu128Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu128Mem) = iemMemFlatMapDataU128AtJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for dqword read+write direct (or bounce) buffer acccess,
 * flat address variant.
 *
 * @param[out] a_pu128Mem   Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_RW
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_FLAT_MAP_U128_RW(a_pu128Mem, a_bUnmapInfo, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu128Mem), &(a_bUnmapInfo), sizeof(RTUINT128), UINT8_MAX, \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_RW, sizeof(RTUINT128) - 1))
#else
# define IEM_MC_MEM_FLAT_MAP_U128_RW(a_pu128Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu128Mem) = iemMemFlatMapDataU128RwJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for dqword writeonly direct (or bounce) buffer acccess,
 * flat address variant.
 *
 * @param[out] a_pu128Mem   Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_WO
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_FLAT_MAP_U128_WO(a_pu128Mem, a_bUnmapInfo, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu128Mem), &(a_bUnmapInfo), sizeof(RTUINT128), UINT8_MAX, \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_W, sizeof(RTUINT128) - 1))
#else
# define IEM_MC_MEM_FLAT_MAP_U128_WO(a_pu128Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu128Mem) = iemMemFlatMapDataU128WoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for dqword readonly direct (or bounce) buffer acccess, flat
 * address variant.
 *
 * @param[out] a_pu128Mem   Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_RO
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_FLAT_MAP_U128_RO(a_pu128Mem, a_bUnmapInfo, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pu128Mem), &(a_bUnmapInfo), sizeof(RTUINT128), UINT8_MAX, \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_R, sizeof(RTUINT128) - 1))
#else
# define IEM_MC_MEM_FLAT_MAP_U128_RO(a_pu128Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu128Mem) = iemMemFlatMapDataU128RoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))
#endif


/* misc */

/**
 * Maps guest memory for 80-bit float writeonly direct (or bounce) buffer acccess.
 *
 * @param[out] a_pr80Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_iSeg       The segment register to access via. No UINT8_MAX!
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_WO
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_MAP_R80_WO(a_pr80Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pr80Mem), &(a_bUnmapInfo), sizeof(RTFLOAT80U), (a_iSeg), \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_W, sizeof(uint64_t) - 1))
#else
# define IEM_MC_MEM_MAP_R80_WO(a_pr80Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pr80Mem) = iemMemMapDataR80WoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for 80-bit float writeonly direct (or bounce) buffer acccess.
 *
 * @param[out] a_pr80Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_WO
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_FLAT_MAP_R80_WO(a_pr80Mem, a_bUnmapInfo, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pr80Mem), &(a_bUnmapInfo), sizeof(RTFLOAT80U), UINT8_MAX, \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_W, sizeof(uint64_t) - 1))
#else
# define IEM_MC_MEM_FLAT_MAP_R80_WO(a_pr80Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pr80Mem) = iemMemFlatMapDataR80WoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))
#endif


/**
 * Maps guest memory for 80-bit BCD writeonly direct (or bounce) buffer acccess.
 *
 * @param[out] a_pd80Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_iSeg       The segment register to access via. No UINT8_MAX!
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_WO
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_MAP_D80_WO(a_pd80Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pd80Mem), &(a_bUnmapInfo), sizeof(RTFLOAT80U), (a_iSeg), \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_W, sizeof(uint64_t) - 1))
#else
# define IEM_MC_MEM_MAP_D80_WO(a_pd80Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pd80Mem) = iemMemMapDataD80WoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))
#endif

/**
 * Maps guest memory for 80-bit BCD writeonly direct (or bounce) buffer acccess.
 *
 * @param[out] a_pd80Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_WO
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_FLAT_MAP_D80_WO(a_pd80Mem, a_bUnmapInfo, a_GCPtrMem) \
    IEM_MC_RETURN_ON_FAILURE(iemMemMap(pVCpu, (void **)&(a_pd80Mem), &(a_bUnmapInfo), sizeof(RTFLOAT80U), UINT8_MAX, \
                                       (a_GCPtrMem), IEM_ACCESS_DATA_W, sizeof(uint64_t) - 1))
#else
# define IEM_MC_MEM_FLAT_MAP_D80_WO(a_pd80Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pd80Mem) = iemMemFlatMapDataD80WoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))
#endif



/* commit + unmap */

/** Commits the memory and unmaps guest memory previously mapped RW.
 * @remarks     May return.
 * @note        Implictly frees the a_bMapInfo variable.
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_COMMIT_AND_UNMAP_RW(a_bMapInfo)         IEM_MC_RETURN_ON_FAILURE(iemMemCommitAndUnmap(pVCpu, a_bMapInfo))
#else
# define IEM_MC_MEM_COMMIT_AND_UNMAP_RW(a_bMapInfo)         iemMemCommitAndUnmapRwJmp(pVCpu, (a_bMapInfo))
#endif

/** Commits the memory and unmaps guest memory previously mapped ATOMIC.
 * @remarks     May return.
 * @note        Implictly frees the a_bMapInfo variable.
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_COMMIT_AND_UNMAP_ATOMIC(a_bMapInfo)     IEM_MC_RETURN_ON_FAILURE(iemMemCommitAndUnmap(pVCpu, a_bMapInfo))
#else
# define IEM_MC_MEM_COMMIT_AND_UNMAP_ATOMIC(a_bMapInfo)     iemMemCommitAndUnmapRwJmp(pVCpu, (a_bMapInfo))
#endif

/** Commits the memory and unmaps guest memory previously mapped W.
 * @remarks     May return.
 * @note        Implictly frees the a_bMapInfo variable.
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_COMMIT_AND_UNMAP_WO(a_bMapInfo)         IEM_MC_RETURN_ON_FAILURE(iemMemCommitAndUnmap(pVCpu, a_bMapInfo))
#else
# define IEM_MC_MEM_COMMIT_AND_UNMAP_WO(a_bMapInfo)         iemMemCommitAndUnmapWoJmp(pVCpu, (a_bMapInfo))
#endif

/** Commits the memory and unmaps guest memory previously mapped R.
 * @remarks     May return.
 * @note        Implictly frees the a_bMapInfo variable.
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_COMMIT_AND_UNMAP_RO(a_bMapInfo)         IEM_MC_RETURN_ON_FAILURE(iemMemCommitAndUnmap(pVCpu, a_bMapInfo))
#else
# define IEM_MC_MEM_COMMIT_AND_UNMAP_RO(a_bMapInfo)         iemMemCommitAndUnmapRoJmp(pVCpu, (a_bMapInfo))
#endif


/** Commits the memory and unmaps the guest memory unless the FPU status word
 * indicates (@a a_u16FSW) and FPU control word indicates a pending exception
 * that would cause FLD not to store.
 *
 * The current understanding is that \#O, \#U, \#IA and \#IS will prevent a
 * store, while \#P will not.
 *
 * @remarks     May in theory return - for now.
 * @note        Implictly frees both the a_bMapInfo and a_u16FSW variables.
 */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_COMMIT_AND_UNMAP_FOR_FPU_STORE_WO(a_bMapInfo, a_u16FSW) do { \
        if (   !(a_u16FSW & X86_FSW_ES) \
            || !(  (a_u16FSW & (X86_FSW_UE | X86_FSW_OE | X86_FSW_IE)) \
                 & ~(pVCpu->cpum.GstCtx.XState.x87.FCW & X86_FCW_MASK_ALL) ) ) \
            IEM_MC_RETURN_ON_FAILURE(iemMemCommitAndUnmap(pVCpu, a_bMapInfo)); \
        else \
            iemMemRollbackAndUnmap(pVCpu, (a_pvMem), IEM_ACCESS_DATA_W); \
    } while (0)
#else
# define IEM_MC_MEM_COMMIT_AND_UNMAP_FOR_FPU_STORE_WO(a_bMapInfo, a_u16FSW) do { \
        if (   !(a_u16FSW & X86_FSW_ES) \
            || !(  (a_u16FSW & (X86_FSW_UE | X86_FSW_OE | X86_FSW_IE)) \
                 & ~(pVCpu->cpum.GstCtx.XState.x87.FCW & X86_FCW_MASK_ALL) ) ) \
            iemMemCommitAndUnmapWoJmp(pVCpu, a_bMapInfo); \
        else \
            iemMemRollbackAndUnmapWo(pVCpu, a_bMapInfo); \
    } while (0)
#endif

/** Rolls back (conceptually only, assumes no writes) and unmaps the guest memory.
 * @note        Implictly frees the a_bMapInfo variable. */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_MEM_ROLLBACK_AND_UNMAP_WO(a_bMapInfo)       iemMemRollbackAndUnmap(pVCpu, a_bMapInfo)
#else
# define IEM_MC_MEM_ROLLBACK_AND_UNMAP_WO(a_bMapInfo)       iemMemRollbackAndUnmapWo(pVCpu, a_bMapInfo)
#endif



/** Calculate efficient address from R/M. */
#ifndef IEM_WITH_SETJMP
# define IEM_MC_CALC_RM_EFF_ADDR(a_GCPtrEff, a_bRm, a_cbImmAndRspOffset) \
    IEM_MC_RETURN_ON_FAILURE(iemOpHlpCalcRmEffAddr(pVCpu, (a_bRm), (a_cbImmAndRspOffset), &(a_GCPtrEff)))
#else
# define IEM_MC_CALC_RM_EFF_ADDR(a_GCPtrEff, a_bRm, a_cbImmAndRspOffset) \
    ((a_GCPtrEff) = iemOpHlpCalcRmEffAddrJmp(pVCpu, (a_bRm), (a_cbImmAndRspOffset)))
#endif


/** The @a a_fSupportedHosts mask are ORed together RT_ARCH_VAL_XXX values. */
#define IEM_MC_NATIVE_IF(a_fSupportedHosts)                               if (false) {
#define IEM_MC_NATIVE_ELSE()                                              } else {
#define IEM_MC_NATIVE_ENDIF()                                             } ((void)0)

#define IEM_MC_NATIVE_EMIT_0(a_fnEmitter)
#define IEM_MC_NATIVE_EMIT_1(a_fnEmitter, a0)                             (void)(a0)
#define IEM_MC_NATIVE_EMIT_2(a_fnEmitter, a0, a1)                         (void)(a0), (void)(a1)
#define IEM_MC_NATIVE_EMIT_2_EX(a_fnEmitter, a0, a1)                      (void)(a0), (void)(a1)
#define IEM_MC_NATIVE_EMIT_3(a_fnEmitter, a0, a1, a2)                     (void)(a0), (void)(a1), (void)(a2)
#define IEM_MC_NATIVE_EMIT_4(a_fnEmitter, a0, a1, a2, a3)                 (void)(a0), (void)(a1), (void)(a2), (void)(a3)
#define IEM_MC_NATIVE_EMIT_5(a_fnEmitter, a0, a1, a2, a3, a4)             (void)(a0), (void)(a1), (void)(a2), (void)(a3), (void)(a4)
#define IEM_MC_NATIVE_EMIT_6(a_fnEmitter, a0, a1, a2, a3, a4, a5)         (void)(a0), (void)(a1), (void)(a2), (void)(a3), (void)(a4), (void)(a5)
#define IEM_MC_NATIVE_EMIT_7(a_fnEmitter, a0, a1, a2, a3, a4, a5, a6)     (void)(a0), (void)(a1), (void)(a2), (void)(a3), (void)(a4), (void)(a5), (void)(a6)
#define IEM_MC_NATIVE_EMIT_8(a_fnEmitter, a0, a1, a2, a3, a4, a5, a6, a7) (void)(a0), (void)(a1), (void)(a2), (void)(a3), (void)(a4), (void)(a5), (void)(a6), (void)(a7)

/** This can be used to direct the register allocator when dealing with
 * x86/AMD64 instructions (like SHL reg,CL) that takes fixed registers. */
#define IEM_MC_NATIVE_SET_AMD64_HOST_REG_FOR_LOCAL(a_VarNm, a_idxHostReg) ((void)0)


#define IEM_MC_CALL_VOID_AIMPL_0(a_pfn)                   (a_pfn)()
#define IEM_MC_CALL_VOID_AIMPL_1(a_pfn, a0)               (a_pfn)((a0))
#define IEM_MC_CALL_VOID_AIMPL_2(a_pfn, a0, a1)           (a_pfn)((a0), (a1))
#define IEM_MC_CALL_VOID_AIMPL_3(a_pfn, a0, a1, a2)       (a_pfn)((a0), (a1), (a2))
#define IEM_MC_CALL_VOID_AIMPL_4(a_pfn, a0, a1, a2, a3)   (a_pfn)((a0), (a1), (a2), (a3))
#define IEM_MC_CALL_AIMPL_3(a_rcType, a_rc, a_pfn, a0, a1, a2)      a_rcType const a_rc = (a_pfn)((a0), (a1), (a2))
#define IEM_MC_CALL_AIMPL_4(a_rcType, a_rc, a_pfn, a0, a1, a2, a3)  a_rcType const a_rc = (a_pfn)((a0), (a1), (a2), (a3))


/** @def IEM_MC_CALL_CIMPL_HLP_RET
 * Helper macro for check that all important IEM_CIMPL_F_XXX bits are set.
 */
#ifdef VBOX_STRICT
# define IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, a_CallExpr) \
    do { \
        uint8_t      const cbInstr     = IEM_GET_INSTR_LEN(pVCpu); /* may be flushed */ \
        uint16_t     const uCsBefore   = pVCpu->cpum.GstCtx.cs.Sel; \
        uint64_t     const uRipBefore  = pVCpu->cpum.GstCtx.rip; \
        uint32_t     const fEflBefore  = pVCpu->cpum.GstCtx.eflags.u; \
        uint32_t     const fExecBefore = pVCpu->iem.s.fExec; \
        VBOXSTRICTRC const rcStrictHlp = a_CallExpr; \
        if (rcStrictHlp == VINF_SUCCESS) \
        { \
            uint64_t const fRipMask = (pVCpu->iem.s.fExec & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_64BIT ? UINT64_MAX : UINT32_MAX; \
            AssertMsg(   ((a_fFlags) & IEM_CIMPL_F_BRANCH_ANY) \
                      || (   ((uRipBefore + cbInstr) & fRipMask) == pVCpu->cpum.GstCtx.rip \
                          && uCsBefore  == pVCpu->cpum.GstCtx.cs.Sel) \
                      || (   ((a_fFlags) & IEM_CIMPL_F_REP) \
                          && uRipBefore == pVCpu->cpum.GstCtx.rip \
                          && uCsBefore  == pVCpu->cpum.GstCtx.cs.Sel), \
                      ("CS:RIP=%04x:%08RX64 + %x -> %04x:%08RX64, expected %04x:%08RX64\n", uCsBefore, uRipBefore, cbInstr, \
                       pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, uCsBefore, (uRipBefore + cbInstr) & fRipMask)); \
            if ((a_fFlags) & IEM_CIMPL_F_RFLAGS) \
            { /* No need to check fEflBefore */ Assert(!((a_fFlags) & IEM_CIMPL_F_STATUS_FLAGS)); } \
            else if ((a_fFlags) & IEM_CIMPL_F_STATUS_FLAGS) \
                AssertMsg(   (pVCpu->cpum.GstCtx.eflags.u & ~(X86_EFL_STATUS_BITS | X86_EFL_RF)) \
                          == (fEflBefore                  & ~(X86_EFL_STATUS_BITS | X86_EFL_RF)), \
                          ("EFL=%#RX32 -> %#RX32\n", fEflBefore, pVCpu->cpum.GstCtx.eflags.u)); \
            else \
                AssertMsg(   (pVCpu->cpum.GstCtx.eflags.u & ~(X86_EFL_RF)) \
                          == (fEflBefore                  & ~(X86_EFL_RF)), \
                          ("EFL=%#RX32 -> %#RX32\n", fEflBefore, pVCpu->cpum.GstCtx.eflags.u)); \
            if (!((a_fFlags) & IEM_CIMPL_F_MODE)) \
            { \
                uint32_t fExecRecalc = iemCalcExecFlags(pVCpu) | (pVCpu->iem.s.fExec & IEM_F_USER_OPTS); \
                AssertMsg(   fExecBefore == fExecRecalc \
                             /* in case ES, DS or SS was external initially (happens alot with HM): */ \
                          || (   fExecBefore == (fExecRecalc & ~IEM_F_MODE_X86_FLAT_OR_PRE_386_MASK) \
                              && (fExecRecalc & IEM_F_MODE_CPUMODE_MASK) == IEMMODE_32BIT), \
                          ("fExec=%#x -> %#x (diff %#x)\n", fExecBefore, fExecRecalc, fExecBefore ^ fExecRecalc)); \
            } \
        } \
        return rcStrictHlp; \
    } while (0)
#else
# define IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, a_CallExpr) return a_CallExpr
#endif

/**
 * Defers the rest of the instruction emulation to a C implementation routine
 * and returns, only taking the standard parameters.
 *
 * @param   a_fFlags        IEM_CIMPL_F_XXX.
 * @param   a_fGstShwFlush  Guest shadow register copies needing to be flushed
 *                          in the native recompiler.
 * @param   a_pfnCImpl      The pointer to the C routine.
 * @sa      IEM_DECL_IMPL_C_TYPE_0 and IEM_CIMPL_DEF_0.
 */
#define IEM_MC_CALL_CIMPL_0(a_fFlags, a_fGstShwFlush, a_pfnCImpl) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, IEM_GET_INSTR_LEN(pVCpu)))

/**
 * Defers the rest of instruction emulation to a C implementation routine and
 * returns, taking one argument in addition to the standard ones.
 *
 * @param   a_fFlags        IEM_CIMPL_F_XXX.
 * @param   a_fGstShwFlush  Guest shadow register copies needing to be flushed
 *                          in the native recompiler.
 * @param   a_pfnCImpl      The pointer to the C routine.
 * @param   a0              The argument.
 */
#define IEM_MC_CALL_CIMPL_1(a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, IEM_GET_INSTR_LEN(pVCpu), a0))

/**
 * Defers the rest of the instruction emulation to a C implementation routine
 * and returns, taking two arguments in addition to the standard ones.
 *
 * @param   a_fFlags        IEM_CIMPL_F_XXX.
 * @param   a_fGstShwFlush  Guest shadow register copies needing to be flushed
 *                          in the native recompiler.
 * @param   a_pfnCImpl      The pointer to the C routine.
 * @param   a0              The first extra argument.
 * @param   a1              The second extra argument.
 */
#define IEM_MC_CALL_CIMPL_2(a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, IEM_GET_INSTR_LEN(pVCpu), a0, a1))

/**
 * Defers the rest of the instruction emulation to a C implementation routine
 * and returns, taking three arguments in addition to the standard ones.
 *
 * @param   a_fFlags        IEM_CIMPL_F_XXX.
 * @param   a_fGstShwFlush  Guest shadow register copies needing to be flushed
 *                          in the native recompiler.
 * @param   a_pfnCImpl      The pointer to the C routine.
 * @param   a0              The first extra argument.
 * @param   a1              The second extra argument.
 * @param   a2              The third extra argument.
 */
#define IEM_MC_CALL_CIMPL_3(a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, IEM_GET_INSTR_LEN(pVCpu), a0, a1, a2))

/**
 * Defers the rest of the instruction emulation to a C implementation routine
 * and returns, taking four arguments in addition to the standard ones.
 *
 * @param   a_fFlags        IEM_CIMPL_F_XXX.
 * @param   a_fGstShwFlush  Guest shadow register copies needing to be flushed
 *                          in the native recompiler.
 * @param   a_pfnCImpl      The pointer to the C routine.
 * @param   a0              The first extra argument.
 * @param   a1              The second extra argument.
 * @param   a2              The third extra argument.
 * @param   a3              The fourth extra argument.
 */
#define IEM_MC_CALL_CIMPL_4(a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2, a3) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, IEM_GET_INSTR_LEN(pVCpu), a0, a1, a2, a3))

/**
 * Defers the rest of the instruction emulation to a C implementation routine
 * and returns, taking five arguments in addition to the standard ones.
 *
 * @param   a_fFlags        IEM_CIMPL_F_XXX.
 * @param   a_fGstShwFlush  Guest shadow register copies needing to be flushed
 *                          in the native recompiler.
 * @param   a_pfnCImpl      The pointer to the C routine.
 * @param   a0              The first extra argument.
 * @param   a1              The second extra argument.
 * @param   a2              The third extra argument.
 * @param   a3              The fourth extra argument.
 * @param   a4              The fifth extra argument.
 */
#define IEM_MC_CALL_CIMPL_5(a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2, a3, a4) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, IEM_GET_INSTR_LEN(pVCpu), a0, a1, a2, a3, a4))

/**
 * Defers the entire instruction emulation to a C implementation routine and
 * returns, only taking the standard parameters.
 *
 * This shall be used without any IEM_MC_BEGIN or IEM_END macro surrounding it.
 *
 * @param   a_fFlags        IEM_CIMPL_F_XXX.
 * @param   a_fGstShwFlush  Guest shadow register copies needing to be flushed
 *                          in the native recompiler.
 * @param   a_pfnCImpl      The pointer to the C routine.
 * @sa      IEM_DECL_IMPL_C_TYPE_0 and IEM_CIMPL_DEF_0.
 */
#define IEM_MC_DEFER_TO_CIMPL_0_RET(a_fFlags, a_fGstShwFlush, a_pfnCImpl) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, IEM_GET_INSTR_LEN(pVCpu)))

/**
 * Defers the entire instruction emulation to a C implementation routine and
 * returns, taking one argument in addition to the standard ones.
 *
 * This shall be used without any IEM_MC_BEGIN or IEM_END macro surrounding it.
 *
 * @param   a_fFlags        IEM_CIMPL_F_XXX.
 * @param   a_fGstShwFlush  Guest shadow register copies needing to be flushed
 *                          in the native recompiler.
 * @param   a_pfnCImpl      The pointer to the C routine.
 * @param   a0              The argument.
 */
#define IEM_MC_DEFER_TO_CIMPL_1_RET(a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, IEM_GET_INSTR_LEN(pVCpu), a0))

/**
 * Defers the entire instruction emulation to a C implementation routine and
 * returns, taking two arguments in addition to the standard ones.
 *
 * This shall be used without any IEM_MC_BEGIN or IEM_END macro surrounding it.
 *
 * @param   a_fFlags        IEM_CIMPL_F_XXX.
 * @param   a_fGstShwFlush  Guest shadow register copies needing to be flushed
 *                          in the native recompiler.
 * @param   a_pfnCImpl      The pointer to the C routine.
 * @param   a0              The first extra argument.
 * @param   a1              The second extra argument.
 */
#define IEM_MC_DEFER_TO_CIMPL_2_RET(a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, IEM_GET_INSTR_LEN(pVCpu), a0, a1))

/**
 * Defers the entire instruction emulation to a C implementation routine and
 * returns, taking three arguments in addition to the standard ones.
 *
 * This shall be used without any IEM_MC_BEGIN or IEM_END macro surrounding it.
 *
 * @param   a_fFlags        IEM_CIMPL_F_XXX.
 * @param   a_fGstShwFlush  Guest shadow register copies needing to be flushed
 *                          in the native recompiler.
 * @param   a_pfnCImpl      The pointer to the C routine.
 * @param   a0              The first extra argument.
 * @param   a1              The second extra argument.
 * @param   a2              The third extra argument.
 */
#define IEM_MC_DEFER_TO_CIMPL_3_RET(a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, IEM_GET_INSTR_LEN(pVCpu), a0, a1, a2))


/**
 * Calls a FPU assembly implementation taking one visible argument.
 *
 * @param   a_pfnAImpl      Pointer to the assembly FPU routine.
 * @param   a0              The first extra argument.
 */
#define IEM_MC_CALL_FPU_AIMPL_1(a_pfnAImpl, a0) \
    do { \
        a_pfnAImpl(&pVCpu->cpum.GstCtx.XState.x87, (a0)); \
    } while (0)

/**
 * Calls a FPU assembly implementation taking two visible arguments.
 *
 * @param   a_pfnAImpl      Pointer to the assembly FPU routine.
 * @param   a0              The first extra argument.
 * @param   a1              The second extra argument.
 */
#define IEM_MC_CALL_FPU_AIMPL_2(a_pfnAImpl, a0, a1) \
    do { \
        a_pfnAImpl(&pVCpu->cpum.GstCtx.XState.x87, (a0), (a1)); \
    } while (0)

/**
 * Calls a FPU assembly implementation taking three visible arguments.
 *
 * @param   a_pfnAImpl      Pointer to the assembly FPU routine.
 * @param   a0              The first extra argument.
 * @param   a1              The second extra argument.
 * @param   a2              The third extra argument.
 */
#define IEM_MC_CALL_FPU_AIMPL_3(a_pfnAImpl, a0, a1, a2) \
    do { \
        a_pfnAImpl(&pVCpu->cpum.GstCtx.XState.x87, (a0), (a1), (a2)); \
    } while (0)

#define IEM_MC_SET_FPU_RESULT(a_FpuData, a_FSW, a_pr80Value) \
    do { \
        (a_FpuData).FSW       = (a_FSW); \
        (a_FpuData).r80Result = *(a_pr80Value); \
    } while (0)

/** Pushes FPU result onto the stack. */
#define IEM_MC_PUSH_FPU_RESULT(a_FpuData, a_uFpuOpcode) \
    iemFpuPushResult(pVCpu, &a_FpuData, a_uFpuOpcode)
/** Pushes FPU result onto the stack and sets the FPUDP. */
#define IEM_MC_PUSH_FPU_RESULT_MEM_OP(a_FpuData, a_iEffSeg, a_GCPtrEff, a_uFpuOpcode) \
    iemFpuPushResultWithMemOp(pVCpu, &a_FpuData, a_iEffSeg, a_GCPtrEff, a_uFpuOpcode)

/** Replaces ST0 with value one and pushes value 2 onto the FPU stack. */
#define IEM_MC_PUSH_FPU_RESULT_TWO(a_FpuDataTwo, a_uFpuOpcode) \
    iemFpuPushResultTwo(pVCpu, &a_FpuDataTwo, a_uFpuOpcode)

/** Stores FPU result in a stack register. */
#define IEM_MC_STORE_FPU_RESULT(a_FpuData, a_iStReg, a_uFpuOpcode) \
    iemFpuStoreResult(pVCpu, &a_FpuData, a_iStReg, a_uFpuOpcode)
/** Stores FPU result in a stack register and pops the stack. */
#define IEM_MC_STORE_FPU_RESULT_THEN_POP(a_FpuData, a_iStReg, a_uFpuOpcode) \
    iemFpuStoreResultThenPop(pVCpu, &a_FpuData, a_iStReg, a_uFpuOpcode)
/** Stores FPU result in a stack register and sets the FPUDP. */
#define IEM_MC_STORE_FPU_RESULT_MEM_OP(a_FpuData, a_iStReg, a_iEffSeg, a_GCPtrEff, a_uFpuOpcode) \
    iemFpuStoreResultWithMemOp(pVCpu, &a_FpuData, a_iStReg, a_iEffSeg, a_GCPtrEff, a_uFpuOpcode)
/** Stores FPU result in a stack register, sets the FPUDP, and pops the
 *  stack. */
#define IEM_MC_STORE_FPU_RESULT_WITH_MEM_OP_THEN_POP(a_FpuData, a_iStReg, a_iEffSeg, a_GCPtrEff, a_uFpuOpcode) \
    iemFpuStoreResultWithMemOpThenPop(pVCpu, &a_FpuData, a_iStReg, a_iEffSeg, a_GCPtrEff, a_uFpuOpcode)

/** Only update the FOP, FPUIP, and FPUCS. (For FNOP.) */
#define IEM_MC_UPDATE_FPU_OPCODE_IP(a_uFpuOpcode) \
    iemFpuUpdateOpcodeAndIp(pVCpu, a_uFpuOpcode)
/** Free a stack register (for FFREE and FFREEP). */
#define IEM_MC_FPU_STACK_FREE(a_iStReg) \
    iemFpuStackFree(pVCpu, a_iStReg)
/** Increment the FPU stack pointer. */
#define IEM_MC_FPU_STACK_INC_TOP() \
    iemFpuStackIncTop(pVCpu)
/** Decrement the FPU stack pointer. */
#define IEM_MC_FPU_STACK_DEC_TOP() \
    iemFpuStackDecTop(pVCpu)

/** Updates the FSW, FOP, FPUIP, and FPUCS. */
#define IEM_MC_UPDATE_FSW(a_u16FSW, a_uFpuOpcode) \
    iemFpuUpdateFSW(pVCpu, a_u16FSW, a_uFpuOpcode)
/** Updates the FSW with a constant value as well as FOP, FPUIP, and FPUCS. */
#define IEM_MC_UPDATE_FSW_CONST(a_u16FSW, a_uFpuOpcode) \
    iemFpuUpdateFSW(pVCpu, a_u16FSW, a_uFpuOpcode)
/** Updates the FSW, FOP, FPUIP, FPUCS, FPUDP, and FPUDS. */
#define IEM_MC_UPDATE_FSW_WITH_MEM_OP(a_u16FSW, a_iEffSeg, a_GCPtrEff, a_uFpuOpcode) \
    iemFpuUpdateFSWWithMemOp(pVCpu, a_u16FSW, a_iEffSeg, a_GCPtrEff, a_uFpuOpcode)
/** Updates the FSW, FOP, FPUIP, and FPUCS, and then pops the stack. */
#define IEM_MC_UPDATE_FSW_THEN_POP(a_u16FSW, a_uFpuOpcode) \
    iemFpuUpdateFSWThenPop(pVCpu, a_u16FSW, a_uFpuOpcode)
/** Updates the FSW, FOP, FPUIP, FPUCS, FPUDP and FPUDS, and then pops the
 *  stack. */
#define IEM_MC_UPDATE_FSW_WITH_MEM_OP_THEN_POP(a_u16FSW, a_iEffSeg, a_GCPtrEff, a_uFpuOpcode) \
    iemFpuUpdateFSWWithMemOpThenPop(pVCpu, a_u16FSW, a_iEffSeg, a_GCPtrEff, a_uFpuOpcode)
/** Updates the FSW, FOP, FPUIP, and FPUCS, and then pops the stack twice. */
#define IEM_MC_UPDATE_FSW_THEN_POP_POP(a_u16FSW, a_uFpuOpcode) \
    iemFpuUpdateFSWThenPopPop(pVCpu, a_u16FSW, a_uFpuOpcode)

/** Raises a FPU stack underflow exception.  Sets FPUIP, FPUCS and FOP. */
#define IEM_MC_FPU_STACK_UNDERFLOW(a_iStDst, a_uFpuOpcode) \
    iemFpuStackUnderflow(pVCpu, a_iStDst, a_uFpuOpcode)
/** Raises a FPU stack underflow exception.  Sets FPUIP, FPUCS and FOP. Pops
 *  stack. */
#define IEM_MC_FPU_STACK_UNDERFLOW_THEN_POP(a_iStDst, a_uFpuOpcode) \
    iemFpuStackUnderflowThenPop(pVCpu, a_iStDst, a_uFpuOpcode)
/** Raises a FPU stack underflow exception.  Sets FPUIP, FPUCS, FOP, FPUDP and
 *  FPUDS. */
#define IEM_MC_FPU_STACK_UNDERFLOW_MEM_OP(a_iStDst, a_iEffSeg, a_GCPtrEff, a_uFpuOpcode) \
    iemFpuStackUnderflowWithMemOp(pVCpu, a_iStDst, a_iEffSeg, a_GCPtrEff, a_uFpuOpcode)
/** Raises a FPU stack underflow exception.  Sets FPUIP, FPUCS, FOP, FPUDP and
 *  FPUDS. Pops stack. */
#define IEM_MC_FPU_STACK_UNDERFLOW_MEM_OP_THEN_POP(a_iStDst, a_iEffSeg, a_GCPtrEff, a_uFpuOpcode) \
    iemFpuStackUnderflowWithMemOpThenPop(pVCpu, a_iStDst, a_iEffSeg, a_GCPtrEff, a_uFpuOpcode)
/** Raises a FPU stack underflow exception.  Sets FPUIP, FPUCS and FOP. Pops
 *  stack twice. */
#define IEM_MC_FPU_STACK_UNDERFLOW_THEN_POP_POP(a_uFpuOpcode) \
    iemFpuStackUnderflowThenPopPop(pVCpu, a_uFpuOpcode)
/** Raises a FPU stack underflow exception for an instruction pushing a result
 *  value onto the stack. Sets FPUIP, FPUCS and FOP. */
#define IEM_MC_FPU_STACK_PUSH_UNDERFLOW(a_uFpuOpcode) \
    iemFpuStackPushUnderflow(pVCpu, a_uFpuOpcode)
/** Raises a FPU stack underflow exception for an instruction pushing a result
 *  value onto the stack and replacing ST0. Sets FPUIP, FPUCS and FOP. */
#define IEM_MC_FPU_STACK_PUSH_UNDERFLOW_TWO(a_uFpuOpcode) \
    iemFpuStackPushUnderflowTwo(pVCpu, a_uFpuOpcode)

/** Raises a FPU stack overflow exception as part of a push attempt.  Sets
 *  FPUIP, FPUCS and FOP. */
#define IEM_MC_FPU_STACK_PUSH_OVERFLOW(a_uFpuOpcode) \
    iemFpuStackPushOverflow(pVCpu, a_uFpuOpcode)
/** Raises a FPU stack overflow exception as part of a push attempt.  Sets
 *  FPUIP, FPUCS, FOP, FPUDP and FPUDS. */
#define IEM_MC_FPU_STACK_PUSH_OVERFLOW_MEM_OP(a_iEffSeg, a_GCPtrEff, a_uFpuOpcode) \
    iemFpuStackPushOverflowWithMemOp(pVCpu, a_iEffSeg, a_GCPtrEff, a_uFpuOpcode)
/** Prepares for using the FPU state.
 * Ensures that we can use the host FPU in the current context (RC+R0.
 * Ensures the guest FPU state in the CPUMCTX is up to date. */
#define IEM_MC_PREPARE_FPU_USAGE()              iemFpuPrepareUsage(pVCpu)
/** Actualizes the guest FPU state so it can be accessed read-only fashion. */
#define IEM_MC_ACTUALIZE_FPU_STATE_FOR_READ()   iemFpuActualizeStateForRead(pVCpu)
/** Actualizes the guest FPU state so it can be accessed and modified. */
#define IEM_MC_ACTUALIZE_FPU_STATE_FOR_CHANGE() iemFpuActualizeStateForChange(pVCpu)

/** Prepares for using the SSE state.
 * Ensures that we can use the host SSE/FPU in the current context (RC+R0.
 * Ensures the guest SSE state in the CPUMCTX is up to date. */
#define IEM_MC_PREPARE_SSE_USAGE()              iemFpuPrepareUsageSse(pVCpu)
/** Actualizes the guest XMM0..15 and MXCSR register state for read-only access. */
#define IEM_MC_ACTUALIZE_SSE_STATE_FOR_READ()   iemFpuActualizeSseStateForRead(pVCpu)
/** Actualizes the guest XMM0..15 and MXCSR register state for read-write access. */
#define IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE() iemFpuActualizeSseStateForChange(pVCpu)

/** Prepares for using the AVX state.
 * Ensures that we can use the host AVX/FPU in the current context (RC+R0.
 * Ensures the guest AVX state in the CPUMCTX is up to date.
 * @note This will include the AVX512 state too when support for it is added
 *       due to the zero extending feature of VEX instruction. */
#define IEM_MC_PREPARE_AVX_USAGE()              iemFpuPrepareUsageAvx(pVCpu)
/** Actualizes the guest XMM0..15 and MXCSR register state for read-only access. */
#define IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ()   iemFpuActualizeAvxStateForRead(pVCpu)
/** Actualizes the guest YMM0..15 and MXCSR register state for read-write access. */
#define IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE() iemFpuActualizeAvxStateForChange(pVCpu)

/**
 * Calls a MMX assembly implementation taking two visible arguments.
 *
 * @param   a_pfnAImpl      Pointer to the assembly MMX routine.
 * @param   a0              The first extra argument.
 * @param   a1              The second extra argument.
 */
#define IEM_MC_CALL_MMX_AIMPL_2(a_pfnAImpl, a0, a1) \
    do { \
        IEM_MC_PREPARE_FPU_USAGE(); \
        a_pfnAImpl(&pVCpu->cpum.GstCtx.XState.x87, (a0), (a1)); \
    } while (0)

/**
 * Calls a MMX assembly implementation taking three visible arguments.
 *
 * @param   a_pfnAImpl      Pointer to the assembly MMX routine.
 * @param   a0              The first extra argument.
 * @param   a1              The second extra argument.
 * @param   a2              The third extra argument.
 */
#define IEM_MC_CALL_MMX_AIMPL_3(a_pfnAImpl, a0, a1, a2) \
    do { \
        IEM_MC_PREPARE_FPU_USAGE(); \
        a_pfnAImpl(&pVCpu->cpum.GstCtx.XState.x87, (a0), (a1), (a2)); \
    } while (0)


/**
 * Calls a SSE assembly implementation taking two visible arguments.
 *
 * @param   a_pfnAImpl      Pointer to the assembly SSE routine.
 * @param   a0              The first extra argument.
 * @param   a1              The second extra argument.
 *
 * @note This throws an \#XF/\#UD exception if the helper indicates an exception
 *       which is unmasked in the guest's MXCSR.
 */
#define IEM_MC_CALL_SSE_AIMPL_2(a_pfnAImpl, a0, a1) \
    do { \
        IEM_MC_PREPARE_SSE_USAGE(); \
        const uint32_t fMxcsrOld = pVCpu->cpum.GstCtx.XState.x87.MXCSR; \
        const uint32_t fMxcsrNew = a_pfnAImpl(fMxcsrOld & ~X86_MXCSR_XCPT_FLAGS, \
                                              (a0), (a1)); \
        pVCpu->cpum.GstCtx.XState.x87.MXCSR |= fMxcsrNew; \
        if (RT_LIKELY((  ~((fMxcsrOld & X86_MXCSR_XCPT_MASK) >> X86_MXCSR_XCPT_MASK_SHIFT) \
                       & (fMxcsrNew & X86_MXCSR_XCPT_FLAGS)) == 0)) \
        { /* probable */ } \
        else \
        { \
            if (pVCpu->cpum.GstCtx.cr4 & X86_CR4_OSXMMEEXCPT) \
                return iemRaiseSimdFpException(pVCpu); \
            return iemRaiseUndefinedOpcode(pVCpu); \
        } \
    } while (0)

/**
 * Calls a SSE assembly implementation taking three visible arguments.
 *
 * @param   a_pfnAImpl      Pointer to the assembly SSE routine.
 * @param   a0              The first extra argument.
 * @param   a1              The second extra argument.
 * @param   a2              The third extra argument.
 *
 * @note This throws an \#XF/\#UD exception if the helper indicates an exception
 *       which is unmasked in the guest's MXCSR.
 */
#define IEM_MC_CALL_SSE_AIMPL_3(a_pfnAImpl, a0, a1, a2) \
    do { \
        IEM_MC_PREPARE_SSE_USAGE(); \
        const uint32_t fMxcsrOld = pVCpu->cpum.GstCtx.XState.x87.MXCSR; \
        const uint32_t fMxcsrNew = a_pfnAImpl(fMxcsrOld & ~X86_MXCSR_XCPT_FLAGS, \
                                              (a0), (a1), (a2)); \
        pVCpu->cpum.GstCtx.XState.x87.MXCSR |= fMxcsrNew; \
        if (RT_LIKELY((  ~((fMxcsrOld & X86_MXCSR_XCPT_MASK) >> X86_MXCSR_XCPT_MASK_SHIFT) \
                       & (fMxcsrNew & X86_MXCSR_XCPT_FLAGS)) == 0)) \
        { /* probable */ } \
        else \
        { \
            if (pVCpu->cpum.GstCtx.cr4 & X86_CR4_OSXMMEEXCPT) \
                return iemRaiseSimdFpException(pVCpu); \
            return iemRaiseUndefinedOpcode(pVCpu); \
        } \
    } while (0)


/**
 * Calls a AVX assembly implementation taking two visible arguments.
 *
 * There is one implicit zero'th argument, a pointer to the extended state.
 *
 * @param   a_pfnAImpl      Pointer to the assembly AVX routine.
 * @param   a0              The first extra argument.
 * @param   a1              The second extra argument.
 *
 * @note This throws an \#XF/\#UD exception if the helper indicates an exception
 *       which is unmasked in the guest's MXCSR.
 */
#define IEM_MC_CALL_AVX_AIMPL_2(a_pfnAImpl, a0, a1) \
    do { \
        IEM_MC_PREPARE_AVX_USAGE(); \
        const uint32_t fMxcsrOld = pVCpu->cpum.GstCtx.XState.x87.MXCSR; \
        const uint32_t fMxcsrNew = a_pfnAImpl(fMxcsrOld & ~X86_MXCSR_XCPT_FLAGS, \
                                              (a0), (a1)); \
        pVCpu->cpum.GstCtx.XState.x87.MXCSR |= fMxcsrNew; \
        if (RT_LIKELY((  ~((fMxcsrOld & X86_MXCSR_XCPT_MASK) >> X86_MXCSR_XCPT_MASK_SHIFT) \
                       & (fMxcsrNew & X86_MXCSR_XCPT_FLAGS)) == 0)) \
        { /* probable */ } \
        else \
        { \
            if (pVCpu->cpum.GstCtx.cr4 & X86_CR4_OSXMMEEXCPT) \
                return iemRaiseSimdFpException(pVCpu); \
            return iemRaiseUndefinedOpcode(pVCpu); \
        } \
    } while (0)

/**
 * Calls a AVX assembly implementation taking three visible arguments.
 *
 * There is one implicit zero'th argument, a pointer to the extended state.
 *
 * @param   a_pfnAImpl      Pointer to the assembly AVX routine.
 * @param   a0              The first extra argument.
 * @param   a1              The second extra argument.
 * @param   a2              The third extra argument.
 *
 * @note This throws an \#XF/\#UD exception if the helper indicates an exception
 *       which is unmasked in the guest's MXCSR.
 */
#define IEM_MC_CALL_AVX_AIMPL_3(a_pfnAImpl, a0, a1, a2) \
    do { \
        IEM_MC_PREPARE_AVX_USAGE(); \
        const uint32_t fMxcsrOld = pVCpu->cpum.GstCtx.XState.x87.MXCSR; \
        const uint32_t fMxcsrNew = a_pfnAImpl(fMxcsrOld & ~X86_MXCSR_XCPT_FLAGS, \
                                              (a0), (a1), (a2)); \
        pVCpu->cpum.GstCtx.XState.x87.MXCSR |= fMxcsrNew; \
        if (RT_LIKELY((  ~((fMxcsrOld & X86_MXCSR_XCPT_MASK) >> X86_MXCSR_XCPT_MASK_SHIFT) \
                       & (fMxcsrNew & X86_MXCSR_XCPT_FLAGS)) == 0)) \
        { /* probable */ } \
        else \
        { \
            if (pVCpu->cpum.GstCtx.cr4 & X86_CR4_OSXMMEEXCPT) \
                return iemRaiseSimdFpException(pVCpu); \
            return iemRaiseUndefinedOpcode(pVCpu); \
        } \
    } while (0)

/** @note Not for IOPL or IF testing. */
#define IEM_MC_IF_EFL_BIT_SET(a_fBit)                   if (pVCpu->cpum.GstCtx.eflags.u & (a_fBit)) {
/** @note Not for IOPL or IF testing. */
#define IEM_MC_IF_EFL_BIT_NOT_SET(a_fBit)               if (!(pVCpu->cpum.GstCtx.eflags.u & (a_fBit))) {
/** @note Not for IOPL or IF testing. */
#define IEM_MC_IF_EFL_ANY_BITS_SET(a_fBits)             if (pVCpu->cpum.GstCtx.eflags.u & (a_fBits)) {
/** @note Not for IOPL or IF testing. */
#define IEM_MC_IF_EFL_NO_BITS_SET(a_fBits)              if (!(pVCpu->cpum.GstCtx.eflags.u & (a_fBits))) {
/** @note Not for IOPL or IF testing. */
#define IEM_MC_IF_EFL_BITS_NE(a_fBit1, a_fBit2)         \
    if (   !!(pVCpu->cpum.GstCtx.eflags.u & (a_fBit1)) \
        != !!(pVCpu->cpum.GstCtx.eflags.u & (a_fBit2)) ) {
/** @note Not for IOPL or IF testing. */
#define IEM_MC_IF_EFL_BITS_EQ(a_fBit1, a_fBit2)         \
    if (   !!(pVCpu->cpum.GstCtx.eflags.u & (a_fBit1)) \
        == !!(pVCpu->cpum.GstCtx.eflags.u & (a_fBit2)) ) {
/** @note Not for IOPL or IF testing. */
#define IEM_MC_IF_EFL_BIT_SET_OR_BITS_NE(a_fBit, a_fBit1, a_fBit2) \
    if (   (pVCpu->cpum.GstCtx.eflags.u & (a_fBit)) \
        ||    !!(pVCpu->cpum.GstCtx.eflags.u & (a_fBit1)) \
           != !!(pVCpu->cpum.GstCtx.eflags.u & (a_fBit2)) ) {
/** @note Not for IOPL or IF testing. */
#define IEM_MC_IF_EFL_BIT_NOT_SET_AND_BITS_EQ(a_fBit, a_fBit1, a_fBit2) \
    if (   !(pVCpu->cpum.GstCtx.eflags.u & (a_fBit)) \
        &&    !!(pVCpu->cpum.GstCtx.eflags.u & (a_fBit1)) \
           == !!(pVCpu->cpum.GstCtx.eflags.u & (a_fBit2)) ) {
#define IEM_MC_IF_CX_IS_NZ()                            if (pVCpu->cpum.GstCtx.cx  != 0) {
#define IEM_MC_IF_ECX_IS_NZ()                           if (pVCpu->cpum.GstCtx.ecx != 0) {
#define IEM_MC_IF_RCX_IS_NZ()                           if (pVCpu->cpum.GstCtx.rcx != 0) {
#define IEM_MC_IF_CX_IS_NOT_ONE()                       if (pVCpu->cpum.GstCtx.cx  != 1) {
#define IEM_MC_IF_ECX_IS_NOT_ONE()                      if (pVCpu->cpum.GstCtx.ecx != 1) {
#define IEM_MC_IF_RCX_IS_NOT_ONE()                      if (pVCpu->cpum.GstCtx.rcx != 1) {
/** @note Not for IOPL or IF testing. */
#define IEM_MC_IF_CX_IS_NOT_ONE_AND_EFL_BIT_SET(a_fBit) \
        if (   pVCpu->cpum.GstCtx.cx != 1 \
            && (pVCpu->cpum.GstCtx.eflags.u & a_fBit)) {
/** @note Not for IOPL or IF testing. */
#define IEM_MC_IF_ECX_IS_NOT_ONE_AND_EFL_BIT_SET(a_fBit) \
        if (   pVCpu->cpum.GstCtx.ecx != 1 \
            && (pVCpu->cpum.GstCtx.eflags.u & a_fBit)) {
/** @note Not for IOPL or IF testing. */
#define IEM_MC_IF_RCX_IS_NOT_ONE_AND_EFL_BIT_SET(a_fBit) \
        if (   pVCpu->cpum.GstCtx.rcx != 1 \
            && (pVCpu->cpum.GstCtx.eflags.u & a_fBit)) {
/** @note Not for IOPL or IF testing. */
#define IEM_MC_IF_CX_IS_NOT_ONE_AND_EFL_BIT_NOT_SET(a_fBit) \
        if (   pVCpu->cpum.GstCtx.cx != 1 \
            && !(pVCpu->cpum.GstCtx.eflags.u & a_fBit)) {
/** @note Not for IOPL or IF testing. */
#define IEM_MC_IF_ECX_IS_NOT_ONE_AND_EFL_BIT_NOT_SET(a_fBit) \
        if (   pVCpu->cpum.GstCtx.ecx != 1 \
            && !(pVCpu->cpum.GstCtx.eflags.u & a_fBit)) {
/** @note Not for IOPL or IF testing. */
#define IEM_MC_IF_RCX_IS_NOT_ONE_AND_EFL_BIT_NOT_SET(a_fBit) \
        if (   pVCpu->cpum.GstCtx.rcx != 1 \
            && !(pVCpu->cpum.GstCtx.eflags.u & a_fBit)) {
#define IEM_MC_IF_LOCAL_IS_Z(a_Local)                   if ((a_Local) == 0) {
#define IEM_MC_IF_GREG_BIT_SET(a_iGReg, a_iBitNo)       if (iemGRegFetchU64(pVCpu, (a_iGReg)) & RT_BIT_64(a_iBitNo)) {

#define IEM_MC_REF_FPUREG(a_pr80Dst, a_iSt) \
    do { (a_pr80Dst) = &pVCpu->cpum.GstCtx.XState.x87.aRegs[a_iSt].r80; } while (0)
#define IEM_MC_IF_FPUREG_IS_EMPTY(a_iSt) \
    if (iemFpuStRegNotEmpty(pVCpu, (a_iSt)) != VINF_SUCCESS) {
#define IEM_MC_IF_FPUREG_NOT_EMPTY(a_iSt) \
    if (iemFpuStRegNotEmpty(pVCpu, (a_iSt)) == VINF_SUCCESS) {
#define IEM_MC_IF_FPUREG_NOT_EMPTY_REF_R80(a_pr80Dst, a_iSt) \
    if (iemFpuStRegNotEmptyRef(pVCpu, (a_iSt), &(a_pr80Dst)) == VINF_SUCCESS) {
#define IEM_MC_IF_TWO_FPUREGS_NOT_EMPTY_REF_R80(a_pr80Dst0, a_iSt0, a_pr80Dst1, a_iSt1) \
    if (iemFpu2StRegsNotEmptyRef(pVCpu, (a_iSt0), &(a_pr80Dst0), (a_iSt1), &(a_pr80Dst1)) == VINF_SUCCESS) {
#define IEM_MC_IF_TWO_FPUREGS_NOT_EMPTY_REF_R80_FIRST(a_pr80Dst0, a_iSt0, a_iSt1) \
    if (iemFpu2StRegsNotEmptyRefFirst(pVCpu, (a_iSt0), &(a_pr80Dst0), (a_iSt1)) == VINF_SUCCESS) {
#define IEM_MC_IF_FCW_IM() \
    if (pVCpu->cpum.GstCtx.XState.x87.FCW & X86_FCW_IM) {

#define IEM_MC_ELSE()                                   } else {
#define IEM_MC_ENDIF()                                  } do {} while (0)


/** Recompiler debugging: Flush guest register shadow copies. */
#define IEM_MC_HINT_FLUSH_GUEST_SHADOW(g_fGstShwFlush)  ((void)0)

/** Recompiler liveness info: input GPR */
#define IEM_MC_LIVENESS_GREG_INPUT(a_iGReg)             ((void)0)
/** Recompiler liveness info: clobbered GPR */
#define IEM_MC_LIVENESS_GREG_CLOBBER(a_iGReg)           ((void)0)
/** Recompiler liveness info: modified GPR register (i.e. input & output)  */
#define IEM_MC_LIVENESS_GREG_MODIFY(a_iGReg)            ((void)0)

/** Recompiler liveness info: input MM register */
#define IEM_MC_LIVENESS_MREG_INPUT(a_iMReg)             ((void)0)
/** Recompiler liveness info: clobbered MM register */
#define IEM_MC_LIVENESS_MREG_CLOBBER(a_iMReg)           ((void)0)
/** Recompiler liveness info: modified MM register (i.e. input & output)  */
#define IEM_MC_LIVENESS_MREG_MODIFY(a_iMReg)            ((void)0)

/** Recompiler liveness info: input SSE register */
#define IEM_MC_LIVENESS_XREG_INPUT(a_iXReg)             ((void)0)
/** Recompiler liveness info: clobbered SSE register */
#define IEM_MC_LIVENESS_XREG_CLOBBER(a_iXReg)           ((void)0)
/** Recompiler liveness info: modified SSE register (i.e. input & output)  */
#define IEM_MC_LIVENESS_XREG_MODIFY(a_iXReg)            ((void)0)

/** Recompiler liveness info: input MXCSR */
#define IEM_MC_LIVENESS_MXCSR_INPUT()                   ((void)0)
/** Recompiler liveness info: clobbered MXCSR */
#define IEM_MC_LIVENESS_MXCSR_CLOBBER()                 ((void)0)
/** Recompiler liveness info: modified MXCSR (i.e. input & output)  */
#define IEM_MC_LIVENESS_MXCSR_MODIFY()                  ((void)0)

/** @todo add more as needed. */

/** @}  */

#endif /* !VMM_INCLUDED_SRC_include_IEMMc_h */

