/* $Id: IEMAllInstThree0f3a.cpp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEM - Instruction Decoding and Emulation, 0x0f 0x3a map.
 *
 * @remarks IEMAllInstVexMap3.cpp.h is a VEX mirror of this file.
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


/** @name Three byte opcodes with first two bytes 0x0f 0x3a
 * @{
 */

/**
 * Common worker for SSSE3 instructions on the forms:
 *      pxxx    xmm1, xmm2/mem128, imm8
 *
 * Proper alignment of the 128-bit operand is enforced.
 * Exceptions type 4. SSSE3 cpuid checks.
 *
 * @sa  iemOpCommonSse41_FullFullImm8_To_Full
 */
FNIEMOP_DEF_1(iemOpCommonSsse3_FullFullImm8_To_Full, PFNIEMAIMPLMEDIAOPTF2U128IMM8, pfnU128)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSsse3);
        IEM_MC_ARG(PRTUINT128U,                 puDst,               0);
        IEM_MC_ARG(PCRTUINT128U,                puSrc,               1);
        IEM_MC_ARG_CONST(uint8_t,               bImmArg, /*=*/ bImm, 2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst,             IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(puSrc,       IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_3(pfnU128, puDst, puSrc, bImmArg);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(PRTUINT128U,                 puDst,               0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,      puSrc, uSrc,         1);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_ARG_CONST(uint8_t,               bImmArg, /*=*/ bImm, 2);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSsse3);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst,             IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_3(pfnU128, puDst, puSrc, bImmArg);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * Common worker for SSE 4.1 instructions on the forms:
 *      pxxx    xmm1, xmm2/mem128, imm8
 *
 * Proper alignment of the 128-bit operand is enforced.
 * No SIMD exceptions. SSE 4.1 cpuid checks.
 *
 * @sa  iemOpCommonSsse3_FullFullImm8_To_Full
 */
FNIEMOP_DEF_1(iemOpCommonSse41_FullFullImm8_To_Full, PFNIEMAIMPLMEDIAOPTF2U128IMM8, pfnU128)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM, XMM, imm8
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
        IEM_MC_ARG(PRTUINT128U,                 puDst,               0);
        IEM_MC_ARG(PCRTUINT128U,                puSrc,               1);
        IEM_MC_ARG_CONST(uint8_t,               bImmArg, /*=*/ bImm, 2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst,             IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(puSrc,       IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_3(pfnU128, puDst, puSrc, bImmArg);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM, [mem128], imm8.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(PRTUINT128U,                 puDst,               0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,      puSrc, uSrc,         1);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_ARG_CONST(uint8_t,               bImmArg, /*=*/ bImm, 2);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst,             IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_3(pfnU128, puDst, puSrc, bImmArg);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * Common worker for SSE 4.1 instructions of the form:
 *      xxx     xmm1, xmm2/mem128, imm8
 *
 * Proper alignment of the 128-bit operand is enforced.
 * MXCSR is used as input and output.
 * Exceptions type 4. SSE 4.1 cpuid checks.
 *
 * @sa  iemOpCommonSse41_FullFullImm8_To_Full
 */
FNIEMOP_DEF_1(iemOpCommonSse41Fp_FullFullImm8_To_Full, PFNIEMAIMPLMEDIAF3XMMIMM8, pfnU128)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM, XMM, imm8.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
        IEM_MC_LOCAL(IEMMEDIAF2XMMSRC,              Src);
        IEM_MC_LOCAL(X86XMMREG,                     Dst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,            pDst,           Dst,    0);
        IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2XMMSRC,    pSrc,           Src,    1);
        IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/ bImm,    2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_FETCH_XREG_PAIR_XMM(Src, IEM_GET_MODRM_REG(pVCpu, bRm), IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_SSE_AIMPL_3(pfnU128, pDst, pSrc, bImmArg);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), Dst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM, [mem128], imm8.
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
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_FETCH_MEM_XMM_ALIGN_SSE_AND_XREG_XMM(Src, IEM_GET_MODRM_REG(pVCpu, bRm), pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_CALL_SSE_AIMPL_3(pfnU128, pDst, pSrc, bImmArg);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), Dst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * Common worker for SSE 4.1 instructions of the form:
 *      xxx     xmm1, xmm2/mem128, imm8
 *
 * Proper alignment of the 128-bit operand is enforced.
 * MXCSR is used as input and output.
 * Exceptions type 4. SSE 4.1 cpuid checks.
 *
 * @sa  iemOpCommonSse41_FullFullImm8_To_Full
 */
FNIEMOP_DEF_1(iemOpCommonSse41Fp_FullImm8_To_Full, PFNIEMAIMPLMEDIAF2U128IMM8, pfnU128)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM, XMM, imm8.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_LOCAL(X86XMMREG,                     uDst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,            puDst,         uDst,    0);
        IEM_MC_ARG(          PCX86XMMREG,           puSrc,                  1);
        IEM_MC_REF_XREG_XMM_CONST(puSrc,        IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/ bImm,    2);
        IEM_MC_CALL_SSE_AIMPL_3(pfnU128, puDst, puSrc, bImmArg);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM, [mem128], imm8.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTGCPTR,                       GCPtrEffSrc);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/ bImm,    2);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_LOCAL(X86XMMREG,                     uSrc);
        IEM_MC_FETCH_MEM_XMM_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_LOCAL(X86XMMREG,                     uDst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,            puDst,          uDst,    0);
        IEM_MC_ARG_LOCAL_REF(PCX86XMMREG,           puSrc,          uSrc,    1);
        IEM_MC_CALL_SSE_AIMPL_3(pfnU128, puDst, puSrc, bImmArg);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * Common worker for SSE-style AES-NI instructions of the form:
 *      aesxxx  xmm1, xmm2/mem128, imm8
 *
 * Proper alignment of the 128-bit operand is enforced.
 * Exceptions type 4. AES-NI cpuid checks.
 *
 * @sa  iemOpCommonSsse3_FullFullImm8_To_Full
 * @sa  iemOpCommonSse41_FullFullImm8_To_Full
 */
