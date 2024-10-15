/* $Id: DisasmReg.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox disassembler- Register Info Helpers.
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
#include <VBox/disopcode.h>
#include <iprt/errcore.h>
#include <VBox/log.h>
#include <VBox/vmm/cpum.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/stdarg.h>
#include "DisasmInternal.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * Array for accessing 64-bit general registers in VMMREGFRAME structure
 * by register's index from disasm.
 */
static const unsigned g_aReg64Index[] =
{
    RT_OFFSETOF(CPUMCTXCORE, rax),        /* DISGREG_RAX */
    RT_OFFSETOF(CPUMCTXCORE, rcx),        /* DISGREG_RCX */
    RT_OFFSETOF(CPUMCTXCORE, rdx),        /* DISGREG_RDX */
    RT_OFFSETOF(CPUMCTXCORE, rbx),        /* DISGREG_RBX */
    RT_OFFSETOF(CPUMCTXCORE, rsp),        /* DISGREG_RSP */
    RT_OFFSETOF(CPUMCTXCORE, rbp),        /* DISGREG_RBP */
    RT_OFFSETOF(CPUMCTXCORE, rsi),        /* DISGREG_RSI */
    RT_OFFSETOF(CPUMCTXCORE, rdi),        /* DISGREG_RDI */
    RT_OFFSETOF(CPUMCTXCORE, r8),         /* DISGREG_R8  */
    RT_OFFSETOF(CPUMCTXCORE, r9),         /* DISGREG_R9  */
    RT_OFFSETOF(CPUMCTXCORE, r10),        /* DISGREG_R10 */
    RT_OFFSETOF(CPUMCTXCORE, r11),        /* DISGREG_R11 */
    RT_OFFSETOF(CPUMCTXCORE, r12),        /* DISGREG_R12 */
    RT_OFFSETOF(CPUMCTXCORE, r13),        /* DISGREG_R13 */
    RT_OFFSETOF(CPUMCTXCORE, r14),        /* DISGREG_R14 */
    RT_OFFSETOF(CPUMCTXCORE, r15)         /* DISGREG_R15 */
};

/**
 * Macro for accessing 64-bit general purpose registers in CPUMCTXCORE structure.
 */
#define DIS_READ_REG64(p, idx)       (*(uint64_t *)((char *)(p) + g_aReg64Index[idx]))
#define DIS_WRITE_REG64(p, idx, val) (*(uint64_t *)((char *)(p) + g_aReg64Index[idx]) = val)
#define DIS_PTR_REG64(p, idx)        ( (uint64_t *)((char *)(p) + g_aReg64Index[idx]))

/**
 * Array for accessing 32-bit general registers in VMMREGFRAME structure
 * by register's index from disasm.
 */
static const unsigned g_aReg32Index[] =
{
    RT_OFFSETOF(CPUMCTXCORE, eax),        /* DISGREG_EAX */
    RT_OFFSETOF(CPUMCTXCORE, ecx),        /* DISGREG_ECX */
    RT_OFFSETOF(CPUMCTXCORE, edx),        /* DISGREG_EDX  */
    RT_OFFSETOF(CPUMCTXCORE, ebx),        /* DISGREG_EBX  */
    RT_OFFSETOF(CPUMCTXCORE, esp),        /* DISGREG_ESP */
    RT_OFFSETOF(CPUMCTXCORE, ebp),        /* DISGREG_EBP */
    RT_OFFSETOF(CPUMCTXCORE, esi),        /* DISGREG_ESI */
    RT_OFFSETOF(CPUMCTXCORE, edi),        /* DISGREG_EDI */
    RT_OFFSETOF(CPUMCTXCORE, r8),         /* DISGREG_R8D */
    RT_OFFSETOF(CPUMCTXCORE, r9),         /* DISGREG_R9D */
    RT_OFFSETOF(CPUMCTXCORE, r10),        /* DISGREG_R1D  */
    RT_OFFSETOF(CPUMCTXCORE, r11),        /* DISGREG_R11D */
    RT_OFFSETOF(CPUMCTXCORE, r12),        /* DISGREG_R12D */
    RT_OFFSETOF(CPUMCTXCORE, r13),        /* DISGREG_R13D */
    RT_OFFSETOF(CPUMCTXCORE, r14),        /* DISGREG_R14D */
    RT_OFFSETOF(CPUMCTXCORE, r15)         /* DISGREG_R15D */
};

