/* $Id: DisasmCore-x86-amd64.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Disassembler - Core Components.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
#include <VBox/disopcode-x86-amd64.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/stdarg.h>
#include <iprt/x86.h>
#include "DisasmInternal-x86-amd64.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
/** @name Parsers
 * @{ */
static FNDISPARSEX86 ParseIllegal;
static FNDISPARSEX86 ParseModRM;
static FNDISPARSEX86 ParseModRM_SizeOnly;
static FNDISPARSEX86 UseModRM;
static FNDISPARSEX86 ParseImmByte;
static FNDISPARSEX86 ParseImmByte_SizeOnly;
static FNDISPARSEX86 ParseImmByteSX;
static FNDISPARSEX86 ParseImmByteSX_SizeOnly;
static FNDISPARSEX86 ParseImmBRel;
static FNDISPARSEX86 ParseImmBRel_SizeOnly;
static FNDISPARSEX86 ParseImmUshort;
static FNDISPARSEX86 ParseImmUshort_SizeOnly;
static FNDISPARSEX86 ParseImmV;
static FNDISPARSEX86 ParseImmV_SizeOnly;
static FNDISPARSEX86 ParseImmVRel;
static FNDISPARSEX86 ParseImmVRel_SizeOnly;
static FNDISPARSEX86 ParseImmZ;
static FNDISPARSEX86 ParseImmZ_SizeOnly;

static FNDISPARSEX86 ParseImmAddr;
static FNDISPARSEX86 ParseImmAddr_SizeOnly;
static FNDISPARSEX86 ParseImmAddrF;
static FNDISPARSEX86 ParseImmAddrF_SizeOnly;
static FNDISPARSEX86 ParseFixedReg;
static FNDISPARSEX86 ParseImmUlong;
static FNDISPARSEX86 ParseImmUlong_SizeOnly;
static FNDISPARSEX86 ParseImmQword;
static FNDISPARSEX86 ParseImmQword_SizeOnly;
static FNDISPARSEX86 ParseInvOpModRm;

static FNDISPARSEX86 ParseTwoByteEsc;
static FNDISPARSEX86 ParseThreeByteEsc4;
static FNDISPARSEX86 ParseThreeByteEsc5;
static FNDISPARSEX86 ParseGrp1;
static FNDISPARSEX86 ParseShiftGrp2;
static FNDISPARSEX86 ParseGrp3;
static FNDISPARSEX86 ParseGrp4;
static FNDISPARSEX86 ParseGrp5;
static FNDISPARSEX86 Parse3DNow;
static FNDISPARSEX86 ParseGrp6;
static FNDISPARSEX86 ParseGrp7;
static FNDISPARSEX86 ParseGrp8;
static FNDISPARSEX86 ParseGrp9;
static FNDISPARSEX86 ParseGrp10;
static FNDISPARSEX86 ParseGrp12;
static FNDISPARSEX86 ParseVGrp12;
static FNDISPARSEX86 ParseGrp13;
static FNDISPARSEX86 ParseVGrp13;
static FNDISPARSEX86 ParseGrp14;
static FNDISPARSEX86 ParseVGrp14;
static FNDISPARSEX86 ParseGrp15;
static FNDISPARSEX86 ParseVGrp15;
static FNDISPARSEX86 ParseGrp16;
static FNDISPARSEX86 ParseVGrp17;
static FNDISPARSEX86 ParseVGrp17;
static FNDISPARSEX86 ParseModFence;
static FNDISPARSEX86 ParseNopPause;
static FNDISPARSEX86 ParseVex2b;
static FNDISPARSEX86 ParseVex3b;
static FNDISPARSEX86 ParseVexDest;

static FNDISPARSEX86 ParseYv;
static FNDISPARSEX86 ParseYb;
static FNDISPARSEX86 ParseXv;
static FNDISPARSEX86 ParseXb;

/** Floating point parsing */
static FNDISPARSEX86 ParseEscFP;
/** @}  */


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Parser opcode table for full disassembly. */
static PFNDISPARSEX86 const g_apfnFullDisasm[IDX_ParseMax] =
{
    ParseIllegal,
    ParseModRM,
    UseModRM,
    ParseImmByte,
    ParseImmBRel,
    ParseImmUshort,
    ParseImmV,
    ParseImmVRel,
    ParseImmAddr,
    ParseFixedReg,
    ParseImmUlong,
    ParseImmQword,
    ParseTwoByteEsc,
    ParseGrp1,
    ParseShiftGrp2,
    ParseGrp3,
    ParseGrp4,
    ParseGrp5,
    Parse3DNow,
    ParseGrp6,
    ParseGrp7,
    ParseGrp8,
    ParseGrp9,
    ParseGrp10,
    ParseGrp12,
    ParseVGrp12,
    ParseGrp13,
    ParseVGrp13,
    ParseGrp14,
    ParseVGrp14,
    ParseGrp15,
    ParseVGrp15,
    ParseGrp16,
    ParseVGrp17,
    ParseModFence,
    ParseYv,
    ParseYb,
    ParseXv,
    ParseXb,
    ParseEscFP,
    ParseNopPause,
    ParseImmByteSX,
    ParseImmZ,
    ParseThreeByteEsc4,
    ParseThreeByteEsc5,
    ParseImmAddrF,
    ParseInvOpModRm,
    ParseVex2b,
    ParseVex3b,
    ParseVexDest
};

/** Parser opcode table for only calculating instruction size. */
static PFNDISPARSEX86 const g_apfnCalcSize[IDX_ParseMax] =
{
    ParseIllegal,
    ParseModRM_SizeOnly,
    UseModRM,
    ParseImmByte_SizeOnly,
    ParseImmBRel_SizeOnly,
    ParseImmUshort_SizeOnly,
    ParseImmV_SizeOnly,
    ParseImmVRel_SizeOnly,
    ParseImmAddr_SizeOnly,
    ParseFixedReg,
    ParseImmUlong_SizeOnly,
    ParseImmQword_SizeOnly,
    ParseTwoByteEsc,
    ParseGrp1,
    ParseShiftGrp2,
    ParseGrp3,
    ParseGrp4,
    ParseGrp5,
    Parse3DNow,
    ParseGrp6,
    ParseGrp7,
    ParseGrp8,
    ParseGrp9,
    ParseGrp10,
    ParseGrp12,
    ParseVGrp12,
    ParseGrp13,
    ParseVGrp13,
    ParseGrp14,
    ParseVGrp14,
    ParseGrp15,
    ParseVGrp15,
    ParseGrp16,
    ParseVGrp17,
    ParseModFence,
    ParseYv,
    ParseYb,
    ParseXv,
    ParseXb,
    ParseEscFP,
    ParseNopPause,
    ParseImmByteSX_SizeOnly,
    ParseImmZ_SizeOnly,
    ParseThreeByteEsc4,
    ParseThreeByteEsc5,
    ParseImmAddrF_SizeOnly,
    ParseInvOpModRm,
    ParseVex2b,
    ParseVex3b,
    ParseVexDest
};


//*****************************************************************************
//*****************************************************************************
static size_t disParseInstruction(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis)
{
    Assert(pOp); Assert(pDis);

    // Store the opcode format string for disasmPrintf
    pDis->pCurInstr = pOp;

    /*
     * Apply filter to instruction type to determine if a full disassembly is required.
     * Note! Multibyte opcodes are always marked harmless until the final byte.
     */
    bool fFiltered;
    if ((pOp->fOpType & pDis->x86.fFilter) == 0)
    {
        fFiltered = true;
        pDis->x86.pfnDisasmFnTable = g_apfnCalcSize;
    }
    else
    {
        /* Not filtered out -> full disassembly */
        fFiltered = false;
        pDis->x86.pfnDisasmFnTable = g_apfnFullDisasm;
    }

    // Should contain the parameter type on input
    pDis->aParams[0].x86.fParam = pOp->fParam1;
    pDis->aParams[1].x86.fParam = pOp->fParam2;
    pDis->aParams[2].x86.fParam = pOp->fParam3;
    pDis->aParams[3].x86.fParam = pOp->fParam4;

    /* Correct the operand size if the instruction is marked as forced or default 64 bits */
    if (!(pOp->fOpType & (DISOPTYPE_X86_FORCED_64_OP_SIZE | DISOPTYPE_X86_DEFAULT_64_OP_SIZE | DISOPTYPE_X86_FORCED_32_OP_SIZE_X86)))
    { /* probably likely */ }
    else
    {
        if (pDis->uCpuMode == DISCPUMODE_64BIT)
        {
            if (pOp->fOpType & DISOPTYPE_X86_FORCED_64_OP_SIZE)
                pDis->x86.uOpMode = DISCPUMODE_64BIT;
            else if (   (pOp->fOpType & DISOPTYPE_X86_DEFAULT_64_OP_SIZE)
                     && !(pDis->x86.fPrefix & DISPREFIX_OPSIZE))
                pDis->x86.uOpMode = DISCPUMODE_64BIT;
        }
        else if (pOp->fOpType & DISOPTYPE_X86_FORCED_32_OP_SIZE_X86)
        {
            /* Forced 32 bits operand size for certain instructions (mov crx, mov drx). */
            Assert(pDis->uCpuMode != DISCPUMODE_64BIT);
            pDis->x86.uOpMode = DISCPUMODE_32BIT;
        }
    }

    if (pOp->idxParse1 != IDX_ParseNop)
    {
        offInstr = pDis->x86.pfnDisasmFnTable[pOp->idxParse1](offInstr, pOp, pDis, &pDis->aParams[0]);
        if (fFiltered == false) pDis->aParams[0].x86.cb = DISGetParamSize(pDis, &pDis->aParams[0]);
    }

    if (pOp->idxParse2 != IDX_ParseNop)
    {
        offInstr = pDis->x86.pfnDisasmFnTable[pOp->idxParse2](offInstr, pOp, pDis, &pDis->aParams[1]);
        if (fFiltered == false) pDis->aParams[1].x86.cb = DISGetParamSize(pDis, &pDis->aParams[1]);
    }

    if (pOp->idxParse3 != IDX_ParseNop)
    {
        offInstr = pDis->x86.pfnDisasmFnTable[pOp->idxParse3](offInstr, pOp, pDis, &pDis->aParams[2]);
        if (fFiltered == false) pDis->aParams[2].x86.cb = DISGetParamSize(pDis, &pDis->aParams[2]);
    }

    if (pOp->idxParse4 != IDX_ParseNop)
    {
        offInstr = pDis->x86.pfnDisasmFnTable[pOp->idxParse4](offInstr, pOp, pDis, &pDis->aParams[3]);
        if (fFiltered == false) pDis->aParams[3].x86.cb = DISGetParamSize(pDis, &pDis->aParams[3]);
    }
    // else simple one byte instruction

    return offInstr;
}
//*****************************************************************************
/* Floating point opcode parsing */
//*****************************************************************************
static size_t ParseEscFP(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    PCDISOPCODE fpop;
    RT_NOREF_PV(pOp);

    uint8_t    ModRM = disReadByte(pDis, offInstr);
    uint8_t    index = pDis->x86.bOpCode - 0xD8;
    if (ModRM <= 0xBF)
    {
        fpop            = &(g_apMapX86_FP_Low[index])[MODRM_REG(ModRM)];
        pDis->pCurInstr = fpop;

        // Should contain the parameter type on input
        pDis->aParams[0].x86.fParam = fpop->fParam1;
        pDis->aParams[1].x86.fParam = fpop->fParam2;
    }
    else
    {
        fpop            = &(g_apMapX86_FP_High[index])[ModRM - 0xC0];
        pDis->pCurInstr = fpop;
    }

    /*
     * Apply filter to instruction type to determine if a full disassembly is required.
     * @note Multibyte opcodes are always marked harmless until the final byte.
     */
    if ((fpop->fOpType & pDis->x86.fFilter) == 0)
        pDis->x86.pfnDisasmFnTable = g_apfnCalcSize;
    else
        /* Not filtered out -> full disassembly */
        pDis->x86.pfnDisasmFnTable = g_apfnFullDisasm;

    /* Correct the operand size if the instruction is marked as forced or default 64 bits */
    if (  pDis->uCpuMode != DISCPUMODE_64BIT
        || !(fpop->fOpType & (DISOPTYPE_X86_FORCED_64_OP_SIZE | DISOPTYPE_X86_DEFAULT_64_OP_SIZE)))
    { /* probably likely */ }
    else
    {
        /* Note: redundant, but just in case this ever changes */
        if (fpop->fOpType & DISOPTYPE_X86_FORCED_64_OP_SIZE)
            pDis->x86.uOpMode = DISCPUMODE_64BIT;
        else if (    (fpop->fOpType & DISOPTYPE_X86_DEFAULT_64_OP_SIZE)
                 &&  !(pDis->x86.fPrefix & DISPREFIX_OPSIZE))
            pDis->x86.uOpMode = DISCPUMODE_64BIT;
    }

    // Little hack to make sure the ModRM byte is included in the returned size
    if (fpop->idxParse1 != IDX_ParseModRM && fpop->idxParse2 != IDX_ParseModRM)
        offInstr++; //ModRM byte

    if (fpop->idxParse1 != IDX_ParseNop)
        offInstr = pDis->x86.pfnDisasmFnTable[fpop->idxParse1](offInstr, fpop, pDis, pParam);

    if (fpop->idxParse2 != IDX_ParseNop)
        offInstr = pDis->x86.pfnDisasmFnTable[fpop->idxParse2](offInstr, fpop, pDis, pParam);

    return offInstr;
}


