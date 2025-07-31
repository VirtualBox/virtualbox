/* $Id: IEMMc-x86.h 110154 2025-07-08 14:57:17Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - IEM_MC_XXX, x86 target.
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

#ifndef VMM_INCLUDED_SRC_VMMAll_target_x86_IEMMc_x86_h
#define VMM_INCLUDED_SRC_VMMAll_target_x86_IEMMc_x86_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/** @name   "Microcode" macros, x86 specifics and overrides.
 * @{
 */

/*
 * We override all the PC updating MCs:
 */

#undef  IEM_MC_ADVANCE_PC_AND_FINISH
#define IEM_MC_ADVANCE_PC_AND_FINISH()                  return iemRegAddToRipAndFinishingClearingRF(pVCpu, IEM_GET_INSTR_LEN(pVCpu))

#undef  IEM_MC_REL_JMP_S8_AND_FINISH
#define IEM_MC_REL_JMP_S8_AND_FINISH(a_i8) \
    return iemRegRipRelativeJumpS8AndFinishClearingRF(pVCpu, IEM_GET_INSTR_LEN(pVCpu), (a_i8), pVCpu->iem.s.enmEffOpSize)

/** @note X86: only usable in 16-bit op size mode.  */
#undef  IEM_MC_REL_JMP_S16_AND_FINISH
#define IEM_MC_REL_JMP_S16_AND_FINISH(a_i16) \
    return iemRegRipRelativeJumpS16AndFinishClearingRF(pVCpu, IEM_GET_INSTR_LEN(pVCpu), (a_i16))

#undef  IEM_MC_REL_JMP_S32_AND_FINISH
#define IEM_MC_REL_JMP_S32_AND_FINISH(a_i32) \
    return iemRegRipRelativeJumpS32AndFinishClearingRF(pVCpu, IEM_GET_INSTR_LEN(pVCpu), (a_i32), pVCpu->iem.s.enmEffOpSize)

#undef  IEM_MC_IND_JMP_U16_AND_FINISH
#define IEM_MC_IND_JMP_U16_AND_FINISH(a_u16NewIP) \
    return iemRegRipJumpU16AndFinishClearingRF((pVCpu), (a_u16NewIP), IEM_GET_INSTR_LEN(pVCpu))

#undef  IEM_MC_IND_JMP_U32_AND_FINISH
#define IEM_MC_IND_JMP_U32_AND_FINISH(a_u32NewIP) \
    return iemRegRipJumpU32AndFinishClearingRF((pVCpu), (a_u32NewIP), IEM_GET_INSTR_LEN(pVCpu))

#undef  IEM_MC_IND_JMP_U64_AND_FINISH
#define IEM_MC_IND_JMP_U64_AND_FINISH(a_u64NewIP) \
    return iemRegRipJumpU64AndFinishClearingRF((pVCpu), (a_u64NewIP), IEM_GET_INSTR_LEN(pVCpu))

/** @note only usable in 16-bit op size mode.  */
#undef  IEM_MC_REL_CALL_S16_AND_FINISH
#define IEM_MC_REL_CALL_S16_AND_FINISH(a_i16) \
    return iemRegRipRelativeCallS16AndFinishClearingRF(pVCpu, IEM_GET_INSTR_LEN(pVCpu), (a_i16))

#undef  IEM_MC_REL_CALL_S32_AND_FINISH
#define IEM_MC_REL_CALL_S32_AND_FINISH(a_i32) \
    return iemRegEip32RelativeCallS32AndFinishClearingRF(pVCpu, IEM_GET_INSTR_LEN(pVCpu), (a_i32))

#undef  IEM_MC_REL_CALL_S64_AND_FINISH
#define IEM_MC_REL_CALL_S64_AND_FINISH(a_i64) \
    return iemRegRip64RelativeCallS64AndFinishClearingRF(pVCpu, IEM_GET_INSTR_LEN(pVCpu), (a_i64))