/**
 * Macro for accessing 32-bit general purpose registers in CPUMCTXCORE structure.
 */
#define DIS_READ_REG32(p, idx)       (*(uint32_t *)((char *)(p) + g_aReg32Index[idx]))
/* From http://www.cs.cmu.edu/~fp/courses/15213-s06/misc/asm64-handout.pdf:
 * ``Perhaps unexpectedly, instructions that move or generate 32-bit register
 *   values also set the upper 32 bits of the register to zero. Consequently
 *   there is no need for an instruction movzlq.''
 */
#define DIS_WRITE_REG32(p, idx, val) (*(uint64_t *)((char *)(p) + g_aReg32Index[idx]) = (uint32_t)val)
#define DIS_PTR_REG32(p, idx)        ( (uint32_t *)((char *)(p) + g_aReg32Index[idx]))

/**
 * Array for accessing 16-bit general registers in CPUMCTXCORE structure
 * by register's index from disasm.
 */
static const unsigned g_aReg16Index[] =
{
    RT_OFFSETOF(CPUMCTXCORE, eax),        /* DISGREG_AX */
    RT_OFFSETOF(CPUMCTXCORE, ecx),        /* DISGREG_CX */
    RT_OFFSETOF(CPUMCTXCORE, edx),        /* DISGREG_DX */
    RT_OFFSETOF(CPUMCTXCORE, ebx),        /* DISGREG_BX */
    RT_OFFSETOF(CPUMCTXCORE, esp),        /* DISGREG_SP */
    RT_OFFSETOF(CPUMCTXCORE, ebp),        /* DISGREG_BP */
    RT_OFFSETOF(CPUMCTXCORE, esi),        /* DISGREG_SI */
    RT_OFFSETOF(CPUMCTXCORE, edi),        /* DISGREG_DI */
    RT_OFFSETOF(CPUMCTXCORE, r8),         /* DISGREG_R8W */
    RT_OFFSETOF(CPUMCTXCORE, r9),         /* DISGREG_R9W */
    RT_OFFSETOF(CPUMCTXCORE, r10),        /* DISGREG_R10W */
    RT_OFFSETOF(CPUMCTXCORE, r11),        /* DISGREG_R11W */
    RT_OFFSETOF(CPUMCTXCORE, r12),        /* DISGREG_R12W */
    RT_OFFSETOF(CPUMCTXCORE, r13),        /* DISGREG_R13W */
    RT_OFFSETOF(CPUMCTXCORE, r14),        /* DISGREG_R14W */
    RT_OFFSETOF(CPUMCTXCORE, r15)         /* DISGREG_R15W */
};

/**
 * Macro for accessing 16-bit general purpose registers in CPUMCTXCORE structure.
 */
#define DIS_READ_REG16(p, idx)          (*(uint16_t *)((char *)(p) + g_aReg16Index[idx]))
#define DIS_WRITE_REG16(p, idx, val)    (*(uint16_t *)((char *)(p) + g_aReg16Index[idx]) = val)
#define DIS_PTR_REG16(p, idx)           ( (uint16_t *)((char *)(p) + g_aReg16Index[idx]))

/**
 * Array for accessing 8-bit general registers in CPUMCTXCORE structure
 * by register's index from disasm.
 */
