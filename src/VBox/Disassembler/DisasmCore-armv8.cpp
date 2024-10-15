/* $Id: DisasmCore-armv8.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Disassembler - Core Components.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DIS
#include <VBox/dis.h>
#include <VBox/log.h>
#include <iprt/asm.h> /* Required to get Armv8A64ConvertImmRImmS2Mask64() from armv8.h. */
#include <iprt/armv8.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/stdarg.h>
#include "DisasmInternal-armv8.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/** Parser callback.
 * @remark no DECLCALLBACK() here because it's considered to be internal and
 *         there is no point in enforcing CDECL. */
typedef int FNDISPARSEARMV8(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit);
/** Pointer to a disassembler parser function. */
typedef FNDISPARSEARMV8 *PFNDISPARSEARMV8;


/** Opcode decoder callback.
 * @remark no DECLCALLBACK() here because it's considered to be internal and
 *         there is no point in enforcing CDECL. */
typedef uint32_t FNDISDECODEARMV8(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8INSNCLASS pInsnClass);
/** Pointer to a disassembler parser function. */
typedef FNDISDECODEARMV8 *PFNDISDECODEARMV8;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
/** @name Parsers
 * @{ */
static FNDISPARSEARMV8 disArmV8ParseIllegal;
static FNDISPARSEARMV8 disArmV8ParseSize;
static FNDISPARSEARMV8 disArmV8ParseImm;
static FNDISPARSEARMV8 disArmV8ParseImmRel;
static FNDISPARSEARMV8 disArmV8ParseImmAdr;
static FNDISPARSEARMV8 disArmV8ParseImmZero;
static FNDISPARSEARMV8 disArmV8ParseReg;
static FNDISPARSEARMV8 disArmV8ParseRegOff;
static FNDISPARSEARMV8 disArmV8ParseImmsImmrN;
static FNDISPARSEARMV8 disArmV8ParseHw;
static FNDISPARSEARMV8 disArmV8ParseCond;
static FNDISPARSEARMV8 disArmV8ParsePState;
static FNDISPARSEARMV8 disArmV8ParseSysReg;
static FNDISPARSEARMV8 disArmV8ParseSh12;
static FNDISPARSEARMV8 disArmV8ParseImmTbz;
static FNDISPARSEARMV8 disArmV8ParseShift;
static FNDISPARSEARMV8 disArmV8ParseShiftAmount;
static FNDISPARSEARMV8 disArmV8ParseImmMemOff;
static FNDISPARSEARMV8 disArmV8ParseSImmMemOff;
static FNDISPARSEARMV8 disArmV8ParseSImmMemOffUnscaled;
static FNDISPARSEARMV8 disArmV8ParseOption;
static FNDISPARSEARMV8 disArmV8ParseS;
static FNDISPARSEARMV8 disArmV8ParseSetPreIndexed;
static FNDISPARSEARMV8 disArmV8ParseSetPostIndexed;
static FNDISPARSEARMV8 disArmV8ParseFpType;
static FNDISPARSEARMV8 disArmV8ParseFpReg;
static FNDISPARSEARMV8 disArmV8ParseFpScale;
static FNDISPARSEARMV8 disArmV8ParseFpFixupFCvt;
static FNDISPARSEARMV8 disArmV8ParseSimdRegScalar;
static FNDISPARSEARMV8 disArmV8ParseImmHImmB;
/** @}  */


/** @name Decoders
 * @{ */
static FNDISDECODEARMV8 disArmV8DecodeIllegal;
static FNDISDECODEARMV8 disArmV8DecodeLookup;
static FNDISDECODEARMV8 disArmV8DecodeCollate;
/** @} */


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Parser opcode table for full disassembly. */
static PFNDISPARSEARMV8 const g_apfnDisasm[kDisParmParseMax] =
{
    disArmV8ParseIllegal,
    disArmV8ParseSize,
    disArmV8ParseImm,
    disArmV8ParseImmRel,
    disArmV8ParseImmAdr,
    disArmV8ParseImmZero,
    disArmV8ParseReg,
    disArmV8ParseRegOff,
    disArmV8ParseImmsImmrN,
    disArmV8ParseHw,
    disArmV8ParseCond,
    disArmV8ParsePState,
    NULL,
    disArmV8ParseSysReg,
    disArmV8ParseSh12,
    disArmV8ParseImmTbz,
    disArmV8ParseShift,
    disArmV8ParseShiftAmount,
    disArmV8ParseImmMemOff,
    disArmV8ParseSImmMemOff,
    disArmV8ParseSImmMemOffUnscaled,
    disArmV8ParseOption,
    disArmV8ParseS,
    disArmV8ParseSetPreIndexed,
    disArmV8ParseSetPostIndexed,
    disArmV8ParseFpType,
    disArmV8ParseFpReg,
    disArmV8ParseFpScale,
    disArmV8ParseFpFixupFCvt,
    disArmV8ParseSimdRegScalar,
    disArmV8ParseImmHImmB
};


/** Opcode decoder table. */
static PFNDISDECODEARMV8 const g_apfnOpcDecode[kDisArmV8OpcDecodeMax] =
{
    disArmV8DecodeIllegal,
    disArmV8DecodeLookup,
    disArmV8DecodeCollate
};


DECLINLINE(uint32_t) disArmV8ExtractBitVecFromInsn(uint32_t u32Insn, uint8_t idxBitStart, uint8_t cBits)
{
    uint32_t fMask = (uint32_t)(RT_BIT_64(idxBitStart + cBits) - 1);
    return (u32Insn & fMask) >> idxBitStart;
}


DECLINLINE(int32_t) disArmV8ExtractBitVecFromInsnSignExtend(uint32_t u32Insn, uint8_t idxBitStart, uint8_t cBits)
{
    uint32_t fMask = RT_BIT_32(idxBitStart + cBits) - 1;
    uint32_t fSign = ~(UINT32_MAX & (RT_BIT_32(cBits - 1) - 1));
    uint32_t fValue = (u32Insn & fMask) >> idxBitStart;
    if (fValue & fSign)
        return (int32_t)(fValue | fSign);

    return (int32_t)fValue;
}