FNIEMOP_DEF_1(iemOpCommonAesNi_FullFullImm8_To_Full, PFNIEMAIMPLMEDIAOPTF2U128IMM8, pfnU128)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fAesNi);
        IEM_MC_ARG(PRTUINT128U,                 puDst,               0);
        IEM_MC_ARG(PCRTUINT128U,                puSrc,               1);
        IEM_MC_ARG_CONST(uint8_t,               bImmArg, /*=*/ bImm, 2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst,             IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(puSrc,       IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_3(pfnU128, puDst, puSrc, bImmArg);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(PRTUINT128U,                 puDst,               0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,      puSrc, uSrc,         1);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_ARG_CONST(uint8_t,               bImmArg, /*=*/ bImm, 2);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fAesNi);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst,             IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_3(pfnU128, puDst, puSrc, bImmArg);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode 0x66 0x0f 0x00 - invalid (vex only). */
/** Opcode 0x66 0x0f 0x01 - invalid (vex only). */
/** Opcode 0x66 0x0f 0x02 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x03 - invalid */
/** Opcode 0x66 0x0f 0x04 - invalid (vex only). */
/** Opcode 0x66 0x0f 0x05 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x06 - invalid (vex only) */
/*  Opcode 0x66 0x0f 0x07 - invalid */
/** Opcode 0x66 0x0f 0x08. */
FNIEMOP_DEF(iemOp_roundps_Vx_Wx_Ib)
{
    IEMOP_MNEMONIC3(RMI, ROUNDPS, roundps, Vx, Wx, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse41Fp_FullImm8_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSse41, iemAImpl_roundps_u128, iemAImpl_roundps_u128_fallback));
}


/** Opcode 0x66 0x0f 0x09. */
FNIEMOP_DEF(iemOp_roundpd_Vx_Wx_Ib)
{
    IEMOP_MNEMONIC3(RMI, ROUNDPD, roundpd, Vx, Wx, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse41Fp_FullImm8_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSse41, iemAImpl_roundpd_u128, iemAImpl_roundpd_u128_fallback));
}


