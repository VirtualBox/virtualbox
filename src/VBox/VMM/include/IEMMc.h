/* $Id: IEMMc.h 110471 2025-07-30 09:12:20Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - IEM_MC_XXX, common.
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
 * There are target specific "microcodes" in addition to the ones listed here.
 * The target specific header may also override the definitions here to allow
 * for differences.
 *
 * @{
 */

#define IEM_MC_BEGIN(a_fMcFlags, a_fCImplFlags) {
#define IEM_MC_END()                            }


/** Dummy MC that prevents native recompilation. */
#define IEM_MC_NO_NATIVE_RECOMPILE()                    ((void)0)

/** Advances RIP, finishes the instruction and returns.
 * This may include raising debug exceptions and such. */
#define IEM_MC_ADVANCE_PC_AND_FINISH() \
    return iemRegPcIncAndFinishClearingFlags(pVCpu)


/** Sets PC, finishes the instruction and returns. */
#define IEM_MC_REL_JMP_S8_AND_FINISH(a_i8) \
    return iemRegPcRelativeJumpS8AndFinishClearingFlags(pVCpu, (a_i8))
/** Sets PC, finishes the instruction and returns. */
#define IEM_MC_REL_JMP_S16_AND_FINISH(a_i16) \
    return iemRegPcRelativeJumpS16AndFinishClearingFlags(pVCpu, (a_i16))
/** Sets PC, finishes the instruction and returns. */
#define IEM_MC_REL_JMP_S32_AND_FINISH(a_i32) \
    return iemRegPcRelativeJumpS32AndFinishClearingFlags(pVCpu, (a_i32))
/** Sets PC, finishes the instruction and returns. */
#define IEM_MC_IND_JMP_U16_AND_FINISH(a_u16NewIP) \
    return iemRegPcJumpU16AndFinishClearingFlags((pVCpu), (a_u16NewIP))
/** Sets PC, finishes the instruction and returns. */
#define IEM_MC_IND_JMP_U32_AND_FINISH(a_u32NewIP) \
    return iemRegPcJumpU32AndFinishClearingFlags((pVCpu), (a_u32NewIP))
/** Sets PC, finishes the instruction and returns. */
#define IEM_MC_IND_JMP_U64_AND_FINISH(a_u64NewIP) \
    return iemRegPcJumpU64AndFinishClearingFlags((pVCpu), (a_u64NewIP))

/** Saves the return address, sets PC, finishes the instruction and returns. */
#define IEM_MC_REL_CALL_S16_AND_FINISH(a_i16) \
    return iemRegPcRelativeCallS16AndFinishClearingFlags(pVCpu, (a_i16))
/** Saves the return address, sets PC, finishes the instruction and returns. */
#define IEM_MC_REL_CALL_S32_AND_FINISH(a_i32) \
    return iemRegPcRelativeCallS32AndFinishClearingFlags(pVCpu, (a_i32))
/** Saves the return address, sets PC, finishes the instruction and returns. */
#define IEM_MC_REL_CALL_S64_AND_FINISH(a_i64) \
    return iemRegPcRelativeCallS64AndFinishClearingFlags(pVCpu, (a_i64))
/** Saves the return address, sets PC, finishes the instruction and returns. */
#define IEM_MC_IND_CALL_U16_AND_FINISH(a_u16NewIP) \
    return iemRegPcIndirectCallU16AndFinishClearingFlags((pVCpu), (a_u16NewIP))
/** Saves the return address, sets PC, finishes the instruction and returns. */
#define IEM_MC_IND_CALL_U32_AND_FINISH(a_u32NewIP) \
    return iemRegPcIndirectCallU32AndFinishClearingFlags((pVCpu), (a_u32NewIP))
/** Saves the return address, sets PC, finishes the instruction and returns. */
#define IEM_MC_IND_CALL_U64_AND_FINISH(a_u64NewIP) \
    return iemRegPcIndirectCallU64AndFinishClearingFlags((pVCpu), (a_u64NewIP))


#define IEM_MC_LOCAL(a_Type, a_Name)                    a_Type a_Name
#define IEM_MC_LOCAL_ASSIGN(a_Type, a_Name, a_Value)    a_Type a_Name = (a_Value)
#define IEM_MC_LOCAL_CONST(a_Type, a_Name, a_Value)     a_Type const a_Name = (a_Value)
#define IEM_MC_NOREF(a_Name)                            RT_NOREF_PV(a_Name) /* NOP/liveness hack */
#define IEM_MC_ARG(a_Type, a_Name, a_iArg)              a_Type a_Name
#define IEM_MC_ARG_CONST(a_Type, a_Name, a_Value, a_iArg)       a_Type const a_Name = (a_Value)
#define IEM_MC_ARG_LOCAL_REF(a_Type, a_Name, a_Local, a_iArg)   a_Type const a_Name = &(a_Local)

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

/** @todo these zero-extends the result, which can be a bit confusing for
 *        IEM_MC_STORE_GREG_I32... */
#define IEM_MC_STORE_GREG_U32(a_iGReg, a_u32Value)      iemGRegStoreU32(pVCpu, (a_iGReg), (uint32_t)(a_u32Value)) /* clear high bits. */
#define IEM_MC_STORE_GREG_I32(a_iGReg, a_i32Value)      *iemGRegRefU64(pVCpu, (a_iGReg)) = (uint32_t)(a_i32Value) /* clear high bits. */
#define IEM_MC_STORE_GREG_U64(a_iGReg, a_u64Value)      iemGRegStoreU64(pVCpu, (a_iGReg), (a_u64Value))
#define IEM_MC_STORE_GREG_I64(a_iGReg, a_i64Value)      *iemGRegRefI64(pVCpu, (a_iGReg)) = (a_i64Value)
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


#define IEM_MC_REF_GREG_U8(a_pu8Dst, a_iGReg)           (a_pu8Dst)  = iemGRegRefU8( pVCpu, (a_iGReg))
#define IEM_MC_REF_GREG_U8_CONST(a_pu8Dst, a_iGReg)     (a_pu8Dst)  = (uint8_t  const *)iemGRegRefU8( pVCpu, (a_iGReg))
#define IEM_MC_REF_GREG_U16(a_pu16Dst, a_iGReg)         (a_pu16Dst) = iemGRegRefU16(pVCpu, (a_iGReg))
#define IEM_MC_REF_GREG_U16_CONST(a_pu16Dst, a_iGReg)   (a_pu16Dst) = (uint16_t const *)iemGRegRefU16(pVCpu, (a_iGReg))
/** @todo X86: User of IEM_MC_REF_GREG_U32 needs to clear the high bits on
 *        commit. Use IEM_MC_CLEAR_HIGH_GREG_U64! */