static int disArmV8ParseIllegal(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pDis, u32Insn, pOp, pInsnClass, pParam, pInsnParm, pf64Bit);
    AssertFailed();
    return VERR_INTERNAL_ERROR;
}


static int disArmV8ParseSize(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pInsnClass, pParam);

    Assert(pInsnParm->cBits == 2);
    uint32_t u32Size = disArmV8ExtractBitVecFromInsn(u32Insn, pInsnParm->idxBitStart, pInsnParm->cBits);
    switch (u32Size)
    {
        case 0: pDis->armv8.cbOperand = sizeof(uint8_t); break;
        case 1: pDis->armv8.cbOperand = sizeof(uint16_t); break;
        case 2: pDis->armv8.cbOperand = sizeof(uint32_t); break;
        case 3: pDis->armv8.cbOperand = sizeof(uint64_t); break;
        default:
            AssertReleaseFailed();
    }
    *pf64Bit =    pDis->armv8.cbOperand == sizeof(uint64_t)
               || (pOp->fFlags & DISARMV8INSNCLASS_F_FORCED_64BIT);
    return VINF_SUCCESS;
}


static int disArmV8ParseImm(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pDis, pOp, pInsnClass, pf64Bit);

    AssertReturn(pInsnParm->idxBitStart + pInsnParm->cBits < 32, VERR_INTERNAL_ERROR_2);

    pParam->uValue = disArmV8ExtractBitVecFromInsn(u32Insn, pInsnParm->idxBitStart, pInsnParm->cBits);
    if (pInsnParm->cBits <= 8)
    {
        pParam->armv8.cb = sizeof(uint8_t);
        pParam->fUse |= DISUSE_IMMEDIATE8;
    }
    else if (pInsnParm->cBits <= 16)
    {
        pParam->armv8.cb = sizeof(uint16_t);
        pParam->fUse |= DISUSE_IMMEDIATE16;
    }
    else if (pInsnParm->cBits <= 32)
    {
        pParam->armv8.cb = sizeof(uint32_t);
        pParam->fUse |= DISUSE_IMMEDIATE32;
    }
    else
        AssertReleaseFailed();

    return VINF_SUCCESS;
}


static int disArmV8ParseImmRel(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pDis, pOp, pInsnClass, pf64Bit);

    AssertReturn(pInsnParm->idxBitStart + pInsnParm->cBits < 32, VERR_INTERNAL_ERROR_2);

    pParam->uValue = (int64_t)disArmV8ExtractBitVecFromInsnSignExtend(u32Insn, pInsnParm->idxBitStart, pInsnParm->cBits);
    if (pInsnParm->cBits <= 8)
    {
        pParam->armv8.cb = sizeof(int8_t);
        pParam->fUse |= DISUSE_IMMEDIATE8_REL;
    }
    else if (pInsnParm->cBits <= 16)
    {
        pParam->armv8.cb = sizeof(int16_t);
        pParam->fUse |= DISUSE_IMMEDIATE16_REL;
    }
    else if (pInsnParm->cBits <= 32)
    {
        pParam->armv8.cb = sizeof(int32_t);
        pParam->fUse |= DISUSE_IMMEDIATE32_REL;
    }
    else
        AssertReleaseFailed();

    return VINF_SUCCESS;
}


static int disArmV8ParseImmAdr(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pDis, pOp, pInsnClass, pf64Bit, pInsnParm);

    pParam->uValue  = disArmV8ExtractBitVecFromInsn(u32Insn, 5, 19);
    pParam->uValue |= disArmV8ExtractBitVecFromInsn(u32Insn, 29, 2) << 29;
    pParam->fUse |= DISUSE_IMMEDIATE32;
    return VINF_SUCCESS;
}


static int disArmV8ParseImmZero(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pDis, u32Insn, pOp, pInsnClass, pf64Bit, pInsnParm);

    pParam->uValue  = 0;
    pParam->fUse |= DISUSE_IMMEDIATE8;
    return VINF_SUCCESS;
}


static int disArmV8ParseReg(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pDis, pOp, pInsnClass);
    pParam->armv8.Op.Reg.idReg = disArmV8ExtractBitVecFromInsn(u32Insn, pInsnParm->idxBitStart, pInsnParm->cBits);
    if (*pf64Bit || (pParam->armv8.enmType == kDisArmv8OpParmAddrInGpr))
        pParam->armv8.Op.Reg.enmRegType = kDisOpParamArmV8RegType_Gpr_64Bit;
    else
        pParam->armv8.Op.Reg.enmRegType = kDisOpParamArmV8RegType_Gpr_32Bit;
    return VINF_SUCCESS;
}


static int disArmV8ParseRegOff(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pDis, pOp, pInsnClass, pf64Bit);
    pParam->armv8.GprIndex.idReg = disArmV8ExtractBitVecFromInsn(u32Insn, pInsnParm->idxBitStart, pInsnParm->cBits);
    pParam->armv8.Op.Reg.enmRegType = kDisOpParamArmV8RegType_Gpr_64Bit; /* Might get overwritten later on. */
    pParam->fUse                   |= DISUSE_INDEX;
    return VINF_SUCCESS;
}


