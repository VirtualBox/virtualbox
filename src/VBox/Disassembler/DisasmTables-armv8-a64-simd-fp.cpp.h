/* $Id: DisasmTables-armv8-a64-simd-fp.cpp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox disassembler - Tables for ARMv8 A64 - SIMD & FP.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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

/*
 * C4.1.96 - Data Processing - Scalar Floating-Point and Advanced SIMD
 *
 * Bit 28 (op0<0>) is already fixed at 0 at this point.
 *
 * Differentiate further based on the op3<0> field.
 * Splitting this up because the decoding would get insane otherwise with tables doing cross referencing...
 *
 *     Bit  10
 *     +-------------------------------------------
 *           0 Advanced SIMD table lookup/permute/extract/copy/three same (FP16)/two-register miscellaneous (FP16)/ three-register extension
 *                           two-register miscellaneous/across lanes/three different/three same/modified immediate/shift by immediate/vector x indexed element/
 *             Cryptographic AES
 *           1 Cryptographic three-register, imm2/three register SHA 512/four-register/two-register SHA 512
 *             XAR
 */
DIS_ARMV8_DECODE_MAP_DEFINE_BEGIN(DataProcSimdFpBit28_0_31_0)
    DIS_ARMV8_DECODE_MAP_INVALID_ENTRY,                 /** @todo */
    DIS_ARMV8_DECODE_MAP_INVALID_ENTRY,                 /** @todo */
DIS_ARMV8_DECODE_MAP_DEFINE_END_SINGLE_BIT(DataProcSimdFpBit28_0_31_0, 10);


/*
 * C4.1.96 - Data Processing - Scalar Floating-Point and Advanced SIMD
 *
 * Bit 28 (op0<0>) is already fixed at 0 at this point.
 *
 * Differentiate further based on the op0<3> field.
 * Splitting this up because the decoding would get insane otherwise with tables doing cross referencing...
 *
 *     Bit  31
 *     +-------------------------------------------
 *           0 Advanced SIMD table lookup/permute/extract/copy/three same (FP16)/two-register miscellaneous (FP16)/ three-register extension
 *                           two-register miscellaneous/across lanes/three different/three same/modified immediate/shift by immediate/vector x indexed element/
 *             Cryptographic AES
 *           1 Cryptographic three-register, imm2/three register SHA 512/four-register/two-register SHA 512
 *             XAR
 */
DIS_ARMV8_DECODE_MAP_DEFINE_BEGIN(DataProcSimdFpBit28_0)
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcSimdFpBit28_0_31_0),
    DIS_ARMV8_DECODE_MAP_INVALID_ENTRY,                     /** @todo */
DIS_ARMV8_DECODE_MAP_DEFINE_END_SINGLE_BIT(DataProcSimdFpBit28_0, 31);


/*
 * SCVTF/UCVTF.
 *
 * Note: The opcode is selected based on the <opcode> field.
 */
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_BEGIN(DataProcFpFixedPConvGpr2FpReg)
    INVALID_OPCODE,
    INVALID_OPCODE,
    DIS_ARMV8_OP(0x1e020000, "scvtf",           OP_ARMV8_A64_SCVTF,     DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e030000, "ucvtf",           OP_ARMV8_A64_UCVTF,     DISOPTYPE_HARMLESS),
    INVALID_OPCODE,
    INVALID_OPCODE,
    INVALID_OPCODE,
    INVALID_OPCODE,
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_DECODER(DataProcFpFixedPConvGpr2FpReg)
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpType,         22,  2, DIS_ARMV8_INSN_PARAM_UNSET),
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpReg,           0,  5, 0 /*idxParam*/),
    DIS_ARMV8_INSN_DECODE(kDisParmParseReg,             5,  5, 1 /*idxParam*/),
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpScale,        10,  6, 2 /*idxParam*/),
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_END_PARAMS_3(DataProcFpFixedPConvGpr2FpReg, 0x7f3f0000 /*fFixedInsn*/, DISARMV8INSNCLASS_F_SF /*fClass*/,
                                                kDisArmV8OpcDecodeNop,
                                                RT_BIT_32(16) | RT_BIT_32(17) | RT_BIT_32(18), 16,
                                                kDisArmv8OpParmReg, kDisArmv8OpParmReg, kDisArmv8OpParmImm);


