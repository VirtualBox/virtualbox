/* $Id: PGMAllGst.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
RT_C_DECLS_BEGIN
/** @todo Do we really need any of these forward declarations? */
#if PGM_GST_TYPE == PGM_TYPE_32BIT \
 || PGM_GST_TYPE == PGM_TYPE_PAE \
 || PGM_GST_TYPE == PGM_TYPE_AMD64
DECLINLINE(int) PGM_GST_NAME(Walk)(PVMCPUCC pVCpu, RTGCPTR GCPtr, PPGMPTWALK pWalk, PGSTPTWALK pGstWalk);
#endif
PGM_GST_DECL(int,  Enter)(PVMCPUCC pVCpu, RTGCPHYS GCPhysCR3);
PGM_GST_DECL(int,  GetPage)(PVMCPUCC pVCpu, RTGCPTR GCPtr, PPGMPTWALK pWalk);
PGM_GST_DECL(int,  ModifyPage)(PVMCPUCC pVCpu, RTGCPTR GCPtr, size_t cb, uint64_t fFlags, uint64_t fMask);
PGM_GST_DECL(int,  Exit)(PVMCPUCC pVCpu);

#ifdef IN_RING3 /* r3 only for now.  */
PGM_GST_DECL(int, Relocate)(PVMCPUCC pVCpu, RTGCPTR offDelta);
#endif
RT_C_DECLS_END


/**
 * Enters the guest mode.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPhysCR3   The physical address from the CR3 register.
 */
PGM_GST_DECL(int, Enter)(PVMCPUCC pVCpu, RTGCPHYS GCPhysCR3)
{
    /*
     * Map and monitor CR3
     */
    uintptr_t idxBth = pVCpu->pgm.s.idxBothModeData;
    AssertReturn(idxBth < RT_ELEMENTS(g_aPgmBothModeData), VERR_PGM_MODE_IPE);
    AssertReturn(g_aPgmBothModeData[idxBth].pfnMapCR3, VERR_PGM_MODE_IPE);
    return g_aPgmBothModeData[idxBth].pfnMapCR3(pVCpu, GCPhysCR3);
}


/**
 * Exits the guest mode.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
PGM_GST_DECL(int, Exit)(PVMCPUCC pVCpu)
{
    uintptr_t idxBth = pVCpu->pgm.s.idxBothModeData;
    AssertReturn(idxBth < RT_ELEMENTS(g_aPgmBothModeData), VERR_PGM_MODE_IPE);
    AssertReturn(g_aPgmBothModeData[idxBth].pfnUnmapCR3, VERR_PGM_MODE_IPE);
    return g_aPgmBothModeData[idxBth].pfnUnmapCR3(pVCpu);
}


#if PGM_GST_TYPE == PGM_TYPE_32BIT \
 || PGM_GST_TYPE == PGM_TYPE_PAE \
 || PGM_GST_TYPE == PGM_TYPE_AMD64


DECLINLINE(int) PGM_GST_NAME(WalkReturnNotPresent)(PVMCPUCC pVCpu, PPGMPTWALK pWalk, uint8_t uLevel)
{
    NOREF(pVCpu);
    pWalk->fNotPresent     = true;
    pWalk->uLevel          = uLevel;
    pWalk->fFailed         = PGM_WALKFAIL_NOT_PRESENT
                           | ((uint32_t)uLevel << PGM_WALKFAIL_LEVEL_SHIFT);
    return VERR_PAGE_TABLE_NOT_PRESENT;
}

DECLINLINE(int) PGM_GST_NAME(WalkReturnBadPhysAddr)(PVMCPUCC pVCpu, PPGMPTWALK pWalk, uint8_t uLevel, int rc)
{
    AssertMsg(rc == VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS, ("%Rrc\n", rc)); NOREF(rc); NOREF(pVCpu);
    pWalk->fBadPhysAddr    = true;
    pWalk->uLevel          = uLevel;
    pWalk->fFailed         = PGM_WALKFAIL_BAD_PHYSICAL_ADDRESS
                           | ((uint32_t)uLevel << PGM_WALKFAIL_LEVEL_SHIFT);
    return VERR_PAGE_TABLE_NOT_PRESENT;
}

DECLINLINE(int) PGM_GST_NAME(WalkReturnRsvdError)(PVMCPUCC pVCpu, PPGMPTWALK pWalk, uint8_t uLevel)
{
    NOREF(pVCpu);
    pWalk->fRsvdError      = true;
    pWalk->uLevel          = uLevel;
    pWalk->fFailed         = PGM_WALKFAIL_RESERVED_BITS
                           | ((uint32_t)uLevel << PGM_WALKFAIL_LEVEL_SHIFT);
    return VERR_PAGE_TABLE_NOT_PRESENT;
}


/**
 * Performs a guest page table walk.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PAGE_TABLE_NOT_PRESENT on failure.  Check pWalk for details.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   GCPtr       The guest virtual address to walk by.
 * @param   pWalk       The page walk info.
 * @param   pGstWalk    The guest mode specific page walk info.
 * @thread  EMT(pVCpu)
 */