#undef  IEM_MC_IND_CALL_U16_AND_FINISH
#define IEM_MC_IND_CALL_U16_AND_FINISH(a_u16NewIP) \
    return iemRegIp16IndirectCallU16AndFinishClearingRF((pVCpu), IEM_GET_INSTR_LEN(pVCpu), (a_u16NewIP))

#undef  IEM_MC_IND_CALL_U32_AND_FINISH
#define IEM_MC_IND_CALL_U32_AND_FINISH(a_u32NewIP) \
    return iemRegEip32IndirectCallU32AndFinishClearingRF((pVCpu), IEM_GET_INSTR_LEN(pVCpu), (a_u32NewIP))

#undef  IEM_MC_IND_CALL_U64_AND_FINISH
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
#define IEM_MC_STORE_GREG_U8_CONST                      IEM_MC_STORE_GREG_U8
#define IEM_MC_STORE_GREG_U16_CONST                     IEM_MC_STORE_GREG_U16

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

/** x86: preserve upper register bits.   */
#define IEM_MC_ADD_GREG_U16(a_iGReg, a_u16Value)        *iemGRegRefU16(pVCpu, (a_iGReg)) += (a_u16Value)

/** x86: preserve upper register bits.   */
#define IEM_MC_SUB_GREG_U16(a_iGReg, a_u8Const)         *iemGRegRefU16(pVCpu, (a_iGReg)) -= (a_u8Const)
#define IEM_MC_SUB_LOCAL_U16(a_u16Value, a_u16Const)    do { (a_u16Value) -= a_u16Const; } while (0)

/** x86: preserve upper register bits.   */
#define IEM_MC_AND_GREG_U8(a_iGReg, a_u8Value)          *iemGRegRefU8( pVCpu, (a_iGReg)) &= (a_u8Value)
/** x86: preserve upper register bits.   */
#define IEM_MC_AND_GREG_U16(a_iGReg, a_u16Value)        *iemGRegRefU16(pVCpu, (a_iGReg)) &= (a_u16Value)

/** x86: preserve upper register bits.   */
#define IEM_MC_OR_GREG_U8(a_iGReg, a_u8Value)           *iemGRegRefU8( pVCpu, (a_iGReg)) |= (a_u8Value)
/** x86: preserve upper register bits.   */
#define IEM_MC_OR_GREG_U16(a_iGReg, a_u16Value)         *iemGRegRefU16(pVCpu, (a_iGReg)) |= (a_u16Value)

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
#define IEM_MC_MERGE_YREG_U64LO_U64LO_ZX_VLMAX(a_iYRegDst, a_iYRegSrc64, a_iYRegSrcHx) /* for vmovlhps */ \
    do { uintptr_t const iYRegDstTmp    = (a_iYRegDst); \
         uintptr_t const iYRegSrc64Tmp  = (a_iYRegSrc64); \
         uintptr_t const iYRegSrcHxTmp  = (a_iYRegSrcHx); \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[0]       = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrcHxTmp].au64[0]; \
         pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegDstTmp].au64[1]       = pVCpu->cpum.GstCtx.XState.x87.aXMM[iYRegSrc64Tmp].au64[0]; \
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

#define IEM_MC_FETCH_MEM_SEG_R80(a_r80Dst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataR80Jmp(pVCpu, &(a_r80Dst), (a_iSeg), (a_GCPtrMem))
#define IEM_MC_FETCH_MEM_SEG_D80(a_d80Dst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataD80Jmp(pVCpu, &(a_d80Dst), (a_iSeg), (a_GCPtrMem))

#define IEM_MC_FETCH_MEM_FLAT_R80(a_r80Dst, a_GCPtrMem) \
    iemMemFlatFetchDataR80Jmp(pVCpu, &(a_r80Dst), (a_GCPtrMem))
#define IEM_MC_FETCH_MEM_FLAT_D80(a_d80Dst, a_GCPtrMem) \
    iemMemFlatFetchDataD80Jmp(pVCpu, &(a_d80Dst), (a_GCPtrMem))