/*
 * FCVTZS/FCVTZU.
 *
 * Note: The opcode is selected based on the <opcode> field.
 */
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_BEGIN(DataProcFpFixedPConvFpReg2Gpr)
    DIS_ARMV8_OP(0x1e180000, "fcvtzs",          OP_ARMV8_A64_FCVTZS,    DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e190000, "fcvtzu",          OP_ARMV8_A64_FCVTZU,    DISOPTYPE_HARMLESS),
    INVALID_OPCODE,
    INVALID_OPCODE,
    INVALID_OPCODE,
    INVALID_OPCODE,
    INVALID_OPCODE,
    INVALID_OPCODE,
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_DECODER(DataProcFpFixedPConvFpReg2Gpr)
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpType,         22,  2, DIS_ARMV8_INSN_PARAM_UNSET),
    DIS_ARMV8_INSN_DECODE(kDisParmParseReg,             0,  5, 0 /*idxParam*/),
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpReg,           5,  5, 1 /*idxParam*/),
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpScale,        10,  6, 2 /*idxParam*/),
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_END_PARAMS_3(DataProcFpFixedPConvFpReg2Gpr, 0x7f3f0000 /*fFixedInsn*/, DISARMV8INSNCLASS_F_SF /*fClass*/,
                                                kDisArmV8OpcDecodeNop,
                                                RT_BIT_32(16) | RT_BIT_32(17) | RT_BIT_32(18), 16,
                                                kDisArmv8OpParmReg, kDisArmv8OpParmReg, kDisArmv8OpParmImm);


/*
 * C4.1.96.32 - Conversion between floating-point and fixed-point
 *
 * Bit 28 (op0<0>) is already fixed at 1 at this point.
 * Bit 30 (op0<2>) is already fixed at 0 at this point.
 * Bit 24 (op1<1>) is already fixed at 0 at this point.
 * Bit 21 (op2<2>) is already fixed at 0 at this point.
 *
 * Differentiate further based on the rmode field.
 */
DIS_ARMV8_DECODE_MAP_DEFINE_BEGIN(DataProcFpFixedPConv)
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcFpFixedPConvGpr2FpReg),
    DIS_ARMV8_DECODE_MAP_INVALID_ENTRY,
    DIS_ARMV8_DECODE_MAP_INVALID_ENTRY,
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcFpFixedPConvFpReg2Gpr),
DIS_ARMV8_DECODE_MAP_DEFINE_END(DataProcFpFixedPConv, RT_BIT_32(19) | RT_BIT_32(20), 19);


/*
 * C4.1.96.33 - Conversion between floating-point and integer.
 *
 * FCVTNS/FCVTNU/SCVTF/UCVTF/FCVTAS/FCVTAU/FMOV/FCVTPS/FCVTPU/FCVTMS/FCVTMU/FCVTZS/FCVTZU.
 *
 * Note: The opcode is selected based on the <rmode>:<opcode> field.
 */
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_BEGIN(DataProcFpConvInt) /** @todo */
    INVALID_OPCODE,
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_DECODER(DataProcFpConvInt)
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpType,         22,  2, DIS_ARMV8_INSN_PARAM_UNSET),
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpReg,           0,  5, 0 /*idxParam*/),
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpReg,           5,  5, 1 /*idxParam*/),
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_END_PARAMS_2(DataProcFpConvInt, 0xff3ffc00 /*fFixedInsn*/, DISARMV8INSNCLASS_F_SF /*fClass*/,
                                                kDisArmV8OpcDecodeNop,
                                                RT_BIT_32(16) | RT_BIT_32(17) | RT_BIT_32(18) | RT_BIT_32(19) | RT_BIT_32(20), 16,
                                                kDisArmv8OpParmReg, kDisArmv8OpParmReg);