static int disArmV8ParseImmsImmrN(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pDis, pOp);
    AssertReturn(pInsnParm->cBits == 13, VERR_INTERNAL_ERROR_2);

    uint32_t u32ImmRaw = disArmV8ExtractBitVecFromInsn(u32Insn, pInsnParm->idxBitStart, pInsnParm->cBits);
    /* N bit must be 0 if 32-bit variant is used. */
    if (   (   (u32ImmRaw & RT_BIT_32(12))
            && !*pf64Bit)
        || (   !(u32ImmRaw & RT_BIT_32(12))
            && *pf64Bit
            && (pInsnClass->fClass & DISARMV8INSNCLASS_F_N_FORCED_1_ON_64BIT)))
        return VERR_DIS_INVALID_OPCODE;

    uint32_t uImm7SizeLen   = ((u32ImmRaw & RT_BIT_32(12)) >> 6) | (u32ImmRaw & 0x3f);
    uint32_t uImm6Rotations = (u32ImmRaw >> 6) & 0x3f;
    pParam->uValue        =   *pf64Bit
                            ? Armv8A64ConvertImmRImmS2Mask64(uImm7SizeLen, uImm6Rotations)
                            : Armv8A64ConvertImmRImmS2Mask32(uImm7SizeLen, uImm6Rotations);
    pParam->armv8.cb      = pParam->uValue > UINT32_MAX ? sizeof(uint64_t) : sizeof(uint32_t);
    pParam->fUse         |= pParam->uValue > UINT32_MAX ? DISUSE_IMMEDIATE64 : DISUSE_IMMEDIATE32;
    return VINF_SUCCESS;
}


static int disArmV8ParseHw(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pDis, pOp, pInsnClass, pParam);
    Assert(pInsnParm->cBits == 2);

    uint32_t u32 = disArmV8ExtractBitVecFromInsn(u32Insn, pInsnParm->idxBitStart, pInsnParm->cBits);
    /* hw<1> must be 0 if this is the 32-bit variant. */
    if (   !*pf64Bit
        && (u32 & RT_BIT_32(1)))
        return VERR_DIS_INVALID_OPCODE;

    Assert(pParam->armv8.enmType == kDisArmv8OpParmImm);
    Assert(pParam->fUse & (DISUSE_IMMEDIATE8 | DISUSE_IMMEDIATE16 | DISUSE_IMMEDIATE32));
    if (u32)
    {
        pParam->armv8.enmExtend = kDisArmv8OpParmExtendLsl;
        pParam->armv8.u.cExtend = ((uint8_t)u32 & 0x3) << 4;
    }
    return VINF_SUCCESS;
}


static int disArmV8ParseCond(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pInsnClass, pOp, pParam, pf64Bit);
    Assert(pInsnParm->cBits <= 4);
    if (pParam)
    {
        /* Conditional as a parameter (CCMP/CCMN). */
        Assert(pParam->armv8.enmType == kDisArmv8OpParmCond);
        pParam->armv8.Op.enmCond = (DISARMV8INSTRCOND)disArmV8ExtractBitVecFromInsn(u32Insn, pInsnParm->idxBitStart, pInsnParm->cBits);
    }
    else /* Conditional for the base instruction. */
        pDis->armv8.enmCond = (DISARMV8INSTRCOND)disArmV8ExtractBitVecFromInsn(u32Insn, pInsnParm->idxBitStart, pInsnParm->cBits);
    return VINF_SUCCESS;
}


static int disArmV8ParsePState(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pDis, pOp, pInsnClass, pInsnParm, pf64Bit);
    uint32_t u32Op1 = disArmV8ExtractBitVecFromInsn(u32Insn, 16, 3);
    uint32_t u32Op2 = disArmV8ExtractBitVecFromInsn(u32Insn,  5, 3);

    Assert(pDis->aParams[1].armv8.enmType == kDisArmv8OpParmImm);
    Assert(pDis->aParams[1].armv8.cb      == sizeof(uint8_t));
    Assert(pDis->aParams[1].uValue        <  16); /* 4 bit field. */

    uint8_t bCRm = (uint8_t)pDis->aParams[1].uValue;

    /* See C6.2.249 for the defined values. */
    switch ((u32Op1 << 3) | u32Op2)
    {
        case 0x03: /* 000 011 */ pParam->armv8.Op.enmPState = kDisArmv8InstrPState_UAO;     break;
        case 0x04: /* 000 100 */ pParam->armv8.Op.enmPState = kDisArmv8InstrPState_PAN;     break;
        case 0x05: /* 000 101 */ pParam->armv8.Op.enmPState = kDisArmv8InstrPState_SPSel;   break;
        case 0x08: /* 001 000 */
        {
            pDis->aParams[1].uValue = bCRm & 0x1;
            switch (bCRm & 0xe)
            {
                case 0: /* 000x */ pParam->armv8.Op.enmPState = kDisArmv8InstrPState_ALLINT; break;
                case 2: /* 001x */ pParam->armv8.Op.enmPState = kDisArmv8InstrPState_PM;     break;
                default:
                    return VERR_DIS_INVALID_OPCODE;
            }
            break;
        }
        case 0x19: /* 011 001 */ pParam->armv8.Op.enmPState = kDisArmv8InstrPState_SSBS;    break;
        case 0x1a: /* 011 010 */ pParam->armv8.Op.enmPState = kDisArmv8InstrPState_DIT;     break;
        case 0x1b: /* 011 011 */
        {
            pDis->aParams[1].uValue = bCRm & 0x1;
            switch (bCRm & 0xe)
            {
                case 2: /* 001x */ pParam->armv8.Op.enmPState = kDisArmv8InstrPState_SVCRSM;   break;
                case 4: /* 010x */ pParam->armv8.Op.enmPState = kDisArmv8InstrPState_SVCRZA;   break;
                case 6: /* 011x */ pParam->armv8.Op.enmPState = kDisArmv8InstrPState_SVCRSMZA; break;
                default:
                    return VERR_DIS_INVALID_OPCODE;
            }
            break;
        }
        case 0x1c: /* 011 100 */ pParam->armv8.Op.enmPState = kDisArmv8InstrPState_TCO;     break;
        case 0x1e: /* 011 110 */ pParam->armv8.Op.enmPState = kDisArmv8InstrPState_DAIFSet; break;
        case 0x1f: /* 011 111 */ pParam->armv8.Op.enmPState = kDisArmv8InstrPState_DAIFClr; break;
        default:
            return VERR_DIS_INVALID_OPCODE;
    }

    return VINF_SUCCESS;
}


