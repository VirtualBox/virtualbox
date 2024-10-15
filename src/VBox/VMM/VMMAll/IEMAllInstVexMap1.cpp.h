/* $Id: IEMAllInstVexMap1.cpp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEM - Instruction Decoding and Emulation.
 *
 * @remarks IEMAllInstTwoByte0f.cpp.h is a legacy mirror of this file.
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


/** @name VEX Opcode Map 1
 * @{
 */

/**
 * Common worker for AVX2 instructions on the forms:
 *     - vpxxx    xmm0, xmm1, xmm2/mem128
 *     - vpxxx    ymm0, ymm1, ymm2/mem256
 *
 * Exceptions type 4. AVX cpuid check for 128-bit operation, AVX2 for 256-bit.
 */
FNIEMOP_DEF_1(iemOpCommonAvxAvx2_Vx_Hx_Wx, PCIEMOPMEDIAF3, pImpl)
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
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx2);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(X86YMMREG,             uSrc1);
            IEM_MC_ARG_LOCAL_REF(PCX86YMMREG,   puSrc1, uSrc1, 1);
            IEM_MC_FETCH_YREG_YMM(uSrc1,    IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_LOCAL(X86YMMREG,             uSrc2);
            IEM_MC_ARG_LOCAL_REF(PCX86YMMREG,   puSrc2, uSrc2, 2);
            IEM_MC_FETCH_YREG_YMM(uSrc2,    IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_LOCAL(X86YMMREG,             uDst);
            IEM_MC_ARG_LOCAL_REF(PX86YMMREG,    puDst,  uDst,  0);
            IEM_MC_CALL_AVX_AIMPL_3(pImpl->pfnU256, puDst, puSrc1, puSrc2);
            IEM_MC_STORE_YREG_YMM_ZX_VLMAX( IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(X86XMMREG,             uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst,  uDst,  0);
            IEM_MC_ARG(PCX86XMMREG,             puSrc1, 1);
            IEM_MC_REF_XREG_XMM_CONST(puSrc1,  IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_ARG(PCX86XMMREG,             puSrc2, 2);
            IEM_MC_REF_XREG_XMM_CONST(puSrc2,  IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_AVX_AIMPL_3(pImpl->pfnU128, puDst, puSrc1, puSrc2);
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
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx2);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(X86YMMREG,             uSrc2);
            IEM_MC_ARG_LOCAL_REF(PCX86YMMREG,   puSrc2, uSrc2, 2);
            IEM_MC_FETCH_MEM_YMM_NO_AC(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_LOCAL(X86YMMREG,             uSrc1);
            IEM_MC_ARG_LOCAL_REF(PCX86YMMREG,   puSrc1, uSrc1, 1);
            IEM_MC_FETCH_YREG_YMM(uSrc1,       IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_LOCAL(X86YMMREG,             uDst);
            IEM_MC_ARG_LOCAL_REF(PX86YMMREG,    puDst,  uDst,  0);
            IEM_MC_CALL_AVX_AIMPL_3(pImpl->pfnU256, puDst, puSrc1, puSrc2);
            IEM_MC_STORE_YREG_YMM_ZX_VLMAX(    IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(X86XMMREG,             uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst,  uDst,  0);
            IEM_MC_LOCAL(X86XMMREG,             uSrc2);
            IEM_MC_ARG_LOCAL_REF(PCX86XMMREG,   puSrc2, uSrc2, 2);
            IEM_MC_FETCH_MEM_XMM_NO_AC(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_ARG(PCX86XMMREG,             puSrc1,        1);
            IEM_MC_REF_XREG_XMM_CONST(puSrc1,   IEM_GET_EFFECTIVE_VVVV(pVCpu));

            IEM_MC_CALL_AVX_AIMPL_3(pImpl->pfnU128, puDst, puSrc1, puSrc2);
            IEM_MC_STORE_XREG_XMM(             IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP(          IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/**
 * Common worker for scalar AVX/AVX2 instructions on the forms (addss,subss,etc.):
 *     - vxxxss       xmm0, xmm1, xmm2/mem32
 *
 * Exceptions type 4. AVX cpuid check for 128-bit operation.
 * Ignores VEX.L, from SDM:
 *     Software should ensure VADDSS is encoded with VEX.L=0.
 *     Encoding VADDSS with VEX.L=1 may encounter unpredictable behavior
 *     across different processor generations.
 */
FNIEMOP_DEF_1(iemOpCommonAvx_Vx_Hx_R32, PFNIEMAIMPLFPAVXF3U128R32, pfnU128)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_LOCAL(X86XMMREG,             uDst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst,  uDst,  0);
        IEM_MC_ARG(PCX86XMMREG,             puSrc1,        1);
        IEM_MC_REF_XREG_XMM_CONST(puSrc1,   IEM_GET_EFFECTIVE_VVVV(pVCpu));
        IEM_MC_ARG(PCRTFLOAT32U,            pr32Src2,      2);
        IEM_MC_REF_XREG_R32_CONST(pr32Src2, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_AVX_AIMPL_3(pfnU128, puDst, puSrc1, pr32Src2);
        IEM_MC_STORE_XREG_XMM(             IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
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
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_LOCAL(RTFLOAT32U,            r32Src2);
        IEM_MC_ARG_LOCAL_REF(PCRTFLOAT32U,  pr32Src2, r32Src2,  2);
        IEM_MC_FETCH_MEM_R32(r32Src2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_LOCAL(X86XMMREG,             uDst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst,    uDst,     0);
        IEM_MC_ARG(PCX86XMMREG,             puSrc1,             1);
        IEM_MC_REF_XREG_XMM_CONST(puSrc1,   IEM_GET_EFFECTIVE_VVVV(pVCpu));
        IEM_MC_CALL_AVX_AIMPL_3(pfnU128, puDst, puSrc1, pr32Src2);
        IEM_MC_STORE_XREG_XMM(             IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
        IEM_MC_CLEAR_YREG_128_UP(          IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * Common worker for scalar AVX/AVX2 instructions on the forms (addsd,subsd,etc.):
 *     - vxxxsd       xmm0, xmm1, xmm2/mem64
 *
 * Exceptions type 4. AVX cpuid check for 128-bit operation.
 * Ignores VEX.L, from SDM:
 *     Software should ensure VADDSD is encoded with VEX.L=0.
 *     Encoding VADDSD with VEX.L=1 may encounter unpredictable behavior
 *     across different processor generations.
 */
FNIEMOP_DEF_1(iemOpCommonAvx_Vx_Hx_R64, PFNIEMAIMPLFPAVXF3U128R64, pfnU128)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_LOCAL(X86XMMREG,             uDst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst,  uDst,  0);
        IEM_MC_ARG(PCX86XMMREG,             puSrc1,        1);
        IEM_MC_REF_XREG_XMM_CONST(puSrc1,   IEM_GET_EFFECTIVE_VVVV(pVCpu));
        IEM_MC_ARG(PCRTFLOAT64U,            pr64Src2,      2);
        IEM_MC_REF_XREG_R64_CONST(pr64Src2, IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_AVX_AIMPL_3(pfnU128, puDst, puSrc1, pr64Src2);
        IEM_MC_STORE_XREG_XMM(             IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
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
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_LOCAL(RTFLOAT64U,            r64Src2);
        IEM_MC_ARG_LOCAL_REF(PCRTFLOAT64U,  pr64Src2, r64Src2,  2);
        IEM_MC_FETCH_MEM_R64(r64Src2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_LOCAL(X86XMMREG,             uDst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst,    uDst,     0);
        IEM_MC_ARG(PCX86XMMREG,             puSrc1,             1);
        IEM_MC_REF_XREG_XMM_CONST(puSrc1,   IEM_GET_EFFECTIVE_VVVV(pVCpu));
        IEM_MC_CALL_AVX_AIMPL_3(pfnU128, puDst, puSrc1, pr64Src2);
        IEM_MC_STORE_XREG_XMM(             IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
        IEM_MC_CLEAR_YREG_128_UP(          IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * Common worker for AVX2 instructions on the forms:
 *     - vpxxx    xmm0, xmm1, xmm2/mem128
 *     - vpxxx    ymm0, ymm1, ymm2/mem256
 *
 * Takes function table for function w/o implicit state parameter.
 *
 * Exceptions type 4. AVX cpuid check for 128-bit operation, AVX2 for 256-bit.
 */
FNIEMOP_DEF_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, PCIEMOPMEDIAOPTF3, pImpl)
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
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx2);
            IEM_MC_LOCAL(RTUINT256U,            uDst);
            IEM_MC_LOCAL(RTUINT256U,            uSrc1);
            IEM_MC_LOCAL(RTUINT256U,            uSrc2);
            IEM_MC_ARG_LOCAL_REF(PRTUINT256U,   puDst,  uDst,  0);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc1, uSrc1, 1);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc2, uSrc2, 2);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_FETCH_YREG_U256(uSrc1,   IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_FETCH_YREG_U256(uSrc2,   IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_VOID_AIMPL_3(pImpl->pfnU256, puDst, puSrc1, puSrc2);
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
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_REF_XREG_U128(puDst,        IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_REF_XREG_U128_CONST(puSrc1, IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_REF_XREG_U128_CONST(puSrc2, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_VOID_AIMPL_3(pImpl->pfnU128, puDst, puSrc1, puSrc2);
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
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx2);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_FETCH_MEM_U256_NO_AC(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_FETCH_YREG_U256(uSrc1,      IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_CALL_VOID_AIMPL_3(pImpl->pfnU256, puDst, puSrc1, puSrc2);
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
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_FETCH_MEM_U128_NO_AC(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_REF_XREG_U128(puDst,         IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_REF_XREG_U128_CONST(puSrc1,  IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_CALL_VOID_AIMPL_3(pImpl->pfnU128, puDst, puSrc1, puSrc2);
            IEM_MC_CLEAR_YREG_128_UP(           IEM_GET_MODRM_REG(pVCpu, bRm));

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/**
 * Common worker for AVX2 instructions on the forms:
 *     - vpunpckhxx  xmm0, xmm1, xmm2/mem128
 *     - vpunpckhxx  ymm0, ymm1, ymm2/mem256
 *
 * The 128-bit memory version of this instruction may elect to skip fetching the
 * lower 64 bits of the operand.  We, however, do not.
 *
 * Exceptions type 4. AVX cpuid check for 128-bit operation, AVX2 for 256-bit.
 */
FNIEMOP_DEF_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_HighSrc, PCIEMOPMEDIAOPTF3, pImpl)
{
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, pImpl);
}


/**
 * Common worker for AVX2 instructions on the forms:
 *     - vpunpcklxx  xmm0, xmm1, xmm2/mem128
 *     - vpunpcklxx  ymm0, ymm1, ymm2/mem256
 *
 * The 128-bit memory version of this instruction may elect to skip fetching the
 * higher 64 bits of the operand.  We, however, do not.
 *
 * Exceptions type 4. AVX cpuid check for 128-bit operation, AVX2 for 256-bit.
 */
FNIEMOP_DEF_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_LowSrc, PCIEMOPMEDIAOPTF3, pImpl)
{
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, pImpl);
}


/**
 * Common worker for AVX2 instructions on the forms:
 *     - vpxxx    xmm0, xmm1/mem128
 *     - vpxxx    ymm0, ymm1/mem256
 *
 * Takes function table for function w/o implicit state parameter.
 *
 * Exceptions type 4. AVX cpuid check for 128-bit operation, AVX2 for 256-bit.
 */
FNIEMOP_DEF_1(iemOpCommonAvxAvx2_Vx_Wx_Opt, PCIEMOPMEDIAOPTF2, pImpl)
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
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx2);
            IEM_MC_LOCAL(RTUINT256U,            uDst);
            IEM_MC_LOCAL(RTUINT256U,            uSrc);
            IEM_MC_ARG_LOCAL_REF(PRTUINT256U,   puDst, uDst, 0);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc, uSrc, 1);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_FETCH_YREG_U256(uSrc,    IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_VOID_AIMPL_2(pImpl->pfnU256, puDst, puSrc);
            IEM_MC_STORE_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
            IEM_MC_ARG(PRTUINT128U,          puDst,  0);
            IEM_MC_ARG(PCRTUINT128U,         puSrc,  1);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_REF_XREG_U128(puDst,        IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_REF_XREG_U128_CONST(puSrc,  IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_VOID_AIMPL_2(pImpl->pfnU128, puDst, puSrc);
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
            IEM_MC_LOCAL(RTUINT256U,            uSrc);
            IEM_MC_LOCAL(RTGCPTR,               GCPtrEffSrc);
            IEM_MC_ARG_LOCAL_REF(PRTUINT256U,   puDst, uDst, 0);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc, uSrc, 1);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx2);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_FETCH_MEM_U256_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_CALL_VOID_AIMPL_2(pImpl->pfnU256, puDst, puSrc);
            IEM_MC_STORE_YREG_U256_ZX_VLMAX(   IEM_GET_MODRM_REG(pVCpu, bRm), uDst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTUINT128U,                uSrc);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
            IEM_MC_ARG(PRTUINT128U,                 puDst,       0);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,      puSrc, uSrc, 1);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_FETCH_MEM_U128_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_REF_XREG_U128(puDst,         IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_CALL_VOID_AIMPL_2(pImpl->pfnU128, puDst, puSrc);
            IEM_MC_CLEAR_YREG_128_UP(           IEM_GET_MODRM_REG(pVCpu, bRm));

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/**
 * Common worker for AVX/AVX2 instructions on the forms:
 *     - vpxxx    xmm0, xmm1/mem128
 *     - vpxxx    ymm0, ymm1/mem256
 *
 * Exceptions type 4. AVX cpuid check for 128-bit operation, AVX2 for 256-bit.
 */
FNIEMOP_DEF_1(iemOpCommonAvxAvx2_Vx_Wx, PCIEMOPMEDIAF2, pImpl)
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
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx2);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(X86YMMREG,             uSrc);
            IEM_MC_ARG_LOCAL_REF(PCX86YMMREG,   puSrc, uSrc,  1);
            IEM_MC_FETCH_YREG_YMM(uSrc,    IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_LOCAL(X86YMMREG,             uDst);
            IEM_MC_ARG_LOCAL_REF(PX86YMMREG,    puDst,  uDst, 0);
            IEM_MC_CALL_AVX_AIMPL_2(pImpl->pfnU256, puDst, puSrc);
            IEM_MC_STORE_YREG_YMM_ZX_VLMAX( IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
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
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst,  uDst,  0);
            IEM_MC_ARG(PCX86XMMREG,             puSrc,         1);
            IEM_MC_REF_XREG_XMM_CONST(puSrc,   IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_AVX_AIMPL_2(pImpl->pfnU128, puDst, puSrc);
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
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx2);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(X86YMMREG,             uSrc);
            IEM_MC_ARG_LOCAL_REF(PCX86YMMREG,   puSrc, uSrc,  1);
            IEM_MC_FETCH_MEM_YMM_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_LOCAL(X86YMMREG,             uDst);
            IEM_MC_ARG_LOCAL_REF(PX86YMMREG,    puDst,  uDst, 0);
            IEM_MC_CALL_AVX_AIMPL_2(pImpl->pfnU256, puDst, puSrc);
            IEM_MC_STORE_YREG_YMM_ZX_VLMAX(    IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(X86XMMREG,             uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst,  uDst, 0);
            IEM_MC_LOCAL(X86XMMREG,             uSrc);
            IEM_MC_ARG_LOCAL_REF(PCX86XMMREG,   puSrc, uSrc, 1);
            IEM_MC_FETCH_MEM_XMM_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_CALL_AVX_AIMPL_2(pImpl->pfnU128, puDst, puSrc);
            IEM_MC_STORE_XREG_XMM(             IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP(          IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}



/*  Opcode VEX.0F 0x00 - invalid */
/*  Opcode VEX.0F 0x01 - invalid */
/*  Opcode VEX.0F 0x02 - invalid */
/*  Opcode VEX.0F 0x03 - invalid */
/*  Opcode VEX.0F 0x04 - invalid */
/*  Opcode VEX.0F 0x05 - invalid */
/*  Opcode VEX.0F 0x06 - invalid */
/*  Opcode VEX.0F 0x07 - invalid */
/*  Opcode VEX.0F 0x08 - invalid */
/*  Opcode VEX.0F 0x09 - invalid */
/*  Opcode VEX.0F 0x0a - invalid */

/** Opcode VEX.0F 0x0b. */
FNIEMOP_DEF(iemOp_vud2)
{
    IEMOP_MNEMONIC(vud2, "vud2");
    IEMOP_RAISE_INVALID_OPCODE_RET();
}

/*  Opcode VEX.0F 0x0c - invalid */
/*  Opcode VEX.0F 0x0d - invalid */
/*  Opcode VEX.0F 0x0e - invalid */
/*  Opcode VEX.0F 0x0f - invalid */


/**
 * @opcode      0x10
 * @oppfx       none
 * @opcpuid     avx
 * @opgroup     og_avx_simdfp_datamove
 * @opxcpttype  4UA
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-22 -> op1=-22
 */
FNIEMOP_DEF(iemOp_vmovups_Vps_Wps)
{
    IEMOP_MNEMONIC2(VEX_RM, VMOVUPS, vmovups, Vps_WO, Wps, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES);
    Assert(pVCpu->iem.s.uVexLength <= 1);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();
        if (pVCpu->iem.s.uVexLength == 0)
            IEM_MC_COPY_YREG_U128_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm),
                                           IEM_GET_MODRM_RM(pVCpu, bRm));
        else
            IEM_MC_COPY_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm),
                                           IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else if (pVCpu->iem.s.uVexLength == 0)
    {
        /*
         * 128-bit: Register, Memory
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U128_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_YREG_U128_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * 256-bit: Register, Memory
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT256U,                uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U256_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x10
 * @oppfx       0x66
 * @opcpuid     avx
 * @opgroup     og_avx_simdfp_datamove
 * @opxcpttype  4UA
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-22 -> op1=-22
 */
FNIEMOP_DEF(iemOp_vmovupd_Vpd_Wpd)
{
    IEMOP_MNEMONIC2(VEX_RM, VMOVUPD, vmovupd, Vpd_WO, Wpd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES);
    Assert(pVCpu->iem.s.uVexLength <= 1);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();
        if (pVCpu->iem.s.uVexLength == 0)
            IEM_MC_COPY_YREG_U128_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm),
                                           IEM_GET_MODRM_RM(pVCpu, bRm));
        else
            IEM_MC_COPY_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm),
                                           IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else if (pVCpu->iem.s.uVexLength == 0)
    {
        /*
         * 128-bit: Memory, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U128_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_YREG_U128_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * 256-bit: Memory, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT256U,                uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U256_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


FNIEMOP_DEF(iemOp_vmovss_Vss_Hss_Wss)
{
    Assert(pVCpu->iem.s.uVexLength <= 1);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /**
         * @opcode      0x10
         * @oppfx       0xf3
         * @opcodesub   11 mr/reg
         * @opcpuid     avx
         * @opgroup     og_avx_simdfp_datamerge
         * @opxcpttype  5
         * @optest      op1=1 op2=0  op3=2    -> op1=2
         * @optest      op1=0 op2=0  op3=-22  -> op1=0xffffffea
         * @optest      op1=3 op2=-1 op3=0x77 -> op1=-4294967177
         * @optest      op1=3 op2=-2 op3=0x77 -> op1=-8589934473
         * @note        HssHi refers to bits 127:32.
         */
        IEMOP_MNEMONIC3(VEX_RVM_REG, VMOVSS, vmovss, Vss_WO, HssHi, Uss, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_IGNORED);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();
        IEM_MC_MERGE_YREG_U32_U96_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm),
                                           IEM_GET_MODRM_RM(pVCpu, bRm) /*U32*/,
                                           IEM_GET_EFFECTIVE_VVVV(pVCpu) /*Hss*/);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /**
         * @opdone
         * @opcode      0x10
         * @oppfx       0xf3
         * @opcodesub   !11 mr/reg
         * @opcpuid     avx
         * @opgroup     og_avx_simdfp_datamove
         * @opxcpttype  5
         * @opfunction  iemOp_vmovss_Vss_Hss_Wss
         * @optest      op1=1 op2=2 -> op1=2
         * @optest      op1=0 op2=-22 -> op1=-22
         */
        IEMOP_MNEMONIC2(VEX_RM_MEM, VMOVSS, vmovss, VssZx_WO, Md, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_IGNORED);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(uint32_t,                  uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U32(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_YREG_U32_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


FNIEMOP_DEF(iemOp_vmovsd_Vsd_Hsd_Wsd)
{
    Assert(pVCpu->iem.s.uVexLength <= 1);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /**
         * @opcode      0x10
         * @oppfx       0xf2
         * @opcodesub   11 mr/reg
         * @opcpuid     avx
         * @opgroup     og_avx_simdfp_datamerge
         * @opxcpttype  5
         * @optest      op1=1 op2=0  op3=2    -> op1=2
         * @optest      op1=0 op2=0  op3=-22  -> op1=0xffffffffffffffea
         * @optest      op1=3 op2=-1 op3=0x77 ->
         *              op1=0xffffffffffffffff0000000000000077
         * @optest      op1=3 op2=0x42 op3=0x77 -> op1=0x420000000000000077
         */
        IEMOP_MNEMONIC3(VEX_RVM_REG, VMOVSD, vmovsd, Vsd_WO, HsdHi, Usd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_IGNORED);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);

        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();
        IEM_MC_MERGE_YREG_U64_U64_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm),
                                           IEM_GET_MODRM_RM(pVCpu, bRm) /*U32*/,
                                           IEM_GET_EFFECTIVE_VVVV(pVCpu) /*Hss*/);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /**
         * @opdone
         * @opcode      0x10
         * @oppfx       0xf2
         * @opcodesub   !11 mr/reg
         * @opcpuid     avx
         * @opgroup     og_avx_simdfp_datamove
         * @opxcpttype  5
         * @opfunction  iemOp_vmovsd_Vsd_Hsd_Wsd
         * @optest      op1=1 op2=2 -> op1=2
         * @optest      op1=0 op2=-22 -> op1=-22
         */
        IEMOP_MNEMONIC2(VEX_RM_MEM, VMOVSD, vmovsd, VsdZx_WO, Mq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_IGNORED);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U64(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_YREG_U64_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x11
 * @oppfx       none
 * @opcpuid     avx
 * @opgroup     og_avx_simdfp_datamove
 * @opxcpttype  4UA
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-22 -> op1=-22
 */
FNIEMOP_DEF(iemOp_vmovups_Wps_Vps)
{
    IEMOP_MNEMONIC2(VEX_MR, VMOVUPS, vmovups, Wps_WO, Vps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES);
    Assert(pVCpu->iem.s.uVexLength <= 1);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();
        if (pVCpu->iem.s.uVexLength == 0)
            IEM_MC_COPY_YREG_U128_ZX_VLMAX(IEM_GET_MODRM_RM(pVCpu, bRm),
                                           IEM_GET_MODRM_REG(pVCpu, bRm));
        else
            IEM_MC_COPY_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_RM(pVCpu, bRm),
                                           IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else if (pVCpu->iem.s.uVexLength == 0)
    {
        /*
         * 128-bit: Memory, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ();

        IEM_MC_FETCH_YREG_U128(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iDQWord*/);
        IEM_MC_STORE_MEM_U128_NO_AC(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * 256-bit: Memory, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT256U,                uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ();

        IEM_MC_FETCH_YREG_U256(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_STORE_MEM_U256_NO_AC(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x11
 * @oppfx       0x66
 * @opcpuid     avx
 * @opgroup     og_avx_simdfp_datamove
 * @opxcpttype  4UA
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-22 -> op1=-22
 */
FNIEMOP_DEF(iemOp_vmovupd_Wpd_Vpd)
{
    IEMOP_MNEMONIC2(VEX_MR, VMOVUPD, vmovupd, Wpd_WO, Vpd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES);
    Assert(pVCpu->iem.s.uVexLength <= 1);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();
        if (pVCpu->iem.s.uVexLength == 0)
            IEM_MC_COPY_YREG_U128_ZX_VLMAX(IEM_GET_MODRM_RM(pVCpu, bRm),
                                           IEM_GET_MODRM_REG(pVCpu, bRm));
        else
            IEM_MC_COPY_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_RM(pVCpu, bRm),
                                           IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else if (pVCpu->iem.s.uVexLength == 0)
    {
        /*
         * 128-bit: Memory, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT128U,                uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ();

        IEM_MC_FETCH_YREG_U128(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iDQWord*/);
        IEM_MC_STORE_MEM_U128_NO_AC(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * 256-bit: Memory, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT256U,                uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ();

        IEM_MC_FETCH_YREG_U256(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_STORE_MEM_U256_NO_AC(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


FNIEMOP_DEF(iemOp_vmovss_Wss_Hss_Vss)
{
    Assert(pVCpu->iem.s.uVexLength <= 1);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /**
         * @opcode      0x11
         * @oppfx       0xf3
         * @opcodesub   11 mr/reg
         * @opcpuid     avx
         * @opgroup     og_avx_simdfp_datamerge
         * @opxcpttype  5
         * @optest      op1=1 op2=0  op3=2    -> op1=2
         * @optest      op1=0 op2=0  op3=-22  -> op1=0xffffffea
         * @optest      op1=3 op2=-1 op3=0x77 -> op1=-4294967177
         * @optest      op1=3 op2=0x42 op3=0x77 -> op1=0x4200000077
         */
        IEMOP_MNEMONIC3(VEX_MVR_REG, VMOVSS, vmovss, Uss_WO, HssHi, Vss, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_IGNORED);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);

        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();
        IEM_MC_MERGE_YREG_U32_U96_ZX_VLMAX(IEM_GET_MODRM_RM(pVCpu, bRm) /*U32*/,
                                           IEM_GET_MODRM_REG(pVCpu, bRm),
                                           IEM_GET_EFFECTIVE_VVVV(pVCpu) /*Hss*/);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /**
         * @opdone
         * @opcode      0x11
         * @oppfx       0xf3
         * @opcodesub   !11 mr/reg
         * @opcpuid     avx
         * @opgroup     og_avx_simdfp_datamove
         * @opxcpttype  5
         * @opfunction  iemOp_vmovss_Vss_Hss_Wss
         * @optest      op1=1 op2=2 -> op1=2
         * @optest      op1=0 op2=-22 -> op1=-22
         */
        IEMOP_MNEMONIC2(VEX_MR_MEM, VMOVSS, vmovss, Md_WO, Vss, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_IGNORED);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(uint32_t,                  uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ();

        IEM_MC_FETCH_YREG_U32(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_STORE_MEM_U32(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


FNIEMOP_DEF(iemOp_vmovsd_Wsd_Hsd_Vsd)
{
    Assert(pVCpu->iem.s.uVexLength <= 1);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /**
         * @opcode      0x11
         * @oppfx       0xf2
         * @opcodesub   11 mr/reg
         * @opcpuid     avx
         * @opgroup     og_avx_simdfp_datamerge
         * @opxcpttype  5
         * @optest      op1=1 op2=0  op3=2    -> op1=2
         * @optest      op1=0 op2=0  op3=-22  -> op1=0xffffffffffffffea
         * @optest      op1=3 op2=-1 op3=0x77 ->
         *              op1=0xffffffffffffffff0000000000000077
         * @optest      op2=0x42 op3=0x77 -> op1=0x420000000000000077
         */
        IEMOP_MNEMONIC3(VEX_MVR_REG, VMOVSD, vmovsd, Usd_WO, HsdHi, Vsd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_IGNORED);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);

        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();
        IEM_MC_MERGE_YREG_U64_U64_ZX_VLMAX(IEM_GET_MODRM_RM(pVCpu, bRm),
                                           IEM_GET_MODRM_REG(pVCpu, bRm),
                                           IEM_GET_EFFECTIVE_VVVV(pVCpu) /*Hss*/);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /**
         * @opdone
         * @opcode      0x11
         * @oppfx       0xf2
         * @opcodesub   !11 mr/reg
         * @opcpuid     avx
         * @opgroup     og_avx_simdfp_datamove
         * @opxcpttype  5
         * @opfunction  iemOp_vmovsd_Wsd_Hsd_Vsd
         * @optest      op1=1 op2=2 -> op1=2
         * @optest      op1=0 op2=-22 -> op1=-22
         */
        IEMOP_MNEMONIC2(VEX_MR_MEM, VMOVSD, vmovsd, Mq_WO, Vsd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_IGNORED);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ();

        IEM_MC_FETCH_YREG_U64(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iQWord*/);
        IEM_MC_STORE_MEM_U64(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


FNIEMOP_DEF(iemOp_vmovlps_Vq_Hq_Mq__vmovhlps)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /**
         * @opcode      0x12
         * @opcodesub   11 mr/reg
         * @oppfx       none
         * @opcpuid     avx
         * @opgroup     og_avx_simdfp_datamerge
         * @opxcpttype  7LZ
         * @optest         op2=0x2200220122022203
         *                 op3=0x3304330533063307
         *              -> op1=0x22002201220222033304330533063307
         * @optest      op2=-1  op3=-42 -> op1=-42
         * @note        op3 and op2 are only the 8-byte high XMM register halfs.
         */
        IEMOP_MNEMONIC3(VEX_RVM_REG, VMOVHLPS, vmovhlps, Vq_WO, HqHi, UqHi, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_ZERO);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_EX(fAvx);

        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();
        IEM_MC_MERGE_YREG_U64HI_U64HI_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm),
                                               IEM_GET_MODRM_RM(pVCpu, bRm),
                                               IEM_GET_EFFECTIVE_VVVV(pVCpu) /*Hq*/);

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
         * @opcpuid     avx
         * @opgroup     og_avx_simdfp_datamove
         * @opxcpttype  5LZ
         * @opfunction  iemOp_vmovlps_Vq_Hq_Mq__vmovhlps
         * @optest      op1=1 op2=0 op3=0 -> op1=0
         * @optest      op1=0 op2=-1 op3=-1 -> op1=-1
         * @optest      op1=1 op2=2 op3=3 -> op1=0x20000000000000003
         * @optest      op2=-1 op3=0x42 -> op1=0xffffffffffffffff0000000000000042
         */
        IEMOP_MNEMONIC3(VEX_RVM_MEM, VMOVLPS, vmovlps, Vq_WO, HqHi, Mq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_ZERO);

        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U64(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_MERGE_YREG_U64LOCAL_U64HI_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm),
                                                  uSrc,
                                                  IEM_GET_EFFECTIVE_VVVV(pVCpu) /*Hq*/);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x12
 * @opcodesub   !11 mr/reg
 * @oppfx       0x66
 * @opcpuid     avx
 * @opgroup     og_avx_pcksclr_datamerge
 * @opxcpttype  5LZ
 * @optest      op2=0 op3=2 -> op1=2
 * @optest      op2=0x22 op3=0x33 -> op1=0x220000000000000033
 * @optest      op2=0xfffffff0fffffff1 op3=0xeeeeeee8eeeeeee9
 *              -> op1=0xfffffff0fffffff1eeeeeee8eeeeeee9
 */
FNIEMOP_DEF(iemOp_vmovlpd_Vq_Hq_Mq)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_MEM_MODE(bRm))
    {
        IEMOP_MNEMONIC3(VEX_RVM_MEM, VMOVLPD, vmovlpd, Vq_WO, HqHi, Mq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_ZERO);

        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U64(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_MERGE_YREG_U64LOCAL_U64HI_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm),
                                                  uSrc,
                                                  IEM_GET_EFFECTIVE_VVVV(pVCpu) /*Hq*/);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }

    /**
     * @opdone
     * @opmnemonic  udvex660f12m3
     * @opcode      0x12
     * @opcodesub   11 mr/reg
     * @oppfx       0x66
     * @opunused    immediate
     * @opcpuid     avx
     * @optest      ->
     */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}


/**
 * @opcode      0x12
 * @oppfx       0xf3
 * @opcpuid     avx
 * @opgroup     og_avx_pcksclr_datamove
 * @opxcpttype  4
 * @optest      vex.l==0 / op1=-1 op2=0xdddddddd00000002eeeeeeee00000001
 *              -> op1=0x00000002000000020000000100000001
 * @optest      vex.l==1 /
 *                 op2=0xbbbbbbbb00000004cccccccc00000003dddddddd00000002eeeeeeee00000001
 *              -> op1=0x0000000400000004000000030000000300000002000000020000000100000001
 */
FNIEMOP_DEF(iemOp_vmovsldup_Vx_Wx)
{
    IEMOP_MNEMONIC2(VEX_RM, VMOVSLDUP, vmovsldup, Vx_WO, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES);
    Assert(pVCpu->iem.s.uVexLength <= 1);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        if (pVCpu->iem.s.uVexLength == 0)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_LOCAL(RTUINT128U,                uSrc);

            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_FETCH_XREG_U128(uSrc, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 0, uSrc, 0);
            IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 1, uSrc, 0);
            IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 2, uSrc, 2);
            IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 3, uSrc, 2);
            IEM_MC_CLEAR_YREG_128_UP(IEM_GET_MODRM_REG(pVCpu, bRm));

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(RTUINT256U,                uSrc);
            IEM_MC_FETCH_YREG_U256(uSrc, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 0, uSrc, 0);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 1, uSrc, 0);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 2, uSrc, 2);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 3, uSrc, 2);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 4, uSrc, 4);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 5, uSrc, 4);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 6, uSrc, 6);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 7, uSrc, 6);
            IEM_MC_CLEAR_ZREG_256_UP(IEM_GET_MODRM_REG(pVCpu, bRm));

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        /*
         * Register, memory.
         */
        if (pVCpu->iem.s.uVexLength == 0)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTUINT128U,                uSrc);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_FETCH_MEM_U128_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 0, uSrc, 0);
            IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 1, uSrc, 0);
            IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 2, uSrc, 2);
            IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 3, uSrc, 2);
            IEM_MC_CLEAR_YREG_128_UP(IEM_GET_MODRM_REG(pVCpu, bRm));

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,               GCPtrEffSrc);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(RTUINT256U,            uSrc);
            IEM_MC_FETCH_MEM_U256_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 0, uSrc, 0);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 1, uSrc, 0);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 2, uSrc, 2);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 3, uSrc, 2);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 4, uSrc, 4);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 5, uSrc, 4);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 6, uSrc, 6);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 7, uSrc, 6);
            IEM_MC_CLEAR_ZREG_256_UP(IEM_GET_MODRM_REG(pVCpu, bRm));

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/**
 * @opcode      0x12
 * @oppfx       0xf2
 * @opcpuid     avx
 * @opgroup     og_avx_pcksclr_datamove
 * @opxcpttype  5
 * @optest      vex.l==0 / op2=0xddddddddeeeeeeee2222222211111111
 *              ->         op1=0x22222222111111112222222211111111
 * @optest      vex.l==1 / op2=0xbbbbbbbbcccccccc4444444433333333ddddddddeeeeeeee2222222211111111
 *              ->         op1=0x4444444433333333444444443333333322222222111111112222222211111111
 */
FNIEMOP_DEF(iemOp_vmovddup_Vx_Wx)
{
    IEMOP_MNEMONIC2(VEX_RM, VMOVDDUP, vmovddup, Vx_WO, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        if (pVCpu->iem.s.uVexLength == 0)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_LOCAL(uint64_t,                  uSrc);

            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_FETCH_XREG_U64(uSrc, IEM_GET_MODRM_RM(pVCpu, bRm), 0 /* a_iQword*/);
            IEM_MC_STORE_XREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), 0 /* a_iQword*/, uSrc);
            IEM_MC_STORE_XREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), 1 /* a_iQword*/, uSrc);
            IEM_MC_CLEAR_YREG_128_UP(IEM_GET_MODRM_REG(pVCpu, bRm));

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(uint64_t,                  uSrc1);
            IEM_MC_LOCAL(uint64_t,                  uSrc2);
            IEM_MC_FETCH_YREG_U64(uSrc1, IEM_GET_MODRM_RM(pVCpu, bRm), 0 /* a_iQword*/);
            IEM_MC_FETCH_YREG_U64(uSrc2, IEM_GET_MODRM_RM(pVCpu, bRm), 2 /* a_iQword*/);

            IEM_MC_STORE_YREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), 0 /* a_iQword*/, uSrc1);
            IEM_MC_STORE_YREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), 1 /* a_iQword*/, uSrc1);
            IEM_MC_STORE_YREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), 2 /* a_iQword*/, uSrc2);
            IEM_MC_STORE_YREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), 3 /* a_iQword*/, uSrc2);
            IEM_MC_CLEAR_ZREG_256_UP(IEM_GET_MODRM_REG(pVCpu, bRm));

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        /*
         * Register, memory.
         */
        if (pVCpu->iem.s.uVexLength == 0)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
            IEM_MC_LOCAL(uint64_t,                  uSrc);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_FETCH_MEM_U64(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_STORE_XREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), 0 /* a_iQword*/, uSrc);
            IEM_MC_STORE_XREG_U64(IEM_GET_MODRM_REG(pVCpu, bRm), 1 /* a_iQword*/, uSrc);
            IEM_MC_CLEAR_YREG_128_UP(IEM_GET_MODRM_REG(pVCpu, bRm));

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,               GCPtrEffSrc);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(RTUINT256U,            uSrc);
            IEM_MC_FETCH_MEM_U256(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

            IEM_MC_STORE_YREG_U64_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iQwDst*/, uSrc, 0 /*a_iQwSrc*/);
            IEM_MC_STORE_YREG_U64_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 1 /*a_iQwDst*/, uSrc, 0 /*a_iQwSrc*/);
            IEM_MC_STORE_YREG_U64_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 2 /*a_iQwDst*/, uSrc, 2 /*a_iQwSrc*/);
            IEM_MC_STORE_YREG_U64_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 3 /*a_iQwDst*/, uSrc, 2 /*a_iQwSrc*/);
            IEM_MC_CLEAR_ZREG_256_UP(IEM_GET_MODRM_REG(pVCpu, bRm));

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/**
 * @opcode      0x13
 * @opcodesub   !11 mr/reg
 * @oppfx       none
 * @opcpuid     avx
 * @opgroup     og_avx_simdfp_datamove
 * @opxcpttype  5
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_vmovlps_Mq_Vq)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_MEM_MODE(bRm))
    {
        IEMOP_MNEMONIC2(VEX_MR_MEM, VMOVLPS, vmovlps, Mq_WO, Vq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_ZERO);

        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ();

        IEM_MC_FETCH_YREG_U64(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iQWord*/);
        IEM_MC_STORE_MEM_U64(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }

    /**
     * @opdone
     * @opmnemonic  udvex0f13m3
     * @opcode      0x13
     * @opcodesub   11 mr/reg
     * @oppfx       none
     * @opunused    immediate
     * @opcpuid     avx
     * @optest      ->
     */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}