#define IEM_MC_FETCH_MEM_SEG_XMM(a_XmmDst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataU128Jmp(pVCpu, &(a_XmmDst).uXmm, (a_iSeg), (a_GCPtrMem))
#define IEM_MC_FETCH_MEM_SEG_XMM_NO_AC(a_XmmDst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataU128NoAcJmp(pVCpu, &(a_XmmDst).uXmm, (a_iSeg), (a_GCPtrMem))
#define IEM_MC_FETCH_MEM_SEG_XMM_ALIGN_SSE(a_XmmDst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataU128AlignedSseJmp(pVCpu, &(a_XmmDst).uXmm, (a_iSeg), (a_GCPtrMem))

#define IEM_MC_FETCH_MEM_FLAT_XMM(a_XmmDst, a_GCPtrMem) \
    iemMemFlatFetchDataU128Jmp(pVCpu, &(a_XmmDst).uXmm, (a_GCPtrMem))
#define IEM_MC_FETCH_MEM_FLAT_XMM_NO_AC(a_XmmDst, a_GCPtrMem) \
    iemMemFlatFetchDataU128NoAcJmp(pVCpu, &(a_XmmDst).uXmm, (a_GCPtrMem))
#define IEM_MC_FETCH_MEM_FLAT_XMM_ALIGN_SSE(a_XmmDst, a_GCPtrMem) \
    iemMemFlatFetchDataU128AlignedSseJmp(pVCpu, &(a_XmmDst).uXmm, (a_GCPtrMem))

#define IEM_MC_FETCH_MEM_SEG_U128_AND_XREG_U128(a_Dst, a_iXReg1, a_iSeg2, a_GCPtrMem2) do { \
        iemMemFetchDataU128Jmp(pVCpu, &(a_Dst).uSrc2, (a_iSeg2), (a_GCPtrMem2)); \
        (a_Dst).uSrc1.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
    } while (0)
#define IEM_MC_FETCH_MEM_FLAT_U128_AND_XREG_U128(a_Dst, a_iXReg1, a_GCPtrMem2) do { \
        iemMemFlatFetchDataU128Jmp(pVCpu, &(a_Dst).uSrc2, (a_GCPtrMem2)); \
        (a_Dst).uSrc1.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
    } while (0)

#define IEM_MC_FETCH_MEM_SEG_XMM_ALIGN_SSE_AND_XREG_XMM(a_Dst, a_iXReg1, a_iSeg2, a_GCPtrMem2) do { \
        iemMemFetchDataU128AlignedSseJmp(pVCpu, &(a_Dst).uSrc2.uXmm, (a_iSeg2), (a_GCPtrMem2)); \
        (a_Dst).uSrc1.uXmm.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.uXmm.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
    } while (0)

#define IEM_MC_FETCH_MEM_SEG_XMM_NO_AC_AND_XREG_XMM(a_Dst, a_iXReg1, a_iSeg2, a_GCPtrMem2) do { \
        iemMemFetchDataU128NoAcJmp(pVCpu, &(a_Dst).uSrc2.uXmm, (a_iSeg2), (a_GCPtrMem2)); \
        (a_Dst).uSrc1.uXmm.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.uXmm.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
    } while (0)

#define IEM_MC_FETCH_MEM_FLAT_XMM_ALIGN_SSE_AND_XREG_XMM(a_Dst, a_iXReg1, a_GCPtrMem2) do { \
        iemMemFlatFetchDataU128AlignedSseJmp(pVCpu, &(a_Dst).uSrc2.uXmm, (a_GCPtrMem2)); \
        (a_Dst).uSrc1.uXmm.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.uXmm.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
    } while (0)

#define IEM_MC_FETCH_MEM_FLAT_XMM_NO_AC_AND_XREG_XMM(a_Dst, a_iXReg1, a_GCPtrMem2) do { \
        iemMemFlatFetchDataU128NoAcJmp(pVCpu, &(a_Dst).uSrc2.uXmm, (a_GCPtrMem2)); \
        (a_Dst).uSrc1.uXmm.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.uXmm.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
    } while (0)