static int disArmV8ParseSysReg(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pDis, pOp, pInsnClass, pf64Bit);
    AssertReturn(pInsnParm->cBits == 15, VERR_INTERNAL_ERROR_2);

    /* Assumes a op0:op1:CRn:CRm:op2 encoding in the instruction starting at the given bit position. */
    uint32_t u32ImmRaw = disArmV8ExtractBitVecFromInsn(u32Insn, pInsnParm->idxBitStart, pInsnParm->cBits);
    pParam->armv8.Op.idSysReg = ARMV8_AARCH64_SYSREG_ID_CREATE(2 + ((u32ImmRaw >> 14) & 0x1),
                                                               (u32ImmRaw >> 11) & 0x7,
                                                               (u32ImmRaw >> 7) & 0xf,
                                                               (u32ImmRaw >> 3) & 0xf,
                                                               u32ImmRaw & 0x7);
    pParam->armv8.cb = 0;
    pParam->fUse    |= DISUSE_REG_SYSTEM;
    return VINF_SUCCESS;
}


static int disArmV8ParseSh12(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pDis, pOp, pInsnClass, pf64Bit);
    Assert(pInsnParm->cBits == 1);
    if (u32Insn & RT_BIT_32(pInsnParm->idxBitStart))
    {
        /* Shift the immediate pointed to. */
        pParam->uValue <<= 12;

        /* Re-evaluate the immediate data size. */
        pParam->fUse &= ~(DISUSE_IMMEDIATE8 | DISUSE_IMMEDIATE16 | DISUSE_IMMEDIATE32);
        if (pParam->uValue <= UINT8_MAX)
        {
            pParam->armv8.cb = sizeof(uint8_t);
            pParam->fUse |= DISUSE_IMMEDIATE8;
        }
        else if (pParam->uValue <= UINT16_MAX)
        {
            pParam->armv8.cb = sizeof(uint16_t);
            pParam->fUse |= DISUSE_IMMEDIATE16;
        }
        else if (pParam->uValue <= UINT32_MAX)
        {
            pParam->armv8.cb = sizeof(uint32_t);
            pParam->fUse |= DISUSE_IMMEDIATE32;
        }
        else
            AssertReleaseFailed();

    }
    return VINF_SUCCESS;
}


static int disArmV8ParseImmTbz(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pDis, pOp, pInsnClass, pf64Bit);

    AssertReturn(!pInsnParm->idxBitStart && !pInsnParm->cBits, VERR_INTERNAL_ERROR_2);

    pParam->uValue = disArmV8ExtractBitVecFromInsn(u32Insn, 19, 5);
    pParam->uValue |= (u32Insn & RT_BIT_32(31)) >> 26;

    pParam->armv8.cb = sizeof(uint8_t);
    pParam->fUse |= DISUSE_IMMEDIATE8;
    return VINF_SUCCESS;
}


static int disArmV8ParseShift(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pDis, pOp, pInsnClass, pf64Bit);

    AssertReturn(pInsnParm->cBits == 2, VERR_INTERNAL_ERROR_2);

    uint32_t u32Shift = disArmV8ExtractBitVecFromInsn(u32Insn, pInsnParm->idxBitStart, pInsnParm->cBits);
    switch (u32Shift)
    {
        case 0: pParam->armv8.enmExtend = kDisArmv8OpParmExtendLsl; break;
        case 1: pParam->armv8.enmExtend = kDisArmv8OpParmExtendLsr; break;
        case 2: pParam->armv8.enmExtend = kDisArmv8OpParmExtendAsr; break;
        case 3: pParam->armv8.enmExtend = kDisArmv8OpParmExtendRor; break;
        default:
            AssertReleaseFailed();
    }
    return VINF_SUCCESS;
}


static int disArmV8ParseShiftAmount(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pDis, pOp, pInsnClass, pf64Bit);

    uint32_t u32Amount = disArmV8ExtractBitVecFromInsn(u32Insn, pInsnParm->idxBitStart, pInsnParm->cBits);
    /* For a 32-bit operand it is impossible to shift/rotate more than 31 bits. */
    if (   !*pf64Bit
        && u32Amount > 31)
        return VERR_DIS_INVALID_OPCODE;

    Assert(pParam->armv8.enmExtend != kDisArmv8OpParmExtendNone);
    Assert(u32Amount < 64);
    pParam->armv8.u.cExtend = (uint8_t)u32Amount;
    /* Any shift operation with a 0 is essentially no shift being applied. */
    if (pParam->armv8.u.cExtend == 0)
        pParam->armv8.enmExtend = kDisArmv8OpParmExtendNone;
    return VINF_SUCCESS;
}


static int disArmV8ParseImmMemOff(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pInsnClass, pOp, pf64Bit);

    AssertReturn(pInsnParm->cBits <= 12, VERR_INTERNAL_ERROR_2);
    AssertReturn(pDis->armv8.cbOperand != 0, VERR_INTERNAL_ERROR_2);

    pParam->armv8.u.offBase = disArmV8ExtractBitVecFromInsn(u32Insn, pInsnParm->idxBitStart, pInsnParm->cBits);
    switch (pDis->armv8.cbOperand)
    {
        case sizeof(uint8_t): break;
        case sizeof(uint16_t): pParam->armv8.u.offBase <<= 1; break;
        case sizeof(uint32_t): pParam->armv8.u.offBase <<= 2; break;
        case sizeof(uint64_t): pParam->armv8.u.offBase <<= 3; break;
        default:
            AssertReleaseFailed();
    }
    pParam->armv8.cb = sizeof(int16_t);
    return VINF_SUCCESS;
}


