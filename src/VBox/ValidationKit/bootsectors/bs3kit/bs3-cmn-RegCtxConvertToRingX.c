/* $Id: bs3-cmn-RegCtxConvertToRingX.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3RegCtxConvertToRingX
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "bs3kit-template-header.h"


/**
 * Transforms a real mode segment into a protected mode selector.
 *
 * @returns Protected mode selector.
 * @param   uSeg            The real mode segment.
 * @param   bRing           The target ring.
 */
static uint16_t bs3RegCtxConvertRealSegToRingX(uint16_t uSeg, uint8_t bRing)
{
    uint16_t uSel;
    if (   uSeg == 0
        || uSeg == BS3_SEL_R0_SS16)
        uSel = BS3_SEL_R0_SS16 + ((uint16_t)bRing << BS3_SEL_RING_SHIFT);
    else if (   uSeg == (BS3_ADDR_BS3TEXT16 >> 4)
             || uSeg == BS3_SEL_R0_CS16)
        uSel = BS3_SEL_R0_CS16 + ((uint16_t)bRing << BS3_SEL_RING_SHIFT);
    else if (   uSeg == (BS3_ADDR_BS3DATA16 >> 4)
             || uSeg == BS3_SEL_R0_DS16)
        uSel = BS3_SEL_R0_DS16 + ((uint16_t)bRing << BS3_SEL_RING_SHIFT);
    else if (uSeg == (BS3_ADDR_BS3SYSTEM16 >> 4))
        uSel = BS3_SEL_SYSTEM16;
    else if (!(uSeg & 0xfff))
        uSel = (uSeg >> (12 - X86_SEL_SHIFT)) + BS3_SEL_TILED;
    else if (uSeg == BS3_SEL_R0_DS16)
        uSel = (uSeg >> (12 - X86_SEL_SHIFT)) + BS3_SEL_TILED;
    else
    {
        Bs3Printf("uSeg=%#x\n", uSeg);
        BS3_ASSERT(0);
        return 0;
    }
    uSel |= bRing;
    return uSel;
}


/**
 * Transforms a protected mode selector to a different ring.
 *
 * @returns Adjusted protected mode selector.
 * @param   uSel            The current selector value.
 * @param   bRing           The target ring.
 * @param   iReg            Register index.
 */
static uint16_t bs3RegCtxConvertProtSelToRingX(uint16_t uSel, uint8_t bRing, uint8_t iReg)
{
    if (   uSel > X86_SEL_RPL
        && !(uSel & X86_SEL_LDT) )
    {
        if (uSel >= BS3_SEL_R0_FIRST && uSel < BS3_SEL_R0_FIRST + (5 << BS3_SEL_RING_SHIFT))
        {
            /* Convert BS3_SEL_R*_XXX to the target ring. */
            uSel &= BS3_SEL_RING_SUB_MASK;
            uSel |= bRing;
            uSel += BS3_SEL_R0_FIRST;
            uSel += (uint16_t)bRing << BS3_SEL_RING_SHIFT;
        }
        else
        {
            /* Convert TEXT16 and DATA16 to BS3_SEL_R*_XXX. */
            uint16_t const uSelRaw = uSel & X86_SEL_MASK_OFF_RPL;
            if (uSelRaw  == BS3_SEL_TEXT16)
                uSel = (BS3_SEL_R0_CS16 | bRing) + ((uint16_t)bRing << BS3_SEL_RING_SHIFT);
            else if (uSelRaw == BS3_SEL_DATA16)
                uSel = (BS3_SEL_R0_DS16 | bRing) + ((uint16_t)bRing << BS3_SEL_RING_SHIFT);
            /* CS and SS must have CPL == DPL.  So, convert to standard selectors as we're
               usually here because Bs3SwitchToRing0 was called to get out of a test situation. */
            else if (iReg == X86_SREG_CS || iReg == X86_SREG_SS)
            {
                if (   Bs3Gdt[uSel >> X86_SEL_SHIFT].Gen.u1Long
                    && BS3_MODE_IS_64BIT_SYS(g_bBs3CurrentMode) )
                    uSel = iReg == X86_SREG_CS ? BS3_SEL_R0_CS64 : BS3_SEL_R0_DS64;
                else
                {
                    uint32_t uFlat   = Bs3SelFar32ToFlat32(0, uSel);
                    bool     fDefBig = Bs3Gdt[uSel >> X86_SEL_SHIFT].Gen.u1DefBig;
                    if (!fDefBig && uFlat == BS3_ADDR_BS3TEXT16 && iReg == X86_SREG_CS)
                        uSel = BS3_SEL_R0_CS16;
                    else if (!fDefBig && uFlat == 0 && iReg == X86_SREG_SS)
                        uSel = BS3_SEL_R0_SS16;
                    else if (fDefBig && uFlat == 0)
                        uSel = iReg == X86_SREG_CS ? BS3_SEL_R0_CS32 : BS3_SEL_R0_SS32;
                    else
                    {
                        Bs3Printf("uSel=%#x iReg=%d\n", uSel, iReg);
                        BS3_ASSERT(0);
                        return uSel;
                    }
                    uSel |= bRing;
                    uSel += (uint16_t)bRing << BS3_SEL_RING_SHIFT;
                }
            }
            /* Adjust the RPL on tiled and MMIO selectors. */
            else if (   uSelRaw == BS3_SEL_VMMDEV_MMIO16
                     || uSelRaw >= BS3_SEL_TILED)
                uSel = uSelRaw | bRing;
        }
    }
    return uSel;
}


