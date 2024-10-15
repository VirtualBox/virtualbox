/* $Id: PGMAllShw.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox - Page Manager, Shadow Paging Template - All context code.
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
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#undef SHWUINT
#undef SHWPT
#undef PSHWPT
#undef SHWPTE
#undef PSHWPTE
#undef SHWPD
#undef PSHWPD
#undef SHWPDE
#undef PSHWPDE
#undef SHW_PDE_PG_MASK
#undef SHW_PD_SHIFT
#undef SHW_PD_MASK
#undef SHW_PDE_ATOMIC_SET
#undef SHW_PDE_ATOMIC_SET2
#undef SHW_PDE_IS_P
#undef SHW_PDE_IS_A
#undef SHW_PDE_IS_BIG
#undef SHW_PTE_PG_MASK
#undef SHW_PTE_IS_P
#undef SHW_PTE_IS_RW
#undef SHW_PTE_IS_US
#undef SHW_PTE_IS_A
#undef SHW_PTE_IS_D
#undef SHW_PTE_IS_P_RW
#undef SHW_PTE_IS_TRACK_DIRTY
#undef SHW_PTE_GET_HCPHYS
#undef SHW_PTE_GET_U
#undef SHW_PTE_LOG64
#undef SHW_PTE_SET
#undef SHW_PTE_ATOMIC_SET
#undef SHW_PTE_ATOMIC_SET2
#undef SHW_PTE_SET_RO
#undef SHW_PTE_SET_RW
#undef SHW_PT_SHIFT
#undef SHW_PT_MASK
#undef SHW_TOTAL_PD_ENTRIES
#undef SHW_PDPT_SHIFT
#undef SHW_PDPT_MASK
#undef SHW_PDPE_PG_MASK

#if PGM_SHW_TYPE == PGM_TYPE_32BIT || PGM_SHW_TYPE == PGM_TYPE_NESTED_32BIT
# define SHWUINT                        uint32_t
# define SHWPT                          X86PT
# define PSHWPT                         PX86PT
# define SHWPTE                         X86PTE
# define PSHWPTE                        PX86PTE
# define SHWPD                          X86PD
# define PSHWPD                         PX86PD
# define SHWPDE                         X86PDE
# define PSHWPDE                        PX86PDE
# define SHW_PDE_PG_MASK                X86_PDE_PG_MASK
# define SHW_PD_SHIFT                   X86_PD_SHIFT
# define SHW_PD_MASK                    X86_PD_MASK
# define SHW_TOTAL_PD_ENTRIES           X86_PG_ENTRIES
# define SHW_PDE_IS_P(Pde)              ( (Pde).u & X86_PDE_P )
# define SHW_PDE_IS_A(Pde)              ( (Pde).u & X86_PDE_A )
# define SHW_PDE_IS_BIG(Pde)            ( (Pde).u & X86_PDE_PS  )
# define SHW_PDE_ATOMIC_SET(Pde, uNew)  do { ASMAtomicWriteU32(&(Pde).u, (uNew)); } while (0)
# define SHW_PDE_ATOMIC_SET2(Pde, Pde2) do { ASMAtomicWriteU32(&(Pde).u, (Pde2).u); } while (0)
# define SHW_PTE_PG_MASK                X86_PTE_PG_MASK
# define SHW_PTE_IS_P(Pte)              ( (Pte).u & X86_PTE_P )
# define SHW_PTE_IS_RW(Pte)             ( (Pte).u & X86_PTE_RW )
# define SHW_PTE_IS_US(Pte)             ( (Pte).u & X86_PTE_US )
# define SHW_PTE_IS_A(Pte)              ( (Pte).u & X86_PTE_A )
# define SHW_PTE_IS_D(Pte)              ( (Pte).u & X86_PTE_D )
# define SHW_PTE_IS_P_RW(Pte)           ( ((Pte).u & (X86_PTE_P | X86_PTE_RW)) == (X86_PTE_P | X86_PTE_RW) )
# define SHW_PTE_IS_TRACK_DIRTY(Pte)    ( !!((Pte).u & PGM_PTFLAGS_TRACK_DIRTY) )
# define SHW_PTE_GET_HCPHYS(Pte)        ( (Pte).u & X86_PTE_PG_MASK )
# define SHW_PTE_LOG64(Pte)             ( (uint64_t)(Pte).u )
# define SHW_PTE_GET_U(Pte)             ( (Pte).u )             /**< Use with care. */
# define SHW_PTE_SET(Pte, uNew)         do { (Pte).u = (uNew); } while (0)
# define SHW_PTE_ATOMIC_SET(Pte, uNew)  do { ASMAtomicWriteU32(&(Pte).u, (uNew)); } while (0)
# define SHW_PTE_ATOMIC_SET2(Pte, Pte2) do { ASMAtomicWriteU32(&(Pte).u, (Pte2).u); } while (0)
# define SHW_PTE_SET_RO(Pte)            do { (Pte).u &= ~(X86PGUINT)X86_PTE_RW; } while (0)
# define SHW_PTE_SET_RW(Pte)            do { (Pte).u |= X86_PTE_RW; } while (0)
# define SHW_PT_SHIFT                   X86_PT_SHIFT
# define SHW_PT_MASK                    X86_PT_MASK