/** Opcode 0x66 0x0f 0x0a. */
FNIEMOP_DEF(iemOp_roundss_Vss_Wss_Ib)
{
    /* The instruction form is very similar to CMPSS. */
    IEMOP_MNEMONIC3(RMI, ROUNDSS, roundss, Vss, Wss, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM32, XMM32.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
        IEM_MC_LOCAL(IEMMEDIAF2XMMSRC,              Src);
        IEM_MC_LOCAL(X86XMMREG,                     Dst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,            pDst,           Dst,    0);
        IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2XMMSRC,    pSrc,           Src,    1);
        IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/ bImm,    2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_FETCH_XREG_PAIR_XMM(Src, IEM_GET_MODRM_REG(pVCpu, bRm), IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_SSE_AIMPL_3(iemAImpl_roundss_u128, pDst, pSrc, bImmArg);
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
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_FETCH_MEM_XMM_U32_AND_XREG_XMM(Src, IEM_GET_MODRM_REG(pVCpu, bRm),
                                              0 /*a_iDword*/, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_CALL_SSE_AIMPL_3(iemAImpl_roundss_u128, pDst, pSrc, bImmArg);
        IEM_MC_STORE_XREG_XMM_U32(IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iDword*/, Dst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}

/** Opcode 0x66 0x0f 0x0b. */
FNIEMOP_DEF(iemOp_roundsd_Vsd_Wsd_Ib)
{
    /* The instruction form is very similar to CMPSD. */
    IEMOP_MNEMONIC3(RMI, ROUNDSD, roundsd, Vsd, Wsd, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM64, XMM64, imm8.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
        IEM_MC_LOCAL(IEMMEDIAF2XMMSRC,              Src);
        IEM_MC_LOCAL(X86XMMREG,                     Dst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,            pDst,           Dst,    0);
        IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2XMMSRC,    pSrc,           Src,    1);
        IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/ bImm,    2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_FETCH_XREG_PAIR_XMM(Src, IEM_GET_MODRM_REG(pVCpu, bRm), IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_SSE_AIMPL_3(iemAImpl_roundsd_u128, pDst, pSrc, bImmArg);
        IEM_MC_STORE_XREG_XMM_U64(IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iQword*/, Dst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM64, [mem64], imm8.
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
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_FETCH_MEM_XMM_U64_AND_XREG_XMM(Src, IEM_GET_MODRM_REG(pVCpu, bRm),
                                              0 /*a_iQword */, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_CALL_SSE_AIMPL_3(iemAImpl_roundsd_u128, pDst, pSrc, bImmArg);
        IEM_MC_STORE_XREG_XMM_U64(IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iQword*/, Dst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode 0x66 0x0f 0x0c. */
FNIEMOP_DEF(iemOp_blendps_Vx_Wx_Ib)
{
    IEMOP_MNEMONIC3(RMI, BLENDPS, blendps, Vx, Wx, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse41_FullFullImm8_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSse41, iemAImpl_blendps_u128, iemAImpl_blendps_u128_fallback));
}


/** Opcode 0x66 0x0f 0x0d. */
FNIEMOP_DEF(iemOp_blendpd_Vx_Wx_Ib)
{
    IEMOP_MNEMONIC3(RMI, BLENDPD, blendpd, Vx, Wx, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse41_FullFullImm8_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSse41, iemAImpl_blendpd_u128, iemAImpl_blendpd_u128_fallback));
}


/** Opcode 0x66 0x0f 0x0e. */
FNIEMOP_DEF(iemOp_pblendw_Vx_Wx_Ib)
{
    IEMOP_MNEMONIC3(RMI, PBLENDW, pblendw, Vx, Wx, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse41_FullFullImm8_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSse41, iemAImpl_pblendw_u128, iemAImpl_pblendw_u128_fallback));
}


/** Opcode      0x0f 0x0f. */
FNIEMOP_DEF(iemOp_palignr_Pq_Qq_Ib)
{
    IEMOP_MNEMONIC3(RMI, PALIGNR, palignr, Pq, Qq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        /** @todo testcase: REX.B / REX.R and MMX register indexing. Ignored? */
        /** @todo testcase: REX.B / REX.R and segment register indexing. Ignored? */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSsse3);
        IEM_MC_ARG(uint64_t *,          pDst, 0);
        IEM_MC_ARG(uint64_t,            uSrc, 1);
        IEM_MC_ARG_CONST(uint8_t,       bImmArg, /*=*/ bImm, 2);
        IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();
        IEM_MC_FETCH_MREG_U64(uSrc, IEM_GET_MODRM_RM_8(bRm));
        IEM_MC_REF_MREG_U64(pDst, IEM_GET_MODRM_REG_8(bRm));
        IEM_MC_CALL_VOID_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_palignr_u64, iemAImpl_palignr_u64_fallback),
                                 pDst, uSrc, bImmArg);
        IEM_MC_MODIFIED_MREG_BY_REF(pDst);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(uint64_t *,                  pDst,                0);
        IEM_MC_ARG(uint64_t,                    uSrc,                1);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_ARG_CONST(uint8_t,               bImmArg, /*=*/ bImm, 2);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSsse3);
        IEM_MC_MAYBE_RAISE_MMX_RELATED_XCPT();
        IEM_MC_FETCH_MEM_U64(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_FPU_USAGE();
        IEM_MC_FPU_TO_MMX_MODE();
        IEM_MC_REF_MREG_U64(pDst, IEM_GET_MODRM_REG_8(bRm));
        IEM_MC_CALL_VOID_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_palignr_u64, iemAImpl_palignr_u64_fallback),
                                 pDst, uSrc, bImmArg);
        IEM_MC_MODIFIED_MREG_BY_REF(pDst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode 0x66 0x0f 0x0f. */
FNIEMOP_DEF(iemOp_palignr_Vx_Wx_Ib)
{
    IEMOP_MNEMONIC3(RMI, PALIGNR, palignr, Vx, Wx, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSsse3_FullFullImm8_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSsse3, iemAImpl_palignr_u128, iemAImpl_palignr_u128_fallback));
}


/*  Opcode 0x66 0x0f 0x10 - invalid */
/*  Opcode 0x66 0x0f 0x11 - invalid */
/*  Opcode 0x66 0x0f 0x12 - invalid */
/*  Opcode 0x66 0x0f 0x13 - invalid */


/** Opcode 0x66 0x0f 0x14. */
FNIEMOP_DEF(iemOp_pextrb_RdMb_Vdq_Ib)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    IEMOP_MNEMONIC3(MRI, PEXTRB, pextrb, Ev, Vq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * greg32, XMM.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
        IEM_MC_LOCAL(uint8_t,   uValue);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_FETCH_XREG_U8(uValue, IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 15 /*a_iByte*/);
        IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_RM(pVCpu, bRm), uValue);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * [mem8], XMM.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(uint8_t,   uValue);
        IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_FETCH_XREG_U8(uValue, IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 15 /*a_iByte*/);
        IEM_MC_STORE_MEM_U8(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uValue);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode 0x66 0x0f 0x15. */
FNIEMOP_DEF(iemOp_pextrw_RdMw_Vdq_Ib)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    IEMOP_MNEMONIC3(MRI, PEXTRW, pextrw, Ev, Vq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * greg32, XMM.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
        IEM_MC_LOCAL(uint16_t,  uValue);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_FETCH_XREG_U16(uValue, IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 7 /*a_iWord*/);
        IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_RM(pVCpu, bRm), uValue);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * [mem16], XMM.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(uint16_t,  uValue);
        IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_FETCH_XREG_U16(uValue, IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 7 /*a_iWord*/);
        IEM_MC_STORE_MEM_U16(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uValue);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


FNIEMOP_DEF(iemOp_pextrd_q_RdMw_Vdq_Ib)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_REX_W)
    {
        /**
         * @opcode      0x16
         * @opcodesub   rex.w=1
         * @oppfx       0x66
         * @opcpuid     sse
         */
        IEMOP_MNEMONIC3(MRI, PEXTRQ, pextrq, Ev, Vq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OZ_PFX);
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /*
             * greg64, XMM.
             */
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
            IEM_MC_LOCAL(uint64_t,  uSrc);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE();
            IEM_MC_FETCH_XREG_U64(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 1 /*a_iQword*/);
            IEM_MC_STORE_GREG_U64(IEM_GET_MODRM_RM(pVCpu, bRm), uSrc);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /*
             * [mem64], XMM.
             */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_LOCAL(uint64_t,  uSrc);
            IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE();

            IEM_MC_FETCH_XREG_U64(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 1 /*a_iQword*/);
            IEM_MC_STORE_MEM_U64(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        /**
         * @opdone
         * @opcode      0x16
         * @opcodesub   rex.w=0
         * @oppfx       0x66
         * @opcpuid     sse
         */
        IEMOP_MNEMONIC3(MRI, PEXTRD, pextrd, Ey, Vd, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OZ_PFX);
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /*
             * greg32, XMM.
             */
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
            IEM_MC_LOCAL(uint32_t,  uSrc);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE();
            IEM_MC_FETCH_XREG_U32(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 3 /*a_iDword*/);
            IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_RM(pVCpu, bRm), uSrc);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /*
             * [mem32], XMM.
             */
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(uint32_t,  uSrc);
            IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE();
            IEM_MC_FETCH_XREG_U32(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 3 /*a_iDword*/);
            IEM_MC_STORE_MEM_U32(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/** Opcode 0x66 0x0f 0x17. */
FNIEMOP_DEF(iemOp_extractps_Ed_Vdq_Ib)
{
    IEMOP_MNEMONIC3(MRI, EXTRACTPS, extractps, Ed, Vdq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * greg32, XMM.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
        IEM_MC_LOCAL(uint32_t,  uSrc);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_FETCH_XREG_U32(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 3 /*a_iDword*/);
        IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_RM(pVCpu, bRm), uSrc);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * [mem32], XMM.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(uint32_t,  uSrc);
        IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_FETCH_XREG_U32(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 3 /*a_iDword*/);
        IEM_MC_STORE_MEM_U32(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/*  Opcode 0x66 0x0f 0x18 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x19 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x1a - invalid */
/*  Opcode 0x66 0x0f 0x1b - invalid */
/*  Opcode 0x66 0x0f 0x1c - invalid */
/*  Opcode 0x66 0x0f 0x1d - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x1e - invalid */
/*  Opcode 0x66 0x0f 0x1f - invalid */


/** Opcode 0x66 0x0f 0x20. */
FNIEMOP_DEF(iemOp_pinsrb_Vdq_RyMb_Ib)
{
    IEMOP_MNEMONIC3(RMI, PINSRB, pinsrb, Vd, Ey, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM, greg32.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
        IEM_MC_LOCAL(uint8_t,   uSrc);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_FETCH_GREG_U8(uSrc, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_STORE_XREG_U8(IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 15 /*a_iByte*/, uSrc);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM, [mem8].
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(uint8_t,   uSrc);
        IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_FETCH_MEM_U8(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_XREG_U8(IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 15 /*a_iByte*/, uSrc);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}

/** Opcode 0x66 0x0f 0x21, */
FNIEMOP_DEF(iemOp_insertps_Vdq_UdqMd_Ib)
{
    IEMOP_MNEMONIC3(RMI, INSERTPS, insertps, Vdq, Wdq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);  /// @todo
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM, XMM.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_LOCAL(uint32_t, uSrc);
        IEM_MC_FETCH_XREG_U32(uSrc, IEM_GET_MODRM_RM(pVCpu, bRm), (bImm >> 6) & 3);
        IEM_MC_STORE_XREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), (bImm >> 4) & 3, uSrc);
        IEM_MC_CLEAR_XREG_U32_MASK(IEM_GET_MODRM_REG(pVCpu, bRm), bImm);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM, [mem32].
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);

        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_LOCAL(uint32_t,  uSrc);
        IEM_MC_FETCH_MEM_U32(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_XREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), (bImm >> 4) & 3, uSrc);
        IEM_MC_CLEAR_XREG_U32_MASK(IEM_GET_MODRM_REG(pVCpu, bRm), bImm);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}

FNIEMOP_DEF(iemOp_pinsrd_q_Vdq_Ey_Ib)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_REX_W)
    {
        /**
         * @opcode      0x22
         * @opcodesub   rex.w=1
         * @oppfx       0x66
         * @opcpuid     sse
         */
        IEMOP_MNEMONIC3(RMI, PINSRQ, pinsrq, Vq, Ey, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OZ_PFX);
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /*
             * XMM, greg64.
             */
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
            IEM_MC_LOCAL(uint64_t,  uSrc);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE();
            IEM_MC_FETCH_GREG_U64(uSrc, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_STORE_XREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 1 /*a_iQword*/, uSrc);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /*
             * XMM, [mem64].
             */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_LOCAL(uint64_t,  uSrc);
            IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE();

            IEM_MC_FETCH_MEM_U64(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_STORE_XREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 1 /*a_iQword*/, uSrc);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        /**
         * @opdone
         * @opcode      0x22
         * @opcodesub   rex.w=0
         * @oppfx       0x66
         * @opcpuid     sse
         */
        IEMOP_MNEMONIC3(RMI, PINSRD, pinsrd, Vd, Ey, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OZ_PFX);
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /*
             * XMM, greg32.
             */
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
            IEM_MC_LOCAL(uint32_t,  uSrc);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE();
            IEM_MC_FETCH_GREG_U32(uSrc, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_STORE_XREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 3 /*a_iDword*/, uSrc);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /*
             * XMM, [mem32].
             */
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(uint32_t,  uSrc);
            IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse41);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE();

            IEM_MC_FETCH_MEM_U32(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_STORE_XREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 3 /*a_iDword*/, uSrc);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/*  Opcode 0x66 0x0f 0x23 - invalid */
/*  Opcode 0x66 0x0f 0x24 - invalid */
/*  Opcode 0x66 0x0f 0x25 - invalid */
/*  Opcode 0x66 0x0f 0x26 - invalid */
/*  Opcode 0x66 0x0f 0x27 - invalid */
/*  Opcode 0x66 0x0f 0x28 - invalid */
/*  Opcode 0x66 0x0f 0x29 - invalid */
/*  Opcode 0x66 0x0f 0x2a - invalid */
/*  Opcode 0x66 0x0f 0x2b - invalid */
/*  Opcode 0x66 0x0f 0x2c - invalid */
/*  Opcode 0x66 0x0f 0x2d - invalid */
/*  Opcode 0x66 0x0f 0x2e - invalid */
/*  Opcode 0x66 0x0f 0x2f - invalid */


/*  Opcode 0x66 0x0f 0x30 - invalid */
/*  Opcode 0x66 0x0f 0x31 - invalid */
/*  Opcode 0x66 0x0f 0x32 - invalid */
/*  Opcode 0x66 0x0f 0x33 - invalid */
/*  Opcode 0x66 0x0f 0x34 - invalid */
/*  Opcode 0x66 0x0f 0x35 - invalid */
/*  Opcode 0x66 0x0f 0x36 - invalid */
/*  Opcode 0x66 0x0f 0x37 - invalid */
/*  Opcode 0x66 0x0f 0x38 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x39 - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x3a - invalid */
/*  Opcode 0x66 0x0f 0x3b - invalid */
/*  Opcode 0x66 0x0f 0x3c - invalid */
/*  Opcode 0x66 0x0f 0x3d - invalid */
/*  Opcode 0x66 0x0f 0x3e - invalid */
/*  Opcode 0x66 0x0f 0x3f - invalid */


/** Opcode 0x66 0x0f 0x40. */
FNIEMOP_DEF(iemOp_dpps_Vx_Wx_Ib)
{
    IEMOP_MNEMONIC3(RMI, DPPS, dpps, Vx, Wx, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse41Fp_FullFullImm8_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSse41, iemAImpl_dpps_u128, iemAImpl_dpps_u128_fallback));
}


/** Opcode 0x66 0x0f 0x41, */
FNIEMOP_DEF(iemOp_dppd_Vdq_Wdq_Ib)
{
    IEMOP_MNEMONIC3(RMI, DPPD, dppd, Vx, Wx, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse41Fp_FullFullImm8_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSse41, iemAImpl_dppd_u128, iemAImpl_dppd_u128_fallback));
}


/** Opcode 0x66 0x0f 0x42. */
FNIEMOP_DEF(iemOp_mpsadbw_Vx_Wx_Ib)
{
    IEMOP_MNEMONIC3(RMI, MPSADBW, mpsadbw, Vx, Wx, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonSse41_FullFullImm8_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fSse41, iemAImpl_mpsadbw_u128, iemAImpl_mpsadbw_u128_fallback));
}