static const unsigned g_aReg8Index[] =
{
    RT_OFFSETOF(CPUMCTXCORE, eax),        /* DISGREG_AL */
    RT_OFFSETOF(CPUMCTXCORE, ecx),        /* DISGREG_CL */
    RT_OFFSETOF(CPUMCTXCORE, edx),        /* DISGREG_DL */
    RT_OFFSETOF(CPUMCTXCORE, ebx),        /* DISGREG_BL */
    RT_OFFSETOF_ADD(CPUMCTXCORE, eax, 1), /* DISGREG_AH */
    RT_OFFSETOF_ADD(CPUMCTXCORE, ecx, 1), /* DISGREG_CH */
    RT_OFFSETOF_ADD(CPUMCTXCORE, edx, 1), /* DISGREG_DH */
    RT_OFFSETOF_ADD(CPUMCTXCORE, ebx, 1), /* DISGREG_BH */
    RT_OFFSETOF(CPUMCTXCORE, r8),         /* DISGREG_R8B */
    RT_OFFSETOF(CPUMCTXCORE, r9),         /* DISGREG_R9B */
    RT_OFFSETOF(CPUMCTXCORE, r10),        /* DISGREG_R10B*/
    RT_OFFSETOF(CPUMCTXCORE, r11),        /* DISGREG_R11B */
    RT_OFFSETOF(CPUMCTXCORE, r12),        /* DISGREG_R12B */
    RT_OFFSETOF(CPUMCTXCORE, r13),        /* DISGREG_R13B */
    RT_OFFSETOF(CPUMCTXCORE, r14),        /* DISGREG_R14B */
    RT_OFFSETOF(CPUMCTXCORE, r15),        /* DISGREG_R15B */
    RT_OFFSETOF(CPUMCTXCORE, esp),        /* DISGREG_SPL; with REX prefix only */
    RT_OFFSETOF(CPUMCTXCORE, ebp),        /* DISGREG_BPL; with REX prefix only */
    RT_OFFSETOF(CPUMCTXCORE, esi),        /* DISGREG_SIL; with REX prefix only */
    RT_OFFSETOF(CPUMCTXCORE, edi)         /* DISGREG_DIL; with REX prefix only */
};

/**
 * Macro for accessing 8-bit general purpose registers in CPUMCTXCORE structure.
 */
#define DIS_READ_REG8(p, idx)           (*(uint8_t *)((char *)(p) + g_aReg8Index[idx]))
#define DIS_WRITE_REG8(p, idx, val)     (*(uint8_t *)((char *)(p) + g_aReg8Index[idx]) = val)
#define DIS_PTR_REG8(p, idx)            ( (uint8_t *)((char *)(p) + g_aReg8Index[idx]))

/**
 * Array for accessing segment registers in CPUMCTXCORE structure
 * by register's index from disasm.
 */
static const unsigned g_aRegSegIndex[] =
{
    RT_OFFSETOF(CPUMCTXCORE, es),         /* DISSELREG_ES */
    RT_OFFSETOF(CPUMCTXCORE, cs),         /* DISSELREG_CS */
    RT_OFFSETOF(CPUMCTXCORE, ss),         /* DISSELREG_SS */
    RT_OFFSETOF(CPUMCTXCORE, ds),         /* DISSELREG_DS */
    RT_OFFSETOF(CPUMCTXCORE, fs),         /* DISSELREG_FS */
    RT_OFFSETOF(CPUMCTXCORE, gs)          /* DISSELREG_GS */
};

/**
 * Macro for accessing segment registers in CPUMCTXCORE structure.
 */
#define DIS_READ_REGSEG(p, idx)         (*((uint16_t *)((char *)(p) + g_aRegSegIndex[idx])))
#define DIS_WRITE_REGSEG(p, idx, val)   (*((uint16_t *)((char *)(p) + g_aRegSegIndex[idx])) = val)



/**
 * Returns the value of the specified 8 bits general purpose register
 *
 */
DISDECL(int) DISFetchReg8(PCCPUMCTXCORE pCtx, unsigned reg8, uint8_t *pVal)
{
    AssertReturnStmt(reg8 < RT_ELEMENTS(g_aReg8Index), *pVal = 0, VERR_INVALID_PARAMETER);

    *pVal = DIS_READ_REG8(pCtx, reg8);
    return VINF_SUCCESS;
}

/**
 * Returns the value of the specified 16 bits general purpose register
 *
 */
DISDECL(int) DISFetchReg16(PCCPUMCTXCORE pCtx, unsigned reg16, uint16_t *pVal)
{
    AssertReturnStmt(reg16 < RT_ELEMENTS(g_aReg16Index), *pVal = 0, VERR_INVALID_PARAMETER);

    *pVal = DIS_READ_REG16(pCtx, reg16);
    return VINF_SUCCESS;
}

/**
 * Returns the value of the specified 32 bits general purpose register
 *
 */
