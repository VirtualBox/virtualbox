/* $Id: IEMAllInstThree0f38.cpp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEM - Instruction Decoding and Emulation.
 *
 * @remarks IEMAllInstVexMap2.cpp.h is a VEX mirror of this file.
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


/** @name Three byte opcodes with first two bytes 0x0f 0x38
 * @{
 */

/**
 * Common worker for MMX instructions on the form:
 *      pxxx    mm1, mm2/mem64
 * that was introduced with SSE3.
 *
 * The @a pfnU64 worker function takes no FXSAVE state, just the operands.
 */
FNIEMOP_DEF_1(iemOpCommonMmxOpt_FullFull_To_Full_Ssse3, PFNIEMAIMPLMEDIAOPTF2U64, pfnU64)
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
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSsse3);
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
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSsse3);
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
 * Common worker for SSSE3 instructions on the forms:
 *      pxxx    xmm1, xmm2/mem128
 *
 * Proper alignment of the 128-bit operand is enforced.
 * Exceptions type 4. SSSE3 cpuid checks.
 *
 * The @a pfnU128 worker function takes no FXSAVE state, just the operands.
 *
 * @sa  iemOpCommonSse2Opt_FullFull_To_Full
 */
FNIEMOP_DEF_1(iemOpCommonSsse3Opt_FullFull_To_Full, PFNIEMAIMPLMEDIAOPTF2U128, pfnU128)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSsse3);
        IEM_MC_ARG(PRTUINT128U,                 puDst, 0);
        IEM_MC_ARG(PCRTUINT128U,                puSrc, 1);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst,             IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(puSrc,       IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU128, puDst, puSrc);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(PRTUINT128U,                 puDst,       0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,      puSrc, uSrc, 1);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSsse3);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst,             IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU128, puDst, puSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * Common worker for SSE4.1 instructions on the forms:
 *      pxxx    xmm1, xmm2/mem128
 *
 * Proper alignment of the 128-bit operand is enforced.
 * Exceptions type 4. SSE4.1 cpuid checks.
 *
 * The @a pfnU128 worker function takes no FXSAVE state, just the operands.
 *
 * @sa  iemOpCommonSse2Opt_FullFull_To_Full, iemOpCommonSsse3Opt_FullFull_To_Full,
 *      iemOpCommonSse41Opt_FullFull_To_Full, iemOpCommonSse42Opt_FullFull_To_Full
 */
FNIEMOP_DEF_1(iemOpCommonSse41Opt_FullFull_To_Full, PFNIEMAIMPLMEDIAOPTF2U128, pfnU128)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
        IEM_MC_ARG(PRTUINT128U,                 puDst, 0);
        IEM_MC_ARG(PCRTUINT128U,                puSrc, 1);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst,             IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(puSrc,       IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU128, puDst, puSrc);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(PRTUINT128U,                 puDst,       0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,      puSrc, uSrc, 1);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst,             IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU128, puDst, puSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * Common worker for SSE4.2 instructions on the forms:
 *      pxxx    xmm1, xmm2/mem128
 *
 * Proper alignment of the 128-bit operand is enforced.
 * Exceptions type 4. SSE4.2 cpuid checks.
 *
 * @sa  iemOpCommonSse2Opt_FullFull_To_Full, iemOpCommonSsse3Opt_FullFull_To_Full,
 *      iemOpCommonSse41Opt_FullFull_To_Full
 */
FNIEMOP_DEF_1(iemOpCommonSse42Opt_FullFull_To_Full, PFNIEMAIMPLMEDIAOPTF2U128, pfnU128)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse42);
        IEM_MC_ARG(PRTUINT128U,                 puDst, 0);
        IEM_MC_ARG(PCRTUINT128U,                puSrc, 1);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst,             IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(puSrc,       IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU128, puDst, puSrc);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(PRTUINT128U,                 puDst,       0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,      puSrc, uSrc, 1);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse42);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst,             IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU128, puDst, puSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * Common worker for SSE-style AES-NI instructions of the form:
 *      aesxxx  xmm1, xmm2/mem128
 *
 * Proper alignment of the 128-bit operand is enforced.
 * Exceptions type 4. AES-NI cpuid checks.
 *
 * Unlike iemOpCommonSse41_FullFull_To_Full, the @a pfnU128 worker function
 * takes no FXSAVE state, just the operands.
 *
 * @sa  iemOpCommonSse2_FullFull_To_Full, iemOpCommonSsse3_FullFull_To_Full,
 *      iemOpCommonSse41_FullFull_To_Full, iemOpCommonSha_FullFull_To_Full
 */
FNIEMOP_DEF_1(iemOpCommonAesNi_FullFull_To_Full, PFNIEMAIMPLMEDIAOPTF2U128, pfnU128)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fAesNi);
        IEM_MC_ARG(PRTUINT128U,                 puDst, 0);
        IEM_MC_ARG(PCRTUINT128U,                puSrc, 1);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst,             IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(puSrc,       IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU128, puDst, puSrc);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(PRTUINT128U,                 puDst,       0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,      puSrc, uSrc, 1);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fAesNi);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst,             IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU128, puDst, puSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * Common worker for SSE-style SHA instructions of the form:
 *      shaxxx  xmm1, xmm2/mem128
 *
 * Proper alignment of the 128-bit operand is enforced.
 * Exceptions type 4. SHA cpuid checks.
 *
 * Unlike iemOpCommonSse41_FullFull_To_Full, the @a pfnU128 worker function
 * takes no FXSAVE state, just the operands.
 *
 * @sa  iemOpCommonSse2_FullFull_To_Full, iemOpCommonSsse3_FullFull_To_Full,
 *      iemOpCommonSse41_FullFull_To_Full, iemOpCommonSse42_FullFull_To_Full,
 *      iemOpCommonAesNi_FullFull_To_Full
 */
FNIEMOP_DEF_1(iemOpCommonSha_FullFull_To_Full, PFNIEMAIMPLMEDIAOPTF2U128, pfnU128)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSha);
        IEM_MC_ARG(PRTUINT128U,                 puDst, 0);
        IEM_MC_ARG(PCRTUINT128U,                puSrc, 1);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst,             IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(puSrc,       IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU128, puDst, puSrc);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(PRTUINT128U,                 puDst,       0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,      puSrc, uSrc, 1);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSha);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst,             IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_2(pfnU128, puDst, puSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode      0x0f 0x38 0x00. */
FNIEMOP_DEF(iemOp_pshufb_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PSHUFB, pshufb, Pq, Qq, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full_Ssse3,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_pshufb_u64,&iemAImpl_pshufb_u64_fallback));
}


/** Opcode 0x66 0x0f 0x38 0x00. */
FNIEMOP_DEF(iemOp_pshufb_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PSHUFB, pshufb, Vx, Wx, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSsse3Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_pshufb_u128, iemAImpl_pshufb_u128_fallback));

}


/*  Opcode      0x0f 0x38 0x01. */
FNIEMOP_DEF(iemOp_phaddw_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PHADDW, phaddw, Pq, Qq, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full_Ssse3,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_phaddw_u64,&iemAImpl_phaddw_u64_fallback));
}


/** Opcode 0x66 0x0f 0x38 0x01. */
FNIEMOP_DEF(iemOp_phaddw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PHADDW, phaddw, Vx, Wx, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSsse3Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_phaddw_u128, iemAImpl_phaddw_u128_fallback));

}


/** Opcode      0x0f 0x38 0x02. */
FNIEMOP_DEF(iemOp_phaddd_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PHADDD, phaddd, Pq, Qq, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full_Ssse3,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_phaddd_u64,&iemAImpl_phaddd_u64_fallback));
}


/** Opcode 0x66 0x0f 0x38 0x02. */
FNIEMOP_DEF(iemOp_phaddd_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PHADDD, phaddd, Vx, Wx, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSsse3Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_phaddd_u128, iemAImpl_phaddd_u128_fallback));

}


/** Opcode      0x0f 0x38 0x03. */
FNIEMOP_DEF(iemOp_phaddsw_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PHADDSW, phaddsw, Pq, Qq, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full_Ssse3,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_phaddsw_u64,&iemAImpl_phaddsw_u64_fallback));
}


/** Opcode 0x66 0x0f 0x38 0x03. */
FNIEMOP_DEF(iemOp_phaddsw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PHADDSW, phaddsw, Vx, Wx, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSsse3Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_phaddsw_u128, iemAImpl_phaddsw_u128_fallback));

}


/** Opcode      0x0f 0x38 0x04. */
FNIEMOP_DEF(iemOp_pmaddubsw_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PMADDUBSW, pmaddubsw, Pq, Qq, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full_Ssse3,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_pmaddubsw_u64, &iemAImpl_pmaddubsw_u64_fallback));
}


/** Opcode 0x66 0x0f 0x38 0x04. */
FNIEMOP_DEF(iemOp_pmaddubsw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PMADDUBSW, pmaddubsw, Vx, Wx, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSsse3Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_pmaddubsw_u128, iemAImpl_pmaddubsw_u128_fallback));

}


/** Opcode      0x0f 0x38 0x05. */
FNIEMOP_DEF(iemOp_phsubw_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PHSUBW, phsubw, Pq, Qq, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full_Ssse3,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_phsubw_u64,&iemAImpl_phsubw_u64_fallback));
}


/** Opcode 0x66 0x0f 0x38 0x05. */
FNIEMOP_DEF(iemOp_phsubw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PHSUBW, phsubw, Vx, Wx, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSsse3Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_phsubw_u128, iemAImpl_phsubw_u128_fallback));

}


/** Opcode      0x0f 0x38 0x06. */
FNIEMOP_DEF(iemOp_phsubd_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PHSUBD, phsubd, Pq, Qq, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full_Ssse3,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_phsubd_u64,&iemAImpl_phsubd_u64_fallback));
}