DECLINLINE(int) PGM_GST_NAME(Walk)(PVMCPUCC pVCpu, RTGCPTR GCPtr, PPGMPTWALK pWalk, PGSTPTWALK pGstWalk)
{
    int rc;

# ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
/** @def PGM_GST_SLAT_WALK
 * Macro to perform guest second-level address translation (EPT or Nested).
 *
 * @param   a_pVCpu         The cross context virtual CPU structure of the calling
 *                          EMT.
 * @param   a_GCPtrNested   The nested-guest linear address that caused the
 *                          second-level translation.
 * @param   a_GCPhysNested  The nested-guest physical address to translate.
 * @param   a_GCPhysOut     Where to store the guest-physical address (result).
 */
#  define PGM_GST_SLAT_WALK(a_pVCpu, a_GCPtrNested, a_GCPhysNested, a_GCPhysOut, a_pWalk) \
    do { \
        if ((a_pVCpu)->pgm.s.enmGuestSlatMode == PGMSLAT_EPT) \
        { \
            PGMPTWALK    WalkSlat; \
            PGMPTWALKGST WalkGstSlat; \
            int const rcX = pgmGstSlatWalk(a_pVCpu, a_GCPhysNested, true /* fIsLinearAddrValid */, a_GCPtrNested, &WalkSlat, \
                                           &WalkGstSlat); \
            if (RT_SUCCESS(rcX)) \
                (a_GCPhysOut) = WalkSlat.GCPhys; \
            else \
            { \
                *(a_pWalk) = WalkSlat; \
                return rcX; \
            } \
        } \
    } while (0)
# endif

    /*
     * Init the walking structures.
     */
    RT_ZERO(*pWalk);
    RT_ZERO(*pGstWalk);
    pWalk->GCPtr = GCPtr;

# if PGM_GST_TYPE == PGM_TYPE_32BIT \
  || PGM_GST_TYPE == PGM_TYPE_PAE
    /*
     * Boundary check for PAE and 32-bit (prevents trouble further down).
     */
    if (RT_UNLIKELY(GCPtr >= _4G))
        return PGM_GST_NAME(WalkReturnNotPresent)(pVCpu, pWalk, 8);
# endif

    uint64_t fEffective;
    {
# if PGM_GST_TYPE == PGM_TYPE_AMD64
        /*
         * The PML4 table.
         */
        rc = pgmGstGetLongModePML4PtrEx(pVCpu, &pGstWalk->pPml4);
        if (RT_SUCCESS(rc)) { /* probable */ }
        else return PGM_GST_NAME(WalkReturnBadPhysAddr)(pVCpu, pWalk, 4, rc);

        PX86PML4E pPml4e;
        pGstWalk->pPml4e  = pPml4e  = &pGstWalk->pPml4->a[(GCPtr >> X86_PML4_SHIFT) & X86_PML4_MASK];
        X86PML4E  Pml4e;
        pGstWalk->Pml4e.u = Pml4e.u = pPml4e->u;

        if (GST_IS_PGENTRY_PRESENT(pVCpu, Pml4e)) { /* probable */ }
        else return PGM_GST_NAME(WalkReturnNotPresent)(pVCpu, pWalk, 4);

        if (RT_LIKELY(GST_IS_PML4E_VALID(pVCpu, Pml4e))) { /* likely */ }
        else return PGM_GST_NAME(WalkReturnRsvdError)(pVCpu, pWalk, 4);

        fEffective = Pml4e.u & (  X86_PML4E_P | X86_PML4E_RW | X86_PML4E_US | X86_PML4E_PWT | X86_PML4E_PCD | X86_PML4E_A
                                | X86_PML4E_NX);
        pWalk->fEffective = fEffective;

        /*
         * The PDPT.
         */
        RTGCPHYS GCPhysPdpt = Pml4e.u & X86_PML4E_PG_MASK;
#  ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
        PGM_GST_SLAT_WALK(pVCpu, GCPtr, GCPhysPdpt, GCPhysPdpt, pWalk);
#  endif
        rc = PGM_GCPHYS_2_PTR_BY_VMCPU(pVCpu, GCPhysPdpt, &pGstWalk->pPdpt);
        if (RT_SUCCESS(rc)) { /* probable */ }
        else return PGM_GST_NAME(WalkReturnBadPhysAddr)(pVCpu, pWalk, 3, rc);

# elif PGM_GST_TYPE == PGM_TYPE_PAE
        rc = pgmGstGetPaePDPTPtrEx(pVCpu, &pGstWalk->pPdpt);
        if (RT_SUCCESS(rc)) { /* probable */ }
        else return PGM_GST_NAME(WalkReturnBadPhysAddr)(pVCpu, pWalk, 8, rc);
# endif
    }
    {
# if PGM_GST_TYPE == PGM_TYPE_AMD64 || PGM_GST_TYPE == PGM_TYPE_PAE
        PX86PDPE pPdpe;
        pGstWalk->pPdpe  = pPdpe  = &pGstWalk->pPdpt->a[(GCPtr >> GST_PDPT_SHIFT) & GST_PDPT_MASK];
        X86PDPE  Pdpe;
        pGstWalk->Pdpe.u = Pdpe.u = pPdpe->u;

        if (GST_IS_PGENTRY_PRESENT(pVCpu, Pdpe)) { /* probable */ }
        else return PGM_GST_NAME(WalkReturnNotPresent)(pVCpu, pWalk, 3);

        if (RT_LIKELY(GST_IS_PDPE_VALID(pVCpu, Pdpe))) { /* likely */ }
        else return PGM_GST_NAME(WalkReturnRsvdError)(pVCpu, pWalk, 3);

#  if PGM_GST_TYPE == PGM_TYPE_AMD64
        fEffective &= (Pdpe.u & (  X86_PDPE_P   | X86_PDPE_RW  | X86_PDPE_US
                                 | X86_PDPE_PWT | X86_PDPE_PCD | X86_PDPE_A));
        fEffective |= Pdpe.u & X86_PDPE_LM_NX;
#  else
        /*
         * NX in the legacy-mode PAE PDPE is reserved. The valid check above ensures the NX bit is not set.
         * The RW, US, A bits MBZ in PAE PDPTE entries but must be 1 the way we compute cumulative (effective) access rights.
         */
        Assert(!(Pdpe.u & X86_PDPE_LM_NX));
        fEffective = X86_PDPE_P | X86_PDPE_RW  | X86_PDPE_US | X86_PDPE_A
                   | (Pdpe.u & (X86_PDPE_PWT | X86_PDPE_PCD));
#  endif
        pWalk->fEffective = fEffective;

        /*
         * The PD.
         */
        RTGCPHYS GCPhysPd = Pdpe.u & X86_PDPE_PG_MASK;
#  ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
        PGM_GST_SLAT_WALK(pVCpu, GCPtr, GCPhysPd, GCPhysPd, pWalk);
#  endif
        rc = PGM_GCPHYS_2_PTR_BY_VMCPU(pVCpu, GCPhysPd, &pGstWalk->pPd);
        if (RT_SUCCESS(rc)) { /* probable */ }
        else return PGM_GST_NAME(WalkReturnBadPhysAddr)(pVCpu, pWalk, 2, rc);

# elif PGM_GST_TYPE == PGM_TYPE_32BIT
        rc = pgmGstGet32bitPDPtrEx(pVCpu, &pGstWalk->pPd);
        if (RT_SUCCESS(rc)) { /* probable */ }
        else return PGM_GST_NAME(WalkReturnBadPhysAddr)(pVCpu, pWalk, 8, rc);
# endif
    }
    {
        PGSTPDE pPde;
        pGstWalk->pPde  = pPde  = &pGstWalk->pPd->a[(GCPtr >> GST_PD_SHIFT) & GST_PD_MASK];
        GSTPDE  Pde;
        pGstWalk->Pde.u = Pde.u = pPde->u;
        if (GST_IS_PGENTRY_PRESENT(pVCpu, Pde)) { /* probable */ }
        else return PGM_GST_NAME(WalkReturnNotPresent)(pVCpu, pWalk, 2);
        if ((Pde.u & X86_PDE_PS) && GST_IS_PSE_ACTIVE(pVCpu))
        {
            if (RT_LIKELY(GST_IS_BIG_PDE_VALID(pVCpu, Pde))) { /* likely */ }
            else return PGM_GST_NAME(WalkReturnRsvdError)(pVCpu, pWalk, 2);

            /*
             * We're done.
             */
# if PGM_GST_TYPE == PGM_TYPE_32BIT
            fEffective  = Pde.u & (X86_PDE4M_P | X86_PDE4M_RW | X86_PDE4M_US | X86_PDE4M_PWT | X86_PDE4M_PCD | X86_PDE4M_A);
# else
            fEffective &= Pde.u & (X86_PDE4M_P | X86_PDE4M_RW | X86_PDE4M_US | X86_PDE4M_PWT | X86_PDE4M_PCD | X86_PDE4M_A);
            fEffective |= Pde.u & X86_PDE2M_PAE_NX;
# endif
            fEffective |= Pde.u & (X86_PDE4M_D | X86_PDE4M_G);
            fEffective |= (Pde.u & X86_PDE4M_PAT) >> X86_PDE4M_PAT_SHIFT;
            pWalk->fEffective = fEffective;
            Assert(GST_IS_NX_ACTIVE(pVCpu) || !(fEffective & PGM_PTATTRS_NX_MASK));
            Assert(fEffective & PGM_PTATTRS_R_MASK);

            pWalk->fBigPage   = true;
            pWalk->fSucceeded = true;
            RTGCPHYS GCPhysPde = GST_GET_BIG_PDE_GCPHYS(pVCpu->CTX_SUFF(pVM), Pde)
                               | (GCPtr & GST_BIG_PAGE_OFFSET_MASK);
# ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
            PGM_GST_SLAT_WALK(pVCpu, GCPtr, GCPhysPde, GCPhysPde, pWalk);
# endif
            pWalk->GCPhys     = GCPhysPde;
            PGM_A20_APPLY_TO_VAR(pVCpu, pWalk->GCPhys); /** @todo why do we apply it here and not below?!? */
            return VINF_SUCCESS;
        }

        if (RT_UNLIKELY(!GST_IS_PDE_VALID(pVCpu, Pde)))
            return PGM_GST_NAME(WalkReturnRsvdError)(pVCpu, pWalk, 2);
# if PGM_GST_TYPE == PGM_TYPE_32BIT
        fEffective  = Pde.u & (X86_PDE_P | X86_PDE_RW | X86_PDE_US | X86_PDE_PWT | X86_PDE_PCD | X86_PDE_A);
# else
        fEffective &= Pde.u & (X86_PDE_P | X86_PDE_RW | X86_PDE_US | X86_PDE_PWT | X86_PDE_PCD | X86_PDE_A);
        fEffective |= Pde.u & X86_PDE_PAE_NX;
# endif
        pWalk->fEffective = fEffective;

        /*
         * The PT.
         */
        RTGCPHYS GCPhysPt = GST_GET_PDE_GCPHYS(Pde);
# ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
        PGM_GST_SLAT_WALK(pVCpu, GCPtr, GCPhysPt, GCPhysPt, pWalk);
# endif
        rc = PGM_GCPHYS_2_PTR_BY_VMCPU(pVCpu, GCPhysPt, &pGstWalk->pPt);
        if (RT_SUCCESS(rc)) { /* probable */ }
        else return PGM_GST_NAME(WalkReturnBadPhysAddr)(pVCpu, pWalk, 1, rc);
    }
    {
        PGSTPTE pPte;
        pGstWalk->pPte  = pPte  = &pGstWalk->pPt->a[(GCPtr >> GST_PT_SHIFT) & GST_PT_MASK];
        GSTPTE  Pte;
        pGstWalk->Pte.u = Pte.u = pPte->u;

        if (GST_IS_PGENTRY_PRESENT(pVCpu, Pte)) { /* probable */ }
        else return PGM_GST_NAME(WalkReturnNotPresent)(pVCpu, pWalk, 1);

        if (RT_LIKELY(GST_IS_PTE_VALID(pVCpu, Pte))) { /* likely */ }
        else return PGM_GST_NAME(WalkReturnRsvdError)(pVCpu, pWalk, 1);

        /*
         * We're done.
         */
        fEffective &= Pte.u & (X86_PTE_P | X86_PTE_RW | X86_PTE_US | X86_PTE_PWT | X86_PTE_PCD | X86_PTE_A);
        fEffective |= Pte.u & (X86_PTE_D | X86_PTE_PAT | X86_PTE_G);
# if PGM_GST_TYPE != PGM_TYPE_32BIT
        fEffective |= Pte.u & X86_PTE_PAE_NX;
# endif
        pWalk->fEffective = fEffective;
        Assert(GST_IS_NX_ACTIVE(pVCpu) || !(fEffective & PGM_PTATTRS_NX_MASK));
        Assert(fEffective & PGM_PTATTRS_R_MASK);

        pWalk->fSucceeded = true;
        RTGCPHYS GCPhysPte = GST_GET_PTE_GCPHYS(Pte)
                           | (GCPtr & GUEST_PAGE_OFFSET_MASK);
# ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
        PGM_GST_SLAT_WALK(pVCpu, GCPtr, GCPhysPte, GCPhysPte, pWalk);
# endif
        pWalk->GCPhys     = GCPhysPte;
        return VINF_SUCCESS;
    }
}

