/* $Id: PGMSlatDefs.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox - Page Manager, SLAT Paging Template - All context code.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/

#undef SLAT_IS_PGENTRY_PRESENT
#undef SLAT_IS_PML4E_VALID
#undef SLAT_IS_PDPE_VALID
#undef SLAT_IS_BIG_PDPE_VALID
#undef SLAT_IS_PDE_VALID
#undef SLAT_IS_BIG_PDE_VALID
#undef SLAT_IS_PTE_VALID
#undef SLAT_GET_PDPE1G_GCPHYS
#undef SLAT_GET_PDE2M_GCPHYS
#undef SLAT_GET_PTE_GCPHYS
#undef SLAT_PAGE_1G_OFFSET_MASK
#undef SLAT_PAGE_2M_OFFSET_MASK
#undef SLAT_PML4_SHIFT
#undef SLAT_PML4_MASK
#undef SLAT_PDPT_SHIFT
#undef SLAT_PDPT_MASK
#undef SLAT_PD_SHIFT
#undef SLAT_PD_MASK
#undef SLAT_PT_SHIFT
#undef SLAT_PT_MASK
#undef SLATPDE
#undef PSLATPDE
#undef SLATPTE
#undef PSLATPTE
#undef PSLATPTWALK

#define SLAT_IS_PGENTRY_PRESENT(a_pVCpu, a_Pge)             ((a_Pge.u) & (a_pVCpu)->pgm.s.fGstEptPresentMask)
#define SLAT_IS_PML4E_VALID(a_pVCpu, a_Pml4e)               (!( (a_Pml4e).u & (a_pVCpu)->pgm.s.fGstEptMbzPml4eMask ))
#define SLAT_IS_PDPE_VALID(a_pVCpu, a_Pdpte)                (!( (a_Pdpte).u & (a_pVCpu)->pgm.s.fGstEptMbzPdpteMask ))
#define SLAT_IS_BIG_PDPE_VALID(a_pVCpu, a_Pdpe)             (!( (a_Pdpe).u  & (a_pVCpu)->pgm.s.fGstEptMbzBigPdpteMask ))
#define SLAT_IS_PDE_VALID(a_pVCpu, a_Pde)                   (!( (a_Pde).u   & (a_pVCpu)->pgm.s.fGstEptMbzPdeMask ))
#define SLAT_IS_BIG_PDE_VALID(a_pVCpu, a_Pde)               (!( (a_Pde).u   & (a_pVCpu)->pgm.s.fGstEptMbzBigPdeMask ))
#define SLAT_IS_PTE_VALID(a_pVCpu, a_Pte)                   (!( (a_Pte).u   & (a_pVCpu)->pgm.s.fGstEptMbzPteMask ))
#define SLAT_GET_PDPE1G_GCPHYS(a_pVCpu, a_Pdpte)            PGM_A20_APPLY(a_pVCpu, ((a_Pdpte).u & EPT_PDPTE1G_PG_MASK))
#define SLAT_GET_PDE2M_GCPHYS(a_pVCpu, a_Pde)               PGM_A20_APPLY(a_pVCpu, ((a_Pde).u   & EPT_PDE2M_PG_MASK))
#define SLAT_GET_PTE_GCPHYS(a_pVCpu, a_Pte)                 PGM_A20_APPLY(a_pVCpu, ((a_Pte).u   & EPT_E_PG_MASK))
#define SLAT_PAGE_1G_OFFSET_MASK                            X86_PAGE_1G_OFFSET_MASK
#define SLAT_PAGE_2M_OFFSET_MASK                            X86_PAGE_2M_OFFSET_MASK
#define SLAT_PML4_SHIFT                                     EPT_PML4_SHIFT
#define SLAT_PML4_MASK                                      EPT_PML4_MASK
#define SLAT_PDPT_SHIFT                                     EPT_PDPT_SHIFT
#define SLAT_PDPT_MASK                                      EPT_PDPT_MASK
#define SLAT_PD_SHIFT                                       EPT_PD_SHIFT
#define SLAT_PD_MASK                                        EPT_PD_MASK
#define SLAT_PT_SHIFT                                       EPT_PT_SHIFT
#define SLAT_PT_MASK                                        EPT_PT_MASK
#define SLATPDE                                             EPTPDE
#define PSLATPDE                                            PEPTPDE
#define SLATPTE                                             EPTPTE
#define PSLATPTE                                            PEPTPTE
#define PSLATPTWALK                                         PPGMPTWALKGSTEPT

#if 0
#  if PGM_SHW_TYPE != PGM_TYPE_EPT
#   error "Only SLAT type of EPT is supported "
#  endif
#  if PGM_GST_TYPE != PGM_TYPE_EPT
#   error "Guest type for SLAT EPT "
#  endif

#  define GST_ATOMIC_OR(a_pu, a_fFlags)          ASMAtomicOrU64((a_pu), (a_fFlags))
#  define GSTPT                                  EPTPT
#  define PGSTPT                                 PEPTPT
#  define GSTPTE                                 EPTPTE
#  define PGSTPTE                                PEPTPTE
#  define GSTPD                                  EPTPD
#  define PGSTPD                                 PEPTPD
#  define GSTPDE                                 EPTPDE
#  define PGSTPDE                                PEPTPDE
#  define GST_GIGANT_PAGE_SIZE                   X86_PAGE_1G_SIZE
#  define GST_GIGANT_PAGE_OFFSET_MASK            X86_PAGE_1G_OFFSET_MASK
#  define GST_PDPE_BIG_PG_MASK                   X86_PDPE1G_PG_MASK
#  define GST_BIG_PAGE_SIZE                      X86_PAGE_2M_SIZE
#  define GST_BIG_PAGE_OFFSET_MASK               X86_PAGE_2M_OFFSET_MASK
#  define GST_PDE_PG_MASK                        EPT_PDE_PG_MASK
#  define GST_PDE_BIG_PG_MASK                    EPT_PDE2M_PG_MASK
#  define GST_PD_SHIFT                           EPT_PD_SHIFT
#  define GST_PD_MASK                            EPT_PD_MASK
#  define GSTPTWALK                              PGMPTWALKGSTEPT
#  define PGSTPTWALK                             PPGMPTWALKGSTEPT
#  define PCGSTPTWALK                            PCPGMPTWALKGSTEPT
#  define GST_PDPE_ENTRIES                       EPT_PG_ENTRIES
#  define GST_PDPT_SHIFT                         EPT_PDPT_SHIFT
#  define GST_PDPE_PG_MASK                       EPT_PDPTE_PG_MASK
#  define GST_PDPT_MASK                          EPT_PDPT_MASK
#  define GST_PTE_PG_MASK                        EPT_E_PG_MASK
#  define GST_CR3_PAGE_MASK                      X86_CR3_EPT_PAGE_MASK
#  define GST_PT_SHIFT                           EPT_PT_SHIFT
#  define GST_PT_MASK                            EPT_PT_MASK
#  define GST_GET_PTE_GCPHYS(Pte)                PGM_A20_APPLY(a_pVCpu, ((Pte).u & GST_PTE_PG_MASK))
#  define GST_GET_PDE_GCPHYS(Pde)                PGM_A20_APPLY(a_pVCpu, ((Pde).u & GST_PDE_PG_MASK))
#  define GST_GET_BIG_PDE_GCPHYS(pVM, Pde)       PGM_A20_APPLY(a_pVCpu, ((Pde).u & GST_PDE_BIG_PG_MASK))
#  define GST_GET_BIG_PDPE_GCPHYS(pVM, Pde)      PGM_A20_APPLY(a_pVCpu, ((Pde).u & GST_PDPE_BIG_PG_MASK))
#  define GST_GET_PTE_SHW_FLAGS(a_pVCpu, Pte)            (true && This_should_perhaps_not_be_used_in_this_context)
#  define GST_GET_PDE_SHW_FLAGS(a_pVCpu, Pde)            (true && This_should_perhaps_not_be_used_in_this_context)
#  define GST_GET_BIG_PDE_SHW_FLAGS(a_pVCpu, Pde)        (true && This_should_perhaps_not_be_used_in_this_context)
#  define GST_GET_BIG_PDE_SHW_FLAGS_4_PTE(a_pVCpu, Pde)  (true && This_should_perhaps_not_be_used_in_this_context)
#  define GST_IS_PTE_VALID(a_pVCpu, Pte)           (!( (Pte).u   & (a_pVCpu)->pgm.s.fGstEptMbzPteMask ))
#  define GST_IS_PDE_VALID(a_pVCpu, Pde)           (!( (Pde).u   & (a_pVCpu)->pgm.s.fGstEptMbzPdeMask ))
#  define GST_IS_BIG_PDE_VALID(a_pVCpu, Pde)       (!( (Pde).u   & (a_pVCpu)->pgm.s.fGstEptMbzBigPdeMask ))
#  define GST_IS_PDPE_VALID(a_pVCpu, Pdpe)         (!( (Pdpe).u  & (a_pVCpu)->pgm.s.fGstEptMbzPdpteMask ))
#  define GST_IS_BIG_PDPE_VALID(a_pVCpu, Pdpe)     (!( (Pdpe).u  & (a_pVCpu)->pgm.s.fGstEptMbzBigPdpteMask ))
#  define GST_IS_PML4E_VALID(a_pVCpu, Pml4e)       (!( (Pml4e).u & (a_pVCpu)->pgm.s.fGstEptMbzPml4eMask ))
#  define GST_IS_PGENTRY_PRESENT(a_pVCpu, Pge)     ((Pge).u & EPT_PRESENT_MASK)
#  define GST_IS_PSE_ACTIVE(a_pVCpu)               (!((a_pVCpu)->pgm.s.fGstEptMbzBigPdeMask & EPT_E_BIT_LEAF))
#  define GST_IS_NX_ACTIVE(a_pVCpu)                (pgmGstIsNoExecuteActive(a_pVCpu))
#  define BTH_IS_NP_ACTIVE(pVM)                  (false)
#endif

