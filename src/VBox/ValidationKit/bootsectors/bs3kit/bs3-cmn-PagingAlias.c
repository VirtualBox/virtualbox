/* $Id: bs3-cmn-PagingAlias.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3PagingAlias, Bs3PagingUnalias
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


#undef Bs3PagingAlias
BS3_CMN_DEF(int, Bs3PagingAlias,(uint64_t uDst, uint64_t uPhysToAlias, uint32_t cbHowMuch, uint64_t fPte))
{
#if ARCH_BITS == 16
    if (!BS3_MODE_IS_V86(g_bBs3CurrentMode))
#endif
    {
        RTCCUINTXREG    cr3 = ASMGetCR3();
        uint32_t        cPages;
        int             rc;

        /*
         * Validate and adjust the input a little.
         */
        if (uDst & X86_PAGE_OFFSET_MASK)
        {
            cbHowMuch += X86_PAGE_SIZE - (uDst & X86_PAGE_OFFSET_MASK);
            uDst      &= ~(uint64_t)X86_PAGE_OFFSET_MASK;
        }
        uPhysToAlias &= X86_PTE_PAE_PG_MASK;
        fPte         &= ~(X86_PTE_PAE_MBZ_MASK_NX | X86_PTE_PAE_PG_MASK);
        cbHowMuch     = RT_ALIGN_32(cbHowMuch, X86_PAGE_SIZE);
        cPages        = cbHowMuch >> X86_PAGE_SHIFT;
        //Bs3TestPrintf("Bs3PagingAlias: adjusted: uDst=%RX64 uPhysToAlias=%RX64 cbHowMuch=%RX32 fPte=%Rx64 cPages=%RX32\n", uDst, uPhysToAlias, cbHowMuch, fPte, cPages);
        if (BS3_MODE_IS_LEGACY_PAGING(g_bBs3CurrentMode))
        {
            X86PTE BS3_FAR *pPteLegacy;
            uint32_t        uDst32 = (uint32_t)uDst;
            uint32_t        uPhysToAlias32 = (uint32_t)uPhysToAlias;
            if (uDst32 != uDst)
            {
                Bs3TestPrintf("warning: Bs3PagingAlias - uDst=%RX64 is out of range for legacy paging!\n", uDst);
                return VERR_INVALID_PARAMETER;
            }
            if (uPhysToAlias32 != uPhysToAlias)
            {
                Bs3TestPrintf("warning: Bs3PagingAlias - uPhysToAlias=%RX64 is out of range for legacy paging!\n", uPhysToAlias);
                return VERR_INVALID_PARAMETER;
            }

            /*
             * Trigger page table splitting first.
             */
            while (cPages > 0)
            {
                pPteLegacy = bs3PagingGetLegacyPte(cr3, uDst32, false, &rc);
                if (pPteLegacy)
                {
                    uint32_t cLeftInPt = X86_PG_ENTRIES - ((uDst32 >> X86_PT_SHIFT) & X86_PT_MASK);
                    if (cPages <= cLeftInPt)
                        break;
                    uDst32 += cLeftInPt << X86_PAGE_SHIFT;
                    cPages -= cLeftInPt;
                }
                else
                {
                    Bs3TestPrintf("warning: Bs3PagingAlias - bs3PagingGetLegacyPte failed: rc=%d\n", rc);
                    return rc;
                }
            }

            /*
             * Make the changes.
             */
            cPages = cbHowMuch >> X86_PAGE_SHIFT;
            uDst32 = (uint32_t)uDst;
            while (cPages > 0)
            {
                uint32_t cLeftInPt = X86_PG_ENTRIES - ((uDst32 >> X86_PT_SHIFT) & X86_PT_MASK);
                pPteLegacy = bs3PagingGetLegacyPte(cr3, uDst32, false, &rc);
                while (cLeftInPt > 0 && cPages > 0)
                {
                    pPteLegacy->u = uPhysToAlias32 | (uint32_t)fPte;
                    pPteLegacy++;
                    uDst32         += X86_PAGE_SIZE;
                    uPhysToAlias32 += X86_PAGE_SIZE;
                    cPages--;
                    cLeftInPt--;
                }
            }
        }
        else
        {
            X86PTEPAE BS3_FAR *pPtePae;
            uint64_t const uDstSaved = uDst;

            /*
             * Trigger page table splitting first.
             */
            while (cPages > 0)
            {
                pPtePae = bs3PagingGetPaePte(cr3, g_bBs3CurrentMode, uDst, false, &rc);
                if (pPtePae)
                {
                    uint32_t cLeftInPt = X86_PG_PAE_ENTRIES - ((uDst >> X86_PT_PAE_SHIFT) & X86_PT_PAE_MASK);
                    if (cPages <= cLeftInPt)
                        break;
                    cPages -= cLeftInPt;
                    uDst   += cLeftInPt << X86_PAGE_SHIFT;
                }
                else
                {
                    Bs3TestPrintf("warning: Bs3PagingAlias - bs3PagingGetLegacyPte failed: rc=%d\n", rc);
                    return rc;
                }
            }

            /*
             * Make the changes.
             */
            cPages = cbHowMuch >> X86_PAGE_SHIFT;
            uDst   = uDstSaved;
            while (cPages > 0)
            {
                uint32_t cLeftInPt = X86_PG_PAE_ENTRIES - ((uDst >> X86_PT_PAE_SHIFT) & X86_PT_PAE_MASK);
                pPtePae = bs3PagingGetPaePte(cr3, g_bBs3CurrentMode, uDst, false, &rc);
                while (cLeftInPt > 0 && cPages > 0)
                {
                    pPtePae->u = uPhysToAlias | fPte;
                    pPtePae++;
                    uDst         += X86_PAGE_SIZE;
                    uPhysToAlias += X86_PAGE_SIZE;
                    cPages--;
                    cLeftInPt--;
                }
            }
        }

        ASMReloadCR3();
    }
#if ARCH_BITS == 16
    /*
     * We can't do this stuff in v8086 mode, so switch to 16-bit prot mode and do it there.
     */
    else
        return Bs3SwitchFromV86To16BitAndCallC((FPFNBS3FAR)Bs3PagingAlias_f16, sizeof(uint64_t)*3 + sizeof(uint32_t),
                                               uDst, uPhysToAlias, cbHowMuch, fPte);
#endif
    return VINF_SUCCESS;
}


#undef Bs3PagingUnalias
BS3_CMN_DEF(int, Bs3PagingUnalias,(uint64_t uDst, uint32_t cbHowMuch))
{
    return BS3_CMN_NM(Bs3PagingAlias)(uDst, uDst, cbHowMuch, X86_PTE_P | X86_PTE_RW | X86_PTE_US | X86_PTE_A | X86_PTE_D);
}

