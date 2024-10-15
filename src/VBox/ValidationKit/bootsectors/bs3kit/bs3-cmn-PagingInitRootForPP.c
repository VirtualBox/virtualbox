/* $Id: bs3-cmn-PagingInitRootForPP.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3PagingInitRootForPP
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
#include "bs3-cmn-memory.h" /* bad bird */
#include <iprt/param.h>


/**
 * Creates page tables for a section of the page directory.
 *
 * @returns VINF_SUCCESS or VERR_NO_MEMORY.
 * @param   pPgDir              The page directory.
 * @param   iFirst              The first PD entry.
 * @param   cEntries            How many PD entries to create pages tables for.
 */
static int Bs3PagingInitPageTablesForPgDir(X86PD BS3_FAR *pPgDir, unsigned iFirst, unsigned cEntries)
{
    uint32_t uCurPhys = (uint32_t)iFirst << X86_PD_SHIFT;

    while (cEntries--)
    {
        X86PT BS3_FAR *pPt = (X86PT BS3_FAR *)Bs3MemAlloc(BS3MEMKIND_TILED, _4K);
        if (pPt)
        {
            unsigned j = 0;
            for (j = 0; j < RT_ELEMENTS(pPt->a); j++, uCurPhys += PAGE_SIZE)
            {
                pPt->a[j].u  = uCurPhys;
                pPt->a[j].u |= X86_PTE_P | X86_PTE_RW | X86_PTE_US | X86_PTE_A | X86_PTE_D;
            }
            pPgDir->a[iFirst].u = Bs3SelPtrToFlat(pPt);
            pPgDir->a[iFirst].u |= X86_PDE_P | X86_PDE_RW | X86_PDE_US | X86_PDE_A;
            iFirst++;
        }
        else
            return VERR_NO_MEMORY;
    }
    return VINF_SUCCESS;
}


#undef Bs3PagingInitRootForPP
BS3_CMN_DEF(int, Bs3PagingInitRootForPP,(void))
{
    X86PD BS3_FAR *pPgDir;

    BS3_ASSERT(g_PhysPagingRootPP == UINT32_MAX);


    /*
     * By default we do a identity mapping of the entire address space
     * using 4 GB pages.  So, we only really need one page directory,
     * that's all.
     *
     * ASSUMES page size extension available, i.e. pentium+.
     */
    pPgDir = (X86PD BS3_FAR *)Bs3MemAllocZ(BS3MEMKIND_TILED, _4K);
    if (pPgDir)
    {
        BS3_XPTR_AUTO(X86PD, XptrPgDir);
        unsigned i;
        int rc = VINF_SUCCESS;

        if (g_uBs3CpuDetected & BS3CPU_F_PSE)
        {
            for (i = 0; i < RT_ELEMENTS(pPgDir->a); i++)
            {
                pPgDir->a[i].u = (uint32_t)i << X86_PD_SHIFT;
                pPgDir->a[i].u |= X86_PDE4M_P | X86_PDE4M_RW | X86_PDE4M_US | X86_PDE4M_PS | X86_PDE4M_A | X86_PDE4M_D;
            }

            /* Free up 4 consequtive entries for raw-mode hypervisor code. */
            if (1) /** @todo detect raw-mode and only do this then. */
                for (i = 0; i < 4; i++)
                    pPgDir->a[i + (UINT32_C(0xc0000000) >> X86_PD_SHIFT)].b.u1Present = 0;
        }
        else
        {
            /*
             * This requires 4MB of page tables if we map everything.
             * So, we check how much memory we have available and make sure we
             * don't use all of it for page tables.
             */
            unsigned cMax = RT_ELEMENTS(pPgDir->a);
            uint32_t cFreePages = g_Bs3Mem4KUpperTiled.Core.cFreeChunks + g_Bs3Mem4KLow.Core.cFreeChunks;
            if (cFreePages >= cMax + 128)
                Bs3PagingInitPageTablesForPgDir(pPgDir, 0, cMax);
            else
            {
                unsigned cTop;
                if (cMax >= 256 /*1MB*/)
                {
                    cMax = cFreePages - 128;
                    cTop = 32;
                }
                else if (cMax >= 128)
                {
                    cMax = cFreePages - 48;
                    cTop = 16;
                }
                else
                {
                    cMax = cFreePages - 16;
                    cTop = RT_MIN(16, cMax / 4);
                }
                Bs3TestPrintf("Bs3PagingInitRootForPP: Warning! insufficient memory for mapping all 4GB!\n"
                              "    Will only map 0x00000000-%#010RX32 and %#010RX32-0xffffffff.\n",
                              (uint32_t)(cMax - cTop) << PAGE_SHIFT, UINT32_MAX - ((uint32_t)cTop << PAGE_SHIFT) + 1);
                rc = Bs3PagingInitPageTablesForPgDir(pPgDir, 0, cMax - cTop);
                if (RT_SUCCESS(rc))
                    rc = Bs3PagingInitPageTablesForPgDir(pPgDir, RT_ELEMENTS(pPgDir->a) - cTop, cTop);
            }
        }

        BS3_XPTR_SET(X86PD, XptrPgDir, pPgDir);
        g_PhysPagingRootPP = BS3_XPTR_GET_FLAT(X86PD, XptrPgDir);
        return rc;
    }

    Bs3Printf("Bs3PagingInitRootForPP: No memory!\n");
    BS3_ASSERT(false);
    return VERR_NO_MEMORY;
}

