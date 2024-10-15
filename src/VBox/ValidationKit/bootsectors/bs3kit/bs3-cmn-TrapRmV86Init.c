/* $Id: bs3-cmn-TrapRmV86Init.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3TrapRmV86Init
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
extern BS3_DECL(void) BS3_FAR_CODE Bs3TrapRmV86GenericEntries(void);

/* These two are ugly.  Need data access for patching purposes. */
extern uint8_t  BS3_FAR_DATA bs3TrapRmV86GenericTrapOrInt[];

/* bs3-cmn-TrapRmV86Data.c: */
#define g_fBs3RmIvtCopied   BS3_DATA_NM(g_fBs3RmIvtCopied)
extern bool    g_fBs3RmIvtCopied;


#undef Bs3TrapRmV86InitEx
BS3_CMN_DEF(void, Bs3TrapRmV86InitEx,(bool f386Plus))
{
    RTFAR16 BS3_FAR *paIvt = Bs3XptrFlatToCurrent(0);
    unsigned iIvt;

    /*
     * Copy the real mode IVT the first time we are here.
     */
    if (!g_fBs3RmIvtCopied)
    {
        Bs3MemCpy(g_aBs3RmIvtOriginal, paIvt, sizeof(g_aBs3RmIvtOriginal));
        g_fBs3RmIvtCopied = true;
    }
    /*
     * The rest of the times, we copy back the original and modify it.
     */
    else
        Bs3MemCpy(paIvt, g_aBs3RmIvtOriginal, sizeof(g_aBs3RmIvtOriginal));


    /*
     * If 386 or later, patch the trap handler code to not jump to the 80286
     * code but continue with the next instruction (the 386+ code).
     */
    if (f386Plus)
    {
        uint8_t BS3_FAR_DATA *pbFunction = &bs3TrapRmV86GenericTrapOrInt[0];
#if ARCH_BITS == 16
        if (g_bBs3CurrentMode != BS3_MODE_RM)
            pbFunction = (uint8_t BS3_FAR_DATA *)BS3_FP_MAKE(BS3_SEL_TILED + 1, BS3_FP_OFF(pbFunction));
#endif
        pbFunction[1] = 0;
        pbFunction[2] = 0;
    }

    /*
     * Since we want to play with V86 mode as well as 8086 and 186 CPUs, we
     * cannot move the IVT from its default location.  So, modify it in place.
     *
     * Note! We must keep INT 10h working, which is easy since the CPU does
     *       use it (well, it's been reserved for 30+ years).
     *       Turns out we must not hook INT 6Dh either then, as some real VGA
     *       BIOS installs their INT 10h handler there as well, and seemingly
     *       must be using it internally or something.
     *
     *       We also keep 15h working for memory interfaces (see bs3-mode-BiosInt15*).
     */
    for (iIvt = 0; iIvt < 256; iIvt++)
        if (iIvt != 0x10 && iIvt != 0x15 && iIvt != 0x6d && iIvt != BS3_TRAP_SYSCALL)
        {
            paIvt[iIvt].off = (uint16_t)(uintptr_t)Bs3TrapRmV86GenericEntries + iIvt * 8;
            paIvt[iIvt].sel = BS3_SEL_TEXT16;
        }
}


#undef Bs3TrapRmV86Init
BS3_CMN_DEF(void, Bs3TrapRmV86Init,(void))
{
    BS3_CMN_NM(Bs3TrapRmV86InitEx)((g_uBs3CpuDetected & BS3CPU_TYPE_MASK) >= BS3CPU_80386);
}

