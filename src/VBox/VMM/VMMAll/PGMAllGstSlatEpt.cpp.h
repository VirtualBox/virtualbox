/* $Id: PGMAllGstSlatEpt.cpp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox - Page Manager, Guest EPT SLAT - All context code.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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

#if PGM_SLAT_TYPE != PGM_SLAT_TYPE_EPT
# error "Unsupported SLAT type."
#endif

/**
 * Checks if the EPT PTE permissions are valid.
 *
 * @returns @c true if valid, @c false otherwise.
 * @param   pVCpu   The cross context virtual CPU structure of the calling EMT.
 * @param   uEntry  The EPT page table entry to check.
 *
 * @remarks Current this ASSUMES @c uEntry is present (debug asserted)!
 */
DECLINLINE(bool) PGM_GST_SLAT_NAME_EPT(WalkIsPermValid)(PCVMCPUCC pVCpu, uint64_t uEntry)
{
    if (!(uEntry & EPT_E_READ))
    {
        if (uEntry & EPT_E_WRITE)
            return false;

        /*
         * Currently all callers of this function check for the present mask prior
         * to calling this function. Hence, the execute bit must be set now.
         */
        Assert(uEntry & EPT_E_EXECUTE);
        Assert(!pVCpu->CTX_SUFF(pVM)->cpum.ro.GuestFeatures.fVmxModeBasedExecuteEpt);
        if (pVCpu->pgm.s.uEptVpidCapMsr & VMX_BF_EPT_VPID_CAP_EXEC_ONLY_MASK)
            return true;
        return false;
    }
    return true;
}


/**
 * Checks if the EPT memory type is valid.
 *
 * @returns @c true if valid, @c false otherwise.
 * @param   uEntry      The EPT page table entry to check.
 * @param   uLevel      The page table walk level.
 */
DECLINLINE(bool) PGM_GST_SLAT_NAME_EPT(WalkIsMemTypeValid)(uint64_t uEntry, uint8_t uLevel)
{
    Assert(uLevel <= 3 && uLevel >= 1); NOREF(uLevel);
    uint8_t const fEptMemTypeMask = uEntry & VMX_BF_EPT_PT_MEMTYPE_MASK;
    switch (fEptMemTypeMask)
    {
        case EPT_E_MEMTYPE_WB:
        case EPT_E_MEMTYPE_UC:
        case EPT_E_MEMTYPE_WP:
        case EPT_E_MEMTYPE_WT:
        case EPT_E_MEMTYPE_WC:
            return true;
    }
    return false;
}


/**
 * Updates page walk result info when a not-present page is encountered.
 *
 * @returns VERR_PAGE_TABLE_NOT_PRESENT.
 * @param   pVCpu   The cross context virtual CPU structure of the calling EMT.
 * @param   pWalk   The page walk info to update.
 * @param   uEntry  The EPT PTE that is not present.
 * @param   uLevel  The page table walk level.
 */
DECLINLINE(int) PGM_GST_SLAT_NAME_EPT(WalkReturnNotPresent)(PCVMCPUCC pVCpu, PPGMPTWALK pWalk, uint64_t uEntry, uint8_t uLevel)
{
    static PGMWALKFAIL const s_afEptViolations[] = { PGM_WALKFAIL_EPT_VIOLATION, PGM_WALKFAIL_EPT_VIOLATION_CONVERTIBLE };
    uint8_t const fEptVeSupported  = pVCpu->CTX_SUFF(pVM)->cpum.ro.GuestFeatures.fVmxEptXcptVe;
    uint8_t const fConvertible     = RT_BOOL(uLevel == 1 || (uEntry & EPT_E_BIT_LEAF));
    uint8_t const idxViolationType = fEptVeSupported & fConvertible & !RT_BF_GET(uEntry, VMX_BF_EPT_PT_SUPPRESS_VE);

    pWalk->fNotPresent = true;
    pWalk->uLevel      = uLevel;
    pWalk->fFailed     = s_afEptViolations[idxViolationType] | ((uint32_t)uLevel << PGM_WALKFAIL_LEVEL_SHIFT);
    return VERR_PAGE_TABLE_NOT_PRESENT;
}


/**
 * Updates page walk result info when a bad physical address is encountered.
 *
 * @returns VERR_PAGE_TABLE_NOT_PRESENT .
 * @param   pVCpu   The cross context virtual CPU structure of the calling EMT.
 * @param   pWalk   The page walk info to update.
 * @param   uLevel  The page table walk level.
 * @param   rc      The error code that caused this bad physical address situation.
 */
