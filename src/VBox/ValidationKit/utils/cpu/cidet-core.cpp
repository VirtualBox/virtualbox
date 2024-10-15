/* $Id: cidet-core.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * CPU Instruction Decoding & Execution Tests - Simple Instructions.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define CIDET_INSTR_TEST_OP_FLAG(a_pInstr, a_fFlag) \
    (   ((a_pInstr)->afOperands[0] & (a_fFlag)) \
     || ((a_pInstr)->afOperands[1] & (a_fFlag)) \
     || (   (a_pInstr)->cOperands > 2 \
         && (   ((a_pInstr)->afOperands[2] & (a_fFlag)) \
             || ((a_pInstr)->afOperands[3] & (a_fFlag))  ) ) )

#define CIDET_INSTR_TEST_OP_MASK_VALUE(a_pInstr, a_fMask, a_fValue) \
    (   ((a_pInstr)->afOperands[0] & (a_fMask)) == (a_fValue) \
     || ((a_pInstr)->afOperands[1] & (a_fMask)) == (a_fValue) \
     || (   (a_pInstr)->cOperands > 2 \
         && (   ((a_pInstr)->afOperands[2] & (a_fMask)) == (a_fValue) \
             || ((a_pInstr)->afOperands[3] & (a_fMask)) == (a_fValue) ) ) )

/** @def CIDET_DPRINTF
 * Debug printf. */
#if 1 //def DEBUG_bird
# define CIDET_DPRINTF(a)   do { RTPrintf a; } while (0)
# define CIDET_DPRINTF_ENABLED
#else
# define CIDET_DPRINTF(a)   do { } while (0)
#endif

/** @def CIDET_DEBUG_DISAS
 * Enables instruction disassembly. */
#if defined(DOXYGEN_RUNNING)
# define CIDET_DEBUG_DISAS 1
#endif


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "cidet.h"

#include <iprt/assert.h>
#include <iprt/rand.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/errcore.h>
#if defined(CIDET_DPRINTF_ENABLED) || defined(CIDET_DEBUG_DISAS)
# include <VBox/dis.h>
# include <iprt/stream.h>
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** For translating CIDET_OF_Z_XXX values (after shifting). */
uint16_t const g_acbCidetOfSizes[] =
{
    /* [CIDET_OF_Z_NONE]    = */  0,
    /* [CIDET_OF_Z_BYTE]    = */  1,
    /* [CIDET_OF_Z_WORD]    = */  2,
    /* [CIDET_OF_Z_DWORD]   = */  4,
    /* [CIDET_OF_Z_QWORD]   = */  8,
    /* [CIDET_OF_Z_TBYTE]   = */  10,
    /* [CIDET_OF_Z_OWORD]   = */  16,
    /* [CIDET_OF_Z_YWORD]   = */  32,
    /* [CIDET_OF_Z_ZWORD]   = */  64,
    /* [CIDET_OF_Z_VAR_WDQ] = */  UINT16_MAX,
    /* [0xa] = */                 0,
    /* [0xb] = */                 0,
    /* [0xc] = */                 0,
    /* [0xd] = */                 0,
    /* [0xe] = */                 0,
    /* [CIDET_OF_Z_SPECIAL] = */  UINT16_MAX - 1,
};


/** Converts operand sizes in bytes to 64-bit masks. */
static const uint64_t g_au64ByteSizeToMask[] =
{
    UINT64_C(0x0000000000000000),
    UINT64_C(0x00000000000000ff),
    UINT64_C(0x000000000000ffff),
    UINT64_C(0x0000000000ffffff),
    UINT64_C(0x00000000ffffffff),
    UINT64_C(0x000000ffffffffff),
    UINT64_C(0x0000ffffffffffff),
    UINT64_C(0x00ffffffffffffff),
    UINT64_C(0xffffffffffffffff),
};

/** Converts operand sizes in bytes to 64-bit signed max values. */
static const int64_t g_ai64ByteSizeToMax[] =
{
    INT64_C(0x0000000000000000),
    INT64_C(0x000000000000007f),
    INT64_C(0x0000000000007fff),
    INT64_C(0x00000000007fffff),
    INT64_C(0x000000007fffffff),
    INT64_C(0x0000007fffffffff),
    INT64_C(0x00007fffffffffff),
    INT64_C(0x007fffffffffffff),
    INT64_C(0x7fffffffffffffff),
};


static bool CidetInstrHasMrmMemOperand(PCCIDETINSTR pInstr)
{
    return CIDET_INSTR_TEST_OP_FLAG(pInstr, CIDET_OF_M_RM_ONLY_M);
}


static bool CidetInstrHasMrmRegOperand(PCCIDETINSTR pInstr)
{
    return CIDET_INSTR_TEST_OP_FLAG(pInstr, CIDET_OF_M_RM_ONLY_R);
}


static bool CidetInstrRespondsToOperandSizePrefixes(PCCIDETINSTR pInstr)
{
    return CIDET_INSTR_TEST_OP_MASK_VALUE(pInstr, CIDET_OF_Z_MASK, CIDET_OF_Z_VAR_WDQ);
}




int CidetCoreInit(PCIDETCORE pThis, RTRAND hRand)
{
    AssertPtr(pThis);
    AssertPtr(hRand);

    RT_ZERO(*pThis);
    pThis->u32Magic  = CIDETCORE_MAGIC;
    pThis->hRand = hRand;
    return VINF_SUCCESS;
}


void CidetCoreDelete(PCIDETCORE pThis)
{
    AssertPtr(pThis); Assert(pThis->u32Magic == CIDETCORE_MAGIC);

    RTRandAdvDestroy(pThis->hRand);
    RT_ZERO(*pThis);
}


/**
 * Report a test failure via CIDET::pfnFailure
 *
 * @returns false
 * @param   pThis           Pointer to the core structure.
 * @param   pszFormat       Format string containing failure details.
 * @param   va              Arguments referenced in @a pszFormat.
 */
static int CidetCoreSetErrorV(PCIDETCORE pThis, const char *pszFormat, va_list va)
{
    pThis->pfnFailure(pThis, pszFormat, va);
    return false;
}


/**
 * Report a test failure via CIDET::pfnFailure
 *
 * @returns false
 * @param   pThis           Pointer to the core structure.
 * @param   pszFormat       Format string containing failure details.
 * @param   ...             Arguments referenced in @a pszFormat.
 */
static bool CidetCoreSetError(PCIDETCORE pThis, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    CidetCoreSetErrorV(pThis, pszFormat, va);
    va_end(va);
    return false;
}


/**
 * Get a signed random number, with a given number of significant bytes.
 *
 * @returns Random number.
 * @param   pThis           Pointer to the core structure.
 * @param   cbSignificant   The number of significant bytes.
 */
static int64_t CidetCoreGetRandS64(PCIDETCORE pThis, uint8_t cbSignificant)
{
    int64_t iVal = RTRandAdvS64(pThis->hRand);
    switch (cbSignificant)
    {
        case 1: return (int8_t)iVal;
        case 2: return (int16_t)iVal;
        case 4: return (int32_t)iVal;
        case 8: return iVal;
        default:
            AssertReleaseFailed();
            return iVal;
    }
}


/**
 * Get an unsigned random number, with a given number of significant bytes.
 *
 * @returns Random number.
 * @param   pThis           Pointer to the core structure.
 * @param   cbSignificant   The number of significant bytes.
 */
static uint64_t CidetCoreGetRandU64(PCIDETCORE pThis, uint8_t cbSignificant)
{
    Assert(cbSignificant == 1 || cbSignificant == 2 || cbSignificant == 4 || cbSignificant == 8);

    uint64_t uVal = RTRandAdvU64(pThis->hRand);
    uVal &= g_au64ByteSizeToMask[cbSignificant];

    return uVal;
}



static void CidetCoreInitializeCtxTemplate(PCIDETCORE pThis)
{
    pThis->InTemplateCtx.rip = UINT64_MAX;
    pThis->InTemplateCtx.rfl = X86_EFL_1 | X86_EFL_ID | X86_EFL_IF;

    unsigned i = RT_ELEMENTS(pThis->InTemplateCtx.aGRegs);
    if (CIDETMODE_IS_LM(pThis->bMode))
        while (i-- > 0)
            pThis->InTemplateCtx.aGRegs[i] = UINT64_C(0x3fefcc00daba005d)
                                           | ((uint64_t)i << 32)
                                           | ((uint32_t)i << 8);
    else
        while (i-- > 0)
            pThis->InTemplateCtx.aGRegs[i] = UINT64_C(0xfada009b)
                                           | ((uint32_t)i << 12)
                                           | ((uint32_t)i << 8);
    i = RT_ELEMENTS(pThis->InTemplateCtx.aSRegs);
    while (i-- > 0)
        pThis->InTemplateCtx.aSRegs[i] = 0; /* Front end sets these afterwards. */
    pThis->InTemplateCtx.cr2  = 0;
#ifndef CIDET_REDUCED_CTX
    pThis->InTemplateCtx.tr   = 0;
    pThis->InTemplateCtx.ldtr = 0;
    pThis->InTemplateCtx.cr0  = 0;
    pThis->InTemplateCtx.cr3  = 0;
    pThis->InTemplateCtx.cr4  = 0;
    pThis->InTemplateCtx.cr8  = 0;
#endif
    pThis->InTemplateCtx.fIgnoredRFlags = 0;
    pThis->InTemplateCtx.uXcpt          = UINT32_MAX;
    pThis->InTemplateCtx.uErr           = UINT64_MAX;
    pThis->InTemplateCtx.fTrickyStack   = false;
}


/**
 * Sets the target mode.
 *
 * Caller must set up default selector values after calling this function.
 *
 * @returns VBox status code.
 * @param   pThis           Pointer to the core structure.
 * @param   bMode           The new mode.
 */
int CidetCoreSetTargetMode(PCIDETCORE pThis, uint8_t bMode)
{
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE); AssertReturn(pThis->u32Magic == CIDETCORE_MAGIC, VERR_INVALID_HANDLE);
    switch (bMode)
    {
        //case CIDETMODE_RM:
        //case CIDETMODE_PE_16:
        //case CIDETMODE_PE_32:
        //case CIDETMODE_PE_V86:
        //case CIDETMODE_PP_16:
        case CIDETMODE_PP_32:
        //case CIDETMODE_PP_V86:
        //case CIDETMODE_PAE_16:
        case CIDETMODE_PAE_32:
        //case CIDETMODE_PAE_V86:
        //case CIDETMODE_LM_S16:
        //case CIDETMODE_LM_32:
        case CIDETMODE_LM_64:
            break;
        default:
            return VERR_NOT_IMPLEMENTED;
    }
    pThis->bMode = bMode;
    CidetCoreInitializeCtxTemplate(pThis);
    return VINF_SUCCESS;
}

#if 0 /* unused */
static bool CidetCoreIsEncodingCompatibleWithInstruction(PCIDETCORE pThis)
{
    RT_NOREF_PV(pThis);
    return true;
}
#endif

/**
 * Selects the next address size mode.
 *
 * @returns @c true if done, @c false if the next wheel needs to be moved.
 * @param   pThis               The core state structure.
 */
static bool cidetCoreSetupNextBaseEncoding_AddressSize(PCIDETCORE pThis)
{
    if (pThis->fAddrSizePrf)
    {
        /*
         * Reset to default.
         */
        pThis->cbAddrMode   = CIDETMODE_GET_BYTE_COUNT(pThis->bMode);
        pThis->fAddrSizePrf = false;
    }
    else
    {
        /*
         * The other addressing size.
         */
        if (CIDETMODE_IS_64BIT(pThis->bMode))
            pThis->cbAddrMode = 4;
        else if (CIDETMODE_IS_32BIT(pThis->bMode))
            pThis->cbAddrMode = 2;
        else
        {
            AssertRelease(CIDETMODE_IS_16BIT(pThis->bMode));
            pThis->cbAddrMode = 2;
        }
        pThis->fAddrSizePrf = true;
    }
    return pThis->fAddrSizePrf;
}


/**
 * Selects the first REG encoding.
 *
 * @param   pThis               The core state structure.
 */
static void cidetCoreSetupFirstBaseEncoding_MrmReg(PCIDETCORE pThis)
{
    pThis->aOperands[pThis->idxMrmRegOp].iReg            = 0;
    pThis->aOperands[pThis->idxMrmRegOp].fIsMem          = false;
    pThis->aOperands[pThis->idxMrmRegOp].fIsRipRelative  = false;
    pThis->aOperands[pThis->idxMrmRegOp].fIsHighByteRegister = false;
    pThis->aOperands[pThis->idxMrmRegOp].cbMemDisp       = 0;
    pThis->aOperands[pThis->idxMrmRegOp].iMemBaseReg     = UINT8_MAX;
    pThis->aOperands[pThis->idxMrmRegOp].iMemIndexReg    = UINT8_MAX;
    pThis->aOperands[pThis->idxMrmRegOp].uMemScale       = 1;
    pThis->aOperands[pThis->idxMrmRegOp].iEffSeg         = UINT8_MAX;
    pThis->bModRm  &= ~X86_MODRM_REG_MASK;
    pThis->fRexR    = false;
}


/**
 * Selects the next REG (ModR/M) encoding.
 *
 * @returns @c true if done, @c false if the next wheel needs to be moved.
 * @param   pThis               The core state structure.
 * @param   iReg                The value of MODRM.REG /w REX.R applied.
 */