#elif PGM_SHW_TYPE == PGM_TYPE_EPT
# define SHWUINT                        uint64_t
# define SHWPT                          EPTPT
# define PSHWPT                         PEPTPT
# define SHWPTE                         EPTPTE
# define PSHWPTE                        PEPTPTE
# define SHWPD                          EPTPD
# define PSHWPD                         PEPTPD
# define SHWPDE                         EPTPDE
# define PSHWPDE                        PEPTPDE
# define SHW_PDE_PG_MASK                EPT_PDE_PG_MASK
# define SHW_PD_SHIFT                   EPT_PD_SHIFT
# define SHW_PD_MASK                    EPT_PD_MASK
# define SHW_PDE_IS_P(Pde)              ( (Pde).u & EPT_E_READ /* always set*/ )
# define SHW_PDE_IS_A(Pde)              ( 1 ) /* We don't use EPT_E_ACCESSED, use with care! */
# define SHW_PDE_IS_BIG(Pde)            ( (Pde).u & EPT_E_LEAF )
# define SHW_PDE_ATOMIC_SET(Pde, uNew)  do { ASMAtomicWriteU64(&(Pde).u, (uNew)); } while (0)
# define SHW_PDE_ATOMIC_SET2(Pde, Pde2) do { ASMAtomicWriteU64(&(Pde).u, (Pde2).u); } while (0)
# define SHW_PTE_PG_MASK                EPT_PTE_PG_MASK
# define SHW_PTE_IS_P(Pte)              ( (Pte).u & EPT_E_READ ) /* Approximation, works for us. */
# define SHW_PTE_IS_RW(Pte)             ( (Pte).u & EPT_E_WRITE )
# define SHW_PTE_IS_US(Pte)             ( true )
# define SHW_PTE_IS_A(Pte)              ( true )
# define SHW_PTE_IS_D(Pte)              ( true )
# define SHW_PTE_IS_P_RW(Pte)           ( ((Pte).u & (EPT_E_READ | EPT_E_WRITE)) == (EPT_E_READ | EPT_E_WRITE) )
# define SHW_PTE_IS_TRACK_DIRTY(Pte)    ( false )
# define SHW_PTE_GET_HCPHYS(Pte)        ( (Pte).u & EPT_PTE_PG_MASK )
# define SHW_PTE_LOG64(Pte)             ( (Pte).u )
# define SHW_PTE_GET_U(Pte)             ( (Pte).u )             /**< Use with care. */
# define SHW_PTE_SET(Pte, uNew)         do { (Pte).u = (uNew); } while (0)
# define SHW_PTE_ATOMIC_SET(Pte, uNew)  do { ASMAtomicWriteU64(&(Pte).u, (uNew)); } while (0)
# define SHW_PTE_ATOMIC_SET2(Pte, Pte2) do { ASMAtomicWriteU64(&(Pte).u, (Pte2).u); } while (0)
# define SHW_PTE_SET_RO(Pte)            do { (Pte).u &= ~(uint64_t)EPT_E_WRITE; } while (0)
# define SHW_PTE_SET_RW(Pte)            do { (Pte).u |= EPT_E_WRITE; } while (0)
# define SHW_PT_SHIFT                   EPT_PT_SHIFT
# define SHW_PT_MASK                    EPT_PT_MASK
# define SHW_PDPT_SHIFT                 EPT_PDPT_SHIFT
# define SHW_PDPT_MASK                  EPT_PDPT_MASK
# define SHW_PDPE_PG_MASK               EPT_PDPE_PG_MASK
# define SHW_TOTAL_PD_ENTRIES           (EPT_PG_AMD64_ENTRIES * EPT_PG_AMD64_PDPE_ENTRIES)

#else
# define SHWUINT                        uint64_t
# define SHWPT                          PGMSHWPTPAE
# define PSHWPT                         PPGMSHWPTPAE
# define SHWPTE                         PGMSHWPTEPAE
# define PSHWPTE                        PPGMSHWPTEPAE
# define SHWPD                          X86PDPAE
# define PSHWPD                         PX86PDPAE
# define SHWPDE                         X86PDEPAE
# define PSHWPDE                        PX86PDEPAE
# define SHW_PDE_PG_MASK                X86_PDE_PAE_PG_MASK
# define SHW_PD_SHIFT                   X86_PD_PAE_SHIFT
# define SHW_PD_MASK                    X86_PD_PAE_MASK
# define SHW_PDE_IS_P(Pde)              ( (Pde).u & X86_PDE_P )
# define SHW_PDE_IS_A(Pde)              ( (Pde).u & X86_PDE_A )
# define SHW_PDE_IS_BIG(Pde)            ( (Pde).u & X86_PDE_PS )
# define SHW_PDE_ATOMIC_SET(Pde, uNew)  do { ASMAtomicWriteU64(&(Pde).u, (uNew)); } while (0)
# define SHW_PDE_ATOMIC_SET2(Pde, Pde2) do { ASMAtomicWriteU64(&(Pde).u, (Pde2).u); } while (0)
# define SHW_PTE_PG_MASK                X86_PTE_PAE_PG_MASK
# define SHW_PTE_IS_P(Pte)              PGMSHWPTEPAE_IS_P(Pte)
# define SHW_PTE_IS_RW(Pte)             PGMSHWPTEPAE_IS_RW(Pte)
# define SHW_PTE_IS_US(Pte)             PGMSHWPTEPAE_IS_US(Pte)
# define SHW_PTE_IS_A(Pte)              PGMSHWPTEPAE_IS_A(Pte)
# define SHW_PTE_IS_D(Pte)              PGMSHWPTEPAE_IS_D(Pte)
# define SHW_PTE_IS_P_RW(Pte)           PGMSHWPTEPAE_IS_P_RW(Pte)
# define SHW_PTE_IS_TRACK_DIRTY(Pte)    PGMSHWPTEPAE_IS_TRACK_DIRTY(Pte)
# define SHW_PTE_GET_HCPHYS(Pte)        PGMSHWPTEPAE_GET_HCPHYS(Pte)
# define SHW_PTE_LOG64(Pte)             PGMSHWPTEPAE_GET_LOG(Pte)
# define SHW_PTE_GET_U(Pte)             PGMSHWPTEPAE_GET_U(Pte)     /**< Use with care. */
# define SHW_PTE_SET(Pte, uNew)         PGMSHWPTEPAE_SET(Pte, uNew)
# define SHW_PTE_ATOMIC_SET(Pte, uNew)  PGMSHWPTEPAE_ATOMIC_SET(Pte, uNew)
# define SHW_PTE_ATOMIC_SET2(Pte, Pte2) PGMSHWPTEPAE_ATOMIC_SET2(Pte, Pte2)
# define SHW_PTE_SET_RO(Pte)            PGMSHWPTEPAE_SET_RO(Pte)
# define SHW_PTE_SET_RW(Pte)            PGMSHWPTEPAE_SET_RW(Pte)
# define SHW_PT_SHIFT                   X86_PT_PAE_SHIFT
# define SHW_PT_MASK                    X86_PT_PAE_MASK