DISDECL(int) DISFetchReg32(PCCPUMCTXCORE pCtx, unsigned reg32, uint32_t *pVal)
{
    AssertReturnStmt(reg32 < RT_ELEMENTS(g_aReg32Index), *pVal = 0, VERR_INVALID_PARAMETER);

    *pVal = DIS_READ_REG32(pCtx, reg32);
    return VINF_SUCCESS;
}

/**
 * Returns the value of the specified 64 bits general purpose register
 *
 */
DISDECL(int) DISFetchReg64(PCCPUMCTXCORE pCtx, unsigned reg64, uint64_t *pVal)
{
    AssertReturnStmt(reg64 < RT_ELEMENTS(g_aReg64Index), *pVal = 0, VERR_INVALID_PARAMETER);

    *pVal = DIS_READ_REG64(pCtx, reg64);
    return VINF_SUCCESS;
}

/**
 * Returns the pointer to the specified 8 bits general purpose register
 *
 */
DISDECL(int) DISPtrReg8(PCPUMCTXCORE pCtx, unsigned reg8, uint8_t **ppReg)
{
    AssertReturnStmt(reg8 < RT_ELEMENTS(g_aReg8Index), *ppReg = NULL, VERR_INVALID_PARAMETER);

    *ppReg = DIS_PTR_REG8(pCtx, reg8);
    return VINF_SUCCESS;
}

/**
 * Returns the pointer to the specified 16 bits general purpose register
 *
 */
DISDECL(int) DISPtrReg16(PCPUMCTXCORE pCtx, unsigned reg16, uint16_t **ppReg)
{
    AssertReturnStmt(reg16 < RT_ELEMENTS(g_aReg16Index), *ppReg = NULL, VERR_INVALID_PARAMETER);

    *ppReg = DIS_PTR_REG16(pCtx, reg16);
    return VINF_SUCCESS;
}

/**
 * Returns the pointer to the specified 32 bits general purpose register
 */
DISDECL(int) DISPtrReg32(PCPUMCTXCORE pCtx, unsigned reg32, uint32_t **ppReg)
{
    AssertReturnStmt(reg32 < RT_ELEMENTS(g_aReg32Index), *ppReg = NULL, VERR_INVALID_PARAMETER);

    *ppReg = DIS_PTR_REG32(pCtx, reg32);
    return VINF_SUCCESS;
}

/**
 * Returns the pointer to the specified 64 bits general purpose register
 */
DISDECL(int) DISPtrReg64(PCPUMCTXCORE pCtx, unsigned reg64, uint64_t **ppReg)
{
    AssertReturnStmt(reg64 < RT_ELEMENTS(g_aReg64Index), *ppReg = NULL, VERR_INVALID_PARAMETER);

    *ppReg = DIS_PTR_REG64(pCtx, reg64);
    return VINF_SUCCESS;
}

/**
 * Returns the value of the specified segment register
 */
DISDECL(int) DISFetchRegSeg(PCCPUMCTXCORE pCtx, DISSELREG sel, RTSEL *pVal)
{
    AssertReturn((unsigned)sel < RT_ELEMENTS(g_aRegSegIndex), VERR_INVALID_PARAMETER);

    AssertCompile(sizeof(uint16_t) == sizeof(RTSEL));
    *pVal = DIS_READ_REGSEG(pCtx, sel);
    return VINF_SUCCESS;
}

/**
 * Updates the value of the specified 64 bits general purpose register
 *
 */
DISDECL(int) DISWriteReg64(PCPUMCTXCORE pRegFrame, unsigned reg64, uint64_t val64)
{
    AssertReturn(reg64 < RT_ELEMENTS(g_aReg64Index), VERR_INVALID_PARAMETER);

    DIS_WRITE_REG64(pRegFrame, reg64, val64);
    return VINF_SUCCESS;
}

/**
 * Updates the value of the specified 32 bits general purpose register
 *
 */
DISDECL(int) DISWriteReg32(PCPUMCTXCORE pRegFrame, unsigned reg32, uint32_t val32)
{
    AssertReturn(reg32 < RT_ELEMENTS(g_aReg32Index), VERR_INVALID_PARAMETER);

    DIS_WRITE_REG32(pRegFrame, reg32, val32);
    return VINF_SUCCESS;
}