static bool cidetCoreSetupNextBaseEncoding_MrmReg(PCIDETCORE pThis, uint8_t iReg)
{
    Assert(pThis->idxMrmRegOp < RT_ELEMENTS(pThis->aOperands) && !pThis->aOperands[pThis->idxMrmRegOp].fIsMem);
    Assert(iReg < 16);

    /*
     * Clear the collision flags here because of the byte register kludge.
     */
    pThis->fHasRegCollisionDirect   = false;
    pThis->fHasRegCollisionMemBase  = false;
    pThis->fHasRegCollisionMemIndex = false;
    pThis->fHasRegCollisionMem      = false;

    /*
     * Clear the REX prefix and high byte register tracking too.  ASSUMES MrmReg is after MrmRmMod.
     */
    Assert(!pThis->fNoRexPrefixMrmRm);
    Assert(!pThis->fHasHighByteRegInMrmRm);
    pThis->fNoRexPrefixMrmReg = false;
    pThis->fNoRexPrefix = false;
    pThis->fHasHighByteRegInMrmReg = false;
    pThis->aOperands[pThis->idxMrmRegOp].fIsHighByteRegister = false;

    /*
     * Special kludge for ah, ch, dh, bh, spl, bpl, sil, and dil.
     * Needs extra care in 64-bit mode and special collision detection code.
     */
    CIDET_DPRINTF(("aOperands[%u].cb=%u fGpr=%u iReg=%d fRex=%d fRexW=%u fRexX=%u fRexB=%u fRexR=%d\n",
                   pThis->idxMrmRegOp, pThis->aOperands[pThis->idxMrmRegOp].cb, CIDET_OF_K_IS_GPR(pThis->fMrmRegOp), iReg,
                   pThis->fRex, pThis->fRexW, pThis->fRexX, pThis->fRexB, pThis->fRexR));
    if (   pThis->aOperands[pThis->idxMrmRegOp].cb == 1
        && CIDET_OF_K_IS_GPR(pThis->fMrmRegOp)
        && iReg >= 3
        && (   iReg <= 6
            || (CIDETMODE_IS_64BIT(pThis->bMode) && iReg == 7 && !pThis->fRex)) )

    {
        if (!pThis->fRex && iReg >= 4 && CIDETMODE_IS_64BIT(pThis->bMode) && !pThis->fNoRexPrefix)
        {
            /* The AMD64 low variants: spl, bpl, sil and dil. */
            pThis->fRex = true;
            pThis->fHasStackRegInMrmReg = iReg == X86_GREG_xSP;

            /* Check for collisions. */
            if (pThis->idxMrmRmOp < RT_ELEMENTS(pThis->aOperands))
            {
                Assert(!pThis->fHasHighByteRegInMrmRm);
                if (!pThis->aOperands[pThis->idxMrmRmOp].fIsMem)
                    pThis->fHasRegCollisionDirect = CIDET_OF_K_IS_GPR(pThis->fMrmRmOp)
                                                 && iReg == pThis->aOperands[pThis->idxMrmRmOp].iReg;
                else
                {
                    Assert(!pThis->fUsesVexIndexRegs || pThis->aOperands[pThis->idxMrmRmOp].iMemIndexReg == UINT8_MAX);

                    pThis->fHasRegCollisionMemBase  = iReg == pThis->aOperands[pThis->idxMrmRmOp].iMemBaseReg;
                    pThis->fHasRegCollisionMemIndex = iReg == pThis->aOperands[pThis->idxMrmRmOp].iMemIndexReg;
                    pThis->fHasRegCollisionMem = pThis->fHasRegCollisionMemBase || pThis->fHasRegCollisionMemIndex;
                }
            }
        }
        else
        {
            /* Next register: ah, ch, dh and bh. */
            iReg++;
            pThis->aOperands[pThis->idxMrmRegOp].iReg = iReg;
            pThis->bModRm &= ~X86_MODRM_REG_MASK;
            pThis->bModRm |= (iReg & X86_MODRM_REG_SMASK) << X86_MODRM_REG_SHIFT;
            pThis->fRex    = false;
            pThis->fRexR   = false;
            pThis->fNoRexPrefixMrmReg       = true;
            pThis->fNoRexPrefix             = true;
            pThis->fHasHighByteRegInMrmReg  = true;
            pThis->fHasStackRegInMrmReg     = false;
            pThis->aOperands[pThis->idxMrmRegOp].fIsHighByteRegister = true;
            Assert(!pThis->fRexW); Assert(!pThis->fRexX); Assert(!pThis->fRexB);

            /* Check for collisions. */
            if (pThis->idxMrmRmOp < RT_ELEMENTS(pThis->aOperands))
            {
                if (!pThis->aOperands[pThis->idxMrmRmOp].fIsMem)
                    pThis->fHasRegCollisionDirect = CIDET_OF_K_IS_GPR(pThis->fMrmRmOp)
                                                 && (   (   pThis->aOperands[pThis->idxMrmRmOp].cb == 1
                                                         && iReg == pThis->aOperands[pThis->idxMrmRmOp].iReg
                                                         && pThis->fHasHighByteRegInMrmRm)
                                                     || (   pThis->aOperands[pThis->idxMrmRmOp].cb > 1
                                                         && iReg - 4 == pThis->aOperands[pThis->idxMrmRmOp].iReg));
                else
                {
                    Assert(!pThis->fUsesVexIndexRegs || pThis->aOperands[pThis->idxMrmRmOp].iMemIndexReg == UINT8_MAX);

                    pThis->fHasRegCollisionMemBase  = iReg - 4 == pThis->aOperands[pThis->idxMrmRmOp].iMemBaseReg;
                    pThis->fHasRegCollisionMemIndex = iReg - 4 == pThis->aOperands[pThis->idxMrmRmOp].iMemIndexReg;
                    pThis->fHasRegCollisionMem = pThis->fHasRegCollisionMemBase || pThis->fHasRegCollisionMemIndex;
                }
            }
        }
        return true;
    }

    Assert(!pThis->fRex || (iReg == 7 && CIDETMODE_IS_64BIT(pThis->bMode)));
    pThis->fRex = false;

    /*
     * Next register.
     */
    iReg = (iReg + 1) & (CIDETMODE_IS_64BIT(pThis->bMode) ? 15 : 7);

    pThis->aOperands[pThis->idxMrmRegOp].iReg = iReg;
    pThis->bModRm &= ~X86_MODRM_REG_MASK;
    pThis->bModRm |= (iReg & X86_MODRM_REG_SMASK) << X86_MODRM_REG_SHIFT;
    pThis->fRexR   = iReg >= 8;
    pThis->fHasStackRegInMrmReg = iReg == X86_GREG_xSP && CIDET_OF_K_IS_GPR(pThis->fMrmRegOp);

    /*
     * Register collision detection.
     */
    if (pThis->idxMrmRmOp < RT_ELEMENTS(pThis->aOperands))
    {
        if (!pThis->aOperands[pThis->idxMrmRmOp].fIsMem)
            pThis->fHasRegCollisionDirect = iReg == pThis->aOperands[pThis->idxMrmRmOp].iReg
                                         && CIDET_OF_K_IS_SAME(pThis->fMrmRmOp, pThis->fMrmRegOp);
        else if (CIDET_OF_K_IS_GPR(pThis->fMrmRegOp))
        {
            Assert(!pThis->fUsesVexIndexRegs || pThis->aOperands[pThis->idxMrmRmOp].iMemIndexReg == UINT8_MAX);
            pThis->fHasRegCollisionMemBase  = iReg == pThis->aOperands[pThis->idxMrmRmOp].iMemBaseReg;
            pThis->fHasRegCollisionMemIndex = iReg == pThis->aOperands[pThis->idxMrmRmOp].iMemIndexReg;
            pThis->fHasRegCollisionMem = pThis->fHasRegCollisionMemBase || pThis->fHasRegCollisionMemIndex;
        }
    }
    Assert(!pThis->fSib);

    return iReg != 0;
}


/**
 * Selects the next MOD & R/M encoding, 16-bit addressing variant.
 *
 * @param   pThis               The core state structure.
 * @param   iReg                The value of MODRM.REG /w REX.R applied.
 */
static void cidetCoreSetupFirstBaseEncoding_MrmRmMod_16bit(PCIDETCORE pThis, uint8_t iReg)
{
    if (CidetInstrHasMrmRegOperand(pThis->pCurInstr))
    {
        pThis->aOperands[pThis->idxMrmRmOp].iReg            = 0;
        pThis->aOperands[pThis->idxMrmRmOp].fIsMem          = false;
        pThis->aOperands[pThis->idxMrmRmOp].fIsRipRelative  = false;
        pThis->aOperands[pThis->idxMrmRmOp].fIsHighByteRegister = false;
        pThis->aOperands[pThis->idxMrmRmOp].cbMemDisp       = 0;
        pThis->aOperands[pThis->idxMrmRmOp].iMemBaseReg     = UINT8_MAX;
        pThis->aOperands[pThis->idxMrmRmOp].iMemIndexReg    = UINT8_MAX;
        pThis->aOperands[pThis->idxMrmRmOp].uMemScale       = 1;
        pThis->aOperands[pThis->idxMrmRmOp].iEffSeg         = UINT8_MAX;
        pThis->bModRm                     &= ~(X86_MODRM_RM_MASK | X86_MODRM_MOD_MASK);
        pThis->bModRm                     |= 3 << X86_MODRM_MOD_SHIFT;
        pThis->fRexB                       = false;
        pThis->fRexX                       = false;
        pThis->fHasMemoryOperand           = false;
        pThis->fHasRegCollisionDirect      = iReg == 0
                                          && CIDET_OF_K_IS_SAME(pThis->fMrmRmOp, pThis->fMrmRegOp);
        pThis->fHasRegCollisionMem         = false;
        pThis->fHasRegCollisionMemBase     = false;
        pThis->fHasRegCollisionMemIndex    = false;
        pThis->fHasStackRegInMrmRmBase     = false;
    }
    else
    {
        Assert(CidetInstrHasMrmMemOperand(pThis->pCurInstr));
        pThis->aOperands[pThis->idxMrmRmOp].iReg            = UINT8_MAX;
        pThis->aOperands[pThis->idxMrmRmOp].fIsMem          = true;
        pThis->aOperands[pThis->idxMrmRmOp].fIsRipRelative  = false;
        pThis->aOperands[pThis->idxMrmRmOp].fIsHighByteRegister = false;
        pThis->aOperands[pThis->idxMrmRmOp].cbMemDisp       = 0;
        pThis->aOperands[pThis->idxMrmRmOp].iMemBaseReg     = X86_GREG_xBX;
        pThis->aOperands[pThis->idxMrmRmOp].iMemIndexReg    = X86_GREG_xSI;
        pThis->aOperands[pThis->idxMrmRmOp].uMemScale       = 1;
        pThis->aOperands[pThis->idxMrmRmOp].iEffSeg         = UINT8_MAX;
        pThis->bModRm                     &= ~(X86_MODRM_RM_MASK | X86_MODRM_MOD_MASK);
        pThis->fRexB                       = false;
        pThis->fRexX                       = false;
        pThis->fHasMemoryOperand           = true;
        pThis->fHasRegCollisionDirect      = false;
        iReg -= pThis->fHasHighByteRegInMrmReg * 4;
        pThis->fHasRegCollisionMemBase     = iReg == X86_GREG_xBX && CIDET_OF_K_IS_GPR(pThis->fMrmRegOp);
        pThis->fHasRegCollisionMemIndex    = iReg == X86_GREG_xSI && CIDET_OF_K_IS_GPR(pThis->fMrmRegOp);
        pThis->fHasRegCollisionMem         = pThis->fHasRegCollisionMemBase || pThis->fHasRegCollisionMemIndex;
        pThis->fHasStackRegInMrmRmBase     = false;
    }
}


/**
 * Selects the next MOD & R/M encoding, 16-bit addressing variant.
 *
 * @returns @c true if done, @c false if the next wheel needs to be moved.
 * @param   pThis               The core state structure.
 * @param   iReg                The value of MODRM.REG /w REX.R applied.
 */