DECLINLINE(int) PGM_GST_SLAT_NAME_EPT(WalkReturnBadPhysAddr)(PCVMCPUCC pVCpu, PPGMPTWALK pWalk, uint8_t uLevel, int rc)
{
    AssertMsg(rc == VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS, ("%Rrc\n", rc)); NOREF(rc); NOREF(pVCpu);
    pWalk->fBadPhysAddr = true;
    pWalk->uLevel       = uLevel;
    pWalk->fFailed      = PGM_WALKFAIL_EPT_VIOLATION         | ((uint32_t)uLevel << PGM_WALKFAIL_LEVEL_SHIFT);
    return VERR_PAGE_TABLE_NOT_PRESENT;
}


/**
 * Updates page walk result info when reserved bits are encountered.
 *
 * @returns VERR_PAGE_TABLE_NOT_PRESENT.
 * @param   pVCpu   The cross context virtual CPU structure of the calling EMT.
 * @param   pWalk   The page walk info to update.
 * @param   uLevel  The page table walk level.
 */
DECLINLINE(int) PGM_GST_SLAT_NAME_EPT(WalkReturnRsvdError)(PVMCPUCC pVCpu, PPGMPTWALK pWalk, uint8_t uLevel)
{
    NOREF(pVCpu);
    pWalk->fRsvdError = true;
    pWalk->uLevel     = uLevel;
    pWalk->fFailed    = PGM_WALKFAIL_EPT_MISCONFIG           | ((uint32_t)uLevel << PGM_WALKFAIL_LEVEL_SHIFT);
    return VERR_PAGE_TABLE_NOT_PRESENT;
}


/**
 * Walks the guest's EPT page table (second-level address translation).
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PAGE_TABLE_NOT_PRESENT on failure. Check pWalk for details.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling EMT.
 * @param   GCPhysNested        The nested-guest physical address to walk.
 * @param   fIsLinearAddrValid  Whether the linear-address in @c GCPtrNested caused
 *                              this page walk.
 * @param   GCPtrNested         The nested-guest linear address that caused this
 *                              translation. If @c fIsLinearAddrValid is false, pass
 *                              0.
 * @param   pWalk               The page walk info.
 * @param   pSlatWalk           The SLAT mode specific page walk info.
 */