/**
 * Transforms a register context to a different ring.
 *
 * @param   pRegCtx     The register context.
 * @param   bRing       The target ring (0..3).
 *
 * @note    Do _NOT_ call this for creating real mode or v8086 contexts, because
 *          it will always output a protected mode context!
 */
#undef Bs3RegCtxConvertToRingX
BS3_CMN_DEF(void, Bs3RegCtxConvertToRingX,(PBS3REGCTX pRegCtx, uint8_t bRing))
{
    if (   (pRegCtx->rflags.u32 & X86_EFL_VM)
        || pRegCtx->bMode == BS3_MODE_RM)
    {
        pRegCtx->rflags.u32 &= ~X86_EFL_VM;
        pRegCtx->bMode &= ~BS3_MODE_CODE_MASK;
        pRegCtx->bMode |= BS3_MODE_CODE_16;
        pRegCtx->cs = bs3RegCtxConvertRealSegToRingX(pRegCtx->cs, bRing);
        pRegCtx->ss = bs3RegCtxConvertRealSegToRingX(pRegCtx->ss, bRing);
        pRegCtx->ds = bs3RegCtxConvertRealSegToRingX(pRegCtx->ds, bRing);
        pRegCtx->es = bs3RegCtxConvertRealSegToRingX(pRegCtx->es, bRing);
        pRegCtx->fs = bs3RegCtxConvertRealSegToRingX(pRegCtx->fs, bRing);
        pRegCtx->gs = bs3RegCtxConvertRealSegToRingX(pRegCtx->gs, bRing);
    }
    else
    {
        pRegCtx->cs = bs3RegCtxConvertProtSelToRingX(pRegCtx->cs, bRing, X86_SREG_CS);
        pRegCtx->ss = bs3RegCtxConvertProtSelToRingX(pRegCtx->ss, bRing, X86_SREG_SS);
        pRegCtx->ds = bs3RegCtxConvertProtSelToRingX(pRegCtx->ds, bRing, X86_SREG_DS);
        pRegCtx->es = bs3RegCtxConvertProtSelToRingX(pRegCtx->es, bRing, X86_SREG_ES);
        pRegCtx->fs = bs3RegCtxConvertProtSelToRingX(pRegCtx->fs, bRing, X86_SREG_FS);
        pRegCtx->gs = bs3RegCtxConvertProtSelToRingX(pRegCtx->gs, bRing, X86_SREG_GS);
    }
    pRegCtx->bCpl = bRing;
}