/**
 * Updates the value of the specified 16 bits general purpose register
 *
 */
DISDECL(int) DISWriteReg16(PCPUMCTXCORE pRegFrame, unsigned reg16, uint16_t val16)
{
    AssertReturn(reg16 < RT_ELEMENTS(g_aReg16Index), VERR_INVALID_PARAMETER);

    DIS_WRITE_REG16(pRegFrame, reg16, val16);
    return VINF_SUCCESS;
}

/**
 * Updates the specified 8 bits general purpose register
 *
 */
DISDECL(int) DISWriteReg8(PCPUMCTXCORE pRegFrame, unsigned reg8, uint8_t val8)
{
    AssertReturn(reg8 < RT_ELEMENTS(g_aReg8Index), VERR_INVALID_PARAMETER);

    DIS_WRITE_REG8(pRegFrame, reg8, val8);
    return VINF_SUCCESS;
}

/**
 * Updates the specified segment register
 *
 */
DISDECL(int) DISWriteRegSeg(PCPUMCTXCORE pCtx, DISSELREG sel, RTSEL val)
{
    AssertReturn((unsigned)sel < RT_ELEMENTS(g_aRegSegIndex), VERR_INVALID_PARAMETER);

    AssertCompile(sizeof(uint16_t) == sizeof(RTSEL));
    DIS_WRITE_REGSEG(pCtx, sel, val);
    return VINF_SUCCESS;
}

/**
 * Returns the value of the parameter in pParam
 *
 * @returns VBox error code
 * @param   pCtx            CPU context structure pointer
 * @param   pDis            Pointer to the disassembler state.
 * @param   pParam          Pointer to the parameter to parse
 * @param   pParamVal       Pointer to parameter value (OUT)
 * @param   parmtype        Parameter type
 *
 * @note    Currently doesn't handle FPU/XMM/MMX/3DNow! parameters correctly!!
 *
 */
