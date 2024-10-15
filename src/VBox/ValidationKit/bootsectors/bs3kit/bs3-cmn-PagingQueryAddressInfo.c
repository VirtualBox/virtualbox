/* $Id: bs3-cmn-PagingQueryAddressInfo.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3PagingQueryAddressInfo
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
#include <bs3kit.h>
#include <iprt/asm-amd64-x86.h>
#include <VBox/err.h>


#undef Bs3PagingQueryAddressInfo
BS3_CMN_DEF(int, Bs3PagingQueryAddressInfo,(uint64_t uFlat, PBS3PAGINGINFO4ADDR pPgInfo))
{
#ifdef _MSC_VER
    BS3PAGINGINFO4ADDR volatile BS3_FAR *pPgInfoNoSseHack = (BS3PAGINGINFO4ADDR volatile BS3_FAR *)pPgInfo;
# define pPgInfo pPgInfoNoSseHack
#endif
    RTCCUINTXREG const  cr3        = ASMGetCR3();
    RTCCUINTXREG const  cr4        = g_uBs3CpuDetected & BS3CPU_F_CPUID ? ASMGetCR4() : 0;
    bool const          fLegacyPTs = !(cr4 & X86_CR4_PAE);
    int                 rc = VERR_OUT_OF_RANGE;


    pPgInfo->fFlags             = 0;
    pPgInfo->u.apbEntries[0]    = NULL;
    pPgInfo->u.apbEntries[1]    = NULL;
    pPgInfo->u.apbEntries[2]    = NULL;
    pPgInfo->u.apbEntries[3]    = NULL;

    if (!fLegacyPTs)
    {
#if TMPL_BITS == 16
        uint32_t const  uMaxAddr = BS3_MODE_IS_RM_OR_V86(g_bBs3CurrentMode) ? _1M - 1 : BS3_SEL_TILED_AREA_SIZE - 1;
#else
        uintptr_t const uMaxAddr = ~(uintptr_t)0;
#endif
        uint64_t const  fEfer    = g_uBs3CpuDetected & BS3CPU_F_LONG_MODE ? ASMRdMsr(MSR_K6_EFER) : 0;

        pPgInfo->cEntries = fEfer & MSR_K6_EFER_LMA ? 4 : 3;
        pPgInfo->cbEntry  = sizeof(X86PTEPAE);
        if ((cr3 & X86_CR3_AMD64_PAGE_MASK) <= uMaxAddr)
        {
            if (   (fEfer & MSR_K6_EFER_LMA)
                && X86_IS_CANONICAL(uFlat))
            {
                /* 48-bit long mode paging. */
                pPgInfo->u.Pae.pPml4e  = (X86PML4E BS3_FAR *)Bs3XptrFlatToCurrent(cr3 & X86_CR3_AMD64_PAGE_MASK);
                pPgInfo->u.Pae.pPml4e += (uFlat >> X86_PML4_SHIFT) & X86_PML4_MASK;
                if (!pPgInfo->u.Pae.pPml4e->n.u1Present)
                    rc = VERR_PAGE_NOT_PRESENT;
                else if ((pPgInfo->u.Pae.pPml4e->u & X86_PML4E_PG_MASK) <= uMaxAddr)
                {
                    pPgInfo->u.Pae.pPdpe  = (X86PDPE BS3_FAR *)Bs3XptrFlatToCurrent(pPgInfo->u.Pae.pPml4e->u & X86_PML4E_PG_MASK);
                    pPgInfo->u.Pae.pPdpe += (uFlat >> X86_PDPT_SHIFT) & X86_PDPT_MASK_AMD64;
                    if (!pPgInfo->u.Pae.pPdpe->n.u1Present)
                        rc = VERR_PAGE_NOT_PRESENT;
                    else if (pPgInfo->u.Pae.pPdpe->b.u1Size)
                        rc = VINF_SUCCESS;
                    else
                        rc = VINF_TRY_AGAIN;
                }
            }
            else if (   !(fEfer & MSR_K6_EFER_LMA)
                     && uFlat <= _4G)
            {
                /* 32-bit PAE paging. */
                pPgInfo->u.Pae.pPdpe  = (X86PDPE BS3_FAR *)Bs3XptrFlatToCurrent(cr3 & X86_CR3_PAE_PAGE_MASK);
                pPgInfo->u.Pae.pPdpe += ((uint32_t)uFlat >> X86_PDPT_SHIFT) & X86_PDPT_MASK_PAE;
                if (!pPgInfo->u.Pae.pPdpe->n.u1Present)
                    rc = VERR_PAGE_NOT_PRESENT;
                else
                    rc = VINF_TRY_AGAIN;
            }

            /* Common code for the PD and PT levels. */
            if (   rc == VINF_TRY_AGAIN
                && (pPgInfo->u.Pae.pPdpe->u & X86_PDPE_PG_MASK) <= uMaxAddr)
            {
                rc = VERR_OUT_OF_RANGE;
                pPgInfo->u.Pae.pPde  = (X86PDEPAE BS3_FAR *)Bs3XptrFlatToCurrent(pPgInfo->u.Pae.pPdpe->u & X86_PDPE_PG_MASK);
                pPgInfo->u.Pae.pPde += (uFlat >> X86_PD_PAE_SHIFT) & X86_PD_PAE_MASK;
                if (!pPgInfo->u.Pae.pPde->n.u1Present)
                    rc = VERR_PAGE_NOT_PRESENT;
                else if (pPgInfo->u.Pae.pPde->b.u1Size)
                    rc = VINF_SUCCESS;
                else if ((pPgInfo->u.Pae.pPde->u & X86_PDE_PAE_PG_MASK) <= uMaxAddr)
                {
                    pPgInfo->u.Pae.pPte = (X86PTEPAE BS3_FAR *)Bs3XptrFlatToCurrent(pPgInfo->u.Pae.pPde->u & X86_PDE_PAE_PG_MASK);
                    rc = VINF_SUCCESS;
                }
            }
            else if (rc == VINF_TRY_AGAIN)
                rc = VERR_OUT_OF_RANGE;
        }
    }
    else
    {
#if TMPL_BITS == 16
        uint32_t const  uMaxAddr = BS3_MODE_IS_RM_OR_V86(g_bBs3CurrentMode) ? _1M - 1 : BS3_SEL_TILED_AREA_SIZE - 1;
#else
        uint32_t const  uMaxAddr = UINT32_MAX;
#endif

        pPgInfo->cEntries = 2;
        pPgInfo->cbEntry  = sizeof(X86PTE);
        if (   uFlat < _4G
            && cr3 <= uMaxAddr)
        {
            pPgInfo->u.Legacy.pPde  = (X86PDE BS3_FAR *)Bs3XptrFlatToCurrent(cr3 & X86_CR3_PAGE_MASK);
            pPgInfo->u.Legacy.pPde += ((uint32_t)uFlat >> X86_PD_SHIFT) & X86_PD_MASK;
            if (!pPgInfo->u.Legacy.pPde->b.u1Present)
                rc = VERR_PAGE_NOT_PRESENT;
            else if (pPgInfo->u.Legacy.pPde->b.u1Size)
                rc = VINF_SUCCESS;
            else if (pPgInfo->u.Legacy.pPde->u <= uMaxAddr)
            {
                pPgInfo->u.Legacy.pPte  = (X86PTE BS3_FAR *)Bs3XptrFlatToCurrent(pPgInfo->u.Legacy.pPde->u & X86_PDE_PG_MASK);
                pPgInfo->u.Legacy.pPte += ((uint32_t)uFlat >> X86_PT_SHIFT) & X86_PT_MASK;
                if (pPgInfo->u.Legacy.pPte->n.u1Present)
                    rc = VINF_SUCCESS;
                else
                    rc = VERR_PAGE_NOT_PRESENT;
            }
        }
    }
    return rc;
}