#define IEM_MC_FETCH_MEM_SEG_XMM_U32_AND_XREG_XMM(a_Dst, a_iXReg1, a_iDWord2, a_iSeg2, a_GCPtrMem2) do {  \
        (a_Dst).uSrc2.uXmm.au64[0] = 0; \
        (a_Dst).uSrc2.uXmm.au64[1] = 0; \
        (a_Dst).uSrc2.uXmm.au32[(a_iDWord2)] = iemMemFetchDataU32Jmp(pVCpu, (a_iSeg2), (a_GCPtrMem2)); \
        (a_Dst).uSrc1.uXmm.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.uXmm.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
    } while (0)
#define IEM_MC_FETCH_MEM_FLAT_XMM_U32_AND_XREG_XMM(a_Dst, a_iXReg1, a_iDWord2, a_GCPtrMem2) do { \
        (a_Dst).uSrc2.uXmm.au64[0] = 0; \
        (a_Dst).uSrc2.uXmm.au64[1] = 0; \
        (a_Dst).uSrc2.uXmm.au32[(a_iDWord2)] = iemMemFlatFetchDataU32Jmp(pVCpu, (a_GCPtrMem2)); \
        (a_Dst).uSrc1.uXmm.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.uXmm.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
    } while (0)

#define IEM_MC_FETCH_MEM_SEG_XMM_U64_AND_XREG_XMM(a_Dst, a_iXReg1, a_iQWord2, a_iSeg2, a_GCPtrMem2) do {  \
        (a_Dst).uSrc2.uXmm.au64[!(a_iQWord2)] = 0; \
        (a_Dst).uSrc2.uXmm.au64[(a_iQWord2)]  = iemMemFetchDataU64Jmp(pVCpu, (a_iSeg2), (a_GCPtrMem2)); \
        (a_Dst).uSrc1.uXmm.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.uXmm.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
    } while (0)
#define IEM_MC_FETCH_MEM_FLAT_XMM_U64_AND_XREG_XMM(a_Dst, a_iXReg1, a_iQWord2, a_GCPtrMem2) do {  \
        (a_Dst).uSrc2.uXmm.au64[1] = 0; \
        (a_Dst).uSrc2.uXmm.au64[(a_iQWord2)]  = iemMemFlatFetchDataU64Jmp(pVCpu, (a_GCPtrMem2)); \
        (a_Dst).uSrc1.uXmm.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.uXmm.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
    } while (0)


#define IEM_MC_FETCH_MEM_SEG_U128_AND_XREG_U128_AND_RAX_RDX_U64(a_Dst, a_iXReg1, a_iSeg2, a_GCPtrMem2) do { \
        iemMemFetchDataU128Jmp(pVCpu, &(a_Dst).uSrc2, (a_iSeg2), (a_GCPtrMem2)); \
        (a_Dst).uSrc1.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
        (a_Dst).u64Rax        = pVCpu->cpum.GstCtx.rax; \
        (a_Dst).u64Rdx        = pVCpu->cpum.GstCtx.rdx; \
    } while (0)
#define IEM_MC_FETCH_MEM_SEG_U128_AND_XREG_U128_AND_EAX_EDX_U32_SX_U64(a_Dst, a_iXReg1, a_iSeg2, a_GCPtrMem2) do { \
        iemMemFetchDataU128Jmp(pVCpu, &(a_Dst).uSrc2, (a_iSeg2), (a_GCPtrMem2)); \
        (a_Dst).uSrc1.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
        (a_Dst).u64Rax        = (int64_t)(int32_t)pVCpu->cpum.GstCtx.eax; \
        (a_Dst).u64Rdx        = (int64_t)(int32_t)pVCpu->cpum.GstCtx.edx; \
    } while (0)