# if PGM_SHW_TYPE == PGM_TYPE_AMD64 || PGM_SHW_TYPE == PGM_TYPE_NESTED_AMD64 || /* whatever: */ PGM_SHW_TYPE == PGM_TYPE_NONE
#  define SHW_PDPT_SHIFT                X86_PDPT_SHIFT
#  define SHW_PDPT_MASK                 X86_PDPT_MASK_AMD64
#  define SHW_PDPE_PG_MASK              X86_PDPE_PG_MASK
#  define SHW_TOTAL_PD_ENTRIES          (X86_PG_AMD64_ENTRIES * X86_PG_AMD64_PDPE_ENTRIES)

# elif PGM_SHW_TYPE == PGM_TYPE_PAE || PGM_SHW_TYPE == PGM_TYPE_NESTED_PAE
#  define SHW_PDPT_SHIFT                X86_PDPT_SHIFT
#  define SHW_PDPT_MASK                 X86_PDPT_MASK_PAE
#  define SHW_PDPE_PG_MASK              X86_PDPE_PG_MASK
#  define SHW_TOTAL_PD_ENTRIES          (X86_PG_PAE_ENTRIES * X86_PG_PAE_PDPE_ENTRIES)

# else
#  error "Misconfigured PGM_SHW_TYPE or something..."
# endif
#endif

#if PGM_SHW_TYPE == PGM_TYPE_NONE && PGM_TYPE_IS_NESTED_OR_EPT(PGM_SHW_TYPE)
# error "PGM_TYPE_IS_NESTED_OR_EPT is true for PGM_TYPE_NONE!"
#endif



/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
RT_C_DECLS_BEGIN
PGM_SHW_DECL(int, GetPage)(PVMCPUCC pVCpu, RTGCUINTPTR GCPtr, uint64_t *pfFlags, PRTHCPHYS pHCPhys);
PGM_SHW_DECL(int, ModifyPage)(PVMCPUCC pVCpu, RTGCUINTPTR GCPtr, size_t cbPages, uint64_t fFlags, uint64_t fMask, uint32_t fOpFlags);
PGM_SHW_DECL(int, Exit)(PVMCPUCC pVCpu);
#ifdef IN_RING3
PGM_SHW_DECL(int, Relocate)(PVMCPUCC pVCpu, RTGCPTR offDelta);
#endif
RT_C_DECLS_END


/**
 * Enters the shadow mode.
 *
 * @returns VBox status code.
 * @param   pVCpu                   The cross context virtual CPU structure.
 */
PGM_SHW_DECL(int, Enter)(PVMCPUCC pVCpu)
{
#if PGM_TYPE_IS_NESTED_OR_EPT(PGM_SHW_TYPE)

# ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
    RTGCPHYS    GCPhysCR3;
    PGMPOOLKIND enmKind;
    if (pVCpu->pgm.s.enmGuestSlatMode != PGMSLAT_EPT)
    {
        GCPhysCR3 = RT_BIT_64(63);
        enmKind   = PGMPOOLKIND_ROOT_NESTED;
    }
    else
    {
        GCPhysCR3 = pVCpu->pgm.s.uEptPtr & EPT_EPTP_PG_MASK;
        enmKind   = PGMPOOLKIND_EPT_PML4_FOR_EPT_PML4;
    }
# else
    RTGCPHYS    const GCPhysCR3 = RT_BIT_64(63);
    PGMPOOLKIND const enmKind   = PGMPOOLKIND_ROOT_NESTED;
# endif
    PVMCC       const pVM       = pVCpu->CTX_SUFF(pVM);

    Assert(HMIsNestedPagingActive(pVM));
    Assert(pVM->pgm.s.fNestedPaging);
    Assert(!pVCpu->pgm.s.pShwPageCR3R3);

    PGM_LOCK_VOID(pVM);

    PPGMPOOLPAGE pNewShwPageCR3;
    int rc = pgmPoolAlloc(pVM, GCPhysCR3, enmKind, PGMPOOLACCESS_DONTCARE, PGM_A20_IS_ENABLED(pVCpu),
                          NIL_PGMPOOL_IDX, UINT32_MAX, true /*fLockPage*/,
                          &pNewShwPageCR3);
    AssertLogRelRCReturnStmt(rc, PGM_UNLOCK(pVM), rc);

    pVCpu->pgm.s.pShwPageCR3R3 = pgmPoolConvertPageToR3(pVM->pgm.s.CTX_SUFF(pPool), pNewShwPageCR3);
    pVCpu->pgm.s.pShwPageCR3R0 = pgmPoolConvertPageToR0(pVM->pgm.s.CTX_SUFF(pPool), pNewShwPageCR3);

    PGM_UNLOCK(pVM);

    Log(("Enter nested shadow paging mode: root %RHv phys %RHp\n", pVCpu->pgm.s.pShwPageCR3R3, pVCpu->pgm.s.CTX_SUFF(pShwPageCR3)->Core.Key));
#else
    NOREF(pVCpu);
#endif
    return VINF_SUCCESS;
}