DECLINLINE(int) PGM_GST_SLAT_NAME_EPT(Walk)(PVMCPUCC pVCpu, RTGCPHYS GCPhysNested, bool fIsLinearAddrValid, RTGCPTR GCPtrNested,
                                            PPGMPTWALK pWalk, PSLATPTWALK pSlatWalk)
{
    Assert(fIsLinearAddrValid || GCPtrNested == 0);

    /*
     * Init walk structures.
     */
    RT_ZERO(*pWalk);
    RT_ZERO(*pSlatWalk);

    pWalk->GCPtr              = GCPtrNested;
    pWalk->GCPhysNested       = GCPhysNested;
    pWalk->fIsLinearAddrValid = fIsLinearAddrValid;
    pWalk->fIsSlat            = true;

    /*
     * Figure out EPT attributes that are cumulative (logical-AND) across page walks.
     *   - R, W, X_SUPER are unconditionally cumulative.
     *     See Intel spec. Table 26-7 "Exit Qualification for EPT Violations".
     *
     *   - X_USER is cumulative but relevant only when mode-based execute control for EPT
     *     which we currently don't support it (asserted below).
     *
     *   - MEMTYPE is not cumulative and only applicable to the final paging entry.
     *
     *   - A, D EPT bits map to the regular page-table bit positions. Thus, they're not
     *     included in the mask below and handled separately. Accessed bits are
     *     cumulative but dirty bits are not cumulative as they're only applicable to
     *     the final paging entry.
     */
    Assert(!pVCpu->CTX_SUFF(pVM)->cpum.ro.GuestFeatures.fVmxModeBasedExecuteEpt);
    uint64_t const fEptAndMask = (  PGM_PTATTRS_EPT_R_MASK
                                  | PGM_PTATTRS_EPT_W_MASK
                                  | PGM_PTATTRS_EPT_X_SUPER_MASK) & PGM_PTATTRS_EPT_MASK;

    /*
     * Do the walk.
     */
    uint64_t fEffective;
    {
        /*
         * EPTP.
         *
         * We currently only support 4-level EPT paging.
         * EPT 5-level paging was documented at some point (bit 7 of MSR_IA32_VMX_EPT_VPID_CAP)
         * but for some reason seems to have been removed from subsequent specs.
         */
        int const rc = pgmGstGetEptPML4PtrEx(pVCpu, &pSlatWalk->pPml4);
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else return PGM_GST_SLAT_NAME_EPT(WalkReturnBadPhysAddr)(pVCpu, pWalk, 4, rc);
    }
    {
        /*
         * PML4E.
         */
        PEPTPML4E pPml4e;
        pSlatWalk->pPml4e = pPml4e = &pSlatWalk->pPml4->a[(GCPhysNested >> SLAT_PML4_SHIFT) & SLAT_PML4_MASK];
        EPTPML4E  Pml4e;
        pSlatWalk->Pml4e.u = Pml4e.u = pPml4e->u;

        if (SLAT_IS_PGENTRY_PRESENT(pVCpu, Pml4e)) { /* probable */ }
        else return PGM_GST_SLAT_NAME_EPT(WalkReturnNotPresent)(pVCpu, pWalk, Pml4e.u, 4);

        if (RT_LIKELY(   SLAT_IS_PML4E_VALID(pVCpu, Pml4e)
                      && PGM_GST_SLAT_NAME_EPT(WalkIsPermValid)(pVCpu, Pml4e.u)))
        { /* likely */ }
        else return PGM_GST_SLAT_NAME_EPT(WalkReturnRsvdError)(pVCpu, pWalk, 4);

        uint64_t const fEptAttrs   = Pml4e.u & EPT_PML4E_ATTR_MASK;
        uint8_t const  fRead       = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_READ);
        uint8_t const  fWrite      = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_WRITE);
        uint8_t const  fExecute    = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_EXECUTE);
        uint8_t const  fAccessed   = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_ACCESSED);
        uint64_t const fEptAndBits = (fEptAttrs << PGM_PTATTRS_EPT_SHIFT) & fEptAndMask;
        fEffective = RT_BF_MAKE(PGM_PTATTRS_R,   fRead)
                   | RT_BF_MAKE(PGM_PTATTRS_W,   fWrite)
                   | RT_BF_MAKE(PGM_PTATTRS_NX, !fExecute)
                   | RT_BF_MAKE(PGM_PTATTRS_A,   fAccessed)
                   | fEptAndBits;
        pWalk->fEffective = fEffective;

        int const rc = PGM_GCPHYS_2_PTR_BY_VMCPU(pVCpu, Pml4e.u & EPT_PML4E_PG_MASK, &pSlatWalk->pPdpt);
        if (RT_SUCCESS(rc)) { /* probable */ }
        else return PGM_GST_SLAT_NAME_EPT(WalkReturnBadPhysAddr)(pVCpu, pWalk, 3, rc);
    }
    {
        /*
         * PDPTE.
         */
        PEPTPDPTE pPdpte;
        pSlatWalk->pPdpte = pPdpte = &pSlatWalk->pPdpt->a[(GCPhysNested >> SLAT_PDPT_SHIFT) & SLAT_PDPT_MASK];
        EPTPDPTE  Pdpte;
        pSlatWalk->Pdpte.u = Pdpte.u = pPdpte->u;

        if (SLAT_IS_PGENTRY_PRESENT(pVCpu, Pdpte)) { /* probable */ }
        else return PGM_GST_SLAT_NAME_EPT(WalkReturnNotPresent)(pVCpu, pWalk, Pdpte.u, 3);

        /* The order of the following "if" and "else if" statements matter. */
        if (   SLAT_IS_PDPE_VALID(pVCpu, Pdpte)
            && PGM_GST_SLAT_NAME_EPT(WalkIsPermValid)(pVCpu, Pdpte.u))
        {
            uint64_t const fEptAttrs   = Pdpte.u & EPT_PDPTE_ATTR_MASK;
            uint8_t const  fRead       = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_READ);
            uint8_t const  fWrite      = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_WRITE);
            uint8_t const  fExecute    = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_EXECUTE);
            uint8_t const  fAccessed   = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_ACCESSED);
            uint64_t const fEptAndBits = (fEptAttrs << PGM_PTATTRS_EPT_SHIFT) & fEptAndMask;
            fEffective &= RT_BF_MAKE(PGM_PTATTRS_R,   fRead)
                        | RT_BF_MAKE(PGM_PTATTRS_W,   fWrite)
                        | RT_BF_MAKE(PGM_PTATTRS_A,   fAccessed)
                        | fEptAndBits;
            fEffective |= RT_BF_MAKE(PGM_PTATTRS_NX, !fExecute);
            pWalk->fEffective = fEffective;
        }
        else if (   SLAT_IS_BIG_PDPE_VALID(pVCpu, Pdpte)
                 && PGM_GST_SLAT_NAME_EPT(WalkIsPermValid)(pVCpu, Pdpte.u)
                 && PGM_GST_SLAT_NAME_EPT(WalkIsMemTypeValid)(Pdpte.u, 3))
        {
            uint64_t const fEptAttrs   = Pdpte.u & EPT_PDPTE1G_ATTR_MASK;
            uint8_t const  fRead       = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_READ);
            uint8_t const  fWrite      = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_WRITE);
            uint8_t const  fExecute    = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_EXECUTE);
            uint8_t const  fAccessed   = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_ACCESSED);
            uint8_t const  fDirty      = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_DIRTY);
            uint8_t const  fMemType    = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_MEMTYPE);
            uint64_t const fEptAndBits = (fEptAttrs << PGM_PTATTRS_EPT_SHIFT) & fEptAndMask;
            fEffective &= RT_BF_MAKE(PGM_PTATTRS_R,            fRead)
                        | RT_BF_MAKE(PGM_PTATTRS_W,            fWrite)
                        | RT_BF_MAKE(PGM_PTATTRS_A,            fAccessed)
                        | fEptAndBits;
            fEffective |= RT_BF_MAKE(PGM_PTATTRS_D,            fDirty)
                        | RT_BF_MAKE(PGM_PTATTRS_EPT_MEMTYPE,  fMemType)
                        | RT_BF_MAKE(PGM_PTATTRS_NX,          !fExecute);
            pWalk->fEffective = fEffective;

            pWalk->fGigantPage = true;
            pWalk->fSucceeded  = true;
            pWalk->GCPhys      = SLAT_GET_PDPE1G_GCPHYS(pVCpu, Pdpte)
                               | (GCPhysNested & SLAT_PAGE_1G_OFFSET_MASK);
            PGM_A20_APPLY_TO_VAR(pVCpu, pWalk->GCPhys);
            return VINF_SUCCESS;
        }
        else return PGM_GST_SLAT_NAME_EPT(WalkReturnRsvdError)(pVCpu, pWalk, 3);

        int const rc = PGM_GCPHYS_2_PTR_BY_VMCPU(pVCpu, Pdpte.u & EPT_PDPTE_PG_MASK, &pSlatWalk->pPd);
        if (RT_SUCCESS(rc)) { /* probable */ }
        else return PGM_GST_SLAT_NAME_EPT(WalkReturnBadPhysAddr)(pVCpu, pWalk, 3, rc);
    }
    {
        /*
         * PDE.
         */
        PSLATPDE pPde;
        pSlatWalk->pPde  = pPde  = &pSlatWalk->pPd->a[(GCPhysNested >> SLAT_PD_SHIFT) & SLAT_PD_MASK];
        SLATPDE  Pde;
        pSlatWalk->Pde.u = Pde.u = pPde->u;

        if (SLAT_IS_PGENTRY_PRESENT(pVCpu, Pde)) { /* probable */ }
        else return PGM_GST_SLAT_NAME_EPT(WalkReturnNotPresent)(pVCpu, pWalk, Pde.u, 2);

        /* The order of the following "if" and "else if" statements matter. */
        if (   SLAT_IS_PDE_VALID(pVCpu, Pde)
            && PGM_GST_SLAT_NAME_EPT(WalkIsPermValid)(pVCpu, Pde.u))
        {
            uint64_t const fEptAttrs   = Pde.u & EPT_PDE_ATTR_MASK;
            uint8_t const  fRead       = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_READ);
            uint8_t const  fWrite      = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_WRITE);
            uint8_t const  fExecute    = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_EXECUTE);
            uint8_t const  fAccessed   = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_ACCESSED);
            uint64_t const fEptAndBits = (fEptAttrs << PGM_PTATTRS_EPT_SHIFT) & fEptAndMask;
            fEffective &= RT_BF_MAKE(PGM_PTATTRS_R,   fRead)
                        | RT_BF_MAKE(PGM_PTATTRS_W,   fWrite)
                        | RT_BF_MAKE(PGM_PTATTRS_A,   fAccessed)
                        | fEptAndBits;
            fEffective |= RT_BF_MAKE(PGM_PTATTRS_NX, !fExecute);
            pWalk->fEffective = fEffective;
        }
        else if (   SLAT_IS_BIG_PDE_VALID(pVCpu, Pde)
                 && PGM_GST_SLAT_NAME_EPT(WalkIsPermValid)(pVCpu, Pde.u)
                 && PGM_GST_SLAT_NAME_EPT(WalkIsMemTypeValid)(Pde.u, 2))
        {
            uint64_t const fEptAttrs   = Pde.u & EPT_PDE2M_ATTR_MASK;
            uint8_t const  fRead       = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_READ);
            uint8_t const  fWrite      = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_WRITE);
            uint8_t const  fExecute    = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_EXECUTE);
            uint8_t const  fAccessed   = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_ACCESSED);
            uint8_t const  fDirty      = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_DIRTY);
            uint8_t const  fMemType    = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_MEMTYPE);
            uint64_t const fEptAndBits = (fEptAttrs << PGM_PTATTRS_EPT_SHIFT) & fEptAndMask;
            fEffective &= RT_BF_MAKE(PGM_PTATTRS_R,            fRead)
                        | RT_BF_MAKE(PGM_PTATTRS_W,            fWrite)
                        | RT_BF_MAKE(PGM_PTATTRS_A,            fAccessed)
                        | fEptAndBits;
            fEffective |= RT_BF_MAKE(PGM_PTATTRS_D,            fDirty)
                        | RT_BF_MAKE(PGM_PTATTRS_EPT_MEMTYPE,  fMemType)
                        | RT_BF_MAKE(PGM_PTATTRS_NX,          !fExecute);
            pWalk->fEffective = fEffective;

            pWalk->fBigPage    = true;
            pWalk->fSucceeded  = true;
            pWalk->GCPhys      = SLAT_GET_PDE2M_GCPHYS(pVCpu, Pde)
                               | (GCPhysNested & SLAT_PAGE_2M_OFFSET_MASK);
            PGM_A20_APPLY_TO_VAR(pVCpu, pWalk->GCPhys);
            return VINF_SUCCESS;
        }
        else return PGM_GST_SLAT_NAME_EPT(WalkReturnRsvdError)(pVCpu, pWalk, 2);

        int const rc = PGM_GCPHYS_2_PTR_BY_VMCPU(pVCpu, Pde.u & EPT_PDE_PG_MASK, &pSlatWalk->pPt);
        if (RT_SUCCESS(rc)) { /* probable */ }
        else return PGM_GST_SLAT_NAME_EPT(WalkReturnBadPhysAddr)(pVCpu, pWalk, 1, rc);
    }
    {
        /*
         * PTE.
         */
        PSLATPTE pPte;
        pSlatWalk->pPte  = pPte  = &pSlatWalk->pPt->a[(GCPhysNested >> SLAT_PT_SHIFT) & SLAT_PT_MASK];
        SLATPTE  Pte;
        pSlatWalk->Pte.u = Pte.u = pPte->u;

        if (SLAT_IS_PGENTRY_PRESENT(pVCpu, Pte)) { /* probable */ }
        else return PGM_GST_SLAT_NAME_EPT(WalkReturnNotPresent)(pVCpu, pWalk, Pte.u, 1);

        if (   SLAT_IS_PTE_VALID(pVCpu, Pte)
            && PGM_GST_SLAT_NAME_EPT(WalkIsPermValid)(pVCpu, Pte.u)
            && PGM_GST_SLAT_NAME_EPT(WalkIsMemTypeValid)(Pte.u, 1))
        { /* likely*/ }
        else
            return PGM_GST_SLAT_NAME_EPT(WalkReturnRsvdError)(pVCpu, pWalk, 1);

        uint64_t const fEptAttrs   = Pte.u & EPT_PTE_ATTR_MASK;
        uint8_t const  fRead       = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_READ);
        uint8_t const  fWrite      = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_WRITE);
        uint8_t const  fExecute    = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_EXECUTE);
        uint8_t const  fAccessed   = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_ACCESSED);
        uint8_t const  fDirty      = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_DIRTY);
        uint8_t const  fMemType    = RT_BF_GET(fEptAttrs, VMX_BF_EPT_PT_MEMTYPE);
        uint64_t const fEptAndBits = (fEptAttrs << PGM_PTATTRS_EPT_SHIFT) & fEptAndMask;
        fEffective &= RT_BF_MAKE(PGM_PTATTRS_R,            fRead)
                    | RT_BF_MAKE(PGM_PTATTRS_W,            fWrite)
                    | RT_BF_MAKE(PGM_PTATTRS_A,            fAccessed)
                    | fEptAndBits;
        fEffective |= RT_BF_MAKE(PGM_PTATTRS_D,            fDirty)
                    | RT_BF_MAKE(PGM_PTATTRS_EPT_MEMTYPE,  fMemType)
                    | RT_BF_MAKE(PGM_PTATTRS_NX,          !fExecute);
        pWalk->fEffective = fEffective;

        pWalk->fSucceeded   = true;
        pWalk->GCPhys       = SLAT_GET_PTE_GCPHYS(pVCpu, Pte) | (GCPhysNested & GUEST_PAGE_OFFSET_MASK);
        return VINF_SUCCESS;
    }
}