#endif /* 32BIT, PAE, AMD64 */

/**
 * Gets effective Guest OS page information.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtr       Guest Context virtual address of the page.
 * @param   pWalk       Where to store the page walk info.
 * @thread  EMT(pVCpu)
 */
PGM_GST_DECL(int, GetPage)(PVMCPUCC pVCpu, RTGCPTR GCPtr, PPGMPTWALK pWalk)
{
#if PGM_GST_TYPE == PGM_TYPE_REAL \
 || PGM_GST_TYPE == PGM_TYPE_PROT

# ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
    if (pVCpu->pgm.s.enmGuestSlatMode == PGMSLAT_EPT)
    {
        PGMPTWALK    WalkSlat;
        PGMPTWALKGST WalkGstSlat;
        int const rc = pgmGstSlatWalk(pVCpu, GCPtr, true /* fIsLinearAddrValid */, GCPtr, &WalkSlat, &WalkGstSlat);
        if (RT_SUCCESS(rc))
        {
            RT_ZERO(*pWalk);
            pWalk->fSucceeded = true;
            pWalk->GCPtr      = GCPtr;
            pWalk->GCPhys     = WalkSlat.GCPhys & ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK;
            pWalk->fEffective = X86_PTE_P | X86_PTE_RW | X86_PTE_US;
        }
        else
            *pWalk = WalkSlat;
        return rc;
    }
# endif

    /*
     * Fake it.
     */
    RT_ZERO(*pWalk);
    pWalk->fSucceeded = true;
    pWalk->GCPtr      = GCPtr;
    pWalk->GCPhys     = GCPtr & ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK;
    pWalk->fEffective = X86_PTE_P | X86_PTE_RW | X86_PTE_US;
    NOREF(pVCpu);
    return VINF_SUCCESS;

#elif PGM_GST_TYPE == PGM_TYPE_32BIT \
   || PGM_GST_TYPE == PGM_TYPE_PAE \
   || PGM_GST_TYPE == PGM_TYPE_AMD64

    GSTPTWALK GstWalk;
    int rc = PGM_GST_NAME(Walk)(pVCpu, GCPtr, pWalk, &GstWalk);
    if (RT_FAILURE(rc))
        return rc;

    Assert(pWalk->fSucceeded);
    Assert(pWalk->GCPtr == GCPtr);

    PGMPTATTRS fFlags;
    if (!pWalk->fBigPage)
        fFlags = (GstWalk.Pte.u & ~(GST_PTE_PG_MASK | X86_PTE_RW | X86_PTE_US))                      /* NX not needed */
               | (pWalk->fEffective & (PGM_PTATTRS_W_MASK | PGM_PTATTRS_US_MASK))
# if PGM_WITH_NX(PGM_GST_TYPE, PGM_GST_TYPE)
               | (pWalk->fEffective & PGM_PTATTRS_NX_MASK)
# endif
                 ;
    else
        fFlags = (GstWalk.Pde.u & ~(GST_PTE_PG_MASK | X86_PDE4M_RW | X86_PDE4M_US | X86_PDE4M_PS))   /* NX not needed */
               | (pWalk->fEffective & (PGM_PTATTRS_W_MASK | PGM_PTATTRS_US_MASK | PGM_PTATTRS_PAT_MASK))
# if PGM_WITH_NX(PGM_GST_TYPE, PGM_GST_TYPE)
               | (pWalk->fEffective & PGM_PTATTRS_NX_MASK)
# endif
               ;

    pWalk->GCPhys    &= ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK;
    pWalk->fEffective = fFlags;
    return VINF_SUCCESS;

#else
# error "shouldn't be here!"
    /* something else... */
    return VERR_NOT_SUPPORTED;
#endif
}