/** Opcode 0x66 0x0f 0x38 0x06. */
FNIEMOP_DEF(iemOp_phsubd_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PHSUBD, phsubd, Vx, Wx, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSsse3Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_phsubd_u128, iemAImpl_phsubd_u128_fallback));

}


/** Opcode      0x0f 0x38 0x07. */
FNIEMOP_DEF(iemOp_phsubsw_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PHSUBSW, phsubsw, Pq, Qq, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full_Ssse3,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_phsubsw_u64,&iemAImpl_phsubsw_u64_fallback));
}


/** Opcode 0x66 0x0f 0x38 0x07. */
FNIEMOP_DEF(iemOp_phsubsw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PHSUBSW, phsubsw, Vx, Wx, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSsse3Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_phsubsw_u128, iemAImpl_phsubsw_u128_fallback));

}


/** Opcode      0x0f 0x38 0x08. */
FNIEMOP_DEF(iemOp_psignb_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PSIGNB, psignb, Pq, Qq, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full_Ssse3,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_psignb_u64, &iemAImpl_psignb_u64_fallback));
}


/** Opcode 0x66 0x0f 0x38 0x08. */
FNIEMOP_DEF(iemOp_psignb_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PSIGNB, psignb, Vx, Wx, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSsse3Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_psignb_u128, iemAImpl_psignb_u128_fallback));

}


/** Opcode      0x0f 0x38 0x09. */
FNIEMOP_DEF(iemOp_psignw_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PSIGNW, psignw, Pq, Qq, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full_Ssse3,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_psignw_u64, &iemAImpl_psignw_u64_fallback));
}


/** Opcode 0x66 0x0f 0x38 0x09. */
FNIEMOP_DEF(iemOp_psignw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PSIGNW, psignw, Vx, Wx, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSsse3Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_psignw_u128, iemAImpl_psignw_u128_fallback));

}


/** Opcode      0x0f 0x38 0x0a. */
FNIEMOP_DEF(iemOp_psignd_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PSIGND, psignd, Pq, Qq, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full_Ssse3,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_psignd_u64, &iemAImpl_psignd_u64_fallback));
}


/** Opcode 0x66 0x0f 0x38 0x0a. */
FNIEMOP_DEF(iemOp_psignd_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PSIGND, psignd, Vx, Wx, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSsse3Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_psignd_u128, iemAImpl_psignd_u128_fallback));

}


/** Opcode      0x0f 0x38 0x0b. */
FNIEMOP_DEF(iemOp_pmulhrsw_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PMULHRSW, pmulhrsw, Pq, Qq, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full_Ssse3,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_pmulhrsw_u64, &iemAImpl_pmulhrsw_u64_fallback));
}


/** Opcode 0x66 0x0f 0x38 0x0b. */
FNIEMOP_DEF(iemOp_pmulhrsw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PMULHRSW, pmulhrsw, Vx, Wx, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSsse3Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_pmulhrsw_u128, iemAImpl_pmulhrsw_u128_fallback));

}


/*  Opcode      0x0f 0x38 0x0c - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x0c - invalid (vex only). */
/*  Opcode      0x0f 0x38 0x0d - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x0d - invalid (vex only). */
/*  Opcode      0x0f 0x38 0x0e - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x0e - invalid (vex only). */
/*  Opcode      0x0f 0x38 0x0f - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x0f - invalid (vex only). */


/*  Opcode      0x0f 0x38 0x10 - invalid */


/** Body for the *blend* instructions. */
#define IEMOP_BODY_P_BLEND_X(a_Instr) \
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm); \
    if (IEM_IS_MODRM_REG_MODE(bRm)) \
    { \
        /* \
         * Register, register. \
         */ \
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0); \
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41); \
        IEM_MC_ARG(PRTUINT128U,  puDst,  0); \
        IEM_MC_ARG(PCRTUINT128U, puSrc,  1); \
        IEM_MC_ARG(PCRTUINT128U, puMask, 2); \
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();  \
        IEM_MC_PREPARE_SSE_USAGE();  \
        IEM_MC_REF_XREG_U128(puDst,  IEM_GET_MODRM_REG(pVCpu, bRm)); \
        IEM_MC_REF_XREG_U128_CONST(puSrc,  IEM_GET_MODRM_RM(pVCpu, bRm)); \
        IEM_MC_REF_XREG_U128_CONST(puMask, 0); \
        IEM_MC_CALL_VOID_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fSse41, \
                                                             iemAImpl_ ## a_Instr ## _u128, \
                                                             iemAImpl_ ## a_Instr ## _u128_fallback), \
                                 puDst, puSrc, puMask); \
        IEM_MC_ADVANCE_RIP_AND_FINISH(); \
        IEM_MC_END(); \
    } \
    else \
    { \
        /* \
         * Register, memory. \
         */ \
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0); \
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc); \
        IEM_MC_LOCAL(RTUINT128U,                uSrc); \
        IEM_MC_ARG(PRTUINT128U,            puDst,       0); \
        IEM_MC_ARG_LOCAL_REF(PCRTUINT128U, puSrc, uSrc, 1); \
        IEM_MC_ARG(PCRTUINT128U,           puMask,      2); \
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0); \
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41); \
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT(); \
        IEM_MC_PREPARE_SSE_USAGE(); \
        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc); \
        IEM_MC_REF_XREG_U128(puDst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
        IEM_MC_REF_XREG_U128_CONST(puMask, 0); \
        IEM_MC_CALL_VOID_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fSse41, \
                                                             iemAImpl_ ## a_Instr ## _u128, \
                                                             iemAImpl_ ## a_Instr ## _u128_fallback), \
                                 puDst, puSrc, puMask); \
        IEM_MC_ADVANCE_RIP_AND_FINISH(); \
        IEM_MC_END(); \
    } \
    (void)0

/** Opcode 0x66 0x0f 0x38 0x10 (legacy only). */
FNIEMOP_DEF(iemOp_pblendvb_Vdq_Wdq)
{
    IEMOP_MNEMONIC3EX(pblendvb_Vdq_Wdq_XMM0, "pblendvb Vdq,Wdq,xmm0",
                      RM0, PBLENDVB, pblendvb, Vdq, Wdq, REG_XMM0, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    IEMOP_BODY_P_BLEND_X(pblendvb);
}


/*  Opcode      0x0f 0x38 0x11 - invalid */
/*  Opcode 0x66 0x0f 0x38 0x11 - invalid */
/*  Opcode      0x0f 0x38 0x12 - invalid */
/*  Opcode 0x66 0x0f 0x38 0x12 - invalid */
/*  Opcode      0x0f 0x38 0x13 - invalid */
/*  Opcode 0x66 0x0f 0x38 0x13 - invalid (vex only). */
/*  Opcode      0x0f 0x38 0x14 - invalid */


/** Opcode 0x66 0x0f 0x38 0x14 (legacy only). */
FNIEMOP_DEF(iemOp_blendvps_Vdq_Wdq)
{
    IEMOP_MNEMONIC3EX(blendvps_Vdq_Wdq_XMM0, "blendvps Vdq,Wdq,xmm0",
                      RM0, BLENDVPS, blendvps, Vdq, Wdq, REG_XMM0, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    IEMOP_BODY_P_BLEND_X(blendvps);
}


/*  Opcode      0x0f 0x38 0x15 - invalid */


/** Opcode 0x66 0x0f 0x38 0x15 (legacy only). */
FNIEMOP_DEF(iemOp_blendvpd_Vdq_Wdq)
{
    IEMOP_MNEMONIC3EX(blendvpd_Vdq_Wdq_XMM0, "blendvpd Vdq,Wdq,xmm0",
                      RM0, BLENDVPD, blendvpd, Vdq, Wdq, REG_XMM0, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    IEMOP_BODY_P_BLEND_X(blendvpd);
}


/*  Opcode      0x0f 0x38 0x16 - invalid */
/*  Opcode 0x66 0x0f 0x38 0x16 - invalid (vex only). */
/*  Opcode      0x0f 0x38 0x17 - invalid */


/**
 * @opcode      0x17
 * @oppfx       0x66
 * @opflmodify  cf,pf,af,zf,sf,of
 * @opflclear   pf,af,sf,of
 */
FNIEMOP_DEF(iemOp_ptest_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PTEST, ptest, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
        IEM_MC_ARG(PCRTUINT128U,                puSrc1,  0);
        IEM_MC_ARG(PCRTUINT128U,                puSrc2,  1);
        IEM_MC_ARG(uint32_t *,                  pEFlags, 2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128_CONST(puSrc1,      IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(puSrc2,      IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_REF_EFLAGS(pEFlags);
        IEM_MC_CALL_VOID_AIMPL_3(iemAImpl_ptest_u128, puSrc1, puSrc2, pEFlags);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(PCRTUINT128U,                puSrc1,        0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc2);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,      puSrc2, uSrc2, 1);
        IEM_MC_ARG(uint32_t *,                  pEFlags,       2);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128_CONST(puSrc1,      IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_EFLAGS(pEFlags);
        IEM_MC_CALL_VOID_AIMPL_3(iemAImpl_ptest_u128, puSrc1, puSrc2, pEFlags);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/*  Opcode      0x0f 0x38 0x18 - invalid */
/*  Opcode 0x66 0x0f 0x38 0x18 - invalid (vex only). */
/*  Opcode      0x0f 0x38 0x19 - invalid */
/*  Opcode 0x66 0x0f 0x38 0x19 - invalid (vex only). */
/*  Opcode      0x0f 0x38 0x1a - invalid */
/*  Opcode 0x66 0x0f 0x38 0x1a - invalid (vex only). */
/*  Opcode      0x0f 0x38 0x1b - invalid */
/*  Opcode 0x66 0x0f 0x38 0x1b - invalid */


/** Opcode      0x0f 0x38 0x1c. */
FNIEMOP_DEF(iemOp_pabsb_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PABSB, pabsb, Pq, Qq, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full_Ssse3,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_pabsb_u64, &iemAImpl_pabsb_u64_fallback));
}


/** Opcode 0x66 0x0f 0x38 0x1c. */
FNIEMOP_DEF(iemOp_pabsb_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PABSB, pabsb, Vx, Wx, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSsse3Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_pabsb_u128, iemAImpl_pabsb_u128_fallback));

}


/** Opcode      0x0f 0x38 0x1d. */
FNIEMOP_DEF(iemOp_pabsw_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PABSW, pabsw, Pq, Qq, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full_Ssse3,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_pabsw_u64, &iemAImpl_pabsw_u64_fallback));
}


/** Opcode 0x66 0x0f 0x38 0x1d. */
FNIEMOP_DEF(iemOp_pabsw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PABSW, pabsw, Vx, Wx, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSsse3Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_pabsw_u128, iemAImpl_pabsw_u128_fallback));

}