/********************************************************************************************************************************
 *
 *
 * SIB byte: (not 16-bit mode)
 * 7 - 6  5 - 3  2-0
 * Scale  Index  Base
 *
 *
 ********************************************************************************************************************************/
static void UseSIB(PDISSTATE pDis, PDISOPPARAM pParam)
{
    unsigned scale = pDis->x86.SIB.Bits.Scale;
    uint8_t  base  = pDis->x86.SIB.Bits.Base;
    uint8_t  index = pDis->x86.SIB.Bits.Index;

    unsigned regtype, vregtype;
    /* There's no way to distinguish between SIB and VSIB
     * and having special parameter to parse explicitly VSIB
     * is not an options since only one instruction (gather)
     * supports it currently. May be changed in the future. */
        if (pDis->x86.uAddrMode == DISCPUMODE_32BIT)
            regtype    = DISUSE_REG_GEN32;
        else
            regtype    = DISUSE_REG_GEN64;
    if (pDis->pCurInstr->uOpcode == OP_GATHER)
        vregtype = (VEXREG_IS256B(pDis->x86.bVexDestReg) ? DISUSE_REG_YMM : DISUSE_REG_XMM);
    else
        vregtype = regtype;

    if (index != 4)
    {
        pParam->fUse |= DISUSE_INDEX | vregtype;
        pParam->x86.Index.idxGenReg = index;

        if (scale != 0)
        {
            pParam->fUse  |= DISUSE_SCALE;
            pParam->x86.uScale = (uint8_t)(1 << scale);
        }
    }

    if (base == 5 && pDis->x86.ModRM.Bits.Mod == 0)
    {
        // [scaled index] + disp32
        if (pDis->x86.uAddrMode == DISCPUMODE_32BIT)
        {
            pParam->fUse |= DISUSE_DISPLACEMENT32;
            pParam->x86.uDisp.i32 = pDis->x86.i32SibDisp;
        }
        else
        {   /* sign-extend to 64 bits */
            pParam->fUse |= DISUSE_DISPLACEMENT64;
            pParam->x86.uDisp.i64 = pDis->x86.i32SibDisp;
        }
    }
    else
    {
        pParam->fUse |= DISUSE_BASE | regtype;
        pParam->x86.Base.idxGenReg = base;
    }
    return;   /* Already fetched everything in ParseSIB; no size returned */
}


static size_t ParseSIB(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp); RT_NOREF_PV(pParam);

    uint8_t SIB = disReadByte(pDis, offInstr);
    offInstr++;

    pDis->x86.SIB.Bits.Base  = SIB_BASE(SIB);
    pDis->x86.SIB.Bits.Index = SIB_INDEX(SIB);
    pDis->x86.SIB.Bits.Scale = SIB_SCALE(SIB);

    if (pDis->x86.fPrefix & DISPREFIX_REX)
    {
        /* REX.B extends the Base field if not scaled index + disp32 */
        if (!(pDis->x86.SIB.Bits.Base == 5 && pDis->x86.ModRM.Bits.Mod == 0))
            pDis->x86.SIB.Bits.Base  |= (!!(pDis->x86.fRexPrefix & DISPREFIX_REX_FLAGS_B)) << 3;

        pDis->x86.SIB.Bits.Index |= (!!(pDis->x86.fRexPrefix & DISPREFIX_REX_FLAGS_X)) << 3;
    }

    if (    pDis->x86.SIB.Bits.Base == 5
        &&  pDis->x86.ModRM.Bits.Mod == 0)
    {
        /* Additional 32 bits displacement. No change in long mode. */
        pDis->x86.i32SibDisp = (int32_t)disReadDWord(pDis, offInstr);
        offInstr += 4;
    }
    return offInstr;
}


static size_t ParseSIB_SizeOnly(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp); RT_NOREF_PV(pParam);

    uint8_t SIB = disReadByte(pDis, offInstr);
    offInstr++;

    pDis->x86.SIB.Bits.Base  = SIB_BASE(SIB);
    pDis->x86.SIB.Bits.Index = SIB_INDEX(SIB);
    pDis->x86.SIB.Bits.Scale = SIB_SCALE(SIB);

    if (pDis->x86.fPrefix & DISPREFIX_REX)
    {
        /* REX.B extends the Base field. */
        pDis->x86.SIB.Bits.Base  |= ((!!(pDis->x86.fRexPrefix & DISPREFIX_REX_FLAGS_B)) << 3);
        /* REX.X extends the Index field. */
        pDis->x86.SIB.Bits.Index |= ((!!(pDis->x86.fRexPrefix & DISPREFIX_REX_FLAGS_X)) << 3);
    }

    if (    pDis->x86.SIB.Bits.Base == 5
        &&  pDis->x86.ModRM.Bits.Mod == 0)
    {
        /* Additional 32 bits displacement. No change in long mode. */
        offInstr += 4;
    }
    return offInstr;
}



/********************************************************************************************************************************
 *
 *
 * ModR/M byte:
 * 7 - 6  5 - 3       2-0
 * Mod    Reg/Opcode  R/M
 *
 *
 ********************************************************************************************************************************/
static void disasmModRMReg(unsigned idx, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam, int fRegAddr)
{
    RT_NOREF_PV(pOp); RT_NOREF_PV(pDis);

#ifdef LOG_ENABLED
    unsigned type    = OP_PARM_VTYPE(pParam->x86.fParam);
#endif
    unsigned subtype = OP_PARM_VSUBTYPE(pParam->x86.fParam);
    if (fRegAddr)
        subtype = (pDis->x86.uAddrMode == DISCPUMODE_64BIT) ? OP_PARM_q : OP_PARM_d;
    else if (subtype == OP_PARM_v || subtype == OP_PARM_NONE || subtype == OP_PARM_y)
    {
        switch (pDis->x86.uOpMode)
        {
        case DISCPUMODE_32BIT:
            subtype = OP_PARM_d;
            break;
        case DISCPUMODE_64BIT:
            subtype = OP_PARM_q;
            break;
        case DISCPUMODE_16BIT:
            if (subtype != OP_PARM_y) /** @todo r=bird: This cannot be right! OP_PARM_y should translate to OP_PARM_d (32-bit), shouldn't it? */
                subtype = OP_PARM_w;
            break;
        default:
            /* make gcc happy */
            break;
        }
    }

    switch (subtype)
    {
    case OP_PARM_b:
        Assert(idx < (pDis->x86.fPrefix & DISPREFIX_REX ? 16U : 8U));

        /* AH, BH, CH & DH map to DIL, SIL, EBL & SPL when a rex prefix is present. */
        /* Intel 64 and IA-32 Architectures Software Developer's Manual: 3.4.1.1 */
        if (    (pDis->x86.fPrefix & DISPREFIX_REX)
            &&  idx >= DISGREG_AH
            &&  idx <= DISGREG_BH)
        {
            idx += (DISGREG_SPL - DISGREG_AH);
        }

        pParam->fUse |= DISUSE_REG_GEN8;
        pParam->x86.Base.idxGenReg = (uint8_t)idx;
        break;

    case OP_PARM_w:
        Assert(idx < (pDis->x86.fPrefix & DISPREFIX_REX ? 16U : 8U));

        pParam->fUse |= DISUSE_REG_GEN16;
        pParam->x86.Base.idxGenReg = (uint8_t)idx;
        break;

    case OP_PARM_d:
        Assert(idx < (pDis->x86.fPrefix & DISPREFIX_REX ? 16U : 8U));

        if (   !(pOp->fOpType & DISOPTYPE_X86_DEFAULT_64_OP_SIZE) /* Tweak for vpmovmskb & pmovmskb. */
            || pDis->x86.uOpMode != DISCPUMODE_64BIT)
            pParam->fUse |= DISUSE_REG_GEN32;
        else
            pParam->fUse |= DISUSE_REG_GEN64;
        pParam->x86.Base.idxGenReg = (uint8_t)idx;
        break;

    case OP_PARM_q:
        pParam->fUse |= DISUSE_REG_GEN64;
        pParam->x86.Base.idxGenReg = (uint8_t)idx;
        break;

    default:
        Log(("disasmModRMReg %x:%x failed!!\n", type, subtype));
        pDis->rc = VERR_DIS_INVALID_MODRM;
        break;
    }
}


static void disasmModRMReg16(unsigned idx, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    static const uint8_t s_auBaseModRMReg16[8]  =
    { DISGREG_BX, DISGREG_BX, DISGREG_BP, DISGREG_BP, DISGREG_SI, DISGREG_DI, DISGREG_BP, DISGREG_BX };

    RT_NOREF_PV(pDis); RT_NOREF_PV(pOp);
    pParam->fUse |= DISUSE_REG_GEN16;
    pParam->x86.Base.idxGenReg = s_auBaseModRMReg16[idx];
    if (idx < 4)
    {
        static const uint8_t s_auIndexModRMReg16[4] = { DISGREG_SI, DISGREG_DI, DISGREG_SI, DISGREG_DI };
        pParam->fUse |= DISUSE_INDEX;
        pParam->x86.Index.idxGenReg = s_auIndexModRMReg16[idx];
    }
}


static void disasmModRMSReg(unsigned idx, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp);
    if (idx >= DISSELREG_END)
    {
        Log(("disasmModRMSReg %d failed!!\n", idx));
        pDis->rc = VERR_DIS_INVALID_PARAMETER;
        return;
    }

    pParam->fUse |= DISUSE_REG_SEG;
    pParam->x86.Base.idxSegReg = (uint8_t)idx;
}