#define IEM_MC_REF_GREG_U32(a_pu32Dst, a_iGReg)         (a_pu32Dst) = iemGRegRefU32(pVCpu, (a_iGReg))
#define IEM_MC_REF_GREG_U32_CONST(a_pu32Dst, a_iGReg)   (a_pu32Dst) = (uint32_t const *)iemGRegRefU32(pVCpu, (a_iGReg))
#define IEM_MC_REF_GREG_I32(a_pi32Dst, a_iGReg)         (a_pi32Dst) = (int32_t        *)iemGRegRefU32(pVCpu, (a_iGReg))
#define IEM_MC_REF_GREG_I32_CONST(a_pi32Dst, a_iGReg)   (a_pi32Dst) = (int32_t  const *)iemGRegRefU32(pVCpu, (a_iGReg))
#define IEM_MC_REF_GREG_U64(a_pu64Dst, a_iGReg)         (a_pu64Dst) = iemGRegRefU64(pVCpu, (a_iGReg))
#define IEM_MC_REF_GREG_U64_CONST(a_pu64Dst, a_iGReg)   (a_pu64Dst) = (uint64_t const *)iemGRegRefU64(pVCpu, (a_iGReg))
#define IEM_MC_REF_GREG_I64(a_pi64Dst, a_iGReg)         (a_pi64Dst) = (int64_t        *)iemGRegRefU64(pVCpu, (a_iGReg))
#define IEM_MC_REF_GREG_I64_CONST(a_pi64Dst, a_iGReg)   (a_pi64Dst) = (int64_t  const *)iemGRegRefU64(pVCpu, (a_iGReg))

#define IEM_MC_ADD_GREG_U32(a_iGReg, a_u32Value) \
    do { /* Clears the high 32 bits of the register. */ \
        uint64_t * const pu64Reg = iemGRegRefU64(pVCpu, (a_iGReg)); \
        *pu64Reg = (uint32_t)((uint32_t)*pu64Reg + (a_u32Value)); \
    } while (0)
#define IEM_MC_ADD_GREG_U64(a_iGReg, a_u64Value)        *iemGRegRefU64(pVCpu, (a_iGReg)) += (a_u64Value)
#define IEM_MC_ADD_LOCAL_U32(a_u32Value, a_u32Const)    do { (a_u32Value) += (a_u32Const); } while (0)
#define IEM_MC_ADD_LOCAL_U64(a_u64Value, a_u64Const)    do { (a_u64Value) += (a_u64Const); } while (0)

#define IEM_MC_SUB_GREG_U32(a_iGReg, a_u8Const) \
    do { /* Clears the high 32 bits of the register. */ \
        uint64_t * const pu64Reg = iemGRegRefU64(pVCpu, (a_iGReg)); \
        *pu64Reg = (uint32_t)((uint32_t)*pu64Reg - (a_u8Const)); \
    } while (0)
#define IEM_MC_SUB_GREG_U64(a_iGReg, a_u8Const)          *iemGRegRefU64(pVCpu, (a_iGReg)) -= (a_u8Const)
#define IEM_MC_SUB_LOCAL_U32(a_u32Value, a_u32Const)    do { (a_u32Value) -= (a_u32Const); } while (0)
#define IEM_MC_SUB_LOCAL_U64(a_u64Value, a_u64Const)    do { (a_u64Value) -= (a_u64Const); } while (0)

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
#define IEM_MC_OR_LOCAL_U64(a_u64Local, a_u64Mask)      do { (a_u64Local) |= (a_u64Mask); } while (0)

#define IEM_MC_SAR_LOCAL_S16(a_i16Local, a_cShift)      do { (a_i16Local) >>= (a_cShift);  } while (0)
#define IEM_MC_SAR_LOCAL_S32(a_i32Local, a_cShift)      do { (a_i32Local) >>= (a_cShift);  } while (0)
#define IEM_MC_SAR_LOCAL_S64(a_i64Local, a_cShift)      do { (a_i64Local) >>= (a_cShift);  } while (0)

#define IEM_MC_SHR_LOCAL_U8(a_u8Local, a_cShift)        do { (a_u8Local)  >>= (a_cShift);  } while (0)

#define IEM_MC_SHL_LOCAL_S16(a_i16Local, a_cShift)      do { (a_i16Local) <<= (a_cShift);  } while (0)
#define IEM_MC_SHL_LOCAL_S32(a_i32Local, a_cShift)      do { (a_i32Local) <<= (a_cShift);  } while (0)
#define IEM_MC_SHL_LOCAL_S64(a_i64Local, a_cShift)      do { (a_i64Local) <<= (a_cShift);  } while (0)


#define IEM_MC_ADD_2LOCS_U32(a_u32Value, a_u32Addend)   do { (a_u32Value) += a_u32Addend; } while (0)
#define IEM_MC_ADD_2LOCS_U64(a_u64Value, a_u64Addend)   do { (a_u64Value) += a_u64Addend; } while (0)

#define IEM_MC_SUB_2LOCS_U32(a_u32Value, a_u32Subtrahend) do { (a_u32Value) -= a_u32Subtrahend; } while (0)
#define IEM_MC_SUB_2LOCS_U64(a_u64Value, a_u64Subtrahend) do { (a_u64Value) -= a_u64Subtrahend; } while (0)

#define IEM_MC_AND_2LOCS_U32(a_u32Local, a_u32Mask)     do { (a_u32Local) &= (a_u32Mask); } while (0)
#define IEM_MC_AND_2LOCS_U64(a_u64Local, a_u64Mask)     do { (a_u64Local) &= (a_u64Mask); } while (0)

#define IEM_MC_OR_2LOCS_U32(a_u32Local, a_u32Mask)      do { (a_u32Local) |= (a_u32Mask); } while (0)
#define IEM_MC_OR_2LOCS_U64(a_u64Local, a_u64Mask)      do { (a_u64Local) |= (a_u64Mask); } while (0)