/** Opcode      0x0f 0x38 0x1e. */
FNIEMOP_DEF(iemOp_pabsd_Pq_Qq)
{
    IEMOP_MNEMONIC2(RM, PABSD, pabsd, Pq, Qq, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonMmxOpt_FullFull_To_Full_Ssse3,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_pabsd_u64, &iemAImpl_pabsd_u64_fallback));
}


/** Opcode 0x66 0x0f 0x38 0x1e. */
FNIEMOP_DEF(iemOp_pabsd_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PABSD, pabsd, Vx, Wx, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSsse3Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_pabsd_u128, iemAImpl_pabsd_u128_fallback));

}


/*  Opcode      0x0f 0x38 0x1f - invalid */
/*  Opcode 0x66 0x0f 0x38 0x1f - invalid */


/** Body for the pmov{s,z}x* instructions. */
#define IEMOP_BODY_PMOV_S_Z(a_Instr, a_SrcWidth, a_fRegNativeArchs, a_fMemNativeArchs) \
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm); \
    if (IEM_IS_MODRM_REG_MODE(bRm)) \
    { \
        /* \
         * Register, register. \
         */ \
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0); \
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41); \
        IEM_MC_ARG(PRTUINT128U,                 puDst, 0); \
        IEM_MC_ARG(uint ## a_SrcWidth ## _t,    uSrc, 1); \
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();  \
        IEM_MC_PREPARE_SSE_USAGE();  \
        IEM_MC_NATIVE_IF(a_fRegNativeArchs) { \
            IEM_MC_NATIVE_EMIT_2(RT_CONCAT3(iemNativeEmit_,a_Instr,_rr_u128), IEM_GET_MODRM_REG(pVCpu, bRm), IEM_GET_MODRM_RM(pVCpu, bRm)); \
        } IEM_MC_NATIVE_ELSE() { \
            IEM_MC_FETCH_XREG_U ## a_SrcWidth (uSrc, IEM_GET_MODRM_RM(pVCpu, bRm), 0); \
            IEM_MC_REF_XREG_U128(puDst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
            IEM_MC_CALL_VOID_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fSse41, \
                                                                 iemAImpl_ ## a_Instr ## _u128, \
                                                                 iemAImpl_v ## a_Instr ## _u128_fallback), \
                                     puDst, uSrc); \
        } IEM_MC_NATIVE_ENDIF(); \
        IEM_MC_ADVANCE_RIP_AND_FINISH(); \
        IEM_MC_END(); \
    } \
    else \
    { \
        /* \
         * Register, memory. \
         */ \
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0); \
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc); \
        IEM_MC_ARG(PRTUINT128U,                 puDst, 0); \
        IEM_MC_ARG(uint ## a_SrcWidth ## _t,    uSrc, 1); \
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0); \
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41); \
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT(); \
        IEM_MC_PREPARE_SSE_USAGE(); \
        IEM_MC_FETCH_MEM_U## a_SrcWidth (uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc); \
        IEM_MC_NATIVE_IF(a_fMemNativeArchs) { \
            IEM_MC_NATIVE_EMIT_2(RT_CONCAT3(iemNativeEmit_,a_Instr,_rv_u128), IEM_GET_MODRM_REG(pVCpu, bRm), uSrc); \
        } IEM_MC_NATIVE_ELSE() { \
            IEM_MC_REF_XREG_U128(puDst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
            IEM_MC_CALL_VOID_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fSse41, \
                                                                 iemAImpl_ ## a_Instr ## _u128, \
                                                                 iemAImpl_v ## a_Instr ## _u128_fallback), \
                                     puDst, uSrc); \
        } IEM_MC_NATIVE_ENDIF(); \
        IEM_MC_ADVANCE_RIP_AND_FINISH(); \
        IEM_MC_END(); \
    } \
    (void)0


/** Opcode 0x66 0x0f 0x38 0x20. */
FNIEMOP_DEF(iemOp_pmovsxbw_Vx_UxMq)
{
     /** @todo r=aeichner Review code, the naming of this function and the parameter type specifiers. */
    IEMOP_MNEMONIC2(RM, PMOVSXBW, pmovsxbw, Vx, Wq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    IEMOP_BODY_PMOV_S_Z(pmovsxbw, 64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/** Opcode 0x66 0x0f 0x38 0x21. */
FNIEMOP_DEF(iemOp_pmovsxbd_Vx_UxMd)
{
     /** @todo r=aeichner Review code, the naming of this function and the parameter type specifiers. */
    IEMOP_MNEMONIC2(RM, PMOVSXBD, pmovsxbd, Vx, Wq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    IEMOP_BODY_PMOV_S_Z(pmovsxbd, 32, 0, 0);
}


/** Opcode 0x66 0x0f 0x38 0x22. */
FNIEMOP_DEF(iemOp_pmovsxbq_Vx_UxMw)
{
     /** @todo r=aeichner Review code, the naming of this function and the parameter type specifiers. */
    IEMOP_MNEMONIC2(RM, PMOVSXBQ, pmovsxbq, Vx, Wq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    IEMOP_BODY_PMOV_S_Z(pmovsxbq, 16, 0, 0);
}


/** Opcode 0x66 0x0f 0x38 0x23. */
FNIEMOP_DEF(iemOp_pmovsxwd_Vx_UxMq)
{
     /** @todo r=aeichner Review code, the naming of this function and the parameter type specifiers. */
    IEMOP_MNEMONIC2(RM, PMOVSXWD, pmovsxwd, Vx, Wq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    IEMOP_BODY_PMOV_S_Z(pmovsxwd, 64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/** Opcode 0x66 0x0f 0x38 0x24. */
FNIEMOP_DEF(iemOp_pmovsxwq_Vx_UxMd)
{
     /** @todo r=aeichner Review code, the naming of this function and the parameter type specifiers. */
    IEMOP_MNEMONIC2(RM, PMOVSXWQ, pmovsxwq, Vx, Wq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    IEMOP_BODY_PMOV_S_Z(pmovsxwq, 32, 0, 0);
}


/** Opcode 0x66 0x0f 0x38 0x25. */
FNIEMOP_DEF(iemOp_pmovsxdq_Vx_UxMq)
{
     /** @todo r=aeichner Review code, the naming of this function and the parameter type specifiers. */
    IEMOP_MNEMONIC2(RM, PMOVSXDQ, pmovsxdq, Vx, Wq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    IEMOP_BODY_PMOV_S_Z(pmovsxdq, 64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/*  Opcode 0x66 0x0f 0x38 0x26 - invalid */
/*  Opcode 0x66 0x0f 0x38 0x27 - invalid */


/** Opcode 0x66 0x0f 0x38 0x28. */
FNIEMOP_DEF(iemOp_pmuldq_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PMULDQ, pmuldq, Vx, Wx, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse41Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSse41, iemAImpl_pmuldq_u128, iemAImpl_pmuldq_u128_fallback));
}


/** Opcode 0x66 0x0f 0x38 0x29. */
FNIEMOP_DEF(iemOp_pcmpeqq_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PCMPEQQ, pcmpeqq, Vx, Wx, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse41Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSse41, iemAImpl_pcmpeqq_u128, iemAImpl_pcmpeqq_u128_fallback));
}


/**
 * @opcode      0x2a
 * @opcodesub   !11 mr/reg
 * @oppfx       0x66
 * @opcpuid     sse4.1
 * @opgroup     og_sse41_cachect
 * @opxcpttype  1
 * @optest      op1=-1 op2=2  -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_movntdqa_Vdq_Mdq)
{
    IEMOP_MNEMONIC2(RM_MEM, MOVNTDQA, movntdqa, Vdq_WO, Mdq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_MEM_MODE(bRm))
    {
        /* Register, memory. */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_ACTUALIZE_SSE_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_XREG_U128(IEM_GET_MODRM_REG(pVCpu, bRm), uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }

    /**
     * @opdone
     * @opmnemonic  ud660f382areg
     * @opcode      0x2a
     * @opcodesub   11 mr/reg
     * @oppfx       0x66
     * @opunused    immediate
     * @opcpuid     sse
     * @optest      ->
     */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}


/** Opcode 0x66 0x0f 0x38 0x2b. */
FNIEMOP_DEF(iemOp_packusdw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PACKUSDW, packusdw, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse41Opt_FullFull_To_Full, iemAImpl_packusdw_u128);
}


/*  Opcode 0x66 0x0f 0x38 0x2c - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0x2d - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0x2e - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0x2f - invalid (vex only). */

/** Opcode 0x66 0x0f 0x38 0x30. */
FNIEMOP_DEF(iemOp_pmovzxbw_Vx_UxMq)
{
     /** @todo r=aeichner Review code, the naming of this function and the parameter type specifiers. */
    IEMOP_MNEMONIC2(RM, PMOVZXBW, pmovzxbw, Vx, Wq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    IEMOP_BODY_PMOV_S_Z(pmovzxbw, 64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/** Opcode 0x66 0x0f 0x38 0x31. */
FNIEMOP_DEF(iemOp_pmovzxbd_Vx_UxMd)
{
     /** @todo r=aeichner Review code, the naming of this function and the parameter type specifiers. */
    IEMOP_MNEMONIC2(RM, PMOVZXBD, pmovzxbd, Vx, Wq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    IEMOP_BODY_PMOV_S_Z(pmovzxbd, 32, 0, 0);
}


/** Opcode 0x66 0x0f 0x38 0x32. */
FNIEMOP_DEF(iemOp_pmovzxbq_Vx_UxMw)
{
     /** @todo r=aeichner Review code, the naming of this function and the parameter type specifiers. */
    IEMOP_MNEMONIC2(RM, PMOVZXBQ, pmovzxbq, Vx, Wq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    IEMOP_BODY_PMOV_S_Z(pmovzxbq, 16, 0, 0);
}


/** Opcode 0x66 0x0f 0x38 0x33. */
FNIEMOP_DEF(iemOp_pmovzxwd_Vx_UxMq)
{
     /** @todo r=aeichner Review code, the naming of this function and the parameter type specifiers. */
    IEMOP_MNEMONIC2(RM, PMOVZXWD, pmovzxwd, Vx, Wq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    IEMOP_BODY_PMOV_S_Z(pmovzxwd, 64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/** Opcode 0x66 0x0f 0x38 0x34. */
FNIEMOP_DEF(iemOp_pmovzxwq_Vx_UxMd)
{
     /** @todo r=aeichner Review code, the naming of this function and the parameter type specifiers. */
    IEMOP_MNEMONIC2(RM, PMOVZXWQ, pmovzxwq, Vx, Wq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    IEMOP_BODY_PMOV_S_Z(pmovzxwq, 32, 0, 0);
}


/** Opcode 0x66 0x0f 0x38 0x35. */
FNIEMOP_DEF(iemOp_pmovzxdq_Vx_UxMq)
{
     /** @todo r=aeichner Review code, the naming of this function and the parameter type specifiers. */
    IEMOP_MNEMONIC2(RM, PMOVZXDQ, pmovzxdq, Vx, Wq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    IEMOP_BODY_PMOV_S_Z(pmovzxdq, 64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64, RT_ARCH_VAL_AMD64 | RT_ARCH_VAL_ARM64);
}


/*  Opcode 0x66 0x0f 0x38 0x36 - invalid (vex only). */


/** Opcode 0x66 0x0f 0x38 0x37. */
FNIEMOP_DEF(iemOp_pcmpgtq_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PCMPGTQ, pcmpgtq, Vx, Wx, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse42Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSse42, iemAImpl_pcmpgtq_u128, iemAImpl_pcmpgtq_u128_fallback));
}


/** Opcode 0x66 0x0f 0x38 0x38. */
FNIEMOP_DEF(iemOp_pminsb_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PMINSB, pminsb, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse41Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSse41, iemAImpl_pminsb_u128, iemAImpl_pminsb_u128_fallback));
}


/** Opcode 0x66 0x0f 0x38 0x39. */
FNIEMOP_DEF(iemOp_pminsd_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PMINSD, pminsd, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse41Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSse41, iemAImpl_pminsd_u128, iemAImpl_pminsd_u128_fallback));
}


/** Opcode 0x66 0x0f 0x38 0x3a. */
FNIEMOP_DEF(iemOp_pminuw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PMINUW, pminuw, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse41Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSse41, iemAImpl_pminuw_u128, iemAImpl_pminuw_u128_fallback));
}


/** Opcode 0x66 0x0f 0x38 0x3b. */
FNIEMOP_DEF(iemOp_pminud_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PMINUD, pminud, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse41Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSse41, iemAImpl_pminud_u128, iemAImpl_pminud_u128_fallback));
}


/** Opcode 0x66 0x0f 0x38 0x3c. */
FNIEMOP_DEF(iemOp_pmaxsb_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PMAXSB, pmaxsb, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse41Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSse41, iemAImpl_pmaxsb_u128, iemAImpl_pmaxsb_u128_fallback));
}


/** Opcode 0x66 0x0f 0x38 0x3d. */
FNIEMOP_DEF(iemOp_pmaxsd_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PMAXSD, pmaxsd, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse41Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSse41, iemAImpl_pmaxsd_u128, iemAImpl_pmaxsd_u128_fallback));
}


/** Opcode 0x66 0x0f 0x38 0x3e. */
FNIEMOP_DEF(iemOp_pmaxuw_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PMAXUW, pmaxuw, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse41Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSse41, iemAImpl_pmaxuw_u128, iemAImpl_pmaxuw_u128_fallback));
}