static int disArmV8ParseSImmMemOff(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pDis, pInsnClass, pf64Bit);

    AssertReturn(pInsnParm->cBits <= 7, VERR_INTERNAL_ERROR_2);
    AssertReturn(   (pOp->fFlags & DISARMV8INSNCLASS_F_FORCED_32BIT)
                 || (pOp->fFlags & DISARMV8INSNCLASS_F_FORCED_64BIT),
                 VERR_INTERNAL_ERROR_2);

    pParam->armv8.cb = sizeof(int16_t);
    pParam->armv8.u.offBase = disArmV8ExtractBitVecFromInsnSignExtend(u32Insn, pInsnParm->idxBitStart, pInsnParm->cBits);
    pParam->armv8.u.offBase <<= (pOp->fFlags & DISARMV8INSNCLASS_F_FORCED_32BIT) ? 2 : 3;
    return VINF_SUCCESS;
}


static int disArmV8ParseSImmMemOffUnscaled(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pDis, pOp, pInsnClass, pf64Bit);

    AssertReturn(pInsnParm->cBits <= 9, VERR_INTERNAL_ERROR_2);
    pParam->armv8.u.offBase = disArmV8ExtractBitVecFromInsnSignExtend(u32Insn, pInsnParm->idxBitStart, pInsnParm->cBits);
    return VINF_SUCCESS;
}


static int disArmV8ParseOption(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pDis, u32Insn, pOp, pInsnClass, pParam, pInsnParm, pf64Bit);

    AssertReturn(pInsnParm->cBits == 3, VERR_INTERNAL_ERROR_2);
    uint32_t u32Opt = disArmV8ExtractBitVecFromInsn(u32Insn, pInsnParm->idxBitStart, pInsnParm->cBits);

    Assert(   pParam->armv8.enmExtend == kDisArmv8OpParmExtendNone
           && (pParam->fUse & DISUSE_INDEX));
    switch (u32Opt)
    {
        case 0: pParam->armv8.enmExtend = kDisArmv8OpParmExtendUxtB; break;
        case 1: pParam->armv8.enmExtend = kDisArmv8OpParmExtendUxtH; break;
        case 2: pParam->armv8.enmExtend = kDisArmv8OpParmExtendUxtW; break;
        case 3: pParam->armv8.enmExtend = kDisArmv8OpParmExtendUxtX; break;
        case 4: pParam->armv8.enmExtend = kDisArmv8OpParmExtendSxtB; break;
        case 5: pParam->armv8.enmExtend = kDisArmv8OpParmExtendSxtH; break;
        case 6: pParam->armv8.enmExtend = kDisArmv8OpParmExtendSxtW; break;
        case 7: pParam->armv8.enmExtend = kDisArmv8OpParmExtendSxtX; break;
        default:
            AssertFailed();
    }

    /* When option<0> is set to 0, the 32-bit name of the GPR is used, 64-bit when option<0> is set to 1. */
    pParam->armv8.GprIndex.enmRegType =   RT_BOOL(u32Opt & 0x1)
                                        ? kDisOpParamArmV8RegType_Gpr_64Bit
                                        : kDisOpParamArmV8RegType_Gpr_32Bit;
    return VINF_SUCCESS;
}


static int disArmV8ParseS(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pDis, u32Insn, pOp, pInsnClass, pParam, pInsnParm, pf64Bit);

    AssertReturn(pInsnParm->cBits == 1, VERR_INTERNAL_ERROR_2);
    bool const fS = RT_BOOL(u32Insn & RT_BIT_32(pInsnParm->idxBitStart));

    Assert(   pParam->armv8.enmExtend != kDisArmv8OpParmExtendNone
           && pDis->armv8.cbOperand > 0
           && pDis->armv8.cbOperand <= 8);
    if (fS)
    {
        switch (pDis->armv8.cbOperand)
        {
            case sizeof(uint8_t):  pParam->armv8.u.cExtend = 0; break;
            case sizeof(uint16_t): pParam->armv8.u.cExtend = 1; break;
            case sizeof(uint32_t): pParam->armv8.u.cExtend = 2; break;
            case sizeof(uint64_t): pParam->armv8.u.cExtend = 3; break;
            default:
                AssertReleaseFailed();
        }
    }
    else if (pParam->armv8.enmExtend == kDisArmv8OpParmExtendUxtX) /* UXTX aka LSL can be ignored if S is not set. */
    {
        pParam->armv8.u.cExtend = 0;
        pParam->armv8.enmExtend = kDisArmv8OpParmExtendNone;
    }

    return VINF_SUCCESS;
}


static int disArmV8ParseSetPreIndexed(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pDis, u32Insn, pOp, pInsnClass, pInsnParm, pf64Bit);

    pParam->fUse |= DISUSE_PRE_INDEXED;
    return VINF_SUCCESS;
}


static int disArmV8ParseSetPostIndexed(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pDis, u32Insn, pOp, pInsnClass, pInsnParm, pf64Bit);

    pParam->fUse |= DISUSE_POST_INDEXED;
    return VINF_SUCCESS;
}


static int disArmV8ParseFpType(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pOp, pInsnClass, pParam, pf64Bit);

    Assert(pDis->armv8.enmFpType == kDisArmv8InstrFpType_Invalid);
    uint32_t u32FpType = disArmV8ExtractBitVecFromInsn(u32Insn, pInsnParm->idxBitStart, pInsnParm->cBits);
    switch (u32FpType)
    {
        case 0: pDis->armv8.enmFpType = kDisArmv8InstrFpType_Single; break;
        case 1: pDis->armv8.enmFpType = kDisArmv8InstrFpType_Double; break;
        case 3: pDis->armv8.enmFpType = kDisArmv8InstrFpType_Half; break;
        default: return VERR_DIS_INVALID_OPCODE;
    }
    return VINF_SUCCESS;
}