/* x x x x x x x x */
/* x x x x x x x x */

#if defined(VBOX_WITH_NESTED_HWVIRT_VMX_EPT) || defined(VBOX_WITH_NESTED_HWVIRT_SVM_XXX) || defined(DOXYGEN_RUNNING)
/** Converts regular style walk info to fast style. */
DECL_FORCE_INLINE(void) PGM_GST_NAME(ConvertPtWalkToFast)(PGMPTWALK const *pSrc, PPGMPTWALKFAST pDst)
{
    pDst->GCPtr              = pSrc->GCPtr;
    pDst->GCPhys             = pSrc->GCPhys;
    pDst->GCPhysNested       = pSrc->GCPhysNested;
    pDst->fInfo              = (pSrc->fSucceeded         ? PGM_WALKINFO_SUCCEEDED            : 0)
                             | (pSrc->fIsSlat            ? PGM_WALKINFO_IS_SLAT              : 0)
                             | (pSrc->fIsLinearAddrValid ? PGM_WALKINFO_IS_LINEAR_ADDR_VALID : 0);
    pDst->fFailed            = pSrc->fFailed | ((uint32_t)pSrc->uLevel << PGM_WALKFAIL_LEVEL_SHIFT);
    pDst->fEffective         = pSrc->fEffective;
}
#endif


#if PGM_GST_TYPE == PGM_TYPE_32BIT \
 || PGM_GST_TYPE == PGM_TYPE_PAE \
 || PGM_GST_TYPE == PGM_TYPE_AMD64

DECLINLINE(int) PGM_GST_NAME(WalkFastReturnNotPresent)(PVMCPUCC pVCpu, PPGMPTWALKFAST pWalk, uint8_t uLevel)
{
    RT_NOREF(pVCpu);
    pWalk->fFailed = PGM_WALKFAIL_NOT_PRESENT           | ((uint32_t)uLevel << PGM_WALKFAIL_LEVEL_SHIFT);
    return VERR_PAGE_TABLE_NOT_PRESENT;
}

DECLINLINE(int) PGM_GST_NAME(WalkFastReturnBadPhysAddr)(PVMCPUCC pVCpu, PPGMPTWALKFAST pWalk, uint8_t uLevel, int rc)
{
    AssertMsg(rc == VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS, ("%Rrc\n", rc)); RT_NOREF(pVCpu, rc);
    pWalk->fFailed = PGM_WALKFAIL_BAD_PHYSICAL_ADDRESS  | ((uint32_t)uLevel << PGM_WALKFAIL_LEVEL_SHIFT);
    return VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS;
}

DECLINLINE(int) PGM_GST_NAME(WalkFastReturnRsvdError)(PVMCPUCC pVCpu, PPGMPTWALKFAST pWalk, uint8_t uLevel)
{
    RT_NOREF(pVCpu);
    pWalk->fFailed = PGM_WALKFAIL_RESERVED_BITS         | ((uint32_t)uLevel << PGM_WALKFAIL_LEVEL_SHIFT);
    return VERR_RESERVED_PAGE_TABLE_BITS;
}

# if defined(VBOX_WITH_NESTED_HWVIRT_VMX_EPT) || defined(VBOX_WITH_NESTED_HWVIRT_SVM_XXX) || defined(DOXYGEN_RUNNING)
/** @def PGM_GST_SLAT_WALK_FAST
 * Macro to perform guest second-level address translation (EPT or Nested).
 *
 * @param   a_pVCpu         The cross context virtual CPU structure of the calling
 *                          EMT.
 * @param   a_GCPtrNested   The nested-guest linear address that caused the
 *                          second-level translation.
 * @param   a_GCPhysNested  The nested-guest physical address to translate.
 * @param   a_fFinal        Set to @a true if this is the final page table entry
 *                          and effective nested page table flags should be
 *                          merged into PGMPTWALKFAST::fEffective.  Otherwise
 *                          set to @a false and nothing done.
 * @param   a_GCPhysOut     Where to store the guest-physical address (result).
 * @param   a_pWalk         The @a pWalk argument to the function.
 */
#  define PGM_GST_SLAT_WALK_FAST(a_pVCpu, a_GCPtrNested, a_GCPhysNested, a_fFinal, a_GCPhysOut, a_pWalk) \
    do { \
        /** @todo Optimize this. Among other things, WalkSlat can be eliminated. WalkGstSlat is completely pointless. */ \
        /** @todo pass fFlags along as appropriate... */ \
        if (a_enmGuestSlatMode != PGMSLAT_DIRECT) \
        { \
            PGMPTWALK    WalkSlat; \
            PGMPTWALKGST WalkGstSlat; \
            int rcX; \
            if (a_enmGuestSlatMode == PGMSLAT_EPT) \
                rcX = PGM_GST_SLAT_NAME_EPT(Walk)(a_pVCpu, a_GCPhysNested, true /* fIsLinearAddrValid */, a_GCPtrNested, \
                                                  &WalkSlat, &WalkGstSlat.u.Ept); \
            else AssertFailedReturn(VERR_NOT_IMPLEMENTED); \
            if (RT_SUCCESS(rcX)) \
                (a_GCPhysOut) = WalkSlat.GCPhys; \
            else \
            { \
                PGM_GST_NAME(ConvertPtWalkToFast)(&WalkSlat, pWalk); \
                return rcX; \
            } \
            if (a_fFinal) \
            {   /* Merge in the nested paging flags for the final GCPhys. */ \
                if (a_enmGuestSlatMode == PGMSLAT_EPT) \
                    (a_pWalk)->fEffective = ((a_pWalk)->fEffective & ~PGM_PTATTRS_EPT_MASK) \
                                          | (WalkSlat.fEffective & PGM_PTATTRS_EPT_MASK); \
                else AssertFailedReturn(VERR_NOT_IMPLEMENTED); \
            } \
        } \
    } while (0)
# else
#  define PGM_GST_SLAT_WALK_FAST(a_pVCpu, a_GCPtrNested, a_GCPhysNested, a_fFinal, a_GCPhysOut, a_pWalk) do { } while (0)
# endif
# if PGM_GST_TYPE == PGM_TYPE_32BIT
#  define PGM_GST_ENSURE_ENTRY_FLAGS_SET(a_pVCpu, a_fEffective, a_pEntryU, a_OrgEntryU, a_fFlags) do { \
       if (!a_fSetFlags || ((a_OrgEntryU) & (a_fFlags)) == (a_fFlags)) \
       { /* likely */ } \
       else \
       { \
           ASMAtomicOrU32((a_pEntryU), (a_fFlags)); \
           (a_fEffective) |= (a_fFlags); \
       } \
    } while (0)
