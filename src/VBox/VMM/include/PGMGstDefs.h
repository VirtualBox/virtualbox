/* $Id: PGMGstDefs.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox - Page Manager, Guest Paging Template - All context code.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
#undef GST_ATOMIC_OR
#undef GSTPT
#undef PGSTPT
#undef GSTPTE
#undef PGSTPTE
#undef GSTPD
#undef PGSTPD
#undef GSTPDE
#undef PGSTPDE
#undef GSTPTWALK
#undef PGSTPTWALK
#undef PCGSTPTWALK
#undef GST_BIG_PAGE_SIZE
#undef GST_BIG_PAGE_OFFSET_MASK
#undef GST_GIGANT_PAGE_SIZE
#undef GST_GIGANT_PAGE_OFFSET_MASK
#undef GST_PDPE_BIG_PG_MASK
#undef GST_PDE_PG_MASK
#undef GST_PDE_BIG_PG_MASK
#undef GST_PD_SHIFT
#undef GST_PD_MASK
#undef GST_PTE_PG_MASK
#undef GST_GET_PTE_SHW_FLAGS
#undef GST_PT_SHIFT
#undef GST_PT_MASK
#undef GST_CR3_PAGE_MASK
#undef GST_PDPE_ENTRIES
#undef GST_PDPT_SHIFT
#undef GST_PDPT_MASK
#undef GST_PDPE_PG_MASK
#undef GST_GET_PTE_GCPHYS
#undef GST_GET_PDE_GCPHYS
#undef GST_GET_BIG_PDE_GCPHYS
#undef GST_GET_BIG_PDPE_GCPHYS
#undef GST_GET_PDE_SHW_FLAGS
#undef GST_GET_BIG_PDE_SHW_FLAGS
#undef GST_GET_BIG_PDE_SHW_FLAGS_4_PTE
#undef GST_IS_PTE_VALID
#undef GST_IS_PDE_VALID
#undef GST_IS_BIG_PDE_VALID
#undef GST_IS_PDPE_VALID
#undef GST_IS_BIG_PDPE_VALID
#undef GST_IS_PML4E_VALID
#undef GST_IS_PGENTRY_PRESENT
#undef GST_IS_PSE_ACTIVE
#undef GST_IS_NX_ACTIVE
#undef BTH_IS_NP_ACTIVE

#if PGM_GST_TYPE == PGM_TYPE_REAL \
 || PGM_GST_TYPE == PGM_TYPE_PROT

# if PGM_SHW_TYPE == PGM_TYPE_EPT
#  define GST_ATOMIC_OR(a_pu, a_fFlags)         ASMAtomicOrU64((a_pu), (a_fFlags))
#  define GSTPT                                 X86PTPAE
#  define PGSTPT                                PX86PTPAE
#  define GSTPTE                                X86PTEPAE
#  define PGSTPTE                               PX86PTEPAE
#  define GSTPD                                 X86PDPAE
#  define PGSTPD                                PX86PDPAE
#  define GSTPDE                                X86PDEPAE
#  define PGSTPDE                               PX86PDEPAE
#  define GST_PTE_PG_MASK                       X86_PTE_PAE_PG_MASK
#  define GST_IS_NX_ACTIVE(pVCpu)               (true && This_should_perhaps_not_be_used_in_this_context)
#  define BTH_IS_NP_ACTIVE(pVM)                 (true)
# else
#  if PGM_SHW_TYPE == PGM_TYPE_32BIT /* Same as shadow paging, but no PGMSHWPTEPAE. */
#   define GST_ATOMIC_OR(a_pu, a_fFlags)        ASMAtomicOrU32((a_pu), (a_fFlags))
#   define GSTPT                                X86PT
#   define PGSTPT                               PX86PT
#   define GSTPTE                               X86PTE
#   define PGSTPTE                              PX86PTE
#   define GSTPD                                X86PD
#   define PGSTPD                               PX86PD
#   define GSTPDE                               X86PDE
#   define PGSTPDE                              PX86PDE
#   define GST_PTE_PG_MASK                      X86_PTE_PG_MASK
#  else
#   define GST_ATOMIC_OR(a_pu, a_fFlags)        ASMAtomicOrU64((a_pu), (a_fFlags))
#   define GSTPT                                X86PTPAE
#   define PGSTPT                               PX86PTPAE
#   define GSTPTE                               X86PTEPAE
#   define PGSTPTE                              PX86PTEPAE
#   define GSTPD                                X86PDPAE
#   define PGSTPD                               PX86PDPAE
#   define GSTPDE                               X86PDEPAE
#   define PGSTPDE                              PX86PDEPAE
#   define GST_PTE_PG_MASK                      X86_PTE_PAE_PG_MASK
#  endif
#  define GST_IS_NX_ACTIVE(pVCpu)               (pgmGstIsNoExecuteActive(pVCpu))
#  if PGM_GST_TYPE == PGM_TYPE_PROT             /* (comment at top of PGMAllBth.h) */
#   define BTH_IS_NP_ACTIVE(pVM)                (pVM->pgm.s.fNestedPaging)
#  else
#   define BTH_IS_NP_ACTIVE(pVM)                (false)
#  endif
# endif
# define GST_GET_PTE_GCPHYS(Pte)                PGM_A20_APPLY(pVCpu, ((Pte).u & GST_PTE_PG_MASK))
# define GST_GET_PDE_GCPHYS(Pde)                (true && This_should_perhaps_not_be_used_in_this_context) //??
# define GST_GET_BIG_PDE_GCPHYS(Pde)            (true && This_should_perhaps_not_be_used_in_this_context) //??
# define GST_GET_PTE_SHW_FLAGS(pVCpu, Pte)      ((Pte).u & (X86_PTE_P | X86_PTE_RW | X86_PTE_US | X86_PTE_A | X86_PTE_D | X86_PTE_G)) /**< @todo Could return P|RW|US|A|D here without consulting the PTE. */
# define GST_GET_PDE_SHW_FLAGS(pVCpu, Pde)      (true && This_should_perhaps_not_be_used_in_this_context) //??
# define GST_GET_BIG_PDE_SHW_FLAGS(pVCpu, Pde)  (true && This_should_perhaps_not_be_used_in_this_context) //??
# define GST_GET_BIG_PDE_SHW_FLAGS_4_PTE(pVCpu, Pde) (true && This_should_perhaps_not_be_used_in_this_context) //??
# define GST_IS_PTE_VALID(pVCpu, Pte)           (true)
# define GST_IS_PDE_VALID(pVCpu, Pde)           (true)
# define GST_IS_BIG_PDE_VALID(pVCpu, Pde)       (true)
# define GST_IS_PDPE_VALID(pVCpu, Pdpe)         (true)
# define GST_IS_BIG_PDPE_VALID(pVCpu, Pdpe)     (true)
# define GST_IS_PML4E_VALID(pVCpu, Pml4e)       (true)
# define GST_IS_PGENTRY_PRESENT(pVCpu, Pge)     ((Pge.u) & X86_PTE_P)
# define GST_IS_PSE_ACTIVE(pVCpu)               (false && This_should_not_be_used_in_this_context)