static size_t UseModRM(size_t const offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    unsigned vtype = OP_PARM_VTYPE(pParam->x86.fParam);
    uint8_t  reg = pDis->x86.ModRM.Bits.Reg;
    uint8_t  mod = pDis->x86.ModRM.Bits.Mod;
    uint8_t  rm  = pDis->x86.ModRM.Bits.Rm;

    switch (vtype)
    {
    case OP_PARM_G: //general purpose register
        disasmModRMReg(reg, pOp, pDis, pParam, 0);
        return offInstr;

    default:
        if (IS_OP_PARM_RARE(vtype))
        {
            switch (vtype)
            {
            case OP_PARM_C: //control register
                pParam->fUse |= DISUSE_REG_CR;

                if (    pDis->pCurInstr->uOpcode == OP_MOV_CR
                    &&  pDis->x86.uOpMode == DISCPUMODE_32BIT
                    &&  (pDis->x86.fPrefix & DISPREFIX_LOCK))
                {
                    pDis->x86.fPrefix &= ~DISPREFIX_LOCK;
                    pParam->x86.Base.idxCtrlReg = DISCREG_CR8;
                }
                else
                    pParam->x86.Base.idxCtrlReg = reg;
                return offInstr;

            case OP_PARM_D: //debug register
                pParam->fUse |= DISUSE_REG_DBG;
                pParam->x86.Base.idxDbgReg = reg;
                return offInstr;

            case OP_PARM_Q: //MMX or memory operand
                if (mod != 3)
                    break;  /* memory operand */
                reg = rm; /* the RM field specifies the xmm register */
                RT_FALL_THRU();

            case OP_PARM_P: //MMX register
                reg &= 7;   /* REX.R has no effect here */
                pParam->fUse |= DISUSE_REG_MMX;
                pParam->x86.Base.idxMmxReg = reg;
                return offInstr;

            case OP_PARM_S: //segment register
                reg &= 7;   /* REX.R has no effect here */
                disasmModRMSReg(reg, pOp, pDis, pParam);
                pParam->fUse |= DISUSE_REG_SEG;
                return offInstr;

            case OP_PARM_T: //test register
                reg &= 7;   /* REX.R has no effect here */
                pParam->fUse |= DISUSE_REG_TEST;
                pParam->x86.Base.idxTestReg = reg;
                return offInstr;

            case OP_PARM_W: //XMM register or memory operand
                if (mod != 3)
                    break;  /* memory operand */
                RT_FALL_THRU();

            case OP_PARM_U: // XMM/YMM register
                reg = rm; /* the RM field specifies the xmm register */
                RT_FALL_THRU();

            case OP_PARM_V: //XMM register
                if (VEXREG_IS256B(pDis->x86.bVexDestReg)
                    && OP_PARM_VSUBTYPE(pParam->x86.fParam) != OP_PARM_dq
                    && OP_PARM_VSUBTYPE(pParam->x86.fParam) != OP_PARM_q
                    && OP_PARM_VSUBTYPE(pParam->x86.fParam) != OP_PARM_d
                    && OP_PARM_VSUBTYPE(pParam->x86.fParam) != OP_PARM_w)
                {
                    // Use YMM register if VEX.L is set.
                    pParam->fUse |= DISUSE_REG_YMM;
                    pParam->x86.Base.idxYmmReg = reg;
                }
                else
                {
                    pParam->fUse |= DISUSE_REG_XMM;
                    pParam->x86.Base.idxXmmReg = reg;
                }
                return offInstr;
            }
        }
    }

    /** @todo bound */

    if (pDis->x86.uAddrMode != DISCPUMODE_16BIT)
    {
        Assert(pDis->x86.uAddrMode == DISCPUMODE_32BIT || pDis->x86.uAddrMode == DISCPUMODE_64BIT);

        /*
         * Note: displacements in long mode are 8 or 32 bits and sign-extended to 64 bits
         */
        switch (mod)
        {
        case 0: //effective address
            if (rm == 4)    /* SIB byte follows ModRM */
                UseSIB(pDis, pParam);
            else
            if (rm == 5)
            {
                /* 32 bits displacement */
                if (pDis->uCpuMode != DISCPUMODE_64BIT)
                {
                    pParam->fUse |= DISUSE_DISPLACEMENT32;
                    pParam->x86.uDisp.i32 = pDis->x86.i32SibDisp;
                }
                else
                {
                    pParam->fUse |= DISUSE_RIPDISPLACEMENT32;
                    pParam->x86.uDisp.i32 = pDis->x86.i32SibDisp;
                }
            }
            else
            {   //register address
                pParam->fUse |= DISUSE_BASE;
                disasmModRMReg(rm, pOp, pDis, pParam, 1);
            }
            break;

        case 1: //effective address + 8 bits displacement
            if (rm == 4)    /* SIB byte follows ModRM */
                UseSIB(pDis, pParam);
            else
            {
                pParam->fUse |= DISUSE_BASE;
                disasmModRMReg(rm, pOp, pDis, pParam, 1);
            }
            pParam->x86.uDisp.i8 = pDis->x86.i32SibDisp;
            pParam->fUse |= DISUSE_DISPLACEMENT8;
            break;

        case 2: //effective address + 32 bits displacement
            if (rm == 4)    /* SIB byte follows ModRM */
                UseSIB(pDis, pParam);
            else
            {
                pParam->fUse |= DISUSE_BASE;
                disasmModRMReg(rm, pOp, pDis, pParam, 1);
            }
            pParam->x86.uDisp.i32 = pDis->x86.i32SibDisp;
            pParam->fUse |= DISUSE_DISPLACEMENT32;
            break;

        case 3: //registers
            disasmModRMReg(rm, pOp, pDis, pParam, 0);
            break;
        }
    }
    else
    {//16 bits addressing mode
        switch (mod)
        {
        case 0: //effective address
            if (rm == 6)
            {//16 bits displacement
                pParam->x86.uDisp.i16 = pDis->x86.i32SibDisp;
                pParam->fUse |= DISUSE_DISPLACEMENT16;
            }
            else
            {
                pParam->fUse |= DISUSE_BASE;
                disasmModRMReg16(rm, pOp, pDis, pParam);
            }
            break;

        case 1: //effective address + 8 bits displacement
            disasmModRMReg16(rm, pOp, pDis, pParam);
            pParam->x86.uDisp.i8 = pDis->x86.i32SibDisp;
            pParam->fUse |= DISUSE_BASE | DISUSE_DISPLACEMENT8;
            break;

        case 2: //effective address + 16 bits displacement
            disasmModRMReg16(rm, pOp, pDis, pParam);
            pParam->x86.uDisp.i16 = pDis->x86.i32SibDisp;
            pParam->fUse |= DISUSE_BASE | DISUSE_DISPLACEMENT16;
            break;

        case 3: //registers
            disasmModRMReg(rm, pOp, pDis, pParam, 0);
            break;
        }
    }
    return offInstr;
}
//*****************************************************************************
// Query the size of the ModRM parameters and fetch the immediate data (if any)
//*****************************************************************************
static size_t QueryModRM(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    uint8_t mod = pDis->x86.ModRM.Bits.Mod;
    uint8_t rm  = pDis->x86.ModRM.Bits.Rm;

    if (pDis->x86.uAddrMode != DISCPUMODE_16BIT)
    {
        Assert(pDis->x86.uAddrMode == DISCPUMODE_32BIT || pDis->x86.uAddrMode == DISCPUMODE_64BIT);

        /*
         * Note: displacements in long mode are 8 or 32 bits and sign-extended to 64 bits
         */
        if (mod != 3 && rm == 4) /* SIB byte follows ModRM */
            offInstr = ParseSIB(offInstr, pOp, pDis, pParam);

        switch (mod)
        {
        case 0: /* Effective address */
            if (rm == 5)    /* 32 bits displacement */
            {
                pDis->x86.i32SibDisp = (int32_t)disReadDWord(pDis, offInstr);
                offInstr += 4;
            }
            /* else register address */
            break;

        case 1: /* Effective address + 8 bits displacement */
            pDis->x86.i32SibDisp = (int8_t)disReadByte(pDis, offInstr);
            offInstr++;
            break;

        case 2: /* Effective address + 32 bits displacement */
            pDis->x86.i32SibDisp = (int32_t)disReadDWord(pDis, offInstr);
            offInstr += 4;
            break;

        case 3: /* registers */
            break;
        }
    }
    else
    {
        /* 16 bits mode */
        switch (mod)
        {
        case 0: /* Effective address */
            if (rm == 6)
            {
                pDis->x86.i32SibDisp = disReadWord(pDis, offInstr);
                offInstr += 2;
            }
            /* else register address */
            break;

        case 1: /* Effective address + 8 bits displacement */
            pDis->x86.i32SibDisp = (int8_t)disReadByte(pDis, offInstr);
            offInstr++;
            break;

        case 2: /* Effective address + 32 bits displacement */
            pDis->x86.i32SibDisp = (int16_t)disReadWord(pDis, offInstr);
            offInstr += 2;
            break;

        case 3: /* registers */
            break;
        }
    }
    return offInstr;
}
//*****************************************************************************
// Parse the ModRM parameters and fetch the immediate data (if any)
//*****************************************************************************
static size_t QueryModRM_SizeOnly(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    uint8_t mod = pDis->x86.ModRM.Bits.Mod;
    uint8_t rm  = pDis->x86.ModRM.Bits.Rm;

    if (pDis->x86.uAddrMode != DISCPUMODE_16BIT)
    {
        Assert(pDis->x86.uAddrMode == DISCPUMODE_32BIT || pDis->x86.uAddrMode == DISCPUMODE_64BIT);
        /*
         * Note: displacements in long mode are 8 or 32 bits and sign-extended to 64 bits
         */
        if (mod != 3 && rm == 4)
        {   /* SIB byte follows ModRM */
            offInstr = ParseSIB_SizeOnly(offInstr, pOp, pDis, pParam);
        }

        switch (mod)
        {
        case 0: //effective address
            if (rm == 5)   /* 32 bits displacement */
                offInstr += 4;
            /* else register address */
            break;

        case 1: /* Effective address + 8 bits displacement */
            offInstr += 1;
            break;

        case 2: /* Effective address + 32 bits displacement */
            offInstr += 4;
            break;

        case 3: /* registers */
            break;
        }
    }
    else
    {
        /* 16 bits mode */
        switch (mod)
        {
        case 0: //effective address
            if (rm == 6)
                offInstr += 2;
            /* else register address */
            break;

        case 1: /* Effective address + 8 bits displacement */
            offInstr++;
            break;

        case 2: /* Effective address + 32 bits displacement */
            offInstr += 2;
            break;

        case 3: /* registers */
            break;
        }
    }
    return offInstr;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseIllegal(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp); RT_NOREF_PV(pParam); RT_NOREF_PV(pDis);
    AssertFailed();
    return offInstr;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseModRM(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    uint8_t ModRM = disReadByte(pDis, offInstr);
    offInstr++;

    pDis->x86.ModRM.Bits.Rm  = MODRM_RM(ModRM);
    pDis->x86.ModRM.Bits.Mod = MODRM_MOD(ModRM);
    pDis->x86.ModRM.Bits.Reg = MODRM_REG(ModRM);

    /* Disregard the mod bits for certain instructions (mov crx, mov drx).
     *
     * From the AMD manual:
     * This instruction is always treated as a register-to-register (MOD = 11) instruction, regardless of the
     * encoding of the MOD field in the MODR/M byte.
     */
    if (!(pOp->fOpType & DISOPTYPE_X86_MOD_FIXED_11))
    { /* likely */ }
    else
        pDis->x86.ModRM.Bits.Mod = 3;

    if (pDis->x86.fPrefix & DISPREFIX_REX)
    {
        Assert(pDis->uCpuMode == DISCPUMODE_64BIT);

        /* REX.R extends the Reg field. */
        pDis->x86.ModRM.Bits.Reg |= (!!(pDis->x86.fRexPrefix & DISPREFIX_REX_FLAGS_R)) << 3;

        /* REX.B extends the Rm field if there is no SIB byte nor a 32 bits displacement */
#if 1
        if (   pDis->x86.ModRM.Bits.Mod == 3 /* The register mode has neither SIB nor disp32. */
            || (   pDis->x86.ModRM.Bits.Rm != 4      /* SIB */
                && (   pDis->x86.ModRM.Bits.Mod != 0 /* disp32/PCREL is mod=0 rm=5 */
                    || pDis->x86.ModRM.Bits.Rm  != 5)))
#else
        if (!(    pDis->x86.ModRM.Bits.Mod != 3
              &&  pDis->x86.ModRM.Bits.Rm  == 4)
            &&
            !(    pDis->x86.ModRM.Bits.Mod == 0
              &&  pDis->x86.ModRM.Bits.Rm  == 5))
#endif
            pDis->x86.ModRM.Bits.Rm |= (!!(pDis->x86.fRexPrefix & DISPREFIX_REX_FLAGS_B)) << 3;
    }
    offInstr = QueryModRM(offInstr, pOp, pDis, pParam);

    return UseModRM(offInstr, pOp, pDis, pParam);
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseModRM_SizeOnly(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    uint8_t ModRM = disReadByte(pDis, offInstr);
    offInstr++;

    pDis->x86.ModRM.Bits.Rm  = MODRM_RM(ModRM);
    pDis->x86.ModRM.Bits.Mod = MODRM_MOD(ModRM);
    pDis->x86.ModRM.Bits.Reg = MODRM_REG(ModRM);

    /* Disregard the mod bits for certain instructions (mov crx, mov drx).
     *
     * From the AMD manual:
     * This instruction is always treated as a register-to-register (MOD = 11) instruction, regardless of the
     * encoding of the MOD field in the MODR/M byte.
     */
    if (pOp->fOpType & DISOPTYPE_X86_MOD_FIXED_11)
        pDis->x86.ModRM.Bits.Mod = 3;

    if (pDis->x86.fPrefix & DISPREFIX_REX)
    {
        Assert(pDis->uCpuMode == DISCPUMODE_64BIT);

        /* REX.R extends the Reg field. */
        pDis->x86.ModRM.Bits.Reg |= ((!!(pDis->x86.fRexPrefix & DISPREFIX_REX_FLAGS_R)) << 3);

        /* REX.B extends the Rm field if there is no SIB byte nor a 32 bits displacement */
        if (!(    pDis->x86.ModRM.Bits.Mod != 3
              &&  pDis->x86.ModRM.Bits.Rm  == 4)
            &&
            !(    pDis->x86.ModRM.Bits.Mod == 0
              &&  pDis->x86.ModRM.Bits.Rm  == 5))
        {
            pDis->x86.ModRM.Bits.Rm |= ((!!(pDis->x86.fRexPrefix & DISPREFIX_REX_FLAGS_B)) << 3);
        }
    }

    offInstr = QueryModRM_SizeOnly(offInstr, pOp, pDis, pParam);

    /* UseModRM is not necessary here; we're only interested in the opcode size */
    return offInstr;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseModFence(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp); RT_NOREF_PV(pParam); RT_NOREF_PV(pDis);
    /* Note! Only used in group 15, so we must account for the mod/rm byte. */
    return offInstr + 1;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseImmByte(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp);
    uint8_t byte = disReadByte(pDis, offInstr);
    if (pParam->x86.fParam == OP_PARM_Lx)
    {
        pParam->fUse  |= (VEXREG_IS256B(pDis->x86.bVexDestReg) ? DISUSE_REG_YMM : DISUSE_REG_XMM);

        // Ignore MSB in 32-bit mode.
        if (pDis->uCpuMode == DISCPUMODE_32BIT)
            byte &= 0x7f;

        pParam->x86.Base.idxXmmReg = byte >> 4;
    }
    else
    {
        pParam->uValue = byte;
        pParam->fUse  |= DISUSE_IMMEDIATE8;
        pParam->x86.cb     = sizeof(uint8_t);
    }
    return offInstr + 1;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseImmByte_SizeOnly(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp); RT_NOREF_PV(pParam); RT_NOREF_PV(pDis);
    return offInstr + 1;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseImmByteSX(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp);
    if (pDis->x86.uOpMode == DISCPUMODE_32BIT)
    {
        pParam->uValue = (uint32_t)(int8_t)disReadByte(pDis, offInstr);
        pParam->fUse  |= DISUSE_IMMEDIATE32_SX8;
        pParam->x86.cb     = sizeof(uint32_t);
    }
    else
    if (pDis->x86.uOpMode == DISCPUMODE_64BIT)
    {
        pParam->uValue = (uint64_t)(int8_t)disReadByte(pDis, offInstr);
        pParam->fUse  |= DISUSE_IMMEDIATE64_SX8;
        pParam->x86.cb     = sizeof(uint64_t);
    }
    else
    {
        pParam->uValue = (uint16_t)(int8_t)disReadByte(pDis, offInstr);
        pParam->fUse  |= DISUSE_IMMEDIATE16_SX8;
        pParam->x86.cb     = sizeof(uint16_t);
    }
    return offInstr + 1;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseImmByteSX_SizeOnly(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp); RT_NOREF_PV(pParam); RT_NOREF_PV(pDis);
    return offInstr + 1;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseImmUshort(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp);
    pParam->uValue = disReadWord(pDis, offInstr);
    pParam->fUse  |= DISUSE_IMMEDIATE16;
    pParam->x86.cb     = sizeof(uint16_t);
    return offInstr + 2;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseImmUshort_SizeOnly(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp); RT_NOREF_PV(pParam); RT_NOREF_PV(pDis);
    return offInstr + 2;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseImmUlong(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp);
    pParam->uValue = disReadDWord(pDis, offInstr);
    pParam->fUse  |= DISUSE_IMMEDIATE32;
    pParam->x86.cb     = sizeof(uint32_t);
    return offInstr + 4;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseImmUlong_SizeOnly(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp); RT_NOREF_PV(pParam); RT_NOREF_PV(pDis);
    return offInstr + 4;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseImmQword(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp);
    pParam->uValue = disReadQWord(pDis, offInstr);
    pParam->fUse  |= DISUSE_IMMEDIATE64;
    pParam->x86.cb     = sizeof(uint64_t);
    return offInstr + 8;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseImmQword_SizeOnly(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(offInstr); RT_NOREF_PV(pOp); RT_NOREF_PV(pParam); RT_NOREF_PV(pDis);
    return offInstr + 8;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseImmV(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp);
    if (pDis->x86.uOpMode == DISCPUMODE_32BIT)
    {
        pParam->uValue = disReadDWord(pDis, offInstr);
        pParam->fUse  |= DISUSE_IMMEDIATE32;
        pParam->x86.cb     = sizeof(uint32_t);
        return offInstr + 4;
    }

    if (pDis->x86.uOpMode == DISCPUMODE_64BIT)
    {
        pParam->uValue = disReadQWord(pDis, offInstr);
        pParam->fUse  |= DISUSE_IMMEDIATE64;
        pParam->x86.cb     = sizeof(uint64_t);
        return offInstr + 8;
    }

    pParam->uValue = disReadWord(pDis, offInstr);
    pParam->fUse  |= DISUSE_IMMEDIATE16;
    pParam->x86.cb     = sizeof(uint16_t);
    return offInstr + 2;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseImmV_SizeOnly(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(offInstr); RT_NOREF_PV(pOp); RT_NOREF_PV(pParam);
    if (pDis->x86.uOpMode == DISCPUMODE_32BIT)
        return offInstr + 4;
    if (pDis->x86.uOpMode == DISCPUMODE_64BIT)
        return offInstr + 8;
    return offInstr + 2;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseImmZ(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp);
    /* Word for 16-bit operand-size or doubleword for 32 or 64-bit operand-size. */
    if (pDis->x86.uOpMode == DISCPUMODE_16BIT)
    {
        pParam->uValue = disReadWord(pDis, offInstr);
        pParam->fUse  |= DISUSE_IMMEDIATE16;
        pParam->x86.cb     = sizeof(uint16_t);
        return offInstr + 2;
    }

    /* 64 bits op mode means *sign* extend to 64 bits. */
    if (pDis->x86.uOpMode == DISCPUMODE_64BIT)
    {
        pParam->uValue = (uint64_t)(int32_t)disReadDWord(pDis, offInstr);
        pParam->fUse  |= DISUSE_IMMEDIATE64;
        pParam->x86.cb     = sizeof(uint64_t);
    }
    else
    {
        pParam->uValue = disReadDWord(pDis, offInstr);
        pParam->fUse  |= DISUSE_IMMEDIATE32;
        pParam->x86.cb     = sizeof(uint32_t);
    }
    return offInstr + 4;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseImmZ_SizeOnly(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(offInstr); RT_NOREF_PV(pOp); RT_NOREF_PV(pParam);
    /* Word for 16-bit operand-size or doubleword for 32 or 64-bit operand-size. */
    if (pDis->x86.uOpMode == DISCPUMODE_16BIT)
        return offInstr + 2;
    return offInstr + 4;
}

//*****************************************************************************
// Relative displacement for branches (rel. to next instruction)
//*****************************************************************************
static size_t ParseImmBRel(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp);
    pParam->uValue = disReadByte(pDis, offInstr);
    pParam->fUse  |= DISUSE_IMMEDIATE8_REL;
    pParam->x86.cb     = sizeof(uint8_t);
    return offInstr + 1;
}
//*****************************************************************************
// Relative displacement for branches (rel. to next instruction)
//*****************************************************************************
static size_t ParseImmBRel_SizeOnly(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(offInstr); RT_NOREF_PV(pOp); RT_NOREF_PV(pParam); RT_NOREF_PV(pDis);
    return offInstr + 1;
}
//*****************************************************************************
// Relative displacement for branches (rel. to next instruction)
//*****************************************************************************
static size_t ParseImmVRel(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp);
    if (pDis->x86.uOpMode == DISCPUMODE_32BIT)
    {
        pParam->uValue = disReadDWord(pDis, offInstr);
        pParam->fUse  |= DISUSE_IMMEDIATE32_REL;
        pParam->x86.cb     = sizeof(int32_t);
        return offInstr + 4;
    }

    if (pDis->x86.uOpMode == DISCPUMODE_64BIT)
    {
        /* 32 bits relative immediate sign extended to 64 bits. */
        pParam->uValue = (uint64_t)(int32_t)disReadDWord(pDis, offInstr);
        pParam->fUse  |= DISUSE_IMMEDIATE64_REL;
        pParam->x86.cb     = sizeof(int64_t);
        return offInstr + 4;
    }

    pParam->uValue = disReadWord(pDis, offInstr);
    pParam->fUse  |= DISUSE_IMMEDIATE16_REL;
    pParam->x86.cb     = sizeof(int16_t);
    return offInstr + 2;
}
//*****************************************************************************
// Relative displacement for branches (rel. to next instruction)
//*****************************************************************************
static size_t ParseImmVRel_SizeOnly(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(offInstr); RT_NOREF_PV(pOp); RT_NOREF_PV(pParam);
    if (pDis->x86.uOpMode == DISCPUMODE_16BIT)
        return offInstr + 2;
    /* Both 32 & 64 bits mode use 32 bits relative immediates. */
    return offInstr + 4;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseImmAddr(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp);
    if (pDis->x86.uAddrMode == DISCPUMODE_32BIT)
    {
        if (OP_PARM_VSUBTYPE(pParam->x86.fParam) == OP_PARM_p)
        {
            /* far 16:32 pointer */
            pParam->uValue = disReadDWord(pDis, offInstr);
            *((uint32_t*)&pParam->uValue+1) = disReadWord(pDis, offInstr+sizeof(uint32_t));
            pParam->fUse   |= DISUSE_IMMEDIATE_ADDR_16_32;
            pParam->x86.cb     = sizeof(uint16_t) + sizeof(uint32_t);
            return offInstr + 4 + 2;
        }

        /*
         * near 32 bits pointer
         *
         * Note: used only in "mov al|ax|eax, [Addr]" and "mov [Addr], al|ax|eax"
         * so we treat it like displacement.
         */
        pParam->x86.uDisp.u32 = disReadDWord(pDis, offInstr);
        pParam->fUse  |= DISUSE_DISPLACEMENT32;
        pParam->x86.cb     = sizeof(uint32_t);
        return offInstr + 4;
    }

    if (pDis->x86.uAddrMode == DISCPUMODE_64BIT)
    {
        /*
         * near 64 bits pointer
         *
         * Note: used only in "mov al|ax|eax, [Addr]" and "mov [Addr], al|ax|eax"
         * so we treat it like displacement.
         */
        Assert(OP_PARM_VSUBTYPE(pParam->x86.fParam) != OP_PARM_p);
        pParam->x86.uDisp.u64 = disReadQWord(pDis, offInstr);
        pParam->fUse  |= DISUSE_DISPLACEMENT64;
        pParam->x86.cb     = sizeof(uint64_t);
        return offInstr + 8;
    }
    if (OP_PARM_VSUBTYPE(pParam->x86.fParam) == OP_PARM_p)
    {
        /* far 16:16 pointer */
        pParam->uValue = disReadDWord(pDis, offInstr);
        pParam->fUse  |= DISUSE_IMMEDIATE_ADDR_16_16;
        pParam->x86.cb     = 2*sizeof(uint16_t);
        return offInstr + 4;
    }

    /*
     * near 16 bits pointer
     *
     * Note: used only in "mov al|ax|eax, [Addr]" and "mov [Addr], al|ax|eax"
     * so we treat it like displacement.
     */
    pParam->x86.uDisp.i16 = disReadWord(pDis, offInstr);
    pParam->fUse  |= DISUSE_DISPLACEMENT16;
    pParam->x86.cb     = sizeof(uint16_t);
    return offInstr + 2;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseImmAddr_SizeOnly(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(offInstr); RT_NOREF_PV(pOp);
    if (pDis->x86.uAddrMode == DISCPUMODE_32BIT)
    {
        if (OP_PARM_VSUBTYPE(pParam->x86.fParam) == OP_PARM_p)
            return offInstr + 4 + 2; /* far 16:32 pointer */
        return offInstr + 4;         /* near 32 bits pointer */
    }
    if (pDis->x86.uAddrMode == DISCPUMODE_64BIT)
    {
        Assert(OP_PARM_VSUBTYPE(pParam->x86.fParam) != OP_PARM_p);
        return offInstr + 8;
    }
    if (OP_PARM_VSUBTYPE(pParam->x86.fParam) == OP_PARM_p)
        return offInstr + 4;        /* far 16:16 pointer */
    return offInstr + 2;            /* near 16 bits pointer */
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseImmAddrF(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp);
    // immediate far pointers - only 16:16 or 16:32; determined by operand, *not* address size!
    Assert(pDis->x86.uOpMode == DISCPUMODE_16BIT || pDis->x86.uOpMode == DISCPUMODE_32BIT);
    Assert(OP_PARM_VSUBTYPE(pParam->x86.fParam) == OP_PARM_p);
    if (pDis->x86.uOpMode == DISCPUMODE_32BIT)
    {
        // far 16:32 pointer
        pParam->uValue = disReadDWord(pDis, offInstr);
        *((uint32_t*)&pParam->uValue+1) = disReadWord(pDis, offInstr+sizeof(uint32_t));
        pParam->fUse   |= DISUSE_IMMEDIATE_ADDR_16_32;
        pParam->x86.cb     = sizeof(uint16_t) + sizeof(uint32_t);
        return offInstr + 4 + 2;
    }

    // far 16:16 pointer
    pParam->uValue = disReadDWord(pDis, offInstr);
    pParam->fUse  |= DISUSE_IMMEDIATE_ADDR_16_16;
    pParam->x86.cb     = 2*sizeof(uint16_t);
    return offInstr + 2 + 2;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseImmAddrF_SizeOnly(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(offInstr); RT_NOREF_PV(pOp);
    // immediate far pointers - only 16:16 or 16:32
    Assert(pDis->x86.uOpMode == DISCPUMODE_16BIT || pDis->x86.uOpMode == DISCPUMODE_32BIT);
    Assert(OP_PARM_VSUBTYPE(pParam->x86.fParam) == OP_PARM_p); RT_NOREF_PV(pParam);
    if (pDis->x86.uOpMode == DISCPUMODE_32BIT)
        return offInstr + 4 + 2;    /* far 16:32 pointer */
    return offInstr + 2 + 2;        /* far 16:16 pointer */
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseFixedReg(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(offInstr);

    /*
     * Sets up flags for stored in OPC fixed registers.
     */

    if (pParam->x86.fParam == OP_PARM_NONE)
    {
        /* No parameter at all. */
        return offInstr;
    }

    AssertCompile(OP_PARM_REG_GEN32_END < OP_PARM_REG_SEG_END);
    AssertCompile(OP_PARM_REG_SEG_END < OP_PARM_REG_GEN16_END);
    AssertCompile(OP_PARM_REG_GEN16_END < OP_PARM_REG_GEN8_END);
    AssertCompile(OP_PARM_REG_GEN8_END < OP_PARM_REG_FP_END);

    if (pParam->x86.fParam <= OP_PARM_REG_GEN32_END)
    {
        /* 32-bit EAX..EDI registers. */
        if (pDis->x86.uOpMode == DISCPUMODE_32BIT)
        {
            /* Use 32-bit registers. */
            pParam->x86.Base.idxGenReg = (uint8_t)(pParam->x86.fParam - OP_PARM_REG_GEN32_START);
            pParam->fUse  |= DISUSE_REG_GEN32;
            pParam->x86.cb     = 4;
        }
        else if (pDis->x86.uOpMode == DISCPUMODE_64BIT)
        {
            /* Use 64-bit registers. */
            pParam->x86.Base.idxGenReg = (uint8_t)(pParam->x86.fParam - OP_PARM_REG_GEN32_START);
            pParam->fUse  |= DISUSE_REG_GEN64;
            pParam->x86.cb     = 8;
        }
        else
        {
            /* Use 16-bit registers. */
            pParam->x86.Base.idxGenReg = (uint8_t)(pParam->x86.fParam - OP_PARM_REG_GEN32_START);
            pParam->fUse  |= DISUSE_REG_GEN16;
            pParam->x86.cb     = 2;
            pParam->x86.fParam = pParam->x86.fParam - OP_PARM_REG_GEN32_START + OP_PARM_REG_GEN16_START;
        }

        if (    (pOp->fOpType & DISOPTYPE_X86_REXB_EXTENDS_OPREG)
            &&  pParam == &pDis->aParams[0]         /* ugly assumption that it only applies to the first parameter */
            &&  (pDis->x86.fPrefix & DISPREFIX_REX)
            &&  (pDis->x86.fRexPrefix & DISPREFIX_REX_FLAGS_B))
        {
            Assert(pDis->uCpuMode == DISCPUMODE_64BIT);
            pParam->x86.Base.idxGenReg += 8;
        }
    }
    else if (pParam->x86.fParam <= OP_PARM_REG_SEG_END)
    {
        /* Segment ES..GS registers. */
        pParam->x86.Base.idxSegReg = (uint8_t)(pParam->x86.fParam - OP_PARM_REG_SEG_START);
        pParam->fUse  |= DISUSE_REG_SEG;
        pParam->x86.cb     = 2;
    }
    else if (pParam->x86.fParam <= OP_PARM_REG_GEN16_END)
    {
        /* 16-bit AX..DI registers. */
        pParam->x86.Base.idxGenReg = (uint8_t)(pParam->x86.fParam - OP_PARM_REG_GEN16_START);
        pParam->fUse  |= DISUSE_REG_GEN16;
        pParam->x86.cb     = 2;
    }
    else if (pParam->x86.fParam <= OP_PARM_REG_GEN8_END)
    {
        /* 8-bit AL..DL, AH..DH registers. */
        pParam->x86.Base.idxGenReg = (uint8_t)(pParam->x86.fParam - OP_PARM_REG_GEN8_START);
        pParam->fUse  |= DISUSE_REG_GEN8;
        pParam->x86.cb     = 1;

        if (   pDis->uCpuMode == DISCPUMODE_64BIT
            && (pOp->fOpType & DISOPTYPE_X86_REXB_EXTENDS_OPREG)
            &&  pParam == &pDis->aParams[0]         /* ugly assumption that it only applies to the first parameter */
            &&  (pDis->x86.fPrefix & DISPREFIX_REX))
        {
            if (pDis->x86.fRexPrefix & DISPREFIX_REX_FLAGS_B)
                pParam->x86.Base.idxGenReg += 8;              /* least significant byte of R8-R15 */
            else if (   pParam->x86.Base.idxGenReg >= DISGREG_AH
                     && pParam->x86.Base.idxGenReg <= DISGREG_BH)
                pParam->x86.Base.idxGenReg += DISGREG_SPL - DISGREG_AH;
        }
    }
    else if (pParam->x86.fParam <= OP_PARM_REG_FP_END)
    {
        /* FPU registers. */
        pParam->x86.Base.idxFpuReg = (uint8_t)(pParam->x86.fParam - OP_PARM_REG_FP_START);
        pParam->fUse  |= DISUSE_REG_FP;
        pParam->x86.cb     = 10;
    }
    else if (pParam->x86.fParam <= OP_PARM_REG_SSE_END)
    {
        /* SSE registers. */
        pParam->x86.Base.idxFpuReg = (uint8_t)(pParam->x86.fParam - OP_PARM_REG_SSE_END);
        pParam->fUse  |= DISUSE_REG_XMM;
        pParam->x86.cb = 16;
    }
    Assert(!(pParam->x86.fParam >= OP_PARM_REG_GEN64_START && pParam->x86.fParam <= OP_PARM_REG_GEN64_END));

    /* else - not supported for now registers. */

    return offInstr;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseXv(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp);

    pParam->fUse |= DISUSE_POINTER_DS_BASED;
    if (pDis->x86.uAddrMode == DISCPUMODE_32BIT)
    {
        pParam->x86.Base.idxGenReg = DISGREG_ESI;
        pParam->fUse |= DISUSE_REG_GEN32;
    }
    else
    if (pDis->x86.uAddrMode == DISCPUMODE_64BIT)
    {
        pParam->x86.Base.idxGenReg = DISGREG_RSI;
        pParam->fUse |= DISUSE_REG_GEN64;
    }
    else
    {
        pParam->x86.Base.idxGenReg = DISGREG_SI;
        pParam->fUse |= DISUSE_REG_GEN16;
    }
    return offInstr;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseXb(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp);

    pParam->fUse |= DISUSE_POINTER_DS_BASED;
    if (pDis->x86.uAddrMode == DISCPUMODE_32BIT)
    {
        pParam->x86.Base.idxGenReg = DISGREG_ESI;
        pParam->fUse |= DISUSE_REG_GEN32;
    }
    else
    if (pDis->x86.uAddrMode == DISCPUMODE_64BIT)
    {
        pParam->x86.Base.idxGenReg = DISGREG_RSI;
        pParam->fUse |= DISUSE_REG_GEN64;
    }
    else
    {
        pParam->x86.Base.idxGenReg = DISGREG_SI;
        pParam->fUse |= DISUSE_REG_GEN16;
    }
    return offInstr;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseYv(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp);

    pParam->fUse |= DISUSE_POINTER_ES_BASED;
    if (pDis->x86.uAddrMode == DISCPUMODE_32BIT)
    {
        pParam->x86.Base.idxGenReg = DISGREG_EDI;
        pParam->fUse |= DISUSE_REG_GEN32;
    }
    else
    if (pDis->x86.uAddrMode == DISCPUMODE_64BIT)
    {
        pParam->x86.Base.idxGenReg = DISGREG_RDI;
        pParam->fUse |= DISUSE_REG_GEN64;
    }
    else
    {
        pParam->x86.Base.idxGenReg = DISGREG_DI;
        pParam->fUse |= DISUSE_REG_GEN16;
    }
    return offInstr;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseYb(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp);

    pParam->fUse |= DISUSE_POINTER_ES_BASED;
    if (pDis->x86.uAddrMode == DISCPUMODE_32BIT)
    {
        pParam->x86.Base.idxGenReg = DISGREG_EDI;
        pParam->fUse |= DISUSE_REG_GEN32;
    }
    else
    if (pDis->x86.uAddrMode == DISCPUMODE_64BIT)
    {
        pParam->x86.Base.idxGenReg = DISGREG_RDI;
        pParam->fUse |= DISUSE_REG_GEN64;
    }
    else
    {
        pParam->x86.Base.idxGenReg = DISGREG_DI;
        pParam->fUse |= DISUSE_REG_GEN16;
    }
    return offInstr;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseInvOpModRm(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp); RT_NOREF_PV(pDis); RT_NOREF_PV(pParam);
    /* This is used to avoid a bunch of special hacks to get the ModRM byte
       included when encountering invalid opcodes in groups. */
    return offInstr + 1;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseVexDest(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp);

    unsigned type = OP_PARM_VTYPE(pParam->x86.fParam);
    switch (type)
    {
        case OP_PARM_H: //XMM or YMM register
            if (VEXREG_IS256B(pDis->x86.bVexDestReg))
            {
                pParam->fUse |= DISUSE_REG_YMM;
                pParam->x86.Base.idxYmmReg = (pDis->x86.bVexDestReg >> 1) ^ 0xf;
            }
            else
            {
                pParam->fUse |= DISUSE_REG_XMM;
                pParam->x86.Base.idxXmmReg = (pDis->x86.bVexDestReg >> 1) ^ 0xf;
            }
            break;

        case OP_PARM_B: // Always OP_PARM_By. Change if it is not so.
            if ((pDis->x86.bVexByte2 & DISPREFIX_VEX_F_W) && pDis->uCpuMode == DISCPUMODE_64BIT)
                pParam->fUse |= DISUSE_REG_GEN64;
            else
                pParam->fUse |= DISUSE_REG_GEN32;
            /// @todo Check if the register number is correct
            pParam->x86.Base.idxGenReg = (pDis->x86.bVexDestReg >> 1) ^ 0xf;
            break;
    }

    return offInstr;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseTwoByteEsc(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp); RT_NOREF_PV(pParam);

    /* 2nd byte */
    pDis->x86.bOpCode = disReadByte(pDis, offInstr);
    offInstr++;

    /* default to the non-prefixed table. */
    PCDISOPCODE pOpcode = &g_aTwoByteMapX86[pDis->x86.bOpCode];

    /* Handle opcode table extensions that rely on the opsize, repe or repne prefix byte.  */
    /** @todo Should we take the first or last prefix byte in case of multiple prefix bytes??? */
    if (pDis->x86.bLastPrefix)
    {
        switch (pDis->x86.bLastPrefix)
        {
        case OP_OPSIZE: /* 0x66 */
            if (g_aTwoByteMapX86_PF66[pDis->x86.bOpCode].uOpcode != OP_INVALID)
            {
                /* Table entry is valid, so use the extension table. */
                pOpcode = &g_aTwoByteMapX86_PF66[pDis->x86.bOpCode];

                /* Cancel prefix changes. */
                pDis->x86.fPrefix &= ~DISPREFIX_OPSIZE;

                if (pDis->uCpuMode == DISCPUMODE_64BIT)
                    pDis->x86.uOpMode = (pDis->x86.fRexPrefix & DISPREFIX_REX_FLAGS_W ? DISCPUMODE_64BIT : DISCPUMODE_32BIT);
                else
                    pDis->x86.uOpMode  = pDis->uCpuMode;
            }
            break;

        case OP_REPNE:   /* 0xF2 */
            if (g_aTwoByteMapX86_PFF2[pDis->x86.bOpCode].uOpcode != OP_INVALID)
            {
                /* Table entry is valid, so use the extension table. */
                pOpcode = &g_aTwoByteMapX86_PFF2[pDis->x86.bOpCode];

                /* Cancel prefix changes. */
                pDis->x86.fPrefix &= ~DISPREFIX_REPNE;
            }
            break;

        case OP_REPE:  /* 0xF3 */
            if (g_aTwoByteMapX86_PFF3[pDis->x86.bOpCode].uOpcode != OP_INVALID)
            {
                /* Table entry is valid, so use the extension table. */
                pOpcode = &g_aTwoByteMapX86_PFF3[pDis->x86.bOpCode];

                /* Cancel prefix changes. */
                pDis->x86.fPrefix &= ~DISPREFIX_REP;
            }
            break;
        }
    }

    return disParseInstruction(offInstr, pOpcode, pDis);
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseThreeByteEsc4(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp); RT_NOREF_PV(pParam);

    /* 3rd byte */
    pDis->x86.bOpCode = disReadByte(pDis, offInstr);
    offInstr++;

    /* default to the non-prefixed table. */
    PCDISOPCODE pOpcode;
    if (g_apThreeByteMapX86_0F38[pDis->x86.bOpCode >> 4])
    {
        pOpcode = g_apThreeByteMapX86_0F38[pDis->x86.bOpCode >> 4];
        pOpcode = &pOpcode[pDis->x86.bOpCode & 0xf];
    }
    else
        pOpcode = &g_InvalidOpcode[0];

    /* Handle opcode table extensions that rely on the address, repne prefix byte.  */
    /** @todo Should we take the first or last prefix byte in case of multiple prefix bytes??? */
    switch (pDis->x86.bLastPrefix)
    {
    case OP_OPSIZE: /* 0x66 */
        if (g_apThreeByteMapX86_660F38[pDis->x86.bOpCode >> 4])
        {
            pOpcode = g_apThreeByteMapX86_660F38[pDis->x86.bOpCode >> 4];
            pOpcode = &pOpcode[pDis->x86.bOpCode & 0xf];

            if (pOpcode->uOpcode != OP_INVALID)
            {
                /* Table entry is valid, so use the extension table. */

                /* Cancel prefix changes. */
                pDis->x86.fPrefix &= ~DISPREFIX_OPSIZE;
                if (pDis->uCpuMode == DISCPUMODE_64BIT)
                    pDis->x86.uOpMode = (pDis->x86.fRexPrefix & DISPREFIX_REX_FLAGS_W ? DISCPUMODE_64BIT : DISCPUMODE_32BIT);
                else
                    pDis->x86.uOpMode  = pDis->uCpuMode;

            }
        }
        break;

    case OP_REPNE:   /* 0xF2 */
        if ((pDis->x86.fPrefix & DISPREFIX_OPSIZE) && g_apThreeByteMapX86_66F20F38[pDis->x86.bOpCode >> 4])
        {
        /* 0x66F2 */
            pOpcode = g_apThreeByteMapX86_66F20F38[pDis->x86.bOpCode >> 4];
            pOpcode = &pOpcode[pDis->x86.bOpCode & 0xf];

            if (pOpcode->uOpcode != OP_INVALID)
            {
                /* Table entry is valid, so use the extension table. */

                /* Cancel prefix changes. */
                pDis->x86.fPrefix &= ~DISPREFIX_REPNE;
                pDis->x86.fPrefix &= ~DISPREFIX_OPSIZE;
                if (pDis->uCpuMode == DISCPUMODE_64BIT)
                    pDis->x86.uOpMode = (pDis->x86.fRexPrefix & DISPREFIX_REX_FLAGS_W ? DISCPUMODE_64BIT : DISCPUMODE_32BIT);
                else
                    pDis->x86.uOpMode  = pDis->uCpuMode;
            }
        }
        else if (g_apThreeByteMapX86_F20F38[pDis->x86.bOpCode >> 4])
        {
            pOpcode = g_apThreeByteMapX86_F20F38[pDis->x86.bOpCode >> 4];
            pOpcode = &pOpcode[pDis->x86.bOpCode & 0xf];

            if (pOpcode->uOpcode != OP_INVALID)
            {
                /* Table entry is valid, so use the extension table. */

                /* Cancel prefix changes. */
                pDis->x86.fPrefix &= ~DISPREFIX_REPNE;
            }
        }
        break;

    case OP_REPE:    /* 0xF3 */
        if (g_apThreeByteMapX86_F30F38[pDis->x86.bOpCode >> 4])
        {
            pOpcode = g_apThreeByteMapX86_F30F38[pDis->x86.bOpCode >> 4];
            pOpcode = &pOpcode[pDis->x86.bOpCode & 0xf];

            if (pOpcode->uOpcode != OP_INVALID)
            {
                /* Table entry is valid, so use the extension table. */

                /* Cancel prefix changes. */
                pDis->x86.fPrefix &= ~DISPREFIX_REP;
            }
        }
    }

    return disParseInstruction(offInstr, pOpcode, pDis);
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseThreeByteEsc5(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp); RT_NOREF_PV(pParam);

    /* 3rd byte */
    pDis->x86.bOpCode = disReadByte(pDis, offInstr);
    offInstr++;

    /* default to the non-prefixed table. */
    PCDISOPCODE pOpcode;
    if (g_apThreeByteMapX86_0F3A[pDis->x86.bOpCode >> 4])
    {
        pOpcode = g_apThreeByteMapX86_0F3A[pDis->x86.bOpCode >> 4];
        pOpcode = &pOpcode[pDis->x86.bOpCode & 0xf];
    }
    else
        pOpcode = &g_InvalidOpcode[0];

    /** @todo Should we take the first or last prefix byte in case of multiple prefix bytes??? */
    if (pDis->x86.bLastPrefix == OP_OPSIZE && g_apThreeByteMapX86_660F3A[pDis->x86.bOpCode >> 4])
    {
        pOpcode = g_apThreeByteMapX86_660F3A[pDis->x86.bOpCode >> 4];
        pOpcode = &pOpcode[pDis->x86.bOpCode & 0xf];

        if (pOpcode->uOpcode != OP_INVALID)
        {
            /* Table entry is valid, so use the extension table. */

            /* Cancel prefix changes. */
            pDis->x86.fPrefix &= ~DISPREFIX_OPSIZE;
            if (pDis->uCpuMode == DISCPUMODE_64BIT)
                pDis->x86.uOpMode = (pDis->x86.fRexPrefix & DISPREFIX_REX_FLAGS_W ? DISCPUMODE_64BIT : DISCPUMODE_32BIT);
            else
                pDis->x86.uOpMode  = pDis->uCpuMode;
        }
    }

    return disParseInstruction(offInstr, pOpcode, pDis);
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseNopPause(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pParam);

    if (pDis->x86.fPrefix & DISPREFIX_REP)
    {
        pOp = &g_aMapX86_NopPause[1]; /* PAUSE */
        pDis->x86.fPrefix &= ~DISPREFIX_REP;
    }
    else
        pOp = &g_aMapX86_NopPause[0]; /* NOP */

    return disParseInstruction(offInstr, pOp, pDis);
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseGrp1(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pParam);

    uint8_t  modrm = disReadByte(pDis, offInstr);
    uint8_t  reg   = MODRM_REG(modrm);
    unsigned idx   = (pDis->x86.bOpCode - 0x80) * 8;

    pOp = &g_aMapX86_Group1[idx+reg];

    return disParseInstruction(offInstr, pOp, pDis);
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseShiftGrp2(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pParam);

    unsigned idx;
    switch (pDis->x86.bOpCode)
    {
    case 0xC0:
    case 0xC1:
        idx = (pDis->x86.bOpCode - 0xC0)*8;
        break;

    case 0xD0:
    case 0xD1:
    case 0xD2:
    case 0xD3:
        idx = (pDis->x86.bOpCode - 0xD0 + 2)*8;
        break;

    default:
        Log(("ParseShiftGrp2: bOpCode=%#x\n", pDis->x86.bOpCode));
        pDis->rc = VERR_DIS_INVALID_OPCODE;
        return offInstr;
    }

    uint8_t modrm = disReadByte(pDis, offInstr);
    uint8_t reg   = MODRM_REG(modrm);

    pOp = &g_aMapX86_Group2[idx+reg];

    return disParseInstruction(offInstr, pOp, pDis);
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseGrp3(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    unsigned idx = (pDis->x86.bOpCode - 0xF6) * 8;
    RT_NOREF_PV(pParam);

    uint8_t modrm = disReadByte(pDis, offInstr);
    uint8_t reg   = MODRM_REG(modrm);

    pOp = &g_aMapX86_Group3[idx+reg];

    return disParseInstruction(offInstr, pOp, pDis);
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseGrp4(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pParam);

    uint8_t modrm = disReadByte(pDis, offInstr);
    uint8_t reg   = MODRM_REG(modrm);

    pOp = &g_aMapX86_Group4[reg];

    return disParseInstruction(offInstr, pOp, pDis);
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseGrp5(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pParam);

    uint8_t modrm = disReadByte(pDis, offInstr);
    uint8_t reg   = MODRM_REG(modrm);

    pOp = &g_aMapX86_Group5[reg];

    return disParseInstruction(offInstr, pOp, pDis);
}
//*****************************************************************************
// 0xF 0xF [ModRM] [SIB] [displacement] imm8_opcode
// It would appear the ModRM byte must always be present. How else can you
// determine the offset of the imm8_opcode byte otherwise?
//
//*****************************************************************************
static size_t Parse3DNow(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    /** @todo This code needs testing!  Esp. wrt invalid opcodes. */

    uint8_t ModRM = disReadByte(pDis, offInstr);
    pDis->x86.ModRM.Bits.Rm  = MODRM_RM(ModRM);
    pDis->x86.ModRM.Bits.Mod = MODRM_MOD(ModRM);
    pDis->x86.ModRM.Bits.Reg = MODRM_REG(ModRM);

    size_t offRet = QueryModRM(offInstr + 1, pOp, pDis, pParam);

    uint8_t opcode = disReadByte(pDis, offRet);
    offRet++;
    pOp = &g_aTwoByteMapX86_3DNow[opcode];

    size_t offStrict = disParseInstruction(offInstr, pOp, pDis);

    AssertMsg(offStrict == offRet - 1  /* the imm8_opcode */ || pOp->uOpcode == OP_INVALID,
              ("offStrict=%#x offRet=%#x uOpCode=%u\n", offStrict, offRet, pOp->uOpcode));
    RT_NOREF_PV(offStrict);

    return offRet;
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseGrp6(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pParam);

    uint8_t modrm = disReadByte(pDis, offInstr);
    uint8_t reg   = MODRM_REG(modrm);

    pOp = &g_aMapX86_Group6[reg];

    return disParseInstruction(offInstr, pOp, pDis);
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseGrp7(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pParam);

    uint8_t modrm = disReadByte(pDis, offInstr);
    uint8_t mod   = MODRM_MOD(modrm);
    uint8_t reg   = MODRM_REG(modrm);
    uint8_t rm    = MODRM_RM(modrm);

    if (mod != 3)
        pOp = &g_aMapX86_Group7_mem[reg];
    else if (reg == 0)
        pOp = &g_aMapX86_Group7_mod11_reg000[rm];
    else if (reg == 1)
        pOp = &g_aMapX86_Group7_mod11_reg001[rm];
    else if (reg == 2)
        pOp = &g_aMapX86_Group7_mod11_reg010[rm];
    else if (reg == 7)
        pOp = &g_aMapX86_Group7_mod11_reg111[rm];
    else if (reg == 4 || reg == 6)
        pOp = &g_aMapX86_Group7_mem[reg];
    else
        pOp = &g_InvalidOpcode[0];

    /* Cannot easily skip this hack because of monitor and vmcall! */
    //little hack to make sure the ModRM byte is included in the returned size
    if (pOp->idxParse1 != IDX_ParseModRM && pOp->idxParse2 != IDX_ParseModRM)
        offInstr++;

    return disParseInstruction(offInstr, pOp, pDis);
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseGrp8(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pParam);

    uint8_t modrm = disReadByte(pDis, offInstr);
    uint8_t reg   = MODRM_REG(modrm);

    pOp = &g_aMapX86_Group8[reg];

    return disParseInstruction(offInstr, pOp, pDis);
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseGrp9(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pParam);

    uint8_t const bRm = disReadByte(pDis, offInstr);
    uint8_t       idx = MODRM_REG(bRm);
    if (MODRM_MOD(bRm) != X86_MOD_REG)
    {
        if (pDis->x86.bLastPrefix == OP_OPSIZE /*0xf3*/)
            pOp = &g_aMapX86_Group9_mem_66[idx];
        else if (pDis->x86.bLastPrefix == OP_REPE /*0xf3*/)
        {
            pDis->x86.fPrefix &= ~DISPREFIX_REP;
            pOp = &g_aMapX86_Group9_mem_f3[idx];
        }
        /** @todo bLastPrefix is also set for OP_SEG & OP_ADDRSIZE which aren't relevant here or to any other of the table */
        else if (pDis->x86.bLastPrefix != OP_REPNE && (pDis->x86.bLastPrefix != OP_LOCK || idx == 1 /*cmpxchg8/16b*/))
        {
            if ((pDis->x86.fRexPrefix & DISPREFIX_REX_FLAGS_W) && (pDis->x86.fPrefix & DISPREFIX_REX))
                idx += 8;
            pOp = &g_aMapX86_Group9_mem_none[idx];
        }
        else
            pOp = &g_InvalidOpcode[0];
    }
    else
    {
        if (pDis->x86.bLastPrefix == OP_REPE /*0xf3*/)
        {
            pDis->x86.fPrefix &= ~DISPREFIX_REP;
            pOp = &g_aMapX86_Group9_mod11_f3[idx];
        }
        /** @todo bLastPrefix is also set for OP_SEG & OP_ADDRSIZE which aren't relevant here or to any other of the table */
        else if (pDis->x86.bLastPrefix != OP_LOCK && pDis->x86.bLastPrefix != OP_REPNE)
            pOp = &g_aMapX86_Group9_mod11_none[idx];
        else
            pOp = &g_InvalidOpcode[0];
    }

    return disParseInstruction(offInstr, pOp, pDis);
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseGrp10(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pParam);

    uint8_t modrm = disReadByte(pDis, offInstr);
    uint8_t reg   = MODRM_REG(modrm);

    pOp = &g_aMapX86_Group10[reg];

    return disParseInstruction(offInstr, pOp, pDis);
}


/**
 * Parses non-vex group 12.
 */
static size_t ParseGrp12(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pParam);

    uint8_t const bRm = disReadByte(pDis, offInstr);
    uint8_t       idx = MODRM_REG(bRm);
    if (pDis->x86.fPrefix & DISPREFIX_OPSIZE)
        idx += 8;   /* 2nd table */
    pOp = &g_aMapX86_Group12[idx];

    return disParseInstruction(offInstr, pOp, pDis);
}


/**
 * Parses vex group 12.
 */
static size_t ParseVGrp12(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pParam);

    uint8_t const bRm = disReadByte(pDis, offInstr);
    if ((pDis->x86.bVexByte2 & DISPREFIX_VEX_F_PP_MASK) == DISPREFIX_VEX_F_PP_66)
        pOp = &g_aMapX86_VGroup12[MODRM_REG(bRm)];
    else
        pOp = &g_InvalidOpcode[0];

    return disParseInstruction(offInstr, pOp, pDis);
}


/**
 * Parses non-vex group 13.
 */
static size_t ParseGrp13(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pParam);

    uint8_t const bRm = disReadByte(pDis, offInstr);
    uint8_t       idx = MODRM_REG(bRm);
    if (pDis->x86.fPrefix & DISPREFIX_OPSIZE)
        idx += 8;   /* 2nd table */
    pOp = &g_aMapX86_Group13[idx];

    return disParseInstruction(offInstr, pOp, pDis);
}


/**
 * Parses vex group 13.
 */
static size_t ParseVGrp13(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pParam);

    uint8_t const bRm = disReadByte(pDis, offInstr);
    if ((pDis->x86.bVexByte2 & DISPREFIX_VEX_F_PP_MASK) == DISPREFIX_VEX_F_PP_66)
        pOp = &g_aMapX86_VGroup13[MODRM_REG(bRm)];
    else
        pOp = &g_InvalidOpcode[0];

    return disParseInstruction(offInstr, pOp, pDis);
}


/**
 * Parses non-vex group 14.
 */
static size_t ParseGrp14(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pParam);

    uint8_t const bRm = disReadByte(pDis, offInstr);
    uint8_t       idx = MODRM_REG(bRm);
    if (pDis->x86.fPrefix & DISPREFIX_OPSIZE)
        idx += 8;   /* 2nd table */
    pOp = &g_aMapX86_Group14[idx];

    return disParseInstruction(offInstr, pOp, pDis);
}


/**
 * Parses vex group 14.
 */
static size_t ParseVGrp14(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pParam);

    uint8_t const bRm = disReadByte(pDis, offInstr);
    if ((pDis->x86.bVexByte2 & DISPREFIX_VEX_F_PP_MASK) == DISPREFIX_VEX_F_PP_66)
        pOp = &g_aMapX86_VGroup14[MODRM_REG(bRm)];
    else
        pOp = &g_InvalidOpcode[0];

    return disParseInstruction(offInstr, pOp, pDis);
}


/**
 * Parses non-vex group 15.
 */
static size_t ParseGrp15(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pParam);

    uint8_t const bRm = disReadByte(pDis, offInstr);
    uint8_t idx       = MODRM_REG(bRm);
    if (MODRM_MOD(bRm) != X86_MOD_REG)
        pOp = &g_aMapX86_Group15_mem[idx];
    else if (pDis->x86.bLastPrefix == OP_REPE /*0xf3*/)
    {
        pDis->x86.fPrefix &= ~DISPREFIX_REP;
        pOp = &g_aMapX86_Group15_mod11[idx + 8];
    }
    /** @todo bLastPrefix is also set for OP_SEG & OP_ADDRSIZE which aren't relevant here or to any other of the table */
    else if (pDis->x86.bLastPrefix != OP_LOCK && pDis->x86.bLastPrefix != OP_REPNE && pDis->x86.bLastPrefix != OP_OPSIZE)
        pOp = &g_aMapX86_Group15_mod11[idx];
    else
        pOp = &g_InvalidOpcode[0];

    return disParseInstruction(offInstr, pOp, pDis);
}