# else
#  define PGM_GST_ENSURE_ENTRY_FLAGS_SET(a_pVCpu, a_fEffective, a_pEntryU, a_OrgEntryU, a_fFlags) do { \
       if (!a_fSetFlags || ((a_OrgEntryU) & (a_fFlags)) == (a_fFlags)) \
       { /* likely */ } \
       else \
       { \
           ASMAtomicOrU64((a_pEntryU), (a_fFlags)); \
           (a_fEffective) |= (a_fFlags); \
       } \
    } while (0)
# endif

/**
 * Performs a guest page table walk.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PAGE_TABLE_NOT_PRESENT, VERR_RESERVED_PAGE_TABLE_BITS or
 *          VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS on normal failure.
 *          The failure reason is also recorded in PGMPTWALKFAST::fFailed.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling EMT.
 * @param   GCPtr               The guest virtual address to walk by.
 * @param   fFlags              PGMQPAGE_F_XXX.
 *                              This is ignored when @a a_fSetFlags is @c false.
 * @param   pWalk               The page walk info.
 * @tparam  a_enmGuestSlatMode  The SLAT mode of the function.
 * @tparam  a_fSetFlags         Whether to process @a fFlags and set accessed
 *                              and dirty flags accordingly.
 * @thread  EMT(pVCpu)
 */