/*
 * FCSEL.
 *
 * Note: The opcode is selected based on the <opcode> field.
 */
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_BEGIN(DataProcFpCondSelect)
    DIS_ARMV8_OP(0x1e200c00, "fcsel",           OP_ARMV8_A64_FCSEL,     DISOPTYPE_HARMLESS),
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_DECODER(DataProcFpCondSelect)
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpType,         22,  2, DIS_ARMV8_INSN_PARAM_UNSET),
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpReg,           0,  5, 0 /*idxParam*/),
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpReg,           5,  5, 1 /*idxParam*/),
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpReg,          16,  5, 2 /*idxParam*/),
    DIS_ARMV8_INSN_DECODE(kDisParmParseCond,           12,  4, 3 /*idxParam*/),
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_END_PARAMS_4(DataProcFpCondSelect, 0xff200c00 /*fFixedInsn*/, 0 /*fClass*/,
                                                kDisArmV8OpcDecodeNop,
                                                RT_BIT_32(29), 29,
                                                kDisArmv8OpParmReg, kDisArmv8OpParmReg, kDisArmv8OpParmReg, kDisArmv8OpParmCond);


/*
 * FMUL/FDIV/FADD/FSUB/FMAX/FMIN/FMAXNM/FMINNM.
 *
 * Note: The opcode is selected based on the <opcode> field.
 */
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_BEGIN(DataProcFpDataProc2Src)
    DIS_ARMV8_OP(0x1e200800, "fmul",            OP_ARMV8_A64_FMUL,      DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e201800, "fdiv",            OP_ARMV8_A64_FDIV,      DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e202800, "fadd",            OP_ARMV8_A64_FADD,      DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e203800, "fsub",            OP_ARMV8_A64_FSUB,      DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e204800, "fmax",            OP_ARMV8_A64_FMAX,      DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e205800, "fmin",            OP_ARMV8_A64_FMIN,      DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e206800, "fmaxnm",          OP_ARMV8_A64_FMAXNM,    DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e207800, "fminnm",          OP_ARMV8_A64_FMINNM,    DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e208800, "fnmul",           OP_ARMV8_A64_FNMUL,     DISOPTYPE_HARMLESS),
    /* Rest of the 4 bit block is invalid */
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_DECODER(DataProcFpDataProc2Src)
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpType,         22,  2, DIS_ARMV8_INSN_PARAM_UNSET),
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpReg,           0,  5, 0 /*idxParam*/),
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpReg,           5,  5, 1 /*idxParam*/),
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpReg,          16,  5, 2 /*idxParam*/),
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_END_PARAMS_3(DataProcFpDataProc2Src, 0xff20fc00 /*fFixedInsn*/, 0 /*fClass*/,
                                                kDisArmV8OpcDecodeNop,
                                                RT_BIT_32(12) | RT_BIT_32(13) | RT_BIT_32(14) | RT_BIT_32(15), 12,
                                                kDisArmv8OpParmReg, kDisArmv8OpParmReg, kDisArmv8OpParmReg);


