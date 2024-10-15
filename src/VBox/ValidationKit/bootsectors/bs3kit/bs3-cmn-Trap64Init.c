/* $Id: bs3-cmn-Trap64Init.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3Trap64Init
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


#undef Bs3Trap64Init
BS3_CMN_DEF(void, Bs3Trap64Init,(void))
{
    Bs3Trap64InitEx(false);
}

#undef Bs3Trap64InitEx
BS3_CMN_DEF(void, Bs3Trap64InitEx,(bool fMoreIstUsage))
{
    static const uint8_t s_aAssignments[] =
    {
        /* [X86_XCPT_DE] = */   3,
        /* [X86_XCPT_DB] = */   2,
        /* [X86_XCPT_NMI] = */  0,
        /* [X86_XCPT_BP] = */   2,
        /* [X86_XCPT_OF] = */   3,
        /* [X86_XCPT_BR] = */   3,
        /* [X86_XCPT_UD] = */   4,
        /* [X86_XCPT_NM] = */   3,
        /* [X86_XCPT_DF] = */   1,
        /*        [0x09] = */   0,
        /* [X86_XCPT_TS] = */   1,
        /* [X86_XCPT_NP] = */   5,
        /* [X86_XCPT_SS] = */   5,
        /* [X86_XCPT_GP] = */   6,
        /* [X86_XCPT_PF] = */   7,
        /*        [0x0f] = */   0,
        /* [X86_XCPT_MF] = */   0,
        /* [X86_XCPT_AC] = */   3,
        /* [X86_XCPT_MC] = */   0,
        /* [X86_XCPT_XF] = */   0,
        /* [X86_XCPT_VE] = */   0,
        /* [X86_XCPT_CP] = */   6,
    };
#ifdef _MSC_VER /* No-SSE hack */
    X86TSS64 BS3_FAR volatile *pTss;
#else
    X86TSS64 BS3_FAR *pTss;
#endif
    unsigned iIdt;

    /*
     * IDT entries, except the system call gate.
     * The #DF entry get IST=1, all others IST=0.
     */
    for (iIdt = 0; iIdt < BS3_TRAP_SYSCALL; iIdt++)
        Bs3Trap64SetGate(iIdt, AMD64_SEL_TYPE_SYS_INT_GATE, 0 /*bDpl*/,
                         BS3_SEL_R0_CS64, g_Bs3Trap64GenericEntriesFlatAddr + iIdt * 8,
                         !fMoreIstUsage ? iIdt == X86_XCPT_DF : iIdt < RT_ELEMENTS(s_aAssignments) ? s_aAssignments[iIdt] : 0);
    for (iIdt = BS3_TRAP_SYSCALL + 1; iIdt < 256; iIdt++)
        Bs3Trap64SetGate(iIdt, AMD64_SEL_TYPE_SYS_INT_GATE, 0 /*bDpl*/,
                         BS3_SEL_R0_CS64, g_Bs3Trap64GenericEntriesFlatAddr + iIdt * 8, 0);

    /*
     * Initialize the normal TSS so we can do ring transitions via the IDT.
     */
    pTss = &Bs3Tss64;
    Bs3MemZero(pTss, sizeof(*pTss));
    pTss->rsp0      = BS3_ADDR_STACK_R0;
    pTss->rsp1      = BS3_ADDR_STACK_R1;
    pTss->rsp2      = BS3_ADDR_STACK_R2;
    pTss->ist1      = BS3_ADDR_STACK_R0_IST1;
    pTss->ist2      = BS3_ADDR_STACK_R0_IST2;
    pTss->ist3      = BS3_ADDR_STACK_R0_IST3;
    pTss->ist4      = BS3_ADDR_STACK_R0_IST4;
    pTss->ist5      = BS3_ADDR_STACK_R0_IST5;
    pTss->ist6      = BS3_ADDR_STACK_R0_IST6;
    pTss->ist7      = BS3_ADDR_STACK_R0_IST7;
}