template<PGMSLAT const a_enmGuestSlatMode = PGMSLAT_DIRECT, bool const a_fSetFlags = false>
DECLINLINE(int) PGM_GST_NAME(WalkFast)(PVMCPUCC pVCpu, RTGCPTR GCPtr, uint32_t fFlags, PPGMPTWALKFAST pWalk)
{
    int rc;

    /*
     * Init the walking structures.
     */
    pWalk->GCPtr        = GCPtr;
    pWalk->GCPhys       = 0;
    pWalk->GCPhysNested = 0;
    pWalk->fInfo        = 0;
    pWalk->fFailed      = 0;
    pWalk->fEffective   = 0;

# if PGM_GST_TYPE == PGM_TYPE_32BIT \
  || PGM_GST_TYPE == PGM_TYPE_PAE
    /*
     * Boundary check for PAE and 32-bit (prevents trouble further down).
     */
    if (RT_LIKELY(GCPtr < _4G))
    { /* extremely likely */ }
    else
        return PGM_GST_NAME(WalkFastReturnNotPresent)(pVCpu, pWalk, 8);
# endif

# if PGM_GST_TYPE == PGM_TYPE_AMD64 || PGM_GST_TYPE == PGM_TYPE_PAE
    PX86PDPT  pPdpt;
# endif
    uint64_t fEffective;
    {
# if PGM_GST_TYPE == PGM_TYPE_AMD64
        /*
         * The PML4 table.
         */
        PX86PML4 pPml4;
        rc = pgmGstGetLongModePML4PtrEx(pVCpu, &pPml4);
        if (RT_SUCCESS(rc)) { /* probable */ }
        else return PGM_GST_NAME(WalkFastReturnBadPhysAddr)(pVCpu, pWalk, 4, rc);

        PX86PML4E pPml4e;
        pPml4e  = &pPml4->a[(GCPtr >> X86_PML4_SHIFT) & X86_PML4_MASK];
        X86PML4E  Pml4e;
        Pml4e.u = ASMAtomicUoReadU64(&pPml4e->u);

        if (GST_IS_PGENTRY_PRESENT(pVCpu, Pml4e)) { /* probable */ }
        else return PGM_GST_NAME(WalkFastReturnNotPresent)(pVCpu, pWalk, 4);

        if (RT_LIKELY(GST_IS_PML4E_VALID(pVCpu, Pml4e))) { /* likely */ }
        else return PGM_GST_NAME(WalkFastReturnRsvdError)(pVCpu, pWalk, 4);

        fEffective = Pml4e.u & (  X86_PML4E_P | X86_PML4E_RW | X86_PML4E_US | X86_PML4E_PWT | X86_PML4E_PCD | X86_PML4E_A
                                | X86_PML4E_NX);
        PGM_GST_ENSURE_ENTRY_FLAGS_SET(a_pVCpu, fEffective, &pPml4e->u, Pml4e.u, X86_PML4E_A);
        pWalk->fEffective = fEffective;

        /*
         * The PDPT.
         */
        RTGCPHYS GCPhysPdpt = Pml4e.u & X86_PML4E_PG_MASK;
        PGM_GST_SLAT_WALK_FAST(pVCpu, GCPtr, GCPhysPdpt, false /*a_fFinal*/, GCPhysPdpt, pWalk);
        rc = pgmPhysGCPhys2CCPtrLockless(pVCpu, GCPhysPdpt, (void **)&pPdpt);
        if (RT_SUCCESS(rc)) { /* probable */ }
        else return PGM_GST_NAME(WalkFastReturnBadPhysAddr)(pVCpu, pWalk, 3, rc);

# elif PGM_GST_TYPE == PGM_TYPE_PAE
        rc = pgmGstGetPaePDPTPtrEx(pVCpu, &pPdpt);
        if (RT_SUCCESS(rc)) { /* probable */ }
        else return PGM_GST_NAME(WalkFastReturnBadPhysAddr)(pVCpu, pWalk, 8, rc);
# endif
    }
    PGSTPD pPd;
    {
# if PGM_GST_TYPE == PGM_TYPE_AMD64 || PGM_GST_TYPE == PGM_TYPE_PAE
        PX86PDPE pPdpe;
        pPdpe  = &pPdpt->a[(GCPtr >> GST_PDPT_SHIFT) & GST_PDPT_MASK];
        X86PDPE  Pdpe;
        Pdpe.u = ASMAtomicUoReadU64(&pPdpe->u);

        if (GST_IS_PGENTRY_PRESENT(pVCpu, Pdpe)) { /* probable */ }
        else return PGM_GST_NAME(WalkFastReturnNotPresent)(pVCpu, pWalk, 3);

        if (RT_LIKELY(GST_IS_PDPE_VALID(pVCpu, Pdpe))) { /* likely */ }
        else return PGM_GST_NAME(WalkFastReturnRsvdError)(pVCpu, pWalk, 3);

#  if PGM_GST_TYPE == PGM_TYPE_AMD64
        fEffective &= (Pdpe.u & (  X86_PDPE_P   | X86_PDPE_RW  | X86_PDPE_US
                                 | X86_PDPE_PWT | X86_PDPE_PCD | X86_PDPE_A));
        fEffective |= Pdpe.u & X86_PDPE_LM_NX;
        PGM_GST_ENSURE_ENTRY_FLAGS_SET(a_pVCpu, fEffective, &pPdpe->u, Pdpe.u, X86_PDE_A);
#  else
        /*
         * NX in the legacy-mode PAE PDPE is reserved. The valid check above ensures the NX bit is not set.
         * The RW, US, A bits MBZ in PAE PDPTE entries but must be 1 the way we compute cumulative (effective) access rights.
         */
        Assert(!(Pdpe.u & X86_PDPE_LM_NX));
        fEffective = X86_PDPE_P | X86_PDPE_RW  | X86_PDPE_US | X86_PDPE_A
                   | (Pdpe.u & (X86_PDPE_PWT | X86_PDPE_PCD));
#  endif
        pWalk->fEffective = fEffective;

        /*
         * The PD.
         */
        RTGCPHYS GCPhysPd = Pdpe.u & X86_PDPE_PG_MASK;
        PGM_GST_SLAT_WALK_FAST(pVCpu, GCPtr, GCPhysPd, false /*a_fFinal*/, GCPhysPd, pWalk);
        rc = pgmPhysGCPhys2CCPtrLockless(pVCpu, GCPhysPd, (void **)&pPd);
        if (RT_SUCCESS(rc)) { /* probable */ }
        else return PGM_GST_NAME(WalkFastReturnBadPhysAddr)(pVCpu, pWalk, 2, rc);

# elif PGM_GST_TYPE == PGM_TYPE_32BIT
        rc = pgmGstGet32bitPDPtrEx(pVCpu, &pPd);
        if (RT_SUCCESS(rc)) { /* probable */ }
        else return PGM_GST_NAME(WalkFastReturnBadPhysAddr)(pVCpu, pWalk, 8, rc);
# endif
    }
    PGSTPT pPt;
    {
        PGSTPDE pPde;
        pPde  = &pPd->a[(GCPtr >> GST_PD_SHIFT) & GST_PD_MASK];
        GSTPDE  Pde;
# if PGM_GST_TYPE != PGM_TYPE_32BIT
        Pde.u = ASMAtomicUoReadU64(&pPde->u);
# else
        Pde.u = ASMAtomicUoReadU32(&pPde->u);
# endif
        if (GST_IS_PGENTRY_PRESENT(pVCpu, Pde)) { /* probable */ }
        else return PGM_GST_NAME(WalkFastReturnNotPresent)(pVCpu, pWalk, 2);
        if ((Pde.u & X86_PDE_PS) && GST_IS_PSE_ACTIVE(pVCpu))
        {
            if (RT_LIKELY(GST_IS_BIG_PDE_VALID(pVCpu, Pde))) { /* likely */ }
            else return PGM_GST_NAME(WalkFastReturnRsvdError)(pVCpu, pWalk, 2);

            /*
             * We're done.
             */
            pWalk->fInfo = PGM_WALKINFO_SUCCEEDED | PGM_WALKINFO_BIG_PAGE;

# if PGM_GST_TYPE == PGM_TYPE_32BIT
            fEffective  = Pde.u & (X86_PDE4M_P | X86_PDE4M_RW | X86_PDE4M_US | X86_PDE4M_PWT | X86_PDE4M_PCD | X86_PDE4M_A);
# else
            fEffective &= Pde.u & (X86_PDE4M_P | X86_PDE4M_RW | X86_PDE4M_US | X86_PDE4M_PWT | X86_PDE4M_PCD | X86_PDE4M_A);
            fEffective |= Pde.u & X86_PDE2M_PAE_NX;
# endif
            fEffective |= Pde.u & (X86_PDE4M_D | X86_PDE4M_G);
            fEffective |= (Pde.u & X86_PDE4M_PAT) >> X86_PDE4M_PAT_SHIFT;

            rc = VINF_SUCCESS;
            if (a_fSetFlags)
            {
                /* We have to validate the access before setting any flags. */
                uint32_t fFailed = 0;
                if ((fFlags & PGMQPAGE_F_USER_MODE) && !(fEffective & X86_PDE4M_US))
                    fFailed |= PGM_WALKFAIL_NOT_ACCESSIBLE_BY_MODE;
                if (fFlags & PGMQPAGE_F_WRITE)
                {
                    if (   (fEffective & X86_PDE4M_RW)
                        || (fFlags & (PGMQPAGE_F_USER_MODE | PGMQPAGE_F_CR0_WP0)) == PGMQPAGE_F_CR0_WP0)
                    { /* likely*/ }
                    else fFailed |= PGM_WALKFAIL_NOT_WRITABLE;
                }
# if PGM_GST_TYPE != PGM_TYPE_32BIT
                else if (fFlags & PGMQPAGE_F_EXECUTE)
                {
                    if (!(fEffective & X86_PDE2M_PAE_NX) || !pVCpu->pgm.s.fNoExecuteEnabled) { /* likely */ }
                    else fFailed |= PGM_WALKFAIL_NOT_EXECUTABLE;
                }
# endif
                if (fFailed == 0)
                {
                    if (!(fFlags & PGMQPAGE_F_WRITE))
                        PGM_GST_ENSURE_ENTRY_FLAGS_SET(a_pVCpu, fEffective, &pPde->u, Pde.u, X86_PDE4M_A);
                    else
                        PGM_GST_ENSURE_ENTRY_FLAGS_SET(a_pVCpu, fEffective, &pPde->u, Pde.u, X86_PDE4M_A | X86_PDE4M_D);
                }
                else
                {
                    pWalk->fFailed = fFailed | (2U << PGM_WALKFAIL_LEVEL_SHIFT);
                    pWalk->fInfo   = PGM_WALKINFO_BIG_PAGE;
                    rc = VERR_ACCESS_DENIED;
                }
            }

            pWalk->fEffective = fEffective;
            Assert(GST_IS_NX_ACTIVE(pVCpu) || !(fEffective & PGM_PTATTRS_NX_MASK));
            Assert(fEffective & PGM_PTATTRS_R_MASK);

            RTGCPHYS GCPhysPde = GST_GET_BIG_PDE_GCPHYS(pVCpu->CTX_SUFF(pVM), Pde)
                               | (GCPtr & GST_BIG_PAGE_OFFSET_MASK);
            PGM_GST_SLAT_WALK_FAST(pVCpu, GCPtr, GCPhysPde, true /*a_fFinal*/, GCPhysPde, pWalk);
            pWalk->GCPhys     = GCPhysPde;
            PGM_A20_APPLY_TO_VAR(pVCpu, pWalk->GCPhys); /** @todo why do we apply it here and not below?!? */
            return rc;
        }

        if (RT_UNLIKELY(!GST_IS_PDE_VALID(pVCpu, Pde)))
            return PGM_GST_NAME(WalkFastReturnRsvdError)(pVCpu, pWalk, 2);
# if PGM_GST_TYPE == PGM_TYPE_32BIT
        fEffective  = Pde.u & (X86_PDE_P | X86_PDE_RW | X86_PDE_US | X86_PDE_PWT | X86_PDE_PCD | X86_PDE_A);
# else
        fEffective &= Pde.u & (X86_PDE_P | X86_PDE_RW | X86_PDE_US | X86_PDE_PWT | X86_PDE_PCD | X86_PDE_A);
        fEffective |= Pde.u & X86_PDE_PAE_NX;
# endif
        PGM_GST_ENSURE_ENTRY_FLAGS_SET(a_pVCpu, fEffective, &pPde->u, Pde.u, X86_PDE_A);
        pWalk->fEffective = fEffective;

        /*
         * The PT.
         */
        RTGCPHYS GCPhysPt = GST_GET_PDE_GCPHYS(Pde);
        PGM_GST_SLAT_WALK_FAST(pVCpu, GCPtr, GCPhysPt, false /*a_fFinal*/, GCPhysPt, pWalk);
        rc = pgmPhysGCPhys2CCPtrLockless(pVCpu, GCPhysPt, (void **)&pPt);
        if (RT_SUCCESS(rc)) { /* probable */ }
        else return PGM_GST_NAME(WalkFastReturnBadPhysAddr)(pVCpu, pWalk, 1, rc);
    }
    {
        PGSTPTE pPte;
        pPte  = &pPt->a[(GCPtr >> GST_PT_SHIFT) & GST_PT_MASK];
        GSTPTE  Pte;
# if PGM_GST_TYPE != PGM_TYPE_32BIT
        Pte.u = ASMAtomicUoReadU64(&pPte->u);
# else
        Pte.u = ASMAtomicUoReadU32(&pPte->u);
# endif

        if (GST_IS_PGENTRY_PRESENT(pVCpu, Pte)) { /* probable */ }
        else return PGM_GST_NAME(WalkFastReturnNotPresent)(pVCpu, pWalk, 1);

        if (RT_LIKELY(GST_IS_PTE_VALID(pVCpu, Pte))) { /* likely */ }
        else return PGM_GST_NAME(WalkFastReturnRsvdError)(pVCpu, pWalk, 1);

        /*
         * We're done.
         */
        pWalk->fInfo = PGM_WALKINFO_SUCCEEDED;

        fEffective &= Pte.u & (X86_PTE_P | X86_PTE_RW | X86_PTE_US | X86_PTE_PWT | X86_PTE_PCD | X86_PTE_A);
# if PGM_GST_TYPE != PGM_TYPE_32BIT
        fEffective |= Pte.u & (X86_PTE_D | X86_PTE_PAT | X86_PTE_G | X86_PTE_PAE_NX);
# else
        fEffective |= Pte.u & (X86_PTE_D | X86_PTE_PAT | X86_PTE_G);
# endif

        rc = VINF_SUCCESS;
        if (a_fSetFlags)
        {
            /* We have to validate the access before setting any flags. */
            uint32_t fFailed = 0;
            if ((fFlags & PGMQPAGE_F_USER_MODE) && !(fEffective & X86_PTE_US))
                fFailed |= PGM_WALKFAIL_NOT_ACCESSIBLE_BY_MODE;
            if (fFlags & PGMQPAGE_F_WRITE)
            {
                if ((fEffective & X86_PTE_RW) || (fFlags & (PGMQPAGE_F_USER_MODE | PGMQPAGE_F_CR0_WP0)) == PGMQPAGE_F_CR0_WP0)
                { /* likely*/ }
                else fFailed |= PGM_WALKFAIL_NOT_WRITABLE;
            }
# if PGM_GST_TYPE != PGM_TYPE_32BIT
            else if (fFlags & PGMQPAGE_F_EXECUTE)
            {
                if (!(fEffective & X86_PTE_PAE_NX) || !pVCpu->pgm.s.fNoExecuteEnabled) { /* likely */ }
                else fFailed |= PGM_WALKFAIL_NOT_EXECUTABLE;
            }
# endif
            if (fFailed == 0)
            {
                if (!(fFlags & PGMQPAGE_F_WRITE))
                    PGM_GST_ENSURE_ENTRY_FLAGS_SET(a_pVCpu, fEffective, &pPte->u, Pte.u, X86_PTE_A);
                else
                    PGM_GST_ENSURE_ENTRY_FLAGS_SET(a_pVCpu, fEffective, &pPte->u, Pte.u, X86_PTE_A | X86_PTE_D);
            }
            else
            {
                pWalk->fFailed = fFailed | (1U << PGM_WALKFAIL_LEVEL_SHIFT);
                pWalk->fInfo   = 0;
                rc = VERR_ACCESS_DENIED;
            }
        }

        pWalk->fEffective = fEffective;
        Assert(GST_IS_NX_ACTIVE(pVCpu) || !(fEffective & PGM_PTATTRS_NX_MASK));
        Assert(fEffective & PGM_PTATTRS_R_MASK);

        RTGCPHYS GCPhysPte = GST_GET_PTE_GCPHYS(Pte)
                           | (GCPtr & GUEST_PAGE_OFFSET_MASK);
        PGM_GST_SLAT_WALK_FAST(pVCpu, GCPtr, GCPhysPte, true /*a_fFinal*/, GCPhysPte, pWalk);
        pWalk->GCPhys     = GCPhysPte;
        return rc;
    }
}