/**
 * @opcode      0x13
 * @opcodesub   !11 mr/reg
 * @oppfx       0x66
 * @opcpuid     avx
 * @opgroup     og_avx_pcksclr_datamove
 * @opxcpttype  5
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_vmovlpd_Mq_Vq)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_MEM_MODE(bRm))
    {
        IEMOP_MNEMONIC2(VEX_MR_MEM, VMOVLPD, vmovlpd, Mq_WO, Vq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_ZERO);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ();

        IEM_MC_FETCH_YREG_U64(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iQWord*/);
        IEM_MC_STORE_MEM_U64(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }

    /**
     * @opdone
     * @opmnemonic  udvex660f13m3
     * @opcode      0x13
     * @opcodesub   11 mr/reg
     * @oppfx       0x66
     * @opunused    immediate
     * @opcpuid     avx
     * @optest      ->
     */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}

/*  Opcode VEX.F3.0F 0x13 - invalid */
/*  Opcode VEX.F2.0F 0x13 - invalid */

/** Opcode VEX.0F 0x14 - vunpcklps Vx, Hx, Wx*/
FNIEMOP_DEF(iemOp_vunpcklps_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VUNPCKLPS, vunpcklps, Vx, Hx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(           vunpcklps);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_LowSrc, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/** Opcode VEX.66.0F 0x14 - vunpcklpd Vx,Hx,Wx   */
FNIEMOP_DEF(iemOp_vunpcklpd_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VUNPCKLPD, vunpcklpd, Vx, Hx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(           vunpcklpd);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_LowSrc, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0x14 - invalid */
/*  Opcode VEX.F2.0F 0x14 - invalid */


/** Opcode VEX.0F 0x15 - vunpckhps Vx, Hx, Wx   */
FNIEMOP_DEF(iemOp_vunpckhps_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VUNPCKHPS, vunpckhps, Vx, Hx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(           vunpckhps);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_LowSrc, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/** Opcode VEX.66.0F 0x15 - vunpckhpd Vx,Hx,Wx   */
FNIEMOP_DEF(iemOp_vunpckhpd_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VUNPCKHPD, vunpckhpd, Vx, Hx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(           vunpckhpd);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_LowSrc, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0x15 - invalid */
/*  Opcode VEX.F2.0F 0x15 - invalid */


FNIEMOP_DEF(iemOp_vmovhps_Vdq_Hq_Mq__vmovlhps_Vdq_Hq_Uq)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /**
         * @opcode      0x16
         * @opcodesub   11 mr/reg
         * @oppfx       none
         * @opcpuid     avx
         * @opgroup     og_avx_simdfp_datamerge
         * @opxcpttype  7LZ
         */
        IEMOP_MNEMONIC3(VEX_RVM_REG, VMOVLHPS, vmovlhps, Vq_WO, Hq, Uq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_ZERO);

        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_EX(fAvx);

        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();
        IEM_MC_MERGE_YREG_U64LO_U64LO_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm),
                                               IEM_GET_MODRM_RM(pVCpu, bRm),
                                               IEM_GET_EFFECTIVE_VVVV(pVCpu) /*Hq*/);

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
         * @opcpuid     avx
         * @opgroup     og_avx_simdfp_datamove
         * @opxcpttype  5LZ
         * @opfunction  iemOp_vmovhps_Vdq_Hq_Mq__vmovlhps_Vdq_Hq_Uq
         */
        IEMOP_MNEMONIC3(VEX_RVM_MEM, VMOVHPS, vmovhps, Vq_WO, Hq, Mq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_ZERO);

        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U64(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_MERGE_YREG_U64LO_U64LOCAL_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm),
                                                  IEM_GET_EFFECTIVE_VVVV(pVCpu) /*Hq*/,
                                                  uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x16
 * @opcodesub   !11 mr/reg
 * @oppfx       0x66
 * @opcpuid     avx
 * @opgroup     og_avx_pcksclr_datamerge
 * @opxcpttype  5LZ
 */
FNIEMOP_DEF(iemOp_vmovhpd_Vdq_Hq_Mq)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_MEM_MODE(bRm))
    {
        IEMOP_MNEMONIC3(VEX_RVM_MEM, VMOVHPD, vmovhpd, Vq_WO, Hq, Mq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_ZERO);

        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U64(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_MERGE_YREG_U64LO_U64LOCAL_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm),
                                                  IEM_GET_EFFECTIVE_VVVV(pVCpu) /*Hq*/,
                                                  uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }

    /**
     * @opdone
     * @opmnemonic  udvex660f16m3
     * @opcode      0x12
     * @opcodesub   11 mr/reg
     * @oppfx       0x66
     * @opunused    immediate
     * @opcpuid     avx
     * @optest      ->
     */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}


/** Opcode VEX.F3.0F 0x16 - vmovshdup Vx, Wx   */
/**
 * @opcode      0x16
 * @oppfx       0xf3
 * @opcpuid     avx
 * @opgroup     og_avx_pcksclr_datamove
 * @opxcpttype  4
 */
FNIEMOP_DEF(iemOp_vmovshdup_Vx_Wx)
{
    IEMOP_MNEMONIC2(VEX_RM, VMOVSHDUP, vmovshdup, Vx_WO, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES);
    Assert(pVCpu->iem.s.uVexLength <= 1);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        if (pVCpu->iem.s.uVexLength == 0)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_LOCAL(RTUINT128U,                  uSrc);

            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_FETCH_XREG_U128(uSrc, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 0, uSrc, 1);
            IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 1, uSrc, 1);
            IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 2, uSrc, 3);
            IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 3, uSrc, 3);
            IEM_MC_CLEAR_YREG_128_UP(IEM_GET_MODRM_REG(pVCpu, bRm));

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(RTUINT256U,                  uSrc);
            IEM_MC_FETCH_YREG_U256(uSrc, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 0, uSrc, 1);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 1, uSrc, 1);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 2, uSrc, 3);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 3, uSrc, 3);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 4, uSrc, 5);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 5, uSrc, 5);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 6, uSrc, 7);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 7, uSrc, 7);
            IEM_MC_CLEAR_ZREG_256_UP(IEM_GET_MODRM_REG(pVCpu, bRm));

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        /*
         * Register, memory.
         */
        if (pVCpu->iem.s.uVexLength == 0)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTUINT128U,                uSrc);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_FETCH_MEM_U128_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 0, uSrc, 1);
            IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 1, uSrc, 1);
            IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 2, uSrc, 3);
            IEM_MC_STORE_XREG_U32_U128(IEM_GET_MODRM_REG(pVCpu, bRm), 3, uSrc, 3);
            IEM_MC_CLEAR_YREG_128_UP(IEM_GET_MODRM_REG(pVCpu, bRm));

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,               GCPtrEffSrc);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(RTUINT256U,            uSrc);
            IEM_MC_FETCH_MEM_U256_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 0, uSrc, 1);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 1, uSrc, 1);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 2, uSrc, 3);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 3, uSrc, 3);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 4, uSrc, 5);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 5, uSrc, 5);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 6, uSrc, 7);
            IEM_MC_STORE_YREG_U32_U256(IEM_GET_MODRM_REG(pVCpu, bRm), 7, uSrc, 7);
            IEM_MC_CLEAR_ZREG_256_UP(IEM_GET_MODRM_REG(pVCpu, bRm));

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/*  Opcode VEX.F2.0F 0x16 - invalid */


/**
 * @opcode      0x17
 * @opcodesub   !11 mr/reg
 * @oppfx       none
 * @opcpuid     avx
 * @opgroup     og_avx_simdfp_datamove
 * @opxcpttype  5
 */
FNIEMOP_DEF(iemOp_vmovhps_Mq_Vq)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_MEM_MODE(bRm))
    {
        IEMOP_MNEMONIC2(VEX_MR_MEM, VMOVHPS, vmovhps, Mq_WO, VqHi, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_ZERO);

        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ();

        IEM_MC_FETCH_YREG_U64(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), 1 /*a_iQWord*/);
        IEM_MC_STORE_MEM_U64(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }

    /**
     * @opdone
     * @opmnemonic  udvex0f17m3
     * @opcode      0x17
     * @opcodesub   11 mr/reg
     * @oppfx       none
     * @opunused    immediate
     * @opcpuid     avx
     * @optest      ->
     */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}


/**
 * @opcode      0x17
 * @opcodesub   !11 mr/reg
 * @oppfx       0x66
 * @opcpuid     avx
 * @opgroup     og_avx_pcksclr_datamove
 * @opxcpttype  5
 */
FNIEMOP_DEF(iemOp_vmovhpd_Mq_Vq)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_MEM_MODE(bRm))
    {
        IEMOP_MNEMONIC2(VEX_MR_MEM, VMOVHPD, vmovhpd, Mq_WO, VqHi, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_ZERO);

        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(uint64_t,                  uSrc);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ();

        IEM_MC_FETCH_YREG_U64(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), 1 /*a_iQWord*/);
        IEM_MC_STORE_MEM_U64(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }

    /**
     * @opdone
     * @opmnemonic  udvex660f17m3
     * @opcode      0x17
     * @opcodesub   11 mr/reg
     * @oppfx       0x66
     * @opunused    immediate
     * @opcpuid     avx
     * @optest      ->
     */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}


/*  Opcode VEX.F3.0F 0x17 - invalid */
/*  Opcode VEX.F2.0F 0x17 - invalid */


/*  Opcode VEX.0F 0x18 - invalid */
/*  Opcode VEX.0F 0x19 - invalid */
/*  Opcode VEX.0F 0x1a - invalid */
/*  Opcode VEX.0F 0x1b - invalid */
/*  Opcode VEX.0F 0x1c - invalid */
/*  Opcode VEX.0F 0x1d - invalid */
/*  Opcode VEX.0F 0x1e - invalid */
/*  Opcode VEX.0F 0x1f - invalid */

/*  Opcode VEX.0F 0x20 - invalid */
/*  Opcode VEX.0F 0x21 - invalid */
/*  Opcode VEX.0F 0x22 - invalid */
/*  Opcode VEX.0F 0x23 - invalid */
/*  Opcode VEX.0F 0x24 - invalid */
/*  Opcode VEX.0F 0x25 - invalid */
/*  Opcode VEX.0F 0x26 - invalid */
/*  Opcode VEX.0F 0x27 - invalid */

/**
 * @opcode      0x28
 * @oppfx       none
 * @opcpuid     avx
 * @opgroup     og_avx_pcksclr_datamove
 * @opxcpttype  1
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 * @note        Almost identical to vmovapd.
 */
FNIEMOP_DEF(iemOp_vmovaps_Vps_Wps)
{
    IEMOP_MNEMONIC2(VEX_RM, VMOVAPS, vmovaps, Vps_WO, Wps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    Assert(pVCpu->iem.s.uVexLength <= 1);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);

        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();
        if (pVCpu->iem.s.uVexLength == 0)
            IEM_MC_COPY_YREG_U128_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm),
                                           IEM_GET_MODRM_RM(pVCpu, bRm));
        else
            IEM_MC_COPY_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm),
                                           IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        if (pVCpu->iem.s.uVexLength == 0)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
            IEM_MC_LOCAL(RTUINT128U,                uSrc);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

            IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_STORE_YREG_U128_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uSrc);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
            IEM_MC_LOCAL(RTUINT256U,                uSrc);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

            IEM_MC_FETCH_MEM_U256_ALIGN_AVX(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_STORE_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uSrc);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/**
 * @opcode      0x28
 * @oppfx       66
 * @opcpuid     avx
 * @opgroup     og_avx_pcksclr_datamove
 * @opxcpttype  1
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 * @note        Almost identical to vmovaps
 */
FNIEMOP_DEF(iemOp_vmovapd_Vpd_Wpd)
{
    IEMOP_MNEMONIC2(VEX_RM, VMOVAPD, vmovapd, Vpd_WO, Wpd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    Assert(pVCpu->iem.s.uVexLength <= 1);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);

        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();
        if (pVCpu->iem.s.uVexLength == 0)
            IEM_MC_COPY_YREG_U128_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm),
                                           IEM_GET_MODRM_RM(pVCpu, bRm));
        else
            IEM_MC_COPY_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm),
                                           IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        if (pVCpu->iem.s.uVexLength == 0)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
            IEM_MC_LOCAL(RTUINT128U,                uSrc);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

            IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_STORE_YREG_U128_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uSrc);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
            IEM_MC_LOCAL(RTUINT256U,                uSrc);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

            IEM_MC_FETCH_MEM_U256_ALIGN_AVX(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_STORE_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uSrc);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}

/**
 * @opmnemonic  udvexf30f28
 * @opcode      0x28
 * @oppfx       0xf3
 * @opunused    vex.modrm
 * @opcpuid     avx
 * @optest      ->
 * @opdone
 */

/**
 * @opmnemonic  udvexf20f28
 * @opcode      0x28
 * @oppfx       0xf2
 * @opunused    vex.modrm
 * @opcpuid     avx
 * @optest      ->
 * @opdone
 */

/**
 * @opcode      0x29
 * @oppfx       none
 * @opcpuid     avx
 * @opgroup     og_avx_pcksclr_datamove
 * @opxcpttype  1
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 * @note        Almost identical to vmovapd.
 */