/** Opcode 0x66 0x0f 0x38 0x3f. */
FNIEMOP_DEF(iemOp_pmaxud_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PMAXUD, pmaxud, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse41Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSse41, iemAImpl_pmaxud_u128, iemAImpl_pmaxud_u128_fallback));
}


/** Opcode 0x66 0x0f 0x38 0x40. */
FNIEMOP_DEF(iemOp_pmulld_Vx_Wx)
{
    IEMOP_MNEMONIC2(RM, PMULLD, pmulld, Vx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse41Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSse41, iemAImpl_pmulld_u128, iemAImpl_pmulld_u128_fallback));
}


/** Opcode 0x66 0x0f 0x38 0x41. */
FNIEMOP_DEF(iemOp_phminposuw_Vdq_Wdq)
{
    IEMOP_MNEMONIC2(RM, PHMINPOSUW, phminposuw, Vdq, Wdq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSse41Opt_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSse41, iemAImpl_phminposuw_u128, iemAImpl_phminposuw_u128_fallback));
}


/*  Opcode 0x66 0x0f 0x38 0x42 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x43 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x44 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x45 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0x46 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0x47 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0x48 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x49 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x4a - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x4b - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x4c - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x4d - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x4e - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x4f - invalid. */

/*  Opcode 0x66 0x0f 0x38 0x50 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x51 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x52 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x53 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x54 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x55 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x56 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x57 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x58 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0x59 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0x5a - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0x5b - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x5c - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x5d - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x5e - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x5f - invalid. */

/*  Opcode 0x66 0x0f 0x38 0x60 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x61 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x62 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x63 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x64 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x65 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x66 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x67 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x68 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x69 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x6a - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x6b - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x6c - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x6d - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x6e - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x6f - invalid. */

/*  Opcode 0x66 0x0f 0x38 0x70 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x71 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x72 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x73 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x74 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x75 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x76 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x77 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x78 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0x79 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0x7a - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x7b - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x7c - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x7d - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x7e - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x7f - invalid. */