#define IEM_MC_FETCH_MEM_FLAT_U128_AND_XREG_U128_AND_RAX_RDX_U64(a_Dst, a_iXReg1, a_GCPtrMem2) do { \
        iemMemFlatFetchDataU128Jmp(pVCpu, &(a_Dst).uSrc2, (a_GCPtrMem2)); \
        (a_Dst).uSrc1.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
        (a_Dst).u64Rax        = pVCpu->cpum.GstCtx.rax; \
        (a_Dst).u64Rdx        = pVCpu->cpum.GstCtx.rdx; \
    } while (0)
#define IEM_MC_FETCH_MEM_FLAT_U128_AND_XREG_U128_AND_EAX_EDX_U32_SX_U64(a_Dst, a_iXReg1, a_GCPtrMem2) do { \
        iemMemFlatFetchDataU128Jmp(pVCpu, &(a_Dst).uSrc2, (a_GCPtrMem2)); \
        (a_Dst).uSrc1.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[0]; \
        (a_Dst).uSrc1.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[(a_iXReg1)].au64[1]; \
        (a_Dst).u64Rax        = (int64_t)(int32_t)pVCpu->cpum.GstCtx.eax; \
        (a_Dst).u64Rdx        = (int64_t)(int32_t)pVCpu->cpum.GstCtx.edx; \
    } while (0)


#define IEM_MC_FETCH_MEM_SEG_YMM(a_YmmDst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataU256NoAcJmp(pVCpu, &(a_YmmDst).ymm, (a_iSeg), (a_GCPtrMem))
#define IEM_MC_FETCH_MEM_SEG_YMM_NO_AC(a_YmmDst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataU256NoAcJmp(pVCpu, &(a_YmmDst).ymm, (a_iSeg), (a_GCPtrMem))
#define IEM_MC_FETCH_MEM_SEG_YMM_ALIGN_AVX(a_YmmDst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataU256AlignedAvxJmp(pVCpu, &(a_YmmDst).ymm, (a_iSeg), (a_GCPtrMem))

#define IEM_MC_FETCH_MEM_SEG_YMM_NO_AC_AND_YREG_YMM(a_uYmmDst, a_iYRegSrc1, a_iSeg2, a_GCPtrMem2) do { \
        uintptr_t const a_iYRegSrc1Tmp = (a_iYRegSrc1); \
        iemMemFetchDataU256NoAcJmp(pVCpu, &(a_uYmmDst).uSrc2.ymm, (a_iSeg2), (a_GCPtrMem2)); \
        (a_uYmmDst).uSrc1.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[a_iYRegSrc1Tmp].au64[0]; \
        (a_uYmmDst).uSrc1.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[a_iYRegSrc1Tmp].au64[1]; \
        (a_uYmmDst).uSrc1.au64[2] = pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[a_iYRegSrc1Tmp].au64[0]; \
        (a_uYmmDst).uSrc1.au64[3] = pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[a_iYRegSrc1Tmp].au64[1]; \
    } while (0)

#define IEM_MC_FETCH_MEM_FLAT_YMM(a_YmmDst, a_GCPtrMem) \
    iemMemFlatFetchDataU256NoAcJmp(pVCpu, &(a_YmmDst).ymm, (a_GCPtrMem))
#define IEM_MC_FETCH_MEM_FLAT_YMM_NO_AC(a_YmmDst, a_GCPtrMem) \
    iemMemFlatFetchDataU256NoAcJmp(pVCpu, &(a_YmmDst).ymm, (a_GCPtrMem))
#define IEM_MC_FETCH_MEM_FLAT_YMM_ALIGN_AVX(a_YmmDst, a_GCPtrMem) \
    iemMemFlatFetchDataU256AlignedAvxJmp(pVCpu, &(a_YmmDst).ymm, (a_GCPtrMem))