FNIEMOP_DEF(iemOp_vmovaps_Wps_Vps)
{
    IEMOP_MNEMONIC2(VEX_MR, VMOVAPS, vmovaps, Wps_WO, Vps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    Assert(pVCpu->iem.s.uVexLength <= 1);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);

        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();
        if (pVCpu->iem.s.uVexLength == 0)
            IEM_MC_COPY_YREG_U128_ZX_VLMAX(IEM_GET_MODRM_RM(pVCpu, bRm),
                IEM_GET_MODRM_REG(pVCpu, bRm));
        else
            IEM_MC_COPY_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_RM(pVCpu, bRm),
                                           IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        if (pVCpu->iem.s.uVexLength == 0)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
            IEM_MC_LOCAL(RTUINT128U,                uSrc);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ();

            IEM_MC_FETCH_YREG_U128(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iDQWord*/);
            IEM_MC_STORE_MEM_U128_ALIGN_SSE(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
            IEM_MC_LOCAL(RTUINT256U,                uSrc);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ();

            IEM_MC_FETCH_YREG_U256(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_STORE_MEM_U256_ALIGN_AVX(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}

/**
 * @opcode      0x29
 * @oppfx       66
 * @opcpuid     avx
 * @opgroup     og_avx_pcksclr_datamove
 * @opxcpttype  1
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 * @note        Almost identical to vmovaps
 */
FNIEMOP_DEF(iemOp_vmovapd_Wpd_Vpd)
{
    IEMOP_MNEMONIC2(VEX_MR, VMOVAPD, vmovapd, Wpd_WO, Vpd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES);
    Assert(pVCpu->iem.s.uVexLength <= 1);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);

        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();
        if (pVCpu->iem.s.uVexLength == 0)
            IEM_MC_COPY_YREG_U128_ZX_VLMAX(IEM_GET_MODRM_RM(pVCpu, bRm),
                IEM_GET_MODRM_REG(pVCpu, bRm));
        else
            IEM_MC_COPY_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_RM(pVCpu, bRm),
                                           IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        if (pVCpu->iem.s.uVexLength == 0)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
            IEM_MC_LOCAL(RTUINT128U,                uSrc);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ();

            IEM_MC_FETCH_YREG_U128(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iDQWord*/);
            IEM_MC_STORE_MEM_U128_ALIGN_SSE(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
            IEM_MC_LOCAL(RTUINT256U,                uSrc);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ();

            IEM_MC_FETCH_YREG_U256(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_STORE_MEM_U256_ALIGN_AVX(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/**
 * @opmnemonic  udvexf30f29
 * @opcode      0x29
 * @oppfx       0xf3
 * @opunused    vex.modrm
 * @opcpuid     avx
 * @optest      ->
 * @opdone
 */

/**
 * @opmnemonic  udvexf20f29
 * @opcode      0x29
 * @oppfx       0xf2
 * @opunused    vex.modrm
 * @opcpuid     avx
 * @optest      ->
 * @opdone
 */


/** Opcode VEX.0F 0x2a - invalid */
/** Opcode VEX.66.0F 0x2a - invalid */


/** Opcode VEX.F3.0F 0x2a - vcvtsi2ss Vss, Hss, Ey */
FNIEMOP_DEF(iemOp_vcvtsi2ss_Vss_Hss_Ey)
{
    IEMOP_MNEMONIC3(VEX_RVM, VCVTSI2SS, vcvtsi2ss, Vps, Hps, Ey, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOP_HLP_IGNORE_VEX_W_PREFIX_IF_NOT_IN_64BIT();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_REX_W)
    {
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* XMM, greg64 */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(X86XMMREG,             uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst,  uDst,  0);
            IEM_MC_ARG(PCX86XMMREG,             puSrc1,        1);
            IEM_MC_REF_XREG_XMM_CONST(puSrc1,   IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_ARG(const int64_t *,         pi64Src2,      2);
            IEM_MC_REF_GREG_I64_CONST(pi64Src2, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vcvtsi2ss_u128_i64, iemAImpl_vcvtsi2ss_u128_i64_fallback),
                                    puDst, puSrc1, pi64Src2);
            IEM_MC_STORE_XREG_XMM(             IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP(          IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /* XMM, [mem64] */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_LOCAL(RTGCPTR,    GCPtrEffSrc);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(X86XMMREG,             uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst,  uDst,          0);
            IEM_MC_ARG(PCX86XMMREG,             puSrc1,                1);
            IEM_MC_REF_XREG_XMM_CONST(puSrc1,   IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_LOCAL(int64_t,    i64Src2);
            IEM_MC_ARG_LOCAL_REF(const int64_t *,   pi64Src2, i64Src2, 2);
            IEM_MC_FETCH_MEM_I64(i64Src2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vcvtsi2ss_u128_i64, iemAImpl_vcvtsi2ss_u128_i64_fallback),
                                    puDst, puSrc1, pi64Src2);
            IEM_MC_STORE_XREG_XMM(             IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP(          IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* XMM, greg32 */
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(X86XMMREG,             uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst,  uDst,  0);
            IEM_MC_ARG(PCX86XMMREG,             puSrc1,        1);
            IEM_MC_REF_XREG_XMM_CONST(puSrc1,   IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_ARG(const int32_t *,         pi32Src2,      2);
            IEM_MC_REF_GREG_I32_CONST(pi32Src2, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vcvtsi2ss_u128_i32, iemAImpl_vcvtsi2ss_u128_i32_fallback),
                                    puDst, puSrc1, pi32Src2);
            IEM_MC_STORE_XREG_XMM(             IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP(          IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /* XMM, [mem32] */
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,    GCPtrEffSrc);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(X86XMMREG,             uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst,  uDst,          0);
            IEM_MC_ARG(PCX86XMMREG,             puSrc1,                1);
            IEM_MC_REF_XREG_XMM_CONST(puSrc1,   IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_LOCAL(int32_t,    i32Src2);
            IEM_MC_ARG_LOCAL_REF(const int32_t *,   pi32Src2, i32Src2, 2);
            IEM_MC_FETCH_MEM_I32(i32Src2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vcvtsi2ss_u128_i32, iemAImpl_vcvtsi2ss_u128_i32_fallback),
                                    puDst, puSrc1, pi32Src2);
            IEM_MC_STORE_XREG_XMM(             IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP(          IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/** Opcode VEX.F2.0F 0x2a - vcvtsi2sd Vsd, Hsd, Ey */
FNIEMOP_DEF(iemOp_vcvtsi2sd_Vsd_Hsd_Ey)
{
    IEMOP_MNEMONIC3(VEX_RVM, VCVTSI2SD, vcvtsi2sd, Vpd, Hpd, Ey, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOP_HLP_IGNORE_VEX_W_PREFIX_IF_NOT_IN_64BIT();
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_REX_W)
    {
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* XMM, greg64 */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(X86XMMREG,             uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst,  uDst,  0);
            IEM_MC_ARG(PCX86XMMREG,             puSrc1,        1);
            IEM_MC_REF_XREG_XMM_CONST(puSrc1,   IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_ARG(const int64_t *,         pi64Src2,      2);
            IEM_MC_REF_GREG_I64_CONST(pi64Src2, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vcvtsi2sd_u128_i64, iemAImpl_vcvtsi2sd_u128_i64_fallback),
                                    puDst, puSrc1, pi64Src2);
            IEM_MC_STORE_XREG_XMM(             IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP(          IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /* XMM, [mem64] */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEM_MC_LOCAL(RTGCPTR,    GCPtrEffSrc);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(X86XMMREG,             uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst,  uDst,          0);
            IEM_MC_ARG(PCX86XMMREG,             puSrc1,                1);
            IEM_MC_REF_XREG_XMM_CONST(puSrc1,   IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_LOCAL(int64_t,    i64Src2);
            IEM_MC_ARG_LOCAL_REF(const int64_t *,   pi64Src2, i64Src2, 2);
            IEM_MC_FETCH_MEM_I64(i64Src2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vcvtsi2sd_u128_i64, iemAImpl_vcvtsi2sd_u128_i64_fallback),
                                    puDst, puSrc1, pi64Src2);
            IEM_MC_STORE_XREG_XMM(             IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP(          IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
    {
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* XMM, greg32 */
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(X86XMMREG,             uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst,  uDst,  0);
            IEM_MC_ARG(PCX86XMMREG,             puSrc1,        1);
            IEM_MC_REF_XREG_XMM_CONST(puSrc1,   IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_ARG(const int32_t *,         pi32Src2,      2);
            IEM_MC_REF_GREG_I32_CONST(pi32Src2, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vcvtsi2sd_u128_i32, iemAImpl_vcvtsi2sd_u128_i32_fallback),
                                    puDst, puSrc1, pi32Src2);
            IEM_MC_STORE_XREG_XMM(             IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP(          IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /* XMM, [mem32] */
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,    GCPtrEffSrc);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(X86XMMREG,             uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst,  uDst,          0);
            IEM_MC_ARG(PCX86XMMREG,             puSrc1,                1);
            IEM_MC_REF_XREG_XMM_CONST(puSrc1,   IEM_GET_EFFECTIVE_VVVV(pVCpu));
            IEM_MC_LOCAL(int32_t,    i32Src2);
            IEM_MC_ARG_LOCAL_REF(const int32_t *,   pi32Src2, i32Src2, 2);
            IEM_MC_FETCH_MEM_I32(i32Src2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vcvtsi2sd_u128_i32, iemAImpl_vcvtsi2sd_u128_i32_fallback),
                                    puDst, puSrc1, pi32Src2);
            IEM_MC_STORE_XREG_XMM(             IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP(          IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/**
 * @opcode      0x2b
 * @opcodesub   !11 mr/reg
 * @oppfx       none
 * @opcpuid     avx
 * @opgroup     og_avx_cachect
 * @opxcpttype  1
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 * @note        Identical implementation to vmovntpd
 */
FNIEMOP_DEF(iemOp_vmovntps_Mps_Vps)
{
    IEMOP_MNEMONIC2(VEX_MR_MEM, VMOVNTPS, vmovntps, Mps_WO, Vps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES);
    Assert(pVCpu->iem.s.uVexLength <= 1);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_MEM_MODE(bRm))
    {
        /*
         * memory, register.
         */
        if (pVCpu->iem.s.uVexLength == 0)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTUINT128U,                uSrc);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

            IEM_MC_FETCH_XREG_U128(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_STORE_MEM_U128_ALIGN_SSE(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTUINT256U,                uSrc);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

            IEM_MC_FETCH_YREG_U256(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_STORE_MEM_U256_ALIGN_AVX(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    /* The register, register encoding is invalid. */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}

/**
 * @opcode      0x2b
 * @opcodesub   !11 mr/reg
 * @oppfx       0x66
 * @opcpuid     avx
 * @opgroup     og_avx_cachect
 * @opxcpttype  1
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 * @note        Identical implementation to vmovntps
 */
FNIEMOP_DEF(iemOp_vmovntpd_Mpd_Vpd)
{
    IEMOP_MNEMONIC2(VEX_MR_MEM, VMOVNTPD, vmovntpd, Mpd_WO, Vpd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES);
    Assert(pVCpu->iem.s.uVexLength <= 1);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_MEM_MODE(bRm))
    {
        /*
         * memory, register.
         */
        if (pVCpu->iem.s.uVexLength == 0)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTUINT128U,                uSrc);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

            IEM_MC_FETCH_XREG_U128(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_STORE_MEM_U128_ALIGN_SSE(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTUINT256U,                uSrc);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

            IEM_MC_FETCH_YREG_U256(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_STORE_MEM_U256_ALIGN_AVX(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    /* The register, register encoding is invalid. */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}

/**
 * @opmnemonic  udvexf30f2b
 * @opcode      0x2b
 * @oppfx       0xf3
 * @opunused    vex.modrm
 * @opcpuid     avx
 * @optest      ->
 * @opdone
 */

/**
 * @opmnemonic  udvexf20f2b
 * @opcode      0x2b
 * @oppfx       0xf2
 * @opunused    vex.modrm
 * @opcpuid     avx
 * @optest      ->
 * @opdone
 */


/*  Opcode VEX.0F 0x2c - invalid */
/*  Opcode VEX.66.0F 0x2c - invalid */

#define IEMOP_VCVTXSS2SI_Gy_Wss_BODY(a_Instr) \
    IEMOP_HLP_IGNORE_VEX_W_PREFIX_IF_NOT_IN_64BIT(); \
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm); \
    if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_REX_W) \
    { \
        if (IEM_IS_MODRM_REG_MODE(bRm)) \
        { \
            /* greg64, XMM */ \
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx); \
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT(); \
            IEM_MC_PREPARE_AVX_USAGE(); \
            IEM_MC_LOCAL(        int64_t,      i64Dst); \
            IEM_MC_ARG_LOCAL_REF(int64_t *,    pi64Dst, i64Dst,     0); \
            IEM_MC_ARG(          PCRTFLOAT32U, pr32Src,             1); \
            IEM_MC_REF_XREG_R32_CONST(pr32Src, IEM_GET_MODRM_RM(pVCpu, bRm)); \
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx, \
                                                                RT_CONCAT3(iemAImpl_,a_Instr,_i64_r32), \
                                                                RT_CONCAT3(iemAImpl_,a_Instr,_i64_r32_fallback)), \
                                    pi64Dst, pr32Src); \
            IEM_MC_STORE_GREG_I64(IEM_GET_MODRM_REG(pVCpu, bRm), i64Dst); \
            IEM_MC_ADVANCE_RIP_AND_FINISH(); \
            IEM_MC_END(); \
        } \
        else \
        { \
            /* greg64, [mem64] */ \
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
            IEM_MC_LOCAL(RTGCPTR,    GCPtrEffSrc); \
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0); \
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx); \
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT(); \
            IEM_MC_PREPARE_AVX_USAGE(); \
            IEM_MC_LOCAL(RTFLOAT32U, r32Src); \
            IEM_MC_ARG_LOCAL_REF(PCRTFLOAT32U, pr32Src, r32Src,     1); \
            IEM_MC_FETCH_MEM_R32(r32Src, pVCpu->iem.s.iEffSeg, GCPtrEffSrc); \
            IEM_MC_LOCAL(        int64_t,      i64Dst); \
            IEM_MC_ARG_LOCAL_REF(int64_t *,    pi64Dst, i64Dst,     0); \
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx, \
                                                                RT_CONCAT3(iemAImpl_,a_Instr,_i64_r32), \
                                                                RT_CONCAT3(iemAImpl_,a_Instr,_i64_r32_fallback)), \
                                    pi64Dst, pr32Src); \
            IEM_MC_STORE_GREG_I64(IEM_GET_MODRM_REG(pVCpu, bRm), i64Dst); \
            IEM_MC_ADVANCE_RIP_AND_FINISH(); \
            IEM_MC_END(); \
        } \
    } \
    else \
    { \
        if (IEM_IS_MODRM_REG_MODE(bRm)) \
        { \
            /* greg, XMM */ \
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0); \
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx); \
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT(); \
            IEM_MC_PREPARE_AVX_USAGE(); \
            IEM_MC_LOCAL(        int32_t,      i32Dst); \
            IEM_MC_ARG_LOCAL_REF(int32_t *,    pi32Dst, i32Dst,     0); \
            IEM_MC_ARG(          PCRTFLOAT32U, pr32Src,             1); \
            IEM_MC_REF_XREG_R32_CONST(pr32Src, IEM_GET_MODRM_RM(pVCpu, bRm)); \
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx, \
                                                                RT_CONCAT3(iemAImpl_,a_Instr,_i32_r32), \
                                                                RT_CONCAT3(iemAImpl_,a_Instr,_i32_r32_fallback)), \
                                    pi32Dst, pr32Src); \
            IEM_MC_STORE_GREG_I32(IEM_GET_MODRM_REG(pVCpu, bRm), i32Dst); \
            IEM_MC_ADVANCE_RIP_AND_FINISH(); \
            IEM_MC_END(); \
        } \
        else \
        { \
            /* greg, [mem] */ \
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0); \
            IEM_MC_LOCAL(RTGCPTR,    GCPtrEffSrc); \
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0); \
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx); \
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT(); \
            IEM_MC_PREPARE_AVX_USAGE(); \
            IEM_MC_LOCAL(RTFLOAT32U, r32Src); \
            IEM_MC_ARG_LOCAL_REF(PCRTFLOAT32U, pr32Src, r32Src,     1); \
            IEM_MC_FETCH_MEM_R32(r32Src, pVCpu->iem.s.iEffSeg, GCPtrEffSrc); \
            IEM_MC_LOCAL(        int32_t,      i32Dst); \
            IEM_MC_ARG_LOCAL_REF(int32_t *,    pi32Dst, i32Dst,     0); \
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx, \
                                                                RT_CONCAT3(iemAImpl_,a_Instr,_i32_r32), \
                                                                RT_CONCAT3(iemAImpl_,a_Instr,_i32_r32_fallback)), \
                                    pi32Dst, pr32Src); \
            IEM_MC_STORE_GREG_I32(IEM_GET_MODRM_REG(pVCpu, bRm), i32Dst); \
            IEM_MC_ADVANCE_RIP_AND_FINISH(); \
            IEM_MC_END(); \
        } \
    } \
    (void)0


#define IEMOP_VCVTXSD2SI_Gy_Wss_BODY(a_Instr) \
    IEMOP_HLP_IGNORE_VEX_W_PREFIX_IF_NOT_IN_64BIT(); \
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm); \
    if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_REX_W) \
    { \
        if (IEM_IS_MODRM_REG_MODE(bRm)) \
        { \
            /* greg64, XMM */ \
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx); \
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT(); \
            IEM_MC_PREPARE_AVX_USAGE(); \
            IEM_MC_LOCAL(        int64_t,      i64Dst); \
            IEM_MC_ARG_LOCAL_REF(int64_t *,    pi64Dst, i64Dst,     0); \
            IEM_MC_ARG(          PCRTFLOAT64U, pr64Src,             1); \
            IEM_MC_REF_XREG_R64_CONST(pr64Src, IEM_GET_MODRM_RM(pVCpu, bRm)); \
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx, \
                                                                RT_CONCAT3(iemAImpl_,a_Instr,_i64_r64), \
                                                                RT_CONCAT3(iemAImpl_,a_Instr,_i64_r64_fallback)), \
                                    pi64Dst, pr64Src); \
            IEM_MC_STORE_GREG_I64(IEM_GET_MODRM_REG(pVCpu, bRm), i64Dst); \
            IEM_MC_ADVANCE_RIP_AND_FINISH(); \
            IEM_MC_END(); \
        } \
        else \
        { \
            /* greg64, [mem64] */ \
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0); \
            IEM_MC_LOCAL(RTGCPTR,    GCPtrEffSrc); \
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0); \
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx); \
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT(); \
            IEM_MC_PREPARE_AVX_USAGE(); \
            IEM_MC_LOCAL(RTFLOAT64U, r64Src); \
            IEM_MC_ARG_LOCAL_REF(PCRTFLOAT64U, pr64Src, r64Src,     1); \
            IEM_MC_FETCH_MEM_R64(r64Src, pVCpu->iem.s.iEffSeg, GCPtrEffSrc); \
            IEM_MC_LOCAL(        int64_t,      i64Dst); \
            IEM_MC_ARG_LOCAL_REF(int64_t *,    pi64Dst, i64Dst,     0); \
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx, \
                                                                RT_CONCAT3(iemAImpl_,a_Instr,_i64_r64), \
                                                                RT_CONCAT3(iemAImpl_,a_Instr,_i64_r64_fallback)), \
                                    pi64Dst, pr64Src); \
            IEM_MC_STORE_GREG_I64(IEM_GET_MODRM_REG(pVCpu, bRm), i64Dst); \
            IEM_MC_ADVANCE_RIP_AND_FINISH(); \
            IEM_MC_END(); \
        } \
    } \
    else \
    { \
        if (IEM_IS_MODRM_REG_MODE(bRm)) \
        { \
            /* greg, XMM */ \
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0); \
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx); \
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT(); \
            IEM_MC_PREPARE_AVX_USAGE(); \
            IEM_MC_LOCAL(        int32_t,      i32Dst); \
            IEM_MC_ARG_LOCAL_REF(int32_t *,    pi32Dst, i32Dst,     0); \
            IEM_MC_ARG(          PCRTFLOAT64U, pr64Src,             1); \
            IEM_MC_REF_XREG_R64_CONST(pr64Src, IEM_GET_MODRM_RM(pVCpu, bRm)); \
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx, \
                                                                RT_CONCAT3(iemAImpl_,a_Instr,_i32_r64), \
                                                                RT_CONCAT3(iemAImpl_,a_Instr,_i32_r64_fallback)), \
                                    pi32Dst, pr64Src); \
            IEM_MC_STORE_GREG_I32(IEM_GET_MODRM_REG(pVCpu, bRm), i32Dst); \
            IEM_MC_ADVANCE_RIP_AND_FINISH(); \
            IEM_MC_END(); \
        } \
        else \
        { \
            /* greg, [mem] */ \
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0); \
            IEM_MC_LOCAL(RTGCPTR,    GCPtrEffSrc); \
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0); \
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx); \
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT(); \
            IEM_MC_PREPARE_AVX_USAGE(); \
            IEM_MC_LOCAL(RTFLOAT64U, r64Src); \
            IEM_MC_ARG_LOCAL_REF(PCRTFLOAT64U, pr64Src, r64Src,     1); \
            IEM_MC_FETCH_MEM_R64(r64Src, pVCpu->iem.s.iEffSeg, GCPtrEffSrc); \
            IEM_MC_LOCAL(        int32_t,      i32Dst); \
            IEM_MC_ARG_LOCAL_REF(int32_t *,    pi32Dst, i32Dst,     0); \
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx, \
                                                                RT_CONCAT3(iemAImpl_,a_Instr,_i32_r64), \
                                                                RT_CONCAT3(iemAImpl_,a_Instr,_i32_r64_fallback)), \
                                    pi32Dst, pr64Src); \
            IEM_MC_STORE_GREG_I32(IEM_GET_MODRM_REG(pVCpu, bRm), i32Dst); \
            IEM_MC_ADVANCE_RIP_AND_FINISH(); \
            IEM_MC_END(); \
        } \
    } \
    (void)0


/** Opcode VEX.F3.0F 0x2c - vcvttss2si Gy, Wss */
FNIEMOP_DEF(iemOp_vcvttss2si_Gy_Wss)
{
    IEMOP_MNEMONIC2(VEX_RM, VCVTTSS2SI, vcvttss2si, Gy, Wss, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOP_VCVTXSS2SI_Gy_Wss_BODY(       vcvttss2si);
}


/** Opcode VEX.F2.0F 0x2c - vcvttsd2si Gy, Wsd */
FNIEMOP_DEF(iemOp_vcvttsd2si_Gy_Wsd)
{
    IEMOP_MNEMONIC2(VEX_RM, VCVTTSD2SI, vcvttsd2si, Gy, Wss, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOP_VCVTXSD2SI_Gy_Wss_BODY(       vcvttsd2si);
}


/*  Opcode VEX.0F 0x2d - invalid */
/*  Opcode VEX.66.0F 0x2d - invalid */


/** Opcode VEX.F3.0F 0x2d - vcvtss2si Gy, Wss */
FNIEMOP_DEF(iemOp_vcvtss2si_Gy_Wss)
{
    IEMOP_MNEMONIC2(VEX_RM, VCVTSS2SI, vcvtss2si, Gy, Wss, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOP_VCVTXSS2SI_Gy_Wss_BODY(      vcvtss2si);
}


/** Opcode VEX.F2.0F 0x2d - vcvtsd2si Gy, Wsd */
FNIEMOP_DEF(iemOp_vcvtsd2si_Gy_Wsd)
{
    IEMOP_MNEMONIC2(VEX_RM, VCVTSD2SI, vcvtsd2si, Gy, Wss, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOP_VCVTXSD2SI_Gy_Wss_BODY(      vcvtsd2si);
}



/**
 * @opcode      0x2e
 * @oppfx       none
 * @opflmodify  cf,pf,af,zf,sf,of
 * @opflclear   af,sf,of
 */
FNIEMOP_DEF(iemOp_vucomiss_Vss_Wss)
{
    IEMOP_MNEMONIC2(VEX_RM, VUCOMISS, vucomiss, Vss, Wss, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_ZERO);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
        IEM_MC_LOCAL(uint32_t, fEFlags);
        IEM_MC_ARG_LOCAL_REF(uint32_t *,        pEFlags, fEFlags,   0);
        IEM_MC_ARG(RTFLOAT32U,                  uSrc1,              1);
        IEM_MC_ARG(RTFLOAT32U,                  uSrc2,              2);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();
        IEM_MC_FETCH_EFLAGS(fEFlags);
        IEM_MC_FETCH_XREG_R32(uSrc1, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iDWord*/);
        IEM_MC_FETCH_XREG_R32(uSrc2, IEM_GET_MODRM_RM(pVCpu, bRm),  0 /*a_iDWord*/);
        IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vucomiss_u128, iemAImpl_vucomiss_u128_fallback),
                                pEFlags, uSrc1, uSrc2);
        IEM_MC_COMMIT_EFLAGS(fEFlags);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(uint32_t, fEFlags);
        IEM_MC_ARG_LOCAL_REF(uint32_t *,        pEFlags, fEFlags,   0);
        IEM_MC_ARG(RTFLOAT32U,                  uSrc1,              1);
        IEM_MC_ARG(RTFLOAT32U,                  uSrc2,              2);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_FETCH_MEM_R32(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_AVX_USAGE();
        IEM_MC_FETCH_EFLAGS(fEFlags);
        IEM_MC_FETCH_XREG_R32(uSrc1, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iDWord*/);
        IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vucomiss_u128, iemAImpl_vucomiss_u128_fallback),
                                pEFlags, uSrc1, uSrc2);
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
FNIEMOP_DEF(iemOp_vucomisd_Vsd_Wsd)
{
    IEMOP_MNEMONIC2(VEX_RM, VUCOMISD, vucomisd, Vsd, Wsd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_ZERO);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
        IEM_MC_LOCAL(uint32_t, fEFlags);
        IEM_MC_ARG_LOCAL_REF(uint32_t *,        pEFlags, fEFlags,   0);
        IEM_MC_ARG(RTFLOAT64U,                  uSrc1,              1);
        IEM_MC_ARG(RTFLOAT64U,                  uSrc2,              2);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();
        IEM_MC_FETCH_EFLAGS(fEFlags);
        IEM_MC_FETCH_XREG_R64(uSrc1, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iQWord*/);
        IEM_MC_FETCH_XREG_R64(uSrc2, IEM_GET_MODRM_RM(pVCpu, bRm),  0 /*a_iQWord*/);
        IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vucomisd_u128, iemAImpl_vucomisd_u128_fallback),
                                pEFlags, uSrc1, uSrc2);
        IEM_MC_COMMIT_EFLAGS(fEFlags);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(uint32_t, fEFlags);
        IEM_MC_ARG_LOCAL_REF(uint32_t *,        pEFlags, fEFlags,   0);
        IEM_MC_ARG(RTFLOAT64U,                  uSrc1,              1);
        IEM_MC_ARG(RTFLOAT64U,                  uSrc2,              2);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_FETCH_MEM_R64(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_AVX_USAGE();
        IEM_MC_FETCH_EFLAGS(fEFlags);
        IEM_MC_FETCH_XREG_R64(uSrc1, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iQWord*/);
        IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vucomisd_u128, iemAImpl_vucomisd_u128_fallback),
                                pEFlags, uSrc1, uSrc2);
        IEM_MC_COMMIT_EFLAGS(fEFlags);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/*  Opcode VEX.F3.0F 0x2e - invalid */
/*  Opcode VEX.F2.0F 0x2e - invalid */

/**
 * @opcode      0x2f
 * @oppfx       none
 * @opflmodify  cf,pf,af,zf,sf,of
 * @opflclear   af,sf,of
 */
FNIEMOP_DEF(iemOp_vcomiss_Vss_Wss)
{
    IEMOP_MNEMONIC2(VEX_RM, VCOMISS, vcomiss, Vss, Wss, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_ZERO);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
        IEM_MC_LOCAL(uint32_t, fEFlags);
        IEM_MC_ARG_LOCAL_REF(uint32_t *,        pEFlags, fEFlags,   0);
        IEM_MC_ARG(RTFLOAT32U,                  uSrc1,              1);
        IEM_MC_ARG(RTFLOAT32U,                  uSrc2,              2);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();
        IEM_MC_FETCH_EFLAGS(fEFlags);
        IEM_MC_FETCH_XREG_R32(uSrc1, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iDWord*/);
        IEM_MC_FETCH_XREG_R32(uSrc2, IEM_GET_MODRM_RM(pVCpu, bRm),  0 /*a_iDWord*/);
        IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vcomiss_u128, iemAImpl_vcomiss_u128_fallback),
                                pEFlags, uSrc1, uSrc2);
        IEM_MC_COMMIT_EFLAGS(fEFlags);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(uint32_t, fEFlags);
        IEM_MC_ARG_LOCAL_REF(uint32_t *,        pEFlags, fEFlags,   0);
        IEM_MC_ARG(RTFLOAT32U,                  uSrc1,              1);
        IEM_MC_ARG(RTFLOAT32U,                  uSrc2,              2);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_FETCH_MEM_R32(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_AVX_USAGE();
        IEM_MC_FETCH_EFLAGS(fEFlags);
        IEM_MC_FETCH_XREG_R32(uSrc1, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iDWord*/);
        IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vcomiss_u128, iemAImpl_vcomiss_u128_fallback),
                                pEFlags, uSrc1, uSrc2);
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
FNIEMOP_DEF(iemOp_vcomisd_Vsd_Wsd)
{
    IEMOP_MNEMONIC2(VEX_RM, VCOMISD, vcomisd, Vsd, Wsd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_SSE, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_ZERO);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
        IEM_MC_LOCAL(uint32_t, fEFlags);
        IEM_MC_ARG_LOCAL_REF(uint32_t *,        pEFlags, fEFlags,   0);
        IEM_MC_ARG(RTFLOAT64U,                  uSrc1,              1);
        IEM_MC_ARG(RTFLOAT64U,                  uSrc2,              2);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();
        IEM_MC_FETCH_EFLAGS(fEFlags);
        IEM_MC_FETCH_XREG_R64(uSrc1, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iQWord*/);
        IEM_MC_FETCH_XREG_R64(uSrc2, IEM_GET_MODRM_RM(pVCpu, bRm),  0 /*a_iQWord*/);
        IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vcomisd_u128, iemAImpl_vcomisd_u128_fallback),
                                pEFlags, uSrc1, uSrc2);
        IEM_MC_COMMIT_EFLAGS(fEFlags);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(uint32_t, fEFlags);
        IEM_MC_ARG_LOCAL_REF(uint32_t *,        pEFlags, fEFlags,   0);
        IEM_MC_ARG(RTFLOAT64U,                  uSrc1,              1);
        IEM_MC_ARG(RTFLOAT64U,                  uSrc2,              2);
        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_FETCH_MEM_R64(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);

        IEM_MC_PREPARE_AVX_USAGE();
        IEM_MC_FETCH_EFLAGS(fEFlags);
        IEM_MC_FETCH_XREG_R64(uSrc1, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iQWord*/);
        IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vcomisd_u128, iemAImpl_vcomisd_u128_fallback),
                                pEFlags, uSrc1, uSrc2);
        IEM_MC_COMMIT_EFLAGS(fEFlags);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/*  Opcode VEX.F3.0F 0x2f - invalid */
/*  Opcode VEX.F2.0F 0x2f - invalid */

/*  Opcode VEX.0F 0x30 - invalid */
/*  Opcode VEX.0F 0x31 - invalid */
/*  Opcode VEX.0F 0x32 - invalid */
/*  Opcode VEX.0F 0x33 - invalid */
/*  Opcode VEX.0F 0x34 - invalid */
/*  Opcode VEX.0F 0x35 - invalid */
/*  Opcode VEX.0F 0x36 - invalid */
/*  Opcode VEX.0F 0x37 - invalid */
/*  Opcode VEX.0F 0x38 - invalid */
/*  Opcode VEX.0F 0x39 - invalid */
/*  Opcode VEX.0F 0x3a - invalid */
/*  Opcode VEX.0F 0x3b - invalid */
/*  Opcode VEX.0F 0x3c - invalid */
/*  Opcode VEX.0F 0x3d - invalid */
/*  Opcode VEX.0F 0x3e - invalid */
/*  Opcode VEX.0F 0x3f - invalid */
/*  Opcode VEX.0F 0x40 - invalid */
/*  Opcode VEX.0F 0x41 - invalid */
/*  Opcode VEX.0F 0x42 - invalid */
/*  Opcode VEX.0F 0x43 - invalid */
/*  Opcode VEX.0F 0x44 - invalid */
/*  Opcode VEX.0F 0x45 - invalid */
/*  Opcode VEX.0F 0x46 - invalid */
/*  Opcode VEX.0F 0x47 - invalid */
/*  Opcode VEX.0F 0x48 - invalid */
/*  Opcode VEX.0F 0x49 - invalid */
/*  Opcode VEX.0F 0x4a - invalid */
/*  Opcode VEX.0F 0x4b - invalid */
/*  Opcode VEX.0F 0x4c - invalid */
/*  Opcode VEX.0F 0x4d - invalid */
/*  Opcode VEX.0F 0x4e - invalid */
/*  Opcode VEX.0F 0x4f - invalid */


/** Opcode VEX.0F 0x50 - vmovmskps Gy, Ups */
FNIEMOP_DEF(iemOp_vmovmskps_Gy_Ups)
{
    IEMOP_MNEMONIC2(VEX_RM_REG, VMOVMSKPS, vmovmskps, Gd, Ux, DISOPTYPE_HARMLESS, IEMOPHINT_VEX_L_ZERO);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        if (pVCpu->iem.s.uVexLength == 0)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
            IEM_MC_LOCAL(uint8_t,           u8Dst);
            IEM_MC_ARG_LOCAL_REF(uint8_t *, pu8Dst,  u8Dst, 0);
            IEM_MC_ARG(PCRTUINT128U,            puSrc,                 1);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_REF_XREG_U128_CONST(puSrc,  IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_VOID_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vmovmskps_u128, iemAImpl_vmovmskps_u128_fallback),
                                     pu8Dst, puSrc);
            IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), u8Dst);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx2);
            IEM_MC_LOCAL(uint8_t,               u8Dst);
            IEM_MC_LOCAL(RTUINT256U,            uSrc);
            IEM_MC_ARG_LOCAL_REF(uint8_t *,     pu8Dst,  u8Dst, 0);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc,   uSrc,  1);

            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_FETCH_YREG_U256(uSrc,    IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_VOID_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vmovmskps_u256, iemAImpl_vmovmskps_u256_fallback),
                                     pu8Dst, puSrc);
            IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), u8Dst);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    /* No memory operand. */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}


/** Opcode VEX.66.0F 0x50 - vmovmskpd Gy,Upd */
FNIEMOP_DEF(iemOp_vmovmskpd_Gy_Upd)
{
    IEMOP_MNEMONIC2(VEX_RM_REG, VMOVMSKPD, vmovmskpd, Gd, Ux, DISOPTYPE_HARMLESS, IEMOPHINT_VEX_L_ZERO);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        if (pVCpu->iem.s.uVexLength == 0)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
            IEM_MC_LOCAL(uint8_t,           u8Dst);
            IEM_MC_ARG_LOCAL_REF(uint8_t *, pu8Dst,  u8Dst, 0);
            IEM_MC_ARG(PCRTUINT128U,            puSrc,                 1);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_REF_XREG_U128_CONST(puSrc,  IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_VOID_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vmovmskpd_u128, iemAImpl_vmovmskpd_u128_fallback),
                                     pu8Dst, puSrc);
            IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), u8Dst);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx2);
            IEM_MC_LOCAL(uint8_t,               u8Dst);
            IEM_MC_LOCAL(RTUINT256U,            uSrc);
            IEM_MC_ARG_LOCAL_REF(uint8_t *,     pu8Dst,  u8Dst, 0);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc,   uSrc,  1);

            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_FETCH_YREG_U256(uSrc,    IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_VOID_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vmovmskpd_u256, iemAImpl_vmovmskpd_u256_fallback),
                                     pu8Dst, puSrc);
            IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), u8Dst);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    /* No memory operand. */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}


/*  Opcode VEX.F3.0F 0x50 - invalid */
/*  Opcode VEX.F2.0F 0x50 - invalid */