static int disArmV8ParseFpReg(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pOp, pInsnClass, pParam, pf64Bit);

    Assert(pDis->armv8.enmFpType != kDisArmv8InstrFpType_Invalid);
    pParam->armv8.Op.Reg.idReg = disArmV8ExtractBitVecFromInsn(u32Insn, pInsnParm->idxBitStart, pInsnParm->cBits);
    switch (pDis->armv8.enmFpType)
    {
        case kDisArmv8InstrFpType_Single: pParam->armv8.Op.Reg.enmRegType = kDisOpParamArmV8RegType_FpReg_Single; break;
        case kDisArmv8InstrFpType_Double: pParam->armv8.Op.Reg.enmRegType = kDisOpParamArmV8RegType_FpReg_Double; break;
        case kDisArmv8InstrFpType_Half:   pParam->armv8.Op.Reg.enmRegType = kDisOpParamArmV8RegType_FpReg_Half;   break;
        default: return VERR_DIS_INVALID_OPCODE;
    }
    return VINF_SUCCESS;
}


static int disArmV8ParseFpScale(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pDis, pOp, pInsnClass);
    Assert(pDis->armv8.enmFpType != kDisArmv8InstrFpType_Invalid);

    uint32_t u32Scale = disArmV8ExtractBitVecFromInsn(u32Insn, pInsnParm->idxBitStart, pInsnParm->cBits);
    if (   !*pf64Bit
        && (u32Scale & RT_BIT_32(5)) == 0)
        return VERR_DIS_INVALID_OPCODE;

    pParam->uValue   = 64 - u32Scale;
    pParam->armv8.cb = sizeof(uint8_t);
    pParam->fUse    |= DISUSE_IMMEDIATE8;
    return VINF_SUCCESS;
}


static int disArmV8ParseFpFixupFCvt(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pDis, pInsnClass, pParam, pInsnParm, pf64Bit);

    /* Nothing to do if this isn't about fcvt. */
    if (pOp->Opc.uOpcode != OP_ARMV8_A64_FCVT)
        return VINF_SUCCESS;

    Assert(pDis->armv8.enmFpType != kDisArmv8InstrFpType_Invalid);
    Assert(   pDis->aParams[0].armv8.enmType == kDisArmv8OpParmReg
           && pDis->aParams[1].armv8.enmType == kDisArmv8OpParmReg);

    /* Convert source and guest register floating point types to the correct widths. */
    uint32_t u32Opc = (u32Insn & (RT_BIT_32(15) | RT_BIT_32(16))) >> 15;
#ifdef VBOX_STRICT
    uint32_t u32FpType = disArmV8ExtractBitVecFromInsn(u32Insn, 22, 2);
    Assert(   u32Opc != u32FpType
           && u32Opc != 2);
#endif

    static const DISOPPARAMARMV8REGTYPE s_aOpc2FpWidth[] =
    {
        kDisOpParamArmV8RegType_FpReg_Single,
        kDisOpParamArmV8RegType_FpReg_Double,
        (DISOPPARAMARMV8REGTYPE)UINT8_MAX,    /* Invalid encoding. */
        kDisOpParamArmV8RegType_FpReg_Half
    };

    pDis->aParams[0].armv8.Op.Reg.enmRegType = s_aOpc2FpWidth[u32Opc];
    return VINF_SUCCESS;
}


static int disArmV8ParseSimdRegScalar(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pDis, pOp, pInsnClass, pParam, pInsnParm, pf64Bit);

    pParam->armv8.Op.Reg.idReg = disArmV8ExtractBitVecFromInsn(u32Insn, pInsnParm->idxBitStart, pInsnParm->cBits);
    pParam->armv8.Op.Reg.enmRegType = kDisOpParamArmV8RegType_Simd_Scalar_64Bit;
    return VINF_SUCCESS;
}


static int disArmV8ParseImmHImmB(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass, PDISOPPARAM pParam, PCDISARMV8INSNPARAM pInsnParm, bool *pf64Bit)
{
    RT_NOREF(pDis, pOp, pInsnClass, pParam, pInsnParm, pf64Bit);

    Assert(pInsnParm->cBits == 7);
    uint32_t u32ImmRaw = disArmV8ExtractBitVecFromInsn(u32Insn, pInsnParm->idxBitStart, pInsnParm->cBits);
    if (!(u32ImmRaw & RT_BIT_32(6))) /* immh == 0xxx is reserved for the scalar variant. */
        return VERR_DIS_INVALID_OPCODE;

    pParam->uValue = 2 * 64 - u32ImmRaw;
    pParam->armv8.cb = sizeof(uint8_t);
    pParam->fUse |= DISUSE_IMMEDIATE8;
    return VINF_SUCCESS;
}


static uint32_t disArmV8DecodeIllegal(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8INSNCLASS pInsnClass)
{
    RT_NOREF(pDis, u32Insn, pInsnClass);
    AssertFailed();
    return UINT32_MAX;
}


static uint32_t disArmV8DecodeLookup(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8INSNCLASS pInsnClass)
{
    RT_NOREF(pDis);

    for (uint32_t i = 0; i < pInsnClass->Hdr.cDecode; i++)
    {
        PCDISARMV8OPCODE pOp = &pInsnClass->paOpcodes[i];
        if (u32Insn == pOp->fValue)
            return i;
    }

    return UINT32_MAX;
}


static uint32_t disArmV8DecodeCollate(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8INSNCLASS pInsnClass)
{
    RT_NOREF(pDis);

    /* Need to build a compact representation of the relevant bits from the mask to create an index. */
    uint32_t fMask = pInsnClass->fMask >> pInsnClass->cShift;

    /** @todo Optimize. */
    uint32_t idx = 0;
    uint32_t cShift = 0;
    while (fMask)
    {
        if (fMask & 0x1)
        {
            idx |= (u32Insn & 1) << cShift;
            cShift++;
        }

        u32Insn >>= 1;
        fMask   >>= 1;
    }

    if (RT_LIKELY(idx < pInsnClass->Hdr.cDecode))
        return idx;

    return UINT32_MAX;
}


/**
 * Looks for possible alias conversions for the given disassembler state.
 *
 * @param   pDis        The disassembler state to process.
 */