/** Opcode 0x66 0x0f 0x38 0x80. */
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
FNIEMOP_DEF(iemOp_invept_Gy_Mdq)
{
    IEMOP_MNEMONIC(invept, "invept Gy,Mdq");
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_MEM_MODE(bRm))
    {
        /* Register, memory. */
        IEMOP_HLP_NO_REAL_OR_V86_MODE();
        if (pVCpu->iem.s.enmEffOpSize == IEMMODE_64BIT)
        {
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_ARG(RTGCPTR,         GCPtrInveptDesc,                        1);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrInveptDesc, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
            IEMOP_HLP_IN_VMX_OPERATION("invept", kVmxVDiag_Invept);
            IEMOP_HLP_VMX_INSTR(       "invept", kVmxVDiag_Invept);
            IEM_MC_ARG_CONST(uint8_t,   iEffSeg, /*=*/ pVCpu->iem.s.iEffSeg,    0);
            IEM_MC_ARG(uint64_t,        uInveptType,                            2);
            IEM_MC_FETCH_GREG_U64(uInveptType, IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_CALL_CIMPL_3(IEM_CIMPL_F_VMEXIT | IEM_CIMPL_F_STATUS_FLAGS, 0,
                                iemCImpl_invept, iEffSeg, GCPtrInveptDesc, uInveptType);
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_ARG(RTGCPTR,         GCPtrInveptDesc,                        1);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrInveptDesc, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
            IEMOP_HLP_IN_VMX_OPERATION("invept", kVmxVDiag_Invept);
            IEMOP_HLP_VMX_INSTR(       "invept", kVmxVDiag_Invept);
            IEM_MC_ARG_CONST(uint8_t,   iEffSeg, /*=*/ pVCpu->iem.s.iEffSeg,    0);
            IEM_MC_ARG(uint32_t,        uInveptType,                            2);
            IEM_MC_FETCH_GREG_U32(uInveptType, IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_CALL_CIMPL_3(IEM_CIMPL_F_VMEXIT | IEM_CIMPL_F_STATUS_FLAGS, 0,
                                iemCImpl_invept, iEffSeg, GCPtrInveptDesc, uInveptType);
            IEM_MC_END();
        }
    }
    Log(("iemOp_invept_Gy_Mdq: invalid encoding -> #UD\n"));
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEMOP_RAISE_INVALID_OPCODE_RET();
}
#else
FNIEMOP_UD_STUB(iemOp_invept_Gy_Mdq);
#endif

/** Opcode 0x66 0x0f 0x38 0x81. */
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
FNIEMOP_DEF(iemOp_invvpid_Gy_Mdq)
{
    IEMOP_MNEMONIC(invvpid, "invvpid Gy,Mdq");
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_MEM_MODE(bRm))
    {
        /* Register, memory. */
        IEMOP_HLP_NO_REAL_OR_V86_MODE();
        if (pVCpu->iem.s.enmEffOpSize == IEMMODE_64BIT)
        {
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_ARG(RTGCPTR,         GCPtrInvvpidDesc,                       1);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrInvvpidDesc, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
            IEMOP_HLP_IN_VMX_OPERATION("invvpid", kVmxVDiag_Invvpid);
            IEMOP_HLP_VMX_INSTR("invvpid", kVmxVDiag_Invvpid);
            IEM_MC_ARG_CONST(uint8_t,   iEffSeg, /*=*/ pVCpu->iem.s.iEffSeg,    0);
            IEM_MC_ARG(uint64_t,        uInvvpidType,                           2);
            IEM_MC_FETCH_GREG_U64(uInvvpidType, IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_CALL_CIMPL_3(IEM_CIMPL_F_VMEXIT | IEM_CIMPL_F_STATUS_FLAGS, 0,
                                iemCImpl_invvpid, iEffSeg, GCPtrInvvpidDesc, uInvvpidType);
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_ARG(RTGCPTR,         GCPtrInvvpidDesc,                       1);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrInvvpidDesc, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
            IEMOP_HLP_IN_VMX_OPERATION("invvpid", kVmxVDiag_Invvpid);
            IEMOP_HLP_VMX_INSTR("invvpid", kVmxVDiag_Invvpid);
            IEM_MC_ARG_CONST(uint8_t,   iEffSeg, /*=*/ pVCpu->iem.s.iEffSeg,    0);
            IEM_MC_ARG(uint32_t,        uInvvpidType,                           2);
            IEM_MC_FETCH_GREG_U32(uInvvpidType, IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_CALL_CIMPL_3(IEM_CIMPL_F_VMEXIT | IEM_CIMPL_F_STATUS_FLAGS, 0,
                                iemCImpl_invvpid, iEffSeg, GCPtrInvvpidDesc, uInvvpidType);
            IEM_MC_END();
        }
    }
    Log(("iemOp_invvpid_Gy_Mdq: invalid encoding -> #UD\n"));
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEMOP_RAISE_INVALID_OPCODE_RET();
}
#else
FNIEMOP_UD_STUB(iemOp_invvpid_Gy_Mdq);
#endif

/** Opcode 0x66 0x0f 0x38 0x82. */
FNIEMOP_DEF(iemOp_invpcid_Gy_Mdq)
{
    IEMOP_MNEMONIC(invpcid, "invpcid Gy,Mdq");
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_MEM_MODE(bRm))
    {
        /* Register, memory. */
        if (pVCpu->iem.s.enmEffOpSize == IEMMODE_64BIT)
        {
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_ARG(RTGCPTR,         GCPtrInvpcidDesc,                       1);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrInvpcidDesc, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
            IEM_MC_ARG_CONST(uint8_t,   iEffSeg, /*=*/ pVCpu->iem.s.iEffSeg,    0);
            IEM_MC_ARG(uint64_t,        uInvpcidType,                           2);
            IEM_MC_FETCH_GREG_U64(uInvpcidType, IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_CALL_CIMPL_3(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_invpcid, iEffSeg, GCPtrInvpcidDesc, uInvpcidType);
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_ARG(RTGCPTR,         GCPtrInvpcidDesc,                       1);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrInvpcidDesc, bRm, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
            IEM_MC_ARG_CONST(uint8_t,   iEffSeg, /*=*/ pVCpu->iem.s.iEffSeg,    0);
            IEM_MC_ARG(uint32_t,        uInvpcidType,                           2);
            IEM_MC_FETCH_GREG_U32(uInvpcidType, IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_CALL_CIMPL_3(IEM_CIMPL_F_VMEXIT, 0, iemCImpl_invpcid, iEffSeg, GCPtrInvpcidDesc, uInvpcidType);
            IEM_MC_END();
        }
    }
    Log(("iemOp_invpcid_Gy_Mdq: invalid encoding -> #UD\n"));
    IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
    IEMOP_RAISE_INVALID_OPCODE_RET();
}


/*  Opcode 0x66 0x0f 0x38 0x83 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x84 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x85 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x86 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x87 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x88 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x89 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x8a - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x8b - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x8c - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0x8d - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x8e - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0x8f - invalid. */

/*  Opcode 0x66 0x0f 0x38 0x90 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0x91 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0x92 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0x93 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0x94 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x95 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0x96 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0x97 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0x98 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0x99 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0x9a - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0x9b - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0x9c - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0x9d - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0x9e - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0x9f - invalid (vex only). */

/*  Opcode 0x66 0x0f 0x38 0xa0 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xa1 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xa2 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xa3 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xa4 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xa5 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xa6 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0xa7 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0xa8 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0xa9 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0xaa - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0xab - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0xac - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0xad - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0xae - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0xaf - invalid (vex only). */

/*  Opcode 0x66 0x0f 0x38 0xb0 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xb1 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xb2 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xb3 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xb4 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xb5 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xb6 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0xb7 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0xb8 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0xb9 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0xba - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0xbb - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0xbc - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0xbd - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0xbe - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0xbf - invalid (vex only). */

/*  Opcode      0x0f 0x38 0xc0 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xc0 - invalid. */
/*  Opcode      0x0f 0x38 0xc1 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xc1 - invalid. */
/*  Opcode      0x0f 0x38 0xc2 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xc2 - invalid. */
/*  Opcode      0x0f 0x38 0xc3 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xc3 - invalid. */
/*  Opcode      0x0f 0x38 0xc4 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xc4 - invalid. */
/*  Opcode      0x0f 0x38 0xc5 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xc5 - invalid. */
/*  Opcode      0x0f 0x38 0xc6 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xc6 - invalid. */
/*  Opcode      0x0f 0x38 0xc7 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xc7 - invalid. */


/** Opcode      0x0f 0x38 0xc8. */
FNIEMOP_DEF(iemOp_sha1nexte_Vdq_Wdq)
{
    IEMOP_MNEMONIC2(RM, SHA1NEXTE, sha1nexte, Vdq, Wdq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSha_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSha, iemAImpl_sha1nexte_u128, iemAImpl_sha1nexte_u128_fallback));
}


/*  Opcode 0x66 0x0f 0x38 0xc8 - invalid. */


/** Opcode      0x0f 0x38 0xc9. */
FNIEMOP_DEF(iemOp_sha1msg1_Vdq_Wdq)
{
    IEMOP_MNEMONIC2(RM, SHA1MSG1, sha1msg1, Vdq, Wdq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSha_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSha, iemAImpl_sha1msg1_u128, iemAImpl_sha1msg1_u128_fallback));
}


/*  Opcode 0x66 0x0f 0x38 0xc9 - invalid. */


/** Opcode      0x0f 0x38 0xca. */
FNIEMOP_DEF(iemOp_sha1msg2_Vdq_Wdq)
{
    IEMOP_MNEMONIC2(RM, SHA1MSG2, sha1msg2, Vdq, Wdq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSha_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSha, iemAImpl_sha1msg2_u128, iemAImpl_sha1msg2_u128_fallback));
}


/*  Opcode 0x66 0x0f 0x38 0xca - invalid. */


/** Opcode      0x0f 0x38 0xcb. */
FNIEMOP_DEF(iemOp_sha256rnds2_Vdq_Wdq)
{
    IEMOP_MNEMONIC2(RM, SHA256RNDS2, sha256rnds2, Vdq, Wdq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES); /** @todo Actually RMI with implicit XMM0 */
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSha);
        IEM_MC_ARG(PRTUINT128U,                 puDst, 0);
        IEM_MC_ARG(PCRTUINT128U,                puSrc, 1);
        IEM_MC_ARG(PCRTUINT128U,                puXmm0, 2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst,             IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(puSrc,       IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(puXmm0,      0);
        IEM_MC_CALL_VOID_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fSha, iemAImpl_sha256rnds2_u128, iemAImpl_sha256rnds2_u128_fallback),
                                 puDst, puSrc, puXmm0);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(PRTUINT128U,                 puDst,       0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,      puSrc, uSrc, 1);
        IEM_MC_ARG(PCRTUINT128U,                puXmm0,      2);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSha);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst,             IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(puXmm0,      0);
        IEM_MC_CALL_VOID_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fSha, iemAImpl_sha256rnds2_u128, iemAImpl_sha256rnds2_u128_fallback),
                                 puDst, puSrc, puXmm0);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/*  Opcode 0x66 0x0f 0x38 0xcb - invalid. */


/** Opcode      0x0f 0x38 0xcc. */
FNIEMOP_DEF(iemOp_sha256msg1_Vdq_Wdq)
{
    IEMOP_MNEMONIC2(RM, SHA256MSG1, sha256msg1, Vdq, Wdq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSha_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSha, iemAImpl_sha256msg1_u128, iemAImpl_sha256msg1_u128_fallback));
}


/*  Opcode 0x66 0x0f 0x38 0xcc - invalid. */


/** Opcode      0x0f 0x38 0xcd. */
FNIEMOP_DEF(iemOp_sha256msg2_Vdq_Wdq)
{
    IEMOP_MNEMONIC2(RM, SHA256MSG2, sha256msg2, Vdq, Wdq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonSha_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSha, iemAImpl_sha256msg2_u128, iemAImpl_sha256msg2_u128_fallback));
}


/*  Opcode 0x66 0x0f 0x38 0xcd - invalid. */
/*  Opcode      0x0f 0x38 0xce - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xce - invalid. */
/*  Opcode      0x0f 0x38 0xcf - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xcf - invalid. */

/*  Opcode 0x66 0x0f 0x38 0xd0 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xd1 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xd2 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xd3 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xd4 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xd5 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xd6 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xd7 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xd8 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xd9 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xda - invalid. */


/** Opcode 0x66 0x0f 0x38 0xdb. */
FNIEMOP_DEF(iemOp_aesimc_Vdq_Wdq)
{
    IEMOP_MNEMONIC2(RM, AESIMC, aesimc, Vdq, Wdq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonAesNi_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fAesNi, iemAImpl_aesimc_u128, iemAImpl_aesimc_u128_fallback));
}


/** Opcode 0x66 0x0f 0x38 0xdc. */
FNIEMOP_DEF(iemOp_aesenc_Vdq_Wdq)
{
    IEMOP_MNEMONIC2(RM, AESENC, aesenc, Vdq, Wdq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonAesNi_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fAesNi, iemAImpl_aesenc_u128, iemAImpl_aesenc_u128_fallback));
}