/** Opcode VEX.0F 0x51 - vsqrtps Vps, Wps */
FNIEMOP_DEF(iemOp_vsqrtps_Vps_Wps)
{
    IEMOP_MNEMONIC2(VEX_RM, VSQRTPS, vsqrtps, Vps, Wps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAF2_INIT_VARS(          vsqrtps);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Wx, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/** Opcode VEX.66.0F 0x51 - vsqrtpd Vpd, Wpd */
FNIEMOP_DEF(iemOp_vsqrtpd_Vpd_Wpd)
{
    IEMOP_MNEMONIC2(VEX_RM, VSQRTPD, vsqrtpd, Vpd, Wpd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAF2_INIT_VARS(          vsqrtpd);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Wx, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/** Opcode VEX.F3.0F 0x51 - vsqrtss Vss, Hss, Wss */
FNIEMOP_DEF(iemOp_vsqrtss_Vss_Hss_Wss)
{
    IEMOP_MNEMONIC3(VEX_RVM, VSQRTSS, vsqrtss, Vps, Hps, Wss, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvx_Vx_Hx_R32,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vsqrtss_u128_r32, iemAImpl_vsqrtss_u128_r32_fallback));
}


/** Opcode VEX.F2.0F 0x51 - vsqrtsd Vsd, Hsd, Wsd */
FNIEMOP_DEF(iemOp_vsqrtsd_Vsd_Hsd_Wsd)
{
    IEMOP_MNEMONIC3(VEX_RVM, VSQRTSD, vsqrtsd, Vps, Hps, Wsd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvx_Vx_Hx_R64,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vsqrtsd_u128_r64, iemAImpl_vsqrtsd_u128_r64_fallback));
}


/** Opcode VEX.0F 0x52 - vrsqrtps Vps, Wps */
FNIEMOP_DEF(iemOp_vrsqrtps_Vps_Wps)
{
    IEMOP_MNEMONIC2(VEX_RM, VRSQRTPS, vrsqrtps, Vps, Wps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAF2_INIT_VARS(           vrsqrtps);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Wx, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/*  Opcode VEX.66.0F 0x52 - invalid */


/** Opcode VEX.F3.0F 0x52 - vrsqrtss Vss, Hss, Wss */
FNIEMOP_DEF(iemOp_vrsqrtss_Vss_Hss_Wss)
{
    IEMOP_MNEMONIC3(VEX_RVM, VRSQRTSS, vrsqrtss, Vps, Hps, Wss, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvx_Vx_Hx_R32,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vrsqrtss_u128_r32, iemAImpl_vrsqrtss_u128_r32_fallback));
}


/*  Opcode VEX.F2.0F 0x52 - invalid */


/** Opcode VEX.0F 0x53 - vrcpps Vps, Wps */
FNIEMOP_DEF(iemOp_vrcpps_Vps_Wps)
{
    IEMOP_MNEMONIC2(VEX_RM, VRCPPS, vrcpps, Vps, Wps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAF2_INIT_VARS(         vrcpps);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Wx, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/*  Opcode VEX.66.0F 0x53 - invalid */


/** Opcode VEX.F3.0F 0x53 - vrcpss Vss, Hss, Wss */
FNIEMOP_DEF(iemOp_vrcpss_Vss_Hss_Wss)
{
    IEMOP_MNEMONIC3(VEX_RVM, VRCPSS, vrcpss, Vps, Hps, Wss, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvx_Vx_Hx_R32,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vrcpss_u128_r32, iemAImpl_vrcpss_u128_r32_fallback));
}


/*  Opcode VEX.F2.0F 0x53 - invalid */


/** Opcode VEX.0F 0x54 - vandps Vps, Hps, Wps */
FNIEMOP_DEF(iemOp_vandps_Vps_Hps_Wps)
{
    IEMOP_MNEMONIC3(VEX_RVM, VANDPS, vandps, Vps, Hps, Wps, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &g_iemAImpl_vpand, &g_iemAImpl_vpand_fallback));
}


/** Opcode VEX.66.0F 0x54 - vandpd Vpd, Hpd, Wpd */
FNIEMOP_DEF(iemOp_vandpd_Vpd_Hpd_Wpd)
{
    IEMOP_MNEMONIC3(VEX_RVM, VANDPD, vandpd, Vpd, Hpd, Wpd, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &g_iemAImpl_vpand, &g_iemAImpl_vpand_fallback));
}


/*  Opcode VEX.F3.0F 0x54 - invalid */
/*  Opcode VEX.F2.0F 0x54 - invalid */


/** Opcode VEX.0F 0x55 - vandnps Vps, Hps, Wps */
FNIEMOP_DEF(iemOp_vandnps_Vps_Hps_Wps)
{
    IEMOP_MNEMONIC3(VEX_RVM, VANDNPS, vandnps, Vps, Hps, Wps, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &g_iemAImpl_vpandn, &g_iemAImpl_vpandn_fallback));
}


/** Opcode VEX.66.0F 0x55 - vandnpd Vpd, Hpd, Wpd */
FNIEMOP_DEF(iemOp_vandnpd_Vpd_Hpd_Wpd)
{
    IEMOP_MNEMONIC3(VEX_RVM, VANDNPD, vandnpd, Vpd, Hpd, Wpd, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &g_iemAImpl_vpandn, &g_iemAImpl_vpandn_fallback));
}


/*  Opcode VEX.F3.0F 0x55 - invalid */
/*  Opcode VEX.F2.0F 0x55 - invalid */

/** Opcode VEX.0F 0x56 - vorps Vps, Hps, Wps */
FNIEMOP_DEF(iemOp_vorps_Vps_Hps_Wps)
{
    IEMOP_MNEMONIC3(VEX_RVM, VORPS, vorps, Vps, Hps, Wps, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &g_iemAImpl_vpor, &g_iemAImpl_vpor_fallback));
}


/** Opcode VEX.66.0F 0x56 - vorpd Vpd, Hpd, Wpd */
FNIEMOP_DEF(iemOp_vorpd_Vpd_Hpd_Wpd)
{
    IEMOP_MNEMONIC3(VEX_RVM, VORPD, vorpd, Vpd, Hpd, Wpd, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &g_iemAImpl_vpor, &g_iemAImpl_vpor_fallback));
}


/*  Opcode VEX.F3.0F 0x56 - invalid */
/*  Opcode VEX.F2.0F 0x56 - invalid */


/** Opcode VEX.0F 0x57 - vxorps Vps, Hps, Wps */
FNIEMOP_DEF(iemOp_vxorps_Vps_Hps_Wps)
{
    IEMOP_MNEMONIC3(VEX_RVM, VXORPS, vxorps, Vps, Hps, Wps, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &g_iemAImpl_vpxor, &g_iemAImpl_vpxor_fallback));
}


/** Opcode VEX.66.0F 0x57 - vxorpd Vpd, Hpd, Wpd */
FNIEMOP_DEF(iemOp_vxorpd_Vpd_Hpd_Wpd)
{
    IEMOP_MNEMONIC3(VEX_RVM, VXORPD, vxorpd, Vpd, Hpd, Wpd, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &g_iemAImpl_vpxor, &g_iemAImpl_vpxor_fallback));
}


/*  Opcode VEX.F3.0F 0x57 - invalid */
/*  Opcode VEX.F2.0F 0x57 - invalid */