/**
 * Exits the shadow mode.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
PGM_SHW_DECL(int, Exit)(PVMCPUCC pVCpu)
{
#if PGM_TYPE_IS_NESTED_OR_EPT(PGM_SHW_TYPE)
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    if (pVCpu->pgm.s.CTX_SUFF(pShwPageCR3))
    {
        PPGMPOOL pPool = pVM->pgm.s.CTX_SUFF(pPool);

        PGM_LOCK_VOID(pVM);

# if defined(VBOX_WITH_NESTED_HWVIRT_VMX_EPT) && PGM_SHW_TYPE == PGM_TYPE_EPT
        if (pVCpu->pgm.s.enmGuestSlatMode == PGMSLAT_EPT)
            pgmPoolUnlockPage(pPool, pVCpu->pgm.s.CTX_SUFF(pShwPageCR3));
# endif

        /* Do *not* unlock this page as we have two of them floating around in the 32-bit host & 64-bit guest case.
         * We currently assert when you try to free one of them; don't bother to really allow this.
         *
         * Note that this is two nested paging root pages max. This isn't a leak. They are reused.
         */
        /* pgmPoolUnlockPage(pPool, pVCpu->pgm.s.CTX_SUFF(pShwPageCR3)); */

        pgmPoolFreeByPage(pPool, pVCpu->pgm.s.CTX_SUFF(pShwPageCR3), NIL_PGMPOOL_IDX, UINT32_MAX);
        pVCpu->pgm.s.pShwPageCR3R3 = 0;
        pVCpu->pgm.s.pShwPageCR3R0 = 0;

        PGM_UNLOCK(pVM);

        Log(("Leave nested shadow paging mode\n"));
    }
#else
    RT_NOREF_PV(pVCpu);
#endif
    return VINF_SUCCESS;
}


#if 0
PGM_SHW_DECL(int, NestedGetPage)(PVMCPUCC pVCpu, PEPTPD pEptPd, PPGMPTWALK pWalk, uint64_t *pfFlags, PRTHCPHYS pHCPhys)
{
#if PGM_SHW_TYPE == PGM_TYPE_EPT
    RTGCPHYS const GCPhysNested = pWalk->GCPhysNested;
    unsigned const iEptPd = ((GCPhysNested >> SHW_PD_SHIFT) & SHW_PD_MASK);
    Assert(iEptPd < EPT_PG_ENTRIES);
    SHWPDE EptPde = pEptPd->a[iEptPd];
    if (!SHW_PDE_IS_P(EptPde))
    {
        *pfFlags = 0;
        *pHCPhys = NIL_RTHCPHYS;
        return VERR_PAGE_TABLE_NOT_PRESENT;
    }

    if (SHW_PDE_IS_BIG(EptPde))
    {
        Assert(pWalk->fBigPage);
        if (pfFlags)
            *pfFlags = (EptPde.u & ~SHW_PDE_PG_MASK);
        if (pHCPhys)
            *pHCPhys = (EptPde.u & EPT_PDE2M_PG_MASK) + (pWalk->GCPhys & (RT_BIT(EPT_PD_SHIFT) - 1) & X86_PAGE_4K_BASE_MASK);
        return VINF_SUCCESS;
    }

    PSHWPT pEptPt;
    int const rc = PGM_HCPHYS_2_PTR(pVCpu->CTX_SUFF(pVM), pVCpu, EptPde.u & EPT_PDE_PG_MASK, &pEptPt);
    if (RT_FAILURE(rc))
    {
        *pfFlags = 0;
        *pHCPhys = NIL_RTHCPHYS;
        return rc;
    }

    unsigned const iEptPt = (GCPhysNested >> SHW_PT_SHIFT) & SHW_PT_MASK;
    Assert(iEptPt < EPT_PG_ENTRIES);
    SHWPTE         EptPte = pEptPt->a[iEptPt];
    if (!SHW_PTE_IS_P(EptPte))
    {
        *pfFlags = 0;
        *pHCPhys = NIL_RTHCPHYS;
        return VERR_PAGE_NOT_PRESENT;
    }

    if (pfFlags)
    {
        /* Read, Write and Execute bits (Present mask) are cumulative. */
        *pfFlags = (SHW_PTE_GET_U(EptPte) & ~SHW_PTE_PG_MASK)
                 & ((EptPde.u & EPT_PRESENT_MASK) | ~(uint64_t)EPT_PRESENT_MASK);
    }
    if (pHCPhys)
        *pHCPhys = SHW_PTE_GET_HCPHYS(EptPte);
    return VINF_SUCCESS;

#else   /* PGM_SHW_TYPE != PGM_TYPE_EPT */
    RT_NOREF(pVCpu, pEptPd, pWalk, *pfFlags, pHCPhys);
    AssertFailed();
    return VERR_PGM_SHW_NONE_IPE;
#endif  /* PGM_SHW_TYPE != PGM_TYPE_EPT */
}
#endif