/*  Opcode 0x66 0x0f 0x43 - invalid */


/** Opcode 0x66 0x0f 0x44. */
FNIEMOP_DEF(iemOp_pclmulqdq_Vdq_Wdq_Ib)
{
    IEMOP_MNEMONIC3(RMI, PCLMULQDQ, pclmulqdq, Vdq, Wdq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fPclMul);
        IEM_MC_ARG(PRTUINT128U,                 puDst,               0);
        IEM_MC_ARG(PCRTUINT128U,                puSrc,               1);
        IEM_MC_ARG_CONST(uint8_t,               bImmArg, /*=*/ bImm, 2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst,             IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(puSrc,       IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fPclMul,
                                                             iemAImpl_pclmulqdq_u128,
                                                             iemAImpl_pclmulqdq_u128_fallback),
                                 puDst, puSrc, bImmArg);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(PRTUINT128U,                 puDst,               0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,      puSrc, uSrc,         1);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_ARG_CONST(uint8_t,               bImmArg, /*=*/ bImm, 2);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fPclMul);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst,             IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fPclMul,
                                                             iemAImpl_pclmulqdq_u128,
                                                             iemAImpl_pclmulqdq_u128_fallback),
                                 puDst, puSrc, bImmArg);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/*  Opcode 0x66 0x0f 0x45 - invalid */
/*  Opcode 0x66 0x0f 0x46 - invalid (vex only)  */
/*  Opcode 0x66 0x0f 0x47 - invalid */
/*  Opcode 0x66 0x0f 0x48 - invalid */
/*  Opcode 0x66 0x0f 0x49 - invalid */
/*  Opcode 0x66 0x0f 0x4a - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x4b - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x4c - invalid (vex only). */
/*  Opcode 0x66 0x0f 0x4d - invalid */
/*  Opcode 0x66 0x0f 0x4e - invalid */
/*  Opcode 0x66 0x0f 0x4f - invalid */