/** Opcode VEX.0F 0x58 - vaddps Vps, Hps, Wps */
FNIEMOP_DEF(iemOp_vaddps_Vps_Hps_Wps)
{
    IEMOP_MNEMONIC3(VEX_RVM, VADDPS, vaddps, Vps, Hps, Wps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAF3_INIT_VARS(          vaddps);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/** Opcode VEX.66.0F 0x58 - vaddpd Vpd, Hpd, Wpd */
FNIEMOP_DEF(iemOp_vaddpd_Vpd_Hpd_Wpd)
{
    IEMOP_MNEMONIC3(VEX_RVM, VADDPD, vaddpd, Vpd, Hpd, Wpd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAF3_INIT_VARS(          vaddpd);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/** Opcode VEX.F3.0F 0x58 - vaddss Vss, Hss, Wss */
FNIEMOP_DEF(iemOp_vaddss_Vss_Hss_Wss)
{
    IEMOP_MNEMONIC3(VEX_RVM, VADDSS, vaddss, Vps, Hps, Wss, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvx_Vx_Hx_R32,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vaddss_u128_r32, iemAImpl_vaddss_u128_r32_fallback));
}


/** Opcode VEX.F2.0F 0x58 - vaddsd Vsd, Hsd, Wsd */
FNIEMOP_DEF(iemOp_vaddsd_Vsd_Hsd_Wsd)
{
    IEMOP_MNEMONIC3(VEX_RVM, VADDSD, vaddsd, Vpd, Hpd, Wsd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvx_Vx_Hx_R64,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vaddsd_u128_r64, iemAImpl_vaddsd_u128_r64_fallback));
}


/** Opcode VEX.0F 0x59 - vmulps Vps, Hps, Wps */
FNIEMOP_DEF(iemOp_vmulps_Vps_Hps_Wps)
{
    IEMOP_MNEMONIC3(VEX_RVM, VMULPS, vmulps, Vps, Hps, Wps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAF3_INIT_VARS(          vmulps);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/** Opcode VEX.66.0F 0x59 - vmulpd Vpd, Hpd, Wpd */
FNIEMOP_DEF(iemOp_vmulpd_Vpd_Hpd_Wpd)
{
    IEMOP_MNEMONIC3(VEX_RVM, VMULPD, vmulpd, Vpd, Hpd, Wpd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAF3_INIT_VARS(          vmulpd);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/** Opcode VEX.F3.0F 0x59 - vmulss Vss, Hss, Wss */
FNIEMOP_DEF(iemOp_vmulss_Vss_Hss_Wss)
{
    IEMOP_MNEMONIC3(VEX_RVM, VMULSS, vmulss, Vps, Hps, Wss, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvx_Vx_Hx_R32,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vmulss_u128_r32, iemAImpl_vmulss_u128_r32_fallback));
}


/** Opcode VEX.F2.0F 0x59 - vmulsd Vsd, Hsd, Wsd */
FNIEMOP_DEF(iemOp_vmulsd_Vsd_Hsd_Wsd)
{
    IEMOP_MNEMONIC3(VEX_RVM, VMULSD, vmulsd, Vpd, Hpd, Wsd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvx_Vx_Hx_R64,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vmulsd_u128_r64, iemAImpl_vmulsd_u128_r64_fallback));
}


/** Opcode VEX.0F 0x5a - vcvtps2pd Vpd, Wps */
FNIEMOP_DEF(iemOp_vcvtps2pd_Vpd_Wps)
{
    IEMOP_MNEMONIC2(VEX_RM, VCVTPS2PD, vcvtps2pd, Vpd, Wps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        if (pVCpu->iem.s.uVexLength)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_ARG(          PCX86XMMREG,   puSrc,       1);
            IEM_MC_REF_XREG_XMM_CONST(          puSrc, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_LOCAL(        X86YMMREG,            uDst);
            IEM_MC_ARG_LOCAL_REF(PX86YMMREG,    puDst, uDst, 0);
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                                iemAImpl_vcvtps2pd_u256_u128,
                                                                iemAImpl_vcvtps2pd_u256_u128_fallback),
                                    puDst, puSrc);
            IEM_MC_STORE_YREG_YMM_ZX_VLMAX( IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_ARG(  const uint64_t *,      pu64Src,     1);
            IEM_MC_REF_XREG_U64_CONST(          pu64Src, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_LOCAL(        X86XMMREG,     uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst, uDst, 0);
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                                iemAImpl_vcvtps2pd_u128_u64,
                                                                iemAImpl_vcvtps2pd_u128_u64_fallback),
                                    puDst, pu64Src);
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
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(X86XMMREG,             uSrc);
            IEM_MC_ARG_LOCAL_REF(PCX86XMMREG,   puSrc, uSrc, 1);
            IEM_MC_FETCH_MEM_XMM_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_LOCAL(X86YMMREG,             uDst);
            IEM_MC_ARG_LOCAL_REF(PX86YMMREG,    puDst,  uDst,  0);
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                                iemAImpl_vcvtps2pd_u256_u128,
                                                                iemAImpl_vcvtps2pd_u256_u128_fallback),
                                    puDst, puSrc);
            IEM_MC_STORE_YREG_YMM_ZX_VLMAX(    IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(        uint64_t,                  u64Src);
            IEM_MC_ARG_LOCAL_REF(const uint64_t *, pu64Src, u64Src, 1);
            IEM_MC_FETCH_MEM_U64(u64Src, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_LOCAL(        X86XMMREG,     uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst, uDst, 0);
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                                iemAImpl_vcvtps2pd_u128_u64,
                                                                iemAImpl_vcvtps2pd_u128_u64_fallback),
                                    puDst, pu64Src);
            IEM_MC_STORE_XREG_XMM(             IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP(          IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/** Opcode VEX.66.0F 0x5a - vcvtpd2ps Vps, Wpd */
FNIEMOP_DEF(iemOp_vcvtpd2ps_Vps_Wpd)
{
    IEMOP_MNEMONIC2(VEX_RM, VCVTPD2PS, vcvtpd2ps, Vps, Wpd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        if (pVCpu->iem.s.uVexLength)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(        X86YMMREG,            uSrc);
            IEM_MC_FETCH_YREG_YMM(uSrc,    IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_ARG_LOCAL_REF(PCX86YMMREG,   puSrc, uSrc, 1);
            IEM_MC_LOCAL(        X86XMMREG,            uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst, uDst, 0);
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                                iemAImpl_vcvtpd2ps_u128_u256,
                                                                iemAImpl_vcvtpd2ps_u128_u256_fallback),
                                    puDst, puSrc);
            IEM_MC_STORE_XREG_XMM(   IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP(IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_ARG(          PCX86XMMREG,   puSrc,       1);
            IEM_MC_REF_XREG_XMM_CONST(          puSrc,   IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_LOCAL(        X86XMMREG,     uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst, uDst, 0);
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                                iemAImpl_vcvtpd2ps_u128_u128,
                                                                iemAImpl_vcvtpd2ps_u128_u128_fallback),
                                    puDst, puSrc);
            IEM_MC_STORE_XREG_XMM(   IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP(IEM_GET_MODRM_REG(pVCpu, bRm));
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
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(        X86YMMREG,            uSrc);
            IEM_MC_ARG_LOCAL_REF(PCX86YMMREG,   puSrc, uSrc, 1);
            IEM_MC_FETCH_MEM_YMM_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_LOCAL(        X86XMMREG,            uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst, uDst, 0);
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                                iemAImpl_vcvtpd2ps_u128_u256,
                                                                iemAImpl_vcvtpd2ps_u128_u256_fallback),
                                    puDst, puSrc);
            IEM_MC_STORE_XREG_XMM(   IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP(IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(X86XMMREG,             uSrc);
            IEM_MC_ARG_LOCAL_REF(PCX86XMMREG,   puSrc, uSrc, 1);
            IEM_MC_FETCH_MEM_XMM_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_LOCAL(        X86XMMREG,     uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst, uDst, 0);
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                                iemAImpl_vcvtpd2ps_u128_u128,
                                                                iemAImpl_vcvtpd2ps_u128_u128_fallback),
                                    puDst, puSrc);
            IEM_MC_STORE_XREG_XMM(             IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP(          IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/** Opcode VEX.F3.0F 0x5a - vcvtss2sd Vsd, Hx, Wss */
FNIEMOP_DEF(iemOp_vcvtss2sd_Vsd_Hx_Wss)
{
    IEMOP_MNEMONIC3(VEX_RVM, VCVTSS2SD, vcvtss2sd, Vsd, Hx, Wss, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvx_Vx_Hx_R32,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vcvtss2sd_u128_r32, iemAImpl_vcvtss2sd_u128_r32_fallback));
}


/** Opcode VEX.F2.0F 0x5a - vcvtsd2ss Vss, Hx, Wsd */
FNIEMOP_DEF(iemOp_vcvtsd2ss_Vss_Hx_Wsd)
{
    IEMOP_MNEMONIC3(VEX_RVM, VCVTSD2SS, vcvtsd2ss, Vss, Hx, Wsd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvx_Vx_Hx_R64,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vcvtsd2ss_u128_r64, iemAImpl_vcvtsd2ss_u128_r64_fallback));
}


/** Opcode VEX.0F 0x5b - vcvtdq2ps Vps, Wdq */
FNIEMOP_DEF(iemOp_vcvtdq2ps_Vps_Wdq)
{
    IEMOP_MNEMONIC2(VEX_RM, VCVTDQ2PS, vcvtdq2ps, Vps, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAF2_INIT_VARS(            vcvtdq2ps);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Wx, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/** Opcode VEX.66.0F 0x5b - vcvtps2dq Vdq, Wps */
FNIEMOP_DEF(iemOp_vcvtps2dq_Vdq_Wps)
{
    IEMOP_MNEMONIC2(VEX_RM, VCVTPS2DQ, vcvtps2dq, Vdq, Wps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAF2_INIT_VARS(            vcvtps2dq);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Wx, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/** Opcode VEX.F3.0F 0x5b - vcvttps2dq Vdq, Wps */
FNIEMOP_DEF(iemOp_vcvttps2dq_Vdq_Wps)
{
    IEMOP_MNEMONIC2(VEX_RM, VCVTTPS2DQ, vcvttps2dq, Vdq, Wps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAF2_INIT_VARS(             vcvttps2dq);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Wx, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F2.0F 0x5b - invalid */


/** Opcode VEX.0F 0x5c - vsubps Vps, Hps, Wps */
FNIEMOP_DEF(iemOp_vsubps_Vps_Hps_Wps)
{
    IEMOP_MNEMONIC3(VEX_RVM, VSUBPS, vsubps, Vps, Hps, Wps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAF3_INIT_VARS(          vsubps);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/** Opcode VEX.66.0F 0x5c - vsubpd Vpd, Hpd, Wpd */
FNIEMOP_DEF(iemOp_vsubpd_Vpd_Hpd_Wpd)
{
    IEMOP_MNEMONIC3(VEX_RVM, VSUBPD, vsubpd, Vpd, Hpd, Wpd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAF3_INIT_VARS(          vsubpd);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/** Opcode VEX.F3.0F 0x5c - vsubss Vss, Hss, Wss */
FNIEMOP_DEF(iemOp_vsubss_Vss_Hss_Wss)
{
    IEMOP_MNEMONIC3(VEX_RVM, VSUBSS, vsubss, Vps, Hps, Wss, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvx_Vx_Hx_R32,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vsubss_u128_r32, iemAImpl_vsubss_u128_r32_fallback));
}


/** Opcode VEX.F2.0F 0x5c - vsubsd Vsd, Hsd, Wsd */
FNIEMOP_DEF(iemOp_vsubsd_Vsd_Hsd_Wsd)
{
    IEMOP_MNEMONIC3(VEX_RVM, VSUBSD, vsubsd, Vpd, Hpd, Wsd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvx_Vx_Hx_R64,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vsubsd_u128_r64, iemAImpl_vsubsd_u128_r64_fallback));
}


/** Opcode VEX.0F 0x5d - vminps Vps, Hps, Wps */
FNIEMOP_DEF(iemOp_vminps_Vps_Hps_Wps)
{
    IEMOP_MNEMONIC3(VEX_RVM, VMINPS, vminps, Vps, Hps, Wps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAF3_INIT_VARS(          vminps);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/** Opcode VEX.66.0F 0x5d - vminpd Vpd, Hpd, Wpd */
FNIEMOP_DEF(iemOp_vminpd_Vpd_Hpd_Wpd)
{
    IEMOP_MNEMONIC3(VEX_RVM, VMINPD, vminpd, Vpd, Hpd, Wpd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAF3_INIT_VARS(          vminpd);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/** Opcode VEX.F3.0F 0x5d - vminss Vss, Hss, Wss */
FNIEMOP_DEF(iemOp_vminss_Vss_Hss_Wss)
{
    IEMOP_MNEMONIC3(VEX_RVM, VMINSS, vminss, Vps, Hps, Wss, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvx_Vx_Hx_R32,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vminss_u128_r32, iemAImpl_vminss_u128_r32_fallback));
}


/** Opcode VEX.F2.0F 0x5d - vminsd Vsd, Hsd, Wsd */
FNIEMOP_DEF(iemOp_vminsd_Vsd_Hsd_Wsd)
{
    IEMOP_MNEMONIC3(VEX_RVM, VMINSD, vminsd, Vpd, Hpd, Wsd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvx_Vx_Hx_R64,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vminsd_u128_r64, iemAImpl_vminsd_u128_r64_fallback));
}


/** Opcode VEX.0F 0x5e - vdivps Vps, Hps, Wps */
FNIEMOP_DEF(iemOp_vdivps_Vps_Hps_Wps)
{
    IEMOP_MNEMONIC3(VEX_RVM, VDIVPS, vdivps, Vps, Hps, Wps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAF3_INIT_VARS(          vdivps);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/** Opcode VEX.66.0F 0x5e - vdivpd Vpd, Hpd, Wpd */
FNIEMOP_DEF(iemOp_vdivpd_Vpd_Hpd_Wpd)
{
    IEMOP_MNEMONIC3(VEX_RVM, VDIVPD, vdivpd, Vpd, Hpd, Wpd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAF3_INIT_VARS(          vdivpd);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/** Opcode VEX.F3.0F 0x5e - vdivss Vss, Hss, Wss */
FNIEMOP_DEF(iemOp_vdivss_Vss_Hss_Wss)
{
    IEMOP_MNEMONIC3(VEX_RVM, VDIVSS, vdivss, Vps, Hps, Wss, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvx_Vx_Hx_R32,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vdivss_u128_r32, iemAImpl_vdivss_u128_r32_fallback));
}


/** Opcode VEX.F2.0F 0x5e - vdivsd Vsd, Hsd, Wsd */
FNIEMOP_DEF(iemOp_vdivsd_Vsd_Hsd_Wsd)
{
    IEMOP_MNEMONIC3(VEX_RVM, VDIVSD, vdivsd, Vpd, Hpd, Wsd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvx_Vx_Hx_R64,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vdivsd_u128_r64, iemAImpl_vdivsd_u128_r64_fallback));
}


/** Opcode VEX.0F 0x5f - vmaxps Vps, Hps, Wps */
FNIEMOP_DEF(iemOp_vmaxps_Vps_Hps_Wps)
{
    IEMOP_MNEMONIC3(VEX_RVM, VMAXPS, vmaxps, Vps, Hps, Wps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAF3_INIT_VARS(          vmaxps);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/** Opcode VEX.66.0F 0x5f - vmaxpd Vpd, Hpd, Wpd */
FNIEMOP_DEF(iemOp_vmaxpd_Vpd_Hpd_Wpd)
{
    IEMOP_MNEMONIC3(VEX_RVM, VMAXPD, vmaxpd, Vpd, Hpd, Wpd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAF3_INIT_VARS(          vmaxpd);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/** Opcode VEX.F3.0F 0x5f - vmaxss Vss, Hss, Wss */
FNIEMOP_DEF(iemOp_vmaxss_Vss_Hss_Wss)
{
    IEMOP_MNEMONIC3(VEX_RVM, VMAXSS, vmaxss, Vps, Hps, Wss, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvx_Vx_Hx_R32,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vmaxss_u128_r32, iemAImpl_vmaxss_u128_r32_fallback));
}


/** Opcode VEX.F2.0F 0x5f - vmaxsd Vsd, Hsd, Wsd */
FNIEMOP_DEF(iemOp_vmaxsd_Vsd_Hsd_Wsd)
{
    IEMOP_MNEMONIC3(VEX_RVM, VMAXSD, vmaxsd, Vpd, Hpd, Wsd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvx_Vx_Hx_R64,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vmaxsd_u128_r64, iemAImpl_vmaxsd_u128_r64_fallback));
}


/*  Opcode VEX.0F 0x60 - invalid */


/** Opcode VEX.66.0F 0x60 - vpunpcklbw Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpunpcklbw_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPUNPCKLBW, vpunpcklbw, Vx, Hx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(           vpunpcklbw);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_LowSrc, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0x60 - invalid */


/*  Opcode VEX.0F 0x61 - invalid */


/** Opcode VEX.66.0F 0x61 - vpunpcklwd Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpunpcklwd_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPUNPCKLWD, vpunpcklwd, Vx, Hx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(           vpunpcklwd);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_LowSrc, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0x61 - invalid */


/*  Opcode VEX.0F 0x62 - invalid */

/** Opcode VEX.66.0F 0x62 - vpunpckldq Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpunpckldq_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPUNPCKLDQ, vpunpckldq, Vx, Hx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(           vpunpckldq);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_LowSrc, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0x62 - invalid */



/*  Opcode VEX.0F 0x63 - invalid */


/** Opcode VEX.66.0F 0x63 - vpacksswb Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpacksswb_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPACKSSWB, vpacksswb, Vx, Hx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(          vpacksswb);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0x63 - invalid */

/*  Opcode VEX.0F 0x64 - invalid */


/** Opcode VEX.66.0F 0x64 - vpcmpgtb Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpcmpgtb_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPCMPGTB, vpcmpgtb, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(         vpcmpgtb);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0x64 - invalid */

/*  Opcode VEX.0F 0x65 - invalid */


/** Opcode VEX.66.0F 0x65 - vpcmpgtw Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpcmpgtw_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPCMPGTW, vpcmpgtw, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(         vpcmpgtw);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0x65 - invalid */

/*  Opcode VEX.0F 0x66 - invalid */


/** Opcode VEX.66.0F 0x66 - vpcmpgtd Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpcmpgtd_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPCMPGTD, vpcmpgtd, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(         vpcmpgtd);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0x66 - invalid */

/*  Opcode VEX.0F 0x67 - invalid */


/** Opcode VEX.66.0F 0x67 - vpackuswb Vx, Hx, W */
FNIEMOP_DEF(iemOp_vpackuswb_Vx_Hx_W)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPACKUSWB, vpackuswb, Vx, Hx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(          vpackuswb);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0x67 - invalid */


///**
// * Common worker for SSE2 instructions on the form:
// *      pxxxx xmm1, xmm2/mem128
// *
// * The 2nd operand is the second half of a register, which in the memory case
// * means a 64-bit memory access for MMX, and for SSE a 128-bit aligned access
// * where it may read the full 128 bits or only the upper 64 bits.
// *
// * Exceptions type 4.
// */
//FNIEMOP_DEF_1(iemOpCommonSse_HighHigh_To_Full, PCIEMOPMEDIAF1H1, pImpl)
//{
//    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
//    if (IEM_IS_MODRM_REG_MODE(bRm))
//    {
//        /*
//         * Register, register.
//         */
//        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
//        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
//        IEM_MC_ARG(PRTUINT128U,          pDst, 0);
//        IEM_MC_ARG(PCRTUINT128U,         pSrc, 1);
//        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
//        IEM_MC_PREPARE_SSE_USAGE();
//        IEM_MC_REF_XREG_U128(pDst, IEM_GET_MODRM_REG(pVCpu, bRm));
//        IEM_MC_REF_XREG_U128_CONST(pSrc, IEM_GET_MODRM_RM(pVCpu, bRm));
//        IEM_MC_CALL_SSE_AIMPL_2(pImpl->pfnU128, pDst, pSrc);
//        IEM_MC_ADVANCE_RIP_AND_FINISH();
//        IEM_MC_END();
//    }
//    else
//    {
//        /*
//         * Register, memory.
//         */
//        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
//        IEM_MC_ARG(PRTUINT128U,                 pDst,       0);
//        IEM_MC_LOCAL(RTUINT128U,                uSrc);
//        IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,      pSrc, uSrc, 1);
//        IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
//
//        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
//        IEMOP_HLP_DONE_DECODING_NO_LOCK_PREFIX_EX(fSse2);
//        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
//        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc); /* Most CPUs probably only right high qword */
//
//        IEM_MC_PREPARE_SSE_USAGE();
//        IEM_MC_REF_XREG_U128(pDst, IEM_GET_MODRM_REG(pVCpu, bRm));
//        IEM_MC_CALL_SSE_AIMPL_2(pImpl->pfnU128, pDst, pSrc);
//
//        IEM_MC_ADVANCE_RIP_AND_FINISH();
//        IEM_MC_END();
//    }
//    return VINF_SUCCESS;
//}


/*  Opcode VEX.0F 0x68 - invalid */

/** Opcode VEX.66.0F 0x68 - vpunpckhbw Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpunpckhbw_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPUNPCKHBW, vpunpckhbw, Vx, Hx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(           vpunpckhbw);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_HighSrc, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0x68 - invalid */


/*  Opcode VEX.0F 0x69 - invalid */


/** Opcode VEX.66.0F 0x69 - vpunpckhwd Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpunpckhwd_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPUNPCKHWD, vpunpckhwd, Vx, Hx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(           vpunpckhwd);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_HighSrc, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0x69 - invalid */


/*  Opcode VEX.0F 0x6a - invalid */


/** Opcode VEX.66.0F 0x6a - vpunpckhdq Vx, Hx, W */
FNIEMOP_DEF(iemOp_vpunpckhdq_Vx_Hx_W)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPUNPCKHDQ, vpunpckhdq, Vx, Hx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(           vpunpckhdq);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_HighSrc, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0x6a - invalid */


/*  Opcode VEX.0F 0x6b - invalid */


/** Opcode VEX.66.0F 0x6b - vpackssdw Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpackssdw_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPACKSSDW, vpackssdw, Vx, Hx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(          vpackssdw);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0x6b - invalid */


/*  Opcode VEX.0F 0x6c - invalid */


/** Opcode VEX.66.0F 0x6c - vpunpcklqdq Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpunpcklqdq_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPUNPCKLQDQ, vpunpcklqdq, Vx, Hx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(            vpunpcklqdq);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_LowSrc, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0x6c - invalid */
/*  Opcode VEX.F2.0F 0x6c - invalid */


/*  Opcode VEX.0F 0x6d - invalid */


/** Opcode VEX.66.0F 0x6d - vpunpckhqdq Vx, Hx, W */
FNIEMOP_DEF(iemOp_vpunpckhqdq_Vx_Hx_W)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPUNPCKHQDQ, vpunpckhqdq, Vx, Hx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(            vpunpckhqdq);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_HighSrc, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0x6d - invalid */


/*  Opcode VEX.0F 0x6e - invalid */

FNIEMOP_DEF(iemOp_vmovd_q_Vy_Ey)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    IEMOP_HLP_IGNORE_VEX_W_PREFIX_IF_NOT_IN_64BIT();
    if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_REX_W)
    {
        /**
         * @opcode      0x6e
         * @opcodesub   rex.w=1
         * @oppfx       0x66
         * @opcpuid     avx
         * @opgroup     og_avx_simdint_datamov
         * @opxcpttype  5
         * @optest      64-bit / op1=1 op2=2   -> op1=2
         * @optest      64-bit / op1=0 op2=-42 -> op1=-42
         */
        IEMOP_MNEMONIC2(VEX_RM, VMOVQ, vmovq, Vq_WO, Eq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OZ_PFX | IEMOPHINT_VEX_L_ZERO);
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* XMM, greg64 */
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
            IEM_MC_LOCAL(uint64_t, u64Tmp);

            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

            IEM_MC_FETCH_GREG_U64(u64Tmp, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_STORE_YREG_U64_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), u64Tmp);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /* XMM, [mem64] */
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR, GCPtrEffSrc);
            IEM_MC_LOCAL(uint64_t, u64Tmp);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

            IEM_MC_FETCH_MEM_U64(u64Tmp, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_STORE_YREG_U64_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), u64Tmp);

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
         * @opcpuid     avx
         * @opgroup     og_avx_simdint_datamov
         * @opxcpttype  5
         * @opfunction  iemOp_vmovd_q_Vy_Ey
         * @optest      op1=1 op2=2   -> op1=2
         * @optest      op1=0 op2=-42 -> op1=-42
         */
        IEMOP_MNEMONIC2(VEX_RM, VMOVD, vmovd, Vd_WO, Ed, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OZ_PFX | IEMOPHINT_VEX_L_ZERO);
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* XMM, greg32 */
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
            IEM_MC_LOCAL(uint32_t, u32Tmp);

            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

            IEM_MC_FETCH_GREG_U32(u32Tmp, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_STORE_YREG_U32_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), u32Tmp);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /* XMM, [mem32] */
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR, GCPtrEffSrc);
            IEM_MC_LOCAL(uint32_t, u32Tmp);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

            IEM_MC_FETCH_MEM_U32(u32Tmp, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_STORE_YREG_U32_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), u32Tmp);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/*  Opcode VEX.F3.0F 0x6e - invalid */


/*  Opcode VEX.0F 0x6f - invalid */

/**
 * @opcode      0x6f
 * @oppfx       0x66
 * @opcpuid     avx
 * @opgroup     og_avx_simdint_datamove
 * @opxcpttype  1
 * @optest      op1=1 op2=2   -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_vmovdqa_Vx_Wx)
{
    IEMOP_MNEMONIC2(VEX_RM, VMOVDQA, vmovdqa, Vx_WO, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES);
    Assert(pVCpu->iem.s.uVexLength <= 1);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);

        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();
        if (pVCpu->iem.s.uVexLength == 0)
            IEM_MC_COPY_YREG_U128_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm),
                                           IEM_GET_MODRM_RM(pVCpu, bRm));
        else
            IEM_MC_COPY_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm),
                                           IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else if (pVCpu->iem.s.uVexLength == 0)
    {
        /*
         * Register, memory128.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT128U, u128Tmp);
        IEM_MC_LOCAL(RTGCPTR,    GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U128_ALIGN_SSE(u128Tmp, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_YREG_U128_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), u128Tmp);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory256.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT256U, u256Tmp);
        IEM_MC_LOCAL(RTGCPTR,    GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U256_ALIGN_AVX(u256Tmp, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), u256Tmp);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}

/**
 * @opcode      0x6f
 * @oppfx       0xf3
 * @opcpuid     avx
 * @opgroup     og_avx_simdint_datamove
 * @opxcpttype  4UA
 * @optest      op1=1 op2=2   -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_vmovdqu_Vx_Wx)
{
    IEMOP_MNEMONIC2(VEX_RM, VMOVDQU, vmovdqu, Vx_WO, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES);
    Assert(pVCpu->iem.s.uVexLength <= 1);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);

        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();
        if (pVCpu->iem.s.uVexLength == 0)
            IEM_MC_COPY_YREG_U128_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm),
                                           IEM_GET_MODRM_RM(pVCpu, bRm));
        else
            IEM_MC_COPY_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm),
                                           IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else if (pVCpu->iem.s.uVexLength == 0)
    {
        /*
         * Register, memory128.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT128U, u128Tmp);
        IEM_MC_LOCAL(RTGCPTR,    GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U128_NO_AC(u128Tmp, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_YREG_U128_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), u128Tmp);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory256.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT256U, u256Tmp);
        IEM_MC_LOCAL(RTGCPTR,    GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U256_NO_AC(u256Tmp, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), u256Tmp);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/*  Opcode VEX.0F 0x70 - invalid */


/**
 * Common worker for AVX/AVX2 instructions on the forms:
 *     - vpxxx    xmm0, xmm2/mem128, imm8
 *     - vpxxx    ymm0, ymm2/mem256, imm8
 *
 * Exceptions type 4. AVX cpuid check for 128-bit operation, AVX2 for 256-bit.
 */
FNIEMOP_DEF_2(iemOpCommonAvxAvx2_vpshufXX_Vx_Wx_Ib, PFNIEMAIMPLMEDIAPSHUFU128, pfnU128, PFNIEMAIMPLMEDIAPSHUFU256, pfnU256)
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
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx2);
            IEM_MC_LOCAL(RTUINT256U,            uDst);
            IEM_MC_LOCAL(RTUINT256U,            uSrc);
            IEM_MC_ARG_LOCAL_REF(PRTUINT256U,   puDst,  uDst,  0);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc,  uSrc,  1);
            IEM_MC_ARG_CONST(uint8_t,           bImmArg, /*=*/ bImm, 2);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_FETCH_YREG_U256(uSrc,        IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_VOID_AIMPL_3(pfnU256, puDst, puSrc, bImmArg);
            IEM_MC_STORE_YREG_U256_ZX_VLMAX(    IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_ARG(PRTUINT128U,             puDst,                 0);
            IEM_MC_ARG(PCRTUINT128U,            puSrc,                 1);
            IEM_MC_ARG_CONST(uint8_t,           bImmArg, /*=*/ bImm, 2);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_REF_XREG_U128(puDst,         IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_REF_XREG_U128_CONST(puSrc,   IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_VOID_AIMPL_3(pfnU128, puDst, puSrc, bImmArg);
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
            IEM_MC_ARG_LOCAL_REF(PRTUINT256U,   puDst,          uDst,  0);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc,          uSrc,  1);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx2);
            IEM_MC_ARG_CONST(uint8_t,           bImmArg, /*=*/ bImm, 2);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_FETCH_MEM_U256_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_CALL_VOID_AIMPL_3(pfnU256, puDst, puSrc, bImmArg);
            IEM_MC_STORE_YREG_U256_ZX_VLMAX(    IEM_GET_MODRM_REG(pVCpu, bRm), uDst);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTUINT128U,            uSrc);
            IEM_MC_LOCAL(RTGCPTR,               GCPtrEffSrc);
            IEM_MC_ARG(PRTUINT128U,             puDst,                 0);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,  puSrc, uSrc,           1);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_ARG_CONST(uint8_t,           bImmArg, /*=*/ bImm, 2);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_FETCH_MEM_U128_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_REF_XREG_U128(puDst,         IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_CALL_VOID_AIMPL_3(pfnU128, puDst, puSrc, bImmArg);
            IEM_MC_CLEAR_YREG_128_UP(           IEM_GET_MODRM_REG(pVCpu, bRm));

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/** Opcode VEX.66.0F 0x70 - vpshufd Vx, Wx, Ib */
FNIEMOP_DEF(iemOp_vpshufd_Vx_Wx_Ib)
{
    IEMOP_MNEMONIC3(VEX_RMI, VPSHUFD, vpshufd, Vx, Wx, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    return FNIEMOP_CALL_2(iemOpCommonAvxAvx2_vpshufXX_Vx_Wx_Ib, iemAImpl_pshufd_u128,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpshufd_u256, iemAImpl_vpshufd_u256_fallback));

}


/** Opcode VEX.F3.0F 0x70 - vpshufhw Vx, Wx, Ib */
FNIEMOP_DEF(iemOp_vpshufhw_Vx_Wx_Ib)
{
    IEMOP_MNEMONIC3(VEX_RMI, VPSHUFHW, vpshufhw, Vx, Wx, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    return FNIEMOP_CALL_2(iemOpCommonAvxAvx2_vpshufXX_Vx_Wx_Ib, iemAImpl_pshufhw_u128,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpshufhw_u256, iemAImpl_vpshufhw_u256_fallback));

}


/** Opcode VEX.F2.0F 0x70 - vpshuflw Vx, Wx, Ib */
FNIEMOP_DEF(iemOp_vpshuflw_Vx_Wx_Ib)
{
    IEMOP_MNEMONIC3(VEX_RMI, VPSHUFLW, vpshuflw, Vx, Wx, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    return FNIEMOP_CALL_2(iemOpCommonAvxAvx2_vpshufXX_Vx_Wx_Ib, iemAImpl_pshuflw_u128,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpshuflw_u256, iemAImpl_vpshuflw_u256_fallback));
}


/**
 * Common worker(s) for AVX/AVX2 instructions on the forms:
 *     - vpxxx    xmm0, xmm2, imm8
 *     - vpxxx    ymm0, ymm2, imm8
 *
 * Exceptions type 4. AVX cpuid check for 128-bit operation, AVX2 for 256-bit.
 */
FNIEMOP_DEF_2(iemOpCommonAvxAvx2_Hx_Ux_Ib_u128, uint8_t, bRm, PFNIEMAIMPLMEDIAPSHUFU128, pfnU128)
{
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx);
        IEM_MC_ARG(PRTUINT128U,             puDst,                 0);
        IEM_MC_ARG(PCRTUINT128U,            puSrc,                 1);
        IEM_MC_ARG_CONST(uint8_t,           bImmArg, /*=*/ bImm, 2);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();
        IEM_MC_REF_XREG_U128(puDst,         IEM_GET_EFFECTIVE_VVVV(pVCpu));
        IEM_MC_REF_XREG_U128_CONST(puSrc,   IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_3(pfnU128, puDst, puSrc, bImmArg);
        IEM_MC_CLEAR_YREG_128_UP(           IEM_GET_EFFECTIVE_VVVV(pVCpu));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    /* No memory operand. */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}

FNIEMOP_DEF_2(iemOpCommonAvxAvx2_Hx_Ux_Ib_u256, uint8_t, bRm, PFNIEMAIMPLMEDIAPSHUFU256, pfnU256)
{
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx2);
        IEM_MC_LOCAL(RTUINT256U,            uDst);
        IEM_MC_LOCAL(RTUINT256U,            uSrc);
        IEM_MC_ARG_LOCAL_REF(PRTUINT256U,   puDst,  uDst,  0);
        IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc,  uSrc,  1);
        IEM_MC_ARG_CONST(uint8_t,           bImmArg, /*=*/ bImm, 2);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();
        IEM_MC_FETCH_YREG_U256(uSrc,        IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_CALL_VOID_AIMPL_3(pfnU256, puDst, puSrc, bImmArg);
        IEM_MC_STORE_YREG_U256_ZX_VLMAX(    IEM_GET_EFFECTIVE_VVVV(pVCpu), uDst);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    /* No memory operand. */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}


/*  Opcode VEX.0F 0x71 11/2 - invalid. */
/** Opcode VEX.66.0F 0x71 11/2. */
FNIEMOP_DEF_1(iemOp_VGrp12_vpsrlw_Hx_Ux_Ib, uint8_t, bRm)
{
    IEMOP_MNEMONIC3(VEX_VMI_REG, VPSRLW, vpsrlw, Hx, Ux, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    if (pVCpu->iem.s.uVexLength)
    {
        return FNIEMOP_CALL_2(iemOpCommonAvxAvx2_Hx_Ux_Ib_u256, bRm,
                              IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpsrlw_imm_u256, iemAImpl_vpsrlw_imm_u256_fallback));
    }
    else
    {
        return FNIEMOP_CALL_2(iemOpCommonAvxAvx2_Hx_Ux_Ib_u128, bRm,
                              IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpsrlw_imm_u128, iemAImpl_vpsrlw_imm_u128_fallback));
    }
}


/*  Opcode VEX.0F 0x71 11/4 - invalid */
/** Opcode VEX.66.0F 0x71 11/4. */
FNIEMOP_DEF_1(iemOp_VGrp12_vpsraw_Hx_Ux_Ib, uint8_t, bRm)
{
    IEMOP_MNEMONIC3(VEX_VMI_REG, VPSRAW, vpsraw, Hx, Ux, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    if (pVCpu->iem.s.uVexLength)
    {
        return FNIEMOP_CALL_2(iemOpCommonAvxAvx2_Hx_Ux_Ib_u256, bRm,
                              IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpsraw_imm_u256, iemAImpl_vpsraw_imm_u256_fallback));
    }
    else
    {
        return FNIEMOP_CALL_2(iemOpCommonAvxAvx2_Hx_Ux_Ib_u128, bRm,
                              IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpsraw_imm_u128, iemAImpl_vpsraw_imm_u128_fallback));
    }
}

/*  Opcode VEX.0F 0x71 11/6 - invalid */

/** Opcode VEX.66.0F 0x71 11/6. */
FNIEMOP_DEF_1(iemOp_VGrp12_vpsllw_Hx_Ux_Ib, uint8_t, bRm)
{
    IEMOP_MNEMONIC3(VEX_VMI_REG, VPSLLW, vpsllw, Hx, Ux, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    if (pVCpu->iem.s.uVexLength)
    {
        return FNIEMOP_CALL_2(iemOpCommonAvxAvx2_Hx_Ux_Ib_u256, bRm,
                              IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpsllw_imm_u256, iemAImpl_vpsllw_imm_u256_fallback));
    }
    else
    {
        return FNIEMOP_CALL_2(iemOpCommonAvxAvx2_Hx_Ux_Ib_u128, bRm,
                              IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpsllw_imm_u128, iemAImpl_vpsllw_imm_u128_fallback));
    }
}


/**
 * VEX Group 12 jump table for register variant.
 */
IEM_STATIC const PFNIEMOPRM g_apfnVexGroup12RegReg[] =
{
    /* /0 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8),
    /* /1 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8),
    /* /2 */ iemOp_InvalidWithRMNeedImm8,   iemOp_VGrp12_vpsrlw_Hx_Ux_Ib, iemOp_InvalidWithRMNeedImm8, iemOp_InvalidWithRMNeedImm8,
    /* /3 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8),
    /* /4 */ iemOp_InvalidWithRMNeedImm8,   iemOp_VGrp12_vpsraw_Hx_Ux_Ib, iemOp_InvalidWithRMNeedImm8, iemOp_InvalidWithRMNeedImm8,
    /* /5 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8),
    /* /6 */ iemOp_InvalidWithRMNeedImm8,   iemOp_VGrp12_vpsllw_Hx_Ux_Ib, iemOp_InvalidWithRMNeedImm8, iemOp_InvalidWithRMNeedImm8,
    /* /7 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8)
};
AssertCompile(RT_ELEMENTS(g_apfnVexGroup12RegReg) == 8*4);


/** Opcode VEX.0F 0x71. */
FNIEMOP_DEF(iemOp_VGrp12)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
        /* register, register */
        return FNIEMOP_CALL_1(g_apfnVexGroup12RegReg[  IEM_GET_MODRM_REG_8(bRm) * 4
                                                     + pVCpu->iem.s.idxPrefix], bRm);
    return FNIEMOP_CALL_1(iemOp_InvalidWithRMNeedImm8, bRm);
}


/*  Opcode VEX.0F 0x72 11/2 - invalid. */
/** Opcode VEX.66.0F 0x72 11/2. */
FNIEMOP_DEF_1(iemOp_VGrp13_vpsrld_Hx_Ux_Ib, uint8_t, bRm)
{
    IEMOP_MNEMONIC3(VEX_VMI_REG, VPSRLD, vpsrld, Hx, Ux, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    if (pVCpu->iem.s.uVexLength)
    {
        return FNIEMOP_CALL_2(iemOpCommonAvxAvx2_Hx_Ux_Ib_u256, bRm,
                              IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpsrld_imm_u256, iemAImpl_vpsrld_imm_u256_fallback));
    }
    else
    {
        return FNIEMOP_CALL_2(iemOpCommonAvxAvx2_Hx_Ux_Ib_u128, bRm,
                              IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpsrld_imm_u128, iemAImpl_vpsrld_imm_u128_fallback));
    }
}


/*  Opcode VEX.0F 0x72 11/4 - invalid. */
/** Opcode VEX.66.0F 0x72 11/4. */
FNIEMOP_DEF_1(iemOp_VGrp13_vpsrad_Hx_Ux_Ib, uint8_t, bRm)
{
    IEMOP_MNEMONIC3(VEX_VMI_REG, VPSRAD, vpsrad, Hx, Ux, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    if (pVCpu->iem.s.uVexLength)
    {
        return FNIEMOP_CALL_2(iemOpCommonAvxAvx2_Hx_Ux_Ib_u256, bRm,
                              IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpsrad_imm_u256, iemAImpl_vpsrad_imm_u256_fallback));
    }
    else
    {
        return FNIEMOP_CALL_2(iemOpCommonAvxAvx2_Hx_Ux_Ib_u128, bRm,
                              IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpsrad_imm_u128, iemAImpl_vpsrad_imm_u128_fallback));
    }
}

/*  Opcode VEX.0F 0x72 11/6 - invalid. */

/** Opcode VEX.66.0F 0x72 11/6. */
FNIEMOP_DEF_1(iemOp_VGrp13_vpslld_Hx_Ux_Ib, uint8_t, bRm)
{
    IEMOP_MNEMONIC3(VEX_VMI_REG, VPSLLD, vpslld, Hx, Ux, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    if (pVCpu->iem.s.uVexLength)
    {
        return FNIEMOP_CALL_2(iemOpCommonAvxAvx2_Hx_Ux_Ib_u256, bRm,
                              IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpslld_imm_u256, iemAImpl_vpslld_imm_u256_fallback));
    }
    else
    {
        return FNIEMOP_CALL_2(iemOpCommonAvxAvx2_Hx_Ux_Ib_u128, bRm,
                              IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpslld_imm_u128, iemAImpl_vpslld_imm_u128_fallback));
    }
}


/**
 * Group 13 jump table for register variant.
 */
IEM_STATIC const PFNIEMOPRM g_apfnVexGroup13RegReg[] =
{
    /* /0 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8),
    /* /1 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8),
    /* /2 */ iemOp_InvalidWithRMNeedImm8,   iemOp_VGrp13_vpsrld_Hx_Ux_Ib, iemOp_InvalidWithRMNeedImm8, iemOp_InvalidWithRMNeedImm8,
    /* /3 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8),
    /* /4 */ iemOp_InvalidWithRMNeedImm8,   iemOp_VGrp13_vpsrad_Hx_Ux_Ib, iemOp_InvalidWithRMNeedImm8, iemOp_InvalidWithRMNeedImm8,
    /* /5 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8),
    /* /6 */ iemOp_InvalidWithRMNeedImm8,   iemOp_VGrp13_vpslld_Hx_Ux_Ib, iemOp_InvalidWithRMNeedImm8, iemOp_InvalidWithRMNeedImm8,
    /* /7 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8)
};
AssertCompile(RT_ELEMENTS(g_apfnVexGroup13RegReg) == 8*4);

/** Opcode VEX.0F 0x72. */
FNIEMOP_DEF(iemOp_VGrp13)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
        /* register, register */
        return FNIEMOP_CALL_1(g_apfnVexGroup13RegReg[ IEM_GET_MODRM_REG_8(bRm) * 4
                                                     + pVCpu->iem.s.idxPrefix], bRm);
    return FNIEMOP_CALL_1(iemOp_InvalidWithRMNeedImm8, bRm);
}


/*  Opcode VEX.0F 0x73 11/2 - invalid. */
/** Opcode VEX.66.0F 0x73 11/2. */
FNIEMOP_DEF_1(iemOp_VGrp14_vpsrlq_Hx_Ux_Ib, uint8_t, bRm)
{
    IEMOP_MNEMONIC3(VEX_VMI_REG, VPSRLQ, vpsrlq, Hx, Ux, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    if (pVCpu->iem.s.uVexLength)
    {
        return FNIEMOP_CALL_2(iemOpCommonAvxAvx2_Hx_Ux_Ib_u256, bRm,
                              IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpsrlq_imm_u256, iemAImpl_vpsrlq_imm_u256_fallback));
    }
    else
    {
        return FNIEMOP_CALL_2(iemOpCommonAvxAvx2_Hx_Ux_Ib_u128, bRm,
                              IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpsrlq_imm_u128, iemAImpl_vpsrlq_imm_u128_fallback));
    }
}


/** Opcode VEX.66.0F 0x73 11/3. */
FNIEMOP_DEF_1(iemOp_VGrp14_vpsrldq_Hx_Ux_Ib, uint8_t, bRm)
{
    IEMOP_MNEMONIC3(VEX_VMI_REG, VPSRLDQ, vpsrldq, Hx, Ux, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    if (pVCpu->iem.s.uVexLength)
    {
        return FNIEMOP_CALL_2(iemOpCommonAvxAvx2_Hx_Ux_Ib_u256, bRm,
                              IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpsrldq_imm_u256, iemAImpl_vpsrldq_imm_u256_fallback));
    }
    else
    {
        return FNIEMOP_CALL_2(iemOpCommonAvxAvx2_Hx_Ux_Ib_u128, bRm,
                              IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpsrldq_imm_u128, iemAImpl_vpsrldq_imm_u128_fallback));
    }
}

/*  Opcode VEX.0F 0x73 11/6 - invalid. */

/** Opcode VEX.66.0F 0x73 11/6. */
FNIEMOP_DEF_1(iemOp_VGrp14_vpsllq_Hx_Ux_Ib, uint8_t, bRm)
{
    IEMOP_MNEMONIC3(VEX_VMI_REG, VPSLLQ, vpsllq, Hx, Ux, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    if (pVCpu->iem.s.uVexLength)
    {
        return FNIEMOP_CALL_2(iemOpCommonAvxAvx2_Hx_Ux_Ib_u256, bRm,
                              IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpsllq_imm_u256, iemAImpl_vpsllq_imm_u256_fallback));
    }
    else
    {
        return FNIEMOP_CALL_2(iemOpCommonAvxAvx2_Hx_Ux_Ib_u128, bRm,
                              IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpsllq_imm_u128, iemAImpl_vpsllq_imm_u128_fallback));
    }
}

/** Opcode VEX.66.0F 0x73 11/7. */
FNIEMOP_DEF_1(iemOp_VGrp14_vpslldq_Hx_Ux_Ib, uint8_t, bRm)
{
    IEMOP_MNEMONIC3(VEX_VMI_REG, VPSLLDQ, vpslldq, Hx, Ux, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    if (pVCpu->iem.s.uVexLength)
    {
        return FNIEMOP_CALL_2(iemOpCommonAvxAvx2_Hx_Ux_Ib_u256, bRm,
                              IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpslldq_imm_u256, iemAImpl_vpslldq_imm_u256_fallback));
    }
    else
    {
        return FNIEMOP_CALL_2(iemOpCommonAvxAvx2_Hx_Ux_Ib_u128, bRm,
                              IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpslldq_imm_u128, iemAImpl_vpslldq_imm_u128_fallback));
    }
}

/*  Opcode VEX.0F 0x73 11/6 - invalid. */

/**
 * Group 14 jump table for register variant.
 */
IEM_STATIC const PFNIEMOPRM g_apfnVexGroup14RegReg[] =
{
    /* /0 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8),
    /* /1 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8),
    /* /2 */ iemOp_InvalidWithRMNeedImm8, iemOp_VGrp14_vpsrlq_Hx_Ux_Ib,  iemOp_InvalidWithRMNeedImm8, iemOp_InvalidWithRMNeedImm8,
    /* /3 */ iemOp_InvalidWithRMNeedImm8, iemOp_VGrp14_vpsrldq_Hx_Ux_Ib, iemOp_InvalidWithRMNeedImm8, iemOp_InvalidWithRMNeedImm8,
    /* /4 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8),
    /* /5 */ IEMOP_X4(iemOp_InvalidWithRMNeedImm8),
    /* /6 */ iemOp_InvalidWithRMNeedImm8, iemOp_VGrp14_vpsllq_Hx_Ux_Ib,  iemOp_InvalidWithRMNeedImm8, iemOp_InvalidWithRMNeedImm8,
    /* /7 */ iemOp_InvalidWithRMNeedImm8, iemOp_VGrp14_vpslldq_Hx_Ux_Ib, iemOp_InvalidWithRMNeedImm8, iemOp_InvalidWithRMNeedImm8,
};
AssertCompile(RT_ELEMENTS(g_apfnVexGroup14RegReg) == 8*4);


/** Opcode VEX.0F 0x73. */
FNIEMOP_DEF(iemOp_VGrp14)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
        /* register, register */
        return FNIEMOP_CALL_1(g_apfnVexGroup14RegReg[ IEM_GET_MODRM_REG_8(bRm) * 4
                                                     + pVCpu->iem.s.idxPrefix], bRm);
    return FNIEMOP_CALL_1(iemOp_InvalidWithRMNeedImm8, bRm);
}


/*  Opcode VEX.0F 0x74 - invalid */


/** Opcode VEX.66.0F 0x74 - vpcmpeqb Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpcmpeqb_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPCMPEQB, vpcmpeqb, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(         vpcmpeqb);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}

/*  Opcode VEX.F3.0F 0x74 - invalid */
/*  Opcode VEX.F2.0F 0x74 - invalid */


/*  Opcode VEX.0F 0x75 - invalid */


/** Opcode VEX.66.0F 0x75 - vpcmpeqw Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpcmpeqw_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPCMPEQW, vpcmpeqw, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(         vpcmpeqw);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0x75 - invalid */
/*  Opcode VEX.F2.0F 0x75 - invalid */


/*  Opcode VEX.0F 0x76 - invalid */


/** Opcode VEX.66.0F 0x76 - vpcmpeqd Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpcmpeqd_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPCMPEQD, vpcmpeqd, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(         vpcmpeqd);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0x76 - invalid */
/*  Opcode VEX.F2.0F 0x76 - invalid */


/** Opcode VEX.0F 0x77 - vzeroupperv vzeroallv */
FNIEMOP_DEF(iemOp_vzeroupperv__vzeroallv)
{
    Assert(pVCpu->iem.s.uVexLength <= 1);
    if (pVCpu->iem.s.uVexLength == 0)
    {
        /*
         * 128-bit: vzeroupper
         */
        IEMOP_MNEMONIC(vzeroupper, "vzeroupper");
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);

        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

        IEM_MC_CLEAR_YREG_128_UP(0);
        IEM_MC_CLEAR_YREG_128_UP(1);
        IEM_MC_CLEAR_YREG_128_UP(2);
        IEM_MC_CLEAR_YREG_128_UP(3);
        IEM_MC_CLEAR_YREG_128_UP(4);
        IEM_MC_CLEAR_YREG_128_UP(5);
        IEM_MC_CLEAR_YREG_128_UP(6);
        IEM_MC_CLEAR_YREG_128_UP(7);

        if (pVCpu->iem.s.enmEffOpSize == IEMMODE_64BIT)
        {
            IEM_MC_CLEAR_YREG_128_UP( 8);
            IEM_MC_CLEAR_YREG_128_UP( 9);
            IEM_MC_CLEAR_YREG_128_UP(10);
            IEM_MC_CLEAR_YREG_128_UP(11);
            IEM_MC_CLEAR_YREG_128_UP(12);
            IEM_MC_CLEAR_YREG_128_UP(13);
            IEM_MC_CLEAR_YREG_128_UP(14);
            IEM_MC_CLEAR_YREG_128_UP(15);
        }

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * 256-bit: vzeroall
         */
        IEMOP_MNEMONIC(vzeroall, "vzeroall");
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);

        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

        IEM_MC_LOCAL_CONST(uint32_t, uZero, 0);
        IEM_MC_STORE_YREG_U32_ZX_VLMAX(0, uZero);
        IEM_MC_STORE_YREG_U32_ZX_VLMAX(1, uZero);
        IEM_MC_STORE_YREG_U32_ZX_VLMAX(2, uZero);
        IEM_MC_STORE_YREG_U32_ZX_VLMAX(3, uZero);
        IEM_MC_STORE_YREG_U32_ZX_VLMAX(4, uZero);
        IEM_MC_STORE_YREG_U32_ZX_VLMAX(5, uZero);
        IEM_MC_STORE_YREG_U32_ZX_VLMAX(6, uZero);
        IEM_MC_STORE_YREG_U32_ZX_VLMAX(7, uZero);

        if (pVCpu->iem.s.enmEffOpSize == IEMMODE_64BIT)
        {
            IEM_MC_STORE_YREG_U32_ZX_VLMAX( 8, uZero);
            IEM_MC_STORE_YREG_U32_ZX_VLMAX( 9, uZero);
            IEM_MC_STORE_YREG_U32_ZX_VLMAX(10, uZero);
            IEM_MC_STORE_YREG_U32_ZX_VLMAX(11, uZero);
            IEM_MC_STORE_YREG_U32_ZX_VLMAX(12, uZero);
            IEM_MC_STORE_YREG_U32_ZX_VLMAX(13, uZero);
            IEM_MC_STORE_YREG_U32_ZX_VLMAX(14, uZero);
            IEM_MC_STORE_YREG_U32_ZX_VLMAX(15, uZero);
        }

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/*  Opcode VEX.66.0F 0x77 - invalid */
/*  Opcode VEX.F3.0F 0x77 - invalid */
/*  Opcode VEX.F2.0F 0x77 - invalid */

/*  Opcode VEX.0F 0x78 - invalid */
/*  Opcode VEX.66.0F 0x78 - invalid */
/*  Opcode VEX.F3.0F 0x78 - invalid */
/*  Opcode VEX.F2.0F 0x78 - invalid */

/*  Opcode VEX.0F 0x79 - invalid */
/*  Opcode VEX.66.0F 0x79 - invalid */
/*  Opcode VEX.F3.0F 0x79 - invalid */
/*  Opcode VEX.F2.0F 0x79 - invalid */

/*  Opcode VEX.0F 0x7a - invalid */
/*  Opcode VEX.66.0F 0x7a - invalid */
/*  Opcode VEX.F3.0F 0x7a - invalid */
/*  Opcode VEX.F2.0F 0x7a - invalid */

/*  Opcode VEX.0F 0x7b - invalid */
/*  Opcode VEX.66.0F 0x7b - invalid */
/*  Opcode VEX.F3.0F 0x7b - invalid */
/*  Opcode VEX.F2.0F 0x7b - invalid */

/*  Opcode VEX.0F 0x7c - invalid */


/** Opcode VEX.66.0F 0x7c - vhaddpd Vpd, Hpd, Wpd */
FNIEMOP_DEF(iemOp_vhaddpd_Vpd_Hpd_Wpd)
{
    IEMOP_MNEMONIC3(VEX_RVM, VHADDPD, vhaddpd, Vps, Hps, Wps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAF3_INIT_VARS(           vhaddpd);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0x7c - invalid */


/** Opcode VEX.F2.0F 0x7c - vhaddps Vps, Hps, Wps */
FNIEMOP_DEF(iemOp_vhaddps_Vps_Hps_Wps)
{
    IEMOP_MNEMONIC3(VEX_RVM, VHADDPS, vhaddps, Vps, Hps, Wps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAF3_INIT_VARS(           vhaddps);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/*  Opcode VEX.0F 0x7d - invalid */


/** Opcode VEX.66.0F 0x7d - vhsubpd Vpd, Hpd, Wpd */
FNIEMOP_DEF(iemOp_vhsubpd_Vpd_Hpd_Wpd)
{
    IEMOP_MNEMONIC3(VEX_RVM, VHSUBPD, vhsubpd, Vps, Hps, Wps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAF3_INIT_VARS(           vhsubpd);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0x7d - invalid */


/** Opcode VEX.F2.0F 0x7d - vhsubps Vps, Hps, Wps */
FNIEMOP_DEF(iemOp_vhsubps_Vps_Hps_Wps)
{
    IEMOP_MNEMONIC3(VEX_RVM, VHSUBPS, vhsubps, Vps, Hps, Wps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAF3_INIT_VARS(           vhsubps);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/*  Opcode VEX.0F 0x7e - invalid */

FNIEMOP_DEF(iemOp_vmovd_q_Ey_Vy)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    IEMOP_HLP_IGNORE_VEX_W_PREFIX_IF_NOT_IN_64BIT();
    if (pVCpu->iem.s.fPrefixes & IEM_OP_PRF_SIZE_REX_W)
    {
        /**
         * @opcode      0x7e
         * @opcodesub   rex.w=1
         * @oppfx       0x66
         * @opcpuid     avx
         * @opgroup     og_avx_simdint_datamov
         * @opxcpttype  5
         * @optest      64-bit / op1=1 op2=2   -> op1=2
         * @optest      64-bit / op1=0 op2=-42 -> op1=-42
         */
        IEMOP_MNEMONIC2(VEX_MR, VMOVQ, vmovq, Eq_WO, Vq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OZ_PFX | IEMOPHINT_VEX_L_ZERO);
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* greg64, XMM */
            IEM_MC_BEGIN(IEM_MC_F_64BIT, 0);
            IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
            IEM_MC_LOCAL(uint64_t, u64Tmp);

            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ();

            IEM_MC_FETCH_YREG_U64(u64Tmp, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iQWord*/);
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
            IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ();

            IEM_MC_FETCH_YREG_U64(u64Tmp, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iQWord*/);
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
         * @opcpuid     avx
         * @opgroup     og_avx_simdint_datamov
         * @opxcpttype  5
         * @opfunction  iemOp_vmovd_q_Vy_Ey
         * @optest      op1=1 op2=2   -> op1=2
         * @optest      op1=0 op2=-42 -> op1=-42
         */
        IEMOP_MNEMONIC2(VEX_MR, VMOVD, vmovd, Ed_WO, Vd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OZ_PFX | IEMOPHINT_VEX_L_ZERO);
        if (IEM_IS_MODRM_REG_MODE(bRm))
        {
            /* greg32, XMM */
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
            IEM_MC_LOCAL(uint32_t, u32Tmp);

            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ();

            IEM_MC_FETCH_YREG_U32(u32Tmp, IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_RM(pVCpu, bRm), u32Tmp);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /* [mem32], XMM */
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR, GCPtrEffSrc);
            IEM_MC_LOCAL(uint32_t, u32Tmp);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ();

            IEM_MC_FETCH_YREG_U32(u32Tmp, IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_STORE_MEM_U32(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, u32Tmp);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/**
 * @opcode      0x7e
 * @oppfx       0xf3
 * @opcpuid     avx
 * @opgroup     og_avx_pcksclr_datamove
 * @opxcpttype  none
 * @optest      op1=1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_vmovq_Vq_Wq)
{
    IEMOP_MNEMONIC2(VEX_RM, VMOVQ, vmovq, Vq_WO, Wq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_ZERO);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);

        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

        IEM_MC_COPY_YREG_U64_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm),
                                      IEM_GET_MODRM_RM(pVCpu, bRm));
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
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U64(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_YREG_U64_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }

}
/*  Opcode VEX.F2.0F 0x7e - invalid */


/*  Opcode VEX.0F 0x7f - invalid */

/**
 * @opcode      0x7f
 * @oppfx       0x66
 * @opcpuid     avx
 * @opgroup     og_avx_simdint_datamove
 * @opxcpttype  1
 * @optest      op1=1 op2=2   -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_vmovdqa_Wx_Vx)
{
    IEMOP_MNEMONIC2(VEX_MR, VMOVDQA, vmovdqa, Wx_WO, Vx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES);
    Assert(pVCpu->iem.s.uVexLength <= 1);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);

        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();
        if (pVCpu->iem.s.uVexLength == 0)
            IEM_MC_COPY_YREG_U128_ZX_VLMAX(IEM_GET_MODRM_RM(pVCpu, bRm),
                                           IEM_GET_MODRM_REG(pVCpu, bRm));
        else
            IEM_MC_COPY_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_RM(pVCpu, bRm),
                                           IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else if (pVCpu->iem.s.uVexLength == 0)
    {
        /*
         * Register, memory128.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT128U, u128Tmp);
        IEM_MC_LOCAL(RTGCPTR,    GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ();

        IEM_MC_FETCH_YREG_U128(u128Tmp, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iDQWord*/);
        IEM_MC_STORE_MEM_U128_ALIGN_SSE(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, u128Tmp);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory256.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT256U, u256Tmp);
        IEM_MC_LOCAL(RTGCPTR,    GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ();

        IEM_MC_FETCH_YREG_U256(u256Tmp, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_STORE_MEM_U256_ALIGN_AVX(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, u256Tmp);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/**
 * @opcode      0x7f
 * @oppfx       0xf3
 * @opcpuid     avx
 * @opgroup     og_avx_simdint_datamove
 * @opxcpttype  4UA
 * @optest      op1=1 op2=2   -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_vmovdqu_Wx_Vx)
{
    IEMOP_MNEMONIC2(VEX_MR, VMOVDQU, vmovdqu, Wx_WO, Vx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES);
    Assert(pVCpu->iem.s.uVexLength <= 1);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);

        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();
        if (pVCpu->iem.s.uVexLength == 0)
            IEM_MC_COPY_YREG_U128_ZX_VLMAX(IEM_GET_MODRM_RM(pVCpu, bRm),
                                           IEM_GET_MODRM_REG(pVCpu, bRm));
        else
            IEM_MC_COPY_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_RM(pVCpu, bRm),
                                           IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else if (pVCpu->iem.s.uVexLength == 0)
    {
        /*
         * Register, memory128.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT128U, u128Tmp);
        IEM_MC_LOCAL(RTGCPTR,    GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ();

        IEM_MC_FETCH_YREG_U128(u128Tmp, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iDQWord*/);
        IEM_MC_STORE_MEM_U128_NO_AC(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, u128Tmp);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory256.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT256U, u256Tmp);
        IEM_MC_LOCAL(RTGCPTR,    GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ();

        IEM_MC_FETCH_YREG_U256(u256Tmp, IEM_GET_MODRM_REG(pVCpu, bRm));
        IEM_MC_STORE_MEM_U256_NO_AC(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, u256Tmp);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}

/*  Opcode VEX.F2.0F 0x7f - invalid */


/*  Opcode VEX.0F 0x80 - invalid  */
/*  Opcode VEX.0F 0x81 - invalid  */
/*  Opcode VEX.0F 0x82 - invalid  */
/*  Opcode VEX.0F 0x83 - invalid  */
/*  Opcode VEX.0F 0x84 - invalid  */
/*  Opcode VEX.0F 0x85 - invalid  */
/*  Opcode VEX.0F 0x86 - invalid  */
/*  Opcode VEX.0F 0x87 - invalid  */
/*  Opcode VEX.0F 0x88 - invalid  */
/*  Opcode VEX.0F 0x89 - invalid  */
/*  Opcode VEX.0F 0x8a - invalid  */
/*  Opcode VEX.0F 0x8b - invalid  */
/*  Opcode VEX.0F 0x8c - invalid  */
/*  Opcode VEX.0F 0x8d - invalid  */
/*  Opcode VEX.0F 0x8e - invalid  */
/*  Opcode VEX.0F 0x8f - invalid  */
/*  Opcode VEX.0F 0x90 - invalid  */
/*  Opcode VEX.0F 0x91 - invalid  */
/*  Opcode VEX.0F 0x92 - invalid  */
/*  Opcode VEX.0F 0x93 - invalid  */
/*  Opcode VEX.0F 0x94 - invalid  */
/*  Opcode VEX.0F 0x95 - invalid  */
/*  Opcode VEX.0F 0x96 - invalid  */
/*  Opcode VEX.0F 0x97 - invalid  */
/*  Opcode VEX.0F 0x98 - invalid  */
/*  Opcode VEX.0F 0x99 - invalid  */
/*  Opcode VEX.0F 0x9a - invalid  */
/*  Opcode VEX.0F 0x9b - invalid  */
/*  Opcode VEX.0F 0x9c - invalid  */
/*  Opcode VEX.0F 0x9d - invalid  */
/*  Opcode VEX.0F 0x9e - invalid  */
/*  Opcode VEX.0F 0x9f - invalid  */
/*  Opcode VEX.0F 0xa0 - invalid  */
/*  Opcode VEX.0F 0xa1 - invalid  */
/*  Opcode VEX.0F 0xa2 - invalid  */
/*  Opcode VEX.0F 0xa3 - invalid  */
/*  Opcode VEX.0F 0xa4 - invalid  */
/*  Opcode VEX.0F 0xa5 - invalid  */
/*  Opcode VEX.0F 0xa6 - invalid  */
/*  Opcode VEX.0F 0xa7 - invalid  */
/*  Opcode VEX.0F 0xa8 - invalid  */
/*  Opcode VEX.0F 0xa9 - invalid  */
/*  Opcode VEX.0F 0xaa - invalid  */
/*  Opcode VEX.0F 0xab - invalid  */
/*  Opcode VEX.0F 0xac - invalid  */
/*  Opcode VEX.0F 0xad - invalid  */


/*  Opcode VEX.0F 0xae mem/0 - invalid. */
/*  Opcode VEX.0F 0xae mem/1 - invalid. */

/**
 * @ opmaps      grp15
 * @ opcode      !11/2
 * @ oppfx       none
 * @ opcpuid     sse
 * @ opgroup     og_sse_mxcsrsm
 * @ opxcpttype  5
 * @ optest      op1=0      -> mxcsr=0
 * @ optest      op1=0x2083 -> mxcsr=0x2083
 * @ optest      op1=0xfffffffe -> value.xcpt=0xd
 * @ optest      op1=0x2083 cr0|=ts -> value.xcpt=0x7
 * @ optest      op1=0x2083 cr0|=em -> value.xcpt=0x6
 * @ optest      op1=0x2083 cr0|=mp -> mxcsr=0x2083
 * @ optest      op1=0x2083 cr4&~=osfxsr -> value.xcpt=0x6
 * @ optest      op1=0x2083 cr0|=ts,em -> value.xcpt=0x6
 * @ optest      op1=0x2083 cr0|=em cr4&~=osfxsr -> value.xcpt=0x6
 * @ optest      op1=0x2083 cr0|=ts,em cr4&~=osfxsr -> value.xcpt=0x6
 * @ optest      op1=0x2083 cr0|=ts,em,mp cr4&~=osfxsr -> value.xcpt=0x6
 */
FNIEMOP_DEF_1(iemOp_VGrp15_vldmxcsr, uint8_t, bRm)
{
    IEMOP_MNEMONIC1(M_MEM, VLDMXCSR, vldmxcsr, Md_RO, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_ZERO);
    IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
    IEM_MC_ARG(RTGCPTR,         GCPtrEff,                                1);
    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEff, bRm, 0);
    IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
    IEM_MC_ACTUALIZE_SSE_STATE_FOR_READ();
    IEM_MC_ARG_CONST(uint8_t,   iEffSeg, /*=*/ pVCpu->iem.s.iEffSeg,     0);
    IEM_MC_CALL_CIMPL_2(IEM_CIMPL_F_FPU, RT_BIT_64(kIemNativeGstReg_MxCsr), iemCImpl_vldmxcsr, iEffSeg, GCPtrEff);
    IEM_MC_END();
}


/**
 * @opmaps      vexgrp15
 * @opcode      !11/3
 * @oppfx       none
 * @opcpuid     avx
 * @opgroup     og_avx_mxcsrsm
 * @opxcpttype  5
 * @optest      mxcsr=0      -> op1=0
 * @optest      mxcsr=0x2083 -> op1=0x2083
 * @optest      mxcsr=0x2084 cr0|=ts -> value.xcpt=0x7
 * @optest      !amd / mxcsr=0x2085 cr0|=em -> op1=0x2085
 * @optest       amd / mxcsr=0x2085 cr0|=em -> value.xcpt=0x6
 * @optest      mxcsr=0x2086 cr0|=mp -> op1=0x2086
 * @optest      mxcsr=0x2087 cr4&~=osfxsr -> op1=0x2087
 * @optest      mxcsr=0x208f cr4&~=osxsave -> value.xcpt=0x6
 * @optest      mxcsr=0x2087 cr4&~=osfxsr,osxsave -> value.xcpt=0x6
 * @optest      !amd / mxcsr=0x2088 cr0|=ts,em -> value.xcpt=0x7
 * @optest      amd  / mxcsr=0x2088 cr0|=ts,em -> value.xcpt=0x6
 * @optest      !amd / mxcsr=0x2089 cr0|=em cr4&~=osfxsr -> op1=0x2089
 * @optest      amd  / mxcsr=0x2089 cr0|=em cr4&~=osfxsr -> value.xcpt=0x6
 * @optest      !amd / mxcsr=0x208a cr0|=ts,em cr4&~=osfxsr -> value.xcpt=0x7
 * @optest      amd  / mxcsr=0x208a cr0|=ts,em cr4&~=osfxsr -> value.xcpt=0x6
 * @optest      !amd / mxcsr=0x208b cr0|=ts,em,mp cr4&~=osfxsr -> value.xcpt=0x7
 * @optest      amd  / mxcsr=0x208b cr0|=ts,em,mp cr4&~=osfxsr -> value.xcpt=0x6
 * @optest      !amd / mxcsr=0x208c xcr0&~=all_avx -> value.xcpt=0x6
 * @optest      amd  / mxcsr=0x208c xcr0&~=all_avx -> op1=0x208c
 * @optest      !amd / mxcsr=0x208d xcr0&~=all_avx_sse -> value.xcpt=0x6
 * @optest      amd  / mxcsr=0x208d xcr0&~=all_avx_sse -> op1=0x208d
 * @optest      !amd / mxcsr=0x208e xcr0&~=all_avx cr0|=ts -> value.xcpt=0x6
 * @optest      amd  / mxcsr=0x208e xcr0&~=all_avx cr0|=ts -> value.xcpt=0x7
 * @optest      mxcsr=0x2082 cr0|=ts cr4&~=osxsave -> value.xcpt=0x6
 * @optest      mxcsr=0x2081 xcr0&~=all_avx cr0|=ts cr4&~=osxsave
 *              -> value.xcpt=0x6
 * @remarks     AMD Jaguar CPU (f0x16,m0,s1) \#UD when CR0.EM is set.  It also
 *              doesn't seem to check XCR0[2:1] != 11b.  This does not match the
 *              APMv4 rev 3.17 page 509.
 * @todo        Test this instruction on AMD Ryzen.
 */
FNIEMOP_DEF_1(iemOp_VGrp15_vstmxcsr,  uint8_t, bRm)
{
    IEMOP_MNEMONIC1(VEX_M_MEM, VSTMXCSR, vstmxcsr, Md_WO, DISOPTYPE_HARMLESS, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_ZERO);
    IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
    IEM_MC_ARG(RTGCPTR,         GCPtrEff,                               1);
    IEM_MC_CALC_RM_EFF_ADDR(GCPtrEff, bRm, 0);
    IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
    IEM_MC_ACTUALIZE_SSE_STATE_FOR_READ();
    IEM_MC_ARG_CONST(uint8_t,   iEffSeg, /*=*/ pVCpu->iem.s.iEffSeg,    0);
    IEM_MC_CALL_CIMPL_2(IEM_CIMPL_F_FPU, 0, iemCImpl_vstmxcsr, iEffSeg, GCPtrEff);
    IEM_MC_END();
}

/*  Opcode VEX.0F 0xae mem/4 - invalid. */
/*  Opcode VEX.0F 0xae mem/5 - invalid. */
/*  Opcode VEX.0F 0xae mem/6 - invalid. */
/*  Opcode VEX.0F 0xae mem/7 - invalid. */

/*  Opcode VEX.0F 0xae 11b/0 - invalid. */
/*  Opcode VEX.0F 0xae 11b/1 - invalid. */
/*  Opcode VEX.0F 0xae 11b/2 - invalid. */
/*  Opcode VEX.0F 0xae 11b/3 - invalid. */
/*  Opcode VEX.0F 0xae 11b/4 - invalid. */
/*  Opcode VEX.0F 0xae 11b/5 - invalid. */
/*  Opcode VEX.0F 0xae 11b/6 - invalid. */
/*  Opcode VEX.0F 0xae 11b/7 - invalid. */

/**
 * Vex group 15 jump table for memory variant.
 */
IEM_STATIC const PFNIEMOPRM g_apfnVexGroup15MemReg[] =
{   /* pfx:  none,                          066h,                           0f3h,                           0f2h */
    /* /0 */ iemOp_InvalidWithRM,           iemOp_InvalidWithRM,            iemOp_InvalidWithRM,            iemOp_InvalidWithRM,
    /* /1 */ iemOp_InvalidWithRM,           iemOp_InvalidWithRM,            iemOp_InvalidWithRM,            iemOp_InvalidWithRM,
    /* /2 */ iemOp_VGrp15_vldmxcsr,         iemOp_InvalidWithRM,            iemOp_InvalidWithRM,            iemOp_InvalidWithRM,
    /* /3 */ iemOp_VGrp15_vstmxcsr,         iemOp_InvalidWithRM,            iemOp_InvalidWithRM,            iemOp_InvalidWithRM,
    /* /4 */ iemOp_InvalidWithRM,           iemOp_InvalidWithRM,            iemOp_InvalidWithRM,            iemOp_InvalidWithRM,
    /* /5 */ iemOp_InvalidWithRM,           iemOp_InvalidWithRM,            iemOp_InvalidWithRM,            iemOp_InvalidWithRM,
    /* /6 */ iemOp_InvalidWithRM,           iemOp_InvalidWithRM,            iemOp_InvalidWithRM,            iemOp_InvalidWithRM,
    /* /7 */ iemOp_InvalidWithRM,           iemOp_InvalidWithRM,            iemOp_InvalidWithRM,            iemOp_InvalidWithRM,
};
AssertCompile(RT_ELEMENTS(g_apfnVexGroup15MemReg) == 8*4);


/** Opcode vex. 0xae. */
FNIEMOP_DEF(iemOp_VGrp15)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
        /* register, register */
        return FNIEMOP_CALL_1(iemOp_InvalidWithRM, bRm);

    /* memory, register */
    return FNIEMOP_CALL_1(g_apfnVexGroup15MemReg[ IEM_GET_MODRM_REG_8(bRm) * 4
                                                 + pVCpu->iem.s.idxPrefix], bRm);
}


/*  Opcode VEX.0F 0xaf - invalid. */

/*  Opcode VEX.0F 0xb0 - invalid. */
/*  Opcode VEX.0F 0xb1 - invalid. */
/*  Opcode VEX.0F 0xb2 - invalid. */
/*  Opcode VEX.0F 0xb2 - invalid. */
/*  Opcode VEX.0F 0xb3 - invalid. */
/*  Opcode VEX.0F 0xb4 - invalid. */
/*  Opcode VEX.0F 0xb5 - invalid. */
/*  Opcode VEX.0F 0xb6 - invalid. */
/*  Opcode VEX.0F 0xb7 - invalid. */
/*  Opcode VEX.0F 0xb8 - invalid. */
/*  Opcode VEX.0F 0xb9 - invalid. */
/*  Opcode VEX.0F 0xba - invalid. */
/*  Opcode VEX.0F 0xbb - invalid. */
/*  Opcode VEX.0F 0xbc - invalid. */
/*  Opcode VEX.0F 0xbd - invalid. */
/*  Opcode VEX.0F 0xbe - invalid. */
/*  Opcode VEX.0F 0xbf - invalid. */

/*  Opcode VEX.0F 0xc0 - invalid. */
/*  Opcode VEX.66.0F 0xc0 - invalid. */
/*  Opcode VEX.F3.0F 0xc0 - invalid. */
/*  Opcode VEX.F2.0F 0xc0 - invalid. */

/*  Opcode VEX.0F 0xc1 - invalid. */
/*  Opcode VEX.66.0F 0xc1 - invalid. */
/*  Opcode VEX.F3.0F 0xc1 - invalid. */
/*  Opcode VEX.F2.0F 0xc1 - invalid. */

#define IEMOP_VCMPP_BODY(a_Instr) \
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm); \
    if (IEM_IS_MODRM_REG_MODE(bRm)) \
    { \
        /* \
         * Register, Register. \
         */ \
        if (pVCpu->iem.s.uVexLength) \
        { \
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0); \
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm); \
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx); \
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT(); \
            IEM_MC_PREPARE_AVX_USAGE(); \
            IEM_MC_LOCAL(X86YMMREG,                     uDst); \
            IEM_MC_ARG_LOCAL_REF(PX86YMMREG,            puDst,         uDst,    0); \
            IEM_MC_LOCAL(IEMMEDIAF2YMMSRC,              uSrc); \
            IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2YMMSRC,    puSrc,         uSrc,    1); \
            IEM_MC_FETCH_YREG_PAIR_YMM(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), IEM_GET_MODRM_RM(pVCpu, bRm)); \
            IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/ bImm,    2); \
            IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, \
                                                                RT_CONCAT3(iemAImpl_,a_Instr,_u256), \
                                                                RT_CONCAT3(iemAImpl_,a_Instr,_u256_fallback)), \
                                    puDst, puSrc, bImmArg); \
            IEM_MC_STORE_YREG_YMM_ZX_VLMAX( IEM_GET_MODRM_REG(pVCpu, bRm), uDst); \
            IEM_MC_ADVANCE_RIP_AND_FINISH(); \
            IEM_MC_END(); \
        } \
        else \
        { \
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0); \
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm); \
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx); \
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT(); \
            IEM_MC_PREPARE_AVX_USAGE(); \
            IEM_MC_LOCAL(X86XMMREG,                     uDst); \
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,            puDst,         uDst,    0); \
            IEM_MC_LOCAL(IEMMEDIAF2XMMSRC,              uSrc); \
            IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2XMMSRC,    puSrc,         uSrc,    1); \
            IEM_MC_FETCH_XREG_PAIR_XMM(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), IEM_GET_MODRM_RM(pVCpu, bRm)); \
            IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/ bImm,    2); \
            IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, \
                                                                RT_CONCAT3(iemAImpl_,a_Instr,_u128), \
                                                                RT_CONCAT3(iemAImpl_,a_Instr,_u128_fallback)), \
                                    puDst, puSrc, bImmArg); \
            IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), uDst); \
            IEM_MC_ADVANCE_RIP_AND_FINISH(); \
            IEM_MC_END(); \
        } \
    } \
    else \
    { \
        /* \
         * Register, Memory. \
         */ \
        if (pVCpu->iem.s.uVexLength) \
        { \
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0); \
            IEM_MC_LOCAL(RTGCPTR,                       GCPtrEffSrc); \
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1); \
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm); \
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx); \
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT(); \
            IEM_MC_PREPARE_AVX_USAGE(); \
            IEM_MC_LOCAL(IEMMEDIAF2YMMSRC,              uSrc); \
            IEM_MC_LOCAL(X86YMMREG,                     uDst); \
            IEM_MC_ARG_LOCAL_REF(PX86YMMREG,            puDst,         uDst,    0); \
            IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2YMMSRC,    puSrc,         uSrc,    1); \
            IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/ bImm,    2); \
            IEM_MC_FETCH_MEM_YMM_ALIGN_AVX_AND_YREG_YMM(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), pVCpu->iem.s.iEffSeg, GCPtrEffSrc); \
            IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, \
                                                                RT_CONCAT3(iemAImpl_,a_Instr,_u256), \
                                                                RT_CONCAT3(iemAImpl_,a_Instr,_u256_fallback)), \
                                    puDst, puSrc, bImmArg); \
            IEM_MC_STORE_YREG_YMM_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uDst); \
            IEM_MC_ADVANCE_RIP_AND_FINISH(); \
            IEM_MC_END(); \
        } \
        else \
        { \
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0); \
            IEM_MC_LOCAL(RTGCPTR,                       GCPtrEffSrc); \
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1); \
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm); \
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx); \
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT(); \
            IEM_MC_PREPARE_AVX_USAGE(); \
            IEM_MC_LOCAL(IEMMEDIAF2XMMSRC,              uSrc); \
            IEM_MC_LOCAL(X86XMMREG,                     uDst); \
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,            puDst,         uDst,    0); \
            IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2XMMSRC,    puSrc,         uSrc,    1); \
            IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/ bImm,    2); \
            IEM_MC_FETCH_MEM_XMM_ALIGN_SSE_AND_XREG_XMM(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), pVCpu->iem.s.iEffSeg, GCPtrEffSrc); \
            IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, \
                                                                RT_CONCAT3(iemAImpl_,a_Instr,_u128), \
                                                                RT_CONCAT3(iemAImpl_,a_Instr,_u128_fallback)), \
                                    puDst, puSrc, bImmArg); \
            IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), uDst); \
            IEM_MC_ADVANCE_RIP_AND_FINISH(); \
            IEM_MC_END(); \
        } \
    } \
    (void)0


