/* $Id: bs3-cmn-PagingMapRamAbove4GForLM.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3PagingInitMapAbove4GForLM
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

#if ARCH_BITS == 16
# error "This does not work in 16-bit mode, only 32-bit and 64-bit"
#endif

#undef Bs3PagingMapRamAbove4GForLM
BS3_CMN_DEF(int, Bs3PagingMapRamAbove4GForLM,(uint64_t *puFailurePoint))
{
    X86PML4 * const pPml4 = (X86PML4 *)((uintptr_t)g_PhysPagingRootLM);
    unsigned        iPml4 = 0;
    unsigned        iPdpt = 4;
    uint64_t        uAddr = _4G;
    X86PDPT *       pPdpt;

    if (puFailurePoint)
        *puFailurePoint = 0;

    /* Must call Bs3PagingInitRootForLM first! */
    if (g_PhysPagingRootLM == UINT32_MAX)
        return VERR_WRONG_ORDER;

    /* Done already? */
    if (pPml4->a[0].u != pPml4->a[4].u)
        return VINF_ALREADY_INITIALIZED;

    /*
     * Map RAM pages up to g_uBs3EndOfRamAbove4G.
     */
    pPdpt = (X86PDPT *)(pPml4->a[0].u & X86_PML4E_PG_MASK);
    while (uAddr < g_uBs3EndOfRamAbove4G)
    {
        X86PDPAE *pPd;
        unsigned  i;

        /* Do we need a new PDPT? */
        if (iPdpt >= RT_ELEMENTS(pPdpt->a))
        {
            if (iPml4 >= RT_ELEMENTS(pPml4->a) / 2)
            {
                if (puFailurePoint)
                    *puFailurePoint = uAddr;
                return VERR_OUT_OF_RANGE;
            }
            pPdpt = (X86PDPT *)Bs3MemAllocZ(BS3MEMKIND_FLAT32, X86_PAGE_SIZE);
            if (!pPdpt || ((uintptr_t)pPdpt & X86_PAGE_OFFSET_MASK))
            {
                if (puFailurePoint)
                    *puFailurePoint = uAddr;
                return !pPdpt ? VERR_NO_MEMORY : VERR_UNSUPPORTED_ALIGNMENT;
            }
            pPml4->a[++iPml4].u = X86_PML4E_P | X86_PML4E_RW | X86_PML4E_US | X86_PML4E_A
                                | (uintptr_t)pPdpt;
            iPdpt = 0;
        }

        /* Allocate a new page directory. */
        pPd = (X86PDPAE *)Bs3MemAlloc(BS3MEMKIND_FLAT32, X86_PAGE_SIZE);
        if (!pPd || ((uintptr_t)pPd & X86_PAGE_OFFSET_MASK))
        {
            if (puFailurePoint)
                *puFailurePoint = uAddr;
            return !pPd ? VERR_NO_MEMORY : VERR_UNSUPPORTED_ALIGNMENT;
        }

        /* Initialize it. */
        for (i = 0; i < RT_ELEMENTS(pPd->a); i++)
        {
            pPd->a[i].u = uAddr | X86_PDE4M_P | X86_PDE4M_RW | X86_PDE4M_US | X86_PDE4M_PS | X86_PDE4M_A | X86_PDE4M_D;
            uAddr += _2M;
        }

        /* Insert it into the page directory pointer table. */
        pPdpt->a[iPdpt++].u = (uintptr_t)pPd | X86_PDPE_P | X86_PDPE_RW | X86_PDPE_US | X86_PDPE_A;
    }

    return VINF_SUCCESS;
}