/**
 * Parses vex group 15.
 */
static size_t ParseVGrp15(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pParam);

    uint8_t const bRm = disReadByte(pDis, offInstr);
    if ((pDis->x86.bVexByte2 & DISPREFIX_VEX_F_PP_MASK) == DISPREFIX_VEX_F_PP_NONE)
        pOp = &g_aMapX86_VGroup15[MODRM_REG(bRm)];
    else
        pOp = &g_InvalidOpcode[0];

    return disParseInstruction(offInstr, pOp, pDis);
}


/**
 * Parses group 16.
 */
static size_t ParseGrp16(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pParam);

    uint8_t const bRm = disReadByte(pDis, offInstr);
    pOp = &g_aMapX86_Group16[MODRM_REG(bRm)];

    return disParseInstruction(offInstr, pOp, pDis);
}


/**
 * Parses (vex) group 17.
 */
static size_t ParseVGrp17(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pParam);

    uint8_t const bRm = disReadByte(pDis, offInstr);
    pOp = &g_aMapX86_VGroup17[(MODRM_REG(bRm) << 1) | (pDis->x86.bVexDestReg & 1)];

    return disParseInstruction(offInstr, pOp, pDis);
}


//*****************************************************************************
//*****************************************************************************
static size_t ParseVex2b(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp); RT_NOREF_PV(pParam);

    uint8_t const bVexByte = disReadByte(pDis, offInstr++);
    if (pDis->uCpuMode == DISCPUMODE_64BIT)
    {
        // VEX.~R => REX.R
        if (!(bVexByte & 0x80))
        {
            pDis->x86.fPrefix   |= DISPREFIX_REX;
            pDis->x86.fRexPrefix = DISPREFIX_REX_FLAGS_R;
        }
    }
    else if ((bVexByte & X86_MODRM_MOD_MASK) != (X86_MOD_REG << X86_MODRM_MOD_SHIFT))
        return disParseInstruction(offInstr - 1, &g_OpcodeLDS, pDis); /* LDS r,mem */

    /* VEX2.byte1 is the same as VEX3.byte2 with the exception of the 7th bit (~R vs W). */
    pDis->x86.bVexByte2   = bVexByte & (DISPREFIX_VEX_F_PP_MASK | DISPREFIX_VEX_F_L | DISPREFIX_VEX_F_VVVV);
    pDis->x86.bVexDestReg = VEX_2B2INT(bVexByte);
    pDis->x86.bOpCode     = disReadByte(pDis, offInstr++);
    pDis->x86.fPrefix    |= DISPREFIX_VEX;

    PCDISOPMAPDESC const pRange    = g_aapVexOpcodesMapRanges[bVexByte & 3][1];
    unsigned  const      idxOpcode = pDis->x86.bOpCode - pRange->idxFirst;
    PCDISOPCODE          pOpCode;
    if (idxOpcode < pRange->cOpcodes)
        pOpCode = &pRange->papOpcodes[idxOpcode];
    else
        pOpCode = &g_InvalidOpcode[0];

    return disParseInstruction(offInstr, pOpCode, pDis);
}
//*****************************************************************************
//*****************************************************************************
static size_t ParseVex3b(size_t offInstr, PCDISOPCODE pOp, PDISSTATE pDis, PDISOPPARAM pParam)
{
    RT_NOREF_PV(pOp); RT_NOREF_PV(pParam);

    uint8_t const bVexByte1 = disReadByte(pDis, offInstr++);
    if (pDis->uCpuMode == DISCPUMODE_64BIT)
    {
        // VEX.~R~X~B => REX.RXB
        pDis->x86.fRexPrefix |= (bVexByte1 >> 5) ^ 7;
        if (pDis->x86.fRexPrefix)
            pDis->x86.fPrefix |= DISPREFIX_REX;
    }
    else if ((bVexByte1 & X86_MODRM_MOD_MASK) != (X86_MOD_REG << X86_MODRM_MOD_SHIFT))
        return disParseInstruction(offInstr - 1, &g_OpcodeLES, pDis); /* LES r,mem */

    uint8_t const bVexByte2 = disReadByte(pDis, offInstr++);
    pDis->x86.fPrefix    |= DISPREFIX_VEX;
    pDis->x86.bVexByte2   = bVexByte2;
    pDis->x86.bOpCode     = disReadByte(pDis, offInstr++);
    pDis->x86.bVexDestReg = VEX_2B2INT(bVexByte2); /** @todo r=bird: why on earth ~vvvv + L; this is obfuscation non-sense. Either split the shit up or just store bVexByte2 raw here! */

    /* Hack alert! Assume VEX.W rules over any 66h prefix and that no VEX
       encoded instructions ever uses the regular x86.uOpMode w/o VEX.W. */
    pDis->x86.uOpMode = (bVexByte2 & 0x80) && pDis->uCpuMode == DISCPUMODE_64BIT ? DISCPUMODE_64BIT : DISCPUMODE_32BIT;

    PCDISOPCODE pOpCode;
    uint8_t const idxVexMap = bVexByte1 & 0x1f;
    if (idxVexMap < RT_ELEMENTS(g_aapVexOpcodesMapRanges[bVexByte2 & 3]))
    {
        PCDISOPMAPDESC const pRange    = g_aapVexOpcodesMapRanges[bVexByte2 & 3][idxVexMap];
        unsigned  const      idxOpcode = pDis->x86.bOpCode - pRange->idxFirst;
        if (idxOpcode < pRange->cOpcodes)
            pOpCode = &pRange->papOpcodes[idxOpcode];
        else
            pOpCode = &g_InvalidOpcode[0];
    }
    else
        pOpCode = &g_InvalidOpcode[0];

    return disParseInstruction(offInstr, pOpCode, pDis);
}