/** Opcode VEX.0F 0xc2 - vcmpps Vps,Hps,Wps,Ib */
FNIEMOP_DEF(iemOp_vcmpps_Vps_Hps_Wps_Ib)
{
    IEMOP_MNEMONIC4(VEX_RVMI, VCMPPS, vcmpps, Vps, Hps, Wps, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOP_VCMPP_BODY(vcmpps);
}


/** Opcode VEX.66.0F 0xc2 - vcmppd Vpd,Hpd,Wpd,Ib */
FNIEMOP_DEF(iemOp_vcmppd_Vpd_Hpd_Wpd_Ib)
{
    IEMOP_MNEMONIC4(VEX_RVMI, VCMPPD, vcmppd, Vpd, Hpd, Wpd, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOP_VCMPP_BODY(vcmppd);
}


/** Opcode VEX.F3.0F 0xc2 - vcmpss Vss,Hss,Wss,Ib */
FNIEMOP_DEF(iemOp_vcmpss_Vss_Hss_Wss_Ib)
{
    IEMOP_MNEMONIC4(VEX_RVMI, VCMPSS, vcmpss, Vss, Hps, Wss, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_VEX_L_IGNORED | IEMOPHINT_IGNORES_REXW);

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
        IEM_MC_LOCAL(IEMMEDIAF2XMMSRC,              uSrc);
        IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2XMMSRC,    puSrc,         uSrc,    1);
        IEM_MC_FETCH_XREG_PAIR_XMM(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_LOCAL(X86XMMREG,                     uDst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,            puDst,         uDst,    0);
        IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/ bImm,    2);
        IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vcmpss_u128, iemAImpl_vcmpss_u128_fallback),
                                puDst, puSrc, bImmArg);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);

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
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_LOCAL(IEMMEDIAF2XMMSRC,              uSrc);
        IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2XMMSRC,    puSrc,         uSrc,    1);
        IEM_MC_FETCH_MEM_XMM_U32_AND_XREG_XMM(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm),
                                              0 /*a_iDword*/, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_LOCAL(X86XMMREG,                     uDst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,            puDst,         uDst,    0);
        IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/ bImm,    2);
        IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vcmpss_u128, iemAImpl_vcmpss_u128_fallback),
                                puDst, puSrc, bImmArg);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/** Opcode VEX.F2.0F 0xc2 - vcmpsd Vsd,Hsd,Wsd,Ib */
FNIEMOP_DEF(iemOp_vcmpsd_Vsd_Hsd_Wsd_Ib)
{
    IEMOP_MNEMONIC4(VEX_RVMI, VCMPSD, vcmpsd, Vsd, Hpd, Wsd, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_VEX_L_IGNORED | IEMOPHINT_IGNORES_REXW);

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
        IEM_MC_LOCAL(IEMMEDIAF2XMMSRC,              uSrc);
        IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2XMMSRC,    puSrc,         uSrc,    1);
        IEM_MC_FETCH_XREG_PAIR_XMM(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_LOCAL(X86XMMREG,                     uDst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,            puDst,         uDst,    0);
        IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/ bImm,    2);
        IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vcmpsd_u128, iemAImpl_vcmpsd_u128_fallback),
                                puDst, puSrc, bImmArg);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);

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
        IEM_MC_MAYBE_RAISE_SSE_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_LOCAL(IEMMEDIAF2XMMSRC,              uSrc);
        IEM_MC_ARG_LOCAL_REF(PCIEMMEDIAF2XMMSRC,    puSrc,         uSrc,    1);
        IEM_MC_FETCH_MEM_XMM_U64_AND_XREG_XMM(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm),
                                              0 /*a_iQword*/, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_LOCAL(X86XMMREG,                     uDst);
        IEM_MC_ARG_LOCAL_REF(PX86XMMREG,            puDst,         uDst,    0);
        IEM_MC_ARG_CONST(uint8_t,                   bImmArg, /*=*/ bImm,    2);
        IEM_MC_CALL_AVX_AIMPL_3(IEM_SELECT_HOST_OR_FALLBACK(fAvx, iemAImpl_vcmpsd_u128, iemAImpl_vcmpsd_u128_fallback),
                                puDst, puSrc, bImmArg);
        IEM_MC_STORE_XREG_XMM(IEM_GET_MODRM_REG(pVCpu, bRm), uDst);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/*  Opcode VEX.0F 0xc3 - invalid */
/*  Opcode VEX.66.0F 0xc3 - invalid */
/*  Opcode VEX.F3.0F 0xc3 - invalid */
/*  Opcode VEX.F2.0F 0xc3 - invalid */

/*  Opcode VEX.0F 0xc4 - invalid */