#define IEM_MC_XOR_2LOCS_U32(a_u32Local, a_u32Mask)     do { (a_u32Local) ^= (a_u32Mask); } while (0)
#define IEM_MC_XOR_2LOCS_U64(a_u64Local, a_u64Mask)     do { (a_u64Local) ^= (a_u64Mask); } while (0)


#define IEM_MC_AND_GREG_U32(a_iGReg, a_u32Value) \
    do {  /* Clears the high 32 bits of the register. */ \
        uint64_t * const pu64Reg = iemGRegRefU64(pVCpu, (a_iGReg)); \
        *pu64Reg = (uint32_t)((uint32_t)*pu64Reg & (a_u32Value)); \
    } while (0)
#define IEM_MC_AND_GREG_U64(a_iGReg, a_u64Value)        *iemGRegRefU64(pVCpu, (a_iGReg)) &= (a_u64Value)

#define IEM_MC_OR_GREG_U32(a_iGReg, a_u32Value) \
    do {  /* Clears the high 32 bits of the register. */ \
        uint64_t * const pu64Reg = iemGRegRefU64(pVCpu, (a_iGReg)); \
        *pu64Reg = (uint32_t)((uint32_t)*pu64Reg | (a_u32Value)); \
    } while (0)
#define IEM_MC_OR_GREG_U64(a_iGReg, a_u64Value)         *iemGRegRefU64(pVCpu, (a_iGReg)) |= (a_u64Value)

#define IEM_MC_BSWAP_LOCAL_U16(a_u16Local)              (a_u16Local) = RT_BSWAP_U16((a_u16Local));
#define IEM_MC_BSWAP_LOCAL_U32(a_u32Local)              (a_u32Local) = RT_BSWAP_U32((a_u32Local));
#define IEM_MC_BSWAP_LOCAL_U64(a_u64Local)              (a_u64Local) = RT_BSWAP_U64((a_u64Local));