/*
 * C4.1.96.34 - Floating-point data-processing (1 source).
 *
 * FMOV/FABS/FNEG/FSQRT/FCVT/FRINTN/FRINTP/FRINTM/FRINTZ/FRINA/FRINTX/FRINTI/FRINT32Z/FRINT32X/FRINT64Z/FRINT64X.
 *
 * Note: The opcode is selected based on the <opcode> field.
 */
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_BEGIN(DataProcFpDataProc1Src)
    DIS_ARMV8_OP(0x1e204000, "fmov",            OP_ARMV8_A64_FMOV,      DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e20c000, "fabs",            OP_ARMV8_A64_FABS,      DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e214000, "fneg",            OP_ARMV8_A64_FNEG,      DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e21c000, "fsqrt",           OP_ARMV8_A64_FSQRT,     DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e224000, "fcvt",            OP_ARMV8_A64_FCVT,      DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e22c000, "fcvt",            OP_ARMV8_A64_FCVT,      DISOPTYPE_HARMLESS),
    INVALID_OPCODE,
    DIS_ARMV8_OP(0x1e23c000, "fcvt",            OP_ARMV8_A64_FCVT,      DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e244000, "frintn",          OP_ARMV8_A64_FRINTN,    DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e24c000, "frintp",          OP_ARMV8_A64_FRINTP,    DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e254000, "frintm",          OP_ARMV8_A64_FRINTM,    DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e25c000, "frintz",          OP_ARMV8_A64_FRINTZ,    DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e264000, "frinta",          OP_ARMV8_A64_FRINTA,    DISOPTYPE_HARMLESS),
    INVALID_OPCODE,
    DIS_ARMV8_OP(0x1e274000, "frintx",          OP_ARMV8_A64_FRINTX,    DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e27c000, "frinti",          OP_ARMV8_A64_FRINTI,    DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e284000, "frint32z",        OP_ARMV8_A64_FRINT32Z,  DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e28c000, "frint32x",        OP_ARMV8_A64_FRINT32X,  DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e294000, "frint64z",        OP_ARMV8_A64_FRINT64Z,  DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e29c000, "frint64x",        OP_ARMV8_A64_FRINT64X,  DISOPTYPE_HARMLESS),
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_DECODER(DataProcFpDataProc1Src)
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpType,         22,  2, DIS_ARMV8_INSN_PARAM_UNSET),
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpReg,           0,  5, 0 /*idxParam*/),
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpReg,           5,  5, 1 /*idxParam*/),
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpFixupFCvt,     0,  0, DIS_ARMV8_INSN_PARAM_UNSET),
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_END_PARAMS_2(DataProcFpDataProc1Src, 0xff3ffc00 /*fFixedInsn*/, 0 /*fClass*/,
                                                kDisArmV8OpcDecodeNop,
                                                RT_BIT_32(15) | RT_BIT_32(16) | RT_BIT_32(17) | RT_BIT_32(18) | RT_BIT_32(19) | RT_BIT_32(20), 15,
                                                kDisArmv8OpParmReg, kDisArmv8OpParmReg);


/*
 * C4.1.96.35 - Floating-point compare.
 *
 * FCMP/FCMPE.
 *
 * Note: The opcode is selected based on the op2<3:4> field.
 */
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_BEGIN(DataProcFpCmpReg)
    DIS_ARMV8_OP(0x1e202000, "fcmp",            OP_ARMV8_A64_FCMP,      DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e202010, "fcmpe",           OP_ARMV8_A64_FCMPE,     DISOPTYPE_HARMLESS),
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_DECODER(DataProcFpCmpReg)
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpType,         22,  2, DIS_ARMV8_INSN_PARAM_UNSET),
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpReg,           5,  5, 0 /*idxParam*/),
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpReg,          16,  5, 1 /*idxParam*/),
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_END_PARAMS_2(DataProcFpCmpReg, 0xff20fc1f /*fFixedInsn*/, 0 /*fClass*/,
                                                kDisArmV8OpcDecodeNop,
                                                RT_BIT_32(4), 4,
                                                kDisArmv8OpParmReg, kDisArmv8OpParmReg);


/*
 * C4.1.96.35 - Floating-point compare.
 *
 * FCMP/FCMPE.
 *
 * Note: The opcode is selected based on the op2<3:4> field.
 */
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_BEGIN(DataProcFpCmpZero)
    DIS_ARMV8_OP(0x1e202008, "fcmp",            OP_ARMV8_A64_FCMP,      DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e202018, "fcmpe",           OP_ARMV8_A64_FCMPE,     DISOPTYPE_HARMLESS),
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_DECODER(DataProcFpCmpZero)
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpType,         22,  2, DIS_ARMV8_INSN_PARAM_UNSET),
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpReg,           5,  5, 0 /*idxParam*/),
    DIS_ARMV8_INSN_DECODE(kDisParmParseImmZero,         0,  0, 1 /*idxParam*/),
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_END_PARAMS_2(DataProcFpCmpZero, 0xff20fc1f /*fFixedInsn*/, 0 /*fClass*/,
                                                kDisArmV8OpcDecodeNop,
                                                RT_BIT_32(4), 4,
                                                kDisArmv8OpParmReg, kDisArmv8OpParmImm);


/*
 * Floating Point compare, differentiate between register and zero variant.
 */
DIS_ARMV8_DECODE_MAP_DEFINE_BEGIN(DataProcFpCmp)
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcFpCmpReg),
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcFpCmpZero),
DIS_ARMV8_DECODE_MAP_DEFINE_END_SINGLE_BIT(DataProcFpCmp, 3);