# undef PGM_GST_SLAT_WALK_FAST
# undef PGM_GST_ENSURE_ENTRY_FLAGS_SET

#endif /* 32BIT, PAE, AMD64 */

/**
 * Guest virtual to guest physical + info translation, the faster and better
 * version.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtr       Guest Context virtual address of the page.
 * @param   fFlags      PGMQPAGE_F_XXX
 * @param   pWalk       Where to store the page walk info.
 * @thread  EMT(pVCpu)
 */
#define PGM_GET_PAGE_F_WRITE
#define PGM_GET_PAGE_F_READ
PGM_GST_DECL(int, QueryPageFast)(PVMCPUCC pVCpu, RTGCPTR GCPtr, uint32_t fFlags, PPGMPTWALKFAST pWalk)
{
#if PGM_GST_TYPE == PGM_TYPE_REAL \
 || PGM_GST_TYPE == PGM_TYPE_PROT

# ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
    if (pVCpu->pgm.s.enmGuestSlatMode == PGMSLAT_EPT)
    {
        /** @todo optimize this case as well.   */
        /** @todo pass fFlags along. */
        PGMPTWALK    WalkSlat;
        PGMPTWALKGST WalkGstSlat;
        int const rc = pgmGstSlatWalk(pVCpu, GCPtr, true /* fIsLinearAddrValid */, GCPtr, &WalkSlat, &WalkGstSlat);
        if (RT_SUCCESS(rc))
        {
            PGMPTWALKFAST_ZERO(pWalk);
            pWalk->GCPtr        = GCPtr;
            pWalk->GCPhys       = WalkSlat.GCPhys;
            pWalk->GCPhysNested = 0;
            pWalk->fInfo        = PGM_WALKINFO_SUCCEEDED;
            pWalk->fFailed      = PGM_WALKFAIL_SUCCESS;
            pWalk->fEffective   = X86_PTE_P | X86_PTE_RW | X86_PTE_US | X86_PTE_A | X86_PTE_D;
        }
        else
            PGM_GST_NAME(ConvertPtWalkToFast)(&WalkSlat, pWalk);
        return rc;
    }
# endif

    /*
     * Fake it.
     */
    pWalk->GCPtr        = GCPtr;
    pWalk->GCPhys       = GCPtr;
    pWalk->GCPhysNested = 0;
    pWalk->fInfo        = PGM_WALKINFO_SUCCEEDED;
    pWalk->fFailed      = PGM_WALKFAIL_SUCCESS;
    pWalk->fEffective   = X86_PTE_P | X86_PTE_RW | X86_PTE_US | X86_PTE_A | X86_PTE_D;
    RT_NOREF(pVCpu, fFlags);
    return VINF_SUCCESS;

#elif PGM_GST_TYPE == PGM_TYPE_32BIT \
   || PGM_GST_TYPE == PGM_TYPE_PAE \
   || PGM_GST_TYPE == PGM_TYPE_AMD64

    int rc;
# if defined(VBOX_WITH_NESTED_HWVIRT_VMX_EPT) || defined(VBOX_WITH_NESTED_HWVIRT_SVM_XXX)
    switch (pVCpu->pgm.s.enmGuestSlatMode)
    {
        case PGMSLAT_DIRECT:
# endif
            if (fFlags)
                rc = PGM_GST_NAME(WalkFast)<PGMSLAT_DIRECT, true>(pVCpu, GCPtr, fFlags, pWalk);
            else
                rc = PGM_GST_NAME(WalkFast)<PGMSLAT_DIRECT, false>(pVCpu, GCPtr, 0, pWalk);
# if defined(VBOX_WITH_NESTED_HWVIRT_VMX_EPT) || defined(VBOX_WITH_NESTED_HWVIRT_SVM_XXX)
            break;
#  ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
        case PGMSLAT_EPT:
            if (fFlags)
                rc = PGM_GST_NAME(WalkFast)<PGMSLAT_EPT, true>(pVCpu, GCPtr, fFlags, pWalk);
            else
                rc = PGM_GST_NAME(WalkFast)<PGMSLAT_EPT, false>(pVCpu, GCPtr, 0, pWalk);
            break;
#  endif
#  ifdef VBOX_WITH_NESTED_HWVIRT_SVM_XXX
        case PGMSLAT_32BIT:
            if (fFlags)
                rc = PGM_GST_NAME(WalkFast)<PGMSLAT_32BIT, true>(pVCpu, GCPtr, fFlags, pWalk);
            else
                rc = PGM_GST_NAME(WalkFast)<PGMSLAT_32BIT, false>(pVCpu, GCPtr, 0, pWalk);
            break;
        case PGMSLAT_PAE:
            if (fFlags)
                rc = PGM_GST_NAME(WalkFast)<PGMSLAT_PAE, true>(pVCpu, GCPtr, fFlags, pWalk);
            else
                rc = PGM_GST_NAME(WalkFast)<PGMSLAT_PAE, false>(pVCpu, GCPtr, 0, pWalk);
            break;
        case PGMSLAT_AMD64:
            if (fFlags)
                rc = PGM_GST_NAME(WalkFast)<PGMSLAT_AMD64, true>(pVCpu, GCPtr, fFlags, pWalk);
            else
                rc = PGM_GST_NAME(WalkFast)<PGMSLAT_AMD64, false>(pVCpu, GCPtr, 0, pWalk);
            break;
#  endif
        default:
            AssertFailedReturn(VERR_INTERNAL_ERROR_4);
    }
# endif
    if (RT_SUCCESS(rc))
    {
        Assert(pWalk->fInfo & PGM_WALKINFO_SUCCEEDED);
        Assert(pWalk->GCPtr == GCPtr);
        Assert((pWalk->GCPhys & GUEST_PAGE_OFFSET_MASK) == (GCPtr & GUEST_PAGE_OFFSET_MASK));
        return VINF_SUCCESS;
    }
    return rc;

#else
# error "shouldn't be here!"
    /* something else... */
    return VERR_NOT_SUPPORTED;
#endif
}

