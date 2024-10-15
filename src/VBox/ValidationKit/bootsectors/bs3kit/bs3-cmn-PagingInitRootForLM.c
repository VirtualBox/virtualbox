/* $Id: bs3-cmn-PagingInitRootForLM.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3PagingInitRootForLM
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


#undef Bs3PagingInitRootForLM
BS3_CMN_DEF(int, Bs3PagingInitRootForLM,(void))
{
    X86PML4 BS3_FAR *pPml4;

    BS3_ASSERT(g_PhysPagingRootLM == UINT32_MAX);

    /*
     * The default is an identity mapping of the first 4GB repeated for the
     * whole 48-bit virtual address space.  So, we need one level more than PAE.
     */
    pPml4 = (X86PML4 BS3_FAR *)Bs3MemAlloc(BS3MEMKIND_TILED, _4K);
    if (pPml4)
    {
        X86PDPT BS3_FAR *pPdPtr = (X86PDPT BS3_FAR *)Bs3MemAlloc(BS3MEMKIND_TILED, _4K);
        BS3_ASSERT((uintptr_t)pPdPtr != (uintptr_t)pPml4);
        if (pPdPtr)
        {
            X86PDPAE BS3_FAR *paPgDirs = (X86PDPAE BS3_FAR *)Bs3MemAlloc(BS3MEMKIND_TILED, _4K * 4U);
            BS3_ASSERT((uintptr_t)paPgDirs != (uintptr_t)pPml4);
            if (paPgDirs)
            {
                unsigned i;
                BS3_XPTR_AUTO(X86PML4,  XPtrPml4);
                BS3_XPTR_AUTO(X86PDPT,  XPtrPdPtr);
                BS3_XPTR_AUTO(X86PDPAE, XPtrPgDirs);

                /* Set up the 2048 2MB pages first. */
                for (i = 0; i < RT_ELEMENTS(paPgDirs->a) * 4U; i++)
                    paPgDirs->a[i].u = ((uint32_t)i << X86_PD_PAE_SHIFT)
                                     | X86_PDE4M_P | X86_PDE4M_RW | X86_PDE4M_US | X86_PDE4M_PS | X86_PDE4M_A | X86_PDE4M_D;

                /* Set up the page directory pointer table next (4GB replicated, remember). */
                BS3_XPTR_SET(X86PDPAE, XPtrPgDirs, paPgDirs);
                pPdPtr->a[0].u = BS3_XPTR_GET_FLAT(X86PDPAE, XPtrPgDirs)
                               | X86_PDPE_P | X86_PDPE_RW | X86_PDPE_US | X86_PDPE_A;
                pPdPtr->a[1].u = pPdPtr->a[0].u + _4K;
                pPdPtr->a[2].u = pPdPtr->a[1].u + _4K;
                pPdPtr->a[3].u = pPdPtr->a[2].u + _4K;

                for (i = 4; i < RT_ELEMENTS(pPdPtr->a); i += 4)
                {
                    pPdPtr->a[i + 0].u = pPdPtr->a[0].u;
                    pPdPtr->a[i + 1].u = pPdPtr->a[1].u;
                    pPdPtr->a[i + 2].u = pPdPtr->a[2].u;
                    pPdPtr->a[i + 3].u = pPdPtr->a[3].u;
                }

                /* Set up the page map level 4 (all entries are the same). */
                BS3_XPTR_SET(X86PDPT, XPtrPdPtr, pPdPtr);
                pPml4->a[0].u = BS3_XPTR_GET_FLAT(X86PDPT, XPtrPdPtr)
                              | X86_PML4E_P | X86_PML4E_RW | X86_PML4E_US | X86_PML4E_A;
                for (i = 1; i < RT_ELEMENTS(pPml4->a); i++)
                    pPml4->a[i].u = pPml4->a[0].u;

                /* Set the global root pointer and we're done. */
                BS3_XPTR_SET(X86PML4, XPtrPml4, pPml4);
                g_PhysPagingRootLM = BS3_XPTR_GET_FLAT(X86PML4, XPtrPml4);
                return VINF_SUCCESS;
            }

            BS3_ASSERT(false);
            Bs3MemFree(pPdPtr, _4K);
        }
        BS3_ASSERT(false);
        Bs3MemFree(pPml4, _4K);
    }
    BS3_ASSERT(false);
    return VERR_NO_MEMORY;
}