/*
 * C4.1.96.36 - Floating-point immediate.
 *
 * FMOV.
 *
 * Note: The opcode is selected based on the <op> field.
 */
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_BEGIN(DataProcFpImm)
    DIS_ARMV8_OP(0x1e201000, "fmov",            OP_ARMV8_A64_FMOV,      DISOPTYPE_HARMLESS),
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_DECODER(DataProcFpImm)
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpType,         22,  2, DIS_ARMV8_INSN_PARAM_UNSET),
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpReg,           0,  5, 0 /*idxParam*/),
    DIS_ARMV8_INSN_DECODE(kDisParmParseImm,            13,  8, 1 /*idxParam*/),
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_END_PARAMS_2(DataProcFpImm, 0xff201fe0 /*fFixedInsn*/, 0 /*fClass*/,
                                                kDisArmV8OpcDecodeNop,
                                                RT_BIT_32(5) | RT_BIT_32(6) | RT_BIT_32(7) | RT_BIT_32(8) | RT_BIT_32(9), 5,
                                                kDisArmv8OpParmReg, kDisArmv8OpParmImm);


/*
 * C4.1.96.37 - Floating-point conditional compare.
 *
 * FCCMP/FCCMPE.
 *
 * Note: The opcode is selected based on the <op> field.
 */
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_BEGIN(DataProcFpCondCmp)
    DIS_ARMV8_OP(0x1e200400, "fccmp",           OP_ARMV8_A64_FCCMP,     DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1e200410, "fccmpe",          OP_ARMV8_A64_FCCMPE,    DISOPTYPE_HARMLESS),
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_DECODER(DataProcFpCondCmp)
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpType,         22,  2, DIS_ARMV8_INSN_PARAM_UNSET),
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpReg,           5,  5, 0 /*idxParam*/),
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpReg,          16,  5, 1 /*idxParam*/),
    DIS_ARMV8_INSN_DECODE(kDisParmParseImm,             0,  4, 2 /*idxParam*/),
    DIS_ARMV8_INSN_DECODE(kDisParmParseCond,           12,  4, 3 /*idxParam*/),
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_END_PARAMS_4(DataProcFpCondCmp, 0xff200c10 /*fFixedInsn*/, 0 /*fClass*/,
                                                kDisArmV8OpcDecodeNop,
                                                RT_BIT_32(4), 4,
                                                kDisArmv8OpParmReg, kDisArmv8OpParmReg, kDisArmv8OpParmImm, kDisArmv8OpParmCond);


/*
 * C4.1.96 - Data Processing - Scalar Floating-Point and Advanced SIMD
 *
 * Bit 28 (op0<0>) is already fixed at 1 at this point.
 * Bit 30 (op0<2>) is already fixed at 0 at this point.
 * Bit 24 (op1<1>) is already fixed at 0 at this point.
 * Bit 21 (op2<2>) is already fixed at 1 at this point.
 * Bit 11 (op3<1>) is already fixed at 0 at this point.
 * Bit 10 (op3<0>) is already fixed at 0 at this point.
 *
 * Differentiate further based on the op3<5:2> field.
 *
 *     Bit  15 14 13 12
 *     +-------------------------------------------
 *           0  0  0  0 Conversion between FP and integer
 *           0  0  0  1 FP immediate
 *           0  0  1  0 FP compare
 *           0  0  1  1 FP immediate
 *           0  1  0  0 FP data-processing (1 source)
 *           0  1  0  1 FP immediate
 *           0  1  1  0 FP compare
 *           0  1  1  1 FP immediate
 *           1  0  0  0 UNDEFINED
 *           1  0  0  1 FP immediate
 *           1  0  1  0 FP compare
 *           1  0  1  1 FP immediate
 *           1  1  0  0 FP data-processing (1 source)
 *           1  1  0  1 FP immediate
 *           1  1  1  0 FP compare
 *           1  1  1  1 FP immediate
 */