static void disArmV8A64InsnAliasesProcess(PDISSTATE pDis)
{
#define DIS_ARMV8_ALIAS(a_Name) s_DisArmv8Alias ## a_Name
#define DIS_ARMV8_ALIAS_CREATE(a_Name, a_szOpcode, a_uOpcode, a_fOpType) static const DISOPCODE DIS_ARMV8_ALIAS(a_Name) = OP(a_szOpcode, 0, 0, 0, a_uOpcode, 0, 0, 0, a_fOpType)
#define DIS_ARMV8_ALIAS_REF(a_Name) &DIS_ARMV8_ALIAS(a_Name)
    switch (pDis->pCurInstr->uOpcode)
    {
        case OP_ARMV8_A64_ORR:
        {
            /* Check for possible MOV conversion for the register variant when: shift is None and the first source is the zero register. */
            Assert(pDis->aParams[1].armv8.enmType == kDisArmv8OpParmReg);
            Assert(   pDis->aParams[1].armv8.Op.Reg.enmRegType == kDisOpParamArmV8RegType_Gpr_32Bit
                   || pDis->aParams[1].armv8.Op.Reg.enmRegType == kDisOpParamArmV8RegType_Gpr_64Bit);

            if (   pDis->aParams[2].armv8.enmType == kDisArmv8OpParmReg
                && pDis->aParams[2].armv8.enmExtend == kDisArmv8OpParmExtendNone
                && pDis->aParams[1].armv8.Op.Reg.idReg == ARMV8_A64_REG_XZR)
            {
                DIS_ARMV8_ALIAS_CREATE(Mov, "mov", OP_ARMV8_A64_MOV, DISOPTYPE_HARMLESS);
                pDis->pCurInstr  = DIS_ARMV8_ALIAS_REF(Mov);
                pDis->aParams[1] = pDis->aParams[2];
                pDis->aParams[2].armv8.enmType = kDisArmv8OpParmNone;
            }
            /** @todo Immediate variant. */
            break;
        }
        case OP_ARMV8_A64_SUBS:
        {
            Assert(pDis->aParams[0].armv8.enmType == kDisArmv8OpParmReg);
            Assert(   pDis->aParams[0].armv8.Op.Reg.enmRegType == kDisOpParamArmV8RegType_Gpr_32Bit
                   || pDis->aParams[0].armv8.Op.Reg.enmRegType == kDisOpParamArmV8RegType_Gpr_64Bit);

            if (pDis->aParams[0].armv8.Op.Reg.idReg == ARMV8_A64_REG_XZR)
            {
                DIS_ARMV8_ALIAS_CREATE(Cmp, "cmp", OP_ARMV8_A64_CMP, DISOPTYPE_HARMLESS);
                pDis->pCurInstr  = DIS_ARMV8_ALIAS_REF(Cmp);
                pDis->aParams[0] = pDis->aParams[1];
                pDis->aParams[1] = pDis->aParams[2];
                pDis->aParams[2].armv8.enmType = kDisArmv8OpParmNone;
            }
            break;
        }
        default:
            break; /* No conversion */
    }
#undef DIS_ARMV8_ALIAS_REF
#undef DIS_ARMV8_ALIAS_CREATE
#undef DIS_ARMV8_ALIAS
}


static int disArmV8A64ParseInstruction(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8OPCODE pOp, PCDISARMV8INSNCLASS pInsnClass)
{
    AssertPtr(pOp);
    AssertPtr(pDis);
    //Assert((u32Insn & pInsnClass->fFixedInsn) == pOp->fValue);
    if ((u32Insn & pInsnClass->fFixedInsn) != pOp->fValue)
        return VERR_DIS_INVALID_OPCODE;

    /* Should contain the parameter type on input. */
    pDis->aParams[0].fUse            = 0;
    pDis->aParams[1].fUse            = 0;
    pDis->aParams[2].fUse            = 0;
    pDis->aParams[3].fUse            = 0;
    pDis->aParams[0].armv8.enmType   = pInsnClass->aenmParamTypes[0];
    pDis->aParams[1].armv8.enmType   = pInsnClass->aenmParamTypes[1];
    pDis->aParams[2].armv8.enmType   = pInsnClass->aenmParamTypes[2];
    pDis->aParams[3].armv8.enmType   = pInsnClass->aenmParamTypes[3];
    pDis->aParams[0].armv8.enmExtend = kDisArmv8OpParmExtendNone;
    pDis->aParams[1].armv8.enmExtend = kDisArmv8OpParmExtendNone;
    pDis->aParams[2].armv8.enmExtend = kDisArmv8OpParmExtendNone;
    pDis->aParams[3].armv8.enmExtend = kDisArmv8OpParmExtendNone;
    pDis->armv8.enmCond              = kDisArmv8InstrCond_Al;
    pDis->armv8.enmFpType            = kDisArmv8InstrFpType_Invalid;
    pDis->armv8.cbOperand            = 0;

    pDis->pCurInstr = &pOp->Opc;
    Assert(&pOp->Opc != &g_ArmV8A64InvalidOpcode[0]);

    bool f64Bit = false;

    /** @todo Get rid of these and move them to the per opcode
     * (SF can become a decoder step). */
    if (pInsnClass->fClass & DISARMV8INSNCLASS_F_SF)
        f64Bit = RT_BOOL(u32Insn & RT_BIT_32(31));
    else if (pInsnClass->fClass & DISARMV8INSNCLASS_F_FORCED_64BIT)
        f64Bit = true;

    if (pOp->fFlags & DISARMV8INSNCLASS_F_FORCED_32BIT)
        f64Bit = false;
    else if (pOp->fFlags & DISARMV8INSNCLASS_F_FORCED_64BIT)
        f64Bit = true;

    int rc = VINF_SUCCESS;
    PCDISARMV8INSNPARAM pDecode = &pInsnClass->paParms[0];
    while (   (pDecode->idxParse != kDisParmParseNop)
           && RT_SUCCESS(rc))
    {
        rc = g_apfnDisasm[pDecode->idxParse](pDis, u32Insn, pOp, pInsnClass,
                                               pDecode->idxParam != DIS_ARMV8_INSN_PARAM_UNSET
                                             ? &pDis->aParams[pDecode->idxParam]
                                             : NULL,
                                             pDecode, &f64Bit);
        pDecode++;
    }

    /* If parameter parsing returned an invalid opcode error the encoding is invalid. */
    if (RT_SUCCESS(rc)) /** @todo Introduce flag to switch alias conversion on/off. */
        disArmV8A64InsnAliasesProcess(pDis);
    else if (rc == VERR_DIS_INVALID_OPCODE)
    {
        pDis->pCurInstr = &g_ArmV8A64InvalidOpcode[0];

        pDis->aParams[0].armv8.enmType = kDisArmv8OpParmNone;
        pDis->aParams[1].armv8.enmType = kDisArmv8OpParmNone;
        pDis->aParams[2].armv8.enmType = kDisArmv8OpParmNone;
        pDis->aParams[3].armv8.enmType = kDisArmv8OpParmNone;
    }
    pDis->rc = rc;
    return rc;
}