/**
 * Validates the lock sequence.
 *
 * The AMD manual lists the following instructions:
 *      ADC
 *      ADD
 *      AND
 *      BTC
 *      BTR
 *      BTS
 *      CMPXCHG
 *      CMPXCHG8B
 *      CMPXCHG16B
 *      DEC
 *      INC
 *      NEG
 *      NOT
 *      OR
 *      SBB
 *      SUB
 *      XADD
 *      XCHG
 *      XOR
 *
 * @param   pDis    Fully disassembled instruction.
 */
static void disValidateLockSequence(PDISSTATE pDis)
{
    Assert(pDis->x86.fPrefix & DISPREFIX_LOCK);

    /*
     * Filter out the valid lock sequences.
     */
    switch (pDis->pCurInstr->uOpcode)
    {
        /* simple: no variations */
        case OP_CMPXCHG8B:
        case OP_CMPXCHG16B:
            return;

        /* simple: /r - reject register destination. */
        case OP_BTC:
        case OP_BTR:
        case OP_BTS:
        case OP_CMPXCHG:
        case OP_XADD:
            if (pDis->x86.ModRM.Bits.Mod == 3)
                break;
            return;

        /*
         * Lots of variants but its sufficient to check that param 1
         * is a memory operand.
         */
        case OP_ADC:
        case OP_ADD:
        case OP_AND:
        case OP_DEC:
        case OP_INC:
        case OP_NEG:
        case OP_NOT:
        case OP_OR:
        case OP_SBB:
        case OP_SUB:
        case OP_XCHG:
        case OP_XOR:
            if (pDis->aParams[0].fUse & (  DISUSE_BASE | DISUSE_INDEX | DISUSE_DISPLACEMENT64 | DISUSE_DISPLACEMENT32
                                         | DISUSE_DISPLACEMENT16 | DISUSE_DISPLACEMENT8 | DISUSE_RIPDISPLACEMENT32))
                return;
            break;

        default:
            break;
    }

    /*
     * Invalid lock sequence, make it a OP_ILLUD2.
     */
    pDis->pCurInstr = &g_aTwoByteMapX86[11];
    Assert(pDis->pCurInstr->uOpcode == OP_ILLUD2);
}