static bool cidetCoreSetupNextBaseEncoding_MrmRmMod_16bit(PCIDETCORE pThis, uint8_t iReg)
{
    AssertRelease(!pThis->fRexB);
    AssertRelease(!pThis->fRexX);
    uint8_t iRm  = pThis->bModRm & X86_MODRM_RM_MASK;
    uint8_t iMod = (pThis->bModRm >> X86_MODRM_MOD_SHIFT) & X86_MODRM_MOD_SMASK;
    if (iMod == 3)
    {
        /*
         * Register access mode.
         */
        Assert(pThis->idxMrmRmOp < RT_ELEMENTS(pThis->aOperands) && !pThis->aOperands[pThis->idxMrmRmOp].fIsMem);
        Assert(!pThis->fHasMemoryOperand);
        Assert(!pThis->fHasRegCollisionMem);
        Assert(!pThis->fHasRegCollisionMemBase);
        Assert(!pThis->fHasRegCollisionMemIndex);
        if (iRm < 7)
        {
            iRm++;
            pThis->aOperands[pThis->idxMrmRmOp].iReg = iRm;
            pThis->bModRm &= ~X86_MODRM_RM_MASK;
            pThis->bModRm |= iRm;
            pThis->fHasRegCollisionDirect = iRm == iReg
                                        && CIDET_OF_K_IS_SAME(pThis->fMrmRmOp, pThis->fMrmRegOp);
            pThis->fHasStackRegInMrmRmBase = iRm == X86_GREG_xSP && CIDET_OF_K_IS_GPR(pThis->fMrmRmOp);
            return true;
        }

        /* If no memory modes, we're done.  */
        if (!CidetInstrHasMrmMemOperand(pThis->pCurInstr))
        {
            cidetCoreSetupFirstBaseEncoding_MrmRmMod_16bit(pThis, iReg);
            return false;
        }

        /* Next mode: 16-bit memory addressing without displacement. */
        pThis->aOperands[pThis->idxMrmRmOp].fIsMem = true;
        pThis->aOperands[pThis->idxMrmRmOp].cbMemDisp = 0;
        iMod = 0;
    }
    else
    {
        /*
         * Memory access mode.
         */
        Assert(pThis->idxMrmRmOp < RT_ELEMENTS(pThis->aOperands) && pThis->aOperands[pThis->idxMrmRmOp].fIsMem);
        Assert(pThis->fHasMemoryOperand);
        if (iRm < 7)
        {
            iRm++;
            switch (iRm)
            {
                case 1:
                    pThis->aOperands[pThis->idxMrmRmOp].iMemBaseReg  = X86_GREG_xBX;
                    pThis->aOperands[pThis->idxMrmRmOp].iMemIndexReg = X86_GREG_xDI;
                    break;
                case 2:
                    pThis->aOperands[pThis->idxMrmRmOp].iMemBaseReg  = X86_GREG_xBP;
                    pThis->aOperands[pThis->idxMrmRmOp].iMemIndexReg = X86_GREG_xSI;
                    break;
                case 3:
                    pThis->aOperands[pThis->idxMrmRmOp].iMemBaseReg  = X86_GREG_xBP;
                    pThis->aOperands[pThis->idxMrmRmOp].iMemIndexReg = X86_GREG_xDI;
                    break;
                case 4:
                    pThis->aOperands[pThis->idxMrmRmOp].iMemBaseReg  = UINT8_MAX;
                    pThis->aOperands[pThis->idxMrmRmOp].iMemIndexReg = X86_GREG_xSI;
                    break;
                case 5:
                    pThis->aOperands[pThis->idxMrmRmOp].iMemBaseReg  = UINT8_MAX;
                    pThis->aOperands[pThis->idxMrmRmOp].iMemIndexReg = X86_GREG_xDI;
                    break;
                case 6:
                    if (iMod == 0)
                    {
                        pThis->aOperands[pThis->idxMrmRmOp].cbMemDisp = 2;
                        pThis->aOperands[pThis->idxMrmRmOp].iMemBaseReg = UINT8_MAX;
                    }
                    else
                        pThis->aOperands[pThis->idxMrmRmOp].iMemBaseReg = X86_GREG_xBP;
                    pThis->aOperands[pThis->idxMrmRmOp].iMemIndexReg = UINT8_MAX;
                    break;
                case 7:
                    if (iMod == 0)
                        pThis->aOperands[pThis->idxMrmRmOp].cbMemDisp = 0;
                    pThis->aOperands[pThis->idxMrmRmOp].iMemBaseReg  = X86_GREG_xBX;
                    pThis->aOperands[pThis->idxMrmRmOp].iMemIndexReg = UINT8_MAX;
                    break;
                default: AssertReleaseFailed();
            }
            pThis->bModRm &= ~X86_MODRM_RM_MASK;
            pThis->bModRm |= iRm;
            if (CIDET_OF_K_IS_GPR(pThis->fMrmRegOp))
            {
                iReg -= pThis->fHasHighByteRegInMrmReg * 4;
                pThis->fHasRegCollisionMemBase = iReg == pThis->aOperands[pThis->idxMrmRmOp].iMemBaseReg;
                pThis->fHasRegCollisionMemIndex = iReg == pThis->aOperands[pThis->idxMrmRmOp].iMemIndexReg;
                pThis->fHasRegCollisionMem = pThis->fHasRegCollisionMemBase || pThis->fHasRegCollisionMemIndex;
            }
            return true;
        }

        /* Last mode? */
        if (iMod >= 2)
        {
            cidetCoreSetupFirstBaseEncoding_MrmRmMod_16bit(pThis, iReg);
            return false;
        }

        /* Next memory addressing mode (if any). */
        iMod++;
        pThis->aOperands[pThis->idxMrmRmOp].cbMemDisp++;
    }
    pThis->aOperands[pThis->idxMrmRmOp].iMemBaseReg  = X86_GREG_xBX;
    pThis->aOperands[pThis->idxMrmRmOp].iMemIndexReg = X86_GREG_xSI;
    pThis->aOperands[pThis->idxMrmRmOp].uMemScale    = 1;
    pThis->bModRm &= ~(X86_MODRM_RM_MASK | X86_MODRM_MOD_MASK);
    pThis->bModRm |= iMod << X86_MODRM_MOD_SHIFT;
    pThis->fHasMemoryOperand = true;
    pThis->fHasRegCollisionDirect = false;
    pThis->fHasStackRegInMrmRmBase = false;
    if (CIDET_OF_K_IS_GPR(pThis->fMrmRmOp))
    {
        iReg -= pThis->fHasHighByteRegInMrmReg * 4;
        pThis->fHasRegCollisionMemBase  = iReg == X86_GREG_xBX;
        pThis->fHasRegCollisionMemIndex = iReg == X86_GREG_xSI;
        pThis->fHasRegCollisionMem = pThis->fHasRegCollisionMemBase || pThis->fHasRegCollisionMemIndex;
    }
    return true;
}


/**
 * Selects the first MOD & R/M encoding, 32-bit and 64-bit addressing variant.
 *
 * @param   pThis               The core state structure.
 * @param   iReg                The value of MODRM.REG /w REX.R applied.
 * @param   f64Bit              Set if 64-bit, clear if 32-bit.
 */
static void cidetCoreSetupFirstBaseEncoding_MrmRmMod_32bit64bit(PCIDETCORE pThis, uint8_t iReg, bool f64Bit)
{
    RT_NOREF_PV(f64Bit);
    if (CidetInstrHasMrmRegOperand(pThis->pCurInstr))
    {
        pThis->aOperands[pThis->idxMrmRmOp].iReg            = 0;
        pThis->aOperands[pThis->idxMrmRmOp].fIsMem          = false;
        pThis->aOperands[pThis->idxMrmRmOp].fIsRipRelative  = false;
        pThis->aOperands[pThis->idxMrmRmOp].fIsHighByteRegister = false;
        pThis->aOperands[pThis->idxMrmRmOp].cbMemDisp       = 0;
        pThis->aOperands[pThis->idxMrmRmOp].iMemBaseReg     = UINT8_MAX;
        pThis->aOperands[pThis->idxMrmRmOp].iMemIndexReg    = UINT8_MAX;
        pThis->aOperands[pThis->idxMrmRmOp].uMemScale       = 1;
        pThis->aOperands[pThis->idxMrmRmOp].iEffSeg         = UINT8_MAX;
        pThis->bModRm                     &= ~(X86_MODRM_RM_MASK | X86_MODRM_MOD_MASK);
        pThis->bModRm                     |= 3 << X86_MODRM_MOD_SHIFT;
        pThis->fRexB                       = false;
        pThis->fRexX                       = false;
        pThis->fHasMemoryOperand           = false;
        pThis->fHasRegCollisionDirect      = iReg == 0
                                          && CIDET_OF_K_IS_SAME(pThis->fMrmRmOp, pThis->fMrmRegOp);
        pThis->fHasRegCollisionMem         = false;
        pThis->fHasRegCollisionMemBase     = false;
        pThis->fHasRegCollisionMemIndex    = false;
        pThis->fHasStackRegInMrmRmBase     = false;
    }
    else
    {
        Assert(CidetInstrHasMrmMemOperand(pThis->pCurInstr));
        pThis->aOperands[pThis->idxMrmRmOp].iReg            = UINT8_MAX;
        pThis->aOperands[pThis->idxMrmRmOp].fIsMem          = true;
        pThis->aOperands[pThis->idxMrmRmOp].fIsRipRelative  = false;
        pThis->aOperands[pThis->idxMrmRmOp].fIsHighByteRegister = false;
        pThis->aOperands[pThis->idxMrmRmOp].cbMemDisp       = 0;
        pThis->aOperands[pThis->idxMrmRmOp].iMemBaseReg     = 0;
        pThis->aOperands[pThis->idxMrmRmOp].iMemIndexReg    = UINT8_MAX;
        pThis->aOperands[pThis->idxMrmRmOp].uMemScale       = 1;
        pThis->aOperands[pThis->idxMrmRmOp].iEffSeg         = UINT8_MAX;
        pThis->bModRm                     &= ~(X86_MODRM_RM_MASK | X86_MODRM_MOD_MASK);
        pThis->fRexB                       = false;
        pThis->fRexX                       = false;
        pThis->fHasMemoryOperand           = true;
        pThis->fHasRegCollisionDirect      = false;
        pThis->fHasRegCollisionMemIndex    = false;
        pThis->fHasRegCollisionMemBase     = iReg == pThis->fHasHighByteRegInMrmReg * 4 && CIDET_OF_K_IS_GPR(pThis->fMrmRegOp);
        pThis->fHasRegCollisionMem         = pThis->fHasRegCollisionMemBase;
        pThis->fHasStackRegInMrmRmBase     = false;
    }
}


/**
 * Selects the next MOD & R/M encoding, 32-bit and 64-bit addressing variant.
 *
 * @returns @c true if done, @c false if the next wheel needs to be moved.
 * @param   pThis               The core state structure.
 * @param   iReg                The value of MODRM.REG /w REX.R applied.
 * @param   f64Bit              Set if 64-bit, clear if 32-bit.
 */