/**
 * Gets effective page information (from the VMM page directory).
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtr       Guest Context virtual address of the page.
 * @param   pfFlags     Where to store the flags. These are X86_PTE_*.
 * @param   pHCPhys     Where to store the HC physical address of the page.
 *                      This is page aligned.
 * @remark  You should use PGMMapGetPage() for pages in a mapping.
 */
PGM_SHW_DECL(int, GetPage)(PVMCPUCC pVCpu, RTGCUINTPTR GCPtr, uint64_t *pfFlags, PRTHCPHYS pHCPhys)
{
#if PGM_SHW_TYPE == PGM_TYPE_NONE
    RT_NOREF(pVCpu, GCPtr);
    AssertFailed();
    *pfFlags = 0;
    *pHCPhys = NIL_RTHCPHYS;
    return VERR_PGM_SHW_NONE_IPE;

#else  /* PGM_SHW_TYPE != PGM_TYPE_NONE */
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);

    PGM_LOCK_ASSERT_OWNER(pVM);

    /*
     * Get the PDE.
     */
# if PGM_SHW_TYPE == PGM_TYPE_AMD64 || PGM_SHW_TYPE == PGM_TYPE_NESTED_AMD64
    X86PDEPAE Pde;

    /* PML4 */
    X86PML4E        Pml4e = pgmShwGetLongModePML4E(pVCpu, GCPtr);
    if (!(Pml4e.u & X86_PML4E_P))
        return VERR_PAGE_TABLE_NOT_PRESENT;

    /* PDPT */
    PX86PDPT        pPDPT;
    int rc = PGM_HCPHYS_2_PTR(pVM, pVCpu, Pml4e.u & X86_PML4E_PG_MASK, &pPDPT);
    if (RT_FAILURE(rc))
        return rc;
    const unsigned  iPDPT = (GCPtr >> SHW_PDPT_SHIFT) & SHW_PDPT_MASK;
    X86PDPE         Pdpe = pPDPT->a[iPDPT];
    if (!(Pdpe.u & X86_PDPE_P))
        return VERR_PAGE_TABLE_NOT_PRESENT;

    /* PD */
    PX86PDPAE       pPd;
    rc = PGM_HCPHYS_2_PTR(pVM, pVCpu, Pdpe.u & X86_PDPE_PG_MASK, &pPd);
    if (RT_FAILURE(rc))
        return rc;
    const unsigned  iPd = (GCPtr >> SHW_PD_SHIFT) & SHW_PD_MASK;
    Pde = pPd->a[iPd];

    /* Merge accessed, write, user and no-execute bits into the PDE. */
    AssertCompile(X86_PML4E_A  == X86_PDPE_A  && X86_PML4E_A  == X86_PDE_A);
    AssertCompile(X86_PML4E_RW == X86_PDPE_RW && X86_PML4E_RW == X86_PDE_RW);
    AssertCompile(X86_PML4E_US == X86_PDPE_US && X86_PML4E_US == X86_PDE_US);
    AssertCompile(X86_PML4E_NX == X86_PDPE_LM_NX && X86_PML4E_NX == X86_PDE_PAE_NX);
    Pde.u &= (Pml4e.u & Pdpe.u) | ~(X86PGPAEUINT)(X86_PML4E_A | X86_PML4E_RW | X86_PML4E_US);
    Pde.u |= (Pml4e.u | Pdpe.u) & X86_PML4E_NX;

# elif PGM_SHW_TYPE == PGM_TYPE_PAE || PGM_SHW_TYPE == PGM_TYPE_NESTED_PAE
    X86PDEPAE       Pde = pgmShwGetPaePDE(pVCpu, GCPtr);