#elif PGM_GST_TYPE == PGM_TYPE_32BIT
# define GST_ATOMIC_OR(a_pu, a_fFlags)          ASMAtomicOrU32((a_pu), (a_fFlags))
# define GSTPT                                  X86PT
# define PGSTPT                                 PX86PT
# define GSTPTE                                 X86PTE
# define PGSTPTE                                PX86PTE
# define GSTPD                                  X86PD
# define PGSTPD                                 PX86PD
# define GSTPDE                                 X86PDE
# define PGSTPDE                                PX86PDE
# define GSTPTWALK                              PGMPTWALKGST32BIT
# define PGSTPTWALK                             PPGMPTWALKGST32BIT
# define PCGSTPTWALK                            PCPGMPTWALKGST32BIT
# define GST_BIG_PAGE_SIZE                      X86_PAGE_4M_SIZE
# define GST_BIG_PAGE_OFFSET_MASK               X86_PAGE_4M_OFFSET_MASK
# define GST_PDE_PG_MASK                        X86_PDE_PG_MASK
# define GST_PDE_BIG_PG_MASK                    X86_PDE4M_PG_MASK
# define GST_GET_PTE_GCPHYS(Pte)                PGM_A20_APPLY(pVCpu, ((Pte).u & GST_PDE_PG_MASK))
# define GST_GET_PDE_GCPHYS(Pde)                PGM_A20_APPLY(pVCpu, ((Pde).u & GST_PDE_PG_MASK))
# define GST_GET_BIG_PDE_GCPHYS(pVM, Pde)       PGM_A20_APPLY(pVCpu, pgmGstGet4MBPhysPage((pVM), Pde))
# define GST_GET_PDE_SHW_FLAGS(pVCpu, Pde)      ((Pde).u & (X86_PDE_P | X86_PDE_RW | X86_PDE_US | X86_PDE_A))
# define GST_GET_BIG_PDE_SHW_FLAGS(pVCpu, Pde) \
    ( ((Pde).u & (X86_PDE4M_P | X86_PDE4M_RW | X86_PDE4M_US | X86_PDE4M_A)) | PGM_PDFLAGS_BIG_PAGE )