static bool cidetCoreSetupNextBaseEncoding_MrmRmMod_32bit64bit(PCIDETCORE pThis, uint8_t iReg, bool f64Bit)
{
    AssertRelease(!pThis->fRexX || CIDETMODE_IS_64BIT(pThis->bMode));
    AssertRelease(!pThis->fRexB || CIDETMODE_IS_64BIT(pThis->bMode));
    uint8_t iRm  = (pThis->bModRm & X86_MODRM_RM_MASK) + pThis->fRexB * 8;
    uint8_t iMod = (pThis->bModRm >> X86_MODRM_MOD_SHIFT) & X86_MODRM_MOD_SMASK;
    if (iMod == 3)
    {
        /*
         * Register access mode.
         */
        Assert(pThis->idxMrmRmOp < RT_ELEMENTS(pThis->aOperands) && !pThis->aOperands[pThis->idxMrmRmOp].fIsMem);
        Assert(!pThis->fHasMemoryOperand);
        Assert(!pThis->fHasRegCollisionMem);
        Assert(!pThis->fHasRegCollisionMemBase);
        Assert(!pThis->fHasRegCollisionMemIndex);

        if (CIDETMODE_IS_64BIT(pThis->bMode) && !pThis->fRexX && !pThis->fNoRexPrefix) /* should be ignored. */
        {
            pThis->fRexX = true;
            return true;
        }

        /* Reset the byte register kludges variables. */
        pThis->aOperands[pThis->idxMrmRmOp].fIsHighByteRegister = false;
        pThis->fHasHighByteRegInMrmRm = false;
        pThis->fNoRexPrefixMrmRm = false;
        pThis->fNoRexPrefix = pThis->fNoRexPrefixMrmReg;

        if (iRm < (CIDETMODE_IS_64BIT(pThis->bMode) && !pThis->fNoRexPrefix ? 15 : 7))
        {
            /*
             * Byte register kludge.
             */
            if (   pThis->aOperands[pThis->idxMrmRmOp].cb == 1
                && CIDET_OF_K_IS_GPR(pThis->fMrmRegOp)
                && iRm >= 3
                && (   iRm <= 6
                    || (iRm == 7 && CIDETMODE_IS_64BIT(pThis->bMode) && !pThis->fRexX) ) )
            {
                if (!pThis->fRexX && iRm >= 4 && CIDETMODE_IS_64BIT(pThis->bMode) && !pThis->fNoRexPrefix)
                {
                    /* The AMD64 low variants: spl, bpl, sil and dil. (Using fRexX here as REG covers fRex.) */
                    pThis->fRexX = true;
                    pThis->fHasRegCollisionDirect = CIDET_OF_K_IS_GPR(pThis->fMrmRegOp)
                                                 && iRm == iReg - pThis->fHasHighByteRegInMrmReg * 4;
                    pThis->fHasStackRegInMrmRmBase = iRm == X86_GREG_xSP && CIDET_OF_K_IS_GPR(pThis->fMrmRegOp);
                }
                else
                {
                    /* Next register: ah, ch, dh and bh. */
                    iRm++;
                    pThis->aOperands[pThis->idxMrmRmOp].iReg = iRm;
                    pThis->bModRm &= ~X86_MODRM_RM_MASK;
                    pThis->bModRm |= iRm & X86_MODRM_RM_MASK;
                    pThis->fRexB   = false;
                    pThis->fRexX   = false;
                    if (!pThis->fRexR && !pThis->fRexW && !pThis->fRex)
                    {
                        pThis->fNoRexPrefixMrmRm = true;
                        pThis->fNoRexPrefix = true;
                        pThis->fHasHighByteRegInMrmRm = true;
                        pThis->aOperands[pThis->idxMrmRmOp].fIsHighByteRegister = true;
                        pThis->fHasRegCollisionDirect = CIDET_OF_K_IS_GPR(pThis->fMrmRegOp)
                                                     && iRm - 4 == iReg - pThis->fHasHighByteRegInMrmReg * 4;
                        pThis->fHasStackRegInMrmRmBase = false;

                    }
                    else
                    {
                        /* Can't do the high stuff, so do the spl, bpl, sil and dil variation instead.
                           Note! We don't set the RexX yet since the base register or operand width holds it down. */
                        pThis->fHasRegCollisionDirect = CIDET_OF_K_IS_GPR(pThis->fMrmRegOp)
                                                     && iRm == iReg - pThis->fHasHighByteRegInMrmReg * 4;
                        pThis->fHasStackRegInMrmRmBase = iRm == X86_GREG_xSP && CIDET_OF_K_IS_GPR(pThis->fMrmRegOp);
                    }
                }
            }
            /*
             * Normal register.
             */
            else
            {
                iRm++;
                pThis->aOperands[pThis->idxMrmRmOp].iReg = iRm;
                pThis->bModRm &= ~X86_MODRM_RM_MASK;
                pThis->bModRm |= iRm & X86_MODRM_RM_MASK;
                pThis->fRexB   = iRm >= 8;
                pThis->fRexX   = false;
                pThis->fHasRegCollisionDirect = iRm == iReg && CIDET_OF_K_IS_SAME(pThis->fMrmRmOp, pThis->fMrmRegOp);
                pThis->fHasStackRegInMrmRmBase = iRm == X86_GREG_xSP && CIDET_OF_K_IS_GPR(pThis->fMrmRegOp);
            }
            return true;
        }

        /* If no memory modes, we're done.  */
        if (!CidetInstrHasMrmMemOperand(pThis->pCurInstr))
        {
            cidetCoreSetupFirstBaseEncoding_MrmRmMod_32bit64bit(pThis, iReg, f64Bit);
            return false;
        }

        /* Next mode: 32-bit/64-bit memory addressing without displacement. */
        pThis->aOperands[pThis->idxMrmRmOp].fIsMem = true;
        pThis->aOperands[pThis->idxMrmRmOp].cbMemDisp = 0;
        iMod = 0;
    }
    else
    {
        /*
         * Memory access mode.
         */
        Assert(pThis->idxMrmRmOp < RT_ELEMENTS(pThis->aOperands) && pThis->aOperands[pThis->idxMrmRmOp].fIsMem);
        Assert(pThis->fHasMemoryOperand);
        Assert(!pThis->fHasStackRegInMrmRmBase);
        if (iRm < (CIDETMODE_IS_64BIT(pThis->bMode) && !pThis->fNoRexPrefix ? 15 : 7))
        {
            iRm++;
            if (iRm == 12)
                iRm++; /* Leave REX.B=1 to the next-sib-base function. */
            if (iRm == 4)
            {
                /* SIB */
                pThis->aOperands[pThis->idxMrmRmOp].iMemBaseReg     = 0;
                pThis->aOperands[pThis->idxMrmRmOp].iMemIndexReg    = 0;
                pThis->fSib = true;
                pThis->bSib = 0;
            }
            else if ((iRm & 7) == 5 && iMod == 0)
            {
                /* Absolute or wrt rip addressing. */
                pThis->aOperands[pThis->idxMrmRmOp].fIsRipRelative  = CIDETMODE_IS_64BIT(pThis->bMode);
                pThis->aOperands[pThis->idxMrmRmOp].iMemBaseReg     = UINT8_MAX;
                pThis->aOperands[pThis->idxMrmRmOp].iMemIndexReg    = UINT8_MAX;
                pThis->aOperands[pThis->idxMrmRmOp].cbMemDisp       = 4;
            }
            else
            {
                if ((iRm & 7) == 6 && iMod == 0)
                {
                    pThis->aOperands[pThis->idxMrmRmOp].cbMemDisp   = 0;
                    pThis->aOperands[pThis->idxMrmRmOp].fIsRipRelative = false;
                }
                pThis->aOperands[pThis->idxMrmRmOp].iMemBaseReg     = iRm;
                pThis->aOperands[pThis->idxMrmRmOp].iMemIndexReg    = UINT8_MAX;
            }
            pThis->aOperands[pThis->idxMrmRmOp].uMemScale = 1;
            pThis->bModRm &= ~X86_MODRM_RM_MASK;
            pThis->bModRm |= iRm & X86_MODRM_RM_MASK;
            pThis->fRexB   = iRm >= 8;
            pThis->fRexX   = false;
            if (CIDET_OF_K_IS_GPR(pThis->fMrmRegOp))
            {
                iReg -= pThis->fHasHighByteRegInMrmReg * 4;
                pThis->fHasRegCollisionMemBase = iReg == pThis->aOperands[pThis->idxMrmRmOp].iMemBaseReg;
                pThis->fHasRegCollisionMemIndex = iReg == pThis->aOperands[pThis->idxMrmRmOp].iMemIndexReg;
                pThis->fHasRegCollisionMem = pThis->fHasRegCollisionMemBase || pThis->fHasRegCollisionMemIndex;
            }
            return true;
        }

        /* Last mode? */
        if (iMod >= 2)
        {
            cidetCoreSetupFirstBaseEncoding_MrmRmMod_32bit64bit(pThis, iReg, f64Bit);
            return false;
        }

        /* Next memory addressing mode (if any). */
        iMod++;
        pThis->aOperands[pThis->idxMrmRmOp].cbMemDisp = iMod == 1 ? 1 : 4;
    }
    pThis->aOperands[pThis->idxMrmRmOp].iMemBaseReg  = 0;
    pThis->aOperands[pThis->idxMrmRmOp].iMemIndexReg = UINT8_MAX;
    pThis->aOperands[pThis->idxMrmRmOp].uMemScale    = 1;
    pThis->bModRm &= ~(X86_MODRM_RM_MASK | X86_MODRM_MOD_MASK);
    pThis->bModRm |= iMod << X86_MODRM_MOD_SHIFT;
    pThis->fRexB   = false;
    pThis->fRexX   = false;
    pThis->fHasMemoryOperand = true;
    pThis->fHasRegCollisionDirect = false;
    pThis->fHasRegCollisionMemIndex = false;
    pThis->fHasRegCollisionMemBase = iReg == pThis->fHasHighByteRegInMrmReg * 4
                                  && CIDET_OF_K_IS_GPR(pThis->fMrmRmOp);
    pThis->fHasRegCollisionMem = pThis->fHasRegCollisionMemBase;
    pThis->fHasStackRegInMrmRmBase = false;
    return true;
}


/**
 * Selects the next MOD & R/M encoding.
 *
 * @returns @c true if done, @c false if the next wheel needs to be moved.
 * @param   pThis               The core state structure.
 * @param   iReg                The value of MODRM.REG /w REX.R applied.
 */
static bool cidetCoreSetupNextBaseEncoding_MrmRmMod(PCIDETCORE pThis, uint8_t iReg)
{
    if (pThis->cbAddrMode == 2)
        return cidetCoreSetupNextBaseEncoding_MrmRmMod_16bit(pThis, iReg);
    if (pThis->cbAddrMode == 4)
        return cidetCoreSetupNextBaseEncoding_MrmRmMod_32bit64bit(pThis, iReg, false);
    if (pThis->cbAddrMode == 8)
        return cidetCoreSetupNextBaseEncoding_MrmRmMod_32bit64bit(pThis, iReg, true);
    AssertReleaseFailedReturn(false);
}



/**
 * Selects the next SIB base register (/ encoding).
 *
 * @returns @c true if done, @c false if the next wheel needs to be moved.
 * @param   pThis               The core state structure.
 * @param   iReg                The value of MODRM.REG /w REX.R applied.
 */
static bool cidetCoreSetupNextBaseEncoding_SibBase(PCIDETCORE pThis, uint8_t iReg)
{
    AssertRelease(!pThis->fRexB || CIDETMODE_IS_64BIT(pThis->bMode));

    uint8_t iBase = (pThis->bSib & X86_SIB_BASE_MASK) + pThis->fRexB * 8;
    iBase = (iBase + 1) & (CIDETMODE_IS_64BIT(pThis->bMode) && !pThis->fNoRexPrefix ? 15 : 7);

    if ((iBase & 7) == 5 && (pThis->bModRm & X86_MODRM_MOD_MASK) == 0)
    {
        pThis->aOperands[pThis->idxMrmRmOp].cbMemDisp   = 4;
        pThis->aOperands[pThis->idxMrmRmOp].iMemBaseReg = UINT8_MAX;
    }
    else
    {
        if ((iBase & 7) == 6 && (pThis->bModRm & X86_MODRM_MOD_MASK) == 0)
            pThis->aOperands[pThis->idxMrmRmOp].cbMemDisp = 0;
        pThis->aOperands[pThis->idxMrmRmOp].iMemBaseReg = iBase;
    }
    pThis->bSib  &= ~X86_SIB_BASE_MASK;
    pThis->bSib  |= iBase & X86_SIB_BASE_MASK;
    pThis->fRexB  = iBase >= 8;
    pThis->fHasRegCollisionMemBase =    pThis->aOperands[pThis->idxMrmRmOp].iMemBaseReg
                                     == iReg - pThis->fHasHighByteRegInMrmReg * 4
                                  && CIDET_OF_K_IS_GPR(pThis->fMrmRegOp);
    pThis->fHasRegCollisionMem = pThis->fHasRegCollisionMemBase || pThis->fHasRegCollisionMemIndex;
    pThis->fHasStackRegInMrmRmBase = iBase == X86_GREG_xSP;

    return iBase != 0;
}


/**
 * Selects the next SIB index register (/ encoding).
 *
 * @returns @c true if done, @c false if the next wheel needs to be moved.
 * @param   pThis               The core state structure.
 * @param   iReg                The value of MODRM.REG /w REX.R applied.
 */
static bool cidetCoreSetupNextBaseEncoding_SibIndex(PCIDETCORE pThis, uint8_t iReg)
{
    AssertRelease(!pThis->fRexX || CIDETMODE_IS_64BIT(pThis->bMode));
    Assert(pThis->idxMrmRmOp < RT_ELEMENTS(pThis->aOperands) && pThis->aOperands[pThis->idxMrmRmOp].fIsMem);

    uint8_t iIndex = ((pThis->bSib >> X86_SIB_INDEX_SHIFT) & X86_SIB_INDEX_SMASK) + pThis->fRexX * 8;
    iIndex = (iIndex + 1) & (CIDETMODE_IS_64BIT(pThis->bMode) && !pThis->fNoRexPrefix ? 15 : 7);

    if (iIndex == 4 && !pThis->fUsesVexIndexRegs)
        pThis->aOperands[pThis->idxMrmRmOp].iMemIndexReg = UINT8_MAX;
    else
        pThis->aOperands[pThis->idxMrmRmOp].iMemIndexReg = iIndex;
    pThis->bSib &= ~X86_SIB_INDEX_MASK;
    pThis->bSib |= (iIndex & X86_SIB_INDEX_SMASK) << X86_SIB_INDEX_SHIFT;
    pThis->fRexX = iIndex >= 8;
    pThis->fHasRegCollisionMemIndex =     pThis->aOperands[pThis->idxMrmRmOp].iMemIndexReg
                                       == iReg - pThis->fHasHighByteRegInMrmReg * 4
                                   && (  !pThis->fUsesVexIndexRegs
                                       ? CIDET_OF_K_IS_GPR(pThis->fMrmRegOp) : CIDET_OF_K_IS_VRX(pThis->fMrmRegOp) );
    pThis->fHasRegCollisionMem = pThis->fHasRegCollisionMemBase || pThis->fHasRegCollisionMemIndex;

    return iIndex != 0;
}


/**
 * Selects the next SIB scale.
 *
 * @returns @c true if done, @c false if the next wheel needs to be moved.
 * @param   pThis               The core state structure.
 * @param   iReg                The value of MODRM.REG /w REX.R applied.
 */
static bool cidetCoreSetupNextBaseEncoding_SibScale(PCIDETCORE pThis, uint8_t iReg)
{
    RT_NOREF_PV(iReg);
    switch ((pThis->bSib >> X86_SIB_SCALE_SHIFT) & X86_SIB_SCALE_SMASK)
    {
        case 0:
            pThis->bSib |= 1 << X86_SIB_SCALE_SHIFT;
            pThis->aOperands[pThis->idxMrmRmOp].uMemScale = 2;
            return true;
        case 1:
            pThis->bSib &= ~X86_SIB_SCALE_MASK;
            pThis->bSib |= 2 << X86_SIB_SCALE_SHIFT;
            pThis->aOperands[pThis->idxMrmRmOp].uMemScale = 4;
            return true;
        case 2:
            pThis->bSib |= 3 << X86_SIB_SCALE_SHIFT;
            pThis->aOperands[pThis->idxMrmRmOp].uMemScale = 8;
            return true;
        case 3:
            pThis->bSib &= ~X86_SIB_SCALE_MASK;
            pThis->aOperands[pThis->idxMrmRmOp].uMemScale = 1;
            return false;

        default: AssertReleaseFailedReturn(false);
    }
}