#define IEM_MC_FETCH_MEM_SEG_U8(a_u8Dst, a_iSeg, a_GCPtrMem) \
    ((a_u8Dst) = iemMemFetchDataU8Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM16_SEG_U8(a_u8Dst, a_iSeg, a_GCPtrMem16) \
    ((a_u8Dst) = iemMemFetchDataU8Jmp(pVCpu, (a_iSeg), (a_GCPtrMem16)))
#define IEM_MC_FETCH_MEM32_SEG_U8(a_u8Dst, a_iSeg, a_GCPtrMem32) \
    ((a_u8Dst) = iemMemFetchDataU8Jmp(pVCpu, (a_iSeg), (a_GCPtrMem32)))

#define IEM_MC_FETCH_MEM_FLAT_U8(a_u8Dst, a_GCPtrMem) \
    ((a_u8Dst) = iemMemFlatFetchDataU8Jmp(pVCpu, (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM16_FLAT_U8(a_u8Dst, a_GCPtrMem16) \
    ((a_u8Dst) = iemMemFlatFetchDataU8Jmp(pVCpu, (a_GCPtrMem16)))
#define IEM_MC_FETCH_MEM32_FLAT_U8(a_u8Dst, a_GCPtrMem32) \
    ((a_u8Dst) = iemMemFlatFetchDataU8Jmp(pVCpu, (a_GCPtrMem32)))

#define IEM_MC_FETCH_MEM_SEG_U16(a_u16Dst, a_iSeg, a_GCPtrMem) \
    ((a_u16Dst) = iemMemFetchDataU16Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_SEG_U16_DISP(a_u16Dst, a_iSeg, a_GCPtrMem, a_offDisp) \
    ((a_u16Dst) = iemMemFetchDataU16Jmp(pVCpu, (a_iSeg), (a_GCPtrMem) + (a_offDisp)))
#define IEM_MC_FETCH_MEM_SEG_I16(a_i16Dst, a_iSeg, a_GCPtrMem) \
    ((a_i16Dst) = (int16_t)iemMemFetchDataU16Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_SEG_I16_DISP(a_i16Dst, a_iSeg, a_GCPtrMem, a_offDisp) \
    ((a_i16Dst) = (int16_t)iemMemFetchDataU16Jmp(pVCpu, (a_iSeg), (a_GCPtrMem) + (a_offDisp)))

#define IEM_MC_FETCH_MEM_FLAT_U16(a_u16Dst, a_GCPtrMem) \
    ((a_u16Dst) = iemMemFlatFetchDataU16Jmp(pVCpu, (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_FLAT_U16_DISP(a_u16Dst, a_GCPtrMem, a_offDisp) \
    ((a_u16Dst) = iemMemFlatFetchDataU16Jmp(pVCpu, (a_GCPtrMem) + (a_offDisp)))
#define IEM_MC_FETCH_MEM_FLAT_I16(a_i16Dst, a_GCPtrMem) \
    ((a_i16Dst) = (int16_t)iemMemFlatFetchDataU16Jmp(pVCpu, (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_FLAT_I16_DISP(a_i16Dst, a_GCPtrMem, a_offDisp) \
    ((a_i16Dst) = (int16_t)iemMemFlatFetchDataU16Jmp(pVCpu, (a_GCPtrMem) + (a_offDisp)))

#define IEM_MC_FETCH_MEM_SEG_U32(a_u32Dst, a_iSeg, a_GCPtrMem) \
    ((a_u32Dst) = iemMemFetchDataU32Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_SEG_U32_DISP(a_u32Dst, a_iSeg, a_GCPtrMem, a_offDisp) \
    ((a_u32Dst) = iemMemFetchDataU32Jmp(pVCpu, (a_iSeg), (a_GCPtrMem) + (a_offDisp)))
#define IEM_MC_FETCH_MEM_SEG_I32(a_i32Dst, a_iSeg, a_GCPtrMem) \
    ((a_i32Dst) = (int32_t)iemMemFetchDataU32Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_SEG_I32_DISP(a_i32Dst, a_iSeg, a_GCPtrMem, a_offDisp) \
    ((a_i32Dst) = (int32_t)iemMemFetchDataU32Jmp(pVCpu, (a_iSeg), (a_GCPtrMem) + (a_offDisp)))

#define IEM_MC_FETCH_MEM_FLAT_U32(a_u32Dst, a_GCPtrMem) \
    ((a_u32Dst) = iemMemFlatFetchDataU32Jmp(pVCpu, (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_FLAT_U32_DISP(a_u32Dst, a_GCPtrMem, a_offDisp) \
    ((a_u32Dst) = iemMemFlatFetchDataU32Jmp(pVCpu, (a_GCPtrMem) + (a_offDisp)))
#define IEM_MC_FETCH_MEM_FLAT_I32(a_i32Dst, a_GCPtrMem) \
    ((a_i32Dst) = (int32_t)iemMemFlatFetchDataU32Jmp(pVCpu, (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_FLAT_I32_DISP(a_i32Dst, a_GCPtrMem, a_offDisp) \
    ((a_i32Dst) = (int32_t)iemMemFlatFetchDataU32Jmp(pVCpu, (a_GCPtrMem) + (a_offDisp)))

#define IEM_MC_FETCH_MEM_SEG_U64(a_u64Dst, a_iSeg, a_GCPtrMem) \
    ((a_u64Dst) = iemMemFetchDataU64Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_SEG_U64_DISP(a_u64Dst, a_iSeg, a_GCPtrMem, a_offDisp) \
    ((a_u64Dst) = iemMemFetchDataU64Jmp(pVCpu, (a_iSeg), (a_GCPtrMem) + (a_offDisp)))
#define IEM_MC_FETCH_MEM_SEG_U64_ALIGN_U128(a_u64Dst, a_iSeg, a_GCPtrMem) \
    ((a_u64Dst) = iemMemFetchDataU64AlignedU128Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_SEG_I64(a_i64Dst, a_iSeg, a_GCPtrMem) \
    ((a_i64Dst) = (int64_t)iemMemFetchDataU64Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))

#define IEM_MC_FETCH_MEM_FLAT_U64(a_u64Dst, a_GCPtrMem) \
    ((a_u64Dst) = iemMemFlatFetchDataU64Jmp(pVCpu, (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_FLAT_U64_DISP(a_u64Dst, a_GCPtrMem, a_offDisp) \
    ((a_u64Dst) = iemMemFlatFetchDataU64Jmp(pVCpu, (a_GCPtrMem) + (a_offDisp)))
#define IEM_MC_FETCH_MEM_FLAT_U64_ALIGN_U128(a_u64Dst, a_GCPtrMem) \
    ((a_u64Dst) = iemMemFlatFetchDataU64AlignedU128Jmp(pVCpu, (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_FLAT_I64(a_i64Dst, a_GCPtrMem) \
    ((a_i64Dst) = (int64_t)iemMemFlatFetchDataU64Jmp(pVCpu, (a_GCPtrMem)))

#define IEM_MC_FETCH_MEM_SEG_R32(a_r32Dst, a_iSeg, a_GCPtrMem) \
    ((a_r32Dst).u = iemMemFetchDataU32Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_SEG_R64(a_r64Dst, a_iSeg, a_GCPtrMem) \
    ((a_r64Dst).u = iemMemFetchDataU64Jmp(pVCpu, (a_iSeg), (a_GCPtrMem)))

#define IEM_MC_FETCH_MEM_FLAT_R32(a_r32Dst, a_GCPtrMem) \
    ((a_r32Dst).u = iemMemFlatFetchDataU32Jmp(pVCpu, (a_GCPtrMem)))
#define IEM_MC_FETCH_MEM_FLAT_R64(a_r64Dst, a_GCPtrMem) \
    ((a_r64Dst).u = iemMemFlatFetchDataU64Jmp(pVCpu, (a_GCPtrMem)))

#define IEM_MC_FETCH_MEM_SEG_U128(a_u128Dst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataU128Jmp(pVCpu, &(a_u128Dst), (a_iSeg), (a_GCPtrMem))
#define IEM_MC_FETCH_MEM_SEG_U128_NO_AC(a_u128Dst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataU128NoAcJmp(pVCpu, &(a_u128Dst), (a_iSeg), (a_GCPtrMem))
#define IEM_MC_FETCH_MEM_SEG_U128_ALIGN_SSE(a_u128Dst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataU128AlignedSseJmp(pVCpu, &(a_u128Dst), (a_iSeg), (a_GCPtrMem))

#define IEM_MC_FETCH_MEM_FLAT_U128(a_u128Dst, a_GCPtrMem) \
    iemMemFlatFetchDataU128Jmp(pVCpu, &(a_u128Dst), (a_GCPtrMem))
#define IEM_MC_FETCH_MEM_FLAT_U128_NO_AC(a_u128Dst, a_GCPtrMem) \
    iemMemFlatFetchDataU128NoAcJmp(pVCpu, &(a_u128Dst), (a_GCPtrMem))
#define IEM_MC_FETCH_MEM_FLAT_U128_ALIGN_SSE(a_u128Dst, a_GCPtrMem) \
    iemMemFlatFetchDataU128AlignedSseJmp(pVCpu, &(a_u128Dst), (a_GCPtrMem))

#define IEM_MC_FETCH_MEM_SEG_U256(a_u256Dst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataU256NoAcJmp(pVCpu, &(a_u256Dst), (a_iSeg), (a_GCPtrMem))
#define IEM_MC_FETCH_MEM_SEG_U256_NO_AC(a_u256Dst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataU256NoAcJmp(pVCpu, &(a_u256Dst), (a_iSeg), (a_GCPtrMem))
#define IEM_MC_FETCH_MEM_SEG_U256_ALIGN_AVX(a_u256Dst, a_iSeg, a_GCPtrMem) \
    iemMemFetchDataU256AlignedAvxJmp(pVCpu, &(a_u256Dst), (a_iSeg), (a_GCPtrMem))

#define IEM_MC_FETCH_MEM_FLAT_U256(a_u256Dst, a_GCPtrMem) \
    iemMemFlatFetchDataU256NoAcJmp(pVCpu, &(a_u256Dst), (a_GCPtrMem))
#define IEM_MC_FETCH_MEM_FLAT_U256_NO_AC(a_u256Dst, a_GCPtrMem) \
    iemMemFlatFetchDataU256NoAcJmp(pVCpu, &(a_u256Dst), (a_GCPtrMem))
#define IEM_MC_FETCH_MEM_FLAT_U256_ALIGN_AVX(a_u256Dst, a_GCPtrMem) \
    iemMemFlatFetchDataU256AlignedAvxJmp(pVCpu, &(a_u256Dst), (a_GCPtrMem))


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

#define IEM_MC_STORE_MEM_SEG_U8(a_iSeg, a_GCPtrMem, a_u8Value) \
    iemMemStoreDataU8Jmp(pVCpu, (a_iSeg), (a_GCPtrMem), (a_u8Value))
#define IEM_MC_STORE_MEM_SEG_U16(a_iSeg, a_GCPtrMem, a_u16Value) \
    iemMemStoreDataU16Jmp(pVCpu, (a_iSeg), (a_GCPtrMem), (a_u16Value))
#define IEM_MC_STORE_MEM_SEG_U32(a_iSeg, a_GCPtrMem, a_u32Value) \
    iemMemStoreDataU32Jmp(pVCpu, (a_iSeg), (a_GCPtrMem), (a_u32Value))
#define IEM_MC_STORE_MEM_SEG_U64(a_iSeg, a_GCPtrMem, a_u64Value) \
    iemMemStoreDataU64Jmp(pVCpu, (a_iSeg), (a_GCPtrMem), (a_u64Value))

#define IEM_MC_STORE_MEM_FLAT_U8(a_GCPtrMem, a_u8Value) \
    iemMemFlatStoreDataU8Jmp(pVCpu, (a_GCPtrMem), (a_u8Value))
#define IEM_MC_STORE_MEM_FLAT_U16(a_GCPtrMem, a_u16Value) \
    iemMemFlatStoreDataU16Jmp(pVCpu, (a_GCPtrMem), (a_u16Value))
#define IEM_MC_STORE_MEM_FLAT_U32(a_GCPtrMem, a_u32Value) \
    iemMemFlatStoreDataU32Jmp(pVCpu, (a_GCPtrMem), (a_u32Value))
#define IEM_MC_STORE_MEM_FLAT_U64(a_GCPtrMem, a_u64Value) \
    iemMemFlatStoreDataU64Jmp(pVCpu, (a_GCPtrMem), (a_u64Value))

#define IEM_MC_STORE_MEM_SEG_U8_CONST(a_iSeg, a_GCPtrMem, a_u8C) \
    iemMemStoreDataU8Jmp(pVCpu, (a_iSeg), (a_GCPtrMem), (a_u8C))
#define IEM_MC_STORE_MEM_SEG_U16_CONST(a_iSeg, a_GCPtrMem, a_u16C) \
    iemMemStoreDataU16Jmp(pVCpu, (a_iSeg), (a_GCPtrMem), (a_u16C))
#define IEM_MC_STORE_MEM_SEG_U32_CONST(a_iSeg, a_GCPtrMem, a_u32C) \
    iemMemStoreDataU32Jmp(pVCpu, (a_iSeg), (a_GCPtrMem), (a_u32C))
#define IEM_MC_STORE_MEM_SEG_U64_CONST(a_iSeg, a_GCPtrMem, a_u64C) \
    iemMemStoreDataU64Jmp(pVCpu, (a_iSeg), (a_GCPtrMem), (a_u64C))

#define IEM_MC_STORE_MEM_FLAT_U8_CONST(a_GCPtrMem, a_u8C) \
    iemMemFlatStoreDataU8Jmp(pVCpu, (a_GCPtrMem), (a_u8C))
#define IEM_MC_STORE_MEM_FLAT_U16_CONST(a_GCPtrMem, a_u16C) \
    iemMemFlatStoreDataU16Jmp(pVCpu, (a_GCPtrMem), (a_u16C))
#define IEM_MC_STORE_MEM_FLAT_U32_CONST(a_GCPtrMem, a_u32C) \
    iemMemFlatStoreDataU32Jmp(pVCpu, (a_GCPtrMem), (a_u32C))
#define IEM_MC_STORE_MEM_FLAT_U64_CONST(a_GCPtrMem, a_u64C) \
    iemMemFlatStoreDataU64Jmp(pVCpu, (a_GCPtrMem), (a_u64C))

#define IEM_MC_STORE_MEM_BY_REF_I8_CONST( a_pi8Dst,  a_i8C)     *(a_pi8Dst)  = (a_i8C)
#define IEM_MC_STORE_MEM_BY_REF_I16_CONST(a_pi16Dst, a_i16C)    *(a_pi16Dst) = (a_i16C)
#define IEM_MC_STORE_MEM_BY_REF_I32_CONST(a_pi32Dst, a_i32C)    *(a_pi32Dst) = (a_i32C)
#define IEM_MC_STORE_MEM_BY_REF_I64_CONST(a_pi64Dst, a_i64C)    *(a_pi64Dst) = (a_i64C)
#define IEM_MC_STORE_MEM_BY_REF_R32_NEG_QNAN(a_pr32Dst)         (a_pr32Dst)->u = UINT32_C(0xffc00000)
#define IEM_MC_STORE_MEM_BY_REF_R64_NEG_QNAN(a_pr64Dst)         (a_pr64Dst)->u = UINT64_C(0xfff8000000000000)

#define IEM_MC_STORE_MEM_SEG_U128(a_iSeg, a_GCPtrMem, a_u128Value) \
    iemMemStoreDataU128Jmp(pVCpu, (a_iSeg), (a_GCPtrMem), &(a_u128Value))
#define IEM_MC_STORE_MEM_SEG_U128_NO_AC(a_iSeg, a_GCPtrMem, a_u128Value) \
    iemMemStoreDataU128NoAcJmp(pVCpu, (a_iSeg), (a_GCPtrMem), &(a_u128Value))

#define IEM_MC_STORE_MEM_FLAT_U128(a_GCPtrMem, a_u128Value) \
    iemMemFlatStoreDataU128Jmp(pVCpu, (a_GCPtrMem), &(a_u128Value))
#define IEM_MC_STORE_MEM_FLAT_U128_NO_AC(a_GCPtrMem, a_u128Value) \
    iemMemFlatStoreDataU128NoAcJmp(pVCpu, (a_GCPtrMem), &(a_u128Value))

#define IEM_MC_STORE_MEM_SEG_U256(a_iSeg, a_GCPtrMem, a_u256Value) \
    iemMemStoreDataU256Jmp(pVCpu, (a_iSeg), (a_GCPtrMem), &(a_u256Value))
#define IEM_MC_STORE_MEM_SEG_U256_NO_AC(a_iSeg, a_GCPtrMem, a_u256Value) \
    iemMemStoreDataU256NoAcJmp(pVCpu, (a_iSeg), (a_GCPtrMem), &(a_u256Value))

#define IEM_MC_STORE_MEM_FLAT_U256(a_GCPtrMem, a_u256Value) \
    iemMemFlatStoreDataU256Jmp(pVCpu, (a_GCPtrMem), &(a_u256Value))
#define IEM_MC_STORE_MEM_FLAT_U256_NO_AC(a_GCPtrMem, a_u256Value) \
    iemMemFlatStoreDataU256NoAcJmp(pVCpu, (a_GCPtrMem), &(a_u256Value))


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
#define IEM_MC_MEM_SEG_MAP_U8_ATOMIC(a_pu8Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu8Mem) = iemMemMapDataU8AtJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))

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
#define IEM_MC_MEM_SEG_MAP_U8_RW(a_pu8Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu8Mem) = iemMemMapDataU8RwJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))

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
#define IEM_MC_MEM_SEG_MAP_U8_WO(a_pu8Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu8Mem) = iemMemMapDataU8WoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))

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
#define IEM_MC_MEM_SEG_MAP_U8_RO(a_pu8Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu8Mem) = iemMemMapDataU8RoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))

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
#define IEM_MC_MEM_FLAT_MAP_U8_ATOMIC(a_pu8Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu8Mem) = iemMemFlatMapDataU8AtJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))

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
#define IEM_MC_MEM_FLAT_MAP_U8_RW(a_pu8Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu8Mem) = iemMemFlatMapDataU8RwJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))

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
#define IEM_MC_MEM_FLAT_MAP_U8_WO(a_pu8Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu8Mem) = iemMemFlatMapDataU8WoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))

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
#define IEM_MC_MEM_FLAT_MAP_U8_RO(a_pu8Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu8Mem) = iemMemFlatMapDataU8RoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))


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
#define IEM_MC_MEM_SEG_MAP_U16_ATOMIC(a_pu16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu16Mem) = iemMemMapDataU16AtJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))

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
#define IEM_MC_MEM_SEG_MAP_U16_RW(a_pu16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu16Mem) = iemMemMapDataU16RwJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))

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
#define IEM_MC_MEM_SEG_MAP_U16_WO(a_pu16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu16Mem) = iemMemMapDataU16WoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))

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
#define IEM_MC_MEM_SEG_MAP_U16_RO(a_pu16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu16Mem) = iemMemMapDataU16RoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))

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
#define IEM_MC_MEM_FLAT_MAP_U16_ATOMIC(a_pu16Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu16Mem) = iemMemFlatMapDataU16AtJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))

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
#define IEM_MC_MEM_FLAT_MAP_U16_RW(a_pu16Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu16Mem) = iemMemFlatMapDataU16RwJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))

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
#define IEM_MC_MEM_FLAT_MAP_U16_WO(a_pu16Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu16Mem) = iemMemFlatMapDataU16WoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))

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
#define IEM_MC_MEM_FLAT_MAP_U16_RO(a_pu16Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu16Mem) = iemMemFlatMapDataU16RoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))

/** int16_t alias. */
#define IEM_MC_MEM_SEG_MAP_I16_WO(a_pi16Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pi16Mem) = (int16_t *)iemMemMapDataU16WoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))

/** Flat int16_t alias. */
#define IEM_MC_MEM_FLAT_MAP_I16_WO(a_pi16Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pi16Mem) = (int16_t *)iemMemFlatMapDataU16WoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))


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
#define IEM_MC_MEM_SEG_MAP_U32_ATOMIC(a_pu32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu32Mem) = iemMemMapDataU32AtJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))

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
#define IEM_MC_MEM_SEG_MAP_U32_RW(a_pu32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu32Mem) = iemMemMapDataU32RwJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))

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
#define IEM_MC_MEM_SEG_MAP_U32_WO(a_pu32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu32Mem) = iemMemMapDataU32WoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))

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
#define IEM_MC_MEM_SEG_MAP_U32_RO(a_pu32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu32Mem) = iemMemMapDataU32RoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))

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
#define IEM_MC_MEM_FLAT_MAP_U32_ATOMIC(a_pu32Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu32Mem) = iemMemFlatMapDataU32AtJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))

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
#define IEM_MC_MEM_FLAT_MAP_U32_RW(a_pu32Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu32Mem) = iemMemFlatMapDataU32RwJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))

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
#define IEM_MC_MEM_FLAT_MAP_U32_WO(a_pu32Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu32Mem) = iemMemFlatMapDataU32WoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))

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
#define IEM_MC_MEM_FLAT_MAP_U32_RO(a_pu32Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu32Mem) = iemMemFlatMapDataU32RoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))

/** int32_t alias. */
#define IEM_MC_MEM_SEG_MAP_I32_WO(a_pi32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pi32Mem) = (int32_t *)iemMemMapDataU32WoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))

/** Flat int32_t alias. */
#define IEM_MC_MEM_FLAT_MAP_I32_WO(a_pi32Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pi32Mem) = (int32_t *)iemMemFlatMapDataU32WoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))

/** RTFLOAT32U alias. */
#define IEM_MC_MEM_SEG_MAP_R32_WO(a_pr32Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pr32Mem) = (PRTFLOAT32U)iemMemMapDataU32WoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))

/** Flat RTFLOAT32U alias. */
#define IEM_MC_MEM_FLAT_MAP_R32_WO(a_pr32Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pr32Mem) = (PRTFLOAT32U)iemMemFlatMapDataU32WoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))


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
#define IEM_MC_MEM_SEG_MAP_U64_ATOMIC(a_pu64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu64Mem) = iemMemMapDataU64AtJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))

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
#define IEM_MC_MEM_SEG_MAP_U64_RW(a_pu64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu64Mem) = iemMemMapDataU64RwJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))

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
#define IEM_MC_MEM_SEG_MAP_U64_WO(a_pu64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu64Mem) = iemMemMapDataU64WoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))

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
#define IEM_MC_MEM_SEG_MAP_U64_RO(a_pu64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu64Mem) = iemMemMapDataU64RoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))

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
#define IEM_MC_MEM_FLAT_MAP_U64_ATOMIC(a_pu64Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu64Mem) = iemMemFlatMapDataU64AtJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))

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
#define IEM_MC_MEM_FLAT_MAP_U64_RW(a_pu64Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu64Mem) = iemMemFlatMapDataU64RwJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))

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
#define IEM_MC_MEM_FLAT_MAP_U64_WO(a_pu64Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu64Mem) = iemMemFlatMapDataU64WoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))

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
#define IEM_MC_MEM_FLAT_MAP_U64_RO(a_pu64Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu64Mem) = iemMemFlatMapDataU64RoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))

/** int64_t alias. */
#define IEM_MC_MEM_SEG_MAP_I64_WO(a_pi64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pi64Mem) = (int64_t *)iemMemMapDataU64WoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))

/** Flat int64_t alias. */
#define IEM_MC_MEM_FLAT_MAP_I64_WO(a_pi64Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pi64Mem) = (int64_t *)iemMemFlatMapDataU64WoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))

/** RTFLOAT64U alias. */
#define IEM_MC_MEM_SEG_MAP_R64_WO(a_pr64Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pr64Mem) = (PRTFLOAT64U)iemMemMapDataU64WoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))

/** Flat RTFLOAT64U alias. */
#define IEM_MC_MEM_FLAT_MAP_R64_WO(a_pr64Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pr64Mem) = (PRTFLOAT64U)iemMemFlatMapDataU64WoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))


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
#define IEM_MC_MEM_SEG_MAP_U128_ATOMIC(a_pu128Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu128Mem) = iemMemMapDataU128AtJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))

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
#define IEM_MC_MEM_SEG_MAP_U128_RW(a_pu128Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu128Mem) = iemMemMapDataU128RwJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))

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
#define IEM_MC_MEM_SEG_MAP_U128_WO(a_pu128Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu128Mem) = iemMemMapDataU128WoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))

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
#define IEM_MC_MEM_SEG_MAP_U128_RO(a_pu128Mem, a_bUnmapInfo, a_iSeg, a_GCPtrMem) \
    (a_pu128Mem) = iemMemMapDataU128RoJmp(pVCpu, &(a_bUnmapInfo), (a_iSeg), (a_GCPtrMem))

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
#define IEM_MC_MEM_FLAT_MAP_U128_ATOMIC(a_pu128Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu128Mem) = iemMemFlatMapDataU128AtJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))

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
#define IEM_MC_MEM_FLAT_MAP_U128_RW(a_pu128Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu128Mem) = iemMemFlatMapDataU128RwJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))

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
#define IEM_MC_MEM_FLAT_MAP_U128_WO(a_pu128Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu128Mem) = iemMemFlatMapDataU128WoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))

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
#define IEM_MC_MEM_FLAT_MAP_U128_RO(a_pu128Mem, a_bUnmapInfo, a_GCPtrMem) \
    (a_pu128Mem) = iemMemFlatMapDataU128RoJmp(pVCpu, &(a_bUnmapInfo), (a_GCPtrMem))


/* commit + unmap */

/** Commits the memory and unmaps guest memory previously mapped RW.
 * @remarks     May return.
 * @note        Implictly frees the a_bMapInfo variable.
 */
#define IEM_MC_MEM_COMMIT_AND_UNMAP_RW(a_bMapInfo)          iemMemCommitAndUnmapRwJmp(pVCpu, (a_bMapInfo))

/** Commits the memory and unmaps guest memory previously mapped ATOMIC.
 * @remarks     May return.
 * @note        Implictly frees the a_bMapInfo variable.
 */
#define IEM_MC_MEM_COMMIT_AND_UNMAP_ATOMIC(a_bMapInfo)      iemMemCommitAndUnmapRwJmp(pVCpu, (a_bMapInfo))

/** Commits the memory and unmaps guest memory previously mapped W.
 * @remarks     May return.
 * @note        Implictly frees the a_bMapInfo variable.
 */
#define IEM_MC_MEM_COMMIT_AND_UNMAP_WO(a_bMapInfo)          iemMemCommitAndUnmapWoJmp(pVCpu, (a_bMapInfo))

/** Commits the memory and unmaps guest memory previously mapped R.
 * @remarks     May return.
 * @note        Implictly frees the a_bMapInfo variable.
 */
#define IEM_MC_MEM_COMMIT_AND_UNMAP_RO(a_bMapInfo)          iemMemCommitAndUnmapRoJmp(pVCpu, (a_bMapInfo))


/** Rolls back (conceptually only, assumes no writes) and unmaps the guest memory.
 * @note        Implictly frees the a_bMapInfo variable. */
#define IEM_MC_MEM_ROLLBACK_AND_UNMAP_WO(a_bMapInfo)        iemMemRollbackAndUnmapWo(pVCpu, a_bMapInfo)



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
#if defined(VBOX_STRICT) && defined(VBOX_VMM_TARGET_X86)
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
            uint64_t const fRipMask = (pVCpu->iem.s.fExec & IEM_F_MODE_X86_CPUMODE_MASK) == IEMMODE_64BIT ? UINT64_MAX : UINT32_MAX; \
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
                              && (fExecRecalc & IEM_F_MODE_X86_CPUMODE_MASK) == IEMMODE_32BIT), \
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
#ifdef IEM_GET_INSTR_LEN
# define IEM_MC_CALL_CIMPL_0(a_fFlags, a_fGstShwFlush, a_pfnCImpl) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, IEM_GET_INSTR_LEN(pVCpu)))
#else
# define IEM_MC_CALL_CIMPL_0(a_fFlags, a_fGstShwFlush, a_pfnCImpl) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu))
#endif

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
#ifdef IEM_GET_INSTR_LEN
# define IEM_MC_CALL_CIMPL_1(a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, IEM_GET_INSTR_LEN(pVCpu), a0))
#else
# define IEM_MC_CALL_CIMPL_1(a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, a0))
#endif

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
#ifdef IEM_CIMPL_NEEDS_INSTR_LEN
# define IEM_MC_CALL_CIMPL_2(a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, IEM_GET_INSTR_LEN(pVCpu), a0, a1))
#else
# define IEM_MC_CALL_CIMPL_2(a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, a0, a1))
#endif

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
#ifdef IEM_CIMPL_NEEDS_INSTR_LEN
# define IEM_MC_CALL_CIMPL_3(a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, IEM_GET_INSTR_LEN(pVCpu), a0, a1, a2))
#else
# define IEM_MC_CALL_CIMPL_3(a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, a0, a1, a2))
#endif

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
#ifdef IEM_CIMPL_NEEDS_INSTR_LEN
# define IEM_MC_CALL_CIMPL_4(a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2, a3) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, IEM_GET_INSTR_LEN(pVCpu), a0, a1, a2, a3))
#else
# define IEM_MC_CALL_CIMPL_4(a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2, a3) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, a0, a1, a2, a3))
#endif

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
#ifdef IEM_CIMPL_NEEDS_INSTR_LEN
# define IEM_MC_CALL_CIMPL_5(a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2, a3, a4) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, IEM_GET_INSTR_LEN(pVCpu), a0, a1, a2, a3, a4))
#else
# define IEM_MC_CALL_CIMPL_5(a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2, a3, a4) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, a0, a1, a2, a3, a4))
#endif

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
#ifdef IEM_CIMPL_NEEDS_INSTR_LEN
# define IEM_MC_DEFER_TO_CIMPL_0_RET(a_fFlags, a_fGstShwFlush, a_pfnCImpl) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, IEM_GET_INSTR_LEN(pVCpu)))
#else
# define IEM_MC_DEFER_TO_CIMPL_0_RET(a_fFlags, a_fGstShwFlush, a_pfnCImpl) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu))
#endif

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
#ifdef IEM_CIMPL_NEEDS_INSTR_LEN
# define IEM_MC_DEFER_TO_CIMPL_1_RET(a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, IEM_GET_INSTR_LEN(pVCpu), a0))
#else
# define IEM_MC_DEFER_TO_CIMPL_1_RET(a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, a0))
#endif

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
#ifdef IEM_CIMPL_NEEDS_INSTR_LEN
# define IEM_MC_DEFER_TO_CIMPL_2_RET(a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, IEM_GET_INSTR_LEN(pVCpu), a0, a1))
#else
# define IEM_MC_DEFER_TO_CIMPL_2_RET(a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, a0, a1))
#endif

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
#ifdef IEM_CIMPL_NEEDS_INSTR_LEN
# define IEM_MC_DEFER_TO_CIMPL_3_RET(a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, IEM_GET_INSTR_LEN(pVCpu), a0, a1, a2))
#else
# define IEM_MC_DEFER_TO_CIMPL_3_RET(a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2) \
    IEM_MC_CALL_CIMPL_HLP_RET(a_fFlags, (a_pfnCImpl)(pVCpu, a0, a1, a2))
#endif



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

/*
 * x86: EFL == RFLAGS/EFLAGS for x86.
 * arm: EFL == NZCV.
 */

#ifdef VBOX_VMM_TARGET_X86
# define IEM_MC_IF_FLAGS_EXPR   (pVCpu->cpum.GstCtx.eflags.u)
#elif defined(VBOX_VMM_TARGET_ARMV8)
# define IEM_MC_IF_FLAGS_EXPR   (pVCpu->cpum.GstCtx.fPState)
#endif

/** @note x86: Not for IOPL or IF testing. */
#define IEM_MC_IF_FLAGS_BIT_SET(a_fBit)                   if (IEM_MC_IF_FLAGS_EXPR & (a_fBit)) {
/** @note x86: Not for IOPL or IF testing. */
#define IEM_MC_IF_FLAGS_BIT_NOT_SET(a_fBit)               if (!(IEM_MC_IF_FLAGS_EXPR & (a_fBit))) {
/** @note x86: Not for IOPL or IF testing. */
#define IEM_MC_IF_FLAGS_ANY_BITS_SET(a_fBits)             if (IEM_MC_IF_FLAGS_EXPR & (a_fBits)) {
/** @note x86: Not for IOPL or IF testing. */
#define IEM_MC_IF_FLAGS_NO_BITS_SET(a_fBits)              if (!(IEM_MC_IF_FLAGS_EXPR & (a_fBits))) {
/** @note x86: Not for IOPL or IF testing. */
#define IEM_MC_IF_FLAGS_BITS_NE(a_fBit1, a_fBit2)         \
    if (   !!(IEM_MC_IF_FLAGS_EXPR & (a_fBit1)) \
        != !!(IEM_MC_IF_FLAGS_EXPR & (a_fBit2)) ) {
/** @note x86: Not for IOPL or IF testing. */
#define IEM_MC_IF_FLAGS_BITS_EQ(a_fBit1, a_fBit2)         \
    if (   !!(IEM_MC_IF_FLAGS_EXPR & (a_fBit1)) \
        == !!(IEM_MC_IF_FLAGS_EXPR & (a_fBit2)) ) {
/** @note x86: Not for IOPL or IF testing. */
#define IEM_MC_IF_FLAGS_BIT_SET_OR_BITS_NE(a_fBit, a_fBit1, a_fBit2) \
    if (   (IEM_MC_IF_FLAGS_EXPR & (a_fBit)) \
        ||    !!(IEM_MC_IF_FLAGS_EXPR & (a_fBit1)) \
           != !!(IEM_MC_IF_FLAGS_EXPR & (a_fBit2)) ) {
/** @note x86: Not for IOPL or IF testing. */
#define IEM_MC_IF_FLAGS_BIT_NOT_SET_AND_BITS_EQ(a_fBit, a_fBit1, a_fBit2) \
    if (   !(IEM_MC_IF_FLAGS_EXPR & (a_fBit)) \
        &&    !!(IEM_MC_IF_FLAGS_EXPR & (a_fBit1)) \
           == !!(IEM_MC_IF_FLAGS_EXPR & (a_fBit2)) ) {
#define IEM_MC_IF_FLAGS_BIT_SET_AND_BIT_NOT_SET(a_fBit1, a_fBit2) \
    if ((IEM_MC_IF_FLAGS_EXPR & (a_fBit1 | a_fBit2)) == (a_fBit1)) {
#define IEM_MC_IF_FLAGS_BIT_SET_OR_BIT_NOT_SET(a_fBit1, a_fBit2) \
    if (   (IEM_MC_IF_FLAGS_EXPR & (a_fBit1)) \
        || !(IEM_MC_IF_FLAGS_EXPR & (a_fBit2))) {

#define IEM_MC_IF_LOCAL_IS_Z(a_Local)                   if ((a_Local) == 0) {
#define IEM_MC_IF_GREG_BIT_SET(a_iGReg, a_iBitNo)       if (iemGRegFetchU64(pVCpu, (a_iGReg)) & RT_BIT_64(a_iBitNo)) {

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


/** @}  */

/*
 * Include the target specific header.
 */
#ifdef VBOX_VMM_TARGET_X86
# include "VMMAll/target-x86/IEMMc-x86.h"
#elif defined(VBOX_VMM_TARGET_ARMV8)
# include "VMMAll/target-armv8/IEMMc-armv8.h"
#else
# error "port me"
#endif

#endif /* !VMM_INCLUDED_SRC_include_IEMMc_h */

