/* $Id: bs3-cmn-RegCtxPrint.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3RegCtxPrint
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


#undef Bs3RegCtxPrint
BS3_CMN_DEF(void, Bs3RegCtxPrint,(PCBS3REGCTX pRegCtx))
{
    if (!BS3_MODE_IS_64BIT_CODE(pRegCtx->bMode))
    {
        Bs3TestPrintf("eax=%08RX32 ebx=%08RX32 ecx=%08RX32 edx=%08RX32 esi=%08RX32 edi=%08RX32\n",
                      pRegCtx->rax.u32, pRegCtx->rbx.u32, pRegCtx->rcx.u32, pRegCtx->rdx.u32, pRegCtx->rsi.u32, pRegCtx->rdi.u32);
        Bs3TestPrintf("eip=%08RX32 esp=%08RX32 ebp=%08RX32 efl=%08RX32 cr0=%08RX32 cr2=%08RX32\n",
                      pRegCtx->rip.u32, pRegCtx->rsp.u32, pRegCtx->rbp.u32, pRegCtx->rflags.u32,
                      pRegCtx->cr0.u32, pRegCtx->cr2.u32);
        Bs3TestPrintf("cs=%04RX16   ds=%04RX16 es=%04RX16 fs=%04RX16 gs=%04RX16   ss=%04RX16 cr3=%08RX32 cr4=%08RX32\n",
                      pRegCtx->cs, pRegCtx->ds, pRegCtx->es, pRegCtx->fs, pRegCtx->gs, pRegCtx->ss,
                      pRegCtx->cr3.u32, pRegCtx->cr4.u32);
    }
    else
    {
        Bs3TestPrintf("rax=%016RX64 rbx=%016RX64 rcx=%016RX64 rdx=%016RX64\n",
                      pRegCtx->rax.u64, pRegCtx->rbx.u64, pRegCtx->rcx.u64, pRegCtx->rdx.u64);
        Bs3TestPrintf("rsi=%016RX64 rdi=%016RX64 r8 =%016RX64 r9 =%016RX64\n",
                      pRegCtx->rsi.u64, pRegCtx->rdi.u64, pRegCtx->r8.u64, pRegCtx->r9.u64);
        Bs3TestPrintf("r10=%016RX64 r11=%016RX64 r12=%016RX64 r13=%016RX64\n",
                      pRegCtx->r10.u64, pRegCtx->r11.u64, pRegCtx->r12.u64, pRegCtx->r13.u64);
        Bs3TestPrintf("r14=%016RX64 r15=%016RX64  cr0=%08RX64  cr4=%08RX64  cr3=%08RX64\n",
                      pRegCtx->r14.u64, pRegCtx->r15.u64, pRegCtx->cr0.u64, pRegCtx->cr4.u64, pRegCtx->cr3.u64);
        Bs3TestPrintf("rip=%016RX64 rsp=%016RX64 rbp=%016RX64 rfl=%08RX64\n",
                      pRegCtx->rip.u64, pRegCtx->rsp.u64, pRegCtx->rbp.u64, pRegCtx->rflags.u32);
        Bs3TestPrintf("cs=%04RX16   ds=%04RX16 es=%04RX16 fs=%04RX16 gs=%04RX16   ss=%04RX16            cr2=%016RX64\n",
                      pRegCtx->cs, pRegCtx->ds, pRegCtx->es, pRegCtx->fs, pRegCtx->gs, pRegCtx->ss,
                      pRegCtx->cr3.u64, pRegCtx->cr2.u64);
    }
    Bs3TestPrintf("tr=%04RX16 ldtr=%04RX16 cpl=%d   mode=%#x fbFlags=%#x\n",
                  pRegCtx->tr, pRegCtx->ldtr, pRegCtx->bCpl, pRegCtx->bMode, pRegCtx->fbFlags);
}