/** Opcode 0x66 0x0f 0x38 0xdd. */
FNIEMOP_DEF(iemOp_aesenclast_Vdq_Wdq)
{
    IEMOP_MNEMONIC2(RM, AESENCLAST, aesenclast, Vdq, Wdq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonAesNi_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fAesNi, iemAImpl_aesenclast_u128, iemAImpl_aesenclast_u128_fallback));
}


/** Opcode 0x66 0x0f 0x38 0xde. */
FNIEMOP_DEF(iemOp_aesdec_Vdq_Wdq)
{
    IEMOP_MNEMONIC2(RM, AESDEC, aesdec, Vdq, Wdq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonAesNi_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fAesNi, iemAImpl_aesdec_u128, iemAImpl_aesdec_u128_fallback));
}


/** Opcode 0x66 0x0f 0x38 0xdf. */
FNIEMOP_DEF(iemOp_aesdeclast_Vdq_Wdq)
{
    IEMOP_MNEMONIC2(RM, AESDECLAST, aesdeclast, Vdq, Wdq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    return FNIEMOP_CALL_1(iemOpCommonAesNi_FullFull_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fAesNi, iemAImpl_aesdeclast_u128, iemAImpl_aesdeclast_u128_fallback));
}


/*  Opcode 0x66 0x0f 0x38 0xe0 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xe1 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xe2 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xe3 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xe4 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xe5 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xe6 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xe7 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xe8 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xe9 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xea - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xeb - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xec - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xed - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xee - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xef - invalid. */


/** Opcode      [0x66] 0x0f 0x38 0xf0. */
FNIEMOP_DEF(iemOp_movbe_Gv_Mv)
{
    IEMOP_MNEMONIC2(RM, MOVBE, movbe, Gv, Ev, DISOPTYPE_HARMLESS, 0);
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fMovBe)
        return iemOp_InvalidNeedRM(pVCpu);

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (!IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, memory.
         */
        switch (pVCpu->iem.s.enmEffOpSize)
        {
            case IEMMODE_16BIT:
                IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
                IEM_MC_LOCAL(uint16_t,  uSrc);
                IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc);

                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_FETCH_MEM_U16(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

                IEM_MC_BSWAP_LOCAL_U16(uSrc);
                IEM_MC_STORE_GREG_U16(IEM_GET_MODRM_REG(pVCpu, bRm), uSrc);

                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            case IEMMODE_32BIT:
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
                IEM_MC_LOCAL(uint32_t,  uSrc);
                IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc);

                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_FETCH_MEM_U32(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

                IEM_MC_BSWAP_LOCAL_U32(uSrc);
                IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), uSrc);

                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            case IEMMODE_64BIT:
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
                IEM_MC_LOCAL(uint64_t,  uSrc);
                IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc);

                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_FETCH_MEM_U64(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

                IEM_MC_BSWAP_LOCAL_U64(uSrc);
                IEM_MC_STORE_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), uSrc);

                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            IEM_NOT_REACHED_DEFAULT_CASE_RET();
        }
    }
    else
    {
        /* Reg/reg not supported. */
        IEMOP_RAISE_INVALID_OPCODE_RET();
    }
}


/*  Opcode 0xf3 0x0f 0x38 0xf0 - invalid. */


/** Opcode 0xf2 0x0f 0x38 0xf0. */
FNIEMOP_DEF(iemOp_crc32_Gd_Eb)
{
    IEMOP_MNEMONIC2(RM, CRC32, crc32, Gd, Eb, DISOPTYPE_HARMLESS, 0);
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fSse42)
        return iemOp_InvalidNeedRM(pVCpu);

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_ARG(uint32_t *,          puDst, 0);
        IEM_MC_ARG(uint8_t,             uSrc,  1);
        IEM_MC_FETCH_GREG_U8(uSrc,  IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_REF_GREG_U32(puDst,  IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fSse42, iemAImpl_crc32_u8, iemAImpl_crc32_u8_fallback), puDst, uSrc);
        IEM_MC_CLEAR_HIGH_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(uint32_t *,          puDst, 0);
        IEM_MC_ARG(uint8_t,             uSrc,  1);
        IEM_MC_LOCAL(RTGCPTR,           GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
        IEM_MC_FETCH_MEM_U8(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_REF_GREG_U32(puDst, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fSse42, iemAImpl_crc32_u8, iemAImpl_crc32_u8_fallback), puDst, uSrc);
        IEM_MC_CLEAR_HIGH_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm));

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode      [0x66] 0x0f 0x38 0xf1. */
FNIEMOP_DEF(iemOp_movbe_Mv_Gv)
{
    IEMOP_MNEMONIC2(MR, MOVBE, movbe, Ev, Gv, DISOPTYPE_HARMLESS, 0);
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fMovBe)
        return iemOp_InvalidNeedRM(pVCpu);

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (!IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Memory, register.
         */
        switch (pVCpu->iem.s.enmEffOpSize)
        {
            case IEMMODE_16BIT:
                IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
                IEM_MC_LOCAL(uint16_t, u16Value);
                IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_FETCH_GREG_U16(u16Value, IEM_GET_MODRM_REG(pVCpu, bRm));
                IEM_MC_BSWAP_LOCAL_U16(u16Value);
                IEM_MC_STORE_MEM_U16(pVCpu->iem.s.iEffSeg, GCPtrEffDst, u16Value);
                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            case IEMMODE_32BIT:
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
                IEM_MC_LOCAL(uint32_t, u32Value);
                IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_FETCH_GREG_U32(u32Value, IEM_GET_MODRM_REG(pVCpu, bRm));
                IEM_MC_BSWAP_LOCAL_U32(u32Value);
                IEM_MC_STORE_MEM_U32(pVCpu->iem.s.iEffSeg, GCPtrEffDst, u32Value);
                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            case IEMMODE_64BIT:
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
                IEM_MC_LOCAL(uint64_t, u64Value);
                IEM_MC_LOCAL(RTGCPTR, GCPtrEffDst);
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffDst, bRm, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_FETCH_GREG_U64(u64Value, IEM_GET_MODRM_REG(pVCpu, bRm));
                IEM_MC_BSWAP_LOCAL_U64(u64Value);
                IEM_MC_STORE_MEM_U64(pVCpu->iem.s.iEffSeg, GCPtrEffDst, u64Value);
                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            IEM_NOT_REACHED_DEFAULT_CASE_RET();
        }
    }
    else
    {
        /* Reg/reg not supported. */
        IEMOP_RAISE_INVALID_OPCODE_RET();
    }
}


/*  Opcode 0xf3 0x0f 0x38 0xf1 - invalid. */


/** Opcode 0xf2 0x0f 0x38 0xf1. */
FNIEMOP_DEF(iemOp_crc32_Gv_Ev)
{
    IEMOP_MNEMONIC2(RM, CRC32, crc32, Gd, Ev, DISOPTYPE_HARMLESS, 0);
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fSse42)
        return iemOp_InvalidNeedRM(pVCpu);

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        switch (pVCpu->iem.s.enmEffOpSize)
        {
            case IEMMODE_16BIT:
                IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_ARG(uint32_t *,          puDst, 0);
                IEM_MC_ARG(uint16_t,            uSrc,  1);
                IEM_MC_FETCH_GREG_U16(uSrc, IEM_GET_MODRM_RM(pVCpu, bRm));
                IEM_MC_REF_GREG_U32(puDst,  IEM_GET_MODRM_REG(pVCpu, bRm));
                IEM_MC_CALL_VOID_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fSse42, iemAImpl_crc32_u16, iemAImpl_crc32_u16_fallback),
                                         puDst, uSrc);
                IEM_MC_CLEAR_HIGH_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm));
                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            case IEMMODE_32BIT:
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_ARG(uint32_t *,          puDst, 0);
                IEM_MC_ARG(uint32_t,            uSrc,  1);
                IEM_MC_FETCH_GREG_U32(uSrc, IEM_GET_MODRM_RM(pVCpu, bRm));
                IEM_MC_REF_GREG_U32(puDst,  IEM_GET_MODRM_REG(pVCpu, bRm));
                IEM_MC_CALL_VOID_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fSse42, iemAImpl_crc32_u32, iemAImpl_crc32_u32_fallback),
                                         puDst, uSrc);
                IEM_MC_CLEAR_HIGH_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm));
                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            case IEMMODE_64BIT:
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_ARG(uint32_t *,          puDst, 0);
                IEM_MC_ARG(uint64_t,            uSrc,  1);
                IEM_MC_FETCH_GREG_U64(uSrc, IEM_GET_MODRM_RM(pVCpu, bRm));
                IEM_MC_REF_GREG_U32(puDst,  IEM_GET_MODRM_REG(pVCpu, bRm));
                IEM_MC_CALL_VOID_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fSse42, iemAImpl_crc32_u64, iemAImpl_crc32_u64_fallback),
                                         puDst, uSrc);
                IEM_MC_CLEAR_HIGH_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm));
                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            IEM_NOT_REACHED_DEFAULT_CASE_RET();
        }
    }
    else
    {
        /*
         * Register, memory.
         */
        switch (pVCpu->iem.s.enmEffOpSize)
        {
            case IEMMODE_16BIT:
                IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
                IEM_MC_ARG(uint32_t *,          puDst, 0);
                IEM_MC_ARG(uint16_t,            uSrc,  1);
                IEM_MC_LOCAL(RTGCPTR,           GCPtrEffSrc);

                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_FETCH_MEM_U16(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

                IEM_MC_REF_GREG_U32(puDst, IEM_GET_MODRM_REG(pVCpu, bRm));
                IEM_MC_CALL_VOID_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fSse42, iemAImpl_crc32_u16, iemAImpl_crc32_u16_fallback),
                                         puDst, uSrc);
                IEM_MC_CLEAR_HIGH_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm));

                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            case IEMMODE_32BIT:
                IEM_MC_BEGIN(IEM_MC_F_MIN_386, 0);
                IEM_MC_ARG(uint32_t *,          puDst, 0);
                IEM_MC_ARG(uint32_t,            uSrc,  1);
                IEM_MC_LOCAL(RTGCPTR,           GCPtrEffSrc);

                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_FETCH_MEM_U32(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

                IEM_MC_REF_GREG_U32(puDst, IEM_GET_MODRM_REG(pVCpu, bRm));
                IEM_MC_CALL_VOID_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fSse42, iemAImpl_crc32_u32, iemAImpl_crc32_u32_fallback),
                                         puDst, uSrc);
                IEM_MC_CLEAR_HIGH_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm));

                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            case IEMMODE_64BIT:
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
                IEM_MC_ARG(uint32_t *,          puDst, 0);
                IEM_MC_ARG(uint64_t,            uSrc,  1);
                IEM_MC_LOCAL(RTGCPTR,           GCPtrEffSrc);

                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX();
                IEM_MC_FETCH_MEM_U64(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

                IEM_MC_REF_GREG_U32(puDst, IEM_GET_MODRM_REG(pVCpu, bRm));
                IEM_MC_CALL_VOID_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fSse42, iemAImpl_crc32_u64, iemAImpl_crc32_u64_fallback),
                                         puDst, uSrc);
                IEM_MC_CLEAR_HIGH_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm));

                IEM_MC_ADVANCE_RIP_AND_FINISH();
                IEM_MC_END();
                break;

            IEM_NOT_REACHED_DEFAULT_CASE_RET();
        }
    }
}