/*  Opcode 0x66 0x0f 0x50 - invalid */
/*  Opcode 0x66 0x0f 0x51 - invalid */
/*  Opcode 0x66 0x0f 0x52 - invalid */
/*  Opcode 0x66 0x0f 0x53 - invalid */
/*  Opcode 0x66 0x0f 0x54 - invalid */
/*  Opcode 0x66 0x0f 0x55 - invalid */
/*  Opcode 0x66 0x0f 0x56 - invalid */
/*  Opcode 0x66 0x0f 0x57 - invalid */
/*  Opcode 0x66 0x0f 0x58 - invalid */
/*  Opcode 0x66 0x0f 0x59 - invalid */
/*  Opcode 0x66 0x0f 0x5a - invalid */
/*  Opcode 0x66 0x0f 0x5b - invalid */
/*  Opcode 0x66 0x0f 0x5c - invalid */
/*  Opcode 0x66 0x0f 0x5d - invalid */
/*  Opcode 0x66 0x0f 0x5e - invalid */
/*  Opcode 0x66 0x0f 0x5f - invalid */


/**
 * @opcode      0x60
 * @oppfx       0x66
 * @opflmodify  cf,pf,af,zf,sf,of
 * @opflclear   pf,af
 */
FNIEMOP_DEF(iemOp_pcmpestrm_Vdq_Wdq_Ib)
{
    IEMOP_MNEMONIC3(RMI, PCMPESTRM, pcmpestrm, Vdq, Wdq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_REX_W)
    {
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /*
             * Register, register.
             */
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse42);
            IEM_MC_ARG(PRTUINT128U,                puDst,             0);
            IEM_MC_ARG(uint32_t *,                 pEFlags,             1);
            IEM_MC_LOCAL(IEMPCMPESTRXSRC,          Src);
            IEM_MC_ARG_LOCAL_REF(PIEMPCMPESTRXSRC, pSrc,           Src, 2);
            IEM_MC_ARG_CONST(uint8_t,              bImmArg, /*=*/ bImm, 3);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE();
            IEM_MC_FETCH_XREG_PAIR_U128_AND_RAX_RDX_U64(Src, IEM_GET_MODRM_REG(pVCpu, bRm), IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_REF_XREG_U128(puDst, 0 /*xmm0*/);
            IEM_MC_REF_EFLAGS(pEFlags);
            IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fSse42,
                                                                 iemAImpl_pcmpestrm_u128,
                                                                 iemAImpl_pcmpestrm_u128_fallback),
                                     puDst, pEFlags, pSrc, bImmArg);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /*
             * Register, memory.
             */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_ARG(PRTUINT128U,                 puDst,               0);
            IEM_MC_ARG(uint32_t *,                  pEFlags,             1);
            IEM_MC_LOCAL(IEMPCMPESTRXSRC,           Src);
            IEM_MC_ARG_LOCAL_REF(PIEMPCMPESTRXSRC,  pSrc,           Src, 2);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEM_MC_ARG_CONST(uint8_t,               bImmArg, /*=*/ bImm, 3);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse42);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE();

            IEM_MC_FETCH_MEM_U128_AND_XREG_U128_AND_RAX_RDX_U64(Src, IEM_GET_MODRM_REG(pVCpu, bRm),
                                                                pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_REF_XREG_U128(puDst, 0 /*xmm0*/);
            IEM_MC_REF_EFLAGS(pEFlags);
            IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fSse42,
                                                                 iemAImpl_pcmpestrm_u128,
                                                                 iemAImpl_pcmpestrm_u128_fallback),
                                     puDst, pEFlags, pSrc, bImmArg);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /*
             * Register, register.
             */
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse42);
            IEM_MC_ARG(PRTUINT128U,                puDst,             0);
            IEM_MC_ARG(uint32_t *,                 pEFlags,             1);
            IEM_MC_LOCAL(IEMPCMPESTRXSRC,          Src);
            IEM_MC_ARG_LOCAL_REF(PIEMPCMPESTRXSRC, pSrc,           Src, 2);
            IEM_MC_ARG_CONST(uint8_t,              bImmArg, /*=*/ bImm, 3);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE();
            IEM_MC_FETCH_XREG_PAIR_U128_AND_EAX_EDX_U32_SX_U64(Src, IEM_GET_MODRM_REG(pVCpu, bRm), IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_REF_XREG_U128(puDst, 0 /*xmm0*/);
            IEM_MC_REF_EFLAGS(pEFlags);
            IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fSse42,
                                                                 iemAImpl_pcmpestrm_u128,
                                                                 iemAImpl_pcmpestrm_u128_fallback),
                                     puDst, pEFlags, pSrc, bImmArg);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /*
             * Register, memory.
             */
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_ARG(PRTUINT128U,                 puDst,             0);
            IEM_MC_ARG(uint32_t *,                  pEFlags,             1);
            IEM_MC_LOCAL(IEMPCMPESTRXSRC,           Src);
            IEM_MC_ARG_LOCAL_REF(PIEMPCMPESTRXSRC,  pSrc,           Src, 2);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEM_MC_ARG_CONST(uint8_t,               bImmArg, /*=*/ bImm, 3);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse42);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE();

            IEM_MC_FETCH_MEM_U128_AND_XREG_U128_AND_EAX_EDX_U32_SX_U64(Src, IEM_GET_MODRM_REG(pVCpu, bRm),
                                                                       pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_REF_XREG_U128(puDst, 0 /*xmm0*/);
            IEM_MC_REF_EFLAGS(pEFlags);
            IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fSse42,
                                                                 iemAImpl_pcmpestrm_u128,
                                                                 iemAImpl_pcmpestrm_u128_fallback),
                                     puDst, pEFlags, pSrc, bImmArg);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/**
 * @opcode      0x61
 * @oppfx       0x66
 * @opflmodify  cf,pf,af,zf,sf,of
 * @opflclear   pf,af
 */
