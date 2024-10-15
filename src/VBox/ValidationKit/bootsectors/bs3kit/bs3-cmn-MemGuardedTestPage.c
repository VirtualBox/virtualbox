/* $Id: bs3-cmn-MemGuardedTestPage.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3MemGuardedTestPageAlloc, Bs3MemGuardedTestPageFree
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
#include "iprt/asm-mem.h"


#undef Bs3MemGuardedTestPageAllocEx
BS3_CMN_PROTO_STUB(void BS3_FAR *, Bs3MemGuardedTestPageAllocEx,(BS3MEMKIND enmKind, uint64_t fPte))
{
    uint8_t BS3_FAR *pb = (uint8_t BS3_FAR *)Bs3MemAlloc(enmKind, X86_PAGE_4K_SIZE * 3);
    if (pb)
    {
        int rc;

        Bs3MemSet(pb, 0xcc, X86_PAGE_4K_SIZE);
        Bs3MemSet(&pb[X86_PAGE_4K_SIZE], 0x00, X86_PAGE_4K_SIZE);
        Bs3MemSet(&pb[X86_PAGE_4K_SIZE*2], 0xaa, X86_PAGE_4K_SIZE);

        rc = Bs3PagingProtectPtr(pb, X86_PAGE_4K_SIZE, fPte, UINT64_MAX & ~fPte);
        if (RT_SUCCESS(rc))
        {
            rc = Bs3PagingProtectPtr(&pb[X86_PAGE_4K_SIZE*2], X86_PAGE_4K_SIZE, fPte, UINT64_MAX & ~fPte);
            if (RT_SUCCESS(rc))
                return pb + X86_PAGE_4K_SIZE;

            Bs3TestPrintf("warning: Bs3MemGuardedTestPageAlloc - Tail protect error %d (mode %#x)\n", rc, g_bBs3CurrentMode);
            Bs3PagingProtectPtr(pb, X86_PAGE_4K_SIZE, X86_PTE_P, 0);
        }
        else
            Bs3TestPrintf("warning: Bs3MemGuardedTestPageAlloc - Head protect error %d (mode %#x)\n", rc, g_bBs3CurrentMode);
        Bs3MemFree(pb, X86_PAGE_4K_SIZE * 3);
    }
    else
        Bs3TestPrintf("warning: Bs3MemGuardedTestPageAlloc - out of memory (mode %#x)\n", g_bBs3CurrentMode);
    return NULL;
}


#undef Bs3MemGuardedTestPageAlloc
BS3_CMN_DEF(void BS3_FAR *, Bs3MemGuardedTestPageAlloc,(BS3MEMKIND enmKind))
{
    return BS3_CMN_FAR_NM(Bs3MemGuardedTestPageAllocEx)(enmKind, 0);
}


#undef Bs3MemGuardedTestPageFree
BS3_CMN_DEF(void, Bs3MemGuardedTestPageFree,(void BS3_FAR *pvGuardedPage))
{
    if (pvGuardedPage)
    {
        uint8_t BS3_FAR *pbGuardViolation;
        uint8_t BS3_FAR *pb = (uint8_t BS3_FAR *)pvGuardedPage - X86_PAGE_4K_SIZE;
        Bs3PagingProtectPtr(pb, X86_PAGE_4K_SIZE,
                            X86_PTE_P | X86_PTE_RW | X86_PTE_US | X86_PTE_A | X86_PTE_D, UINT64_MAX);
        Bs3PagingProtectPtr(&pb[X86_PAGE_4K_SIZE*2], X86_PAGE_4K_SIZE,
                            X86_PTE_P | X86_PTE_RW | X86_PTE_US | X86_PTE_A | X86_PTE_D, UINT64_MAX);

        pbGuardViolation = ASMMemFirstMismatchingU8(pb, X86_PAGE_4K_SIZE, 0xcc);
        if (pbGuardViolation)
            Bs3TestFailedF("Leading guard page touched: byte %#05x is %#04x instead of 0xcc\n",
                           (unsigned)(uintptr_t)(pbGuardViolation - pb), *pbGuardViolation);

        pbGuardViolation = ASMMemFirstMismatchingU8(&pb[X86_PAGE_4K_SIZE*2], X86_PAGE_4K_SIZE, 0xaa);
        if (pbGuardViolation)
            Bs3TestFailedF("Trailing guard page touched: byte %#05x is %#04x instead of 0xaa\n",
                           (unsigned)(uintptr_t)(pbGuardViolation - &pb[X86_PAGE_4K_SIZE*2]), *pbGuardViolation);

        Bs3MemFree(pb, X86_PAGE_4K_SIZE * 3);
    }
}