#define IEM_MC_FETCH_MEM_FLAT_YMM_ALIGN_AVX_AND_YREG_YMM(a_uYmmDst, a_iYRegSrc1, a_GCPtrMem2) do { \
        uintptr_t const a_iYRegSrc1Tmp = (a_iYRegSrc1); \
        iemMemFlatFetchDataU256AlignedAvxJmp(pVCpu, &(a_uYmmDst).uSrc2.ymm, (a_GCPtrMem2)); \
        (a_uYmmDst).uSrc1.au64[0] = pVCpu->cpum.GstCtx.XState.x87.aXMM[a_iYRegSrc1Tmp].au64[0]; \
        (a_uYmmDst).uSrc1.au64[1] = pVCpu->cpum.GstCtx.XState.x87.aXMM[a_iYRegSrc1Tmp].au64[1]; \
        (a_uYmmDst).uSrc1.au64[2] = pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[a_iYRegSrc1Tmp].au64[0]; \
        (a_uYmmDst).uSrc1.au64[3] = pVCpu->cpum.GstCtx.XState.u.YmmHi.aYmmHi[a_iYRegSrc1Tmp].au64[1]; \
    } while (0)



#define IEM_MC_FETCH_MEM_SEG_U8_ZX_U16(a_u16Dst, a_iSeg, a_GCPtrMem) \
    ((a_u16Dst) = iemMemFetchDataU8Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_SEG_U8_ZX_U32(a_u32Dst, a_iSeg, a_GCPtrMem) \
    ((a_u32Dst) = iemMemFetchDataU8Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_SEG_U8_ZX_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    ((a_u64Dst) = iemMemFetchDataU8Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_SEG_U16_ZX_U32(a_u32Dst, a_iSeg, a_GCPtrMem) \
    ((a_u32Dst) = iemMemFetchDataU16Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_SEG_U16_ZX_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    ((a_u64Dst) = iemMemFetchDataU16Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_SEG_U32_ZX_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    ((a_u64Dst) = iemMemFetchDataU32Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))

#define IEM_MC_FETCH_MEM_FLAT_U8_ZX_U16(a_u16Dst, a_GCPtrMem) \
    ((a_u16Dst) = iemMemFlatFetchDataU8Jmp(pVCpu, (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_FLAT_U8_ZX_U32(a_u32Dst, a_GCPtrMem) \
    ((a_u32Dst) = iemMemFlatFetchDataU8Jmp(pVCpu, (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_FLAT_U8_ZX_U64(a_u64Dst, a_GCPtrMem) \
    ((a_u64Dst) = iemMemFlatFetchDataU8Jmp(pVCpu, (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_FLAT_U16_ZX_U32(a_u32Dst, a_GCPtrMem) \
    ((a_u32Dst) = iemMemFlatFetchDataU16Jmp(pVCpu, (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_FLAT_U16_ZX_U64(a_u64Dst, a_GCPtrMem) \
    ((a_u64Dst) = iemMemFlatFetchDataU16Jmp(pVCpu, (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_FLAT_U32_ZX_U64(a_u64Dst, a_GCPtrMem) \
    ((a_u64Dst) = iemMemFlatFetchDataU32Jmp(pVCpu, (a_GCPtrMem)))

#define IEM_MC_FETCH_MEM_SEG_U8_SX_U16(a_u16Dst, a_iSeg, a_GCPtrMem) \
    ((a_u16Dst) = (int8_t)iemMemFetchDataU8Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_SEG_U8_SX_U32(a_u32Dst, a_iSeg, a_GCPtrMem) \
    ((a_u32Dst) = (int8_t)iemMemFetchDataU8Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_SEG_U8_SX_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    ((a_u64Dst) = (int8_t)iemMemFetchDataU8Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_SEG_U16_SX_U32(a_u32Dst, a_iSeg, a_GCPtrMem) \
    ((a_u32Dst) = (int16_t)iemMemFetchDataU16Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_SEG_U16_SX_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    ((a_u64Dst) = (int16_t)iemMemFetchDataU16Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_SEG_U32_SX_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    ((a_u64Dst) = (int32_t)iemMemFetchDataU32Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))

#define IEM_MC_FETCH_MEM_FLAT_U8_SX_U16(a_u16Dst, a_GCPtrMem) \
    ((a_u16Dst) = (int8_t)iemMemFlatFetchDataU8Jmp(pVCpu, (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_FLAT_U8_SX_U32(a_u32Dst, a_GCPtrMem) \
    ((a_u32Dst) = (int8_t)iemMemFlatFetchDataU8Jmp(pVCpu, (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_FLAT_U8_SX_U64(a_u64Dst, a_GCPtrMem) \
    ((a_u64Dst) = (int8_t)iemMemFlatFetchDataU8Jmp(pVCpu, (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_FLAT_U16_SX_U32(a_u32Dst, a_GCPtrMem) \
    ((a_u32Dst) = (int16_t)iemMemFlatFetchDataU16Jmp(pVCpu, (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_FLAT_U16_SX_U64(a_u64Dst, a_GCPtrMem) \
    ((a_u64Dst) = (int16_t)iemMemFlatFetchDataU16Jmp(pVCpu, (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_FLAT_U32_SX_U64(a_u64Dst, a_GCPtrMem) \
    ((a_u64Dst) = (int32_t)iemMemFlatFetchDataU32Jmp(pVCpu, (a_GCPtrMem)))

#define IEM_MC_STORE_MEM_BY_REF_R80_NEG_QNAN(a_pr80Dst) \
    do { \
        (a_pr80Dst)->au64[0] = UINT64_C(0xc000000000000000); \
        (a_pr80Dst)->au16[4] = UINT16_C(0xffff); \
    } while (0)
#define IEM_MC_STORE_MEM_BY_REF_D80_INDEF(a_pd80Dst) \
    do { \
        (a_pd80Dst)->au64[0] = UINT64_C(0xc000000000000000); \
        (a_pd80Dst)->au16[4] = UINT16_C(0xffff); \
    } while (0)

#define IEM_MC_STORE_MEM_SEG_U128_ALIGN_SSE(a_iSeg, a_GCPtrMem, a_u128Value) \
    iemMemStoreDataU128AlignedSseJmp(pVCpu, (a_iSeg), (a_GCPtrMem), (a_u128Value))

#define IEM_MC_STORE_MEM_FLAT_U128_ALIGN_SSE(a_GCPtrMem, a_u128Value) \
    iemMemStoreDataU128AlignedSseJmp(pVCpu, UINT8_MAX, (a_GCPtrMem), (a_u128Value))

#define IEM_MC_STORE_MEM_SEG_U256_ALIGN_AVX(a_iSeg, a_GCPtrMem, a_u256Value) \
    iemMemStoreDataU256AlignedAvxJmp(pVCpu, (a_iSeg), (a_GCPtrMem), &(a_u256Value))

#define IEM_MC_STORE_MEM_FLAT_U256_ALIGN_AVX(a_GCPtrMem, a_u256Value) \
    iemMemFlatStoreDataU256AlignedAvxJmp(pVCpu, (a_GCPtrMem), &(a_u256Value))


/* Regular stack push and pop: */
#define IEM_MC_PUSH_U16(a_u16Value)             iemMemStackPushU16Jmp(pVCpu, (a_u16Value))
#define IEM_MC_PUSH_U32(a_u32Value)             iemMemStackPushU32Jmp(pVCpu, (a_u32Value))
#define IEM_MC_PUSH_U32_SREG(a_uSegVal)         iemMemStackPushU32SRegJmp(pVCpu, (a_uSegVal))
#define IEM_MC_PUSH_U64(a_u64Value)             iemMemStackPushU64Jmp(pVCpu, (a_u64Value))

#define IEM_MC_POP_GREG_U16(a_iGReg)            iemMemStackPopGRegU16Jmp(pVCpu, (a_iGReg))
#define IEM_MC_POP_GREG_U32(a_iGReg)            iemMemStackPopGRegU32Jmp(pVCpu, (a_iGReg))
#define IEM_MC_POP_GREG_U64(a_iGReg)            iemMemStackPopGRegU64Jmp(pVCpu, (a_iGReg))

/* 32-bit flat stack push and pop: */
#define IEM_MC_FLAT32_PUSH_U16(a_u16Value)      iemMemFlat32StackPushU16Jmp(pVCpu, (a_u16Value))
#define IEM_MC_FLAT32_PUSH_U32(a_u32Value)      iemMemFlat32StackPushU32Jmp(pVCpu, (a_u32Value))
#define IEM_MC_FLAT32_PUSH_U32_SREG(a_uSegVal)  iemMemFlat32StackPushU32SRegJmp(pVCpu, (a_uSegVal))

#define IEM_MC_FLAT32_POP_GREG_U16(a_iGReg)     iemMemFlat32StackPopGRegU16Jmp(pVCpu, a_iGReg))
#define IEM_MC_FLAT32_POP_GREG_U32(a_iGReg)     iemMemFlat32StackPopGRegU32Jmp(pVCpu, a_iGReg))

/* 64-bit flat stack push and pop: */
#define IEM_MC_FLAT64_PUSH_U16(a_u16Value)      iemMemFlat64StackPushU16Jmp(pVCpu, (a_u16Value))
#define IEM_MC_FLAT64_PUSH_U64(a_u64Value)      iemMemFlat64StackPushU64Jmp(pVCpu, (a_u64Value))

#define IEM_MC_FLAT64_POP_GREG_U16(a_iGReg)     iemMemFlat64StackPopGRegU16Jmp(pVCpu, (a_iGReg))
#define IEM_MC_FLAT64_POP_GREG_U64(a_iGReg)     iemMemFlat64StackPopGRegU64Jmp(pVCpu, (a_iGReg))


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
#define IEM_MC_MEM_SEG_MAP_R80_WO(a_pr80Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pr80Mem) = iemMemMapDataR80WoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))

/**
 * Maps guest memory for 80-bit float writeonly direct (or bounce) buffer acccess.
 *
 * @param[out] a_pr80Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_WO
 */
#define IEM_MC_MEM_FLAT_MAP_R80_WO(a_pr80Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pr80Mem) = iemMemFlatMapDataR80WoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))


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
#define IEM_MC_MEM_SEG_MAP_D80_WO(a_pd80Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pd80Mem) = iemMemMapDataD80WoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))

/**
 * Maps guest memory for 80-bit BCD writeonly direct (or bounce) buffer acccess.
 *
 * @param[out] a_pd80Mem    Where to return the pointer to the mapping.
 * @param[out] a_bUnmapInfo Where to return umapping instructions. uint8_t.
 * @param[in]  a_GCPtrMem   The memory address.
 * @remarks Will return/long jump on errors.
 * @see     IEM_MC_MEM_COMMIT_AND_UNMAP_WO
 */
#define IEM_MC_MEM_FLAT_MAP_D80_WO(a_pd80Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pd80Mem) = iemMemFlatMapDataD80WoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))


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
#define IEM_MC_MEM_COMMIT_AND_UNMAP_FOR_FPU_STORE_WO(a_bMapInfo, a_u16FSW) do { \
        if (   !(a_u16FSW & X86_FSW_ES) \
            || !(  (a_u16FSW & (X86_FSW_UE | X86_FSW_OE | X86_FSW_IE)) \
                 & ~(pVCpu->cpum.GstCtx.XState.x87.FCW & X86_FCW_MASK_ALL) ) ) \
            iemMemCommitAndUnmapWoJmp(pVCpu, a_bMapInfo); \
        else \
            iemMemRollbackAndUnmapWo(pVCpu, a_bMapInfo); \
    } while (0)



/** Calculate efficient address from R/M. */
#define IEM_MC_CALC_RM_EFF_ADDR(a_GCPtrEff, a_bRm, a_cbImmAndRspOffset) \
    ((a_GCPtrEff) = iemOpHlpCalcRmEffAddrJmp(pVCpu, (a_bRm), (a_cbImmAndRspOffset)))


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

/** @}  */

#endif /* !VMM_INCLUDED_SRC_VMMAll_target_x86_IEMMc_x86_h */