FNIEMOP_DEF(iemOp_pcmpestri_Vdq_Wdq_Ib)
{
    IEMOP_MNEMONIC3(RMI, PCMPESTRI, pcmpestri, Vdq, Wdq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_REX_W)
    {
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /*
             * Register, register.
             */
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse42);
            IEM_MC_ARG(uint32_t *,                 pu32Ecx,             0);
            IEM_MC_ARG(uint32_t *,                 pEFlags,             1);
            IEM_MC_LOCAL(IEMPCMPESTRXSRC,          Src);
            IEM_MC_ARG_LOCAL_REF(PIEMPCMPESTRXSRC, pSrc,           Src, 2);
            IEM_MC_ARG_CONST(uint8_t,              bImmArg, /*=*/ bImm, 3);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE();
            IEM_MC_FETCH_XREG_PAIR_U128_AND_RAX_RDX_U64(Src, IEM_GET_MODRM_REG(pVCpu, bRm), IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_REF_GREG_U32(pu32Ecx, X86_GREG_xCX);
            IEM_MC_CLEAR_HIGH_GREG_U64(X86_GREG_xCX);
            IEM_MC_REF_EFLAGS(pEFlags);
            IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fSse42,
                                                                 iemAImpl_pcmpestri_u128,
                                                                 iemAImpl_pcmpestri_u128_fallback),
                                     pu32Ecx, pEFlags, pSrc, bImmArg);
            /** @todo testcase: High dword of RCX cleared? */
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /*
             * Register, memory.
             */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_ARG(uint32_t *,                  pu32Ecx,             0);
            IEM_MC_ARG(uint32_t *,                  pEFlags,             1);
            IEM_MC_LOCAL(IEMPCMPESTRXSRC,           Src);
            IEM_MC_ARG_LOCAL_REF(PIEMPCMPESTRXSRC,  pSrc,           Src, 2);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEM_MC_ARG_CONST(uint8_t,               bImmArg, /*=*/ bImm, 3);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse42);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE();

            IEM_MC_FETCH_MEM_U128_AND_XREG_U128_AND_RAX_RDX_U64(Src, IEM_GET_MODRM_REG(pVCpu, bRm),
                                                                pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_REF_GREG_U32(pu32Ecx, X86_GREG_xCX);
            IEM_MC_CLEAR_HIGH_GREG_U64(X86_GREG_xCX);
            IEM_MC_REF_EFLAGS(pEFlags);
            IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fSse42,
                                                                 iemAImpl_pcmpestri_u128,
                                                                 iemAImpl_pcmpestri_u128_fallback),
                                     pu32Ecx, pEFlags, pSrc, bImmArg);
            /** @todo testcase: High dword of RCX cleared? */
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /*
             * Register, register.
             */
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse42);
            IEM_MC_ARG(uint32_t *,                 pu32Ecx,             0);
            IEM_MC_ARG(uint32_t *,                 pEFlags,             1);
            IEM_MC_LOCAL(IEMPCMPESTRXSRC,          Src);
            IEM_MC_ARG_LOCAL_REF(PIEMPCMPESTRXSRC, pSrc,           Src, 2);
            IEM_MC_ARG_CONST(uint8_t,              bImmArg, /*=*/ bImm, 3);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE();
            IEM_MC_FETCH_XREG_PAIR_U128_AND_EAX_EDX_U32_SX_U64(Src, IEM_GET_MODRM_REG(pVCpu, bRm), IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_REF_GREG_U32(pu32Ecx, X86_GREG_xCX);
            IEM_MC_CLEAR_HIGH_GREG_U64(X86_GREG_xCX);
            IEM_MC_REF_EFLAGS(pEFlags);
            IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fSse42,
                                                                 iemAImpl_pcmpestri_u128,
                                                                 iemAImpl_pcmpestri_u128_fallback),
                                     pu32Ecx, pEFlags, pSrc, bImmArg);
            /** @todo testcase: High dword of RCX cleared? */
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /*
             * Register, memory.
             */
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_ARG(uint32_t *,                  pu32Ecx,             0);
            IEM_MC_ARG(uint32_t *,                  pEFlags,             1);
            IEM_MC_LOCAL(IEMPCMPESTRXSRC,           Src);
            IEM_MC_ARG_LOCAL_REF(PIEMPCMPESTRXSRC,  pSrc,           Src, 2);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEM_MC_ARG_CONST(uint8_t,               bImmArg, /*=*/ bImm, 3);
            IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse42);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE();

            IEM_MC_FETCH_MEM_U128_AND_XREG_U128_AND_EAX_EDX_U32_SX_U64(Src, IEM_GET_MODRM_REG(pVCpu, bRm),
                                                                       pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_REF_GREG_U32(pu32Ecx, X86_GREG_xCX);
            IEM_MC_CLEAR_HIGH_GREG_U64(X86_GREG_xCX);
            IEM_MC_REF_EFLAGS(pEFlags);
            IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fSse42,
                                                                 iemAImpl_pcmpestri_u128,
                                                                 iemAImpl_pcmpestri_u128_fallback),
                                     pu32Ecx, pEFlags, pSrc, bImmArg);
            /** @todo testcase: High dword of RCX cleared? */
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/**
 * @opcode      0x62
 * @oppfx       0x66
 * @opflmodify  cf,pf,af,zf,sf,of
 * @opflclear   pf,af
 */
FNIEMOP_DEF(iemOp_pcmpistrm_Vdq_Wdq_Ib)
{
    IEMOP_MNEMONIC3(RMI, PCMPISTRM, pcmpistrm, Vdq, Wdq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse42);
        IEM_MC_ARG(PRTUINT128U,                puDst,             0);
        IEM_MC_ARG(uint32_t *,                 pEFlags,             1);
        IEM_MC_LOCAL(IEMPCMPISTRXSRC,          Src);
        IEM_MC_ARG_LOCAL_REF(PIEMPCMPISTRXSRC, pSrc,           Src, 2);
        IEM_MC_ARG_CONST(uint8_t,              bImmArg, /*=*/ bImm, 3);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_FETCH_XREG_PAIR_U128(Src, IEM_GET_MODRM_REG(pVCpu, bRm), IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_REF_XREG_U128(puDst, 0 /*xmm0*/);
        IEM_MC_REF_EFLAGS(pEFlags);
        IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fSse42,
                                                             iemAImpl_pcmpistrm_u128,
                                                             iemAImpl_pcmpistrm_u128_fallback),
                                 puDst, pEFlags, pSrc, bImmArg);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(PRTUINT128U,                 puDst,               0);
        IEM_MC_ARG(uint32_t *,                  pEFlags,             1);
        IEM_MC_LOCAL(IEMPCMPISTRXSRC,           Src);
        IEM_MC_ARG_LOCAL_REF(PIEMPCMPISTRXSRC,  pSrc,           Src, 2);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_ARG_CONST(uint8_t,               bImmArg, /*=*/ bImm, 3);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse42);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_FETCH_MEM_U128_AND_XREG_U128(Src, IEM_GET_MODRM_REG(pVCpu, bRm), pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_REF_XREG_U128(puDst, 0 /*xmm0*/);
        IEM_MC_REF_EFLAGS(pEFlags);
        IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fSse42,
                                                             iemAImpl_pcmpistrm_u128,
                                                             iemAImpl_pcmpistrm_u128_fallback),
                                 puDst, pEFlags, pSrc, bImmArg);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x63
 * @oppfx       0x66
 * @opflmodify  cf,pf,af,zf,sf,of
 * @opflclear   pf,af
 */
