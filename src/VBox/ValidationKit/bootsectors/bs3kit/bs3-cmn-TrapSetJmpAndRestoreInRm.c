/* $Id: bs3-cmn-TrapSetJmpAndRestoreInRm.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3TrapSetJmpAndRestoreInRm
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


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
/* assembly helpers */
BS3_MODE_PROTO_NOSB(void, Bs3TrapSetJmpAndRestoreInRmAsm, (uint32_t, uint32_t));


#undef Bs3TrapSetJmpAndRestoreInRm
BS3_CMN_DEF(void, Bs3TrapSetJmpAndRestoreInRm,(PCBS3REGCTX pCtxRestore, PBS3TRAPFRAME pTrapFrame))
{
#if TMPL_BITS == 16
    if (g_bBs3CurrentMode == BS3_MODE_RM)
        Bs3TrapSetJmpAndRestore(pCtxRestore, pTrapFrame);
    else
#endif
    {
        uint32_t const pfRealModeCtxRestore = Bs3SelFlatDataToRealMode(Bs3SelPtrToFlat((PBS3REGCTX)pCtxRestore));
        uint32_t const pfRealModeTrapFrame  = Bs3SelFlatDataToRealMode(Bs3SelPtrToFlat(pTrapFrame));

#if TMPL_BITS == 16
        switch (g_bBs3CurrentMode & BS3_MODE_SYS_MASK)
        {
            case BS3_MODE_SYS_PE16:
                Bs3TrapSetJmpAndRestoreInRmAsm_pe16(pfRealModeCtxRestore, pfRealModeTrapFrame);
                break;
            case BS3_MODE_SYS_PE32:
                Bs3TrapSetJmpAndRestoreInRmAsm_pe32_16(pfRealModeCtxRestore, pfRealModeTrapFrame);
                break;
            case BS3_MODE_SYS_PP16:
                Bs3TrapSetJmpAndRestoreInRmAsm_pp16(pfRealModeCtxRestore, pfRealModeTrapFrame);
                break;
            case BS3_MODE_SYS_PP32:
                Bs3TrapSetJmpAndRestoreInRmAsm_pp32_16(pfRealModeCtxRestore, pfRealModeTrapFrame);
                break;
            case BS3_MODE_SYS_PAE16:
                Bs3TrapSetJmpAndRestoreInRmAsm_pae16(pfRealModeCtxRestore, pfRealModeTrapFrame);
                break;
            case BS3_MODE_SYS_PAE32:
                Bs3TrapSetJmpAndRestoreInRmAsm_pae32_16(pfRealModeCtxRestore, pfRealModeTrapFrame);
                break;
            case BS3_MODE_SYS_LM:
                Bs3TrapSetJmpAndRestoreInRmAsm_lm16(pfRealModeCtxRestore, pfRealModeTrapFrame);
                break;
            default:
                BS3_ASSERT(0);
        }

#elif TMPL_BITS == 32
        switch (g_bBs3CurrentMode & BS3_MODE_SYS_MASK)
        {
            case BS3_MODE_SYS_PE16:
                Bs3TrapSetJmpAndRestoreInRmAsm_pe16_32(pfRealModeCtxRestore, pfRealModeTrapFrame);
                break;
            case BS3_MODE_SYS_PE32:
                Bs3TrapSetJmpAndRestoreInRmAsm_pe32(pfRealModeCtxRestore, pfRealModeTrapFrame);
                break;
            case BS3_MODE_SYS_PP16:
                Bs3TrapSetJmpAndRestoreInRmAsm_pp16_32(pfRealModeCtxRestore, pfRealModeTrapFrame);
                break;
            case BS3_MODE_SYS_PP32:
                Bs3TrapSetJmpAndRestoreInRmAsm_pp32(pfRealModeCtxRestore, pfRealModeTrapFrame);
                break;
            case BS3_MODE_SYS_PAE16:
                Bs3TrapSetJmpAndRestoreInRmAsm_pae16_32(pfRealModeCtxRestore, pfRealModeTrapFrame);
                break;
            case BS3_MODE_SYS_PAE32:
                Bs3TrapSetJmpAndRestoreInRmAsm_pae32(pfRealModeCtxRestore, pfRealModeTrapFrame);
                break;
            case BS3_MODE_SYS_LM:
                Bs3TrapSetJmpAndRestoreInRmAsm_lm32(pfRealModeCtxRestore, pfRealModeTrapFrame);
                break;
            default:
                BS3_ASSERT(0);
        }

#elif TMPL_BITS == 64
        switch (g_bBs3CurrentMode & BS3_MODE_SYS_MASK)
        {
            case BS3_MODE_SYS_LM:
                Bs3TrapSetJmpAndRestoreInRmAsm_lm64(pfRealModeCtxRestore, pfRealModeTrapFrame);
                break;
            default:
                BS3_ASSERT(0);
        }
#else
# error Bogus TMPL_BITS
#endif
    }
}