/*  Opcode      0x0f 0x38 0xf2 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0xf2 - invalid. */
/*  Opcode 0xf3 0x0f 0x38 0xf2 - invalid. */
/*  Opcode 0xf2 0x0f 0x38 0xf2 - invalid. */

/*  Opcode      0x0f 0x38 0xf3 - invalid (vex only - group 17). */
/*  Opcode 0x66 0x0f 0x38 0xf3 - invalid (vex only - group 17). */
/*  Opcode 0xf3 0x0f 0x38 0xf3 - invalid (vex only - group 17). */
/*  Opcode 0xf2 0x0f 0x38 0xf3 - invalid (vex only - group 17). */

/*  Opcode      0x0f 0x38 0xf4 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xf4 - invalid. */
/*  Opcode 0xf3 0x0f 0x38 0xf4 - invalid. */
/*  Opcode 0xf2 0x0f 0x38 0xf4 - invalid. */

/*  Opcode      0x0f 0x38 0xf5 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0xf5 - invalid. */
/*  Opcode 0xf3 0x0f 0x38 0xf5 - invalid (vex only). */
/*  Opcode 0xf2 0x0f 0x38 0xf5 - invalid (vex only). */

/*  Opcode      0x0f 0x38 0xf6 - invalid. */

#define ADX_EMIT(a_Variant) \
    do \
    { \
        if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fAdx) \
            return iemOp_InvalidNeedRM(pVCpu); \
        uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm); \
        if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_REX_W) \
        { \
            if (IEM_IS_MODRM_REG_MODE(bRm)) \
            { \
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_ARG(uint64_t,    u64Src,     2); \
                IEM_MC_FETCH_GREG_U64(u64Src, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_ARG(uint64_t *,  pu64Dst,    1); \
                IEM_MC_REF_GREG_U64(pu64Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_ARG_EFLAGS(      fEFlagsIn,  0); \
                IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, \
                                    IEM_SELECT_HOST_OR_FALLBACK(fAdx, iemAImpl_## a_Variant ##_u64, iemAImpl_## a_Variant ##_u64_fallback), \
                                    fEFlagsIn, pu64Dst, u64Src); \
                IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
            } \
            else \
            { \
                IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
                IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc); \
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                \
                IEM_MC_ARG(uint64_t,    u64Src,     2); \
                IEM_MC_FETCH_MEM_U64(u64Src, pVCpu->iem.s.iEffSeg, GCPtrEffSrc); \
                IEM_MC_ARG(uint64_t *,  pu64Dst,    1); \
                IEM_MC_REF_GREG_U64(pu64Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_ARG_EFLAGS(      fEFlagsIn,  0); \
                IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, \
                                    IEM_SELECT_HOST_OR_FALLBACK(fAdx, iemAImpl_## a_Variant ##_u64, iemAImpl_## a_Variant ##_u64_fallback), \
                                    fEFlagsIn, pu64Dst, u64Src); \
                IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
            } \
        } \
        else \
        { \
            if (IEM_IS_MODRM_REG_MODE(bRm)) \
            { \
                IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                IEM_MC_ARG(uint32_t,    u32Src,     2); \
                IEM_MC_FETCH_GREG_U32(u32Src, IEM_GET_MODRM_RM(pVCpu, bRm)); \
                IEM_MC_ARG(uint32_t *,  pu32Dst,    1); \
                IEM_MC_REF_GREG_U32(pu32Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_ARG_EFLAGS(      fEFlagsIn,  0); \
                IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, \
                                    IEM_SELECT_HOST_OR_FALLBACK(fAdx, iemAImpl_## a_Variant ##_u32, iemAImpl_## a_Variant ##_u32_fallback), \
                                    fEFlagsIn, pu32Dst, u32Src); \
                IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
            } \
            else \
            { \
                IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0); \
                IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc); \
                IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1); \
                IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX(); \
                \
                IEM_MC_ARG(uint32_t,    u32Src,     2); \
                IEM_MC_FETCH_MEM_U32(u32Src, pVCpu->iem.s.iEffSeg, GCPtrEffSrc); \
                IEM_MC_ARG(uint32_t *,  pu32Dst,    1); \
                IEM_MC_REF_GREG_U32(pu32Dst, IEM_GET_MODRM_REG(pVCpu, bRm)); \
                IEM_MC_ARG_EFLAGS(      fEFlagsIn,  0); \
                IEM_MC_CALL_AIMPL_3(uint32_t, fEFlagsRet, \
                                    IEM_SELECT_HOST_OR_FALLBACK(fAdx, iemAImpl_## a_Variant ##_u32, iemAImpl_## a_Variant ##_u32_fallback), \
                                    fEFlagsIn, pu32Dst, u32Src); \
                IEM_MC_COMMIT_EFLAGS(fEFlagsRet); \
                IEM_MC_ADVANCE_RIP_AND_FINISH(); \
                IEM_MC_END(); \
            } \
        } \
    } while(0)


/**
 * @opcode      0xf6
 * @oppfx       0x66
 * @opfltest    cf
 * @opflmodify  cf
 */
FNIEMOP_DEF(iemOp_adcx_Gy_Ey)
{
    IEMOP_MNEMONIC2(RM, ADCX, adcx, Gy, Ey, DISOPTYPE_HARMLESS, 0);
    ADX_EMIT(adcx);
}


/**
 * @opcode      0xf6
 * @oppfx       0xf3
 * @opfltest    of
 * @opflmodify  of
 */
FNIEMOP_DEF(iemOp_adox_Gy_Ey)
{
    IEMOP_MNEMONIC2(RM, ADOX, adox, Gy, Ey, DISOPTYPE_HARMLESS, 0);
    ADX_EMIT(adox);
}


/*  Opcode 0xf2 0x0f 0x38 0xf6 - invalid (vex only). */

/*  Opcode      0x0f 0x38 0xf7 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x38 0xf7 - invalid (vex only). */
/*  Opcode 0xf3 0x0f 0x38 0xf7 - invalid (vex only). */
/*  Opcode 0xf2 0x0f 0x38 0xf7 - invalid (vex only). */

/*  Opcode      0x0f 0x38 0xf8 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xf8 - invalid. */
/*  Opcode 0xf3 0x0f 0x38 0xf8 - invalid. */
/*  Opcode 0xf2 0x0f 0x38 0xf8 - invalid. */

/*  Opcode      0x0f 0x38 0xf9 - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xf9 - invalid. */
/*  Opcode 0xf3 0x0f 0x38 0xf9 - invalid. */
/*  Opcode 0xf2 0x0f 0x38 0xf9 - invalid. */

/*  Opcode      0x0f 0x38 0xfa - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xfa - invalid. */
/*  Opcode 0xf3 0x0f 0x38 0xfa - invalid. */
/*  Opcode 0xf2 0x0f 0x38 0xfa - invalid. */

/*  Opcode      0x0f 0x38 0xfb - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xfb - invalid. */
/*  Opcode 0xf3 0x0f 0x38 0xfb - invalid. */
/*  Opcode 0xf2 0x0f 0x38 0xfb - invalid. */

/*  Opcode      0x0f 0x38 0xfc - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xfc - invalid. */
/*  Opcode 0xf3 0x0f 0x38 0xfc - invalid. */
/*  Opcode 0xf2 0x0f 0x38 0xfc - invalid. */

/*  Opcode      0x0f 0x38 0xfd - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xfd - invalid. */
/*  Opcode 0xf3 0x0f 0x38 0xfd - invalid. */
/*  Opcode 0xf2 0x0f 0x38 0xfd - invalid. */

/*  Opcode      0x0f 0x38 0xfe - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xfe - invalid. */
/*  Opcode 0xf3 0x0f 0x38 0xfe - invalid. */
/*  Opcode 0xf2 0x0f 0x38 0xfe - invalid. */

/*  Opcode      0x0f 0x38 0xff - invalid. */
/*  Opcode 0x66 0x0f 0x38 0xff - invalid. */
/*  Opcode 0xf3 0x0f 0x38 0xff - invalid. */
/*  Opcode 0xf2 0x0f 0x38 0xff - invalid. */


/**
 * Three byte opcode map, first two bytes are 0x0f 0x38.
 * @sa      g_apfnVexMap2
 */