/**
 * Selects the next segment prefix.
 *
 * @returns @c true if done, @c false if the next wheel needs to be moved.
 * @param   pThis               The core state structure.
 */
static bool cidetCoreSetupNextBaseEncoding_SegmentPrefix(PCIDETCORE pThis)
{
    if (   pThis->fHasMemoryOperand
        && (pThis->fTestCfg & CIDET_TESTCFG_SEG_PRF_MASK))
    {
        switch (pThis->uSegPrf)
        {
            case X86_SREG_COUNT:
                pThis->uSegPrf = X86_SREG_ES;
                if (pThis->fTestCfg & CIDET_TESTCFG_SEG_PRF_ES)
                    return true;
                RT_FALL_THRU();
            case X86_SREG_ES:
                pThis->uSegPrf = X86_SREG_CS;
                if (pThis->fTestCfg & CIDET_TESTCFG_SEG_PRF_CS)
                    return true;
                RT_FALL_THRU();
            case X86_SREG_CS:
                pThis->uSegPrf = X86_SREG_SS;
                if (pThis->fTestCfg & CIDET_TESTCFG_SEG_PRF_SS)
                    return true;
                RT_FALL_THRU();
            case X86_SREG_SS:
                pThis->uSegPrf = X86_SREG_DS;
                if (pThis->fTestCfg & CIDET_TESTCFG_SEG_PRF_DS)
                    return true;
                RT_FALL_THRU();
            case X86_SREG_DS:
                pThis->uSegPrf = X86_SREG_FS;
                if (pThis->fTestCfg & CIDET_TESTCFG_SEG_PRF_FS)
                    return true;
                RT_FALL_THRU();
            case X86_SREG_FS:
                pThis->uSegPrf = X86_SREG_GS;
                if (pThis->fTestCfg & CIDET_TESTCFG_SEG_PRF_GS)
                    return true;
                RT_FALL_THRU();
            case X86_SREG_GS:
                break;
            default: AssertReleaseFailedBreak();
        }
        pThis->uSegPrf = X86_SREG_COUNT;
    }
    return false;
}


/**
 * Updates the variable sized operands.
 *
 * @param   pThis               The core state structure.
 */
static void cidetCoreUpdateOperandSizes(PCIDETCORE pThis)
{
    uint8_t iOp = pThis->cOperands;
    while (iOp-- > 0)
        pThis->aOperands[iOp].cb = (uint8_t)CidetCoreGetOperandSize(pThis, iOp);
}


/**
 * Selects the next operand size.
 *
 * @returns @c true if done, @c false if the next wheel needs to be moved.
 * @param   pThis               The core state structure.
 */
static bool cidetCoreSetupNextBaseEncoding_OperandSize(PCIDETCORE pThis)
{
    if (CidetInstrRespondsToOperandSizePrefixes(pThis->pCurInstr))
    {
        if (CIDETMODE_IS_64BIT(pThis->bMode))
        {
            switch (pThis->fOpSizePrf + pThis->fRexW * 2)
            {
                case 0:
                    pThis->fOpSizePrf = true;
                    cidetCoreUpdateOperandSizes(pThis);
                    return true;
                case 1:
                    pThis->fOpSizePrf = false;
                    if (pThis->fNoRexPrefix)
                        break;
                    pThis->fRexW = true;
                    cidetCoreUpdateOperandSizes(pThis);
                    return true;
                case 2:
                    pThis->fOpSizePrf = true; /* check that it's ignored. */
                    cidetCoreUpdateOperandSizes(pThis);
                    return true;
                default:
                    AssertReleaseFailedBreak();
                case 3:
                    break;
            }
        }
        else
        {
            if (!pThis->fOpSizePrf)
            {
                pThis->fOpSizePrf = true;
                cidetCoreUpdateOperandSizes(pThis);
                return true;
            }
        }
        pThis->fRexW = false;
        pThis->fOpSizePrf = false;
        cidetCoreUpdateOperandSizes(pThis);
    }
    return false;
}


static bool CidetCoreSetupNextBaseEncoding(PCIDETCORE pThis)
{
    if (pThis->fUsesModRm)
    {
        /*
         * The wheels are lined up as follows:
         *      1. Address size prefix.
         *      2. MODRM.MOD
         *      3. MODRM.REG + REX.R
         *      4. MODRM.R/M + REX.B
         *      5. SIB - MODRM.R/M == 4 && MODRM.MOD != 3:
         *          5a) SIB.BASE + REX.B
         *          5b) SIB.INDEX + REX.X
         *          5c) SIB.SCALE
         *      6. Segment prefix overrides if applicable and supported (memory).
         *      7. Operand size prefix and REX.W if applicable.
         */
        if (cidetCoreSetupNextBaseEncoding_OperandSize(pThis))
            return true;
        if (cidetCoreSetupNextBaseEncoding_SegmentPrefix(pThis))
            return true;

        /* The ModR/M register value for collision detection. */
        uint8_t iReg = ((pThis->bModRm >> X86_MODRM_REG_SHIFT) & X86_MODRM_REG_SMASK) + pThis->fRexR * 8;

        if (pThis->fSib)
        {
            AssertRelease(pThis->fHasMemoryOperand);
            if (cidetCoreSetupNextBaseEncoding_SibScale(pThis, iReg))
                return true;
            if (cidetCoreSetupNextBaseEncoding_SibIndex(pThis, iReg))
                return true;
            if (cidetCoreSetupNextBaseEncoding_SibBase(pThis, iReg))
                return true;
            Assert(pThis->bSib == 0);
            pThis->fSib = false;
        }

        if (cidetCoreSetupNextBaseEncoding_MrmRmMod(pThis, iReg))
            return true;
        if (cidetCoreSetupNextBaseEncoding_MrmReg(pThis, iReg))
            return true;
        if (cidetCoreSetupNextBaseEncoding_AddressSize(pThis))
            return true;
    }
    else
        AssertFailedReturn(false);
    return false;
}


static bool CidetCoreSetupFirstBaseEncoding(PCIDETCORE pThis)
{
    /*
     * Reset all the knobs and wheels.
     */
    pThis->fSib         = false;
    pThis->uSegPrf      = X86_SREG_COUNT;
    pThis->fAddrSizePrf = false;
    pThis->fOpSizePrf   = false;
    pThis->fRexW        = false;
    pThis->fRexR        = false;
    pThis->fRexX        = false;
    pThis->fRexB        = false;
    pThis->fRex         = false;
    pThis->bModRm       = 0;
    pThis->bSib         = 0;

    /* Indicators. */
    pThis->cbAddrMode               = CIDETMODE_GET_BYTE_COUNT(pThis->bMode);
    pThis->fHasMemoryOperand        = false;
    pThis->fHasRegCollisionMem      = false;
    pThis->fHasRegCollisionMemBase  = false;
    pThis->fHasRegCollisionMemIndex = false;
    pThis->fHasStackRegInMrmRmBase  = false;

    /*
     * Now, drill down on the instruction encoding.
     */
    if (pThis->pCurInstr->fFlags & CIDET_IF_MODRM)
    {
        Assert(pThis->fUsesModRm == true);
        cidetCoreSetupFirstBaseEncoding_MrmReg(pThis);
        if (pThis->cbAddrMode == 2)
            cidetCoreSetupFirstBaseEncoding_MrmRmMod_16bit(pThis, 0);
        else if (pThis->cbAddrMode == 4)
            cidetCoreSetupFirstBaseEncoding_MrmRmMod_32bit64bit(pThis, 0, false);
        else if (pThis->cbAddrMode == 8)
            cidetCoreSetupFirstBaseEncoding_MrmRmMod_32bit64bit(pThis, 0, true);
        else
            AssertReleaseFailedReturn(false);
    }
    else
        AssertFailedReturn(false);
    return true;
}


/**
 * The next memory operand configuration.
 *
 * @returns true if new one to test, false if we've reached end already.
 * @param   pThis               The core state structure.
 */
static bool CidetCoreSetupNextMemoryOperandConfig(PCIDETCORE pThis)
{
    RT_NOREF_PV(pThis);
    return false;
}


/**
 * Sets up the first memory operand configuration and counts memory operands.
 *
 * @returns true on success, false if no data buffers configured or failure.
 * @param   pThis               The core state structure.
 */
static bool CidetCoreSetupFirstMemoryOperandConfig(PCIDETCORE pThis)
{
    pThis->cMemoryOperands = 0;
    PCIDETBUF pDataBuf = &pThis->DataBuf;
    uint8_t idxOp = pThis->cOperands;
    while (idxOp-- > 0)
        if (!pThis->aOperands[idxOp].fIsMem)
            pThis->aOperands[idxOp].pDataBuf = NULL;
        else
        {
            if (RT_UNLIKELY(!pThis->cDataBufConfigs))
                return false;

            pDataBuf->idxCfg       = 0;
            pDataBuf->pCfg         = &pThis->paDataBufConfigs[0];
            pDataBuf->off          = 0;
            pDataBuf->cb           = pThis->aOperands[idxOp].cb;
            pDataBuf->cbSegLimit   = UINT16_MAX;
            pDataBuf->offSegBase   = 0;
            pDataBuf->fActive      = false;
            pDataBuf->idxOp        = idxOp;
            pDataBuf->fXcptAfterInstruction = false;
            pDataBuf->enmExpectXcpt = kCidetExpectXcpt_None;
            pThis->aOperands[idxOp].pDataBuf = pDataBuf;
            pThis->cMemoryOperands++;
            pDataBuf++;
        }

    /** @todo implement more than one memory operand. */
    AssertReleaseReturn(pThis->cMemoryOperands <= 1, false);
    return true;
}


/**
 * The next code buffer configuration.
 *
 * @returns true if new one to test, false if we've reached end already.
 * @param   pThis               The core state structure.
 */
static bool CidetCoreSetupNextCodeBufferConfig(PCIDETCORE pThis)
{
    RT_NOREF_PV(pThis);
    return false;
}


/**
 * Sets up the first code buffer configuration.
 *
 * @returns true on success, false if no data buffers configured or failure.
 * @param   pThis               The core state structure.
 */
static bool CidetCoreSetupFirstCodeBufferConfig(PCIDETCORE pThis)
{
    Assert(pThis->cCodeBufConfigs > 0);
    Assert(CIDETBUF_IS_CODE(pThis->paCodeBufConfigs[0].fFlags));
    pThis->CodeBuf.idxCfg       = 0;
    pThis->CodeBuf.pCfg         = &pThis->paCodeBufConfigs[0];
    pThis->CodeBuf.off          = 0;
    pThis->CodeBuf.cb           = 0x1000;
    pThis->CodeBuf.cbSegLimit   = UINT16_MAX;
    pThis->CodeBuf.offSegBase   = 0;
    pThis->CodeBuf.fActive      = true;
    pThis->CodeBuf.idxOp        = 7;
    pThis->CodeBuf.fXcptAfterInstruction = false;
    pThis->CodeBuf.enmExpectXcpt = kCidetExpectXcpt_None;
    return true;
}


/**
 * Gets the (encoded) size of the given operand in the current context.
 *
 * @returns Size in bytes.
 * @param   pThis               The core state structure (for context).
 * @param   iOp                 The operand index.
 */
uint32_t CidetCoreGetOperandSize(PCIDETCORE pThis, uint8_t iOp)
{
    Assert(iOp < RT_ELEMENTS(pThis->aOperands));
    uint32_t cbOp = g_acbCidetOfSizes[(pThis->aOperands[iOp].fFlags & CIDET_OF_Z_MASK) >> CIDET_OF_Z_SHIFT];
    if (cbOp == UINT16_MAX)
    {
        Assert((pThis->aOperands[iOp].fFlags & CIDET_OF_Z_MASK) == CIDET_OF_Z_VAR_WDQ);
        if (CIDETMODE_IS_64BIT(pThis->bMode))
        {
            if (pThis->fRexW)
                cbOp = 8;
            else if (!pThis->fOpSizePrf)
                cbOp = 4;
            else
                cbOp = 2;
        }
        else if (CIDETMODE_IS_32BIT(pThis->bMode))
            cbOp = !pThis->fOpSizePrf ? 4 : 2;
        else
        {
            Assert(CIDETMODE_IS_16BIT(pThis->bMode));
            cbOp = !pThis->fOpSizePrf ? 2 : 4;
        }
        return cbOp;
    }

    if (cbOp == UINT16_MAX - 1)
    {
        Assert((pThis->aOperands[iOp].fFlags & CIDET_OF_Z_MASK) == CIDET_OF_Z_SPECIAL);
        AssertReleaseFailedReturn(0);
    }

    if (cbOp)
    {
#ifdef VBOX_STRICT
        switch (cbOp)
        {
            case  1: Assert((pThis->aOperands[iOp].fFlags & CIDET_OF_Z_MASK) == CIDET_OF_Z_BYTE); break;
            case  2: Assert((pThis->aOperands[iOp].fFlags & CIDET_OF_Z_MASK) == CIDET_OF_Z_WORD); break;
            case  4: Assert((pThis->aOperands[iOp].fFlags & CIDET_OF_Z_MASK) == CIDET_OF_Z_DWORD); break;
            case  8: Assert((pThis->aOperands[iOp].fFlags & CIDET_OF_Z_MASK) == CIDET_OF_Z_QWORD); break;
            case 10: Assert((pThis->aOperands[iOp].fFlags & CIDET_OF_Z_MASK) == CIDET_OF_Z_TBYTE); break;
            case 16: Assert((pThis->aOperands[iOp].fFlags & CIDET_OF_Z_MASK) == CIDET_OF_Z_OWORD); break;
            case 32: Assert((pThis->aOperands[iOp].fFlags & CIDET_OF_Z_MASK) == CIDET_OF_Z_YWORD); break;
            case 64: Assert((pThis->aOperands[iOp].fFlags & CIDET_OF_Z_MASK) == CIDET_OF_Z_ZWORD); break;
            default: AssertFailed();
        }
#endif
        return cbOp;
    }
    AssertReleaseFailedReturn(0);
}


