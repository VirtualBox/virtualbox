/* $Id: IEMAllInstVexMap3.cpp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEM - Instruction Decoding and Emulation, 0x0f 0x3a map.
 *
 * @remarks IEMAllInstThree0f3a.cpp.h is a VEX mirror of this file.
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


/** @name VEX Opcode Map 3
 * @{
 */

/**
 * Common worker for AVX2 instructions on the forms:
 *     - vpxxx    xmm0, xmm1, xmm2/mem128, imm8
 *     - vpxxx    ymm0, ymm1, ymm2/mem256, imm8
 *
 * Takes function table for function w/o implicit state parameter.
 *
 * Exceptions type 4. AVX cpuid check for 128-bit operation, AVX2 for 256-bit.
 */
FNIEMOP_DEF_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Ib_Opt, PCIEMOPMEDIAOPTF3IMM8, pImpl)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        if (pVCpu->iem.s.uVexLength)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx2);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(RTUINT256U,            uDst);
            IEM_MC_LOCAL(RTUINT256U,            uSrc1);
            IEM_MC_LOCAL(RTUINT256U,            uSrc2);
            IEM_MC_ARG_LOCAL_REF(PRTUINT256U,   puDst,  uDst,  0);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc1, uSrc1, 1);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc2, uSrc2, 2);
            IEM_MC_ARG_CONST(uint8_t,           bImmArg, /*=*/ bImm, 3);
            IEM_MC_FETCH_YREG_U256(uSrc1,   IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_FETCH_YREG_U256(uSrc2,   IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnU256, puDst, puSrc1, puSrc2, bImmArg);
            IEM_MC_STORE_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
            IEM_MC_ARG(PRTUINT128U,          puDst,  0);
            IEM_MC_ARG(PCRTUINT128U,         puSrc1, 1);
            IEM_MC_ARG(PCRTUINT128U,         puSrc2, 2);
            IEM_MC_ARG_CONST(uint8_t,        bImmArg, /*=*/ bImm, 3);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_REF_XREG_U128(puDst,        IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_REF_XREG_U128_CONST(puSrc1, IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_REF_XREG_U128_CONST(puSrc2, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnU128, puDst, puSrc1, puSrc2, bImmArg);
            IEM_MC_CLEAR_YREG_128_UP(          IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        /*
         * Register, memory.
         */
        if (pVCpu->iem.s.uVexLength)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTUINT256U,            uDst);
            IEM_MC_LOCAL(RTUINT256U,            uSrc1);
            IEM_MC_LOCAL(RTUINT256U,            uSrc2);
            IEM_MC_LOCAL(RTGCPTR,               GCPtrEffSrc);
            IEM_MC_ARG_LOCAL_REF(PRTUINT256U,   puDst,  uDst,  0);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc1, uSrc1, 1);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc2, uSrc2, 2);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEM_MC_ARG_CONST(uint8_t,           bImmArg, /*=*/ bImm, 3);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx2);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_FETCH_MEM_U256_NO_AC(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_FETCH_YREG_U256(uSrc1,      IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnU256, puDst, puSrc1, puSrc2, bImmArg);
            IEM_MC_STORE_YREG_U256_ZX_VLMAX(   IEM_GET_MODRM_REG(pVCpu, bRm), uDst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTUINT128U,                uSrc2);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
            IEM_MC_ARG(PRTUINT128U,                 puDst,         0);
            IEM_MC_ARG(PCRTUINT128U,                puSrc1,        1);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,      puSrc2, uSrc2, 2);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEM_MC_ARG_CONST(uint8_t,               bImmArg, /*=*/ bImm, 3);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_FETCH_MEM_U128_NO_AC(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_REF_XREG_U128(puDst,         IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_REF_XREG_U128_CONST(puSrc1,  IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnU128, puDst, puSrc1, puSrc2, bImmArg);
            IEM_MC_CLEAR_YREG_128_UP(           IEM_GET_MODRM_REG(pVCpu, bRm));

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/**
 * Common worker for AVX instructions on the forms:
 *     - vxxxp{s,d}    xmm0, xmm1/mem128, imm8
 *     - vxxxp{s,d}    ymm0, ymm1/mem256, imm8
 *
 * Exceptions type 4. AVX cpuid check for both 128-bit and 256-bit operation.
 */
FNIEMOP_DEF_1(iemOpCommonAvxAvx_Vx_Wx_Ib, PCIEMOPMEDIAF2IMM8, pImpl)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        if (pVCpu->iem.s.uVexLength)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_LOCAL(X86YMMREG,             uDst);
            IEM_MC_ARG_LOCAL_REF(PX86YMMREG,    puDst,  uDst,         0);
            IEM_MC_LOCAL(X86YMMREG,             uSrc);
            IEM_MC_ARG_LOCAL_REF(PCX86YMMREG,   puSrc,  uSrc,         1);
            IEM_MC_FETCH_YREG_YMM(uSrc,        IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_ARG_CONST(uint8_t,           bImmArg, /*=*/ bImm,  2);
            IEM_MC_CALL_AVX_AIMPL_3(pImpl->pfnU256, puDst, puSrc, bImmArg);
            IEM_MC_STORE_YREG_YMM_ZX_VLMAX(    IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_LOCAL(X86XMMREG,             uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst,  uDst,         0);
            IEM_MC_ARG(PCX86XMMREG,             puSrc,                1);
            IEM_MC_REF_XREG_XMM_CONST(puSrc,   IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_ARG_CONST(uint8_t,           bImmArg, /*=*/ bImm,  2);
            IEM_MC_CALL_AVX_AIMPL_3(pImpl->pfnU128, puDst, puSrc, bImmArg);
            IEM_MC_STORE_XREG_XMM(             IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP(          IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        /*
         * Register, memory.
         */
        if (pVCpu->iem.s.uVexLength)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,               GCPtrEffSrc);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_LOCAL(X86YMMREG,             uDst);
            IEM_MC_ARG_LOCAL_REF(PX86YMMREG,    puDst,          uDst, 0);
            IEM_MC_LOCAL(X86YMMREG,             uSrc);
            IEM_MC_ARG_LOCAL_REF(PCX86YMMREG,   puSrc,          uSrc, 1);
            IEM_MC_ARG_CONST(uint8_t,           bImmArg, /*=*/ bImm,  2);
            IEM_MC_FETCH_MEM_YMM_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_CALL_AVX_AIMPL_3(pImpl->pfnU256, puDst, puSrc, bImmArg);
            IEM_MC_STORE_YREG_YMM_ZX_VLMAX(    IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,               GCPtrEffSrc);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_LOCAL(X86XMMREG,             uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst,  uDst,         0);
            IEM_MC_LOCAL(X86XMMREG,             uSrc);
            IEM_MC_ARG_LOCAL_REF(PCX86XMMREG,   puSrc, uSrc,          1);
            IEM_MC_ARG_CONST(uint8_t,           bImmArg, /*=*/ bImm,  2);
            IEM_MC_FETCH_MEM_XMM_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_CALL_AVX_AIMPL_3(pImpl->pfnU128, puDst, puSrc, bImmArg);
            IEM_MC_STORE_XREG_XMM(             IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP(          IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/**
 * Common worker for AVX instructions on the forms:
 *     - vpermilps/d    xmm0, xmm1/mem128, imm8
 *     - vpermilps/d    ymm0, ymm1/mem256, imm8
 *
 * Takes function table for function w/o implicit state parameter.
 *
 * Exceptions type 4. AVX cpuid check for both 128-bit and 256-bit operation.
 */
FNIEMOP_DEF_1(iemOpCommonAvxAvx_Vx_Wx_Ib_Opt, PCIEMOPMEDIAOPTF2IMM8, pImpl)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        if (pVCpu->iem.s.uVexLength)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_LOCAL(RTUINT256U,            uDst);
            IEM_MC_LOCAL(RTUINT256U,            uSrc);
            IEM_MC_ARG_LOCAL_REF(PRTUINT256U,   puDst,  uDst,         0);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc,  uSrc,         1);
            IEM_MC_ARG_CONST(uint8_t,           bImmArg, /*=*/ bImm,  2);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_FETCH_YREG_U256(uSrc,        IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_VOID_AIMPL_3(pImpl->pfnU256, puDst, puSrc, bImmArg);
            IEM_MC_STORE_YREG_U256_ZX_VLMAX(    IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_ARG(PRTUINT128U,             puDst,                0);
            IEM_MC_ARG(PCRTUINT128U,            puSrc,                1);
            IEM_MC_ARG_CONST(uint8_t,           bImmArg, /*=*/ bImm,  2);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_REF_XREG_U128(puDst,         IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_REF_XREG_U128_CONST(puSrc,   IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_VOID_AIMPL_3(pImpl->pfnU128, puDst, puSrc, bImmArg);
            IEM_MC_CLEAR_YREG_128_UP(           IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        /*
         * Register, memory.
         */
        if (pVCpu->iem.s.uVexLength)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTUINT256U,            uDst);
            IEM_MC_LOCAL(RTUINT256U,            uSrc);
            IEM_MC_LOCAL(RTGCPTR,               GCPtrEffSrc);
            IEM_MC_ARG_LOCAL_REF(PRTUINT256U,   puDst,          uDst, 0);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc,          uSrc, 1);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_ARG_CONST(uint8_t,           bImmArg, /*=*/ bImm,  2);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_FETCH_MEM_U256_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_CALL_VOID_AIMPL_3(pImpl->pfnU256, puDst, puSrc, bImmArg);
            IEM_MC_STORE_YREG_U256_ZX_VLMAX(    IEM_GET_MODRM_REG(pVCpu, bRm), uDst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTUINT128U,            uSrc);
            IEM_MC_LOCAL(RTGCPTR,               GCPtrEffSrc);
            IEM_MC_ARG(PRTUINT128U,             puDst,                0);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,  puSrc, uSrc,          1);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_ARG_CONST(uint8_t,           bImmArg, /*=*/ bImm,  2);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_FETCH_MEM_U128_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_REF_XREG_U128(puDst,         IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_CALL_VOID_AIMPL_3(pImpl->pfnU128, puDst, puSrc, bImmArg);
            IEM_MC_CLEAR_YREG_128_UP(           IEM_GET_MODRM_REG(pVCpu, bRm));

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/**
 * Common worker for AVX instructions on the forms:
 *     - vblendps/d    xmm0, xmm1, xmm2/mem128, imm8
 *     - vblendps/d    ymm0, ymm1, ymm2/mem256, imm8
 *
 * Takes function table for function w/o implicit state parameter.
 *
 * Exceptions type 4. AVX cpuid check for both 128-bit and 256-bit operation.
 */
FNIEMOP_DEF_1(iemOpCommonAvxAvx_Vx_Hx_Wx_Ib_Opt, PCIEMOPMEDIAOPTF3IMM8, pImpl)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        if (pVCpu->iem.s.uVexLength)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
            IEM_MC_LOCAL(RTUINT256U,            uDst);
            IEM_MC_LOCAL(RTUINT256U,            uSrc1);
            IEM_MC_LOCAL(RTUINT256U,            uSrc2);
            IEM_MC_ARG_LOCAL_REF(PRTUINT256U,   puDst,  uDst,  0);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc1, uSrc1, 1);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc2, uSrc2, 2);
            IEM_MC_ARG_CONST(uint8_t,           bImmArg, /*=*/ bImm, 3);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_FETCH_YREG_U256(uSrc1,   IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_FETCH_YREG_U256(uSrc2,   IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnU256, puDst, puSrc1, puSrc2, bImmArg);
            IEM_MC_STORE_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
            IEM_MC_ARG(PRTUINT128U,          puDst,  0);
            IEM_MC_ARG(PCRTUINT128U,         puSrc1, 1);
            IEM_MC_ARG(PCRTUINT128U,         puSrc2, 2);
            IEM_MC_ARG_CONST(uint8_t,        bImmArg, /*=*/ bImm, 3);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_REF_XREG_U128(puDst,        IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_REF_XREG_U128_CONST(puSrc1, IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_REF_XREG_U128_CONST(puSrc2, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnU128, puDst, puSrc1, puSrc2, bImmArg);
            IEM_MC_CLEAR_YREG_128_UP(          IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        /*
         * Register, memory.
         */
        if (pVCpu->iem.s.uVexLength)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTUINT256U,            uDst);
            IEM_MC_LOCAL(RTUINT256U,            uSrc1);
            IEM_MC_LOCAL(RTUINT256U,            uSrc2);
            IEM_MC_LOCAL(RTGCPTR,               GCPtrEffSrc);
            IEM_MC_ARG_LOCAL_REF(PRTUINT256U,   puDst,  uDst,  0);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc1, uSrc1, 1);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc2, uSrc2, 2);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEM_MC_ARG_CONST(uint8_t,           bImmArg, /*=*/ bImm, 3);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_FETCH_MEM_U256_NO_AC(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_FETCH_YREG_U256(uSrc1,      IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnU256, puDst, puSrc1, puSrc2, bImmArg);
            IEM_MC_STORE_YREG_U256_ZX_VLMAX(   IEM_GET_MODRM_REG(pVCpu, bRm), uDst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTUINT128U,                uSrc2);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
            IEM_MC_ARG(PRTUINT128U,                 puDst,         0);
            IEM_MC_ARG(PCRTUINT128U,                puSrc1,        1);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,      puSrc2, uSrc2, 2);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEM_MC_ARG_CONST(uint8_t,               bImmArg, /*=*/ bImm, 3);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_FETCH_MEM_U128_NO_AC(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_REF_XREG_U128(puDst,         IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_REF_XREG_U128_CONST(puSrc1,  IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnU128, puDst, puSrc1, puSrc2, bImmArg);
            IEM_MC_CLEAR_YREG_128_UP(           IEM_GET_MODRM_REG(pVCpu, bRm));

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/** Opcode VEX.66.0F3A 0x00. */
FNIEMOP_DEF(iemOp_vpermq_Vqq_Wqq_Ib)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx2);
        IEM_MC_LOCAL(RTUINT256U,            uDst);
        IEM_MC_LOCAL(RTUINT256U,            uSrc);
        IEM_MC_ARG_LOCAL_REF(PRTUINT256U,   puDst, uDst,         0);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc, uSrc,         1);
        IEM_MC_ARG_CONST(uint8_t,           bImmArg, /*=*/ bImm, 2);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();
        IEM_MC_FETCH_YREG_U256(uSrc, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpermq_u256, iemAImpl_vpermq_u256_fallback),
                                 puDst, puSrc, bImmArg);
        IEM_MC_STORE_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTGCPTR,               GCPtrEffSrc);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx2);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_LOCAL(RTUINT256U,            uSrc);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc, uSrc,         1);
        IEM_MC_FETCH_MEM_U256_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_LOCAL(RTUINT256U,            uDst);
        IEM_MC_ARG_LOCAL_REF(PRTUINT256U,   puDst, uDst,         0);
        IEM_MC_ARG_CONST(uint8_t,           bImmArg, /*=*/ bImm, 2);
        IEM_MC_CALL_VOID_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpermq_u256, iemAImpl_vpermq_u256_fallback),
                                 puDst, puSrc, bImmArg);
        IEM_MC_STORE_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode VEX.66.0F3A 0x01. */
FNIEMOP_DEF(iemOp_vpermpd_Vqq_Wqq_Ib)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx2);
        IEM_MC_LOCAL(RTUINT256U,            uDst);
        IEM_MC_LOCAL(RTUINT256U,            uSrc);
        IEM_MC_ARG_LOCAL_REF(PRTUINT256U,   puDst, uDst,         0);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc, uSrc,         1);
        IEM_MC_ARG_CONST(uint8_t,           bImmArg, /*=*/ bImm, 2);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();
        IEM_MC_FETCH_YREG_U256(uSrc, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpermpd_u256, iemAImpl_vpermpd_u256_fallback),
                                 puDst, puSrc, bImmArg);
        IEM_MC_STORE_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTGCPTR,               GCPtrEffSrc);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx2);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_LOCAL(RTUINT256U,            uSrc);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc, uSrc,         1);
        IEM_MC_FETCH_MEM_U256_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_LOCAL(RTUINT256U,            uDst);
        IEM_MC_ARG_LOCAL_REF(PRTUINT256U,   puDst, uDst,         0);
        IEM_MC_ARG_CONST(uint8_t,           bImmArg, /*=*/ bImm, 2);
        IEM_MC_CALL_VOID_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpermpd_u256, iemAImpl_vpermpd_u256_fallback),
                                 puDst, puSrc, bImmArg);
        IEM_MC_STORE_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode VEX.66.0F3A 0x02.
 * AVX2,AVX2  */
