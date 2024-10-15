/* $Id: bs3-cmn-PagingInitRootForPAE.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3PagingInitRootForPAE
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


#undef Bs3PagingInitRootForPAE
BS3_CMN_DEF(int, Bs3PagingInitRootForPAE,(void))
{
    X86PDPT BS3_FAR *pPdPtr;

    BS3_ASSERT(g_PhysPagingRootPAE == UINT32_MAX);

    /*
     * By default we do a identity mapping of the entire address space
     * using 2 GB pages.  So, we need four page directories and one page
     * directory pointer table with 4 entries.  (We cannot share the PDPT with
     * long mode because of reserved bit which will cause fatal trouble.)
     *
     * We assume that the availability of PAE means that PSE is available too.
     */
/** @todo testcase: loading invalid PDPTREs will tripple fault the CPU, won't it? We guru with invalid guest state. */
    pPdPtr = (X86PDPT BS3_FAR *)Bs3MemAlloc(BS3MEMKIND_TILED, sizeof(X86PDPE) * 4U);
    if (pPdPtr)
    {
        X86PDPAE BS3_FAR *paPgDirs;
        BS3_ASSERT(((uintptr_t)pPdPtr & 0x3f) == 0);

        paPgDirs = (X86PDPAE BS3_FAR *)Bs3MemAlloc(BS3MEMKIND_TILED, _4K * 4U);
        if (paPgDirs)
        {
            unsigned i;
            BS3_XPTR_AUTO(X86PDPT, XPtrPdPtr);
            BS3_XPTR_AUTO(X86PDPAE, XPtrPgDirs);

            /* Set up the 2048 2MB pages first. */
            for (i = 0; i < RT_ELEMENTS(paPgDirs->a) * 4U; i++)
                paPgDirs->a[i].u = ((uint32_t)i << X86_PD_PAE_SHIFT)
                                 | X86_PDE4M_P | X86_PDE4M_RW | X86_PDE4M_US | X86_PDE4M_PS | X86_PDE4M_A | X86_PDE4M_D;

            /* Set up the four page directory pointer table entries. */
            BS3_XPTR_SET(X86PDPAE, XPtrPgDirs, paPgDirs);
            pPdPtr->a[0].u = BS3_XPTR_GET_FLAT(X86PDPAE, XPtrPgDirs) | X86_PDPE_P;
            pPdPtr->a[1].u = pPdPtr->a[0].u + _4K;
            pPdPtr->a[2].u = pPdPtr->a[1].u + _4K;
            pPdPtr->a[3].u = pPdPtr->a[2].u + _4K;

            /* Free up 8 consequtive entries for raw-mode hypervisor code. */
            if (1) /** @todo detect raw-mode and only do this then. */
                for (i = 0; i < 8; i++)
                    paPgDirs->a[i + (UINT32_C(0xc0000000) >> X86_PD_PAE_SHIFT)].b.u1Present = 0;

            /* Set the global root pointer and we're done. */
            BS3_XPTR_SET(X86PDPT, XPtrPdPtr, pPdPtr);
            g_PhysPagingRootPAE = BS3_XPTR_GET_FLAT(X86PDPT, XPtrPdPtr);
            return VINF_SUCCESS;
        }
        BS3_ASSERT(false);
        Bs3MemFree(pPdPtr, _4K);
    }
    BS3_ASSERT(false);
    return VERR_NO_MEMORY;
}

