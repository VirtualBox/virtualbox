/* $Id: IEMAllInstTwoByte0f.cpp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEM - Instruction Decoding and Emulation.
 *
 * @remarks IEMAllInstVexMap1.cpp.h is a VEX mirror of this file.
 *          Any update here is likely needed in that file too.
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


/** @name Two byte opcodes (first byte 0x0f).
 *
 * @{
 */


/**
 * Common worker for MMX instructions on the form:
 *      pxxx    mm1, mm2/mem64
 *
 * The @a pfnU64 worker function takes no FXSAVE state, just the operands.
 */
FNIEMOP_DEF_1(iemOpCommonMmxOpt_FullFull_To_Full, PFNIEMAIMPLMEDIAOPTF2U64, pfnU64)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * MMX, MMX.
         */
        /** @todo testcase: REX.B / REX.R and MMX register indexing. Ignored? */
        /** @todo testcase: REX.B / REX.R and segment register indexing. Ignored? */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fMmx);
        IEM_MC_ARG(uint64_t *,          pDst, 0);
        IEM_MC_ARG(uint64_t const *,    pSrc, 1);
        IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_REF_MREG_U64(pDst, IEM_GET_MODRM_REG_8(bRm));
        IEM_MC_REF_MREG_U64_CONST(pSrc, IEM_GET_MODRM_RM_8(bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU64, pDst, pSrc);
        IEM_MC_MODIFIED_MREG_BY_REF(pDst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * MMX, [mem64].
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(uint64_t *,                  pDst,       0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_ARG_LOCAL_REF(uint64_t const *,  pSrc, uSrc, 1);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fMmx);
        IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
        IEM_MC_FETCH_MEM_U64(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_REF_MREG_U64(pDst, IEM_GET_MODRM_REG_8(bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU64, pDst, pSrc);
        IEM_MC_MODIFIED_MREG_BY_REF(pDst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * Common worker for MMX instructions on the form:
 *      pxxx    mm1, mm2/mem64
 * for instructions introduced with SSE.
 *
 * The @a pfnU64 worker function takes no FXSAVE state, just the operands.
 */
FNIEMOP_DEF_1(iemOpCommonMmxSseOpt_FullFull_To_Full, PFNIEMAIMPLMEDIAOPTF2U64, pfnU64)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * MMX, MMX.
         */
        /** @todo testcase: REX.B / REX.R and MMX register indexing. Ignored? */
        /** @todo testcase: REX.B / REX.R and segment register indexing. Ignored? */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX_2_OR(fSse, fAmdMmxExts);
        IEM_MC_ARG(uint64_t *,          pDst, 0);
        IEM_MC_ARG(uint64_t const *,    pSrc, 1);
        IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_REF_MREG_U64(pDst, IEM_GET_MODRM_REG_8(bRm));
        IEM_MC_REF_MREG_U64_CONST(pSrc, IEM_GET_MODRM_RM_8(bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU64, pDst, pSrc);
        IEM_MC_MODIFIED_MREG_BY_REF(pDst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * MMX, [mem64].
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(uint64_t *,                  pDst,       0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_ARG_LOCAL_REF(uint64_t const *,  pSrc, uSrc, 1);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX_2_OR(fSse, fAmdMmxExts);
        IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
        IEM_MC_FETCH_MEM_U64(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_REF_MREG_U64(pDst, IEM_GET_MODRM_REG_8(bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU64, pDst, pSrc);
        IEM_MC_MODIFIED_MREG_BY_REF(pDst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * Common worker for MMX instructions on the form:
 *      pxxx    mm1, mm2/mem64
 * that was introduced with SSE2.
 */
FNIEMOP_DEF_1(iemOpCommonMmxOpt_FullFull_To_Full_Sse2, PFNIEMAIMPLMEDIAOPTF2U64, pfnU64)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * MMX, MMX.
         */
        /** @todo testcase: REX.B / REX.R and MMX register indexing. Ignored? */
        /** @todo testcase: REX.B / REX.R and segment register indexing. Ignored? */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_ARG(uint64_t *,          pDst, 0);
        IEM_MC_ARG(uint64_t const *,    pSrc, 1);
        IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_REF_MREG_U64(pDst, IEM_GET_MODRM_REG_8(bRm));
        IEM_MC_REF_MREG_U64_CONST(pSrc, IEM_GET_MODRM_RM_8(bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU64, pDst, pSrc);
        IEM_MC_MODIFIED_MREG_BY_REF(pDst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * MMX, [mem64].
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(uint64_t *,                  pDst,       0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_ARG_LOCAL_REF(uint64_t const *,  pSrc, uSrc, 1);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
        IEM_MC_FETCH_MEM_U64(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_REF_MREG_U64(pDst, IEM_GET_MODRM_REG_8(bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU64, pDst, pSrc);
        IEM_MC_MODIFIED_MREG_BY_REF(pDst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * Common worker for SSE instructions of the form:
 *      pxxx    xmm1, xmm2/mem128
 *
 * Proper alignment of the 128-bit operand is enforced.
 * SSE cpuid checks. No SIMD FP exceptions.
 *
 * The @a pfnU128 worker function takes no FXSAVE state, just the operands.
 *
 * @sa iemOpCommonSse2_FullFull_To_Full
 */
FNIEMOP_DEF_1(iemOpCommonSseOpt_FullFull_To_Full, PFNIEMAIMPLMEDIAOPTF2U128, pfnU128)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM, XMM.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_ARG(PRTUINT128U,          pDst, 0);
        IEM_MC_ARG(PCRTUINT128U,         pSrc, 1);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(pDst, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(pSrc, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU128, pDst, pSrc);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM, [mem128].
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(PRTUINT128U,                 pDst,       0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,      pSrc, uSrc, 1);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(pDst, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU128, pDst, pSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * Common worker for SSE2 instructions on the forms:
 *      pxxx    xmm1, xmm2/mem128
 *
 * Proper alignment of the 128-bit operand is enforced.
 * Exceptions type 4. SSE2 cpuid checks.
 *
 * The @a pfnU128 worker function takes no FXSAVE state, just the operands.
 *
 * @sa iemOpCommonSse41_FullFull_To_Full, iemOpCommonSse2_FullFull_To_Full
 */
FNIEMOP_DEF_1(iemOpCommonSse2Opt_FullFull_To_Full, PFNIEMAIMPLMEDIAOPTF2U128, pfnU128)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM, XMM.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_ARG(PRTUINT128U,          pDst, 0);
        IEM_MC_ARG(PCRTUINT128U,         pSrc, 1);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(pDst, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(pSrc, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU128, pDst, pSrc);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM, [mem128].
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(PRTUINT128U,                 pDst,       0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,      pSrc, uSrc, 1);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(pDst, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU128, pDst, pSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * A body preprocessor variant of iemOpCommonSse2Opt_FullFull_To_Full in order
 * to support native emitters for certain instructions.
 */
#define SSE2_OPT_BODY_FullFull_To_Full(a_Ins, a_pImplExpr, a_fRegNativeArchs, a_fMemNativeArchs) \
        PFNIEMAIMPLMEDIAOPTF2U128 const pfnU128 = (a_pImplExpr); \
        uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm); \
        if (IEM_IS_MODRM_REG_MODE(bRm)) \
        { \
            /* \
             * XMM, XMM. \
             */ \
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0); \
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2); \
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT(); \
            IEM_MC_PREPARE_SSE_USAGE(); \
            IEM_MC_NATIVE_IF(a_fRegNativeArchs) { \
                IEM_MC_NATIVE_EMIT_2(RT_CONCAT3(iemNativeEmit_,a_Ins,_rr_u128), IEM_GET_MODRM_REG(pVCpu, bRm), IEM_GET_MODRM_RM(pVCpu, bRm)); \
            } IEM_MC_NATIVE_ELSE() { \
                IEM_MC_ARG(PRTUINT128U,          pDst, 0); \
                IEM_MC_REF_XREG_U128(pDst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_ARG(PCRTUINT128U,         pSrc, 1); \
                IEM_MC_REF_XREG_U128_CONST(pSrc, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_CALL_VOID_AIMPL_2(pfnU128, pDst, pSrc); \
            } IEM_MC_NATIVE_ENDIF(); \
            IEM_MC_ADVANCE_RIP_AND_FINISH(); \
            IEM_MC_END(); \
        } \
        else \
        { \
            /* \
             * XMM, [mem128]. \
             */ \
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0); \
            IEM_MC_LOCAL(RTUINT128U,                uSrc); \
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc); \
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0); \
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2); \
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT(); \
            IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc); \
            IEM_MC_PREPARE_SSE_USAGE(); \
            IEM_MC_NATIVE_IF(a_fRegNativeArchs) { \
                IEM_MC_NATIVE_EMIT_2(RT_CONCAT3(iemNativeEmit_,a_Ins,_rv_u128), IEM_GET_MODRM_REG(pVCpu, bRm), uSrc); \
            } IEM_MC_NATIVE_ELSE() { \
                IEM_MC_ARG(PRTUINT128U,             pDst,       0); \
                IEM_MC_REF_XREG_U128(pDst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,  pSrc, uSrc, 1); \
                IEM_MC_CALL_VOID_AIMPL_2(pfnU128, pDst, pSrc); \
            } IEM_MC_NATIVE_ENDIF(); \
            IEM_MC_ADVANCE_RIP_AND_FINISH(); \
            IEM_MC_END(); \
        } void(0)


/**
 * Common worker for MMX instructions on the forms:
 *      pxxxx mm1, mm2/mem32
 *
 * The 2nd operand is the first half of a register, which in the memory case
 * means a 32-bit memory access.
 */
FNIEMOP_DEF_1(iemOpCommonMmx_LowLow_To_Full, PFNIEMAIMPLMEDIAOPTF2U64, pfnU64)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * MMX, MMX.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fMmx);
        IEM_MC_ARG(uint64_t *,              puDst, 0);
        IEM_MC_ARG(uint64_t const *,        puSrc, 1);
        IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_REF_MREG_U64(puDst,          IEM_GET_MODRM_REG_8(bRm));
        IEM_MC_REF_MREG_U64_CONST(puSrc,    IEM_GET_MODRM_RM_8(bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU64, puDst, puSrc);
        IEM_MC_MODIFIED_MREG_BY_REF(puDst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * MMX, [mem32].
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(uint64_t *,                  puDst,       0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_ARG_LOCAL_REF(uint64_t const *,  puSrc, uSrc, 1);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fMmx);
        IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
        IEM_MC_FETCH_MEM_U32_ZX_U64(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_REF_MREG_U64(puDst, IEM_GET_MODRM_REG_8(bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU64, puDst, puSrc);
        IEM_MC_MODIFIED_MREG_BY_REF(puDst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * Common worker for SSE instructions on the forms:
 *      pxxxx xmm1, xmm2/mem128
 *
 * The 2nd operand is the first half of a register, which in the memory case
 * 128-bit aligned 64-bit or 128-bit memory accessed for SSE.
 *
 * Exceptions type 4.
 */
FNIEMOP_DEF_1(iemOpCommonSse_LowLow_To_Full, PFNIEMAIMPLMEDIAOPTF2U128, pfnU128)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM, XMM.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_ARG(PRTUINT128U,             puDst, 0);
        IEM_MC_ARG(PCRTUINT128U,            puSrc, 1);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();
        IEM_MC_REF_XREG_U128(puDst,         IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(puSrc,   IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU128, puDst, puSrc);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM, [mem128].
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(PRTUINT128U,             puDst,       0);
        IEM_MC_LOCAL(RTUINT128U,            uSrc);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,  puSrc, uSrc, 1);
        IEM_MC_LOCAL(RTGCPTR,               GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        /** @todo Most CPUs probably only read the low qword. We read everything to
         *        make sure we apply segmentation and alignment checks correctly.
         *        When we have time, it would be interesting to explore what real
         *        CPUs actually does and whether it will do a TLB load for the high
         *        part or skip any associated \#PF. Ditto for segmentation \#GPs. */
        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();
        IEM_MC_REF_XREG_U128(puDst,         IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU128, puDst, puSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * Common worker for SSE2 instructions on the forms:
 *      pxxxx xmm1, xmm2/mem128
 *
 * The 2nd operand is the first half of a register, which in the memory case
 * 128-bit aligned 64-bit or 128-bit memory accessed for SSE.
 *
 * Exceptions type 4.
 */
FNIEMOP_DEF_1(iemOpCommonSse2_LowLow_To_Full, PFNIEMAIMPLMEDIAOPTF2U128, pfnU128)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM, XMM.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_ARG(PRTUINT128U,             puDst, 0);
        IEM_MC_ARG(PCRTUINT128U,            puSrc, 1);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();
        IEM_MC_REF_XREG_U128(puDst,         IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(puSrc,   IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU128, puDst, puSrc);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM, [mem128].
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(PRTUINT128U,             puDst,       0);
        IEM_MC_LOCAL(RTUINT128U,            uSrc);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,  puSrc, uSrc, 1);
        IEM_MC_LOCAL(RTGCPTR,               GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        /** @todo Most CPUs probably only read the low qword. We read everything to
         *        make sure we apply segmentation and alignment checks correctly.
         *        When we have time, it would be interesting to explore what real
         *        CPUs actually does and whether it will do a TLB load for the high
         *        part or skip any associated \#PF. Ditto for segmentation \#GPs. */
        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();
        IEM_MC_REF_XREG_U128(puDst,         IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU128, puDst, puSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * Common worker for MMX instructions on the form:
 *      pxxxx mm1, mm2/mem64
 *
 * The 2nd operand is the second half of a register, which in the memory case
 * means a 64-bit memory access for MMX.
 */
FNIEMOP_DEF_1(iemOpCommonMmx_HighHigh_To_Full, PFNIEMAIMPLMEDIAOPTF2U64, pfnU64)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * MMX, MMX.
         */
        /** @todo testcase: REX.B / REX.R and MMX register indexing. Ignored? */
        /** @todo testcase: REX.B / REX.R and segment register indexing. Ignored? */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fMmx);
        IEM_MC_ARG(uint64_t *,              puDst, 0);
        IEM_MC_ARG(uint64_t const *,        puSrc, 1);
        IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_REF_MREG_U64(puDst,          IEM_GET_MODRM_REG_8(bRm));
        IEM_MC_REF_MREG_U64_CONST(puSrc,    IEM_GET_MODRM_RM_8(bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU64, puDst, puSrc);
        IEM_MC_MODIFIED_MREG_BY_REF(puDst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * MMX, [mem64].
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(uint64_t *,                  puDst,       0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_ARG_LOCAL_REF(uint64_t const *,  puSrc, uSrc, 1);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fMmx);
        IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
        IEM_MC_FETCH_MEM_U64(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc); /* intel docs this to be full 64-bit read */

        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_REF_MREG_U64(puDst,              IEM_GET_MODRM_REG_8(bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU64, puDst, puSrc);
        IEM_MC_MODIFIED_MREG_BY_REF(puDst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * Common worker for SSE instructions on the form:
 *      pxxxx xmm1, xmm2/mem128
 *
 * The 2nd operand is the second half of a register, which for SSE a 128-bit
 * aligned access where it may read the full 128 bits or only the upper 64 bits.
 *
 * Exceptions type 4.
 */
FNIEMOP_DEF_1(iemOpCommonSse_HighHigh_To_Full, PFNIEMAIMPLMEDIAOPTF2U128, pfnU128)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM, XMM.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_ARG(PRTUINT128U,             puDst, 0);
        IEM_MC_ARG(PCRTUINT128U,            puSrc, 1);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst,         IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(puSrc,   IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU128, puDst, puSrc);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM, [mem128].
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(PRTUINT128U,                 puDst,       0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,      puSrc, uSrc, 1);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        /** @todo Most CPUs probably only read the high qword. We read everything to
         *        make sure we apply segmentation and alignment checks correctly.
         *        When we have time, it would be interesting to explore what real
         *        CPUs actually does and whether it will do a TLB load for the lower
         *        part or skip any associated \#PF. */
        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU128, puDst, puSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * Common worker for SSE instructions on the forms:
 *      pxxs       xmm1, xmm2/mem128
 *
 * Proper alignment of the 128-bit operand is enforced.
 * Exceptions type 2. SSE cpuid checks.
 *
 * @sa iemOpCommonSse41_FullFull_To_Full, iemOpCommonSse2_FullFull_To_Full
 */
FNIEMOP_DEF_1(iemOpCommonSseFp_FullFull_To_Full, PFNIEMAIMPLFPSSEF2U128, pfnU128)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM128, XMM128.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_LOCAL(X86XMMREG,             SseRes);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    pSseRes,        SseRes,     0);
        IEM_MC_ARG(PCX86XMMREG,             pSrc1,                      1);
        IEM_MC_ARG(PCX86XMMREG,             pSrc2,                      2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_XMM_CONST(pSrc1, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_XMM_CONST(pSrc2, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_SSE_AIMPL_3(pfnU128, pSseRes, pSrc1, pSrc2);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), SseRes);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM128, [mem128].
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(X86XMMREG,             SseRes);
        IEM_MC_LOCAL(X86XMMREG,             uSrc2);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    pSseRes,        SseRes,     0);
        IEM_MC_ARG(PCX86XMMREG,             pSrc1,                      1);
        IEM_MC_ARG_LOCAL_REF(PCX86XMMREG,   pSrc2, uSrc2,               2);
        IEM_MC_LOCAL(RTGCPTR, GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_XMM_ALIGN_SSE(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_XMM_CONST(pSrc1, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_CALL_SSE_AIMPL_3(pfnU128, pSseRes, pSrc1, pSrc2);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), SseRes);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * A body preprocessor variant of iemOpCommonSseFp_FullFull_To_Full in order
 * to support native emitters for certain instructions.
 */
#define SSE_FP_BODY_FullFull_To_Full(a_Ins, a_pImplExpr, a_fRegNativeArchs, a_fMemNativeArchs) \
        PFNIEMAIMPLFPSSEF2U128 const pfnU128 = (a_pImplExpr); \
        uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm); \
        if (IEM_IS_MODRM_REG_MODE(bRm)) \
        { \
            /* \
             * XMM, XMM. \
             */ \
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0); \
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse); \
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT(); \
            IEM_MC_PREPARE_SSE_USAGE(); \
            IEM_MC_NATIVE_IF(a_fRegNativeArchs) { \
                IEM_MC_LIVENESS_MXCSR_MODIFY(); \
                IEM_MC_NATIVE_EMIT_2_EX(RT_CONCAT3(iemNativeEmit_,a_Ins,_rr_u128), IEM_GET_MODRM_REG(pVCpu, bRm), IEM_GET_MODRM_RM(pVCpu, bRm)); \
            } IEM_MC_NATIVE_ELSE() { \
                IEM_MC_LOCAL(X86XMMREG,             SseRes); \
                IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    pSseRes,        SseRes,     0); \
                IEM_MC_ARG(PCX86XMMREG,             pSrc1,                      1); \
                IEM_MC_REF_XREG_XMM_CONST(pSrc1, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_ARG(PCX86XMMREG,             pSrc2,                      2); \
                IEM_MC_REF_XREG_XMM_CONST(pSrc2, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_CALL_SSE_AIMPL_3(pfnU128, pSseRes, pSrc1, pSrc2); \
                IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), SseRes); \
            } IEM_MC_NATIVE_ENDIF(); \
            IEM_MC_ADVANCE_RIP_AND_FINISH(); \
            IEM_MC_END(); \
        } \
        else \
        { \
            /* \
             * XMM, [mem128]. \
             */ \
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0); \
            IEM_MC_LOCAL(X86XMMREG,                 uSrc2); \
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc); \
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0); \
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse); \
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT(); \
            IEM_MC_FETCH_MEM_XMM_ALIGN_SSE(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc); \
            IEM_MC_PREPARE_SSE_USAGE(); \
            IEM_MC_NATIVE_IF(a_fRegNativeArchs) { \
                IEM_MC_LIVENESS_MXCSR_MODIFY(); \
                IEM_MC_NATIVE_EMIT_2_EX(RT_CONCAT3(iemNativeEmit_,a_Ins,_rv_u128), IEM_GET_MODRM_REG(pVCpu, bRm), uSrc2); \
            } IEM_MC_NATIVE_ELSE() { \
                IEM_MC_LOCAL(X86XMMREG,             SseRes); \
                IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    pSseRes,        SseRes,     0); \
                IEM_MC_ARG(PCX86XMMREG,             pSrc1,                      1); \
                IEM_MC_ARG_LOCAL_REF(PCX86XMMREG,   pSrc2, uSrc2,               2); \
                IEM_MC_REF_XREG_XMM_CONST(pSrc1, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_CALL_SSE_AIMPL_3(pfnU128, pSseRes, pSrc1, pSrc2); \
                IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), SseRes); \
            } IEM_MC_NATIVE_ENDIF(); \
            IEM_MC_ADVANCE_RIP_AND_FINISH(); \
            IEM_MC_END(); \
        } void(0)


/**
 * Common worker for SSE instructions on the forms:
 *      pxxs       xmm1, xmm2/mem32
 *
 * Proper alignment of the 128-bit operand is enforced.
 * Exceptions type 3. SSE cpuid checks.
 *
 * @sa iemOpCommonSse41_FullFull_To_Full, iemOpCommonSse2_FullFull_To_Full
 */
FNIEMOP_DEF_1(iemOpCommonSseFp_FullR32_To_Full, PFNIEMAIMPLFPSSEF2U128R32, pfnU128_R32)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM128, XMM32.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_LOCAL(X86XMMREG,             SseRes);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    pSseRes,        SseRes,     0);
        IEM_MC_ARG(PCX86XMMREG,             pSrc1,                      1);
        IEM_MC_ARG(PCRTFLOAT32U,            pSrc2,                      2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_XMM_CONST(pSrc1, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_R32_CONST(pSrc2, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_SSE_AIMPL_3(pfnU128_R32, pSseRes, pSrc1, pSrc2);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), SseRes);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM128, [mem32].
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(X86XMMREG,             SseRes);
        IEM_MC_LOCAL(RTFLOAT32U,            r32Src2);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    pSseRes,        SseRes,     0);
        IEM_MC_ARG(PCX86XMMREG,             pSrc1,                      1);
        IEM_MC_ARG_LOCAL_REF(PCRTFLOAT32U,  pr32Src2,       r32Src2,    2);
        IEM_MC_LOCAL(RTGCPTR, GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_R32(r32Src2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_XMM_CONST(pSrc1, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_CALL_SSE_AIMPL_3(pfnU128_R32, pSseRes, pSrc1, pr32Src2);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), SseRes);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * Common worker for SSE2 instructions on the forms:
 *      pxxd       xmm1, xmm2/mem128
 *
 * Proper alignment of the 128-bit operand is enforced.
 * Exceptions type 2. SSE cpuid checks.
 *
 * @sa iemOpCommonSseFp_FullFull_To_Full
 */
FNIEMOP_DEF_1(iemOpCommonSse2Fp_FullFull_To_Full, PFNIEMAIMPLFPSSEF2U128, pfnU128)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM128, XMM128.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_LOCAL(X86XMMREG,             SseRes);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    pSseRes,        SseRes,     0);
        IEM_MC_ARG(PCX86XMMREG,             pSrc1,                      1);
        IEM_MC_ARG(PCX86XMMREG,             pSrc2,                      2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_XMM_CONST(pSrc1, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_XMM_CONST(pSrc2, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_SSE_AIMPL_3(pfnU128, pSseRes, pSrc1, pSrc2);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), SseRes);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM128, [mem128].
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(X86XMMREG,             SseRes);
        IEM_MC_LOCAL(X86XMMREG,             uSrc2);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    pSseRes,        SseRes,     0);
        IEM_MC_ARG(PCX86XMMREG,             pSrc1,                      1);
        IEM_MC_ARG_LOCAL_REF(PCX86XMMREG,   pSrc2, uSrc2,               2);
        IEM_MC_LOCAL(RTGCPTR, GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_XMM_ALIGN_SSE(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_XMM_CONST(pSrc1, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_CALL_SSE_AIMPL_3(pfnU128, pSseRes, pSrc1, pSrc2);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), SseRes);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * Common worker for SSE2 instructions on the forms:
 *      pxxs       xmm1, xmm2/mem64
 *
 * Proper alignment of the 128-bit operand is enforced.
 * Exceptions type 3. SSE2 cpuid checks.
 *
 * @sa iemOpCommonSse41_FullFull_To_Full, iemOpCommonSse2_FullFull_To_Full
 */
FNIEMOP_DEF_1(iemOpCommonSse2Fp_FullR64_To_Full, PFNIEMAIMPLFPSSEF2U128R64, pfnU128_R64)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM, XMM.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_LOCAL(X86XMMREG,             SseRes);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    pSseRes,        SseRes,     0);
        IEM_MC_ARG(PCX86XMMREG,             pSrc1,                      1);
        IEM_MC_ARG(PCRTFLOAT64U,            pSrc2,                      2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_XMM_CONST(pSrc1, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_R64_CONST(pSrc2, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_SSE_AIMPL_3(pfnU128_R64, pSseRes, pSrc1, pSrc2);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), SseRes);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM, [mem64].
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(X86XMMREG,             SseRes);
        IEM_MC_LOCAL(RTFLOAT64U,            r64Src2);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    pSseRes,        SseRes,     0);
        IEM_MC_ARG(PCX86XMMREG,             pSrc1,                      1);
        IEM_MC_ARG_LOCAL_REF(PCRTFLOAT64U,  pr64Src2,       r64Src2,    2);
        IEM_MC_LOCAL(RTGCPTR, GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_R64(r64Src2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_XMM_CONST(pSrc1, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_CALL_SSE_AIMPL_3(pfnU128_R64, pSseRes, pSrc1, pr64Src2);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), SseRes);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * Common worker for SSE2 instructions on the form:
 *      pxxxx xmm1, xmm2/mem128
 *
 * The 2nd operand is the second half of a register, which for SSE a 128-bit
 * aligned access where it may read the full 128 bits or only the upper 64 bits.
 *
 * Exceptions type 4.
 */
FNIEMOP_DEF_1(iemOpCommonSse2_HighHigh_To_Full, PFNIEMAIMPLMEDIAOPTF2U128, pfnU128)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM, XMM.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_ARG(PRTUINT128U,             puDst, 0);
        IEM_MC_ARG(PCRTUINT128U,            puSrc, 1);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst,         IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(puSrc,   IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU128, puDst, puSrc);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM, [mem128].
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(PRTUINT128U,                 puDst,       0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,      puSrc, uSrc, 1);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        /** @todo Most CPUs probably only read the high qword. We read everything to
         *        make sure we apply segmentation and alignment checks correctly.
         *        When we have time, it would be interesting to explore what real
         *        CPUs actually does and whether it will do a TLB load for the lower
         *        part or skip any associated \#PF. */
        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU128, puDst, puSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * Common worker for SSE3 instructions on the forms:
 *      hxxx      xmm1, xmm2/mem128
 *
 * Proper alignment of the 128-bit operand is enforced.
 * Exceptions type 2. SSE3 cpuid checks.
 *
 * @sa iemOpCommonSse41_FullFull_To_Full, iemOpCommonSse2_FullFull_To_Full
 */
FNIEMOP_DEF_1(iemOpCommonSse3Fp_FullFull_To_Full, PFNIEMAIMPLFPSSEF2U128, pfnU128)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM, XMM.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse3);
        IEM_MC_LOCAL(X86XMMREG,             SseRes);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    pSseRes,        SseRes,     0);
        IEM_MC_ARG(PCX86XMMREG,             pSrc1,                      1);
        IEM_MC_ARG(PCX86XMMREG,             pSrc2,                      2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_XMM_CONST(pSrc1, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_XMM_CONST(pSrc2, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_SSE_AIMPL_3(pfnU128, pSseRes, pSrc1, pSrc2);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), SseRes);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM, [mem128].
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(X86XMMREG,             SseRes);
        IEM_MC_LOCAL(X86XMMREG,             uSrc2);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    pSseRes,        SseRes,     0);
        IEM_MC_ARG(PCX86XMMREG,             pSrc1,                      1);
        IEM_MC_ARG_LOCAL_REF(PCX86XMMREG,   pSrc2, uSrc2,               2);
        IEM_MC_LOCAL(RTGCPTR, GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse3);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_XMM_ALIGN_SSE(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_XMM_CONST(pSrc1, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_CALL_SSE_AIMPL_3(pfnU128, pSseRes, pSrc1, pSrc2);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), SseRes);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode 0x0f 0x00 /0. */
FNIEMOPRM_DEF(iemOp_Grp6_sldt)
{
    IEMOP_MNEMONIC(sldt, "sldt Rv/Mw");
    IEMOP_HLP_MIN_286();
    IEMOP_HLP_NO_REAL_OR_V86_MODE();

    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        IEMOP_HLP_DECODED_NL_1(OP_SLDT, IEMOPFORM_M_REG, OP_PARM_Ew, DISOPTYPE_DANGEROUS | DISOPTYPE_PRIVILEGED_NOTRAP);
        IEM_MC_DEFER_TO_CIMPL_2_RET(IEM_CIMPL_F_VMEXIT, RT_BIT_64(kIemNativeGstReg_GprFirst + IEM_GET_MODRM_RM(pVCpu, bRm)),
                                    iemCImpl_sldt_reg, IEM_GET_MODRM_RM(pVCpu, bRm), pVCpu->iem.s.enmEffOpSize);
    }

    /* Ignore operand size here, memory refs are always 16-bit. */
    IEM_MC_BEGIN(IEM_MC_F_MIN_286, 0);
    IEM_MC_ARG(RTGCPTR,         GCPtrEffDst,                            1);
    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
    IEMOP_HLP_DECODED_NL_1(OP_SLDT, IEMOPFORM_M_MEM, OP_PARM_Ew, DISOPTYPE_DANGEROUS | DISOPTYPE_PRIVILEGED_NOTRAP);
    IEM_MC_ARG_CONST(uint8_t,   iEffSeg, /*=*/ pVCpu->iem.s.iEffSeg,    0);
    IEM_MC_CALL_CIMPL_2(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_sldt_mem, iEffSeg, GCPtrEffDst);
    IEM_MC_END();
}


/** Opcode 0x0f 0x00 /1. */
FNIEMOPRM_DEF(iemOp_Grp6_str)
{
    IEMOP_MNEMONIC(str, "str Rv/Mw");
    IEMOP_HLP_MIN_286();
    IEMOP_HLP_NO_REAL_OR_V86_MODE();


    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        IEMOP_HLP_DECODED_NL_1(OP_STR, IEMOPFORM_M_REG, OP_PARM_Ew, DISOPTYPE_DANGEROUS | DISOPTYPE_PRIVILEGED_NOTRAP);
        IEM_MC_DEFER_TO_CIMPL_2_RET(IEM_CIMPL_F_VMEXIT, RT_BIT_64(kIemNativeGstReg_GprFirst + IEM_GET_MODRM_RM(pVCpu, bRm)),
                                    iemCImpl_str_reg, IEM_GET_MODRM_RM(pVCpu, bRm), pVCpu->iem.s.enmEffOpSize);
    }

    /* Ignore operand size here, memory refs are always 16-bit. */
    IEM_MC_BEGIN(IEM_MC_F_MIN_286, 0);
    IEM_MC_ARG(RTGCPTR,         GCPtrEffDst,                            1);
    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
    IEMOP_HLP_DECODED_NL_1(OP_STR, IEMOPFORM_M_MEM, OP_PARM_Ew, DISOPTYPE_DANGEROUS | DISOPTYPE_PRIVILEGED_NOTRAP);
    IEM_MC_ARG_CONST(uint8_t,   iEffSeg, /*=*/ pVCpu->iem.s.iEffSeg,    0);
    IEM_MC_CALL_CIMPL_2(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_str_mem, iEffSeg, GCPtrEffDst);
    IEM_MC_END();
}


/** Opcode 0x0f 0x00 /2. */
FNIEMOPRM_DEF(iemOp_Grp6_lldt)
{
    IEMOP_MNEMONIC(lldt, "lldt Ew");
    IEMOP_HLP_MIN_286();
    IEMOP_HLP_NO_REAL_OR_V86_MODE();

    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_286, 0);
        IEMOP_HLP_DECODED_NL_1(OP_LLDT, IEMOPFORM_M_REG, OP_PARM_Ew, DISOPTYPE_DANGEROUS);
        IEM_MC_ARG(uint16_t, u16Sel, 0);
        IEM_MC_FETCH_GREG_U16(u16Sel, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_CIMPL_1(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_lldt, u16Sel);
        IEM_MC_END();
    }
    else
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_286, 0);
        IEM_MC_ARG(uint16_t, u16Sel, 0);
        IEM_MC_LOCAL(RTGCPTR, GCPtrEffSrc);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DECODED_NL_1(OP_LLDT, IEMOPFORM_M_MEM, OP_PARM_Ew, DISOPTYPE_DANGEROUS);
        IEM_MC_RAISE_GP0_IF_CPL_NOT_ZERO(); /** @todo test order */
        IEM_MC_FETCH_MEM_U16(u16Sel, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_CALL_CIMPL_1(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_lldt, u16Sel);
        IEM_MC_END();
    }
}


/** Opcode 0x0f 0x00 /3. */
FNIEMOPRM_DEF(iemOp_Grp6_ltr)
{
    IEMOP_MNEMONIC(ltr, "ltr Ew");
    IEMOP_HLP_MIN_286();
    IEMOP_HLP_NO_REAL_OR_V86_MODE();

    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_286, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_ARG(uint16_t, u16Sel, 0);
        IEM_MC_FETCH_GREG_U16(u16Sel, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_CIMPL_1(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_ltr, u16Sel);
        IEM_MC_END();
    }
    else
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_286, 0);
        IEM_MC_ARG(uint16_t, u16Sel, 0);
        IEM_MC_LOCAL(RTGCPTR, GCPtrEffSrc);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_RAISE_GP0_IF_CPL_NOT_ZERO(); /** @todo test order */
        IEM_MC_FETCH_MEM_U16(u16Sel, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_CALL_CIMPL_1(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_ltr, u16Sel);
        IEM_MC_END();
    }
}


/* Need to associate flag info with the blocks, so duplicate the code. */
#define IEMOP_BODY_GRP6_VERX(bRm, fWrite) \
        IEMOP_HLP_MIN_286(); \
        IEMOP_HLP_NO_REAL_OR_V86_MODE(); \
        \
        if (IEM_IS_MODRM_REG_MODE(bRm)) \
        { \
            IEM_MC_BEGIN(IEM_MC_F_MIN_286, 0); \
            IEMOP_HLP_DECODED_NL_1(fWrite ? OP_VERW : OP_VERR, IEMOPFORM_M_MEM, OP_PARM_Ew, DISOPTYPE_DANGEROUS | DISOPTYPE_PRIVILEGED_NOTRAP); \
            IEM_MC_ARG(uint16_t,    u16Sel,            0); \
            IEM_MC_ARG_CONST(bool,  fWriteArg, fWrite, 1); \
            IEM_MC_FETCH_GREG_U16(u16Sel, IEM_GET_MODRM_RM(pVCpu, bRm)); \
            IEM_MC_CALL_CIMPL_2(IEM_CIMPL_F_STATUS_FLAGS, 0, iemCImpl_VerX, u16Sel, fWriteArg); \
            IEM_MC_END(); \
        } \
        else \
        { \
            IEM_MC_BEGIN(IEM_MC_F_MIN_286, 0); \
            IEM_MC_ARG(uint16_t,    u16Sel,            0); \
            IEM_MC_ARG_CONST(bool,  fWriteArg, fWrite, 1); \
            IEM_MC_LOCAL(RTGCPTR, GCPtrEffSrc); \
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0); \
            IEMOP_HLP_DECODED_NL_1(fWrite ? OP_VERW : OP_VERR, IEMOPFORM_M_MEM, OP_PARM_Ew, DISOPTYPE_DANGEROUS | DISOPTYPE_PRIVILEGED_NOTRAP); \
            IEM_MC_FETCH_MEM_U16(u16Sel, pVCpu->iem.s.iEffSeg, GCPtrEffSrc); \
            IEM_MC_CALL_CIMPL_2(IEM_CIMPL_F_STATUS_FLAGS, 0, iemCImpl_VerX, u16Sel, fWriteArg); \
            IEM_MC_END(); \
        } (void)0

/**
 * @opmaps      grp6
 * @opcode      /4
 * @opflmodify  zf
 */
FNIEMOPRM_DEF(iemOp_Grp6_verr)
{
    IEMOP_MNEMONIC(verr, "verr Ew");
    IEMOP_BODY_GRP6_VERX(bRm, false);
}


/**
 * @opmaps      grp6
 * @opcode      /5
 * @opflmodify  zf
 */
FNIEMOPRM_DEF(iemOp_Grp6_verw)
{
    IEMOP_MNEMONIC(verw, "verw Ew");
    IEMOP_BODY_GRP6_VERX(bRm, true);
}


/**
 * Group 6 jump table.
 */
IEM_STATIC const PFNIEMOPRM g_apfnGroup6[8] =
{
    iemOp_Grp6_sldt,
    iemOp_Grp6_str,
    iemOp_Grp6_lldt,
    iemOp_Grp6_ltr,
    iemOp_Grp6_verr,
    iemOp_Grp6_verw,
    iemOp_InvalidWithRM,
    iemOp_InvalidWithRM
};

/** Opcode 0x0f 0x00. */
FNIEMOP_DEF(iemOp_Grp6)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    return FNIEMOP_CALL_1(g_apfnGroup6[IEM_GET_MODRM_REG_8(bRm)], bRm);
}


/** Opcode 0x0f 0x01 /0. */
FNIEMOP_DEF_1(iemOp_Grp7_sgdt, uint8_t, bRm)
{
    IEMOP_MNEMONIC(sgdt, "sgdt Ms");
    IEMOP_HLP_MIN_286();
    IEMOP_HLP_64BIT_OP_SIZE();
    IEM_MC_BEGIN(IEM_MC_F_MIN_286, 0);
    IEM_MC_ARG(RTGCPTR,         GCPtrEffSrc,                            1);
    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_ARG_CONST(uint8_t,   iEffSeg, /*=*/ pVCpu->iem.s.iEffSeg,    0);
    IEM_MC_CALL_CIMPL_2(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_sgdt, iEffSeg, GCPtrEffSrc);
    IEM_MC_END();
}


/** Opcode 0x0f 0x01 /0. */
FNIEMOP_DEF(iemOp_Grp7_vmcall)
{
    IEMOP_MNEMONIC(vmcall, "vmcall");
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); /** @todo check prefix effect on the VMX instructions. ASSUMING no lock for now. */

    /* Note! We do not check any CPUMFEATURES::fSvm here as we (GIM) generally
             want all hypercalls regardless of instruction used, and if a
             hypercall isn't handled by GIM or HMSvm will raise an #UD.
             (NEM/win makes ASSUMPTIONS about this behavior.)  */
    IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_RFLAGS | IEM_CIMPL_F_VMEXIT | IEM_CIMPL_F_END_TB, 0, iemCImpl_vmcall);
}


/** Opcode 0x0f 0x01 /0. */
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
FNIEMOP_DEF(iemOp_Grp7_vmlaunch)
{
    IEMOP_MNEMONIC(vmlaunch, "vmlaunch");
    IEMOP_HLP_IN_VMX_OPERATION("vmlaunch", kVmxVDiag_Vmentry);
    IEMOP_HLP_VMX_INSTR("vmlaunch", kVmxVDiag_Vmentry);
    IEMOP_HLP_DONE_DECODING();
    IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_BRANCH_INDIRECT | IEM_CIMPL_F_BRANCH_FAR
                                | IEM_CIMPL_F_MODE | IEM_CIMPL_F_RFLAGS | IEM_CIMPL_F_VMEXIT | IEM_CIMPL_F_END_TB, 0,
                                iemCImpl_vmlaunch);
}
#else
FNIEMOP_DEF(iemOp_Grp7_vmlaunch)
{
    IEMOP_BITCH_ABOUT_STUB();
    IEMOP_RAISE_INVALID_OPCODE_RET();
}
#endif


/** Opcode 0x0f 0x01 /0. */
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
FNIEMOP_DEF(iemOp_Grp7_vmresume)
{
    IEMOP_MNEMONIC(vmresume, "vmresume");
    IEMOP_HLP_IN_VMX_OPERATION("vmresume", kVmxVDiag_Vmentry);
    IEMOP_HLP_VMX_INSTR("vmresume", kVmxVDiag_Vmentry);
    IEMOP_HLP_DONE_DECODING();
    IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_BRANCH_INDIRECT | IEM_CIMPL_F_BRANCH_FAR
                                | IEM_CIMPL_F_MODE | IEM_CIMPL_F_RFLAGS | IEM_CIMPL_F_VMEXIT | IEM_CIMPL_F_END_TB, 0,
                                iemCImpl_vmresume);
}
#else
FNIEMOP_DEF(iemOp_Grp7_vmresume)
{
    IEMOP_BITCH_ABOUT_STUB();
    IEMOP_RAISE_INVALID_OPCODE_RET();
}
#endif


/** Opcode 0x0f 0x01 /0. */
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
FNIEMOP_DEF(iemOp_Grp7_vmxoff)
{
    IEMOP_MNEMONIC(vmxoff, "vmxoff");
    IEMOP_HLP_IN_VMX_OPERATION("vmxoff", kVmxVDiag_Vmxoff);
    IEMOP_HLP_VMX_INSTR("vmxoff", kVmxVDiag_Vmxoff);
    IEMOP_HLP_DONE_DECODING();
    IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_vmxoff);
}
#else
FNIEMOP_DEF(iemOp_Grp7_vmxoff)
{
    IEMOP_BITCH_ABOUT_STUB();
    IEMOP_RAISE_INVALID_OPCODE_RET();
}
#endif


/** Opcode 0x0f 0x01 /1. */
FNIEMOP_DEF_1(iemOp_Grp7_sidt, uint8_t, bRm)
{
    IEMOP_MNEMONIC(sidt, "sidt Ms");
    IEMOP_HLP_MIN_286();
    IEMOP_HLP_64BIT_OP_SIZE();
    IEM_MC_BEGIN(IEM_MC_F_MIN_286, 0);
    IEM_MC_ARG(RTGCPTR,         GCPtrEffSrc,                            1);
    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_ARG_CONST(uint8_t,   iEffSeg, /*=*/ pVCpu->iem.s.iEffSeg,    0);
    IEM_MC_CALL_CIMPL_2(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_sidt, iEffSeg, GCPtrEffSrc);
    IEM_MC_END();
}


/** Opcode 0x0f 0x01 /1. */
FNIEMOP_DEF(iemOp_Grp7_monitor)
{
    IEMOP_MNEMONIC(monitor, "monitor");
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); /** @todo Verify that monitor is allergic to lock prefixes. */
    IEM_MC_DEFER_TO_CIMPL_1_RET(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_monitor, pVCpu->iem.s.iEffSeg);
}


/** Opcode 0x0f 0x01 /1. */
FNIEMOP_DEF(iemOp_Grp7_mwait)
{
    IEMOP_MNEMONIC(mwait, "mwait"); /** @todo Verify that mwait is allergic to lock prefixes. */
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_END_TB | IEM_CIMPL_F_VMEXIT, 0, iemCImpl_mwait);
}


/** Opcode 0x0f 0x01 /2. */
FNIEMOP_DEF_1(iemOp_Grp7_lgdt, uint8_t, bRm)
{
    IEMOP_MNEMONIC(lgdt, "lgdt");
    IEMOP_HLP_64BIT_OP_SIZE();
    IEM_MC_BEGIN(0, 0);
    IEM_MC_ARG(RTGCPTR,         GCPtrEffSrc,                                    1);
    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_ARG_CONST(uint8_t,   iEffSeg,       /*=*/ pVCpu->iem.s.iEffSeg,      0);
    IEM_MC_ARG_CONST(IEMMODE,   enmEffOpSizeArg,/*=*/pVCpu->iem.s.enmEffOpSize, 2);
    IEM_MC_CALL_CIMPL_3(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_lgdt, iEffSeg, GCPtrEffSrc, enmEffOpSizeArg);
    IEM_MC_END();
}


/** Opcode 0x0f 0x01 0xd0. */
FNIEMOP_DEF(iemOp_Grp7_xgetbv)
{
    IEMOP_MNEMONIC(xgetbv, "xgetbv");
    if (IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fXSaveRstor)
    {
        /** @todo r=ramshankar: We should use
         *        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX and
         *        IEMOP_HLP_DONE_DECODING_NO_SIZE_OP_REPZ_OR_REPNZ_PREFIXES here. */
/** @todo testcase: test prefixes and exceptions. currently not checking for the
 *        OPSIZE one ... */
        IEMOP_HLP_DONE_DECODING_NO_LOCK_REPZ_OR_REPNZ_PREFIXES();
        IEM_MC_DEFER_TO_CIMPL_0_RET(0,
                                      RT_BIT_64(kIemNativeGstReg_GprFirst + X86_GREG_xAX)
                                    | RT_BIT_64(kIemNativeGstReg_GprFirst + X86_GREG_xDX),
                                    iemCImpl_xgetbv);
    }
    IEMOP_RAISE_INVALID_OPCODE_RET();
}


/** Opcode 0x0f 0x01 0xd1. */
FNIEMOP_DEF(iemOp_Grp7_xsetbv)
{
    IEMOP_MNEMONIC(xsetbv, "xsetbv");
    if (IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fXSaveRstor)
    {
        /** @todo r=ramshankar: We should use
         *        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX and
         *        IEMOP_HLP_DONE_DECODING_NO_SIZE_OP_REPZ_OR_REPNZ_PREFIXES here. */
/** @todo testcase: test prefixes and exceptions. currently not checking for the
 *        OPSIZE one ... */
        IEMOP_HLP_DONE_DECODING_NO_LOCK_REPZ_OR_REPNZ_PREFIXES();
        IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_xsetbv);
    }
    IEMOP_RAISE_INVALID_OPCODE_RET();
}


/** Opcode 0x0f 0x01 /3. */
FNIEMOP_DEF_1(iemOp_Grp7_lidt, uint8_t, bRm)
{
    IEMOP_MNEMONIC(lidt, "lidt");
    IEMMODE enmEffOpSize = IEM_IS_64BIT_CODE(pVCpu) ? IEMMODE_64BIT : pVCpu->iem.s.enmEffOpSize;
    IEM_MC_BEGIN(0, 0);
    IEM_MC_ARG(RTGCPTR,         GCPtrEffSrc,                                    1);
    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_ARG_CONST(uint8_t,   iEffSeg,         /*=*/ pVCpu->iem.s.iEffSeg,    0);
    IEM_MC_ARG_CONST(IEMMODE,   enmEffOpSizeArg, /*=*/ enmEffOpSize,            2);
    IEM_MC_CALL_CIMPL_3(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_lidt, iEffSeg, GCPtrEffSrc, enmEffOpSizeArg);
    IEM_MC_END();
}


/** Opcode 0x0f 0x01 0xd8. */
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
FNIEMOP_DEF(iemOp_Grp7_Amd_vmrun)
{
    IEMOP_MNEMONIC(vmrun, "vmrun");
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); /** @todo check prefix effect on the SVM instructions. ASSUMING no lock for now. */
    IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_BRANCH_INDIRECT | IEM_CIMPL_F_BRANCH_FAR
                                | IEM_CIMPL_F_MODE | IEM_CIMPL_F_RFLAGS | IEM_CIMPL_F_VMEXIT | IEM_CIMPL_F_END_TB, 0,
                                iemCImpl_vmrun);
}
#else
FNIEMOP_UD_STUB(iemOp_Grp7_Amd_vmrun);
#endif

/** Opcode 0x0f 0x01 0xd9. */
FNIEMOP_DEF(iemOp_Grp7_Amd_vmmcall)
{
    IEMOP_MNEMONIC(vmmcall, "vmmcall");
    /** @todo r=bird: Table A-8 on page 524 in vol 3 has VMGEXIT for this
     *        opcode sequence when F3 or F2 is used as prefix. So, the assumtion
     *        here cannot be right... */
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); /** @todo check prefix effect on the SVM instructions. ASSUMING no lock for now. */

    /* Note! We do not check any CPUMFEATURES::fSvm here as we (GIM) generally
             want all hypercalls regardless of instruction used, and if a
             hypercall isn't handled by GIM or HMSvm will raise an #UD.
             (NEM/win makes ASSUMPTIONS about this behavior.) */
    IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_VMEXIT | IEM_CIMPL_F_END_TB, 0, iemCImpl_vmmcall);
}

/** Opcode 0x0f 0x01 0xda. */
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
FNIEMOP_DEF(iemOp_Grp7_Amd_vmload)
{
    IEMOP_MNEMONIC(vmload, "vmload");
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); /** @todo check prefix effect on the SVM instructions. ASSUMING no lock for now. */
    IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_vmload);
}
#else
FNIEMOP_UD_STUB(iemOp_Grp7_Amd_vmload);
#endif


/** Opcode 0x0f 0x01 0xdb. */
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
FNIEMOP_DEF(iemOp_Grp7_Amd_vmsave)
{
    IEMOP_MNEMONIC(vmsave, "vmsave");
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); /** @todo check prefix effect on the SVM instructions. ASSUMING no lock for now. */
    IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_vmsave);
}
#else
FNIEMOP_UD_STUB(iemOp_Grp7_Amd_vmsave);
#endif


/** Opcode 0x0f 0x01 0xdc. */
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
FNIEMOP_DEF(iemOp_Grp7_Amd_stgi)
{
    IEMOP_MNEMONIC(stgi, "stgi");
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); /** @todo check prefix effect on the SVM instructions. ASSUMING no lock for now. */
    IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_stgi);
}
#else
FNIEMOP_UD_STUB(iemOp_Grp7_Amd_stgi);
#endif


/** Opcode 0x0f 0x01 0xdd. */
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
FNIEMOP_DEF(iemOp_Grp7_Amd_clgi)
{
    IEMOP_MNEMONIC(clgi, "clgi");
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); /** @todo check prefix effect on the SVM instructions. ASSUMING no lock for now. */
    IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_clgi);
}
#else
FNIEMOP_UD_STUB(iemOp_Grp7_Amd_clgi);
#endif


/** Opcode 0x0f 0x01 0xdf. */
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
FNIEMOP_DEF(iemOp_Grp7_Amd_invlpga)
{
    IEMOP_MNEMONIC(invlpga, "invlpga");
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); /** @todo check prefix effect on the SVM instructions. ASSUMING no lock for now. */
    IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_invlpga);
}
#else
FNIEMOP_UD_STUB(iemOp_Grp7_Amd_invlpga);
#endif


/** Opcode 0x0f 0x01 0xde. */
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
FNIEMOP_DEF(iemOp_Grp7_Amd_skinit)
{
    IEMOP_MNEMONIC(skinit, "skinit");
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); /** @todo check prefix effect on the SVM instructions. ASSUMING no lock for now. */
    IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_skinit);
}
#else
FNIEMOP_UD_STUB(iemOp_Grp7_Amd_skinit);
#endif


/** Opcode 0x0f 0x01 /4. */
FNIEMOP_DEF_1(iemOp_Grp7_smsw, uint8_t, bRm)
{
    IEMOP_MNEMONIC(smsw, "smsw");
    IEMOP_HLP_MIN_286();
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_DEFER_TO_CIMPL_2_RET(IEM_CIMPL_F_VMEXIT, RT_BIT_64(kIemNativeGstReg_GprFirst + IEM_GET_MODRM_RM(pVCpu, bRm)),
                                    iemCImpl_smsw_reg, IEM_GET_MODRM_RM(pVCpu, bRm), pVCpu->iem.s.enmEffOpSize);
    }

    /* Ignore operand size here, memory refs are always 16-bit. */
    IEM_MC_BEGIN(IEM_MC_F_MIN_286, 0);
    IEM_MC_ARG(RTGCPTR,         GCPtrEffDst,                            1);
    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_ARG_CONST(uint8_t,   iEffSeg, /*=*/ pVCpu->iem.s.iEffSeg,    0);
    IEM_MC_CALL_CIMPL_2(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_smsw_mem, iEffSeg, GCPtrEffDst);
    IEM_MC_END();
}


/** Opcode 0x0f 0x01 /6. */
FNIEMOP_DEF_1(iemOp_Grp7_lmsw, uint8_t, bRm)
{
    /* The operand size is effectively ignored, all is 16-bit and only the
       lower 3-bits are used. */
    IEMOP_MNEMONIC(lmsw, "lmsw");
    IEMOP_HLP_MIN_286();
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_286, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_ARG(uint16_t, u16Tmp,                         0);
        IEM_MC_ARG_CONST(RTGCPTR,  GCPtrEffDst, NIL_RTGCPTR, 1);
        IEM_MC_FETCH_GREG_U16(u16Tmp, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_CIMPL_2(IEM_CIMPL_F_MODE | IEM_CIMPL_F_VMEXIT, RT_BIT_64(kIemNativeGstReg_Cr0),
                            iemCImpl_lmsw, u16Tmp, GCPtrEffDst);
        IEM_MC_END();
    }
    else
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_286, 0);
        IEM_MC_ARG(uint16_t, u16Tmp,      0);
        IEM_MC_ARG(RTGCPTR,  GCPtrEffDst, 1);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_FETCH_MEM_U16(u16Tmp, pVCpu->iem.s.iEffSeg, GCPtrEffDst);
        IEM_MC_CALL_CIMPL_2(IEM_CIMPL_F_MODE | IEM_CIMPL_F_VMEXIT, RT_BIT_64(kIemNativeGstReg_Cr0),
                            iemCImpl_lmsw, u16Tmp, GCPtrEffDst);
        IEM_MC_END();
    }
}


/** Opcode 0x0f 0x01 /7. */
FNIEMOP_DEF_1(iemOp_Grp7_invlpg, uint8_t, bRm)
{
    IEMOP_MNEMONIC(invlpg, "invlpg");
    IEMOP_HLP_MIN_486();
    IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
    IEM_MC_ARG(RTGCPTR, GCPtrEffDst, 0);
    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_CALL_CIMPL_1(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_invlpg, GCPtrEffDst);
    IEM_MC_END();
}


/** Opcode 0x0f 0x01 0xf8. */
FNIEMOP_DEF(iemOp_Grp7_swapgs)
{
    IEMOP_MNEMONIC(swapgs, "swapgs");
    IEMOP_HLP_ONLY_64BIT();
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_DEFER_TO_CIMPL_0_RET(0, RT_BIT_64(kIemNativeGstReg_SegBaseFirst + X86_SREG_GS), iemCImpl_swapgs);
}


/** Opcode 0x0f 0x01 0xf9. */
FNIEMOP_DEF(iemOp_Grp7_rdtscp)
{
    IEMOP_MNEMONIC(rdtscp, "rdtscp");
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_VMEXIT,
                                  RT_BIT_64(kIemNativeGstReg_GprFirst + X86_GREG_xAX)
                                | RT_BIT_64(kIemNativeGstReg_GprFirst + X86_GREG_xDX)
                                | RT_BIT_64(kIemNativeGstReg_GprFirst + X86_GREG_xCX),
                                iemCImpl_rdtscp);
}


/**
 * Group 7 jump table, memory variant.
 */
IEM_STATIC const PFNIEMOPRM g_apfnGroup7Mem[8] =
{
    iemOp_Grp7_sgdt,
    iemOp_Grp7_sidt,
    iemOp_Grp7_lgdt,
    iemOp_Grp7_lidt,
    iemOp_Grp7_smsw,
    iemOp_InvalidWithRM,
    iemOp_Grp7_lmsw,
    iemOp_Grp7_invlpg
};


/** Opcode 0x0f 0x01. */
FNIEMOP_DEF(iemOp_Grp7)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_MEM_MODE(bRm))
        return FNIEMOP_CALL_1(g_apfnGroup7Mem[IEM_GET_MODRM_REG_8(bRm)], bRm);

    switch (IEM_GET_MODRM_REG_8(bRm))
    {
        case 0:
            switch (IEM_GET_MODRM_RM_8(bRm))
            {
                case 1: return FNIEMOP_CALL(iemOp_Grp7_vmcall);
                case 2: return FNIEMOP_CALL(iemOp_Grp7_vmlaunch);
                case 3: return FNIEMOP_CALL(iemOp_Grp7_vmresume);
                case 4: return FNIEMOP_CALL(iemOp_Grp7_vmxoff);
            }
            IEMOP_RAISE_INVALID_OPCODE_RET();

        case 1:
            switch (IEM_GET_MODRM_RM_8(bRm))
            {
                case 0: return FNIEMOP_CALL(iemOp_Grp7_monitor);
                case 1: return FNIEMOP_CALL(iemOp_Grp7_mwait);
            }
            IEMOP_RAISE_INVALID_OPCODE_RET();

        case 2:
            switch (IEM_GET_MODRM_RM_8(bRm))
            {
                case 0: return FNIEMOP_CALL(iemOp_Grp7_xgetbv);
                case 1: return FNIEMOP_CALL(iemOp_Grp7_xsetbv);
            }
            IEMOP_RAISE_INVALID_OPCODE_RET();

        case 3:
            switch (IEM_GET_MODRM_RM_8(bRm))
            {
                case 0: return FNIEMOP_CALL(iemOp_Grp7_Amd_vmrun);
                case 1: return FNIEMOP_CALL(iemOp_Grp7_Amd_vmmcall);
                case 2: return FNIEMOP_CALL(iemOp_Grp7_Amd_vmload);
                case 3: return FNIEMOP_CALL(iemOp_Grp7_Amd_vmsave);
                case 4: return FNIEMOP_CALL(iemOp_Grp7_Amd_stgi);
                case 5: return FNIEMOP_CALL(iemOp_Grp7_Amd_clgi);
                case 6: return FNIEMOP_CALL(iemOp_Grp7_Amd_skinit);
                case 7: return FNIEMOP_CALL(iemOp_Grp7_Amd_invlpga);
                IEM_NOT_REACHED_DEFAULT_CASE_RET();
            }

        case 4:
            return FNIEMOP_CALL_1(iemOp_Grp7_smsw, bRm);

        case 5:
            IEMOP_RAISE_INVALID_OPCODE_RET();

        case 6:
            return FNIEMOP_CALL_1(iemOp_Grp7_lmsw, bRm);

        case 7:
            switch (IEM_GET_MODRM_RM_8(bRm))
            {
                case 0: return FNIEMOP_CALL(iemOp_Grp7_swapgs);
                case 1: return FNIEMOP_CALL(iemOp_Grp7_rdtscp);
            }
            IEMOP_RAISE_INVALID_OPCODE_RET();

        IEM_NOT_REACHED_DEFAULT_CASE_RET();
    }
}

FNIEMOP_DEF_1(iemOpCommonLarLsl_Gv_Ew, bool, fIsLar)
{
    IEMOP_HLP_NO_REAL_OR_V86_MODE();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);

    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        switch (pVCpu->iem.s.enmEffOpSize)
        {
            case IEMMODE_16BIT:
                IEM_MC_BEGIN(0, 0);
                IEMOP_HLP_DECODED_NL_2(fIsLar ? OP_LAR : OP_LSL, IEMOPFORM_RM_REG, OP_PARM_Gv, OP_PARM_Ew, DISOPTYPE_DANGEROUS | DISOPTYPE_PRIVILEGED_NOTRAP);
                IEM_MC_ARG(uint16_t *,  pu16Dst,           0);
                IEM_MC_ARG(uint16_t,    u16Sel,            1);
                IEM_MC_ARG_CONST(bool,  fIsLarArg, fIsLar, 2);

                IEM_MC_FETCH_GREG_U16(u16Sel, IEM_GET_MODRM_RM(pVCpu, bRm));
                IEM_MC_REF_GREG_U16(pu16Dst, IEM_GET_MODRM_REG(pVCpu, bRm));
                IEM_MC_CALL_CIMPL_3(IEM_CIMPL_F_STATUS_FLAGS, RT_BIT_64(kIemNativeGstReg_GprFirst + IEM_GET_MODRM_REG(pVCpu, bRm)),
                                    iemCImpl_LarLsl_u16, pu16Dst, u16Sel, fIsLarArg);

                IEM_MC_END();
                break;

            case IEMMODE_32BIT:
            case IEMMODE_64BIT:
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
                IEMOP_HLP_DECODED_NL_2(fIsLar ? OP_LAR : OP_LSL, IEMOPFORM_RM_REG, OP_PARM_Gv, OP_PARM_Ew, DISOPTYPE_DANGEROUS | DISOPTYPE_PRIVILEGED_NOTRAP);
                IEM_MC_ARG(uint64_t *,  pu64Dst,           0);
                IEM_MC_ARG(uint16_t,    u16Sel,            1);
                IEM_MC_ARG_CONST(bool,  fIsLarArg, fIsLar, 2);

                IEM_MC_FETCH_GREG_U16(u16Sel, IEM_GET_MODRM_RM(pVCpu, bRm));
                IEM_MC_REF_GREG_U64(pu64Dst, IEM_GET_MODRM_REG(pVCpu, bRm));
                IEM_MC_CALL_CIMPL_3(IEM_CIMPL_F_STATUS_FLAGS, RT_BIT_64(kIemNativeGstReg_GprFirst + IEM_GET_MODRM_REG(pVCpu, bRm)),
                                    iemCImpl_LarLsl_u64, pu64Dst, u16Sel, fIsLarArg);

                IEM_MC_END();
                break;

            IEM_NOT_REACHED_DEFAULT_CASE_RET();
        }
    }
    else
    {
        switch (pVCpu->iem.s.enmEffOpSize)
        {
            case IEMMODE_16BIT:
                IEM_MC_BEGIN(0, 0);
                IEM_MC_ARG(uint16_t *,  pu16Dst,           0);
                IEM_MC_ARG(uint16_t,    u16Sel,            1);
                IEM_MC_ARG_CONST(bool,  fIsLarArg, fIsLar, 2);
                IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc);

                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
                IEMOP_HLP_DECODED_NL_2(fIsLar ? OP_LAR : OP_LSL, IEMOPFORM_RM_MEM, OP_PARM_Gv, OP_PARM_Ew, DISOPTYPE_DANGEROUS | DISOPTYPE_PRIVILEGED_NOTRAP);

                IEM_MC_FETCH_MEM_U16(u16Sel, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
                IEM_MC_REF_GREG_U16(pu16Dst, IEM_GET_MODRM_REG(pVCpu, bRm));
                IEM_MC_CALL_CIMPL_3(IEM_CIMPL_F_STATUS_FLAGS, RT_BIT_64(kIemNativeGstReg_GprFirst + IEM_GET_MODRM_REG(pVCpu, bRm)),
                                    iemCImpl_LarLsl_u16, pu16Dst, u16Sel, fIsLarArg);

                IEM_MC_END();
                break;

            case IEMMODE_32BIT:
            case IEMMODE_64BIT:
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
                IEM_MC_ARG(uint64_t *,  pu64Dst,           0);
                IEM_MC_ARG(uint16_t,    u16Sel,            1);
                IEM_MC_ARG_CONST(bool,  fIsLarArg, fIsLar, 2);
                IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc);

                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
                IEMOP_HLP_DECODED_NL_2(fIsLar ? OP_LAR : OP_LSL, IEMOPFORM_RM_MEM, OP_PARM_Gv, OP_PARM_Ew, DISOPTYPE_DANGEROUS | DISOPTYPE_PRIVILEGED_NOTRAP);
/** @todo testcase: make sure it's a 16-bit read. */

                IEM_MC_FETCH_MEM_U16(u16Sel, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
                IEM_MC_REF_GREG_U64(pu64Dst, IEM_GET_MODRM_REG(pVCpu, bRm));
                IEM_MC_CALL_CIMPL_3(IEM_CIMPL_F_STATUS_FLAGS, RT_BIT_64(kIemNativeGstReg_GprFirst + IEM_GET_MODRM_REG(pVCpu, bRm)),
                                    iemCImpl_LarLsl_u64, pu64Dst, u16Sel, fIsLarArg);

                IEM_MC_END();
                break;

            IEM_NOT_REACHED_DEFAULT_CASE_RET();
        }
    }
}



/**
 * @opcode      0x02
 * @opflmodify  zf
 */
FNIEMOP_DEF(iemOp_lar_Gv_Ew)
{
    IEMOP_MNEMONIC(lar, "lar Gv,Ew");
    return FNIEMOP_CALL_1(iemOpCommonLarLsl_Gv_Ew, true);
}


/**
 * @opcode      0x03
 * @opflmodify  zf
 */
FNIEMOP_DEF(iemOp_lsl_Gv_Ew)
{
    IEMOP_MNEMONIC(lsl, "lsl Gv,Ew");
    return FNIEMOP_CALL_1(iemOpCommonLarLsl_Gv_Ew, false);
}


/** Opcode 0x0f 0x05. */
FNIEMOP_DEF(iemOp_syscall)
{
    if (RT_LIKELY(pVCpu->iem.s.uTargetCpu != IEMTARGETCPU_286))
    {
        IEMOP_MNEMONIC(syscall, "syscall");
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_BRANCH_INDIRECT | IEM_CIMPL_F_BRANCH_FAR | IEM_CIMPL_F_BRANCH_STACK_FAR
                                    | IEM_CIMPL_F_MODE | IEM_CIMPL_F_RFLAGS | IEM_CIMPL_F_END_TB, 0, iemCImpl_syscall);
    }
    else
    {
        IEMOP_MNEMONIC(loadall286, "loadall286");
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_BRANCH_INDIRECT | IEM_CIMPL_F_BRANCH_FAR | IEM_CIMPL_F_BRANCH_STACK_FAR
                                    | IEM_CIMPL_F_MODE | IEM_CIMPL_F_RFLAGS | IEM_CIMPL_F_END_TB,
                                    RT_BIT_64(kIemNativeGstReg_Cr0), iemCImpl_loadall286);
    }
}


/** Opcode 0x0f 0x06. */
FNIEMOP_DEF(iemOp_clts)
{
    IEMOP_MNEMONIC(clts, "clts");
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_VMEXIT, RT_BIT_64(kIemNativeGstReg_Cr0), iemCImpl_clts);
}


/** Opcode 0x0f 0x07. */
FNIEMOP_DEF(iemOp_sysret)
{
    IEMOP_MNEMONIC(sysret, "sysret");  /** @todo 386 LOADALL   */
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_DEFER_TO_CIMPL_1_RET(IEM_CIMPL_F_BRANCH_INDIRECT | IEM_CIMPL_F_BRANCH_FAR | IEM_CIMPL_F_BRANCH_STACK_FAR
                                | IEM_CIMPL_F_MODE | IEM_CIMPL_F_RFLAGS | IEM_CIMPL_F_END_TB, 0,
                                iemCImpl_sysret, pVCpu->iem.s.enmEffOpSize);
}


/** Opcode 0x0f 0x08. */
FNIEMOP_DEF(iemOp_invd)
{
    IEMOP_MNEMONIC0(FIXED, INVD, invd, DISOPTYPE_PRIVILEGED, 0);
    IEMOP_HLP_MIN_486();
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_invd);
}


/** Opcode 0x0f 0x09. */
FNIEMOP_DEF(iemOp_wbinvd)
{
    IEMOP_MNEMONIC0(FIXED, WBINVD, wbinvd, DISOPTYPE_PRIVILEGED, 0);
    IEMOP_HLP_MIN_486();
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_wbinvd);
}


/** Opcode 0x0f 0x0b. */
FNIEMOP_DEF(iemOp_ud2)
{
    IEMOP_MNEMONIC(ud2, "ud2");
    IEMOP_RAISE_INVALID_OPCODE_RET();
}

/** Opcode 0x0f 0x0d. */
FNIEMOP_DEF(iemOp_nop_Ev_GrpP)
{
    /* AMD prefetch group, Intel implements this as NOP Ev (and so do we). */
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fLongMode && !IEM_GET_GUEST_CPU_FEATURES(pVCpu)->f3DNowPrefetch)
    {
        IEMOP_MNEMONIC(GrpPNotSupported, "GrpP");
        IEMOP_RAISE_INVALID_OPCODE_RET();
    }

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        IEMOP_MNEMONIC(GrpPInvalid, "GrpP");
        IEMOP_RAISE_INVALID_OPCODE_RET();
    }

    switch (IEM_GET_MODRM_REG_8(bRm))
    {
        case 2: /* Aliased to /0 for the time being. */
        case 4: /* Aliased to /0 for the time being. */
        case 5: /* Aliased to /0 for the time being. */
        case 6: /* Aliased to /0 for the time being. */
        case 7: /* Aliased to /0 for the time being. */
        case 0: IEMOP_MNEMONIC(prefetch, "prefetch"); break;
        case 1: IEMOP_MNEMONIC(prefetchw_1, "prefetchw"); break;
        case 3: IEMOP_MNEMONIC(prefetchw_3, "prefetchw"); break;
        IEM_NOT_REACHED_DEFAULT_CASE_RET();
    }

    IEM_MC_BEGIN(0, 0);
    IEM_MC_LOCAL(RTGCPTR,  GCPtrEffSrc);
    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    /* Currently a NOP. */
    IEM_MC_NOREF(GCPtrEffSrc);
    IEM_MC_ADVANCE_RIP_AND_FINISH();
    IEM_MC_END();
}


/** Opcode 0x0f 0x0e. */
FNIEMOP_DEF(iemOp_femms)
{
    IEMOP_MNEMONIC(femms, "femms");

    IEM_MC_BEGIN(0, 0);
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_MAYBE_RAISE_DEVICE_NOT_AVAILABLE();
    IEM_MC_MAYBE_RAISE_FPU_XCPT();
    IEM_MC_ACTUALIZE_FPU_STATE_FOR_CHANGE();
    IEM_MC_FPU_FROM_MMX_MODE();
    IEM_MC_ADVANCE_RIP_AND_FINISH();
    IEM_MC_END();
}


/** Opcode 0x0f 0x0f. */
FNIEMOP_DEF(iemOp_3Dnow)
{
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->f3DNow)
    {
        IEMOP_MNEMONIC(Inv3Dnow, "3Dnow");
        IEMOP_RAISE_INVALID_OPCODE_RET();
    }

#ifdef IEM_WITH_3DNOW
    /* This is pretty sparse, use switch instead of table. */
    uint8_t b; IEM_OPCODE_GET_NEXT_U8(&b);
    return FNIEMOP_CALL_1(iemOp_3DNowDispatcher, b);
#else
    IEMOP_BITCH_ABOUT_STUB();
    return VERR_IEM_INSTR_NOT_IMPLEMENTED;
#endif
}


/**
 * @opcode      0x10
 * @oppfx       none
 * @opcpuid     sse
 * @opgroup     og_sse_simdfp_datamove
 * @opxcpttype  4UA
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-22 -> op1=-22
 */
FNIEMOP_DEF(iemOp_movups_Vps_Wps)
{
    IEMOP_MNEMONIC2(RM, MOVUPS, movups, Vps_WO, Wps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM128, XMM128.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();
        IEM_MC_COPY_XREG_U128(IEM_GET_MODRM_REG(pVCpu, bRm),
                              IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM128, [mem128].
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc); /** @todo optimize this one day... */
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U128_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_XREG_U128(IEM_GET_MODRM_REG(pVCpu, bRm), uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }

}


/**
 * @opcode      0x10
 * @oppfx       0x66
 * @opcpuid     sse2
 * @opgroup     og_sse2_pcksclr_datamove
 * @opxcpttype  4UA
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_movupd_Vpd_Wpd)
{
    IEMOP_MNEMONIC2(RM, MOVUPD, movupd, Vpd_WO, Wpd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM128, XMM128.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();
        IEM_MC_COPY_XREG_U128(IEM_GET_MODRM_REG(pVCpu, bRm),
                              IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM128, [mem128].
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc); /** @todo optimize this one day... */
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U128_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_XREG_U128(IEM_GET_MODRM_REG(pVCpu, bRm), uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x10
 * @oppfx       0xf3
 * @opcpuid     sse
 * @opgroup     og_sse_simdfp_datamove
 * @opxcpttype  5
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-22 -> op1=-22
 */
FNIEMOP_DEF(iemOp_movss_Vss_Wss)
{
    IEMOP_MNEMONIC2(RM, MOVSS, movss, VssZx_WO, Wss, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM32, XMM32.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_LOCAL(uint32_t,                  uSrc);

        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();
        IEM_MC_FETCH_XREG_U32(uSrc, IEM_GET_MODRM_RM(pVCpu, bRm), 0 /*a_iDword*/ );
        IEM_MC_STORE_XREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iDword*/, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM128, [mem32].
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(uint32_t,                  uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U32(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_XREG_U32_ZX_U128(IEM_GET_MODRM_REG(pVCpu, bRm), uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x10
 * @oppfx       0xf2
 * @opcpuid     sse2
 * @opgroup     og_sse2_pcksclr_datamove
 * @opxcpttype  5
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_movsd_Vsd_Wsd)
{
    IEMOP_MNEMONIC2(RM, MOVSD, movsd, VsdZx_WO, Wsd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM64, XMM64.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_LOCAL(uint64_t,                  uSrc);

        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();
        IEM_MC_FETCH_XREG_U64(uSrc, IEM_GET_MODRM_RM(pVCpu, bRm), 0 /* a_iQword*/);
        IEM_MC_STORE_XREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), 0 /* a_iQword*/, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM128, [mem64].
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U64(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_XREG_U64_ZX_U128(IEM_GET_MODRM_REG(pVCpu, bRm), uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x11
 * @oppfx       none
 * @opcpuid     sse
 * @opgroup     og_sse_simdfp_datamove
 * @opxcpttype  4UA
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_movups_Wps_Vps)
{
    IEMOP_MNEMONIC2(MR, MOVUPS, movups, Wps_WO, Vps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM128, XMM128.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();
        IEM_MC_COPY_XREG_U128(IEM_GET_MODRM_RM(pVCpu, bRm),
                              IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * [mem128], XMM128.
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc); /** @todo optimize this one day... */
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_READ();

        IEM_MC_FETCH_XREG_U128(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_STORE_MEM_U128_NO_AC(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x11
 * @oppfx       0x66
 * @opcpuid     sse2
 * @opgroup     og_sse2_pcksclr_datamove
 * @opxcpttype  4UA
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_movupd_Wpd_Vpd)
{
    IEMOP_MNEMONIC2(MR, MOVUPD, movupd, Wpd_WO, Vpd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM128, XMM128.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();
        IEM_MC_COPY_XREG_U128(IEM_GET_MODRM_RM(pVCpu, bRm),
                              IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * [mem128], XMM128.
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc); /** @todo optimize this one day... */
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_READ();

        IEM_MC_FETCH_XREG_U128(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_STORE_MEM_U128_NO_AC(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x11
 * @oppfx       0xf3
 * @opcpuid     sse
 * @opgroup     og_sse_simdfp_datamove
 * @opxcpttype  5
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-22 -> op1=-22
 */
FNIEMOP_DEF(iemOp_movss_Wss_Vss)
{
    IEMOP_MNEMONIC2(MR, MOVSS, movss, Wss_WO, Vss, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM32, XMM32.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_LOCAL(uint32_t,                  uSrc);

        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();
        IEM_MC_FETCH_XREG_U32(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iDword*/);
        IEM_MC_STORE_XREG_U32(IEM_GET_MODRM_RM(pVCpu, bRm), 0 /*a_iDword*/, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * [mem32], XMM32.
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(uint32_t,                  uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_READ();

        IEM_MC_FETCH_XREG_U32(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iDword*/);
        IEM_MC_STORE_MEM_U32(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x11
 * @oppfx       0xf2
 * @opcpuid     sse2
 * @opgroup     og_sse2_pcksclr_datamove
 * @opxcpttype  5
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_movsd_Wsd_Vsd)
{
    IEMOP_MNEMONIC2(MR, MOVSD, movsd, Wsd_WO, Vsd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM64, XMM64.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_LOCAL(uint64_t,                  uSrc);

        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();
        IEM_MC_FETCH_XREG_U64(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /* a_iQword*/);
        IEM_MC_STORE_XREG_U64(IEM_GET_MODRM_RM(pVCpu, bRm), 0 /* a_iQword*/, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * [mem64], XMM64.
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_READ();

        IEM_MC_FETCH_XREG_U64(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /* a_iQword*/);
        IEM_MC_STORE_MEM_U64(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


FNIEMOP_DEF(iemOp_movlps_Vq_Mq__movhlps)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /**
         * @opcode      0x12
         * @opcodesub   11 mr/reg
         * @oppfx       none
         * @opcpuid     sse
         * @opgroup     og_sse_simdfp_datamove
         * @opxcpttype  5
         * @optest      op1=1 op2=2 -> op1=2
         * @optest      op1=0 op2=-42 -> op1=-42
         */
        IEMOP_MNEMONIC2(RM_REG, MOVHLPS, movhlps, Vq_WO, UqHi, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);

        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_LOCAL(uint64_t,                  uSrc);

        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();
        IEM_MC_FETCH_XREG_U64(uSrc, IEM_GET_MODRM_RM(pVCpu, bRm), 1 /* a_iQword*/);
        IEM_MC_STORE_XREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), 0 /* a_iQword*/, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /**
         * @opdone
         * @opcode      0x12
         * @opcodesub   !11 mr/reg
         * @oppfx       none
         * @opcpuid     sse
         * @opgroup     og_sse_simdfp_datamove
         * @opxcpttype  5
         * @optest      op1=1 op2=2 -> op1=2
         * @optest      op1=0 op2=-42 -> op1=-42
         * @opfunction  iemOp_movlps_Vq_Mq__vmovhlps
         */
        IEMOP_MNEMONIC2(RM_MEM, MOVLPS, movlps, Vq_WO, Mq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);

        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U64(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_XREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), 0 /* a_iQword*/, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x12
 * @opcodesub   !11 mr/reg
 * @oppfx       0x66
 * @opcpuid     sse2
 * @opgroup     og_sse2_pcksclr_datamove
 * @opxcpttype  5
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_movlpd_Vq_Mq)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_MEM_MODE(bRm))
    {
        IEMOP_MNEMONIC2(RM_MEM, MOVLPD, movlpd, Vq_WO, Mq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);

        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U64(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_XREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), 0 /* a_iQword*/, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }

    /**
     * @opdone
     * @opmnemonic  ud660f12m3
     * @opcode      0x12
     * @opcodesub   11 mr/reg
     * @oppfx       0x66
     * @opunused    immediate
     * @opcpuid     sse
     * @optest      ->
     */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}


/**
 * @opcode      0x12
 * @oppfx       0xf3
 * @opcpuid     sse3
 * @opgroup     og_sse3_pcksclr_datamove
 * @opxcpttype  4
 * @optest      op1=-1 op2=0xdddddddd00000002eeeeeeee00000001 ->
 *              op1=0x00000002000000020000000100000001
 */
FNIEMOP_DEF(iemOp_movsldup_Vdq_Wdq)
{
    IEMOP_MNEMONIC2(RM, MOVSLDUP, movsldup, Vdq_WO, Wdq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM, XMM.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse3);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);

        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_FETCH_XREG_U128(uSrc, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 0, uSrc, 0);
        IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 1, uSrc, 0);
        IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 2, uSrc, 2);
        IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 3, uSrc, 2);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM, [mem128].
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse3);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 0, uSrc, 0);
        IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 1, uSrc, 0);
        IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 2, uSrc, 2);
        IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 3, uSrc, 2);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x12
 * @oppfx       0xf2
 * @opcpuid     sse3
 * @opgroup     og_sse3_pcksclr_datamove
 * @opxcpttype  5
 * @optest      op1=-1 op2=0xddddddddeeeeeeee2222222211111111 ->
 *              op1=0x22222222111111112222222211111111
 */
FNIEMOP_DEF(iemOp_movddup_Vdq_Wdq)
{
    IEMOP_MNEMONIC2(RM, MOVDDUP, movddup, Vdq_WO, Wdq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM128, XMM64.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse3);
        IEM_MC_LOCAL(uint64_t,                  uSrc);

        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_FETCH_XREG_U64(uSrc, IEM_GET_MODRM_RM(pVCpu, bRm), 0 /* a_iQword*/);
        IEM_MC_STORE_XREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), 0 /* a_iQword*/, uSrc);
        IEM_MC_STORE_XREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), 1 /* a_iQword*/, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM128, [mem64].
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
        IEM_MC_LOCAL(uint64_t,                  uSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse3);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_FETCH_MEM_U64(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_XREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), 0 /* a_iQword*/, uSrc);
        IEM_MC_STORE_XREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), 1 /* a_iQword*/, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x13
 * @opcodesub   !11 mr/reg
 * @oppfx       none
 * @opcpuid     sse
 * @opgroup     og_sse_simdfp_datamove
 * @opxcpttype  5
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_movlps_Mq_Vq)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_MEM_MODE(bRm))
    {
        IEMOP_MNEMONIC2(MR_MEM, MOVLPS, movlps, Mq_WO, Vq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);

        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_READ();

        IEM_MC_FETCH_XREG_U64(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /* a_iQword*/);
        IEM_MC_STORE_MEM_U64(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }

    /**
     * @opdone
     * @opmnemonic  ud0f13m3
     * @opcode      0x13
     * @opcodesub   11 mr/reg
     * @oppfx       none
     * @opunused    immediate
     * @opcpuid     sse
     * @optest      ->
     */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}


/**
 * @opcode      0x13
 * @opcodesub   !11 mr/reg
 * @oppfx       0x66
 * @opcpuid     sse2
 * @opgroup     og_sse2_pcksclr_datamove
 * @opxcpttype  5
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_movlpd_Mq_Vq)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_MEM_MODE(bRm))
    {
        IEMOP_MNEMONIC2(MR_MEM, MOVLPD, movlpd, Mq_WO, Vq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);

        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_READ();

        IEM_MC_FETCH_XREG_U64(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /* a_iQword*/);
        IEM_MC_STORE_MEM_U64(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }

    /**
     * @opdone
     * @opmnemonic  ud660f13m3
     * @opcode      0x13
     * @opcodesub   11 mr/reg
     * @oppfx       0x66
     * @opunused    immediate
     * @opcpuid     sse
     * @optest      ->
     */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}


/**
 * @opmnemonic  udf30f13
 * @opcode      0x13
 * @oppfx       0xf3
 * @opunused    intel-modrm
 * @opcpuid     sse
 * @optest      ->
 * @opdone
 */

/**
 * @opmnemonic  udf20f13
 * @opcode      0x13
 * @oppfx       0xf2
 * @opunused    intel-modrm
 * @opcpuid     sse
 * @optest      ->
 * @opdone
 */

/** Opcode      0x0f 0x14 - unpcklps Vx, Wx*/
FNIEMOP_DEF(iemOp_unpcklps_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, UNPCKLPS, unpcklps, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse_LowLow_To_Full, iemAImpl_unpcklps_u128);
}


/** Opcode 0x66 0x0f 0x14 - unpcklpd Vx, Wx   */
FNIEMOP_DEF(iemOp_unpcklpd_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, UNPCKLPD, unpcklpd, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2_LowLow_To_Full, iemAImpl_unpcklpd_u128);
}


/**
 * @opdone
 * @opmnemonic  udf30f14
 * @opcode      0x14
 * @oppfx       0xf3
 * @opunused    intel-modrm
 * @opcpuid     sse
 * @optest      ->
 * @opdone
 */

/**
 * @opmnemonic  udf20f14
 * @opcode      0x14
 * @oppfx       0xf2
 * @opunused    intel-modrm
 * @opcpuid     sse
 * @optest      ->
 * @opdone
 */

/** Opcode      0x0f 0x15 - unpckhps Vx, Wx   */
FNIEMOP_DEF(iemOp_unpckhps_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, UNPCKHPS, unpckhps, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse_HighHigh_To_Full, iemAImpl_unpckhps_u128);
}


/** Opcode 0x66 0x0f 0x15 - unpckhpd Vx, Wx   */
FNIEMOP_DEF(iemOp_unpckhpd_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, UNPCKHPD, unpckhpd, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2_HighHigh_To_Full, iemAImpl_unpckhpd_u128);
}


/*  Opcode 0xf3 0x0f 0x15 - invalid */
/*  Opcode 0xf2 0x0f 0x15 - invalid */

/**
 * @opdone
 * @opmnemonic  udf30f15
 * @opcode      0x15
 * @oppfx       0xf3
 * @opunused    intel-modrm
 * @opcpuid     sse
 * @optest      ->
 * @opdone
 */

/**
 * @opmnemonic  udf20f15
 * @opcode      0x15
 * @oppfx       0xf2
 * @opunused    intel-modrm
 * @opcpuid     sse
 * @optest      ->
 * @opdone
 */

FNIEMOP_DEF(iemOp_movhps_Vdq_Mq__movlhps_Vdq_Uq)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /**
         * @opcode      0x16
         * @opcodesub   11 mr/reg
         * @oppfx       none
         * @opcpuid     sse
         * @opgroup     og_sse_simdfp_datamove
         * @opxcpttype  5
         * @optest      op1=1 op2=2 -> op1=2
         * @optest      op1=0 op2=-42 -> op1=-42
         */
        IEMOP_MNEMONIC2(RM_REG, MOVLHPS, movlhps, VqHi_WO, Uq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);

        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_LOCAL(uint64_t,                  uSrc);

        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();
        IEM_MC_FETCH_XREG_U64(uSrc, IEM_GET_MODRM_RM(pVCpu, bRm), 0 /* a_iQword*/);
        IEM_MC_STORE_XREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), 1 /*a_iQword*/, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /**
         * @opdone
         * @opcode      0x16
         * @opcodesub   !11 mr/reg
         * @oppfx       none
         * @opcpuid     sse
         * @opgroup     og_sse_simdfp_datamove
         * @opxcpttype  5
         * @optest      op1=1 op2=2 -> op1=2
         * @optest      op1=0 op2=-42 -> op1=-42
         * @opfunction  iemOp_movhps_Vdq_Mq__movlhps_Vdq_Uq
         */
        IEMOP_MNEMONIC2(RM_MEM, MOVHPS, movhps, VqHi_WO, Mq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);

        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U64(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_XREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), 1 /*a_iQword*/, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x16
 * @opcodesub   !11 mr/reg
 * @oppfx       0x66
 * @opcpuid     sse2
 * @opgroup     og_sse2_pcksclr_datamove
 * @opxcpttype  5
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_movhpd_Vdq_Mq)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_MEM_MODE(bRm))
    {
        IEMOP_MNEMONIC2(RM_MEM, MOVHPD, movhpd, VqHi_WO, Mq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);

        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U64(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_XREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), 1 /*a_iQword*/, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }

    /**
     * @opdone
     * @opmnemonic  ud660f16m3
     * @opcode      0x16
     * @opcodesub   11 mr/reg
     * @oppfx       0x66
     * @opunused    immediate
     * @opcpuid     sse
     * @optest      ->
     */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}


/**
 * @opcode      0x16
 * @oppfx       0xf3
 * @opcpuid     sse3
 * @opgroup     og_sse3_pcksclr_datamove
 * @opxcpttype  4
 * @optest      op1=-1 op2=0x00000002dddddddd00000001eeeeeeee ->
 *              op1=0x00000002000000020000000100000001
 */
FNIEMOP_DEF(iemOp_movshdup_Vdq_Wdq)
{
    IEMOP_MNEMONIC2(RM, MOVSHDUP, movshdup, Vdq_WO, Wdq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM128, XMM128.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse3);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);

        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_FETCH_XREG_U128(uSrc, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 0, uSrc, 1);
        IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 1, uSrc, 1);
        IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 2, uSrc, 3);
        IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 3, uSrc, 3);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM128, [mem128].
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse3);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 0, uSrc, 1);
        IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 1, uSrc, 1);
        IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 2, uSrc, 3);
        IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 3, uSrc, 3);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}

/**
 * @opdone
 * @opmnemonic  udf30f16
 * @opcode      0x16
 * @oppfx       0xf2
 * @opunused    intel-modrm
 * @opcpuid     sse
 * @optest      ->
 * @opdone
 */


/**
 * @opcode      0x17
 * @opcodesub   !11 mr/reg
 * @oppfx       none
 * @opcpuid     sse
 * @opgroup     og_sse_simdfp_datamove
 * @opxcpttype  5
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_movhps_Mq_Vq)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_MEM_MODE(bRm))
    {
        IEMOP_MNEMONIC2(MR_MEM, MOVHPS, movhps, Mq_WO, VqHi, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);

        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_READ();

        IEM_MC_FETCH_XREG_U64(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), 1 /* a_iQword*/);
        IEM_MC_STORE_MEM_U64(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }

    /**
     * @opdone
     * @opmnemonic  ud0f17m3
     * @opcode      0x17
     * @opcodesub   11 mr/reg
     * @oppfx       none
     * @opunused    immediate
     * @opcpuid     sse
     * @optest      ->
     */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}


/**
 * @opcode      0x17
 * @opcodesub   !11 mr/reg
 * @oppfx       0x66
 * @opcpuid     sse2
 * @opgroup     og_sse2_pcksclr_datamove
 * @opxcpttype  5
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_movhpd_Mq_Vq)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_MEM_MODE(bRm))
    {
        IEMOP_MNEMONIC2(MR_MEM, MOVHPD, movhpd, Mq_WO, VqHi, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);

        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_READ();

        IEM_MC_FETCH_XREG_U64(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), 1 /* a_iQword*/);
        IEM_MC_STORE_MEM_U64(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }

    /**
     * @opdone
     * @opmnemonic  ud660f17m3
     * @opcode      0x17
     * @opcodesub   11 mr/reg
     * @oppfx       0x66
     * @opunused    immediate
     * @opcpuid     sse
     * @optest      ->
     */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}


/**
 * @opdone
 * @opmnemonic  udf30f17
 * @opcode      0x17
 * @oppfx       0xf3
 * @opunused    intel-modrm
 * @opcpuid     sse
 * @optest      ->
 * @opdone
 */

/**
 * @opmnemonic  udf20f17
 * @opcode      0x17
 * @oppfx       0xf2
 * @opunused    intel-modrm
 * @opcpuid     sse
 * @optest      ->
 * @opdone
 */


/** Opcode 0x0f 0x18. */
FNIEMOP_DEF(iemOp_prefetch_Grp16)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_MEM_MODE(bRm))
    {
        switch (IEM_GET_MODRM_REG_8(bRm))
        {
            case 4: /* Aliased to /0 for the time being according to AMD. */
            case 5: /* Aliased to /0 for the time being according to AMD. */
            case 6: /* Aliased to /0 for the time being according to AMD. */
            case 7: /* Aliased to /0 for the time being according to AMD. */
            case 0: IEMOP_MNEMONIC(prefetchNTA, "prefetchNTA m8"); break;
            case 1: IEMOP_MNEMONIC(prefetchT0, "prefetchT0  m8"); break;
            case 2: IEMOP_MNEMONIC(prefetchT1, "prefetchT1  m8"); break;
            case 3: IEMOP_MNEMONIC(prefetchT2, "prefetchT2  m8"); break;
            IEM_NOT_REACHED_DEFAULT_CASE_RET();
        }

        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(RTGCPTR,  GCPtrEffSrc);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        /* Currently a NOP. */
        IEM_MC_NOREF(GCPtrEffSrc);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}


/** Opcode 0x0f 0x19..0x1f. */
FNIEMOP_DEF(iemOp_nop_Ev)
{
    IEMOP_MNEMONIC(nop_Ev, "nop Ev");
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(RTGCPTR, GCPtrEffSrc);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        /* Currently a NOP. */
        IEM_MC_NOREF(GCPtrEffSrc);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode 0x0f 0x20. */
FNIEMOP_DEF(iemOp_mov_Rd_Cd)
{
    /* mod is ignored, as is operand size overrides. */
/** @todo testcase: check memory encoding. */
    IEMOP_MNEMONIC(mov_Rd_Cd, "mov Rd,Cd");
    IEMOP_HLP_MIN_386();
    if (IEM_IS_64BIT_CODE(pVCpu))
        pVCpu->iem.s.enmEffOpSize = pVCpu->iem.s.enmDefOpSize = IEMMODE_64BIT;
    else
        pVCpu->iem.s.enmEffOpSize = pVCpu->iem.s.enmDefOpSize = IEMMODE_32BIT;

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    uint8_t iCrReg = IEM_GET_MODRM_REG(pVCpu, bRm);
    if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_LOCK)
    {
        /* The lock prefix can be used to encode CR8 accesses on some CPUs. */
        if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fMovCr8In32Bit)
            IEMOP_RAISE_INVALID_OPCODE_RET(); /* #UD takes precedence over #GP(), see test. */
        iCrReg |= 8;
    }
    switch (iCrReg)
    {
        case 0: case 2: case 3: case 4: case 8:
            break;
        default:
            IEMOP_RAISE_INVALID_OPCODE_RET();
    }
    IEMOP_HLP_DONE_DECODING();

    IEM_MC_DEFER_TO_CIMPL_2_RET(IEM_CIMPL_F_VMEXIT,
                                RT_BIT_64(kIemNativeGstReg_GprFirst + IEM_GET_MODRM_RM(pVCpu, bRm)),
                                iemCImpl_mov_Rd_Cd, IEM_GET_MODRM_RM(pVCpu, bRm), iCrReg);
}


/** Opcode 0x0f 0x21. */
FNIEMOP_DEF(iemOp_mov_Rd_Dd)
{
/** @todo testcase: check memory encoding. */
    IEMOP_MNEMONIC(mov_Rd_Dd, "mov Rd,Dd");
    IEMOP_HLP_MIN_386();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_REX_R)
        IEMOP_RAISE_INVALID_OPCODE_RET();
    IEM_MC_DEFER_TO_CIMPL_2_RET(IEM_CIMPL_F_VMEXIT,
                                RT_BIT_64(kIemNativeGstReg_GprFirst + IEM_GET_MODRM_RM(pVCpu, bRm)),
                                iemCImpl_mov_Rd_Dd, IEM_GET_MODRM_RM(pVCpu, bRm), IEM_GET_MODRM_REG_8(bRm));
}


/** Opcode 0x0f 0x22. */
FNIEMOP_DEF(iemOp_mov_Cd_Rd)
{
    /* mod is ignored, as is operand size overrides. */
    IEMOP_MNEMONIC(mov_Cd_Rd, "mov Cd,Rd");
    IEMOP_HLP_MIN_386();
    if (IEM_IS_64BIT_CODE(pVCpu))
        pVCpu->iem.s.enmEffOpSize = pVCpu->iem.s.enmDefOpSize = IEMMODE_64BIT;
    else
        pVCpu->iem.s.enmEffOpSize = pVCpu->iem.s.enmDefOpSize = IEMMODE_32BIT;

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    uint8_t iCrReg = IEM_GET_MODRM_REG(pVCpu, bRm);
    if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_LOCK)
    {
        /* The lock prefix can be used to encode CR8 accesses on some CPUs. */
        if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fMovCr8In32Bit)
            IEMOP_RAISE_INVALID_OPCODE_RET(); /* #UD takes precedence over #GP(), see test. */
        iCrReg |= 8;
    }
    switch (iCrReg)
    {
        case 0: case 2: case 3: case 4: case 8:
            break;
        default:
            IEMOP_RAISE_INVALID_OPCODE_RET();
    }
    IEMOP_HLP_DONE_DECODING();

    /** @todo r=aeichner Split this up as flushing the cr0 is excessive for crX != 0? */
    if (iCrReg & (2 | 8))
        IEM_MC_DEFER_TO_CIMPL_2_RET(IEM_CIMPL_F_VMEXIT, 0,
                                    iemCImpl_mov_Cd_Rd, iCrReg, IEM_GET_MODRM_RM(pVCpu, bRm));
    else
        IEM_MC_DEFER_TO_CIMPL_2_RET(IEM_CIMPL_F_MODE | IEM_CIMPL_F_VMEXIT, RT_BIT_64(kIemNativeGstReg_Cr0) | RT_BIT_64(kIemNativeGstReg_Cr4),
                                    iemCImpl_mov_Cd_Rd, iCrReg, IEM_GET_MODRM_RM(pVCpu, bRm));
}


/** Opcode 0x0f 0x23. */
FNIEMOP_DEF(iemOp_mov_Dd_Rd)
{
    IEMOP_MNEMONIC(mov_Dd_Rd, "mov Dd,Rd");
    IEMOP_HLP_MIN_386();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_REX_R)
        IEMOP_RAISE_INVALID_OPCODE_RET();
    IEM_MC_DEFER_TO_CIMPL_2_RET(IEM_CIMPL_F_MODE | IEM_CIMPL_F_VMEXIT, 0,
                                iemCImpl_mov_Dd_Rd, IEM_GET_MODRM_REG_8(bRm), IEM_GET_MODRM_RM(pVCpu, bRm));
}


/** Opcode 0x0f 0x24. */
FNIEMOP_DEF(iemOp_mov_Rd_Td)
{
    IEMOP_MNEMONIC(mov_Rd_Td, "mov Rd,Td");
    IEMOP_HLP_MIN_386();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    if (RT_LIKELY(IEM_GET_TARGET_CPU(pVCpu) >= IEMTARGETCPU_PENTIUM))
        IEMOP_RAISE_INVALID_OPCODE_RET();
    IEM_MC_DEFER_TO_CIMPL_2_RET(0, RT_BIT_64(kIemNativeGstReg_GprFirst + IEM_GET_MODRM_RM(pVCpu, bRm)),
                                iemCImpl_mov_Rd_Td, IEM_GET_MODRM_RM(pVCpu, bRm), IEM_GET_MODRM_REG_8(bRm));
}


/** Opcode 0x0f 0x26. */
FNIEMOP_DEF(iemOp_mov_Td_Rd)
{
    IEMOP_MNEMONIC(mov_Td_Rd, "mov Td,Rd");
    IEMOP_HLP_MIN_386();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    if (RT_LIKELY(IEM_GET_TARGET_CPU(pVCpu) >= IEMTARGETCPU_PENTIUM))
        IEMOP_RAISE_INVALID_OPCODE_RET();
    IEM_MC_DEFER_TO_CIMPL_2_RET(0, 0, iemCImpl_mov_Td_Rd, IEM_GET_MODRM_REG_8(bRm), IEM_GET_MODRM_RM(pVCpu, bRm));
}


/**
 * @opcode      0x28
 * @oppfx       none
 * @opcpuid     sse
 * @opgroup     og_sse_simdfp_datamove
 * @opxcpttype  1
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_movaps_Vps_Wps)
{
    IEMOP_MNEMONIC2(RM, MOVAPS, movaps, Vps_WO, Wps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();
        IEM_MC_COPY_XREG_U128(IEM_GET_MODRM_REG(pVCpu, bRm),
                              IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc); /** @todo optimize this one day... */
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_XREG_U128(IEM_GET_MODRM_REG(pVCpu, bRm), uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}

/**
 * @opcode      0x28
 * @oppfx       66
 * @opcpuid     sse2
 * @opgroup     og_sse2_pcksclr_datamove
 * @opxcpttype  1
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_movapd_Vpd_Wpd)
{
    IEMOP_MNEMONIC2(RM, MOVAPD, movapd, Vpd_WO, Wpd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();
        IEM_MC_COPY_XREG_U128(IEM_GET_MODRM_REG(pVCpu, bRm),
                              IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc); /** @todo optimize this one day... */
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_XREG_U128(IEM_GET_MODRM_REG(pVCpu, bRm), uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}

/*  Opcode 0xf3 0x0f 0x28 - invalid */
/*  Opcode 0xf2 0x0f 0x28 - invalid */

/**
 * @opcode      0x29
 * @oppfx       none
 * @opcpuid     sse
 * @opgroup     og_sse_simdfp_datamove
 * @opxcpttype  1
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_movaps_Wps_Vps)
{
    IEMOP_MNEMONIC2(MR, MOVAPS, movaps, Wps_WO, Vps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();
        IEM_MC_COPY_XREG_U128(IEM_GET_MODRM_RM(pVCpu, bRm),
                              IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Memory, register.
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc); /** @todo optimize this one day... */
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_READ();

        IEM_MC_FETCH_XREG_U128(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_STORE_MEM_U128_ALIGN_SSE(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}

/**
 * @opcode      0x29
 * @oppfx       66
 * @opcpuid     sse2
 * @opgroup     og_sse2_pcksclr_datamove
 * @opxcpttype  1
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_movapd_Wpd_Vpd)
{
    IEMOP_MNEMONIC2(MR, MOVAPD, movapd, Wpd_WO, Vpd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();
        IEM_MC_COPY_XREG_U128(IEM_GET_MODRM_RM(pVCpu, bRm),
                              IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Memory, register.
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc); /** @todo optimize this one day... */
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_READ();

        IEM_MC_FETCH_XREG_U128(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_STORE_MEM_U128_ALIGN_SSE(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}

/*  Opcode 0xf3 0x0f 0x29 - invalid */
/*  Opcode 0xf2 0x0f 0x29 - invalid */


/** Opcode      0x0f 0x2a - cvtpi2ps Vps, Qpi */
FNIEMOP_DEF(iemOp_cvtpi2ps_Vps_Qpi)
{
    IEMOP_MNEMONIC2(RM, CVTPI2PS, cvtpi2ps, Vps, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0); /// @todo
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM, MMX
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_LOCAL(X86XMMREG,             Dst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    pDst, Dst,          0);
        IEM_MC_ARG(uint64_t,                u64Src,             1);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_MAYBE_RAISE_FPU_XCPT();
        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_FETCH_XREG_XMM(Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); /* Need it because the high quadword remains unchanged. */
        IEM_MC_FETCH_MREG_U64(u64Src, IEM_GET_MODRM_RM_8(bRm));

        IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvtpi2ps_u128, pDst, u64Src);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), Dst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM, [mem64]
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(X86XMMREG,             Dst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    pDst, Dst,          0);
        IEM_MC_ARG(uint64_t,                u64Src,             1);
        IEM_MC_LOCAL(RTGCPTR,               GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_MAYBE_RAISE_FPU_XCPT();
        IEM_MC_FETCH_MEM_U64(u64Src, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvtpi2ps_u128, pDst, u64Src);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), Dst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode 0x66 0x0f 0x2a - cvtpi2pd Vpd, Qpi */
FNIEMOP_DEF(iemOp_cvtpi2pd_Vpd_Qpi)
{
    IEMOP_MNEMONIC2(RM, CVTPI2PD, cvtpi2pd, Vps, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0); /// @todo
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM, MMX
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_LOCAL(X86XMMREG,             Dst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    pDst, Dst,          0);
        IEM_MC_ARG(uint64_t,                u64Src,             1);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_MAYBE_RAISE_FPU_XCPT();
        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_FETCH_MREG_U64(u64Src, IEM_GET_MODRM_RM_8(bRm));

        IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvtpi2pd_u128, pDst, u64Src);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), Dst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM, [mem64]
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(X86XMMREG,             Dst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    pDst, Dst,          0);
        IEM_MC_ARG(uint64_t,                u64Src,             1);
        IEM_MC_LOCAL(RTGCPTR,               GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_MAYBE_RAISE_FPU_XCPT();
        IEM_MC_FETCH_MEM_U64(u64Src, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        /* Doesn't cause a transition to MMX mode. */
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvtpi2pd_u128, pDst, u64Src);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), Dst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode 0xf3 0x0f 0x2a - cvtsi2ss Vss, Ey */
FNIEMOP_DEF(iemOp_cvtsi2ss_Vss_Ey)
{
    IEMOP_MNEMONIC2(RM, CVTSI2SS, cvtsi2ss, Vss, Ey, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_REX_W)
    {
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* XMM, greg64 */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_LOCAL(RTFLOAT32U,  r32Dst);
            IEM_MC_ARG_LOCAL_REF(PRTFLOAT32U,   pr32Dst, r32Dst,  0);
            IEM_MC_ARG(const int64_t *,         pi64Src,          1);

            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE(); /** @todo: This is superfluous because IEM_MC_CALL_SSE_AIMPL_2() is calling this but the tstIEMCheckMc testcase depends on it. */

            IEM_MC_REF_GREG_I64_CONST(pi64Src, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvtsi2ss_r32_i64, pr32Dst, pi64Src);
            IEM_MC_STORE_XREG_R32(IEM_GET_MODRM_REG(pVCpu, bRm), r32Dst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /* XMM, [mem64] */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_LOCAL(RTGCPTR,    GCPtrEffSrc);
            IEM_MC_LOCAL(RTFLOAT32U, r32Dst);
            IEM_MC_LOCAL(int64_t,    i64Src);
            IEM_MC_ARG_LOCAL_REF(PRTFLOAT32U,       pr32Dst, r32Dst,     0);
            IEM_MC_ARG_LOCAL_REF(const int64_t *,   pi64Src, i64Src,     1);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE(); /** @todo: This is superfluous because IEM_MC_CALL_SSE_AIMPL_2() is calling this but the tstIEMCheckMc testcase depends on it. */

            IEM_MC_FETCH_MEM_I64(i64Src, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvtsi2ss_r32_i64, pr32Dst, pi64Src);
            IEM_MC_STORE_XREG_R32(IEM_GET_MODRM_REG(pVCpu, bRm), r32Dst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* greg, XMM */
            IEM_MC_BEGIN(0, 0);
            IEM_MC_LOCAL(RTFLOAT32U, r32Dst);
            IEM_MC_ARG_LOCAL_REF(PRTFLOAT32U, pr32Dst, r32Dst,     0);
            IEM_MC_ARG(const int32_t *,       pi32Src,             1);

            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE(); /** @todo: This is superfluous because IEM_MC_CALL_SSE_AIMPL_2() is calling this but the tstIEMCheckMc testcase depends on it. */

            IEM_MC_REF_GREG_I32_CONST(pi32Src, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvtsi2ss_r32_i32, pr32Dst, pi32Src);
            IEM_MC_STORE_XREG_R32(IEM_GET_MODRM_REG(pVCpu, bRm), r32Dst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /* greg, [mem32] */
            IEM_MC_BEGIN(0, 0);
            IEM_MC_LOCAL(RTGCPTR,    GCPtrEffSrc);
            IEM_MC_LOCAL(RTFLOAT32U, r32Dst);
            IEM_MC_LOCAL(int32_t,    i32Src);
            IEM_MC_ARG_LOCAL_REF(PRTFLOAT32U,      pr32Dst, r32Dst,     0);
            IEM_MC_ARG_LOCAL_REF(const int32_t *,  pi32Src, i32Src,     1);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE(); /** @todo This is superfluous because IEM_MC_CALL_SSE_AIMPL_2() is calling this but the tstIEMCheckMc testcase depends on it. */

            IEM_MC_FETCH_MEM_I32(i32Src, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvtsi2ss_r32_i32, pr32Dst, pi32Src);
            IEM_MC_STORE_XREG_R32(IEM_GET_MODRM_REG(pVCpu, bRm), r32Dst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/** Opcode 0xf2 0x0f 0x2a - cvtsi2sd Vsd, Ey */
FNIEMOP_DEF(iemOp_cvtsi2sd_Vsd_Ey)
{
    IEMOP_MNEMONIC2(RM, CVTSI2SD, cvtsi2sd, Vsd, Ey, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_REX_W)
    {
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* XMM, greg64 */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_LOCAL(RTFLOAT64U,  r64Dst);
            IEM_MC_ARG_LOCAL_REF(PRTFLOAT64U,   pr64Dst, r64Dst,  0);
            IEM_MC_ARG(const int64_t *,         pi64Src,          1);

            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE(); /** @todo This is superfluous because IEM_MC_CALL_SSE_AIMPL_2() is calling this but the tstIEMCheckMc testcase depends on it. */

            IEM_MC_REF_GREG_I64_CONST(pi64Src, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvtsi2sd_r64_i64, pr64Dst, pi64Src);
            IEM_MC_STORE_XREG_R64(IEM_GET_MODRM_REG(pVCpu, bRm), r64Dst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /* XMM, [mem64] */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_LOCAL(RTGCPTR,    GCPtrEffSrc);
            IEM_MC_LOCAL(RTFLOAT64U, r64Dst);
            IEM_MC_LOCAL(int64_t,    i64Src);
            IEM_MC_ARG_LOCAL_REF(PRTFLOAT64U,       pr64Dst, r64Dst,     0);
            IEM_MC_ARG_LOCAL_REF(const int64_t *,   pi64Src, i64Src,     1);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE(); /** @todo This is superfluous because IEM_MC_CALL_SSE_AIMPL_2() is calling this but the tstIEMCheckMc testcase depends on it. */

            IEM_MC_FETCH_MEM_I64(i64Src, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvtsi2sd_r64_i64, pr64Dst, pi64Src);
            IEM_MC_STORE_XREG_R64(IEM_GET_MODRM_REG(pVCpu, bRm), r64Dst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* XMM, greg32 */
            IEM_MC_BEGIN(0, 0);
            IEM_MC_LOCAL(RTFLOAT64U, r64Dst);
            IEM_MC_ARG_LOCAL_REF(PRTFLOAT64U, pr64Dst, r64Dst,     0);
            IEM_MC_ARG(const int32_t *,       pi32Src,             1);

            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE(); /** @todo: This is superfluous because IEM_MC_CALL_SSE_AIMPL_2() is calling this but the tstIEMCheckMc testcase depends on it. */

            IEM_MC_REF_GREG_I32_CONST(pi32Src, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvtsi2sd_r64_i32, pr64Dst, pi32Src);
            IEM_MC_STORE_XREG_R64(IEM_GET_MODRM_REG(pVCpu, bRm), r64Dst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /* XMM, [mem32] */
            IEM_MC_BEGIN(0, 0);
            IEM_MC_LOCAL(RTGCPTR,    GCPtrEffSrc);
            IEM_MC_LOCAL(RTFLOAT64U, r64Dst);
            IEM_MC_LOCAL(int32_t,    i32Src);
            IEM_MC_ARG_LOCAL_REF(PRTFLOAT64U,      pr64Dst, r64Dst,     0);
            IEM_MC_ARG_LOCAL_REF(const int32_t *,  pi32Src, i32Src,     1);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE(); /** @todo: This is superfluous because IEM_MC_CALL_SSE_AIMPL_2() is calling this but the tstIEMCheckMc testcase depends on it. */

            IEM_MC_FETCH_MEM_I32(i32Src, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvtsi2sd_r64_i32, pr64Dst, pi32Src);
            IEM_MC_STORE_XREG_R64(IEM_GET_MODRM_REG(pVCpu, bRm), r64Dst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/**
 * @opcode      0x2b
 * @opcodesub   !11 mr/reg
 * @oppfx       none
 * @opcpuid     sse
 * @opgroup     og_sse1_cachect
 * @opxcpttype  1
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_movntps_Mps_Vps)
{
    IEMOP_MNEMONIC2(MR_MEM, MOVNTPS, movntps, Mps_WO, Vps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_MEM_MODE(bRm))
    {
        /*
         * memory, register.
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc); /** @todo optimize this one day... */
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();

        IEM_MC_FETCH_XREG_U128(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_STORE_MEM_U128_ALIGN_SSE(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    /* The register, register encoding is invalid. */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}

/**
 * @opcode      0x2b
 * @opcodesub   !11 mr/reg
 * @oppfx       0x66
 * @opcpuid     sse2
 * @opgroup     og_sse2_cachect
 * @opxcpttype  1
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_movntpd_Mpd_Vpd)
{
    IEMOP_MNEMONIC2(MR_MEM, MOVNTPD, movntpd, Mpd_WO, Vpd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_MEM_MODE(bRm))
    {
        /*
         * memory, register.
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc); /** @todo optimize this one day... */
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();

        IEM_MC_FETCH_XREG_U128(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_STORE_MEM_U128_ALIGN_SSE(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    /* The register, register encoding is invalid. */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}
/*  Opcode 0xf3 0x0f 0x2b - invalid */
/*  Opcode 0xf2 0x0f 0x2b - invalid */


/** Opcode      0x0f 0x2c - cvttps2pi Ppi, Wps */
FNIEMOP_DEF(iemOp_cvttps2pi_Ppi_Wps)
{
    IEMOP_MNEMONIC2(RM, CVTTPS2PI, cvttps2pi, Pq, Wps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0); /// @todo
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_LOCAL(uint64_t,              u64Dst);
        IEM_MC_ARG_LOCAL_REF(uint64_t *,    pu64Dst, u64Dst,    0);
        IEM_MC_ARG(uint64_t,                u64Src,             1);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_FETCH_XREG_U64(u64Src, IEM_GET_MODRM_RM(pVCpu, bRm), 0 /* a_iQword*/);

        IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvttps2pi_u128, pu64Dst, u64Src);
        IEM_MC_STORE_MREG_U64(IEM_GET_MODRM_REG_8(bRm), u64Dst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(uint64_t,              u64Dst);
        IEM_MC_ARG_LOCAL_REF(uint64_t *,    pu64Dst, u64Dst,    0);
        IEM_MC_ARG(uint64_t,                u64Src,             1);
        IEM_MC_LOCAL(RTGCPTR,               GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_U64(u64Src, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvttps2pi_u128, pu64Dst, u64Src);
        IEM_MC_STORE_MREG_U64(IEM_GET_MODRM_REG_8(bRm), u64Dst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode 0x66 0x0f 0x2c - cvttpd2pi Ppi, Wpd */
FNIEMOP_DEF(iemOp_cvttpd2pi_Ppi_Wpd)
{
    IEMOP_MNEMONIC2(RM, CVTTPD2PI, cvttpd2pi, Pq, Wpd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0); /// @todo
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_LOCAL(uint64_t,              u64Dst);
        IEM_MC_ARG_LOCAL_REF(uint64_t *,    pu64Dst, u64Dst,    0);
        IEM_MC_ARG(PCX86XMMREG,             pSrc,               1);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_REF_XREG_XMM_CONST(pSrc, IEM_GET_MODRM_RM(pVCpu, bRm));

        IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvttpd2pi_u128, pu64Dst, pSrc);
        IEM_MC_STORE_MREG_U64(IEM_GET_MODRM_REG_8(bRm), u64Dst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(uint64_t,              u64Dst);
        IEM_MC_ARG_LOCAL_REF(uint64_t *,    pu64Dst,    u64Dst, 0);
        IEM_MC_LOCAL(X86XMMREG,             uSrc);
        IEM_MC_ARG_LOCAL_REF(PCX86XMMREG,   pSrc,       uSrc,   1);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_XMM_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvttpd2pi_u128, pu64Dst, pSrc);
        IEM_MC_STORE_MREG_U64(IEM_GET_MODRM_REG_8(bRm), u64Dst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode 0xf3 0x0f 0x2c - cvttss2si Gy, Wss */
FNIEMOP_DEF(iemOp_cvttss2si_Gy_Wss)
{
    IEMOP_MNEMONIC2(RM, CVTTSS2SI, cvttss2si, Gy, Wsd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_REX_W)
    {
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* greg64, XMM */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_LOCAL(int64_t,   i64Dst);
            IEM_MC_ARG_LOCAL_REF(int64_t *,  pi64Dst, i64Dst,     0);
            IEM_MC_ARG(const uint32_t *,     pu32Src,             1);

            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE(); /** @todo: This is superfluous because IEM_MC_CALL_SSE_AIMPL_2() is calling this but the tstIEMCheckMc testcase depends on it. */

            IEM_MC_REF_XREG_U32_CONST(pu32Src, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvttss2si_i64_r32, pi64Dst, pu32Src);
            IEM_MC_STORE_GREG_I64(IEM_GET_MODRM_REG(pVCpu, bRm), i64Dst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /* greg64, [mem64] */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc);
            IEM_MC_LOCAL(int64_t,   i64Dst);
            IEM_MC_LOCAL(uint32_t,  u32Src);
            IEM_MC_ARG_LOCAL_REF(int64_t *,         pi64Dst, i64Dst,     0);
            IEM_MC_ARG_LOCAL_REF(const uint32_t *,  pu32Src, u32Src,     1);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE(); /** @todo: This is superfluous because IEM_MC_CALL_SSE_AIMPL_2() is calling this but the tstIEMCheckMc testcase depends on it. */

            IEM_MC_FETCH_MEM_U32(u32Src, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvttss2si_i64_r32, pi64Dst, pu32Src);
            IEM_MC_STORE_GREG_I64(IEM_GET_MODRM_REG(pVCpu, bRm), i64Dst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* greg, XMM */
            IEM_MC_BEGIN(0, 0);
            IEM_MC_LOCAL(int32_t,   i32Dst);
            IEM_MC_ARG_LOCAL_REF(int32_t *,  pi32Dst, i32Dst,     0);
            IEM_MC_ARG(const uint32_t *,     pu32Src,             1);

            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE(); /** @todo: This is superfluous because IEM_MC_CALL_SSE_AIMPL_2() is calling this but the tstIEMCheckMc testcase depends on it. */

            IEM_MC_REF_XREG_U32_CONST(pu32Src, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvttss2si_i32_r32, pi32Dst, pu32Src);
            IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), i32Dst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /* greg, [mem] */
            IEM_MC_BEGIN(0, 0);
            IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc);
            IEM_MC_LOCAL(int32_t,   i32Dst);
            IEM_MC_LOCAL(uint32_t,  u32Src);
            IEM_MC_ARG_LOCAL_REF(int32_t *,         pi32Dst, i32Dst,     0);
            IEM_MC_ARG_LOCAL_REF(const uint32_t *,  pu32Src, u32Src,     1);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE(); /** @todo: This is superfluous because IEM_MC_CALL_SSE_AIMPL_2() is calling this but the tstIEMCheckMc testcase depends on it. */

            IEM_MC_FETCH_MEM_U32(u32Src, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvttss2si_i32_r32, pi32Dst, pu32Src);
            IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), i32Dst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/** Opcode 0xf2 0x0f 0x2c - cvttsd2si Gy, Wsd */
FNIEMOP_DEF(iemOp_cvttsd2si_Gy_Wsd)
{
    IEMOP_MNEMONIC2(RM, CVTTSD2SI, cvttsd2si, Gy, Wsd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_REX_W)
    {
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* greg64, XMM */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_LOCAL(int64_t,   i64Dst);
            IEM_MC_ARG_LOCAL_REF(int64_t *,  pi64Dst, i64Dst,     0);
            IEM_MC_ARG(const uint64_t *,     pu64Src,             1);

            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE(); /** @todo: This is superfluous because IEM_MC_CALL_SSE_AIMPL_2() is calling this but the tstIEMCheckMc testcase depends on it. */

            IEM_MC_REF_XREG_U64_CONST(pu64Src, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvttsd2si_i64_r64, pi64Dst, pu64Src);
            IEM_MC_STORE_GREG_I64(IEM_GET_MODRM_REG(pVCpu, bRm), i64Dst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /* greg64, [mem64] */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc);
            IEM_MC_LOCAL(int64_t,   i64Dst);
            IEM_MC_LOCAL(uint64_t,  u64Src);
            IEM_MC_ARG_LOCAL_REF(int64_t *,         pi64Dst, i64Dst,     0);
            IEM_MC_ARG_LOCAL_REF(const uint64_t *,  pu64Src, u64Src,     1);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE(); /** @todo: This is superfluous because IEM_MC_CALL_SSE_AIMPL_2() is calling this but the tstIEMCheckMc testcase depends on it. */

            IEM_MC_FETCH_MEM_U64(u64Src, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvttsd2si_i64_r64, pi64Dst, pu64Src);
            IEM_MC_STORE_GREG_I64(IEM_GET_MODRM_REG(pVCpu, bRm), i64Dst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* greg, XMM */
            IEM_MC_BEGIN(0, 0);
            IEM_MC_LOCAL(int32_t,   i32Dst);
            IEM_MC_ARG_LOCAL_REF(int32_t *,  pi32Dst, i32Dst,     0);
            IEM_MC_ARG(const uint64_t *,     pu64Src,             1);

            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE(); /** @todo: This is superfluous because IEM_MC_CALL_SSE_AIMPL_2() is calling this but the tstIEMCheckMc testcase depends on it. */

            IEM_MC_REF_XREG_U64_CONST(pu64Src, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvttsd2si_i32_r64, pi32Dst, pu64Src);
            IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), i32Dst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /* greg32, [mem32] */
            IEM_MC_BEGIN(0, 0);
            IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc);
            IEM_MC_LOCAL(int32_t,   i32Dst);
            IEM_MC_LOCAL(uint64_t,  u64Src);
            IEM_MC_ARG_LOCAL_REF(int32_t *,         pi32Dst, i32Dst,     0);
            IEM_MC_ARG_LOCAL_REF(const uint64_t *,  pu64Src, u64Src,     1);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE(); /** @todo: This is superfluous because IEM_MC_CALL_SSE_AIMPL_2() is calling this but the tstIEMCheckMc testcase depends on it. */

            IEM_MC_FETCH_MEM_U64(u64Src, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvttsd2si_i32_r64, pi32Dst, pu64Src);
            IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), i32Dst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/** Opcode      0x0f 0x2d - cvtps2pi Ppi, Wps */
FNIEMOP_DEF(iemOp_cvtps2pi_Ppi_Wps)
{
    IEMOP_MNEMONIC2(RM, CVTPS2PI, cvtps2pi, Pq, Wps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0); /// @todo
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_LOCAL(uint64_t,              u64Dst);
        IEM_MC_ARG_LOCAL_REF(uint64_t *,    pu64Dst, u64Dst,    0);
        IEM_MC_ARG(uint64_t,                u64Src,             1);

        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_FETCH_XREG_U64(u64Src, IEM_GET_MODRM_RM(pVCpu, bRm), 0 /* a_iQword*/);

        IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvtps2pi_u128, pu64Dst, u64Src);
        IEM_MC_STORE_MREG_U64(IEM_GET_MODRM_REG_8(bRm), u64Dst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(uint64_t,              u64Dst);
        IEM_MC_ARG_LOCAL_REF(uint64_t *,    pu64Dst, u64Dst,    0);
        IEM_MC_ARG(uint64_t,                u64Src,             1);
        IEM_MC_LOCAL(RTGCPTR,               GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_U64(u64Src, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvtps2pi_u128, pu64Dst, u64Src);
        IEM_MC_STORE_MREG_U64(IEM_GET_MODRM_REG_8(bRm), u64Dst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode 0x66 0x0f 0x2d - cvtpd2pi Qpi, Wpd */
FNIEMOP_DEF(iemOp_cvtpd2pi_Qpi_Wpd)
{
    IEMOP_MNEMONIC2(RM, CVTPD2PI, cvtpd2pi, Pq, Wpd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0); /// @todo
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_LOCAL(uint64_t,              u64Dst);
        IEM_MC_ARG_LOCAL_REF(uint64_t *,    pu64Dst, u64Dst,    0);
        IEM_MC_ARG(PCX86XMMREG,             pSrc,               1);

        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_REF_XREG_XMM_CONST(pSrc, IEM_GET_MODRM_RM(pVCpu, bRm));

        IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvtpd2pi_u128, pu64Dst, pSrc);
        IEM_MC_STORE_MREG_U64(IEM_GET_MODRM_REG_8(bRm), u64Dst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(uint64_t,              u64Dst);
        IEM_MC_ARG_LOCAL_REF(uint64_t *,    pu64Dst,    u64Dst, 0);
        IEM_MC_LOCAL(X86XMMREG,             uSrc);
        IEM_MC_ARG_LOCAL_REF(PCX86XMMREG,   pSrc,       uSrc,   1);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_XMM_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvtpd2pi_u128, pu64Dst, pSrc);
        IEM_MC_STORE_MREG_U64(IEM_GET_MODRM_REG_8(bRm), u64Dst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode 0xf3 0x0f 0x2d - cvtss2si Gy, Wss */
FNIEMOP_DEF(iemOp_cvtss2si_Gy_Wss)
{
    IEMOP_MNEMONIC2(RM, CVTSS2SI, cvtss2si, Gy, Wsd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_REX_W)
    {
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* greg64, XMM */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_LOCAL(int64_t,   i64Dst);
            IEM_MC_ARG_LOCAL_REF(int64_t *,  pi64Dst, i64Dst,     0);
            IEM_MC_ARG(const uint32_t *,     pu32Src,             1);

            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE(); /** @todo: This is superfluous because IEM_MC_CALL_SSE_AIMPL_2() is calling this but the tstIEMCheckMc testcase depends on it. */

            IEM_MC_REF_XREG_U32_CONST(pu32Src, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvtss2si_i64_r32, pi64Dst, pu32Src);
            IEM_MC_STORE_GREG_I64(IEM_GET_MODRM_REG(pVCpu, bRm), i64Dst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /* greg64, [mem64] */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc);
            IEM_MC_LOCAL(int64_t,   i64Dst);
            IEM_MC_LOCAL(uint32_t,  u32Src);
            IEM_MC_ARG_LOCAL_REF(int64_t *,         pi64Dst, i64Dst,     0);
            IEM_MC_ARG_LOCAL_REF(const uint32_t *,  pu32Src, u32Src,     1);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE(); /** @todo: This is superfluous because IEM_MC_CALL_SSE_AIMPL_2() is calling this but the tstIEMCheckMc testcase depends on it. */

            IEM_MC_FETCH_MEM_U32(u32Src, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvtss2si_i64_r32, pi64Dst, pu32Src);
            IEM_MC_STORE_GREG_I64(IEM_GET_MODRM_REG(pVCpu, bRm), i64Dst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* greg, XMM */
            IEM_MC_BEGIN(0, 0);
            IEM_MC_LOCAL(int32_t,   i32Dst);
            IEM_MC_ARG_LOCAL_REF(int32_t *,  pi32Dst, i32Dst,     0);
            IEM_MC_ARG(const uint32_t *,     pu32Src,             1);

            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE(); /** @todo: This is superfluous because IEM_MC_CALL_SSE_AIMPL_2() is calling this but the tstIEMCheckMc testcase depends on it. */

            IEM_MC_REF_XREG_U32_CONST(pu32Src, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvtss2si_i32_r32,  pi32Dst, pu32Src);
            IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), i32Dst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /* greg, [mem] */
            IEM_MC_BEGIN(0, 0);
            IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc);
            IEM_MC_LOCAL(int32_t,   i32Dst);
            IEM_MC_LOCAL(uint32_t,  u32Src);
            IEM_MC_ARG_LOCAL_REF(int32_t *,         pi32Dst, i32Dst,     0);
            IEM_MC_ARG_LOCAL_REF(const uint32_t *,  pu32Src, u32Src,     1);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE(); /** @todo: This is superfluous because IEM_MC_CALL_SSE_AIMPL_2() is calling this but the tstIEMCheckMc testcase depends on it. */

            IEM_MC_FETCH_MEM_U32(u32Src, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvtss2si_i32_r32, pi32Dst, pu32Src);
            IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), i32Dst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/** Opcode 0xf2 0x0f 0x2d - cvtsd2si Gy, Wsd */
FNIEMOP_DEF(iemOp_cvtsd2si_Gy_Wsd)
{
    IEMOP_MNEMONIC2(RM, CVTSD2SI, cvtsd2si, Gy, Wsd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_REX_W)
    {
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* greg64, XMM */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_LOCAL(int64_t,   i64Dst);
            IEM_MC_ARG_LOCAL_REF(int64_t *,  pi64Dst, i64Dst,     0);
            IEM_MC_ARG(const uint64_t *,     pu64Src,             1);

            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE(); /** @todo: This is superfluous because IEM_MC_CALL_SSE_AIMPL_2() is calling this but the tstIEMCheckMc testcase depends on it. */

            IEM_MC_REF_XREG_U64_CONST(pu64Src, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvtsd2si_i64_r64, pi64Dst, pu64Src);
            IEM_MC_STORE_GREG_I64(IEM_GET_MODRM_REG(pVCpu, bRm), i64Dst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /* greg64, [mem64] */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc);
            IEM_MC_LOCAL(int64_t,   i64Dst);
            IEM_MC_LOCAL(uint64_t,  u64Src);
            IEM_MC_ARG_LOCAL_REF(int64_t *,         pi64Dst, i64Dst,     0);
            IEM_MC_ARG_LOCAL_REF(const uint64_t *,  pu64Src, u64Src,     1);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE(); /** @todo: This is superfluous because IEM_MC_CALL_SSE_AIMPL_3() is calling this but the tstIEMCheckMc testcase depends on it. */

            IEM_MC_FETCH_MEM_U64(u64Src, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvtsd2si_i64_r64, pi64Dst, pu64Src);
            IEM_MC_STORE_GREG_I64(IEM_GET_MODRM_REG(pVCpu, bRm), i64Dst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* greg32, XMM */
            IEM_MC_BEGIN(0, 0);
            IEM_MC_LOCAL(int32_t,   i32Dst);
            IEM_MC_ARG_LOCAL_REF(int32_t *,  pi32Dst, i32Dst,     0);
            IEM_MC_ARG(const uint64_t *,     pu64Src,             1);

            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE(); /** @todo: This is superfluous because IEM_MC_CALL_SSE_AIMPL_2() is calling this but the tstIEMCheckMc testcase depends on it. */

            IEM_MC_REF_XREG_U64_CONST(pu64Src, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvtsd2si_i32_r64, pi32Dst, pu64Src);
            IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), i32Dst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /* greg32, [mem64] */
            IEM_MC_BEGIN(0, 0);
            IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc);
            IEM_MC_LOCAL(int32_t,   i32Dst);
            IEM_MC_LOCAL(uint64_t,  u64Src);
            IEM_MC_ARG_LOCAL_REF(int32_t *,         pi32Dst, i32Dst,     0);
            IEM_MC_ARG_LOCAL_REF(const uint64_t *,  pu64Src, u64Src,     1);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE(); /** @todo: This is superfluous because IEM_MC_CALL_SSE_AIMPL_2() is calling this but the tstIEMCheckMc testcase depends on it. */

            IEM_MC_FETCH_MEM_U64(u64Src, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvtsd2si_i32_r64, pi32Dst, pu64Src);
            IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), i32Dst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/**
 * @opcode      0x2e
 * @oppfx       none
 * @opflmodify  cf,pf,af,zf,sf,of
 * @opflclear   af,sf,of
 */
FNIEMOP_DEF(iemOp_ucomiss_Vss_Wss)
{
    IEMOP_MNEMONIC2(RM, UCOMISS, ucomiss, Vss, Wss, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_LOCAL(uint32_t, fEFlags);
        IEM_MC_ARG_LOCAL_REF(uint32_t *,        pEFlags, fEFlags,   0);
        IEM_MC_ARG(RTFLOAT32U,                  uSrc1,              1);
        IEM_MC_ARG(RTFLOAT32U,                  uSrc2,              2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_FETCH_EFLAGS(fEFlags);
        IEM_MC_FETCH_XREG_R32(uSrc1, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iDWord*/);
        IEM_MC_FETCH_XREG_R32(uSrc2, IEM_GET_MODRM_RM(pVCpu, bRm), 0 /*a_iDWord*/);
        IEM_MC_CALL_SSE_AIMPL_3(iemAImpl_ucomiss_u128, pEFlags, uSrc1, uSrc2);
        IEM_MC_COMMIT_EFLAGS(fEFlags);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(uint32_t, fEFlags);
        IEM_MC_ARG_LOCAL_REF(uint32_t *,        pEFlags, fEFlags,   0);
        IEM_MC_ARG(RTFLOAT32U,                  uSrc1,              1);
        IEM_MC_ARG(RTFLOAT32U,                  uSrc2,              2);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_R32(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_FETCH_EFLAGS(fEFlags);
        IEM_MC_FETCH_XREG_R32(uSrc1, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iDWord*/);
        IEM_MC_CALL_SSE_AIMPL_3(iemAImpl_ucomiss_u128, pEFlags, uSrc1, uSrc2);
        IEM_MC_COMMIT_EFLAGS(fEFlags);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x2e
 * @oppfx       0x66
 * @opflmodify  cf,pf,af,zf,sf,of
 * @opflclear   af,sf,of
 */
FNIEMOP_DEF(iemOp_ucomisd_Vsd_Wsd)
{
    IEMOP_MNEMONIC2(RM, UCOMISD, ucomisd, Vsd, Wsd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_LOCAL(uint32_t, fEFlags);
        IEM_MC_ARG_LOCAL_REF(uint32_t *,        pEFlags, fEFlags,   0);
        IEM_MC_ARG(RTFLOAT64U,                  uSrc1,             1);
        IEM_MC_ARG(RTFLOAT64U,                  uSrc2,              2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_FETCH_EFLAGS(fEFlags);
        IEM_MC_FETCH_XREG_R64(uSrc1, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iQWord*/);
        IEM_MC_FETCH_XREG_R64(uSrc2, IEM_GET_MODRM_RM(pVCpu, bRm),  0 /*a_iQWord*/);
        IEM_MC_CALL_SSE_AIMPL_3(iemAImpl_ucomisd_u128, pEFlags, uSrc1, uSrc2);
        IEM_MC_COMMIT_EFLAGS(fEFlags);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(uint32_t, fEFlags);
        IEM_MC_ARG_LOCAL_REF(uint32_t *,        pEFlags, fEFlags,   0);
        IEM_MC_ARG(RTFLOAT64U,                  uSrc1,              1);
        IEM_MC_ARG(RTFLOAT64U,                  uSrc2,              2);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_R64(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_FETCH_EFLAGS(fEFlags);
        IEM_MC_FETCH_XREG_R64(uSrc1, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iQWord*/);
        IEM_MC_CALL_SSE_AIMPL_3(iemAImpl_ucomisd_u128, pEFlags, uSrc1, uSrc2);
        IEM_MC_COMMIT_EFLAGS(fEFlags);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/*  Opcode 0xf3 0x0f 0x2e - invalid */
/*  Opcode 0xf2 0x0f 0x2e - invalid */


/**
 * @opcode      0x2e
 * @oppfx       none
 * @opflmodify  cf,pf,af,zf,sf,of
 * @opflclear   af,sf,of
 */
FNIEMOP_DEF(iemOp_comiss_Vss_Wss)
{
    IEMOP_MNEMONIC2(RM, COMISS, comiss, Vss, Wss, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_LOCAL(uint32_t, fEFlags);
        IEM_MC_ARG_LOCAL_REF(uint32_t *,        pEFlags, fEFlags,   0);
        IEM_MC_ARG(RTFLOAT32U,                  uSrc1,              1);
        IEM_MC_ARG(RTFLOAT32U,                  uSrc2,              2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_FETCH_EFLAGS(fEFlags);
        IEM_MC_FETCH_XREG_R32(uSrc1, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iDWord*/);
        IEM_MC_FETCH_XREG_R32(uSrc2, IEM_GET_MODRM_RM(pVCpu, bRm),  0 /*a_iDWord*/);
        IEM_MC_CALL_SSE_AIMPL_3(iemAImpl_comiss_u128, pEFlags, uSrc1, uSrc2);
        IEM_MC_COMMIT_EFLAGS(fEFlags);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(uint32_t, fEFlags);
        IEM_MC_ARG_LOCAL_REF(uint32_t *,        pEFlags, fEFlags,   0);
        IEM_MC_ARG(RTFLOAT32U,                  uSrc1,              1);
        IEM_MC_ARG(RTFLOAT32U,                  uSrc2,              2);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_R32(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_FETCH_EFLAGS(fEFlags);
        IEM_MC_FETCH_XREG_R32(uSrc1, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iDWord*/);
        IEM_MC_CALL_SSE_AIMPL_3(iemAImpl_comiss_u128, pEFlags, uSrc1, uSrc2);
        IEM_MC_COMMIT_EFLAGS(fEFlags);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x2f
 * @oppfx       0x66
 * @opflmodify  cf,pf,af,zf,sf,of
 * @opflclear   af,sf,of
 */
FNIEMOP_DEF(iemOp_comisd_Vsd_Wsd)
{
    IEMOP_MNEMONIC2(RM, COMISD, comisd, Vsd, Wsd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_LOCAL(uint32_t, fEFlags);
        IEM_MC_ARG_LOCAL_REF(uint32_t *,        pEFlags, fEFlags,   0);
        IEM_MC_ARG(RTFLOAT64U,                  uSrc1,              1);
        IEM_MC_ARG(RTFLOAT64U,                  uSrc2,              2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_FETCH_EFLAGS(fEFlags);
        IEM_MC_FETCH_XREG_R64(uSrc1, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iQWord*/);
        IEM_MC_FETCH_XREG_R64(uSrc2, IEM_GET_MODRM_RM(pVCpu, bRm),  0 /*a_iQWord*/);
        IEM_MC_CALL_SSE_AIMPL_3(iemAImpl_comisd_u128, pEFlags, uSrc1, uSrc2);
        IEM_MC_COMMIT_EFLAGS(fEFlags);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(uint32_t, fEFlags);
        IEM_MC_ARG_LOCAL_REF(uint32_t *,        pEFlags, fEFlags,   0);
        IEM_MC_ARG(RTFLOAT64U,                  uSrc1,              1);
        IEM_MC_ARG(RTFLOAT64U,                  uSrc2,              2);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_R64(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_FETCH_EFLAGS(fEFlags);
        IEM_MC_FETCH_XREG_R64(uSrc1, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iQWord*/);
        IEM_MC_CALL_SSE_AIMPL_3(iemAImpl_comisd_u128, pEFlags, uSrc1, uSrc2);
        IEM_MC_COMMIT_EFLAGS(fEFlags);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/*  Opcode 0xf3 0x0f 0x2f - invalid */
/*  Opcode 0xf2 0x0f 0x2f - invalid */

/** Opcode 0x0f 0x30. */
FNIEMOP_DEF(iemOp_wrmsr)
{
    IEMOP_MNEMONIC(wrmsr, "wrmsr");
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_wrmsr);
}


/** Opcode 0x0f 0x31. */
FNIEMOP_DEF(iemOp_rdtsc)
{
    IEMOP_MNEMONIC(rdtsc, "rdtsc");
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_VMEXIT,
                                  RT_BIT_64(kIemNativeGstReg_GprFirst + X86_GREG_xAX)
                                | RT_BIT_64(kIemNativeGstReg_GprFirst + X86_GREG_xDX),
                                iemCImpl_rdtsc);
}


/** Opcode 0x0f 0x33. */
FNIEMOP_DEF(iemOp_rdmsr)
{
    IEMOP_MNEMONIC(rdmsr, "rdmsr");
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_VMEXIT,
                                  RT_BIT_64(kIemNativeGstReg_GprFirst + X86_GREG_xAX)
                                | RT_BIT_64(kIemNativeGstReg_GprFirst + X86_GREG_xDX),
                                iemCImpl_rdmsr);
}


/** Opcode 0x0f 0x34. */
FNIEMOP_DEF(iemOp_rdpmc)
{
    IEMOP_MNEMONIC(rdpmc, "rdpmc");
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_VMEXIT,
                                  RT_BIT_64(kIemNativeGstReg_GprFirst + X86_GREG_xAX)
                                | RT_BIT_64(kIemNativeGstReg_GprFirst + X86_GREG_xDX),
                                iemCImpl_rdpmc);
}


/** Opcode 0x0f 0x34. */
FNIEMOP_DEF(iemOp_sysenter)
{
    IEMOP_MNEMONIC0(FIXED, SYSENTER, sysenter, DISOPTYPE_CONTROLFLOW | DISOPTYPE_UNCOND_CONTROLFLOW, 0);
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_BRANCH_INDIRECT | IEM_CIMPL_F_BRANCH_FAR | IEM_CIMPL_F_BRANCH_STACK_FAR
                                | IEM_CIMPL_F_MODE | IEM_CIMPL_F_RFLAGS | IEM_CIMPL_F_VMEXIT | IEM_CIMPL_F_END_TB, 0,
                                iemCImpl_sysenter);
}

/** Opcode 0x0f 0x35. */
FNIEMOP_DEF(iemOp_sysexit)
{
    IEMOP_MNEMONIC0(FIXED, SYSEXIT, sysexit, DISOPTYPE_CONTROLFLOW | DISOPTYPE_UNCOND_CONTROLFLOW, 0);
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_DEFER_TO_CIMPL_1_RET(IEM_CIMPL_F_BRANCH_INDIRECT | IEM_CIMPL_F_BRANCH_FAR | IEM_CIMPL_F_BRANCH_STACK_FAR
                                | IEM_CIMPL_F_MODE | IEM_CIMPL_F_RFLAGS | IEM_CIMPL_F_VMEXIT | IEM_CIMPL_F_END_TB, 0,
                                iemCImpl_sysexit, pVCpu->iem.s.enmEffOpSize);
}

/** Opcode 0x0f 0x37. */
FNIEMOP_STUB(iemOp_getsec);


/** Opcode 0x0f 0x38. */
FNIEMOP_DEF(iemOp_3byte_Esc_0f_38)
{
#ifdef IEM_WITH_THREE_0F_38
    uint8_t b; IEM_OPCODE_GET_NEXT_U8(&b);
    return FNIEMOP_CALL(g_apfnThreeByte0f38[(uintptr_t)b * 4 + pVCpu->iem.s.idxPrefix]);
#else
    IEMOP_BITCH_ABOUT_STUB();
    return VERR_IEM_INSTR_NOT_IMPLEMENTED;
#endif
}


/** Opcode 0x0f 0x3a. */
FNIEMOP_DEF(iemOp_3byte_Esc_0f_3a)
{
#ifdef IEM_WITH_THREE_0F_3A
    uint8_t b; IEM_OPCODE_GET_NEXT_U8(&b);
    return FNIEMOP_CALL(g_apfnThreeByte0f3a[(uintptr_t)b * 4 + pVCpu->iem.s.idxPrefix]);
#else
    IEMOP_BITCH_ABOUT_STUB();
    return VERR_IEM_INSTR_NOT_IMPLEMENTED;
#endif
}


/**
 * Implements a conditional move.
 *
 * Wish there was an obvious way to do this where we could share and reduce
 * code bloat.
 *
 * @param   a_Cnd       The conditional "microcode" operation.
 */
#define CMOV_X(a_Cnd) \
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm); \
    if (IEM_IS_MODRM_REG_MODE(bRm)) \
    { \
        switch (pVCpu->iem.s.enmEffOpSize) \
        { \
            case IEMMODE_16BIT: \
                IEM_MC_BEGIN(0, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_LOCAL(uint16_t, u16Tmp); \
                a_Cnd { \
                    IEM_MC_FETCH_GREG_U16(u16Tmp, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                    IEM_MC_STORE_GREG_U16(IEM_GET_MODRM_REG(pVCpu, bRm), u16Tmp); \
                } IEM_MC_ENDIF(); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            case IEMMODE_32BIT: \
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_LOCAL(uint32_t, u32Tmp); \
                a_Cnd { \
                    IEM_MC_FETCH_GREG_U32(u32Tmp, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                    IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), u32Tmp); \
                } IEM_MC_ELSE() { \
                    IEM_MC_CLEAR_HIGH_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm)); \
                } IEM_MC_ENDIF(); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            case IEMMODE_64BIT: \
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_LOCAL(uint64_t, u64Tmp); \
                a_Cnd { \
                    IEM_MC_FETCH_GREG_U64(u64Tmp, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                    IEM_MC_STORE_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), u64Tmp); \
                } IEM_MC_ENDIF(); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            IEM_NOT_REACHED_DEFAULT_CASE_RET(); \
        } \
    } \
    else \
    { \
        switch (pVCpu->iem.s.enmEffOpSize) \
        { \
            case IEMMODE_16BIT: \
                IEM_MC_BEGIN(0, 0); \
                IEM_MC_LOCAL(RTGCPTR,  GCPtrEffSrc); \
                IEM_MC_LOCAL(uint16_t, u16Tmp); \
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_FETCH_MEM_U16(u16Tmp, pVCpu->iem.s.iEffSeg, GCPtrEffSrc); \
                a_Cnd { \
                    IEM_MC_STORE_GREG_U16(IEM_GET_MODRM_REG(pVCpu, bRm), u16Tmp); \
                } IEM_MC_ENDIF(); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            case IEMMODE_32BIT: \
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                IEM_MC_LOCAL(RTGCPTR,  GCPtrEffSrc); \
                IEM_MC_LOCAL(uint32_t, u32Tmp); \
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_FETCH_MEM_U32(u32Tmp, pVCpu->iem.s.iEffSeg, GCPtrEffSrc); \
                a_Cnd { \
                    IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), u32Tmp); \
                } IEM_MC_ELSE() { \
                    IEM_MC_CLEAR_HIGH_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm)); \
                } IEM_MC_ENDIF(); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            case IEMMODE_64BIT: \
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
                IEM_MC_LOCAL(RTGCPTR,  GCPtrEffSrc); \
                IEM_MC_LOCAL(uint64_t, u64Tmp); \
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_FETCH_MEM_U64(u64Tmp, pVCpu->iem.s.iEffSeg, GCPtrEffSrc); \
                a_Cnd { \
                    IEM_MC_STORE_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), u64Tmp); \
                } IEM_MC_ENDIF(); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            IEM_NOT_REACHED_DEFAULT_CASE_RET(); \
        } \
    } do {} while (0)



/**
 * @opcode      0x40
 * @opfltest    of
 */
FNIEMOP_DEF(iemOp_cmovo_Gv_Ev)
{
    IEMOP_MNEMONIC(cmovo_Gv_Ev, "cmovo Gv,Ev");
    CMOV_X(IEM_MC_IF_EFL_BIT_SET(X86_EFL_OF));
}


/**
 * @opcode      0x41
 * @opfltest    of
 */
FNIEMOP_DEF(iemOp_cmovno_Gv_Ev)
{
    IEMOP_MNEMONIC(cmovno_Gv_Ev, "cmovno Gv,Ev");
    CMOV_X(IEM_MC_IF_EFL_BIT_NOT_SET(X86_EFL_OF));
}


/**
 * @opcode      0x42
 * @opfltest    cf
 */
FNIEMOP_DEF(iemOp_cmovc_Gv_Ev)
{
    IEMOP_MNEMONIC(cmovc_Gv_Ev, "cmovc Gv,Ev");
    CMOV_X(IEM_MC_IF_EFL_BIT_SET(X86_EFL_CF));
}


/**
 * @opcode      0x43
 * @opfltest    cf
 */
FNIEMOP_DEF(iemOp_cmovnc_Gv_Ev)
{
    IEMOP_MNEMONIC(cmovnc_Gv_Ev, "cmovnc Gv,Ev");
    CMOV_X(IEM_MC_IF_EFL_BIT_NOT_SET(X86_EFL_CF));
}


/**
 * @opcode      0x44
 * @opfltest    zf
 */
FNIEMOP_DEF(iemOp_cmove_Gv_Ev)
{
    IEMOP_MNEMONIC(cmove_Gv_Ev, "cmove Gv,Ev");
    CMOV_X(IEM_MC_IF_EFL_BIT_SET(X86_EFL_ZF));
}


/**
 * @opcode      0x45
 * @opfltest    zf
 */
FNIEMOP_DEF(iemOp_cmovne_Gv_Ev)
{
    IEMOP_MNEMONIC(cmovne_Gv_Ev, "cmovne Gv,Ev");
    CMOV_X(IEM_MC_IF_EFL_BIT_NOT_SET(X86_EFL_ZF));
}


/**
 * @opcode      0x46
 * @opfltest    cf,zf
 */
FNIEMOP_DEF(iemOp_cmovbe_Gv_Ev)
{
    IEMOP_MNEMONIC(cmovbe_Gv_Ev, "cmovbe Gv,Ev");
    CMOV_X(IEM_MC_IF_EFL_ANY_BITS_SET(X86_EFL_CF | X86_EFL_ZF));
}


/**
 * @opcode      0x47
 * @opfltest    cf,zf
 */
FNIEMOP_DEF(iemOp_cmovnbe_Gv_Ev)
{
    IEMOP_MNEMONIC(cmovnbe_Gv_Ev, "cmovnbe Gv,Ev");
    CMOV_X(IEM_MC_IF_EFL_NO_BITS_SET(X86_EFL_CF | X86_EFL_ZF));
}


/**
 * @opcode      0x48
 * @opfltest    sf
 */
FNIEMOP_DEF(iemOp_cmovs_Gv_Ev)
{
    IEMOP_MNEMONIC(cmovs_Gv_Ev, "cmovs Gv,Ev");
    CMOV_X(IEM_MC_IF_EFL_BIT_SET(X86_EFL_SF));
}


/**
 * @opcode      0x49
 * @opfltest    sf
 */
FNIEMOP_DEF(iemOp_cmovns_Gv_Ev)
{
    IEMOP_MNEMONIC(cmovns_Gv_Ev, "cmovns Gv,Ev");
    CMOV_X(IEM_MC_IF_EFL_BIT_NOT_SET(X86_EFL_SF));
}


/**
 * @opcode      0x4a
 * @opfltest    pf
 */
FNIEMOP_DEF(iemOp_cmovp_Gv_Ev)
{
    IEMOP_MNEMONIC(cmovp_Gv_Ev, "cmovp Gv,Ev");
    CMOV_X(IEM_MC_IF_EFL_BIT_SET(X86_EFL_PF));
}


/**
 * @opcode      0x4b
 * @opfltest    pf
 */
FNIEMOP_DEF(iemOp_cmovnp_Gv_Ev)
{
    IEMOP_MNEMONIC(cmovnp_Gv_Ev, "cmovnp Gv,Ev");
    CMOV_X(IEM_MC_IF_EFL_BIT_NOT_SET(X86_EFL_PF));
}


/**
 * @opcode      0x4c
 * @opfltest    sf,of
 */
FNIEMOP_DEF(iemOp_cmovl_Gv_Ev)
{
    IEMOP_MNEMONIC(cmovl_Gv_Ev, "cmovl Gv,Ev");
    CMOV_X(IEM_MC_IF_EFL_BITS_NE(X86_EFL_SF, X86_EFL_OF));
}


/**
 * @opcode      0x4d
 * @opfltest    sf,of
 */
FNIEMOP_DEF(iemOp_cmovnl_Gv_Ev)
{
    IEMOP_MNEMONIC(cmovnl_Gv_Ev, "cmovnl Gv,Ev");
    CMOV_X(IEM_MC_IF_EFL_BITS_EQ(X86_EFL_SF, X86_EFL_OF));
}


/**
 * @opcode      0x4e
 * @opfltest    zf,sf,of
 */
FNIEMOP_DEF(iemOp_cmovle_Gv_Ev)
{
    IEMOP_MNEMONIC(cmovle_Gv_Ev, "cmovle Gv,Ev");
    CMOV_X(IEM_MC_IF_EFL_BIT_SET_OR_BITS_NE(X86_EFL_ZF, X86_EFL_SF, X86_EFL_OF));
}


/**
 * @opcode      0x4e
 * @opfltest    zf,sf,of
 */
FNIEMOP_DEF(iemOp_cmovnle_Gv_Ev)
{
    IEMOP_MNEMONIC(cmovnle_Gv_Ev, "cmovnle Gv,Ev");
    CMOV_X(IEM_MC_IF_EFL_BIT_NOT_SET_AND_BITS_EQ(X86_EFL_ZF, X86_EFL_SF, X86_EFL_OF));
}

#undef CMOV_X

/** Opcode      0x0f 0x50 - movmskps Gy, Ups */
FNIEMOP_DEF(iemOp_movmskps_Gy_Ups)
{
    IEMOP_MNEMONIC2(RM_REG, MOVMSKPS, movmskps, Gy, Ux, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0); /** @todo */
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_LOCAL(uint8_t,           u8Dst);
        IEM_MC_ARG_LOCAL_REF(uint8_t *, pu8Dst,  u8Dst, 0);
        IEM_MC_ARG(PCRTUINT128U,        puSrc,          1);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128_CONST(puSrc, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_2(iemAImpl_movmskps_u128, pu8Dst, puSrc);
        IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), u8Dst);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    /* No memory operand. */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}


/** Opcode 0x66 0x0f 0x50 - movmskpd Gy, Upd */
FNIEMOP_DEF(iemOp_movmskpd_Gy_Upd)
{
    IEMOP_MNEMONIC2(RM_REG, MOVMSKPD, movmskpd, Gy, Ux, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0); /** @todo */
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_LOCAL(uint8_t,           u8Dst);
        IEM_MC_ARG_LOCAL_REF(uint8_t *, pu8Dst,  u8Dst, 0);
        IEM_MC_ARG(PCRTUINT128U,        puSrc,          1);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128_CONST(puSrc, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_2(iemAImpl_movmskpd_u128, pu8Dst, puSrc);
        IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), u8Dst);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    /* No memory operand. */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();

}


/*  Opcode 0xf3 0x0f 0x50 - invalid */
/*  Opcode 0xf2 0x0f 0x50 - invalid */


/** Opcode      0x0f 0x51 - sqrtps Vps, Wps */
FNIEMOP_DEF(iemOp_sqrtps_Vps_Wps)
{
    IEMOP_MNEMONIC2(RM, SQRTPS, sqrtps, Vps, Wps, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSseFp_FullFull_To_Full, iemAImpl_sqrtps_u128);
}


/** Opcode 0x66 0x0f 0x51 - sqrtpd Vpd, Wpd */
FNIEMOP_DEF(iemOp_sqrtpd_Vpd_Wpd)
{
    IEMOP_MNEMONIC2(RM, SQRTPD, sqrtpd, Vpd, Wpd, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Fp_FullFull_To_Full, iemAImpl_sqrtpd_u128);
}


/** Opcode 0xf3 0x0f 0x51 - sqrtss Vss, Wss */
FNIEMOP_DEF(iemOp_sqrtss_Vss_Wss)
{
    IEMOP_MNEMONIC2(RM, SQRTSS, sqrtss, Vss, Wss, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSseFp_FullR32_To_Full, iemAImpl_sqrtss_u128_r32);
}


/** Opcode 0xf2 0x0f 0x51 - sqrtsd Vsd, Wsd */
FNIEMOP_DEF(iemOp_sqrtsd_Vsd_Wsd)
{
    IEMOP_MNEMONIC2(RM, SQRTSD, sqrtsd, Vsd, Wsd, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Fp_FullR64_To_Full, iemAImpl_sqrtsd_u128_r64);
}


/** Opcode      0x0f 0x52 - rsqrtps Vps, Wps */
FNIEMOP_DEF(iemOp_rsqrtps_Vps_Wps)
{
    IEMOP_MNEMONIC2(RM, RSQRTPS, rsqrtps, Vps, Wps, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSseFp_FullFull_To_Full, iemAImpl_rsqrtps_u128);
}


/*  Opcode 0x66 0x0f 0x52 - invalid */


/** Opcode 0xf3 0x0f 0x52 - rsqrtss Vss, Wss */
FNIEMOP_DEF(iemOp_rsqrtss_Vss_Wss)
{
    IEMOP_MNEMONIC2(RM, RSQRTSS, rsqrtss, Vss, Wss, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSseFp_FullR32_To_Full, iemAImpl_rsqrtss_u128_r32);
}


/*  Opcode 0xf2 0x0f 0x52 - invalid */


/** Opcode      0x0f 0x53 - rcpps Vps, Wps */
FNIEMOP_DEF(iemOp_rcpps_Vps_Wps)
{
    IEMOP_MNEMONIC2(RM, RCPPS, rcpps, Vps, Wps, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSseFp_FullFull_To_Full, iemAImpl_rcpps_u128);
}


/*  Opcode 0x66 0x0f 0x53 - invalid */


/** Opcode 0xf3 0x0f 0x53 - rcpss Vss, Wss */
FNIEMOP_DEF(iemOp_rcpss_Vss_Wss)
{
    IEMOP_MNEMONIC2(RM, RCPSS, rcpss, Vss, Wss, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSseFp_FullR32_To_Full, iemAImpl_rcpss_u128_r32);
}


/*  Opcode 0xf2 0x0f 0x53 - invalid */


/** Opcode      0x0f 0x54 - andps Vps, Wps */
FNIEMOP_DEF(iemOp_andps_Vps_Wps)
{
    IEMOP_MNEMONIC2(RM, ANDPS, andps, Vps, Wps, DISOPTYPE_HARMLESS, 0);
    SSE2_OPT_BODY_FullFull_To_Full(pand, iemAImpl_pand_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/** Opcode 0x66 0x0f 0x54 - andpd Vpd, Wpd */
FNIEMOP_DEF(iemOp_andpd_Vpd_Wpd)
{
    IEMOP_MNEMONIC2(RM, ANDPD, andpd, Vpd, Wpd, DISOPTYPE_HARMLESS, 0);
    SSE2_OPT_BODY_FullFull_To_Full(pand, iemAImpl_pand_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/*  Opcode 0xf3 0x0f 0x54 - invalid */
/*  Opcode 0xf2 0x0f 0x54 - invalid */


/** Opcode      0x0f 0x55 - andnps Vps, Wps */
FNIEMOP_DEF(iemOp_andnps_Vps_Wps)
{
    IEMOP_MNEMONIC2(RM, ANDNPS, andnps, Vps, Wps, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSseOpt_FullFull_To_Full, iemAImpl_pandn_u128);
}


/** Opcode 0x66 0x0f 0x55 - andnpd Vpd, Wpd */
FNIEMOP_DEF(iemOp_andnpd_Vpd_Wpd)
{
    IEMOP_MNEMONIC2(RM, ANDNPD, andnpd, Vpd, Wpd, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_pandn_u128);
}


/*  Opcode 0xf3 0x0f 0x55 - invalid */
/*  Opcode 0xf2 0x0f 0x55 - invalid */


/** Opcode      0x0f 0x56 - orps Vps, Wps */
FNIEMOP_DEF(iemOp_orps_Vps_Wps)
{
    IEMOP_MNEMONIC2(RM, ORPS, orps, Vps, Wps, DISOPTYPE_HARMLESS, 0);
    SSE2_OPT_BODY_FullFull_To_Full(por, iemAImpl_por_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/** Opcode 0x66 0x0f 0x56 - orpd Vpd, Wpd */
FNIEMOP_DEF(iemOp_orpd_Vpd_Wpd)
{
    IEMOP_MNEMONIC2(RM, ORPD, orpd, Vpd, Wpd, DISOPTYPE_HARMLESS, 0);
    SSE2_OPT_BODY_FullFull_To_Full(por, iemAImpl_por_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/*  Opcode 0xf3 0x0f 0x56 - invalid */
/*  Opcode 0xf2 0x0f 0x56 - invalid */


/** Opcode      0x0f 0x57 - xorps Vps, Wps */
FNIEMOP_DEF(iemOp_xorps_Vps_Wps)
{
    IEMOP_MNEMONIC2(RM, XORPS, xorps, Vps, Wps, DISOPTYPE_HARMLESS, 0);
    SSE2_OPT_BODY_FullFull_To_Full(pxor, iemAImpl_pxor_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/** Opcode 0x66 0x0f 0x57 - xorpd Vpd, Wpd */
FNIEMOP_DEF(iemOp_xorpd_Vpd_Wpd)
{
    IEMOP_MNEMONIC2(RM, XORPD, xorpd, Vpd, Wpd, DISOPTYPE_HARMLESS, 0);
    SSE2_OPT_BODY_FullFull_To_Full(pxor, iemAImpl_pxor_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/*  Opcode 0xf3 0x0f 0x57 - invalid */
/*  Opcode 0xf2 0x0f 0x57 - invalid */

/** Opcode      0x0f 0x58 - addps Vps, Wps */
FNIEMOP_DEF(iemOp_addps_Vps_Wps)
{
    IEMOP_MNEMONIC2(RM, ADDPS, addps, Vps, Wps, DISOPTYPE_HARMLESS, 0);
    SSE_FP_BODY_FullFull_To_Full(addps, iemAImpl_addps_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/** Opcode 0x66 0x0f 0x58 - addpd Vpd, Wpd */
FNIEMOP_DEF(iemOp_addpd_Vpd_Wpd)
{
    IEMOP_MNEMONIC2(RM, ADDPD, addpd, Vpd, Wpd, DISOPTYPE_HARMLESS, 0);
    SSE_FP_BODY_FullFull_To_Full(addpd, iemAImpl_addpd_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/** Opcode 0xf3 0x0f 0x58 - addss Vss, Wss */
FNIEMOP_DEF(iemOp_addss_Vss_Wss)
{
    IEMOP_MNEMONIC2(RM, ADDSS, addss, Vss, Wss, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSseFp_FullR32_To_Full, iemAImpl_addss_u128_r32);
}


/** Opcode 0xf2 0x0f 0x58 - addsd Vsd, Wsd */
FNIEMOP_DEF(iemOp_addsd_Vsd_Wsd)
{
    IEMOP_MNEMONIC2(RM, ADDSD, addsd, Vsd, Wsd, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Fp_FullR64_To_Full, iemAImpl_addsd_u128_r64);
}


/** Opcode      0x0f 0x59 - mulps Vps, Wps */
FNIEMOP_DEF(iemOp_mulps_Vps_Wps)
{
    IEMOP_MNEMONIC2(RM, MULPS, mulps, Vps, Wps, DISOPTYPE_HARMLESS, 0);
    SSE_FP_BODY_FullFull_To_Full(mulps, iemAImpl_mulps_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/** Opcode 0x66 0x0f 0x59 - mulpd Vpd, Wpd */
FNIEMOP_DEF(iemOp_mulpd_Vpd_Wpd)
{
    IEMOP_MNEMONIC2(RM, MULPD, mulpd, Vpd, Wpd, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Fp_FullFull_To_Full, iemAImpl_mulpd_u128);
}


/** Opcode 0xf3 0x0f 0x59 - mulss Vss, Wss */
FNIEMOP_DEF(iemOp_mulss_Vss_Wss)
{
    IEMOP_MNEMONIC2(RM, MULSS, mulss, Vss, Wss, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSseFp_FullR32_To_Full, iemAImpl_mulss_u128_r32);
}


/** Opcode 0xf2 0x0f 0x59 - mulsd Vsd, Wsd */
FNIEMOP_DEF(iemOp_mulsd_Vsd_Wsd)
{
    IEMOP_MNEMONIC2(RM, MULSD, mulsd, Vsd, Wsd, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Fp_FullR64_To_Full, iemAImpl_mulsd_u128_r64);
}


/** Opcode      0x0f 0x5a - cvtps2pd Vpd, Wps */
FNIEMOP_DEF(iemOp_cvtps2pd_Vpd_Wps)
{
    IEMOP_MNEMONIC2(RM, CVTPS2PD, cvtps2pd, Vpd_WO, Wps, DISOPTYPE_HARMLESS, 0);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM, XMM[63:0].
         */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_LOCAL(X86XMMREG,             SseRes);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    pSseRes,        SseRes,     0);
        IEM_MC_ARG(uint64_t const *,        pu64Src,                    1);  /* The input is actually two 32-bit float values, */
        IEM_MC_REF_XREG_U64_CONST(pu64Src, IEM_GET_MODRM_RM(pVCpu, bRm));    /* but we've got no matching type or MC. */
        IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvtps2pd_u128, pSseRes, pu64Src);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), SseRes);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM, [mem64].
         */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEM_MC_LOCAL(RTGCPTR, GCPtrEffSrc);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();

        IEM_MC_LOCAL(uint64_t,              u64Src);
        IEM_MC_ARG_LOCAL_REF(uint64_t const *, pu64Src, u64Src,         1); /* (see comment above wrt type) */
        IEM_MC_FETCH_MEM_U64(u64Src, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_LOCAL(X86XMMREG,             SseRes);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    pSseRes,        SseRes,     0);
        IEM_MC_CALL_SSE_AIMPL_2(iemAImpl_cvtps2pd_u128, pSseRes, pu64Src);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), SseRes);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode 0x66 0x0f 0x5a - cvtpd2ps Vps, Wpd */
FNIEMOP_DEF(iemOp_cvtpd2ps_Vps_Wpd)
{
    IEMOP_MNEMONIC2(RM, CVTPD2PS, cvtpd2ps, Vps_WO, Wpd, DISOPTYPE_HARMLESS, 0);
    /** @todo inefficient as we don't need to fetch the destination (write-only). */
    return FNIEMOP_CALL_1(iemOpCommonSse2Fp_FullFull_To_Full, iemAImpl_cvtpd2ps_u128);
}


/** Opcode 0xf3 0x0f 0x5a - cvtss2sd Vsd, Wss */
FNIEMOP_DEF(iemOp_cvtss2sd_Vsd_Wss)
{
    IEMOP_MNEMONIC2(RM, CVTSS2SD, cvtss2sd, Vsd, Wss, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSseFp_FullR32_To_Full, iemAImpl_cvtss2sd_u128_r32);
}


/** Opcode 0xf2 0x0f 0x5a - cvtsd2ss Vss, Wsd */
FNIEMOP_DEF(iemOp_cvtsd2ss_Vss_Wsd)
{
    IEMOP_MNEMONIC2(RM, CVTSD2SS, cvtsd2ss, Vss, Wsd, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Fp_FullR64_To_Full, iemAImpl_cvtsd2ss_u128_r64);
}


/** Opcode      0x0f 0x5b - cvtdq2ps Vps, Wdq */
FNIEMOP_DEF(iemOp_cvtdq2ps_Vps_Wdq)
{
    IEMOP_MNEMONIC2(RM, CVTDQ2PS, cvtdq2ps, Vps_WO, Wdq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    /** @todo inefficient as we don't need to fetch the destination (write-only). */
    return FNIEMOP_CALL_1(iemOpCommonSse2Fp_FullFull_To_Full, iemAImpl_cvtdq2ps_u128);
}


/** Opcode 0x66 0x0f 0x5b - cvtps2dq Vdq, Wps */
FNIEMOP_DEF(iemOp_cvtps2dq_Vdq_Wps)
{
    IEMOP_MNEMONIC2(RM, CVTPS2DQ, cvtps2dq, Vdq_WO, Wps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    /** @todo inefficient as we don't need to fetch the destination (write-only). */
    return FNIEMOP_CALL_1(iemOpCommonSse2Fp_FullFull_To_Full, iemAImpl_cvtps2dq_u128);
}


/** Opcode 0xf3 0x0f 0x5b - cvttps2dq Vdq, Wps */
FNIEMOP_DEF(iemOp_cvttps2dq_Vdq_Wps)
{
    IEMOP_MNEMONIC2(RM, CVTTPS2DQ, cvttps2dq, Vdq_WO, Wps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    /** @todo inefficient as we don't need to fetch the destination (write-only). */
    return FNIEMOP_CALL_1(iemOpCommonSse2Fp_FullFull_To_Full, iemAImpl_cvttps2dq_u128);
}


/*  Opcode 0xf2 0x0f 0x5b - invalid */


/** Opcode      0x0f 0x5c - subps Vps, Wps */
FNIEMOP_DEF(iemOp_subps_Vps_Wps)
{
    IEMOP_MNEMONIC2(RM, SUBPS, subps, Vps, Wps, DISOPTYPE_HARMLESS, 0);
    SSE_FP_BODY_FullFull_To_Full(subps, iemAImpl_subps_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/** Opcode 0x66 0x0f 0x5c - subpd Vpd, Wpd */
FNIEMOP_DEF(iemOp_subpd_Vpd_Wpd)
{
    IEMOP_MNEMONIC2(RM, SUBPD, subpd, Vpd, Wpd, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Fp_FullFull_To_Full, iemAImpl_subpd_u128);
}


/** Opcode 0xf3 0x0f 0x5c - subss Vss, Wss */
FNIEMOP_DEF(iemOp_subss_Vss_Wss)
{
    IEMOP_MNEMONIC2(RM, SUBSS, subss, Vss, Wss, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSseFp_FullR32_To_Full, iemAImpl_subss_u128_r32);
}


/** Opcode 0xf2 0x0f 0x5c - subsd Vsd, Wsd */
FNIEMOP_DEF(iemOp_subsd_Vsd_Wsd)
{
    IEMOP_MNEMONIC2(RM, SUBSD, subsd, Vsd, Wsd, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Fp_FullR64_To_Full, iemAImpl_subsd_u128_r64);
}


/** Opcode      0x0f 0x5d - minps Vps, Wps */
FNIEMOP_DEF(iemOp_minps_Vps_Wps)
{
    IEMOP_MNEMONIC2(RM, MINPS, minps, Vps, Wps, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSseFp_FullFull_To_Full, iemAImpl_minps_u128);
}


/** Opcode 0x66 0x0f 0x5d - minpd Vpd, Wpd */
FNIEMOP_DEF(iemOp_minpd_Vpd_Wpd)
{
    IEMOP_MNEMONIC2(RM, MINPD, minpd, Vpd, Wpd, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Fp_FullFull_To_Full, iemAImpl_minpd_u128);
}


/** Opcode 0xf3 0x0f 0x5d - minss Vss, Wss */
FNIEMOP_DEF(iemOp_minss_Vss_Wss)
{
    IEMOP_MNEMONIC2(RM, MINSS, minss, Vss, Wss, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSseFp_FullR32_To_Full, iemAImpl_minss_u128_r32);
}


/** Opcode 0xf2 0x0f 0x5d - minsd Vsd, Wsd */
FNIEMOP_DEF(iemOp_minsd_Vsd_Wsd)
{
    IEMOP_MNEMONIC2(RM, MINSD, minsd, Vsd, Wsd, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Fp_FullR64_To_Full, iemAImpl_minsd_u128_r64);
}


/** Opcode      0x0f 0x5e - divps Vps, Wps */
FNIEMOP_DEF(iemOp_divps_Vps_Wps)
{
    IEMOP_MNEMONIC2(RM, DIVPS, divps, Vps, Wps, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSseFp_FullFull_To_Full, iemAImpl_divps_u128);
}


/** Opcode 0x66 0x0f 0x5e - divpd Vpd, Wpd */
FNIEMOP_DEF(iemOp_divpd_Vpd_Wpd)
{
    IEMOP_MNEMONIC2(RM, DIVPD, divpd, Vpd, Wpd, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Fp_FullFull_To_Full, iemAImpl_divpd_u128);
}


/** Opcode 0xf3 0x0f 0x5e - divss Vss, Wss */
FNIEMOP_DEF(iemOp_divss_Vss_Wss)
{
    IEMOP_MNEMONIC2(RM, DIVSS, divss, Vss, Wss, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSseFp_FullR32_To_Full, iemAImpl_divss_u128_r32);
}


/** Opcode 0xf2 0x0f 0x5e - divsd Vsd, Wsd */
FNIEMOP_DEF(iemOp_divsd_Vsd_Wsd)
{
    IEMOP_MNEMONIC2(RM, DIVSD, divsd, Vsd, Wsd, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Fp_FullR64_To_Full, iemAImpl_divsd_u128_r64);
}


/** Opcode      0x0f 0x5f - maxps Vps, Wps */
FNIEMOP_DEF(iemOp_maxps_Vps_Wps)
{
    IEMOP_MNEMONIC2(RM, MAXPS, maxps, Vps, Wps, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSseFp_FullFull_To_Full, iemAImpl_maxps_u128);
}


/** Opcode 0x66 0x0f 0x5f - maxpd Vpd, Wpd */
FNIEMOP_DEF(iemOp_maxpd_Vpd_Wpd)
{
    IEMOP_MNEMONIC2(RM, MAXPD, maxpd, Vpd, Wpd, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Fp_FullFull_To_Full, iemAImpl_maxpd_u128);
}


/** Opcode 0xf3 0x0f 0x5f - maxss Vss, Wss */
FNIEMOP_DEF(iemOp_maxss_Vss_Wss)
{
    IEMOP_MNEMONIC2(RM, MAXSS, maxss, Vss, Wss, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSseFp_FullR32_To_Full, iemAImpl_maxss_u128_r32);
}


/** Opcode 0xf2 0x0f 0x5f - maxsd Vsd, Wsd */
FNIEMOP_DEF(iemOp_maxsd_Vsd_Wsd)
{
    IEMOP_MNEMONIC2(RM, MAXSD, maxsd, Vsd, Wsd, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Fp_FullR64_To_Full, iemAImpl_maxsd_u128_r64);
}


/** Opcode      0x0f 0x60 - punpcklbw Pq, Qd */
FNIEMOP_DEF(iemOp_punpcklbw_Pq_Qd)
{
    IEMOP_MNEMONIC2(RM, PUNPCKLBW, punpcklbw, Pq, Qd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, 0);
    return FNIEMOP_CALL_1(iemOpCommonMmx_LowLow_To_Full, iemAImpl_punpcklbw_u64);
}


/** Opcode 0x66 0x0f 0x60 - punpcklbw Vx, W */
FNIEMOP_DEF(iemOp_punpcklbw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PUNPCKLBW, punpcklbw, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2_LowLow_To_Full, iemAImpl_punpcklbw_u128);
}


/*  Opcode 0xf3 0x0f 0x60 - invalid */


/** Opcode      0x0f 0x61 - punpcklwd Pq, Qd */
FNIEMOP_DEF(iemOp_punpcklwd_Pq_Qd)
{
    /** @todo AMD mark the MMX version as 3DNow!. Intel says MMX CPUID req. */
    IEMOP_MNEMONIC2(RM, PUNPCKLWD, punpcklwd, Pq, Qd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, 0);
    return FNIEMOP_CALL_1(iemOpCommonMmx_LowLow_To_Full, iemAImpl_punpcklwd_u64);
}


/** Opcode 0x66 0x0f 0x61 - punpcklwd Vx, Wx */
FNIEMOP_DEF(iemOp_punpcklwd_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PUNPCKLWD, punpcklwd, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2_LowLow_To_Full, iemAImpl_punpcklwd_u128);
}


/*  Opcode 0xf3 0x0f 0x61 - invalid */


/** Opcode      0x0f 0x62 - punpckldq Pq, Qd */
FNIEMOP_DEF(iemOp_punpckldq_Pq_Qd)
{
    IEMOP_MNEMONIC2(RM, PUNPCKLDQ, punpckldq, Pq, Qd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, 0);
    return FNIEMOP_CALL_1(iemOpCommonMmx_LowLow_To_Full, iemAImpl_punpckldq_u64);
}


/** Opcode 0x66 0x0f 0x62 - punpckldq Vx, Wx */
FNIEMOP_DEF(iemOp_punpckldq_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PUNPCKLDQ, punpckldq, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2_LowLow_To_Full, iemAImpl_punpckldq_u128);
}


/*  Opcode 0xf3 0x0f 0x62 - invalid */



/** Opcode      0x0f 0x63 - packsswb Pq, Qq */
FNIEMOP_DEF(iemOp_packsswb_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PACKSSWB, packsswb, Pq, Qd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, 0);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_packsswb_u64);
}


/** Opcode 0x66 0x0f 0x63 - packsswb Vx, Wx */
FNIEMOP_DEF(iemOp_packsswb_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PACKSSWB, packsswb, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_packsswb_u128);
}


/*  Opcode 0xf3 0x0f 0x63 - invalid */


/** Opcode      0x0f 0x64 - pcmpgtb Pq, Qq */
FNIEMOP_DEF(iemOp_pcmpgtb_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PCMPGTB, pcmpgtb, Pq, Qq, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_pcmpgtb_u64);
}


/** Opcode 0x66 0x0f 0x64 - pcmpgtb Vx, Wx */
FNIEMOP_DEF(iemOp_pcmpgtb_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PCMPGTB, pcmpgtb, Vx, Wx, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_pcmpgtb_u128);
}


/*  Opcode 0xf3 0x0f 0x64 - invalid */


/** Opcode      0x0f 0x65 - pcmpgtw Pq, Qq */
FNIEMOP_DEF(iemOp_pcmpgtw_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PCMPGTW, pcmpgtw, Pq, Qq, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_pcmpgtw_u64);
}


/** Opcode 0x66 0x0f 0x65 - pcmpgtw Vx, Wx */
FNIEMOP_DEF(iemOp_pcmpgtw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PCMPGTW, pcmpgtw, Vx, Wx, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_pcmpgtw_u128);
}


/*  Opcode 0xf3 0x0f 0x65 - invalid */


/** Opcode      0x0f 0x66 - pcmpgtd Pq, Qq */
FNIEMOP_DEF(iemOp_pcmpgtd_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PCMPGTD, pcmpgtd, Pq, Qq, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_pcmpgtd_u64);
}


/** Opcode 0x66 0x0f 0x66 - pcmpgtd Vx, Wx */
FNIEMOP_DEF(iemOp_pcmpgtd_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PCMPGTD, pcmpgtd, Vx, Wx, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_pcmpgtd_u128);
}


/*  Opcode 0xf3 0x0f 0x66 - invalid */


/** Opcode      0x0f 0x67 - packuswb Pq, Qq */
FNIEMOP_DEF(iemOp_packuswb_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PACKUSWB, packuswb, Pq, Qd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, 0);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_packuswb_u64);
}


/** Opcode 0x66 0x0f 0x67 - packuswb Vx, Wx */
FNIEMOP_DEF(iemOp_packuswb_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PACKUSWB, packuswb, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    SSE2_OPT_BODY_FullFull_To_Full(packuswb, iemAImpl_packuswb_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/*  Opcode 0xf3 0x0f 0x67 - invalid */


/** Opcode      0x0f 0x68 - punpckhbw Pq, Qq
 * @note Intel and AMD both uses Qd for the second parameter, however they
 *       both list it as a mmX/mem64 operand and intel describes it as being
 *       loaded as a qword, so it should be Qq, shouldn't it? */
FNIEMOP_DEF(iemOp_punpckhbw_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PUNPCKHBW, punpckhbw, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, 0);
    return FNIEMOP_CALL_1(iemOpCommonMmx_HighHigh_To_Full, iemAImpl_punpckhbw_u64);
}


/** Opcode 0x66 0x0f 0x68 - punpckhbw Vx, Wx */
FNIEMOP_DEF(iemOp_punpckhbw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PUNPCKHBW, punpckhbw, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2_HighHigh_To_Full, iemAImpl_punpckhbw_u128);
}


/*  Opcode 0xf3 0x0f 0x68 - invalid */


/** Opcode      0x0f 0x69 - punpckhwd Pq, Qq
 * @note Intel and AMD both uses Qd for the second parameter, however they
 *       both list it as a mmX/mem64 operand and intel describes it as being
 *       loaded as a qword, so it should be Qq, shouldn't it? */
FNIEMOP_DEF(iemOp_punpckhwd_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PUNPCKHWD, punpckhwd, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, 0);
    return FNIEMOP_CALL_1(iemOpCommonMmx_HighHigh_To_Full, iemAImpl_punpckhwd_u64);
}


/** Opcode 0x66 0x0f 0x69 - punpckhwd Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_punpckhwd_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PUNPCKHWD, punpckhwd, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2_HighHigh_To_Full, iemAImpl_punpckhwd_u128);

}


/*  Opcode 0xf3 0x0f 0x69 - invalid */


/** Opcode      0x0f 0x6a - punpckhdq Pq, Qq
 * @note Intel and AMD both uses Qd for the second parameter, however they
 *       both list it as a mmX/mem64 operand and intel describes it as being
 *       loaded as a qword, so it should be Qq, shouldn't it? */
FNIEMOP_DEF(iemOp_punpckhdq_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PUNPCKHDQ, punpckhdq, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, 0);
    return FNIEMOP_CALL_1(iemOpCommonMmx_HighHigh_To_Full, iemAImpl_punpckhdq_u64);
}


/** Opcode 0x66 0x0f 0x6a - punpckhdq Vx, Wx */
FNIEMOP_DEF(iemOp_punpckhdq_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PUNPCKHDQ, punpckhdq, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2_HighHigh_To_Full, iemAImpl_punpckhdq_u128);
}


/*  Opcode 0xf3 0x0f 0x6a - invalid */


/** Opcode      0x0f 0x6b - packssdw Pq, Qd */
FNIEMOP_DEF(iemOp_packssdw_Pq_Qd)
{
    IEMOP_MNEMONIC2(RM, PACKSSDW, packssdw, Pq, Qd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, 0);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_packssdw_u64);
}


/** Opcode 0x66 0x0f 0x6b - packssdw Vx, Wx */
FNIEMOP_DEF(iemOp_packssdw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PACKSSDW, packssdw, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_packssdw_u128);
}


/*  Opcode 0xf3 0x0f 0x6b - invalid */


/*  Opcode      0x0f 0x6c - invalid */


/** Opcode 0x66 0x0f 0x6c - punpcklqdq Vx, Wx */
FNIEMOP_DEF(iemOp_punpcklqdq_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PUNPCKLQDQ, punpcklqdq, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2_LowLow_To_Full, iemAImpl_punpcklqdq_u128);
}


/*  Opcode 0xf3 0x0f 0x6c - invalid */
/*  Opcode 0xf2 0x0f 0x6c - invalid */


/*  Opcode      0x0f 0x6d - invalid */


/** Opcode 0x66 0x0f 0x6d - punpckhqdq Vx, Wx */
FNIEMOP_DEF(iemOp_punpckhqdq_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PUNPCKHQDQ, punpckhqdq, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2_HighHigh_To_Full, iemAImpl_punpckhqdq_u128);
}


/*  Opcode 0xf3 0x0f 0x6d - invalid */


FNIEMOP_DEF(iemOp_movd_q_Pd_Ey)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_REX_W)
    {
        /**
         * @opcode      0x6e
         * @opcodesub   rex.w=1
         * @oppfx       none
         * @opcpuid     mmx
         * @opgroup     og_mmx_datamove
         * @opxcpttype  5
         * @optest      64-bit / op1=1 op2=2   -> op1=2   ftw=0xff
         * @optest      64-bit / op1=0 op2=-42 -> op1=-42 ftw=0xff
         */
        IEMOP_MNEMONIC2(RM, MOVQ, movq, Pq_WO, Eq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OZ_PFX);
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* MMX, greg64 */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fMmx);
            IEM_MC_LOCAL(uint64_t, u64Tmp);

            IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_FPU_STATE_FOR_CHANGE();
            IEM_MC_FPU_TO_MMX_MODE();

            IEM_MC_FETCH_GREG_U64(u64Tmp, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_STORE_MREG_U64(IEM_GET_MODRM_REG_8(bRm), u64Tmp);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /* MMX, [mem64] */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_LOCAL(RTGCPTR, GCPtrEffSrc);
            IEM_MC_LOCAL(uint64_t, u64Tmp);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fMmx);
            IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_FPU_STATE_FOR_CHANGE();

            IEM_MC_FETCH_MEM_U64(u64Tmp, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_STORE_MREG_U64(IEM_GET_MODRM_REG_8(bRm), u64Tmp);
            IEM_MC_FPU_TO_MMX_MODE();

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        /**
         * @opdone
         * @opcode      0x6e
         * @opcodesub   rex.w=0
         * @oppfx       none
         * @opcpuid     mmx
         * @opgroup     og_mmx_datamove
         * @opxcpttype  5
         * @opfunction  iemOp_movd_q_Pd_Ey
         * @optest      op1=1 op2=2   -> op1=2   ftw=0xff
         * @optest      op1=0 op2=-42 -> op1=-42 ftw=0xff
         */
        IEMOP_MNEMONIC2(RM, MOVD, movd, PdZx_WO, Ed, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OZ_PFX);
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* MMX, greg32 */
            IEM_MC_BEGIN(0, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fMmx);
            IEM_MC_LOCAL(uint32_t, u32Tmp);

            IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_FPU_STATE_FOR_CHANGE();
            IEM_MC_FPU_TO_MMX_MODE();

            IEM_MC_FETCH_GREG_U32(u32Tmp, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_STORE_MREG_U32_ZX_U64(IEM_GET_MODRM_REG_8(bRm), u32Tmp);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /* MMX, [mem32] */
            IEM_MC_BEGIN(0, 0);
            IEM_MC_LOCAL(RTGCPTR, GCPtrEffSrc);
            IEM_MC_LOCAL(uint32_t, u32Tmp);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fMmx);
            IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_FPU_STATE_FOR_CHANGE();

            IEM_MC_FETCH_MEM_U32(u32Tmp, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_STORE_MREG_U32_ZX_U64(IEM_GET_MODRM_REG_8(bRm), u32Tmp);
            IEM_MC_FPU_TO_MMX_MODE();

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}

FNIEMOP_DEF(iemOp_movd_q_Vy_Ey)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_REX_W)
    {
        /**
         * @opcode      0x6e
         * @opcodesub   rex.w=1
         * @oppfx       0x66
         * @opcpuid     sse2
         * @opgroup     og_sse2_simdint_datamove
         * @opxcpttype  5
         * @optest      64-bit / op1=1 op2=2   -> op1=2
         * @optest      64-bit / op1=0 op2=-42 -> op1=-42
         */
        IEMOP_MNEMONIC2(RM, MOVQ, movq, VqZx_WO, Eq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OZ_PFX);
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* XMM, greg64 */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
            IEM_MC_LOCAL(uint64_t, u64Tmp);

            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();

            IEM_MC_FETCH_GREG_U64(u64Tmp, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_STORE_XREG_U64_ZX_U128(IEM_GET_MODRM_REG(pVCpu, bRm), u64Tmp);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /* XMM, [mem64] */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_LOCAL(RTGCPTR, GCPtrEffSrc);
            IEM_MC_LOCAL(uint64_t, u64Tmp);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();

            IEM_MC_FETCH_MEM_U64(u64Tmp, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_STORE_XREG_U64_ZX_U128(IEM_GET_MODRM_REG(pVCpu, bRm), u64Tmp);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        /**
         * @opdone
         * @opcode      0x6e
         * @opcodesub   rex.w=0
         * @oppfx       0x66
         * @opcpuid     sse2
         * @opgroup     og_sse2_simdint_datamove
         * @opxcpttype  5
         * @opfunction  iemOp_movd_q_Vy_Ey
         * @optest      op1=1 op2=2   -> op1=2
         * @optest      op1=0 op2=-42 -> op1=-42
         */
        IEMOP_MNEMONIC2(RM, MOVD, movd, VdZx_WO, Ed, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OZ_PFX);
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* XMM, greg32 */
            IEM_MC_BEGIN(0, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
            IEM_MC_LOCAL(uint32_t, u32Tmp);

            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();

            IEM_MC_FETCH_GREG_U32(u32Tmp, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_STORE_XREG_U32_ZX_U128(IEM_GET_MODRM_REG(pVCpu, bRm), u32Tmp);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /* XMM, [mem32] */
            IEM_MC_BEGIN(0, 0);
            IEM_MC_LOCAL(RTGCPTR, GCPtrEffSrc);
            IEM_MC_LOCAL(uint32_t, u32Tmp);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();

            IEM_MC_FETCH_MEM_U32(u32Tmp, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_STORE_XREG_U32_ZX_U128(IEM_GET_MODRM_REG(pVCpu, bRm), u32Tmp);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}

/*  Opcode 0xf3 0x0f 0x6e - invalid */


/**
 * @opcode      0x6f
 * @oppfx       none
 * @opcpuid     mmx
 * @opgroup     og_mmx_datamove
 * @opxcpttype  5
 * @optest      op1=1 op2=2   -> op1=2   ftw=0xff
 * @optest      op1=0 op2=-42 -> op1=-42 ftw=0xff
 */
FNIEMOP_DEF(iemOp_movq_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, MOVQ, movq, Pq_WO, Qq, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fMmx);
        IEM_MC_LOCAL(uint64_t, u64Tmp);

        IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_FPU_STATE_FOR_CHANGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_FETCH_MREG_U64(u64Tmp, IEM_GET_MODRM_RM_8(bRm));
        IEM_MC_STORE_MREG_U64(IEM_GET_MODRM_REG_8(bRm), u64Tmp);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(uint64_t, u64Tmp);
        IEM_MC_LOCAL(RTGCPTR,  GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fMmx);
        IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_FPU_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U64(u64Tmp, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_STORE_MREG_U64(IEM_GET_MODRM_REG_8(bRm), u64Tmp);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}

/**
 * @opcode      0x6f
 * @oppfx       0x66
 * @opcpuid     sse2
 * @opgroup     og_sse2_simdint_datamove
 * @opxcpttype  1
 * @optest      op1=1 op2=2   -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_movdqa_Vdq_Wdq)
{
    IEMOP_MNEMONIC2(RM, MOVDQA, movdqa, Vdq_WO, Wdq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);

        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();

        IEM_MC_COPY_XREG_U128(IEM_GET_MODRM_REG(pVCpu, bRm),
                              IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(RTUINT128U, u128Tmp);
        IEM_MC_LOCAL(RTGCPTR,    GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(u128Tmp, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_XREG_U128(IEM_GET_MODRM_REG(pVCpu, bRm), u128Tmp);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}

/**
 * @opcode      0x6f
 * @oppfx       0xf3
 * @opcpuid     sse2
 * @opgroup     og_sse2_simdint_datamove
 * @opxcpttype  4UA
 * @optest      op1=1 op2=2   -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_movdqu_Vdq_Wdq)
{
    IEMOP_MNEMONIC2(RM, MOVDQU, movdqu, Vdq_WO, Wdq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();
        IEM_MC_COPY_XREG_U128(IEM_GET_MODRM_REG(pVCpu, bRm),
                              IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(RTUINT128U, u128Tmp);
        IEM_MC_LOCAL(RTGCPTR,    GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();
        IEM_MC_FETCH_MEM_U128_NO_AC(u128Tmp, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_XREG_U128(IEM_GET_MODRM_REG(pVCpu, bRm), u128Tmp);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode      0x0f 0x70 - pshufw Pq, Qq, Ib */
FNIEMOP_DEF(iemOp_pshufw_Pq_Qq_Ib)
{
    IEMOP_MNEMONIC3(RMI, PSHUFW, pshufw, Pq, Qq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, 0);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(0, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX_2_OR(fSse, fAmdMmxExts);
        IEM_MC_ARG(uint64_t *,          pDst, 0);
        IEM_MC_ARG(uint64_t const *,    pSrc, 1);
        IEM_MC_ARG_CONST(uint8_t,       bImmArg, /*=*/ bImm, 2);
        IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_REF_MREG_U64(pDst,       IEM_GET_MODRM_REG_8(bRm));
        IEM_MC_REF_MREG_U64_CONST(pSrc, IEM_GET_MODRM_RM_8(bRm));
        IEM_MC_CALL_VOID_AIMPL_3(iemAImpl_pshufw_u64, pDst, pSrc, bImmArg);
        IEM_MC_MODIFIED_MREG_BY_REF(pDst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_ARG(uint64_t *,                  pDst,       0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_ARG_LOCAL_REF(uint64_t const *,  pSrc, uSrc, 1);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_ARG_CONST(uint8_t,               bImmArg, /*=*/ bImm, 2);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX_2_OR(fSse, fAmdMmxExts);
        IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
        IEM_MC_FETCH_MEM_U64(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_REF_MREG_U64(pDst, IEM_GET_MODRM_REG_8(bRm));
        IEM_MC_CALL_VOID_AIMPL_3(iemAImpl_pshufw_u64, pDst, pSrc, bImmArg);
        IEM_MC_MODIFIED_MREG_BY_REF(pDst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * Common worker for SSE2 instructions on the forms:
 *      pshufd      xmm1, xmm2/mem128, imm8
 *      pshufhw     xmm1, xmm2/mem128, imm8
 *      pshuflw     xmm1, xmm2/mem128, imm8
 *
 * Proper alignment of the 128-bit operand is enforced.
 * Exceptions type 4. SSE2 cpuid checks.
 */
FNIEMOP_DEF_1(iemOpCommonSse2_pshufXX_Vx_Wx_Ib, PFNIEMAIMPLMEDIAPSHUFU128, pfnWorker)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(0, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_ARG(PRTUINT128U,         puDst, 0);
        IEM_MC_ARG(PCRTUINT128U,        puSrc, 1);
        IEM_MC_ARG_CONST(uint8_t,       bImmArg, /*=*/ bImm, 2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst,       IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(puSrc, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_3(pfnWorker, puDst, puSrc, bImmArg);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_ARG(PRTUINT128U,                 puDst,       0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,      puSrc, uSrc, 1);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_ARG_CONST(uint8_t,               bImmArg, /*=*/ bImm, 2);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();

        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_3(pfnWorker, puDst, puSrc, bImmArg);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode 0x66 0x0f 0x70 - pshufd Vx, Wx, Ib */
FNIEMOP_DEF(iemOp_pshufd_Vx_Wx_Ib)
{
    IEMOP_MNEMONIC3(RMI, PSHUFD, pshufd, Vx, Wx, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2_pshufXX_Vx_Wx_Ib, iemAImpl_pshufd_u128);
}


/** Opcode 0xf3 0x0f 0x70 - pshufhw Vx, Wx, Ib */
FNIEMOP_DEF(iemOp_pshufhw_Vx_Wx_Ib)
{
    IEMOP_MNEMONIC3(RMI, PSHUFHW, pshufhw, Vx, Wx, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2_pshufXX_Vx_Wx_Ib, iemAImpl_pshufhw_u128);
}


/** Opcode 0xf2 0x0f 0x70 - pshuflw Vx, Wx, Ib */
FNIEMOP_DEF(iemOp_pshuflw_Vx_Wx_Ib)
{
    IEMOP_MNEMONIC3(RMI, PSHUFLW, pshuflw, Vx, Wx, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2_pshufXX_Vx_Wx_Ib, iemAImpl_pshuflw_u128);
}


/**
 * Common worker for MMX instructions of the form:
 *      psrlw       mm, imm8
 *      psraw       mm, imm8
 *      psllw       mm, imm8
 *      psrld       mm, imm8
 *      psrad       mm, imm8
 *      pslld       mm, imm8
 *      psrlq       mm, imm8
 *      psllq       mm, imm8
 *
 */
FNIEMOP_DEF_2(iemOpCommonMmx_Shift_Imm, uint8_t, bRm, PFNIEMAIMPLMEDIAPSHIFTU64, pfnU64)
{
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, immediate.
         */
        IEM_MC_BEGIN(0, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fMmx);
        IEM_MC_ARG(uint64_t *,          pDst, 0);
        IEM_MC_ARG_CONST(uint8_t,       bShiftArg, /*=*/ bImm, 1);
        IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_REF_MREG_U64(pDst,       IEM_GET_MODRM_RM_8(bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU64, pDst, bShiftArg);
        IEM_MC_MODIFIED_MREG_BY_REF(pDst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory not supported.
         */
        /// @todo Caller already enforced register mode?!
        AssertFailedReturn(VINF_SUCCESS);
    }
}


#if 0 /*unused*/
/**
 * Common worker for SSE2 instructions of the form:
 *      psrlw       xmm, imm8
 *      psraw       xmm, imm8
 *      psllw       xmm, imm8
 *      psrld       xmm, imm8
 *      psrad       xmm, imm8
 *      pslld       xmm, imm8
 *      psrlq       xmm, imm8
 *      psllq       xmm, imm8
 *
 */
FNIEMOP_DEF_2(iemOpCommonSse2_Shift_Imm, uint8_t, bRm, PFNIEMAIMPLMEDIAPSHIFTU128, pfnU128)
{
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, immediate.
         */
        IEM_MC_BEGIN(0, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_ARG(PRTUINT128U,         pDst, 0);
        IEM_MC_ARG_CONST(uint8_t,       bShiftArg, /*=*/ bImm, 1);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(pDst, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU128, pDst, bShiftArg);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        /// @todo Caller already enforced register mode?!
        AssertFailedReturn(VINF_SUCCESS);
    }
}
#endif


/**
 * Preprocessor macro variant of iemOpCommonSse2_Shift_Imm
 */
#define SSE2_SHIFT_BODY_Imm(a_Ins, a_bRm, a_fRegNativeArchs) \
        if (IEM_IS_MODRM_REG_MODE((a_bRm))) \
        { \
            /* \
             * Register, immediate. \
             */ \
            IEM_MC_BEGIN(0, 0); \
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm); \
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2); \
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT(); \
            IEM_MC_PREPARE_SSE_USAGE(); \
            IEM_MC_NATIVE_IF(a_fRegNativeArchs) { \
                IEM_MC_NATIVE_EMIT_2(RT_CONCAT3(iemNativeEmit_,a_Ins,_ri_u128), IEM_GET_MODRM_RM(pVCpu, (a_bRm)), bImm); \
            } IEM_MC_NATIVE_ELSE() { \
                IEM_MC_ARG(PRTUINT128U,         pDst, 0); \
                IEM_MC_ARG_CONST(uint8_t,       bShiftArg, /*=*/ bImm, 1); \
                IEM_MC_REF_XREG_U128(pDst, IEM_GET_MODRM_RM(pVCpu, (a_bRm))); \
                IEM_MC_CALL_VOID_AIMPL_2(RT_CONCAT3(iemAImpl_,a_Ins,_imm_u128), pDst, bShiftArg); \
            } IEM_MC_NATIVE_ENDIF(); \
            IEM_MC_ADVANCE_RIP_AND_FINISH(); \
            IEM_MC_END(); \
        } \
        else \
        { \
            /* \
             * Register, memory. \
             */ \
            AssertFailedReturn(VINF_SUCCESS); \
        } (void)0


/** Opcode 0x0f 0x71 11/2 - psrlw Nq, Ib */
FNIEMOPRM_DEF(iemOp_Grp12_psrlw_Nq_Ib)
{
//    IEMOP_MNEMONIC2(RI, PSRLW, psrlw, Nq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, 0);
    return FNIEMOP_CALL_2(iemOpCommonMmx_Shift_Imm, bRm, iemAImpl_psrlw_imm_u64);
}


/** Opcode 0x66 0x0f 0x71 11/2. */
FNIEMOPRM_DEF(iemOp_Grp12_psrlw_Ux_Ib)
{
//    IEMOP_MNEMONIC2(RI, PSRLW, psrlw, Ux, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    SSE2_SHIFT_BODY_Imm(psrlw, bRm, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/** Opcode 0x0f 0x71 11/4. */
FNIEMOPRM_DEF(iemOp_Grp12_psraw_Nq_Ib)
{
//    IEMOP_MNEMONIC2(RI, PSRAW, psraw, Nq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, 0);
    return FNIEMOP_CALL_2(iemOpCommonMmx_Shift_Imm, bRm, iemAImpl_psraw_imm_u64);
}


/** Opcode 0x66 0x0f 0x71 11/4. */
FNIEMOPRM_DEF(iemOp_Grp12_psraw_Ux_Ib)
{
//    IEMOP_MNEMONIC2(RI, PSRAW, psraw, Ux, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    SSE2_SHIFT_BODY_Imm(psraw, bRm, 0);
}


/** Opcode 0x0f 0x71 11/6. */
FNIEMOPRM_DEF(iemOp_Grp12_psllw_Nq_Ib)
{
//    IEMOP_MNEMONIC2(RI, PSLLW, psllw, Nq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, 0);
    return FNIEMOP_CALL_2(iemOpCommonMmx_Shift_Imm, bRm, iemAImpl_psllw_imm_u64);
}


/** Opcode 0x66 0x0f 0x71 11/6. */
FNIEMOPRM_DEF(iemOp_Grp12_psllw_Ux_Ib)
{
//    IEMOP_MNEMONIC2(RI, PSLLW, psllw, Ux, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    SSE2_SHIFT_BODY_Imm(psllw, bRm, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/**
 * Group 12 jump table for register variant.
 */
IEM_STATIC const PFNIEMOPRM g_apfnGroup12RegReg[] =
{
    /* /0 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8),
    /* /1 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8),
    /* /2 */ iemOp_Grp12_psrlw_Nq_Ib,   iemOp_Grp12_psrlw_Ux_Ib,    iemOp_InvalidWithRMNeedImm8, iemOp_InvalidWithRMNeedImm8,
    /* /3 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8),
    /* /4 */ iemOp_Grp12_psraw_Nq_Ib,   iemOp_Grp12_psraw_Ux_Ib,    iemOp_InvalidWithRMNeedImm8, iemOp_InvalidWithRMNeedImm8,
    /* /5 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8),
    /* /6 */ iemOp_Grp12_psllw_Nq_Ib,   iemOp_Grp12_psllw_Ux_Ib,    iemOp_InvalidWithRMNeedImm8, iemOp_InvalidWithRMNeedImm8,
    /* /7 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8)
};
AssertCompile(RT_ELEMENTS(g_apfnGroup12RegReg) == 8*4);


/** Opcode 0x0f 0x71. */
FNIEMOP_DEF(iemOp_Grp12)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
        /* register, register */
        return FNIEMOP_CALL_1(g_apfnGroup12RegReg[  IEM_GET_MODRM_REG_8(bRm) * 4
                                                  + pVCpu->iem.s.idxPrefix], bRm);
    return FNIEMOP_CALL_1(iemOp_InvalidWithRMNeedImm8, bRm);
}


/** Opcode 0x0f 0x72 11/2. */
FNIEMOPRM_DEF(iemOp_Grp13_psrld_Nq_Ib)
{
//    IEMOP_MNEMONIC2(RI, PSRLD, psrld, Nq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, 0);
    return FNIEMOP_CALL_2(iemOpCommonMmx_Shift_Imm, bRm, iemAImpl_psrld_imm_u64);
}


/** Opcode 0x66 0x0f 0x72 11/2. */
FNIEMOPRM_DEF(iemOp_Grp13_psrld_Ux_Ib)
{
//    IEMOP_MNEMONIC2(RI, PSRLD, psrld, Ux, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    SSE2_SHIFT_BODY_Imm(psrld, bRm, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/** Opcode 0x0f 0x72 11/4. */
FNIEMOPRM_DEF(iemOp_Grp13_psrad_Nq_Ib)
{
//    IEMOP_MNEMONIC2(RI, PSRAD, psrad, Nq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, 0);
    return FNIEMOP_CALL_2(iemOpCommonMmx_Shift_Imm, bRm, iemAImpl_psrad_imm_u64);
}


/** Opcode 0x66 0x0f 0x72 11/4. */
FNIEMOPRM_DEF(iemOp_Grp13_psrad_Ux_Ib)
{
//    IEMOP_MNEMONIC2(RI, PSRAD, psrad, Ux, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    SSE2_SHIFT_BODY_Imm(psrad, bRm, 0);
}


/** Opcode 0x0f 0x72 11/6. */
FNIEMOPRM_DEF(iemOp_Grp13_pslld_Nq_Ib)
{
//    IEMOP_MNEMONIC2(RI, PSLLD, pslld, Nq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, 0);
    return FNIEMOP_CALL_2(iemOpCommonMmx_Shift_Imm, bRm, iemAImpl_pslld_imm_u64);
}

/** Opcode 0x66 0x0f 0x72 11/6. */
FNIEMOPRM_DEF(iemOp_Grp13_pslld_Ux_Ib)
{
//    IEMOP_MNEMONIC2(RI, PSLLD, pslld, Ux, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    SSE2_SHIFT_BODY_Imm(pslld, bRm, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/**
 * Group 13 jump table for register variant.
 */
IEM_STATIC const PFNIEMOPRM g_apfnGroup13RegReg[] =
{
    /* /0 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8),
    /* /1 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8),
    /* /2 */ iemOp_Grp13_psrld_Nq_Ib,   iemOp_Grp13_psrld_Ux_Ib,    iemOp_InvalidWithRMNeedImm8, iemOp_InvalidWithRMNeedImm8,
    /* /3 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8),
    /* /4 */ iemOp_Grp13_psrad_Nq_Ib,   iemOp_Grp13_psrad_Ux_Ib,    iemOp_InvalidWithRMNeedImm8, iemOp_InvalidWithRMNeedImm8,
    /* /5 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8),
    /* /6 */ iemOp_Grp13_pslld_Nq_Ib,   iemOp_Grp13_pslld_Ux_Ib,    iemOp_InvalidWithRMNeedImm8, iemOp_InvalidWithRMNeedImm8,
    /* /7 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8)
};
AssertCompile(RT_ELEMENTS(g_apfnGroup13RegReg) == 8*4);

/** Opcode 0x0f 0x72. */
FNIEMOP_DEF(iemOp_Grp13)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
        /* register, register */
        return FNIEMOP_CALL_1(g_apfnGroup13RegReg[ IEM_GET_MODRM_REG_8(bRm) * 4
                                                  + pVCpu->iem.s.idxPrefix], bRm);
    return FNIEMOP_CALL_1(iemOp_InvalidWithRMNeedImm8, bRm);
}


/** Opcode 0x0f 0x73 11/2. */
FNIEMOPRM_DEF(iemOp_Grp14_psrlq_Nq_Ib)
{
//    IEMOP_MNEMONIC2(RI, PSRLQ, psrlq, Nq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, 0);
    return FNIEMOP_CALL_2(iemOpCommonMmx_Shift_Imm, bRm, iemAImpl_psrlq_imm_u64);
}


/** Opcode 0x66 0x0f 0x73 11/2. */
FNIEMOPRM_DEF(iemOp_Grp14_psrlq_Ux_Ib)
{
//    IEMOP_MNEMONIC2(RI, PSRLQ, psrlq, Ux, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    SSE2_SHIFT_BODY_Imm(psrlq, bRm, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/** Opcode 0x66 0x0f 0x73 11/3. */
FNIEMOPRM_DEF(iemOp_Grp14_psrldq_Ux_Ib)
{
//    IEMOP_MNEMONIC2(RI, PSRLDQ, psrldq, Ux, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    SSE2_SHIFT_BODY_Imm(psrldq, bRm, 0);
}


/** Opcode 0x0f 0x73 11/6. */
FNIEMOPRM_DEF(iemOp_Grp14_psllq_Nq_Ib)
{
//    IEMOP_MNEMONIC2(RI, PSLLQ, psllq, Nq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, 0);
    return FNIEMOP_CALL_2(iemOpCommonMmx_Shift_Imm, bRm, iemAImpl_psllq_imm_u64);
}


/** Opcode 0x66 0x0f 0x73 11/6. */
FNIEMOPRM_DEF(iemOp_Grp14_psllq_Ux_Ib)
{
//    IEMOP_MNEMONIC2(RI, PSLLQ, psllq, Ux, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    SSE2_SHIFT_BODY_Imm(psllq, bRm, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/** Opcode 0x66 0x0f 0x73 11/7. */
FNIEMOPRM_DEF(iemOp_Grp14_pslldq_Ux_Ib)
{
//    IEMOP_MNEMONIC2(RI, PSLLDQ, pslldq, Ux, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    SSE2_SHIFT_BODY_Imm(pslldq, bRm, 0);
}

/**
 * Group 14 jump table for register variant.
 */
IEM_STATIC const PFNIEMOPRM g_apfnGroup14RegReg[] =
{
    /* /0 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8),
    /* /1 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8),
    /* /2 */ iemOp_Grp14_psrlq_Nq_Ib,     iemOp_Grp14_psrlq_Ux_Ib,  iemOp_InvalidWithRMNeedImm8, iemOp_InvalidWithRMNeedImm8,
    /* /3 */ iemOp_InvalidWithRMNeedImm8, iemOp_Grp14_psrldq_Ux_Ib, iemOp_InvalidWithRMNeedImm8, iemOp_InvalidWithRMNeedImm8,
    /* /4 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8),
    /* /5 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8),
    /* /6 */ iemOp_Grp14_psllq_Nq_Ib,     iemOp_Grp14_psllq_Ux_Ib,  iemOp_InvalidWithRMNeedImm8, iemOp_InvalidWithRMNeedImm8,
    /* /7 */ iemOp_InvalidWithRMNeedImm8, iemOp_Grp14_pslldq_Ux_Ib, iemOp_InvalidWithRMNeedImm8, iemOp_InvalidWithRMNeedImm8,
};
AssertCompile(RT_ELEMENTS(g_apfnGroup14RegReg) == 8*4);


/** Opcode 0x0f 0x73. */
FNIEMOP_DEF(iemOp_Grp14)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
        /* register, register */
        return FNIEMOP_CALL_1(g_apfnGroup14RegReg[ IEM_GET_MODRM_REG_8(bRm) * 4
                                                  + pVCpu->iem.s.idxPrefix], bRm);
    return FNIEMOP_CALL_1(iemOp_InvalidWithRMNeedImm8, bRm);
}


/** Opcode      0x0f 0x74 - pcmpeqb Pq, Qq */
FNIEMOP_DEF(iemOp_pcmpeqb_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PCMPEQB, pcmpeqb, Pq, Qq, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_pcmpeqb_u64);
}


/** Opcode 0x66 0x0f 0x74 - pcmpeqb Vx, Wx */
FNIEMOP_DEF(iemOp_pcmpeqb_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PCMPEQB, pcmpeqb, Vx, Wx, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    SSE2_OPT_BODY_FullFull_To_Full(pcmpeqb, iemAImpl_pcmpeqb_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/*  Opcode 0xf3 0x0f 0x74 - invalid */
/*  Opcode 0xf2 0x0f 0x74 - invalid */


/** Opcode      0x0f 0x75 - pcmpeqw Pq, Qq */
FNIEMOP_DEF(iemOp_pcmpeqw_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PCMPEQW, pcmpeqw, Pq, Qq, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_pcmpeqw_u64);
}


/** Opcode 0x66 0x0f 0x75 - pcmpeqw Vx, Wx */
FNIEMOP_DEF(iemOp_pcmpeqw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PCMPEQW, pcmpeqw, Vx, Wx, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    SSE2_OPT_BODY_FullFull_To_Full(pcmpeqw, iemAImpl_pcmpeqw_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/*  Opcode 0xf3 0x0f 0x75 - invalid */
/*  Opcode 0xf2 0x0f 0x75 - invalid */


/** Opcode      0x0f 0x76 - pcmpeqd Pq, Qq */
FNIEMOP_DEF(iemOp_pcmpeqd_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PCMPEQD, pcmpeqd, Pq, Qq, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_pcmpeqd_u64);
}


/** Opcode 0x66 0x0f 0x76 - pcmpeqd Vx, Wx */
FNIEMOP_DEF(iemOp_pcmpeqd_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PCMPEQD, pcmpeqd, Vx, Wx, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    SSE2_OPT_BODY_FullFull_To_Full(pcmpeqd, iemAImpl_pcmpeqd_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/*  Opcode 0xf3 0x0f 0x76 - invalid */
/*  Opcode 0xf2 0x0f 0x76 - invalid */


/** Opcode      0x0f 0x77 - emms (vex has vzeroall and vzeroupper here) */
FNIEMOP_DEF(iemOp_emms)
{
    IEMOP_MNEMONIC(emms, "emms");
    IEM_MC_BEGIN(0, 0);
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_MAYBE_RAISE_DEVICE_NOT_AVAILABLE();
    IEM_MC_MAYBE_RAISE_FPU_XCPT();
    IEM_MC_ACTUALIZE_FPU_STATE_FOR_CHANGE();
    IEM_MC_FPU_FROM_MMX_MODE();
    IEM_MC_ADVANCE_RIP_AND_FINISH();
    IEM_MC_END();
}

/*  Opcode 0x66 0x0f 0x77 - invalid */
/*  Opcode 0xf3 0x0f 0x77 - invalid */
/*  Opcode 0xf2 0x0f 0x77 - invalid */

/** Opcode      0x0f 0x78 - VMREAD Ey, Gy */
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
FNIEMOP_DEF(iemOp_vmread_Ey_Gy)
{
    IEMOP_MNEMONIC(vmread, "vmread Ey,Gy");
    IEMOP_HLP_IN_VMX_OPERATION("vmread", kVmxVDiag_Vmread);
    IEMOP_HLP_VMX_INSTR("vmread", kVmxVDiag_Vmread);
    IEMMODE const enmEffOpSize = IEM_IS_64BIT_CODE(pVCpu) ? IEMMODE_64BIT : IEMMODE_32BIT;

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        if (enmEffOpSize == IEMMODE_64BIT)
        {
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEMOP_HLP_DONE_DECODING_NO_SIZE_OP_REPZ_OR_REPNZ_PREFIXES();
            IEM_MC_ARG(uint64_t *, pu64Dst, 0);
            IEM_MC_ARG(uint64_t,   u64Enc,  1);
            IEM_MC_FETCH_GREG_U64(u64Enc, IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_REF_GREG_U64(pu64Dst, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_CIMPL_2(IEM_CIMPL_F_VMEXIT | IEM_CIMPL_F_STATUS_FLAGS,
                                RT_BIT_64(kIemNativeGstReg_GprFirst + IEM_GET_MODRM_RM(pVCpu, bRm)),
                                iemCImpl_vmread_reg64, pu64Dst, u64Enc);
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(0, 0);
            IEMOP_HLP_DONE_DECODING_NO_SIZE_OP_REPZ_OR_REPNZ_PREFIXES();
            IEM_MC_ARG(uint64_t *, pu64Dst, 0);
            IEM_MC_ARG(uint32_t,   u32Enc,  1);
            IEM_MC_FETCH_GREG_U32(u32Enc, IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_REF_GREG_U64(pu64Dst, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_CIMPL_2(IEM_CIMPL_F_VMEXIT | IEM_CIMPL_F_STATUS_FLAGS,
                                RT_BIT_64(kIemNativeGstReg_GprFirst + IEM_GET_MODRM_RM(pVCpu, bRm)),
                                iemCImpl_vmread_reg32, pu64Dst, u32Enc);
            IEM_MC_END();
        }
    }
    else
    {
        /*
         * Memory, register.
         */
        if (enmEffOpSize == IEMMODE_64BIT)
        {
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_ARG(RTGCPTR,         GCPtrVal,                                         1);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrVal, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_SIZE_OP_REPZ_OR_REPNZ_PREFIXES();
            IEM_MC_ARG_CONST(uint8_t,   iEffSeg, /*=*/ pVCpu->iem.s.iEffSeg,              0);
            IEM_MC_ARG(uint64_t,        u64Enc,                                           2);
            IEM_MC_FETCH_GREG_U64(u64Enc, IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_CALL_CIMPL_3(IEM_CIMPL_F_VMEXIT | IEM_CIMPL_F_STATUS_FLAGS, 0,
                                iemCImpl_vmread_mem_reg64, iEffSeg, GCPtrVal, u64Enc);
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(0, 0);
            IEM_MC_ARG(RTGCPTR,         GCPtrVal,                                         1);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrVal, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_SIZE_OP_REPZ_OR_REPNZ_PREFIXES();
            IEM_MC_ARG_CONST(uint8_t,   iEffSeg, /*=*/ pVCpu->iem.s.iEffSeg,              0);
            IEM_MC_ARG(uint32_t,        u32Enc,                                           2);
            IEM_MC_FETCH_GREG_U32(u32Enc, IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_CALL_CIMPL_3(IEM_CIMPL_F_VMEXIT | IEM_CIMPL_F_STATUS_FLAGS, 0,
                                iemCImpl_vmread_mem_reg32, iEffSeg, GCPtrVal, u32Enc);
            IEM_MC_END();
        }
    }
}
#else
FNIEMOP_UD_STUB(iemOp_vmread_Ey_Gy);
#endif

/*  Opcode 0x66 0x0f 0x78 - AMD Group 17 */
FNIEMOP_STUB(iemOp_AmdGrp17);
/*  Opcode 0xf3 0x0f 0x78 - invalid */
/*  Opcode 0xf2 0x0f 0x78 - invalid */

/** Opcode      0x0f 0x79 - VMWRITE Gy, Ey */
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
FNIEMOP_DEF(iemOp_vmwrite_Gy_Ey)
{
    IEMOP_MNEMONIC(vmwrite, "vmwrite Gy,Ey");
    IEMOP_HLP_IN_VMX_OPERATION("vmwrite", kVmxVDiag_Vmwrite);
    IEMOP_HLP_VMX_INSTR("vmwrite", kVmxVDiag_Vmwrite);
    IEMMODE const enmEffOpSize = IEM_IS_64BIT_CODE(pVCpu) ? IEMMODE_64BIT : IEMMODE_32BIT;

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        if (enmEffOpSize == IEMMODE_64BIT)
        {
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEMOP_HLP_DONE_DECODING_NO_SIZE_OP_REPZ_OR_REPNZ_PREFIXES();
            IEM_MC_ARG(uint64_t, u64Val, 0);
            IEM_MC_ARG(uint64_t, u64Enc, 1);
            IEM_MC_FETCH_GREG_U64(u64Val, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_FETCH_GREG_U64(u64Enc, IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_CALL_CIMPL_2(IEM_CIMPL_F_VMEXIT | IEM_CIMPL_F_STATUS_FLAGS, 0, iemCImpl_vmwrite_reg, u64Val, u64Enc);
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(0, 0);
            IEMOP_HLP_DONE_DECODING_NO_SIZE_OP_REPZ_OR_REPNZ_PREFIXES();
            IEM_MC_ARG(uint32_t, u32Val, 0);
            IEM_MC_ARG(uint32_t, u32Enc, 1);
            IEM_MC_FETCH_GREG_U32(u32Val, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_FETCH_GREG_U32(u32Enc, IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_CALL_CIMPL_2(IEM_CIMPL_F_VMEXIT | IEM_CIMPL_F_STATUS_FLAGS, 0, iemCImpl_vmwrite_reg, u32Val, u32Enc);
            IEM_MC_END();
        }
    }
    else
    {
        /*
         * Register, memory.
         */
        if (enmEffOpSize == IEMMODE_64BIT)
        {
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_ARG(RTGCPTR,         GCPtrVal,                                         1);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrVal, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_SIZE_OP_REPZ_OR_REPNZ_PREFIXES();
            IEM_MC_ARG_CONST(uint8_t,   iEffSeg, /*=*/ pVCpu->iem.s.iEffSeg,              0);
            IEM_MC_ARG(uint64_t,        u64Enc,                                           2);
            IEM_MC_FETCH_GREG_U64(u64Enc, IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_CALL_CIMPL_3(IEM_CIMPL_F_VMEXIT | IEM_CIMPL_F_STATUS_FLAGS, 0,
                                iemCImpl_vmwrite_mem, iEffSeg, GCPtrVal, u64Enc);
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(0, 0);
            IEM_MC_ARG(RTGCPTR,         GCPtrVal,                                         1);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrVal, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_SIZE_OP_REPZ_OR_REPNZ_PREFIXES();
            IEM_MC_ARG(uint32_t,        u32Enc,                                           2);
            IEM_MC_ARG_CONST(uint8_t,   iEffSeg, /*=*/ pVCpu->iem.s.iEffSeg,              0);
            IEM_MC_FETCH_GREG_U32(u32Enc, IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_CALL_CIMPL_3(IEM_CIMPL_F_VMEXIT | IEM_CIMPL_F_STATUS_FLAGS, 0,
                                iemCImpl_vmwrite_mem, iEffSeg, GCPtrVal, u32Enc);
            IEM_MC_END();
        }
    }
}
#else
FNIEMOP_UD_STUB(iemOp_vmwrite_Gy_Ey);
#endif
/*  Opcode 0x66 0x0f 0x79 - invalid */
/*  Opcode 0xf3 0x0f 0x79 - invalid */
/*  Opcode 0xf2 0x0f 0x79 - invalid */

/*  Opcode      0x0f 0x7a - invalid */
/*  Opcode 0x66 0x0f 0x7a - invalid */
/*  Opcode 0xf3 0x0f 0x7a - invalid */
/*  Opcode 0xf2 0x0f 0x7a - invalid */

/*  Opcode      0x0f 0x7b - invalid */
/*  Opcode 0x66 0x0f 0x7b - invalid */
/*  Opcode 0xf3 0x0f 0x7b - invalid */
/*  Opcode 0xf2 0x0f 0x7b - invalid */

/*  Opcode      0x0f 0x7c - invalid */


/** Opcode 0x66 0x0f 0x7c - haddpd Vpd, Wpd */
FNIEMOP_DEF(iemOp_haddpd_Vpd_Wpd)
{
    IEMOP_MNEMONIC2(RM, HADDPD, haddpd, Vpd, Wpd, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse3Fp_FullFull_To_Full, iemAImpl_haddpd_u128);
}


/*  Opcode 0xf3 0x0f 0x7c - invalid */


/** Opcode 0xf2 0x0f 0x7c - haddps Vps, Wps */
FNIEMOP_DEF(iemOp_haddps_Vps_Wps)
{
    IEMOP_MNEMONIC2(RM, HADDPS, haddps, Vps, Wps, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse3Fp_FullFull_To_Full, iemAImpl_haddps_u128);
}


/*  Opcode      0x0f 0x7d - invalid */


/** Opcode 0x66 0x0f 0x7d - hsubpd Vpd, Wpd */
FNIEMOP_DEF(iemOp_hsubpd_Vpd_Wpd)
{
    IEMOP_MNEMONIC2(RM, HSUBPD, hsubpd, Vpd, Wpd, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse3Fp_FullFull_To_Full, iemAImpl_hsubpd_u128);
}


/*  Opcode 0xf3 0x0f 0x7d - invalid */


/** Opcode 0xf2 0x0f 0x7d - hsubps Vps, Wps */
FNIEMOP_DEF(iemOp_hsubps_Vps_Wps)
{
    IEMOP_MNEMONIC2(RM, HSUBPS, hsubps, Vps, Wps, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse3Fp_FullFull_To_Full, iemAImpl_hsubps_u128);
}


/** Opcode      0x0f 0x7e - movd_q Ey, Pd */
FNIEMOP_DEF(iemOp_movd_q_Ey_Pd)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_REX_W)
    {
        /**
         * @opcode      0x7e
         * @opcodesub   rex.w=1
         * @oppfx       none
         * @opcpuid     mmx
         * @opgroup     og_mmx_datamove
         * @opxcpttype  5
         * @optest      64-bit / op1=1 op2=2   -> op1=2   ftw=0xff
         * @optest      64-bit / op1=0 op2=-42 -> op1=-42 ftw=0xff
         */
        IEMOP_MNEMONIC2(MR, MOVQ, movq, Eq_WO, Pq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OZ_PFX);
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* greg64, MMX */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fMmx);
            IEM_MC_LOCAL(uint64_t, u64Tmp);

            IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_FPU_STATE_FOR_CHANGE();
            IEM_MC_FPU_TO_MMX_MODE();

            IEM_MC_FETCH_MREG_U64(u64Tmp, IEM_GET_MODRM_REG_8(bRm));
            IEM_MC_STORE_GREG_U64(IEM_GET_MODRM_RM(pVCpu, bRm), u64Tmp);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /* [mem64], MMX */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_LOCAL(RTGCPTR, GCPtrEffSrc);
            IEM_MC_LOCAL(uint64_t, u64Tmp);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fMmx);
            IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_FPU_STATE_FOR_CHANGE();

            IEM_MC_FETCH_MREG_U64(u64Tmp, IEM_GET_MODRM_REG_8(bRm));
            IEM_MC_STORE_MEM_U64(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, u64Tmp);
            IEM_MC_FPU_TO_MMX_MODE();

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        /**
         * @opdone
         * @opcode      0x7e
         * @opcodesub   rex.w=0
         * @oppfx       none
         * @opcpuid     mmx
         * @opgroup     og_mmx_datamove
         * @opxcpttype  5
         * @opfunction  iemOp_movd_q_Pd_Ey
         * @optest      op1=1 op2=2   -> op1=2   ftw=0xff
         * @optest      op1=0 op2=-42 -> op1=-42 ftw=0xff
         */
        IEMOP_MNEMONIC2(MR, MOVD, movd, Ed_WO, Pd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OZ_PFX);
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* greg32, MMX */
            IEM_MC_BEGIN(0, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fMmx);
            IEM_MC_LOCAL(uint32_t, u32Tmp);

            IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_FPU_STATE_FOR_CHANGE();
            IEM_MC_FPU_TO_MMX_MODE();

            IEM_MC_FETCH_MREG_U32(u32Tmp, IEM_GET_MODRM_REG_8(bRm), 0);
            IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_RM(pVCpu, bRm), u32Tmp);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /* [mem32], MMX */
            IEM_MC_BEGIN(0, 0);
            IEM_MC_LOCAL(RTGCPTR, GCPtrEffSrc);
            IEM_MC_LOCAL(uint32_t, u32Tmp);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fMmx);
            IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_FPU_STATE_FOR_CHANGE();

            IEM_MC_FETCH_MREG_U32(u32Tmp, IEM_GET_MODRM_REG_8(bRm), 0);
            IEM_MC_STORE_MEM_U32(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, u32Tmp);
            IEM_MC_FPU_TO_MMX_MODE();

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


FNIEMOP_DEF(iemOp_movd_q_Ey_Vy)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_REX_W)
    {
        /**
         * @opcode      0x7e
         * @opcodesub   rex.w=1
         * @oppfx       0x66
         * @opcpuid     sse2
         * @opgroup     og_sse2_simdint_datamove
         * @opxcpttype  5
         * @optest      64-bit / op1=1 op2=2   -> op1=2
         * @optest      64-bit / op1=0 op2=-42 -> op1=-42
         */
        IEMOP_MNEMONIC2(MR, MOVQ, movq, Eq_WO, Vq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OZ_PFX);
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* greg64, XMM */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
            IEM_MC_LOCAL(uint64_t, u64Tmp);

            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_ACTUALIZE_SSE_STATE_FOR_READ();

            IEM_MC_FETCH_XREG_U64(u64Tmp, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /* a_iQword*/);
            IEM_MC_STORE_GREG_U64(IEM_GET_MODRM_RM(pVCpu, bRm), u64Tmp);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /* [mem64], XMM */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_LOCAL(RTGCPTR, GCPtrEffSrc);
            IEM_MC_LOCAL(uint64_t, u64Tmp);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_ACTUALIZE_SSE_STATE_FOR_READ();

            IEM_MC_FETCH_XREG_U64(u64Tmp, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /* a_iQword*/);
            IEM_MC_STORE_MEM_U64(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, u64Tmp);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        /**
         * @opdone
         * @opcode      0x7e
         * @opcodesub   rex.w=0
         * @oppfx       0x66
         * @opcpuid     sse2
         * @opgroup     og_sse2_simdint_datamove
         * @opxcpttype  5
         * @opfunction  iemOp_movd_q_Vy_Ey
         * @optest      op1=1 op2=2   -> op1=2
         * @optest      op1=0 op2=-42 -> op1=-42
         */
        IEMOP_MNEMONIC2(MR, MOVD, movd, Ed_WO, Vd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OZ_PFX);
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* greg32, XMM */
            IEM_MC_BEGIN(0, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
            IEM_MC_LOCAL(uint32_t, u32Tmp);

            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_ACTUALIZE_SSE_STATE_FOR_READ();

            IEM_MC_FETCH_XREG_U32(u32Tmp, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iDword*/);
            IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_RM(pVCpu, bRm), u32Tmp);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /* [mem32], XMM */
            IEM_MC_BEGIN(0, 0);
            IEM_MC_LOCAL(RTGCPTR, GCPtrEffSrc);
            IEM_MC_LOCAL(uint32_t, u32Tmp);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_ACTUALIZE_SSE_STATE_FOR_READ();

            IEM_MC_FETCH_XREG_U32(u32Tmp, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iDword*/);
            IEM_MC_STORE_MEM_U32(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, u32Tmp);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}

/**
 * @opcode      0x7e
 * @oppfx       0xf3
 * @opcpuid     sse2
 * @opgroup     og_sse2_pcksclr_datamove
 * @opxcpttype  none
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_movq_Vq_Wq)
{
    IEMOP_MNEMONIC2(RM, MOVQ, movq, VqZx_WO, Wq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM128, XMM64.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_LOCAL(uint64_t,                  uSrc);

        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();

        IEM_MC_FETCH_XREG_U64(uSrc, IEM_GET_MODRM_RM(pVCpu, bRm), 0 /* a_iQword*/);
        IEM_MC_STORE_XREG_U64_ZX_U128(IEM_GET_MODRM_REG(pVCpu, bRm), uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM128, [mem64].
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U64(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_XREG_U64_ZX_U128(IEM_GET_MODRM_REG(pVCpu, bRm), uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}

/*  Opcode 0xf2 0x0f 0x7e - invalid */


/** Opcode      0x0f 0x7f - movq Qq, Pq */
FNIEMOP_DEF(iemOp_movq_Qq_Pq)
{
    IEMOP_MNEMONIC2(MR, MOVQ, movq, Qq_WO, Pq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OZ_PFX | IEMOPHINT_IGNORES_REXW);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * MMX, MMX.
         */
        /** @todo testcase: REX.B / REX.R and MMX register indexing. Ignored? */
        /** @todo testcase: REX.B / REX.R and segment register indexing. Ignored? */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fMmx);
        IEM_MC_LOCAL(uint64_t, u64Tmp);
        IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_FPU_STATE_FOR_CHANGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_FETCH_MREG_U64(u64Tmp, IEM_GET_MODRM_REG_8(bRm));
        IEM_MC_STORE_MREG_U64(IEM_GET_MODRM_RM_8(bRm), u64Tmp);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * [mem64], MMX.
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(uint64_t, u64Tmp);
        IEM_MC_LOCAL(RTGCPTR,  GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fMmx);
        IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_FPU_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MREG_U64(u64Tmp, IEM_GET_MODRM_REG_8(bRm));
        IEM_MC_STORE_MEM_U64(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, u64Tmp);
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}

/** Opcode 0x66 0x0f 0x7f - movdqa Wx,Vx */
FNIEMOP_DEF(iemOp_movdqa_Wx_Vx)
{
    IEMOP_MNEMONIC2(MR, MOVDQA, movdqa, Wx_WO, Vx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM, XMM.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();
        IEM_MC_COPY_XREG_U128(IEM_GET_MODRM_RM(pVCpu, bRm),
                              IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * [mem128], XMM.
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(RTUINT128U, u128Tmp);
        IEM_MC_LOCAL(RTGCPTR,    GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_READ();

        IEM_MC_FETCH_XREG_U128(u128Tmp, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_STORE_MEM_U128_ALIGN_SSE(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, u128Tmp);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}

/** Opcode 0xf3 0x0f 0x7f - movdqu Wx,Vx */
FNIEMOP_DEF(iemOp_movdqu_Wx_Vx)
{
    IEMOP_MNEMONIC2(MR, MOVDQU, movdqu, Wx_WO, Vx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM, XMM.
         */
        IEM_MC_BEGIN(0, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();
        IEM_MC_COPY_XREG_U128(IEM_GET_MODRM_RM(pVCpu, bRm),
                              IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * [mem128], XMM.
         */
        IEM_MC_BEGIN(0, 0);
        IEM_MC_LOCAL(RTUINT128U, u128Tmp);
        IEM_MC_LOCAL(RTGCPTR,    GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_READ();

        IEM_MC_FETCH_XREG_U128(u128Tmp, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_STORE_MEM_U128_NO_AC(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, u128Tmp);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}

/*  Opcode 0xf2 0x0f 0x7f - invalid */


/**
 * @opcode      0x80
 * @opfltest    of
 */
FNIEMOP_DEF(iemOp_jo_Jv)
{
    IEMOP_MNEMONIC(jo_Jv, "jo  Jv");
    IEMOP_HLP_MIN_386();
    IEMOP_HLP_DEFAULT_64BIT_OP_SIZE_AND_INTEL_IGNORES_OP_SIZE_PREFIX();
    if (pVCpu->iem.s.enmEffOpSize == IEMMODE_16BIT)
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int16_t i16Imm; IEM_OPCODE_GET_NEXT_S16(&i16Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_OF) {
            IEM_MC_REL_JMP_S16_AND_FINISH(i16Imm);
        } IEM_MC_ELSE() {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
    else
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int32_t i32Imm; IEM_OPCODE_GET_NEXT_S32(&i32Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_OF) {
            IEM_MC_REL_JMP_S32_AND_FINISH(i32Imm);
        } IEM_MC_ELSE() {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x81
 * @opfltest    of
 */
FNIEMOP_DEF(iemOp_jno_Jv)
{
    IEMOP_MNEMONIC(jno_Jv, "jno Jv");
    IEMOP_HLP_MIN_386();
    IEMOP_HLP_DEFAULT_64BIT_OP_SIZE_AND_INTEL_IGNORES_OP_SIZE_PREFIX();
    if (pVCpu->iem.s.enmEffOpSize == IEMMODE_16BIT)
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int16_t i16Imm; IEM_OPCODE_GET_NEXT_S16(&i16Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_OF) {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ELSE() {
            IEM_MC_REL_JMP_S16_AND_FINISH(i16Imm);
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
    else
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int32_t i32Imm; IEM_OPCODE_GET_NEXT_S32(&i32Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_OF) {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ELSE() {
            IEM_MC_REL_JMP_S32_AND_FINISH(i32Imm);
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x82
 * @opfltest    cf
 */
FNIEMOP_DEF(iemOp_jc_Jv)
{
    IEMOP_MNEMONIC(jc_Jv, "jc/jb/jnae Jv");
    IEMOP_HLP_MIN_386();
    IEMOP_HLP_DEFAULT_64BIT_OP_SIZE_AND_INTEL_IGNORES_OP_SIZE_PREFIX();
    if (pVCpu->iem.s.enmEffOpSize == IEMMODE_16BIT)
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int16_t i16Imm; IEM_OPCODE_GET_NEXT_S16(&i16Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_CF) {
            IEM_MC_REL_JMP_S16_AND_FINISH(i16Imm);
        } IEM_MC_ELSE() {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
    else
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int32_t i32Imm; IEM_OPCODE_GET_NEXT_S32(&i32Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_CF) {
            IEM_MC_REL_JMP_S32_AND_FINISH(i32Imm);
        } IEM_MC_ELSE() {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x83
 * @opfltest    cf
 */
FNIEMOP_DEF(iemOp_jnc_Jv)
{
    IEMOP_MNEMONIC(jnc_Jv, "jnc/jnb/jae Jv");
    IEMOP_HLP_MIN_386();
    IEMOP_HLP_DEFAULT_64BIT_OP_SIZE_AND_INTEL_IGNORES_OP_SIZE_PREFIX();
    if (pVCpu->iem.s.enmEffOpSize == IEMMODE_16BIT)
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int16_t i16Imm; IEM_OPCODE_GET_NEXT_S16(&i16Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_CF) {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ELSE() {
            IEM_MC_REL_JMP_S16_AND_FINISH(i16Imm);
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
    else
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int32_t i32Imm; IEM_OPCODE_GET_NEXT_S32(&i32Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_CF) {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ELSE() {
            IEM_MC_REL_JMP_S32_AND_FINISH(i32Imm);
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x84
 * @opfltest    zf
 */
FNIEMOP_DEF(iemOp_je_Jv)
{
    IEMOP_MNEMONIC(je_Jv, "je/jz Jv");
    IEMOP_HLP_MIN_386();
    IEMOP_HLP_DEFAULT_64BIT_OP_SIZE_AND_INTEL_IGNORES_OP_SIZE_PREFIX();
    if (pVCpu->iem.s.enmEffOpSize == IEMMODE_16BIT)
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int16_t i16Imm; IEM_OPCODE_GET_NEXT_S16(&i16Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_ZF) {
            IEM_MC_REL_JMP_S16_AND_FINISH(i16Imm);
        } IEM_MC_ELSE() {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
    else
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int32_t i32Imm; IEM_OPCODE_GET_NEXT_S32(&i32Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_ZF) {
            IEM_MC_REL_JMP_S32_AND_FINISH(i32Imm);
        } IEM_MC_ELSE() {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x85
 * @opfltest    zf
 */
FNIEMOP_DEF(iemOp_jne_Jv)
{
    IEMOP_MNEMONIC(jne_Jv, "jne/jnz Jv");
    IEMOP_HLP_MIN_386();
    IEMOP_HLP_DEFAULT_64BIT_OP_SIZE_AND_INTEL_IGNORES_OP_SIZE_PREFIX();
    if (pVCpu->iem.s.enmEffOpSize == IEMMODE_16BIT)
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int16_t i16Imm; IEM_OPCODE_GET_NEXT_S16(&i16Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_ZF) {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ELSE() {
            IEM_MC_REL_JMP_S16_AND_FINISH(i16Imm);
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
    else
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int32_t i32Imm; IEM_OPCODE_GET_NEXT_S32(&i32Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_ZF) {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ELSE() {
            IEM_MC_REL_JMP_S32_AND_FINISH(i32Imm);
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x86
 * @opfltest    cf,zf
 */
FNIEMOP_DEF(iemOp_jbe_Jv)
{
    IEMOP_MNEMONIC(jbe_Jv, "jbe/jna Jv");
    IEMOP_HLP_MIN_386();
    IEMOP_HLP_DEFAULT_64BIT_OP_SIZE_AND_INTEL_IGNORES_OP_SIZE_PREFIX();
    if (pVCpu->iem.s.enmEffOpSize == IEMMODE_16BIT)
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int16_t i16Imm; IEM_OPCODE_GET_NEXT_S16(&i16Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_ANY_BITS_SET(X86_EFL_CF | X86_EFL_ZF) {
            IEM_MC_REL_JMP_S16_AND_FINISH(i16Imm);
        } IEM_MC_ELSE() {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
    else
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int32_t i32Imm; IEM_OPCODE_GET_NEXT_S32(&i32Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_ANY_BITS_SET(X86_EFL_CF | X86_EFL_ZF) {
            IEM_MC_REL_JMP_S32_AND_FINISH(i32Imm);
        } IEM_MC_ELSE() {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x87
 * @opfltest    cf,zf
 */
FNIEMOP_DEF(iemOp_jnbe_Jv)
{
    IEMOP_MNEMONIC(ja_Jv, "jnbe/ja Jv");
    IEMOP_HLP_MIN_386();
    IEMOP_HLP_DEFAULT_64BIT_OP_SIZE_AND_INTEL_IGNORES_OP_SIZE_PREFIX();
    if (pVCpu->iem.s.enmEffOpSize == IEMMODE_16BIT)
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int16_t i16Imm; IEM_OPCODE_GET_NEXT_S16(&i16Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_ANY_BITS_SET(X86_EFL_CF | X86_EFL_ZF) {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ELSE() {
            IEM_MC_REL_JMP_S16_AND_FINISH(i16Imm);
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
    else
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int32_t i32Imm; IEM_OPCODE_GET_NEXT_S32(&i32Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_ANY_BITS_SET(X86_EFL_CF | X86_EFL_ZF) {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ELSE() {
            IEM_MC_REL_JMP_S32_AND_FINISH(i32Imm);
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x88
 * @opfltest    sf
 */
FNIEMOP_DEF(iemOp_js_Jv)
{
    IEMOP_MNEMONIC(js_Jv, "js  Jv");
    IEMOP_HLP_MIN_386();
    IEMOP_HLP_DEFAULT_64BIT_OP_SIZE_AND_INTEL_IGNORES_OP_SIZE_PREFIX();
    if (pVCpu->iem.s.enmEffOpSize == IEMMODE_16BIT)
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int16_t i16Imm; IEM_OPCODE_GET_NEXT_S16(&i16Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_SF) {
            IEM_MC_REL_JMP_S16_AND_FINISH(i16Imm);
        } IEM_MC_ELSE() {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
    else
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int32_t i32Imm; IEM_OPCODE_GET_NEXT_S32(&i32Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_SF) {
            IEM_MC_REL_JMP_S32_AND_FINISH(i32Imm);
        } IEM_MC_ELSE() {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x89
 * @opfltest    sf
 */
FNIEMOP_DEF(iemOp_jns_Jv)
{
    IEMOP_MNEMONIC(jns_Jv, "jns Jv");
    IEMOP_HLP_MIN_386();
    IEMOP_HLP_DEFAULT_64BIT_OP_SIZE_AND_INTEL_IGNORES_OP_SIZE_PREFIX();
    if (pVCpu->iem.s.enmEffOpSize == IEMMODE_16BIT)
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int16_t i16Imm; IEM_OPCODE_GET_NEXT_S16(&i16Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_SF) {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ELSE() {
            IEM_MC_REL_JMP_S16_AND_FINISH(i16Imm);
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
    else
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int32_t i32Imm; IEM_OPCODE_GET_NEXT_S32(&i32Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_SF) {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ELSE() {
            IEM_MC_REL_JMP_S32_AND_FINISH(i32Imm);
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x8a
 * @opfltest    pf
 */
FNIEMOP_DEF(iemOp_jp_Jv)
{
    IEMOP_MNEMONIC(jp_Jv, "jp  Jv");
    IEMOP_HLP_MIN_386();
    IEMOP_HLP_DEFAULT_64BIT_OP_SIZE_AND_INTEL_IGNORES_OP_SIZE_PREFIX();
    if (pVCpu->iem.s.enmEffOpSize == IEMMODE_16BIT)
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int16_t i16Imm; IEM_OPCODE_GET_NEXT_S16(&i16Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_PF) {
            IEM_MC_REL_JMP_S16_AND_FINISH(i16Imm);
        } IEM_MC_ELSE() {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
    else
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int32_t i32Imm; IEM_OPCODE_GET_NEXT_S32(&i32Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_PF) {
            IEM_MC_REL_JMP_S32_AND_FINISH(i32Imm);
        } IEM_MC_ELSE() {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x8b
 * @opfltest    pf
 */
FNIEMOP_DEF(iemOp_jnp_Jv)
{
    IEMOP_MNEMONIC(jnp_Jv, "jnp Jv");
    IEMOP_HLP_MIN_386();
    IEMOP_HLP_DEFAULT_64BIT_OP_SIZE_AND_INTEL_IGNORES_OP_SIZE_PREFIX();
    if (pVCpu->iem.s.enmEffOpSize == IEMMODE_16BIT)
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int16_t i16Imm; IEM_OPCODE_GET_NEXT_S16(&i16Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_PF) {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ELSE() {
            IEM_MC_REL_JMP_S16_AND_FINISH(i16Imm);
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
    else
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int32_t i32Imm; IEM_OPCODE_GET_NEXT_S32(&i32Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_PF) {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ELSE() {
            IEM_MC_REL_JMP_S32_AND_FINISH(i32Imm);
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x8c
 * @opfltest    sf,of
 */
FNIEMOP_DEF(iemOp_jl_Jv)
{
    IEMOP_MNEMONIC(jl_Jv, "jl/jnge Jv");
    IEMOP_HLP_MIN_386();
    IEMOP_HLP_DEFAULT_64BIT_OP_SIZE_AND_INTEL_IGNORES_OP_SIZE_PREFIX();
    if (pVCpu->iem.s.enmEffOpSize == IEMMODE_16BIT)
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int16_t i16Imm; IEM_OPCODE_GET_NEXT_S16(&i16Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BITS_NE(X86_EFL_SF, X86_EFL_OF) {
            IEM_MC_REL_JMP_S16_AND_FINISH(i16Imm);
        } IEM_MC_ELSE() {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
    else
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int32_t i32Imm; IEM_OPCODE_GET_NEXT_S32(&i32Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BITS_NE(X86_EFL_SF, X86_EFL_OF) {
            IEM_MC_REL_JMP_S32_AND_FINISH(i32Imm);
        } IEM_MC_ELSE() {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x8d
 * @opfltest    sf,of
 */
FNIEMOP_DEF(iemOp_jnl_Jv)
{
    IEMOP_MNEMONIC(jge_Jv, "jnl/jge Jv");
    IEMOP_HLP_MIN_386();
    IEMOP_HLP_DEFAULT_64BIT_OP_SIZE_AND_INTEL_IGNORES_OP_SIZE_PREFIX();
    if (pVCpu->iem.s.enmEffOpSize == IEMMODE_16BIT)
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int16_t i16Imm; IEM_OPCODE_GET_NEXT_S16(&i16Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BITS_NE(X86_EFL_SF, X86_EFL_OF) {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ELSE() {
            IEM_MC_REL_JMP_S16_AND_FINISH(i16Imm);
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
    else
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int32_t i32Imm; IEM_OPCODE_GET_NEXT_S32(&i32Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BITS_NE(X86_EFL_SF, X86_EFL_OF) {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ELSE() {
            IEM_MC_REL_JMP_S32_AND_FINISH(i32Imm);
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x8e
 * @opfltest    zf,sf,of
 */
FNIEMOP_DEF(iemOp_jle_Jv)
{
    IEMOP_MNEMONIC(jle_Jv, "jle/jng Jv");
    IEMOP_HLP_MIN_386();
    IEMOP_HLP_DEFAULT_64BIT_OP_SIZE_AND_INTEL_IGNORES_OP_SIZE_PREFIX();
    if (pVCpu->iem.s.enmEffOpSize == IEMMODE_16BIT)
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int16_t i16Imm; IEM_OPCODE_GET_NEXT_S16(&i16Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET_OR_BITS_NE(X86_EFL_ZF, X86_EFL_SF, X86_EFL_OF) {
            IEM_MC_REL_JMP_S16_AND_FINISH(i16Imm);
        } IEM_MC_ELSE() {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
    else
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int32_t i32Imm; IEM_OPCODE_GET_NEXT_S32(&i32Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET_OR_BITS_NE(X86_EFL_ZF, X86_EFL_SF, X86_EFL_OF) {
            IEM_MC_REL_JMP_S32_AND_FINISH(i32Imm);
        } IEM_MC_ELSE() {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x8f
 * @opfltest    zf,sf,of
 */
FNIEMOP_DEF(iemOp_jnle_Jv)
{
    IEMOP_MNEMONIC(jg_Jv, "jnle/jg Jv");
    IEMOP_HLP_MIN_386();
    IEMOP_HLP_DEFAULT_64BIT_OP_SIZE_AND_INTEL_IGNORES_OP_SIZE_PREFIX();
    if (pVCpu->iem.s.enmEffOpSize == IEMMODE_16BIT)
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int16_t i16Imm; IEM_OPCODE_GET_NEXT_S16(&i16Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET_OR_BITS_NE(X86_EFL_ZF, X86_EFL_SF, X86_EFL_OF) {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ELSE() {
            IEM_MC_REL_JMP_S16_AND_FINISH(i16Imm);
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
    else
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        int32_t i32Imm; IEM_OPCODE_GET_NEXT_S32(&i32Imm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET_OR_BITS_NE(X86_EFL_ZF, X86_EFL_SF, X86_EFL_OF) {
            IEM_MC_ADVANCE_RIP_AND_FINISH();
        } IEM_MC_ELSE() {
            IEM_MC_REL_JMP_S32_AND_FINISH(i32Imm);
        } IEM_MC_ENDIF();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x90
 * @opfltest    of
 */
FNIEMOP_DEF(iemOp_seto_Eb)
{
    IEMOP_MNEMONIC(seto_Eb, "seto Eb");
    IEMOP_HLP_MIN_386();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);

    /** @todo Encoding test: Check if the 'reg' field is ignored or decoded in
     *        any way. AMD says it's "unused", whatever that means.  We're
     *        ignoring for now. */
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /* register target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_OF) {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 1);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 0);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /* memory target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_OF) {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 1);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 0);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x91
 * @opfltest    of
 */
FNIEMOP_DEF(iemOp_setno_Eb)
{
    IEMOP_MNEMONIC(setno_Eb, "setno Eb");
    IEMOP_HLP_MIN_386();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);

    /** @todo Encoding test: Check if the 'reg' field is ignored or decoded in
     *        any way. AMD says it's "unused", whatever that means.  We're
     *        ignoring for now. */
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /* register target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_OF) {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 0);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 1);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /* memory target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_OF) {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 0);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 1);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x92
 * @opfltest    cf
 */
FNIEMOP_DEF(iemOp_setc_Eb)
{
    IEMOP_MNEMONIC(setc_Eb, "setc Eb");
    IEMOP_HLP_MIN_386();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);

    /** @todo Encoding test: Check if the 'reg' field is ignored or decoded in
     *        any way. AMD says it's "unused", whatever that means.  We're
     *        ignoring for now. */
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /* register target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_CF) {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 1);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 0);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /* memory target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_CF) {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 1);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 0);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x93
 * @opfltest    cf
 */
FNIEMOP_DEF(iemOp_setnc_Eb)
{
    IEMOP_MNEMONIC(setnc_Eb, "setnc Eb");
    IEMOP_HLP_MIN_386();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);

    /** @todo Encoding test: Check if the 'reg' field is ignored or decoded in
     *        any way. AMD says it's "unused", whatever that means.  We're
     *        ignoring for now. */
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /* register target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_CF) {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 0);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 1);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /* memory target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_CF) {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 0);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 1);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x94
 * @opfltest    zf
 */
FNIEMOP_DEF(iemOp_sete_Eb)
{
    IEMOP_MNEMONIC(sete_Eb, "sete Eb");
    IEMOP_HLP_MIN_386();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);

    /** @todo Encoding test: Check if the 'reg' field is ignored or decoded in
     *        any way. AMD says it's "unused", whatever that means.  We're
     *        ignoring for now. */
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /* register target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_ZF) {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 1);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 0);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /* memory target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_ZF) {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 1);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 0);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x95
 * @opfltest    zf
 */
FNIEMOP_DEF(iemOp_setne_Eb)
{
    IEMOP_MNEMONIC(setne_Eb, "setne Eb");
    IEMOP_HLP_MIN_386();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);

    /** @todo Encoding test: Check if the 'reg' field is ignored or decoded in
     *        any way. AMD says it's "unused", whatever that means.  We're
     *        ignoring for now. */
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /* register target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_ZF) {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 0);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 1);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /* memory target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_ZF) {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 0);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 1);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x96
 * @opfltest    cf,zf
 */
FNIEMOP_DEF(iemOp_setbe_Eb)
{
    IEMOP_MNEMONIC(setbe_Eb, "setbe Eb");
    IEMOP_HLP_MIN_386();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);

    /** @todo Encoding test: Check if the 'reg' field is ignored or decoded in
     *        any way. AMD says it's "unused", whatever that means.  We're
     *        ignoring for now. */
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /* register target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_ANY_BITS_SET(X86_EFL_CF | X86_EFL_ZF) {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 1);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 0);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /* memory target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_ANY_BITS_SET(X86_EFL_CF | X86_EFL_ZF) {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 1);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 0);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x97
 * @opfltest    cf,zf
 */
FNIEMOP_DEF(iemOp_setnbe_Eb)
{
    IEMOP_MNEMONIC(setnbe_Eb, "setnbe Eb");
    IEMOP_HLP_MIN_386();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);

    /** @todo Encoding test: Check if the 'reg' field is ignored or decoded in
     *        any way. AMD says it's "unused", whatever that means.  We're
     *        ignoring for now. */
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /* register target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_ANY_BITS_SET(X86_EFL_CF | X86_EFL_ZF) {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 0);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 1);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /* memory target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_ANY_BITS_SET(X86_EFL_CF | X86_EFL_ZF) {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 0);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 1);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x98
 * @opfltest    sf
 */
FNIEMOP_DEF(iemOp_sets_Eb)
{
    IEMOP_MNEMONIC(sets_Eb, "sets Eb");
    IEMOP_HLP_MIN_386();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);

    /** @todo Encoding test: Check if the 'reg' field is ignored or decoded in
     *        any way. AMD says it's "unused", whatever that means.  We're
     *        ignoring for now. */
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /* register target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_SF) {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 1);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 0);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /* memory target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_SF) {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 1);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 0);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x99
 * @opfltest    sf
 */
FNIEMOP_DEF(iemOp_setns_Eb)
{
    IEMOP_MNEMONIC(setns_Eb, "setns Eb");
    IEMOP_HLP_MIN_386();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);

    /** @todo Encoding test: Check if the 'reg' field is ignored or decoded in
     *        any way. AMD says it's "unused", whatever that means.  We're
     *        ignoring for now. */
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /* register target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_SF) {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 0);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 1);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /* memory target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_SF) {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 0);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 1);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x9a
 * @opfltest    pf
 */
FNIEMOP_DEF(iemOp_setp_Eb)
{
    IEMOP_MNEMONIC(setp_Eb, "setp Eb");
    IEMOP_HLP_MIN_386();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);

    /** @todo Encoding test: Check if the 'reg' field is ignored or decoded in
     *        any way. AMD says it's "unused", whatever that means.  We're
     *        ignoring for now. */
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /* register target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_PF) {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 1);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 0);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /* memory target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_PF) {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 1);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 0);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x9b
 * @opfltest    pf
 */
FNIEMOP_DEF(iemOp_setnp_Eb)
{
    IEMOP_MNEMONIC(setnp_Eb, "setnp Eb");
    IEMOP_HLP_MIN_386();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);

    /** @todo Encoding test: Check if the 'reg' field is ignored or decoded in
     *        any way. AMD says it's "unused", whatever that means.  We're
     *        ignoring for now. */
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /* register target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_PF) {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 0);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 1);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /* memory target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET(X86_EFL_PF) {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 0);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 1);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x9c
 * @opfltest    sf,of
 */
FNIEMOP_DEF(iemOp_setl_Eb)
{
    IEMOP_MNEMONIC(setl_Eb, "setl Eb");
    IEMOP_HLP_MIN_386();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);

    /** @todo Encoding test: Check if the 'reg' field is ignored or decoded in
     *        any way. AMD says it's "unused", whatever that means.  We're
     *        ignoring for now. */
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /* register target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BITS_NE(X86_EFL_SF, X86_EFL_OF) {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 1);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 0);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /* memory target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BITS_NE(X86_EFL_SF, X86_EFL_OF) {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 1);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 0);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x9d
 * @opfltest    sf,of
 */
FNIEMOP_DEF(iemOp_setnl_Eb)
{
    IEMOP_MNEMONIC(setnl_Eb, "setnl Eb");
    IEMOP_HLP_MIN_386();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);

    /** @todo Encoding test: Check if the 'reg' field is ignored or decoded in
     *        any way. AMD says it's "unused", whatever that means.  We're
     *        ignoring for now. */
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /* register target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BITS_NE(X86_EFL_SF, X86_EFL_OF) {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 0);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 1);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /* memory target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BITS_NE(X86_EFL_SF, X86_EFL_OF) {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 0);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 1);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x9e
 * @opfltest    zf,sf,of
 */
FNIEMOP_DEF(iemOp_setle_Eb)
{
    IEMOP_MNEMONIC(setle_Eb, "setle Eb");
    IEMOP_HLP_MIN_386();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);

    /** @todo Encoding test: Check if the 'reg' field is ignored or decoded in
     *        any way. AMD says it's "unused", whatever that means.  We're
     *        ignoring for now. */
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /* register target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET_OR_BITS_NE(X86_EFL_ZF, X86_EFL_SF, X86_EFL_OF) {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 1);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 0);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /* memory target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET_OR_BITS_NE(X86_EFL_ZF, X86_EFL_SF, X86_EFL_OF) {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 1);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 0);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x9f
 * @opfltest    zf,sf,of
 */
FNIEMOP_DEF(iemOp_setnle_Eb)
{
    IEMOP_MNEMONIC(setnle_Eb, "setnle Eb");
    IEMOP_HLP_MIN_386();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);

    /** @todo Encoding test: Check if the 'reg' field is ignored or decoded in
     *        any way. AMD says it's "unused", whatever that means.  We're
     *        ignoring for now. */
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /* register target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET_OR_BITS_NE(X86_EFL_ZF, X86_EFL_SF, X86_EFL_OF) {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 0);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_GREG_U8_CONST(IEM_GET_MODRM_RM(pVCpu, bRm), 1);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /* memory target */
        IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
        IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_IF_EFL_BIT_SET_OR_BITS_NE(X86_EFL_ZF, X86_EFL_SF, X86_EFL_OF) {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 0);
        } IEM_MC_ELSE() {
            IEM_MC_STORE_MEM_U8_CONST(pVCpu->iem.s.iEffSeg, GCPtrEffDst, 1);
        } IEM_MC_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode 0x0f 0xa0. */
FNIEMOP_DEF(iemOp_push_fs)
{
    IEMOP_MNEMONIC(push_fs, "push fs");
    IEMOP_HLP_MIN_386();
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    return FNIEMOP_CALL_1(iemOpCommonPushSReg, X86_SREG_FS);
}


/** Opcode 0x0f 0xa1. */
FNIEMOP_DEF(iemOp_pop_fs)
{
    IEMOP_MNEMONIC(pop_fs, "pop fs");
    IEMOP_HLP_MIN_386();
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEMOP_HLP_DEFAULT_64BIT_OP_SIZE();
    IEM_MC_DEFER_TO_CIMPL_2_RET(0,
                                  RT_BIT_64(kIemNativeGstReg_GprFirst + X86_GREG_xSP)
                                | RT_BIT_64(kIemNativeGstReg_SegSelFirst    + X86_SREG_FS)
                                | RT_BIT_64(kIemNativeGstReg_SegBaseFirst   + X86_SREG_FS)
                                | RT_BIT_64(kIemNativeGstReg_SegLimitFirst  + X86_SREG_FS)
                                | RT_BIT_64(kIemNativeGstReg_SegAttribFirst + X86_SREG_FS),
                                iemCImpl_pop_Sreg, X86_SREG_FS, pVCpu->iem.s.enmEffOpSize);
}


/** Opcode 0x0f 0xa2. */
FNIEMOP_DEF(iemOp_cpuid)
{
    IEMOP_MNEMONIC(cpuid, "cpuid");
    IEMOP_HLP_MIN_486(); /* not all 486es. */
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_VMEXIT,
                                  RT_BIT_64(kIemNativeGstReg_GprFirst + X86_GREG_xAX)
                                | RT_BIT_64(kIemNativeGstReg_GprFirst + X86_GREG_xCX)
                                | RT_BIT_64(kIemNativeGstReg_GprFirst + X86_GREG_xDX)
                                | RT_BIT_64(kIemNativeGstReg_GprFirst + X86_GREG_xBX),
                                iemCImpl_cpuid);
}


/**
 * Body for iemOp_bt_Ev_Gv, iemOp_btc_Ev_Gv, iemOp_btr_Ev_Gv and
 * iemOp_bts_Ev_Gv.
 */

#define IEMOP_BODY_BIT_Ev_Gv_RW(a_fnNormalU16, a_fnNormalU32, a_fnNormalU64) \
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm); \
    IEMOP_VERIFICATION_UNDEFINED_EFLAGS(X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF); \
    \
    if (IEM_IS_MODRM_REG_MODE(bRm)) \
    { \
        /* register destination. */ \
        switch (pVCpu->iem.s.enmEffOpSize) \
        { \
            case IEMMODE_16BIT: \
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                \
                IEM_MC_ARG(uint16_t,        u16Src,                 2); \
                IEM_MC_FETCH_GREG_U16(u16Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_AND_LOCAL_U16(u16Src, 0xf); \
                IEM_MC_ARG(uint16_t *,      pu16Dst,                1); \
                IEM_MC_REF_GREG_U16(pu16Dst, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_ARG_EFLAGS(          fEFlagsIn,              0); \
                IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU16, fEFlagsIn, pu16Dst, u16Src); \
                IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            case IEMMODE_32BIT: \
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                \
                IEM_MC_ARG(uint32_t,        u32Src,                 2); \
                IEM_MC_FETCH_GREG_U32(u32Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_AND_LOCAL_U32(u32Src, 0x1f); \
                IEM_MC_ARG(uint32_t *,      pu32Dst,                1); \
                IEM_MC_REF_GREG_U32(pu32Dst, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_ARG_EFLAGS(          fEFlagsIn,              0); \
                IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU32, fEFlagsIn, pu32Dst, u32Src); \
                IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                \
                IEM_MC_CLEAR_HIGH_GREG_U64(IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            case IEMMODE_64BIT: \
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                \
                IEM_MC_ARG(uint64_t,        u64Src,                 2); \
                IEM_MC_FETCH_GREG_U64(u64Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_AND_LOCAL_U64(u64Src, 0x3f); \
                IEM_MC_ARG(uint64_t *,      pu64Dst,                1); \
                IEM_MC_REF_GREG_U64(pu64Dst, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_ARG_EFLAGS(          fEFlagsIn,              0); \
                IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU64, fEFlagsIn, pu64Dst, u64Src); \
                IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            IEM_NOT_REACHED_DEFAULT_CASE_RET(); \
        } \
    } \
    else \
    { \
        /* memory destination. */ \
        /** @todo test negative bit offsets! */ \
        if (!(pVCpu->iem.s.fPrefixes & IEM_OP_PRF_LOCK) || (pVCpu->iem.s.fExec & IEM_F_X86_DISREGARD_LOCK)) \
        { \
            switch (pVCpu->iem.s.enmEffOpSize) \
            { \
                case IEMMODE_16BIT: \
                    IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                    IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0); \
                    IEMOP_HLP_DONE_DECODING(); \
                    \
                    IEM_MC_ARG(uint16_t,                u16Src,                 2); \
                    IEM_MC_FETCH_GREG_U16(u16Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_LOCAL_ASSIGN(int16_t,        i16AddrAdj, /*=*/ u16Src); \
                    IEM_MC_AND_ARG_U16(u16Src, 0x0f); \
                    IEM_MC_SAR_LOCAL_S16(i16AddrAdj, 4); \
                    IEM_MC_SHL_LOCAL_S16(i16AddrAdj, 1); \
                    IEM_MC_ADD_LOCAL_S16_TO_EFF_ADDR(GCPtrEffDst, i16AddrAdj); \
                    \
                    IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                    IEM_MC_ARG(uint16_t *,              pu16Dst,                1); \
                    IEM_MC_MEM_MAP_U16_RW(pu16Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                    \
                    IEM_MC_ARG_EFLAGS(                  fEFlagsIn,              0); \
                    IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU16, fEFlagsIn, pu16Dst, u16Src); \
                    \
                    IEM_MC_MEM_COMMIT_AND_UNMAP_RW(bUnmapInfo); \
                    IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                    IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                    IEM_MC_END(); \
                    break; \
                \
                case IEMMODE_32BIT: \
                    IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                    IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0); \
                    IEMOP_HLP_DONE_DECODING(); \
                    \
                    IEM_MC_ARG(uint32_t,                u32Src,                 2); \
                    IEM_MC_FETCH_GREG_U32(u32Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_LOCAL_ASSIGN(int32_t,        i32AddrAdj, /*=*/ u32Src); \
                    IEM_MC_AND_ARG_U32(u32Src, 0x1f); \
                    IEM_MC_SAR_LOCAL_S32(i32AddrAdj, 5); \
                    IEM_MC_SHL_LOCAL_S32(i32AddrAdj, 2); \
                    IEM_MC_ADD_LOCAL_S32_TO_EFF_ADDR(GCPtrEffDst, i32AddrAdj); \
                    \
                    IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                    IEM_MC_ARG(uint32_t *,              pu32Dst,                1); \
                    IEM_MC_MEM_MAP_U32_RW(pu32Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                    \
                    IEM_MC_ARG_EFLAGS(                  fEFlagsIn,              0); \
                    IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU32, fEFlagsIn, pu32Dst, u32Src); \
                    \
                    IEM_MC_MEM_COMMIT_AND_UNMAP_RW(bUnmapInfo); \
                    IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                    IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                    IEM_MC_END(); \
                    break; \
                \
                case IEMMODE_64BIT: \
                    IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
                    IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0); \
                    IEMOP_HLP_DONE_DECODING(); \
                    \
                    IEM_MC_ARG(uint64_t,                u64Src,                 2); \
                    IEM_MC_FETCH_GREG_U64(u64Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_LOCAL_ASSIGN(int64_t,        i64AddrAdj, /*=*/ u64Src); \
                    IEM_MC_AND_ARG_U64(u64Src, 0x3f); \
                    IEM_MC_SAR_LOCAL_S64(i64AddrAdj, 6); \
                    IEM_MC_SHL_LOCAL_S64(i64AddrAdj, 3); \
                    IEM_MC_ADD_LOCAL_S64_TO_EFF_ADDR(GCPtrEffDst, i64AddrAdj); \
                    \
                    IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                    IEM_MC_ARG(uint64_t *,              pu64Dst,                1); \
                    IEM_MC_MEM_MAP_U64_RW(pu64Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                    \
                    IEM_MC_ARG_EFLAGS(                  fEFlagsIn,              0); \
                    IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU64, fEFlagsIn, pu64Dst, u64Src); \
                    \
                    IEM_MC_MEM_COMMIT_AND_UNMAP_RW(bUnmapInfo); \
                    IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                    IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                    IEM_MC_END(); \
                    break; \
                \
                IEM_NOT_REACHED_DEFAULT_CASE_RET(); \
            } \
        } \
        else \
        { \
            (void)0
/* Separate macro to work around parsing issue in IEMAllInstPython.py */
#define IEMOP_BODY_BIT_Ev_Gv_LOCKED(a_fnLockedU16, a_fnLockedU32, a_fnLockedU64) \
            switch (pVCpu->iem.s.enmEffOpSize) \
            { \
                case IEMMODE_16BIT: \
                    IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                    IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0); \
                    IEMOP_HLP_DONE_DECODING(); \
                    \
                    IEM_MC_ARG(uint16_t,                u16Src,                 2); \
                    IEM_MC_FETCH_GREG_U16(u16Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_LOCAL_ASSIGN(int16_t,        i16AddrAdj, /*=*/ u16Src); \
                    IEM_MC_AND_ARG_U16(u16Src, 0x0f); \
                    IEM_MC_SAR_LOCAL_S16(i16AddrAdj, 4); \
                    IEM_MC_SHL_LOCAL_S16(i16AddrAdj, 1); \
                    IEM_MC_ADD_LOCAL_S16_TO_EFF_ADDR(GCPtrEffDst, i16AddrAdj); \
                    \
                    IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                    IEM_MC_ARG(uint16_t *,              pu16Dst,                1); \
                    IEM_MC_MEM_MAP_U16_ATOMIC(pu16Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                    \
                    IEM_MC_ARG_EFLAGS(                  fEFlagsIn,              0); \
                    IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnLockedU16, fEFlagsIn, pu16Dst, u16Src); \
                    \
                    IEM_MC_MEM_COMMIT_AND_UNMAP_ATOMIC(bUnmapInfo); \
                    IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                    IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                    IEM_MC_END(); \
                    break; \
                \
                case IEMMODE_32BIT: \
                    IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                    IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0); \
                    IEMOP_HLP_DONE_DECODING(); \
                    \
                    IEM_MC_ARG(uint32_t,                u32Src,                 2); \
                    IEM_MC_FETCH_GREG_U32(u32Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_LOCAL_ASSIGN(int32_t,        i32AddrAdj, /*=*/ u32Src); \
                    IEM_MC_AND_ARG_U32(u32Src, 0x1f); \
                    IEM_MC_SAR_LOCAL_S32(i32AddrAdj, 5); \
                    IEM_MC_SHL_LOCAL_S32(i32AddrAdj, 2); \
                    IEM_MC_ADD_LOCAL_S32_TO_EFF_ADDR(GCPtrEffDst, i32AddrAdj); \
                    \
                    IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                    IEM_MC_ARG(uint32_t *,              pu32Dst,                1); \
                    IEM_MC_MEM_MAP_U32_ATOMIC(pu32Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                    \
                    IEM_MC_ARG_EFLAGS(                  fEFlagsIn,              0); \
                    IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnLockedU32, fEFlagsIn, pu32Dst, u32Src); \
                    \
                    IEM_MC_MEM_COMMIT_AND_UNMAP_ATOMIC(bUnmapInfo); \
                    IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                    IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                    IEM_MC_END(); \
                    break; \
                \
                case IEMMODE_64BIT: \
                    IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
                    IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0); \
                    IEMOP_HLP_DONE_DECODING(); \
                    \
                    IEM_MC_ARG(uint64_t,                u64Src,                 2); \
                    IEM_MC_FETCH_GREG_U64(u64Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_LOCAL_ASSIGN(int64_t,        i64AddrAdj, /*=*/ u64Src); \
                    IEM_MC_AND_ARG_U64(u64Src, 0x3f); \
                    IEM_MC_SAR_LOCAL_S64(i64AddrAdj, 6); \
                    IEM_MC_SHL_LOCAL_S64(i64AddrAdj, 3); \
                    IEM_MC_ADD_LOCAL_S64_TO_EFF_ADDR(GCPtrEffDst, i64AddrAdj); \
                    \
                    IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                    IEM_MC_ARG(uint64_t *,              pu64Dst,                1); \
                    IEM_MC_MEM_MAP_U64_ATOMIC(pu64Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                    \
                    IEM_MC_ARG_EFLAGS(                  fEFlagsIn,              0); \
                    IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnLockedU64, fEFlagsIn, pu64Dst, u64Src); \
                    \
                    IEM_MC_MEM_COMMIT_AND_UNMAP_ATOMIC(bUnmapInfo); \
                    IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                    IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                    IEM_MC_END(); \
                    break; \
                \
                IEM_NOT_REACHED_DEFAULT_CASE_RET(); \
            } \
        } \
    } \
    (void)0

/* Read-only version (bt). */
#define IEMOP_BODY_BIT_Ev_Gv_RO(a_fnNormalU16, a_fnNormalU32, a_fnNormalU64) \
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm); \
    IEMOP_VERIFICATION_UNDEFINED_EFLAGS(X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF); \
    \
    if (IEM_IS_MODRM_REG_MODE(bRm)) \
    { \
        /* register destination. */ \
        switch (pVCpu->iem.s.enmEffOpSize) \
        { \
            case IEMMODE_16BIT: \
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                \
                IEM_MC_ARG(uint16_t,            u16Src,     2); \
                IEM_MC_FETCH_GREG_U16(u16Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_AND_LOCAL_U16(u16Src, 0xf); \
                IEM_MC_ARG(uint16_t const *,    pu16Dst,    1); \
                IEM_MC_REF_GREG_U16_CONST(pu16Dst, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_ARG_EFLAGS(              fEFlagsIn,  0); \
                IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU16, fEFlagsIn, pu16Dst, u16Src); \
                IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            case IEMMODE_32BIT: \
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                \
                IEM_MC_ARG(uint32_t,            u32Src,     2); \
                IEM_MC_FETCH_GREG_U32(u32Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_AND_LOCAL_U32(u32Src, 0x1f); \
                IEM_MC_ARG(uint32_t const *,    pu32Dst,    1); \
                IEM_MC_REF_GREG_U32_CONST(pu32Dst, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_ARG_EFLAGS(              fEFlagsIn,  0); \
                IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU32, fEFlagsIn, pu32Dst, u32Src); \
                IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            case IEMMODE_64BIT: \
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                \
                IEM_MC_ARG(uint64_t,            u64Src,     2); \
                IEM_MC_FETCH_GREG_U64(u64Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_AND_LOCAL_U64(u64Src, 0x3f); \
                IEM_MC_ARG(uint64_t const *,    pu64Dst,    1); \
                IEM_MC_REF_GREG_U64_CONST(pu64Dst, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_ARG_EFLAGS(              fEFlagsIn,  0); \
                IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU64, fEFlagsIn, pu64Dst, u64Src); \
                IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            IEM_NOT_REACHED_DEFAULT_CASE_RET(); \
        } \
    } \
    else \
    { \
        /* memory destination. */ \
        /** @todo test negative bit offsets! */ \
        if (!(pVCpu->iem.s.fPrefixes & IEM_OP_PRF_LOCK)) \
        { \
            switch (pVCpu->iem.s.enmEffOpSize) \
            { \
                case IEMMODE_16BIT: \
                    IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                    IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0); \
                    IEMOP_HLP_DONE_DECODING(); \
                    \
                    IEM_MC_ARG(uint16_t,                u16Src,                 2); \
                    IEM_MC_FETCH_GREG_U16(u16Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_LOCAL_ASSIGN(int16_t,        i16AddrAdj, /*=*/ u16Src); \
                    IEM_MC_AND_ARG_U16(u16Src, 0x0f); \
                    IEM_MC_SAR_LOCAL_S16(i16AddrAdj, 4); \
                    IEM_MC_SHL_LOCAL_S16(i16AddrAdj, 1); \
                    IEM_MC_ADD_LOCAL_S16_TO_EFF_ADDR(GCPtrEffDst, i16AddrAdj); \
                    \
                    IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                    IEM_MC_ARG(uint16_t const *,        pu16Dst,                1); \
                    IEM_MC_MEM_MAP_U16_RO(pu16Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                    \
                    IEM_MC_ARG_EFLAGS(                  fEFlagsIn,              0); \
                    IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU16, fEFlagsIn, pu16Dst, u16Src); \
                    \
                    IEM_MC_MEM_COMMIT_AND_UNMAP_RO(bUnmapInfo); \
                    IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                    IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                    IEM_MC_END(); \
                    break; \
                \
                case IEMMODE_32BIT: \
                    IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                    IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0); \
                    IEMOP_HLP_DONE_DECODING(); \
                    \
                    IEM_MC_ARG(uint32_t,                u32Src,                 2); \
                    IEM_MC_FETCH_GREG_U32(u32Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_LOCAL_ASSIGN(int32_t,        i32AddrAdj, /*=*/ u32Src); \
                    IEM_MC_AND_ARG_U32(u32Src, 0x1f); \
                    IEM_MC_SAR_LOCAL_S32(i32AddrAdj, 5); \
                    IEM_MC_SHL_LOCAL_S32(i32AddrAdj, 2); \
                    IEM_MC_ADD_LOCAL_S32_TO_EFF_ADDR(GCPtrEffDst, i32AddrAdj); \
                    \
                    IEM_MC_ARG(uint32_t const *,        pu32Dst,                1); \
                    IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                    IEM_MC_MEM_MAP_U32_RO(pu32Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                    \
                    IEM_MC_ARG_EFLAGS(                  fEFlagsIn,              0); \
                    IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU32, fEFlagsIn, pu32Dst, u32Src); \
                    \
                    IEM_MC_MEM_COMMIT_AND_UNMAP_RO(bUnmapInfo); \
                    IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                    IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                    IEM_MC_END(); \
                    break; \
                \
                case IEMMODE_64BIT: \
                    IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
                    IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);  \
                    IEMOP_HLP_DONE_DECODING(); \
                    \
                    IEM_MC_ARG(uint64_t,                u64Src,                 2); \
                    IEM_MC_FETCH_GREG_U64(u64Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_LOCAL_ASSIGN(int64_t,        i64AddrAdj, /*=*/ u64Src); \
                    IEM_MC_AND_ARG_U64(u64Src, 0x3f); \
                    IEM_MC_SAR_LOCAL_S64(i64AddrAdj, 6); \
                    IEM_MC_SHL_LOCAL_S64(i64AddrAdj, 3); \
                    IEM_MC_ADD_LOCAL_S64_TO_EFF_ADDR(GCPtrEffDst, i64AddrAdj); \
                    \
                    IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                    IEM_MC_ARG(uint64_t const *,        pu64Dst,                1); \
                    IEM_MC_MEM_MAP_U64_RO(pu64Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                    \
                    IEM_MC_ARG_EFLAGS(                  fEFlagsIn,              0); \
                    IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU64, fEFlagsIn, pu64Dst, u64Src); \
                    \
                    IEM_MC_MEM_COMMIT_AND_UNMAP_RO(bUnmapInfo); \
                    IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                    IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                    IEM_MC_END(); \
                    break; \
                \
                IEM_NOT_REACHED_DEFAULT_CASE_RET(); \
            } \
        } \
        else \
        { \
            IEMOP_HLP_DONE_DECODING(); \
            IEMOP_RAISE_INVALID_LOCK_PREFIX_RET(); \
        } \
    } \
    (void)0


/**
 * @opcode      0xa3
 * @oppfx       n/a
 * @opflclass   bitmap
 */
FNIEMOP_DEF(iemOp_bt_Ev_Gv)
{
    IEMOP_MNEMONIC(bt_Ev_Gv, "bt  Ev,Gv");
    IEMOP_HLP_MIN_386();
    IEMOP_BODY_BIT_Ev_Gv_RO(iemAImpl_bt_u16, iemAImpl_bt_u32, iemAImpl_bt_u64);
}


/**
 * Common worker for iemOp_shrd_Ev_Gv_Ib and iemOp_shld_Ev_Gv_Ib.
 */
#define IEMOP_BODY_SHLD_SHR_Ib(a_pImplExpr) \
    PCIEMOPSHIFTDBLSIZES const pImpl = (a_pImplExpr); \
    \
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm); \
    IEMOP_VERIFICATION_UNDEFINED_EFLAGS(X86_EFL_AF | X86_EFL_OF); \
    \
    if (IEM_IS_MODRM_REG_MODE(bRm)) \
    { \
        uint8_t cShift; IEM_OPCODE_GET_NEXT_U8(&cShift); \
        \
        switch (pVCpu->iem.s.enmEffOpSize) \
        { \
            case IEMMODE_16BIT: \
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_ARG(uint16_t *,      pu16Dst,                0); \
                IEM_MC_ARG(uint16_t,        u16Src,                 1); \
                IEM_MC_ARG_CONST(uint8_t,   cShiftArg, /*=*/cShift, 2); \
                IEM_MC_ARG(uint32_t *,      pEFlags,                3); \
                \
                IEM_MC_FETCH_GREG_U16(u16Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_REF_GREG_U16(pu16Dst, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_REF_EFLAGS(pEFlags); \
                IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnNormalU16, pu16Dst, u16Src, cShiftArg, pEFlags); \
                \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
                \
            case IEMMODE_32BIT: \
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_ARG(uint32_t *,      pu32Dst,                0); \
                IEM_MC_ARG(uint32_t,        u32Src,                 1); \
                IEM_MC_ARG_CONST(uint8_t,   cShiftArg, /*=*/cShift, 2); \
                IEM_MC_ARG(uint32_t *,      pEFlags,                3); \
                \
                IEM_MC_FETCH_GREG_U32(u32Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_REF_GREG_U32(pu32Dst, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_REF_EFLAGS(pEFlags); \
                IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnNormalU32, pu32Dst, u32Src, cShiftArg, pEFlags); \
                \
                IEM_MC_CLEAR_HIGH_GREG_U64(IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
                \
            case IEMMODE_64BIT: \
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_ARG(uint64_t *,      pu64Dst,                0); \
                IEM_MC_ARG(uint64_t,        u64Src,                 1); \
                IEM_MC_ARG_CONST(uint8_t,   cShiftArg, /*=*/cShift, 2); \
                IEM_MC_ARG(uint32_t *,      pEFlags,                3); \
                \
                IEM_MC_FETCH_GREG_U64(u64Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_REF_GREG_U64(pu64Dst, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_REF_EFLAGS(pEFlags); \
                IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnNormalU64, pu64Dst, u64Src, cShiftArg, pEFlags); \
                \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
                \
            IEM_NOT_REACHED_DEFAULT_CASE_RET(); \
        } \
    } \
    else \
    { \
        switch (pVCpu->iem.s.enmEffOpSize) \
        { \
            case IEMMODE_16BIT: \
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 1); \
                \
                uint8_t cShift; IEM_OPCODE_GET_NEXT_U8(&cShift); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                \
                IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                IEM_MC_ARG(uint16_t *,              pu16Dst,                0); \
                IEM_MC_MEM_MAP_U16_RW(pu16Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                \
                IEM_MC_ARG(uint16_t,                u16Src,                 1); \
                IEM_MC_FETCH_GREG_U16(u16Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_ARG_CONST(uint8_t,           cShiftArg,/*=*/ cShift, 2); \
                IEM_MC_ARG_LOCAL_EFLAGS(            pEFlags, EFlags,        3); \
                IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnNormalU16, pu16Dst, u16Src, cShiftArg, pEFlags); \
                \
                IEM_MC_MEM_COMMIT_AND_UNMAP_RW(bUnmapInfo); \
                IEM_MC_COMMIT_EFLAGS(EFlags); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
                \
            case IEMMODE_32BIT: \
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 1); \
                \
                uint8_t cShift; IEM_OPCODE_GET_NEXT_U8(&cShift); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                \
                IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                IEM_MC_ARG(uint32_t *,              pu32Dst,                0); \
                IEM_MC_MEM_MAP_U32_RW(pu32Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                \
                IEM_MC_ARG(uint32_t,                u32Src,                 1); \
                IEM_MC_FETCH_GREG_U32(u32Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_ARG_CONST(uint8_t,           cShiftArg,/*=*/ cShift, 2); \
                IEM_MC_ARG_LOCAL_EFLAGS(            pEFlags, EFlags,        3); \
                IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnNormalU32, pu32Dst, u32Src, cShiftArg, pEFlags); \
                \
                IEM_MC_MEM_COMMIT_AND_UNMAP_RW(bUnmapInfo); \
                IEM_MC_COMMIT_EFLAGS(EFlags); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
                \
            case IEMMODE_64BIT: \
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
                IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 1); \
                \
                uint8_t cShift; IEM_OPCODE_GET_NEXT_U8(&cShift); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                \
                IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                IEM_MC_ARG(uint64_t *,              pu64Dst,                0); \
                IEM_MC_MEM_MAP_U64_RW(pu64Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                \
                IEM_MC_ARG(uint64_t,                u64Src,                 1); \
                IEM_MC_FETCH_GREG_U64(u64Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_ARG_CONST(uint8_t,           cShiftArg,/*=*/ cShift, 2); \
                IEM_MC_ARG_LOCAL_EFLAGS(            pEFlags, EFlags,        3); \
                \
                IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnNormalU64, pu64Dst, u64Src, cShiftArg, pEFlags); \
                \
                IEM_MC_MEM_COMMIT_AND_UNMAP_RW(bUnmapInfo); \
                IEM_MC_COMMIT_EFLAGS(EFlags); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
                \
            IEM_NOT_REACHED_DEFAULT_CASE_RET(); \
        } \
    } (void)0


/**
 * Common worker for iemOp_shrd_Ev_Gv_CL and iemOp_shld_Ev_Gv_CL.
 */
#define IEMOP_BODY_SHLD_SHRD_Ev_Gv_CL(a_pImplExpr) \
    PCIEMOPSHIFTDBLSIZES const pImpl = (a_pImplExpr); \
    \
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm); \
    IEMOP_VERIFICATION_UNDEFINED_EFLAGS(X86_EFL_AF | X86_EFL_OF); \
    \
    if (IEM_IS_MODRM_REG_MODE(bRm)) \
    { \
        switch (pVCpu->iem.s.enmEffOpSize) \
        { \
            case IEMMODE_16BIT: \
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_ARG(uint16_t *,      pu16Dst,                0); \
                IEM_MC_ARG(uint16_t,        u16Src,                 1); \
                IEM_MC_ARG(uint8_t,         cShiftArg,              2); \
                IEM_MC_ARG(uint32_t *,      pEFlags,                3); \
                \
                IEM_MC_FETCH_GREG_U16(u16Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_FETCH_GREG_U8(cShiftArg, X86_GREG_xCX); \
                IEM_MC_REF_GREG_U16(pu16Dst, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_REF_EFLAGS(pEFlags); \
                IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnNormalU16, pu16Dst, u16Src, cShiftArg, pEFlags); \
                \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
                \
            case IEMMODE_32BIT: \
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_ARG(uint32_t *,      pu32Dst,                0); \
                IEM_MC_ARG(uint32_t,        u32Src,                 1); \
                IEM_MC_ARG(uint8_t,         cShiftArg,              2); \
                IEM_MC_ARG(uint32_t *,      pEFlags,                3); \
                \
                IEM_MC_FETCH_GREG_U32(u32Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_FETCH_GREG_U8(cShiftArg, X86_GREG_xCX); \
                IEM_MC_REF_GREG_U32(pu32Dst, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_REF_EFLAGS(pEFlags); \
                IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnNormalU32, pu32Dst, u32Src, cShiftArg, pEFlags); \
                \
                IEM_MC_CLEAR_HIGH_GREG_U64(IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
                \
            case IEMMODE_64BIT: \
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_ARG(uint64_t *,      pu64Dst,                0); \
                IEM_MC_ARG(uint64_t,        u64Src,                 1); \
                IEM_MC_ARG(uint8_t,         cShiftArg,              2); \
                IEM_MC_ARG(uint32_t *,      pEFlags,                3); \
                \
                IEM_MC_FETCH_GREG_U64(u64Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_FETCH_GREG_U8(cShiftArg, X86_GREG_xCX); \
                IEM_MC_REF_GREG_U64(pu64Dst, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_REF_EFLAGS(pEFlags); \
                IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnNormalU64, pu64Dst, u64Src, cShiftArg, pEFlags); \
                \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
                \
            IEM_NOT_REACHED_DEFAULT_CASE_RET(); \
        } \
    } \
    else \
    { \
        switch (pVCpu->iem.s.enmEffOpSize) \
        { \
            case IEMMODE_16BIT: \
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                IEM_MC_ARG(uint16_t *,              pu16Dst,                0); \
                IEM_MC_ARG(uint16_t,                u16Src,                 1); \
                IEM_MC_ARG(uint8_t,                 cShiftArg,              2); \
                IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                \
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_FETCH_GREG_U16(u16Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_FETCH_GREG_U8(cShiftArg, X86_GREG_xCX); \
                IEM_MC_ARG_LOCAL_EFLAGS(            pEFlags, EFlags,        3); \
                IEM_MC_MEM_MAP_U16_RW(pu16Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnNormalU16, pu16Dst, u16Src, cShiftArg, pEFlags); \
                \
                IEM_MC_MEM_COMMIT_AND_UNMAP_RW(bUnmapInfo); \
                IEM_MC_COMMIT_EFLAGS(EFlags); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
                \
            case IEMMODE_32BIT: \
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                IEM_MC_ARG(uint32_t *,              pu32Dst,                0); \
                IEM_MC_ARG(uint32_t,                u32Src,                 1); \
                IEM_MC_ARG(uint8_t,                 cShiftArg,              2); \
                IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                \
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_FETCH_GREG_U32(u32Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_FETCH_GREG_U8(cShiftArg, X86_GREG_xCX); \
                IEM_MC_ARG_LOCAL_EFLAGS(            pEFlags, EFlags,        3); \
                IEM_MC_MEM_MAP_U32_RW(pu32Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnNormalU32, pu32Dst, u32Src, cShiftArg, pEFlags); \
                \
                IEM_MC_MEM_COMMIT_AND_UNMAP_RW(bUnmapInfo); \
                IEM_MC_COMMIT_EFLAGS(EFlags); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
                \
            case IEMMODE_64BIT: \
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
                IEM_MC_ARG(uint64_t *,              pu64Dst,                0); \
                IEM_MC_ARG(uint64_t,                u64Src,                 1); \
                IEM_MC_ARG(uint8_t,                 cShiftArg,              2); \
                IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                \
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_FETCH_GREG_U64(u64Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_FETCH_GREG_U8(cShiftArg, X86_GREG_xCX); \
                IEM_MC_ARG_LOCAL_EFLAGS(            pEFlags, EFlags,        3); \
                IEM_MC_MEM_MAP_U64_RW(pu64Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnNormalU64, pu64Dst, u64Src, cShiftArg, pEFlags); \
                \
                IEM_MC_MEM_COMMIT_AND_UNMAP_RW(bUnmapInfo); \
                IEM_MC_COMMIT_EFLAGS(EFlags); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
                \
            IEM_NOT_REACHED_DEFAULT_CASE_RET(); \
        } \
    } (void)0


/**
 * @opcode      0xa4
 * @opflclass   shift_count
 */
FNIEMOP_DEF(iemOp_shld_Ev_Gv_Ib)
{
    IEMOP_MNEMONIC(shld_Ev_Gv_Ib, "shld Ev,Gv,Ib");
    IEMOP_HLP_MIN_386();
    IEMOP_BODY_SHLD_SHR_Ib(IEMTARGETCPU_EFL_BEHAVIOR_SELECT(g_iemAImpl_shld_eflags));
}


/**
 * @opcode      0xa5
 * @opflclass   shift_count
 */
FNIEMOP_DEF(iemOp_shld_Ev_Gv_CL)
{
    IEMOP_MNEMONIC(shld_Ev_Gv_CL, "shld Ev,Gv,CL");
    IEMOP_HLP_MIN_386();
    IEMOP_BODY_SHLD_SHRD_Ev_Gv_CL(IEMTARGETCPU_EFL_BEHAVIOR_SELECT(g_iemAImpl_shld_eflags));
}


/** Opcode 0x0f 0xa8. */
FNIEMOP_DEF(iemOp_push_gs)
{
    IEMOP_MNEMONIC(push_gs, "push gs");
    IEMOP_HLP_MIN_386();
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    return FNIEMOP_CALL_1(iemOpCommonPushSReg, X86_SREG_GS);
}


/** Opcode 0x0f 0xa9. */
FNIEMOP_DEF(iemOp_pop_gs)
{
    IEMOP_MNEMONIC(pop_gs, "pop gs");
    IEMOP_HLP_MIN_386();
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEMOP_HLP_DEFAULT_64BIT_OP_SIZE();
    IEM_MC_DEFER_TO_CIMPL_2_RET(0,
                                  RT_BIT_64(kIemNativeGstReg_GprFirst + X86_GREG_xSP)
                                | RT_BIT_64(kIemNativeGstReg_SegSelFirst    + X86_SREG_GS)
                                | RT_BIT_64(kIemNativeGstReg_SegBaseFirst   + X86_SREG_GS)
                                | RT_BIT_64(kIemNativeGstReg_SegLimitFirst  + X86_SREG_GS)
                                | RT_BIT_64(kIemNativeGstReg_SegAttribFirst + X86_SREG_GS),
                                iemCImpl_pop_Sreg, X86_SREG_GS, pVCpu->iem.s.enmEffOpSize);
}


/** Opcode 0x0f 0xaa. */
FNIEMOP_DEF(iemOp_rsm)
{
    IEMOP_MNEMONIC0(FIXED, RSM, rsm, DISOPTYPE_HARMLESS, 0);
    IEMOP_HLP_MIN_386(); /* 386SL and later. */
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_DEFER_TO_CIMPL_0_RET(IEM_CIMPL_F_BRANCH_INDIRECT | IEM_CIMPL_F_BRANCH_FAR | IEM_CIMPL_F_BRANCH_STACK_FAR
                                | IEM_CIMPL_F_MODE | IEM_CIMPL_F_RFLAGS | IEM_CIMPL_F_VMEXIT | IEM_CIMPL_F_END_TB, 0,
                                iemCImpl_rsm);
}



/**
 * @opcode      0xab
 * @oppfx       n/a
 * @opflclass   bitmap
 */
FNIEMOP_DEF(iemOp_bts_Ev_Gv)
{
    IEMOP_MNEMONIC(bts_Ev_Gv, "bts Ev,Gv");
    IEMOP_HLP_MIN_386();
    IEMOP_BODY_BIT_Ev_Gv_RW(    iemAImpl_bts_u16,        iemAImpl_bts_u32,        iemAImpl_bts_u64);
    IEMOP_BODY_BIT_Ev_Gv_LOCKED(iemAImpl_bts_u16_locked, iemAImpl_bts_u32_locked, iemAImpl_bts_u64_locked);
}


/**
 * @opcode      0xac
 * @opflclass   shift_count
 */
FNIEMOP_DEF(iemOp_shrd_Ev_Gv_Ib)
{
    IEMOP_MNEMONIC(shrd_Ev_Gv_Ib, "shrd Ev,Gv,Ib");
    IEMOP_HLP_MIN_386();
    IEMOP_BODY_SHLD_SHR_Ib(IEMTARGETCPU_EFL_BEHAVIOR_SELECT(g_iemAImpl_shrd_eflags));
}


/**
 * @opcode      0xad
 * @opflclass   shift_count
 */
FNIEMOP_DEF(iemOp_shrd_Ev_Gv_CL)
{
    IEMOP_MNEMONIC(shrd_Ev_Gv_CL, "shrd Ev,Gv,CL");
    IEMOP_HLP_MIN_386();
    IEMOP_BODY_SHLD_SHRD_Ev_Gv_CL(IEMTARGETCPU_EFL_BEHAVIOR_SELECT(g_iemAImpl_shrd_eflags));
}


/** Opcode 0x0f 0xae mem/0. */
FNIEMOP_DEF_1(iemOp_Grp15_fxsave,   uint8_t, bRm)
{
    IEMOP_MNEMONIC(fxsave, "fxsave m512");
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fFxSaveRstor)
        IEMOP_RAISE_INVALID_OPCODE_RET();

    IEM_MC_BEGIN(IEM_MC_F_MIN_PENTIUM_II, 0);
    IEM_MC_ARG(RTGCPTR,         GCPtrEff,                                       1);
    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEff, bRm, 0);
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_ACTUALIZE_FPU_STATE_FOR_READ();
    IEM_MC_ARG_CONST(uint8_t,   iEffSeg,      /*=*/ pVCpu->iem.s.iEffSeg,       0);
    IEM_MC_ARG_CONST(IEMMODE,   enmEffOpSize, /*=*/pVCpu->iem.s.enmEffOpSize,   2);
    IEM_MC_CALL_CIMPL_3(IEM_CIMPL_F_FPU, 0, iemCImpl_fxsave, iEffSeg, GCPtrEff, enmEffOpSize);
    IEM_MC_END();
}


/** Opcode 0x0f 0xae mem/1. */
FNIEMOP_DEF_1(iemOp_Grp15_fxrstor,  uint8_t, bRm)
{
    IEMOP_MNEMONIC(fxrstor, "fxrstor m512");
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fFxSaveRstor)
        IEMOP_RAISE_INVALID_OPCODE_RET();

    IEM_MC_BEGIN(IEM_MC_F_MIN_PENTIUM_II, 0);
    IEM_MC_ARG(RTGCPTR,         GCPtrEff,                                       1);
    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEff, bRm, 0);
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_ACTUALIZE_FPU_STATE_FOR_CHANGE();
    IEM_MC_ARG_CONST(uint8_t,   iEffSeg,      /*=*/ pVCpu->iem.s.iEffSeg,       0);
    IEM_MC_ARG_CONST(IEMMODE,   enmEffOpSize, /*=*/pVCpu->iem.s.enmEffOpSize,   2);
    IEM_MC_CALL_CIMPL_3(IEM_CIMPL_F_FPU, RT_BIT_64(kIemNativeGstReg_FpuFcw) | RT_BIT_64(kIemNativeGstReg_FpuFsw) | RT_BIT_64(kIemNativeGstReg_MxCsr),
                        iemCImpl_fxrstor, iEffSeg, GCPtrEff, enmEffOpSize);
    IEM_MC_END();
}


/**
 * @opmaps      grp15
 * @opcode      !11/2
 * @oppfx       none
 * @opcpuid     sse
 * @opgroup     og_sse_mxcsrsm
 * @opxcpttype  5
 * @optest      op1=0      -> mxcsr=0
 * @optest      op1=0x2083 -> mxcsr=0x2083
 * @optest      op1=0xfffffffe -> value.xcpt=0xd
 * @optest      op1=0x2083 cr0|=ts -> value.xcpt=0x7
 * @optest      op1=0x2083 cr0|=em -> value.xcpt=0x6
 * @optest      op1=0x2083 cr0|=mp -> mxcsr=0x2083
 * @optest      op1=0x2083 cr4&~=osfxsr -> value.xcpt=0x6
 * @optest      op1=0x2083 cr0|=ts,em -> value.xcpt=0x6
 * @optest      op1=0x2083 cr0|=em cr4&~=osfxsr -> value.xcpt=0x6
 * @optest      op1=0x2083 cr0|=ts,em cr4&~=osfxsr -> value.xcpt=0x6
 * @optest      op1=0x2083 cr0|=ts,em,mp cr4&~=osfxsr -> value.xcpt=0x6
 */
FNIEMOP_DEF_1(iemOp_Grp15_ldmxcsr, uint8_t, bRm)
{
    IEMOP_MNEMONIC1(M_MEM, LDMXCSR, ldmxcsr, Md_RO, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fSse)
        IEMOP_RAISE_INVALID_OPCODE_RET();

    IEM_MC_BEGIN(IEM_MC_F_MIN_PENTIUM_II, 0);
    IEM_MC_ARG(RTGCPTR,         GCPtrEff,                                1);
    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEff, bRm, 0);
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_ACTUALIZE_SSE_STATE_FOR_READ();
    IEM_MC_ARG_CONST(uint8_t,   iEffSeg, /*=*/ pVCpu->iem.s.iEffSeg,     0);
    IEM_MC_CALL_CIMPL_2(IEM_CIMPL_F_FPU, RT_BIT_64(kIemNativeGstReg_MxCsr), iemCImpl_ldmxcsr, iEffSeg, GCPtrEff);
    IEM_MC_END();
}


/**
 * @opmaps      grp15
 * @opcode      !11/3
 * @oppfx       none
 * @opcpuid     sse
 * @opgroup     og_sse_mxcsrsm
 * @opxcpttype  5
 * @optest      mxcsr=0      -> op1=0
 * @optest      mxcsr=0x2083 -> op1=0x2083
 * @optest      mxcsr=0x2084 cr0|=ts -> value.xcpt=0x7
 * @optest      mxcsr=0x2085 cr0|=em -> value.xcpt=0x6
 * @optest      mxcsr=0x2086 cr0|=mp -> op1=0x2086
 * @optest      mxcsr=0x2087 cr4&~=osfxsr -> value.xcpt=0x6
 * @optest      mxcsr=0x2088 cr0|=ts,em -> value.xcpt=0x6
 * @optest      mxcsr=0x2089 cr0|=em cr4&~=osfxsr -> value.xcpt=0x6
 * @optest      mxcsr=0x208a cr0|=ts,em cr4&~=osfxsr -> value.xcpt=0x6
 * @optest      mxcsr=0x208b cr0|=ts,em,mp cr4&~=osfxsr -> value.xcpt=0x6
 */
FNIEMOP_DEF_1(iemOp_Grp15_stmxcsr,  uint8_t, bRm)
{
    IEMOP_MNEMONIC1(M_MEM, STMXCSR, stmxcsr, Md_WO, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fSse)
        IEMOP_RAISE_INVALID_OPCODE_RET();

    IEM_MC_BEGIN(IEM_MC_F_MIN_PENTIUM_II, 0);
    IEM_MC_ARG(RTGCPTR,         GCPtrEff,                                1);
    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEff, bRm, 0);
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_ACTUALIZE_SSE_STATE_FOR_READ();
    IEM_MC_ARG_CONST(uint8_t,   iEffSeg, /*=*/ pVCpu->iem.s.iEffSeg,     0);
    IEM_MC_CALL_CIMPL_2(IEM_CIMPL_F_FPU, 0, iemCImpl_stmxcsr, iEffSeg, GCPtrEff);
    IEM_MC_END();
}


/**
 * @opmaps      grp15
 * @opcode      !11/4
 * @oppfx       none
 * @opcpuid     xsave
 * @opgroup     og_system
 * @opxcpttype  none
 */
FNIEMOP_DEF_1(iemOp_Grp15_xsave,    uint8_t, bRm)
{
    IEMOP_MNEMONIC1(M_MEM, XSAVE, xsave, M_RW, DISOPTYPE_HARMLESS, 0);
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fXSaveRstor)
        IEMOP_RAISE_INVALID_OPCODE_RET();

    IEM_MC_BEGIN(IEM_MC_F_MIN_CORE, 0);
    IEM_MC_ARG(RTGCPTR,         GCPtrEff,                                       1);
    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEff, bRm, 0);
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_ACTUALIZE_FPU_STATE_FOR_READ();
    IEM_MC_ARG_CONST(uint8_t,   iEffSeg,      /*=*/ pVCpu->iem.s.iEffSeg,       0);
    IEM_MC_ARG_CONST(IEMMODE,   enmEffOpSize, /*=*/ pVCpu->iem.s.enmEffOpSize,  2);
    IEM_MC_CALL_CIMPL_3(IEM_CIMPL_F_FPU, 0, iemCImpl_xsave, iEffSeg, GCPtrEff, enmEffOpSize);
    IEM_MC_END();
}


/**
 * @opmaps      grp15
 * @opcode      !11/5
 * @oppfx       none
 * @opcpuid     xsave
 * @opgroup     og_system
 * @opxcpttype  none
 */
FNIEMOP_DEF_1(iemOp_Grp15_xrstor,   uint8_t, bRm)
{
    IEMOP_MNEMONIC1(M_MEM, XRSTOR, xrstor, M_RO, DISOPTYPE_HARMLESS, 0);
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fXSaveRstor)
        IEMOP_RAISE_INVALID_OPCODE_RET();

    IEM_MC_BEGIN(IEM_MC_F_MIN_CORE, 0);
    IEM_MC_ARG(RTGCPTR,         GCPtrEff,                                       1);
    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEff, bRm, 0);
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_ACTUALIZE_FPU_STATE_FOR_READ();
    IEM_MC_ARG_CONST(uint8_t,   iEffSeg,      /*=*/ pVCpu->iem.s.iEffSeg,       0);
    IEM_MC_ARG_CONST(IEMMODE,   enmEffOpSize, /*=*/ pVCpu->iem.s.enmEffOpSize,  2);
    IEM_MC_CALL_CIMPL_3(IEM_CIMPL_F_FPU, RT_BIT_64(kIemNativeGstReg_FpuFcw) | RT_BIT_64(kIemNativeGstReg_FpuFsw) | RT_BIT_64(kIemNativeGstReg_MxCsr),
                        iemCImpl_xrstor, iEffSeg, GCPtrEff, enmEffOpSize);
    IEM_MC_END();
}

/** Opcode 0x0f 0xae mem/6. */
FNIEMOP_STUB_1(iemOp_Grp15_xsaveopt, uint8_t, bRm);

/**
 * @opmaps      grp15
 * @opcode      !11/7
 * @oppfx       none
 * @opcpuid     clfsh
 * @opgroup     og_cachectl
 * @optest      op1=1 ->
 */
FNIEMOP_DEF_1(iemOp_Grp15_clflush,  uint8_t, bRm)
{
    IEMOP_MNEMONIC1(M_MEM, CLFLUSH, clflush, Mb_RO, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fClFlush)
        return FNIEMOP_CALL_1(iemOp_InvalidWithRMAllNeeded, bRm);

    IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
    IEM_MC_ARG(RTGCPTR,         GCPtrEff,                                1);
    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEff, bRm, 0);
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_ARG_CONST(uint8_t,   iEffSeg, /*=*/ pVCpu->iem.s.iEffSeg,     0);
    IEM_MC_CALL_CIMPL_2(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_clflush_clflushopt, iEffSeg, GCPtrEff);
    IEM_MC_END();
}

/**
 * @opmaps      grp15
 * @opcode      !11/7
 * @oppfx       0x66
 * @opcpuid     clflushopt
 * @opgroup     og_cachectl
 * @optest      op1=1 ->
 */
FNIEMOP_DEF_1(iemOp_Grp15_clflushopt,  uint8_t, bRm)
{
    IEMOP_MNEMONIC1(M_MEM, CLFLUSHOPT, clflushopt, Mb_RO, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fClFlushOpt)
        return FNIEMOP_CALL_1(iemOp_InvalidWithRMAllNeeded, bRm);

    IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
    IEM_MC_ARG(RTGCPTR,         GCPtrEff,                                1);
    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEff, bRm, 0);
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEM_MC_ARG_CONST(uint8_t,   iEffSeg, /*=*/ pVCpu->iem.s.iEffSeg,     0);
    IEM_MC_CALL_CIMPL_2(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_clflush_clflushopt, iEffSeg, GCPtrEff);
    IEM_MC_END();
}


/** Opcode 0x0f 0xae 11b/5. */
FNIEMOP_DEF_1(iemOp_Grp15_lfence,   uint8_t, bRm)
{
    RT_NOREF_PV(bRm);
    IEMOP_MNEMONIC(lfence, "lfence");
    IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
#ifdef RT_ARCH_ARM64
    IEM_MC_CALL_VOID_AIMPL_0(iemAImpl_lfence);
#else
    if (IEM_GET_HOST_CPU_FEATURES(pVCpu)->fSse2)
        IEM_MC_CALL_VOID_AIMPL_0(iemAImpl_lfence);
    else
        IEM_MC_CALL_VOID_AIMPL_0(iemAImpl_alt_mem_fence);
#endif
    IEM_MC_ADVANCE_RIP_AND_FINISH();
    IEM_MC_END();
}


/** Opcode 0x0f 0xae 11b/6. */
FNIEMOP_DEF_1(iemOp_Grp15_mfence,   uint8_t, bRm)
{
    RT_NOREF_PV(bRm);
    IEMOP_MNEMONIC(mfence, "mfence");
    IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
#ifdef RT_ARCH_ARM64
    IEM_MC_CALL_VOID_AIMPL_0(iemAImpl_mfence);
#else
    if (IEM_GET_HOST_CPU_FEATURES(pVCpu)->fSse2)
        IEM_MC_CALL_VOID_AIMPL_0(iemAImpl_mfence);
    else
        IEM_MC_CALL_VOID_AIMPL_0(iemAImpl_alt_mem_fence);
#endif
    IEM_MC_ADVANCE_RIP_AND_FINISH();
    IEM_MC_END();
}


/** Opcode 0x0f 0xae 11b/7. */
FNIEMOP_DEF_1(iemOp_Grp15_sfence,   uint8_t, bRm)
{
    RT_NOREF_PV(bRm);
    IEMOP_MNEMONIC(sfence, "sfence");
    IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
#ifdef RT_ARCH_ARM64
    IEM_MC_CALL_VOID_AIMPL_0(iemAImpl_sfence);
#else
    if (IEM_GET_HOST_CPU_FEATURES(pVCpu)->fSse2)
        IEM_MC_CALL_VOID_AIMPL_0(iemAImpl_sfence);
    else
        IEM_MC_CALL_VOID_AIMPL_0(iemAImpl_alt_mem_fence);
#endif
    IEM_MC_ADVANCE_RIP_AND_FINISH();
    IEM_MC_END();
}


/** Opcode 0xf3 0x0f 0xae 11b/0. */
FNIEMOP_DEF_1(iemOp_Grp15_rdfsbase, uint8_t, bRm)
{
    IEMOP_MNEMONIC(rdfsbase, "rdfsbase Ry");
    if (pVCpu->iem.s.enmEffOpSize == IEMMODE_64BIT)
    {
        IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fFsGsBase);
        IEM_MC_MAYBE_RAISE_FSGSBASE_XCPT();
        IEM_MC_LOCAL(uint64_t, u64Dst);
        IEM_MC_FETCH_SREG_BASE_U64(u64Dst, X86_SREG_FS);
        IEM_MC_STORE_GREG_U64(IEM_GET_MODRM_RM(pVCpu, bRm), u64Dst);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fFsGsBase);
        IEM_MC_MAYBE_RAISE_FSGSBASE_XCPT();
        IEM_MC_LOCAL(uint32_t, u32Dst);
        IEM_MC_FETCH_SREG_BASE_U32(u32Dst, X86_SREG_FS);
        IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_RM(pVCpu, bRm), u32Dst);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode 0xf3 0x0f 0xae 11b/1. */
FNIEMOP_DEF_1(iemOp_Grp15_rdgsbase, uint8_t, bRm)
{
    IEMOP_MNEMONIC(rdgsbase, "rdgsbase Ry");
    if (pVCpu->iem.s.enmEffOpSize == IEMMODE_64BIT)
    {
        IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fFsGsBase);
        IEM_MC_MAYBE_RAISE_FSGSBASE_XCPT();
        IEM_MC_LOCAL(uint64_t, u64Dst);
        IEM_MC_FETCH_SREG_BASE_U64(u64Dst, X86_SREG_GS);
        IEM_MC_STORE_GREG_U64(IEM_GET_MODRM_RM(pVCpu, bRm), u64Dst);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fFsGsBase);
        IEM_MC_MAYBE_RAISE_FSGSBASE_XCPT();
        IEM_MC_LOCAL(uint32_t, u32Dst);
        IEM_MC_FETCH_SREG_BASE_U32(u32Dst, X86_SREG_GS);
        IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_RM(pVCpu, bRm), u32Dst);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode 0xf3 0x0f 0xae 11b/2. */
FNIEMOP_DEF_1(iemOp_Grp15_wrfsbase, uint8_t, bRm)
{
    IEMOP_MNEMONIC(wrfsbase, "wrfsbase Ry");
    if (pVCpu->iem.s.enmEffOpSize == IEMMODE_64BIT)
    {
        IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fFsGsBase);
        IEM_MC_MAYBE_RAISE_FSGSBASE_XCPT();
        IEM_MC_LOCAL(uint64_t, u64Dst);
        IEM_MC_FETCH_GREG_U64(u64Dst, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_MAYBE_RAISE_NON_CANONICAL_ADDR_GP0(u64Dst);
        IEM_MC_STORE_SREG_BASE_U64(X86_SREG_FS, u64Dst);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fFsGsBase);
        IEM_MC_MAYBE_RAISE_FSGSBASE_XCPT();
        IEM_MC_LOCAL(uint32_t, u32Dst);
        IEM_MC_FETCH_GREG_U32(u32Dst, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_STORE_SREG_BASE_U64(X86_SREG_FS, u32Dst);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode 0xf3 0x0f 0xae 11b/3. */
FNIEMOP_DEF_1(iemOp_Grp15_wrgsbase, uint8_t, bRm)
{
    IEMOP_MNEMONIC(wrgsbase, "wrgsbase Ry");
    if (pVCpu->iem.s.enmEffOpSize == IEMMODE_64BIT)
    {
        IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fFsGsBase);
        IEM_MC_MAYBE_RAISE_FSGSBASE_XCPT();
        IEM_MC_LOCAL(uint64_t, u64Dst);
        IEM_MC_FETCH_GREG_U64(u64Dst, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_MAYBE_RAISE_NON_CANONICAL_ADDR_GP0(u64Dst);
        IEM_MC_STORE_SREG_BASE_U64(X86_SREG_GS, u64Dst);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fFsGsBase);
        IEM_MC_MAYBE_RAISE_FSGSBASE_XCPT();
        IEM_MC_LOCAL(uint32_t, u32Dst);
        IEM_MC_FETCH_GREG_U32(u32Dst, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_STORE_SREG_BASE_U64(X86_SREG_GS, u32Dst);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * Group 15 jump table for register variant.
 */
IEM_STATIC const PFNIEMOPRM g_apfnGroup15RegReg[] =
{   /* pfx:  none,                          066h,                           0f3h,                           0f2h */
    /* /0 */ iemOp_InvalidWithRM,           iemOp_InvalidWithRM,            iemOp_Grp15_rdfsbase,           iemOp_InvalidWithRM,
    /* /1 */ iemOp_InvalidWithRM,           iemOp_InvalidWithRM,            iemOp_Grp15_rdgsbase,           iemOp_InvalidWithRM,
    /* /2 */ iemOp_InvalidWithRM,           iemOp_InvalidWithRM,            iemOp_Grp15_wrfsbase,           iemOp_InvalidWithRM,
    /* /3 */ iemOp_InvalidWithRM,           iemOp_InvalidWithRM,            iemOp_Grp15_wrgsbase,           iemOp_InvalidWithRM,
    /* /4 */ IEMOP_X4(iemOp_InvalidWithRM),
    /* /5 */ iemOp_Grp15_lfence,            iemOp_InvalidWithRM,            iemOp_InvalidWithRM,            iemOp_InvalidWithRM,
    /* /6 */ iemOp_Grp15_mfence,            iemOp_InvalidWithRM,            iemOp_InvalidWithRM,            iemOp_InvalidWithRM,
    /* /7 */ iemOp_Grp15_sfence,            iemOp_InvalidWithRM,            iemOp_InvalidWithRM,            iemOp_InvalidWithRM,
};
AssertCompile(RT_ELEMENTS(g_apfnGroup15RegReg) == 8*4);


/**
 * Group 15 jump table for memory variant.
 */
IEM_STATIC const PFNIEMOPRM g_apfnGroup15MemReg[] =
{   /* pfx:  none,                          066h,                           0f3h,                           0f2h */
    /* /0 */ iemOp_Grp15_fxsave,            iemOp_InvalidWithRM,            iemOp_InvalidWithRM,            iemOp_InvalidWithRM,
    /* /1 */ iemOp_Grp15_fxrstor,           iemOp_InvalidWithRM,            iemOp_InvalidWithRM,            iemOp_InvalidWithRM,
    /* /2 */ iemOp_Grp15_ldmxcsr,           iemOp_InvalidWithRM,            iemOp_InvalidWithRM,            iemOp_InvalidWithRM,
    /* /3 */ iemOp_Grp15_stmxcsr,           iemOp_InvalidWithRM,            iemOp_InvalidWithRM,            iemOp_InvalidWithRM,
    /* /4 */ iemOp_Grp15_xsave,             iemOp_InvalidWithRM,            iemOp_InvalidWithRM,            iemOp_InvalidWithRM,
    /* /5 */ iemOp_Grp15_xrstor,            iemOp_InvalidWithRM,            iemOp_InvalidWithRM,            iemOp_InvalidWithRM,
    /* /6 */ iemOp_Grp15_xsaveopt,          iemOp_InvalidWithRM,            iemOp_InvalidWithRM,            iemOp_InvalidWithRM,
    /* /7 */ iemOp_Grp15_clflush,           iemOp_Grp15_clflushopt,         iemOp_InvalidWithRM,            iemOp_InvalidWithRM,
};
AssertCompile(RT_ELEMENTS(g_apfnGroup15MemReg) == 8*4);


/** Opcode 0x0f 0xae. */
FNIEMOP_DEF(iemOp_Grp15)
{
    IEMOP_HLP_MIN_586(); /* Not entirely accurate nor needed, but useful for debugging 286 code. */
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
        /* register, register */
        return FNIEMOP_CALL_1(g_apfnGroup15RegReg[ IEM_GET_MODRM_REG_8(bRm) * 4
                                                  + pVCpu->iem.s.idxPrefix], bRm);
    /* memory, register */
    return FNIEMOP_CALL_1(g_apfnGroup15MemReg[ IEM_GET_MODRM_REG_8(bRm) * 4
                                              + pVCpu->iem.s.idxPrefix], bRm);
}


/**
 * @opcode      0xaf
 * @opflclass   multiply
 */
FNIEMOP_DEF(iemOp_imul_Gv_Ev)
{
    IEMOP_MNEMONIC(imul_Gv_Ev, "imul Gv,Ev");
    IEMOP_HLP_MIN_386();
    IEMOP_VERIFICATION_UNDEFINED_EFLAGS(X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF);
    const IEMOPBINSIZES * const pImpl = IEMTARGETCPU_EFL_BEHAVIOR_SELECT(g_iemAImpl_imul_two_eflags);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    IEMOP_BODY_BINARY_rv_rm(bRm, pImpl->pfnNormalU16, pImpl->pfnNormalU32, pImpl->pfnNormalU64, IEM_MC_F_MIN_386, imul, 0);
}


/**
 * @opcode      0xb0
 * @opflclass   arithmetic
 */
FNIEMOP_DEF(iemOp_cmpxchg_Eb_Gb)
{
    IEMOP_MNEMONIC(cmpxchg_Eb_Gb, "cmpxchg Eb,Gb");
    IEMOP_HLP_MIN_486();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);

    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_486, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_ARG(uint8_t *,       pu8Dst,                 0);
        IEM_MC_ARG(uint8_t *,       pu8Al,                  1);
        IEM_MC_ARG(uint8_t,         u8Src,                  2);
        IEM_MC_ARG(uint32_t *,      pEFlags,                3);

        IEM_MC_FETCH_GREG_U8(u8Src, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_GREG_U8(pu8Dst, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_REF_GREG_U8(pu8Al, X86_GREG_xAX);
        IEM_MC_REF_EFLAGS(pEFlags);
        IEM_MC_CALL_VOID_AIMPL_4(iemAImpl_cmpxchg_u8, pu8Dst, pu8Al, u8Src, pEFlags);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
#define IEMOP_BODY_CMPXCHG_BYTE(a_fnWorker, a_Type) \
            IEM_MC_BEGIN(IEM_MC_F_MIN_486, 0); \
            IEM_MC_LOCAL(RTGCPTR,       GCPtrEffDst); \
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0); \
            IEMOP_HLP_DONE_DECODING(); \
            \
            IEM_MC_LOCAL(uint8_t,           bUnmapInfo); \
            IEM_MC_ARG(uint8_t *,           pu8Dst,                 0); \
            IEM_MC_MEM_MAP_U8_##a_Type(pu8Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
            \
            IEM_MC_ARG(uint8_t,             u8Src,                  2); \
            IEM_MC_FETCH_GREG_U8(u8Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
            \
            IEM_MC_LOCAL(uint8_t,           u8Al); \
            IEM_MC_FETCH_GREG_U8(u8Al, X86_GREG_xAX); \
            IEM_MC_ARG_LOCAL_REF(uint8_t *, pu8Al, u8Al,            1); \
            \
            IEM_MC_ARG_LOCAL_EFLAGS(        pEFlags, EFlags,        3); \
            IEM_MC_CALL_VOID_AIMPL_4(a_fnWorker, pu8Dst, pu8Al, u8Src, pEFlags); \
            \
            IEM_MC_MEM_COMMIT_AND_UNMAP_##a_Type(bUnmapInfo); \
            IEM_MC_COMMIT_EFLAGS(EFlags); \
            IEM_MC_STORE_GREG_U8(X86_GREG_xAX, u8Al); \
            IEM_MC_ADVANCE_RIP_AND_FINISH(); \
            IEM_MC_END()

        if (!(pVCpu->iem.s.fPrefixes & IEM_OP_PRF_LOCK) || (pVCpu->iem.s.fExec & IEM_F_X86_DISREGARD_LOCK))
        {
            IEMOP_BODY_CMPXCHG_BYTE(iemAImpl_cmpxchg_u8,RW);
        }
        else
        {
            IEMOP_BODY_CMPXCHG_BYTE(iemAImpl_cmpxchg_u8_locked,ATOMIC);
        }
    }
}

/**
 * @opcode      0xb1
 * @opflclass   arithmetic
 */
FNIEMOP_DEF(iemOp_cmpxchg_Ev_Gv)
{
    IEMOP_MNEMONIC(cmpxchg_Ev_Gv, "cmpxchg Ev,Gv");
    IEMOP_HLP_MIN_486();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);

    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        switch (pVCpu->iem.s.enmEffOpSize)
        {
            case IEMMODE_16BIT:
                IEM_MC_BEGIN(IEM_MC_F_MIN_486, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_ARG(uint16_t *,      pu16Dst,                0);
                IEM_MC_ARG(uint16_t *,      pu16Ax,                 1);
                IEM_MC_ARG(uint16_t,        u16Src,                 2);
                IEM_MC_ARG(uint32_t *,      pEFlags,                3);

                IEM_MC_FETCH_GREG_U16(u16Src, IEM_GET_MODRM_REG(pVCpu, bRm));
                IEM_MC_REF_GREG_U16(pu16Dst, IEM_GET_MODRM_RM(pVCpu, bRm));
                IEM_MC_REF_GREG_U16(pu16Ax, X86_GREG_xAX);
                IEM_MC_REF_EFLAGS(pEFlags);
                IEM_MC_CALL_VOID_AIMPL_4(iemAImpl_cmpxchg_u16, pu16Dst, pu16Ax, u16Src, pEFlags);

                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            case IEMMODE_32BIT:
                IEM_MC_BEGIN(IEM_MC_F_MIN_486, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_ARG(uint32_t *,      pu32Dst,                0);
                IEM_MC_ARG(uint32_t *,      pu32Eax,                1);
                IEM_MC_ARG(uint32_t,        u32Src,                 2);
                IEM_MC_ARG(uint32_t *,      pEFlags,                3);

                IEM_MC_FETCH_GREG_U32(u32Src, IEM_GET_MODRM_REG(pVCpu, bRm));
                IEM_MC_REF_GREG_U32(pu32Dst, IEM_GET_MODRM_RM(pVCpu, bRm));
                IEM_MC_REF_GREG_U32(pu32Eax, X86_GREG_xAX);
                IEM_MC_REF_EFLAGS(pEFlags);
                IEM_MC_CALL_VOID_AIMPL_4(iemAImpl_cmpxchg_u32, pu32Dst, pu32Eax, u32Src, pEFlags);

                IEM_MC_IF_EFL_BIT_SET(X86_EFL_ZF) {
                    IEM_MC_CLEAR_HIGH_GREG_U64(IEM_GET_MODRM_RM(pVCpu, bRm));
                } IEM_MC_ELSE() {
                    IEM_MC_CLEAR_HIGH_GREG_U64(X86_GREG_xAX);
                } IEM_MC_ENDIF();

                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            case IEMMODE_64BIT:
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_ARG(uint64_t *,      pu64Dst,                0);
                IEM_MC_ARG(uint64_t *,      pu64Rax,                1);
                IEM_MC_ARG(uint64_t,        u64Src,                 2);
                IEM_MC_ARG(uint32_t *,      pEFlags,                3);

                IEM_MC_FETCH_GREG_U64(u64Src, IEM_GET_MODRM_REG(pVCpu, bRm));
                IEM_MC_REF_GREG_U64(pu64Dst, IEM_GET_MODRM_RM(pVCpu, bRm));
                IEM_MC_REF_GREG_U64(pu64Rax, X86_GREG_xAX);
                IEM_MC_REF_EFLAGS(pEFlags);
                IEM_MC_CALL_VOID_AIMPL_4(iemAImpl_cmpxchg_u64, pu64Dst, pu64Rax, u64Src, pEFlags);

                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            IEM_NOT_REACHED_DEFAULT_CASE_RET();
        }
    }
    else
    {
#define IEMOP_BODY_CMPXCHG_EV_GV(a_fnWorker16, a_fnWorker32, a_fnWorker64,a_Type) \
        do { \
            switch (pVCpu->iem.s.enmEffOpSize) \
            { \
                case IEMMODE_16BIT: \
                    IEM_MC_BEGIN(IEM_MC_F_MIN_486, 0); \
                    \
                    IEM_MC_LOCAL(uint8_t,       bUnmapInfo); \
                    IEM_MC_LOCAL(RTGCPTR,       GCPtrEffDst); \
                    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0); \
                    IEMOP_HLP_DONE_DECODING(); \
                    \
                    IEM_MC_ARG(uint16_t *,              pu16Dst,                0); \
                    IEM_MC_MEM_MAP_U16_##a_Type(pu16Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                    \
                    IEM_MC_ARG(uint16_t,                u16Src,                 2); \
                    IEM_MC_FETCH_GREG_U16(u16Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    \
                    IEM_MC_LOCAL(uint16_t,              u16Ax); \
                    IEM_MC_FETCH_GREG_U16(u16Ax, X86_GREG_xAX); \
                    IEM_MC_ARG_LOCAL_REF(uint16_t *,    pu16Ax,  u16Ax,         1); \
                    \
                    IEM_MC_ARG_LOCAL_EFLAGS(            pEFlags, EFlags,        3); \
                    IEM_MC_CALL_VOID_AIMPL_4(a_fnWorker16, pu16Dst, pu16Ax, u16Src, pEFlags); \
                    \
                    IEM_MC_MEM_COMMIT_AND_UNMAP_##a_Type(bUnmapInfo); \
                    IEM_MC_COMMIT_EFLAGS(EFlags); \
                    IEM_MC_STORE_GREG_U16(X86_GREG_xAX, u16Ax); \
                    IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                    IEM_MC_END(); \
                    break; \
                \
                case IEMMODE_32BIT: \
                    IEM_MC_BEGIN(IEM_MC_F_MIN_486, 0); \
                    IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0); \
                    IEMOP_HLP_DONE_DECODING(); \
                    \
                    IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                    IEM_MC_ARG(uint32_t *,              pu32Dst,                0); \
                    IEM_MC_MEM_MAP_U32_##a_Type(pu32Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                    \
                    IEM_MC_ARG(uint32_t,                u32Src,                 2); \
                    IEM_MC_FETCH_GREG_U32(u32Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    \
                    IEM_MC_LOCAL(uint32_t,              u32Eax); \
                    IEM_MC_FETCH_GREG_U32(u32Eax, X86_GREG_xAX); \
                    IEM_MC_ARG_LOCAL_REF(uint32_t *,    pu32Eax, u32Eax,        1); \
                    \
                    IEM_MC_ARG_LOCAL_EFLAGS(            pEFlags, EFlags,        3); \
                    IEM_MC_CALL_VOID_AIMPL_4(a_fnWorker32, pu32Dst, pu32Eax, u32Src, pEFlags); \
                    \
                    IEM_MC_MEM_COMMIT_AND_UNMAP_##a_Type(bUnmapInfo); \
                    IEM_MC_COMMIT_EFLAGS(EFlags); \
                    \
                    IEM_MC_IF_EFL_BIT_NOT_SET(X86_EFL_ZF) { \
                        IEM_MC_STORE_GREG_U32(X86_GREG_xAX, u32Eax); \
                    } IEM_MC_ENDIF(); \
                    \
                    IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                    IEM_MC_END(); \
                    break; \
                \
                case IEMMODE_64BIT: \
                    IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
                    IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0); \
                    IEMOP_HLP_DONE_DECODING(); \
                    \
                    IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                    IEM_MC_ARG(uint64_t *,              pu64Dst,                0); \
                    IEM_MC_MEM_MAP_U64_##a_Type(pu64Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                    \
                    IEM_MC_ARG(uint64_t,                u64Src,                 2); \
                    IEM_MC_FETCH_GREG_U64(u64Src, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    \
                    IEM_MC_LOCAL(uint64_t,              u64Rax); \
                    IEM_MC_FETCH_GREG_U64(u64Rax, X86_GREG_xAX); \
                    IEM_MC_ARG_LOCAL_REF(uint64_t *,    pu64Rax, u64Rax,        1); \
                    \
                    IEM_MC_ARG_LOCAL_EFLAGS(            pEFlags, EFlags,        3); \
                    \
                    IEM_MC_CALL_VOID_AIMPL_4(a_fnWorker64, pu64Dst, pu64Rax, u64Src, pEFlags); \
                    \
                    IEM_MC_MEM_COMMIT_AND_UNMAP_##a_Type(bUnmapInfo); \
                    IEM_MC_COMMIT_EFLAGS(EFlags); \
                    IEM_MC_STORE_GREG_U64(X86_GREG_xAX, u64Rax); \
                    IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                    IEM_MC_END(); \
                    break; \
                \
                IEM_NOT_REACHED_DEFAULT_CASE_RET(); \
            } \
        } while (0)

        if (!(pVCpu->iem.s.fPrefixes & IEM_OP_PRF_LOCK) || (pVCpu->iem.s.fExec & IEM_F_X86_DISREGARD_LOCK))
        {
            IEMOP_BODY_CMPXCHG_EV_GV(iemAImpl_cmpxchg_u16, iemAImpl_cmpxchg_u32, iemAImpl_cmpxchg_u64,RW);
        }
        else
        {
            IEMOP_BODY_CMPXCHG_EV_GV(iemAImpl_cmpxchg_u16_locked, iemAImpl_cmpxchg_u32_locked, iemAImpl_cmpxchg_u64_locked,ATOMIC);
        }
    }
}


/** Opcode 0x0f 0xb2. */
FNIEMOP_DEF(iemOp_lss_Gv_Mp)
{
    IEMOP_MNEMONIC(lss_Gv_Mp, "lss Gv,Mp");
    IEMOP_HLP_MIN_386();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
        IEMOP_RAISE_INVALID_OPCODE_RET();
    return FNIEMOP_CALL_2(iemOpCommonLoadSRegAndGreg, X86_SREG_SS, bRm);
}


/**
 * @opcode      0xb3
 * @oppfx       n/a
 * @opflclass   bitmap
 */
FNIEMOP_DEF(iemOp_btr_Ev_Gv)
{
    IEMOP_MNEMONIC(btr_Ev_Gv, "btr Ev,Gv");
    IEMOP_HLP_MIN_386();
    IEMOP_BODY_BIT_Ev_Gv_RW(    iemAImpl_btr_u16,        iemAImpl_btr_u32,        iemAImpl_btr_u64);
    IEMOP_BODY_BIT_Ev_Gv_LOCKED(iemAImpl_btr_u16_locked, iemAImpl_btr_u32_locked, iemAImpl_btr_u64_locked);
}


/** Opcode 0x0f 0xb4. */
FNIEMOP_DEF(iemOp_lfs_Gv_Mp)
{
    IEMOP_MNEMONIC(lfs_Gv_Mp, "lfs Gv,Mp");
    IEMOP_HLP_MIN_386();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
        IEMOP_RAISE_INVALID_OPCODE_RET();
    return FNIEMOP_CALL_2(iemOpCommonLoadSRegAndGreg, X86_SREG_FS, bRm);
}


/** Opcode 0x0f 0xb5. */
FNIEMOP_DEF(iemOp_lgs_Gv_Mp)
{
    IEMOP_MNEMONIC(lgs_Gv_Mp, "lgs Gv,Mp");
    IEMOP_HLP_MIN_386();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
        IEMOP_RAISE_INVALID_OPCODE_RET();
    return FNIEMOP_CALL_2(iemOpCommonLoadSRegAndGreg, X86_SREG_GS, bRm);
}


/** Opcode 0x0f 0xb6. */
FNIEMOP_DEF(iemOp_movzx_Gv_Eb)
{
    IEMOP_MNEMONIC(movzx_Gv_Eb, "movzx Gv,Eb");
    IEMOP_HLP_MIN_386();

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);

    /*
     * If rm is denoting a register, no more instruction bytes.
     */
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        switch (pVCpu->iem.s.enmEffOpSize)
        {
            case IEMMODE_16BIT:
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_LOCAL(uint16_t, u16Value);
                IEM_MC_FETCH_GREG_U8_ZX_U16(u16Value, IEM_GET_MODRM_RM(pVCpu, bRm));
                IEM_MC_STORE_GREG_U16(IEM_GET_MODRM_REG(pVCpu, bRm), u16Value);
                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            case IEMMODE_32BIT:
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_LOCAL(uint32_t, u32Value);
                IEM_MC_FETCH_GREG_U8_ZX_U32(u32Value, IEM_GET_MODRM_RM(pVCpu, bRm));
                IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), u32Value);
                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            case IEMMODE_64BIT:
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_LOCAL(uint64_t, u64Value);
                IEM_MC_FETCH_GREG_U8_ZX_U64(u64Value, IEM_GET_MODRM_RM(pVCpu, bRm));
                IEM_MC_STORE_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), u64Value);
                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            IEM_NOT_REACHED_DEFAULT_CASE_RET();
        }
    }
    else
    {
        /*
         * We're loading a register from memory.
         */
        switch (pVCpu->iem.s.enmEffOpSize)
        {
            case IEMMODE_16BIT:
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
                IEM_MC_LOCAL(uint16_t, u16Value);
                IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_FETCH_MEM_U8_ZX_U16(u16Value, pVCpu->iem.s.iEffSeg, GCPtrEffDst);
                IEM_MC_STORE_GREG_U16(IEM_GET_MODRM_REG(pVCpu, bRm), u16Value);
                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            case IEMMODE_32BIT:
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
                IEM_MC_LOCAL(uint32_t, u32Value);
                IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_FETCH_MEM_U8_ZX_U32(u32Value, pVCpu->iem.s.iEffSeg, GCPtrEffDst);
                IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), u32Value);
                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            case IEMMODE_64BIT:
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
                IEM_MC_LOCAL(uint64_t, u64Value);
                IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_FETCH_MEM_U8_ZX_U64(u64Value, pVCpu->iem.s.iEffSeg, GCPtrEffDst);
                IEM_MC_STORE_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), u64Value);
                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            IEM_NOT_REACHED_DEFAULT_CASE_RET();
        }
    }
}


/** Opcode 0x0f 0xb7. */
FNIEMOP_DEF(iemOp_movzx_Gv_Ew)
{
    IEMOP_MNEMONIC(movzx_Gv_Ew, "movzx Gv,Ew");
    IEMOP_HLP_MIN_386();

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);

    /** @todo Not entirely sure how the operand size prefix is handled here,
     *        assuming that it will be ignored. Would be nice to have a few
     *        test for this. */

    /** @todo There should be no difference in the behaviour whether REX.W is
     *        present or not... */

    /*
     * If rm is denoting a register, no more instruction bytes.
     */
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        if (pVCpu->iem.s.enmEffOpSize != IEMMODE_64BIT)
        {
            IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
            IEM_MC_LOCAL(uint32_t, u32Value);
            IEM_MC_FETCH_GREG_U16_ZX_U32(u32Value, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), u32Value);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
            IEM_MC_LOCAL(uint64_t, u64Value);
            IEM_MC_FETCH_GREG_U16_ZX_U64(u64Value, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_STORE_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), u64Value);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        /*
         * We're loading a register from memory.
         */
        if (pVCpu->iem.s.enmEffOpSize != IEMMODE_64BIT)
        {
            IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
            IEM_MC_LOCAL(uint32_t, u32Value);
            IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
            IEM_MC_FETCH_MEM_U16_ZX_U32(u32Value, pVCpu->iem.s.iEffSeg, GCPtrEffDst);
            IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), u32Value);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_LOCAL(uint64_t, u64Value);
            IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
            IEM_MC_FETCH_MEM_U16_ZX_U64(u64Value, pVCpu->iem.s.iEffSeg, GCPtrEffDst);
            IEM_MC_STORE_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), u64Value);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/** Opcode      0x0f 0xb8 - JMPE (reserved for emulator on IPF) */
FNIEMOP_UD_STUB(iemOp_jmpe);


/**
 * @opcode      0xb8
 * @oppfx       0xf3
 * @opflmodify  cf,pf,af,zf,sf,of
 * @opflclear   cf,pf,af,sf,of
 */
FNIEMOP_DEF(iemOp_popcnt_Gv_Ev)
{
    IEMOP_MNEMONIC2(RM, POPCNT, popcnt, Gv, Ev, DISOPTYPE_HARMLESS, 0);
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fPopCnt)
        return iemOp_InvalidNeedRM(pVCpu);
#ifndef TST_IEM_CHECK_MC
# if (defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)) && !defined(IEM_WITHOUT_ASSEMBLY)
    static const IEMOPBINSIZES s_Native =
    {   NULL, NULL, iemAImpl_popcnt_u16, NULL, iemAImpl_popcnt_u32, NULL, iemAImpl_popcnt_u64, NULL };
# endif
    static const IEMOPBINSIZES s_Fallback =
    {   NULL, NULL, iemAImpl_popcnt_u16_fallback, NULL, iemAImpl_popcnt_u32_fallback, NULL, iemAImpl_popcnt_u64_fallback, NULL };
#endif
    const IEMOPBINSIZES * const pImpl = IEM_SELECT_HOST_OR_FALLBACK(fPopCnt, &s_Native, &s_Fallback);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    IEMOP_BODY_BINARY_rv_rm(bRm, pImpl->pfnNormalU16, pImpl->pfnNormalU32, pImpl->pfnNormalU64, IEM_MC_F_NOT_286_OR_OLDER, popcnt, 0);
}


/**
 * @opcode      0xb9
 * @opinvalid   intel-modrm
 * @optest      ->
 */
FNIEMOP_DEF(iemOp_Grp10)
{
    /*
     * AMD does not decode beyond the 0xb9 whereas intel does the modr/m bit
     * too. See bs3-cpu-decoder-1.c32.  So, we can forward to iemOp_InvalidNeedRM.
     */
    Log(("iemOp_Grp10 aka UD1 -> #UD\n"));
    IEMOP_MNEMONIC2EX(ud1, "ud1", RM, UD1, ud1, Gb, Eb, DISOPTYPE_INVALID, IEMOPHINT_IGNORES_OP_SIZES); /* just picked Gb,Eb here. */
    return FNIEMOP_CALL(iemOp_InvalidNeedRM);
}


/**
 * Body for group 8 bit instruction.
 */
#define IEMOP_BODY_BIT_Ev_Ib_RW(a_fnNormalU16, a_fnNormalU32, a_fnNormalU64) \
    IEMOP_VERIFICATION_UNDEFINED_EFLAGS(X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF); \
    \
    if (IEM_IS_MODRM_REG_MODE(bRm)) \
    { \
        /* register destination. */ \
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm); \
        \
        switch (pVCpu->iem.s.enmEffOpSize) \
        { \
            case IEMMODE_16BIT: \
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_ARG(uint16_t *,      pu16Dst,                    1); \
                IEM_MC_REF_GREG_U16(pu16Dst, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_ARG_EFLAGS(          fEFlagsIn,                  0); \
                IEM_MC_ARG_CONST(uint16_t,  u16Src, /*=*/ bImm & 0x0f,  2); \
                IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU16, fEFlagsIn, pu16Dst, u16Src); \
                IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            case IEMMODE_32BIT: \
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_ARG(uint32_t *,      pu32Dst,                    1); \
                IEM_MC_REF_GREG_U32(pu32Dst, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_ARG_EFLAGS(          fEFlagsIn,                  0); \
                IEM_MC_ARG_CONST(uint32_t,  u32Src, /*=*/ bImm & 0x1f,  2); \
                IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU32, fEFlagsIn, pu32Dst, u32Src); \
                IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                \
                IEM_MC_CLEAR_HIGH_GREG_U64(IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            case IEMMODE_64BIT: \
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_ARG(uint64_t *,      pu64Dst,                    1); \
                IEM_MC_REF_GREG_U64(pu64Dst, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_ARG_EFLAGS(          fEFlagsIn,                  0); \
                IEM_MC_ARG_CONST(uint64_t,  u64Src, /*=*/ bImm & 0x3f,  2); \
                IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU64, fEFlagsIn, pu64Dst, u64Src); \
                IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            IEM_NOT_REACHED_DEFAULT_CASE_RET(); \
        } \
    } \
    else \
    { \
        /* memory destination. */ \
        /** @todo test negative bit offsets! */ \
        if (!(pVCpu->iem.s.fPrefixes & IEM_OP_PRF_LOCK) || (pVCpu->iem.s.fExec & IEM_F_X86_DISREGARD_LOCK)) \
        { \
            switch (pVCpu->iem.s.enmEffOpSize) \
            { \
                case IEMMODE_16BIT: \
                    IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                    IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 1); \
                    \
                    uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm); \
                    IEMOP_HLP_DONE_DECODING(); \
                    \
                    IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                    IEM_MC_ARG(uint16_t *,              pu16Dst,                    1); \
                    IEM_MC_MEM_MAP_U16_RW(pu16Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                    \
                    IEM_MC_ARG_EFLAGS(                  fEFlagsIn,                  0); \
                    IEM_MC_ARG_CONST(uint16_t,          u16Src, /*=*/ bImm & 0x0f,  2); \
                    IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU16, fEFlagsIn, pu16Dst, u16Src); \
                    \
                    IEM_MC_MEM_COMMIT_AND_UNMAP_RW(bUnmapInfo); \
                    IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                    IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                    IEM_MC_END(); \
                    break; \
                \
                case IEMMODE_32BIT: \
                    IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                    IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 1); \
                    \
                    uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm); \
                    IEMOP_HLP_DONE_DECODING(); \
                    \
                    IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                    IEM_MC_ARG(uint32_t *,              pu32Dst,                    1); \
                    IEM_MC_MEM_MAP_U32_RW(pu32Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                    \
                    IEM_MC_ARG_EFLAGS(                  fEFlagsIn,                  0); \
                    IEM_MC_ARG_CONST(uint32_t,          u32Src, /*=*/ bImm & 0x1f,  2); \
                    IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU32, fEFlagsIn, pu32Dst, u32Src); \
                    \
                    IEM_MC_MEM_COMMIT_AND_UNMAP_RW(bUnmapInfo); \
                    IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                    IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                    IEM_MC_END(); \
                    break; \
                \
                case IEMMODE_64BIT: \
                    IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
                    IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 1); \
                    \
                    uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm); \
                    IEMOP_HLP_DONE_DECODING(); \
                    \
                    IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                    IEM_MC_ARG(uint64_t *,              pu64Dst,                    1); \
                    IEM_MC_MEM_MAP_U64_RW(pu64Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                    \
                    IEM_MC_ARG_EFLAGS(                  fEFlagsIn,                  0); \
                    IEM_MC_ARG_CONST(uint64_t,          u64Src, /*=*/ bImm & 0x3f,  2); \
                    IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU64, fEFlagsIn, pu64Dst, u64Src); \
                    \
                    IEM_MC_MEM_COMMIT_AND_UNMAP_RW(bUnmapInfo); \
                    IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                    IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                    IEM_MC_END(); \
                    break; \
                \
                IEM_NOT_REACHED_DEFAULT_CASE_RET(); \
            } \
        } \
        else \
        { \
            (void)0
/* Separate macro to work around parsing issue in IEMAllInstPython.py */
#define IEMOP_BODY_BIT_Ev_Ib_LOCKED(a_fnLockedU16, a_fnLockedU32, a_fnLockedU64) \
            switch (pVCpu->iem.s.enmEffOpSize) \
            { \
                case IEMMODE_16BIT: \
                    IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                    IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 1); \
                    \
                    uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm); \
                    IEMOP_HLP_DONE_DECODING(); \
                    \
                    IEM_MC_ARG(uint16_t *,              pu16Dst,                    1); \
                    IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                    IEM_MC_MEM_MAP_U16_ATOMIC(pu16Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                    \
                    IEM_MC_ARG_EFLAGS(                  fEFlagsIn,                  0); \
                    IEM_MC_ARG_CONST(uint16_t,          u16Src, /*=*/ bImm & 0x0f,  2); \
                    IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnLockedU16, fEFlagsIn, pu16Dst, u16Src); \
                    \
                    IEM_MC_MEM_COMMIT_AND_UNMAP_ATOMIC(bUnmapInfo); \
                    IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                    IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                    IEM_MC_END(); \
                    break; \
                \
                case IEMMODE_32BIT: \
                    IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                    IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 1); \
                    \
                    uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm); \
                    IEMOP_HLP_DONE_DECODING(); \
                    \
                    IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                    IEM_MC_ARG(uint32_t *,              pu32Dst,                    1); \
                    IEM_MC_MEM_MAP_U32_ATOMIC(pu32Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                    \
                    IEM_MC_ARG_EFLAGS(                  fEFlagsIn,                  0); \
                    IEM_MC_ARG_CONST(uint32_t,          u32Src, /*=*/ bImm & 0x1f,  2); \
                    IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnLockedU32, fEFlagsIn, pu32Dst, u32Src); \
                    \
                    IEM_MC_MEM_COMMIT_AND_UNMAP_ATOMIC(bUnmapInfo); \
                    IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                    IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                    IEM_MC_END(); \
                    break; \
                \
                case IEMMODE_64BIT: \
                    IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
                    IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 1); \
                    \
                    uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm); \
                    IEMOP_HLP_DONE_DECODING(); \
                    \
                    IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                    IEM_MC_ARG(uint64_t *,              pu64Dst,                    1); \
                    IEM_MC_MEM_MAP_U64_ATOMIC(pu64Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                    \
                    IEM_MC_ARG_EFLAGS(                  fEFlagsIn,                  0); \
                    IEM_MC_ARG_CONST(uint64_t,          u64Src, /*=*/ bImm & 0x3f,  2); \
                    IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnLockedU64, fEFlagsIn, pu64Dst, u64Src); \
                    \
                    IEM_MC_MEM_COMMIT_AND_UNMAP_ATOMIC(bUnmapInfo); \
                    IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                    IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                    IEM_MC_END(); \
                    break; \
                \
                IEM_NOT_REACHED_DEFAULT_CASE_RET(); \
            } \
        } \
    } \
    (void)0

/* Read-only version (bt) */
#define IEMOP_BODY_BIT_Ev_Ib_RO(a_fnNormalU16, a_fnNormalU32, a_fnNormalU64) \
    IEMOP_VERIFICATION_UNDEFINED_EFLAGS(X86_EFL_OF | X86_EFL_SF | X86_EFL_ZF | X86_EFL_AF | X86_EFL_PF); \
    \
    if (IEM_IS_MODRM_REG_MODE(bRm)) \
    { \
        /* register destination. */ \
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm); \
        \
        switch (pVCpu->iem.s.enmEffOpSize) \
        { \
            case IEMMODE_16BIT: \
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_ARG(uint16_t const *,    pu16Dst,                    1); \
                IEM_MC_REF_GREG_U16_CONST(pu16Dst, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_ARG_EFLAGS(              fEFlagsIn,                  0); \
                IEM_MC_ARG_CONST(uint16_t,      u16Src, /*=*/ bImm & 0x0f,  2); \
                IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU16, fEFlagsIn, pu16Dst, u16Src); \
                IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            case IEMMODE_32BIT: \
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_ARG(uint32_t const *,    pu32Dst,                    1); \
                IEM_MC_REF_GREG_U32_CONST(pu32Dst, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_ARG_EFLAGS(              fEFlagsIn,                  0); \
                IEM_MC_ARG_CONST(uint32_t,      u32Src, /*=*/ bImm & 0x1f,  2); \
                IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU32, fEFlagsIn, pu32Dst, u32Src); \
                IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            case IEMMODE_64BIT: \
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_ARG(uint64_t const *,    pu64Dst,                    1); \
                IEM_MC_REF_GREG_U64_CONST(pu64Dst, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_ARG_EFLAGS(              fEFlagsIn,                  0); \
                IEM_MC_ARG_CONST(uint64_t,      u64Src, /*=*/ bImm & 0x3f,  2); \
                IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU64, fEFlagsIn, pu64Dst, u64Src); \
                IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            IEM_NOT_REACHED_DEFAULT_CASE_RET(); \
        } \
    } \
    else \
    { \
        /* memory destination. */ \
        /** @todo test negative bit offsets! */ \
        if (!(pVCpu->iem.s.fPrefixes & IEM_OP_PRF_LOCK)) \
        { \
            switch (pVCpu->iem.s.enmEffOpSize) \
            { \
                case IEMMODE_16BIT: \
                    IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                    IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 1); \
                    \
                    uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm); \
                    IEMOP_HLP_DONE_DECODING(); \
                    \
                    IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                    IEM_MC_ARG(uint16_t const *,        pu16Dst,                    1); \
                    IEM_MC_MEM_MAP_U16_RO(pu16Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                    \
                    IEM_MC_ARG_EFLAGS(                  fEFlagsIn,                  0); \
                    IEM_MC_ARG_CONST(uint16_t,          u16Src, /*=*/ bImm & 0x0f,  2); \
                    IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU16, fEFlagsIn, pu16Dst, u16Src); \
                    \
                    IEM_MC_MEM_COMMIT_AND_UNMAP_RO(bUnmapInfo); \
                    IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                    IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                    IEM_MC_END(); \
                    break; \
                \
                case IEMMODE_32BIT: \
                    IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                    IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 1); \
                    \
                    uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm); \
                    IEMOP_HLP_DONE_DECODING(); \
                    \
                    IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                    IEM_MC_ARG(uint32_t const *,        pu32Dst,                    1); \
                    IEM_MC_MEM_MAP_U32_RO(pu32Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                    \
                    IEM_MC_ARG_EFLAGS(                  fEFlagsIn,                  0); \
                    IEM_MC_ARG_CONST(uint32_t,          u32Src, /*=*/ bImm & 0x1f,  2); \
                    IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU32, fEFlagsIn, pu32Dst, u32Src); \
                    \
                    IEM_MC_MEM_COMMIT_AND_UNMAP_RO(bUnmapInfo); \
                    IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                    IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                    IEM_MC_END(); \
                    break; \
                \
                case IEMMODE_64BIT: \
                    IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
                    IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 1); \
                    \
                    uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm); \
                    IEMOP_HLP_DONE_DECODING(); \
                    \
                    IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                    IEM_MC_ARG(uint64_t const *,        pu64Dst,                    1); \
                    IEM_MC_MEM_MAP_U64_RO(pu64Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                    \
                    IEM_MC_ARG_EFLAGS(                  fEFlagsIn,                  0); \
                    IEM_MC_ARG_CONST(uint64_t,          u64Src, /*=*/ bImm & 0x3f,  2); \
                    IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, a_fnNormalU64, fEFlagsIn, pu64Dst, u64Src); \
                    \
                    IEM_MC_MEM_COMMIT_AND_UNMAP_RO(bUnmapInfo); \
                    IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                    IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                    IEM_MC_END(); \
                    break; \
                \
                IEM_NOT_REACHED_DEFAULT_CASE_RET(); \
            } \
        } \
        else \
        { \
            IEMOP_HLP_DONE_DECODING(); \
            IEMOP_RAISE_INVALID_LOCK_PREFIX_RET(); \
        } \
    } \
    (void)0


/**
 * @opmaps      grp8
 * @opcode      /4
 * @oppfx       n/a
 * @opflclass   bitmap
 */
FNIEMOPRM_DEF(iemOp_Grp8_bt_Ev_Ib)
{
    IEMOP_MNEMONIC(bt_Ev_Ib, "bt  Ev,Ib");
    IEMOP_BODY_BIT_Ev_Ib_RO(iemAImpl_bt_u16, iemAImpl_bt_u32, iemAImpl_bt_u64);
}


/**
 * @opmaps      grp8
 * @opcode      /5
 * @oppfx       n/a
 * @opflclass   bitmap
 */
FNIEMOPRM_DEF(iemOp_Grp8_bts_Ev_Ib)
{
    IEMOP_MNEMONIC(bts_Ev_Ib, "bts Ev,Ib");
    IEMOP_BODY_BIT_Ev_Ib_RW(    iemAImpl_bts_u16,        iemAImpl_bts_u32,        iemAImpl_bts_u64);
    IEMOP_BODY_BIT_Ev_Ib_LOCKED(iemAImpl_bts_u16_locked, iemAImpl_bts_u32_locked, iemAImpl_bts_u64_locked);
}


/**
 * @opmaps      grp8
 * @opcode      /6
 * @oppfx       n/a
 * @opflclass   bitmap
 */
FNIEMOPRM_DEF(iemOp_Grp8_btr_Ev_Ib)
{
    IEMOP_MNEMONIC(btr_Ev_Ib, "btr Ev,Ib");
    IEMOP_BODY_BIT_Ev_Ib_RW(    iemAImpl_btr_u16,        iemAImpl_btr_u32,        iemAImpl_btr_u64);
    IEMOP_BODY_BIT_Ev_Ib_LOCKED(iemAImpl_btr_u16_locked, iemAImpl_btr_u32_locked, iemAImpl_btr_u64_locked);
}


/**
 * @opmaps      grp8
 * @opcode      /7
 * @oppfx       n/a
 * @opflclass   bitmap
 */
FNIEMOPRM_DEF(iemOp_Grp8_btc_Ev_Ib)
{
    IEMOP_MNEMONIC(btc_Ev_Ib, "btc Ev,Ib");
    IEMOP_BODY_BIT_Ev_Ib_RW(    iemAImpl_btc_u16,        iemAImpl_btc_u32,        iemAImpl_btc_u64);
    IEMOP_BODY_BIT_Ev_Ib_LOCKED(iemAImpl_btc_u16_locked, iemAImpl_btc_u32_locked, iemAImpl_btc_u64_locked);
}


/** Opcode 0x0f 0xba. */
FNIEMOP_DEF(iemOp_Grp8)
{
    IEMOP_HLP_MIN_386();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    switch (IEM_GET_MODRM_REG_8(bRm))
    {
        case 4: return FNIEMOP_CALL_1(iemOp_Grp8_bt_Ev_Ib, bRm);
        case 5: return FNIEMOP_CALL_1(iemOp_Grp8_bts_Ev_Ib, bRm);
        case 6: return FNIEMOP_CALL_1(iemOp_Grp8_btr_Ev_Ib, bRm);
        case 7: return FNIEMOP_CALL_1(iemOp_Grp8_btc_Ev_Ib, bRm);

        case 0: case 1: case 2: case 3:
            /* Both AMD and Intel want full modr/m decoding and imm8. */
            return FNIEMOP_CALL_1(iemOp_InvalidWithRMAllNeedImm8, bRm);

        IEM_NOT_REACHED_DEFAULT_CASE_RET();
    }
}


/**
 * @opcode      0xbb
 * @oppfx       n/a
 * @opflclass   bitmap
 */
FNIEMOP_DEF(iemOp_btc_Ev_Gv)
{
    IEMOP_MNEMONIC(btc_Ev_Gv, "btc Ev,Gv");
    IEMOP_HLP_MIN_386();
    IEMOP_BODY_BIT_Ev_Gv_RW(    iemAImpl_btc_u16,        iemAImpl_btc_u32,        iemAImpl_btc_u64);
    IEMOP_BODY_BIT_Ev_Gv_LOCKED(iemAImpl_btc_u16_locked, iemAImpl_btc_u32_locked, iemAImpl_btc_u64_locked);
}


/**
 * Body for BSF and BSR instructions.
 *
 * These cannot use iemOpHlpBinaryOperator_rv_rm because they don't always write
 * the destination register, which means that for 32-bit operations the high
 * bits must be left alone.
 *
 * @param   pImpl   Pointer to the instruction implementation (assembly).
 */
#define IEMOP_BODY_BIT_SCAN_OPERATOR_RV_RM(pImpl) \
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm); \
    \
    /* \
     * If rm is denoting a register, no more instruction bytes. \
     */ \
    if (IEM_IS_MODRM_REG_MODE(bRm)) \
    { \
        switch (pVCpu->iem.s.enmEffOpSize) \
        { \
            case IEMMODE_16BIT: \
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                \
                IEM_MC_ARG(uint16_t,    u16Src,     2); \
                IEM_MC_FETCH_GREG_U16(u16Src, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_ARG(uint16_t *,  pu16Dst,    1); \
                IEM_MC_REF_GREG_U16(pu16Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_ARG_EFLAGS(      fEFlagsIn,  0); \
                IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, pImpl->pfnNormalU16, fEFlagsIn, pu16Dst, u16Src); \
                IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            case IEMMODE_32BIT: \
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                \
                IEM_MC_ARG(uint32_t,    u32Src,     2); \
                IEM_MC_FETCH_GREG_U32(u32Src, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_ARG(uint32_t *,  pu32Dst,    1); \
                IEM_MC_REF_GREG_U32(pu32Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_ARG_EFLAGS(      fEFlagsIn,  0); \
                IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, pImpl->pfnNormalU32, fEFlagsIn, pu32Dst, u32Src); \
                IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                IEM_MC_IF_EFL_BIT_NOT_SET(X86_EFL_ZF) { \
                    IEM_MC_CLEAR_HIGH_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm)); \
                } IEM_MC_ENDIF(); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
            \
            case IEMMODE_64BIT: \
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                \
                IEM_MC_ARG(uint64_t,    u64Src,     2); \
                IEM_MC_FETCH_GREG_U64(u64Src, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_ARG(uint64_t *,  pu64Dst,    1); \
                IEM_MC_REF_GREG_U64(pu64Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_ARG_EFLAGS(      fEFlagsIn,  0); \
                IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, pImpl->pfnNormalU64, fEFlagsIn, pu64Dst, u64Src); \
                IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                \
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
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                IEM_MC_LOCAL(RTGCPTR,   GCPtrEffDst); \
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                \
                IEM_MC_ARG(uint16_t,    u16Src,     2); \
                IEM_MC_FETCH_MEM_U16(u16Src, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                IEM_MC_ARG(uint16_t *,  pu16Dst,    1); \
                IEM_MC_REF_GREG_U16(pu16Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_ARG_EFLAGS(      fEFlagsIn,  0); \
                IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, pImpl->pfnNormalU16, fEFlagsIn, pu16Dst, u16Src); \
                IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
                \
            case IEMMODE_32BIT: \
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0); \
                IEM_MC_LOCAL(RTGCPTR,   GCPtrEffDst); \
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                \
                IEM_MC_ARG(uint32_t,    u32Src,     2); \
                IEM_MC_FETCH_MEM_U32(u32Src, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                IEM_MC_ARG(uint32_t *,  pu32Dst,    1); \
                IEM_MC_REF_GREG_U32(pu32Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_ARG_EFLAGS(      fEFlagsIn,  0); \
                IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, pImpl->pfnNormalU32, fEFlagsIn, pu32Dst, u32Src); \
                IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                \
                IEM_MC_IF_EFL_BIT_NOT_SET(X86_EFL_ZF) { \
                    IEM_MC_CLEAR_HIGH_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm)); \
                } IEM_MC_ENDIF(); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
                \
            case IEMMODE_64BIT: \
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
                IEM_MC_LOCAL(RTGCPTR,   GCPtrEffDst); \
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                \
                IEM_MC_ARG(uint64_t,    u64Src,     2); \
                IEM_MC_FETCH_MEM_U64(u64Src, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                IEM_MC_ARG(uint64_t *,  pu64Dst,    1); \
                IEM_MC_REF_GREG_U64(pu64Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_ARG_EFLAGS(      fEFlagsIn,  0); \
                IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, pImpl->pfnNormalU64, fEFlagsIn, pu64Dst, u64Src); \
                IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
                break; \
                \
            IEM_NOT_REACHED_DEFAULT_CASE_RET(); \
        } \
    } (void)0


/**
 * @opcode      0xbc
 * @oppfx       !0xf3
 * @opfltest    cf,pf,af,sf,of
 * @opflmodify  cf,pf,af,zf,sf,of
 * @opflundef   cf,pf,af,sf,of
 * @todo AMD doesn't modify cf,pf,af,sf&of but since intel does, we're forced to
 *       document them as inputs. Sigh.
 */
FNIEMOP_DEF(iemOp_bsf_Gv_Ev)
{
    IEMOP_MNEMONIC(bsf_Gv_Ev, "bsf Gv,Ev");
    IEMOP_HLP_MIN_386();
    IEMOP_VERIFICATION_UNDEFINED_EFLAGS(X86_EFL_OF | X86_EFL_SF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF);
    PCIEMOPBINSIZES const pImpl = IEMTARGETCPU_EFL_BEHAVIOR_SELECT(g_iemAImpl_bsf_eflags);
    IEMOP_BODY_BIT_SCAN_OPERATOR_RV_RM(pImpl);
}


/**
 * @opcode      0xbc
 * @oppfx       0xf3
 * @opfltest    pf,af,sf,of
 * @opflmodify  cf,pf,af,zf,sf,of
 * @opflundef   pf,af,sf,of
 * @todo AMD doesn't modify pf,af,sf&of but since intel does, we're forced to
 *       document them as inputs. Sigh.
 */
FNIEMOP_DEF(iemOp_tzcnt_Gv_Ev)
{
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fBmi1)
        return FNIEMOP_CALL(iemOp_bsf_Gv_Ev);
    IEMOP_MNEMONIC2(RM, TZCNT, tzcnt, Gv, Ev, DISOPTYPE_HARMLESS, 0);

#ifndef TST_IEM_CHECK_MC
    static const IEMOPBINSIZES s_iemAImpl_tzcnt =
    {   NULL, NULL,     iemAImpl_tzcnt_u16, NULL,       iemAImpl_tzcnt_u32, NULL,       iemAImpl_tzcnt_u64, NULL };
    static const IEMOPBINSIZES s_iemAImpl_tzcnt_amd =
    {   NULL, NULL,     iemAImpl_tzcnt_u16_amd, NULL,   iemAImpl_tzcnt_u32_amd, NULL,   iemAImpl_tzcnt_u64_amd, NULL };
    static const IEMOPBINSIZES s_iemAImpl_tzcnt_intel =
    {   NULL, NULL,     iemAImpl_tzcnt_u16_intel, NULL, iemAImpl_tzcnt_u32_intel, NULL, iemAImpl_tzcnt_u64_intel, NULL };
    static const IEMOPBINSIZES * const s_iemAImpl_tzcnt_eflags[2][4] =
    {
        { &s_iemAImpl_tzcnt_intel, &s_iemAImpl_tzcnt_intel, &s_iemAImpl_tzcnt_amd, &s_iemAImpl_tzcnt_intel },
        { &s_iemAImpl_tzcnt,       &s_iemAImpl_tzcnt_intel, &s_iemAImpl_tzcnt_amd, &s_iemAImpl_tzcnt }
    };
#endif
    IEMOP_VERIFICATION_UNDEFINED_EFLAGS(X86_EFL_OF | X86_EFL_SF | X86_EFL_AF | X86_EFL_PF);
    const IEMOPBINSIZES * const pImpl = IEMTARGETCPU_EFL_BEHAVIOR_SELECT_EX(s_iemAImpl_tzcnt_eflags,
                                                                            IEM_GET_HOST_CPU_FEATURES(pVCpu)->fBmi1);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    IEMOP_BODY_BINARY_rv_rm(bRm, pImpl->pfnNormalU16, pImpl->pfnNormalU32, pImpl->pfnNormalU64, IEM_MC_F_NOT_286_OR_OLDER, tzcnt, 0);
}


/**
 * @opcode      0xbd
 * @oppfx       !0xf3
 * @opfltest    cf,pf,af,sf,of
 * @opflmodify  cf,pf,af,zf,sf,of
 * @opflundef   cf,pf,af,sf,of
 * @todo AMD doesn't modify cf,pf,af,sf&of but since intel does, we're forced to
 *       document them as inputs. Sigh.
 */
FNIEMOP_DEF(iemOp_bsr_Gv_Ev)
{
    IEMOP_MNEMONIC(bsr_Gv_Ev, "bsr Gv,Ev");
    IEMOP_HLP_MIN_386();
    IEMOP_VERIFICATION_UNDEFINED_EFLAGS(X86_EFL_OF | X86_EFL_SF | X86_EFL_AF | X86_EFL_PF | X86_EFL_CF);
    PCIEMOPBINSIZES const pImpl = IEMTARGETCPU_EFL_BEHAVIOR_SELECT(g_iemAImpl_bsr_eflags);
    IEMOP_BODY_BIT_SCAN_OPERATOR_RV_RM(pImpl);
}


/**
 * @opcode      0xbd
 * @oppfx       0xf3
 * @opfltest    pf,af,sf,of
 * @opflmodify  cf,pf,af,zf,sf,of
 * @opflundef   pf,af,sf,of
 * @todo AMD doesn't modify pf,af,sf&of but since intel does, we're forced to
 *       document them as inputs. Sigh.
 */
FNIEMOP_DEF(iemOp_lzcnt_Gv_Ev)
{
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fAbm)
        return FNIEMOP_CALL(iemOp_bsr_Gv_Ev);
    IEMOP_MNEMONIC2(RM, LZCNT, lzcnt, Gv, Ev, DISOPTYPE_HARMLESS, 0);

#ifndef TST_IEM_CHECK_MC
    static const IEMOPBINSIZES s_iemAImpl_lzcnt =
    {   NULL, NULL,     iemAImpl_lzcnt_u16, NULL,       iemAImpl_lzcnt_u32, NULL,       iemAImpl_lzcnt_u64, NULL };
    static const IEMOPBINSIZES s_iemAImpl_lzcnt_amd =
    {   NULL,  NULL,    iemAImpl_lzcnt_u16_amd, NULL,   iemAImpl_lzcnt_u32_amd, NULL,   iemAImpl_lzcnt_u64_amd, NULL };
    static const IEMOPBINSIZES s_iemAImpl_lzcnt_intel =
    {   NULL,  NULL,    iemAImpl_lzcnt_u16_intel, NULL, iemAImpl_lzcnt_u32_intel, NULL, iemAImpl_lzcnt_u64_intel, NULL };
    static const IEMOPBINSIZES * const s_iemAImpl_lzcnt_eflags[2][4] =
    {
        { &s_iemAImpl_lzcnt_intel, &s_iemAImpl_lzcnt_intel, &s_iemAImpl_lzcnt_amd, &s_iemAImpl_lzcnt_intel },
        { &s_iemAImpl_lzcnt,       &s_iemAImpl_lzcnt_intel, &s_iemAImpl_lzcnt_amd, &s_iemAImpl_lzcnt }
    };
#endif
    IEMOP_VERIFICATION_UNDEFINED_EFLAGS(X86_EFL_OF | X86_EFL_SF | X86_EFL_AF | X86_EFL_PF);
    const IEMOPBINSIZES * const pImpl = IEMTARGETCPU_EFL_BEHAVIOR_SELECT_EX(s_iemAImpl_lzcnt_eflags,
                                                                            IEM_GET_HOST_CPU_FEATURES(pVCpu)->fBmi1);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    IEMOP_BODY_BINARY_rv_rm(bRm, pImpl->pfnNormalU16, pImpl->pfnNormalU32, pImpl->pfnNormalU64, IEM_MC_F_NOT_286_OR_OLDER, lzcnt, 0);
}



/** Opcode 0x0f 0xbe. */
FNIEMOP_DEF(iemOp_movsx_Gv_Eb)
{
    IEMOP_MNEMONIC(movsx_Gv_Eb, "movsx Gv,Eb");
    IEMOP_HLP_MIN_386();

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);

    /*
     * If rm is denoting a register, no more instruction bytes.
     */
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        switch (pVCpu->iem.s.enmEffOpSize)
        {
            case IEMMODE_16BIT:
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_LOCAL(uint16_t, u16Value);
                IEM_MC_FETCH_GREG_U8_SX_U16(u16Value, IEM_GET_MODRM_RM(pVCpu, bRm));
                IEM_MC_STORE_GREG_U16(IEM_GET_MODRM_REG(pVCpu, bRm), u16Value);
                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            case IEMMODE_32BIT:
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_LOCAL(uint32_t, u32Value);
                IEM_MC_FETCH_GREG_U8_SX_U32(u32Value, IEM_GET_MODRM_RM(pVCpu, bRm));
                IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), u32Value);
                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            case IEMMODE_64BIT:
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_LOCAL(uint64_t, u64Value);
                IEM_MC_FETCH_GREG_U8_SX_U64(u64Value, IEM_GET_MODRM_RM(pVCpu, bRm));
                IEM_MC_STORE_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), u64Value);
                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            IEM_NOT_REACHED_DEFAULT_CASE_RET();
        }
    }
    else
    {
        /*
         * We're loading a register from memory.
         */
        switch (pVCpu->iem.s.enmEffOpSize)
        {
            case IEMMODE_16BIT:
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
                IEM_MC_LOCAL(uint16_t, u16Value);
                IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_FETCH_MEM_U8_SX_U16(u16Value, pVCpu->iem.s.iEffSeg, GCPtrEffDst);
                IEM_MC_STORE_GREG_U16(IEM_GET_MODRM_REG(pVCpu, bRm), u16Value);
                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            case IEMMODE_32BIT:
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
                IEM_MC_LOCAL(uint32_t, u32Value);
                IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_FETCH_MEM_U8_SX_U32(u32Value, pVCpu->iem.s.iEffSeg, GCPtrEffDst);
                IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), u32Value);
                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            case IEMMODE_64BIT:
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
                IEM_MC_LOCAL(uint64_t, u64Value);
                IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_FETCH_MEM_U8_SX_U64(u64Value, pVCpu->iem.s.iEffSeg, GCPtrEffDst);
                IEM_MC_STORE_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), u64Value);
                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            IEM_NOT_REACHED_DEFAULT_CASE_RET();
        }
    }
}


/** Opcode 0x0f 0xbf. */
FNIEMOP_DEF(iemOp_movsx_Gv_Ew)
{
    IEMOP_MNEMONIC(movsx_Gv_Ew, "movsx Gv,Ew");
    IEMOP_HLP_MIN_386();

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);

    /** @todo Not entirely sure how the operand size prefix is handled here,
     *        assuming that it will be ignored. Would be nice to have a few
     *        test for this. */
    /*
     * If rm is denoting a register, no more instruction bytes.
     */
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        if (pVCpu->iem.s.enmEffOpSize != IEMMODE_64BIT)
        {
            IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
            IEM_MC_LOCAL(uint32_t, u32Value);
            IEM_MC_FETCH_GREG_U16_SX_U32(u32Value, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), u32Value);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
            IEM_MC_LOCAL(uint64_t, u64Value);
            IEM_MC_FETCH_GREG_U16_SX_U64(u64Value, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_STORE_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), u64Value);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        /*
         * We're loading a register from memory.
         */
        if (pVCpu->iem.s.enmEffOpSize != IEMMODE_64BIT)
        {
            IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
            IEM_MC_LOCAL(uint32_t, u32Value);
            IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
            IEM_MC_FETCH_MEM_U16_SX_U32(u32Value, pVCpu->iem.s.iEffSeg, GCPtrEffDst);
            IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), u32Value);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_LOCAL(uint64_t, u64Value);
            IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
            IEM_MC_FETCH_MEM_U16_SX_U64(u64Value, pVCpu->iem.s.iEffSeg, GCPtrEffDst);
            IEM_MC_STORE_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), u64Value);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/**
 * @opcode      0xc0
 * @opflclass   arithmetic
 */
FNIEMOP_DEF(iemOp_xadd_Eb_Gb)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    IEMOP_HLP_MIN_486();
    IEMOP_MNEMONIC(xadd_Eb_Gb, "xadd Eb,Gb");

    /*
     * If rm is denoting a register, no more instruction bytes.
     */
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        IEM_MC_BEGIN(IEM_MC_F_MIN_486, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_ARG(uint8_t *,  pu8Dst,  0);
        IEM_MC_ARG(uint8_t *,  pu8Reg,  1);
        IEM_MC_ARG(uint32_t *, pEFlags, 2);

        IEM_MC_REF_GREG_U8(pu8Dst, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_REF_GREG_U8(pu8Reg, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_EFLAGS(pEFlags);
        IEM_MC_CALL_VOID_AIMPL_3(iemAImpl_xadd_u8, pu8Dst, pu8Reg, pEFlags);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * We're accessing memory.
         */
#define IEMOP_BODY_XADD_BYTE(a_fnWorker, a_Type) \
            IEM_MC_BEGIN(IEM_MC_F_MIN_486, 0); \
            IEM_MC_LOCAL(RTGCPTR,           GCPtrEffDst); \
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0); \
            IEMOP_HLP_DONE_DECODING(); \
            \
            IEM_MC_LOCAL(uint8_t,           bUnmapInfo); \
            IEM_MC_ARG(uint8_t *,           pu8Dst,                 0); \
            IEM_MC_MEM_MAP_U8_##a_Type(pu8Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
            \
            IEM_MC_LOCAL(uint8_t,           u8RegCopy); \
            IEM_MC_FETCH_GREG_U8(u8RegCopy, IEM_GET_MODRM_REG(pVCpu, bRm)); \
            IEM_MC_ARG_LOCAL_REF(uint8_t *, pu8Reg,     u8RegCopy,  1); \
            \
            IEM_MC_ARG_LOCAL_EFLAGS(        pEFlags,    EFlags,     2); \
            IEM_MC_CALL_VOID_AIMPL_3(a_fnWorker, pu8Dst, pu8Reg, pEFlags); \
            \
            IEM_MC_MEM_COMMIT_AND_UNMAP_##a_Type(bUnmapInfo); \
            IEM_MC_COMMIT_EFLAGS(EFlags); \
            IEM_MC_STORE_GREG_U8(IEM_GET_MODRM_REG(pVCpu, bRm), u8RegCopy); \
            IEM_MC_ADVANCE_RIP_AND_FINISH(); \
            IEM_MC_END()
        if (!(pVCpu->iem.s.fPrefixes & IEM_OP_PRF_LOCK) || (pVCpu->iem.s.fExec & IEM_F_X86_DISREGARD_LOCK))
        {
            IEMOP_BODY_XADD_BYTE(iemAImpl_xadd_u8,RW);
        }
        else
        {
            IEMOP_BODY_XADD_BYTE(iemAImpl_xadd_u8_locked,ATOMIC);
        }
    }
}


/**
 * @opcode      0xc1
 * @opflclass   arithmetic
 */
FNIEMOP_DEF(iemOp_xadd_Ev_Gv)
{
    IEMOP_MNEMONIC(xadd_Ev_Gv, "xadd Ev,Gv");
    IEMOP_HLP_MIN_486();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);

    /*
     * If rm is denoting a register, no more instruction bytes.
     */
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        switch (pVCpu->iem.s.enmEffOpSize)
        {
            case IEMMODE_16BIT:
                IEM_MC_BEGIN(IEM_MC_F_MIN_486, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_ARG(uint16_t *, pu16Dst,  0);
                IEM_MC_ARG(uint16_t *, pu16Reg,  1);
                IEM_MC_ARG(uint32_t *, pEFlags, 2);

                IEM_MC_REF_GREG_U16(pu16Dst, IEM_GET_MODRM_RM(pVCpu, bRm));
                IEM_MC_REF_GREG_U16(pu16Reg, IEM_GET_MODRM_REG(pVCpu, bRm));
                IEM_MC_REF_EFLAGS(pEFlags);
                IEM_MC_CALL_VOID_AIMPL_3(iemAImpl_xadd_u16, pu16Dst, pu16Reg, pEFlags);

                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            case IEMMODE_32BIT:
                IEM_MC_BEGIN(IEM_MC_F_MIN_486, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_ARG(uint32_t *, pu32Dst,  0);
                IEM_MC_ARG(uint32_t *, pu32Reg,  1);
                IEM_MC_ARG(uint32_t *, pEFlags, 2);

                IEM_MC_REF_GREG_U32(pu32Dst, IEM_GET_MODRM_RM(pVCpu, bRm));
                IEM_MC_REF_GREG_U32(pu32Reg, IEM_GET_MODRM_REG(pVCpu, bRm));
                IEM_MC_REF_EFLAGS(pEFlags);
                IEM_MC_CALL_VOID_AIMPL_3(iemAImpl_xadd_u32, pu32Dst, pu32Reg, pEFlags);

                IEM_MC_CLEAR_HIGH_GREG_U64(IEM_GET_MODRM_RM(pVCpu, bRm));
                IEM_MC_CLEAR_HIGH_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm));
                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            case IEMMODE_64BIT:
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_ARG(uint64_t *, pu64Dst,  0);
                IEM_MC_ARG(uint64_t *, pu64Reg,  1);
                IEM_MC_ARG(uint32_t *, pEFlags, 2);

                IEM_MC_REF_GREG_U64(pu64Dst, IEM_GET_MODRM_RM(pVCpu, bRm));
                IEM_MC_REF_GREG_U64(pu64Reg, IEM_GET_MODRM_REG(pVCpu, bRm));
                IEM_MC_REF_EFLAGS(pEFlags);
                IEM_MC_CALL_VOID_AIMPL_3(iemAImpl_xadd_u64, pu64Dst, pu64Reg, pEFlags);

                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            IEM_NOT_REACHED_DEFAULT_CASE_RET();
        }
    }
    else
    {
        /*
         * We're accessing memory.
         */
#define IEMOP_BODY_XADD_EV_GV(a_fnWorker16, a_fnWorker32, a_fnWorker64, a_Type) \
        do { \
            switch (pVCpu->iem.s.enmEffOpSize) \
            { \
                case IEMMODE_16BIT: \
                    IEM_MC_BEGIN(IEM_MC_F_MIN_486, 0); \
                    IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0); \
                    IEMOP_HLP_DONE_DECODING(); \
                    \
                    IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                    IEM_MC_ARG(uint16_t *,              pu16Dst,                0); \
                    IEM_MC_MEM_MAP_U16_##a_Type(pu16Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                    \
                    IEM_MC_LOCAL(uint16_t,              u16RegCopy); \
                    IEM_MC_FETCH_GREG_U16(u16RegCopy, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_ARG_LOCAL_REF(uint16_t *,    pu16Reg, u16RegCopy,    1); \
                    \
                    IEM_MC_ARG_LOCAL_EFLAGS(            pEFlags, EFlags,        2); \
                    IEM_MC_CALL_VOID_AIMPL_3(a_fnWorker16, pu16Dst, pu16Reg, pEFlags); \
                    \
                    IEM_MC_MEM_COMMIT_AND_UNMAP_##a_Type(bUnmapInfo); \
                    IEM_MC_COMMIT_EFLAGS(EFlags); \
                    IEM_MC_STORE_GREG_U16(IEM_GET_MODRM_REG(pVCpu, bRm), u16RegCopy); \
                    IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                    IEM_MC_END(); \
                    break; \
                \
                case IEMMODE_32BIT: \
                    IEM_MC_BEGIN(IEM_MC_F_MIN_486, 0); \
                    IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0); \
                    IEMOP_HLP_DONE_DECODING(); \
                    \
                    IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                    IEM_MC_ARG(uint32_t *,              pu32Dst,                0); \
                    IEM_MC_MEM_MAP_U32_##a_Type(pu32Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                    \
                    IEM_MC_LOCAL(uint32_t,              u32RegCopy); \
                    IEM_MC_FETCH_GREG_U32(u32RegCopy, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_ARG_LOCAL_REF(uint32_t *,    pu32Reg, u32RegCopy,    1); \
                    \
                    IEM_MC_ARG_LOCAL_EFLAGS(            pEFlags, EFlags,        2); \
                    IEM_MC_CALL_VOID_AIMPL_3(a_fnWorker32, pu32Dst, pu32Reg, pEFlags); \
                    \
                    IEM_MC_MEM_COMMIT_AND_UNMAP_##a_Type(bUnmapInfo); \
                    IEM_MC_COMMIT_EFLAGS(EFlags); \
                    IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), u32RegCopy); \
                    IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                    IEM_MC_END(); \
                    break; \
                \
                case IEMMODE_64BIT: \
                    IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
                    IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
                    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0); \
                    IEMOP_HLP_DONE_DECODING(); \
                    \
                    IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
                    IEM_MC_ARG(uint64_t *,              pu64Dst,                0); \
                    IEM_MC_MEM_MAP_U64_##a_Type(pu64Dst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
                    \
                    IEM_MC_LOCAL(uint64_t,              u64RegCopy); \
                    IEM_MC_FETCH_GREG_U64(u64RegCopy, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                    IEM_MC_ARG_LOCAL_REF(uint64_t *,    pu64Reg, u64RegCopy,    1); \
                    \
                    IEM_MC_ARG_LOCAL_EFLAGS(            pEFlags, EFlags,        2); \
                    IEM_MC_CALL_VOID_AIMPL_3(a_fnWorker64, pu64Dst, pu64Reg, pEFlags); \
                    \
                    IEM_MC_MEM_COMMIT_AND_UNMAP_##a_Type(bUnmapInfo); \
                    IEM_MC_COMMIT_EFLAGS(EFlags); \
                    IEM_MC_STORE_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), u64RegCopy); \
                    IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                    IEM_MC_END(); \
                    break; \
                \
                IEM_NOT_REACHED_DEFAULT_CASE_RET(); \
            } \
        } while (0)

        if (!(pVCpu->iem.s.fPrefixes & IEM_OP_PRF_LOCK) || (pVCpu->iem.s.fExec & IEM_F_X86_DISREGARD_LOCK))
        {
            IEMOP_BODY_XADD_EV_GV(iemAImpl_xadd_u16, iemAImpl_xadd_u32, iemAImpl_xadd_u64,RW);
        }
        else
        {
            IEMOP_BODY_XADD_EV_GV(iemAImpl_xadd_u16_locked, iemAImpl_xadd_u32_locked, iemAImpl_xadd_u64_locked,ATOMIC);
        }
    }
}


/** Opcode      0x0f 0xc2 - cmpps Vps,Wps,Ib */
FNIEMOP_DEF(iemOp_cmpps_Vps_Wps_Ib)
{
    IEMOP_MNEMONIC3(RMI, CMPPS, cmpps, Vps, Wps, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM, XMM.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_LOCAL(IEMMEDIAF2XMMSRC,              Src);
        IEM_MC_LOCAL(X86XMMREG,                     Dst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,            pDst,           Dst,    0);
        IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2XMMSRC,    pSrc,           Src,    1);
        IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/ bImm,    2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_FETCH_XREG_PAIR_XMM(Src, IEM_GET_MODRM_REG(pVCpu, bRm), IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_SSE_AIMPL_3(iemAImpl_cmpps_u128, pDst, pSrc, bImmArg);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), Dst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM, [mem128].
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(IEMMEDIAF2XMMSRC,              Src);
        IEM_MC_LOCAL(X86XMMREG,                     Dst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,            pDst,           Dst,    0);
        IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2XMMSRC,    pSrc,           Src,    1);
        IEM_MC_LOCAL(RTGCPTR,                       GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/ bImm,    2);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_FETCH_MEM_XMM_ALIGN_SSE_AND_XREG_XMM(Src, IEM_GET_MODRM_REG(pVCpu, bRm), pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_CALL_SSE_AIMPL_3(iemAImpl_cmpps_u128, pDst, pSrc, bImmArg);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), Dst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode 0x66 0x0f 0xc2 - cmppd Vpd,Wpd,Ib */
FNIEMOP_DEF(iemOp_cmppd_Vpd_Wpd_Ib)
{
    IEMOP_MNEMONIC3(RMI, CMPPD, cmppd, Vpd, Wpd, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM, XMM.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_LOCAL(IEMMEDIAF2XMMSRC,              Src);
        IEM_MC_LOCAL(X86XMMREG,                     Dst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,            pDst,           Dst,    0);
        IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2XMMSRC,    pSrc,           Src,    1);
        IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/ bImm,    2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_FETCH_XREG_PAIR_XMM(Src, IEM_GET_MODRM_REG(pVCpu, bRm), IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_SSE_AIMPL_3(iemAImpl_cmppd_u128, pDst, pSrc, bImmArg);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), Dst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM, [mem128].
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(IEMMEDIAF2XMMSRC,              Src);
        IEM_MC_LOCAL(X86XMMREG,                     Dst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,            pDst,           Dst,    0);
        IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2XMMSRC,    pSrc,           Src,    1);
        IEM_MC_LOCAL(RTGCPTR,                       GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/ bImm,    2);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_FETCH_MEM_XMM_ALIGN_SSE_AND_XREG_XMM(Src, IEM_GET_MODRM_REG(pVCpu, bRm), pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_CALL_SSE_AIMPL_3(iemAImpl_cmppd_u128, pDst, pSrc, bImmArg);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), Dst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode 0xf3 0x0f 0xc2 - cmpss Vss,Wss,Ib */
FNIEMOP_DEF(iemOp_cmpss_Vss_Wss_Ib)
{
    IEMOP_MNEMONIC3(RMI, CMPSS, cmpss, Vss, Wss, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM32, XMM32.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_LOCAL(IEMMEDIAF2XMMSRC,              Src);
        IEM_MC_LOCAL(X86XMMREG,                     Dst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,            pDst,           Dst,    0);
        IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2XMMSRC,    pSrc,           Src,    1);
        IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/ bImm,    2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_FETCH_XREG_PAIR_XMM(Src, IEM_GET_MODRM_REG(pVCpu, bRm), IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_SSE_AIMPL_3(iemAImpl_cmpss_u128, pDst, pSrc, bImmArg);
        IEM_MC_STORE_XREG_XMM_U32(IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iDword*/, Dst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM32, [mem32].
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(IEMMEDIAF2XMMSRC,              Src);
        IEM_MC_LOCAL(X86XMMREG,                     Dst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,            pDst,           Dst,    0);
        IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2XMMSRC,    pSrc,           Src,    1);
        IEM_MC_LOCAL(RTGCPTR,                       GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/ bImm,    2);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_FETCH_MEM_XMM_U32_AND_XREG_XMM(Src, IEM_GET_MODRM_REG(pVCpu, bRm),
                                              0 /*a_iDword*/, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_CALL_SSE_AIMPL_3(iemAImpl_cmpss_u128, pDst, pSrc, bImmArg);
        IEM_MC_STORE_XREG_XMM_U32(IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iDword*/, Dst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode 0xf2 0x0f 0xc2 - cmpsd Vsd,Wsd,Ib */
FNIEMOP_DEF(iemOp_cmpsd_Vsd_Wsd_Ib)
{
    IEMOP_MNEMONIC3(RMI, CMPSD, cmpsd, Vsd, Wsd, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM64, XMM64.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_LOCAL(IEMMEDIAF2XMMSRC,              Src);
        IEM_MC_LOCAL(X86XMMREG,                     Dst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,            pDst,           Dst,    0);
        IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2XMMSRC,    pSrc,           Src,    1);
        IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/ bImm,    2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_FETCH_XREG_PAIR_XMM(Src, IEM_GET_MODRM_REG(pVCpu, bRm), IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_SSE_AIMPL_3(iemAImpl_cmpsd_u128, pDst, pSrc, bImmArg);
        IEM_MC_STORE_XREG_XMM_U64(IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iQword*/, Dst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM64, [mem64].
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(IEMMEDIAF2XMMSRC,              Src);
        IEM_MC_LOCAL(X86XMMREG,                     Dst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,            pDst,           Dst,    0);
        IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2XMMSRC,    pSrc,           Src,    1);
        IEM_MC_LOCAL(RTGCPTR,                       GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/ bImm,    2);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_FETCH_MEM_XMM_U64_AND_XREG_XMM(Src, IEM_GET_MODRM_REG(pVCpu, bRm),
                                              0 /*a_iQword */, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_CALL_SSE_AIMPL_3(iemAImpl_cmpsd_u128, pDst, pSrc, bImmArg);
        IEM_MC_STORE_XREG_XMM_U64(IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iQword*/, Dst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode 0x0f 0xc3. */
FNIEMOP_DEF(iemOp_movnti_My_Gy)
{
    IEMOP_MNEMONIC(movnti_My_Gy, "movnti My,Gy");

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);

    /* Only the register -> memory form makes sense, assuming #UD for the other form. */
    if (IEM_IS_MODRM_MEM_MODE(bRm))
    {
        switch (pVCpu->iem.s.enmEffOpSize)
        {
            case IEMMODE_32BIT:
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
                IEM_MC_LOCAL(uint32_t, u32Value);
                IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);

                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);

                IEM_MC_FETCH_GREG_U32(u32Value, IEM_GET_MODRM_REG(pVCpu, bRm));
                IEM_MC_STORE_MEM_U32(pVCpu->iem.s.iEffSeg, GCPtrEffDst, u32Value);
                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            case IEMMODE_64BIT:
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
                IEM_MC_LOCAL(uint64_t, u64Value);
                IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);

                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);

                IEM_MC_FETCH_GREG_U64(u64Value, IEM_GET_MODRM_REG(pVCpu, bRm));
                IEM_MC_STORE_MEM_U64(pVCpu->iem.s.iEffSeg, GCPtrEffDst, u64Value);
                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            case IEMMODE_16BIT:
                /** @todo check this form.   */
                IEMOP_RAISE_INVALID_OPCODE_RET();

            IEM_NOT_REACHED_DEFAULT_CASE_RET();
        }
    }
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}


/*  Opcode 0x66 0x0f 0xc3 - invalid */
/*  Opcode 0xf3 0x0f 0xc3 - invalid */
/*  Opcode 0xf2 0x0f 0xc3 - invalid */


/** Opcode      0x0f 0xc4 - pinsrw Pq, Ry/Mw,Ib */
FNIEMOP_DEF(iemOp_pinsrw_Pq_RyMw_Ib)
{
    IEMOP_MNEMONIC3(RMI, PINSRW, pinsrw, Pq, Ey, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, 0);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(uint16_t,    uValue);

        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX_2_OR(fSse, fAmdMmxExts);
        IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_FETCH_GREG_U16(uValue, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_STORE_MREG_U16(IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 3, uValue);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTGCPTR,     GCPtrEffSrc);
        IEM_MC_LOCAL(uint16_t,    uValue);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX_2_OR(fSse, fAmdMmxExts);
        IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
        IEM_MC_PREPARE_FPU_USAGE();

        IEM_MC_FETCH_MEM_U16(uValue, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_FPU_TO_MMX_MODE();
        IEM_MC_STORE_MREG_U16(IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 3, uValue);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode 0x66 0x0f 0xc4 - pinsrw Vdq, Ry/Mw,Ib */
FNIEMOP_DEF(iemOp_pinsrw_Vdq_RyMw_Ib)
{
    IEMOP_MNEMONIC3(RMI, PINSRW, pinsrw, Vq, Ey, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);

        IEM_MC_LOCAL(uint16_t,           uValue);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_FETCH_GREG_U16(uValue, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_STORE_XREG_U16(IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 7, uValue);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTGCPTR,            GCPtrEffSrc);
        IEM_MC_LOCAL(uint16_t,           uValue);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_FETCH_MEM_U16(uValue, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_XREG_U16(IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 7, uValue);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/*  Opcode 0xf3 0x0f 0xc4 - invalid */
/*  Opcode 0xf2 0x0f 0xc4 - invalid */


/** Opcode      0x0f 0xc5 - pextrw Gd, Nq, Ib */
FNIEMOP_DEF(iemOp_pextrw_Gd_Nq_Ib)
{
    /*IEMOP_MNEMONIC3(RMI_REG, PEXTRW, pextrw, Gd, Nq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, 0);*/ /** @todo */
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Greg32, MMX, imm8.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX_2_OR(fSse, fAmdMmxExts);
        IEM_MC_LOCAL(uint16_t,  uValue);
        IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();
        IEM_MC_FETCH_MREG_U16(uValue, IEM_GET_MODRM_RM_8(bRm), bImm & 3);
        IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), uValue);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    /* No memory operand. */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}


/** Opcode 0x66 0x0f 0xc5 - pextrw Gd, Udq, Ib */
FNIEMOP_DEF(iemOp_pextrw_Gd_Udq_Ib)
{
    IEMOP_MNEMONIC3(RMI_REG, PEXTRW, pextrw, Gd, Ux, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Greg32, XMM, imm8.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_LOCAL(uint16_t,  uValue);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_FETCH_XREG_U16(uValue, IEM_GET_MODRM_RM(pVCpu, bRm), bImm & 7);
        IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), uValue);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    /* No memory operand. */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}


/*  Opcode 0xf3 0x0f 0xc5 - invalid */
/*  Opcode 0xf2 0x0f 0xc5 - invalid */


/** Opcode      0x0f 0xc6 - shufps Vps, Wps, Ib */
FNIEMOP_DEF(iemOp_shufps_Vps_Wps_Ib)
{
    IEMOP_MNEMONIC3(RMI, SHUFPS, shufps, Vps, Wps, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM, XMM, imm8.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_ARG(PRTUINT128U,          pDst, 0);
        IEM_MC_ARG(PCRTUINT128U,         pSrc, 1);
        IEM_MC_ARG_CONST(uint8_t,        bImmArg, /*=*/ bImm, 2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(pDst, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(pSrc, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_3(iemAImpl_shufps_u128, pDst, pSrc, bImmArg);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM, [mem128], imm8.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(PRTUINT128U,                 pDst,       0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,      pSrc, uSrc, 1);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_ARG_CONST(uint8_t,               bImmArg, /*=*/ bImm, 2);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(pDst, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_3(iemAImpl_shufps_u128, pDst, pSrc, bImmArg);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode 0x66 0x0f 0xc6 - shufpd Vpd, Wpd, Ib */
FNIEMOP_DEF(iemOp_shufpd_Vpd_Wpd_Ib)
{
    IEMOP_MNEMONIC3(RMI, SHUFPD, shufpd, Vpd, Wpd, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM, XMM, imm8.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_ARG(PRTUINT128U,          pDst, 0);
        IEM_MC_ARG(PCRTUINT128U,         pSrc, 1);
        IEM_MC_ARG_CONST(uint8_t,       bImmArg, /*=*/ bImm, 2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(pDst, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(pSrc, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_3(iemAImpl_shufpd_u128, pDst, pSrc, bImmArg);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM, [mem128], imm8.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(PRTUINT128U,                 pDst,       0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,      pSrc, uSrc, 1);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_ARG_CONST(uint8_t,               bImmArg, /*=*/ bImm, 2);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(pDst, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_3(iemAImpl_shufpd_u128, pDst, pSrc, bImmArg);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/*  Opcode 0xf3 0x0f 0xc6 - invalid */
/*  Opcode 0xf2 0x0f 0xc6 - invalid */


/**
 * @opmaps      grp9
 * @opcode      /1
 * @opcodesub   !11 mr/reg rex.w=0
 * @oppfx       n/a
 * @opflmodify  zf
 */
FNIEMOP_DEF_1(iemOp_Grp9_cmpxchg8b_Mq, uint8_t, bRm)
{
    IEMOP_MNEMONIC(cmpxchg8b, "cmpxchg8b Mq");
#define IEMOP_BODY_CMPXCHG8B(a_fnWorker, a_Type) \
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0); \
        IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0); \
        IEMOP_HLP_DONE_DECODING_EX(fCmpXchg8b); \
        \
        IEM_MC_LOCAL(uint8_t,               bUnmapInfo); \
        IEM_MC_ARG(uint64_t *,              pu64MemDst,             0); \
        IEM_MC_MEM_MAP_U64_##a_Type(pu64MemDst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
        \
        IEM_MC_LOCAL(RTUINT64U,             u64EaxEdx); \
        IEM_MC_FETCH_GREG_PAIR_U32(u64EaxEdx, X86_GREG_xAX, X86_GREG_xDX); \
        IEM_MC_ARG_LOCAL_REF(PRTUINT64U,    pu64EaxEdx, u64EaxEdx,  1); \
        \
        IEM_MC_LOCAL(RTUINT64U,             u64EbxEcx); \
        IEM_MC_FETCH_GREG_PAIR_U32(u64EbxEcx, X86_GREG_xBX, X86_GREG_xCX); \
        IEM_MC_ARG_LOCAL_REF(PRTUINT64U,    pu64EbxEcx, u64EbxEcx,  2); \
        \
        IEM_MC_ARG_LOCAL_EFLAGS(            pEFlags,    EFlags,     3); \
        IEM_MC_CALL_VOID_AIMPL_4(a_fnWorker, pu64MemDst, pu64EaxEdx, pu64EbxEcx, pEFlags); \
        \
        IEM_MC_MEM_COMMIT_AND_UNMAP_##a_Type(bUnmapInfo); \
        IEM_MC_COMMIT_EFLAGS(EFlags); \
        IEM_MC_IF_EFL_BIT_NOT_SET(X86_EFL_ZF) { \
            IEM_MC_STORE_GREG_PAIR_U32(X86_GREG_xAX, X86_GREG_xDX, u64EaxEdx); \
        } IEM_MC_ENDIF(); \
        IEM_MC_ADVANCE_RIP_AND_FINISH(); \
        \
        IEM_MC_END()
    if (!(pVCpu->iem.s.fPrefixes & IEM_OP_PRF_LOCK) || (pVCpu->iem.s.fExec & IEM_F_X86_DISREGARD_LOCK))
    {
        IEMOP_BODY_CMPXCHG8B(iemAImpl_cmpxchg8b,RW);
    }
    else
    {
        IEMOP_BODY_CMPXCHG8B(iemAImpl_cmpxchg8b_locked,ATOMIC);
    }
}


/**
 * @opmaps      grp9
 * @opcode      /1
 * @opcodesub   !11 mr/reg rex.w=1
 * @oppfx       n/a
 * @opflmodify  zf
 */
FNIEMOP_DEF_1(iemOp_Grp9_cmpxchg16b_Mdq, uint8_t, bRm)
{
    IEMOP_MNEMONIC(cmpxchg16b, "cmpxchg16b Mdq");
    if (IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fCmpXchg16b)
    {
        /*
         * This is hairy, very hairy macro fun.   We're walking a fine line
         * here to make the code parsable by IEMAllInstPython.py and fit into
         * the patterns IEMAllThrdPython.py requires for the code morphing.
         */
#define BODY_CMPXCHG16B_HEAD(bUnmapInfoStmt, a_Type) \
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
            IEM_MC_LOCAL(RTGCPTR,               GCPtrEffDst); \
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0); \
            IEMOP_HLP_DONE_DECODING(); \
            \
            IEM_MC_RAISE_GP0_IF_EFF_ADDR_UNALIGNED(GCPtrEffDst, 16); \
            bUnmapInfoStmt; \
            IEM_MC_ARG(PRTUINT128U,             pu128MemDst,                0); \
            IEM_MC_MEM_MAP_U128_##a_Type(pu128MemDst, bUnmapInfo, pVCpu->iem.s.iEffSeg, GCPtrEffDst); \
            \
            IEM_MC_LOCAL(RTUINT128U, u128RaxRdx); \
            IEM_MC_FETCH_GREG_PAIR_U64(u128RaxRdx, X86_GREG_xAX, X86_GREG_xDX); \
            IEM_MC_ARG_LOCAL_REF(PRTUINT128U,   pu128RaxRdx, u128RaxRdx,    1); \
            \
            IEM_MC_LOCAL(RTUINT128U, u128RbxRcx); \
            IEM_MC_FETCH_GREG_PAIR_U64(u128RbxRcx, X86_GREG_xBX, X86_GREG_xCX); \
            IEM_MC_ARG_LOCAL_REF(PRTUINT128U,   pu128RbxRcx, u128RbxRcx,    2); \
            \
            IEM_MC_ARG_LOCAL_EFLAGS(            pEFlags,     EFlags,        3)

#define BODY_CMPXCHG16B_TAIL(a_Type) \
            IEM_MC_MEM_COMMIT_AND_UNMAP_##a_Type(bUnmapInfo); \
            IEM_MC_COMMIT_EFLAGS(EFlags); \
            IEM_MC_IF_EFL_BIT_NOT_SET(X86_EFL_ZF) { \
                IEM_MC_STORE_GREG_PAIR_U64(X86_GREG_xAX, X86_GREG_xDX, u128RaxRdx); \
            } IEM_MC_ENDIF(); \
            IEM_MC_ADVANCE_RIP_AND_FINISH(); \
            IEM_MC_END()

#ifdef RT_ARCH_AMD64
        if (IEM_GET_HOST_CPU_FEATURES(pVCpu)->fCmpXchg16b)
        {
            if (!(pVCpu->iem.s.fPrefixes & IEM_OP_PRF_LOCK) || (pVCpu->iem.s.fExec & IEM_F_X86_DISREGARD_LOCK))
            {
                BODY_CMPXCHG16B_HEAD(IEM_MC_LOCAL(uint8_t, bUnmapInfo),RW);
                IEM_MC_CALL_VOID_AIMPL_4(iemAImpl_cmpxchg16b, pu128MemDst, pu128RaxRdx, pu128RbxRcx, pEFlags);
                BODY_CMPXCHG16B_TAIL(RW);
            }
            else
            {
                BODY_CMPXCHG16B_HEAD(IEM_MC_LOCAL(uint8_t, bUnmapInfo),ATOMIC);
                IEM_MC_CALL_VOID_AIMPL_4(iemAImpl_cmpxchg16b_locked, pu128MemDst, pu128RaxRdx, pu128RbxRcx, pEFlags);
                BODY_CMPXCHG16B_TAIL(ATOMIC);
            }
        }
        else
        {   /* (see comments in #else case below) */
            if (pVCpu->CTX_SUFF(pVM)->cCpus == 1)
            {
                BODY_CMPXCHG16B_HEAD(IEM_MC_LOCAL(uint8_t, bUnmapInfo),RW);
                IEM_MC_CALL_VOID_AIMPL_4(iemAImpl_cmpxchg16b_fallback, pu128MemDst, pu128RaxRdx, pu128RbxRcx, pEFlags);
                BODY_CMPXCHG16B_TAIL(RW);
            }
            else
            {
                BODY_CMPXCHG16B_HEAD(IEM_MC_ARG(uint8_t, bUnmapInfo, 4),RW);
                IEM_MC_CALL_CIMPL_5(IEM_CIMPL_F_STATUS_FLAGS,
                                      RT_BIT_64(kIemNativeGstReg_GprFirst + X86_GREG_xAX)
                                    | RT_BIT_64(kIemNativeGstReg_GprFirst + X86_GREG_xDX),
                                    iemCImpl_cmpxchg16b_fallback_rendezvous, pu128MemDst, pu128RaxRdx, pu128RbxRcx,
                                                                             pEFlags, bUnmapInfo);
                IEM_MC_END();
            }
        }

#elif defined(RT_ARCH_ARM64)
        /** @todo may require fallback for unaligned accesses... */
        if (!(pVCpu->iem.s.fPrefixes & IEM_OP_PRF_LOCK) || (pVCpu->iem.s.fExec & IEM_F_X86_DISREGARD_LOCK))
        {
            BODY_CMPXCHG16B_HEAD(IEM_MC_LOCAL(uint8_t, bUnmapInfo),RW);
            IEM_MC_CALL_VOID_AIMPL_4(iemAImpl_cmpxchg16b, pu128MemDst, pu128RaxRdx, pu128RbxRcx, pEFlags);
            BODY_CMPXCHG16B_TAIL(RW);
        }
        else
        {
            BODY_CMPXCHG16B_HEAD(IEM_MC_LOCAL(uint8_t, bUnmapInfo),ATOMIC);
            IEM_MC_CALL_VOID_AIMPL_4(iemAImpl_cmpxchg16b_locked, pu128MemDst, pu128RaxRdx, pu128RbxRcx, pEFlags);
            BODY_CMPXCHG16B_TAIL(ATOMIC);
        }

#else
        /* Note! The fallback for 32-bit systems and systems without CX16 is multiple
                 accesses and not all all atomic, which works fine on in UNI CPU guest
                 configuration (ignoring DMA).  If guest SMP is active we have no choice
                 but to use a rendezvous callback here.  Sigh. */
        if (pVCpu->CTX_SUFF(pVM)->cCpus == 1)
        {
            BODY_CMPXCHG16B_HEAD(IEM_MC_LOCAL(uint8_t, bUnmapInfo),RW);
            IEM_MC_CALL_VOID_AIMPL_4(iemAImpl_cmpxchg16b_fallback, pu128MemDst, pu128RaxRdx, pu128RbxRcx, pEFlags);
            BODY_CMPXCHG16B_TAIL(RW);
        }
        else
        {
            BODY_CMPXCHG16B_HEAD(IEM_MC_ARG(uint8_t, bUnmapInfo, 4),RW);
            IEM_MC_CALL_CIMPL_4(IEM_CIMPL_F_STATUS_FLAGS,
                                  RT_BIT_64(kIemNativeGstReg_GprFirst + X86_GREG_xAX)
                                | RT_BIT_64(kIemNativeGstReg_GprFirst + X86_GREG_xDX),
                                iemCImpl_cmpxchg16b_fallback_rendezvous,
                                pu128MemDst, pu128RaxRdx, pu128RbxRcx, pEFlags);
            IEM_MC_END();
            /* Does not get here, tail code is duplicated in iemCImpl_cmpxchg16b_fallback_rendezvous. */
        }
#endif

#undef BODY_CMPXCHG16B
    }
    Log(("cmpxchg16b -> #UD\n"));
    IEMOP_RAISE_INVALID_OPCODE_RET();
}

FNIEMOP_DEF_1(iemOp_Grp9_cmpxchg8bOr16b, uint8_t, bRm)
{
    if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_REX_W)
        return FNIEMOP_CALL_1(iemOp_Grp9_cmpxchg16b_Mdq, bRm);
    return FNIEMOP_CALL_1(iemOp_Grp9_cmpxchg8b_Mq, bRm);
}


/** Opcode 0x0f 0xc7 11/6. */
FNIEMOP_DEF_1(iemOp_Grp9_rdrand_Rv, uint8_t, bRm)
{
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fRdRand)
        IEMOP_RAISE_INVALID_OPCODE_RET();

    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_ARG_CONST(uint8_t, iReg,        /*=*/ IEM_GET_MODRM_RM(pVCpu, bRm), 0);
        IEM_MC_ARG_CONST(IEMMODE, enmEffOpSize,/*=*/ pVCpu->iem.s.enmEffOpSize,    1);
        IEM_MC_CALL_CIMPL_2(IEM_CIMPL_F_RFLAGS | IEM_CIMPL_F_VMEXIT,
                            RT_BIT_64(kIemNativeGstReg_GprFirst + IEM_GET_MODRM_RM(pVCpu, bRm)),
                            iemCImpl_rdrand, iReg, enmEffOpSize);
        IEM_MC_END();
    }
    /* Register only. */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}

/** Opcode 0x0f 0xc7 !11/6. */
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
FNIEMOP_DEF_1(iemOp_Grp9_vmptrld_Mq, uint8_t, bRm)
{
    IEMOP_MNEMONIC(vmptrld, "vmptrld");
    IEMOP_HLP_IN_VMX_OPERATION("vmptrld", kVmxVDiag_Vmptrld);
    IEMOP_HLP_VMX_INSTR("vmptrld", kVmxVDiag_Vmptrld);
    IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
    IEM_MC_ARG(RTGCPTR,         GCPtrEffSrc,                            1);
    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
    IEMOP_HLP_DONE_DECODING_NO_SIZE_OP_REPZ_OR_REPNZ_PREFIXES();
    IEM_MC_ARG_CONST(uint8_t,   iEffSeg, /*=*/ pVCpu->iem.s.iEffSeg,    0);
    IEM_MC_CALL_CIMPL_2(IEM_CIMPL_F_VMEXIT | IEM_CIMPL_F_STATUS_FLAGS, 0, iemCImpl_vmptrld, iEffSeg, GCPtrEffSrc);
    IEM_MC_END();
}
#else
FNIEMOP_UD_STUB_1(iemOp_Grp9_vmptrld_Mq, uint8_t, bRm);
#endif

/** Opcode 0x66 0x0f 0xc7 !11/6. */
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
FNIEMOP_DEF_1(iemOp_Grp9_vmclear_Mq, uint8_t, bRm)
{
    IEMOP_MNEMONIC(vmclear, "vmclear");
    IEMOP_HLP_IN_VMX_OPERATION("vmclear", kVmxVDiag_Vmclear);
    IEMOP_HLP_VMX_INSTR("vmclear", kVmxVDiag_Vmclear);
    IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
    IEM_MC_ARG(RTGCPTR,         GCPtrEffDst,                            1);
    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
    IEMOP_HLP_DONE_DECODING();
    IEM_MC_ARG_CONST(uint8_t,   iEffSeg, /*=*/ pVCpu->iem.s.iEffSeg,    0);
    IEM_MC_CALL_CIMPL_2(IEM_CIMPL_F_VMEXIT | IEM_CIMPL_F_STATUS_FLAGS, 0, iemCImpl_vmclear, iEffSeg, GCPtrEffDst);
    IEM_MC_END();
}
#else
FNIEMOP_UD_STUB_1(iemOp_Grp9_vmclear_Mq, uint8_t, bRm);
#endif

/** Opcode 0xf3 0x0f 0xc7 !11/6. */
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
FNIEMOP_DEF_1(iemOp_Grp9_vmxon_Mq, uint8_t, bRm)
{
    IEMOP_MNEMONIC(vmxon, "vmxon");
    IEMOP_HLP_VMX_INSTR("vmxon", kVmxVDiag_Vmxon);
    IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
    IEM_MC_ARG(RTGCPTR,         GCPtrEffSrc,                            1);
    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
    IEMOP_HLP_DONE_DECODING();
    IEM_MC_ARG_CONST(uint8_t,   iEffSeg, /*=*/ pVCpu->iem.s.iEffSeg,    0);
    IEM_MC_CALL_CIMPL_2(IEM_CIMPL_F_VMEXIT | IEM_CIMPL_F_STATUS_FLAGS, 0, iemCImpl_vmxon, iEffSeg, GCPtrEffSrc);
    IEM_MC_END();
}
#else
FNIEMOP_UD_STUB_1(iemOp_Grp9_vmxon_Mq, uint8_t, bRm);
#endif

/** Opcode [0xf3] 0x0f 0xc7 !11/7. */
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
FNIEMOP_DEF_1(iemOp_Grp9_vmptrst_Mq, uint8_t, bRm)
{
    IEMOP_MNEMONIC(vmptrst, "vmptrst");
    IEMOP_HLP_IN_VMX_OPERATION("vmptrst", kVmxVDiag_Vmptrst);
    IEMOP_HLP_VMX_INSTR("vmptrst", kVmxVDiag_Vmptrst);
    IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
    IEM_MC_ARG(RTGCPTR,         GCPtrEffDst,                            1);
    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
    IEMOP_HLP_DONE_DECODING_NO_SIZE_OP_REPZ_OR_REPNZ_PREFIXES();
    IEM_MC_ARG_CONST(uint8_t,   iEffSeg, /*=*/ pVCpu->iem.s.iEffSeg,    0);
    IEM_MC_CALL_CIMPL_2(IEM_CIMPL_F_VMEXIT | IEM_CIMPL_F_STATUS_FLAGS, 0, iemCImpl_vmptrst, iEffSeg, GCPtrEffDst);
    IEM_MC_END();
}
#else
FNIEMOP_UD_STUB_1(iemOp_Grp9_vmptrst_Mq, uint8_t, bRm);
#endif

/** Opcode 0x0f 0xc7 11/7. */
FNIEMOP_DEF_1(iemOp_Grp9_rdseed_Rv, uint8_t, bRm)
{
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fRdSeed)
        IEMOP_RAISE_INVALID_OPCODE_RET();

    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /* register destination. */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_ARG_CONST(uint8_t, iReg,        /*=*/ IEM_GET_MODRM_RM(pVCpu, bRm), 0);
        IEM_MC_ARG_CONST(IEMMODE, enmEffOpSize,/*=*/ pVCpu->iem.s.enmEffOpSize,    1);
        IEM_MC_CALL_CIMPL_2(IEM_CIMPL_F_RFLAGS | IEM_CIMPL_F_VMEXIT,
                            RT_BIT_64(kIemNativeGstReg_GprFirst + IEM_GET_MODRM_RM(pVCpu, bRm)),
                            iemCImpl_rdseed, iReg, enmEffOpSize);
        IEM_MC_END();
    }
    /* Register only. */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}

/**
 * Group 9 jump table for register variant.
 */
IEM_STATIC const PFNIEMOPRM g_apfnGroup9RegReg[] =
{   /* pfx:  none,                          066h,                           0f3h,                           0f2h */
    /* /0 */ IEMOP_X4(iemOp_InvalidWithRM),
    /* /1 */ IEMOP_X4(iemOp_InvalidWithRM),
    /* /2 */ IEMOP_X4(iemOp_InvalidWithRM),
    /* /3 */ IEMOP_X4(iemOp_InvalidWithRM),
    /* /4 */ IEMOP_X4(iemOp_InvalidWithRM),
    /* /5 */ IEMOP_X4(iemOp_InvalidWithRM),
    /* /6 */ iemOp_Grp9_rdrand_Rv,          iemOp_Grp9_rdrand_Rv,           iemOp_InvalidWithRM,            iemOp_InvalidWithRM,
    /* /7 */ iemOp_Grp9_rdseed_Rv,          iemOp_Grp9_rdseed_Rv,           iemOp_InvalidWithRM,            iemOp_InvalidWithRM,
};
AssertCompile(RT_ELEMENTS(g_apfnGroup9RegReg) == 8*4);


/**
 * Group 9 jump table for memory variant.
 */
IEM_STATIC const PFNIEMOPRM g_apfnGroup9MemReg[] =
{   /* pfx:  none,                          066h,                           0f3h,                           0f2h */
    /* /0 */ IEMOP_X4(iemOp_InvalidWithRM),
    /* /1 */ iemOp_Grp9_cmpxchg8bOr16b,     iemOp_Grp9_cmpxchg8bOr16b,      iemOp_Grp9_cmpxchg8bOr16b,      iemOp_Grp9_cmpxchg8bOr16b, /* see bs3-cpu-decoding-1 */
    /* /2 */ IEMOP_X4(iemOp_InvalidWithRM),
    /* /3 */ IEMOP_X4(iemOp_InvalidWithRM),
    /* /4 */ IEMOP_X4(iemOp_InvalidWithRM),
    /* /5 */ IEMOP_X4(iemOp_InvalidWithRM),
    /* /6 */ iemOp_Grp9_vmptrld_Mq,         iemOp_Grp9_vmclear_Mq,          iemOp_Grp9_vmxon_Mq,            iemOp_InvalidWithRM,
    /* /7 */ iemOp_Grp9_vmptrst_Mq,         iemOp_InvalidWithRM,            iemOp_InvalidWithRM,            iemOp_InvalidWithRM,
};
AssertCompile(RT_ELEMENTS(g_apfnGroup9MemReg) == 8*4);


/** Opcode 0x0f 0xc7. */
FNIEMOP_DEF(iemOp_Grp9)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
        /* register, register */
        return FNIEMOP_CALL_1(g_apfnGroup9RegReg[ IEM_GET_MODRM_REG_8(bRm) * 4
                                                 + pVCpu->iem.s.idxPrefix], bRm);
    /* memory, register */
    return FNIEMOP_CALL_1(g_apfnGroup9MemReg[ IEM_GET_MODRM_REG_8(bRm) * 4
                                             + pVCpu->iem.s.idxPrefix], bRm);
}


/**
 * Common 'bswap register' helper.
 */
FNIEMOP_DEF_1(iemOpCommonBswapGReg, uint8_t, iReg)
{
    switch (pVCpu->iem.s.enmEffOpSize)
    {
        case IEMMODE_16BIT:
            IEM_MC_BEGIN(IEM_MC_F_MIN_486, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
            IEM_MC_ARG(uint32_t *,  pu32Dst, 0);
            IEM_MC_REF_GREG_U32(pu32Dst, iReg);     /* Don't clear the high dword! */
            IEM_MC_CALL_VOID_AIMPL_1(iemAImpl_bswap_u16, pu32Dst);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
            break;

        case IEMMODE_32BIT:
            IEM_MC_BEGIN(IEM_MC_F_MIN_486, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
            IEM_MC_ARG(uint32_t *,  pu32Dst, 0);
            IEM_MC_REF_GREG_U32(pu32Dst, iReg);
            IEM_MC_CALL_VOID_AIMPL_1(iemAImpl_bswap_u32, pu32Dst);
            IEM_MC_CLEAR_HIGH_GREG_U64(iReg);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
            break;

        case IEMMODE_64BIT:
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
            IEM_MC_ARG(uint64_t *,  pu64Dst, 0);
            IEM_MC_REF_GREG_U64(pu64Dst, iReg);
            IEM_MC_CALL_VOID_AIMPL_1(iemAImpl_bswap_u64, pu64Dst);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
            break;

        IEM_NOT_REACHED_DEFAULT_CASE_RET();
    }
}


/** Opcode 0x0f 0xc8. */
FNIEMOP_DEF(iemOp_bswap_rAX_r8)
{
    IEMOP_MNEMONIC(bswap_rAX_r8, "bswap rAX/r8");
    /* Note! Intel manuals states that R8-R15 can be accessed by using a REX.X
             prefix.  REX.B is the correct prefix it appears.  For a parallel
             case, see iemOp_mov_AL_Ib and iemOp_mov_eAX_Iv. */
    IEMOP_HLP_MIN_486();
    return FNIEMOP_CALL_1(iemOpCommonBswapGReg, X86_GREG_xAX | pVCpu->iem.s.uRexB);
}


/** Opcode 0x0f 0xc9. */
FNIEMOP_DEF(iemOp_bswap_rCX_r9)
{
    IEMOP_MNEMONIC(bswap_rCX_r9, "bswap rCX/r9");
    IEMOP_HLP_MIN_486();
    return FNIEMOP_CALL_1(iemOpCommonBswapGReg, X86_GREG_xCX | pVCpu->iem.s.uRexB);
}


/** Opcode 0x0f 0xca. */
FNIEMOP_DEF(iemOp_bswap_rDX_r10)
{
    IEMOP_MNEMONIC(bswap_rDX_r9, "bswap rDX/r10");
    IEMOP_HLP_MIN_486();
    return FNIEMOP_CALL_1(iemOpCommonBswapGReg, X86_GREG_xDX | pVCpu->iem.s.uRexB);
}


/** Opcode 0x0f 0xcb. */
FNIEMOP_DEF(iemOp_bswap_rBX_r11)
{
    IEMOP_MNEMONIC(bswap_rBX_r9, "bswap rBX/r11");
    IEMOP_HLP_MIN_486();
    return FNIEMOP_CALL_1(iemOpCommonBswapGReg, X86_GREG_xBX | pVCpu->iem.s.uRexB);
}


/** Opcode 0x0f 0xcc. */
FNIEMOP_DEF(iemOp_bswap_rSP_r12)
{
    IEMOP_MNEMONIC(bswap_rSP_r12, "bswap rSP/r12");
    IEMOP_HLP_MIN_486();
    return FNIEMOP_CALL_1(iemOpCommonBswapGReg, X86_GREG_xSP | pVCpu->iem.s.uRexB);
}


/** Opcode 0x0f 0xcd. */
FNIEMOP_DEF(iemOp_bswap_rBP_r13)
{
    IEMOP_MNEMONIC(bswap_rBP_r13, "bswap rBP/r13");
    IEMOP_HLP_MIN_486();
    return FNIEMOP_CALL_1(iemOpCommonBswapGReg, X86_GREG_xBP | pVCpu->iem.s.uRexB);
}


/** Opcode 0x0f 0xce. */
FNIEMOP_DEF(iemOp_bswap_rSI_r14)
{
    IEMOP_MNEMONIC(bswap_rSI_r14, "bswap rSI/r14");
    IEMOP_HLP_MIN_486();
    return FNIEMOP_CALL_1(iemOpCommonBswapGReg, X86_GREG_xSI | pVCpu->iem.s.uRexB);
}


/** Opcode 0x0f 0xcf. */
FNIEMOP_DEF(iemOp_bswap_rDI_r15)
{
    IEMOP_MNEMONIC(bswap_rDI_r15, "bswap rDI/r15");
    IEMOP_HLP_MIN_486();
    return FNIEMOP_CALL_1(iemOpCommonBswapGReg, X86_GREG_xDI | pVCpu->iem.s.uRexB);
}


/*  Opcode      0x0f 0xd0 - invalid */


/** Opcode 0x66 0x0f 0xd0 - addsubpd Vpd, Wpd */
FNIEMOP_DEF(iemOp_addsubpd_Vpd_Wpd)
{
    IEMOP_MNEMONIC2(RM, ADDSUBPD, addsubpd, Vpd, Wpd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse3Fp_FullFull_To_Full, iemAImpl_addsubpd_u128);
}


/*  Opcode 0xf3 0x0f 0xd0 - invalid */


/** Opcode 0xf2 0x0f 0xd0 - addsubps Vps, Wps */
FNIEMOP_DEF(iemOp_addsubps_Vps_Wps)
{
    IEMOP_MNEMONIC2(RM, ADDSUBPS, addsubps, Vps, Wps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse3Fp_FullFull_To_Full, iemAImpl_addsubps_u128);
}



/** Opcode      0x0f 0xd1 - psrlw Pq, Qq */
FNIEMOP_DEF(iemOp_psrlw_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PSRLW, psrlw, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, 0);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_psrlw_u64);
}

/** Opcode 0x66 0x0f 0xd1 - psrlw Vx, Wx */
FNIEMOP_DEF(iemOp_psrlw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PSRLW, psrlw, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_psrlw_u128);
}

/*  Opcode 0xf3 0x0f 0xd1 - invalid */
/*  Opcode 0xf2 0x0f 0xd1 - invalid */

/** Opcode      0x0f 0xd2 - psrld Pq, Qq */
FNIEMOP_DEF(iemOp_psrld_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PSRLD, psrld, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, 0);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_psrld_u64);
}


/** Opcode 0x66 0x0f 0xd2 - psrld Vx, Wx */
FNIEMOP_DEF(iemOp_psrld_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PSRLD, psrld, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_psrld_u128);
}


/*  Opcode 0xf3 0x0f 0xd2 - invalid */
/*  Opcode 0xf2 0x0f 0xd2 - invalid */

/** Opcode      0x0f 0xd3 - psrlq Pq, Qq */
FNIEMOP_DEF(iemOp_psrlq_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PSRLQ, psrlq, Pq, Qq, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_psrlq_u64);
}


/** Opcode 0x66 0x0f 0xd3 - psrlq Vx, Wx */
FNIEMOP_DEF(iemOp_psrlq_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PSRLQ, psrlq, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_psrlq_u128);
}


/*  Opcode 0xf3 0x0f 0xd3 - invalid */
/*  Opcode 0xf2 0x0f 0xd3 - invalid */


/** Opcode      0x0f 0xd4 - paddq Pq, Qq */
FNIEMOP_DEF(iemOp_paddq_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PADDQ, paddq, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full_Sse2, iemAImpl_paddq_u64);
}


/** Opcode 0x66 0x0f 0xd4 - paddq Vx, Wx */
FNIEMOP_DEF(iemOp_paddq_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PADDQ, paddq, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    SSE2_OPT_BODY_FullFull_To_Full(paddq, iemAImpl_paddq_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/*  Opcode 0xf3 0x0f 0xd4 - invalid */
/*  Opcode 0xf2 0x0f 0xd4 - invalid */

/** Opcode      0x0f 0xd5 - pmullw Pq, Qq */
FNIEMOP_DEF(iemOp_pmullw_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PMULLW, pmullw, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_pmullw_u64);
}

/** Opcode 0x66 0x0f 0xd5 - pmullw Vx, Wx */
FNIEMOP_DEF(iemOp_pmullw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PMULLW, pmullw, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    SSE2_OPT_BODY_FullFull_To_Full(pmullw, iemAImpl_pmullw_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/*  Opcode 0xf3 0x0f 0xd5 - invalid */
/*  Opcode 0xf2 0x0f 0xd5 - invalid */

/*  Opcode      0x0f 0xd6 - invalid */

/**
 * @opcode      0xd6
 * @oppfx       0x66
 * @opcpuid     sse2
 * @opgroup     og_sse2_pcksclr_datamove
 * @opxcpttype  none
 * @optest      op1=-1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_movq_Wq_Vq)
{
    IEMOP_MNEMONIC2(MR, MOVQ, movq, WqZxReg_WO, Vq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_LOCAL(uint64_t,                  uSrc);

        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();

        IEM_MC_FETCH_XREG_U64(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /* a_iQword*/);
        IEM_MC_STORE_XREG_U64_ZX_U128(IEM_GET_MODRM_RM(pVCpu, bRm), uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Memory, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_READ();

        IEM_MC_FETCH_XREG_U64(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /* a_iQword*/);
        IEM_MC_STORE_MEM_U64(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0xd6
 * @opcodesub   11 mr/reg
 * @oppfx       f3
 * @opcpuid     sse2
 * @opgroup     og_sse2_simdint_datamove
 * @optest      op1=1 op2=2   -> op1=2   ftw=0xff
 * @optest      op1=0 op2=-42 -> op1=-42 ftw=0xff
 */
FNIEMOP_DEF(iemOp_movq2dq_Vdq_Nq)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEMOP_MNEMONIC2(RM_REG, MOVQ2DQ, movq2dq, VqZx_WO, Nq, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_LOCAL(uint64_t,                  uSrc);

        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_FPU_STATE_FOR_CHANGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_FETCH_MREG_U64(uSrc, IEM_GET_MODRM_RM_8(bRm));
        IEM_MC_STORE_XREG_U64_ZX_U128(IEM_GET_MODRM_REG(pVCpu, bRm), uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }

    /**
     * @opdone
     * @opmnemonic  udf30fd6mem
     * @opcode      0xd6
     * @opcodesub   !11 mr/reg
     * @oppfx       f3
     * @opunused    intel-modrm
     * @opcpuid     sse
     * @optest      ->
     */
    else
        return FNIEMOP_CALL_1(iemOp_InvalidWithRMNeedDecode, bRm);
}


/**
 * @opcode      0xd6
 * @opcodesub   11 mr/reg
 * @oppfx       f2
 * @opcpuid     sse2
 * @opgroup     og_sse2_simdint_datamove
 * @optest      op1=1 op2=2   -> op1=2   ftw=0xff
 * @optest      op1=0 op2=-42 -> op1=-42 ftw=0xff
 * @optest      op1=0 op2=0x1123456789abcdef -> op1=0x1123456789abcdef ftw=0xff
 * @optest      op1=0 op2=0xfedcba9876543210 -> op1=0xfedcba9876543210 ftw=0xff
 * @optest      op1=-42 op2=0xfedcba9876543210
 *                  ->  op1=0xfedcba9876543210 ftw=0xff
 */
FNIEMOP_DEF(iemOp_movdq2q_Pq_Uq)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEMOP_MNEMONIC2(RM_REG, MOVDQ2Q, movdq2q, Pq_WO, Uq, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_LOCAL(uint64_t,                  uSrc);

        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_FPU_STATE_FOR_CHANGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_FETCH_XREG_U64(uSrc, IEM_GET_MODRM_RM(pVCpu, bRm), 0 /* a_iQword*/);
        IEM_MC_STORE_MREG_U64(IEM_GET_MODRM_REG_8(bRm), uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }

    /**
     * @opdone
     * @opmnemonic  udf20fd6mem
     * @opcode      0xd6
     * @opcodesub   !11 mr/reg
     * @oppfx       f2
     * @opunused    intel-modrm
     * @opcpuid     sse
     * @optest      ->
     */
    else
        return FNIEMOP_CALL_1(iemOp_InvalidWithRMNeedDecode, bRm);
}


/** Opcode      0x0f 0xd7 - pmovmskb Gd, Nq */
FNIEMOP_DEF(iemOp_pmovmskb_Gd_Nq)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    /* Docs says register only. */
    if (IEM_IS_MODRM_REG_MODE(bRm)) /** @todo test that this is registers only. */
    {
        /* Note! Taking the lazy approch here wrt the high 32-bits of the GREG. */
        IEMOP_MNEMONIC2(RM_REG, PMOVMSKB, pmovmskb, Gd, Nq, DISOPTYPE_X86_MMX | DISOPTYPE_HARMLESS, 0);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX_2_OR(fSse, fAmdMmxExts);
        IEM_MC_ARG(uint64_t *,              puDst, 0);
        IEM_MC_ARG(uint64_t const *,        puSrc, 1);
        IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_REF_GREG_U64(puDst,          IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_MREG_U64_CONST(puSrc,    IEM_GET_MODRM_RM_8(bRm));
        IEM_MC_CALL_VOID_AIMPL_2(iemAImpl_pmovmskb_u64, puDst, puSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}


/** Opcode 0x66 0x0f 0xd7 -  */
FNIEMOP_DEF(iemOp_pmovmskb_Gd_Ux)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    /* Docs says register only. */
    if (IEM_IS_MODRM_REG_MODE(bRm)) /** @todo test that this is registers only. */
    {
        /* Note! Taking the lazy approch here wrt the high 32-bits of the GREG. */
        IEMOP_MNEMONIC2(RM_REG, PMOVMSKB, pmovmskb, Gd, Ux, DISOPTYPE_X86_SSE | DISOPTYPE_HARMLESS, 0);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_NATIVE_IF(RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64) {
            IEM_MC_LIVENESS_GREG_CLOBBER(IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_LIVENESS_XREG_INPUT(IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_NATIVE_EMIT_2(iemNativeEmit_pmovmskb_rr_u128, IEM_GET_MODRM_REG(pVCpu, bRm), IEM_GET_MODRM_RM(pVCpu, bRm));
        } IEM_MC_NATIVE_ELSE() {
            IEM_MC_ARG(uint64_t *,              puDst, 0);
            IEM_MC_ARG(PCRTUINT128U,            puSrc, 1);
            IEM_MC_REF_GREG_U64(puDst,          IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_REF_XREG_U128_CONST(puSrc,   IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_VOID_AIMPL_2(iemAImpl_pmovmskb_u128, puDst, puSrc);
        } IEM_MC_NATIVE_ENDIF();
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}


/*  Opcode 0xf3 0x0f 0xd7 - invalid */
/*  Opcode 0xf2 0x0f 0xd7 - invalid */


/** Opcode      0x0f 0xd8 - psubusb Pq, Qq */
FNIEMOP_DEF(iemOp_psubusb_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PSUBUSB, psubusb, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_psubusb_u64);
}


/** Opcode 0x66 0x0f 0xd8 - psubusb Vx, Wx */
FNIEMOP_DEF(iemOp_psubusb_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PSUBUSB, psubusb, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_psubusb_u128);
}


/*  Opcode 0xf3 0x0f 0xd8 - invalid */
/*  Opcode 0xf2 0x0f 0xd8 - invalid */

/** Opcode      0x0f 0xd9 - psubusw Pq, Qq */
FNIEMOP_DEF(iemOp_psubusw_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PSUBUSW, psubusw, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_psubusw_u64);
}


/** Opcode 0x66 0x0f 0xd9 - psubusw Vx, Wx */
FNIEMOP_DEF(iemOp_psubusw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PSUBUSW, psubusw, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_psubusw_u128);
}


/*  Opcode 0xf3 0x0f 0xd9 - invalid */
/*  Opcode 0xf2 0x0f 0xd9 - invalid */

/** Opcode      0x0f 0xda - pminub Pq, Qq */
FNIEMOP_DEF(iemOp_pminub_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PMINUB, pminub, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxSseOpt_FullFull_To_Full, iemAImpl_pminub_u64);
}


/** Opcode 0x66 0x0f 0xda - pminub Vx, Wx */
FNIEMOP_DEF(iemOp_pminub_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PMINUB, pminub, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_pminub_u128);
}

/*  Opcode 0xf3 0x0f 0xda - invalid */
/*  Opcode 0xf2 0x0f 0xda - invalid */

/** Opcode      0x0f 0xdb - pand Pq, Qq */
FNIEMOP_DEF(iemOp_pand_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PAND, pand, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_pand_u64);
}


/** Opcode 0x66 0x0f 0xdb - pand Vx, Wx */
FNIEMOP_DEF(iemOp_pand_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PAND, pand, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    SSE2_OPT_BODY_FullFull_To_Full(pand, iemAImpl_pand_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/*  Opcode 0xf3 0x0f 0xdb - invalid */
/*  Opcode 0xf2 0x0f 0xdb - invalid */

/** Opcode      0x0f 0xdc - paddusb Pq, Qq */
FNIEMOP_DEF(iemOp_paddusb_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PADDUSB, paddusb, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_paddusb_u64);
}


/** Opcode 0x66 0x0f 0xdc - paddusb Vx, Wx */
FNIEMOP_DEF(iemOp_paddusb_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PADDUSB, paddusb, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    SSE2_OPT_BODY_FullFull_To_Full(paddusb, iemAImpl_paddusb_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/*  Opcode 0xf3 0x0f 0xdc - invalid */
/*  Opcode 0xf2 0x0f 0xdc - invalid */

/** Opcode      0x0f 0xdd - paddusw Pq, Qq */
FNIEMOP_DEF(iemOp_paddusw_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PADDUSW, paddusw, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_paddusw_u64);
}


/** Opcode 0x66 0x0f 0xdd - paddusw Vx, Wx */
FNIEMOP_DEF(iemOp_paddusw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PADDUSW, paddusw, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    SSE2_OPT_BODY_FullFull_To_Full(paddusw, iemAImpl_paddusw_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/*  Opcode 0xf3 0x0f 0xdd - invalid */
/*  Opcode 0xf2 0x0f 0xdd - invalid */

/** Opcode      0x0f 0xde - pmaxub Pq, Qq */
FNIEMOP_DEF(iemOp_pmaxub_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PMAXUB, pmaxub, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxSseOpt_FullFull_To_Full, iemAImpl_pmaxub_u64);
}


/** Opcode 0x66 0x0f 0xde - pmaxub Vx, W */
FNIEMOP_DEF(iemOp_pmaxub_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PMAXUB, pmaxub, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_pmaxub_u128);
}

/*  Opcode 0xf3 0x0f 0xde - invalid */
/*  Opcode 0xf2 0x0f 0xde - invalid */


/** Opcode      0x0f 0xdf - pandn Pq, Qq */
FNIEMOP_DEF(iemOp_pandn_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PANDN, pandn, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_pandn_u64);
}


/** Opcode 0x66 0x0f 0xdf - pandn Vx, Wx */
FNIEMOP_DEF(iemOp_pandn_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PANDN, pandn, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_pandn_u128);
}


/*  Opcode 0xf3 0x0f 0xdf - invalid */
/*  Opcode 0xf2 0x0f 0xdf - invalid */

/** Opcode      0x0f 0xe0 - pavgb Pq, Qq */
FNIEMOP_DEF(iemOp_pavgb_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PAVGB, pavgb, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxSseOpt_FullFull_To_Full, iemAImpl_pavgb_u64);
}


/** Opcode 0x66 0x0f 0xe0 - pavgb Vx, Wx */
FNIEMOP_DEF(iemOp_pavgb_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PAVGB, pavgb, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_pavgb_u128);
}


/*  Opcode 0xf3 0x0f 0xe0 - invalid */
/*  Opcode 0xf2 0x0f 0xe0 - invalid */

/** Opcode      0x0f 0xe1 - psraw Pq, Qq */
FNIEMOP_DEF(iemOp_psraw_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PSRAW, psraw, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_psraw_u64);
}


/** Opcode 0x66 0x0f 0xe1 - psraw Vx, Wx */
FNIEMOP_DEF(iemOp_psraw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PSRAW, psraw, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_psraw_u128);
}


/*  Opcode 0xf3 0x0f 0xe1 - invalid */
/*  Opcode 0xf2 0x0f 0xe1 - invalid */

/** Opcode      0x0f 0xe2 - psrad Pq, Qq */
FNIEMOP_DEF(iemOp_psrad_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PSRAD, psrad, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_psrad_u64);
}


/** Opcode 0x66 0x0f 0xe2 - psrad Vx, Wx */
FNIEMOP_DEF(iemOp_psrad_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PSRAD, psrad, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_psrad_u128);
}


/*  Opcode 0xf3 0x0f 0xe2 - invalid */
/*  Opcode 0xf2 0x0f 0xe2 - invalid */

/** Opcode      0x0f 0xe3 - pavgw Pq, Qq */
FNIEMOP_DEF(iemOp_pavgw_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PAVGW, pavgw, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxSseOpt_FullFull_To_Full, iemAImpl_pavgw_u64);
}


/** Opcode 0x66 0x0f 0xe3 - pavgw Vx, Wx */
FNIEMOP_DEF(iemOp_pavgw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PAVGW, pavgw, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_pavgw_u128);
}


/*  Opcode 0xf3 0x0f 0xe3 - invalid */
/*  Opcode 0xf2 0x0f 0xe3 - invalid */

/** Opcode      0x0f 0xe4 - pmulhuw Pq, Qq */
FNIEMOP_DEF(iemOp_pmulhuw_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PMULHUW, pmulhuw, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxSseOpt_FullFull_To_Full, iemAImpl_pmulhuw_u64);
}


/** Opcode 0x66 0x0f 0xe4 - pmulhuw Vx, Wx */
FNIEMOP_DEF(iemOp_pmulhuw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PMULHUW, pmulhuw, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_pmulhuw_u128);
}


/*  Opcode 0xf3 0x0f 0xe4 - invalid */
/*  Opcode 0xf2 0x0f 0xe4 - invalid */

/** Opcode      0x0f 0xe5 - pmulhw Pq, Qq */
FNIEMOP_DEF(iemOp_pmulhw_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PMULHW, pmulhw, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_pmulhw_u64);
}


/** Opcode 0x66 0x0f 0xe5 - pmulhw Vx, Wx */
FNIEMOP_DEF(iemOp_pmulhw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PMULHW, pmulhw, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_pmulhw_u128);
}


/*  Opcode 0xf3 0x0f 0xe5 - invalid */
/*  Opcode 0xf2 0x0f 0xe5 - invalid */
/*  Opcode      0x0f 0xe6 - invalid */


/** Opcode 0x66 0x0f 0xe6 - cvttpd2dq Vx, Wpd */
FNIEMOP_DEF(iemOp_cvttpd2dq_Vx_Wpd)
{
    IEMOP_MNEMONIC2(RM, CVTTPD2DQ, cvttpd2dq, Vx, Wpd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Fp_FullFull_To_Full, iemAImpl_cvttpd2dq_u128);
}


/** Opcode 0xf3 0x0f 0xe6 - cvtdq2pd Vx, Wpd */
FNIEMOP_DEF(iemOp_cvtdq2pd_Vx_Wpd)
{
    IEMOP_MNEMONIC2(RM, CVTDQ2PD, cvtdq2pd, Vx, Wpd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Fp_FullFull_To_Full, iemAImpl_cvtdq2pd_u128);
}


/** Opcode 0xf2 0x0f 0xe6 - cvtpd2dq Vx, Wpd */
FNIEMOP_DEF(iemOp_cvtpd2dq_Vx_Wpd)
{
    IEMOP_MNEMONIC2(RM, CVTPD2DQ, cvtpd2dq, Vx, Wpd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Fp_FullFull_To_Full, iemAImpl_cvtpd2dq_u128);
}


/**
 * @opcode      0xe7
 * @opcodesub   !11 mr/reg
 * @oppfx       none
 * @opcpuid     sse
 * @opgroup     og_sse1_cachect
 * @opxcpttype  none
 * @optest      op1=-1 op2=2  -> op1=2   ftw=0xff
 * @optest      op1=0 op2=-42 -> op1=-42 ftw=0xff
 */
FNIEMOP_DEF(iemOp_movntq_Mq_Pq)
{
    IEMOP_MNEMONIC2(MR_MEM, MOVNTQ, movntq, Mq_WO, Pq, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_MEM_MODE(bRm))
    {
        /* Register, memory. */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fMmx);
        IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_FPU_STATE_FOR_CHANGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_FETCH_MREG_U64(uSrc, IEM_GET_MODRM_REG_8(bRm));
        IEM_MC_STORE_MEM_U64(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    /**
     * @opdone
     * @opmnemonic  ud0fe7reg
     * @opcode      0xe7
     * @opcodesub   11 mr/reg
     * @oppfx       none
     * @opunused    immediate
     * @opcpuid     sse
     * @optest      ->
     */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}

/**
 * @opcode      0xe7
 * @opcodesub   !11 mr/reg
 * @oppfx       0x66
 * @opcpuid     sse2
 * @opgroup     og_sse2_cachect
 * @opxcpttype  1
 * @optest      op1=-1 op2=2  -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_movntdq_Mdq_Vdq)
{
    IEMOP_MNEMONIC2(MR_MEM, MOVNTDQ, movntdq, Mdq_WO, Vdq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_MEM_MODE(bRm))
    {
        /* Register, memory. */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_READ();

        IEM_MC_FETCH_XREG_U128(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_STORE_MEM_U128_ALIGN_SSE(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }

    /**
     * @opdone
     * @opmnemonic  ud660fe7reg
     * @opcode      0xe7
     * @opcodesub   11 mr/reg
     * @oppfx       0x66
     * @opunused    immediate
     * @opcpuid     sse
     * @optest      ->
     */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}

/*  Opcode 0xf3 0x0f 0xe7 - invalid */
/*  Opcode 0xf2 0x0f 0xe7 - invalid */


/** Opcode      0x0f 0xe8 - psubsb Pq, Qq */
FNIEMOP_DEF(iemOp_psubsb_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PSUBSB, psubsb, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_psubsb_u64);
}


/** Opcode 0x66 0x0f 0xe8 - psubsb Vx, Wx */
FNIEMOP_DEF(iemOp_psubsb_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PSUBSB, psubsb, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_psubsb_u128);
}


/*  Opcode 0xf3 0x0f 0xe8 - invalid */
/*  Opcode 0xf2 0x0f 0xe8 - invalid */

/** Opcode      0x0f 0xe9 - psubsw Pq, Qq */
FNIEMOP_DEF(iemOp_psubsw_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PSUBSW, psubsw, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_psubsw_u64);
}


/** Opcode 0x66 0x0f 0xe9 - psubsw Vx, Wx */
FNIEMOP_DEF(iemOp_psubsw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PSUBSW, psubsw, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_psubsw_u128);
}


/*  Opcode 0xf3 0x0f 0xe9 - invalid */
/*  Opcode 0xf2 0x0f 0xe9 - invalid */


/** Opcode      0x0f 0xea - pminsw Pq, Qq */
FNIEMOP_DEF(iemOp_pminsw_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PMINSW, pminsw, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxSseOpt_FullFull_To_Full, iemAImpl_pminsw_u64);
}


/** Opcode 0x66 0x0f 0xea - pminsw Vx, Wx */
FNIEMOP_DEF(iemOp_pminsw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PMINSW, pminsw, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_pminsw_u128);
}


/*  Opcode 0xf3 0x0f 0xea - invalid */
/*  Opcode 0xf2 0x0f 0xea - invalid */


/** Opcode      0x0f 0xeb - por Pq, Qq */
FNIEMOP_DEF(iemOp_por_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, POR, por, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_por_u64);
}


/** Opcode 0x66 0x0f 0xeb - por Vx, Wx */
FNIEMOP_DEF(iemOp_por_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, POR, por, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    SSE2_OPT_BODY_FullFull_To_Full(por, iemAImpl_por_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/*  Opcode 0xf3 0x0f 0xeb - invalid */
/*  Opcode 0xf2 0x0f 0xeb - invalid */

/** Opcode      0x0f 0xec - paddsb Pq, Qq */
FNIEMOP_DEF(iemOp_paddsb_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PADDSB, paddsb, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_paddsb_u64);
}


/** Opcode 0x66 0x0f 0xec - paddsb Vx, Wx */
FNIEMOP_DEF(iemOp_paddsb_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PADDSB, paddsb, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_paddsb_u128);
}


/*  Opcode 0xf3 0x0f 0xec - invalid */
/*  Opcode 0xf2 0x0f 0xec - invalid */

/** Opcode      0x0f 0xed - paddsw Pq, Qq */
FNIEMOP_DEF(iemOp_paddsw_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PADDSW, paddsw, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_paddsw_u64);
}


/** Opcode 0x66 0x0f 0xed - paddsw Vx, Wx */
FNIEMOP_DEF(iemOp_paddsw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PADDSW, paddsw, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_paddsw_u128);
}


/*  Opcode 0xf3 0x0f 0xed - invalid */
/*  Opcode 0xf2 0x0f 0xed - invalid */


/** Opcode      0x0f 0xee - pmaxsw Pq, Qq */
FNIEMOP_DEF(iemOp_pmaxsw_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PMAXSW, pmaxsw, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxSseOpt_FullFull_To_Full, iemAImpl_pmaxsw_u64);
}


/** Opcode 0x66 0x0f 0xee - pmaxsw Vx, Wx */
FNIEMOP_DEF(iemOp_pmaxsw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PMAXSW, pmaxsw, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_pmaxsw_u128);
}


/*  Opcode 0xf3 0x0f 0xee - invalid */
/*  Opcode 0xf2 0x0f 0xee - invalid */


/** Opcode      0x0f 0xef - pxor Pq, Qq */
FNIEMOP_DEF(iemOp_pxor_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PXOR, pxor, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_pxor_u64);
}


/** Opcode 0x66 0x0f 0xef - pxor Vx, Wx */
FNIEMOP_DEF(iemOp_pxor_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PXOR, pxor, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    SSE2_OPT_BODY_FullFull_To_Full(pxor, iemAImpl_pxor_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/*  Opcode 0xf3 0x0f 0xef - invalid */
/*  Opcode 0xf2 0x0f 0xef - invalid */

/*  Opcode      0x0f 0xf0 - invalid */
/*  Opcode 0x66 0x0f 0xf0 - invalid */


/** Opcode 0xf2 0x0f 0xf0 - lddqu Vx, Mx */
FNIEMOP_DEF(iemOp_lddqu_Vx_Mx)
{
    IEMOP_MNEMONIC2(RM_MEM, LDDQU, lddqu, Vdq_WO, Mx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register - (not implemented, assuming it raises \#UD).
         */
        IEMOP_RAISE_INVALID_OPCODE_RET();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT128U, u128Tmp);
        IEM_MC_LOCAL(RTGCPTR,    GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse3);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();
        IEM_MC_FETCH_MEM_U128_NO_AC(u128Tmp, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_XREG_U128(IEM_GET_MODRM_REG(pVCpu, bRm), u128Tmp);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode      0x0f 0xf1 - psllw Pq, Qq */
FNIEMOP_DEF(iemOp_psllw_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PSLLW, psllw, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, 0);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_psllw_u64);
}


/** Opcode 0x66 0x0f 0xf1 - psllw Vx, Wx */
FNIEMOP_DEF(iemOp_psllw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PSLLW, psllw, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_psllw_u128);
}


/*  Opcode 0xf2 0x0f 0xf1 - invalid */

/** Opcode      0x0f 0xf2 - pslld Pq, Qq */
FNIEMOP_DEF(iemOp_pslld_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PSLLD, pslld, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, 0);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_pslld_u64);
}


/** Opcode 0x66 0x0f 0xf2 - pslld Vx, Wx */
FNIEMOP_DEF(iemOp_pslld_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PSLLD, pslld, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_pslld_u128);
}


/*  Opcode 0xf2 0x0f 0xf2 - invalid */

/** Opcode      0x0f 0xf3 - psllq Pq, Qq */
FNIEMOP_DEF(iemOp_psllq_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PSLLQ, psllq, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, 0);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_psllq_u64);
}


/** Opcode 0x66 0x0f 0xf3 - psllq Vx, Wx */
FNIEMOP_DEF(iemOp_psllq_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PSLLQ, psllq, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_psllq_u128);
}

/*  Opcode 0xf2 0x0f 0xf3 - invalid */

/** Opcode      0x0f 0xf4 - pmuludq Pq, Qq */
FNIEMOP_DEF(iemOp_pmuludq_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PMULUDQ, pmuludq, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_pmuludq_u64);
}


/** Opcode 0x66 0x0f 0xf4 - pmuludq Vx, W */
FNIEMOP_DEF(iemOp_pmuludq_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PMULUDQ, pmuludq, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_pmuludq_u128);
}


/*  Opcode 0xf2 0x0f 0xf4 - invalid */

/** Opcode      0x0f 0xf5 - pmaddwd Pq, Qq */
FNIEMOP_DEF(iemOp_pmaddwd_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PMADDWD, pmaddwd, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, 0);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_pmaddwd_u64);
}


/** Opcode 0x66 0x0f 0xf5 - pmaddwd Vx, Wx */
FNIEMOP_DEF(iemOp_pmaddwd_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PMADDWD, pmaddwd, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_pmaddwd_u128);
}

/*  Opcode 0xf2 0x0f 0xf5 - invalid */

/** Opcode      0x0f 0xf6 - psadbw Pq, Qq */
FNIEMOP_DEF(iemOp_psadbw_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PSADBW, psadbw, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxSseOpt_FullFull_To_Full, iemAImpl_psadbw_u64);
}


/** Opcode 0x66 0x0f 0xf6 - psadbw Vx, Wx */
FNIEMOP_DEF(iemOp_psadbw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PSADBW, psadbw, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse2Opt_FullFull_To_Full, iemAImpl_psadbw_u128);
}


/*  Opcode 0xf2 0x0f 0xf6 - invalid */

/** Opcode      0x0f 0xf7 - maskmovq Pq, Nq */
FNIEMOP_DEF(iemOp_maskmovq_Pq_Nq)
{
//  IEMOP_MNEMONIC2(RM, MASKMOVQ, maskmovq, Pq, Nq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES); /** @todo */
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * MMX, MMX, (implicit) [ ER]DI
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX_2_OR(fSse, fAmdMmxExts);
        IEM_MC_LOCAL(        uint64_t,         u64EffAddr);
        IEM_MC_LOCAL(        uint64_t,         u64Mem);
        IEM_MC_ARG_LOCAL_REF(uint64_t *,       pu64Mem, u64Mem, 0);
        IEM_MC_ARG(          uint64_t const *, puSrc,           1);
        IEM_MC_ARG(          uint64_t const *, puMsk,           2);
        IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();

        IEM_MC_FETCH_GREG_U64(u64EffAddr, X86_GREG_xDI);
        IEM_MC_FETCH_MEM_U64(u64Mem, pVCpu->iem.s.iEffSeg, u64EffAddr);
        IEM_MC_REF_MREG_U64_CONST(puSrc,  IEM_GET_MODRM_REG_8(bRm));
        IEM_MC_REF_MREG_U64_CONST(puMsk,  IEM_GET_MODRM_RM_8(bRm));
        IEM_MC_CALL_VOID_AIMPL_3(iemAImpl_maskmovq_u64, pu64Mem, puSrc, puMsk);
        IEM_MC_STORE_MEM_U64(pVCpu->iem.s.iEffSeg, u64EffAddr, u64Mem);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /* The memory, register encoding is invalid. */
        IEMOP_RAISE_INVALID_OPCODE_RET();
    }
}


/** Opcode 0x66 0x0f 0xf7 - maskmovdqu Vdq, Udq */
FNIEMOP_DEF(iemOp_maskmovdqu_Vdq_Udq)
{
//  IEMOP_MNEMONIC2(RM, MASKMOVDQU, maskmovdqu, Vdq, Udq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES); /** @todo */
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM, XMM, (implicit) [ ER]DI
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX_2_OR(fSse, fAmdMmxExts);
        IEM_MC_LOCAL(        uint64_t,         u64EffAddr);
        IEM_MC_LOCAL(        RTUINT128U,       u128Mem);
        IEM_MC_ARG_LOCAL_REF(PRTUINT128U,      pu128Mem, u128Mem, 0);
        IEM_MC_ARG(          PCRTUINT128U,     puSrc,             1);
        IEM_MC_ARG(          PCRTUINT128U,     puMsk,             2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_FETCH_GREG_U64(u64EffAddr, X86_GREG_xDI);
        IEM_MC_FETCH_MEM_U128(u128Mem, pVCpu->iem.s.iEffSeg, u64EffAddr);
        IEM_MC_REF_XREG_U128_CONST(puSrc, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(puMsk, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_3(iemAImpl_maskmovdqu_u128, pu128Mem, puSrc, puMsk);
        IEM_MC_STORE_MEM_U128(pVCpu->iem.s.iEffSeg, u64EffAddr, u128Mem);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /* The memory, register encoding is invalid. */
        IEMOP_RAISE_INVALID_OPCODE_RET();
    }
}


/*  Opcode 0xf2 0x0f 0xf7 - invalid */


/** Opcode      0x0f 0xf8 - psubb Pq, Qq */
FNIEMOP_DEF(iemOp_psubb_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PSUBB, psubb, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_psubb_u64);
}


/** Opcode 0x66 0x0f 0xf8 - psubb Vx, Wx */
FNIEMOP_DEF(iemOp_psubb_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PSUBB, psubb, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    SSE2_OPT_BODY_FullFull_To_Full(psubb, iemAImpl_psubb_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/*  Opcode 0xf2 0x0f 0xf8 - invalid */


/** Opcode      0x0f 0xf9 - psubw Pq, Qq */
FNIEMOP_DEF(iemOp_psubw_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PSUBW, psubw, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_psubw_u64);
}


/** Opcode 0x66 0x0f 0xf9 - psubw Vx, Wx */
FNIEMOP_DEF(iemOp_psubw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PSUBW, psubw, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    SSE2_OPT_BODY_FullFull_To_Full(psubw, iemAImpl_psubw_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/*  Opcode 0xf2 0x0f 0xf9 - invalid */


/** Opcode      0x0f 0xfa - psubd Pq, Qq */
FNIEMOP_DEF(iemOp_psubd_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PSUBD, psubd, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_psubd_u64);
}


/** Opcode 0x66 0x0f 0xfa - psubd Vx, Wx */
FNIEMOP_DEF(iemOp_psubd_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PSUBD, psubd, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    SSE2_OPT_BODY_FullFull_To_Full(psubd, iemAImpl_psubd_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/*  Opcode 0xf2 0x0f 0xfa - invalid */


/** Opcode      0x0f 0xfb - psubq Pq, Qq */
FNIEMOP_DEF(iemOp_psubq_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PSUBQ, psubq, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full_Sse2, iemAImpl_psubq_u64);
}


/** Opcode 0x66 0x0f 0xfb - psubq Vx, Wx */
FNIEMOP_DEF(iemOp_psubq_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PSUBQ, psubq, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    SSE2_OPT_BODY_FullFull_To_Full(psubq, iemAImpl_psubq_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/*  Opcode 0xf2 0x0f 0xfb - invalid */


/** Opcode      0x0f 0xfc - paddb Pq, Qq */
FNIEMOP_DEF(iemOp_paddb_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PADDB, paddb, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_paddb_u64);
}


/** Opcode 0x66 0x0f 0xfc - paddb Vx, Wx */
FNIEMOP_DEF(iemOp_paddb_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PADDB, paddb, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    SSE2_OPT_BODY_FullFull_To_Full(paddb, iemAImpl_paddb_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/*  Opcode 0xf2 0x0f 0xfc - invalid */


/** Opcode      0x0f 0xfd - paddw Pq, Qq */
FNIEMOP_DEF(iemOp_paddw_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PADDW, paddw, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_paddw_u64);
}


/** Opcode 0x66 0x0f 0xfd - paddw Vx, Wx */
FNIEMOP_DEF(iemOp_paddw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PADDW, paddw, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    SSE2_OPT_BODY_FullFull_To_Full(paddw, iemAImpl_paddw_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/*  Opcode 0xf2 0x0f 0xfd - invalid */


/** Opcode      0x0f 0xfe - paddd Pq, Qq */
FNIEMOP_DEF(iemOp_paddd_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PADDD, paddd, Pq, Qq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_MMX, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full, iemAImpl_paddd_u64);
}


/** Opcode 0x66 0x0f 0xfe - paddd Vx, W */
FNIEMOP_DEF(iemOp_paddd_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PADDD, paddd, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    SSE2_OPT_BODY_FullFull_To_Full(paddd, iemAImpl_paddd_u128, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/*  Opcode 0xf2 0x0f 0xfe - invalid */


/** Opcode **** 0x0f 0xff - UD0 */
FNIEMOP_DEF(iemOp_ud0)
{
    IEMOP_MNEMONIC(ud0, "ud0");
    if (pVCpu->iem.s.enmCpuVendor == CPUMCPUVENDOR_INTEL)
    {
        uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm); RT_NOREF(bRm);
        if (IEM_IS_MODRM_MEM_MODE(bRm))
            IEM_OPCODE_SKIP_RM_EFF_ADDR_BYTES(bRm);
    }
    IEMOP_HLP_DONE_DECODING();
    IEMOP_RAISE_INVALID_OPCODE_RET();
}



/**
 * Two byte opcode map, first byte 0x0f.
 *
 * @remarks The g_apfnVexMap1 table is currently a subset of this one, so please
 *          check if it needs updating as well when making changes.
 */
const PFNIEMOP g_apfnTwoByteMap[] =
{
    /*          no prefix,                  066h prefix                 f3h prefix,                 f2h prefix */
    /* 0x00 */  IEMOP_X4(iemOp_Grp6),
    /* 0x01 */  IEMOP_X4(iemOp_Grp7),
    /* 0x02 */  IEMOP_X4(iemOp_lar_Gv_Ew),
    /* 0x03 */  IEMOP_X4(iemOp_lsl_Gv_Ew),
    /* 0x04 */  IEMOP_X4(iemOp_Invalid),
    /* 0x05 */  IEMOP_X4(iemOp_syscall),
    /* 0x06 */  IEMOP_X4(iemOp_clts),
    /* 0x07 */  IEMOP_X4(iemOp_sysret),
    /* 0x08 */  IEMOP_X4(iemOp_invd),
    /* 0x09 */  IEMOP_X4(iemOp_wbinvd),
    /* 0x0a */  IEMOP_X4(iemOp_Invalid),
    /* 0x0b */  IEMOP_X4(iemOp_ud2),
    /* 0x0c */  IEMOP_X4(iemOp_Invalid),
    /* 0x0d */  IEMOP_X4(iemOp_nop_Ev_GrpP),
    /* 0x0e */  IEMOP_X4(iemOp_femms),
    /* 0x0f */  IEMOP_X4(iemOp_3Dnow),

    /* 0x10 */  iemOp_movups_Vps_Wps,       iemOp_movupd_Vpd_Wpd,       iemOp_movss_Vss_Wss,        iemOp_movsd_Vsd_Wsd,
    /* 0x11 */  iemOp_movups_Wps_Vps,       iemOp_movupd_Wpd_Vpd,       iemOp_movss_Wss_Vss,        iemOp_movsd_Wsd_Vsd,
    /* 0x12 */  iemOp_movlps_Vq_Mq__movhlps, iemOp_movlpd_Vq_Mq,        iemOp_movsldup_Vdq_Wdq,     iemOp_movddup_Vdq_Wdq,
    /* 0x13 */  iemOp_movlps_Mq_Vq,         iemOp_movlpd_Mq_Vq,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x14 */  iemOp_unpcklps_Vx_Wx,       iemOp_unpcklpd_Vx_Wx,       iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x15 */  iemOp_unpckhps_Vx_Wx,       iemOp_unpckhpd_Vx_Wx,       iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x16 */  iemOp_movhps_Vdq_Mq__movlhps_Vdq_Uq, iemOp_movhpd_Vdq_Mq, iemOp_movshdup_Vdq_Wdq,   iemOp_InvalidNeedRM,
    /* 0x17 */  iemOp_movhps_Mq_Vq,         iemOp_movhpd_Mq_Vq,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x18 */  IEMOP_X4(iemOp_prefetch_Grp16),
    /* 0x19 */  IEMOP_X4(iemOp_nop_Ev),
    /* 0x1a */  IEMOP_X4(iemOp_nop_Ev),
    /* 0x1b */  IEMOP_X4(iemOp_nop_Ev),
    /* 0x1c */  IEMOP_X4(iemOp_nop_Ev),
    /* 0x1d */  IEMOP_X4(iemOp_nop_Ev),
    /* 0x1e */  IEMOP_X4(iemOp_nop_Ev),
    /* 0x1f */  IEMOP_X4(iemOp_nop_Ev),

    /* 0x20 */  iemOp_mov_Rd_Cd,            iemOp_mov_Rd_Cd,            iemOp_mov_Rd_Cd,            iemOp_mov_Rd_Cd,
    /* 0x21 */  iemOp_mov_Rd_Dd,            iemOp_mov_Rd_Dd,            iemOp_mov_Rd_Dd,            iemOp_mov_Rd_Dd,
    /* 0x22 */  iemOp_mov_Cd_Rd,            iemOp_mov_Cd_Rd,            iemOp_mov_Cd_Rd,            iemOp_mov_Cd_Rd,
    /* 0x23 */  iemOp_mov_Dd_Rd,            iemOp_mov_Dd_Rd,            iemOp_mov_Dd_Rd,            iemOp_mov_Dd_Rd,
    /* 0x24 */  iemOp_mov_Rd_Td,            iemOp_mov_Rd_Td,            iemOp_mov_Rd_Td,            iemOp_mov_Rd_Td,
    /* 0x25 */  iemOp_Invalid,              iemOp_Invalid,              iemOp_Invalid,              iemOp_Invalid,
    /* 0x26 */  iemOp_mov_Td_Rd,            iemOp_mov_Td_Rd,            iemOp_mov_Td_Rd,            iemOp_mov_Td_Rd,
    /* 0x27 */  iemOp_Invalid,              iemOp_Invalid,              iemOp_Invalid,              iemOp_Invalid,
    /* 0x28 */  iemOp_movaps_Vps_Wps,       iemOp_movapd_Vpd_Wpd,       iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x29 */  iemOp_movaps_Wps_Vps,       iemOp_movapd_Wpd_Vpd,       iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x2a */  iemOp_cvtpi2ps_Vps_Qpi,     iemOp_cvtpi2pd_Vpd_Qpi,     iemOp_cvtsi2ss_Vss_Ey,      iemOp_cvtsi2sd_Vsd_Ey,
    /* 0x2b */  iemOp_movntps_Mps_Vps,      iemOp_movntpd_Mpd_Vpd,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x2c */  iemOp_cvttps2pi_Ppi_Wps,    iemOp_cvttpd2pi_Ppi_Wpd,    iemOp_cvttss2si_Gy_Wss,     iemOp_cvttsd2si_Gy_Wsd,
    /* 0x2d */  iemOp_cvtps2pi_Ppi_Wps,     iemOp_cvtpd2pi_Qpi_Wpd,     iemOp_cvtss2si_Gy_Wss,      iemOp_cvtsd2si_Gy_Wsd,
    /* 0x2e */  iemOp_ucomiss_Vss_Wss,      iemOp_ucomisd_Vsd_Wsd,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x2f */  iemOp_comiss_Vss_Wss,       iemOp_comisd_Vsd_Wsd,       iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,

    /* 0x30 */  IEMOP_X4(iemOp_wrmsr),
    /* 0x31 */  IEMOP_X4(iemOp_rdtsc),
    /* 0x32 */  IEMOP_X4(iemOp_rdmsr),
    /* 0x33 */  IEMOP_X4(iemOp_rdpmc),
    /* 0x34 */  IEMOP_X4(iemOp_sysenter),
    /* 0x35 */  IEMOP_X4(iemOp_sysexit),
    /* 0x36 */  IEMOP_X4(iemOp_Invalid),
    /* 0x37 */  IEMOP_X4(iemOp_getsec),
    /* 0x38 */  IEMOP_X4(iemOp_3byte_Esc_0f_38),
    /* 0x39 */  IEMOP_X4(iemOp_InvalidNeed3ByteEscRM),
    /* 0x3a */  IEMOP_X4(iemOp_3byte_Esc_0f_3a),
    /* 0x3b */  IEMOP_X4(iemOp_InvalidNeed3ByteEscRMImm8),
    /* 0x3c */  IEMOP_X4(iemOp_InvalidNeed3ByteEscRM),
    /* 0x3d */  IEMOP_X4(iemOp_InvalidNeed3ByteEscRM),
    /* 0x3e */  IEMOP_X4(iemOp_InvalidNeed3ByteEscRMImm8),
    /* 0x3f */  IEMOP_X4(iemOp_InvalidNeed3ByteEscRMImm8),

    /* 0x40 */  IEMOP_X4(iemOp_cmovo_Gv_Ev),
    /* 0x41 */  IEMOP_X4(iemOp_cmovno_Gv_Ev),
    /* 0x42 */  IEMOP_X4(iemOp_cmovc_Gv_Ev),
    /* 0x43 */  IEMOP_X4(iemOp_cmovnc_Gv_Ev),
    /* 0x44 */  IEMOP_X4(iemOp_cmove_Gv_Ev),
    /* 0x45 */  IEMOP_X4(iemOp_cmovne_Gv_Ev),
    /* 0x46 */  IEMOP_X4(iemOp_cmovbe_Gv_Ev),
    /* 0x47 */  IEMOP_X4(iemOp_cmovnbe_Gv_Ev),
    /* 0x48 */  IEMOP_X4(iemOp_cmovs_Gv_Ev),
    /* 0x49 */  IEMOP_X4(iemOp_cmovns_Gv_Ev),
    /* 0x4a */  IEMOP_X4(iemOp_cmovp_Gv_Ev),
    /* 0x4b */  IEMOP_X4(iemOp_cmovnp_Gv_Ev),
    /* 0x4c */  IEMOP_X4(iemOp_cmovl_Gv_Ev),
    /* 0x4d */  IEMOP_X4(iemOp_cmovnl_Gv_Ev),
    /* 0x4e */  IEMOP_X4(iemOp_cmovle_Gv_Ev),
    /* 0x4f */  IEMOP_X4(iemOp_cmovnle_Gv_Ev),

    /* 0x50 */  iemOp_movmskps_Gy_Ups,      iemOp_movmskpd_Gy_Upd,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x51 */  iemOp_sqrtps_Vps_Wps,       iemOp_sqrtpd_Vpd_Wpd,       iemOp_sqrtss_Vss_Wss,       iemOp_sqrtsd_Vsd_Wsd,
    /* 0x52 */  iemOp_rsqrtps_Vps_Wps,      iemOp_InvalidNeedRM,        iemOp_rsqrtss_Vss_Wss,      iemOp_InvalidNeedRM,
    /* 0x53 */  iemOp_rcpps_Vps_Wps,        iemOp_InvalidNeedRM,        iemOp_rcpss_Vss_Wss,        iemOp_InvalidNeedRM,
    /* 0x54 */  iemOp_andps_Vps_Wps,        iemOp_andpd_Vpd_Wpd,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x55 */  iemOp_andnps_Vps_Wps,       iemOp_andnpd_Vpd_Wpd,       iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x56 */  iemOp_orps_Vps_Wps,         iemOp_orpd_Vpd_Wpd,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x57 */  iemOp_xorps_Vps_Wps,        iemOp_xorpd_Vpd_Wpd,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x58 */  iemOp_addps_Vps_Wps,        iemOp_addpd_Vpd_Wpd,        iemOp_addss_Vss_Wss,        iemOp_addsd_Vsd_Wsd,
    /* 0x59 */  iemOp_mulps_Vps_Wps,        iemOp_mulpd_Vpd_Wpd,        iemOp_mulss_Vss_Wss,        iemOp_mulsd_Vsd_Wsd,
    /* 0x5a */  iemOp_cvtps2pd_Vpd_Wps,     iemOp_cvtpd2ps_Vps_Wpd,     iemOp_cvtss2sd_Vsd_Wss,     iemOp_cvtsd2ss_Vss_Wsd,
    /* 0x5b */  iemOp_cvtdq2ps_Vps_Wdq,     iemOp_cvtps2dq_Vdq_Wps,     iemOp_cvttps2dq_Vdq_Wps,    iemOp_InvalidNeedRM,
    /* 0x5c */  iemOp_subps_Vps_Wps,        iemOp_subpd_Vpd_Wpd,        iemOp_subss_Vss_Wss,        iemOp_subsd_Vsd_Wsd,
    /* 0x5d */  iemOp_minps_Vps_Wps,        iemOp_minpd_Vpd_Wpd,        iemOp_minss_Vss_Wss,        iemOp_minsd_Vsd_Wsd,
    /* 0x5e */  iemOp_divps_Vps_Wps,        iemOp_divpd_Vpd_Wpd,        iemOp_divss_Vss_Wss,        iemOp_divsd_Vsd_Wsd,
    /* 0x5f */  iemOp_maxps_Vps_Wps,        iemOp_maxpd_Vpd_Wpd,        iemOp_maxss_Vss_Wss,        iemOp_maxsd_Vsd_Wsd,

    /* 0x60 */  iemOp_punpcklbw_Pq_Qd,      iemOp_punpcklbw_Vx_Wx,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x61 */  iemOp_punpcklwd_Pq_Qd,      iemOp_punpcklwd_Vx_Wx,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x62 */  iemOp_punpckldq_Pq_Qd,      iemOp_punpckldq_Vx_Wx,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x63 */  iemOp_packsswb_Pq_Qq,       iemOp_packsswb_Vx_Wx,       iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x64 */  iemOp_pcmpgtb_Pq_Qq,        iemOp_pcmpgtb_Vx_Wx,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x65 */  iemOp_pcmpgtw_Pq_Qq,        iemOp_pcmpgtw_Vx_Wx,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x66 */  iemOp_pcmpgtd_Pq_Qq,        iemOp_pcmpgtd_Vx_Wx,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x67 */  iemOp_packuswb_Pq_Qq,       iemOp_packuswb_Vx_Wx,       iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x68 */  iemOp_punpckhbw_Pq_Qq,      iemOp_punpckhbw_Vx_Wx,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x69 */  iemOp_punpckhwd_Pq_Qq,      iemOp_punpckhwd_Vx_Wx,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x6a */  iemOp_punpckhdq_Pq_Qq,      iemOp_punpckhdq_Vx_Wx,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x6b */  iemOp_packssdw_Pq_Qd,       iemOp_packssdw_Vx_Wx,       iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x6c */  iemOp_InvalidNeedRM,        iemOp_punpcklqdq_Vx_Wx,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x6d */  iemOp_InvalidNeedRM,        iemOp_punpckhqdq_Vx_Wx,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x6e */  iemOp_movd_q_Pd_Ey,         iemOp_movd_q_Vy_Ey,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x6f */  iemOp_movq_Pq_Qq,           iemOp_movdqa_Vdq_Wdq,       iemOp_movdqu_Vdq_Wdq,       iemOp_InvalidNeedRM,

    /* 0x70 */  iemOp_pshufw_Pq_Qq_Ib,      iemOp_pshufd_Vx_Wx_Ib,      iemOp_pshufhw_Vx_Wx_Ib,     iemOp_pshuflw_Vx_Wx_Ib,
    /* 0x71 */  IEMOP_X4(iemOp_Grp12),
    /* 0x72 */  IEMOP_X4(iemOp_Grp13),
    /* 0x73 */  IEMOP_X4(iemOp_Grp14),
    /* 0x74 */  iemOp_pcmpeqb_Pq_Qq,        iemOp_pcmpeqb_Vx_Wx,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x75 */  iemOp_pcmpeqw_Pq_Qq,        iemOp_pcmpeqw_Vx_Wx,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x76 */  iemOp_pcmpeqd_Pq_Qq,        iemOp_pcmpeqd_Vx_Wx,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x77 */  iemOp_emms,                 iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,

    /* 0x78 */  iemOp_vmread_Ey_Gy,         iemOp_AmdGrp17,             iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x79 */  iemOp_vmwrite_Gy_Ey,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x7a */  iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x7b */  iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x7c */  iemOp_InvalidNeedRM,        iemOp_haddpd_Vpd_Wpd,       iemOp_InvalidNeedRM,        iemOp_haddps_Vps_Wps,
    /* 0x7d */  iemOp_InvalidNeedRM,        iemOp_hsubpd_Vpd_Wpd,       iemOp_InvalidNeedRM,        iemOp_hsubps_Vps_Wps,
    /* 0x7e */  iemOp_movd_q_Ey_Pd,         iemOp_movd_q_Ey_Vy,         iemOp_movq_Vq_Wq,           iemOp_InvalidNeedRM,
    /* 0x7f */  iemOp_movq_Qq_Pq,           iemOp_movdqa_Wx_Vx,         iemOp_movdqu_Wx_Vx,         iemOp_InvalidNeedRM,

    /* 0x80 */  IEMOP_X4(iemOp_jo_Jv),
    /* 0x81 */  IEMOP_X4(iemOp_jno_Jv),
    /* 0x82 */  IEMOP_X4(iemOp_jc_Jv),
    /* 0x83 */  IEMOP_X4(iemOp_jnc_Jv),
    /* 0x84 */  IEMOP_X4(iemOp_je_Jv),
    /* 0x85 */  IEMOP_X4(iemOp_jne_Jv),
    /* 0x86 */  IEMOP_X4(iemOp_jbe_Jv),
    /* 0x87 */  IEMOP_X4(iemOp_jnbe_Jv),
    /* 0x88 */  IEMOP_X4(iemOp_js_Jv),
    /* 0x89 */  IEMOP_X4(iemOp_jns_Jv),
    /* 0x8a */  IEMOP_X4(iemOp_jp_Jv),
    /* 0x8b */  IEMOP_X4(iemOp_jnp_Jv),
    /* 0x8c */  IEMOP_X4(iemOp_jl_Jv),
    /* 0x8d */  IEMOP_X4(iemOp_jnl_Jv),
    /* 0x8e */  IEMOP_X4(iemOp_jle_Jv),
    /* 0x8f */  IEMOP_X4(iemOp_jnle_Jv),

    /* 0x90 */  IEMOP_X4(iemOp_seto_Eb),
    /* 0x91 */  IEMOP_X4(iemOp_setno_Eb),
    /* 0x92 */  IEMOP_X4(iemOp_setc_Eb),
    /* 0x93 */  IEMOP_X4(iemOp_setnc_Eb),
    /* 0x94 */  IEMOP_X4(iemOp_sete_Eb),
    /* 0x95 */  IEMOP_X4(iemOp_setne_Eb),
    /* 0x96 */  IEMOP_X4(iemOp_setbe_Eb),
    /* 0x97 */  IEMOP_X4(iemOp_setnbe_Eb),
    /* 0x98 */  IEMOP_X4(iemOp_sets_Eb),
    /* 0x99 */  IEMOP_X4(iemOp_setns_Eb),
    /* 0x9a */  IEMOP_X4(iemOp_setp_Eb),
    /* 0x9b */  IEMOP_X4(iemOp_setnp_Eb),
    /* 0x9c */  IEMOP_X4(iemOp_setl_Eb),
    /* 0x9d */  IEMOP_X4(iemOp_setnl_Eb),
    /* 0x9e */  IEMOP_X4(iemOp_setle_Eb),
    /* 0x9f */  IEMOP_X4(iemOp_setnle_Eb),

    /* 0xa0 */  IEMOP_X4(iemOp_push_fs),
    /* 0xa1 */  IEMOP_X4(iemOp_pop_fs),
    /* 0xa2 */  IEMOP_X4(iemOp_cpuid),
    /* 0xa3 */  IEMOP_X4(iemOp_bt_Ev_Gv),
    /* 0xa4 */  IEMOP_X4(iemOp_shld_Ev_Gv_Ib),
    /* 0xa5 */  IEMOP_X4(iemOp_shld_Ev_Gv_CL),
    /* 0xa6 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xa7 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xa8 */  IEMOP_X4(iemOp_push_gs),
    /* 0xa9 */  IEMOP_X4(iemOp_pop_gs),
    /* 0xaa */  IEMOP_X4(iemOp_rsm),
    /* 0xab */  IEMOP_X4(iemOp_bts_Ev_Gv),
    /* 0xac */  IEMOP_X4(iemOp_shrd_Ev_Gv_Ib),
    /* 0xad */  IEMOP_X4(iemOp_shrd_Ev_Gv_CL),
    /* 0xae */  IEMOP_X4(iemOp_Grp15),
    /* 0xaf */  IEMOP_X4(iemOp_imul_Gv_Ev),

    /* 0xb0 */  IEMOP_X4(iemOp_cmpxchg_Eb_Gb),
    /* 0xb1 */  IEMOP_X4(iemOp_cmpxchg_Ev_Gv),
    /* 0xb2 */  IEMOP_X4(iemOp_lss_Gv_Mp),
    /* 0xb3 */  IEMOP_X4(iemOp_btr_Ev_Gv),
    /* 0xb4 */  IEMOP_X4(iemOp_lfs_Gv_Mp),
    /* 0xb5 */  IEMOP_X4(iemOp_lgs_Gv_Mp),
    /* 0xb6 */  IEMOP_X4(iemOp_movzx_Gv_Eb),
    /* 0xb7 */  IEMOP_X4(iemOp_movzx_Gv_Ew),
    /* 0xb8 */  iemOp_jmpe,                 iemOp_InvalidNeedRM,        iemOp_popcnt_Gv_Ev,         iemOp_InvalidNeedRM,
    /* 0xb9 */  IEMOP_X4(iemOp_Grp10),
    /* 0xba */  IEMOP_X4(iemOp_Grp8),
    /* 0xbb */  IEMOP_X4(iemOp_btc_Ev_Gv), // 0xf3?
    /* 0xbc */  iemOp_bsf_Gv_Ev,            iemOp_bsf_Gv_Ev,            iemOp_tzcnt_Gv_Ev,          iemOp_bsf_Gv_Ev,
    /* 0xbd */  iemOp_bsr_Gv_Ev,            iemOp_bsr_Gv_Ev,            iemOp_lzcnt_Gv_Ev,          iemOp_bsr_Gv_Ev,
    /* 0xbe */  IEMOP_X4(iemOp_movsx_Gv_Eb),
    /* 0xbf */  IEMOP_X4(iemOp_movsx_Gv_Ew),

    /* 0xc0 */  IEMOP_X4(iemOp_xadd_Eb_Gb),
    /* 0xc1 */  IEMOP_X4(iemOp_xadd_Ev_Gv),
    /* 0xc2 */  iemOp_cmpps_Vps_Wps_Ib,     iemOp_cmppd_Vpd_Wpd_Ib,     iemOp_cmpss_Vss_Wss_Ib,     iemOp_cmpsd_Vsd_Wsd_Ib,
    /* 0xc3 */  iemOp_movnti_My_Gy,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xc4 */  iemOp_pinsrw_Pq_RyMw_Ib,    iemOp_pinsrw_Vdq_RyMw_Ib,   iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0xc5 */  iemOp_pextrw_Gd_Nq_Ib,      iemOp_pextrw_Gd_Udq_Ib,     iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0xc6 */  iemOp_shufps_Vps_Wps_Ib,    iemOp_shufpd_Vpd_Wpd_Ib,    iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0xc7 */  IEMOP_X4(iemOp_Grp9),
    /* 0xc8 */  IEMOP_X4(iemOp_bswap_rAX_r8),
    /* 0xc9 */  IEMOP_X4(iemOp_bswap_rCX_r9),
    /* 0xca */  IEMOP_X4(iemOp_bswap_rDX_r10),
    /* 0xcb */  IEMOP_X4(iemOp_bswap_rBX_r11),
    /* 0xcc */  IEMOP_X4(iemOp_bswap_rSP_r12),
    /* 0xcd */  IEMOP_X4(iemOp_bswap_rBP_r13),
    /* 0xce */  IEMOP_X4(iemOp_bswap_rSI_r14),
    /* 0xcf */  IEMOP_X4(iemOp_bswap_rDI_r15),

    /* 0xd0 */  iemOp_InvalidNeedRM,        iemOp_addsubpd_Vpd_Wpd,     iemOp_InvalidNeedRM,        iemOp_addsubps_Vps_Wps,
    /* 0xd1 */  iemOp_psrlw_Pq_Qq,          iemOp_psrlw_Vx_Wx,          iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xd2 */  iemOp_psrld_Pq_Qq,          iemOp_psrld_Vx_Wx,          iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xd3 */  iemOp_psrlq_Pq_Qq,          iemOp_psrlq_Vx_Wx,          iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xd4 */  iemOp_paddq_Pq_Qq,          iemOp_paddq_Vx_Wx,          iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xd5 */  iemOp_pmullw_Pq_Qq,         iemOp_pmullw_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xd6 */  iemOp_InvalidNeedRM,        iemOp_movq_Wq_Vq,           iemOp_movq2dq_Vdq_Nq,       iemOp_movdq2q_Pq_Uq,
    /* 0xd7 */  iemOp_pmovmskb_Gd_Nq,       iemOp_pmovmskb_Gd_Ux,       iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xd8 */  iemOp_psubusb_Pq_Qq,        iemOp_psubusb_Vx_Wx,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xd9 */  iemOp_psubusw_Pq_Qq,        iemOp_psubusw_Vx_Wx,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xda */  iemOp_pminub_Pq_Qq,         iemOp_pminub_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xdb */  iemOp_pand_Pq_Qq,           iemOp_pand_Vx_Wx,           iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xdc */  iemOp_paddusb_Pq_Qq,        iemOp_paddusb_Vx_Wx,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xdd */  iemOp_paddusw_Pq_Qq,        iemOp_paddusw_Vx_Wx,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xde */  iemOp_pmaxub_Pq_Qq,         iemOp_pmaxub_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xdf */  iemOp_pandn_Pq_Qq,          iemOp_pandn_Vx_Wx,          iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,

    /* 0xe0 */  iemOp_pavgb_Pq_Qq,          iemOp_pavgb_Vx_Wx,          iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xe1 */  iemOp_psraw_Pq_Qq,          iemOp_psraw_Vx_Wx,          iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xe2 */  iemOp_psrad_Pq_Qq,          iemOp_psrad_Vx_Wx,          iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xe3 */  iemOp_pavgw_Pq_Qq,          iemOp_pavgw_Vx_Wx,          iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xe4 */  iemOp_pmulhuw_Pq_Qq,        iemOp_pmulhuw_Vx_Wx,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xe5 */  iemOp_pmulhw_Pq_Qq,         iemOp_pmulhw_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xe6 */  iemOp_InvalidNeedRM,        iemOp_cvttpd2dq_Vx_Wpd,     iemOp_cvtdq2pd_Vx_Wpd,      iemOp_cvtpd2dq_Vx_Wpd,
    /* 0xe7 */  iemOp_movntq_Mq_Pq,         iemOp_movntdq_Mdq_Vdq,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xe8 */  iemOp_psubsb_Pq_Qq,         iemOp_psubsb_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xe9 */  iemOp_psubsw_Pq_Qq,         iemOp_psubsw_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xea */  iemOp_pminsw_Pq_Qq,         iemOp_pminsw_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xeb */  iemOp_por_Pq_Qq,            iemOp_por_Vx_Wx,            iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xec */  iemOp_paddsb_Pq_Qq,         iemOp_paddsb_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xed */  iemOp_paddsw_Pq_Qq,         iemOp_paddsw_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xee */  iemOp_pmaxsw_Pq_Qq,         iemOp_pmaxsw_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xef */  iemOp_pxor_Pq_Qq,           iemOp_pxor_Vx_Wx,           iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,

    /* 0xf0 */  iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,        iemOp_lddqu_Vx_Mx,
    /* 0xf1 */  iemOp_psllw_Pq_Qq,          iemOp_psllw_Vx_Wx,          iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xf2 */  iemOp_pslld_Pq_Qq,          iemOp_pslld_Vx_Wx,          iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xf3 */  iemOp_psllq_Pq_Qq,          iemOp_psllq_Vx_Wx,          iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xf4 */  iemOp_pmuludq_Pq_Qq,        iemOp_pmuludq_Vx_Wx,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xf5 */  iemOp_pmaddwd_Pq_Qq,        iemOp_pmaddwd_Vx_Wx,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xf6 */  iemOp_psadbw_Pq_Qq,         iemOp_psadbw_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xf7 */  iemOp_maskmovq_Pq_Nq,       iemOp_maskmovdqu_Vdq_Udq,   iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xf8 */  iemOp_psubb_Pq_Qq,          iemOp_psubb_Vx_Wx,          iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xf9 */  iemOp_psubw_Pq_Qq,          iemOp_psubw_Vx_Wx,          iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xfa */  iemOp_psubd_Pq_Qq,          iemOp_psubd_Vx_Wx,          iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xfb */  iemOp_psubq_Pq_Qq,          iemOp_psubq_Vx_Wx,          iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xfc */  iemOp_paddb_Pq_Qq,          iemOp_paddb_Vx_Wx,          iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xfd */  iemOp_paddw_Pq_Qq,          iemOp_paddw_Vx_Wx,          iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xfe */  iemOp_paddd_Pq_Qq,          iemOp_paddd_Vx_Wx,          iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xff */  IEMOP_X4(iemOp_ud0),
};
AssertCompile(RT_ELEMENTS(g_apfnTwoByteMap) == 1024);

/** @} */