FNIEMOP_DEF(iemOp_vpblendd_Vx_Hx_Wx_Ib)
{
    IEMOP_MNEMONIC4(VEX_RVMI, VPBLENDD, vpblendd, Vx_WO, Hx, Wx, Ib, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3IMM8_INIT_VARS(vpblendd);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Ib_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.66.0F3A 0x03 - invalid */


/** Opcode VEX.66.0F3A 0x04.
 * AVX,AVX  */
FNIEMOP_DEF(iemOp_vpermilps_Vx_Wx_Ib)
{
    IEMOP_MNEMONIC3(VEX_RMI, VPERMILPS, vpermilps, Vx_WO, Wx, Ib, DISOPTYPE_HARMLESS, IEMOPHINT_VEX_V_ZERO);
    IEMOPMEDIAOPTF2IMM8_INIT_VARS(vpermilps);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx_Vx_Wx_Ib_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/** Opcode VEX.66.0F3A 0x05.
 * AVX,AVX  */
FNIEMOP_DEF(iemOp_vpermilpd_Vx_Wx_Ib)
{
    IEMOP_MNEMONIC3(VEX_RMI, VPERMILPD, vpermilpd, Vx_WO, Wx, Ib, DISOPTYPE_HARMLESS, IEMOPHINT_VEX_V_ZERO);
    IEMOPMEDIAOPTF2IMM8_INIT_VARS(vpermilpd);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx_Vx_Wx_Ib_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/** Opcode VEX.66.0F3A 0x06 (vex only) */
FNIEMOP_DEF(iemOp_vperm2f128_Vqq_Hqq_Wqq_Ib)
{
    IEMOP_MNEMONIC4(VEX_RVMI, VPERM2F128, vperm2f128, Vqq_WO, Hqq, Wqq, Ib, DISOPTYPE_HARMLESS, 0);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_VEX_DECODING_L1_EX(fAvx2);
        IEM_MC_LOCAL(RTUINT256U,            uDst);
        IEM_MC_LOCAL(RTUINT256U,            uSrc1);
        IEM_MC_LOCAL(RTUINT256U,            uSrc2);
        IEM_MC_ARG_LOCAL_REF(PRTUINT256U,   puDst,   uDst,  0);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc1,  uSrc1, 1);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc2,  uSrc2, 2);
        IEM_MC_ARG_CONST(uint8_t,           bImmArg, bImm,  3);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();
        IEM_MC_FETCH_YREG_U256(uSrc1, IEM_GET_EFFECTIVE_VVVV(pVCpu));
        IEM_MC_FETCH_YREG_U256(uSrc2, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vperm2f128_u256, iemAImpl_vperm2f128_u256_fallback),
                                                             puDst, puSrc1, puSrc2, bImmArg);
        IEM_MC_STORE_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT256U,                uDst);
        IEM_MC_LOCAL(RTUINT256U,                uSrc1);
        IEM_MC_LOCAL(RTUINT256U,                uSrc2);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
        IEM_MC_ARG_LOCAL_REF(PRTUINT256U,       puDst,   uDst,  0);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,      puSrc1,  uSrc1, 1);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,      puSrc2,  uSrc2, 2);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_ARG_CONST(uint8_t,               bImmArg, bImm,  3);
        IEMOP_HLP_DONE_VEX_DECODING_L1_EX(fAvx2);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_FETCH_YREG_U256(uSrc1, IEM_GET_EFFECTIVE_VVVV(pVCpu));
        IEM_MC_FETCH_MEM_U256_NO_AC(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vperm2f128_u256, iemAImpl_vperm2f128_u256_fallback),
                                                             puDst, puSrc1, puSrc2, bImmArg);
        IEM_MC_STORE_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/*  Opcode VEX.66.0F3A 0x07 - invalid */