const PFNIEMOP g_apfnThreeByte0f38[] =
{
    /*          no prefix,                  066h prefix                 f3h prefix,                 f2h prefix */
    /* 0x00 */  iemOp_pshufb_Pq_Qq,         iemOp_pshufb_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x01 */  iemOp_phaddw_Pq_Qq,         iemOp_phaddw_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x02 */  iemOp_phaddd_Pq_Qq,         iemOp_phaddd_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x03 */  iemOp_phaddsw_Pq_Qq,        iemOp_phaddsw_Vx_Wx,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x04 */  iemOp_pmaddubsw_Pq_Qq,      iemOp_pmaddubsw_Vx_Wx,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x05 */  iemOp_phsubw_Pq_Qq,         iemOp_phsubw_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x06 */  iemOp_phsubd_Pq_Qq,         iemOp_phsubd_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x07 */  iemOp_phsubsw_Pq_Qq,        iemOp_phsubsw_Vx_Wx,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x08 */  iemOp_psignb_Pq_Qq,         iemOp_psignb_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x09 */  iemOp_psignw_Pq_Qq,         iemOp_psignw_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x0a */  iemOp_psignd_Pq_Qq,         iemOp_psignd_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x0b */  iemOp_pmulhrsw_Pq_Qq,       iemOp_pmulhrsw_Vx_Wx,       iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x0c */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x0d */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x0e */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x0f */  IEMOP_X4(iemOp_InvalidNeedRM),

    /* 0x10 */  iemOp_InvalidNeedRM,        iemOp_pblendvb_Vdq_Wdq,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x11 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x12 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x13 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x14 */  iemOp_InvalidNeedRM,        iemOp_blendvps_Vdq_Wdq,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x15 */  iemOp_InvalidNeedRM,        iemOp_blendvpd_Vdq_Wdq,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x16 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x17 */  iemOp_InvalidNeedRM,        iemOp_ptest_Vx_Wx,          iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x18 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x19 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x1a */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x1b */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x1c */  iemOp_pabsb_Pq_Qq,          iemOp_pabsb_Vx_Wx,          iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x1d */  iemOp_pabsw_Pq_Qq,          iemOp_pabsw_Vx_Wx,          iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x1e */  iemOp_pabsd_Pq_Qq,          iemOp_pabsd_Vx_Wx,          iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x1f */  IEMOP_X4(iemOp_InvalidNeedRM),

    /* 0x20 */  iemOp_InvalidNeedRM,        iemOp_pmovsxbw_Vx_UxMq,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x21 */  iemOp_InvalidNeedRM,        iemOp_pmovsxbd_Vx_UxMd,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x22 */  iemOp_InvalidNeedRM,        iemOp_pmovsxbq_Vx_UxMw,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x23 */  iemOp_InvalidNeedRM,        iemOp_pmovsxwd_Vx_UxMq,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x24 */  iemOp_InvalidNeedRM,        iemOp_pmovsxwq_Vx_UxMd,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x25 */  iemOp_InvalidNeedRM,        iemOp_pmovsxdq_Vx_UxMq,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x26 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x27 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x28 */  iemOp_InvalidNeedRM,        iemOp_pmuldq_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x29 */  iemOp_InvalidNeedRM,        iemOp_pcmpeqq_Vx_Wx,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x2a */  iemOp_InvalidNeedRM,        iemOp_movntdqa_Vdq_Mdq,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x2b */  iemOp_InvalidNeedRM,        iemOp_packusdw_Vx_Wx,       iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x2c */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x2d */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x2e */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x2f */  IEMOP_X4(iemOp_InvalidNeedRM),

    /* 0x30 */  iemOp_InvalidNeedRM,        iemOp_pmovzxbw_Vx_UxMq,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x31 */  iemOp_InvalidNeedRM,        iemOp_pmovzxbd_Vx_UxMd,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x32 */  iemOp_InvalidNeedRM,        iemOp_pmovzxbq_Vx_UxMw,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x33 */  iemOp_InvalidNeedRM,        iemOp_pmovzxwd_Vx_UxMq,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x34 */  iemOp_InvalidNeedRM,        iemOp_pmovzxwq_Vx_UxMd,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x35 */  iemOp_InvalidNeedRM,        iemOp_pmovzxdq_Vx_UxMq,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x36 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x37 */  iemOp_InvalidNeedRM,        iemOp_pcmpgtq_Vx_Wx,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x38 */  iemOp_InvalidNeedRM,        iemOp_pminsb_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x39 */  iemOp_InvalidNeedRM,        iemOp_pminsd_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x3a */  iemOp_InvalidNeedRM,        iemOp_pminuw_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x3b */  iemOp_InvalidNeedRM,        iemOp_pminud_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x3c */  iemOp_InvalidNeedRM,        iemOp_pmaxsb_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x3d */  iemOp_InvalidNeedRM,        iemOp_pmaxsd_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x3e */  iemOp_InvalidNeedRM,        iemOp_pmaxuw_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x3f */  iemOp_InvalidNeedRM,        iemOp_pmaxud_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,

    /* 0x40 */  iemOp_InvalidNeedRM,        iemOp_pmulld_Vx_Wx,         iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x41 */  iemOp_InvalidNeedRM,        iemOp_phminposuw_Vdq_Wdq,   iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x42 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x43 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x44 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x45 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x46 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x47 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x48 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x49 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x4a */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x4b */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x4c */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x4d */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x4e */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x4f */  IEMOP_X4(iemOp_InvalidNeedRM),

    /* 0x50 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x51 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x52 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x53 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x54 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x55 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x56 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x57 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x58 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x59 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x5a */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x5b */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x5c */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x5d */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x5e */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x5f */  IEMOP_X4(iemOp_InvalidNeedRM),

    /* 0x60 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x61 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x62 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x63 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x64 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x65 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x66 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x67 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x68 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x69 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x6a */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x6b */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x6c */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x6d */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x6e */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x6f */  IEMOP_X4(iemOp_InvalidNeedRM),

    /* 0x70 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x71 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x72 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x73 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x74 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x75 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x76 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x77 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x78 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x79 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x7a */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x7b */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x7c */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x7d */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x7e */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x7f */  IEMOP_X4(iemOp_InvalidNeedRM),

    /* 0x80 */  iemOp_InvalidNeedRM,        iemOp_invept_Gy_Mdq,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x81 */  iemOp_InvalidNeedRM,        iemOp_invvpid_Gy_Mdq,       iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x82 */  iemOp_InvalidNeedRM,        iemOp_invpcid_Gy_Mdq,       iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x83 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x84 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x85 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x86 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x87 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x88 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x89 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x8a */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x8b */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x8c */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x8d */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x8e */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x8f */  IEMOP_X4(iemOp_InvalidNeedRM),

    /* 0x90 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x91 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x92 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x93 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x94 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x95 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x96 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x97 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x98 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x99 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x9a */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x9b */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x9c */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x9d */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x9e */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x9f */  IEMOP_X4(iemOp_InvalidNeedRM),

    /* 0xa0 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xa1 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xa2 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xa3 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xa4 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xa5 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xa6 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xa7 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xa8 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xa9 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xaa */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xab */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xac */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xad */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xae */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xaf */  IEMOP_X4(iemOp_InvalidNeedRM),

    /* 0xb0 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xb1 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xb2 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xb3 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xb4 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xb5 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xb6 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xb7 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xb8 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xb9 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xba */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xbb */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xbc */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xbd */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xbe */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xbf */  IEMOP_X4(iemOp_InvalidNeedRM),

    /* 0xc0 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xc1 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xc2 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xc3 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xc4 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xc5 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xc6 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xc7 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xc8 */  iemOp_sha1nexte_Vdq_Wdq,    iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xc9 */  iemOp_sha1msg1_Vdq_Wdq,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xca */  iemOp_sha1msg2_Vdq_Wdq,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xcb */  iemOp_sha256rnds2_Vdq_Wdq,  iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xcc */  iemOp_sha256msg1_Vdq_Wdq,   iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xcd */  iemOp_sha256msg2_Vdq_Wdq,   iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xce */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xcf */  IEMOP_X4(iemOp_InvalidNeedRM),

    /* 0xd0 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xd1 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xd2 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xd3 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xd4 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xd5 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xd6 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xd7 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xd8 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xd9 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xda */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xdb */  iemOp_InvalidNeedRM,        iemOp_aesimc_Vdq_Wdq,       iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xdc */  iemOp_InvalidNeedRM,        iemOp_aesenc_Vdq_Wdq,       iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xdd */  iemOp_InvalidNeedRM,        iemOp_aesenclast_Vdq_Wdq,   iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xde */  iemOp_InvalidNeedRM,        iemOp_aesdec_Vdq_Wdq,       iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xdf */  iemOp_InvalidNeedRM,        iemOp_aesdeclast_Vdq_Wdq,   iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,

    /* 0xe0 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xe1 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xe2 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xe3 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xe4 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xe5 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xe6 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xe7 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xe8 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xe9 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xea */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xeb */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xec */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xed */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xee */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xef */  IEMOP_X4(iemOp_InvalidNeedRM),

    /* 0xf0 */  iemOp_movbe_Gv_Mv,          iemOp_movbe_Gv_Mv,          iemOp_InvalidNeedRM,        iemOp_crc32_Gd_Eb,
    /* 0xf1 */  iemOp_movbe_Mv_Gv,          iemOp_movbe_Mv_Gv,          iemOp_InvalidNeedRM,        iemOp_crc32_Gv_Ev,
    /* 0xf2 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xf3 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xf4 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xf5 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xf6 */  iemOp_InvalidNeedRM,        iemOp_adcx_Gy_Ey,           iemOp_adox_Gy_Ey,           iemOp_InvalidNeedRM,
    /* 0xf7 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xf8 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xf9 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xfa */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xfb */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xfc */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xfd */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xfe */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xff */  IEMOP_X4(iemOp_InvalidNeedRM),
};
AssertCompile(RT_ELEMENTS(g_apfnThreeByte0f38) == 1024);

/** @} */

