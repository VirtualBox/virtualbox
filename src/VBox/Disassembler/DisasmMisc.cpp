/* $Id: DisasmMisc.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox disassembler- Misc Helpers.
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
#include <iprt/errcore.h>
#include <VBox/log.h>
#include <VBox/vmm/cpum.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/stdarg.h>
#include "DisasmInternal.h"


DISDECL(uint8_t) DISGetParamSize(PCDISSTATE pDis, PCDISOPPARAM pParam)
{
    unsigned subtype = OP_PARM_VSUBTYPE(pParam->x86.fParam);
    switch (subtype)
    {
        case OP_PARM_v:
            switch (pDis->x86.uOpMode)
            {
                case DISCPUMODE_32BIT:
                    return 4;
                case DISCPUMODE_64BIT:
                    return 8;
                case DISCPUMODE_16BIT:
                    return 2;
                default: AssertFailed(); /* make gcc happy */ return 4;
            }
            break;

        case OP_PARM_b:
            return 1;

        case OP_PARM_w:
            return 2;

        case OP_PARM_d:
            return 4;

        case OP_PARM_q:
            return 8;

        case OP_PARM_dq:
            return 16;

        case OP_PARM_qq:
            return 32;

        case 0: /* nop, pause, lea, wrmsr, rdmsr, etc.  Most of these due to DISOPPARAM::cb being initialized in the wrong place
                   (disParseInstruction) where it will be called on intermediate stuff like IDX_ParseTwoByteEsc.  The parameter
                   parsers should do it instead, though I see the potential filtering issue. */
            //Assert(   pDis->pCurInstr
            //       && (   pDis->pCurInstr->uOpcode == OP_NOP
            //           || pDis->pCurInstr->uOpcode == OP_LEA ));
            return 0;

        case OP_PARM_p: /* far pointer */
            if (pDis->x86.uAddrMode == DISCPUMODE_32BIT)
                return 6;   /* 16:32 */
            if (pDis->x86.uAddrMode == DISCPUMODE_64BIT)
                return 12;  /* 16:64 */
            return 4;       /* 16:16 */

        case OP_PARM_s: /* lgdt, sgdt, lidt, sidt */
            return pDis->uCpuMode == DISCPUMODE_64BIT ? 2 + 8 : 2 + 4;

        case OP_PARM_a:
            return pDis->x86.uOpMode == DISCPUMODE_16BIT ? 2 + 2 : 4 + 4;

        case OP_PARM_pi:
            return 8;

        case OP_PARM_sd:
        case OP_PARM_ss:
            return 16;

        case OP_PARM_x:
        case OP_PARM_pd:
        case OP_PARM_ps:
            return VEXREG_IS256B(pDis->x86.bVexDestReg) ? 32 : 16; //??

        case OP_PARM_y:
            return pDis->x86.uOpMode == DISCPUMODE_64BIT ? 4 : 8;  //??

        case OP_PARM_z:
            if (pParam->x86.cb)
                return pParam->x86.cb;
            return pDis->x86.uOpMode == DISCPUMODE_16BIT ? 2 : 4;  //??

        default:
            if (pParam->x86.cb)
                return pParam->x86.cb;
            /// @todo dangerous!!!
            AssertMsgFailed(("subtype=%#x fParam=%#x fUse=%#RX64 op=%#x\n", subtype, pParam->x86.fParam, pParam->fUse,
                             pDis->pCurInstr ? pDis->pCurInstr->uOpcode : 0));
            return 4;
    }
}

#if 0 /* currently unused */
DISDECL(DISSELREG) DISDetectSegReg(PCDISSTATE pDis, PCDISOPPARAM pParam)
{
    if (pDis->x86.fPrefix & DISPREFIX_SEG)
        /* Use specified SEG: prefix. */
        return (DISSELREG)pDis->x86.idxSegPrefix;

    /* Guess segment register by parameter type. */
    if (pParam->fUse & (DISUSE_REG_GEN32|DISUSE_REG_GEN64|DISUSE_REG_GEN16))
    {
        AssertCompile(DISGREG_ESP == DISGREG_RSP);
        AssertCompile(DISGREG_EBP == DISGREG_RBP);
        AssertCompile(DISGREG_ESP == DISGREG_SP);
        AssertCompile(DISGREG_EBP == DISGREG_BP);
        if (pParam->x86.Base.idxGenReg == DISGREG_ESP || pParam->x86.Base.idxGenReg == DISGREG_EBP)
            return DISSELREG_SS;
    }
    /* Default is use DS: for data access. */
    return DISSELREG_DS;
}


DISDECL(uint8_t) DISQuerySegPrefixByte(PCDISSTATE pDis)
{
    Assert(pDis->x86.fPrefix & DISPREFIX_SEG);
    switch (pDis->x86.idxSegPrefix)
    {
    case DISSELREG_ES:
        return 0x26;
    case DISSELREG_CS:
        return 0x2E;
    case DISSELREG_SS:
        return 0x36;
    case DISSELREG_DS:
        return 0x3E;
    case DISSELREG_FS:
        return 0x64;
    case DISSELREG_GS:
        return 0x65;
    default:
        AssertFailed();
        return 0;
    }
}
#endif