/**
 * Internal worker for DISInstrEx and DISInstrWithPrefetchedBytes.
 *
 * @returns VBox status code.
 * @param   pDis            Initialized disassembler state.
 * @param   paOneByteMap    The one byte opcode map to use.
 * @param   pcbInstr        Where to store the instruction size. Can be NULL.
 */
DECLHIDDEN(int) disInstrWorkerX86(PDISSTATE pDis, PCDISOPCODE paOneByteMap, uint32_t *pcbInstr)
{
    /*
     * Parse byte by byte.
     */
    size_t offInstr = 0;
    for (;;)
    {
        uint8_t  const        bCode     = disReadByte(pDis, offInstr++);
        enum OPCODESX86 const enmOpcode = (enum OPCODESX86)paOneByteMap[bCode].uOpcode;

        /* Hardcoded assumption about OP_* values!! */
        if (enmOpcode <= OP_LAST_PREFIX)
        {
            /* The REX prefix must precede the opcode byte(s). Any other placement is ignored. */
            if (enmOpcode != OP_REX)
            {
                /* Last prefix byte (for SSE2 extension tables); don't include the REX prefix */
                pDis->x86.bLastPrefix = (uint8_t)enmOpcode;
                pDis->x86.fPrefix    &= ~DISPREFIX_REX;
            }

            switch (enmOpcode)
            {
            case OP_INVALID:
                if (pcbInstr)
                    *pcbInstr = (uint32_t)offInstr;
                return pDis->rc = VERR_DIS_INVALID_OPCODE;

            // segment override prefix byte
            case OP_SEG:
                pDis->x86.idxSegPrefix = (uint8_t)(paOneByteMap[bCode].fParam1 - OP_PARM_REG_SEG_START);
#if 0  /* Try be accurate in our reporting, shouldn't break anything... :-) */
                /* Segment prefixes for CS, DS, ES and SS are ignored in long mode. */
                if (   pDis->uCpuMode != DISCPUMODE_64BIT
                    || pDis->idxSegPrefix >= DISSELREG_FS)
                    pDis->x86.fPrefix   |= DISPREFIX_SEG;
#else
                pDis->x86.fPrefix |= DISPREFIX_SEG;
#endif
                continue;   //fetch the next byte

            // lock prefix byte
            case OP_LOCK:
                pDis->x86.fPrefix |= DISPREFIX_LOCK;
                continue;   //fetch the next byte

            // address size override prefix byte
            case OP_ADDRSIZE:
                pDis->x86.fPrefix |= DISPREFIX_ADDRSIZE;
                if (pDis->uCpuMode == DISCPUMODE_16BIT)
                    pDis->x86.uAddrMode = DISCPUMODE_32BIT;
                else
                if (pDis->uCpuMode == DISCPUMODE_32BIT)
                    pDis->x86.uAddrMode = DISCPUMODE_16BIT;
                else
                    pDis->x86.uAddrMode = DISCPUMODE_32BIT;     /* 64 bits */
                continue;   //fetch the next byte

            // operand size override prefix byte
            case OP_OPSIZE:
                pDis->x86.fPrefix |= DISPREFIX_OPSIZE;
                if (pDis->uCpuMode == DISCPUMODE_16BIT)
                    pDis->x86.uOpMode = DISCPUMODE_32BIT;
                else
                    pDis->x86.uOpMode = DISCPUMODE_16BIT;  /* for 32 and 64 bits mode (there is no 32 bits operand size override prefix) */
                continue;   //fetch the next byte

            // rep and repne are not really prefixes, but we'll treat them as such
            case OP_REPE:
                pDis->x86.fPrefix |= DISPREFIX_REP;
                continue;   //fetch the next byte

            case OP_REPNE:
                pDis->x86.fPrefix |= DISPREFIX_REPNE;
                continue;   //fetch the next byte

            case OP_REX:
                Assert(pDis->uCpuMode == DISCPUMODE_64BIT);
                /* REX prefix byte */
                pDis->x86.fPrefix   |= DISPREFIX_REX;
                pDis->x86.fRexPrefix = (uint8_t)DISPREFIX_REX_OP_2_FLAGS(paOneByteMap[bCode].fParam1);
                if (pDis->x86.fRexPrefix & DISPREFIX_REX_FLAGS_W)
                    pDis->x86.uOpMode = DISCPUMODE_64BIT;  /* overrides size prefix byte */
                continue;   //fetch the next byte
            default:
                AssertFailed();
                break;
            }
        }

        /* first opcode byte. */
        pDis->x86.bOpCode  = bCode;
        pDis->x86.cbPrefix = (uint8_t)offInstr - 1;

        offInstr = disParseInstruction(offInstr, &paOneByteMap[bCode], pDis);
        break;
    }

    pDis->cbInstr = (uint8_t)offInstr;
    if (pcbInstr)
        *pcbInstr = (uint32_t)offInstr;

    if (pDis->x86.fPrefix & DISPREFIX_LOCK)
        disValidateLockSequence(pDis);

    return pDis->rc;
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
DECLHIDDEN(PCDISOPCODE) disInitializeStateX86(PDISSTATE pDis, DISCPUMODE enmCpuMode, uint32_t fFilter)
{
#ifdef VBOX_STRICT /* poison */
    pDis->aParams[0].x86.Base.idxGenReg    = 0xc1;
    pDis->aParams[1].x86.Base.idxGenReg    = 0xc2;
    pDis->aParams[2].x86.Base.idxGenReg    = 0xc3;
    pDis->aParams[0].x86.Index.idxGenReg   = 0xc4;
    pDis->aParams[1].x86.Index.idxGenReg   = 0xc5;
    pDis->aParams[2].x86.Index.idxGenReg   = 0xc6;
    pDis->aParams[0].x86.uDisp.u64         = UINT64_C(0xd1d1d1d1d1d1d1d1);
    pDis->aParams[1].x86.uDisp.u64         = UINT64_C(0xd2d2d2d2d2d2d2d2);
    pDis->aParams[2].x86.uDisp.u64         = UINT64_C(0xd3d3d3d3d3d3d3d3);
    pDis->aParams[0].uValue                = UINT64_C(0xb1b1b1b1b1b1b1b1);
    pDis->aParams[1].uValue                = UINT64_C(0xb2b2b2b2b2b2b2b2);
    pDis->aParams[2].uValue                = UINT64_C(0xb3b3b3b3b3b3b3b3);
    pDis->aParams[0].x86.uScale            = 28;
    pDis->aParams[1].x86.uScale            = 29;
    pDis->aParams[2].x86.uScale            = 30;
#endif

    pDis->x86.fPrefix           = DISPREFIX_NONE;
    pDis->x86.idxSegPrefix      = DISSELREG_DS;
    pDis->x86.pfnDisasmFnTable  = g_apfnFullDisasm;
    pDis->x86.fFilter           = fFilter;

    PCDISOPCODE paOneByteMap;
    if (enmCpuMode == DISCPUMODE_64BIT)
    {
        pDis->x86.uAddrMode     = DISCPUMODE_64BIT;
        pDis->x86.uOpMode       = DISCPUMODE_32BIT;
        paOneByteMap        = g_aOneByteMapX64;
    }
    else
    {
        pDis->x86.uAddrMode     = (uint8_t)enmCpuMode;
        pDis->x86.uOpMode       = (uint8_t)enmCpuMode;
        paOneByteMap        = g_aOneByteMapX86;
    }
    return paOneByteMap;
}