# elif PGM_SHW_TYPE == PGM_TYPE_EPT
    EPTPDE Pde;
    const unsigned iPd = ((GCPtr >> SHW_PD_SHIFT) & SHW_PD_MASK);

    if (pVCpu->pgm.s.enmGuestSlatMode == PGMSLAT_DIRECT)
    {
        PEPTPD pPDDst;
        int rc = pgmShwGetEPTPDPtr(pVCpu, GCPtr, NULL, &pPDDst);
        if (rc == VINF_SUCCESS) /** @todo this function isn't expected to return informational status codes. Check callers / fix. */
        { /* likely */ }
        else
        {
            AssertRC(rc);
            return rc;
        }
        Assert(pPDDst);
        Pde = pPDDst->a[iPd];
    }
    else
    {
#  ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
        Assert(pVCpu->pgm.s.enmGuestSlatMode == PGMSLAT_EPT);
        Assert(!(GCPtr & GUEST_PAGE_OFFSET_MASK));
        PGMPTWALK      Walk;
        PGMPTWALKGST   GstWalkAll;
        RTGCPHYS const GCPhysNestedPage = GCPtr;
        int rc = pgmGstSlatWalk(pVCpu, GCPhysNestedPage, false /*fIsLinearAddrValid*/, 0 /*GCPtrNestedFault*/, &Walk,
                                &GstWalkAll);
        if (RT_SUCCESS(rc))
        {
#   ifdef DEBUG_ramshankar
            /* Paranoia. */
            Assert(GstWalkAll.enmType == PGMPTWALKGSTTYPE_EPT);
            Assert(Walk.fSucceeded);
            Assert(Walk.fEffective & (PGM_PTATTRS_EPT_R_MASK | PGM_PTATTRS_EPT_W_MASK | PGM_PTATTRS_EPT_X_SUPER_MASK));
            Assert(Walk.fIsSlat);
            Assert(RT_BOOL(Walk.fEffective & PGM_PTATTRS_R_MASK)  ==  RT_BOOL(Walk.fEffective & PGM_PTATTRS_EPT_R_MASK));
            Assert(RT_BOOL(Walk.fEffective & PGM_PTATTRS_W_MASK)  ==  RT_BOOL(Walk.fEffective & PGM_PTATTRS_EPT_W_MASK));
            Assert(RT_BOOL(Walk.fEffective & PGM_PTATTRS_NX_MASK) == !RT_BOOL(Walk.fEffective & PGM_PTATTRS_EPT_X_SUPER_MASK));
#   endif
            PGM_A20_ASSERT_MASKED(pVCpu, Walk.GCPhys);

            /* Update the nested-guest physical address with the translated guest-physical address. */
            GCPtr = Walk.GCPhys;

            /* Get the PD. */
            PSHWPD pEptPd;
            rc = pgmShwGetNestedEPTPDPtr(pVCpu, GCPhysNestedPage, NULL /*ppPdpt*/, &pEptPd, &GstWalkAll);
            AssertRCReturn(rc, rc);
            Assert(pEptPd);

            Assert(iPd < EPT_PG_ENTRIES);
            Pde = pEptPd->a[iPd];
        }
        else
        {
            Log(("Failed to translate nested-guest physical address %#RGp rc=%Rrc\n", GCPhysNestedPage, rc));
            return rc;
        }

#  else  /* !VBOX_WITH_NESTED_HWVIRT_VMX_EPT */
        AssertFailed();
        return VERR_PGM_SHW_NONE_IPE;
#  endif /* !VBOX_WITH_NESTED_HWVIRT_VMX_EPT */
    }

# elif PGM_SHW_TYPE == PGM_TYPE_32BIT || PGM_SHW_TYPE == PGM_TYPE_NESTED_32BIT
    X86PDE          Pde = pgmShwGet32BitPDE(pVCpu, GCPtr);

# else
#  error "Misconfigured PGM_SHW_TYPE or something..."
# endif
    if (!SHW_PDE_IS_P(Pde))
        return VERR_PAGE_TABLE_NOT_PRESENT;

    /* Deal with large pages. */
    if (SHW_PDE_IS_BIG(Pde))
    {
        /*
         * Store the results.
         * RW and US flags depend on the entire page translation hierarchy - except for
         * legacy PAE which has a simplified PDPE.
         */
        if (pfFlags)
        {
            *pfFlags = (Pde.u & ~SHW_PDE_PG_MASK);
# if PGM_WITH_NX(PGM_SHW_TYPE, PGM_SHW_TYPE) || PGM_SHW_TYPE == PGM_TYPE_NESTED_PAE || PGM_SHW_TYPE == PGM_TYPE_NESTED_AMD64
            if (   (Pde.u & X86_PTE_PAE_NX)
#  if PGM_WITH_NX(PGM_SHW_TYPE, PGM_SHW_TYPE)
                && CPUMIsGuestNXEnabled(pVCpu) /** @todo why do we have to check the guest state here? */
#  endif
               )
                *pfFlags |= X86_PTE_PAE_NX;
# endif
        }

        if (pHCPhys)
            *pHCPhys = (Pde.u & SHW_PDE_PG_MASK) + (GCPtr & (RT_BIT(SHW_PD_SHIFT) - 1) & X86_PAGE_4K_BASE_MASK);

        return VINF_SUCCESS;
    }

    /*
     * Get PT entry.
     */
    PSHWPT          pPT;
    int rc2 = PGM_HCPHYS_2_PTR(pVM, pVCpu, Pde.u & SHW_PDE_PG_MASK, &pPT);
    if (RT_FAILURE(rc2))
        return rc2;
    const unsigned  iPt = (GCPtr >> SHW_PT_SHIFT) & SHW_PT_MASK;
    SHWPTE          Pte = pPT->a[iPt];
    if (!SHW_PTE_IS_P(Pte))
        return VERR_PAGE_NOT_PRESENT;

    /*
     * Store the results.
     * RW and US flags depend on the entire page translation hierarchy - except for
     * legacy PAE which has a simplified PDPE.
     */
    if (pfFlags)
    {
        *pfFlags = (SHW_PTE_GET_U(Pte) & ~SHW_PTE_PG_MASK)
                 & ((Pde.u & (X86_PTE_RW | X86_PTE_US)) | ~(uint64_t)(X86_PTE_RW | X86_PTE_US));

# if PGM_WITH_NX(PGM_SHW_TYPE, PGM_SHW_TYPE) || PGM_SHW_TYPE == PGM_TYPE_NESTED_PAE || PGM_SHW_TYPE == PGM_TYPE_NESTED_AMD64
        /* The NX bit is determined by a bitwise OR between the PT and PD */
        if (   ((SHW_PTE_GET_U(Pte) | Pde.u) & X86_PTE_PAE_NX)
#  if PGM_WITH_NX(PGM_SHW_TYPE, PGM_SHW_TYPE)
            && CPUMIsGuestNXEnabled(pVCpu) /** @todo why do we have to check the guest state here? */
#  endif
           )
            *pfFlags |= X86_PTE_PAE_NX;
# endif
    }

    if (pHCPhys)
        *pHCPhys = SHW_PTE_GET_HCPHYS(Pte);

    return VINF_SUCCESS;
#endif /* PGM_SHW_TYPE != PGM_TYPE_NONE */
}