/* x x x x x x x x */
/* x x x x x x x x */

/**
 * Modify page flags for a range of pages in the guest's tables
 *
 * The existing flags are ANDed with the fMask and ORed with the fFlags.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtr       Virtual address of the first page in the range. Page aligned!
 * @param   cb          Size (in bytes) of the page range to apply the modification to. Page aligned!
 * @param   fFlags      The OR  mask - page flags X86_PTE_*, excluding the page mask of course.
 * @param   fMask       The AND mask - page flags X86_PTE_*.
 */
PGM_GST_DECL(int, ModifyPage)(PVMCPUCC pVCpu, RTGCPTR GCPtr, size_t cb, uint64_t fFlags, uint64_t fMask)
{
    Assert((cb & GUEST_PAGE_OFFSET_MASK) == 0); RT_NOREF_PV(cb);

#if PGM_GST_TYPE == PGM_TYPE_32BIT \
 || PGM_GST_TYPE == PGM_TYPE_PAE \
 || PGM_GST_TYPE == PGM_TYPE_AMD64
    for (;;)
    {
        PGMPTWALK Walk;
        GSTPTWALK GstWalk;
        int rc = PGM_GST_NAME(Walk)(pVCpu, GCPtr, &Walk, &GstWalk);
        if (RT_FAILURE(rc))
            return rc;

        if (!Walk.fBigPage)
        {
            /*
             * 4KB Page table, process
             *
             * Walk pages till we're done.
             */
            unsigned iPTE = (GCPtr >> GST_PT_SHIFT) & GST_PT_MASK;
            while (iPTE < RT_ELEMENTS(GstWalk.pPt->a))
            {
                GSTPTE Pte = GstWalk.pPt->a[iPTE];
                Pte.u = (Pte.u & (fMask | X86_PTE_PAE_PG_MASK))
                      | (fFlags & ~GST_PTE_PG_MASK);
                GstWalk.pPt->a[iPTE] = Pte;

                /* next page */
                cb -= GUEST_PAGE_SIZE;
                if (!cb)
                    return VINF_SUCCESS;
                GCPtr += GUEST_PAGE_SIZE;
                iPTE++;
            }
        }
        else
        {
            /*
             * 2/4MB Page table
             */
            GSTPDE PdeNew;
# if PGM_GST_TYPE == PGM_TYPE_32BIT
            PdeNew.u = (GstWalk.Pde.u & (fMask | ((fMask & X86_PTE_PAT) << X86_PDE4M_PAT_SHIFT) | GST_PDE_BIG_PG_MASK | X86_PDE4M_PG_HIGH_MASK | X86_PDE4M_PS))
# else
            PdeNew.u = (GstWalk.Pde.u & (fMask | ((fMask & X86_PTE_PAT) << X86_PDE4M_PAT_SHIFT) | GST_PDE_BIG_PG_MASK | X86_PDE4M_PS))
# endif
                     | (fFlags & ~GST_PTE_PG_MASK)
                     | ((fFlags & X86_PTE_PAT) << X86_PDE4M_PAT_SHIFT);
            *GstWalk.pPde = PdeNew;

            /* advance */
            const unsigned cbDone = GST_BIG_PAGE_SIZE - (GCPtr & GST_BIG_PAGE_OFFSET_MASK);
            if (cbDone >= cb)
                return VINF_SUCCESS;
            cb    -= cbDone;
            GCPtr += cbDone;
        }
    }

#else
    /* real / protected mode: ignore. */
    NOREF(pVCpu); NOREF(GCPtr); NOREF(fFlags); NOREF(fMask);
    return VINF_SUCCESS;
#endif
}


#ifdef IN_RING3
/**
 * Relocate any GC pointers related to guest mode paging.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   offDelta    The relocation offset.
 */
PGM_GST_DECL(int, Relocate)(PVMCPUCC pVCpu, RTGCPTR offDelta)
{
    RT_NOREF(pVCpu, offDelta);
    return VINF_SUCCESS;
}
#endif