/** Opcode VEX.66.0F 0xc4 - vpinsrw Vdq,Hdq,Ry/Mw,Ib */
FNIEMOP_DEF(iemOp_vpinsrw_Vdq_Hdq_RyMw_Ib)
{
    /*IEMOP_MNEMONIC4(VEX_RMV, VPINSRW, vpinsrw, Vdq, Vdq, Ey, Ib, DISOPTYPE_HARMLESS, IEMOPHINT_VEX_L_ZERO);*/ /** @todo */
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_LOCAL(RTUINT128U,       uSrc1);
        IEM_MC_LOCAL(uint16_t,         uValue);

        IEMOP_HLP_DONE_VEX_DECODING_L0_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_FETCH_XREG_U128(uSrc1,  IEM_GET_EFFECTIVE_VVVV(pVCpu));
        IEM_MC_FETCH_GREG_U16(uValue,  IEM_GET_MODRM_RM(pVCpu, bRm));
        IEM_MC_STORE_XREG_U128(        IEM_GET_MODRM_REG(pVCpu, bRm), uSrc1);
        IEM_MC_STORE_XREG_U16(         IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 7, uValue);
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
        IEM_MC_LOCAL(uint16_t,         uValue);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1);
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEMOP_HLP_DONE_VEX_DECODING_L0_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

        IEM_MC_FETCH_XREG_U128(uSrc1,  IEM_GET_EFFECTIVE_VVVV(pVCpu));
        IEM_MC_FETCH_MEM_U16(uValue,   pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_XREG_U128(        IEM_GET_MODRM_REG(pVCpu, bRm), uSrc1);
        IEM_MC_STORE_XREG_U16(         IEM_GET_MODRM_REG(pVCpu, bRm), bImm & 7, uValue);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/*  Opcode VEX.F3.0F 0xc4 - invalid */
/*  Opcode VEX.F2.0F 0xc4 - invalid */

/*  Opcode VEX.0F 0xc5 - invalid */


/** Opcode VEX.66.0F 0xc5 - vpextrw Gd, Udq, Ib */
FNIEMOP_DEF(iemOp_vpextrw_Gd_Udq_Ib)
{
    IEMOP_MNEMONIC3(VEX_RMI_REG, VPEXTRW, vpextrw, Gd, Ux, Ib, DISOPTYPE_HARMLESS, IEMOPHINT_VEX_L_ZERO);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * greg32, XMM, imm8.
         */
        uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm);
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_EX(fAvx);
        IEM_MC_LOCAL(uint16_t,  uValue);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();
        IEM_MC_FETCH_XREG_U16(uValue, IEM_GET_MODRM_RM(pVCpu, bRm), bImm & 7);
        IEM_MC_STORE_GREG_U32(IEM_GET_MODRM_REG(pVCpu, bRm), uValue);
        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    /* No memory operand. */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}


/*  Opcode VEX.F3.0F 0xc5 - invalid */
/*  Opcode VEX.F2.0F 0xc5 - invalid */


#define VSHUFP_X(a_Instr) \
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm); \
    if (IEM_IS_MODRM_REG_MODE(bRm)) \
    { \
        /* \
         * Register, register. \
         */ \
        if (pVCpu->iem.s.uVexLength) \
        { \
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm); \
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0); \
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx2); \
            IEM_MC_LOCAL(RTUINT256U,            uDst); \
            IEM_MC_LOCAL(RTUINT256U,            uSrc1); \
            IEM_MC_LOCAL(RTUINT256U,            uSrc2); \
            IEM_MC_ARG_LOCAL_REF(PRTUINT256U,   puDst,  uDst,          0); \
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc1, uSrc1,         1); \
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc2, uSrc2,         2); \
            IEM_MC_ARG_CONST(uint8_t,           bImmArg, /*=*/ bImm,   3); \
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT(); \
            IEM_MC_PREPARE_AVX_USAGE(); \
            IEM_MC_FETCH_YREG_U256(uSrc1,   IEM_GET_EFFECTIVE_VVVV(pVCpu)); \
            IEM_MC_FETCH_YREG_U256(uSrc2,   IEM_GET_MODRM_RM(pVCpu, bRm)); \
            IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_ ## a_Instr ## _u256, \
                                                                 iemAImpl_ ## a_Instr ## _u256_fallback), puDst, puSrc1, puSrc2, bImmArg); \
            IEM_MC_STORE_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), uDst); \
            IEM_MC_ADVANCE_RIP_AND_FINISH(); \
            IEM_MC_END(); \
        } \
        else \
        { \
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm); \
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0); \
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx); \
            IEM_MC_ARG(PRTUINT128U,          puDst,                 0); \
            IEM_MC_ARG(PCRTUINT128U,         puSrc1,                1); \
            IEM_MC_ARG(PCRTUINT128U,         puSrc2,                2); \
            IEM_MC_ARG_CONST(uint8_t,        bImmArg, /*=*/ bImm,   3); \
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT(); \
            IEM_MC_PREPARE_AVX_USAGE(); \
            IEM_MC_REF_XREG_U128(puDst,        IEM_GET_MODRM_REG(pVCpu, bRm)); \
            IEM_MC_REF_XREG_U128_CONST(puSrc1, IEM_GET_EFFECTIVE_VVVV(pVCpu)); \
            IEM_MC_REF_XREG_U128_CONST(puSrc2, IEM_GET_MODRM_RM(pVCpu, bRm)); \
            IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_ ## a_Instr ## _u128, \
                                                                 iemAImpl_ ## a_Instr ## _u128_fallback), puDst, puSrc1, puSrc2, bImmArg); \
            IEM_MC_CLEAR_YREG_128_UP(          IEM_GET_MODRM_REG(pVCpu, bRm)); \
            IEM_MC_ADVANCE_RIP_AND_FINISH(); \
            IEM_MC_END(); \
        } \
    } \
    else \
    { \
        /* \
         * Register, memory. \
         */ \
        if (pVCpu->iem.s.uVexLength) \
        { \
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0); \
            IEM_MC_LOCAL(RTUINT256U,            uDst); \
            IEM_MC_LOCAL(RTUINT256U,            uSrc1); \
            IEM_MC_LOCAL(RTUINT256U,            uSrc2); \
            IEM_MC_LOCAL(RTGCPTR,               GCPtrEffSrc); \
            IEM_MC_ARG_LOCAL_REF(PRTUINT256U,   puDst,          uDst,  0); \
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc1,         uSrc1, 1); \
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc2,         uSrc2, 2); \
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1); \
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm); \
            IEM_MC_ARG_CONST(uint8_t,           bImmArg, /*=*/  bImm,  3); \
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx2); \
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT(); \
            IEM_MC_PREPARE_AVX_USAGE(); \
            IEM_MC_FETCH_MEM_U256_NO_AC(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc); \
            IEM_MC_FETCH_YREG_U256(uSrc1,      IEM_GET_EFFECTIVE_VVVV(pVCpu)); \
            IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_ ## a_Instr ## _u256, \
                                                                 iemAImpl_ ## a_Instr ## _u256_fallback), puDst, puSrc1, puSrc2, bImmArg); \
            IEM_MC_STORE_YREG_U256_ZX_VLMAX(   IEM_GET_MODRM_REG(pVCpu, bRm), uDst); \
            IEM_MC_ADVANCE_RIP_AND_FINISH(); \
            IEM_MC_END(); \
        } \
        else \
        { \
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0); \
            IEM_MC_LOCAL(RTUINT128U,                uSrc2); \
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc); \
            IEM_MC_ARG(PRTUINT128U,                 puDst,                 0); \
            IEM_MC_ARG(PCRTUINT128U,                puSrc1,                1); \
            IEM_MC_ARG_LOCAL_REF(PCRTUINT128U,      puSrc2,         uSrc2, 2); \
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 1); \
            uint8_t bImm; IEM_OPCODE_GET_NEXT_U8(&bImm); \
            IEM_MC_ARG_CONST(uint8_t,               bImmArg, /*=*/  bImm,  3); \
            IEMOP_HLP_DONE_VEX_DECODING_EX(fAvx); \
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT(); \
            IEM_MC_PREPARE_AVX_USAGE(); \
            IEM_MC_FETCH_MEM_U128_NO_AC(uSrc2, pVCpu->iem.s.iEffSeg, GCPtrEffSrc); \
            IEM_MC_REF_XREG_U128(puDst,         IEM_GET_MODRM_REG(pVCpu, bRm)); \
            IEM_MC_REF_XREG_U128_CONST(puSrc1,  IEM_GET_EFFECTIVE_VVVV(pVCpu)); \
            IEM_MC_CALL_VOID_AIMPL_4(IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_ ## a_Instr ## _u128, \
                                                                 iemAImpl_ ## a_Instr ## _u128_fallback), puDst, puSrc1, puSrc2, bImmArg); \
            IEM_MC_CLEAR_YREG_128_UP(           IEM_GET_MODRM_REG(pVCpu, bRm)); \
            IEM_MC_ADVANCE_RIP_AND_FINISH(); \
            IEM_MC_END(); \
        } \
    } \
    (void)0

/** Opcode VEX.0F 0xc6 - vshufps Vps,Hps,Wps,Ib */
FNIEMOP_DEF(iemOp_vshufps_Vps_Hps_Wps_Ib)
{
    IEMOP_MNEMONIC4(VEX_RMI, VSHUFPS, vshufps, Vpd, Hpd, Wpd, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_SKIP_PYTHON); /** @todo */
    VSHUFP_X(vshufps);
}


/** Opcode VEX.66.0F 0xc6 - vshufpd Vpd,Hpd,Wpd,Ib */
FNIEMOP_DEF(iemOp_vshufpd_Vpd_Hpd_Wpd_Ib)
{
    IEMOP_MNEMONIC4(VEX_RMI, VSHUFPD, vshufpd, Vpd, Hpd, Wpd, Ib, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_SKIP_PYTHON); /** @todo */
    VSHUFP_X(vshufpd);
}
#undef VSHUFP_X


/*  Opcode VEX.F3.0F 0xc6 - invalid */
/*  Opcode VEX.F2.0F 0xc6 - invalid */

/*  Opcode VEX.0F 0xc7 - invalid */
/*  Opcode VEX.66.0F 0xc7 - invalid */
/*  Opcode VEX.F3.0F 0xc7 - invalid */
/*  Opcode VEX.F2.0F 0xc7 - invalid */

/*  Opcode VEX.0F 0xc8 - invalid */
/*  Opcode VEX.0F 0xc9 - invalid */
/*  Opcode VEX.0F 0xca - invalid */
/*  Opcode VEX.0F 0xcb - invalid */
/*  Opcode VEX.0F 0xcc - invalid */
/*  Opcode VEX.0F 0xcd - invalid */
/*  Opcode VEX.0F 0xce - invalid */
/*  Opcode VEX.0F 0xcf - invalid */


/*  Opcode VEX.0F 0xd0 - invalid */


/** Opcode VEX.66.0F 0xd0 - vaddsubpd Vpd, Hpd, Wpd */
FNIEMOP_DEF(iemOp_vaddsubpd_Vpd_Hpd_Wpd)
{
    IEMOP_MNEMONIC3(VEX_RVM, VADDSUBPD, vaddsubpd, Vpd, Hpd, Wpd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAF3_INIT_VARS(             vaddsubpd);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0xd0 - invalid */


/** Opcode VEX.F2.0F 0xd0 - vaddsubps Vps, Hps, Wps */
FNIEMOP_DEF(iemOp_vaddsubps_Vps_Hps_Wps)
{
    IEMOP_MNEMONIC3(VEX_RVM, VADDSUBPS, vaddsubps, Vps, Hps, Wps, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAF3_INIT_VARS(             vaddsubps);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx, IEM_SELECT_HOST_OR_FALLBACK(fAvx, &s_Host, &s_Fallback));
}


/*  Opcode VEX.0F 0xd1 - invalid */


/** Opcode VEX.66.0F 0xd1 - vpsrlw Vx, Hx, W */
FNIEMOP_DEF(iemOp_vpsrlw_Vx_Hx_W)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPSRLW, vpsrlw, Vx, Hx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpsrlw);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}

/*  Opcode VEX.F3.0F 0xd1 - invalid */
/*  Opcode VEX.F2.0F 0xd1 - invalid */

/*  Opcode VEX.0F 0xd2 - invalid */
/** Opcode VEX.66.0F 0xd2 - vpsrld Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpsrld_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPSRLD, vpsrld, Vx, Hx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpsrld);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}

/*  Opcode VEX.F3.0F 0xd2 - invalid */
/*  Opcode VEX.F2.0F 0xd2 - invalid */

/*  Opcode VEX.0F 0xd3 - invalid */
/** Opcode VEX.66.0F 0xd3 - vpsrlq Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpsrlq_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPSRLQ, vpsrlq, Vx, Hx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpsrlq);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}

/*  Opcode VEX.F3.0F 0xd3 - invalid */
/*  Opcode VEX.F2.0F 0xd3 - invalid */

/*  Opcode VEX.0F 0xd4 - invalid */


/** Opcode VEX.66.0F 0xd4 - vpaddq Vx, Hx, W */
FNIEMOP_DEF(iemOp_vpaddq_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPADDQ, vpaddq, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(       vpaddq);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0xd4 - invalid */
/*  Opcode VEX.F2.0F 0xd4 - invalid */

/*  Opcode VEX.0F 0xd5 - invalid */


/** Opcode VEX.66.0F 0xd5 - vpmullw Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpmullw_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPMULLW, vpmullw, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpmullw);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0xd5 - invalid */
/*  Opcode VEX.F2.0F 0xd5 - invalid */

/*  Opcode VEX.0F 0xd6 - invalid */

/**
 * @opcode      0xd6
 * @oppfx       0x66
 * @opcpuid     avx
 * @opgroup     og_avx_pcksclr_datamove
 * @opxcpttype  none
 * @optest      op1=-1 op2=2 -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_vmovq_Wq_Vq)
{
    IEMOP_MNEMONIC2(VEX_MR, VMOVQ, vmovq, Wq_WO, Vq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES | IEMOPHINT_VEX_L_ZERO);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);

        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

        IEM_MC_COPY_YREG_U64_ZX_VLMAX(IEM_GET_MODRM_RM(pVCpu, bRm),
                                      IEM_GET_MODRM_REG(pVCpu, bRm));
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
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ();

        IEM_MC_FETCH_YREG_U64(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iQWord*/);
        IEM_MC_STORE_MEM_U64(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}

/*  Opcode VEX.F3.0F 0xd6 - invalid */
/*  Opcode VEX.F2.0F 0xd6 - invalid */


/*  Opcode VEX.0F 0xd7 - invalid */

/** Opcode VEX.66.0F 0xd7 -  */
FNIEMOP_DEF(iemOp_vpmovmskb_Gd_Ux)
{
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    /* Docs says register only. */
    if (IEM_IS_MODRM_REG_MODE(bRm)) /** @todo test that this is registers only. */
    {
        /* Note! Taking the lazy approch here wrt the high 32-bits of the GREG. */
        IEMOP_MNEMONIC2(VEX_RM_REG, VPMOVMSKB, vpmovmskb, Gd, Ux, DISOPTYPE_X86_SSE | DISOPTYPE_HARMLESS, 0);
        if (pVCpu->iem.s.uVexLength)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx2);
            IEM_MC_ARG(uint64_t *,              puDst, 0);
            IEM_MC_LOCAL(RTUINT256U,            uSrc);
            IEM_MC_ARG_LOCAL_REF(PCRTUINT256U,  puSrc, uSrc, 1);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_REF_GREG_U64(puDst,          IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_FETCH_YREG_U256(uSrc,        IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_VOID_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx2, iemAImpl_vpmovmskb_u256,
                                                                 iemAImpl_vpmovmskb_u256_fallback), puDst, puSrc);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_ARG(uint64_t *,              puDst, 0);
            IEM_MC_ARG(PCRTUINT128U,            puSrc, 1);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();
            IEM_MC_REF_GREG_U64(puDst,          IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_REF_XREG_U128_CONST(puSrc,   IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_CALL_VOID_AIMPL_2(iemAImpl_pmovmskb_u128, puDst, puSrc);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}


/*  Opcode VEX.F3.0F 0xd7 - invalid */
/*  Opcode VEX.F2.0F 0xd7 - invalid */


/*  Opcode VEX.0F 0xd8 - invalid */

/** Opcode VEX.66.0F 0xd8 - vpsubusb Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpsubusb_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPSUBUSB, vpsubusb, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpsubusb);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0xd8 - invalid */
/*  Opcode VEX.F2.0F 0xd8 - invalid */

/*  Opcode VEX.0F 0xd9 - invalid */


/** Opcode VEX.66.0F 0xd9 - vpsubusw Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpsubusw_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPSUBUSW, vpsubusw, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpsubusw);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0xd9 - invalid */
/*  Opcode VEX.F2.0F 0xd9 - invalid */

/*  Opcode VEX.0F 0xda - invalid */


/** Opcode VEX.66.0F 0xda - vpminub Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpminub_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPMINUB, vpminub, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpminub);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0xda - invalid */
/*  Opcode VEX.F2.0F 0xda - invalid */

/*  Opcode VEX.0F 0xdb - invalid */


/** Opcode VEX.66.0F 0xdb - vpand Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpand_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPAND, vpand, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &g_iemAImpl_vpand, &g_iemAImpl_vpand_fallback));
}


/*  Opcode VEX.F3.0F 0xdb - invalid */
/*  Opcode VEX.F2.0F 0xdb - invalid */

/*  Opcode VEX.0F 0xdc - invalid */


/** Opcode VEX.66.0F 0xdc - vpaddusb Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpaddusb_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPADDUSB, vpaddusb, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpaddusb);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0xdc - invalid */
/*  Opcode VEX.F2.0F 0xdc - invalid */

/*  Opcode VEX.0F 0xdd - invalid */


/** Opcode VEX.66.0F 0xdd - vpaddusw Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpaddusw_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPADDUSW, vpaddusw, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpaddusw);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0xdd - invalid */
/*  Opcode VEX.F2.0F 0xdd - invalid */

/*  Opcode VEX.0F 0xde - invalid */


/** Opcode VEX.66.0F 0xde - vpmaxub Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpmaxub_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPMAXUB, vpmaxub, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpmaxub);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0xde - invalid */
/*  Opcode VEX.F2.0F 0xde - invalid */

/*  Opcode VEX.0F 0xdf - invalid */


/** Opcode VEX.66.0F 0xdf - vpandn Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpandn_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPANDN, vpandn, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &g_iemAImpl_vpandn, &g_iemAImpl_vpandn_fallback));
}


/*  Opcode VEX.F3.0F 0xdf - invalid */
/*  Opcode VEX.F2.0F 0xdf - invalid */

/*  Opcode VEX.0F 0xe0 - invalid */


/** Opcode VEX.66.0F 0xe0 - vpavgb Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpavgb_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPAVGB, vpavgb, Vx, Hx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpavgb);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0xe0 - invalid */
/*  Opcode VEX.F2.0F 0xe0 - invalid */

/*  Opcode VEX.0F 0xe1 - invalid */
/** Opcode VEX.66.0F 0xe1 - vpsraw Vx, Hx, W */
FNIEMOP_DEF(iemOp_vpsraw_Vx_Hx_W)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPSRAW, vpsraw, Vx, Hx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpsraw);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}

/*  Opcode VEX.F3.0F 0xe1 - invalid */
/*  Opcode VEX.F2.0F 0xe1 - invalid */

/*  Opcode VEX.0F 0xe2 - invalid */
/** Opcode VEX.66.0F 0xe2 - vpsrad Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpsrad_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPSRAD, vpsrad, Vx, Hx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpsrad);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}

/*  Opcode VEX.F3.0F 0xe2 - invalid */
/*  Opcode VEX.F2.0F 0xe2 - invalid */

/*  Opcode VEX.0F 0xe3 - invalid */


/** Opcode VEX.66.0F 0xe3 - vpavgw Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpavgw_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPAVGW, vpavgw, Vx, Hx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpavgw);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0xe3 - invalid */
/*  Opcode VEX.F2.0F 0xe3 - invalid */

/*  Opcode VEX.0F 0xe4 - invalid */


/** Opcode VEX.66.0F 0xe4 - vpmulhuw Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpmulhuw_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPMULHUW, vpmulhuw, Vx, Hx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpmulhuw);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0xe4 - invalid */
/*  Opcode VEX.F2.0F 0xe4 - invalid */

/*  Opcode VEX.0F 0xe5 - invalid */


/** Opcode VEX.66.0F 0xe5 - vpmulhw Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpmulhw_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPMULHW, vpmulhw, Vx, Hx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpmulhw);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0xe5 - invalid */
/*  Opcode VEX.F2.0F 0xe5 - invalid */

/*  Opcode VEX.0F 0xe6 - invalid */


