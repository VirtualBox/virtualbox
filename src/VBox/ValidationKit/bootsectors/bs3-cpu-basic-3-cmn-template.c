/* $Id: bs3-cpu-basic-3-cmn-template.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - bs3-cpu-basic-3, C code template, common code (CMN).
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
*   Assembly Symbols                                                                                                             *
*********************************************************************************************************************************/
#if ARCH_BITS != 64
extern BS3_DECL_FAR(void) BS3_CMN_FAR_NM(bs3CpuBasic3_lea_16)(void);
extern BS3_DECL_FAR(void) BS3_CMN_FAR_NM(bs3CpuBasic3_lea_32)(void);
#else
extern BS3_DECL_FAR(void) BS3_CMN_FAR_NM(bs3CpuBasic3_lea_64)(void);
#endif



BS3_DECL_FAR(uint8_t) BS3_CMN_NM(bs3CpuBasic3_Lea)(uint8_t bMode)
{
    /* Repeat the test so the native recompiler get a chance to kick in...  */
    unsigned i;
    RT_NOREF(bMode);

#if ARCH_BITS != 64
    {
        FPFNBS3FAR pfnWorker16 = Bs3SelLnkCodePtrToCurPtr(BS3_CMN_FAR_NM(bs3CpuBasic3_lea_16));
        for (i = 0; i < 64; i++)
            pfnWorker16();
    }
    {
        FPFNBS3FAR pfnWorker32 = Bs3SelLnkCodePtrToCurPtr(BS3_CMN_FAR_NM(bs3CpuBasic3_lea_32));
        for (i = 0; i < 64; i++)
            pfnWorker32();
    }
#else
    {
        /* The worker for this is in the high DLL, so have to get the address. */
        extern FPFNBS3FAR BS3_CMN_NM(g_pfnbs3CpuBasic3_lea_64);
        for (i = 0; i < 64; i++)
            BS3_CMN_NM(g_pfnbs3CpuBasic3_lea_64)();
    }
#endif
    return 0;
}