DISDECL(int) DISQueryParamVal(PCPUMCTXCORE pCtx, PCDISSTATE pDis, PCDISOPPARAM pParam, PDISQPVPARAMVAL pParamVal, DISQPVWHICH parmtype)
{
    memset(pParamVal, 0, sizeof(*pParamVal));

    if (DISUSE_IS_EFFECTIVE_ADDR(pParam->fUse))
    {
        // Effective address
        pParamVal->type = DISQPV_TYPE_ADDRESS;
        pParamVal->size = pParam->cb;

        if (pParam->fUse & DISUSE_BASE)
        {
            if (pParam->fUse & DISUSE_REG_GEN8)
            {
                pParamVal->flags |= DISQPV_FLAG_8;
                if (RT_FAILURE(DISFetchReg8(pCtx, pParam->Base.idxGenReg, &pParamVal->val.val8))) return VERR_INVALID_PARAMETER;
            }
            else
            if (pParam->fUse & DISUSE_REG_GEN16)
            {
                pParamVal->flags |= DISQPV_FLAG_16;
                if (RT_FAILURE(DISFetchReg16(pCtx, pParam->Base.idxGenReg, &pParamVal->val.val16))) return VERR_INVALID_PARAMETER;
            }
            else
            if (pParam->fUse & DISUSE_REG_GEN32)
            {
                pParamVal->flags |= DISQPV_FLAG_32;
                if (RT_FAILURE(DISFetchReg32(pCtx, pParam->Base.idxGenReg, &pParamVal->val.val32))) return VERR_INVALID_PARAMETER;
            }
            else
            if (pParam->fUse & DISUSE_REG_GEN64)
            {
                pParamVal->flags |= DISQPV_FLAG_64;
                if (RT_FAILURE(DISFetchReg64(pCtx, pParam->Base.idxGenReg, &pParamVal->val.val64))) return VERR_INVALID_PARAMETER;
            }
            else
            {
                AssertFailed();
                return VERR_INVALID_PARAMETER;
            }
        }
        // Note that scale implies index (SIB byte)
        if (pParam->fUse & DISUSE_INDEX)
        {
            if (pParam->fUse & DISUSE_REG_GEN16)
            {
                uint16_t val16;

                pParamVal->flags |= DISQPV_FLAG_16;
                if (RT_FAILURE(DISFetchReg16(pCtx, pParam->Index.idxGenReg, &val16))) return VERR_INVALID_PARAMETER;

                Assert(!(pParam->fUse & DISUSE_SCALE));   /* shouldn't be possible in 16 bits mode */

                pParamVal->val.val16 += val16;
            }
            else
            if (pParam->fUse & DISUSE_REG_GEN32)
            {
                uint32_t val32;

                pParamVal->flags |= DISQPV_FLAG_32;
                if (RT_FAILURE(DISFetchReg32(pCtx, pParam->Index.idxGenReg, &val32))) return VERR_INVALID_PARAMETER;

                if (pParam->fUse & DISUSE_SCALE)
                    val32 *= pParam->uScale;

                pParamVal->val.val32 += val32;
            }
            else
            if (pParam->fUse & DISUSE_REG_GEN64)
            {
                uint64_t val64;

                pParamVal->flags |= DISQPV_FLAG_64;
                if (RT_FAILURE(DISFetchReg64(pCtx, pParam->Index.idxGenReg, &val64))) return VERR_INVALID_PARAMETER;

                if (pParam->fUse & DISUSE_SCALE)
                    val64 *= pParam->uScale;

                pParamVal->val.val64 += val64;
            }
            else
                AssertFailed();
        }

        if (pParam->fUse & DISUSE_DISPLACEMENT8)
        {
            if (pDis->uCpuMode == DISCPUMODE_32BIT)
                pParamVal->val.i32 += (int32_t)pParam->uDisp.i8;
            else if (pDis->uCpuMode == DISCPUMODE_64BIT)
                pParamVal->val.i64 += (int64_t)pParam->uDisp.i8;
            else
                pParamVal->val.i16 += (int16_t)pParam->uDisp.i8;
        }
        else if (pParam->fUse & DISUSE_DISPLACEMENT16)
        {
            if (pDis->uCpuMode == DISCPUMODE_32BIT)
                pParamVal->val.i32 += (int32_t)pParam->uDisp.i16;
            else if (pDis->uCpuMode == DISCPUMODE_64BIT)
                pParamVal->val.i64 += (int64_t)pParam->uDisp.i16;
            else
                pParamVal->val.i16 += pParam->uDisp.i16;
        }
        else if (pParam->fUse & DISUSE_DISPLACEMENT32)
        {
            if (pDis->uCpuMode == DISCPUMODE_32BIT)
                pParamVal->val.i32 += pParam->uDisp.i32;
            else
                pParamVal->val.i64 += pParam->uDisp.i32;
        }
        else if (pParam->fUse & DISUSE_DISPLACEMENT64)
        {
            Assert(pDis->uCpuMode == DISCPUMODE_64BIT);
            pParamVal->val.i64 += pParam->uDisp.i64;
        }
        else if (pParam->fUse & DISUSE_RIPDISPLACEMENT32)
        {
            Assert(pDis->uCpuMode == DISCPUMODE_64BIT);
            /* Relative to the RIP of the next instruction. */
            pParamVal->val.i64 += pParam->uDisp.i32 + pCtx->rip + pDis->cbInstr;
        }
        return VINF_SUCCESS;
    }

    if (pParam->fUse & (DISUSE_REG_GEN8|DISUSE_REG_GEN16|DISUSE_REG_GEN32|DISUSE_REG_GEN64|DISUSE_REG_FP|DISUSE_REG_MMX|DISUSE_REG_XMM|DISUSE_REG_CR|DISUSE_REG_DBG|DISUSE_REG_SEG|DISUSE_REG_TEST))
    {
        if (parmtype == DISQPVWHICH_DST)
        {
            // Caller needs to interpret the register according to the instruction (source/target, special value etc)
            pParamVal->type = DISQPV_TYPE_REGISTER;
            pParamVal->size = pParam->cb;
            return VINF_SUCCESS;
        }
        //else DISQPVWHICH_SRC

        pParamVal->type = DISQPV_TYPE_IMMEDIATE;

        if (pParam->fUse & DISUSE_REG_GEN8)
        {
            pParamVal->flags |= DISQPV_FLAG_8;
            pParamVal->size   = sizeof(uint8_t);
            if (RT_FAILURE(DISFetchReg8(pCtx, pParam->Base.idxGenReg, &pParamVal->val.val8))) return VERR_INVALID_PARAMETER;
        }
        else
        if (pParam->fUse & DISUSE_REG_GEN16)
        {
            pParamVal->flags |= DISQPV_FLAG_16;
            pParamVal->size   = sizeof(uint16_t);
            if (RT_FAILURE(DISFetchReg16(pCtx, pParam->Base.idxGenReg, &pParamVal->val.val16))) return VERR_INVALID_PARAMETER;
        }
        else
        if (pParam->fUse & DISUSE_REG_GEN32)
        {
            pParamVal->flags |= DISQPV_FLAG_32;
            pParamVal->size   = sizeof(uint32_t);
            if (RT_FAILURE(DISFetchReg32(pCtx, pParam->Base.idxGenReg, &pParamVal->val.val32))) return VERR_INVALID_PARAMETER;
        }
        else
        if (pParam->fUse & DISUSE_REG_GEN64)
        {
            pParamVal->flags |= DISQPV_FLAG_64;
            pParamVal->size   = sizeof(uint64_t);
            if (RT_FAILURE(DISFetchReg64(pCtx, pParam->Base.idxGenReg, &pParamVal->val.val64))) return VERR_INVALID_PARAMETER;
        }
        else
        {
            // Caller needs to interpret the register according to the instruction (source/target, special value etc)
            pParamVal->type = DISQPV_TYPE_REGISTER;
        }
        Assert(!(pParam->fUse & DISUSE_IMMEDIATE));
        return VINF_SUCCESS;
    }

    if (pParam->fUse & DISUSE_IMMEDIATE)
    {
        pParamVal->type = DISQPV_TYPE_IMMEDIATE;
        if (pParam->fUse & (DISUSE_IMMEDIATE8|DISUSE_IMMEDIATE8_REL))
        {
            pParamVal->flags |= DISQPV_FLAG_8;
            if (pParam->cb == 2)
            {
                pParamVal->size   = sizeof(uint16_t);
                pParamVal->val.val16 = (uint8_t)pParam->uValue;
            }
            else
            {
                pParamVal->size   = sizeof(uint8_t);
                pParamVal->val.val8 = (uint8_t)pParam->uValue;
            }
        }
        else
        if (pParam->fUse & (DISUSE_IMMEDIATE16|DISUSE_IMMEDIATE16_REL|DISUSE_IMMEDIATE_ADDR_0_16|DISUSE_IMMEDIATE16_SX8))
        {
            pParamVal->flags |= DISQPV_FLAG_16;
            pParamVal->size   = sizeof(uint16_t);
            pParamVal->val.val16 = (uint16_t)pParam->uValue;
            AssertMsg(pParamVal->size == pParam->cb || ((pParam->cb == 1) && (pParam->fUse & DISUSE_IMMEDIATE16_SX8)), ("pParamVal->size %d vs %d EIP=%RX32\n", pParamVal->size, pParam->cb, pCtx->eip) );
        }
        else
        if (pParam->fUse & (DISUSE_IMMEDIATE32|DISUSE_IMMEDIATE32_REL|DISUSE_IMMEDIATE_ADDR_0_32|DISUSE_IMMEDIATE32_SX8))
        {
            pParamVal->flags |= DISQPV_FLAG_32;
            pParamVal->size   = sizeof(uint32_t);
            pParamVal->val.val32 = (uint32_t)pParam->uValue;
            Assert(pParamVal->size == pParam->cb || ((pParam->cb == 1) && (pParam->fUse & DISUSE_IMMEDIATE32_SX8)) );
        }
        else
        if (pParam->fUse & (DISUSE_IMMEDIATE64 | DISUSE_IMMEDIATE64_REL | DISUSE_IMMEDIATE64_SX8))
        {
            pParamVal->flags |= DISQPV_FLAG_64;
            pParamVal->size   = sizeof(uint64_t);
            pParamVal->val.val64 = pParam->uValue;
            Assert(pParamVal->size == pParam->cb || ((pParam->cb == 1) && (pParam->fUse & DISUSE_IMMEDIATE64_SX8)) );
        }
        else
        if (pParam->fUse & (DISUSE_IMMEDIATE_ADDR_16_16))
        {
            pParamVal->flags |= DISQPV_FLAG_FARPTR16;
            pParamVal->size   = sizeof(uint16_t)*2;
            pParamVal->val.farptr.sel    = (uint16_t)RT_LOWORD(pParam->uValue >> 16);
            pParamVal->val.farptr.offset = (uint32_t)RT_LOWORD(pParam->uValue);
            Assert(pParamVal->size == pParam->cb);
        }
        else
        if (pParam->fUse & (DISUSE_IMMEDIATE_ADDR_16_32))
        {
            pParamVal->flags |= DISQPV_FLAG_FARPTR32;
            pParamVal->size   = sizeof(uint16_t) + sizeof(uint32_t);
            pParamVal->val.farptr.sel    = (uint16_t)RT_LOWORD(pParam->uValue >> 32);
            pParamVal->val.farptr.offset = (uint32_t)(pParam->uValue & 0xFFFFFFFF);
            Assert(pParam->cb == 8);
        }
    }
    return VINF_SUCCESS;
}