/** Opcode VEX.66.0F 0xe6 - vcvttpd2dq Vx, Wpd */
FNIEMOP_DEF(iemOp_vcvttpd2dq_Vx_Wpd)
{
    IEMOP_MNEMONIC2(VEX_RM, VCVTTPD2DQ, vcvttpd2dq, Vx, Wpd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        if (pVCpu->iem.s.uVexLength)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(        X86YMMREG,            uSrc);
            IEM_MC_FETCH_YREG_YMM(uSrc,    IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_ARG_LOCAL_REF(PCX86YMMREG,   puSrc, uSrc, 1);
            IEM_MC_LOCAL(        X86XMMREG,            uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst, uDst, 0);
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                                iemAImpl_vcvttpd2dq_u128_u256,
                                                                iemAImpl_vcvttpd2dq_u128_u256_fallback),
                                    puDst, puSrc);
            IEM_MC_STORE_XREG_XMM(   IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP(IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_ARG(          PCX86XMMREG,   puSrc,       1);
            IEM_MC_REF_XREG_XMM_CONST(          puSrc,   IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_LOCAL(        X86XMMREG,     uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst, uDst, 0);
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                                iemAImpl_vcvttpd2dq_u128_u128,
                                                                iemAImpl_vcvttpd2dq_u128_u128_fallback),
                                    puDst, puSrc);
            IEM_MC_STORE_XREG_XMM(   IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP(IEM_GET_MODRM_REG(pVCpu, bRm));
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
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(        X86YMMREG,            uSrc);
            IEM_MC_ARG_LOCAL_REF(PCX86YMMREG,   puSrc, uSrc, 1);
            IEM_MC_FETCH_MEM_YMM_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_LOCAL(        X86XMMREG,            uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst, uDst, 0);
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                                iemAImpl_vcvttpd2dq_u128_u256,
                                                                iemAImpl_vcvttpd2dq_u128_u256_fallback),
                                    puDst, puSrc);
            IEM_MC_STORE_XREG_XMM(   IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP(IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(X86XMMREG,             uSrc);
            IEM_MC_ARG_LOCAL_REF(PCX86XMMREG,   puSrc, uSrc, 1);
            IEM_MC_FETCH_MEM_XMM_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_LOCAL(        X86XMMREG,     uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst, uDst, 0);
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                                iemAImpl_vcvttpd2dq_u128_u128,
                                                                iemAImpl_vcvttpd2dq_u128_u128_fallback),
                                    puDst, puSrc);
            IEM_MC_STORE_XREG_XMM(             IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP(          IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/** Opcode VEX.F3.0F 0xe6 - vcvtdq2pd Vx, Wpd */
FNIEMOP_DEF(iemOp_vcvtdq2pd_Vx_Wpd)
{
    IEMOP_MNEMONIC2(VEX_RM, VCVTDQ2PD, vcvtdq2pd, Vpd, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        if (pVCpu->iem.s.uVexLength)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_ARG(          PCX86XMMREG,   puSrc,       1);
            IEM_MC_REF_XREG_XMM_CONST(          puSrc, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_LOCAL(        X86YMMREG,            uDst);
            IEM_MC_ARG_LOCAL_REF(PX86YMMREG,    puDst, uDst, 0);
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                                iemAImpl_vcvtdq2pd_u256_u128,
                                                                iemAImpl_vcvtdq2pd_u256_u128_fallback),
                                    puDst, puSrc);
            IEM_MC_STORE_YREG_YMM_ZX_VLMAX( IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_ARG(  const uint64_t *,      pu64Src,     1);
            IEM_MC_REF_XREG_U64_CONST(          pu64Src, IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_LOCAL(        X86XMMREG,     uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst, uDst, 0);
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                                iemAImpl_vcvtdq2pd_u128_u64,
                                                                iemAImpl_vcvtdq2pd_u128_u64_fallback),
                                    puDst, pu64Src);
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
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(X86XMMREG,             uSrc);
            IEM_MC_ARG_LOCAL_REF(PCX86XMMREG,   puSrc, uSrc, 1);
            IEM_MC_FETCH_MEM_XMM_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_LOCAL(X86YMMREG,             uDst);
            IEM_MC_ARG_LOCAL_REF(PX86YMMREG,    puDst,  uDst,  0);
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                                iemAImpl_vcvtdq2pd_u256_u128,
                                                                iemAImpl_vcvtdq2pd_u256_u128_fallback),
                                    puDst, puSrc);
            IEM_MC_STORE_YREG_YMM_ZX_VLMAX(    IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(        uint64_t,                  u64Src);
            IEM_MC_ARG_LOCAL_REF(const uint64_t *, pu64Src, u64Src, 1);
            IEM_MC_FETCH_MEM_U64(u64Src, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_LOCAL(        X86XMMREG,     uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst, uDst, 0);
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                                iemAImpl_vcvtdq2pd_u128_u64,
                                                                iemAImpl_vcvtdq2pd_u128_u64_fallback),
                                    puDst, pu64Src);
            IEM_MC_STORE_XREG_XMM(             IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP(          IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/** Opcode VEX.F2.0F 0xe6 - vcvtpd2dq Vx, Wpd */
FNIEMOP_DEF(iemOp_vcvtpd2dq_Vx_Wpd)
{
    IEMOP_MNEMONIC2(VEX_RM, VCVTPD2DQ, vcvtpd2dq, Vx, Wpd, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register.
         */
        if (pVCpu->iem.s.uVexLength)
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(        X86YMMREG,            uSrc);
            IEM_MC_FETCH_YREG_YMM(uSrc,    IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_ARG_LOCAL_REF(PCX86YMMREG,   puSrc, uSrc, 1);
            IEM_MC_LOCAL(        X86XMMREG,            uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst, uDst, 0);
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                                iemAImpl_vcvtpd2dq_u128_u256,
                                                                iemAImpl_vcvtpd2dq_u128_u256_fallback),
                                    puDst, puSrc);
            IEM_MC_STORE_XREG_XMM(   IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP(IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_ARG(          PCX86XMMREG,   puSrc,       1);
            IEM_MC_REF_XREG_XMM_CONST(          puSrc,   IEM_GET_MODRM_RM(pVCpu, bRm));
            IEM_MC_LOCAL(        X86XMMREG,     uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst, uDst, 0);
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                                iemAImpl_vcvtpd2dq_u128_u128,
                                                                iemAImpl_vcvtpd2dq_u128_u128_fallback),
                                    puDst, puSrc);
            IEM_MC_STORE_XREG_XMM(   IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP(IEM_GET_MODRM_REG(pVCpu, bRm));
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
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(        X86YMMREG,            uSrc);
            IEM_MC_ARG_LOCAL_REF(PCX86YMMREG,   puSrc, uSrc, 1);
            IEM_MC_FETCH_MEM_YMM_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_LOCAL(        X86XMMREG,            uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst, uDst, 0);
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                                iemAImpl_vcvtpd2dq_u128_u256,
                                                                iemAImpl_vcvtpd2dq_u128_u256_fallback),
                                    puDst, puSrc);
            IEM_MC_STORE_XREG_XMM(   IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP(IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);
            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_PREPARE_AVX_USAGE();

            IEM_MC_LOCAL(X86XMMREG,             uSrc);
            IEM_MC_ARG_LOCAL_REF(PCX86XMMREG,   puSrc, uSrc, 1);
            IEM_MC_FETCH_MEM_XMM_NO_AC(uSrc, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
            IEM_MC_LOCAL(        X86XMMREG,     uDst);
            IEM_MC_ARG_LOCAL_REF(PX86XMMREG,    puDst, uDst, 0);
            IEM_MC_CALL_AVX_AIMPL_2(IEM_SELECT_HOST_OR_FALLBACK(fAvx,
                                                                iemAImpl_vcvtpd2dq_u128_u128,
                                                                iemAImpl_vcvtpd2dq_u128_u128_fallback),
                                    puDst, puSrc);
            IEM_MC_STORE_XREG_XMM(             IEM_GET_MODRM_REG(pVCpu, bRm), uDst);
            IEM_MC_CLEAR_YREG_128_UP(          IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
}


/* Opcode VEX.0F 0xe7 - invalid */

/**
 * @opcode      0xe7
 * @opcodesub   !11 mr/reg
 * @oppfx       0x66
 * @opcpuid     avx
 * @opgroup     og_avx_cachect
 * @opxcpttype  1
 * @optest      op1=-1 op2=2  -> op1=2
 * @optest      op1=0 op2=-42 -> op1=-42
 */
FNIEMOP_DEF(iemOp_vmovntdq_Mx_Vx)
{
    IEMOP_MNEMONIC2(VEX_MR_MEM, VMOVNTDQ, vmovntdq, Mx_WO, Vx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES);
    Assert(pVCpu->iem.s.uVexLength <= 1);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_MEM_MODE(bRm))
    {
        if (pVCpu->iem.s.uVexLength == 0)
        {
            /*
             * 128-bit: Memory, register.
             */
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTUINT128U,                uSrc);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ();

            IEM_MC_FETCH_YREG_U128(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm), 0 /*a_iDQWord*/);
            IEM_MC_STORE_MEM_U128_ALIGN_SSE(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
        else
        {
            /*
             * 256-bit: Memory, register.
             */
            IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
            IEM_MC_LOCAL(RTUINT256U,                uSrc);
            IEM_MC_LOCAL(RTGCPTR,                   GCPtrEffSrc);

            IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
            IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
            IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
            IEM_MC_ACTUALIZE_AVX_STATE_FOR_READ();

            IEM_MC_FETCH_YREG_U256(uSrc, IEM_GET_MODRM_REG(pVCpu, bRm));
            IEM_MC_STORE_MEM_U256_ALIGN_AVX(pVCpu->iem.s.iEffSeg, GCPtrEffSrc, uSrc);

            IEM_MC_ADVANCE_RIP_AND_FINISH();
            IEM_MC_END();
        }
    }
    /**
     * @opdone
     * @opmnemonic  udvex660fe7reg
     * @opcode      0xe7
     * @opcodesub   11 mr/reg
     * @oppfx       0x66
     * @opunused    immediate
     * @opcpuid     avx
     * @optest      ->
     */
    else
        IEMOP_RAISE_INVALID_OPCODE_RET();
}

/*  Opcode VEX.F3.0F 0xe7 - invalid */
/*  Opcode VEX.F2.0F 0xe7 - invalid */


/*  Opcode VEX.0F 0xe8 - invalid */


/** Opcode VEX.66.0F 0xe8 - vpsubsb Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpsubsb_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPSUBSB, vpsubsb, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpsubsb);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0xe8 - invalid */
/*  Opcode VEX.F2.0F 0xe8 - invalid */

/*  Opcode VEX.0F 0xe9 - invalid */


/** Opcode VEX.66.0F 0xe9 - vpsubsw Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpsubsw_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPSUBSW, vpsubsw, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpsubsw);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0xe9 - invalid */
/*  Opcode VEX.F2.0F 0xe9 - invalid */

/*  Opcode VEX.0F 0xea - invalid */


/** Opcode VEX.66.0F 0xea - vpminsw Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpminsw_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPMINSW, vpminsw, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpminsw);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0xea - invalid */
/*  Opcode VEX.F2.0F 0xea - invalid */

/*  Opcode VEX.0F 0xeb - invalid */


/** Opcode VEX.66.0F 0xeb - vpor Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpor_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPOR, vpor, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &g_iemAImpl_vpor, &g_iemAImpl_vpor_fallback));
}



/*  Opcode VEX.F3.0F 0xeb - invalid */
/*  Opcode VEX.F2.0F 0xeb - invalid */

/*  Opcode VEX.0F 0xec - invalid */


/** Opcode VEX.66.0F 0xec - vpaddsb Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpaddsb_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPADDSB, vpaddsb, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpaddsb);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0xec - invalid */
/*  Opcode VEX.F2.0F 0xec - invalid */

/*  Opcode VEX.0F 0xed - invalid */


/** Opcode VEX.66.0F 0xed - vpaddsw Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpaddsw_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPADDSW, vpaddsw, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpaddsw);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0xed - invalid */
/*  Opcode VEX.F2.0F 0xed - invalid */

/*  Opcode VEX.0F 0xee - invalid */


/** Opcode VEX.66.0F 0xee - vpmaxsw Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpmaxsw_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPMAXSW, vpmaxsw, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpmaxsw);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F3.0F 0xee - invalid */
/*  Opcode VEX.F2.0F 0xee - invalid */


/*  Opcode VEX.0F 0xef - invalid */


/** Opcode VEX.66.0F 0xef - vpxor Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpxor_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPXOR, vpxor, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt,
                          IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &g_iemAImpl_vpxor, &g_iemAImpl_vpxor_fallback));
}


/*  Opcode VEX.F3.0F 0xef - invalid */
/*  Opcode VEX.F2.0F 0xef - invalid */

/*  Opcode VEX.0F 0xf0 - invalid */
/*  Opcode VEX.66.0F 0xf0 - invalid */


/** Opcode VEX.F2.0F 0xf0 - vlddqu Vx, Mx */
FNIEMOP_DEF(iemOp_vlddqu_Vx_Mx)
{
    IEMOP_MNEMONIC2(VEX_RM_MEM, VLDDQU, vlddqu, Vx_WO, Mx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES);
    Assert(pVCpu->iem.s.uVexLength <= 1);
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * Register, register - (not implemented, assuming it raises \#UD).
         */
        IEMOP_RAISE_INVALID_OPCODE_RET();
    }
    else if (pVCpu->iem.s.uVexLength == 0)
    {
        /*
         * Register, memory128.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT128U, u128Tmp);
        IEM_MC_LOCAL(RTGCPTR,    GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U128_NO_AC(u128Tmp, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_YREG_U128_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), u128Tmp);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
    else
    {
        /*
         * Register, memory256.
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEM_MC_LOCAL(RTUINT256U, u256Tmp);
        IEM_MC_LOCAL(RTGCPTR,    GCPtrEffSrc);

        IEM_MC_CALC_RM_EFF_ADDR(GCPtrEffSrc, bRm, 0);
        IEMOP_HLP_DONE_VEX_DECODING_NO_VVVV_EX(fAvx);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_ACTUALIZE_AVX_STATE_FOR_CHANGE();

        IEM_MC_FETCH_MEM_U256_NO_AC(u256Tmp, pVCpu->iem.s.iEffSeg, GCPtrEffSrc);
        IEM_MC_STORE_YREG_U256_ZX_VLMAX(IEM_GET_MODRM_REG(pVCpu, bRm), u256Tmp);

        IEM_MC_ADVANCE_RIP_AND_FINISH();
        IEM_MC_END();
    }
}


/*  Opcode VEX.0F 0xf1 - invalid */
/** Opcode VEX.66.0F 0xf1 - vpsllw Vx, Hx, W */
FNIEMOP_DEF(iemOp_vpsllw_Vx_Hx_W)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPSLLW, vpsllw, Vx, Hx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpsllw);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}

/*  Opcode VEX.F2.0F 0xf1 - invalid */

/*  Opcode VEX.0F 0xf2 - invalid */
/** Opcode VEX.66.0F 0xf2 - vpslld Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpslld_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPSLLD, vpslld, Vx, Hx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpslld);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}
/*  Opcode VEX.F2.0F 0xf2 - invalid */

/*  Opcode VEX.0F 0xf3 - invalid */
/** Opcode VEX.66.0F 0xf3 - vpsllq Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpsllq_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPSLLQ, vpsllq, Vx, Hx, Wx, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpsllq);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}
/*  Opcode VEX.F2.0F 0xf3 - invalid */

/*  Opcode VEX.0F 0xf4 - invalid */


/** Opcode VEX.66.0F 0xf4 - vpmuludq Vx, Hx, W */
FNIEMOP_DEF(iemOp_vpmuludq_Vx_Hx_W)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPMULUDQ, vpmuludq, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpmuludq);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F2.0F 0xf4 - invalid */

/*  Opcode VEX.0F 0xf5 - invalid */


/** Opcode VEX.66.0F 0xf5 - vpmaddwd Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpmaddwd_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPMADDWD, vpmaddwd, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpmaddwd);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F2.0F 0xf5 - invalid */

/*  Opcode VEX.0F 0xf6 - invalid */


/** Opcode VEX.66.0F 0xf6 - vpsadbw Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpsadbw_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPSADBW, vpsadbw, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(vpsadbw);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F2.0F 0xf6 - invalid */

/*  Opcode VEX.0F 0xf7 - invalid */


/** Opcode VEX.66.0F 0xf7 - vmaskmovdqu Vdq, Udq */
FNIEMOP_DEF(iemOp_vmaskmovdqu_Vdq_Udq)
{
//  IEMOP_MNEMONIC2(RM, VMASKMOVDQU, vmaskmovdqu, Vdq, Udq, DISOPTYPE_HARMLESS | DISOPTYPE_X86_AVX, IEMOPHINT_IGNORES_OP_SIZES); /** @todo */
    uint8_t bRm; IEM_OPCODE_GET_NEXT_U8(&bRm);
    if (IEM_IS_MODRM_REG_MODE(bRm))
    {
        /*
         * XMM, XMM, (implicit) [ ER]DI
         */
        IEM_MC_BEGIN(IEM_MC_F_NOT_286_OR_OLDER, 0);
        IEMOP_HLP_DONE_VEX_DECODING_L0_AND_NO_VVVV_EX(fAvx);
        IEM_MC_LOCAL(        uint64_t,         u64EffAddr);
        IEM_MC_LOCAL(        RTUINT128U,       u128Mem);
        IEM_MC_ARG_LOCAL_REF(PRTUINT128U,      pu128Mem, u128Mem, 0);
        IEM_MC_ARG(          PCRTUINT128U,     puSrc,             1);
        IEM_MC_ARG(          PCRTUINT128U,     puMsk,             2);
        IEM_MC_MAYBE_RAISE_AVX_RELATED_XCPT();
        IEM_MC_PREPARE_AVX_USAGE();

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


/*  Opcode VEX.F2.0F 0xf7 - invalid */

/*  Opcode VEX.0F 0xf8 - invalid */


/** Opcode VEX.66.0F 0xf8 - vpsubb Vx, Hx, W */
FNIEMOP_DEF(iemOp_vpsubb_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPSUBB, vpsubb, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(  vpsubb);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F2.0F 0xf8 - invalid */

/*  Opcode VEX.0F 0xf9 - invalid */


/** Opcode VEX.66.0F 0xf9 - vpsubw Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpsubw_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPSUBW, vpsubw, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(  vpsubw);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F2.0F 0xf9 - invalid */

/*  Opcode VEX.0F 0xfa - invalid */


/** Opcode VEX.66.0F 0xfa - vpsubd Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpsubd_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPSUBD, vpsubd, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(  vpsubd);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F2.0F 0xfa - invalid */

/*  Opcode VEX.0F 0xfb - invalid */


/** Opcode VEX.66.0F 0xfb - vpsubq Vx, Hx, W */
FNIEMOP_DEF(iemOp_vpsubq_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPSUBQ, vpsubq, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(  vpsubq);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F2.0F 0xfb - invalid */

/*  Opcode VEX.0F 0xfc - invalid */


/** Opcode VEX.66.0F 0xfc - vpaddb Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpaddb_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPADDB, vpaddb, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(  vpaddb);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F2.0F 0xfc - invalid */

/*  Opcode VEX.0F 0xfd - invalid */


/** Opcode VEX.66.0F 0xfd - vpaddw Vx, Hx, Wx */
FNIEMOP_DEF(iemOp_vpaddw_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPADDW, vpaddw, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(  vpaddw);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F2.0F 0xfd - invalid */

/*  Opcode VEX.0F 0xfe - invalid */


/** Opcode VEX.66.0F 0xfe - vpaddd Vx, Hx, W */
FNIEMOP_DEF(iemOp_vpaddd_Vx_Hx_Wx)
{
    IEMOP_MNEMONIC3(VEX_RVM, VPADDD, vpaddd, Vx, Hx, Wx, DISOPTYPE_HARMLESS, 0);
    IEMOPMEDIAOPTF3_INIT_VARS(  vpaddd);
    return FNIEMOP_CALL_1(iemOpCommonAvxAvx2_Vx_Hx_Wx_Opt, IEM_SELECT_HOST_OR_FALLBACK(fAvx2, &s_Host, &s_Fallback));
}


/*  Opcode VEX.F2.0F 0xfe - invalid */


/** Opcode **** 0x0f 0xff - UD0 */
FNIEMOP_DEF(iemOp_vud0)
{
/** @todo testcase: vud0 */
    IEMOP_MNEMONIC(vud0, "vud0");
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
 * VEX opcode map \#1.
 *
 * @sa  g_apfnTwoByteMap
 */
const PFNIEMOP g_apfnVexMap1[] =
{
    /*          no prefix,                  066h prefix                 f3h prefix,                 f2h prefix */
    /* 0x00 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x01 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x02 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x03 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x04 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x05 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x06 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x07 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x08 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x09 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x0a */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x0b */  IEMOP_X4(iemOp_vud2), /* ?? */
    /* 0x0c */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x0d */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x0e */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x0f */  IEMOP_X4(iemOp_InvalidNeedRM),

    /* 0x10 */  iemOp_vmovups_Vps_Wps,      iemOp_vmovupd_Vpd_Wpd,      iemOp_vmovss_Vss_Hss_Wss,   iemOp_vmovsd_Vsd_Hsd_Wsd,
    /* 0x11 */  iemOp_vmovups_Wps_Vps,      iemOp_vmovupd_Wpd_Vpd,      iemOp_vmovss_Wss_Hss_Vss,   iemOp_vmovsd_Wsd_Hsd_Vsd,
    /* 0x12 */  iemOp_vmovlps_Vq_Hq_Mq__vmovhlps, iemOp_vmovlpd_Vq_Hq_Mq, iemOp_vmovsldup_Vx_Wx,    iemOp_vmovddup_Vx_Wx,
    /* 0x13 */  iemOp_vmovlps_Mq_Vq,        iemOp_vmovlpd_Mq_Vq,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x14 */  iemOp_vunpcklps_Vx_Hx_Wx,   iemOp_vunpcklpd_Vx_Hx_Wx,   iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x15 */  iemOp_vunpckhps_Vx_Hx_Wx,   iemOp_vunpckhpd_Vx_Hx_Wx,   iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x16 */  iemOp_vmovhps_Vdq_Hq_Mq__vmovlhps_Vdq_Hq_Uq, iemOp_vmovhpd_Vdq_Hq_Mq, iemOp_vmovshdup_Vx_Wx, iemOp_InvalidNeedRM,
    /* 0x17 */  iemOp_vmovhps_Mq_Vq,        iemOp_vmovhpd_Mq_Vq,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x18 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x19 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x1a */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x1b */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x1c */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x1d */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x1e */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x1f */  IEMOP_X4(iemOp_InvalidNeedRM),

    /* 0x20 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x21 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x22 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x23 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x24 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x25 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x26 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x27 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x28 */  iemOp_vmovaps_Vps_Wps,      iemOp_vmovapd_Vpd_Wpd,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x29 */  iemOp_vmovaps_Wps_Vps,      iemOp_vmovapd_Wpd_Vpd,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x2a */  iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,        iemOp_vcvtsi2ss_Vss_Hss_Ey, iemOp_vcvtsi2sd_Vsd_Hsd_Ey,
    /* 0x2b */  iemOp_vmovntps_Mps_Vps,     iemOp_vmovntpd_Mpd_Vpd,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x2c */  iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,        iemOp_vcvttss2si_Gy_Wss,    iemOp_vcvttsd2si_Gy_Wsd,
    /* 0x2d */  iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,        iemOp_vcvtss2si_Gy_Wss,     iemOp_vcvtsd2si_Gy_Wsd,
    /* 0x2e */  iemOp_vucomiss_Vss_Wss,     iemOp_vucomisd_Vsd_Wsd,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x2f */  iemOp_vcomiss_Vss_Wss,      iemOp_vcomisd_Vsd_Wsd,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,

    /* 0x30 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x31 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x32 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x33 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x34 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x35 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x36 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x37 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x38 */  IEMOP_X4(iemOp_InvalidNeedRM),  /** @todo check that there is no escape table stuff here */
    /* 0x39 */  IEMOP_X4(iemOp_InvalidNeedRM),  /** @todo check that there is no escape table stuff here */
    /* 0x3a */  IEMOP_X4(iemOp_InvalidNeedRM),  /** @todo check that there is no escape table stuff here */
    /* 0x3b */  IEMOP_X4(iemOp_InvalidNeedRM),  /** @todo check that there is no escape table stuff here */
    /* 0x3c */  IEMOP_X4(iemOp_InvalidNeedRM),  /** @todo check that there is no escape table stuff here */
    /* 0x3d */  IEMOP_X4(iemOp_InvalidNeedRM),  /** @todo check that there is no escape table stuff here */
    /* 0x3e */  IEMOP_X4(iemOp_InvalidNeedRM),  /** @todo check that there is no escape table stuff here */
    /* 0x3f */  IEMOP_X4(iemOp_InvalidNeedRM),  /** @todo check that there is no escape table stuff here */

    /* 0x40 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x41 */  IEMOP_X4(iemOp_InvalidNeedRM),
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

    /* 0x50 */  iemOp_vmovmskps_Gy_Ups,     iemOp_vmovmskpd_Gy_Upd,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x51 */  iemOp_vsqrtps_Vps_Wps,      iemOp_vsqrtpd_Vpd_Wpd,      iemOp_vsqrtss_Vss_Hss_Wss,  iemOp_vsqrtsd_Vsd_Hsd_Wsd,
    /* 0x52 */  iemOp_vrsqrtps_Vps_Wps,     iemOp_InvalidNeedRM,        iemOp_vrsqrtss_Vss_Hss_Wss, iemOp_InvalidNeedRM,
    /* 0x53 */  iemOp_vrcpps_Vps_Wps,       iemOp_InvalidNeedRM,        iemOp_vrcpss_Vss_Hss_Wss,   iemOp_InvalidNeedRM,
    /* 0x54 */  iemOp_vandps_Vps_Hps_Wps,   iemOp_vandpd_Vpd_Hpd_Wpd,   iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x55 */  iemOp_vandnps_Vps_Hps_Wps,  iemOp_vandnpd_Vpd_Hpd_Wpd,  iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x56 */  iemOp_vorps_Vps_Hps_Wps,    iemOp_vorpd_Vpd_Hpd_Wpd,    iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x57 */  iemOp_vxorps_Vps_Hps_Wps,   iemOp_vxorpd_Vpd_Hpd_Wpd,   iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x58 */  iemOp_vaddps_Vps_Hps_Wps,   iemOp_vaddpd_Vpd_Hpd_Wpd,   iemOp_vaddss_Vss_Hss_Wss,   iemOp_vaddsd_Vsd_Hsd_Wsd,
    /* 0x59 */  iemOp_vmulps_Vps_Hps_Wps,   iemOp_vmulpd_Vpd_Hpd_Wpd,   iemOp_vmulss_Vss_Hss_Wss,   iemOp_vmulsd_Vsd_Hsd_Wsd,
    /* 0x5a */  iemOp_vcvtps2pd_Vpd_Wps,    iemOp_vcvtpd2ps_Vps_Wpd,    iemOp_vcvtss2sd_Vsd_Hx_Wss, iemOp_vcvtsd2ss_Vss_Hx_Wsd,
    /* 0x5b */  iemOp_vcvtdq2ps_Vps_Wdq,    iemOp_vcvtps2dq_Vdq_Wps,    iemOp_vcvttps2dq_Vdq_Wps,   iemOp_InvalidNeedRM,
    /* 0x5c */  iemOp_vsubps_Vps_Hps_Wps,   iemOp_vsubpd_Vpd_Hpd_Wpd,   iemOp_vsubss_Vss_Hss_Wss,   iemOp_vsubsd_Vsd_Hsd_Wsd,
    /* 0x5d */  iemOp_vminps_Vps_Hps_Wps,   iemOp_vminpd_Vpd_Hpd_Wpd,   iemOp_vminss_Vss_Hss_Wss,   iemOp_vminsd_Vsd_Hsd_Wsd,
    /* 0x5e */  iemOp_vdivps_Vps_Hps_Wps,   iemOp_vdivpd_Vpd_Hpd_Wpd,   iemOp_vdivss_Vss_Hss_Wss,   iemOp_vdivsd_Vsd_Hsd_Wsd,
    /* 0x5f */  iemOp_vmaxps_Vps_Hps_Wps,   iemOp_vmaxpd_Vpd_Hpd_Wpd,   iemOp_vmaxss_Vss_Hss_Wss,   iemOp_vmaxsd_Vsd_Hsd_Wsd,

    /* 0x60 */  iemOp_InvalidNeedRM,        iemOp_vpunpcklbw_Vx_Hx_Wx,  iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x61 */  iemOp_InvalidNeedRM,        iemOp_vpunpcklwd_Vx_Hx_Wx,  iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x62 */  iemOp_InvalidNeedRM,        iemOp_vpunpckldq_Vx_Hx_Wx,  iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x63 */  iemOp_InvalidNeedRM,        iemOp_vpacksswb_Vx_Hx_Wx,   iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x64 */  iemOp_InvalidNeedRM,        iemOp_vpcmpgtb_Vx_Hx_Wx,    iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x65 */  iemOp_InvalidNeedRM,        iemOp_vpcmpgtw_Vx_Hx_Wx,    iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x66 */  iemOp_InvalidNeedRM,        iemOp_vpcmpgtd_Vx_Hx_Wx,    iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x67 */  iemOp_InvalidNeedRM,        iemOp_vpackuswb_Vx_Hx_W,    iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x68 */  iemOp_InvalidNeedRM,        iemOp_vpunpckhbw_Vx_Hx_Wx,  iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x69 */  iemOp_InvalidNeedRM,        iemOp_vpunpckhwd_Vx_Hx_Wx,  iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x6a */  iemOp_InvalidNeedRM,        iemOp_vpunpckhdq_Vx_Hx_W,   iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x6b */  iemOp_InvalidNeedRM,        iemOp_vpackssdw_Vx_Hx_Wx,   iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x6c */  iemOp_InvalidNeedRM,        iemOp_vpunpcklqdq_Vx_Hx_Wx, iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x6d */  iemOp_InvalidNeedRM,        iemOp_vpunpckhqdq_Vx_Hx_W,  iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x6e */  iemOp_InvalidNeedRM,        iemOp_vmovd_q_Vy_Ey,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x6f */  iemOp_InvalidNeedRM,        iemOp_vmovdqa_Vx_Wx,        iemOp_vmovdqu_Vx_Wx,        iemOp_InvalidNeedRM,

    /* 0x70 */  iemOp_InvalidNeedRM,        iemOp_vpshufd_Vx_Wx_Ib,     iemOp_vpshufhw_Vx_Wx_Ib,    iemOp_vpshuflw_Vx_Wx_Ib,
    /* 0x71 */  iemOp_InvalidNeedRM,        iemOp_VGrp12,               iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x72 */  iemOp_InvalidNeedRM,        iemOp_VGrp13,               iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x73 */  iemOp_InvalidNeedRM,        iemOp_VGrp14,               iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x74 */  iemOp_InvalidNeedRM,        iemOp_vpcmpeqb_Vx_Hx_Wx,    iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x75 */  iemOp_InvalidNeedRM,        iemOp_vpcmpeqw_Vx_Hx_Wx,    iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x76 */  iemOp_InvalidNeedRM,        iemOp_vpcmpeqd_Vx_Hx_Wx,    iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0x77 */  iemOp_vzeroupperv__vzeroallv, iemOp_InvalidNeedRM,      iemOp_InvalidNeedRM,       iemOp_InvalidNeedRM,
    /* 0x78 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x79 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x7a */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x7b */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x7c */  iemOp_InvalidNeedRM,        iemOp_vhaddpd_Vpd_Hpd_Wpd,  iemOp_InvalidNeedRM,        iemOp_vhaddps_Vps_Hps_Wps,
    /* 0x7d */  iemOp_InvalidNeedRM,        iemOp_vhsubpd_Vpd_Hpd_Wpd,  iemOp_InvalidNeedRM,        iemOp_vhsubps_Vps_Hps_Wps,
    /* 0x7e */  iemOp_InvalidNeedRM,        iemOp_vmovd_q_Ey_Vy,        iemOp_vmovq_Vq_Wq,          iemOp_InvalidNeedRM,
    /* 0x7f */  iemOp_InvalidNeedRM,        iemOp_vmovdqa_Wx_Vx,        iemOp_vmovdqu_Wx_Vx,        iemOp_InvalidNeedRM,

    /* 0x80 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x81 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0x82 */  IEMOP_X4(iemOp_InvalidNeedRM),
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
    /* 0xae */  IEMOP_X4(iemOp_VGrp15),
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
    /* 0xc2 */  iemOp_vcmpps_Vps_Hps_Wps_Ib, iemOp_vcmppd_Vpd_Hpd_Wpd_Ib, iemOp_vcmpss_Vss_Hss_Wss_Ib, iemOp_vcmpsd_Vsd_Hsd_Wsd_Ib,
    /* 0xc3 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xc4 */  iemOp_InvalidNeedRM,        iemOp_vpinsrw_Vdq_Hdq_RyMw_Ib, iemOp_InvalidNeedRMImm8, iemOp_InvalidNeedRMImm8,
    /* 0xc5 */  iemOp_InvalidNeedRM,        iemOp_vpextrw_Gd_Udq_Ib,       iemOp_InvalidNeedRMImm8,    iemOp_InvalidNeedRMImm8,
    /* 0xc6 */  iemOp_vshufps_Vps_Hps_Wps_Ib, iemOp_vshufpd_Vpd_Hpd_Wpd_Ib, iemOp_InvalidNeedRMImm8,iemOp_InvalidNeedRMImm8,
    /* 0xc7 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xc8 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xc9 */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xca */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xcb */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xcc */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xcd */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xce */  IEMOP_X4(iemOp_InvalidNeedRM),
    /* 0xcf */  IEMOP_X4(iemOp_InvalidNeedRM),

    /* 0xd0 */  iemOp_InvalidNeedRM,        iemOp_vaddsubpd_Vpd_Hpd_Wpd, iemOp_InvalidNeedRM,       iemOp_vaddsubps_Vps_Hps_Wps,
    /* 0xd1 */  iemOp_InvalidNeedRM,        iemOp_vpsrlw_Vx_Hx_W,       iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xd2 */  iemOp_InvalidNeedRM,        iemOp_vpsrld_Vx_Hx_Wx,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xd3 */  iemOp_InvalidNeedRM,        iemOp_vpsrlq_Vx_Hx_Wx,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xd4 */  iemOp_InvalidNeedRM,        iemOp_vpaddq_Vx_Hx_Wx,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xd5 */  iemOp_InvalidNeedRM,        iemOp_vpmullw_Vx_Hx_Wx,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xd6 */  iemOp_InvalidNeedRM,        iemOp_vmovq_Wq_Vq,          iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xd7 */  iemOp_InvalidNeedRM,        iemOp_vpmovmskb_Gd_Ux,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xd8 */  iemOp_InvalidNeedRM,        iemOp_vpsubusb_Vx_Hx_Wx,    iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xd9 */  iemOp_InvalidNeedRM,        iemOp_vpsubusw_Vx_Hx_Wx,    iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xda */  iemOp_InvalidNeedRM,        iemOp_vpminub_Vx_Hx_Wx,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xdb */  iemOp_InvalidNeedRM,        iemOp_vpand_Vx_Hx_Wx,       iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xdc */  iemOp_InvalidNeedRM,        iemOp_vpaddusb_Vx_Hx_Wx,    iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xdd */  iemOp_InvalidNeedRM,        iemOp_vpaddusw_Vx_Hx_Wx,    iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xde */  iemOp_InvalidNeedRM,        iemOp_vpmaxub_Vx_Hx_Wx,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xdf */  iemOp_InvalidNeedRM,        iemOp_vpandn_Vx_Hx_Wx,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,

    /* 0xe0 */  iemOp_InvalidNeedRM,        iemOp_vpavgb_Vx_Hx_Wx,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xe1 */  iemOp_InvalidNeedRM,        iemOp_vpsraw_Vx_Hx_W,       iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xe2 */  iemOp_InvalidNeedRM,        iemOp_vpsrad_Vx_Hx_Wx,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xe3 */  iemOp_InvalidNeedRM,        iemOp_vpavgw_Vx_Hx_Wx,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xe4 */  iemOp_InvalidNeedRM,        iemOp_vpmulhuw_Vx_Hx_Wx,    iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xe5 */  iemOp_InvalidNeedRM,        iemOp_vpmulhw_Vx_Hx_Wx,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xe6 */  iemOp_InvalidNeedRM,        iemOp_vcvttpd2dq_Vx_Wpd,    iemOp_vcvtdq2pd_Vx_Wpd,     iemOp_vcvtpd2dq_Vx_Wpd,
    /* 0xe7 */  iemOp_InvalidNeedRM,        iemOp_vmovntdq_Mx_Vx,       iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xe8 */  iemOp_InvalidNeedRM,        iemOp_vpsubsb_Vx_Hx_Wx,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xe9 */  iemOp_InvalidNeedRM,        iemOp_vpsubsw_Vx_Hx_Wx,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xea */  iemOp_InvalidNeedRM,        iemOp_vpminsw_Vx_Hx_Wx,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xeb */  iemOp_InvalidNeedRM,        iemOp_vpor_Vx_Hx_Wx,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xec */  iemOp_InvalidNeedRM,        iemOp_vpaddsb_Vx_Hx_Wx,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xed */  iemOp_InvalidNeedRM,        iemOp_vpaddsw_Vx_Hx_Wx,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xee */  iemOp_InvalidNeedRM,        iemOp_vpmaxsw_Vx_Hx_Wx,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xef */  iemOp_InvalidNeedRM,        iemOp_vpxor_Vx_Hx_Wx,       iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,

    /* 0xf0 */  iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,        iemOp_vlddqu_Vx_Mx,
    /* 0xf1 */  iemOp_InvalidNeedRM,        iemOp_vpsllw_Vx_Hx_W,       iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xf2 */  iemOp_InvalidNeedRM,        iemOp_vpslld_Vx_Hx_Wx,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xf3 */  iemOp_InvalidNeedRM,        iemOp_vpsllq_Vx_Hx_Wx,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xf4 */  iemOp_InvalidNeedRM,        iemOp_vpmuludq_Vx_Hx_W,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xf5 */  iemOp_InvalidNeedRM,        iemOp_vpmaddwd_Vx_Hx_Wx,    iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xf6 */  iemOp_InvalidNeedRM,        iemOp_vpsadbw_Vx_Hx_Wx,     iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xf7 */  iemOp_InvalidNeedRM,        iemOp_vmaskmovdqu_Vdq_Udq,  iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xf8 */  iemOp_InvalidNeedRM,        iemOp_vpsubb_Vx_Hx_Wx,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xf9 */  iemOp_InvalidNeedRM,        iemOp_vpsubw_Vx_Hx_Wx,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xfa */  iemOp_InvalidNeedRM,        iemOp_vpsubd_Vx_Hx_Wx,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xfb */  iemOp_InvalidNeedRM,        iemOp_vpsubq_Vx_Hx_Wx,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xfc */  iemOp_InvalidNeedRM,        iemOp_vpaddb_Vx_Hx_Wx,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xfd */  iemOp_InvalidNeedRM,        iemOp_vpaddw_Vx_Hx_Wx,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xfe */  iemOp_InvalidNeedRM,        iemOp_vpaddd_Vx_Hx_Wx,      iemOp_InvalidNeedRM,        iemOp_InvalidNeedRM,
    /* 0xff */  IEMOP_X4(iemOp_vud0) /* ?? */
};
AssertCompile(RT_ELEMENTS(g_apfnVexMap1) == 1024);
/** @}  */