# define GST_GET_BIG_PDE_SHW_FLAGS_4_PTE(pVCpu, Pde) \
    ((Pde).u & (X86_PDE4M_P | X86_PDE4M_RW | X86_PDE4M_US | X86_PDE4M_A | X86_PDE4M_D | X86_PDE4M_G))
# define GST_PD_SHIFT                           X86_PD_SHIFT
# define GST_PD_MASK                            X86_PD_MASK
# define GST_PTE_PG_MASK                        X86_PTE_PG_MASK
# define GST_GET_PTE_SHW_FLAGS(pVCpu, Pte)      ((Pte).u & (X86_PTE_P | X86_PTE_RW | X86_PTE_US | X86_PTE_A | X86_PTE_D | X86_PTE_G))
# define GST_PT_SHIFT                           X86_PT_SHIFT
# define GST_PT_MASK                            X86_PT_MASK
# define GST_CR3_PAGE_MASK                      X86_CR3_PAGE_MASK
# define GST_IS_PTE_VALID(pVCpu, Pte)           (true)
# define GST_IS_PDE_VALID(pVCpu, Pde)           (true)
# define GST_IS_BIG_PDE_VALID(pVCpu, Pde)       (!( (Pde).u & (pVCpu)->pgm.s.fGst32BitMbzBigPdeMask ))
//# define GST_IS_PDPE_VALID(pVCpu, Pdpe)         (false)
//# define GST_IS_BIG_PDPE_VALID(pVCpu, Pdpe)     (false)
//# define GST_IS_PML4E_VALID(pVCpu, Pml4e)       (false)
# define GST_IS_PGENTRY_PRESENT(pVCpu, Pge)     ((Pge.u) & X86_PTE_P)
# define GST_IS_PSE_ACTIVE(pVCpu)               pgmGst32BitIsPageSizeExtActive(pVCpu)
# define GST_IS_NX_ACTIVE(pVCpu)                (false)
# define BTH_IS_NP_ACTIVE(pVM)                  (false)

#elif   PGM_GST_TYPE == PGM_TYPE_PAE \
     || PGM_GST_TYPE == PGM_TYPE_AMD64
# define GST_ATOMIC_OR(a_pu, a_fFlags)          ASMAtomicOrU64((a_pu), (a_fFlags))
# define GSTPT                                  X86PTPAE
# define PGSTPT                                 PX86PTPAE
# define GSTPTE                                 X86PTEPAE
# define PGSTPTE                                PX86PTEPAE
# define GSTPD                                  X86PDPAE
# define PGSTPD                                 PX86PDPAE
# define GSTPDE                                 X86PDEPAE
# define PGSTPDE                                PX86PDEPAE
# define GST_BIG_PAGE_SIZE                      X86_PAGE_2M_SIZE
# define GST_BIG_PAGE_OFFSET_MASK               X86_PAGE_2M_OFFSET_MASK
# define GST_PDE_PG_MASK                        X86_PDE_PAE_PG_MASK
# define GST_PDE_BIG_PG_MASK                    X86_PDE2M_PAE_PG_MASK
# define GST_GET_PTE_GCPHYS(Pte)                PGM_A20_APPLY(pVCpu, ((Pte).u & GST_PTE_PG_MASK))
# define GST_GET_PDE_GCPHYS(Pde)                PGM_A20_APPLY(pVCpu, ((Pde).u & GST_PDE_PG_MASK))
# define GST_GET_BIG_PDE_GCPHYS(pVM, Pde)       PGM_A20_APPLY(pVCpu, ((Pde).u & GST_PDE_BIG_PG_MASK))
# define GST_GET_PTE_SHW_FLAGS(pVCpu, Pte)      ((Pte).u & (pVCpu)->pgm.s.fGst64ShadowedPteMask )
# define GST_GET_PDE_SHW_FLAGS(pVCpu, Pde)      ((Pde).u & (pVCpu)->pgm.s.fGst64ShadowedPdeMask )
# define GST_GET_BIG_PDE_SHW_FLAGS(pVCpu, Pde)  ( ((Pde).u & (pVCpu)->pgm.s.fGst64ShadowedBigPdeMask ) | PGM_PDFLAGS_BIG_PAGE)
# define GST_GET_BIG_PDE_SHW_FLAGS_4_PTE(pVCpu, Pde)  ((Pde).u & (pVCpu)->pgm.s.fGst64ShadowedBigPde4PteMask )