static int disArmV8A64ParseInvOpcode(PDISSTATE pDis)
{
    pDis->pCurInstr = &g_ArmV8A64InvalidOpcode[0];
    pDis->rc = VERR_DIS_INVALID_OPCODE;
    return VERR_DIS_INVALID_OPCODE;
}


static int disInstrArmV8DecodeWorker(PDISSTATE pDis, uint32_t u32Insn, PCDISARMV8DECODEHDR pHdr)
{
    while (   pHdr
           && pHdr->enmDecodeType != kDisArmV8DecodeType_InsnClass)
    {
        if (pHdr->enmDecodeType == kDisArmV8DecodeType_Map)
        {
            PCDISARMV8DECODEMAP pMap = (PCDISARMV8DECODEMAP)pHdr;

            uint32_t idxNext = (u32Insn & pMap->fMask) >> pMap->cShift;
            if (RT_LIKELY(idxNext < pMap->Hdr.cDecode))
                pHdr = pMap->papNext[idxNext];
            else
            {
                pHdr = NULL;
                break;
            }
        }
        else
        {
            Assert(pHdr->enmDecodeType == kDisArmV8DecodeType_Table);
            PCDISARMV8DECODETBL pTbl = (PCDISARMV8DECODETBL)pHdr;

            /* Walk all entries in the table and select the best match. */
            pHdr = NULL;
            for (uint32_t i = 0; i < pTbl->Hdr.cDecode; i++)
            {
                PCDISARMV8DECODETBLENTRY pEntry = &pTbl->paEntries[i];
                if ((u32Insn & pEntry->fMask) == pEntry->fValue)
                {
                    pHdr = pEntry->pHdrNext;
                    break;
                }
            }
        }
    }

    if (pHdr)
    {
        Assert(pHdr->enmDecodeType == kDisArmV8DecodeType_InsnClass);
        PCDISARMV8INSNCLASS pInsnClass = (PCDISARMV8INSNCLASS)pHdr;

        /* Decode the opcode from the instruction class. */
        uint32_t uOpcRaw = 0;
        if (pInsnClass->Hdr.cDecode > 1)
        {
            uOpcRaw = (u32Insn & pInsnClass->fMask) >> pInsnClass->cShift;
            if (pInsnClass->enmOpcDecode != kDisArmV8OpcDecodeNop)
                uOpcRaw = g_apfnOpcDecode[pInsnClass->enmOpcDecode](pDis, uOpcRaw, pInsnClass);
        }

        if (uOpcRaw < pInsnClass->Hdr.cDecode)
        {
            PCDISARMV8OPCODE pOp = &pInsnClass->paOpcodes[uOpcRaw];
            return disArmV8A64ParseInstruction(pDis, u32Insn, pOp, pInsnClass);
        }
    }

    return disArmV8A64ParseInvOpcode(pDis);
}


/**
 * Internal worker for DISInstrEx and DISInstrWithPrefetchedBytes.
 *
 * @returns VBox status code.
 * @param   pDis            Initialized disassembler state.
 * @param   paOneByteMap    The one byte opcode map to use.
 * @param   pcbInstr        Where to store the instruction size. Can be NULL.
 */
DECLHIDDEN(int) disInstrWorkerArmV8(PDISSTATE pDis, PCDISOPCODE paOneByteMap, uint32_t *pcbInstr)
{
    RT_NOREF(paOneByteMap);

    if (pDis->uCpuMode == DISCPUMODE_ARMV8_A64)
    {
        *pcbInstr = sizeof(uint32_t);

        /* Instructions are always little endian and 4 bytes. */
        uint32_t u32Insn = disReadDWord(pDis, 0 /*offInstr*/);
        if (RT_FAILURE(pDis->rc))
            return pDis->rc;

        /** @todo r=bird: This is a waste of time if the host is little endian... */
        pDis->Instr.u32 = RT_LE2H_U32(u32Insn);
        pDis->cbInstr   = sizeof(u32Insn);

        return disInstrArmV8DecodeWorker(pDis, u32Insn, &g_aArmV8A64InsnDecodeL0.Hdr);
    }

    AssertReleaseFailed();
    return VERR_NOT_IMPLEMENTED;
}


/**
 * Inlined worker that initializes the disassembler state.
 *
 * @returns The primary opcode map to use.
 * @param   pDis            The disassembler state.
 * @param   uInstrAddr      The instruction address.
 * @param   enmCpuMode      The CPU mode.
 * @param   fFilter         The instruction filter settings.
 * @param   pfnReadBytes    The byte reader, can be NULL.
 * @param   pvUser          The user data for the reader.
 */
DECLHIDDEN(PCDISOPCODE) disInitializeStateArmV8(PDISSTATE pDis, DISCPUMODE enmCpuMode, uint32_t fFilter)
{
    RT_NOREF(pDis, enmCpuMode, fFilter);
    return NULL;
}