static bool CideCoreSetInstruction(PCIDETCORE pThis, PCCIDETINSTR pInstr)
{
    AssertReleaseMsgReturn(RT_VALID_PTR(pInstr), ("%p\n", pInstr), false);

    pThis->pCurInstr = pInstr;

    /*
     * Extract info from the instruction descriptor.
     */
    pThis->fUsesModRm           = false;
    pThis->fUsesVexIndexRegs    = false;
    pThis->idxMrmRegOp          = 7;
    pThis->idxMrmRmOp           = 7;
    pThis->fMrmRegOp            = 0;
    pThis->fMrmRmOp             = 0;
    pThis->fInstrFlags          = pInstr->fFlags;
    pThis->cOperands            = pInstr->cOperands;
    if (pInstr->fFlags & CIDET_IF_MODRM)
    {
        pThis->fUsesModRm = true;
        for (uint8_t iOp = 0; iOp < pInstr->cOperands; iOp++)
            if (pInstr->afOperands[iOp] & CIDET_OF_M_REG)
            {
                pThis->idxMrmRegOp = iOp;
                pThis->fMrmRegOp   = pInstr->afOperands[iOp];
            }
            else if (pInstr->afOperands[iOp] & CIDET_OF_M_RM)
            {
                pThis->idxMrmRmOp = iOp;
                pThis->fMrmRmOp   = pInstr->afOperands[iOp];
            }
    }
    else
        AssertFailedReturn(false);

    uint8_t iOp;
    for (iOp = 0; iOp < pInstr->cOperands; iOp++)
    {
        pThis->aOperands[iOp].fFlags            = pInstr->afOperands[iOp];
        pThis->aOperands[iOp].iReg              = UINT8_MAX;
        pThis->aOperands[iOp].cb                = (uint8_t)CidetCoreGetOperandSize(pThis, iOp);
        pThis->aOperands[iOp].fIsImmediate      = (pInstr->afOperands[iOp] & CIDET_OF_K_MASK) == CIDET_OF_K_IMM;
        pThis->aOperands[iOp].fIsMem            = (pInstr->afOperands[iOp] & CIDET_OF_K_MASK) == CIDET_OF_K_MEM;
        pThis->aOperands[iOp].fIsRipRelative    = false;
        pThis->aOperands[iOp].cbMemDisp         = 0;
        pThis->aOperands[iOp].iMemBaseReg       = UINT8_MAX;
        pThis->aOperands[iOp].iMemIndexReg      = UINT8_MAX;
        pThis->aOperands[iOp].uMemScale         = 1;
        pThis->aOperands[iOp].iEffSeg           = UINT8_MAX;
        pThis->aOperands[iOp].offSeg            = UINT64_MAX;
        pThis->aOperands[iOp].uEffAddr          = UINT64_MAX;
        pThis->aOperands[iOp].uImmDispValue     = UINT64_MAX;
        pThis->aOperands[iOp].uMemBaseRegValue  = UINT64_MAX;
        pThis->aOperands[iOp].uMemIndexRegValue = UINT64_MAX;
        pThis->aOperands[iOp].In.pv             = NULL;
        pThis->aOperands[iOp].Expected.pv       = NULL;
        pThis->aOperands[iOp].pDataBuf          = NULL;
    }

    for (; iOp < RT_ELEMENTS(pThis->aOperands); iOp++)
    {
        pThis->aOperands[iOp].fFlags            = 0;
        pThis->aOperands[iOp].iReg              = UINT8_MAX;
        pThis->aOperands[iOp].cb                = 0;
        pThis->aOperands[iOp].fIsImmediate      = false;
        pThis->aOperands[iOp].fIsMem            = false;
        pThis->aOperands[iOp].fIsRipRelative    = false;
        pThis->aOperands[iOp].cbMemDisp         = 0;
        pThis->aOperands[iOp].iMemBaseReg       = UINT8_MAX;
        pThis->aOperands[iOp].iMemIndexReg      = UINT8_MAX;
        pThis->aOperands[iOp].uMemScale         = 1;
        pThis->aOperands[iOp].iEffSeg           = UINT8_MAX;
        pThis->aOperands[iOp].offSeg            = UINT64_MAX;
        pThis->aOperands[iOp].uEffAddr          = UINT64_MAX;
        pThis->aOperands[iOp].uImmDispValue     = UINT64_MAX;
        pThis->aOperands[iOp].uMemBaseRegValue  = UINT64_MAX;
        pThis->aOperands[iOp].uMemIndexRegValue = UINT64_MAX;
        pThis->aOperands[iOp].In.pv             = NULL;
        pThis->aOperands[iOp].Expected.pv       = NULL;
        pThis->aOperands[iOp].pDataBuf          = NULL;
    }

    /*
     * Reset various things.
     */
    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aiInOut); i++)
        pThis->aiInOut[i] = 0;

    return true;
}