/**
 * Returns the pointer to a register of the parameter in pParam. We need this
 * pointer when an interpreted instruction updates a register as a side effect.
 * In CMPXCHG we know that only [r/e]ax is updated, but with XADD this could
 * be every register.
 *
 * @returns VBox error code
 * @param   pCtx            CPU context structure pointer
 * @param   pDis            Pointer to the disassembler state.
 * @param   pParam          Pointer to the parameter to parse
 * @param   pReg            Pointer to parameter value (OUT)
 * @param   cbsize          Parameter size (OUT)
 *
 * @note    Currently doesn't handle FPU/XMM/MMX/3DNow! parameters correctly!!
 *
 */
DISDECL(int) DISQueryParamRegPtr(PCPUMCTXCORE pCtx, PCDISSTATE pDis, PCDISOPPARAM pParam, void **ppReg, size_t *pcbSize)
{
    NOREF(pDis);
    if (pParam->fUse & (DISUSE_REG_GEN8|DISUSE_REG_GEN16|DISUSE_REG_GEN32|DISUSE_REG_FP|DISUSE_REG_MMX|DISUSE_REG_XMM|DISUSE_REG_CR|DISUSE_REG_DBG|DISUSE_REG_SEG|DISUSE_REG_TEST))
    {
        if (pParam->fUse & DISUSE_REG_GEN8)
        {
            uint8_t *pu8Reg;
            if (RT_SUCCESS(DISPtrReg8(pCtx, pParam->Base.idxGenReg, &pu8Reg)))
            {
                *pcbSize = sizeof(uint8_t);
                *ppReg = (void *)pu8Reg;
                return VINF_SUCCESS;
            }
        }
        else
        if (pParam->fUse & DISUSE_REG_GEN16)
        {
            uint16_t *pu16Reg;
            if (RT_SUCCESS(DISPtrReg16(pCtx, pParam->Base.idxGenReg, &pu16Reg)))
            {
                *pcbSize = sizeof(uint16_t);
                *ppReg = (void *)pu16Reg;
                return VINF_SUCCESS;
            }
        }
        else
        if (pParam->fUse & DISUSE_REG_GEN32)
        {
            uint32_t *pu32Reg;
            if (RT_SUCCESS(DISPtrReg32(pCtx, pParam->Base.idxGenReg, &pu32Reg)))
            {
                *pcbSize = sizeof(uint32_t);
                *ppReg = (void *)pu32Reg;
                return VINF_SUCCESS;
            }
        }
        else
        if (pParam->fUse & DISUSE_REG_GEN64)
        {
            uint64_t *pu64Reg;
            if (RT_SUCCESS(DISPtrReg64(pCtx, pParam->Base.idxGenReg, &pu64Reg)))
            {
                *pcbSize = sizeof(uint64_t);
                *ppReg = (void *)pu64Reg;
                return VINF_SUCCESS;
            }
        }
    }
    return VERR_INVALID_PARAMETER;
}

