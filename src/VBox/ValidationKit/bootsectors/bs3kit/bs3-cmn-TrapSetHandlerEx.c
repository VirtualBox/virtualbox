/* $Id: bs3-cmn-TrapSetHandlerEx.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3Trap32SetHandlerEx
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
#include <iprt/asm-amd64-x86.h>


/*********************************************************************************************************************************
*   External Symbols                                                                                                             *
*********************************************************************************************************************************/
extern uint16_t     BS3_DATA_NM(g_apfnBs3TrapHandlers_c16)[256];
extern uint32_t     BS3_DATA_NM(g_apfnBs3TrapHandlers_c32)[256];
extern uint64_t     BS3_DATA_NM(g_apfnBs3TrapHandlers_c64)[256];


#undef Bs3TrapSetHandlerEx
BS3_CMN_DEF(void, Bs3TrapSetHandlerEx,(uint8_t iIdt, PFNBS3TRAPHANDLER16 pfnHandler16,
                                       PFNBS3TRAPHANDLER32 pfnHandler32, PFNBS3TRAPHANDLER64 pfnHandler64))
{
    RTCCUINTREG fFlags = ASMIntDisableFlags();
#if ARCH_BITS == 16
    /* Far real mode pointers as input. */
    g_apfnBs3TrapHandlers_c16[iIdt]  = (uint16_t)pfnHandler16;
    g_apfnBs3TrapHandlers_c32[iIdt]  = Bs3SelRealModeCodeToFlat((PFNBS3FARADDRCONV)pfnHandler32);
    g_apfnBs3TrapHandlers_c64[iIdt]  = Bs3SelRealModeCodeToFlat((PFNBS3FARADDRCONV)pfnHandler64);
#else
    /* Flat pointers as input. */
    g_apfnBs3TrapHandlers_c16[iIdt]  = (uint16_t)Bs3SelFlatCodeToProtFar16((uintptr_t)pfnHandler16);
    g_apfnBs3TrapHandlers_c32[iIdt]  = (uint32_t)(uintptr_t)pfnHandler32;
    g_apfnBs3TrapHandlers_c64[iIdt]  = (uintptr_t)pfnHandler64;
#endif
    ASMSetFlags(fFlags);
}

