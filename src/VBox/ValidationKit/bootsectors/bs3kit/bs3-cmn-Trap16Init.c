/* $Id: bs3-cmn-Trap16Init.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3Trap16Init
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
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/* We ASSUME that BS3CLASS16CODE is 64KB aligned, so the low 16-bit of the
   flat address matches.   Also, these symbols are defined both with
   and without underscore prefixes. */
extern BS3_DECL(void) BS3_FAR_CODE Bs3Trap16DoubleFaultHandler80386(void);
extern BS3_DECL(void) BS3_FAR_CODE Bs3Trap16DoubleFaultHandler80286(void);
extern BS3_DECL(void) BS3_FAR_CODE Bs3Trap16GenericEntries(void);

/* These two are ugly.  Need data access for patching purposes. */
extern uint8_t  BS3_FAR_DATA bs3Trap16GenericTrapOrInt[];


#undef Bs3Trap16InitEx
BS3_CMN_DEF(void, Bs3Trap16InitEx,(bool f386Plus))
{
    X86TSS16 BS3_FAR *pTss;
    unsigned iIdt;

    /*
     * If 386 or later, patch the trap handler code to not jump to the 80286
     * code but continue with the next instruction (the 386+ code).
     */
    if (f386Plus)
    {
        uint8_t BS3_FAR_DATA *pbFunction = &bs3Trap16GenericTrapOrInt[0];
#if ARCH_BITS == 16
        if (g_bBs3CurrentMode != BS3_MODE_RM)
            pbFunction = (uint8_t BS3_FAR_DATA *)BS3_FP_MAKE(BS3_SEL_TILED + 1, BS3_FP_OFF(pbFunction));
#endif
        pbFunction[1] = 0;
        pbFunction[2] = 0;
    }

    /*
     * IDT entries, except the system call gate.
     */
    for (iIdt = 0; iIdt < 256; iIdt++)
        if (iIdt != BS3_TRAP_SYSCALL)
            Bs3Trap16SetGate(iIdt, X86_SEL_TYPE_SYS_286_INT_GATE, 0 /*bDpl*/,
                             BS3_SEL_R0_CS16, (uint16_t)(uintptr_t)Bs3Trap16GenericEntries + iIdt * 8, 0 /*cParams*/);

    /*
     * Initialize the normal TSS so we can do ring transitions via the IDT.
     */
    pTss = &Bs3Tss16;
    Bs3MemZero(pTss, sizeof(*pTss));
    pTss->sp0       = BS3_ADDR_STACK_R0;
    pTss->ss0       = BS3_SEL_R0_SS16;
    pTss->sp1       = BS3_ADDR_STACK_R1;
    pTss->ss1       = BS3_SEL_R1_SS16 | 1;
    pTss->sp2       = BS3_ADDR_STACK_R2;
    pTss->ss2       = BS3_SEL_R2_SS16 | 2;

    /*
     * Initialize the double fault TSS.
     * cr3 is filled in by switcher code, when needed.
     */
    pTss = &Bs3Tss16DoubleFault;
    Bs3MemZero(pTss, sizeof(*pTss));
    pTss->sp0       = BS3_ADDR_STACK_R0;
    pTss->ss0       = BS3_SEL_R0_SS16;
    pTss->sp1       = BS3_ADDR_STACK_R1;
    pTss->ss1       = BS3_SEL_R1_SS16 | 1;
    pTss->sp2       = BS3_ADDR_STACK_R2;
    pTss->ss2       = BS3_SEL_R2_SS16 | 2;
    pTss->ip        = (uint16_t)(uintptr_t)(f386Plus ? &Bs3Trap16DoubleFaultHandler80386 : &Bs3Trap16DoubleFaultHandler80286);
    pTss->flags     = X86_EFL_1;
    pTss->sp        = BS3_ADDR_STACK_R0_IST1;
    pTss->es        = BS3_SEL_R0_DS16;
    pTss->ds        = BS3_SEL_R0_DS16;
    pTss->cs        = BS3_SEL_R0_CS16;
    pTss->ss        = BS3_SEL_R0_SS16;
    pTss->dx        = f386Plus;

    Bs3Trap16SetGate(X86_XCPT_DF, X86_SEL_TYPE_SYS_TASK_GATE, 0 /*bDpl*/, BS3_SEL_TSS16_DF, 0, 0 /*cParams*/);
}


#undef Bs3Trap16Init
BS3_CMN_DEF(void, Bs3Trap16Init,(void))
{
    BS3_CMN_NM(Bs3Trap16InitEx)((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_80386);
}

