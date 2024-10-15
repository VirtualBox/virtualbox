/* $Id: IEMAllInstCommonBodyMacros.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEM - Instruction Decoding and Emulation, Common Body Macros.
 *
 * This is placed in its own file without anything else in it, so that it can
 * be digested by SimplerParser in IEMAllInstPython.py prior processing
 * any of the other IEMAllInstruction*.cpp.h files.  For instance
 * IEMAllInstCommon.cpp.h wouldn't do as it defines several invalid
 * instructions and such that could confuse the parser result.
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


/**
 * Body for word/dword/qword instructions like ADD, AND, OR, ++ with a register
 * as the destination.
 *
 * @note Used both in OneByte and TwoByte0f.
 */
#define IEMOP_BODY_BINARY_rv_rm(a_bRm, a_fnNormalU16, a_fnNormalU32, a_fnNormalU64, a_f16BitMcFlag, a_EmitterBasename, a_fNativeArchs) \
    /* \
     * If rm is denoting a register, no more instruction bytes. \
     */ \
    if (IEM_IS_MODRM_REG_MODE(a_bRm)) \
    { \
        switch (pVCpu->iem.s.enmEffOpSize) \
        { \
            case IEMMODE_16BIT: \
                IEM_MC_BEGIN(a_f16BitMcFlag, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_ARG(uint16_t,        u16Src,     2); \
                IEM_MC_FETCH_GREG_U16(u16Src, IEM_GET_MODRM_RM(pVCpu, a_bRm)); \
                IEM_MC_NATIVE_IF(a_fNativeArchs) { \
                    IEM_MC_LOCAL(uint16_t,  u16Dst); \
                    IEM_MC_FETCH_GREG_U16(u16Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_LOCAL_EFLAGS(uEFlags); \
                    IEM_MC_NATIVE_EMIT_3(RT_CONCAT3(iemNativeEmit_,a_EmitterBasename,_r_r_efl)<16>, u16Dst, u16Src, uEFlags); \
                    IEM_MC_STORE_GREG_U16(IEM_GET_MODRM_REG(pVCpu, bRm), u16Dst); \
                    IEM_MC_COMMIT_EFLAGS_OPT(uEFlags); \
                } IEM_MC_NATIVE_ELSE() { \
                    IEM_MC_ARG(uint16_t *,  pu16Dst,    1); \
                    IEM_MC_REF_GREG_U16(pu16Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_ARG_EFLAGS(      fEFlagsIn,  0); \
                    IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU16, fEFlagsIn, pu16Dst, u16Src); \
                    IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                } IEM_MC_NATIVE_ENDIF(); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            case IEMMODE_32BIT: \
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_ARG(uint32_t,        u32Src,     2); \
                IEM_MC_FETCH_GREG_U32(u32Src, IEM_GET_MODRM_RM(pVCpu, a_bRm)); \
                IEM_MC_NATIVE_IF(a_fNativeArchs) { \
                    IEM_MC_LOCAL(uint32_t,  u32Dst); \
                    IEM_MC_FETCH_GREG_U32(u32Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_LOCAL_EFLAGS(uEFlags); \
                    IEM_MC_NATIVE_EMIT_3(RT_CONCAT3(iemNativeEmit_,a_EmitterBasename,_r_r_efl)<32>, u32Dst, u32Src, uEFlags); \
                    IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), u32Dst); \
                    IEM_MC_COMMIT_EFLAGS_OPT(uEFlags); \
                } IEM_MC_NATIVE_ELSE() { \
                    IEM_MC_ARG(uint32_t *,  pu32Dst,    1); \
                    IEM_MC_REF_GREG_U32(pu32Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_ARG_EFLAGS(      fEFlagsIn,  0); \
                    IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU32, fEFlagsIn, pu32Dst, u32Src); \
                    IEM_MC_CLEAR_HIGH_GREG_U64(IEM_GET_MODRM_REG(pVCpu, a_bRm)); \
                    IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                } IEM_MC_NATIVE_ENDIF(); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            case IEMMODE_64BIT: \
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_ARG(uint64_t,        u64Src,     2); \
                IEM_MC_FETCH_GREG_U64(u64Src, IEM_GET_MODRM_RM(pVCpu, a_bRm)); \
                IEM_MC_NATIVE_IF(a_fNativeArchs) { \
                    IEM_MC_LOCAL(uint64_t,  u64Dst); \
                    IEM_MC_FETCH_GREG_U64(u64Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_LOCAL_EFLAGS(uEFlags); \
                    IEM_MC_NATIVE_EMIT_3(RT_CONCAT3(iemNativeEmit_,a_EmitterBasename,_r_r_efl)<64>, u64Dst, u64Src, uEFlags); \
                    IEM_MC_STORE_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), u64Dst); \
                    IEM_MC_COMMIT_EFLAGS_OPT(uEFlags); \
                } IEM_MC_NATIVE_ELSE() { \
                    IEM_MC_ARG(uint64_t *,  pu64Dst,    1); \
                    IEM_MC_REF_GREG_U64(pu64Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_ARG_EFLAGS(      fEFlagsIn,  0); \
                    IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU64, fEFlagsIn, pu64Dst, u64Src); \
                    IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                } IEM_MC_NATIVE_ENDIF(); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            IEM_NOT_REACHED_DEFAULT_CASE_RET(); \
        } \
    } \
    else \
    { \
        /* \
         * We're accessing memory. \
         */ \
        switch (pVCpu->iem.s.enmEffOpSize) \
        { \
            case IEMMODE_16BIT: \
                IEM_MC_BEGIN(a_f16BitMcFlag, 0); \
                IEM_MC_LOCAL(RTGCPTR,  GCPtrEffDst); \
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, a_bRm, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_ARG(uint16_t,        u16Src,     2); \
                IEM_MC_FETCH_MEM_U16(u16Src, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                IEM_MC_NATIVE_IF(a_fNativeArchs) { \
                    IEM_MC_LOCAL(uint16_t,  u16Dst); \
                    IEM_MC_FETCH_GREG_U16(u16Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_LOCAL_EFLAGS(uEFlags); \
                    IEM_MC_NATIVE_EMIT_3(RT_CONCAT3(iemNativeEmit_,a_EmitterBasename,_r_r_efl)<16>, u16Dst, u16Src, uEFlags); \
                    IEM_MC_STORE_GREG_U16(IEM_GET_MODRM_REG(pVCpu, bRm), u16Dst); \
                    IEM_MC_COMMIT_EFLAGS_OPT(uEFlags); \
                } IEM_MC_NATIVE_ELSE() { \
                    IEM_MC_ARG(uint16_t *,  pu16Dst,    1); \
                    IEM_MC_REF_GREG_U16(pu16Dst, IEM_GET_MODRM_REG(pVCpu, a_bRm)); \
                    IEM_MC_ARG_EFLAGS(      fEFlagsIn,  0); \
                    IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU16, fEFlagsIn, pu16Dst, u16Src); \
                    IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                } IEM_MC_NATIVE_ENDIF(); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            case IEMMODE_32BIT: \
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                IEM_MC_LOCAL(RTGCPTR,  GCPtrEffDst); \
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, a_bRm, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_ARG(uint32_t,        u32Src,     2); \
                IEM_MC_FETCH_MEM_U32(u32Src, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                IEM_MC_NATIVE_IF(a_fNativeArchs) { \
                    IEM_MC_LOCAL(uint32_t,  u32Dst); \
                    IEM_MC_FETCH_GREG_U32(u32Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_LOCAL_EFLAGS(uEFlags); \
                    IEM_MC_NATIVE_EMIT_3(RT_CONCAT3(iemNativeEmit_,a_EmitterBasename,_r_r_efl)<32>, u32Dst, u32Src, uEFlags); \
                    IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), u32Dst); \
                    IEM_MC_COMMIT_EFLAGS_OPT(uEFlags); \
                } IEM_MC_NATIVE_ELSE() { \
                    IEM_MC_ARG(uint32_t *, pu32Dst,     1); \
                    IEM_MC_REF_GREG_U32(pu32Dst, IEM_GET_MODRM_REG(pVCpu, a_bRm)); \
                    IEM_MC_ARG_EFLAGS(      fEFlagsIn,  0); \
                    IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU32, fEFlagsIn, pu32Dst, u32Src); \
                    IEM_MC_CLEAR_HIGH_GREG_U64(IEM_GET_MODRM_REG(pVCpu, a_bRm)); \
                    IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                } IEM_MC_NATIVE_ENDIF(); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            case IEMMODE_64BIT: \
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
                IEM_MC_LOCAL(RTGCPTR,  GCPtrEffDst); \
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, a_bRm, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_ARG(uint64_t,        u64Src,  2); \
                IEM_MC_FETCH_MEM_U64(u64Src, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                IEM_MC_NATIVE_IF(a_fNativeArchs) { \
                    IEM_MC_LOCAL(uint64_t,  u64Dst); \
                    IEM_MC_FETCH_GREG_U64(u64Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_LOCAL_EFLAGS(uEFlags); \
                    IEM_MC_NATIVE_EMIT_3(RT_CONCAT3(iemNativeEmit_,a_EmitterBasename,_r_r_efl)<64>, u64Dst, u64Src, uEFlags); \
                    IEM_MC_STORE_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), u64Dst); \
                    IEM_MC_COMMIT_EFLAGS_OPT(uEFlags); \
                } IEM_MC_NATIVE_ELSE() { \
                    IEM_MC_ARG(uint64_t *,  pu64Dst,    1); \
                    IEM_MC_REF_GREG_U64(pu64Dst, IEM_GET_MODRM_REG(pVCpu, a_bRm)); \
                    IEM_MC_ARG_EFLAGS(      fEFlagsIn,  0); \
                    IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU64, fEFlagsIn, pu64Dst, u64Src); \
                    IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                } IEM_MC_NATIVE_ENDIF(); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            IEM_NOT_REACHED_DEFAULT_CASE_RET(); \
        } \
    } \
    (void)0