static bool CidetCoreSetupInOut(PCIDETCORE pThis)
{
    /*
     * Enumerate the operands.
     */
    uint8_t *pbBuf = &pThis->abBuf[0];
    pbBuf = RT_ALIGN_PT(pbBuf, 16, uint8_t *);

    uint8_t idxOp = pThis->cOperands;
    while (idxOp-- > 0)
    {
        if (pThis->aOperands[idxOp].fIsMem)
        {
            /*
             * Memory operand.
             */
            Assert(pThis->aOperands[idxOp].fIsMem);

            /* Set the In & Expected members to point to temporary buffer space. */
            pThis->aOperands[idxOp].Expected.pu8 = pbBuf;
            pbBuf += pThis->aOperands[idxOp].cb;
            pbBuf = RT_ALIGN_PT(pbBuf, 16, uint8_t *);

            pThis->aOperands[idxOp].In.pu8 = pbBuf;
            pbBuf += pThis->aOperands[idxOp].cb;
            pbBuf = RT_ALIGN_PT(pbBuf, 16, uint8_t *);

            /* Initialize the buffer we're gonna use. */
            pThis->aOperands[idxOp].iEffSeg = pThis->uSegPrf != X86_SREG_COUNT
                                            ? pThis->uSegPrf
                                            : !(pThis->aOperands[idxOp].fFlags & CIDET_OF_ALWAYS_SEG_ES) ? X86_SREG_DS
                                            : X86_SREG_ES;

            PCIDETBUF pDataBuf = pThis->aOperands[idxOp].pDataBuf;
            AssertReleaseReturn(pDataBuf, false);
            Assert(pDataBuf->cb == pThis->aOperands[idxOp].cb);
            Assert(pDataBuf->idxOp == idxOp);
            if (!pThis->pfnReInitDataBuf(pThis, pDataBuf))
            {
                pThis->cSkippedReInitDataBuf++;
                return false;
            }
            pDataBuf->fActive = true;

            /* Calc buffer related operand members. */
            pThis->aOperands[idxOp].uEffAddr = pDataBuf->uEffBufAddr + pDataBuf->off;
            uint64_t offSeg = pThis->aOperands[idxOp].uEffAddr - pDataBuf->uSegBase;
            pThis->aOperands[idxOp].offSeg = offSeg;
            AssertRelease(offSeg <= g_au64ByteSizeToMask[pThis->cbAddrMode]);

            /*
             * Select register and displacement values for the buffer addressing (works on offSeg).
             */
            uint8_t const iMemIndexReg = pThis->aOperands[idxOp].iMemIndexReg;
            uint8_t const iMemBaseReg  = pThis->aOperands[idxOp].iMemBaseReg;
            if (pThis->aOperands[idxOp].fIsRipRelative)
            {
                /* rip relative. */
                pThis->aOperands[idxOp].uImmDispValue = offSeg - (pThis->InCtx.rip + pThis->cbInstr);
                Assert(pThis->aOperands[idxOp].cbMemDisp == 4);
                if (   (int64_t)pThis->aOperands[idxOp].uImmDispValue > INT32_MAX
                    || (int64_t)pThis->aOperands[idxOp].uImmDispValue < INT32_MIN)
                {
                    pThis->cSkippedDataBufWrtRip++;
                    return false;
                }
            }
            else if (iMemBaseReg != UINT8_MAX)
            {
                if (   iMemBaseReg != iMemIndexReg
                    || pThis->fUsesVexIndexRegs)
                {
                    /* [base] or [base + disp] or [base + index * scale] or [base + index * scale + disp] */
                    if (pThis->aOperands[idxOp].cbMemDisp > 0)
                    {
                        pThis->aOperands[idxOp].uImmDispValue = CidetCoreGetRandS64(pThis, pThis->aOperands[idxOp].cbMemDisp);
                        offSeg -= (int64_t)pThis->aOperands[idxOp].uImmDispValue;
                    }

                    if (iMemIndexReg != UINT8_MAX)
                    {
                        pThis->aOperands[idxOp].uMemIndexRegValue = CidetCoreGetRandU64(pThis, pThis->cbAddrMode);
                        offSeg -= pThis->aOperands[idxOp].uMemIndexRegValue * pThis->aOperands[idxOp].uMemScale;
                    }

                    pThis->aOperands[idxOp].uMemBaseRegValue = offSeg & g_au64ByteSizeToMask[pThis->cbAddrMode];
                }
                else
                {
                    /* base == index;  [base + index * scale] or [base * (scale + 1)]. */
                    uint8_t const uEffScale = pThis->aOperands[idxOp].uMemScale + 1;
                    if (pThis->aOperands[idxOp].cbMemDisp > 0)
                    {
                        pThis->aOperands[idxOp].uImmDispValue = CidetCoreGetRandS64(pThis, pThis->aOperands[idxOp].cbMemDisp);
                        offSeg -= (int64_t)pThis->aOperands[idxOp].uImmDispValue;
                        offSeg &= g_au64ByteSizeToMask[pThis->cbAddrMode];
                        uint8_t uRemainder = offSeg % uEffScale;
                        if (uRemainder != 0)
                        {
                            Assert(pThis->aOperands[idxOp].cbMemDisp < 8);
                            Assert(   (int64_t)pThis->aOperands[idxOp].uImmDispValue
                                   <= g_ai64ByteSizeToMax[pThis->aOperands[idxOp].cbMemDisp]);
                            pThis->aOperands[idxOp].uImmDispValue = (int64_t)pThis->aOperands[idxOp].uImmDispValue
                                                                  + uRemainder;
                            offSeg -= uRemainder;
                            if (  (int64_t)pThis->aOperands[idxOp].uImmDispValue
                                > g_ai64ByteSizeToMax[pThis->aOperands[idxOp].cbMemDisp])
                            {
                                pThis->aOperands[idxOp].uImmDispValue -= uEffScale;
                                offSeg += uEffScale;
                            }
                            Assert(offSeg % uEffScale == 0);
                        }
                    }
                    else
                    {
                        offSeg &= g_au64ByteSizeToMask[pThis->cbAddrMode];
                        if (offSeg % uEffScale != 0)
                        {
                            pThis->cSkippedSameBaseIndexRemainder++;
                            return false;
                        }
                    }
                    offSeg /= uEffScale;
                    pThis->aOperands[idxOp].uMemBaseRegValue = pThis->aOperands[idxOp].uMemIndexRegValue = offSeg;
                }
            }
            else if (iMemIndexReg != UINT8_MAX)
            {
                /* [index * scale] or  [index * scale + disp] */
                if (pThis->aOperands[idxOp].cbMemDisp > 0)
                {
                    pThis->aOperands[idxOp].uImmDispValue = CidetCoreGetRandS64(pThis, pThis->aOperands[idxOp].cbMemDisp);
                    offSeg -= (int64_t)pThis->aOperands[idxOp].uImmDispValue;
                    pThis->aOperands[idxOp].uImmDispValue += offSeg & (RT_BIT_64(pThis->aOperands[idxOp].uMemScale) - 1);
                    offSeg &= ~(RT_BIT_64(pThis->aOperands[idxOp].uMemScale) - 1);
                }
                else if (offSeg & (RT_BIT_64(pThis->aOperands[idxOp].uMemScale) - 1))
                {
                    pThis->cSkippedOnlyIndexRemainder++;
                    return false;
                }

                pThis->aOperands[idxOp].uMemIndexRegValue = offSeg / pThis->aOperands[idxOp].uMemScale;
                Assert((offSeg % pThis->aOperands[idxOp].uMemScale) == 0);
                AssertRelease(!pThis->fUsesVexIndexRegs); /** @todo implement VEX indexing */
            }
            else
            {
                /* [disp] */
                Assert(   pThis->aOperands[idxOp].cbMemDisp == 8
                       || pThis->aOperands[idxOp].cbMemDisp == 4
                       || pThis->aOperands[idxOp].cbMemDisp == 2
                       || pThis->aOperands[idxOp].cbMemDisp == 1);
                if (  pThis->aOperands[idxOp].cbMemDisp == 4
                    ? (int64_t)offSeg != (int32_t)offSeg
                    : pThis->aOperands[idxOp].cbMemDisp == 2
                    ? (int64_t)offSeg != (int16_t)offSeg
                    : pThis->aOperands[idxOp].cbMemDisp == 1
                    ? (int64_t)offSeg != (int8_t)offSeg
                    : false /* 8 */)
                {
                    pThis->cSkippedDirectAddressingOverflow++;
                    return false;
                }
                pThis->aOperands[idxOp].uImmDispValue = offSeg;
            }

            /*
             * Modify the input and expected output contexts with the base and
             * index register values.  To simplify verification and the work
             * here, we update the uMemBaseRegValue and uMemIndexRegValue
             * members to reflect the whole register.
             */
            if (iMemBaseReg != UINT8_MAX)
            {
                if (pThis->cbAddrMode == 4)
                {
                    pThis->aOperands[idxOp].uMemBaseRegValue &= UINT32_MAX;
                    pThis->aOperands[idxOp].uMemBaseRegValue |= pThis->InCtx.aGRegs[iMemBaseReg] & UINT64_C(0xffffffff00000000);
                }
                else if (pThis->cbAddrMode == 2)
                {
                    pThis->aOperands[idxOp].uMemBaseRegValue &= UINT16_MAX;
                    pThis->aOperands[idxOp].uMemBaseRegValue |= pThis->InCtx.aGRegs[iMemBaseReg] & UINT64_C(0xffffffffffff0000);
                }
                pThis->InCtx.aGRegs[iMemBaseReg]       = pThis->aOperands[idxOp].uMemBaseRegValue;
                pThis->ExpectedCtx.aGRegs[iMemBaseReg] = pThis->aOperands[idxOp].uMemBaseRegValue;
            }

            if (iMemIndexReg != UINT8_MAX)
            {
                if (pThis->cbAddrMode == 4)
                {
                    pThis->aOperands[idxOp].uMemIndexRegValue &= UINT32_MAX;
                    pThis->aOperands[idxOp].uMemIndexRegValue |= pThis->InCtx.aGRegs[iMemIndexReg] & UINT64_C(0xffffffff00000000);
                }
                else if (pThis->cbAddrMode == 2)
                {
                    pThis->aOperands[idxOp].uMemIndexRegValue &= UINT16_MAX;
                    pThis->aOperands[idxOp].uMemIndexRegValue |= pThis->InCtx.aGRegs[iMemIndexReg] & UINT64_C(0xffffffffffff0000);
                }
                pThis->InCtx.aGRegs[iMemIndexReg]       = pThis->aOperands[idxOp].uMemIndexRegValue;
                pThis->ExpectedCtx.aGRegs[iMemIndexReg] = pThis->aOperands[idxOp].uMemIndexRegValue;
            }
        }
        else
        {
            /*
             * Non-memory, so clear the memory related members.
             */
            Assert(!pThis->aOperands[idxOp].fIsMem);
            pThis->aOperands[idxOp].iEffSeg  = UINT8_MAX;
            pThis->aOperands[idxOp].offSeg   = UINT64_MAX;
            pThis->aOperands[idxOp].uEffAddr = UINT64_MAX;
            pThis->aOperands[idxOp].pDataBuf = NULL;

            switch (pThis->aOperands[idxOp].fFlags & CIDET_OF_K_MASK)
            {
                case CIDET_OF_K_GPR:
                    if (!pThis->aOperands[idxOp].fIsHighByteRegister)
                    {
                        pThis->aOperands[idxOp].In.pv = &pThis->InCtx.aGRegs[pThis->aOperands[idxOp].iReg];
                        pThis->aOperands[idxOp].Expected.pv = &pThis->ExpectedCtx.aGRegs[pThis->aOperands[idxOp].iReg];
                    }
                    else
                    {
                        pThis->aOperands[idxOp].In.pv = &pThis->InCtx.aGRegs[pThis->aOperands[idxOp].iReg - 4];
                        pThis->aOperands[idxOp].In.pu8++;
                        pThis->aOperands[idxOp].Expected.pv = &pThis->ExpectedCtx.aGRegs[pThis->aOperands[idxOp].iReg - 4];
                        pThis->aOperands[idxOp].Expected.pu8++;
                    }
                    break;

                case CIDET_OF_K_IMM:
                    pThis->aOperands[idxOp].In.pv = NULL;
                    pThis->aOperands[idxOp].Expected.pv = NULL;
                    break;

                case CIDET_OF_K_SREG:
                    if (pThis->aOperands[idxOp].iReg < RT_ELEMENTS(pThis->InCtx.aSRegs))
                    {
                        pThis->aOperands[idxOp].In.pv = &pThis->InCtx.aSRegs[pThis->aOperands[idxOp].iReg];
                        pThis->aOperands[idxOp].Expected.pv = &pThis->ExpectedCtx.aSRegs[pThis->aOperands[idxOp].iReg];
                    }
                    else
                    {
                        pThis->aOperands[idxOp].In.pv = NULL;
                        pThis->aOperands[idxOp].Expected.pv = NULL;
                    }
                    break;

                case CIDET_OF_K_CR:
                case CIDET_OF_K_SSE:
                case CIDET_OF_K_AVX:
                case CIDET_OF_K_AVX512:
                case CIDET_OF_K_FPU:
                case CIDET_OF_K_MMX:
                case CIDET_OF_K_AVXFUTURE:
                case CIDET_OF_K_SPECIAL:
                case CIDET_OF_K_TEST:
                    /** @todo Implement testing these registers. */
                case CIDET_OF_K_NONE:
                default:
                    AssertReleaseFailedReturn(false);
            }
        }
    }
    AssertRelease((uintptr_t)pbBuf - (uintptr_t)&pThis->abBuf[0] <= sizeof(pThis->abBuf));

    /*
     * Call instruction specific setup function (for operand values and flags).
     */
    int rc = pThis->pCurInstr->pfnSetupInOut(pThis, false /*fInvalid*/);
    if (RT_FAILURE(rc))
    {
        pThis->cSkippedSetupInOut++;
        return false;
    }

    /*
     * Do the 2nd set of the memory operand preparations.
     */
    if (pThis->fHasMemoryOperand)
    {
        idxOp = pThis->cOperands;
        while (idxOp-- > 0)
            if (pThis->aOperands[idxOp].fIsMem)
            {
                Assert(pThis->aOperands[idxOp].pDataBuf);
                if (!pThis->pfnSetupDataBuf(pThis, pThis->aOperands[idxOp].pDataBuf, pThis->aOperands[idxOp].In.pv))
                {
                    pThis->cSkippedSetupDataBuf++;
                    return false;
                }

                Assert(   pThis->aOperands[idxOp].iMemBaseReg == UINT8_MAX
                       || pThis->InCtx.aGRegs[pThis->aOperands[idxOp].iMemBaseReg] == pThis->aOperands[idxOp].uMemBaseRegValue);
                Assert(   pThis->aOperands[idxOp].iMemIndexReg == UINT8_MAX
                       || (  !pThis->fUsesVexIndexRegs
                           ?    pThis->InCtx.aGRegs[pThis->aOperands[idxOp].iMemIndexReg]
                             == pThis->aOperands[idxOp].uMemIndexRegValue
                           : false /** @todo VEX indexing */));
            }
    }

    return true;
}


/**
 * Figures the instruction length.
 *
 * This is a duplicate of CidetCoreAssemble() with the buffer updates removed.
 *
 * @returns true and pThis->cbInstr on success, false on failure.
 * @param   pThis           The core state structure (for context).
 */
static bool CidetCoreAssembleLength(PCIDETCORE pThis)
{
    uint8_t off = 0;

    /*
     * Prefixes.
     */
    if (1)
    {
        if (pThis->fAddrSizePrf)
            off++;
        if (pThis->fOpSizePrf)
            off++;
    }
    else
    {
        /** @todo prefix list. */
    }

    /*
     * Prefixes that must come right before the opcode.
     */
    /** @todo VEX and EVEX. */
    if (pThis->fVex)
    {
        /** @todo VEX and EVEX. */
    }
    else if (pThis->fEvex)
    {
        /** @todo VEX and EVEX. */
    }
    else
    {
        if (pThis->fRexB || pThis->fRexX || pThis->fRexR || pThis->fRexW || pThis->fRex)
            off++;
    }

    /*
     * The opcode.
     */
    //uint8_t const *pbOpcode = pThis->pCurInstr->abOpcode;
    switch (pThis->pCurInstr->cbOpcode)
    {
        case 3: off++; RT_FALL_THRU();
        case 2: off++; RT_FALL_THRU();
        case 1: off++;
            break;
        default:
            AssertReleaseFailedReturn(false);
    }

    /*
     * Mod R/M
     */
    if (pThis->fUsesModRm)
    {
        off++;
        if (pThis->fSib)
            off++;
        if (pThis->idxMrmRmOp < RT_ELEMENTS(pThis->aOperands))
        {
            //uint64_t uDispValue = pThis->aOperands[pThis->idxMrmRmOp].uImmDispValue;
            switch (pThis->aOperands[pThis->idxMrmRmOp].cbMemDisp)
            {
                case 0: break;
                case 8:
                case 7:
                case 6:
                case 5:
                case 4:
                case 3:
                case 2:
                case 1:
                    break;
                default: AssertReleaseFailedReturn(false);
            }
            off += pThis->aOperands[pThis->idxMrmRmOp].cbMemDisp;
        }
    }

    /*
     * Immediates.
     */
    uint8_t iOp = pThis->cOperands;
    while (iOp-- > 0)
        if ((pThis->aOperands[iOp].fFlags & CIDET_OF_K_MASK) == CIDET_OF_K_IMM)
        {
            //uint64_t uImmValue = pThis->aOperands[iOp].uImmDispValue;
            switch (pThis->aOperands[iOp].cb)
            {
                case 8:
                case 7:
                case 6:
                case 5:
                case 4:
                case 3:
                case 2:
                case 1:
                    break;
                default: AssertReleaseFailedReturn(false);
            }
            off += pThis->aOperands[iOp].cb;
        }

    pThis->cbInstr = off;
    return true;
}


/**
 * Assembles the instruction.
 *
 * This is a duplicate of CidetCoreAssembleLength() with buffer writes.
 *
 * @returns true and pThis->cbInstr and pThis->abInstr on success, false on
 *          failure.
 * @param   pThis           The core state structure (for context).
 */
static bool CidetCoreAssemble(PCIDETCORE pThis)
{
    uint8_t off = 0;

    /*
     * Prefixes.
     */
    if (1)
    {
        if (pThis->fAddrSizePrf)
            pThis->abInstr[off++] = 0x67;
        if (pThis->fOpSizePrf)
            pThis->abInstr[off++] = 0x66;
    }
    else
    {
        /** @todo prefix list. */
    }

    /*
     * Prefixes that must come right before the opcode.
     */
    /** @todo VEX and EVEX. */
    if (pThis->fVex)
    {
        /** @todo VEX and EVEX. */
    }
    else if (pThis->fEvex)
    {
        /** @todo VEX and EVEX. */
    }
    else
    {
        if (pThis->fRexB || pThis->fRexX || pThis->fRexR || pThis->fRexW || pThis->fRex)
            pThis->abInstr[off++] = 0x40 | (pThis->fRexB * 1) | (pThis->fRexX * 2) | (pThis->fRexR * 4) | (pThis->fRexW * 8);
    }

    /*
     * The opcode.
     */
    uint8_t const *pbOpcode = pThis->pCurInstr->abOpcode;
    switch (pThis->pCurInstr->cbOpcode)
    {
        case 3: pThis->abInstr[off++] = *pbOpcode++; RT_FALL_THRU();
        case 2: pThis->abInstr[off++] = *pbOpcode++; RT_FALL_THRU();
        case 1: pThis->abInstr[off++] = *pbOpcode++;
            break;
        default:
            AssertReleaseFailedReturn(false);
    }

    /*
     * Mod R/M
     */
    if (pThis->fUsesModRm)
    {
        pThis->abInstr[off++] = pThis->bModRm;
        if (pThis->fSib)
            pThis->abInstr[off++] = pThis->bSib;
        if (pThis->idxMrmRmOp < RT_ELEMENTS(pThis->aOperands))
        {
            uint64_t uDispValue = pThis->aOperands[pThis->idxMrmRmOp].uImmDispValue;
            switch (pThis->aOperands[pThis->idxMrmRmOp].cbMemDisp)
            {
                case 0: break;
                case 8: pThis->abInstr[off + 3] = (uDispValue >> 56) & UINT8_C(0xff); RT_FALL_THRU();
                case 7: pThis->abInstr[off + 3] = (uDispValue >> 48) & UINT8_C(0xff); RT_FALL_THRU();
                case 6: pThis->abInstr[off + 3] = (uDispValue >> 40) & UINT8_C(0xff); RT_FALL_THRU();
                case 5: pThis->abInstr[off + 3] = (uDispValue >> 32) & UINT8_C(0xff); RT_FALL_THRU();
                case 4: pThis->abInstr[off + 3] = (uDispValue >> 24) & UINT8_C(0xff); RT_FALL_THRU();
                case 3: pThis->abInstr[off + 2] = (uDispValue >> 16) & UINT8_C(0xff); RT_FALL_THRU();
                case 2: pThis->abInstr[off + 1] = (uDispValue >>  8) & UINT8_C(0xff); RT_FALL_THRU();
                case 1: pThis->abInstr[off] = uDispValue & UINT8_C(0xff);
                    break;
                default: AssertReleaseFailedReturn(false);
            }
            off += pThis->aOperands[pThis->idxMrmRmOp].cbMemDisp;
        }
    }

    /*
     * Immediates.
     */
    uint8_t iOp = pThis->cOperands;
    while (iOp-- > 0)
        if ((pThis->aOperands[iOp].fFlags & CIDET_OF_K_MASK) == CIDET_OF_K_IMM)
        {
            uint64_t uImmValue = pThis->aOperands[iOp].uImmDispValue;
            switch (pThis->aOperands[iOp].cb)
            {
                case 8: pThis->abInstr[off + 3] = (uImmValue >> 56) & UINT8_C(0xff); RT_FALL_THRU();
                case 7: pThis->abInstr[off + 3] = (uImmValue >> 48) & UINT8_C(0xff); RT_FALL_THRU();
                case 6: pThis->abInstr[off + 3] = (uImmValue >> 40) & UINT8_C(0xff); RT_FALL_THRU();
                case 5: pThis->abInstr[off + 3] = (uImmValue >> 32) & UINT8_C(0xff); RT_FALL_THRU();
                case 4: pThis->abInstr[off + 3] = (uImmValue >> 24) & UINT8_C(0xff); RT_FALL_THRU();
                case 3: pThis->abInstr[off + 2] = (uImmValue >> 16) & UINT8_C(0xff); RT_FALL_THRU();
                case 2: pThis->abInstr[off + 1] = (uImmValue >>  8) & UINT8_C(0xff); RT_FALL_THRU();
                case 1: pThis->abInstr[off] = uImmValue & UINT8_C(0xff);
                    break;
                default: AssertReleaseFailedReturn(false);
            }
            off += pThis->aOperands[iOp].cb;
        }

    pThis->cbInstr = off;
    return true;
}