DIS_ARMV8_DECODE_MAP_DEFINE_BEGIN(DataProcSimdFpBit28_1_30_0_24_0_21_1_11_0_10_0)
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcFpConvInt),
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcFpImm),
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcFpCmp),
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcFpImm),
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcFpDataProc1Src),
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcFpImm),
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcFpCmp),
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcFpImm),
    DIS_ARMV8_DECODE_MAP_INVALID_ENTRY,
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcFpImm),
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcFpCmp),
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcFpImm),
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcFpDataProc1Src),
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcFpImm),
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcFpCmp),
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcFpImm),
DIS_ARMV8_DECODE_MAP_DEFINE_END(DataProcSimdFpBit28_1_30_0_24_0_21_1_11_0_10_0, RT_BIT_32(12) | RT_BIT_32(13) | RT_BIT_32(14) | RT_BIT_32(15), 12);


/*
 * C4.1.96 - Data Processing - Scalar Floating-Point and Advanced SIMD
 *
 * Bit 28 (op0<0>) is already fixed at 1 at this point.
 * Bit 30 (op0<2>) is already fixed at 0 at this point.
 * Bit 24 (op1<1>) is already fixed at 0 at this point.
 * Bit 21 (op2<2>) is already fixed at 1 at this point.
 *
 * Differentiate further based on the op3<1:0> field.
 *
 *     Bit  11 10
 *     +-------------------------------------------
 *           0  0 Conversion between FP and integer / FP data-processing (1 source) / compare / immediate
 *           0  1 FP conditional compare
 *           1  0 FP data processing (2 source)
 *           1  1 FP conditional select
 */
DIS_ARMV8_DECODE_MAP_DEFINE_BEGIN(DataProcSimdFpBit28_1_30_0_24_0_21_1)
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcSimdFpBit28_1_30_0_24_0_21_1_11_0_10_0),
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcFpCondCmp),
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcFpDataProc2Src),
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcFpCondSelect),
DIS_ARMV8_DECODE_MAP_DEFINE_END(DataProcSimdFpBit28_1_30_0_24_0_21_1, RT_BIT_32(10) | RT_BIT_32(11), 10);


/*
 * C4.1.96 - Data Processing - Scalar Floating-Point and Advanced SIMD
 *
 * Bit 28 (op0<0>) is already fixed at 1 at this point.
 * Bit 30 (op0<2>) is already fixed at 0 at this point.
 * Bit 24 (op1<1>) is already fixed at 0 at this point.
 *
 * Differentiate further based on the op2<2> field.
 *
 *     Bit  21
 *     +-------------------------------------------
 *           0 Conversion between FP and fixed-point
 *           1 Conversion between FP and integer/FP data-processing (1 source) /
 *             compare / immediate / conditional compare / data-processing (2 source) / conditional select
 */
DIS_ARMV8_DECODE_MAP_DEFINE_BEGIN(DataProcSimdFpBit28_1_30_0_24_0)
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcFpFixedPConv),
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcSimdFpBit28_1_30_0_24_0_21_1),
DIS_ARMV8_DECODE_MAP_DEFINE_END_SINGLE_BIT(DataProcSimdFpBit28_1_30_0_24_0, 21);


/*
 * FMADD/FMSUB/FNMADD/FNMSUB.
 *
 * Note: The o1,o0 bitfields are concatenated to form an index.
 */
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_BEGIN(DataProcFpDataProc3Src)
    DIS_ARMV8_OP(0x1f000000, "fmadd",           OP_ARMV8_A64_FMADD,     DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1f008000, "fmsub",           OP_ARMV8_A64_FMSUB,     DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1f200000, "fnmadd",          OP_ARMV8_A64_FNMADD,    DISOPTYPE_HARMLESS),
    DIS_ARMV8_OP(0x1f208000, "fnmsub",          OP_ARMV8_A64_FNMSUB,    DISOPTYPE_HARMLESS),
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_DECODER(DataProcFpDataProc3Src)
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpType,         22,  2, DIS_ARMV8_INSN_PARAM_UNSET),
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpReg,           0,  5, 0 /*idxParam*/),
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpReg,           5,  5, 1 /*idxParam*/),
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpReg,          16,  5, 2 /*idxParam*/),
    DIS_ARMV8_INSN_DECODE(kDisParmParseFpReg,          10,  5, 3 /*idxParam*/),
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_END_PARAMS_4(DataProcFpDataProc3Src, 0xff208000 /*fFixedInsn*/, 0 /*fClass*/,
                                                kDisArmV8OpcDecodeCollate,
                                                RT_BIT_32(15) | RT_BIT_32(21), 15,
                                                kDisArmv8OpParmReg, kDisArmv8OpParmReg, kDisArmv8OpParmReg, kDisArmv8OpParmReg);


