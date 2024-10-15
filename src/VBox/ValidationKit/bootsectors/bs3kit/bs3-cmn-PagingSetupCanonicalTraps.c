/* $Id: bs3-cmn-PagingSetupCanonicalTraps.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3PagingSetupCanonicalTraps
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
#include "bs3-cmn-paging.h"
#include "iprt/asm-amd64-x86.h"


#undef Bs3PagingSetupCanonicalTraps
BS3_CMN_PROTO_STUB(void BS3_FAR *, Bs3PagingSetupCanonicalTraps,(void))
{
    if (g_uBs3CpuDetected & BS3CPU_F_LONG_MODE)
    {
#if ARCH_BITS == 16
        if (!BS3_MODE_IS_RM_OR_V86(g_bBs3CurrentMode))
#endif
        {
            uint8_t BS3_FAR   *pb;
            X86PTEPAE BS3_FAR *paLoPtes;
            X86PTEPAE BS3_FAR *paHiPtes;
            int                rc;

            /* Already initialized? Likely. */
            if (g_cbBs3PagingCanonicalTraps != 0)
                return Bs3XptrFlatToCurrent(g_uBs3PagingCanonicalTrapsAddr);

            /* Initialize AMD64 page tables if necessary (unlikely). */
            if (g_PhysPagingRootLM == UINT32_MAX)
            {
                rc = Bs3PagingInitRootForLM();
                if (RT_FAILURE(rc))
                    return NULL;
            }

            /*
             * Get the page table entries first to avoid having to unmap things.
             */
            paLoPtes = bs3PagingGetPaePte(g_PhysPagingRootLM, BS3_MODE_LM64, UINT64_C(0x00007fffffffe000), false, &rc);
            paHiPtes = bs3PagingGetPaePte(g_PhysPagingRootLM, BS3_MODE_LM64, UINT64_C(0xffff800000000000), false, &rc);
            if (!paHiPtes || !paLoPtes)
            {
                Bs3TestPrintf("warning: Bs3PagingSetupCanonicalTraps - failed to get PTEs!\n");
                return NULL;
            }

            /*
             * Allocate the buffer. Currently using 8KB on each side.
             */
            pb = (uint8_t BS3_FAR *)Bs3MemAlloc(BS3MEMKIND_TILED, X86_PAGE_SIZE * 4);
            if (pb)
            {
                RTCCUINTXREG uFlat = Bs3SelPtrToFlat(pb);

                /*
                 * Inject it into the page tables.
                 */
                paLoPtes[0].u &= ~X86_PTE_PAE_PG_MASK;
                paLoPtes[0].u |= uFlat + X86_PAGE_SIZE * 0;
                paLoPtes[1].u &= ~X86_PTE_PAE_PG_MASK;
                paLoPtes[1].u |= uFlat + X86_PAGE_SIZE * 1;

                paHiPtes[0].u &= ~X86_PTE_PAE_PG_MASK;
                paHiPtes[0].u |= uFlat + X86_PAGE_SIZE * 2;
                paHiPtes[1].u &= ~X86_PTE_PAE_PG_MASK;
                paHiPtes[1].u |= uFlat + X86_PAGE_SIZE * 3;
                ASMReloadCR3();

                /*
                 * Update globals and return successfully.
                 */
                g_uBs3PagingCanonicalTrapsAddr = uFlat;
                g_cbBs3PagingCanonicalTraps    = X86_PAGE_SIZE * 4;
                g_cbBs3PagingOneCanonicalTrap  = X86_PAGE_SIZE * 2;
                return pb;
            }

            Bs3TestPrintf("warning: Bs3PagingSetupCanonicalTraps - out of memory (mode %#x)\n", g_bBs3CurrentMode);
        }
#if ARCH_BITS == 16
        else
            Bs3TestPrintf("warning: Bs3PagingSetupCanonicalTraps was called in RM or V86 mode (%#x)!\n", g_bBs3CurrentMode);
#endif
    }
    return NULL;
}