/**
 * Body for word/dword/qword instructions like ADD, AND, OR, ++ with a register
 * as the destination.
 *
 * @note Used both in OneByte and TwoByte0f.
 */
#define IEMOP_BODY_BINARY_TODO_rv_rm(a_bRm, a_fnNormalU16, a_fnNormalU32, a_fnNormalU64, a_f16BitMcFlag, a_EmitterBasename, a_fNativeArchs) \
    /* \
     * If rm is denoting a register, no more instruction bytes. \
     */ \
    if (IEM_IS_MODRM_REG_MODE(a_bRm)) \
    { \
        switch (pVCpu->iem.s.enmEffOpSize) \
        { \
            case IEMMODE_16BIT: \
                IEM_MC_BEGIN(a_f16BitMcFlag, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_ARG(uint16_t,        u16Src,  1); \
                IEM_MC_FETCH_GREG_U16(u16Src, IEM_GET_MODRM_RM(pVCpu, a_bRm)); \
                IEM_MC_NATIVE_IF(a_fNativeArchs) { \
                    IEM_MC_LOCAL(uint16_t,  u16Dst); \
                    IEM_MC_FETCH_GREG_U16(u16Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_LOCAL_EFLAGS(uEFlags); \
                    IEM_MC_NATIVE_EMIT_3(RT_CONCAT3(iemNativeEmit_,a_EmitterBasename,_r_r_efl)<16>, u16Dst, u16Src, uEFlags); \
                    IEM_MC_STORE_GREG_U16(IEM_GET_MODRM_REG(pVCpu, bRm), u16Dst); \
                    IEM_MC_COMMIT_EFLAGS_OPT(uEFlags); \
                } IEM_MC_NATIVE_ELSE() { \
                    IEM_MC_ARG(uint16_t *,  pu16Dst, 0); \
                    IEM_MC_REF_GREG_U16(pu16Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_ARG(uint32_t *,  pEFlags, 2); \
                    IEM_MC_REF_EFLAGS(pEFlags); \
                    IEM_MC_CALL_VOID_AIMPL_3(a_fnNormalU16, pu16Dst, u16Src, pEFlags); \
                } IEM_MC_NATIVE_ENDIF(); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            case IEMMODE_32BIT: \
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_ARG(uint32_t,        u32Src,  1); \
                IEM_MC_FETCH_GREG_U32(u32Src, IEM_GET_MODRM_RM(pVCpu, a_bRm)); \
                IEM_MC_NATIVE_IF(a_fNativeArchs) { \
                    IEM_MC_LOCAL(uint32_t,  u32Dst); \
                    IEM_MC_FETCH_GREG_U32(u32Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_LOCAL_EFLAGS(uEFlags); \
                    IEM_MC_NATIVE_EMIT_3(RT_CONCAT3(iemNativeEmit_,a_EmitterBasename,_r_r_efl)<32>, u32Dst, u32Src, uEFlags); \
                    IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), u32Dst); \
                    IEM_MC_COMMIT_EFLAGS_OPT(uEFlags); \
                } IEM_MC_NATIVE_ELSE() { \
                    IEM_MC_ARG(uint32_t *,  pu32Dst, 0); \
                    IEM_MC_REF_GREG_U32(pu32Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_ARG(uint32_t *,  pEFlags, 2); \
                    IEM_MC_REF_EFLAGS(pEFlags); \
                    IEM_MC_CALL_VOID_AIMPL_3(a_fnNormalU32, pu32Dst, u32Src, pEFlags); \
                    IEM_MC_CLEAR_HIGH_GREG_U64(IEM_GET_MODRM_REG(pVCpu, a_bRm)); \
                } IEM_MC_NATIVE_ENDIF(); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            case IEMMODE_64BIT: \
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_ARG(uint64_t,        u64Src,  1); \
                IEM_MC_FETCH_GREG_U64(u64Src, IEM_GET_MODRM_RM(pVCpu, a_bRm)); \
                IEM_MC_NATIVE_IF(a_fNativeArchs) { \
                    IEM_MC_LOCAL(uint64_t,  u64Dst); \
                    IEM_MC_FETCH_GREG_U64(u64Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_LOCAL_EFLAGS(uEFlags); \
                    IEM_MC_NATIVE_EMIT_3(RT_CONCAT3(iemNativeEmit_,a_EmitterBasename,_r_r_efl)<64>, u64Dst, u64Src, uEFlags); \
                    IEM_MC_STORE_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), u64Dst); \
                    IEM_MC_COMMIT_EFLAGS_OPT(uEFlags); \
                } IEM_MC_NATIVE_ELSE() { \
                    IEM_MC_ARG(uint64_t *,  pu64Dst, 0); \
                    IEM_MC_REF_GREG_U64(pu64Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_ARG(uint32_t *,  pEFlags, 2); \
                    IEM_MC_REF_EFLAGS(pEFlags); \
                    IEM_MC_CALL_VOID_AIMPL_3(a_fnNormalU64, pu64Dst, u64Src, pEFlags); \
                } IEM_MC_NATIVE_ENDIF(); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            IEM_NOT_REACHED_DEFAULT_CASE_RET(); \
        } \
    } \
    else \
    { \
        /* \
         * We're accessing memory. \
         */ \
        switch (pVCpu->iem.s.enmEffOpSize) \
        { \
            case IEMMODE_16BIT: \
                IEM_MC_BEGIN(a_f16BitMcFlag, 0); \
                IEM_MC_LOCAL(RTGCPTR,  GCPtrEffDst); \
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, a_bRm, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_ARG(uint16_t,   u16Src,  1); \
                IEM_MC_FETCH_MEM_U16(u16Src, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                IEM_MC_NATIVE_IF(a_fNativeArchs) { \
                    IEM_MC_LOCAL(uint16_t,  u16Dst); \
                    IEM_MC_FETCH_GREG_U16(u16Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_LOCAL_EFLAGS(uEFlags); \
                    IEM_MC_NATIVE_EMIT_3(RT_CONCAT3(iemNativeEmit_,a_EmitterBasename,_r_r_efl)<16>, u16Dst, u16Src, uEFlags); \
                    IEM_MC_STORE_GREG_U16(IEM_GET_MODRM_REG(pVCpu, bRm), u16Dst); \
                    IEM_MC_COMMIT_EFLAGS_OPT(uEFlags); \
                } IEM_MC_NATIVE_ELSE() { \
                    IEM_MC_ARG(uint16_t *, pu16Dst, 0); \
                    IEM_MC_REF_GREG_U16(pu16Dst, IEM_GET_MODRM_REG(pVCpu, a_bRm)); \
                    IEM_MC_ARG(uint32_t *, pEFlags, 2); \
                    IEM_MC_REF_EFLAGS(pEFlags); \
                    IEM_MC_CALL_VOID_AIMPL_3(a_fnNormalU16, pu16Dst, u16Src, pEFlags); \
                } IEM_MC_NATIVE_ENDIF(); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            case IEMMODE_32BIT: \
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                IEM_MC_LOCAL(RTGCPTR,  GCPtrEffDst); \
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, a_bRm, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_ARG(uint32_t,   u32Src,  1); \
                IEM_MC_FETCH_MEM_U32(u32Src, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                IEM_MC_NATIVE_IF(a_fNativeArchs) { \
                    IEM_MC_LOCAL(uint32_t,  u32Dst); \
                    IEM_MC_FETCH_GREG_U32(u32Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_LOCAL_EFLAGS(uEFlags); \
                    IEM_MC_NATIVE_EMIT_3(RT_CONCAT3(iemNativeEmit_,a_EmitterBasename,_r_r_efl)<32>, u32Dst, u32Src, uEFlags); \
                    IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), u32Dst); \
                    IEM_MC_COMMIT_EFLAGS_OPT(uEFlags); \
                } IEM_MC_NATIVE_ELSE() { \
                    IEM_MC_ARG(uint32_t *, pu32Dst, 0); \
                    IEM_MC_ARG(uint32_t *, pEFlags, 2); \
                    IEM_MC_REF_GREG_U32(pu32Dst, IEM_GET_MODRM_REG(pVCpu, a_bRm)); \
                    IEM_MC_REF_EFLAGS(pEFlags); \
                    IEM_MC_CALL_VOID_AIMPL_3(a_fnNormalU32, pu32Dst, u32Src, pEFlags); \
                    IEM_MC_CLEAR_HIGH_GREG_U64(IEM_GET_MODRM_REG(pVCpu, a_bRm)); \
                } IEM_MC_NATIVE_ENDIF(); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            case IEMMODE_64BIT: \
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
                IEM_MC_LOCAL(RTGCPTR,  GCPtrEffDst); \
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, a_bRm, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_ARG(uint64_t,   u64Src,  1); \
                IEM_MC_FETCH_MEM_U64(u64Src, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                IEM_MC_NATIVE_IF(a_fNativeArchs) { \
                    IEM_MC_LOCAL(uint64_t,  u64Dst); \
                    IEM_MC_FETCH_GREG_U64(u64Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_LOCAL_EFLAGS(uEFlags); \
                    IEM_MC_NATIVE_EMIT_3(RT_CONCAT3(iemNativeEmit_,a_EmitterBasename,_r_r_efl)<64>, u64Dst, u64Src, uEFlags); \
                    IEM_MC_STORE_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), u64Dst); \
                    IEM_MC_COMMIT_EFLAGS_OPT(uEFlags); \
                } IEM_MC_NATIVE_ELSE() { \
                    IEM_MC_ARG(uint64_t *, pu64Dst, 0); \
                    IEM_MC_ARG(uint32_t *, pEFlags, 2); \
                    IEM_MC_REF_GREG_U64(pu64Dst, IEM_GET_MODRM_REG(pVCpu, a_bRm)); \
                    IEM_MC_REF_EFLAGS(pEFlags); \
                    IEM_MC_CALL_VOID_AIMPL_3(a_fnNormalU64, pu64Dst, u64Src, pEFlags); \
                } IEM_MC_NATIVE_ENDIF(); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            IEM_NOT_REACHED_DEFAULT_CASE_RET(); \
        } \
    } \
    (void)0