/*
 * C4.1.96 - Data Processing - Scalar Floating-Point and Advanced SIMD
 *
 * Bit 28 (op0<0>) is already fixed at 1 at this point.
 * Bit 30 (op0<2>) is already fixed at 0 at this point.
 *
 * Differentiate further based on the op1<1> field.
 *
 *     Bit  24
 *     +-------------------------------------------
 *           0 Conversion between FP and fixed-point/Conversion between FP and integer/
 *             FP data-processing (1 source) / compare / immediate / conditional compare / data-processing (2 source) /
 *             conditional select
 *           1 FP data-processing (3 source)
 */
DIS_ARMV8_DECODE_MAP_DEFINE_BEGIN(DataProcSimdFpBit28_1_30_0)
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcSimdFpBit28_1_30_0_24_0),
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcFpDataProc3Src),
DIS_ARMV8_DECODE_MAP_DEFINE_END_SINGLE_BIT(DataProcSimdFpBit28_1_30_0, 24);


/*
 * C4.1.96.12 - Data Processing - Advanced SIMD scalar shift by immediate
 *
 * FMADD/FMSUB/FNMADD/FNMSUB.
 *
 * Note: The U,opcode bitfields are concatenated to form an index.
 */
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_BEGIN(DataProcSimdScalarShiftByImm)
    DIS_ARMV8_OP(0x5f000400, "sshr",            OP_ARMV8_A64_SSHR,      DISOPTYPE_HARMLESS),
    INVALID_OPCODE,
    DIS_ARMV8_OP(0x5f001400, "ssra",            OP_ARMV8_A64_SSRA,      DISOPTYPE_HARMLESS),
    INVALID_OPCODE,
    DIS_ARMV8_OP(0x5f002400, "srshr",           OP_ARMV8_A64_SRSHR,     DISOPTYPE_HARMLESS),
    INVALID_OPCODE,
    DIS_ARMV8_OP(0x5f003400, "srsra",           OP_ARMV8_A64_SRSRA,     DISOPTYPE_HARMLESS),
    INVALID_OPCODE,
    INVALID_OPCODE,
    INVALID_OPCODE,
#if 0 /** @todo */
    DIS_ARMV8_OP(0x5f005400, "shl",             OP_ARMV8_A64_SHL,       DISOPTYPE_HARMLESS),
    INVALID_OPCODE,
    DIS_ARMV8_OP(0x5f007400, "sqshl",           OP_ARMV8_A64_SQSHL,     DISOPTYPE_HARMLESS),
    INVALID_OPCODE,
    DIS_ARMV8_OP(0x5f009400, "sqshrn",          OP_ARMV8_A64_SQSHRN,    DISOPTYPE_HARMLESS),
    INVALID_OPCODE,
    DIS_ARMV8_OP(0x5f009c00, "sqrshrn",         OP_ARMV8_A64_SQRSHRN,   DISOPTYPE_HARMLESS),
#endif
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_DECODER(DataProcSimdScalarShiftByImm)
    DIS_ARMV8_INSN_DECODE(kDisParmParseSimdRegScalar,   0,  5, 0 /*idxParam*/),
    DIS_ARMV8_INSN_DECODE(kDisParmParseSimdRegScalar,   5,  5, 1 /*idxParam*/),
    DIS_ARMV8_INSN_DECODE(kDisParmParseImmHImmB,       16,  7, 2 /*idxParam*/),