FNIEMOP_DEF(iemOp_pcmpistri_Vdq_Wdq_Ib)
{
    IEMOP_MNEMONIC3(RMI, PCMPISTRI, pcmpistri, Vdq, Wdq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse42);
        IEM_MC_ARG(uint32_t *,                 pEFlags,             0);
        IEM_MC_ARG(PCRTUINT128U,               pSrc1,               1);
        IEM_MC_ARG(PCRTUINT128U,               pSrc2,               2);
        IEM_MC_ARG_CONST(uint8_t,              bImmArg, /*=*/ bImm, 3);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128_CONST(pSrc1, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(pSrc2, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_REF_EFLAGS(pEFlags);
        IEM_MC_CALL_AIMPL_4(uint32_t, u32Ecx,
                            IEM_SELECT_HOST_OR_FALLBACK(fSse42,
                                                        iemAImpl_pcmpistri_u128,
                                                        iemAImpl_pcmpistri_u128_fallback),
                            pEFlags, pSrc1, pSrc2, bImmArg);
        /** @todo testcase: High dword of RCX cleared? */
        IEM_MC_STORE_GREG_U32(X86_GREG_xCX, u32Ecx);
        IEM_MC_CLEAR_HIGH_GREG_U64(X86_GREG_xCX);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(uint32_t *,                 pEFlags,             0);
        IEM_MC_ARG(PCRTUINT128U,               pSrc1,               1);
        IEM_MC_LOCAL(RTUINT128U,               Src2);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,     pSrc2, Src2,         2);
        IEM_MC_LOCAL(RTGCPTR,                  GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_ARG_CONST(uint8_t,              bImmArg, /*=*/ bImm, 3);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse42);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_FETCH_MEM_U128(Src2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_REF_XREG_U128_CONST(pSrc1, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_EFLAGS(pEFlags);
        IEM_MC_CALL_AIMPL_4(uint32_t, u32Ecx,
                            IEM_SELECT_HOST_OR_FALLBACK(fSse42,
                                                        iemAImpl_pcmpistri_u128,
                                                        iemAImpl_pcmpistri_u128_fallback),
                            pEFlags, pSrc1, pSrc2, bImmArg);
        /** @todo testcase: High dword of RCX cleared? */
        IEM_MC_STORE_GREG_U32(X86_GREG_xCX, u32Ecx);
        IEM_MC_CLEAR_HIGH_GREG_U64(X86_GREG_xCX);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/*  Opcode 0x66 0x0f 0x64 - invalid */
/*  Opcode 0x66 0x0f 0x65 - invalid */
/*  Opcode 0x66 0x0f 0x66 - invalid */
/*  Opcode 0x66 0x0f 0x67 - invalid */
/*  Opcode 0x66 0x0f 0x68 - invalid */
/*  Opcode 0x66 0x0f 0x69 - invalid */
/*  Opcode 0x66 0x0f 0x6a - invalid */
/*  Opcode 0x66 0x0f 0x6b - invalid */
/*  Opcode 0x66 0x0f 0x6c - invalid */
/*  Opcode 0x66 0x0f 0x6d - invalid */
/*  Opcode 0x66 0x0f 0x6e - invalid */
/*  Opcode 0x66 0x0f 0x6f - invalid */

/*  Opcodes 0x0f 0x70 thru 0x0f 0xb0 are unused.  */


/*  Opcode      0x0f 0xc0 - invalid */
/*  Opcode      0x0f 0xc1 - invalid */
/*  Opcode      0x0f 0xc2 - invalid */
/*  Opcode      0x0f 0xc3 - invalid */
/*  Opcode      0x0f 0xc4 - invalid */
/*  Opcode      0x0f 0xc5 - invalid */
/*  Opcode      0x0f 0xc6 - invalid */
/*  Opcode      0x0f 0xc7 - invalid */
/*  Opcode      0x0f 0xc8 - invalid */
/*  Opcode      0x0f 0xc9 - invalid */
/*  Opcode      0x0f 0xca - invalid */
/*  Opcode      0x0f 0xcb - invalid */


/*  Opcode      0x0f 0xcc */
FNIEMOP_DEF(iemOp_sha1rnds4_Vdq_Wdq_Ib)
{
    IEMOP_MNEMONIC3(RMI, SHA1RNDS4, sha1rnds4, Vdq, Wdq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM, XMM, imm8
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSha);
        IEM_MC_ARG(PRTUINT128U,                 puDst,               0);
        IEM_MC_ARG(PCRTUINT128U,                puSrc,               1);
        IEM_MC_ARG_CONST(uint8_t,               bImmArg, /*=*/ bImm, 2);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst,             IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(puSrc,       IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fSha,
                                                             iemAImpl_sha1rnds4_u128,
                                                             iemAImpl_sha1rnds4_u128_fallback),
                                 puDst, puSrc, bImmArg);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM, [mem128], imm8.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_ARG(PRTUINT128U,                 puDst,               0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,      puSrc, uSrc,         1);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_ARG_CONST(uint8_t,               bImmArg, /*=*/ bImm, 2);
        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSha);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_SSE_USAGE();
        IEM_MC_REF_XREG_U128(puDst,             IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fSha,
                                                             iemAImpl_sha1rnds4_u128,
                                                             iemAImpl_sha1rnds4_u128_fallback),
                                 puDst, puSrc, bImmArg);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/*  Opcode      0x0f 0xcd - invalid */
/*  Opcode      0x0f 0xce - invalid */
/*  Opcode      0x0f 0xcf - invalid */


/*  Opcode 0x66 0x0f 0xd0 - invalid */
/*  Opcode 0x66 0x0f 0xd1 - invalid */
/*  Opcode 0x66 0x0f 0xd2 - invalid */
/*  Opcode 0x66 0x0f 0xd3 - invalid */
/*  Opcode 0x66 0x0f 0xd4 - invalid */
/*  Opcode 0x66 0x0f 0xd5 - invalid */
/*  Opcode 0x66 0x0f 0xd6 - invalid */
/*  Opcode 0x66 0x0f 0xd7 - invalid */
/*  Opcode 0x66 0x0f 0xd8 - invalid */
/*  Opcode 0x66 0x0f 0xd9 - invalid */
/*  Opcode 0x66 0x0f 0xda - invalid */
/*  Opcode 0x66 0x0f 0xdb - invalid */
/*  Opcode 0x66 0x0f 0xdc - invalid */
/*  Opcode 0x66 0x0f 0xdd - invalid */
/*  Opcode 0x66 0x0f 0xde - invalid */


/*  Opcode 0x66 0x0f 0xdf - (aeskeygenassist). */
FNIEMOP_DEF(iemOp_aeskeygen_Vdq_Wdq_Ib)
{
    IEMOP_MNEMONIC3(RMI, AESKEYGEN, aeskeygen, Vdq, Wdq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, 0);
    return FNIEMOP_CALL_1(iemOpCommonAesNi_FullFullImm8_To_Full,
                          IEM_SELECT_HOST_OR_FALLBACK(fAesNi, iemAImpl_aeskeygenassist_u128, iemAImpl_aeskeygenassist_u128_fallback));
}


/*  Opcode 0xf2 0x0f 0xf0 - invalid (vex only) */


/**
 * Three byte opcode map, first two bytes are 0x0f 0x3a.
 * @sa      g_apfnVexMap2
 */
const PFNIEMOP g_apfnThreeByte0f3a[] =
{
    /*          no prefix,                  066h prefix                 f3h prefix,                 f2h prefix */
    /* 0x00 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x01 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x02 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x03 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x04 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x05 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x06 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x07 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x08 */  iemOp_InvalidNeedRMImm8,    iemOp_roundps_Vx_Wx_Ib,     iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x09 */  iemOp_InvalidNeedRMImm8,    iemOp_roundpd_Vx_Wx_Ib,     iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x0a */  iemOp_InvalidNeedRMImm8,    iemOp_roundss_Vss_Wss_Ib,   iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x0b */  iemOp_InvalidNeedRMImm8,    iemOp_roundsd_Vsd_Wsd_Ib,   iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x0c */  iemOp_InvalidNeedRMImm8,    iemOp_blendps_Vx_Wx_Ib,     iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x0d */  iemOp_InvalidNeedRMImm8,    iemOp_blendpd_Vx_Wx_Ib,     iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x0e */  iemOp_InvalidNeedRMImm8,    iemOp_pblendw_Vx_Wx_Ib,     iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x0f */  iemOp_palignr_Pq_Qq_Ib,     iemOp_palignr_Vx_Wx_Ib,     iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,

    /* 0x10 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x11 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x12 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x13 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x14 */  iemOp_InvalidNeedRMImm8,    iemOp_pextrb_RdMb_Vdq_Ib,   iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x15 */  iemOp_InvalidNeedRMImm8,    iemOp_pextrw_RdMw_Vdq_Ib,   iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x16 */  iemOp_InvalidNeedRMImm8,    iemOp_pextrd_q_RdMw_Vdq_Ib, iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x17 */  iemOp_InvalidNeedRMImm8,    iemOp_extractps_Ed_Vdq_Ib,  iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x18 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x19 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x1a */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x1b */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x1c */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x1d */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x1e */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x1f */  IEMOP_X4(iemOp_InvalidNeedRMImm8),

    /* 0x20 */  iemOp_InvalidNeedRMImm8,    iemOp_pinsrb_Vdq_RyMb_Ib,   iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x21 */  iemOp_InvalidNeedRMImm8,    iemOp_insertps_Vdq_UdqMd_Ib,iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x22 */  iemOp_InvalidNeedRMImm8,    iemOp_pinsrd_q_Vdq_Ey_Ib,   iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x23 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x24 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x25 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x26 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x27 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x28 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x29 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x2a */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x2b */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x2c */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x2d */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x2e */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x2f */  IEMOP_X4(iemOp_InvalidNeedRMImm8),

    /* 0x30 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x31 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x32 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x33 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x34 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x35 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x36 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x37 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x38 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x39 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x3a */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x3b */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x3c */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x3d */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x3e */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x3f */  IEMOP_X4(iemOp_InvalidNeedRMImm8),

    /* 0x40 */  iemOp_InvalidNeedRMImm8,    iemOp_dpps_Vx_Wx_Ib,        iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x41 */  iemOp_InvalidNeedRMImm8,    iemOp_dppd_Vdq_Wdq_Ib,      iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x42 */  iemOp_InvalidNeedRMImm8,    iemOp_mpsadbw_Vx_Wx_Ib,     iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x43 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x44 */  iemOp_InvalidNeedRMImm8,    iemOp_pclmulqdq_Vdq_Wdq_Ib, iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x45 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x46 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x47 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x48 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x49 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x4a */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x4b */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x4c */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x4d */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x4e */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x4f */  IEMOP_X4(iemOp_InvalidNeedRMImm8),

    /* 0x50 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x51 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x52 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x53 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x54 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x55 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x56 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x57 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x58 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x59 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x5a */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x5b */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x5c */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x5d */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x5e */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x5f */  IEMOP_X4(iemOp_InvalidNeedRMImm8),

    /* 0x60 */  iemOp_InvalidNeedRMImm8,    iemOp_pcmpestrm_Vdq_Wdq_Ib, iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x61 */  iemOp_InvalidNeedRMImm8,    iemOp_pcmpestri_Vdq_Wdq_Ib, iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x62 */  iemOp_InvalidNeedRMImm8,    iemOp_pcmpistrm_Vdq_Wdq_Ib, iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x63 */  iemOp_InvalidNeedRMImm8,    iemOp_pcmpistri_Vdq_Wdq_Ib, iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x64 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x65 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x66 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x67 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x68 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x69 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x6a */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x6b */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x6c */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x6d */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x6e */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x6f */  IEMOP_X4(iemOp_InvalidNeedRMImm8),

    /* 0x70 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x71 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x72 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x73 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x74 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x75 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x76 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x77 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x78 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x79 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x7a */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x7b */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x7c */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x7d */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x7e */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x7f */  IEMOP_X4(iemOp_InvalidNeedRMImm8),

    /* 0x80 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x81 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x82 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x83 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x84 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x85 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x86 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x87 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x88 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x89 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x8a */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x8b */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x8c */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x8d */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x8e */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x8f */  IEMOP_X4(iemOp_InvalidNeedRMImm8),

    /* 0x90 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x91 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x92 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x93 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x94 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x95 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x96 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x97 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x98 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x99 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x9a */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x9b */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x9c */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x9d */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x9e */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x9f */  IEMOP_X4(iemOp_InvalidNeedRMImm8),

    /* 0xa0 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xa1 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xa2 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xa3 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xa4 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xa5 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xa6 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xa7 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xa8 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xa9 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xaa */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xab */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xac */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xad */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xae */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xaf */  IEMOP_X4(iemOp_InvalidNeedRMImm8),

    /* 0xb0 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xb1 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xb2 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xb3 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xb4 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xb5 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xb6 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xb7 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xb8 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xb9 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xba */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xbb */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xbc */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xbd */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xbe */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xbf */  IEMOP_X4(iemOp_InvalidNeedRMImm8),

    /* 0xc0 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xc1 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xc2 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xc3 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xc4 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xc5 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xc6 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xc7 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xc8 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xc9 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xca */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xcb */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xcc */  iemOp_sha1rnds4_Vdq_Wdq_Ib, iemOp_InvalidNeedRMImm8,   iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0xcd */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xce */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xcf */  IEMOP_X4(iemOp_InvalidNeedRMImm8),

    /* 0xd0 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xd1 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xd2 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xd3 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xd4 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xd5 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xd6 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xd7 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xd8 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xd9 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xda */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xdb */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xdc */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xdd */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xde */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xdf */  iemOp_InvalidNeedRMImm8, iemOp_aeskeygen_Vdq_Wdq_Ib,   iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,

    /* 0xe0 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xe1 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xe2 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xe3 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xe4 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xe5 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xe6 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xe7 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xe8 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xe9 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xea */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xeb */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xec */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xed */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xee */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xef */  IEMOP_X4(iemOp_InvalidNeedRMImm8),

    /* 0xf0 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xf1 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xf2 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xf3 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xf4 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xf5 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xf6 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xf7 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xf8 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xf9 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xfa */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xfb */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xfc */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xfd */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xfe */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0xff */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
};
AssertCompile(RT_ELEMENTS(g_apfnThreeByte0f3a) == 1024);

/** @} */