# define GST_PD_SHIFT                           X86_PD_PAE_SHIFT
# define GST_PD_MASK                            X86_PD_PAE_MASK
# if PGM_GST_TYPE == PGM_TYPE_PAE
#  define GSTPTWALK                             PGMPTWALKGSTPAE
#  define PGSTPTWALK                            PPGMPTWALKGSTPAE
#  define PCGSTPTWALK                           PCPGMPTWALKGSTPAE
#  define GST_PDPE_ENTRIES                      X86_PG_PAE_PDPE_ENTRIES
#  define GST_PDPE_PG_MASK                      X86_PDPE_PG_MASK
#  define GST_PDPT_SHIFT                        X86_PDPT_SHIFT
#  define GST_PDPT_MASK                         X86_PDPT_MASK_PAE
#  define GST_PTE_PG_MASK                       X86_PTE_PAE_PG_MASK
#  define GST_CR3_PAGE_MASK                     X86_CR3_PAE_PAGE_MASK
#  define GST_IS_PTE_VALID(pVCpu, Pte)          (!( (Pte).u   & (pVCpu)->pgm.s.fGstPaeMbzPteMask ))
#  define GST_IS_PDE_VALID(pVCpu, Pde)          (!( (Pde).u   & (pVCpu)->pgm.s.fGstPaeMbzPdeMask ))
#  define GST_IS_BIG_PDE_VALID(pVCpu, Pde)      (!( (Pde).u   & (pVCpu)->pgm.s.fGstPaeMbzBigPdeMask ))
#  define GST_IS_PDPE_VALID(pVCpu, Pdpe)        (!( (Pdpe).u  & (pVCpu)->pgm.s.fGstPaeMbzPdpeMask ))
//# define GST_IS_BIG_PDPE_VALID(pVCpu, Pdpe)    (false)
//# define GST_IS_PML4E_VALID(pVCpu, Pml4e)      (false)
# else
#  define GSTPTWALK                             PGMPTWALKGSTAMD64
#  define PGSTPTWALK                            PPGMPTWALKGSTAMD64
#  define PCGSTPTWALK                           PCPGMPTWALKGSTAMD64
#  define GST_PDPE_ENTRIES                      X86_PG_AMD64_PDPE_ENTRIES
#  define GST_PDPT_SHIFT                        X86_PDPT_SHIFT
#  define GST_PDPE_PG_MASK                      X86_PDPE_PG_MASK
#  define GST_PDPT_MASK                         X86_PDPT_MASK_AMD64
#  define GST_PTE_PG_MASK                       X86_PTE_PAE_PG_MASK
#  define GST_CR3_PAGE_MASK                     X86_CR3_AMD64_PAGE_MASK
#  define GST_IS_PTE_VALID(pVCpu, Pte)          (!( (Pte).u   & (pVCpu)->pgm.s.fGstAmd64MbzPteMask ))
#  define GST_IS_PDE_VALID(pVCpu, Pde)          (!( (Pde).u   & (pVCpu)->pgm.s.fGstAmd64MbzPdeMask ))
#  define GST_IS_BIG_PDE_VALID(pVCpu, Pde)      (!( (Pde).u   & (pVCpu)->pgm.s.fGstAmd64MbzBigPdeMask ))
#  define GST_IS_PDPE_VALID(pVCpu, Pdpe)        (!( (Pdpe).u  & (pVCpu)->pgm.s.fGstAmd64MbzPdpeMask ))
#  define GST_IS_BIG_PDPE_VALID(pVCpu, Pdpe)    (!( (Pdpe).u  & (pVCpu)->pgm.s.fGstAmd64MbzBigPdpeMask ))
#  define GST_IS_PML4E_VALID(pVCpu, Pml4e)      (!( (Pml4e).u & (pVCpu)->pgm.s.fGstAmd64MbzPml4eMask ))
# endif
# define GST_IS_PGENTRY_PRESENT(pVCpu, Pge)     ((Pge.u) & X86_PTE_P)
# define GST_PT_SHIFT                           X86_PT_PAE_SHIFT
# define GST_PT_MASK                            X86_PT_PAE_MASK
# define GST_IS_PSE_ACTIVE(pVCpu)               (true)
# define GST_IS_NX_ACTIVE(pVCpu)                (pgmGstIsNoExecuteActive(pVCpu))
# define BTH_IS_NP_ACTIVE(pVM)                  (false)

#else
# error "Unknown PGM_GST_TYPE."
#endif