/**
 * Modify page flags for a range of pages in the shadow context.
 *
 * The existing flags are ANDed with the fMask and ORed with the fFlags.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtr       Virtual address of the first page in the range. Page aligned!
 * @param   cb          Size (in bytes) of the range to apply the modification to. Page aligned!
 * @param   fFlags      The OR  mask - page flags X86_PTE_*, excluding the page mask of course.
 * @param   fMask       The AND mask - page flags X86_PTE_*.
 *                      Be extremely CAREFUL with ~'ing values because they can be 32-bit!
 * @param   fOpFlags    A combination of the PGM_MK_PK_XXX flags.
 * @remark  You must use PGMMapModifyPage() for pages in a mapping.
 */
PGM_SHW_DECL(int, ModifyPage)(PVMCPUCC pVCpu, RTGCUINTPTR GCPtr, size_t cb, uint64_t fFlags, uint64_t fMask, uint32_t fOpFlags)
{
#if PGM_SHW_TYPE == PGM_TYPE_NONE
    RT_NOREF(pVCpu, GCPtr, cb, fFlags, fMask, fOpFlags);
    AssertFailed();
    return VERR_PGM_SHW_NONE_IPE;

#else  /* PGM_SHW_TYPE != PGM_TYPE_NONE */
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    PGM_LOCK_ASSERT_OWNER(pVM);

    /*
     * Walk page tables and pages till we're done.
     */
    int rc;
    for (;;)
    {
        /*
         * Get the PDE.
         */
# if PGM_SHW_TYPE == PGM_TYPE_AMD64 || PGM_SHW_TYPE == PGM_TYPE_NESTED_AMD64
        X86PDEPAE       Pde;
        /* PML4 */
        X86PML4E        Pml4e = pgmShwGetLongModePML4E(pVCpu, GCPtr);
        if (!(Pml4e.u & X86_PML4E_P))
            return VERR_PAGE_TABLE_NOT_PRESENT;

        /* PDPT */
        PX86PDPT        pPDPT;
        rc = PGM_HCPHYS_2_PTR(pVM, pVCpu, Pml4e.u & X86_PML4E_PG_MASK, &pPDPT);
        if (RT_FAILURE(rc))
            return rc;
        const unsigned  iPDPT = (GCPtr >> SHW_PDPT_SHIFT) & SHW_PDPT_MASK;
        X86PDPE         Pdpe = pPDPT->a[iPDPT];
        if (!(Pdpe.u & X86_PDPE_P))
            return VERR_PAGE_TABLE_NOT_PRESENT;

        /* PD */
        PX86PDPAE       pPd;
        rc = PGM_HCPHYS_2_PTR(pVM, pVCpu, Pdpe.u & X86_PDPE_PG_MASK, &pPd);
        if (RT_FAILURE(rc))
            return rc;
        const unsigned iPd = (GCPtr >> SHW_PD_SHIFT) & SHW_PD_MASK;
        Pde = pPd->a[iPd];

# elif PGM_SHW_TYPE == PGM_TYPE_PAE || PGM_SHW_TYPE == PGM_TYPE_NESTED_PAE
        X86PDEPAE       Pde = pgmShwGetPaePDE(pVCpu, GCPtr);

# elif PGM_SHW_TYPE == PGM_TYPE_EPT
        EPTPDE Pde;
        const unsigned iPd = ((GCPtr >> SHW_PD_SHIFT) & SHW_PD_MASK);
        if (pVCpu->pgm.s.enmGuestSlatMode == PGMSLAT_DIRECT)
        {
            PEPTPD pPDDst;
            rc = pgmShwGetEPTPDPtr(pVCpu, GCPtr, NULL, &pPDDst);
            if (rc != VINF_SUCCESS)
            {
                AssertRC(rc);
                return rc;
            }
            Assert(pPDDst);
            Pde = pPDDst->a[iPd];
        }
        else
        {
#  ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
            Assert(pVCpu->pgm.s.enmGuestSlatMode == PGMSLAT_EPT);
            Assert(!(GCPtr & GUEST_PAGE_OFFSET_MASK));
            PGMPTWALK      Walk;
            PGMPTWALKGST   GstWalkAll;
            RTGCPHYS const GCPhysNestedPage = GCPtr;
            rc = pgmGstSlatWalk(pVCpu, GCPhysNestedPage, false /*fIsLinearAddrValid*/, 0 /*GCPtrNestedFault*/, &Walk,
                                &GstWalkAll);
            if (RT_SUCCESS(rc))
            {
#   ifdef DEBUG_ramshankar
                /* Paranoia. */
                Assert(GstWalkAll.enmType == PGMPTWALKGSTTYPE_EPT);
                Assert(Walk.fSucceeded);
                Assert(Walk.fEffective & (PGM_PTATTRS_EPT_R_MASK | PGM_PTATTRS_EPT_W_MASK | PGM_PTATTRS_EPT_X_SUPER_MASK));
                Assert(Walk.fIsSlat);
                Assert(RT_BOOL(Walk.fEffective & PGM_PTATTRS_R_MASK)  ==  RT_BOOL(Walk.fEffective & PGM_PTATTRS_EPT_R_MASK));
                Assert(RT_BOOL(Walk.fEffective & PGM_PTATTRS_W_MASK)  ==  RT_BOOL(Walk.fEffective & PGM_PTATTRS_EPT_W_MASK));
                Assert(RT_BOOL(Walk.fEffective & PGM_PTATTRS_NX_MASK) == !RT_BOOL(Walk.fEffective & PGM_PTATTRS_EPT_X_SUPER_MASK));
#   endif
                PGM_A20_ASSERT_MASKED(pVCpu, Walk.GCPhys);
                Assert(!(fFlags & X86_PTE_RW) || (Walk.fEffective & PGM_PTATTRS_W_MASK));

                /* Update the nested-guest physical address with the translated guest-physical address. */
                GCPtr = Walk.GCPhys;

                /* Get the PD. */
                PSHWPD pEptPd;
                rc = pgmShwGetNestedEPTPDPtr(pVCpu, GCPhysNestedPage, NULL /*ppPdpt*/, &pEptPd, &GstWalkAll);
                AssertRCReturn(rc, rc);
                Assert(pEptPd);
                Assert(iPd < EPT_PG_ENTRIES);
                Pde = pEptPd->a[iPd];
            }
            else
            {
                Log(("Failed to translate nested-guest physical address %#RGp rc=%Rrc\n", GCPhysNestedPage, rc));
                return rc;
            }

#  else  /* !VBOX_WITH_NESTED_HWVIRT_VMX_EPT */
            AssertFailed();
            return VERR_PGM_SHW_NONE_IPE;
#  endif /* !VBOX_WITH_NESTED_HWVIRT_VMX_EPT */
        }

# else /* PGM_TYPE_32BIT || PGM_SHW_TYPE == PGM_TYPE_NESTED_32BIT */
        X86PDE          Pde = pgmShwGet32BitPDE(pVCpu, GCPtr);
# endif
        if (!SHW_PDE_IS_P(Pde))
            return VERR_PAGE_TABLE_NOT_PRESENT;

        AssertFatalMsg(!SHW_PDE_IS_BIG(Pde), ("Pde=%#RX64\n", (uint64_t)Pde.u));

        /*
         * Map the page table.
         */
        PSHWPT          pPT;
        rc = PGM_HCPHYS_2_PTR(pVM, pVCpu, Pde.u & SHW_PDE_PG_MASK, &pPT);
        if (RT_FAILURE(rc))
            return rc;

        unsigned        iPTE = (GCPtr >> SHW_PT_SHIFT) & SHW_PT_MASK;
        while (iPTE < RT_ELEMENTS(pPT->a))
        {
            if (SHW_PTE_IS_P(pPT->a[iPTE]))
            {
                SHWPTE const    OrgPte = pPT->a[iPTE];
                SHWPTE          NewPte;

                SHW_PTE_SET(NewPte, (SHW_PTE_GET_U(OrgPte) & (fMask | SHW_PTE_PG_MASK)) | (fFlags & ~SHW_PTE_PG_MASK));
                if (!SHW_PTE_IS_P(NewPte))
                {
                    /** @todo Some CSAM code path might end up here and upset
                     *  the page pool. */
                    AssertMsgFailed(("NewPte=%#RX64 OrgPte=%#RX64 GCPtr=%#RGv\n", SHW_PTE_LOG64(NewPte), SHW_PTE_LOG64(OrgPte), GCPtr));
                }
                else if (   SHW_PTE_IS_RW(NewPte)
                         && !SHW_PTE_IS_RW(OrgPte)
                         && !(fOpFlags & PGM_MK_PG_IS_MMIO2) )
                {
                    /** @todo Optimize \#PF handling by caching data.  We can
                     *        then use this when PGM_MK_PG_IS_WRITE_FAULT is
                     *        set instead of resolving the guest physical
                     *        address yet again. */
                    PGMPTWALK GstWalk;
                    rc = PGMGstGetPage(pVCpu, GCPtr, &GstWalk);
                    AssertRC(rc);
                    if (RT_SUCCESS(rc))
                    {
                        Assert((GstWalk.fEffective & X86_PTE_RW) || !(CPUMGetGuestCR0(pVCpu) & X86_CR0_WP /* allow netware hack */));
                        PPGMPAGE pPage = pgmPhysGetPage(pVM, GstWalk.GCPhys);
                        Assert(pPage);
                        if (pPage)
                        {
                            rc = pgmPhysPageMakeWritable(pVM, pPage, GstWalk.GCPhys);
                            AssertRCReturn(rc, rc);
                            Log(("%s: pgmPhysPageMakeWritable on %RGv / %RGp %R[pgmpage]\n", __PRETTY_FUNCTION__, GCPtr, GstWalk.GCPhys, pPage));
                        }
                    }
                }

                SHW_PTE_ATOMIC_SET2(pPT->a[iPTE], NewPte);
                Assert((SHW_PTE_GET_U(NewPte) & EPT_E_LEAF) == (SHW_PTE_GET_U(OrgPte) & EPT_E_LEAF));

# if PGM_SHW_TYPE == PGM_TYPE_EPT
                HMInvalidatePhysPage(pVM, (RTGCPHYS)GCPtr);
# else
                PGM_INVL_PG_ALL_VCPU(pVM, GCPtr);
# endif
            }

            /* next page */
            cb -= HOST_PAGE_SIZE;
            if (!cb)
                return VINF_SUCCESS;
            GCPtr += HOST_PAGE_SIZE;
            iPTE++;
        }
    }
#endif /* PGM_SHW_TYPE != PGM_TYPE_NONE */
}


#ifdef IN_RING3
/**
 * Relocate any GC pointers related to shadow mode paging.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   offDelta    The relocation offset.
 */
PGM_SHW_DECL(int, Relocate)(PVMCPUCC pVCpu, RTGCPTR offDelta)
{
    RT_NOREF(pVCpu, offDelta);
    return VINF_SUCCESS;
}
#endif