/** Opcode VEX.66.0F3A 0x08. */
FNIEMOP_DEF(iemOp_vroundps_Vx_Wx_Ib)
{
    IEMOP_MNEMONIC3(VEX_RMI, VROUNDPS, vroundps, Vx_WO, Wx, Ib, DISOPTYPE_HARMLESS, IEMOPHINT_VEX_V_ZERO);
    IEMOPMEDIAF2IMM8_INIT_VARS(        vroundps);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx_Vx_Wx_Ib, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/** Opcode VEX.66.0F3A 0x09. */
FNIEMOP_DEF(iemOp_vroundpd_Vx_Wx_Ib)
{
    IEMOP_MNEMONIC3(VEX_RMI, VROUNDPD, vroundpd, Vx_WO, Wx, Ib, DISOPTYPE_HARMLESS, IEMOPHINT_VEX_V_ZERO);
    IEMOPMEDIAF2IMM8_INIT_VARS(        vroundpd);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx_Vx_Wx_Ib, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/** Opcode VEX.66.0F3A 0x0a. */
FNIEMOP_DEF(iemOp_vroundss_Vss_Wss_Ib)
{
    IEMOP_MNEMONIC4(VEX_RVMI, VROUNDSS, vroundss, Vps, Hps, Wss, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM32, XMM32.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();
        IEM_MC_LOCAL(IEMMEDIAF2XMMSRC,              Src);
        IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2XMMSRC,    pSrc,           Src,    1);
        IEM_MC_FETCH_XREG_PAIR_XMM(Src, IEM_GET_EFFECTIVE_VVVV(pVCpu), IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_LOCAL(X86XMMREG,                     uDst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,            puDst,          uDst,   0);
        IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/  bImm,   2);
        IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vroundss_u128, iemAImpl_vroundss_u128_fallback),
                                puDst, pSrc, bImmArg);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
        IEM_MC_CLEAR_YREG_128_UP( IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM32, [mem32].
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTGCPTR,                       GCPtrEffSrc);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();
        IEM_MC_LOCAL(IEMMEDIAF2XMMSRC,              Src);
        IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2XMMSRC,    pSrc,           Src,    1);
        IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/ bImm,    2);
        IEM_MC_FETCH_MEM_XMM_U32_AND_XREG_XMM(Src, IEM_GET_EFFECTIVE_VVVV(pVCpu),
                                              0 /*a_iDword*/, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_LOCAL(X86XMMREG,                     uDst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,            puDst,          uDst,   0);
        IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vroundss_u128, iemAImpl_vroundss_u128_fallback),
                                puDst, pSrc, bImmArg);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
        IEM_MC_CLEAR_YREG_128_UP( IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode VEX.66.0F3A 0x0b. */
FNIEMOP_DEF(iemOp_vroundsd_Vsd_Wsd_Ib)
{
    IEMOP_MNEMONIC4(VEX_RVMI, VROUNDSD, vroundsd, Vpd, Hpd, Wsd, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM64, XMM64.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();
        IEM_MC_LOCAL(IEMMEDIAF2XMMSRC,              Src);
        IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2XMMSRC,    pSrc,           Src,    1);
        IEM_MC_FETCH_XREG_PAIR_XMM(Src, IEM_GET_EFFECTIVE_VVVV(pVCpu), IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_LOCAL(X86XMMREG,                     uDst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,            puDst,          uDst,   0);
        IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/  bImm,   2);
        IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vroundsd_u128, iemAImpl_vroundsd_u128_fallback),
                                puDst, pSrc, bImmArg);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
        IEM_MC_CLEAR_YREG_128_UP( IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM64, [mem64].
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTGCPTR,                       GCPtrEffSrc);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();
        IEM_MC_LOCAL(IEMMEDIAF2XMMSRC,              Src);
        IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2XMMSRC,    pSrc,           Src,    1);
        IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/ bImm,    2);
        IEM_MC_FETCH_MEM_XMM_U64_AND_XREG_XMM(Src, IEM_GET_EFFECTIVE_VVVV(pVCpu),
                                              0 /*a_iQword*/, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_LOCAL(X86XMMREG,                     uDst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,            puDst,          uDst,   0);
        IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vroundsd_u128, iemAImpl_vroundsd_u128_fallback),
                                puDst, pSrc, bImmArg);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
        IEM_MC_CLEAR_YREG_128_UP( IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode VEX.66.0F3A 0x0c.
 * AVX,AVX  */
FNIEMOP_DEF(iemOp_vblendps_Vx_Hx_Wx_Ib)
{
    IEMOP_MNEMONIC4(VEX_RVMI, VBLENDPS, vblendps, Vx_WO, Hx, Wx, Ib, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3IMM8_INIT_VARS(vblendps);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx_Vx_Hx_Wx_Ib_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/** Opcode VEX.66.0F3A 0x0d.
 * AVX,AVX  */
FNIEMOP_DEF(iemOp_vblendpd_Vx_Hx_Wx_Ib)
{
    IEMOP_MNEMONIC4(VEX_RVMI, VBLENDPD, vblendpd, Vx_WO, Hx, Wx, Ib, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3IMM8_INIT_VARS(vblendpd);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx_Vx_Hx_Wx_Ib_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/** Opcode VEX.66.0F3A 0x0e.
 * AVX,AVX2  */
FNIEMOP_DEF(iemOp_vpblendw_Vx_Hx_Wx_Ib)
{
    IEMOP_MNEMONIC4(VEX_RVMI, VPBLENDW, vpblendw, Vx_WO, Hx, Wx, Ib, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3IMM8_INIT_VARS(vpblendw);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Ib_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/** Opcode VEX.0F3A 0x0f - invalid. */


/** Opcode VEX.66.0F3A 0x0f.
 * AVX,AVX2  */
FNIEMOP_DEF(iemOp_vpalignr_Vx_Hx_Wx_Ib)
{
    IEMOP_MNEMONIC4(VEX_RVMI, VPALIGNR, vpalignr, Vx_WO, Hx, Wx, Ib, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3IMM8_INIT_VARS(vpalignr);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Ib_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.66.0F3A 0x10 - invalid */
/*  Opcode VEX.66.0F3A 0x11 - invalid */
/*  Opcode VEX.66.0F3A 0x12 - invalid */
/*  Opcode VEX.66.0F3A 0x13 - invalid */


/** Opcode VEX.66.0F3A 0x14 - vpextrb Eb, Vdq, Ib */
FNIEMOP_DEF(iemOp_vpextrb_Eb_Vdq_Ib)
{
    IEMOP_MNEMONIC3(VEX_MRI, VPEXTRB, vpextrb, Eb, Vdq, Ib, DISOPTYPE_HARMLESS, IEMOPHINT_VEX_L_ZERO | IEMOPHINT_VEX_V_ZERO | IEMOPHINT_IGNORES_REXW);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * greg32, XMM, imm8.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_LOCAL(uint8_t, uValue);
        IEM_MC_FETCH_XREG_U8(uValue, IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 15 /*a_iByte*/);
        IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_RM(pVCpu, bRm), uValue);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * [mem8], XMM, imm8.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);

        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_LOCAL(uint8_t, uValue);
        IEM_MC_FETCH_XREG_U8(uValue, IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 15 /*a_iByte*/);
        IEM_MC_STORE_MEM_U8(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uValue);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode VEX.66.0F3A 0x15 - vpextrw Ew, Vdq, Ib */
FNIEMOP_DEF(iemOp_vpextrw_Ew_Vdq_Ib)
{
    /** @todo testcase: check that this ignores VEX.W. */
    IEMOP_MNEMONIC3(VEX_MRI, VPEXTRW, vpextrw, Ew_WO, Vdq, Ib, DISOPTYPE_HARMLESS, IEMOPHINT_VEX_L_ZERO | IEMOPHINT_VEX_V_ZERO | IEMOPHINT_IGNORES_REXW);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * greg32, XMM, imm8.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(uint16_t,  uValue);

        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_FETCH_XREG_U16(uValue, IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 7);
        IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_RM(pVCpu, bRm), uValue);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * [mem16], XMM, imm8.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(uint16_t,  uValue);
        IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);

        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_FETCH_XREG_U16(uValue, IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 7);
        IEM_MC_STORE_MEM_U16(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uValue);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode VEX.66.0F3A 0x16 - vpextrd / vpextrq Eq / Ey, Vdq, Ib */
FNIEMOP_DEF(iemOp_vpextrd_q_Ey_Vdq_Ib)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_REX_W)
    {
        IEMOP_MNEMONIC3(VEX_MRI, VPEXTRQ, vpextrq, Eq_WO, Vdq, Ib, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_ZERO | IEMOPHINT_VEX_W_ONE);
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /*
             * greg64, XMM, imm8.
             */
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(uint64_t,  uValue);

            IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_FETCH_XREG_U64(uValue, IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 1);
            IEM_MC_STORE_GREG_U64(IEM_GET_MODRM_RM(pVCpu, bRm), uValue);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /*
             * [mem64], XMM, imm8.
             */
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(uint64_t,  uValue);
            IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);

            IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_FETCH_XREG_U64(uValue, IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 1);
            IEM_MC_STORE_MEM_U64(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uValue);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        /**
         * @opdone
         */
        IEMOP_MNEMONIC3(VEX_MRI, VPEXTRD, vpextrd, Ey_WO, Vdq, Ib, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_ZERO | IEMOPHINT_VEX_W_ZERO);
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /*
             * greg32, XMM, imm8.
             */
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(uint32_t,  uValue);

            IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_FETCH_XREG_U32(uValue, IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 3);
            IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_RM(pVCpu, bRm), uValue);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /*
             * [mem32], XMM, imm8.
             */
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(uint32_t,  uValue);
            IEM_MC_LOCAL(RTGCPTR,   GCPtrEffSrc);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);

            IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_FETCH_XREG_U32(uValue, IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 3);
            IEM_MC_STORE_MEM_U32(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uValue);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/** Opcode VEX.66.0F3A 0x17. */
FNIEMOP_DEF(iemOp_vextractps_Ed_Vdq_Ib)
{
    //IEMOP_MNEMONIC3(VEX_MRI_REG, VEXTRACTPS, vextractps, Ed, Vdq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_VEX_L_ZERO);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * greg32, XMM, imm8.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(uint32_t,      uSrc);

        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_FETCH_XREG_U32(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 3 /*a_iDword*/);
        IEM_MC_STORE_GREG_U32(      IEM_GET_MODRM_RM(pVCpu, bRm), uSrc);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * [mem32], XMM, imm8.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(uint32_t,      uSrc);
        IEM_MC_LOCAL(RTGCPTR,       GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_FETCH_XREG_U32(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 3 /*a_iDword*/);
        IEM_MC_STORE_MEM_U32(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode VEX.66.0F3A 0x18 (vex only). */
FNIEMOP_DEF(iemOp_vinsertf128_Vqq_Hqq_Wqq_Ib)
{
    //IEMOP_MNEMONIC4(VEX_RMI, VINSERTF128, vinsertf128, Vx, Hx, Wx, Ib, DISOPTYPE_HARMLESS, 0);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_VEX_DECODING_L1_EX(fAvx2);
        IEM_MC_LOCAL(RTUINT128U,            uSrc);

        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_FETCH_XREG_U128(uSrc,                                  IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_COPY_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), IEM_GET_EFFECTIVE_VVVV(pVCpu));
        IEM_MC_STORE_YREG_U128(        IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 1, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT128U,            uSrc);
        IEM_MC_LOCAL(RTGCPTR,               GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_VEX_DECODING_L1_EX(fAvx2);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_FETCH_MEM_U128_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_COPY_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), IEM_GET_EFFECTIVE_VVVV(pVCpu));
        IEM_MC_STORE_YREG_U128(        IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 1, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode VEX.66.0F3A 0x19 (vex only). */
FNIEMOP_DEF(iemOp_vextractf128_Wdq_Vqq_Ib)
{
    IEMOP_MNEMONIC3(VEX_MRI, VEXTRACTF128, vextractf128, Wdq, Vqq, Ib, DISOPTYPE_HARMLESS, 0);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_VEX_DECODING_L1_AND_NO_VVVV_EX(fAvx2);

        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_LOCAL(RTUINT128U, uDst);
        IEM_MC_FETCH_YREG_U128(uDst,   IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 1);
        IEM_MC_STORE_XREG_U128(        IEM_GET_MODRM_RM(pVCpu, bRm), uDst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTGCPTR,               GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_VEX_DECODING_L1_AND_NO_VVVV_EX(fAvx2);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_LOCAL(RTUINT128U, uDst);
        IEM_MC_FETCH_YREG_U128(uDst,   IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 1);
        IEM_MC_STORE_MEM_U128_NO_AC(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uDst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/*  Opcode VEX.66.0F3A 0x1a - invalid */
/*  Opcode VEX.66.0F3A 0x1b - invalid */
/*  Opcode VEX.66.0F3A 0x1c - invalid */
/** Opcode VEX.66.0F3A 0x1d (vex only). */
FNIEMOP_STUB(iemOp_vcvtps2ph_Wx_Vx_Ib);
/*  Opcode VEX.66.0F3A 0x1e - invalid */
/*  Opcode VEX.66.0F3A 0x1f - invalid */


/** Opcode VEX.66.0F3A 0x20. */
FNIEMOP_DEF(iemOp_vpinsrb_Vdq_Hdq_RyMb_Ib)
{
    /*IEMOP_MNEMONIC4(VEX_RMVI, VPINSRB, vpinsrb, Vdq, Hdq, Ey, Ib, DISOPTYPE_HARMLESS, IEMOPHINT_VEX_L_ZERO);*/ /** @todo */
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT128U,       uSrc1);
        IEM_MC_LOCAL(uint8_t,          uValue);

        IEMOP_HLP_DONE_VEX_DECODING_L0_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_FETCH_XREG_U128(uSrc1,  IEM_GET_EFFECTIVE_VVVV(pVCpu));
        IEM_MC_FETCH_GREG_U8(uValue,   IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_STORE_XREG_U128(        IEM_GET_MODRM_REG(pVCpu, bRm), uSrc1);
        IEM_MC_STORE_XREG_U8(          IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 15, uValue);
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
        IEM_MC_LOCAL(RTUINT128U,       uSrc1);
        IEM_MC_LOCAL(uint8_t,          uValue);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_VEX_DECODING_L0_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_FETCH_XREG_U128(uSrc1,  IEM_GET_EFFECTIVE_VVVV(pVCpu));
        IEM_MC_FETCH_MEM_U8(uValue,    pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_XREG_U128(        IEM_GET_MODRM_REG(pVCpu, bRm), uSrc1);
        IEM_MC_STORE_XREG_U8(          IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 15, uValue);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode VEX.66.0F3A 0x21, */
FNIEMOP_DEF(iemOp_vinsertps_Vdq_Hdq_UdqMd_Ib)
{
    //IEMOP_MNEMONIC4(VEX_RVMR_REG, VINSERTPS, vinsertps, Vdq, Hdq, UdqMd, Ib, DISOPTYPE_X86_AVX, IEMOPHINT_VEX_L_ZERO);  /// @todo
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM, XMM, XMM, imm8.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT128U,      uSrc1);
        IEM_MC_LOCAL(uint32_t,        uSrc2);

        IEMOP_HLP_DONE_VEX_DECODING_L0_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_FETCH_XREG_U128(uSrc1, IEM_GET_EFFECTIVE_VVVV(pVCpu));
        IEM_MC_FETCH_XREG_U32(uSrc2,  IEM_GET_MODRM_RM(pVCpu, bRm),  (bImm >> 6) & 3);
        IEM_MC_STORE_XREG_U128(       IEM_GET_MODRM_REG(pVCpu, bRm), uSrc1);
        IEM_MC_STORE_XREG_U32(        IEM_GET_MODRM_REG(pVCpu, bRm), (bImm >> 4) & 3, uSrc2);
        IEM_MC_CLEAR_XREG_U32_MASK(   IEM_GET_MODRM_REG(pVCpu, bRm), bImm);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * XMM, XMM, [mem32], imm8.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTGCPTR,         GCPtrEffSrc);
        IEM_MC_LOCAL(RTUINT128U,      uSrc1);
        IEM_MC_LOCAL(uint32_t,        uSrc2);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_VEX_DECODING_L0_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_FETCH_XREG_U128(uSrc1, IEM_GET_EFFECTIVE_VVVV(pVCpu));
        IEM_MC_FETCH_MEM_U32(uSrc2,   pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_XREG_U128(       IEM_GET_MODRM_REG(pVCpu, bRm), uSrc1);
        IEM_MC_STORE_XREG_U32(        IEM_GET_MODRM_REG(pVCpu, bRm), (bImm >> 4) & 3, uSrc2);
        IEM_MC_CLEAR_XREG_U32_MASK(   IEM_GET_MODRM_REG(pVCpu, bRm), bImm);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode VEX.66.0F3A 0x22. */
FNIEMOP_DEF(iemOp_vpinsrd_q_Vdq_Hdq_Ey_Ib)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_REX_W)
    {
        /*IEMOP_MNEMONIC4(VEX_RMVI, VPINSRQ, vpinsrq, Vdq, Hdq, Ey, Ib, DISOPTYPE_HARMLESS, IEMOPHINT_VEX_L_ZERO);*/ /** @todo */
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /*
             * Register, register.
             */
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTUINT128U,       uSrc1);
            IEM_MC_LOCAL(uint64_t,         uValue);

            IEMOP_HLP_DONE_VEX_DECODING_L0_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_FETCH_XREG_U128(uSrc1,  IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_FETCH_GREG_U64(uValue,  IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_STORE_XREG_U128(        IEM_GET_MODRM_REG(pVCpu, bRm), uSrc1);
            IEM_MC_STORE_XREG_U64(         IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 1, uValue);
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
            IEM_MC_LOCAL(RTUINT128U,       uSrc1);
            IEM_MC_LOCAL(uint64_t,         uValue);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEMOP_HLP_DONE_VEX_DECODING_L0_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_FETCH_XREG_U128(uSrc1,  IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_FETCH_MEM_U64(uValue,   pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_STORE_XREG_U128(        IEM_GET_MODRM_REG(pVCpu, bRm), uSrc1);
            IEM_MC_STORE_XREG_U64(         IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 1, uValue);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        /*IEMOP_MNEMONIC4(VEX_RMVI, VPINSRD, vpinsrd, Vdq, Hdq, Ey, Ib, DISOPTYPE_HARMLESS, IEMOPHINT_VEX_L_ZERO);*/ /** @todo */
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /*
             * Register, register.
             */
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEM_MC_LOCAL(RTUINT128U,       uSrc1);
            IEM_MC_LOCAL(uint32_t,         uValue);

            IEMOP_HLP_DONE_VEX_DECODING_L0_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_FETCH_XREG_U128(uSrc1,  IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_FETCH_GREG_U32(uValue,  IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_STORE_XREG_U128(        IEM_GET_MODRM_REG(pVCpu, bRm), uSrc1);
            IEM_MC_STORE_XREG_U32(         IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 3, uValue);
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
            IEM_MC_LOCAL(RTUINT128U,       uSrc1);
            IEM_MC_LOCAL(uint32_t,         uValue);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEMOP_HLP_DONE_VEX_DECODING_L0_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_FETCH_XREG_U128(uSrc1,  IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_FETCH_MEM_U32(uValue,   pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_STORE_XREG_U128(        IEM_GET_MODRM_REG(pVCpu, bRm), uSrc1);
            IEM_MC_STORE_XREG_U32(         IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 3, uValue);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/*  Opcode VEX.66.0F3A 0x23 - invalid */
/*  Opcode VEX.66.0F3A 0x24 - invalid */
/*  Opcode VEX.66.0F3A 0x25 - invalid */
/*  Opcode VEX.66.0F3A 0x26 - invalid */
/*  Opcode VEX.66.0F3A 0x27 - invalid */
/*  Opcode VEX.66.0F3A 0x28 - invalid */
/*  Opcode VEX.66.0F3A 0x29 - invalid */
/*  Opcode VEX.66.0F3A 0x2a - invalid */
/*  Opcode VEX.66.0F3A 0x2b - invalid */
/*  Opcode VEX.66.0F3A 0x2c - invalid */
/*  Opcode VEX.66.0F3A 0x2d - invalid */
/*  Opcode VEX.66.0F3A 0x2e - invalid */
/*  Opcode VEX.66.0F3A 0x2f - invalid */


/*  Opcode VEX.66.0F3A 0x30 - invalid */
/*  Opcode VEX.66.0F3A 0x31 - invalid */
/*  Opcode VEX.66.0F3A 0x32 - invalid */
/*  Opcode VEX.66.0F3A 0x33 - invalid */
/*  Opcode VEX.66.0F3A 0x34 - invalid */
/*  Opcode VEX.66.0F3A 0x35 - invalid */
/*  Opcode VEX.66.0F3A 0x36 - invalid */
/*  Opcode VEX.66.0F3A 0x37 - invalid */


/** Opcode VEX.66.0F3A 0x38 (vex only). */
FNIEMOP_DEF(iemOp_vinserti128_Vqq_Hqq_Wqq_Ib)
{
    //IEMOP_MNEMONIC4(VEX_RMI, VINSERTI128, vinserti128, Vx, Hx, Wx, Ib, DISOPTYPE_HARMLESS, 0);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_VEX_DECODING_L1_EX(fAvx2);
        IEM_MC_LOCAL(RTUINT128U,            uSrc);

        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_FETCH_XREG_U128(uSrc,                                  IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_COPY_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), IEM_GET_EFFECTIVE_VVVV(pVCpu));
        IEM_MC_STORE_YREG_U128(        IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 1, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT128U,            uSrc);
        IEM_MC_LOCAL(RTGCPTR,               GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_VEX_DECODING_L1_EX(fAvx2);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_FETCH_MEM_U128_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_COPY_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), IEM_GET_EFFECTIVE_VVVV(pVCpu));
        IEM_MC_STORE_YREG_U128(        IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 1, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode VEX.66.0F3A 0x39 (vex only). */
FNIEMOP_DEF(iemOp_vextracti128_Wdq_Vqq_Ib)
{
    IEMOP_MNEMONIC3(VEX_MRI, VEXTRACTI128, vextracti128, Wdq, Vqq, Ib, DISOPTYPE_HARMLESS, 0);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_VEX_DECODING_L1_AND_NO_VVVV_EX(fAvx2);

        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_LOCAL(RTUINT128U, uDst);
        IEM_MC_FETCH_YREG_U128(uDst,   IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 1);
        IEM_MC_STORE_XREG_U128(        IEM_GET_MODRM_RM(pVCpu, bRm), uDst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTGCPTR,               GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_VEX_DECODING_L1_AND_NO_VVVV_EX(fAvx2);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_LOCAL(RTUINT128U, uDst);
        IEM_MC_FETCH_YREG_U128(uDst,   IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 1);
        IEM_MC_STORE_MEM_U128_NO_AC(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uDst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/*  Opcode VEX.66.0F3A 0x3a - invalid */
/*  Opcode VEX.66.0F3A 0x3b - invalid */
/*  Opcode VEX.66.0F3A 0x3c - invalid */
/*  Opcode VEX.66.0F3A 0x3d - invalid */
/*  Opcode VEX.66.0F3A 0x3e - invalid */
/*  Opcode VEX.66.0F3A 0x3f - invalid */


/** Opcode VEX.66.0F3A 0x40. */
FNIEMOP_DEF(iemOp_vdpps_Vx_Hx_Wx_Ib)
{
    IEMOP_MNEMONIC4(VEX_RVMI, VDPPS, vdpps, Vps, Hps, Wps, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, Register
         */
        if (pVCpu->iem.s.uVexLength)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_LOCAL(IEMMEDIAF2YMMSRC,              uSrc);
            IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2YMMSRC,    puSrc,          uSrc,   1);
            IEM_MC_FETCH_YREG_PAIR_YMM(uSrc, IEM_GET_EFFECTIVE_VVVV(pVCpu), IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_LOCAL(X86YMMREG,                     uDst);
            IEM_MC_ARG_LOCAL_REF(PX86YMMREG,            puDst,          uDst,   0);
            IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/  bImm,   2);
            IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vdpps_u256, iemAImpl_vdpps_u256_fallback),
                                    puDst, puSrc, bImmArg);
            IEM_MC_STORE_YREG_YMM_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_LOCAL(IEMMEDIAF2XMMSRC,              uSrc);
            IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2XMMSRC,    puSrc,          uSrc,   1);
            IEM_MC_FETCH_XREG_PAIR_XMM(uSrc, IEM_GET_EFFECTIVE_VVVV(pVCpu), IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_LOCAL(X86XMMREG,                     uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,            puDst,          uDst,   0);
            IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/  bImm,   2);
            IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vdpps_u128, iemAImpl_vdpps_u128_fallback),
                                    puDst, puSrc, bImmArg);
            IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP( IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        /*
         * Register, Memory.
         */
        if (pVCpu->iem.s.uVexLength)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,                       GCPtrEffSrc);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_LOCAL(IEMMEDIAF2YMMSRC,              uSrc);
            IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2YMMSRC,    puSrc,         uSrc,    1);
            IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/ bImm,    2);
            IEM_MC_FETCH_MEM_YMM_ALIGN_AVX_AND_YREG_YMM(uSrc, IEM_GET_EFFECTIVE_VVVV(pVCpu),
                                                        pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_LOCAL(X86YMMREG,                     uDst);
            IEM_MC_ARG_LOCAL_REF(PX86YMMREG,            puDst,          uDst,   0);
            IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vdpps_u256, iemAImpl_vdpps_u256_fallback),
                                    puDst, puSrc, bImmArg);
            IEM_MC_STORE_YREG_YMM_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,                       GCPtrEffSrc);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_LOCAL(IEMMEDIAF2XMMSRC,              uSrc);
            IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2XMMSRC,    puSrc,         uSrc,    1);
            IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/ bImm,    2);
            IEM_MC_FETCH_MEM_XMM_ALIGN_SSE_AND_XREG_XMM(uSrc, IEM_GET_EFFECTIVE_VVVV(pVCpu),
                                                        pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_LOCAL(X86XMMREG,                     uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,            puDst,          uDst,   0);
            IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vdpps_u128, iemAImpl_vdpps_u128_fallback),
                                    puDst, puSrc, bImmArg);
            IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP( IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/** Opcode VEX.66.0F3A 0x41, */
FNIEMOP_DEF(iemOp_vdppd_Vdq_Hdq_Wdq_Ib)
{
    IEMOP_MNEMONIC4(VEX_RVMI, VDPPD, vdppd, Vpd, Hpd, Wpd, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_VEX_L_ZERO);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, Register
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();
        IEM_MC_LOCAL(IEMMEDIAF2XMMSRC,              uSrc);
        IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2XMMSRC,    puSrc,          uSrc,   1);
        IEM_MC_FETCH_XREG_PAIR_XMM(uSrc, IEM_GET_EFFECTIVE_VVVV(pVCpu), IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_LOCAL(X86XMMREG,                     uDst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,            puDst,          uDst,   0);
        IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/  bImm,   2);
        IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vdppd_u128, iemAImpl_vdppd_u128_fallback),
                                puDst, puSrc, bImmArg);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
        IEM_MC_CLEAR_YREG_128_UP( IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, Memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTGCPTR,                       GCPtrEffSrc);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();
        IEM_MC_LOCAL(IEMMEDIAF2XMMSRC,              uSrc);
        IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2XMMSRC,    puSrc,         uSrc,    1);
        IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/ bImm,    2);
        IEM_MC_FETCH_MEM_XMM_ALIGN_SSE_AND_XREG_XMM(uSrc, IEM_GET_EFFECTIVE_VVVV(pVCpu),
                                                    pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_LOCAL(X86XMMREG,                     uDst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,            puDst,          uDst,   0);
        IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vdppd_u128, iemAImpl_vdppd_u128_fallback),
                                puDst, puSrc, bImmArg);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
        IEM_MC_CLEAR_YREG_128_UP( IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode VEX.66.0F3A 0x42. */
FNIEMOP_DEF(iemOp_vmpsadbw_Vx_Hx_Wx_Ib)
{
    IEMOP_MNEMONIC4(VEX_RVMI, VMPSADBW, vmpsadbw, Vx_WO, Hx, Wx, Ib, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3IMM8_INIT_VARS(vmpsadbw);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx_Vx_Hx_Wx_Ib_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/*  Opcode VEX.66.0F3A 0x43 - invalid */


/** Opcode VEX.66.0F3A 0x44. */
FNIEMOP_DEF(iemOp_vpclmulqdq_Vdq_Hdq_Wdq_Ib)
{
    IEMOP_MNEMONIC4(VEX_RVMI, VPCLMULQDQ, vpclmulqdq, Vdq_WO, Hdq, Wdq, Id, DISOPTYPE_HARMLESS, IEMOPHINT_VEX_L_ZERO);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_VEX_DECODING_L0_EX(fPclMul);
        IEM_MC_ARG(PRTUINT128U,          puDst,  0);
        IEM_MC_ARG(PCRTUINT128U,         puSrc1, 1);
        IEM_MC_ARG(PCRTUINT128U,         puSrc2, 2);
        IEM_MC_ARG_CONST(uint8_t,        bImmArg, /*=*/ bImm, 3);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();
        IEM_MC_REF_XREG_U128(puDst,        IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(puSrc1, IEM_GET_EFFECTIVE_VVVV(pVCpu));
        IEM_MC_REF_XREG_U128_CONST(puSrc2, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fPclMul, iemAImpl_vpclmulqdq_u128, iemAImpl_vpclmulqdq_u128_fallback),
                                                             puDst, puSrc1, puSrc2, bImmArg);
        IEM_MC_CLEAR_YREG_128_UP(          IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc2);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
        IEM_MC_ARG(PRTUINT128U,                 puDst,         0);
        IEM_MC_ARG(PCRTUINT128U,                puSrc1,        1);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,      puSrc2, uSrc2, 2);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_ARG_CONST(uint8_t,               bImmArg, /*=*/ bImm, 3);
        IEMOP_HLP_DONE_VEX_DECODING_L0_EX(fPclMul);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_FETCH_MEM_U128_NO_AC(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_REF_XREG_U128(puDst,         IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(puSrc1,  IEM_GET_EFFECTIVE_VVVV(pVCpu));
        IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fPclMul, iemAImpl_vpclmulqdq_u128, iemAImpl_vpclmulqdq_u128_fallback),
                                                             puDst, puSrc1, puSrc2, bImmArg);
        IEM_MC_CLEAR_YREG_128_UP(           IEM_GET_MODRM_REG(pVCpu, bRm));

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/*  Opcode VEX.66.0F3A 0x45 - invalid */


/** Opcode VEX.66.0F3A 0x46 (vex only)  */
FNIEMOP_DEF(iemOp_vperm2i128_Vqq_Hqq_Wqq_Ib)
{
    IEMOP_MNEMONIC4(VEX_RVMI, VPERM2I128, vperm2i128, Vqq_WO, Hqq, Wqq, Ib, DISOPTYPE_HARMLESS, IEMOPHINT_VEX_L_ONE);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_VEX_DECODING_L1_EX(fAvx2);
        IEM_MC_LOCAL(RTUINT256U,            uDst);
        IEM_MC_LOCAL(RTUINT256U,            uSrc1);
        IEM_MC_LOCAL(RTUINT256U,            uSrc2);
        IEM_MC_ARG_LOCAL_REF(PRTUINT256U,   puDst,   uDst,  0);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc1,  uSrc1, 1);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc2,  uSrc2, 2);
        IEM_MC_ARG_CONST(uint8_t,           bImmArg, bImm,  3);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();
        IEM_MC_FETCH_YREG_U256(uSrc1, IEM_GET_EFFECTIVE_VVVV(pVCpu));
        IEM_MC_FETCH_YREG_U256(uSrc2, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vperm2i128_u256, iemAImpl_vperm2i128_u256_fallback),
                                                             puDst, puSrc1, puSrc2, bImmArg);
        IEM_MC_STORE_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT256U,                uDst);
        IEM_MC_LOCAL(RTUINT256U,                uSrc1);
        IEM_MC_LOCAL(RTUINT256U,                uSrc2);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
        IEM_MC_ARG_LOCAL_REF(PRTUINT256U,       puDst,   uDst,  0);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,      puSrc1,  uSrc1, 1);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,      puSrc2,  uSrc2, 2);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_ARG_CONST(uint8_t,               bImmArg, bImm,  3);
        IEMOP_HLP_DONE_VEX_DECODING_L1_EX(fAvx2);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_FETCH_YREG_U256(uSrc1, IEM_GET_EFFECTIVE_VVVV(pVCpu));
        IEM_MC_FETCH_MEM_U256_NO_AC(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vperm2i128_u256, iemAImpl_vperm2i128_u256_fallback),
                                                             puDst, puSrc1, puSrc2, bImmArg);
        IEM_MC_STORE_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/*  Opcode VEX.66.0F3A 0x47 - invalid */
/** Opcode VEX.66.0F3A 0x48 (AMD tables only). */
FNIEMOP_STUB(iemOp_vperlmilzz2ps_Vx_Hx_Wp_Lx);
/** Opcode VEX.66.0F3A 0x49 (AMD tables only). */
FNIEMOP_STUB(iemOp_vperlmilzz2pd_Vx_Hx_Wp_Lx);


/**
 * Common worker for AVX2 instructions on the forms:
 *     - vblendvps/d    xmm0, xmm1, xmm2/mem128, xmm4
 *     - vblendvps/d    ymm0, ymm1, ymm2/mem256, ymm4
 *
 * Exceptions type 4. AVX cpuid check for both 128-bit and 256-bit operations.
 * Additionally, it triggers \#UD if VEX.W is 1.
 */
FNIEMOP_DEF_1(iemOpCommonAvxAvx_Vx_Hx_Wx_Lx, PCIEMOPBLENDOP, pImpl)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        uint8_t bOp4; IEM_OPCODE_GET_NEXT_U8(&bOp4);
        if (pVCpu->iem.s.uVexLength)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_W0_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_LOCAL(RTUINT256U,            uDst);
            IEM_MC_ARG_LOCAL_REF(PRTUINT256U,   puDst,  uDst,  0);
            IEM_MC_LOCAL(RTUINT256U,            uSrc1);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc1, uSrc1, 1);
            IEM_MC_FETCH_YREG_U256(uSrc1,       IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_LOCAL(RTUINT256U,            uSrc2);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc2, uSrc2, 2);
            IEM_MC_FETCH_YREG_U256(uSrc2,       IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_LOCAL(RTUINT256U,            uSrc3);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc3, uSrc3, 3);
            IEM_MC_FETCH_YREG_U256(uSrc3,       IEM_GET_IMM8_REG(pVCpu, bOp4));
            IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnU256, puDst, puSrc1, puSrc2, puSrc3);
            IEM_MC_STORE_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_W0_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_ARG(PRTUINT128U,             puDst,  0);
            IEM_MC_REF_XREG_U128(puDst,         IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ARG(PCRTUINT128U,            puSrc1, 1);
            IEM_MC_REF_XREG_U128_CONST(puSrc1,  IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_ARG(PCRTUINT128U,            puSrc2, 2);
            IEM_MC_REF_XREG_U128_CONST(puSrc2,  IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_ARG(PCRTUINT128U,            puSrc3, 3);
            IEM_MC_REF_XREG_U128_CONST(puSrc3,  IEM_GET_IMM8_REG(pVCpu, bOp4));
            IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnU128, puDst, puSrc1, puSrc2, puSrc3);
            IEM_MC_CLEAR_YREG_128_UP(           IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        /*
         * Register, memory.
         */
        if (pVCpu->iem.s.uVexLength)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,               GCPtrEffSrc);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            uint8_t bOp4; IEM_OPCODE_GET_NEXT_U8(&bOp4);
            IEMOP_HLP_DONE_VEX_DECODING_W0_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(RTUINT256U,            uSrc2);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc2, uSrc2, 2);
            IEM_MC_FETCH_MEM_U256_NO_AC(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

            IEM_MC_LOCAL(RTUINT256U,            uSrc1);
            IEM_MC_FETCH_YREG_U256(uSrc1,       IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc1, uSrc1, 1);
            IEM_MC_LOCAL(RTUINT256U,            uSrc3);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc3, uSrc3, 3);
            IEM_MC_FETCH_YREG_U256(uSrc3,       IEM_GET_IMM8_REG(pVCpu, bOp4));
            IEM_MC_LOCAL(RTUINT256U,            uDst);
            IEM_MC_ARG_LOCAL_REF(PRTUINT256U,   puDst,  uDst,  0);
            IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnU256, puDst, puSrc1, puSrc2, puSrc3);
            IEM_MC_STORE_YREG_U256_ZX_VLMAX(    IEM_GET_MODRM_REG(pVCpu, bRm), uDst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            uint8_t bOp4; IEM_OPCODE_GET_NEXT_U8(&bOp4);
            IEMOP_HLP_DONE_VEX_DECODING_W0_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(RTUINT128U,                uSrc2);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,      puSrc2, uSrc2, 2);
            IEM_MC_FETCH_MEM_U128_NO_AC(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

            IEM_MC_ARG(PRTUINT128U,                 puDst,         0);
            IEM_MC_REF_XREG_U128(puDst,             IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ARG(PCRTUINT128U,                puSrc1,        1);
            IEM_MC_REF_XREG_U128_CONST(puSrc1,      IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_ARG(PCRTUINT128U,                puSrc3,        3);
            IEM_MC_REF_XREG_U128_CONST(puSrc3,      IEM_GET_IMM8_REG(pVCpu, bOp4));
            IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnU128, puDst, puSrc1, puSrc2, puSrc3);
            IEM_MC_CLEAR_YREG_128_UP(               IEM_GET_MODRM_REG(pVCpu, bRm));

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/** Opcode VEX.66.0F3A 0x4a (vex only).
 * AVX, AVX  */
FNIEMOP_DEF(iemOp_vblendvps_Vx_Hx_Wx_Lx)
{
    IEMOP_MNEMONIC4(VEX_RVMR, VBLENDVPS, vblendvps, Vx, Hx, Wx, Lx, DISOPTYPE_HARMLESS, 0);
    IEMOPBLENDOP_INIT_VARS(vblendvps);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx_Vx_Hx_Wx_Lx, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/** Opcode VEX.66.0F3A 0x4b (vex only).
 * AVX, AVX  */
FNIEMOP_DEF(iemOp_vblendvpd_Vx_Hx_Wx_Lx)
{
    IEMOP_MNEMONIC4(VEX_RVMR, VBLENDVPD, vblendvpd, Vx, Hx, Wx, Lx, DISOPTYPE_HARMLESS, 0);
    IEMOPBLENDOP_INIT_VARS(vblendvpd);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx_Vx_Hx_Wx_Lx, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/**
 * Common worker for AVX2 instructions on the forms:
 *     - vpxxx    xmm0, xmm1, xmm2/mem128, xmm4
 *     - vpxxx    ymm0, ymm1, ymm2/mem256, ymm4
 *
 * Exceptions type 4. AVX cpuid check for 128-bit operation, AVX2 for 256-bit.
 * Additionally, both VEX.W and VEX.L must be zero.
 */
FNIEMOP_DEF_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Lx, PCIEMOPBLENDOP, pImpl)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        uint8_t bOp4; IEM_OPCODE_GET_NEXT_U8(&bOp4);
        if (pVCpu->iem.s.uVexLength)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_W0_EX(fAvx2);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(RTUINT256U,            uSrc1);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc1, uSrc1, 1);
            IEM_MC_FETCH_YREG_U256(uSrc1,   IEM_GET_EFFECTIVE_VVVV(pVCpu));

            IEM_MC_LOCAL(RTUINT256U,            uSrc2);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc2, uSrc2, 2);
            IEM_MC_FETCH_YREG_U256(uSrc2,   IEM_GET_MODRM_RM(pVCpu, bRm));

            IEM_MC_LOCAL(RTUINT256U,            uSrc3);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc3, uSrc3, 3);
            IEM_MC_FETCH_YREG_U256(uSrc3,   IEM_GET_IMM8_REG(pVCpu, bOp4));

            IEM_MC_LOCAL(RTUINT256U,            uDst);
            IEM_MC_ARG_LOCAL_REF(PRTUINT256U,   puDst,  uDst,  0);

            IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnU256, puDst, puSrc1, puSrc2, puSrc3);

            IEM_MC_STORE_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_W0_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_ARG(PRTUINT128U,             puDst,  0);
            IEM_MC_REF_XREG_U128(puDst,         IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ARG(PCRTUINT128U,            puSrc1, 1);
            IEM_MC_REF_XREG_U128_CONST(puSrc1,  IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_ARG(PCRTUINT128U,            puSrc2, 2);
            IEM_MC_REF_XREG_U128_CONST(puSrc2,  IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_ARG(PCRTUINT128U,            puSrc3, 3);
            IEM_MC_REF_XREG_U128_CONST(puSrc3,  IEM_GET_IMM8_REG(pVCpu, bOp4));
            IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnU128, puDst, puSrc1, puSrc2, puSrc3);
            IEM_MC_CLEAR_YREG_128_UP(           IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        /*
         * Register, memory.
         */
        if (pVCpu->iem.s.uVexLength)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,               GCPtrEffSrc);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            uint8_t bOp4; IEM_OPCODE_GET_NEXT_U8(&bOp4);

            IEMOP_HLP_DONE_VEX_DECODING_W0_EX(fAvx2);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(RTUINT256U,            uSrc2);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc2, uSrc2, 2);
            IEM_MC_FETCH_MEM_U256_NO_AC(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

            IEM_MC_LOCAL(RTUINT256U,            uSrc1);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc1, uSrc1, 1);
            IEM_MC_FETCH_YREG_U256(uSrc1,       IEM_GET_EFFECTIVE_VVVV(pVCpu));

            IEM_MC_LOCAL(RTUINT256U,            uSrc3);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc3, uSrc3, 3);
            IEM_MC_FETCH_YREG_U256(uSrc3,       IEM_GET_IMM8_REG(pVCpu, bOp4));

            IEM_MC_LOCAL(RTUINT256U,            uDst);
            IEM_MC_ARG_LOCAL_REF(PRTUINT256U,   puDst,  uDst,  0);

            IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnU256, puDst, puSrc1, puSrc2, puSrc3);

            IEM_MC_STORE_YREG_U256_ZX_VLMAX(    IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,               GCPtrEffSrc);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            uint8_t bOp4; IEM_OPCODE_GET_NEXT_U8(&bOp4);

            IEMOP_HLP_DONE_VEX_DECODING_W0_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(RTUINT128U,            uSrc2);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,  puSrc2, uSrc2, 2);
            IEM_MC_FETCH_MEM_U128_NO_AC(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

            IEM_MC_ARG(PRTUINT128U,             puDst,         0);
            IEM_MC_REF_XREG_U128(puDst,         IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ARG(PCRTUINT128U,            puSrc1,        1);
            IEM_MC_REF_XREG_U128_CONST(puSrc1,  IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_ARG(PCRTUINT128U,            puSrc3,        3);
            IEM_MC_REF_XREG_U128_CONST(puSrc3,  IEM_GET_IMM8_REG(pVCpu, bOp4));
            IEM_MC_CALL_VOID_AIMPL_4(pImpl->pfnU128, puDst, puSrc1, puSrc2, puSrc3);
            IEM_MC_CLEAR_YREG_128_UP(           IEM_GET_MODRM_REG(pVCpu, bRm));

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/** Opcode VEX.66.0F3A 0x4c (vex only).
 * AVX, AVX2  */
FNIEMOP_DEF(iemOp_vpblendvb_Vx_Hx_Wx_Lx)
{
    /** @todo testcase: cover VEX.W=1 and check that it triggers \#UD on both real
     *        and emulated hardware. */
    IEMOP_MNEMONIC4(VEX_RVMR, VPBLENDVB, vpblendvb, Vx_WO, Hx, Wx, Lx, DISOPTYPE_HARMLESS, IEMOPHINT_VEX_W_ZERO);
    IEMOPBLENDOP_INIT_VARS(vpblendvb);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Lx, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.66.0F3A 0x4d - invalid */
/*  Opcode VEX.66.0F3A 0x4e - invalid */
/*  Opcode VEX.66.0F3A 0x4f - invalid */


/*  Opcode VEX.66.0F3A 0x50 - invalid */
/*  Opcode VEX.66.0F3A 0x51 - invalid */
/*  Opcode VEX.66.0F3A 0x52 - invalid */
/*  Opcode VEX.66.0F3A 0x53 - invalid */
/*  Opcode VEX.66.0F3A 0x54 - invalid */
/*  Opcode VEX.66.0F3A 0x55 - invalid */
/*  Opcode VEX.66.0F3A 0x56 - invalid */
/*  Opcode VEX.66.0F3A 0x57 - invalid */
/*  Opcode VEX.66.0F3A 0x58 - invalid */
/*  Opcode VEX.66.0F3A 0x59 - invalid */
/*  Opcode VEX.66.0F3A 0x5a - invalid */
/*  Opcode VEX.66.0F3A 0x5b - invalid */
/** Opcode VEX.66.0F3A 0x5c (AMD tables only). */
FNIEMOP_STUB(iemOp_vfmaddsubps_Vx_Lx_Wx_Hx);
/** Opcode VEX.66.0F3A 0x5d (AMD tables only). */
FNIEMOP_STUB(iemOp_vfmaddsubpd_Vx_Lx_Wx_Hx);
/** Opcode VEX.66.0F3A 0x5e (AMD tables only). */
FNIEMOP_STUB(iemOp_vfmsubaddps_Vx_Lx_Wx_Hx);
/** Opcode VEX.66.0F3A 0x5f (AMD tables only). */
FNIEMOP_STUB(iemOp_vfmsubaddpd_Vx_Lx_Wx_Hx);


/**
 * @opcode      0x60
 * @oppfx       0x66
 * @opflmodify  cf,pf,af,zf,sf,of
 * @opflclear   pf,af
 */
FNIEMOP_DEF(iemOp_vpcmpestrm_Vdq_Wdq_Ib)
{
    IEMOP_MNEMONIC3(VEX_RMI, VPCMPESTRM, vpcmpestrm, Vdq, Wdq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);

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
            IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
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
            IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                                 iemAImpl_vpcmpestrm_u128,
                                                                 iemAImpl_vpcmpestrm_u128_fallback),
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
            IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE();

            IEM_MC_FETCH_MEM_U128_AND_XREG_U128_AND_RAX_RDX_U64(Src, IEM_GET_MODRM_REG(pVCpu, bRm),
                                                                pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_REF_XREG_U128(puDst, 0 /*xmm0*/);
            IEM_MC_REF_EFLAGS(pEFlags);
            IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                                 iemAImpl_vpcmpestrm_u128,
                                                                 iemAImpl_vpcmpestrm_u128_fallback),
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
            IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
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
                                                                 iemAImpl_vpcmpestrm_u128,
                                                                 iemAImpl_vpcmpestrm_u128_fallback),
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
            IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE();

            IEM_MC_FETCH_MEM_U128_AND_XREG_U128_AND_EAX_EDX_U32_SX_U64(Src, IEM_GET_MODRM_REG(pVCpu, bRm),
                                                                       pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_REF_XREG_U128(puDst, 0 /*xmm0*/);
            IEM_MC_REF_EFLAGS(pEFlags);
            IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                                 iemAImpl_vpcmpestrm_u128,
                                                                 iemAImpl_vpcmpestrm_u128_fallback),
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
FNIEMOP_DEF(iemOp_vpcmpestri_Vdq_Wdq_Ib)
{
    IEMOP_MNEMONIC3(VEX_RMI, VPCMPESTRI, vpcmpestri, Vdq, Wdq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);

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
            IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
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
            IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                                 iemAImpl_vpcmpestri_u128,
                                                                 iemAImpl_vpcmpestri_u128_fallback),
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
            IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE();

            IEM_MC_FETCH_MEM_U128_AND_XREG_U128_AND_RAX_RDX_U64(Src, IEM_GET_MODRM_REG(pVCpu, bRm),
                                                                pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_REF_GREG_U32(pu32Ecx, X86_GREG_xCX);
            IEM_MC_CLEAR_HIGH_GREG_U64(X86_GREG_xCX);
            IEM_MC_REF_EFLAGS(pEFlags);
            IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                                 iemAImpl_vpcmpestri_u128,
                                                                 iemAImpl_vpcmpestri_u128_fallback),
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
            IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
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
            IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                                 iemAImpl_vpcmpestri_u128,
                                                                 iemAImpl_vpcmpestri_u128_fallback),
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
            IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
            IEM_MC_PREPARE_SSE_USAGE();

            IEM_MC_FETCH_MEM_U128_AND_XREG_U128_AND_EAX_EDX_U32_SX_U64(Src, IEM_GET_MODRM_REG(pVCpu, bRm),
                                                                       pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_REF_GREG_U32(pu32Ecx, X86_GREG_xCX);
            IEM_MC_CLEAR_HIGH_GREG_U64(X86_GREG_xCX);
            IEM_MC_REF_EFLAGS(pEFlags);
            IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                                 iemAImpl_vpcmpestri_u128,
                                                                 iemAImpl_vpcmpestri_u128_fallback),
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
FNIEMOP_DEF(iemOp_vpcmpistrm_Vdq_Wdq_Ib)
{
    IEMOP_MNEMONIC3(VEX_RMI, VPCMPISTRM, vpcmpistrm, Vdq, Wdq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
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
        IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                             iemAImpl_vpcmpistrm_u128,
                                                             iemAImpl_vpcmpistrm_u128_fallback),
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
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_FETCH_MEM_U128_AND_XREG_U128(Src, IEM_GET_MODRM_REG(pVCpu, bRm), pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_REF_XREG_U128(puDst, 0 /*xmm0*/);
        IEM_MC_REF_EFLAGS(pEFlags);
        IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                             iemAImpl_vpcmpistrm_u128,
                                                             iemAImpl_vpcmpistrm_u128_fallback),
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
FNIEMOP_DEF(iemOp_vpcmpistri_Vdq_Wdq_Ib)
{
    IEMOP_MNEMONIC3(VEX_RMI, VPCMPISTRI, vpcmpistri, Vdq, Wdq, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);

    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
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
                            IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                        iemAImpl_vpcmpistri_u128,
                                                        iemAImpl_vpcmpistri_u128_fallback),
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
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_SSE_USAGE();

        IEM_MC_FETCH_MEM_U128(Src2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_REF_XREG_U128_CONST(pSrc1, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_EFLAGS(pEFlags);
        IEM_MC_CALL_AIMPL_4(uint32_t, u32Ecx,
                            IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                        iemAImpl_vpcmpistri_u128,
                                                        iemAImpl_vpcmpistri_u128_fallback),
                            pEFlags, pSrc1, pSrc2, bImmArg);
        /** @todo testcase: High dword of RCX cleared? */
        IEM_MC_STORE_GREG_U32(X86_GREG_xCX, u32Ecx);
        IEM_MC_CLEAR_HIGH_GREG_U64(X86_GREG_xCX);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/*  Opcode VEX.66.0F3A 0x64 - invalid */
/*  Opcode VEX.66.0F3A 0x65 - invalid */
/*  Opcode VEX.66.0F3A 0x66 - invalid */
/*  Opcode VEX.66.0F3A 0x67 - invalid */
/** Opcode VEX.66.0F3A 0x68 (AMD tables only). */
FNIEMOP_STUB(iemOp_vfmaddps_Vx_Lx_Wx_Hx);
/** Opcode VEX.66.0F3A 0x69 (AMD tables only). */
FNIEMOP_STUB(iemOp_vfmaddpd_Vx_Lx_Wx_Hx);
/** Opcode VEX.66.0F3A 0x6a (AMD tables only). */
FNIEMOP_STUB(iemOp_vfmaddss_Vx_Lx_Wx_Hx);
/** Opcode VEX.66.0F3A 0x6b (AMD tables only). */
FNIEMOP_STUB(iemOp_vfmaddsd_Vx_Lx_Wx_Hx);
/** Opcode VEX.66.0F3A 0x6c (AMD tables only). */
FNIEMOP_STUB(iemOp_vfmsubps_Vx_Lx_Wx_Hx);
/** Opcode VEX.66.0F3A 0x6d (AMD tables only). */
FNIEMOP_STUB(iemOp_vfmsubpd_Vx_Lx_Wx_Hx);
/** Opcode VEX.66.0F3A 0x6e (AMD tables only). */
FNIEMOP_STUB(iemOp_vfmsubss_Vx_Lx_Wx_Hx);
/** Opcode VEX.66.0F3A 0x6f (AMD tables only). */
FNIEMOP_STUB(iemOp_vfmsubsd_Vx_Lx_Wx_Hx);

/*  Opcode VEX.66.0F3A 0x70 - invalid */
/*  Opcode VEX.66.0F3A 0x71 - invalid */
/*  Opcode VEX.66.0F3A 0x72 - invalid */
/*  Opcode VEX.66.0F3A 0x73 - invalid */
/*  Opcode VEX.66.0F3A 0x74 - invalid */
/*  Opcode VEX.66.0F3A 0x75 - invalid */
/*  Opcode VEX.66.0F3A 0x76 - invalid */
/*  Opcode VEX.66.0F3A 0x77 - invalid */
/** Opcode VEX.66.0F3A 0x78 (AMD tables only). */
FNIEMOP_STUB(iemOp_vfnmaddps_Vx_Lx_Wx_Hx);
/** Opcode VEX.66.0F3A 0x79 (AMD tables only). */
FNIEMOP_STUB(iemOp_vfnmaddpd_Vx_Lx_Wx_Hx);
/** Opcode VEX.66.0F3A 0x7a (AMD tables only). */
FNIEMOP_STUB(iemOp_vfnmaddss_Vx_Lx_Wx_Hx);
/** Opcode VEX.66.0F3A 0x7b (AMD tables only). */
FNIEMOP_STUB(iemOp_vfnmaddsd_Vx_Lx_Wx_Hx);
/** Opcode VEX.66.0F3A 0x7c (AMD tables only). */
FNIEMOP_STUB(iemOp_vfnmsubps_Vx_Lx_Wx_Hx);
/** Opcode VEX.66.0F3A 0x7d (AMD tables only). */
FNIEMOP_STUB(iemOp_vfnmsubpd_Vx_Lx_Wx_Hx);
/** Opcode VEX.66.0F3A 0x7e (AMD tables only). */
FNIEMOP_STUB(iemOp_vfnmsubss_Vx_Lx_Wx_Hx);
/** Opcode VEX.66.0F3A 0x7f (AMD tables only). */
FNIEMOP_STUB(iemOp_vfnmsubsd_Vx_Lx_Wx_Hx);

/*  Opcodes 0x0f 0x80 thru 0x0f 0xb0 are unused.  */


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
/*  Opcode      0x0f 0xcc - invalid */
/*  Opcode      0x0f 0xcd - invalid */
/*  Opcode      0x0f 0xce - invalid */
/*  Opcode      0x0f 0xcf - invalid */


/*  Opcode VEX.66.0F3A 0xd0 - invalid */
/*  Opcode VEX.66.0F3A 0xd1 - invalid */
/*  Opcode VEX.66.0F3A 0xd2 - invalid */
/*  Opcode VEX.66.0F3A 0xd3 - invalid */
/*  Opcode VEX.66.0F3A 0xd4 - invalid */
/*  Opcode VEX.66.0F3A 0xd5 - invalid */
/*  Opcode VEX.66.0F3A 0xd6 - invalid */
/*  Opcode VEX.66.0F3A 0xd7 - invalid */
/*  Opcode VEX.66.0F3A 0xd8 - invalid */
/*  Opcode VEX.66.0F3A 0xd9 - invalid */
/*  Opcode VEX.66.0F3A 0xda - invalid */
/*  Opcode VEX.66.0F3A 0xdb - invalid */
/*  Opcode VEX.66.0F3A 0xdc - invalid */
/*  Opcode VEX.66.0F3A 0xdd - invalid */
/*  Opcode VEX.66.0F3A 0xde - invalid */


/*  Opcode VEX.66.0F3A 0xdf - (aeskeygenassist). */
FNIEMOP_DEF(iemOp_vaeskeygen_Vdq_Wdq_Ib)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX_2(fAvx, fAesNi);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();
        IEM_MC_ARG(PRTUINT128U,                 puDst,               0);
        IEM_MC_ARG(PCRTUINT128U,                puSrc,               1);
        IEM_MC_ARG_CONST(uint8_t,               bImmArg, /*=*/ bImm, 2);
        IEM_MC_REF_XREG_U128(puDst,             IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_REF_XREG_U128_CONST(puSrc,       IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAesNi, iemAImpl_vaeskeygenassist_u128, iemAImpl_vaeskeygenassist_u128_fallback),
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
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_ARG_CONST(uint8_t,               bImmArg, /*=*/ bImm, 2);
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX_2(fAvx, fAesNi);

        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();
        IEM_MC_ARG(PRTUINT128U,                 puDst,               0);
        IEM_MC_REF_XREG_U128(puDst,             IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_LOCAL(RTUINT128U,                uSrc);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,      puSrc, uSrc,         1);
        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_CALL_VOID_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAesNi, iemAImpl_vaeskeygenassist_u128, iemAImpl_vaeskeygenassist_u128_fallback),
                                 puDst, puSrc, bImmArg);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0xf0
 * @oppfx       0xf2
 * @opflclass   unchanged
 */
FNIEMOP_DEF(iemOp_rorx_Gy_Ey_Ib)
{
    IEMOP_MNEMONIC3(VEX_RMI, RORX, rorx, Gy, Ey, Ib, DISOPTYPE_HARMLESS, IEMOPHINT_VEX_L_ZERO | IEMOPHINT_VEX_V_ZERO);
    IEMOP_HLP_IGNORE_VEX_W_PREFIX_IF_NOT_IN_64BIT();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        uint8_t bImm8; IEM_OPCODE_GET_NEXT_U8(&bImm8);
        if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_REX_W)
        {
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fBmi2);
            IEM_MC_ARG(uint64_t *,          pDst,    0);
            IEM_MC_ARG(uint64_t,            uSrc1,   1);
            IEM_MC_ARG_CONST(uint64_t,      uSrc2,   bImm8, 2);
            IEM_MC_FETCH_GREG_U64(uSrc1, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_REF_GREG_U64(pDst,    IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_CALL_VOID_AIMPL_3(iemAImpl_rorx_u64, pDst, uSrc1, uSrc2);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fBmi2);
            IEM_MC_ARG(uint32_t *,          pDst,    0);
            IEM_MC_ARG(uint32_t,            uSrc1,   1);
            IEM_MC_ARG_CONST(uint32_t,      uSrc2,   bImm8, 2);
            IEM_MC_FETCH_GREG_U32(uSrc1, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_REF_GREG_U32(pDst,    IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_CALL_VOID_AIMPL_3(iemAImpl_rorx_u32, pDst, uSrc1, uSrc2);
            IEM_MC_CLEAR_HIGH_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        /*
         * Register, memory.
         */
        if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_REX_W)
        {
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_ARG(uint64_t *,          pDst,    0);
            IEM_MC_ARG(uint64_t,            uSrc1,   1);
            IEM_MC_LOCAL(RTGCPTR,           GCPtrEffSrc);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
            uint8_t bImm8; IEM_OPCODE_GET_NEXT_U8(&bImm8);
            IEM_MC_ARG_CONST(uint64_t,      uSrc2,   bImm8, 2);
            IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fBmi2);
            IEM_MC_FETCH_MEM_U64(uSrc1, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_REF_GREG_U64(pDst, IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_CALL_VOID_AIMPL_3(iemAImpl_rorx_u64, pDst, uSrc1, uSrc2);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_ARG(uint32_t *,          pDst,    0);
            IEM_MC_ARG(uint32_t,            uSrc1,   1);
            IEM_MC_LOCAL(RTGCPTR,           GCPtrEffSrc);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
            uint8_t bImm8; IEM_OPCODE_GET_NEXT_U8(&bImm8);
            IEM_MC_ARG_CONST(uint32_t,      uSrc2,   bImm8, 2);
            IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fBmi2);
            IEM_MC_FETCH_MEM_U32(uSrc1, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_REF_GREG_U32(pDst, IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_CALL_VOID_AIMPL_3(iemAImpl_rorx_u32, pDst, uSrc1, uSrc2);
            IEM_MC_CLEAR_HIGH_GREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/**
 * VEX opcode map \#3.
 *
 * @sa      g_apfnThreeByte0f3a
 */
const PFNIEMOP g_apfnVexMap3[] =
{
    /*          no prefix,                  066h prefix                 f3h prefix,                 f2h prefix */
    /* 0x00 */  iemOp_InvalidNeedRMImm8,    iemOp_vpermq_Vqq_Wqq_Ib,    iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x01 */  iemOp_InvalidNeedRMImm8,    iemOp_vpermpd_Vqq_Wqq_Ib,   iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x02 */  iemOp_InvalidNeedRMImm8,    iemOp_vpblendd_Vx_Hx_Wx_Ib, iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x03 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x04 */  iemOp_InvalidNeedRMImm8,    iemOp_vpermilps_Vx_Wx_Ib,   iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x05 */  iemOp_InvalidNeedRMImm8,    iemOp_vpermilpd_Vx_Wx_Ib,   iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x06 */  iemOp_InvalidNeedRMImm8,    iemOp_vperm2f128_Vqq_Hqq_Wqq_Ib, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,
    /* 0x07 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x08 */  iemOp_InvalidNeedRMImm8,    iemOp_vroundps_Vx_Wx_Ib,    iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x09 */  iemOp_InvalidNeedRMImm8,    iemOp_vroundpd_Vx_Wx_Ib,    iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x0a */  iemOp_InvalidNeedRMImm8,    iemOp_vroundss_Vss_Wss_Ib,  iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x0b */  iemOp_InvalidNeedRMImm8,    iemOp_vroundsd_Vsd_Wsd_Ib,  iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x0c */  iemOp_InvalidNeedRMImm8,    iemOp_vblendps_Vx_Hx_Wx_Ib, iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x0d */  iemOp_InvalidNeedRMImm8,    iemOp_vblendpd_Vx_Hx_Wx_Ib, iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x0e */  iemOp_InvalidNeedRMImm8,    iemOp_vpblendw_Vx_Hx_Wx_Ib, iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x0f */  iemOp_InvalidNeedRMImm8,    iemOp_vpalignr_Vx_Hx_Wx_Ib, iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,

    /* 0x10 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x11 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x12 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x13 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x14 */  iemOp_InvalidNeedRMImm8,    iemOp_vpextrb_Eb_Vdq_Ib,    iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x15 */  iemOp_InvalidNeedRMImm8,    iemOp_vpextrw_Ew_Vdq_Ib,    iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x16 */  iemOp_InvalidNeedRMImm8,    iemOp_vpextrd_q_Ey_Vdq_Ib,  iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x17 */  iemOp_InvalidNeedRMImm8,    iemOp_vextractps_Ed_Vdq_Ib, iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x18 */  iemOp_InvalidNeedRMImm8,    iemOp_vinsertf128_Vqq_Hqq_Wqq_Ib, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,
    /* 0x19 */  iemOp_InvalidNeedRMImm8,    iemOp_vextractf128_Wdq_Vqq_Ib, iemOp_InvalidNeedRMImm8,  iemOp_InvalidNeedRMImm8,
    /* 0x1a */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x1b */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x1c */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x1d */  iemOp_InvalidNeedRMImm8,    iemOp_vcvtps2ph_Wx_Vx_Ib,   iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x1e */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x1f */  IEMOP_X4(iemOp_InvalidNeedRMImm8),

    /* 0x20 */  iemOp_InvalidNeedRMImm8,    iemOp_vpinsrb_Vdq_Hdq_RyMb_Ib, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,
    /* 0x21 */  iemOp_InvalidNeedRMImm8,    iemOp_vinsertps_Vdq_Hdq_UdqMd_Ib, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,
    /* 0x22 */  iemOp_InvalidNeedRMImm8,    iemOp_vpinsrd_q_Vdq_Hdq_Ey_Ib, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,
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
    /* 0x38 */  iemOp_InvalidNeedRMImm8,    iemOp_vinserti128_Vqq_Hqq_Wqq_Ib, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,
    /* 0x39 */  iemOp_InvalidNeedRMImm8,    iemOp_vextracti128_Wdq_Vqq_Ib, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,
    /* 0x3a */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x3b */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x3c */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x3d */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x3e */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x3f */  IEMOP_X4(iemOp_InvalidNeedRMImm8),

    /* 0x40 */  iemOp_InvalidNeedRMImm8,    iemOp_vdpps_Vx_Hx_Wx_Ib,    iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x41 */  iemOp_InvalidNeedRMImm8,    iemOp_vdppd_Vdq_Hdq_Wdq_Ib, iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x42 */  iemOp_InvalidNeedRMImm8,    iemOp_vmpsadbw_Vx_Hx_Wx_Ib, iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0x43 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x44 */  iemOp_InvalidNeedRMImm8,    iemOp_vpclmulqdq_Vdq_Hdq_Wdq_Ib, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,
    /* 0x45 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x46 */  iemOp_InvalidNeedRMImm8,    iemOp_vperm2i128_Vqq_Hqq_Wqq_Ib, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,
    /* 0x47 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x48 */  iemOp_InvalidNeedRMImm8,    iemOp_vperlmilzz2ps_Vx_Hx_Wp_Lx, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,
    /* 0x49 */  iemOp_InvalidNeedRMImm8,    iemOp_vperlmilzz2pd_Vx_Hx_Wp_Lx, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,
    /* 0x4a */  iemOp_InvalidNeedRMImm8,    iemOp_vblendvps_Vx_Hx_Wx_Lx, iemOp_InvalidNeedRMImm8,   iemOp_InvalidNeedRMImm8,
    /* 0x4b */  iemOp_InvalidNeedRMImm8,    iemOp_vblendvpd_Vx_Hx_Wx_Lx, iemOp_InvalidNeedRMImm8,   iemOp_InvalidNeedRMImm8,
    /* 0x4c */  iemOp_InvalidNeedRMImm8,    iemOp_vpblendvb_Vx_Hx_Wx_Lx, iemOp_InvalidNeedRMImm8,   iemOp_InvalidNeedRMImm8,
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
    /* 0x5c */  iemOp_InvalidNeedRMImm8,    iemOp_vfmaddsubps_Vx_Lx_Wx_Hx, iemOp_InvalidNeedRMImm8,   iemOp_InvalidNeedRMImm8,
    /* 0x5d */  iemOp_InvalidNeedRMImm8,    iemOp_vfmaddsubpd_Vx_Lx_Wx_Hx, iemOp_InvalidNeedRMImm8,   iemOp_InvalidNeedRMImm8,
    /* 0x5e */  iemOp_InvalidNeedRMImm8,    iemOp_vfmsubaddps_Vx_Lx_Wx_Hx, iemOp_InvalidNeedRMImm8,   iemOp_InvalidNeedRMImm8,
    /* 0x5f */  iemOp_InvalidNeedRMImm8,    iemOp_vfmsubaddpd_Vx_Lx_Wx_Hx, iemOp_InvalidNeedRMImm8,   iemOp_InvalidNeedRMImm8,

    /* 0x60 */  iemOp_InvalidNeedRMImm8,    iemOp_vpcmpestrm_Vdq_Wdq_Ib, iemOp_InvalidNeedRMImm8,   iemOp_InvalidNeedRMImm8,
    /* 0x61 */  iemOp_InvalidNeedRMImm8,    iemOp_vpcmpestri_Vdq_Wdq_Ib, iemOp_InvalidNeedRMImm8,   iemOp_InvalidNeedRMImm8,
    /* 0x62 */  iemOp_InvalidNeedRMImm8,    iemOp_vpcmpistrm_Vdq_Wdq_Ib, iemOp_InvalidNeedRMImm8,   iemOp_InvalidNeedRMImm8,
    /* 0x63 */  iemOp_InvalidNeedRMImm8,    iemOp_vpcmpistri_Vdq_Wdq_Ib, iemOp_InvalidNeedRMImm8,   iemOp_InvalidNeedRMImm8,
    /* 0x64 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x65 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x66 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x67 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x68 */  iemOp_InvalidNeedRMImm8,    iemOp_vfmaddps_Vx_Lx_Wx_Hx, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,
    /* 0x69 */  iemOp_InvalidNeedRMImm8,    iemOp_vfmaddpd_Vx_Lx_Wx_Hx, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,
    /* 0x6a */  iemOp_InvalidNeedRMImm8,    iemOp_vfmaddss_Vx_Lx_Wx_Hx, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,
    /* 0x6b */  iemOp_InvalidNeedRMImm8,    iemOp_vfmaddsd_Vx_Lx_Wx_Hx, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,
    /* 0x6c */  iemOp_InvalidNeedRMImm8,    iemOp_vfmsubps_Vx_Lx_Wx_Hx, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,
    /* 0x6d */  iemOp_InvalidNeedRMImm8,    iemOp_vfmsubpd_Vx_Lx_Wx_Hx, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,
    /* 0x6e */  iemOp_InvalidNeedRMImm8,    iemOp_vfmsubss_Vx_Lx_Wx_Hx, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,
    /* 0x6f */  iemOp_InvalidNeedRMImm8,    iemOp_vfmsubsd_Vx_Lx_Wx_Hx, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,

    /* 0x70 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x71 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x72 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x73 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x74 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x75 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x76 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x77 */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
    /* 0x78 */  iemOp_InvalidNeedRMImm8,    iemOp_vfnmaddps_Vx_Lx_Wx_Hx, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,
    /* 0x79 */  iemOp_InvalidNeedRMImm8,    iemOp_vfnmaddpd_Vx_Lx_Wx_Hx, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,
    /* 0x7a */  iemOp_InvalidNeedRMImm8,    iemOp_vfnmaddss_Vx_Lx_Wx_Hx, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,
    /* 0x7b */  iemOp_InvalidNeedRMImm8,    iemOp_vfnmaddsd_Vx_Lx_Wx_Hx, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,
    /* 0x7c */  iemOp_InvalidNeedRMImm8,    iemOp_vfnmsubps_Vx_Lx_Wx_Hx, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,
    /* 0x7d */  iemOp_InvalidNeedRMImm8,    iemOp_vfnmsubpd_Vx_Lx_Wx_Hx, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,
    /* 0x7e */  iemOp_InvalidNeedRMImm8,    iemOp_vfnmsubss_Vx_Lx_Wx_Hx, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,
    /* 0x7f */  iemOp_InvalidNeedRMImm8,    iemOp_vfnmsubsd_Vx_Lx_Wx_Hx, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,

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
    /* 0xcc */  IEMOP_X4(iemOp_InvalidNeedRMImm8),
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
    /* 0xdf */  iemOp_vaeskeygen_Vdq_Wdq_Ib, iemOp_InvalidNeedRMImm8,  iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,

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

    /* 0xf0 */  iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,    iemOp_rorx_Gy_Ey_Ib,
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
AssertCompile(RT_ELEMENTS(g_apfnVexMap3) == 1024);

/** @} */