DIS_ARMV8_DECODE_INSN_CLASS_DEFINE_END_PARAMS_3(DataProcSimdScalarShiftByImm, 0xff80fc00 /*fFixedInsn*/, 0 /*fClass*/,
                                                kDisArmV8OpcDecodeCollate,
                                    /* opcode */  RT_BIT_32(11) | RT_BIT_32(12) | RT_BIT_32(13) | RT_BIT_32(14) | RT_BIT_32(15)
                                    /* U */     | RT_BIT_32(29), 11,
                                                kDisArmv8OpParmReg, kDisArmv8OpParmReg, kDisArmv8OpParmImm);


/*
 * C4.1.96 - Data Processing - Scalar Floating-Point and Advanced SIMD
 *
 * Bit 28 (op0<0>) is already fixed at 1 at this point.
 * Bit 30 (op0<2>) is already fixed at 1 at this point.
 * Bit 10 (op3<0>) is already fixed at 1 at this point.
 *
 * Differentiate further based on the op1<1> field.
 *
 *     Bit  24
 *     +-------------------------------------------
 *           0 Advanced SIMD scalar copy / scalar three same FP16 / scalar three same /
 *             scalar three same extra
 *           1 Advanced SIMD scalar shift by immediate
 */
DIS_ARMV8_DECODE_MAP_DEFINE_BEGIN(DataProcSimdFpBit28_1_30_1_10_1)
    DIS_ARMV8_DECODE_MAP_INVALID_ENTRY, //DIS_ARMV8_DECODE_MAP_ENTRY(DataProcSimdFpBit28_1_30_1_10_0),
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcSimdScalarShiftByImm),
DIS_ARMV8_DECODE_MAP_DEFINE_END_SINGLE_BIT(DataProcSimdFpBit28_1_30_1_10_1, 24);


/*
 * C4.1.96 - Data Processing - Scalar Floating-Point and Advanced SIMD
 *
 * Bit 28 (op0<0>) is already fixed at 1 at this point.
 * Bit 30 (op0<2>) is already fixed at 1 at this point.
 *
 * Differentiate further based on the op3<0> field.
 *
 *     Bit  10
 *     +-------------------------------------------
 *           0 Cryptographic three-register SHA / two-register SHA
 *             Advanced SIMD scalar two-register miscellaneous FP16 / scalar two-register miscellaneous / scalar pairwise /
 *             scalar three different / scalar x indexed element
 *           1 Advanced SIMD scalar copy / scalar three same FP16 / scalar three same /
 *             scalar shift by immediate / scalar three same extra
 */
DIS_ARMV8_DECODE_MAP_DEFINE_BEGIN(DataProcSimdFpBit28_1_30_1)
    DIS_ARMV8_DECODE_MAP_INVALID_ENTRY, //DIS_ARMV8_DECODE_MAP_ENTRY(DataProcSimdFpBit28_1_30_1_10_0),
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcSimdFpBit28_1_30_1_10_1),
DIS_ARMV8_DECODE_MAP_DEFINE_END_SINGLE_BIT(DataProcSimdFpBit28_1_30_1, 10);


/*
 * C4.1.96 - Data Processing - Scalar Floating-Point and Advanced SIMD
 *
 * Bit 28 (op0<0>) is already fixed at 1 at this point.
 *
 * Differentiate further based on the op0<2> field.
 *
 *     Bit  30
 *     +-------------------------------------------
 *           0 Conversion between FP and fixed-point/Conversion between FP and integer/
 *             FP data-processing (1 source) / compare / immediate / conditional compare / data-processing (2 source) /
 *             conditional select / data-processing (3 source)
 *           1 Cryptographic three-register SHA / two-register SHA
 *             Advanced SIMD scalar two-register miscellaneous FP16 / scalar two-register miscellaneous / scalar pairwise /
 *             scalar three different / scalar x indexed element / scalar copy / scalar three same FP16 / scalar three same /
 *             scalar shift by immediate / scalar three same extra
 */
DIS_ARMV8_DECODE_MAP_DEFINE_BEGIN(DataProcSimdFpBit28_1)
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcSimdFpBit28_1_30_0),
    DIS_ARMV8_DECODE_MAP_ENTRY(DataProcSimdFpBit28_1_30_1),
DIS_ARMV8_DECODE_MAP_DEFINE_END_SINGLE_BIT(DataProcSimdFpBit28_1, 30);