static bool CidetCoreReInitCodeBuf(PCIDETCORE pThis)
{
    /*
     * Re-initialize the buffer.  Requires instruction length and positioning.
     */
    if (CidetCoreAssembleLength(pThis))
    {
        pThis->CodeBuf.cb  = pThis->cbInstr;
        pThis->CodeBuf.off = CIDET_CODE_BUF_SIZE - PAGE_SIZE - pThis->cbInstr;
        if (pThis->pfnReInitCodeBuf(pThis, &pThis->CodeBuf))
        {
            pThis->CodeBuf.fActive = true;

            /*
             * Update the RIP and CS values in the input and expected contexts.
             */
            pThis->InCtx.rip       = pThis->CodeBuf.uEffBufAddr + pThis->CodeBuf.offActive - pThis->CodeBuf.uSegBase;
            pThis->ExpectedCtx.rip = pThis->InCtx.rip + pThis->cbInstr; /** @todo account for expected traps. */
            if (pThis->CodeBuf.uSeg != UINT32_MAX)
            {
                pThis->InCtx.aSRegs[X86_SREG_CS] = pThis->CodeBuf.uSeg;
                pThis->ExpectedCtx.aSRegs[X86_SREG_CS] = pThis->CodeBuf.uSeg;
            }
            return true;
        }
        else
            pThis->cSkippedReInitCodeBuf++;
    }
    else
        pThis->cSkippedAssemble++;
    return false;
}


#ifdef CIDET_DEBUG_DISAS
/**
 * @callback_method_impl{FNDISREADBYTES}
 */
static DECLCALLBACK(int) cidetCoreDisReadBytes(PDISSTATE pDis, uint8_t offInstr, uint8_t cbMinRead, uint8_t cbMaxRead)
{
    PCIDETCORE pThis = (PCIDETCORE)pDis->pvUser;
    memcpy(&pDis->abInstr[offInstr], &pThis->abInstr[offInstr], cbMaxRead);
    pDis->cbCachedInstr = offInstr + cbMaxRead;
    return VINF_SUCCESS;
}
#endif


static bool CidetCoreSetupCodeBuf(PCIDETCORE pThis, unsigned iSubTest)
{
    if (CidetCoreAssemble(pThis))
    {
        //CIDET_DPRINTF(("%04u: %.*Rhxs\n", i, pThis->cbInstr, pThis->abInstr));
#ifdef CIDET_DEBUG_DISAS
        DISCPUSTATE Dis;
        char        szInstr[80] = {0};
        uint32_t    cbInstr;
        int rcDis = DISInstrToStrEx(pThis->InCtx.rip,
                                    CIDETMODE_IS_64BIT(pThis->bMode)   ? DISCPUMODE_64BIT
                                    : CIDETMODE_IS_32BIT(pThis->bMode) ? DISCPUMODE_32BIT : DISCPUMODE_16BIT,
                                    cidetCoreDisReadBytes,
                                    pThis,
                                    DISOPTYPE_ALL,
                                    &Dis,
                                    &cbInstr,
                                    szInstr, sizeof(szInstr));
        CIDET_DPRINTF(("%04u: %s", iSubTest, szInstr));
        Assert(cbInstr == pThis->cbInstr);
#else
        RT_NOREF_PV(iSubTest);
#endif
        if (pThis->pfnSetupCodeBuf(pThis, &pThis->CodeBuf, pThis->abInstr))
        {
            return true;
        }
        pThis->cSkippedSetupCodeBuf++;
    }
    else
        pThis->cSkippedAssemble++;
    return false;
}


/**
 * Compares the output with the output expectations.
 *
 * @returns true if ok, false if not (calls pfnFailure too).
 * @param   pThis           The core state structure.
 */
static bool CidetCoreCheckResults(PCIDETCORE pThis)
{
    if (memcmp(&pThis->ActualCtx, &pThis->ExpectedCtx, CIDETCPUCTX_COMPARE_SIZE) == 0)
        return true;

    unsigned cDiffs = 0;
#define IF_FIELD_DIFFERS_SET_ERROR(a_Field, a_Fmt) \
        if (pThis->ActualCtx.a_Field != pThis->ExpectedCtx.a_Field) \
        { \
            CidetCoreSetError(pThis, #a_Field " differs: got %#llx expected %#llx", \
                              pThis->ActualCtx.a_Field, pThis->ExpectedCtx.a_Field); \
            cDiffs++; \
        } else do { } while (0)

    IF_FIELD_DIFFERS_SET_ERROR(rip,                    "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(rfl,                    "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(aGRegs[X86_GREG_xAX],   "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(aGRegs[X86_GREG_xBX],   "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(aGRegs[X86_GREG_xCX],   "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(aGRegs[X86_GREG_xDX],   "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(aGRegs[X86_GREG_xSP],   "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(aGRegs[X86_GREG_xBP],   "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(aGRegs[X86_GREG_xSI],   "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(aGRegs[X86_GREG_xDI],   "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(aGRegs[X86_GREG_x8],    "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(aGRegs[X86_GREG_x9],    "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(aGRegs[X86_GREG_x9],    "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(aGRegs[X86_GREG_x10],   "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(aGRegs[X86_GREG_x11],   "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(aGRegs[X86_GREG_x12],   "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(aGRegs[X86_GREG_x13],   "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(aGRegs[X86_GREG_x14],   "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(aGRegs[X86_GREG_x15],   "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(aSRegs[X86_SREG_CS],    "%#06x");
    IF_FIELD_DIFFERS_SET_ERROR(aSRegs[X86_SREG_SS],    "%#06x");
    IF_FIELD_DIFFERS_SET_ERROR(aSRegs[X86_SREG_DS],    "%#06x");
    IF_FIELD_DIFFERS_SET_ERROR(aSRegs[X86_SREG_ES],    "%#06x");
    IF_FIELD_DIFFERS_SET_ERROR(aSRegs[X86_SREG_FS],    "%#06x");
    IF_FIELD_DIFFERS_SET_ERROR(aSRegs[X86_SREG_GS],    "%#06x");
    IF_FIELD_DIFFERS_SET_ERROR(uXcpt,                  "%#04x");
    IF_FIELD_DIFFERS_SET_ERROR(uErr,                   "%#04llx");
    IF_FIELD_DIFFERS_SET_ERROR(cr2,                    "%#010llx");
#ifndef CIDET_REDUCED_CTX
    IF_FIELD_DIFFERS_SET_ERROR(tr,                     "%#06x");
    IF_FIELD_DIFFERS_SET_ERROR(ldtr,                   "%#06x");
    IF_FIELD_DIFFERS_SET_ERROR(cr0,                    "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(cr3,                    "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(cr4,                    "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(cr8,                    "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(dr0,                    "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(dr1,                    "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(dr2,                    "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(dr3,                    "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(dr6,                    "%#010llx");
    IF_FIELD_DIFFERS_SET_ERROR(dr7,                    "%#010llx");
#endif

AssertMsgFailed(("cDiffs=%d\n", cDiffs));
    Assert(cDiffs > 0);
    return cDiffs == 0;
}


static bool CidetCoreTest_Basic(PCIDETCORE pThis)
{
    /*
     * Iterate all encodings.
     */
    if (!CidetCoreSetupFirstBaseEncoding(pThis))
        return CidetCoreSetError(pThis, "CidetCoreSetupFirstBaseEncoding failed");
    unsigned cExecuted = 0;
    unsigned cSkipped = 0;
    do
    {
        /*
         * Iterate data buffer configurations (one iteration if none).
         */
        if (CidetCoreSetupFirstMemoryOperandConfig(pThis))
        {
            do
            {
                /*
                 * Iterate code buffer configurations.
                 */
                if (!CidetCoreSetupFirstCodeBufferConfig(pThis))
                    return CidetCoreSetError(pThis, "CidetCoreSetupFirstMemoryOperandConfig failed");
                do
                {
                    /*
                     * Set up inputs and expected outputs, then emit the test code.
                     */
                    pThis->InCtx        = pThis->InTemplateCtx;
                    pThis->InCtx.fTrickyStack = pThis->fHasStackRegInMrmRmBase || pThis->fHasStackRegInMrmReg;
                    pThis->ExpectedCtx  = pThis->InCtx;
                    if (   CidetCoreReInitCodeBuf(pThis)
                        && CidetCoreSetupInOut(pThis)
                        && CidetCoreSetupCodeBuf(pThis, cSkipped + cExecuted)
                       )
                    {
                        if (pThis->pfnExecute(pThis))
                        {
                            cExecuted++;

                            /*
                             * Check the result against our expectations.
                             */
                            CidetCoreCheckResults(pThis);
                            /** @todo check result. */

                        }
                        else
                            cSkipped++;
                    }
                    else
                        cSkipped++;
                } while (CidetCoreSetupNextCodeBufferConfig(pThis));
            } while (CidetCoreSetupNextMemoryOperandConfig(pThis));
        }
        else
            cSkipped++;
    } while (CidetCoreSetupNextBaseEncoding(pThis));

    CIDET_DPRINTF(("CidetCoreTest_Basic: cExecuted=%u cSkipped=%u\n"
                   "  cSkippedSetupInOut               =%u\n"
                   "  cSkippedReInitDataBuf            =%u\n"
                   "  cSkippedSetupDataBuf             =%u\n"
                   "  cSkippedDataBufWrtRip            =%u\n"
                   "  cSkippedAssemble                 =%u\n"
                   "  cSkippedReInitCodeBuf            =%u\n"
                   "  cSkippedSetupCodeBuf             =%u\n"
                   "  cSkippedSameBaseIndexRemainder   =%u\n"
                   "  cSkippedOnlyIndexRemainder       =%u\n"
                   "  cSkippedDirectAddressingOverflow =%u\n"
                   ,
                   cExecuted, cSkipped,
                   pThis->cSkippedSetupInOut,
                   pThis->cSkippedReInitDataBuf,
                   pThis->cSkippedSetupDataBuf,
                   pThis->cSkippedDataBufWrtRip,
                   pThis->cSkippedAssemble,
                   pThis->cSkippedReInitCodeBuf,
                   pThis->cSkippedSetupCodeBuf,
                   pThis->cSkippedSameBaseIndexRemainder,
                   pThis->cSkippedOnlyIndexRemainder,
                   pThis->cSkippedDirectAddressingOverflow
                   ));

    return true;
}


bool CidetCoreTestInstruction(PCIDETCORE pThis, PCCIDETINSTR pInstr)
{
    AssertReleaseMsgReturn(RT_VALID_PTR(pThis), ("%p\n", pThis), false);
    AssertReleaseReturn(pThis->u32Magic == CIDETCORE_MAGIC, false);
    AssertReleaseReturn(pThis->cCodeBufConfigs > 0, false);

    if (!CideCoreSetInstruction(pThis, pInstr))
        return CidetCoreSetError(pThis, "CideCoreSetInstruction failed");

    bool fResult = CidetCoreTest_Basic(pThis);

    return fResult;
}

