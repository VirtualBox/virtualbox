/* $Id: PGMAllBth.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox - Page Manager, Shadow+Guest Paging Template - All context code.
 *
 * @remarks Extended page tables (intel) are built with PGM_GST_TYPE set to
 *          PGM_TYPE_PROT (and PGM_SHW_TYPE set to PGM_TYPE_EPT).
 *          bird: WTF does this mean these days?  Looking at PGMAll.cpp it's
 *
 * @remarks This file is one big \#ifdef-orgy!
 *
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

#ifdef _MSC_VER
/** @todo we're generating unnecessary code in nested/ept shadow mode and for
 *        real/prot-guest+RC mode. */
# pragma warning(disable: 4505)
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
RT_C_DECLS_BEGIN
PGM_BTH_DECL(int, Enter)(PVMCPUCC pVCpu, RTGCPHYS GCPhysCR3);
#ifndef IN_RING3
PGM_BTH_DECL(int, Trap0eHandler)(PVMCPUCC pVCpu, RTGCUINT uErr, PCPUMCTX pCtx, RTGCPTR pvFault, bool *pfLockTaken);
PGM_BTH_DECL(int, NestedTrap0eHandler)(PVMCPUCC pVCpu, RTGCUINT uErr, PCPUMCTX pCtx, RTGCPHYS GCPhysNestedFault,
                                       bool fIsLinearAddrValid, RTGCPTR GCPtrNestedFault, PPGMPTWALK pWalk, bool *pfLockTaken);
# if defined(VBOX_WITH_NESTED_HWVIRT_VMX_EPT) && PGM_SHW_TYPE == PGM_TYPE_EPT
static void PGM_BTH_NAME(NestedSyncPageWorker)(PVMCPUCC pVCpu, PSHWPTE pPte, RTGCPHYS GCPhysPage, PPGMPOOLPAGE pShwPage,
                                               unsigned iPte, SLATPTE GstSlatPte);
static int  PGM_BTH_NAME(NestedSyncPage)(PVMCPUCC pVCpu, RTGCPHYS GCPhysNestedPage, RTGCPHYS GCPhysPage, unsigned cPages,
                                         uint32_t uErr, PPGMPTWALKGST pGstWalkAll);
static int  PGM_BTH_NAME(NestedSyncPT)(PVMCPUCC pVCpu, RTGCPHYS GCPhysNestedPage, RTGCPHYS GCPhysPage, PPGMPTWALKGST pGstWalkAll);
# endif /* VBOX_WITH_NESTED_HWVIRT_VMX_EPT */
#endif
PGM_BTH_DECL(int, InvalidatePage)(PVMCPUCC pVCpu, RTGCPTR GCPtrPage);
static int PGM_BTH_NAME(SyncPage)(PVMCPUCC pVCpu, GSTPDE PdeSrc, RTGCPTR GCPtrPage, unsigned cPages, unsigned uErr);
static int PGM_BTH_NAME(CheckDirtyPageFault)(PVMCPUCC pVCpu, uint32_t uErr, PSHWPDE pPdeDst, GSTPDE const *pPdeSrc, RTGCPTR GCPtrPage);
static int PGM_BTH_NAME(SyncPT)(PVMCPUCC pVCpu, unsigned iPD, PGSTPD pPDSrc, RTGCPTR GCPtrPage);
#if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
static void PGM_BTH_NAME(SyncPageWorker)(PVMCPUCC pVCpu, PSHWPTE pPteDst, GSTPDE PdeSrc, GSTPTE PteSrc, PPGMPOOLPAGE pShwPage, unsigned iPTDst);
#else
static void PGM_BTH_NAME(SyncPageWorker)(PVMCPUCC pVCpu, PSHWPTE pPteDst, RTGCPHYS GCPhysPage, PPGMPOOLPAGE pShwPage, unsigned iPTDst);
#endif
PGM_BTH_DECL(int, VerifyAccessSyncPage)(PVMCPUCC pVCpu, RTGCPTR Addr, unsigned fPage, unsigned uErr);
PGM_BTH_DECL(int, PrefetchPage)(PVMCPUCC pVCpu, RTGCPTR GCPtrPage);
PGM_BTH_DECL(int, SyncCR3)(PVMCPUCC pVCpu, uint64_t cr0, uint64_t cr3, uint64_t cr4, bool fGlobal);
#ifdef VBOX_STRICT
PGM_BTH_DECL(unsigned, AssertCR3)(PVMCPUCC pVCpu, uint64_t cr3, uint64_t cr4, RTGCPTR GCPtr = 0, RTGCPTR cb = ~(RTGCPTR)0);
#endif
PGM_BTH_DECL(int, MapCR3)(PVMCPUCC pVCpu, RTGCPHYS GCPhysCR3);
PGM_BTH_DECL(int, UnmapCR3)(PVMCPUCC pVCpu);

#ifdef IN_RING3
PGM_BTH_DECL(int, Relocate)(PVMCPUCC pVCpu, RTGCPTR offDelta);
#endif
RT_C_DECLS_END




/*
 * Filter out some illegal combinations of guest and shadow paging, so we can
 * remove redundant checks inside functions.
 */
#if    PGM_GST_TYPE == PGM_TYPE_PAE && PGM_SHW_TYPE != PGM_TYPE_PAE \
    && !PGM_TYPE_IS_NESTED_OR_EPT(PGM_SHW_TYPE) && PGM_SHW_TYPE != PGM_TYPE_NONE
# error "Invalid combination; PAE guest implies PAE shadow"
#endif

#if    (PGM_GST_TYPE == PGM_TYPE_REAL || PGM_GST_TYPE == PGM_TYPE_PROT) \
    && !(   PGM_SHW_TYPE == PGM_TYPE_32BIT || PGM_SHW_TYPE == PGM_TYPE_PAE || PGM_SHW_TYPE == PGM_TYPE_AMD64 \
         || PGM_TYPE_IS_NESTED_OR_EPT(PGM_SHW_TYPE) || PGM_SHW_TYPE == PGM_TYPE_NONE)
# error "Invalid combination; real or protected mode without paging implies 32 bits or PAE shadow paging."
#endif

#if     (PGM_GST_TYPE == PGM_TYPE_32BIT || PGM_GST_TYPE == PGM_TYPE_PAE) \
    && !(   PGM_SHW_TYPE == PGM_TYPE_32BIT || PGM_SHW_TYPE == PGM_TYPE_PAE \
         || PGM_TYPE_IS_NESTED_OR_EPT(PGM_SHW_TYPE) || PGM_SHW_TYPE == PGM_TYPE_NONE)
# error "Invalid combination; 32 bits guest paging or PAE implies 32 bits or PAE shadow paging."
#endif

#if    (PGM_GST_TYPE == PGM_TYPE_AMD64 && PGM_SHW_TYPE != PGM_TYPE_AMD64 && !PGM_TYPE_IS_NESTED_OR_EPT(PGM_SHW_TYPE) && PGM_SHW_TYPE != PGM_TYPE_NONE) \
    || (PGM_SHW_TYPE == PGM_TYPE_AMD64 && PGM_GST_TYPE != PGM_TYPE_AMD64 && PGM_GST_TYPE != PGM_TYPE_PROT)
# error "Invalid combination; AMD64 guest implies AMD64 shadow and vice versa"
#endif


/**
 * Enters the shadow+guest mode.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPhysCR3   The physical address from the CR3 register.
 */
PGM_BTH_DECL(int, Enter)(PVMCPUCC pVCpu, RTGCPHYS GCPhysCR3)
{
    /* Here we deal with allocation of the root shadow page table for real and protected mode during mode switches;
     * Other modes rely on MapCR3/UnmapCR3 to setup the shadow root page tables.
     */
#if  (   (   PGM_SHW_TYPE == PGM_TYPE_32BIT \
          || PGM_SHW_TYPE == PGM_TYPE_PAE    \
          || PGM_SHW_TYPE == PGM_TYPE_AMD64) \
      && (   PGM_GST_TYPE == PGM_TYPE_REAL   \
          || PGM_GST_TYPE == PGM_TYPE_PROT))

    PVMCC pVM = pVCpu->CTX_SUFF(pVM);

    Assert(!pVM->pgm.s.fNestedPaging);

    PGM_LOCK_VOID(pVM);
    /* Note: we only really need shadow paging in real and protected mode for VT-x and AMD-V (excluding nested paging/EPT modes),
     *       but any calls to GC need a proper shadow page setup as well.
     */
    /* Free the previous root mapping if still active. */
    PPGMPOOL pPool = pVM->pgm.s.CTX_SUFF(pPool);
    PPGMPOOLPAGE pOldShwPageCR3 = pVCpu->pgm.s.CTX_SUFF(pShwPageCR3);
    if (pOldShwPageCR3)
    {
        Assert(pOldShwPageCR3->enmKind != PGMPOOLKIND_FREE);

        /* Mark the page as unlocked; allow flushing again. */
        pgmPoolUnlockPage(pPool, pVCpu->pgm.s.CTX_SUFF(pShwPageCR3));

        pgmPoolFreeByPage(pPool, pOldShwPageCR3, NIL_PGMPOOL_IDX, UINT32_MAX);
        pVCpu->pgm.s.pShwPageCR3R3 = NIL_RTR3PTR;
        pVCpu->pgm.s.pShwPageCR3R0 = NIL_RTR0PTR;
    }

    /* construct a fake address. */
    GCPhysCR3 = RT_BIT_64(63);
    PPGMPOOLPAGE pNewShwPageCR3;
    int rc = pgmPoolAlloc(pVM, GCPhysCR3, BTH_PGMPOOLKIND_ROOT, PGMPOOLACCESS_DONTCARE, PGM_A20_IS_ENABLED(pVCpu),
                          NIL_PGMPOOL_IDX, UINT32_MAX, false /*fLockPage*/,
                          &pNewShwPageCR3);
    AssertRCReturn(rc, rc);

    pVCpu->pgm.s.pShwPageCR3R3 = pgmPoolConvertPageToR3(pPool, pNewShwPageCR3);
    pVCpu->pgm.s.pShwPageCR3R0 = pgmPoolConvertPageToR0(pPool, pNewShwPageCR3);

    /* Mark the page as locked; disallow flushing. */
    pgmPoolLockPage(pPool, pNewShwPageCR3);

    /* Set the current hypervisor CR3. */
    CPUMSetHyperCR3(pVCpu, PGMGetHyperCR3(pVCpu));

    PGM_UNLOCK(pVM);
    return rc;
#else
    NOREF(pVCpu); NOREF(GCPhysCR3);
    return VINF_SUCCESS;
#endif
}


#ifndef IN_RING3

# if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
/**
 * Deal with a guest page fault.
 *
 * @returns Strict VBox status code.
 * @retval  VINF_EM_RAW_GUEST_TRAP
 * @retval  VINF_EM_RAW_EMULATE_INSTR
 *
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   pWalk           The guest page table walk result.
 * @param   uErr            The error code.
 */
PGM_BTH_DECL(VBOXSTRICTRC, Trap0eHandlerGuestFault)(PVMCPUCC pVCpu, PPGMPTWALK pWalk, RTGCUINT uErr)
{
    /*
     * Calc the error code for the guest trap.
     */
    uint32_t uNewErr = GST_IS_NX_ACTIVE(pVCpu)
                     ? uErr & (X86_TRAP_PF_RW | X86_TRAP_PF_US | X86_TRAP_PF_ID)
                     : uErr & (X86_TRAP_PF_RW | X86_TRAP_PF_US);
    if (   pWalk->fRsvdError
        || pWalk->fBadPhysAddr)
    {
        uNewErr |= X86_TRAP_PF_RSVD | X86_TRAP_PF_P;
        Assert(!pWalk->fNotPresent);
    }
    else if (!pWalk->fNotPresent)
        uNewErr |= X86_TRAP_PF_P;
    TRPMSetErrorCode(pVCpu, uNewErr);

    LogFlow(("Guest trap; cr2=%RGv uErr=%RGv lvl=%d\n", pWalk->GCPtr, uErr, pWalk->uLevel));
    STAM_STATS({ pVCpu->pgmr0.s.pStatTrap0eAttributionR0 = &pVCpu->pgm.s.Stats.StatRZTrap0eTime2GuestTrap; });
    return VINF_EM_RAW_GUEST_TRAP;
}
# endif /* PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE) */


#if !PGM_TYPE_IS_NESTED(PGM_SHW_TYPE) && PGM_SHW_TYPE != PGM_TYPE_NONE
/**
 * Deal with a guest page fault.
 *
 * The caller has taken the PGM lock.
 *
 * @returns Strict VBox status code.
 *
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   uErr            The error code.
 * @param   pCtx            Pointer to the register context for the CPU.
 * @param   pvFault         The fault address.
 * @param   pPage           The guest page at @a pvFault.
 * @param   pWalk           The guest page table walk result.
 * @param   pGstWalk        The guest paging-mode specific walk information.
 * @param   pfLockTaken     PGM lock taken here or not (out).  This is true
 *                          when we're called.
 */
static VBOXSTRICTRC PGM_BTH_NAME(Trap0eHandlerDoAccessHandlers)(PVMCPUCC pVCpu, RTGCUINT uErr, PCPUMCTX pCtx,
                                                                RTGCPTR pvFault, PPGMPAGE pPage, bool *pfLockTaken
# if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE) || defined(DOXYGEN_RUNNING)
                                                                , PPGMPTWALK pWalk
                                                                , PGSTPTWALK pGstWalk
# endif
                                                                )
{
# if !PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
    GSTPDE const    PdeSrcDummy = { X86_PDE_P | X86_PDE_US | X86_PDE_RW | X86_PDE_A };
# endif
    PVMCC           pVM         = pVCpu->CTX_SUFF(pVM);
    VBOXSTRICTRC    rcStrict;

    if (PGM_PAGE_HAS_ANY_PHYSICAL_HANDLERS(pPage))
    {
        /*
         * Physical page access handler.
         */
# if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
        const RTGCPHYS  GCPhysFault = pWalk->GCPhys;
# else
        const RTGCPHYS  GCPhysFault = PGM_A20_APPLY(pVCpu, (RTGCPHYS)pvFault);
# endif
        PPGMPHYSHANDLER pCur;
        rcStrict = pgmHandlerPhysicalLookup(pVM, GCPhysFault, &pCur);
        if (RT_SUCCESS(rcStrict))
        {
            PCPGMPHYSHANDLERTYPEINT const pCurType = PGMPHYSHANDLER_GET_TYPE(pVM, pCur);

#  ifdef PGM_SYNC_N_PAGES
            /*
             * If the region is write protected and we got a page not present fault, then sync
             * the pages. If the fault was caused by a read, then restart the instruction.
             * In case of write access continue to the GC write handler.
             *
             * ASSUMES that there is only one handler per page or that they have similar write properties.
             */
            if (   !(uErr & X86_TRAP_PF_P)
                &&  pCurType->enmKind == PGMPHYSHANDLERKIND_WRITE)
            {
#   if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
                rcStrict = PGM_BTH_NAME(SyncPage)(pVCpu, pGstWalk->Pde, pvFault, PGM_SYNC_NR_PAGES, uErr);
#   else
                rcStrict = PGM_BTH_NAME(SyncPage)(pVCpu, PdeSrcDummy, pvFault, PGM_SYNC_NR_PAGES, uErr);
#   endif
                if (    RT_FAILURE(rcStrict)
                    || !(uErr & X86_TRAP_PF_RW)
                    || rcStrict == VINF_PGM_SYNCPAGE_MODIFIED_PDE)
                {
                    AssertMsgRC(rcStrict, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
                    STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eHandlersOutOfSync);
                    STAM_STATS({ pVCpu->pgmr0.s.pStatTrap0eAttributionR0 = &pVCpu->pgm.s.Stats.StatRZTrap0eTime2OutOfSyncHndPhys; });
                    return rcStrict;
                }
            }
#  endif
#  ifdef PGM_WITH_MMIO_OPTIMIZATIONS
            /*
             * If the access was not thru a #PF(RSVD|...) resync the page.
             */
            if (   !(uErr & X86_TRAP_PF_RSVD)
                && pCurType->enmKind != PGMPHYSHANDLERKIND_WRITE
#   if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
                && (pWalk->fEffective & (PGM_PTATTRS_W_MASK | PGM_PTATTRS_US_MASK))
                                      == PGM_PTATTRS_W_MASK  /** @todo Remove pGstWalk->Core.fEffectiveUS and X86_PTE_US further down in the sync code. */
#   endif
               )
            {
#   if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
                rcStrict = PGM_BTH_NAME(SyncPage)(pVCpu, pGstWalk->Pde, pvFault, PGM_SYNC_NR_PAGES, uErr);
#   else
                rcStrict = PGM_BTH_NAME(SyncPage)(pVCpu, PdeSrcDummy, pvFault, PGM_SYNC_NR_PAGES, uErr);
#   endif
                if (    RT_FAILURE(rcStrict)
                    || rcStrict == VINF_PGM_SYNCPAGE_MODIFIED_PDE)
                {
                    AssertMsgRC(rcStrict, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
                    STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eHandlersOutOfSync);
                    STAM_STATS({ pVCpu->pgmr0.s.pStatTrap0eAttributionR0 = &pVCpu->pgm.s.Stats.StatRZTrap0eTime2OutOfSyncHndPhys; });
                    return rcStrict;
                }
            }
#  endif

            AssertMsg(   pCurType->enmKind != PGMPHYSHANDLERKIND_WRITE
                      || (pCurType->enmKind == PGMPHYSHANDLERKIND_WRITE && (uErr & X86_TRAP_PF_RW)),
                      ("Unexpected trap for physical handler: %08X (phys=%08x) pPage=%R[pgmpage] uErr=%X, enmKind=%d\n",
                       pvFault, GCPhysFault, pPage, uErr, pCurType->enmKind));
            if (pCurType->enmKind == PGMPHYSHANDLERKIND_WRITE)
                STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eHandlersPhysWrite);
            else
            {
                STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eHandlersPhysAll);
                if (uErr & X86_TRAP_PF_RSVD) STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eHandlersPhysAllOpt);
            }

            if (pCurType->pfnPfHandler)
            {
                STAM_PROFILE_START(&pCur->Stat, h);

                if (pCurType->fKeepPgmLock)
                {
                    rcStrict = pCurType->pfnPfHandler(pVM, pVCpu, uErr, pCtx, pvFault, GCPhysFault,
                                                      !pCurType->fRing0DevInsIdx ? pCur->uUser
                                                      : (uintptr_t)PDMDeviceRing0IdxToInstance(pVM, pCur->uUser));

                    STAM_PROFILE_STOP(&pCur->Stat, h); /* no locking needed, entry is unlikely reused before we get here. */
                }
                else
                {
                    uint64_t const uUser = !pCurType->fRing0DevInsIdx ? pCur->uUser
                                         : (uintptr_t)PDMDeviceRing0IdxToInstance(pVM, pCur->uUser);
                    PGM_UNLOCK(pVM);
                    *pfLockTaken = false;

                    rcStrict = pCurType->pfnPfHandler(pVM, pVCpu, uErr, pCtx, pvFault, GCPhysFault, uUser);

                    STAM_PROFILE_STOP(&pCur->Stat, h); /* no locking needed, entry is unlikely reused before we get here. */
                }
            }
            else
                rcStrict = VINF_EM_RAW_EMULATE_INSTR;

            STAM_STATS({ pVCpu->pgmr0.s.pStatTrap0eAttributionR0 = &pVCpu->pgm.s.Stats.StatRZTrap0eTime2HndPhys; });
            return rcStrict;
        }
        AssertMsgReturn(rcStrict == VERR_NOT_FOUND, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)), rcStrict);
    }

    /*
     * There is a handled area of the page, but this fault doesn't belong to it.
     * We must emulate the instruction.
     *
     * To avoid crashing (non-fatal) in the interpreter and go back to the recompiler
     * we first check if this was a page-not-present fault for a page with only
     * write access handlers. Restart the instruction if it wasn't a write access.
     */
    STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eHandlersUnhandled);

    if (    !PGM_PAGE_HAS_ACTIVE_ALL_HANDLERS(pPage)
        &&  !(uErr & X86_TRAP_PF_P))
    {
#  if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
        rcStrict = PGM_BTH_NAME(SyncPage)(pVCpu, pGstWalk->Pde, pvFault, PGM_SYNC_NR_PAGES, uErr);
#  else
        rcStrict = PGM_BTH_NAME(SyncPage)(pVCpu, PdeSrcDummy, pvFault, PGM_SYNC_NR_PAGES, uErr);
#  endif
        if (    RT_FAILURE(rcStrict)
            ||  rcStrict == VINF_PGM_SYNCPAGE_MODIFIED_PDE
            ||  !(uErr & X86_TRAP_PF_RW))
        {
            AssertMsgRC(rcStrict, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
            STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eHandlersOutOfSync);
            STAM_STATS({ pVCpu->pgmr0.s.pStatTrap0eAttributionR0 = &pVCpu->pgm.s.Stats.StatRZTrap0eTime2OutOfSyncHndPhys; });
            return rcStrict;
        }
    }

    /** @todo This particular case can cause quite a lot of overhead. E.g. early stage of kernel booting in Ubuntu 6.06
     *        It's writing to an unhandled part of the LDT page several million times.
     */
    rcStrict = PGMInterpretInstruction(pVCpu, pvFault);
    LogFlow(("PGM: PGMInterpretInstruction -> rcStrict=%d pPage=%R[pgmpage]\n", VBOXSTRICTRC_VAL(rcStrict), pPage));
    STAM_STATS({ pVCpu->pgmr0.s.pStatTrap0eAttributionR0 = &pVCpu->pgm.s.Stats.StatRZTrap0eTime2HndUnhandled; });
    return rcStrict;
} /* if any kind of handler */
# endif /* !PGM_TYPE_IS_NESTED(PGM_SHW_TYPE) && PGM_SHW_TYPE != PGM_TYPE_NONE*/


/**
 * \#PF Handler for raw-mode guest execution.
 *
 * @returns VBox status code (appropriate for trap handling and GC return).
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   uErr        The trap error code.
 * @param   pCtx        Pointer to the register context for the CPU.
 * @param   pvFault     The fault address.
 * @param   pfLockTaken PGM lock taken here or not (out)
 */
PGM_BTH_DECL(int, Trap0eHandler)(PVMCPUCC pVCpu, RTGCUINT uErr, PCPUMCTX pCtx, RTGCPTR pvFault, bool *pfLockTaken)
{
    PVMCC pVM = pVCpu->CTX_SUFF(pVM); NOREF(pVM);

    *pfLockTaken = false;

# if  (   PGM_GST_TYPE == PGM_TYPE_32BIT || PGM_GST_TYPE == PGM_TYPE_REAL || PGM_GST_TYPE == PGM_TYPE_PROT \
       || PGM_GST_TYPE == PGM_TYPE_PAE   || PGM_GST_TYPE == PGM_TYPE_AMD64) \
    && !PGM_TYPE_IS_NESTED(PGM_SHW_TYPE) \
    && (PGM_SHW_TYPE != PGM_TYPE_EPT || PGM_GST_TYPE == PGM_TYPE_PROT) \
    && PGM_SHW_TYPE != PGM_TYPE_NONE
    int rc;

#  if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
    /*
     * Walk the guest page translation tables and check if it's a guest fault.
     */
    PGMPTWALK Walk;
    GSTPTWALK GstWalk;
    rc = PGM_GST_NAME(Walk)(pVCpu, pvFault, &Walk, &GstWalk);
    if (RT_FAILURE_NP(rc))
        return VBOXSTRICTRC_TODO(PGM_BTH_NAME(Trap0eHandlerGuestFault)(pVCpu, &Walk, uErr));

    /* assert some GstWalk sanity. */
#   if PGM_GST_TYPE == PGM_TYPE_AMD64
    /*AssertMsg(GstWalk.Pml4e.u == GstWalk.pPml4e->u, ("%RX64 %RX64\n", (uint64_t)GstWalk.Pml4e.u, (uint64_t)GstWalk.pPml4e->u));  - not always true with SMP guests. */
#   endif
#   if PGM_GST_TYPE == PGM_TYPE_AMD64 || PGM_GST_TYPE == PGM_TYPE_PAE
    /*AssertMsg(GstWalk.Pdpe.u == GstWalk.pPdpe->u, ("%RX64 %RX64\n", (uint64_t)GstWalk.Pdpe.u, (uint64_t)GstWalk.pPdpe->u)); - ditto */
#   endif
    /*AssertMsg(GstWalk.Pde.u == GstWalk.pPde->u, ("%RX64 %RX64\n", (uint64_t)GstWalk.Pde.u, (uint64_t)GstWalk.pPde->u)); - ditto */
    /*AssertMsg(GstWalk.Core.fBigPage || GstWalk.Pte.u == GstWalk.pPte->u, ("%RX64 %RX64\n", (uint64_t)GstWalk.Pte.u, (uint64_t)GstWalk.pPte->u)); - ditto */
    Assert(Walk.fSucceeded);
    Assert(Walk.fEffective & PGM_PTATTRS_R_MASK);

    if (uErr & (X86_TRAP_PF_RW | X86_TRAP_PF_US | X86_TRAP_PF_ID))
    {
        if (    (   (uErr & X86_TRAP_PF_RW)
                 && !(Walk.fEffective & PGM_PTATTRS_W_MASK)
                 && (   (uErr & X86_TRAP_PF_US)
                     || CPUMIsGuestR0WriteProtEnabled(pVCpu)) )
            ||  ((uErr & X86_TRAP_PF_US) && !(Walk.fEffective & PGM_PTATTRS_US_MASK))
            ||  ((uErr & X86_TRAP_PF_ID) &&  (Walk.fEffective & PGM_PTATTRS_NX_MASK))
           )
            return VBOXSTRICTRC_TODO(PGM_BTH_NAME(Trap0eHandlerGuestFault)(pVCpu, &Walk, uErr));
    }

    /* Take the big lock now before we update flags. */
    *pfLockTaken = true;
    PGM_LOCK_VOID(pVM);

    /*
     * Set the accessed and dirty flags.
     */
    /** @todo Should probably use cmpxchg logic here as we're potentially racing
     *        other CPUs in SMP configs. (the lock isn't enough, since we take it
     *        after walking and the page tables could be stale already) */
#   if PGM_GST_TYPE == PGM_TYPE_AMD64
    if (!(GstWalk.Pml4e.u & X86_PML4E_A))
    {
        GstWalk.Pml4e.u |= X86_PML4E_A;
        GST_ATOMIC_OR(&GstWalk.pPml4e->u, X86_PML4E_A);
    }
    if (!(GstWalk.Pdpe.u & X86_PDPE_A))
    {
        GstWalk.Pdpe.u |= X86_PDPE_A;
        GST_ATOMIC_OR(&GstWalk.pPdpe->u, X86_PDPE_A);
    }
#   endif
    if (Walk.fBigPage)
    {
        Assert(GstWalk.Pde.u & X86_PDE_PS);
        if (uErr & X86_TRAP_PF_RW)
        {
            if ((GstWalk.Pde.u & (X86_PDE4M_A | X86_PDE4M_D)) != (X86_PDE4M_A | X86_PDE4M_D))
            {
                GstWalk.Pde.u |= X86_PDE4M_A | X86_PDE4M_D;
                GST_ATOMIC_OR(&GstWalk.pPde->u, X86_PDE4M_A | X86_PDE4M_D);
            }
        }
        else
        {
            if (!(GstWalk.Pde.u & X86_PDE4M_A))
            {
                GstWalk.Pde.u |= X86_PDE4M_A;
                GST_ATOMIC_OR(&GstWalk.pPde->u, X86_PDE4M_A);
            }
        }
    }
    else
    {
        Assert(!(GstWalk.Pde.u & X86_PDE_PS));
        if (!(GstWalk.Pde.u & X86_PDE_A))
        {
            GstWalk.Pde.u |= X86_PDE_A;
            GST_ATOMIC_OR(&GstWalk.pPde->u, X86_PDE_A);
        }

        if (uErr & X86_TRAP_PF_RW)
        {
#   ifdef VBOX_WITH_STATISTICS
            if (GstWalk.Pte.u & X86_PTE_D)
                STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,PageAlreadyDirty));
            else
                STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,DirtiedPage));
#   endif
            if ((GstWalk.Pte.u & (X86_PTE_A | X86_PTE_D)) != (X86_PTE_A | X86_PTE_D))
            {
                GstWalk.Pte.u |= X86_PTE_A | X86_PTE_D;
                GST_ATOMIC_OR(&GstWalk.pPte->u, X86_PTE_A | X86_PTE_D);
            }
        }
        else
        {
            if (!(GstWalk.Pte.u & X86_PTE_A))
            {
                GstWalk.Pte.u |= X86_PTE_A;
                GST_ATOMIC_OR(&GstWalk.pPte->u, X86_PTE_A);
            }
        }
        Assert(GstWalk.Pte.u == GstWalk.pPte->u);
    }
#if 0
    /* Disabling this since it's not reliable for SMP, see @bugref{10092#c22}. */
    AssertMsg(GstWalk.Pde.u == GstWalk.pPde->u || GstWalk.pPte->u == GstWalk.pPde->u,
              ("%RX64 %RX64 pPte=%p pPde=%p Pte=%RX64\n", (uint64_t)GstWalk.Pde.u, (uint64_t)GstWalk.pPde->u, GstWalk.pPte, GstWalk.pPde, (uint64_t)GstWalk.pPte->u));
#endif

#  else  /* !PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE) */
    GSTPDE const PdeSrcDummy = { X86_PDE_P | X86_PDE_US | X86_PDE_RW | X86_PDE_A}; /** @todo eliminate this */

    /* Take the big lock now. */
    *pfLockTaken = true;
    PGM_LOCK_VOID(pVM);
#  endif /* !PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE) */

#  ifdef PGM_WITH_MMIO_OPTIMIZATIONS
    /*
     * If it is a reserved bit fault we know that it is an MMIO (access
     * handler) related fault and can skip some 200 lines of code.
     */
    if (uErr & X86_TRAP_PF_RSVD)
    {
        Assert(uErr & X86_TRAP_PF_P);
        PPGMPAGE pPage;
#   if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
        rc = pgmPhysGetPageEx(pVM, Walk.GCPhys, &pPage);
        if (RT_SUCCESS(rc) && PGM_PAGE_HAS_ACTIVE_ALL_HANDLERS(pPage))
            return VBOXSTRICTRC_TODO(PGM_BTH_NAME(Trap0eHandlerDoAccessHandlers)(pVCpu, uErr, pCtx, pvFault, pPage,
                                                                                 pfLockTaken, &Walk, &GstWalk));
        rc = PGM_BTH_NAME(SyncPage)(pVCpu, GstWalk.Pde, pvFault, 1, uErr);
#   else
        rc = pgmPhysGetPageEx(pVM, PGM_A20_APPLY(pVCpu, (RTGCPHYS)pvFault), &pPage);
        if (RT_SUCCESS(rc) && PGM_PAGE_HAS_ACTIVE_ALL_HANDLERS(pPage))
            return VBOXSTRICTRC_TODO(PGM_BTH_NAME(Trap0eHandlerDoAccessHandlers)(pVCpu, uErr, pCtx, pvFault, pPage, pfLockTaken));
        rc = PGM_BTH_NAME(SyncPage)(pVCpu, PdeSrcDummy, pvFault, 1, uErr);
#   endif
        AssertRC(rc);
        PGM_INVL_PG(pVCpu, pvFault);
        return rc; /* Restart with the corrected entry. */
    }
#  endif /* PGM_WITH_MMIO_OPTIMIZATIONS */

    /*
     * Fetch the guest PDE, PDPE and PML4E.
     */
#  if PGM_SHW_TYPE == PGM_TYPE_32BIT
    const unsigned  iPDDst = pvFault >> SHW_PD_SHIFT;
    PX86PD          pPDDst = pgmShwGet32BitPDPtr(pVCpu);

#  elif PGM_SHW_TYPE == PGM_TYPE_PAE
    const unsigned  iPDDst = (pvFault >> SHW_PD_SHIFT) & SHW_PD_MASK;   /* pPDDst index, not used with the pool. */
    PX86PDPAE       pPDDst;
#   if PGM_GST_TYPE == PGM_TYPE_PAE
    rc = pgmShwSyncPaePDPtr(pVCpu, pvFault, GstWalk.Pdpe.u, &pPDDst);
#   else
    rc = pgmShwSyncPaePDPtr(pVCpu, pvFault, X86_PDPE_P, &pPDDst);       /* RW, US and A are reserved in PAE mode. */
#   endif
    AssertMsgReturn(rc == VINF_SUCCESS, ("rc=%Rrc\n", rc), RT_FAILURE_NP(rc) ? rc : VERR_IPE_UNEXPECTED_INFO_STATUS);

#  elif PGM_SHW_TYPE == PGM_TYPE_AMD64
    const unsigned  iPDDst = ((pvFault >> SHW_PD_SHIFT) & SHW_PD_MASK);
    PX86PDPAE       pPDDst;
#   if PGM_GST_TYPE == PGM_TYPE_PROT  /* (AMD-V nested paging) */
    rc = pgmShwSyncLongModePDPtr(pVCpu, pvFault, X86_PML4E_P | X86_PML4E_RW | X86_PML4E_US | X86_PML4E_A,
                                 X86_PDPE_P | X86_PDPE_RW | X86_PDPE_US | X86_PDPE_A, &pPDDst);
#   else
    rc = pgmShwSyncLongModePDPtr(pVCpu, pvFault, GstWalk.Pml4e.u, GstWalk.Pdpe.u, &pPDDst);
#   endif
    AssertMsgReturn(rc == VINF_SUCCESS, ("rc=%Rrc\n", rc), RT_FAILURE_NP(rc) ? rc : VERR_IPE_UNEXPECTED_INFO_STATUS);

#  elif PGM_SHW_TYPE == PGM_TYPE_EPT
    const unsigned  iPDDst = ((pvFault >> SHW_PD_SHIFT) & SHW_PD_MASK);
    PEPTPD          pPDDst;
    rc = pgmShwGetEPTPDPtr(pVCpu, pvFault, NULL, &pPDDst);
    AssertMsgReturn(rc == VINF_SUCCESS, ("rc=%Rrc\n", rc), RT_FAILURE_NP(rc) ? rc : VERR_IPE_UNEXPECTED_INFO_STATUS);
#  endif
    Assert(pPDDst);

#  if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
    /*
     * Dirty page handling.
     *
     * If we successfully correct the write protection fault due to dirty bit
     * tracking, then return immediately.
     */
    if (uErr & X86_TRAP_PF_RW)  /* write fault? */
    {
        STAM_PROFILE_START(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,DirtyBitTracking), a);
        rc = PGM_BTH_NAME(CheckDirtyPageFault)(pVCpu, uErr, &pPDDst->a[iPDDst], GstWalk.pPde, pvFault);
        STAM_PROFILE_STOP(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,DirtyBitTracking), a);
        if (rc == VINF_PGM_HANDLED_DIRTY_BIT_FAULT)
        {
            STAM_STATS({ pVCpu->pgmr0.s.pStatTrap0eAttributionR0
                        = rc == VINF_PGM_HANDLED_DIRTY_BIT_FAULT
                          ? &pVCpu->pgm.s.Stats.StatRZTrap0eTime2DirtyAndAccessed
                          : &pVCpu->pgm.s.Stats.StatRZTrap0eTime2GuestTrap; });
            Log8(("Trap0eHandler: returns VINF_SUCCESS\n"));
            return VINF_SUCCESS;
        }
#ifdef DEBUG_bird
        AssertMsg(GstWalk.Pde.u == GstWalk.pPde->u || GstWalk.pPte->u == GstWalk.pPde->u || pVM->cCpus > 1, ("%RX64 %RX64\n", (uint64_t)GstWalk.Pde.u, (uint64_t)GstWalk.pPde->u)); // - triggers with smp w7 guests.
        AssertMsg(Walk.fBigPage || GstWalk.Pte.u == GstWalk.pPte->u || pVM->cCpus > 1, ("%RX64 %RX64\n", (uint64_t)GstWalk.Pte.u, (uint64_t)GstWalk.pPte->u)); // - ditto.
#endif
    }

#   if 0 /* rarely useful; leave for debugging. */
    STAM_COUNTER_INC(&pVCpu->pgm.s.StatRZTrap0ePD[iPDSrc]);
#   endif
#  endif /* PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE) */

    /*
     * A common case is the not-present error caused by lazy page table syncing.
     *
     * It is IMPORTANT that we weed out any access to non-present shadow PDEs
     * here so we can safely assume that the shadow PT is present when calling
     * SyncPage later.
     *
     * On failure, we ASSUME that SyncPT is out of memory or detected some kind
     * of mapping conflict and defer to SyncCR3 in R3.
     * (Again, we do NOT support access handlers for non-present guest pages.)
     *
     */
#  if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
    Assert(GstWalk.Pde.u & X86_PDE_P);
#  endif
    if (    !(uErr & X86_TRAP_PF_P) /* not set means page not present instead of page protection violation */
        &&  !SHW_PDE_IS_P(pPDDst->a[iPDDst]))
    {
        STAM_STATS({ pVCpu->pgmr0.s.pStatTrap0eAttributionR0 = &pVCpu->pgm.s.Stats.StatRZTrap0eTime2SyncPT; });
#  if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
        LogFlow(("=>SyncPT %04x = %08RX64\n", (pvFault >> GST_PD_SHIFT) & GST_PD_MASK, (uint64_t)GstWalk.Pde.u));
        rc = PGM_BTH_NAME(SyncPT)(pVCpu, (pvFault >> GST_PD_SHIFT) & GST_PD_MASK, GstWalk.pPd, pvFault);
#  else
        LogFlow(("=>SyncPT pvFault=%RGv\n", pvFault));
        rc = PGM_BTH_NAME(SyncPT)(pVCpu, 0, NULL, pvFault);
#  endif
        if (RT_SUCCESS(rc))
            return rc;
        Log(("SyncPT: %RGv failed!! rc=%Rrc\n", pvFault, rc));
        VMCPU_FF_SET(pVCpu, VMCPU_FF_PGM_SYNC_CR3); /** @todo no need to do global sync, right? */
        return VINF_PGM_SYNC_CR3;
    }

    /*
     * Check if this fault address is flagged for special treatment,
     * which means we'll have to figure out the physical address and
     * check flags associated with it.
     *
     * ASSUME that we can limit any special access handling to pages
     * in page tables which the guest believes to be present.
     */
#  if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
    RTGCPHYS GCPhys = Walk.GCPhys & ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK;
#  else
    RTGCPHYS GCPhys = PGM_A20_APPLY(pVCpu, (RTGCPHYS)pvFault & ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK);
#  endif /* PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE) */
    PPGMPAGE pPage;
    rc = pgmPhysGetPageEx(pVM, GCPhys, &pPage);
    if (RT_FAILURE(rc))
    {
        /*
         * When the guest accesses invalid physical memory (e.g. probing
         * of RAM or accessing a remapped MMIO range), then we'll fall
         * back to the recompiler to emulate the instruction.
         */
        LogFlow(("PGM #PF: pgmPhysGetPageEx(%RGp) failed with %Rrc\n", GCPhys, rc));
        STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eHandlersInvalid);
        STAM_STATS({ pVCpu->pgmr0.s.pStatTrap0eAttributionR0 = &pVCpu->pgm.s.Stats.StatRZTrap0eTime2InvalidPhys; });
        return VINF_EM_RAW_EMULATE_INSTR;
    }

    /*
     * Any handlers for this page?
     */
    if (PGM_PAGE_HAS_ACTIVE_HANDLERS(pPage) && !PGM_PAGE_IS_HNDL_PHYS_NOT_IN_HM(pPage))
# if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
        return VBOXSTRICTRC_TODO(PGM_BTH_NAME(Trap0eHandlerDoAccessHandlers)(pVCpu, uErr, pCtx, pvFault, pPage, pfLockTaken,
                                                                             &Walk, &GstWalk));
# else
        return VBOXSTRICTRC_TODO(PGM_BTH_NAME(Trap0eHandlerDoAccessHandlers)(pVCpu, uErr, pCtx, pvFault, pPage, pfLockTaken));
# endif

    /*
     * We are here only if page is present in Guest page tables and
     * trap is not handled by our handlers.
     *
     * Check it for page out-of-sync situation.
     */
    if (!(uErr & X86_TRAP_PF_P))
    {
        /*
         * Page is not present in our page tables. Try to sync it!
         */
        if (uErr & X86_TRAP_PF_US)
            STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,PageOutOfSyncUser));
        else /* supervisor */
            STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,PageOutOfSyncSupervisor));

        if (PGM_PAGE_IS_BALLOONED(pPage))
        {
            /* Emulate reads from ballooned pages as they are not present in
               our shadow page tables. (Required for e.g. Solaris guests; soft
               ecc, random nr generator.) */
            rc = VBOXSTRICTRC_TODO(PGMInterpretInstruction(pVCpu, pvFault));
            LogFlow(("PGM: PGMInterpretInstruction balloon -> rc=%d pPage=%R[pgmpage]\n", rc, pPage));
            STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,PageOutOfSyncBallloon));
            STAM_STATS({ pVCpu->pgmr0.s.pStatTrap0eAttributionR0 = &pVCpu->pgm.s.Stats.StatRZTrap0eTime2Ballooned; });
            return rc;
        }

#   if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
        rc = PGM_BTH_NAME(SyncPage)(pVCpu, GstWalk.Pde, pvFault, PGM_SYNC_NR_PAGES, uErr);
#   else
        rc = PGM_BTH_NAME(SyncPage)(pVCpu, PdeSrcDummy, pvFault, PGM_SYNC_NR_PAGES, uErr);
#   endif
        if (RT_SUCCESS(rc))
        {
            /* The page was successfully synced, return to the guest. */
            STAM_STATS({ pVCpu->pgmr0.s.pStatTrap0eAttributionR0 = &pVCpu->pgm.s.Stats.StatRZTrap0eTime2OutOfSync; });
            return VINF_SUCCESS;
        }
    }
    else /* uErr & X86_TRAP_PF_P: */
    {
        /*
         * Write protected pages are made writable when the guest makes the
         * first write to it.  This happens for pages that are shared, write
         * monitored or not yet allocated.
         *
         * We may also end up here when CR0.WP=0 in the guest.
         *
         * Also, a side effect of not flushing global PDEs are out of sync
         * pages due to physical monitored regions, that are no longer valid.
         * Assume for now it only applies to the read/write flag.
         */
        if (uErr & X86_TRAP_PF_RW)
        {
            /*
             * Check if it is a read-only page.
             */
            if (PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_ALLOCATED)
            {
                Log(("PGM #PF: Make writable: %RGp %R[pgmpage] pvFault=%RGp uErr=%#x\n", GCPhys, pPage, pvFault, uErr));
#   ifndef VBOX_WITH_NEW_LAZY_PAGE_ALLOC
                Assert(!PGM_PAGE_IS_ZERO(pPage));
#   endif
                AssertFatalMsg(!PGM_PAGE_IS_BALLOONED(pPage), ("Unexpected ballooned page at %RGp\n", GCPhys));
#  ifdef PGM_WITH_PAGE_ZEROING_DETECTION
                if (   PGM_PAGE_GET_STATE(pPage) == PGM_PAGE_STATE_ZERO
                    && (pvFault & X86_PAGE_OFFSET_MASK) == 0
                    && pgmHandlePageZeroingCode(pVCpu, pCtx))
                {
                    STAM_STATS({ pVCpu->pgmr0.s.pStatTrap0eAttributionR0 = &pVCpu->pgm.s.Stats.StatRZTrap0eTime2PageZeroing; });
                    return VINF_SUCCESS;
                }
#  endif
                STAM_STATS({ pVCpu->pgmr0.s.pStatTrap0eAttributionR0 = &pVCpu->pgm.s.Stats.StatRZTrap0eTime2MakeWritable; });

                rc = pgmPhysPageMakeWritable(pVM, pPage, GCPhys);
                if (rc != VINF_SUCCESS)
                {
                    AssertMsg(rc == VINF_PGM_SYNC_CR3 || RT_FAILURE(rc), ("%Rrc\n", rc));
                    return rc;
                }
                if (RT_UNLIKELY(VM_FF_IS_SET(pVM, VM_FF_PGM_NO_MEMORY)))
                    return VINF_EM_NO_MEMORY;
            }

#   if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
            /*
             * Check to see if we need to emulate the instruction if CR0.WP=0.
             */
            if (    !(Walk.fEffective & PGM_PTATTRS_W_MASK)
                &&  (CPUMGetGuestCR0(pVCpu) & (X86_CR0_WP | X86_CR0_PG)) == X86_CR0_PG
                &&  CPUMGetGuestCPL(pVCpu) < 3)
            {
                Assert((uErr & (X86_TRAP_PF_RW | X86_TRAP_PF_P)) == (X86_TRAP_PF_RW | X86_TRAP_PF_P));

                /*
                 * The Netware WP0+RO+US hack.
                 *
                 * Netware sometimes(/always?) runs with WP0.  It has been observed doing
                 * excessive write accesses to pages which are mapped with US=1 and RW=0
                 * while WP=0.  This causes a lot of exits and extremely slow execution.
                 * To avoid trapping and emulating every write here, we change the shadow
                 * page table entry to map it as US=0 and RW=1 until user mode tries to
                 * access it again (see further below).  We count these shadow page table
                 * changes so we can avoid having to clear the page pool every time the WP
                 * bit changes to 1 (see PGMCr0WpEnabled()).
                 */
#    if (PGM_GST_TYPE == PGM_TYPE_32BIT || PGM_GST_TYPE == PGM_TYPE_PAE) && 1
                if (   (Walk.fEffective & (PGM_PTATTRS_W_MASK | PGM_PTATTRS_US_MASK)) == PGM_PTATTRS_US_MASK
                    && (Walk.fBigPage || (GstWalk.Pde.u & X86_PDE_RW))
                    && pVM->cCpus == 1 /* Sorry, no go on SMP. Add CFGM option? */)
                {
                    Log(("PGM #PF: Netware WP0+RO+US hack: pvFault=%RGp uErr=%#x (big=%d)\n", pvFault, uErr, Walk.fBigPage));
                    rc = pgmShwMakePageSupervisorAndWritable(pVCpu, pvFault, Walk.fBigPage, PGM_MK_PG_IS_WRITE_FAULT);
                    if (rc == VINF_SUCCESS || rc == VINF_PGM_SYNC_CR3)
                    {
                        PGM_INVL_PG(pVCpu, pvFault);
                        pVCpu->pgm.s.cNetwareWp0Hacks++;
                        STAM_STATS({ pVCpu->pgmr0.s.pStatTrap0eAttributionR0 = &pVCpu->pgm.s.Stats.StatRZTrap0eTime2Wp0RoUsHack; });
                        return rc;
                    }
                    AssertMsg(RT_FAILURE_NP(rc), ("%Rrc\n", rc));
                    Log(("pgmShwMakePageSupervisorAndWritable(%RGv) failed with rc=%Rrc - ignored\n", pvFault, rc));
                }
#    endif

                /* Interpret the access. */
                rc = VBOXSTRICTRC_TODO(PGMInterpretInstruction(pVCpu, pvFault));
                Log(("PGM #PF: WP0 emulation (pvFault=%RGp uErr=%#x cpl=%d fBig=%d fEffUs=%d)\n", pvFault, uErr, CPUMGetGuestCPL(pVCpu), Walk.fBigPage, !!(Walk.fEffective & PGM_PTATTRS_US_MASK)));
                if (RT_SUCCESS(rc))
                    STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eWPEmulInRZ);
                else
                    STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eWPEmulToR3);
                STAM_STATS({ pVCpu->pgmr0.s.pStatTrap0eAttributionR0 = &pVCpu->pgm.s.Stats.StatRZTrap0eTime2WPEmulation; });
                return rc;
            }
#   endif
            /// @todo count the above case; else
            if (uErr & X86_TRAP_PF_US)
                STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,PageOutOfSyncUserWrite));
            else /* supervisor */
                STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,PageOutOfSyncSupervisorWrite));

            /*
             * Sync the page.
             *
             * Note: Do NOT use PGM_SYNC_NR_PAGES here. That only works if the
             *       page is not present, which is not true in this case.
             */
#   if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
            rc = PGM_BTH_NAME(SyncPage)(pVCpu, GstWalk.Pde, pvFault, 1, uErr);
#   else
            rc = PGM_BTH_NAME(SyncPage)(pVCpu, PdeSrcDummy, pvFault, 1, uErr);
#   endif
            if (RT_SUCCESS(rc))
            {
               /*
                * Page was successfully synced, return to guest but invalidate
                * the TLB first as the page is very likely to be in it.
                */
#   if PGM_SHW_TYPE == PGM_TYPE_EPT
                HMInvalidatePhysPage(pVM, (RTGCPHYS)pvFault);
#   else
                PGM_INVL_PG(pVCpu, pvFault);
#   endif
#   ifdef VBOX_STRICT
                PGMPTWALK GstPageWalk;
                GstPageWalk.GCPhys = RTGCPHYS_MAX;
                if (!pVM->pgm.s.fNestedPaging)
                {
                    rc = PGMGstGetPage(pVCpu, pvFault, &GstPageWalk);
                    AssertMsg(RT_SUCCESS(rc) && ((GstPageWalk.fEffective & X86_PTE_RW) || ((CPUMGetGuestCR0(pVCpu) & (X86_CR0_WP | X86_CR0_PG)) == X86_CR0_PG && CPUMGetGuestCPL(pVCpu) < 3)), ("rc=%Rrc fPageGst=%RX64\n", rc, GstPageWalk.fEffective));
                    LogFlow(("Obsolete physical monitor page out of sync %RGv - phys %RGp flags=%08llx\n", pvFault, GstPageWalk.GCPhys, GstPageWalk.fEffective));
                }
#    if 0 /* Bogus! Triggers incorrectly with w7-64 and later for the SyncPage case: "Pde at %RGv changed behind our back?" */
                uint64_t fPageShw = 0;
                rc = PGMShwGetPage(pVCpu, pvFault, &fPageShw, NULL);
                AssertMsg((RT_SUCCESS(rc) && (fPageShw & X86_PTE_RW)) || pVM->cCpus > 1 /* new monitor can be installed/page table flushed between the trap exit and PGMTrap0eHandler */,
                          ("rc=%Rrc fPageShw=%RX64 GCPhys2=%RGp fPageGst=%RX64 pvFault=%RGv\n", rc, fPageShw, GstPageWalk.GCPhys, fPageGst, pvFault));
#    endif
#   endif /* VBOX_STRICT */
                STAM_STATS({ pVCpu->pgmr0.s.pStatTrap0eAttributionR0 = &pVCpu->pgm.s.Stats.StatRZTrap0eTime2OutOfSyncHndObs; });
                return VINF_SUCCESS;
            }
        }
#    if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
        /*
         * Check for Netware WP0+RO+US hack from above and undo it when user
         * mode accesses the page again.
         */
        else if (   (Walk.fEffective & (PGM_PTATTRS_W_MASK | PGM_PTATTRS_US_MASK)) == PGM_PTATTRS_US_MASK
                 && (Walk.fBigPage || (GstWalk.Pde.u & X86_PDE_RW))
                 &&  pVCpu->pgm.s.cNetwareWp0Hacks > 0
                 &&  (CPUMGetGuestCR0(pVCpu) & (X86_CR0_WP | X86_CR0_PG)) == X86_CR0_PG
                 &&  CPUMGetGuestCPL(pVCpu) == 3
                 &&  pVM->cCpus == 1
                )
        {
            Log(("PGM #PF: Undo netware WP0+RO+US hack: pvFault=%RGp uErr=%#x\n", pvFault, uErr));
            rc = PGM_BTH_NAME(SyncPage)(pVCpu, GstWalk.Pde, pvFault, 1, uErr);
            if (RT_SUCCESS(rc))
            {
                PGM_INVL_PG(pVCpu, pvFault);
                pVCpu->pgm.s.cNetwareWp0Hacks--;
                STAM_STATS({ pVCpu->pgmr0.s.pStatTrap0eAttributionR0 = &pVCpu->pgm.s.Stats.StatRZTrap0eTime2Wp0RoUsUnhack; });
                return VINF_SUCCESS;
            }
        }
#    endif /* PGM_WITH_PAGING */

        /** @todo else: why are we here? */

#   if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE) && defined(VBOX_STRICT)
        /*
         * Check for VMM page flags vs. Guest page flags consistency.
         * Currently only for debug purposes.
         */
        if (RT_SUCCESS(rc))
        {
            /* Get guest page flags. */
            PGMPTWALK GstPageWalk;
            int rc2 = PGMGstGetPage(pVCpu, pvFault, &GstPageWalk);
            if (RT_SUCCESS(rc2))
            {
                uint64_t fPageShw = 0;
                rc2 = PGMShwGetPage(pVCpu, pvFault, &fPageShw, NULL);

#if 0
                /*
                 * Compare page flags.
                 * Note: we have AVL, A, D bits desynced.
                 */
                AssertMsg(      (fPageShw & ~(X86_PTE_A | X86_PTE_D | X86_PTE_AVL_MASK))
                             == (fPageGst & ~(X86_PTE_A | X86_PTE_D | X86_PTE_AVL_MASK))
                          || (   pVCpu->pgm.s.cNetwareWp0Hacks > 0
                              &&    (fPageShw & ~(X86_PTE_A | X86_PTE_D | X86_PTE_AVL_MASK | X86_PTE_RW | X86_PTE_US))
                                 == (fPageGst & ~(X86_PTE_A | X86_PTE_D | X86_PTE_AVL_MASK | X86_PTE_RW | X86_PTE_US))
                              && (fPageShw & (X86_PTE_RW | X86_PTE_US)) == X86_PTE_RW
                              && (fPageGst & (X86_PTE_RW | X86_PTE_US)) == X86_PTE_US),
                          ("Page flags mismatch! pvFault=%RGv uErr=%x GCPhys=%RGp fPageShw=%RX64 fPageGst=%RX64 rc=%d\n",
                           pvFault, (uint32_t)uErr, GCPhys, fPageShw, fPageGst, rc));
01:01:15.623511 00:08:43.266063 Expression: (fPageShw & ~(X86_PTE_A | X86_PTE_D | X86_PTE_AVL_MASK)) == (fPageGst & ~(X86_PTE_A | X86_PTE_D | X86_PTE_AVL_MASK)) || ( pVCpu->pgm.s.cNetwareWp0Hacks > 0 && (fPageShw & ~(X86_PTE_A | X86_PTE_D | X86_PTE_AVL_MASK | X86_PTE_RW | X86_PTE_US)) == (fPageGst & ~(X86_PTE_A | X86_PTE_D | X86_PTE_AVL_MASK | X86_PTE_RW | X86_PTE_US)) && (fPageShw & (X86_PTE_RW | X86_PTE_US)) == X86_PTE_RW && (fPageGst & (X86_PTE_RW | X86_PTE_US)) == X86_PTE_US)
01:01:15.623511 00:08:43.266064 Location  : e:\vbox\svn\trunk\srcPage flags mismatch! pvFault=fffff801b0d7b000 uErr=11 GCPhys=0000000019b52000 fPageShw=0 fPageGst=77b0000000000121 rc=0

01:01:15.625516 00:08:43.268051 Expression: (fPageShw & ~(X86_PTE_A | X86_PTE_D | X86_PTE_AVL_MASK)) == (fPageGst & ~(X86_PTE_A | X86_PTE_D | X86_PTE_AVL_MASK)) || ( pVCpu->pgm.s.cNetwareWp0Hacks > 0 && (fPageShw & ~(X86_PTE_A | X86_PTE_D | X86_PTE_AVL_MASK | X86_PTE_RW | X86_PTE_US)) == (fPageGst & ~(X86_PTE_A | X86_PTE_D | X86_PTE_AVL_MASK | X86_PTE_RW | X86_PTE_US)) && (fPageShw & (X86_PTE_RW | X86_PTE_US)) == X86_PTE_RW && (fPageGst & (X86_PTE_RW | X86_PTE_US)) == X86_PTE_US)
01:01:15.625516 00:08:43.268051 Location  :
e:\vbox\svn\trunk\srcPage flags mismatch!
pvFault=fffff801b0d7b000
                uErr=11     X86_TRAP_PF_ID | X86_TRAP_PF_P
GCPhys=0000000019b52000
fPageShw=0
fPageGst=77b0000000000121
rc=0
#endif

            }
            else
                AssertMsgFailed(("PGMGstGetPage rc=%Rrc\n", rc));
        }
        else
            AssertMsgFailed(("PGMGCGetPage rc=%Rrc\n", rc));
#   endif /* PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE) && VBOX_STRICT */
    }


    /*
     * If we get here it is because something failed above, i.e. most like guru
     * meditiation time.
     */
    LogRel(("%s: returns rc=%Rrc pvFault=%RGv uErr=%RX64 cs:rip=%04x:%08RX64\n",
            __PRETTY_FUNCTION__, rc, pvFault, (uint64_t)uErr, pCtx->cs.Sel, pCtx->rip));
    return rc;

# else  /* Nested paging, EPT except PGM_GST_TYPE = PROT, NONE.   */
    NOREF(uErr); NOREF(pCtx); NOREF(pvFault);
    AssertReleaseMsgFailed(("Shw=%d Gst=%d is not implemented!\n", PGM_SHW_TYPE, PGM_GST_TYPE));
    return VERR_PGM_NOT_USED_IN_MODE;
# endif
}


# if defined(VBOX_WITH_NESTED_HWVIRT_VMX_EPT)
/**
 * Deals with a nested-guest \#PF fault for a guest-physical page with a handler.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   uErr                The error code.
 * @param   pCtx                Pointer to the register context for the CPU.
 * @param   GCPhysNestedFault   The nested-guest physical address of the fault.
 * @param   pPage               The guest page at @a GCPhysNestedFault.
 * @param   GCPhysFault         The guest-physical address of the fault.
 * @param   pGstWalkAll         The guest page walk result.
 * @param   pfLockTaken         Where to store whether the PGM is still held when
 *                              this function completes.
 *
 * @note    The caller has taken the PGM lock.
 */
static VBOXSTRICTRC PGM_BTH_NAME(NestedTrap0eHandlerDoAccessHandlers)(PVMCPUCC pVCpu, RTGCUINT uErr, PCPUMCTX pCtx,
                                                                      RTGCPHYS GCPhysNestedFault, PPGMPAGE pPage,
                                                                      RTGCPHYS GCPhysFault, PPGMPTWALKGST pGstWalkAll,
                                                                      bool *pfLockTaken)
{
#  if PGM_GST_TYPE == PGM_TYPE_PROT \
   && PGM_SHW_TYPE == PGM_TYPE_EPT

    /** @todo Assert uErr isn't X86_TRAP_PF_RSVD and remove release checks. */
    PGM_A20_ASSERT_MASKED(pVCpu, GCPhysFault);
    AssertMsgReturn(PGM_PAGE_HAS_ANY_PHYSICAL_HANDLERS(pPage), ("%RGp %RGp uErr=%u\n", GCPhysNestedFault, GCPhysFault, uErr),
                    VERR_PGM_HANDLER_IPE_1);

    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    RTGCPHYS const GCPhysNestedPage = GCPhysNestedFault & ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK;
    RTGCPHYS const GCPhysPage       = GCPhysFault & ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK;

    /*
     * Physical page access handler.
     */
    PPGMPHYSHANDLER pCur;
    VBOXSTRICTRC rcStrict = pgmHandlerPhysicalLookup(pVM, GCPhysPage, &pCur);
    AssertRCReturn(VBOXSTRICTRC_VAL(rcStrict), rcStrict);

    PCPGMPHYSHANDLERTYPEINT const pCurType = PGMPHYSHANDLER_GET_TYPE(pVM, pCur);
    Assert(pCurType);

    /*
     * If the region is write protected and we got a page not present fault, then sync
     * the pages. If the fault was caused by a read, then restart the instruction.
     * In case of write access continue to the GC write handler.
     */
    if (   !(uErr & X86_TRAP_PF_P)
        &&  pCurType->enmKind == PGMPHYSHANDLERKIND_WRITE)
    {
        Log7Func(("Syncing Monitored: GCPhysNestedPage=%RGp GCPhysPage=%RGp uErr=%#x\n", GCPhysNestedPage, GCPhysPage, uErr));
        rcStrict = PGM_BTH_NAME(NestedSyncPage)(pVCpu, GCPhysNestedPage, GCPhysPage, 1 /*cPages*/, uErr, pGstWalkAll);
        Assert(rcStrict != VINF_PGM_SYNCPAGE_MODIFIED_PDE);
        if (    RT_FAILURE(rcStrict)
            || !(uErr & X86_TRAP_PF_RW))
        {
            AssertMsgRC(rcStrict, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
            STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eHandlersOutOfSync);
            STAM_STATS({ pVCpu->pgmr0.s.pStatTrap0eAttributionR0 = &pVCpu->pgm.s.Stats.StatRZTrap0eTime2OutOfSyncHndPhys; });
            return rcStrict;
        }
    }
    else if (   !(uErr & X86_TRAP_PF_RSVD)
             && pCurType->enmKind != PGMPHYSHANDLERKIND_WRITE)
    {
        /*
         * If the access was NOT through an EPT misconfig (i.e. RSVD), sync the page.
         * This can happen for the VMX APIC-access page.
         */
        Log7Func(("Syncing MMIO: GCPhysNestedPage=%RGp GCPhysPage=%RGp\n", GCPhysNestedPage, GCPhysPage));
        rcStrict = PGM_BTH_NAME(NestedSyncPage)(pVCpu, GCPhysNestedPage, GCPhysPage, 1 /*cPages*/, uErr, pGstWalkAll);
        Assert(rcStrict != VINF_PGM_SYNCPAGE_MODIFIED_PDE);
        if (RT_FAILURE(rcStrict))
        {
            AssertMsgRC(rcStrict, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
            STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eHandlersOutOfSync);
            STAM_STATS({ pVCpu->pgmr0.s.pStatTrap0eAttributionR0 = &pVCpu->pgm.s.Stats.StatRZTrap0eTime2OutOfSyncHndPhys; });
            return rcStrict;
        }
    }

    AssertMsg(   pCurType->enmKind != PGMPHYSHANDLERKIND_WRITE
              || (pCurType->enmKind == PGMPHYSHANDLERKIND_WRITE && (uErr & X86_TRAP_PF_RW)),
              ("Unexpected trap for physical handler: %08X (phys=%08x) pPage=%R[pgmpage] uErr=%X, enmKind=%d\n",
               GCPhysNestedFault, GCPhysFault, pPage, uErr, pCurType->enmKind));
    if (pCurType->enmKind == PGMPHYSHANDLERKIND_WRITE)
        STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eHandlersPhysWrite);
    else
    {
        STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eHandlersPhysAll);
        if (uErr & X86_TRAP_PF_RSVD)
            STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eHandlersPhysAllOpt);
    }

    if (pCurType->pfnPfHandler)
    {
        STAM_PROFILE_START(&pCur->Stat, h);
        uint64_t const uUser = !pCurType->fRing0DevInsIdx ? pCur->uUser
                             : (uintptr_t)PDMDeviceRing0IdxToInstance(pVM, pCur->uUser);

        if (pCurType->fKeepPgmLock)
        {
            rcStrict = pCurType->pfnPfHandler(pVM, pVCpu, uErr, pCtx, GCPhysNestedFault, GCPhysFault, uUser);
            STAM_PROFILE_STOP(&pCur->Stat, h);
        }
        else
        {
            PGM_UNLOCK(pVM);
            *pfLockTaken = false;
            rcStrict = pCurType->pfnPfHandler(pVM, pVCpu, uErr, pCtx, GCPhysNestedFault, GCPhysFault, uUser);
            STAM_PROFILE_STOP(&pCur->Stat, h); /* no locking needed, entry is unlikely reused before we get here. */
        }
    }
    else
    {
        AssertMsgFailed(("What's going on here!? Fault falls outside handler range!?\n"));
        rcStrict = VINF_EM_RAW_EMULATE_INSTR;
    }

    STAM_STATS({ pVCpu->pgmr0.s.pStatTrap0eAttributionR0 = &pVCpu->pgm.s.Stats.StatRZTrap0eTime2HndPhys; });
    return rcStrict;

#  else
    RT_NOREF8(pVCpu, uErr, pCtx, GCPhysNestedFault, pPage, GCPhysFault, pGstWalkAll, pfLockTaken);
    AssertReleaseMsgFailed(("Shw=%d Gst=%d is not implemented!\n", PGM_SHW_TYPE, PGM_GST_TYPE));
    return VERR_PGM_NOT_USED_IN_MODE;
#  endif
}
# endif /* VBOX_WITH_NESTED_HWVIRT_VMX_EPT */


/**
 * Nested \#PF handler for nested-guest hardware-assisted execution using nested
 * paging.
 *
 * @returns VBox status code (appropriate for trap handling and GC return).
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   uErr                The fault error (X86_TRAP_PF_*).
 * @param   pCtx                Pointer to the register context for the CPU.
 * @param   GCPhysNestedFault   The nested-guest physical address of the fault.
 * @param   fIsLinearAddrValid  Whether translation of a nested-guest linear address
 *                              caused this fault. If @c false, GCPtrNestedFault
 *                              must be 0.
 * @param   GCPtrNestedFault    The nested-guest linear address of this fault.
 * @param   pWalk               The guest page table walk result.
 * @param   pfLockTaken         Where to store whether the PGM lock is still held
 *                              when this function completes.
 */
PGM_BTH_DECL(int, NestedTrap0eHandler)(PVMCPUCC pVCpu, RTGCUINT uErr, PCPUMCTX pCtx, RTGCPHYS GCPhysNestedFault,
                                       bool fIsLinearAddrValid, RTGCPTR GCPtrNestedFault, PPGMPTWALK pWalk, bool *pfLockTaken)
{
    *pfLockTaken = false;
# if defined(VBOX_WITH_NESTED_HWVIRT_VMX_EPT) \
    && PGM_GST_TYPE == PGM_TYPE_PROT \
    && PGM_SHW_TYPE == PGM_TYPE_EPT

    Assert(CPUMIsGuestVmxEptPagingEnabled(pVCpu));
    Assert(PGM_A20_IS_ENABLED(pVCpu));

    /* We don't support mode-based execute control for EPT yet. */
    Assert(!pVCpu->CTX_SUFF(pVM)->cpum.ro.GuestFeatures.fVmxModeBasedExecuteEpt);
    Assert(!(uErr & X86_TRAP_PF_US));

    /* Take the big lock now. */
    *pfLockTaken = true;
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    PGM_LOCK_VOID(pVM);

    /*
     * Walk the guest EPT tables and check if it's an EPT violation or misconfiguration.
     */
    if (fIsLinearAddrValid)
        Log7Func(("cs:rip=%04x:%#08RX64 GCPhysNestedFault=%RGp uErr=%#x GCPtrNestedFault=%RGv\n",
                  pCtx->cs.Sel, pCtx->rip, GCPhysNestedFault, uErr, GCPtrNestedFault));
    else
        Log7Func(("cs:rip=%04x:%#08RX64 GCPhysNestedFault=%RGp uErr=%#x\n",
                  pCtx->cs.Sel, pCtx->rip, GCPhysNestedFault, uErr));
    PGMPTWALKGST GstWalkAll;
    int rc = pgmGstSlatWalk(pVCpu, GCPhysNestedFault, fIsLinearAddrValid, GCPtrNestedFault, pWalk, &GstWalkAll);
    if (RT_FAILURE(rc))
        return rc;

    Assert(GstWalkAll.enmType == PGMPTWALKGSTTYPE_EPT);
    Assert(pWalk->fSucceeded);
    Assert(pWalk->fEffective & (PGM_PTATTRS_EPT_R_MASK | PGM_PTATTRS_EPT_W_MASK | PGM_PTATTRS_EPT_X_SUPER_MASK));
    Assert(pWalk->fIsSlat);

#  ifdef DEBUG_ramshankar
    /* Paranoia. */
    Assert(RT_BOOL(pWalk->fEffective & PGM_PTATTRS_R_MASK)  ==  RT_BOOL(pWalk->fEffective & PGM_PTATTRS_EPT_R_MASK));
    Assert(RT_BOOL(pWalk->fEffective & PGM_PTATTRS_W_MASK)  ==  RT_BOOL(pWalk->fEffective & PGM_PTATTRS_EPT_W_MASK));
    Assert(RT_BOOL(pWalk->fEffective & PGM_PTATTRS_NX_MASK) == !RT_BOOL(pWalk->fEffective & PGM_PTATTRS_EPT_X_SUPER_MASK));
#  endif

    Log7Func(("SLAT: GCPhysNestedFault=%RGp -> GCPhys=%#RGp\n", GCPhysNestedFault, pWalk->GCPhys));

    /*
     * Check page-access permissions.
     */
    if (   ((uErr & X86_TRAP_PF_RW) && !(pWalk->fEffective & PGM_PTATTRS_W_MASK))
        || ((uErr & X86_TRAP_PF_ID) &&  (pWalk->fEffective & PGM_PTATTRS_NX_MASK)))
    {
        Log7Func(("Permission failed! GCPtrNested=%RGv GCPhysNested=%RGp uErr=%#x fEffective=%#RX64\n", GCPtrNestedFault,
                  GCPhysNestedFault, uErr, pWalk->fEffective));
        pWalk->fFailed = PGM_WALKFAIL_EPT_VIOLATION;
        return VERR_ACCESS_DENIED;
    }

    PGM_A20_ASSERT_MASKED(pVCpu, pWalk->GCPhys);
    RTGCPHYS const GCPhysPage       = pWalk->GCPhys & ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK;
    RTGCPHYS const GCPhysNestedPage = GCPhysNestedFault & ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK;

    /*
     * If we were called via an EPT misconfig, it should've already resulted in a nested-guest VM-exit.
     */
    AssertMsgReturn(!(uErr & X86_TRAP_PF_RSVD),
                    ("Unexpected EPT misconfig VM-exit. GCPhysPage=%RGp GCPhysNestedPage=%RGp\n", GCPhysPage, GCPhysNestedPage),
                    VERR_PGM_MAPPING_IPE);

    /*
     * Fetch and sync the nested-guest EPT page directory pointer.
     */
    PEPTPD pEptPd;
    rc = pgmShwGetNestedEPTPDPtr(pVCpu, GCPhysNestedPage, NULL /*ppPdpt*/, &pEptPd, &GstWalkAll);
    AssertRCReturn(rc, rc);
    Assert(pEptPd);

    /*
     * A common case is the not-present error caused by lazy page table syncing.
     *
     * It is IMPORTANT that we weed out any access to non-present shadow PDEs
     * here so we can safely assume that the shadow PT is present when calling
     * NestedSyncPage later.
     *
     * NOTE: It's possible we will be syncing the VMX APIC-access page here.
     * In that case, we would sync the page but will NOT go ahead with emulating
     * the APIC-access VM-exit through IEM. However, once the page is mapped in
     * the shadow tables, subsequent APIC-access VM-exits for the nested-guest
     * will be triggered by hardware. Maybe calling the IEM #PF handler can be
     * considered as an optimization later.
     */
    unsigned const iPde = (GCPhysNestedPage >> SHW_PD_SHIFT) & SHW_PD_MASK;
    if (   !(uErr & X86_TRAP_PF_P)
        && !(pEptPd->a[iPde].u & EPT_PRESENT_MASK))
    {
        STAM_STATS({ pVCpu->pgmr0.s.pStatTrap0eAttributionR0 = &pVCpu->pgm.s.Stats.StatRZTrap0eTime2SyncPT; });
        Log7Func(("NestedSyncPT: Lazy. GCPhysNestedPage=%RGp GCPhysPage=%RGp\n", GCPhysNestedPage, GCPhysPage));
        rc = PGM_BTH_NAME(NestedSyncPT)(pVCpu, GCPhysNestedPage, GCPhysPage, &GstWalkAll);
        if (RT_SUCCESS(rc))
            return rc;
        AssertMsgFailedReturn(("NestedSyncPT: %RGv failed! rc=%Rrc\n", GCPhysNestedPage, rc), VERR_PGM_MAPPING_IPE);
    }

    /*
     * Check if this fault address is flagged for special treatment.
     * This handles faults on an MMIO or write-monitored page.
     *
     * If this happens to be the VMX APIC-access page, we don't treat is as MMIO
     * but rather sync it further below (as a regular guest page) which lets
     * hardware-assisted execution trigger the APIC-access VM-exits of the
     * nested-guest directly.
     */
    PPGMPAGE pPage;
    rc = pgmPhysGetPageEx(pVM, GCPhysPage, &pPage);
    if (RT_FAILURE(rc))
    {
        /*
         * We failed to get the physical page which means it's a reserved/invalid
         * page address (not MMIO even). This can typically be observed with
         * Microsoft Hyper-V enabled Windows guests. We must fall back to emulating
         * the instruction, see @bugref{10318#c7}.
         */
        STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eHandlersInvalid);
        STAM_STATS({ pVCpu->pgmr0.s.pStatTrap0eAttributionR0 = &pVCpu->pgm.s.Stats.StatRZTrap0eTime2InvalidPhys; });
        return VINF_EM_RAW_EMULATE_INSTR;
    }
    /* Check if this is an MMIO page and NOT the VMX APIC-access page. */
    if (PGM_PAGE_HAS_ACTIVE_HANDLERS(pPage) && !PGM_PAGE_IS_HNDL_PHYS_NOT_IN_HM(pPage))
    {
        Log7Func(("MMIO: Calling NestedTrap0eHandlerDoAccessHandlers for GCPhys %RGp\n", GCPhysPage));
        return VBOXSTRICTRC_TODO(PGM_BTH_NAME(NestedTrap0eHandlerDoAccessHandlers)(pVCpu, uErr, pCtx, GCPhysNestedFault,
                                                                                   pPage, pWalk->GCPhys, &GstWalkAll,
                                                                                   pfLockTaken));
    }

    /*
     * We are here only if page is present in nested-guest page tables but the
     * trap is not handled by our handlers. Check for page out-of-sync situation.
     */
    if (!(uErr & X86_TRAP_PF_P))
    {
        Assert(!PGM_PAGE_IS_BALLOONED(pPage));
        Assert(!(uErr & X86_TRAP_PF_US));   /* Mode-based execute not supported yet. */
        STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,PageOutOfSyncSupervisor));

        Log7Func(("SyncPage: Not-Present: GCPhysNestedPage=%RGp GCPhysPage=%RGp\n", GCPhysNestedFault, GCPhysPage));
        rc = PGM_BTH_NAME(NestedSyncPage)(pVCpu, GCPhysNestedPage, GCPhysPage, PGM_SYNC_NR_PAGES, uErr, &GstWalkAll);
        if (RT_SUCCESS(rc))
        {
            STAM_STATS({ pVCpu->pgmr0.s.pStatTrap0eAttributionR0 = &pVCpu->pgm.s.Stats.StatRZTrap0eTime2OutOfSync; });
            return VINF_SUCCESS;
        }
    }
    else if (uErr & X86_TRAP_PF_RW)
    {
        /*
         * Write protected pages are made writable when the guest makes the
         * first write to it. This happens for pages that are shared, write
         * monitored or not yet allocated.
         *
         * We may also end up here when CR0.WP=0 in the guest.
         *
         * Also, a side effect of not flushing global PDEs are out of sync
         * pages due to physical monitored regions, that are no longer valid.
         * Assume for now it only applies to the read/write flag.
         */
        if (PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_ALLOCATED)
        {
            /* This is a read-only page. */
            AssertFatalMsg(!PGM_PAGE_IS_BALLOONED(pPage), ("Unexpected ballooned page at %RGp\n", GCPhysPage));
#ifdef PGM_WITH_PAGE_ZEROING_DETECTION
            if (   PGM_PAGE_GET_STATE(pPage) == PGM_PAGE_STATE_ZERO
                && (GCPhysNestedFault & X86_PAGE_OFFSET_MASK) == 0
                && pgmHandlePageZeroingCode(pVCpu, pCtx))
            {
                STAM_STATS({ pVCpu->pgmr0.s.pStatTrap0eAttributionR0 = &pVCpu->pgm.s.Stats.StatRZTrap0eTime2PageZeroing; });
                return VINF_SUCCESS;
            }
#endif
            STAM_STATS({ pVCpu->pgmr0.s.pStatTrap0eAttributionR0 = &pVCpu->pgm.s.Stats.StatRZTrap0eTime2MakeWritable; });

            Log7Func(("Calling pgmPhysPageMakeWritable for GCPhysPage=%RGp\n", GCPhysPage));
            rc = pgmPhysPageMakeWritable(pVM, pPage, GCPhysPage);
            if (rc != VINF_SUCCESS)
            {
                AssertMsg(rc == VINF_PGM_SYNC_CR3 || RT_FAILURE(rc), ("%Rrc\n", rc));
                return rc;
            }
            if (RT_UNLIKELY(VM_FF_IS_SET(pVM, VM_FF_PGM_NO_MEMORY)))
                return VINF_EM_NO_MEMORY;
        }

        Assert(!(uErr & X86_TRAP_PF_US));   /* Mode-based execute not supported yet. */
        STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,PageOutOfSyncSupervisorWrite));

        /*
         * Sync the write-protected page.
         * Note: Do NOT use PGM_SYNC_NR_PAGES here. That only works if the
         *       page is not present, which is not true in this case.
         */
        Log7Func(("SyncPage: RW: cs:rip=%04x:%#RX64 GCPhysNestedPage=%RGp uErr=%#RX32 GCPhysPage=%RGp WalkGCPhys=%RGp\n",
                  pCtx->cs.Sel, pCtx->rip, GCPhysNestedPage, (uint32_t)uErr, GCPhysPage, pWalk->GCPhys));
        rc = PGM_BTH_NAME(NestedSyncPage)(pVCpu, GCPhysNestedPage, GCPhysPage, 1 /* cPages */, uErr, &GstWalkAll);
        if (RT_SUCCESS(rc))
        {
            HMInvalidatePhysPage(pVM, GCPhysPage);
            STAM_STATS({ pVCpu->pgmr0.s.pStatTrap0eAttributionR0 = &pVCpu->pgm.s.Stats.StatRZTrap0eTime2OutOfSyncHndObs; });
            return VINF_SUCCESS;
        }
    }

    /*
     * If we get here it is because something failed above => guru meditation time?
     */
    LogRelMaxFunc(32, ("rc=%Rrc GCPhysNestedFault=%#RGp (%#RGp) uErr=%#RX32 cs:rip=%04x:%08RX64\n",
                       rc, GCPhysNestedFault, GCPhysPage, (uint32_t)uErr, pCtx->cs.Sel, pCtx->rip));
    return VERR_PGM_MAPPING_IPE;

# else /* !VBOX_WITH_NESTED_HWVIRT_VMX_EPT || PGM_GST_TYPE != PGM_TYPE_PROT || PGM_SHW_TYPE != PGM_TYPE_EPT */
    RT_NOREF7(pVCpu, uErr, pCtx, GCPhysNestedFault, fIsLinearAddrValid, GCPtrNestedFault, pWalk);
    AssertReleaseMsgFailed(("Shw=%d Gst=%d is not implemented!\n", PGM_SHW_TYPE, PGM_GST_TYPE));
    return VERR_PGM_NOT_USED_IN_MODE;
# endif
}

#endif /* !IN_RING3 */


/**
 * Emulation of the invlpg instruction.
 *
 *
 * @returns VBox status code.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtrPage   Page to invalidate.
 *
 * @remark  ASSUMES that the guest is updating before invalidating. This order
 *          isn't required by the CPU, so this is speculative and could cause
 *          trouble.
 * @remark  No TLB shootdown is done on any other VCPU as we assume that
 *          invlpg emulation is the *only* reason for calling this function.
 *          (The guest has to shoot down TLB entries on other CPUs itself)
 *          Currently true, but keep in mind!
 *
 * @todo    Clean this up! Most of it is (or should be) no longer necessary as we catch all page table accesses.
 *          Should only be required when PGMPOOL_WITH_OPTIMIZED_DIRTY_PT is active (PAE or AMD64 (for now))
 */
PGM_BTH_DECL(int, InvalidatePage)(PVMCPUCC pVCpu, RTGCPTR GCPtrPage)
{
#if    PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE) \
    && !PGM_TYPE_IS_NESTED_OR_EPT(PGM_SHW_TYPE) \
    && PGM_SHW_TYPE != PGM_TYPE_NONE
    int rc;
    PVMCC    pVM   = pVCpu->CTX_SUFF(pVM);
    PPGMPOOL pPool = pVM->pgm.s.CTX_SUFF(pPool);

    PGM_LOCK_ASSERT_OWNER(pVM);

    LogFlow(("InvalidatePage %RGv\n", GCPtrPage));

    /*
     * Get the shadow PD entry and skip out if this PD isn't present.
     * (Guessing that it is frequent for a shadow PDE to not be present, do this first.)
     */
# if PGM_SHW_TYPE == PGM_TYPE_32BIT
    const unsigned  iPDDst    = (uint32_t)GCPtrPage >> SHW_PD_SHIFT;
    PX86PDE         pPdeDst   = pgmShwGet32BitPDEPtr(pVCpu, GCPtrPage);
    AssertReturn(pPdeDst, VERR_INTERNAL_ERROR_3);

    /* Fetch the pgm pool shadow descriptor. */
    PPGMPOOLPAGE    pShwPde = pVCpu->pgm.s.CTX_SUFF(pShwPageCR3);
#  ifdef IN_RING3 /* Possible we didn't resync yet when called from REM. */
    if (!pShwPde)
    {
        STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,InvalidatePageSkipped));
        return VINF_SUCCESS;
    }
#  else
    Assert(pShwPde);
#  endif

# elif PGM_SHW_TYPE == PGM_TYPE_PAE
    const unsigned  iPdpt     = (uint32_t)GCPtrPage >> X86_PDPT_SHIFT;
    PX86PDPT        pPdptDst  = pgmShwGetPaePDPTPtr(pVCpu);

    /* If the shadow PDPE isn't present, then skip the invalidate. */
#  ifdef IN_RING3 /* Possible we didn't resync yet when called from REM. */
    if (!pPdptDst || !(pPdptDst->a[iPdpt].u & X86_PDPE_P))
#  else
    if (!(pPdptDst->a[iPdpt].u & X86_PDPE_P))
#  endif
    {
        STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,InvalidatePageSkipped));
        PGM_INVL_PG(pVCpu, GCPtrPage);
        return VINF_SUCCESS;
    }

    /* Fetch the pgm pool shadow descriptor. */
    PPGMPOOLPAGE pShwPde = pgmPoolGetPage(pPool, pPdptDst->a[iPdpt].u & X86_PDPE_PG_MASK);
    AssertReturn(pShwPde, VERR_PGM_POOL_GET_PAGE_FAILED);

    PX86PDPAE       pPDDst  = (PX86PDPAE)PGMPOOL_PAGE_2_PTR_V2(pVM, pVCpu, pShwPde);
    const unsigned  iPDDst  = (GCPtrPage >> SHW_PD_SHIFT) & SHW_PD_MASK;
    PX86PDEPAE      pPdeDst = &pPDDst->a[iPDDst];

# else /* PGM_SHW_TYPE == PGM_TYPE_AMD64 */
    /* PML4 */
    /*const unsigned  iPml4     = (GCPtrPage >> X86_PML4_SHIFT) & X86_PML4_MASK;*/
    const unsigned  iPdpt     = (GCPtrPage >> X86_PDPT_SHIFT) & X86_PDPT_MASK_AMD64;
    const unsigned  iPDDst    = (GCPtrPage >> SHW_PD_SHIFT) & SHW_PD_MASK;
    PX86PDPAE       pPDDst;
    PX86PDPT        pPdptDst;
    PX86PML4E       pPml4eDst;
    rc = pgmShwGetLongModePDPtr(pVCpu, GCPtrPage, &pPml4eDst, &pPdptDst, &pPDDst);
    if (rc != VINF_SUCCESS)
    {
        AssertMsg(rc == VERR_PAGE_DIRECTORY_PTR_NOT_PRESENT || rc == VERR_PAGE_MAP_LEVEL4_NOT_PRESENT, ("Unexpected rc=%Rrc\n", rc));
        STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,InvalidatePageSkipped));
        PGM_INVL_PG(pVCpu, GCPtrPage);
        return VINF_SUCCESS;
    }
    PX86PDEPAE  pPdeDst  = &pPDDst->a[iPDDst];
    Assert(pPDDst);
    Assert(pPdptDst->a[iPdpt].u & X86_PDPE_P);

    /* Fetch the pgm pool shadow descriptor. */
    PPGMPOOLPAGE pShwPde = pgmPoolGetPage(pPool, pPdptDst->a[iPdpt].u & SHW_PDPE_PG_MASK);
    Assert(pShwPde);

# endif /* PGM_SHW_TYPE == PGM_TYPE_AMD64 */

    const SHWPDE PdeDst = *pPdeDst;
    if (!(PdeDst.u & X86_PDE_P))
    {
        STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,InvalidatePageSkipped));
        PGM_INVL_PG(pVCpu, GCPtrPage);
        return VINF_SUCCESS;
    }

    /*
     * Get the guest PD entry and calc big page.
     */
# if PGM_GST_TYPE == PGM_TYPE_32BIT
    PGSTPD          pPDSrc      = pgmGstGet32bitPDPtr(pVCpu);
    const unsigned  iPDSrc      = (uint32_t)GCPtrPage >> GST_PD_SHIFT;
    GSTPDE          PdeSrc      = pPDSrc->a[iPDSrc];
# else /* PGM_GST_TYPE != PGM_TYPE_32BIT */
    unsigned        iPDSrc = 0;
#  if PGM_GST_TYPE == PGM_TYPE_PAE
    X86PDPE         PdpeSrcIgn;
    PX86PDPAE       pPDSrc      = pgmGstGetPaePDPtr(pVCpu, GCPtrPage, &iPDSrc, &PdpeSrcIgn);
#  else /* AMD64 */
    PX86PML4E       pPml4eSrcIgn;
    X86PDPE         PdpeSrcIgn;
    PX86PDPAE       pPDSrc      = pgmGstGetLongModePDPtr(pVCpu, GCPtrPage, &pPml4eSrcIgn, &PdpeSrcIgn, &iPDSrc);
#  endif
    GSTPDE          PdeSrc;

    if (pPDSrc)
        PdeSrc = pPDSrc->a[iPDSrc];
    else
        PdeSrc.u = 0;
# endif /* PGM_GST_TYPE != PGM_TYPE_32BIT */
    const bool      fWasBigPage = RT_BOOL(PdeDst.u & PGM_PDFLAGS_BIG_PAGE);
    const bool      fIsBigPage  = (PdeSrc.u & X86_PDE_PS) && GST_IS_PSE_ACTIVE(pVCpu);
    if (fWasBigPage != fIsBigPage)
        STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,InvalidatePageSkipped));

# ifdef IN_RING3
    /*
     * If a CR3 Sync is pending we may ignore the invalidate page operation
     * depending on the kind of sync and if it's a global page or not.
     * This doesn't make sense in GC/R0 so we'll skip it entirely there.
     */
#  ifdef PGM_SKIP_GLOBAL_PAGEDIRS_ON_NONGLOBAL_FLUSH
    if (    VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_PGM_SYNC_CR3)
        || (   VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_PGM_SYNC_CR3_NON_GLOBAL)
            && fIsBigPage
            && (PdeSrc.u & X86_PDE4M_G)
           )
       )
#  else
    if (VM_FF_IS_ANY_SET(pVM, VM_FF_PGM_SYNC_CR3 | VM_FF_PGM_SYNC_CR3_NON_GLOBAL) )
#  endif
    {
        STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,InvalidatePageSkipped));
        return VINF_SUCCESS;
    }
# endif /* IN_RING3 */

    /*
     * Deal with the Guest PDE.
     */
    rc = VINF_SUCCESS;
    if (PdeSrc.u & X86_PDE_P)
    {
        Assert(    (PdeSrc.u & X86_PDE_US) == (PdeDst.u & X86_PDE_US)
               &&  ((PdeSrc.u & X86_PDE_RW) || !(PdeDst.u & X86_PDE_RW) || pVCpu->pgm.s.cNetwareWp0Hacks > 0));
        if (!fIsBigPage)
        {
            /*
             * 4KB - page.
             */
            PPGMPOOLPAGE    pShwPage = pgmPoolGetPage(pPool, PdeDst.u & SHW_PDE_PG_MASK);
            RTGCPHYS        GCPhys   = GST_GET_PDE_GCPHYS(PdeSrc);

# if PGM_SHW_TYPE == PGM_TYPE_PAE && PGM_GST_TYPE == PGM_TYPE_32BIT
            /* Select the right PDE as we're emulating a 4kb page table with 2 shadow page tables. */
            GCPhys = PGM_A20_APPLY(pVCpu, GCPhys | ((iPDDst & 1) * (GUEST_PAGE_SIZE / 2)));
# endif
            if (pShwPage->GCPhys == GCPhys)
            {
                /* Syncing it here isn't 100% safe and it's probably not worth spending time syncing it. */
                PSHWPT pPTDst = (PSHWPT)PGMPOOL_PAGE_2_PTR_V2(pVM, pVCpu, pShwPage);

                PGSTPT pPTSrc;
                rc = PGM_GCPHYS_2_PTR_V2(pVM, pVCpu, GST_GET_PDE_GCPHYS(PdeSrc), &pPTSrc);
                if (RT_SUCCESS(rc))
                {
                    const unsigned iPTSrc = (GCPtrPage >> GST_PT_SHIFT) & GST_PT_MASK;
                    GSTPTE PteSrc = pPTSrc->a[iPTSrc];
                    const unsigned iPTDst = (GCPtrPage >> SHW_PT_SHIFT) & SHW_PT_MASK;
                    PGM_BTH_NAME(SyncPageWorker)(pVCpu, &pPTDst->a[iPTDst], PdeSrc, PteSrc, pShwPage, iPTDst);
                    Log2(("SyncPage: 4K  %RGv PteSrc:{P=%d RW=%d U=%d raw=%08llx} PteDst=%08llx %s\n",
                            GCPtrPage, PteSrc.u & X86_PTE_P,
                            (PteSrc.u & PdeSrc.u & X86_PTE_RW),
                            (PteSrc.u & PdeSrc.u & X86_PTE_US),
                            (uint64_t)PteSrc.u,
                            SHW_PTE_LOG64(pPTDst->a[iPTDst]),
                            SHW_PTE_IS_TRACK_DIRTY(pPTDst->a[iPTDst]) ? " Track-Dirty" : ""));
                }
                STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,InvalidatePage4KBPages));
                PGM_INVL_PG(pVCpu, GCPtrPage);
            }
            else
            {
                /*
                 * The page table address changed.
                 */
                LogFlow(("InvalidatePage: Out-of-sync at %RGp PdeSrc=%RX64 PdeDst=%RX64 ShwGCPhys=%RGp iPDDst=%#x\n",
                         GCPtrPage, (uint64_t)PdeSrc.u, (uint64_t)PdeDst.u, pShwPage->GCPhys, iPDDst));
                pgmPoolFree(pVM, PdeDst.u & SHW_PDE_PG_MASK, pShwPde->idx, iPDDst);
                SHW_PDE_ATOMIC_SET(*pPdeDst, 0);
                STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,InvalidatePagePDOutOfSync));
                PGM_INVL_VCPU_TLBS(pVCpu);
            }
        }
        else
        {
            /*
             * 2/4MB - page.
             */
            /* Before freeing the page, check if anything really changed. */
            PPGMPOOLPAGE    pShwPage = pgmPoolGetPage(pPool, PdeDst.u & SHW_PDE_PG_MASK);
            RTGCPHYS        GCPhys   = GST_GET_BIG_PDE_GCPHYS(pVM, PdeSrc);
# if PGM_SHW_TYPE == PGM_TYPE_PAE && PGM_GST_TYPE == PGM_TYPE_32BIT
            /* Select the right PDE as we're emulating a 4MB page directory with two 2 MB shadow PDEs.*/
            GCPhys = PGM_A20_APPLY(pVCpu, GCPhys | (GCPtrPage & (1 << X86_PD_PAE_SHIFT)));
# endif
            if (    pShwPage->GCPhys == GCPhys
                &&  pShwPage->enmKind == BTH_PGMPOOLKIND_PT_FOR_BIG)
            {
                /* ASSUMES a the given bits are identical for 4M and normal PDEs */
                /** @todo This test is wrong as it cannot check the G bit!
                 *        FIXME */
                if (        (PdeSrc.u & (X86_PDE_P | X86_PDE_RW | X86_PDE_US))
                        ==  (PdeDst.u & (X86_PDE_P | X86_PDE_RW | X86_PDE_US))
                    &&  (   (PdeSrc.u & X86_PDE4M_D) /** @todo rainy day: What about read-only 4M pages? not very common, but still... */
                         || (PdeDst.u & PGM_PDFLAGS_TRACK_DIRTY)))
                {
                    LogFlow(("Skipping flush for big page containing %RGv (PD=%X .u=%RX64)-> nothing has changed!\n", GCPtrPage, iPDSrc, PdeSrc.u));
                    STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,InvalidatePage4MBPagesSkip));
                    return VINF_SUCCESS;
                }
            }

            /*
             * Ok, the page table is present and it's been changed in the guest.
             * If we're in host context, we'll just mark it as not present taking the lazy approach.
             * We could do this for some flushes in GC too, but we need an algorithm for
             * deciding which 4MB pages containing code likely to be executed very soon.
             */
            LogFlow(("InvalidatePage: Out-of-sync PD at %RGp PdeSrc=%RX64 PdeDst=%RX64\n",
                     GCPtrPage, (uint64_t)PdeSrc.u, (uint64_t)PdeDst.u));
            pgmPoolFree(pVM, PdeDst.u & SHW_PDE_PG_MASK, pShwPde->idx, iPDDst);
            SHW_PDE_ATOMIC_SET(*pPdeDst, 0);
            STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,InvalidatePage4MBPages));
            PGM_INVL_BIG_PG(pVCpu, GCPtrPage);
        }
    }
    else
    {
        /*
         * Page directory is not present, mark shadow PDE not present.
         */
        pgmPoolFree(pVM, PdeDst.u & SHW_PDE_PG_MASK, pShwPde->idx, iPDDst);
        SHW_PDE_ATOMIC_SET(*pPdeDst, 0);
        STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,InvalidatePagePDNPs));
        PGM_INVL_PG(pVCpu, GCPtrPage);
    }
    return rc;

#else /* guest real and protected mode, nested + ept, none. */
    /* There's no such thing as InvalidatePage when paging is disabled, so just ignore. */
    NOREF(pVCpu); NOREF(GCPtrPage);
    return VINF_SUCCESS;
#endif
}

#if PGM_SHW_TYPE != PGM_TYPE_NONE

/**
 * Update the tracking of shadowed pages.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pShwPage    The shadow page.
 * @param   HCPhys      The physical page we is being dereferenced.
 * @param   iPte        Shadow PTE index
 * @param   GCPhysPage  Guest physical address (only valid if pShwPage->fDirty is set)
 */
DECLINLINE(void) PGM_BTH_NAME(SyncPageWorkerTrackDeref)(PVMCPUCC pVCpu, PPGMPOOLPAGE pShwPage, RTHCPHYS HCPhys, uint16_t iPte,
                                                        RTGCPHYS GCPhysPage)
{
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);

# if    defined(PGMPOOL_WITH_OPTIMIZED_DIRTY_PT) \
     && PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE) \
     && (PGM_GST_TYPE == PGM_TYPE_PAE || PGM_GST_TYPE == PGM_TYPE_AMD64 || PGM_SHW_TYPE == PGM_TYPE_PAE /* pae/32bit combo */)

    /* Use the hint we retrieved from the cached guest PT. */
    if (pShwPage->fDirty)
    {
        PPGMPOOL pPool = pVM->pgm.s.CTX_SUFF(pPool);

        Assert(pShwPage->cPresent);
        Assert(pPool->cPresent);
        pShwPage->cPresent--;
        pPool->cPresent--;

        PPGMPAGE pPhysPage = pgmPhysGetPage(pVM, GCPhysPage);
        AssertRelease(pPhysPage);
        pgmTrackDerefGCPhys(pPool, pShwPage, pPhysPage, iPte);
        return;
    }
# else
    NOREF(GCPhysPage);
# endif

    /** @todo If this turns out to be a bottle neck (*very* likely) two things can be done:
     *      1. have a medium sized HCPhys -> GCPhys TLB (hash?)
     *      2. write protect all shadowed pages. I.e. implement caching.
     *
     *  2023-08-24 bird: If we allow the ZeroPg to enter the shadow page tables,
     *  this becomes a common occurence and we screw up. A better to the above would
     *  be to have a parallel table that records the guest physical addresses of the
     *  pages mapped by the shadow page table...  For nested page tables,
     *  we can easily correleate a table entry to a page entry, so it won't be
     *  needed for those.
     */
# if  PGM_TYPE_IS_NESTED_OR_EPT(PGM_SHW_TYPE) || !PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
    /*
     * For non-paged guest tables, EPT and nested tables we can figure out the
     * physical page corresponding to the entry and dereference it.
     * (This ASSUMES that shadow PTs won't be used ever be used out of place.)
     */
    if (   pShwPage->enmKind == PGMPOOLKIND_EPT_PT_FOR_PHYS
        || pShwPage->enmKind == PGMPOOLKIND_PAE_PT_FOR_PHYS
        || pShwPage->enmKind == PGMPOOLKIND_32BIT_PT_FOR_PHYS)
    {
        RTGCPHYS GCPhysNestedEntry = pShwPage->GCPhys + ((uint32_t)iPte << X86_PAGE_SHIFT);
        if (!pShwPage->fA20Enabled)
            GCPhysNestedEntry &= ~(uint64_t)RT_BIT_64(20);
        PPGMPAGE const pPhysPage = pgmPhysGetPage(pVM, GCPhysNestedEntry);
        AssertRelease(pPhysPage);
        pgmTrackDerefGCPhys(pVM->pgm.s.CTX_SUFF(pPool), pShwPage, pPhysPage, iPte);
    }
    else
        AssertMsgFailed(("enmKind=%d GCPhys=%RGp\n", pShwPage->enmKind, pShwPage->GCPhys));
# endif

    /** @todo duplicated in the 2nd half of pgmPoolTracDerefGCPhysHint */

    /*
     * Find the guest address.
     */
    STAM_PROFILE_START(&pVM->pgm.s.Stats.StatTrackDeref, a);
    LogFlow(("SyncPageWorkerTrackDeref(%d,%d): Damn HCPhys=%RHp pShwPage->idx=%#x!!!\n",
             PGM_SHW_TYPE, PGM_GST_TYPE, HCPhys, pShwPage->idx));
    uint32_t const idRamRangeMax = RT_MIN(pVM->pgm.s.idRamRangeMax, RT_ELEMENTS(pVM->pgm.s.apRamRanges) - 1U);
    Assert(pVM->pgm.s.apRamRanges[0] == NULL);
    for (uint32_t idx = 1; idx <= idRamRangeMax; idx++)
    {
        PPGMRAMRANGE const pRam = pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRanges[idx];
        AssertContinue(pRam);
        unsigned iPage = pRam->cb >> GUEST_PAGE_SHIFT;
        while (iPage-- > 0)
        {
            if (PGM_PAGE_GET_HCPHYS(&pRam->aPages[iPage]) == HCPhys)
            {
                PPGMPOOL pPool = pVM->pgm.s.CTX_SUFF(pPool);

                Assert(pShwPage->cPresent);
                Assert(pPool->cPresent);
                pShwPage->cPresent--;
                pPool->cPresent--;

                pgmTrackDerefGCPhys(pPool, pShwPage, &pRam->aPages[iPage], iPte);
                STAM_PROFILE_STOP(&pVM->pgm.s.Stats.StatTrackDeref, a);
                return;
            }
        }
    }

    for (;;)
        AssertReleaseMsgFailed(("HCPhys=%RHp wasn't found!\n", HCPhys));
}


/**
 * Update the tracking of shadowed pages.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pShwPage    The shadow page.
 * @param   u16         The top 16-bit of the pPage->HCPhys.
 * @param   pPage       Pointer to the guest page. this will be modified.
 * @param   iPTDst      The index into the shadow table.
 */
DECLINLINE(void) PGM_BTH_NAME(SyncPageWorkerTrackAddref)(PVMCPUCC pVCpu, PPGMPOOLPAGE pShwPage, uint16_t u16,
                                                         PPGMPAGE pPage, const unsigned iPTDst)
{
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);

    /*
     * Just deal with the simple first time here.
     */
    if (!u16)
    {
        STAM_COUNTER_INC(&pVM->pgm.s.Stats.StatTrackVirgin);
        u16 = PGMPOOL_TD_MAKE(1, pShwPage->idx);
        /* Save the page table index. */
        PGM_PAGE_SET_PTE_INDEX(pVM, pPage, iPTDst);
    }
    else
        u16 = pgmPoolTrackPhysExtAddref(pVM, pPage, u16, pShwPage->idx, iPTDst);

    /* write back */
    Log2(("SyncPageWorkerTrackAddRef: u16=%#x->%#x  iPTDst=%#x pPage=%p\n", u16, PGM_PAGE_GET_TRACKING(pPage), iPTDst, pPage));
    PGM_PAGE_SET_TRACKING(pVM, pPage, u16);

    /* update statistics. */
    pVM->pgm.s.CTX_SUFF(pPool)->cPresent++;
    pShwPage->cPresent++;
    if (pShwPage->iFirstPresent > iPTDst)
        pShwPage->iFirstPresent = iPTDst;
}


/**
 * Modifies a shadow PTE to account for access handlers.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pPage       The page in question.
 * @param   GCPhysPage  The guest-physical address of the page.
 * @param   fPteSrc     The shadowed flags of the source PTE.  Must include the
 *                      A (accessed) bit so it can be emulated correctly.
 * @param   pPteDst     The shadow PTE (output).  This is temporary storage and
 *                      does not need to be set atomically.
 */
DECLINLINE(void) PGM_BTH_NAME(SyncHandlerPte)(PVMCC pVM, PVMCPUCC pVCpu, PCPGMPAGE pPage, RTGCPHYS GCPhysPage, uint64_t fPteSrc,
                                              PSHWPTE pPteDst)
{
    RT_NOREF_PV(pVM); RT_NOREF_PV(fPteSrc); RT_NOREF_PV(pVCpu); RT_NOREF_PV(GCPhysPage);

    /** @todo r=bird: Are we actually handling dirty and access bits for pages with access handlers correctly? No.
     *  Update: \#PF should deal with this before or after calling the handlers. It has all the info to do the job efficiently. */
    if (!PGM_PAGE_HAS_ACTIVE_ALL_HANDLERS(pPage))
    {
        LogFlow(("SyncHandlerPte: monitored page (%R[pgmpage]) -> mark read-only\n", pPage));
# if PGM_SHW_TYPE == PGM_TYPE_EPT
        pPteDst->u = PGM_PAGE_GET_HCPHYS(pPage) | EPT_E_READ | EPT_E_EXECUTE | EPT_E_MEMTYPE_WB | EPT_E_IGNORE_PAT;
# else
        if (fPteSrc & X86_PTE_A)
        {
            SHW_PTE_SET(*pPteDst, fPteSrc | PGM_PAGE_GET_HCPHYS(pPage));
            SHW_PTE_SET_RO(*pPteDst);
        }
        else
            SHW_PTE_SET(*pPteDst, 0);
# endif
    }
# ifdef PGM_WITH_MMIO_OPTIMIZATIONS
#  if PGM_SHW_TYPE == PGM_TYPE_EPT || PGM_SHW_TYPE == PGM_TYPE_PAE || PGM_SHW_TYPE == PGM_TYPE_AMD64
    else if (   PGM_PAGE_HAS_ACTIVE_ALL_HANDLERS(pPage)
             && (   BTH_IS_NP_ACTIVE(pVM)
                 || (fPteSrc & (X86_PTE_RW | X86_PTE_US)) == X86_PTE_RW) /** @todo Remove X86_PTE_US here and pGstWalk->Core.fEffectiveUS before the sync page test. */
#   if PGM_SHW_TYPE == PGM_TYPE_AMD64
             && pVM->pgm.s.fLessThan52PhysicalAddressBits
#   endif
            )
    {
        LogFlow(("SyncHandlerPte: MMIO page -> invalid \n"));
#   if PGM_SHW_TYPE == PGM_TYPE_EPT
        /* 25.2.3.1: Reserved physical address bit -> EPT Misconfiguration (exit 49) */
        pPteDst->u = pVM->pgm.s.HCPhysInvMmioPg
        /* 25.2.3.1: bits 2:0 = 010b -> EPT Misconfiguration (exit 49) */
                   | EPT_E_WRITE
        /* 25.2.3.1: leaf && 2:0 != 0 && u3Emt in {2, 3, 7} -> EPT Misconfiguration */
                   | EPT_E_MEMTYPE_INVALID_3;
#   else
        /* Set high page frame bits that MBZ (bankers on PAE, CPU dependent on AMD64).  */
        SHW_PTE_SET(*pPteDst, pVM->pgm.s.HCPhysInvMmioPg | X86_PTE_PAE_MBZ_MASK_NO_NX | X86_PTE_P);
#   endif
    }
#  endif
# endif /* PGM_WITH_MMIO_OPTIMIZATIONS */
    else
    {
        LogFlow(("SyncHandlerPte: monitored page (%R[pgmpage]) -> mark not present\n", pPage));
        SHW_PTE_SET(*pPteDst, 0);
    }
    /** @todo count these kinds of entries. */
}


/**
 * Creates a 4K shadow page for a guest page.
 *
 * For 4M pages the caller must convert the PDE4M to a PTE, this includes adjusting the
 * physical address.  The PdeSrc argument only the flags are used.  No page
 * structured will be mapped in this function.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pPteDst     Destination page table entry.
 * @param   PdeSrc      Source page directory entry (i.e. Guest OS page directory entry).
 *                      Can safely assume that only the flags are being used.
 * @param   PteSrc      Source page table entry (i.e. Guest OS page table entry).
 * @param   pShwPage    Pointer to the shadow page.
 * @param   iPTDst      The index into the shadow table.
 *
 * @remark  Not used for 2/4MB pages!
 */
# if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE) || defined(DOXYGEN_RUNNING)
static void PGM_BTH_NAME(SyncPageWorker)(PVMCPUCC pVCpu, PSHWPTE pPteDst, GSTPDE PdeSrc, GSTPTE PteSrc,
                                         PPGMPOOLPAGE pShwPage, unsigned iPTDst)
# else
static void PGM_BTH_NAME(SyncPageWorker)(PVMCPUCC pVCpu, PSHWPTE pPteDst, RTGCPHYS GCPhysPage,
                                         PPGMPOOLPAGE pShwPage, unsigned iPTDst)
# endif
{
    PVMCC    pVM = pVCpu->CTX_SUFF(pVM);
    RTGCPHYS GCPhysOldPage = NIL_RTGCPHYS;

# if    defined(PGMPOOL_WITH_OPTIMIZED_DIRTY_PT) \
     && PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE) \
     && (PGM_GST_TYPE == PGM_TYPE_PAE || PGM_GST_TYPE == PGM_TYPE_AMD64 || PGM_SHW_TYPE == PGM_TYPE_PAE /* pae/32bit combo */)

    if (pShwPage->fDirty)
    {
        PPGMPOOL pPool = pVM->pgm.s.CTX_SUFF(pPool);
        PGSTPT pGstPT;

        /* Note that iPTDst can be used to index the guest PT even in the pae/32bit combo as we copy only half the table; see pgmPoolAddDirtyPage. */
        pGstPT = (PGSTPT)&pPool->aDirtyPages[pShwPage->idxDirtyEntry].aPage[0];
        GCPhysOldPage = GST_GET_PTE_GCPHYS(pGstPT->a[iPTDst]);
        pGstPT->a[iPTDst].u = PteSrc.u;
    }
# else
    Assert(!pShwPage->fDirty);
# endif

# if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
    if (   (PteSrc.u & X86_PTE_P)
        && GST_IS_PTE_VALID(pVCpu, PteSrc))
# endif
    {
#  if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
        RTGCPHYS GCPhysPage = GST_GET_PTE_GCPHYS(PteSrc);
#  endif
        PGM_A20_ASSERT_MASKED(pVCpu, GCPhysPage);

        /*
         * Find the ram range.
         */
        PPGMPAGE pPage;
        int rc = pgmPhysGetPageEx(pVM, GCPhysPage, &pPage);
        if (RT_SUCCESS(rc))
        {
            /* Ignore ballooned pages.
               Don't return errors or use a fatal assert here as part of a
               shadow sync range might included ballooned pages. */
            if (PGM_PAGE_IS_BALLOONED(pPage))
            {
                Assert(!SHW_PTE_IS_P(*pPteDst)); /** @todo user tracking needs updating if this triggers. */
                return;
            }

# ifndef VBOX_WITH_NEW_LAZY_PAGE_ALLOC
            /* Make the page writable if necessary. */
            if (    PGM_PAGE_GET_TYPE(pPage)  == PGMPAGETYPE_RAM
                &&  (   PGM_PAGE_IS_ZERO(pPage)
#  if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
                     || (   (PteSrc.u & X86_PTE_RW)
#  else
                     || (   1
#  endif
                         && PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_ALLOCATED
#  ifdef VBOX_WITH_REAL_WRITE_MONITORED_PAGES
                         && PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_WRITE_MONITORED
#  endif
#  ifdef VBOX_WITH_PAGE_SHARING
                         && PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_SHARED
#  endif
                        )
                     )
               )
            {
                rc = pgmPhysPageMakeWritable(pVM, pPage, GCPhysPage);
                AssertRC(rc);
            }
# endif

            /*
             * Make page table entry.
             */
            SHWPTE PteDst;
# if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
            uint64_t fGstShwPteFlags = GST_GET_PTE_SHW_FLAGS(pVCpu, PteSrc);
# else
            uint64_t fGstShwPteFlags = X86_PTE_P | X86_PTE_RW | X86_PTE_US | X86_PTE_A | X86_PTE_D;
# endif
            if (!PGM_PAGE_HAS_ACTIVE_HANDLERS(pPage) || PGM_PAGE_IS_HNDL_PHYS_NOT_IN_HM(pPage))
            {
# if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
                /*
                 * If the page or page directory entry is not marked accessed,
                 * we mark the page not present.
                 */
                if (!(PteSrc.u & X86_PTE_A) || !(PdeSrc.u & X86_PDE_A))
                {
                    LogFlow(("SyncPageWorker: page and or page directory not accessed -> mark not present\n"));
                    STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,AccessedPage));
                    SHW_PTE_SET(PteDst, 0);
                }
                /*
                 * If the page is not flagged as dirty and is writable, then make it read-only, so we can set the dirty bit
                 * when the page is modified.
                 */
                else if (!(PteSrc.u & X86_PTE_D) && (PdeSrc.u & PteSrc.u & X86_PTE_RW))
                {
                    AssertCompile(X86_PTE_RW == X86_PDE_RW);
                    STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,DirtyPage));
                    SHW_PTE_SET(PteDst,
                                  fGstShwPteFlags
                                | PGM_PAGE_GET_HCPHYS(pPage)
                                | PGM_PTFLAGS_TRACK_DIRTY);
                    SHW_PTE_SET_RO(PteDst);
                }
                else
# endif
                {
                    STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,DirtyPageSkipped));
# if PGM_SHW_TYPE == PGM_TYPE_EPT
                    PteDst.u = PGM_PAGE_GET_HCPHYS(pPage)
                             | EPT_E_READ | EPT_E_WRITE | EPT_E_EXECUTE | EPT_E_MEMTYPE_WB | EPT_E_IGNORE_PAT;
# else
                    SHW_PTE_SET(PteDst, fGstShwPteFlags | PGM_PAGE_GET_HCPHYS(pPage));
# endif
                }

                /*
                 * Make sure only allocated pages are mapped writable.
                 */
                if (   SHW_PTE_IS_P_RW(PteDst)
                    && PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_ALLOCATED)
                {
# ifndef VBOX_WITH_NEW_LAZY_PAGE_ALLOC
                    /* Still applies to shared pages. */
                    Assert(!PGM_PAGE_IS_ZERO(pPage));
# endif
                    SHW_PTE_SET_RO(PteDst);   /** @todo this isn't quite working yet. Why, isn't it? */
                    Log3(("SyncPageWorker: write-protecting %RGp pPage=%R[pgmpage]at iPTDst=%d\n", GCPhysPage, pPage, iPTDst));
                }
            }
            else
                PGM_BTH_NAME(SyncHandlerPte)(pVM, pVCpu, pPage, GCPhysPage, fGstShwPteFlags, &PteDst);

            /*
             * Keep user track up to date.
             */
            if (SHW_PTE_IS_P(PteDst))
            {
                if (!SHW_PTE_IS_P(*pPteDst))
                    PGM_BTH_NAME(SyncPageWorkerTrackAddref)(pVCpu, pShwPage, PGM_PAGE_GET_TRACKING(pPage), pPage, iPTDst);
                else if (SHW_PTE_GET_HCPHYS(*pPteDst) != SHW_PTE_GET_HCPHYS(PteDst))
                {
                    Log2(("SyncPageWorker: deref! *pPteDst=%RX64 PteDst=%RX64\n", SHW_PTE_LOG64(*pPteDst), SHW_PTE_LOG64(PteDst)));
                    PGM_BTH_NAME(SyncPageWorkerTrackDeref)(pVCpu, pShwPage, SHW_PTE_GET_HCPHYS(*pPteDst), iPTDst, GCPhysOldPage);
                    PGM_BTH_NAME(SyncPageWorkerTrackAddref)(pVCpu, pShwPage, PGM_PAGE_GET_TRACKING(pPage), pPage, iPTDst);
                }
            }
            else if (SHW_PTE_IS_P(*pPteDst))
            {
                Log2(("SyncPageWorker: deref! *pPteDst=%RX64\n", SHW_PTE_LOG64(*pPteDst)));
                PGM_BTH_NAME(SyncPageWorkerTrackDeref)(pVCpu, pShwPage, SHW_PTE_GET_HCPHYS(*pPteDst), iPTDst, GCPhysOldPage);
            }

            /*
             * Update statistics and commit the entry.
             */
# if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
            if (!(PteSrc.u & X86_PTE_G))
                pShwPage->fSeenNonGlobal = true;
# endif
            SHW_PTE_ATOMIC_SET2(*pPteDst, PteDst);
            return;
        }

/** @todo count these three different kinds. */
        Log2(("SyncPageWorker: invalid address in Pte\n"));
    }
# if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
    else if (!(PteSrc.u & X86_PTE_P))
        Log2(("SyncPageWorker: page not present in Pte\n"));
    else
        Log2(("SyncPageWorker: invalid Pte\n"));
# endif

    /*
     * The page is not present or the PTE is bad. Replace the shadow PTE by
     * an empty entry, making sure to keep the user tracking up to date.
     */
    if (SHW_PTE_IS_P(*pPteDst))
    {
        Log2(("SyncPageWorker: deref! *pPteDst=%RX64\n", SHW_PTE_LOG64(*pPteDst)));
        PGM_BTH_NAME(SyncPageWorkerTrackDeref)(pVCpu, pShwPage, SHW_PTE_GET_HCPHYS(*pPteDst), iPTDst, GCPhysOldPage);
    }
    SHW_PTE_ATOMIC_SET(*pPteDst, 0);
}


/**
 * Syncs a guest OS page.
 *
 * There are no conflicts at this point, neither is there any need for
 * page table allocations.
 *
 * When called in PAE or AMD64 guest mode, the guest PDPE shall be valid.
 * When called in AMD64 guest mode, the guest PML4E shall be valid.
 *
 * @returns VBox status code.
 * @returns VINF_PGM_SYNCPAGE_MODIFIED_PDE if it modifies the PDE in any way.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   PdeSrc      Page directory entry of the guest.
 * @param   GCPtrPage   Guest context page address.
 * @param   cPages      Number of pages to sync (PGM_SYNC_N_PAGES) (default=1).
 * @param   uErr        Fault error (X86_TRAP_PF_*).
 */
static int PGM_BTH_NAME(SyncPage)(PVMCPUCC pVCpu, GSTPDE PdeSrc, RTGCPTR GCPtrPage, unsigned cPages, unsigned uErr)
{
    PVMCC    pVM   = pVCpu->CTX_SUFF(pVM);
    PPGMPOOL pPool = pVM->pgm.s.CTX_SUFF(pPool); NOREF(pPool);
    LogFlow(("SyncPage: GCPtrPage=%RGv cPages=%u uErr=%#x\n", GCPtrPage, cPages, uErr));
    RT_NOREF_PV(uErr); RT_NOREF_PV(cPages); RT_NOREF_PV(GCPtrPage);

    PGM_LOCK_ASSERT_OWNER(pVM);

# if   (   PGM_GST_TYPE == PGM_TYPE_32BIT \
        || PGM_GST_TYPE == PGM_TYPE_PAE \
        || PGM_GST_TYPE == PGM_TYPE_AMD64) \
    && !PGM_TYPE_IS_NESTED_OR_EPT(PGM_SHW_TYPE)

    /*
     * Assert preconditions.
     */
    Assert(PdeSrc.u & X86_PDE_P);
    Assert(cPages);
#  if 0 /* rarely useful; leave for debugging. */
    STAM_COUNTER_INC(&pVCpu->pgm.s.StatSyncPagePD[(GCPtrPage >> GST_PD_SHIFT) & GST_PD_MASK]);
#  endif

    /*
     * Get the shadow PDE, find the shadow page table in the pool.
     */
#  if PGM_SHW_TYPE == PGM_TYPE_32BIT
    const unsigned  iPDDst   = (GCPtrPage >> SHW_PD_SHIFT) & SHW_PD_MASK;
    PX86PDE         pPdeDst  = pgmShwGet32BitPDEPtr(pVCpu, GCPtrPage);
    AssertReturn(pPdeDst, VERR_INTERNAL_ERROR_3);

    /* Fetch the pgm pool shadow descriptor. */
    PPGMPOOLPAGE    pShwPde = pVCpu->pgm.s.CTX_SUFF(pShwPageCR3);
    Assert(pShwPde);

#  elif PGM_SHW_TYPE == PGM_TYPE_PAE
    const unsigned  iPDDst  = (GCPtrPage >> SHW_PD_SHIFT) & SHW_PD_MASK;
    PPGMPOOLPAGE    pShwPde = NULL;
    PX86PDPAE       pPDDst;

    /* Fetch the pgm pool shadow descriptor. */
    int rc2 = pgmShwGetPaePoolPagePD(pVCpu, GCPtrPage, &pShwPde);
    AssertRCSuccessReturn(rc2, rc2);
    Assert(pShwPde);

    pPDDst             = (PX86PDPAE)PGMPOOL_PAGE_2_PTR_V2(pVM, pVCpu, pShwPde);
    PX86PDEPAE pPdeDst = &pPDDst->a[iPDDst];

#  elif PGM_SHW_TYPE == PGM_TYPE_AMD64
    const unsigned  iPDDst   = (GCPtrPage >> SHW_PD_SHIFT) & SHW_PD_MASK;
    const unsigned  iPdpt    = (GCPtrPage >> X86_PDPT_SHIFT) & X86_PDPT_MASK_AMD64;
    PX86PDPAE       pPDDst   = NULL;            /* initialized to shut up gcc */
    PX86PDPT        pPdptDst = NULL;            /* initialized to shut up gcc */

    int rc2 = pgmShwGetLongModePDPtr(pVCpu, GCPtrPage, NULL, &pPdptDst, &pPDDst);
    AssertRCSuccessReturn(rc2, rc2);
    Assert(pPDDst && pPdptDst);
    PX86PDEPAE      pPdeDst = &pPDDst->a[iPDDst];
#  endif
    SHWPDE          PdeDst   = *pPdeDst;

    /*
     * - In the guest SMP case we could have blocked while another VCPU reused
     *   this page table.
     * - With W7-64 we may also take this path when the A bit is cleared on
     *   higher level tables (PDPE/PML4E).  The guest does not invalidate the
     *   relevant TLB entries.  If we're write monitoring any page mapped by
     *   the modified entry, we may end up here with a "stale" TLB entry.
     */
    if (!(PdeDst.u & X86_PDE_P))
    {
        Log(("CPU%u: SyncPage: Pde at %RGv changed behind our back? (pPdeDst=%p/%RX64) uErr=%#x\n", pVCpu->idCpu, GCPtrPage, pPdeDst, (uint64_t)PdeDst.u, (uint32_t)uErr));
        AssertMsg(pVM->cCpus > 1 || (uErr & (X86_TRAP_PF_P | X86_TRAP_PF_RW)) == (X86_TRAP_PF_P | X86_TRAP_PF_RW),
                  ("Unexpected missing PDE p=%p/%RX64 uErr=%#x\n", pPdeDst, (uint64_t)PdeDst.u, (uint32_t)uErr));
        if (uErr & X86_TRAP_PF_P)
            PGM_INVL_PG(pVCpu, GCPtrPage);
        return VINF_SUCCESS;    /* force the instruction to be executed again. */
    }

    PPGMPOOLPAGE    pShwPage = pgmPoolGetPage(pPool, PdeDst.u & SHW_PDE_PG_MASK);
    Assert(pShwPage);

#  if PGM_GST_TYPE == PGM_TYPE_AMD64
    /* Fetch the pgm pool shadow descriptor. */
    PPGMPOOLPAGE    pShwPde  = pgmPoolGetPage(pPool, pPdptDst->a[iPdpt].u & X86_PDPE_PG_MASK);
    Assert(pShwPde);
#  endif

    /*
     * Check that the page is present and that the shadow PDE isn't out of sync.
     */
    const bool      fBigPage  = (PdeSrc.u & X86_PDE_PS) && GST_IS_PSE_ACTIVE(pVCpu);
    const bool      fPdeValid = !fBigPage ? GST_IS_PDE_VALID(pVCpu, PdeSrc) : GST_IS_BIG_PDE_VALID(pVCpu, PdeSrc);
    RTGCPHYS        GCPhys;
    if (!fBigPage)
    {
        GCPhys = GST_GET_PDE_GCPHYS(PdeSrc);
#  if PGM_SHW_TYPE == PGM_TYPE_PAE && PGM_GST_TYPE == PGM_TYPE_32BIT
        /* Select the right PDE as we're emulating a 4kb page table with 2 shadow page tables. */
        GCPhys = PGM_A20_APPLY(pVCpu, GCPhys | ((iPDDst & 1) * (GUEST_PAGE_SIZE / 2)));
#  endif
    }
    else
    {
        GCPhys = GST_GET_BIG_PDE_GCPHYS(pVM, PdeSrc);
#  if PGM_SHW_TYPE == PGM_TYPE_PAE && PGM_GST_TYPE == PGM_TYPE_32BIT
        /* Select the right PDE as we're emulating a 4MB page directory with two 2 MB shadow PDEs.*/
        GCPhys = PGM_A20_APPLY(pVCpu, GCPhys | (GCPtrPage & (1 << X86_PD_PAE_SHIFT)));
#  endif
    }
    /** @todo This doesn't check the G bit of 2/4MB pages. FIXME */
    if (   fPdeValid
        && pShwPage->GCPhys == GCPhys
        && (PdeSrc.u & X86_PDE_P)
        && (PdeSrc.u & X86_PDE_US) == (PdeDst.u & X86_PDE_US)
        && ((PdeSrc.u & X86_PDE_RW) == (PdeDst.u & X86_PDE_RW) || !(PdeDst.u & X86_PDE_RW))
#  if PGM_WITH_NX(PGM_GST_TYPE, PGM_SHW_TYPE)
        && ((PdeSrc.u & X86_PDE_PAE_NX) == (PdeDst.u & X86_PDE_PAE_NX) || !GST_IS_NX_ACTIVE(pVCpu))
#  endif
       )
    {
        /*
         * Check that the PDE is marked accessed already.
         * Since we set the accessed bit *before* getting here on a #PF, this
         * check is only meant for dealing with non-#PF'ing paths.
         */
        if (PdeSrc.u & X86_PDE_A)
        {
            PSHWPT pPTDst = (PSHWPT)PGMPOOL_PAGE_2_PTR_V2(pVM, pVCpu, pShwPage);
            if (!fBigPage)
            {
                /*
                 * 4KB Page - Map the guest page table.
                 */
                PGSTPT pPTSrc;
                int rc = PGM_GCPHYS_2_PTR_V2(pVM, pVCpu, GST_GET_PDE_GCPHYS(PdeSrc), &pPTSrc);
                if (RT_SUCCESS(rc))
                {
#  ifdef PGM_SYNC_N_PAGES
                    Assert(cPages == 1 || !(uErr & X86_TRAP_PF_P));
                    if (    cPages > 1
                        &&  !(uErr & X86_TRAP_PF_P)
                        &&  !VM_FF_IS_SET(pVM, VM_FF_PGM_NO_MEMORY))
                    {
                        /*
                         * This code path is currently only taken when the caller is PGMTrap0eHandler
                         * for non-present pages!
                         *
                         * We're setting PGM_SYNC_NR_PAGES pages around the faulting page to sync it and
                         * deal with locality.
                         */
                        unsigned        iPTDst      = (GCPtrPage >> SHW_PT_SHIFT) & SHW_PT_MASK;
#   if PGM_SHW_TYPE == PGM_TYPE_PAE && PGM_GST_TYPE == PGM_TYPE_32BIT
                        /* Select the right PDE as we're emulating a 4kb page table with 2 shadow page tables. */
                        const unsigned  offPTSrc    = ((GCPtrPage >> SHW_PD_SHIFT) & 1) * 512;
#   else
                        const unsigned  offPTSrc    = 0;
#   endif
                        const unsigned  iPTDstEnd   = RT_MIN(iPTDst + PGM_SYNC_NR_PAGES / 2, RT_ELEMENTS(pPTDst->a));
                        if (iPTDst < PGM_SYNC_NR_PAGES / 2)
                            iPTDst = 0;
                        else
                            iPTDst -= PGM_SYNC_NR_PAGES / 2;

                        for (; iPTDst < iPTDstEnd; iPTDst++)
                        {
                            const PGSTPTE pPteSrc = &pPTSrc->a[offPTSrc + iPTDst];

                            if (   (pPteSrc->u & X86_PTE_P)
                                && !SHW_PTE_IS_P(pPTDst->a[iPTDst]))
                            {
                                RTGCPTR GCPtrCurPage = (GCPtrPage & ~(RTGCPTR)(GST_PT_MASK << GST_PT_SHIFT))
                                                     | ((offPTSrc + iPTDst) << GUEST_PAGE_SHIFT);
                                NOREF(GCPtrCurPage);
                                PGM_BTH_NAME(SyncPageWorker)(pVCpu, &pPTDst->a[iPTDst], PdeSrc, *pPteSrc, pShwPage, iPTDst);
                                Log2(("SyncPage: 4K+ %RGv PteSrc:{P=%d RW=%d U=%d raw=%08llx} PteDst=%08llx%s\n",
                                      GCPtrCurPage, pPteSrc->u & X86_PTE_P,
                                      !!(pPteSrc->u & PdeSrc.u & X86_PTE_RW),
                                      !!(pPteSrc->u & PdeSrc.u & X86_PTE_US),
                                      (uint64_t)pPteSrc->u,
                                      SHW_PTE_LOG64(pPTDst->a[iPTDst]),
                                      SHW_PTE_IS_TRACK_DIRTY(pPTDst->a[iPTDst]) ? " Track-Dirty" : ""));
                            }
                        }
                    }
                    else
#  endif /* PGM_SYNC_N_PAGES */
                    {
                        const unsigned iPTSrc = (GCPtrPage >> GST_PT_SHIFT) & GST_PT_MASK;
                        GSTPTE PteSrc = pPTSrc->a[iPTSrc];
                        const unsigned iPTDst = (GCPtrPage >> SHW_PT_SHIFT) & SHW_PT_MASK;
                        PGM_BTH_NAME(SyncPageWorker)(pVCpu, &pPTDst->a[iPTDst], PdeSrc, PteSrc, pShwPage, iPTDst);
                        Log2(("SyncPage: 4K  %RGv PteSrc:{P=%d RW=%d U=%d raw=%08llx} PteDst=%08llx %s\n",
                              GCPtrPage, PteSrc.u & X86_PTE_P,
                              !!(PteSrc.u & PdeSrc.u & X86_PTE_RW),
                              !!(PteSrc.u & PdeSrc.u & X86_PTE_US),
                              (uint64_t)PteSrc.u,
                              SHW_PTE_LOG64(pPTDst->a[iPTDst]),
                              SHW_PTE_IS_TRACK_DIRTY(pPTDst->a[iPTDst]) ? " Track-Dirty" : ""));
                    }
                }
                else /* MMIO or invalid page: emulated in #PF handler. */
                {
                    LogFlow(("PGM_GCPHYS_2_PTR_V2 %RGp failed with %Rrc\n", GCPhys, rc));
                    Assert(!SHW_PTE_IS_P(pPTDst->a[(GCPtrPage >> SHW_PT_SHIFT) & SHW_PT_MASK]));
                }
            }
            else
            {
                /*
                 * 4/2MB page - lazy syncing shadow 4K pages.
                 * (There are many causes of getting here, it's no longer only CSAM.)
                 */
                /* Calculate the GC physical address of this 4KB shadow page. */
                GCPhys = PGM_A20_APPLY(pVCpu, GST_GET_BIG_PDE_GCPHYS(pVM, PdeSrc) | (GCPtrPage & GST_BIG_PAGE_OFFSET_MASK));
                /* Find ram range. */
                PPGMPAGE pPage;
                int rc = pgmPhysGetPageEx(pVM, GCPhys, &pPage);
                if (RT_SUCCESS(rc))
                {
                    AssertFatalMsg(!PGM_PAGE_IS_BALLOONED(pPage), ("Unexpected ballooned page at %RGp\n", GCPhys));

#  ifndef VBOX_WITH_NEW_LAZY_PAGE_ALLOC
                    /* Try to make the page writable if necessary. */
                    if (    PGM_PAGE_GET_TYPE(pPage)  == PGMPAGETYPE_RAM
                        &&  (   PGM_PAGE_IS_ZERO(pPage)
                             || (   (PdeSrc.u & X86_PDE_RW)
                                 && PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_ALLOCATED
#   ifdef VBOX_WITH_REAL_WRITE_MONITORED_PAGES
                                 && PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_WRITE_MONITORED
#   endif
#   ifdef VBOX_WITH_PAGE_SHARING
                                 && PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_SHARED
#   endif
                                 )
                             )
                       )
                    {
                        rc = pgmPhysPageMakeWritable(pVM, pPage, GCPhys);
                        AssertRC(rc);
                    }
#  endif

                    /*
                     * Make shadow PTE entry.
                     */
                    SHWPTE PteDst;
                    if (!PGM_PAGE_HAS_ACTIVE_HANDLERS(pPage) || PGM_PAGE_IS_HNDL_PHYS_NOT_IN_HM(pPage))
                        SHW_PTE_SET(PteDst, GST_GET_BIG_PDE_SHW_FLAGS_4_PTE(pVCpu, PdeSrc) | PGM_PAGE_GET_HCPHYS(pPage));
                    else
                        PGM_BTH_NAME(SyncHandlerPte)(pVM, pVCpu, pPage, GCPhys, GST_GET_BIG_PDE_SHW_FLAGS_4_PTE(pVCpu, PdeSrc), &PteDst);

                    const unsigned iPTDst = (GCPtrPage >> SHW_PT_SHIFT) & SHW_PT_MASK;
                    if (    SHW_PTE_IS_P(PteDst)
                        &&  !SHW_PTE_IS_P(pPTDst->a[iPTDst]))
                        PGM_BTH_NAME(SyncPageWorkerTrackAddref)(pVCpu, pShwPage, PGM_PAGE_GET_TRACKING(pPage), pPage, iPTDst);

                    /* Make sure only allocated pages are mapped writable. */
                    if (    SHW_PTE_IS_P_RW(PteDst)
                        &&  PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_ALLOCATED)
                    {
# ifndef VBOX_WITH_NEW_LAZY_PAGE_ALLOC
                        /* Still applies to shared pages. */
                        Assert(!PGM_PAGE_IS_ZERO(pPage));
# endif
                        SHW_PTE_SET_RO(PteDst);   /** @todo this isn't quite working yet... */
                        Log3(("SyncPage: write-protecting %RGp pPage=%R[pgmpage] at %RGv\n", GCPhys, pPage, GCPtrPage));
                    }

                    SHW_PTE_ATOMIC_SET2(pPTDst->a[iPTDst], PteDst);

                    /*
                     * If the page is not flagged as dirty and is writable, then make it read-only
                     * at PD level, so we can set the dirty bit when the page is modified.
                     *
                     * ASSUMES that page access handlers are implemented on page table entry level.
                     *      Thus we will first catch the dirty access and set PDE.D and restart. If
                     *      there is an access handler, we'll trap again and let it work on the problem.
                     */
                    /** @todo r=bird: figure out why we need this here, SyncPT should've taken care of this already.
                     * As for invlpg, it simply frees the whole shadow PT.
                     * ...It's possibly because the guest clears it and the guest doesn't really tell us... */
                    if ((PdeSrc.u & (X86_PDE4M_D | X86_PDE_RW)) == X86_PDE_RW)
                    {
                        STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,DirtyPageBig));
                        PdeDst.u |= PGM_PDFLAGS_TRACK_DIRTY;
                        PdeDst.u &= ~(SHWUINT)X86_PDE_RW;
                    }
                    else
                    {
                        PdeDst.u &= ~(SHWUINT)(PGM_PDFLAGS_TRACK_DIRTY | X86_PDE_RW);
                        PdeDst.u |= PdeSrc.u & X86_PDE_RW;
                    }
                    SHW_PDE_ATOMIC_SET2(*pPdeDst, PdeDst);
                    Log2(("SyncPage: BIG %RGv PdeSrc:{P=%d RW=%d U=%d raw=%08llx} GCPhys=%RGp%s\n",
                          GCPtrPage, PdeSrc.u & X86_PDE_P, !!(PdeSrc.u & X86_PDE_RW), !!(PdeSrc.u & X86_PDE_US),
                          (uint64_t)PdeSrc.u, GCPhys, PdeDst.u & PGM_PDFLAGS_TRACK_DIRTY ? " Track-Dirty" : ""));
                }
                else
                {
                    LogFlow(("pgmPhysGetPageEx %RGp (big) failed with %Rrc\n", GCPhys, rc));
                    /** @todo must wipe the shadow page table entry in this
                     *        case. */
                }
            }
            PGM_DYNMAP_UNUSED_HINT(pVCpu, pPdeDst);
            return VINF_SUCCESS;
        }

        STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,SyncPagePDNAs));
    }
    else if (fPdeValid)
    {
        STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,SyncPagePDOutOfSync));
        Log2(("SyncPage: Out-Of-Sync PDE at %RGp PdeSrc=%RX64 PdeDst=%RX64 (GCPhys %RGp vs %RGp)\n",
              GCPtrPage, (uint64_t)PdeSrc.u, (uint64_t)PdeDst.u, pShwPage->GCPhys, GCPhys));
    }
    else
    {
/// @todo        STAM_COUNTER_INC(&pVCpu->pgm.s.CTX_MID_Z(Stat,SyncPagePDOutOfSyncAndInvalid));
        Log2(("SyncPage: Bad PDE at %RGp PdeSrc=%RX64 PdeDst=%RX64 (GCPhys %RGp vs %RGp)\n",
              GCPtrPage, (uint64_t)PdeSrc.u, (uint64_t)PdeDst.u, pShwPage->GCPhys, GCPhys));
    }

    /*
     * Mark the PDE not present.  Restart the instruction and let #PF call SyncPT.
     * Yea, I'm lazy.
     */
    pgmPoolFreeByPage(pPool, pShwPage, pShwPde->idx, iPDDst);
    SHW_PDE_ATOMIC_SET(*pPdeDst, 0);

    PGM_DYNMAP_UNUSED_HINT(pVCpu, pPdeDst);
    PGM_INVL_VCPU_TLBS(pVCpu);
    return VINF_PGM_SYNCPAGE_MODIFIED_PDE;


# elif (PGM_GST_TYPE == PGM_TYPE_REAL || PGM_GST_TYPE == PGM_TYPE_PROT) \
    && !PGM_TYPE_IS_NESTED(PGM_SHW_TYPE) \
    && (PGM_SHW_TYPE != PGM_TYPE_EPT || PGM_GST_TYPE == PGM_TYPE_PROT)
    NOREF(PdeSrc);

#  ifdef PGM_SYNC_N_PAGES
    /*
     * Get the shadow PDE, find the shadow page table in the pool.
     */
#   if PGM_SHW_TYPE == PGM_TYPE_32BIT
    X86PDE          PdeDst = pgmShwGet32BitPDE(pVCpu, GCPtrPage);

#   elif PGM_SHW_TYPE == PGM_TYPE_PAE
    X86PDEPAE       PdeDst = pgmShwGetPaePDE(pVCpu, GCPtrPage);

#   elif PGM_SHW_TYPE == PGM_TYPE_AMD64
    const unsigned  iPDDst   = ((GCPtrPage >> SHW_PD_SHIFT) & SHW_PD_MASK);
    const unsigned  iPdpt    = (GCPtrPage >> X86_PDPT_SHIFT) & X86_PDPT_MASK_AMD64; NOREF(iPdpt);
    PX86PDPAE       pPDDst   = NULL;            /* initialized to shut up gcc */
    X86PDEPAE       PdeDst;
    PX86PDPT        pPdptDst = NULL;            /* initialized to shut up gcc */

    int rc = pgmShwGetLongModePDPtr(pVCpu, GCPtrPage, NULL, &pPdptDst, &pPDDst);
    AssertRCSuccessReturn(rc, rc);
    Assert(pPDDst && pPdptDst);
    PdeDst = pPDDst->a[iPDDst];

#   elif PGM_SHW_TYPE == PGM_TYPE_EPT
    const unsigned  iPDDst = ((GCPtrPage >> SHW_PD_SHIFT) & SHW_PD_MASK);
    PEPTPD          pPDDst;
    EPTPDE          PdeDst;

    int rc = pgmShwGetEPTPDPtr(pVCpu, GCPtrPage, NULL, &pPDDst);
    if (rc != VINF_SUCCESS)
    {
        AssertRC(rc);
        return rc;
    }
    Assert(pPDDst);
    PdeDst = pPDDst->a[iPDDst];
#   endif
    /* In the guest SMP case we could have blocked while another VCPU reused this page table. */
    if (!SHW_PDE_IS_P(PdeDst))
    {
        AssertMsg(pVM->cCpus > 1, ("Unexpected missing PDE %RX64\n", (uint64_t)PdeDst.u));
        Log(("CPU%d: SyncPage: Pde at %RGv changed behind our back!\n", pVCpu->idCpu, GCPtrPage));
        return VINF_SUCCESS;    /* force the instruction to be executed again. */
    }

    /* Can happen in the guest SMP case; other VCPU activated this PDE while we were blocking to handle the page fault. */
    if (SHW_PDE_IS_BIG(PdeDst))
    {
        Assert(pVM->pgm.s.fNestedPaging);
        Log(("CPU%d: SyncPage: Pde (big:%RX64) at %RGv changed behind our back!\n", pVCpu->idCpu, PdeDst.u, GCPtrPage));
        return VINF_SUCCESS;
    }

    /* Mask away the page offset. */
    GCPtrPage &= ~((RTGCPTR)0xfff);

    PPGMPOOLPAGE  pShwPage = pgmPoolGetPage(pPool, PdeDst.u & SHW_PDE_PG_MASK);
    PSHWPT        pPTDst   = (PSHWPT)PGMPOOL_PAGE_2_PTR_V2(pVM, pVCpu, pShwPage);

    Assert(cPages == 1 || !(uErr & X86_TRAP_PF_P));
    if (    cPages > 1
        &&  !(uErr & X86_TRAP_PF_P)
        &&  !VM_FF_IS_SET(pVM, VM_FF_PGM_NO_MEMORY))
    {
        /*
         * This code path is currently only taken when the caller is PGMTrap0eHandler
         * for non-present pages!
         *
         * We're setting PGM_SYNC_NR_PAGES pages around the faulting page to sync it and
         * deal with locality.
         */
        unsigned        iPTDst    = (GCPtrPage >> SHW_PT_SHIFT) & SHW_PT_MASK;
        const unsigned  iPTDstEnd = RT_MIN(iPTDst + PGM_SYNC_NR_PAGES / 2, RT_ELEMENTS(pPTDst->a));
        if (iPTDst < PGM_SYNC_NR_PAGES / 2)
            iPTDst = 0;
        else
            iPTDst -= PGM_SYNC_NR_PAGES / 2;
        for (; iPTDst < iPTDstEnd; iPTDst++)
        {
            if (!SHW_PTE_IS_P(pPTDst->a[iPTDst]))
            {
                RTGCPTR GCPtrCurPage = PGM_A20_APPLY(pVCpu, (GCPtrPage & ~(RTGCPTR)(SHW_PT_MASK << SHW_PT_SHIFT))
                                                          | (iPTDst << GUEST_PAGE_SHIFT));

                PGM_BTH_NAME(SyncPageWorker)(pVCpu, &pPTDst->a[iPTDst], GCPtrCurPage, pShwPage, iPTDst);
                Log2(("SyncPage: 4K+ %RGv PteSrc:{P=1 RW=1 U=1} PteDst=%08llx%s\n",
                      GCPtrCurPage,
                      SHW_PTE_LOG64(pPTDst->a[iPTDst]),
                      SHW_PTE_IS_TRACK_DIRTY(pPTDst->a[iPTDst]) ? " Track-Dirty" : ""));

                if (RT_UNLIKELY(VM_FF_IS_SET(pVM, VM_FF_PGM_NO_MEMORY)))
                    break;
            }
            else
                Log4(("%RGv iPTDst=%x pPTDst->a[iPTDst] %RX64\n",
                      (GCPtrPage & ~(RTGCPTR)(SHW_PT_MASK << SHW_PT_SHIFT)) | (iPTDst << GUEST_PAGE_SHIFT), iPTDst, SHW_PTE_LOG64(pPTDst->a[iPTDst]) ));
        }
    }
    else
#  endif /* PGM_SYNC_N_PAGES */
    {
        const unsigned  iPTDst       = (GCPtrPage >> SHW_PT_SHIFT) & SHW_PT_MASK;
        RTGCPTR         GCPtrCurPage = PGM_A20_APPLY(pVCpu, (GCPtrPage & ~(RTGCPTR)(SHW_PT_MASK << SHW_PT_SHIFT))
                                                          | (iPTDst << GUEST_PAGE_SHIFT));

        PGM_BTH_NAME(SyncPageWorker)(pVCpu, &pPTDst->a[iPTDst], GCPtrCurPage, pShwPage, iPTDst);

        Log2(("SyncPage: 4K  %RGv PteSrc:{P=1 RW=1 U=1}PteDst=%08llx%s\n",
              GCPtrPage,
              SHW_PTE_LOG64(pPTDst->a[iPTDst]),
              SHW_PTE_IS_TRACK_DIRTY(pPTDst->a[iPTDst]) ? " Track-Dirty" : ""));
    }
    return VINF_SUCCESS;

# else
    NOREF(PdeSrc);
    AssertReleaseMsgFailed(("Shw=%d Gst=%d is not implemented!\n", PGM_GST_TYPE, PGM_SHW_TYPE));
    return VERR_PGM_NOT_USED_IN_MODE;
# endif
}

#endif /* PGM_SHW_TYPE != PGM_TYPE_NONE */

#if !defined(IN_RING3) && defined(VBOX_WITH_NESTED_HWVIRT_VMX_EPT) && PGM_SHW_TYPE == PGM_TYPE_EPT

/**
 * Sync a shadow page for a nested-guest page.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pPte            The shadow page table entry.
 * @param   GCPhysPage      The guest-physical address of the page.
 * @param   pShwPage        The shadow page of the page table.
 * @param   iPte            The index of the page table entry.
 * @param   pGstSlatPte     The guest SLAT page table entry.
 *
 * @note    Not to be used for 2/4MB pages!
 */
static void PGM_BTH_NAME(NestedSyncPageWorker)(PVMCPUCC pVCpu, PSHWPTE pPte, RTGCPHYS GCPhysPage, PPGMPOOLPAGE pShwPage,
                                               unsigned iPte, SLATPTE GstSlatPte)
{
    PGM_A20_ASSERT_MASKED(pVCpu, GCPhysPage);
    Assert(PGMPOOL_PAGE_IS_NESTED(pShwPage));
    Assert(!pShwPage->fDirty);
    Assert(pVCpu->pgm.s.enmGuestSlatMode == PGMSLAT_EPT);
    AssertMsg(!(GstSlatPte.u & EPT_E_LEAF), ("Large page unexpected: %RX64\n", GstSlatPte.u));
    AssertMsg((GstSlatPte.u & EPT_PTE_PG_MASK) == GCPhysPage,
              ("PTE address mismatch. GCPhysPage=%RGp Pte=%RX64\n", GCPhysPage, GstSlatPte.u & EPT_PTE_PG_MASK));

    /*
     * Find the ram range.
     */
    PPGMPAGE pPage;
    int rc = pgmPhysGetPageEx(pVCpu->CTX_SUFF(pVM), GCPhysPage, &pPage);
    if (RT_SUCCESS(rc))
    { /* likely */ }
    else
    {
        /*
         * This is a RAM hole/invalid/reserved address (not MMIO).
         * Nested Microsoft Hyper-V maps addresses like 0xf0220000 as RW WB memory.
         * Shadow a not-present page similar to MMIO, see @bugref{10318#c7}.
         */
        Assert(rc == VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS);
        if (SHW_PTE_IS_P(*pPte))
        {
            Log2(("NestedSyncPageWorker: deref! *pPte=%RX64\n", SHW_PTE_LOG64(*pPte)));
            PGM_BTH_NAME(SyncPageWorkerTrackDeref)(pVCpu, pShwPage, SHW_PTE_GET_HCPHYS(*pPte), iPte, NIL_RTGCPHYS);
        }
        Log7Func(("RAM hole/reserved %RGp -> ShwPte=0\n", GCPhysPage));
        SHW_PTE_ATOMIC_SET(*pPte, 0);
        return;
    }

    Assert(!PGM_PAGE_IS_BALLOONED(pPage));

    /*
     * Make page table entry.
     */
    SHWPTE Pte;
    uint64_t const fGstShwPteFlags = (GstSlatPte.u & pVCpu->pgm.s.fGstEptShadowedPteMask)
                                   | EPT_E_MEMTYPE_WB | EPT_E_IGNORE_PAT;
    if (!PGM_PAGE_HAS_ACTIVE_HANDLERS(pPage) || PGM_PAGE_IS_HNDL_PHYS_NOT_IN_HM(pPage))
    {
# ifndef VBOX_WITH_NEW_LAZY_PAGE_ALLOC
        /* If it's the zero page or write to an unallocated page, allocate it to make it writable. */
        if (    PGM_PAGE_GET_TYPE(pPage)  == PGMPAGETYPE_RAM
            &&  (   PGM_PAGE_IS_ZERO(pPage)
                 || (   (GstSlatPte.u & EPT_E_WRITE)
                     && PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_ALLOCATED
#  ifdef VBOX_WITH_REAL_WRITE_MONITORED_PAGES
                     && PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_WRITE_MONITORED
#  endif
#  ifdef VBOX_WITH_PAGE_SHARING
                     && PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_SHARED
#  endif
                     && PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_BALLOONED
                 )
              )
           )
        {
            rc = pgmPhysPageMakeWritable(pVCpu->CTX_SUFF(pVM), pPage, GCPhysPage);
            AssertRC(rc);
            Log7Func(("made writable (%R[pgmpage]) at %RGp\n", pPage, GCPhysPage));
        }
# endif
        /** @todo access bit. */
        Pte.u = PGM_PAGE_GET_HCPHYS(pPage) | fGstShwPteFlags;
        Log7Func(("regular page (%R[pgmpage]) at %RGp -> %RX64\n", pPage, GCPhysPage, Pte.u));

        /* Make sure only allocated pages are mapped writable. */
        if (   (fGstShwPteFlags & EPT_E_WRITE)
            && PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_ALLOCATED)
        {
            Pte.u &= ~EPT_E_WRITE;
            Log7Func(("write-protecting page (%R[pgmpage]) at %RGp -> %RX64\n", pPage, GCPhysPage, Pte.u));
        }
    }
    else if (!PGM_PAGE_HAS_ACTIVE_ALL_HANDLERS(pPage))
    {
        /** @todo access bit. */
        Pte.u = PGM_PAGE_GET_HCPHYS(pPage) | (fGstShwPteFlags & ~EPT_E_WRITE);
        Log7Func(("monitored page (%R[pgmpage]) at %RGp -> %RX64\n", pPage, GCPhysPage, Pte.u));
    }
    else
    {
        /** @todo Do MMIO optimizations here too? */
        Log7Func(("mmio/all page (%R[pgmpage]) at %RGp -> 0\n", pPage, GCPhysPage));
        Pte.u = 0;
    }

    /* Make sure only allocated pages are mapped writable. */
    Assert(!SHW_PTE_IS_P_RW(Pte) || PGM_PAGE_IS_ALLOCATED(pPage));

    /*
     * Keep user track up to date.
     */
    if (SHW_PTE_IS_P(Pte))
    {
        if (!SHW_PTE_IS_P(*pPte))
            PGM_BTH_NAME(SyncPageWorkerTrackAddref)(pVCpu, pShwPage, PGM_PAGE_GET_TRACKING(pPage), pPage, iPte);
        else if (SHW_PTE_GET_HCPHYS(*pPte) != SHW_PTE_GET_HCPHYS(Pte))
        {
            Log2(("NestedSyncPageWorker: deref! *pPte=%RX64 Pte=%RX64\n", SHW_PTE_LOG64(*pPte), SHW_PTE_LOG64(Pte)));
            PGM_BTH_NAME(SyncPageWorkerTrackDeref)(pVCpu, pShwPage, SHW_PTE_GET_HCPHYS(*pPte), iPte, NIL_RTGCPHYS);
            PGM_BTH_NAME(SyncPageWorkerTrackAddref)(pVCpu, pShwPage, PGM_PAGE_GET_TRACKING(pPage), pPage, iPte);
        }
    }
    else if (SHW_PTE_IS_P(*pPte))
    {
        Log2(("NestedSyncPageWorker: deref! *pPte=%RX64\n", SHW_PTE_LOG64(*pPte)));
        PGM_BTH_NAME(SyncPageWorkerTrackDeref)(pVCpu, pShwPage, SHW_PTE_GET_HCPHYS(*pPte), iPte, NIL_RTGCPHYS);
    }

    /*
     * Commit the entry.
     */
    SHW_PTE_ATOMIC_SET2(*pPte, Pte);
    return;
}


/**
 * Syncs a nested-guest page.
 *
 * There are no conflicts at this point, neither is there any need for
 * page table allocations.
 *
 * @returns VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   GCPhysNestedPage    The nested-guest physical address of the page being
 *                              synced.
 * @param   GCPhysPage          The guest-physical address of the page being synced.
 * @param   cPages              Number of pages to sync (PGM_SYNC_N_PAGES) (default=1).
 * @param   uErr                The page fault error (X86_TRAP_PF_XXX).
 * @param   pGstWalkAll         The guest page table walk result.
 */
static int PGM_BTH_NAME(NestedSyncPage)(PVMCPUCC pVCpu, RTGCPHYS GCPhysNestedPage, RTGCPHYS GCPhysPage, unsigned cPages,
                                        uint32_t uErr, PPGMPTWALKGST pGstWalkAll)
{
    PGM_A20_ASSERT_MASKED(pVCpu, GCPhysPage);
    Assert(!(GCPhysNestedPage & GUEST_PAGE_OFFSET_MASK));
    Assert(!(GCPhysPage & GUEST_PAGE_OFFSET_MASK));

    PVMCC    pVM   = pVCpu->CTX_SUFF(pVM);
    PPGMPOOL pPool = pVM->pgm.s.CTX_SUFF(pPool); NOREF(pPool);
    Log7Func(("GCPhysNestedPage=%RGv GCPhysPage=%RGp cPages=%u uErr=%#x\n", GCPhysNestedPage, GCPhysPage, cPages, uErr));
    RT_NOREF_PV(uErr); RT_NOREF_PV(cPages);

    PGM_LOCK_ASSERT_OWNER(pVM);

    /*
     * Get the shadow PDE, find the shadow page table in the pool.
     */
    unsigned const iPde = ((GCPhysNestedPage >> EPT_PD_SHIFT) & EPT_PD_MASK);
    PEPTPD pPd;
    int rc = pgmShwGetNestedEPTPDPtr(pVCpu, GCPhysNestedPage, NULL, &pPd, pGstWalkAll);
    if (RT_SUCCESS(rc))
    { /* likely */ }
    else
    {
        Log(("Failed to fetch EPT PD for %RGp (%RGp) rc=%Rrc\n", GCPhysNestedPage, GCPhysPage, rc));
        return rc;
    }
    Assert(pPd);
    EPTPDE Pde = pPd->a[iPde];

    /* In the guest SMP case we could have blocked while another VCPU reused this page table. */
    if (!SHW_PDE_IS_P(Pde))
    {
        AssertMsg(pVM->cCpus > 1, ("Unexpected missing PDE %RX64\n", (uint64_t)Pde.u));
        Log7Func(("CPU%d: SyncPage: Pde at %RGp changed behind our back!\n", pVCpu->idCpu, GCPhysNestedPage));
        return VINF_SUCCESS;    /* force the instruction to be executed again. */
    }

    /* Can happen in the guest SMP case; other VCPU activated this PDE while we were blocking to handle the page fault. */
    if (SHW_PDE_IS_BIG(Pde))
    {
        Log7Func(("CPU%d: SyncPage: %RGp changed behind our back!\n", pVCpu->idCpu, GCPhysNestedPage));
        return VINF_SUCCESS;
    }

    PPGMPOOLPAGE pShwPage = pgmPoolGetPage(pPool, Pde.u & EPT_PDE_PG_MASK);
    PEPTPT       pPt      = (PEPTPT)PGMPOOL_PAGE_2_PTR_V2(pVM, pVCpu, pShwPage);

    /*
     * If we've shadowed a guest EPT PDE that maps a 2M page using a 4K table,
     * then sync the 4K sub-page in the 2M range.
     */
    if (pGstWalkAll->u.Ept.Pde.u & EPT_E_LEAF)
    {
        Assert(!SHW_PDE_IS_BIG(Pde));

        Assert(pGstWalkAll->u.Ept.Pte.u == 0);
        Assert((Pde.u & EPT_PRESENT_MASK) == (pGstWalkAll->u.Ept.Pde.u & EPT_PRESENT_MASK));
        Assert(pShwPage->GCPhys == (pGstWalkAll->u.Ept.Pde.u & EPT_PDE2M_PG_MASK));

#if defined(VBOX_STRICT) && defined(DEBUG_ramshankar)
        PPGMPAGE pPage;
        rc = pgmPhysGetPageEx(pVM, GCPhysPage, &pPage); AssertRC(rc);
        Assert(PGM_PAGE_GET_PDE_TYPE(pPage) != PGM_PAGE_PDE_TYPE_PDE);
        Assert(pShwPage->enmKind == PGMPOOLKIND_EPT_PT_FOR_EPT_2MB);
#endif
        uint64_t const fGstShwPteFlags = (pGstWalkAll->u.Ept.Pde.u & pVCpu->pgm.s.fGstEptShadowedBigPdeMask & ~EPT_E_LEAF)
                                       | EPT_E_MEMTYPE_WB | EPT_E_IGNORE_PAT;
        SLATPTE GstSlatPte;
        GstSlatPte.u = GCPhysPage | fGstShwPteFlags;

        unsigned const iPte = (GCPhysNestedPage >> SHW_PT_SHIFT) & SHW_PT_MASK;
        PGM_BTH_NAME(NestedSyncPageWorker)(pVCpu, &pPt->a[iPte], GCPhysPage, pShwPage, iPte, GstSlatPte);
        Log7Func(("4K: GCPhysPage=%RGp iPte=%u ShwPte=%08llx\n", GCPhysPage, iPte, SHW_PTE_LOG64(pPt->a[iPte])));
        return VINF_SUCCESS;
    }

    Assert(cPages == 1 || !(uErr & X86_TRAP_PF_P));
# ifdef PGM_SYNC_N_PAGES
    if (    cPages > 1
        && !(uErr & X86_TRAP_PF_P)
        && !VM_FF_IS_SET(pVM, VM_FF_PGM_NO_MEMORY))
    {
        /*
         * This code path is currently only taken for non-present pages!
         *
         * We're setting PGM_SYNC_NR_PAGES pages around the faulting page to sync it and
         * deal with locality.
         */
        unsigned       iPte    = (GCPhysNestedPage >> SHW_PT_SHIFT) & SHW_PT_MASK;
        unsigned const iPteEnd = RT_MIN(iPte + PGM_SYNC_NR_PAGES / 2, RT_ELEMENTS(pPt->a));
        if (iPte < PGM_SYNC_NR_PAGES / 2)
            iPte = 0;
        else
            iPte -= PGM_SYNC_NR_PAGES / 2;
        for (; iPte < iPteEnd; iPte++)
        {
            if (!SHW_PTE_IS_P(pPt->a[iPte]))
            {
                PGMPTWALKGST GstWalkPt;
                PGMPTWALK    WalkPt;
                GCPhysNestedPage &= ~(SHW_PT_MASK << SHW_PT_SHIFT);
                GCPhysNestedPage |= (iPte << GUEST_PAGE_SHIFT);
                rc = pgmGstSlatWalk(pVCpu, GCPhysNestedPage, false /*fIsLinearAddrValid*/, 0 /*GCPtrNested*/, &WalkPt,
                                    &GstWalkPt);
                if (RT_SUCCESS(rc))
                    PGM_BTH_NAME(NestedSyncPageWorker)(pVCpu, &pPt->a[iPte], WalkPt.GCPhys, pShwPage, iPte, GstWalkPt.u.Ept.Pte);
                else
                {
                    /*
                     * This could be MMIO pages reserved by the nested-hypevisor or genuinely not-present pages.
                     * Ensure the shadow tables entry is not-present.
                     */
                    /** @todo Potential room for optimization (explained in NestedSyncPT). */
                    AssertMsg(!pPt->a[iPte].u, ("%RX64\n", pPt->a[iPte].u));
                }
                Log7Func(("Many: %RGp iPte=%u ShwPte=%RX64\n", GCPhysNestedPage, iPte, SHW_PTE_LOG64(pPt->a[iPte])));
                if (RT_UNLIKELY(VM_FF_IS_SET(pVM, VM_FF_PGM_NO_MEMORY)))
                    break;
            }
            else
            {
#  ifdef VBOX_STRICT
                /* Paranoia - Verify address of the page is what it should be. */
                PGMPTWALKGST GstWalkPt;
                PGMPTWALK    WalkPt;
                GCPhysNestedPage &= ~(SHW_PT_MASK << SHW_PT_SHIFT);
                GCPhysNestedPage |= (iPte << GUEST_PAGE_SHIFT);
                rc = pgmGstSlatWalk(pVCpu, GCPhysNestedPage, false /*fIsLinearAddrValid*/, 0 /*GCPtrNested*/, &WalkPt, &GstWalkPt);
                AssertRC(rc);
                PPGMPAGE pPage;
                rc = pgmPhysGetPageEx(pVM, WalkPt.GCPhys, &pPage);
                AssertRC(rc);
                AssertMsg(PGM_PAGE_GET_HCPHYS(pPage) == SHW_PTE_GET_HCPHYS(pPt->a[iPte]),
                          ("PGM page and shadow PTE address conflict. GCPhysNestedPage=%RGp GCPhysPage=%RGp HCPhys=%RHp Shw=%RHp\n",
                           GCPhysNestedPage, WalkPt.GCPhys, PGM_PAGE_GET_HCPHYS(pPage), SHW_PTE_GET_HCPHYS(pPt->a[iPte])));
#  endif
                Log7Func(("Many3: %RGp iPte=%u ShwPte=%RX64\n", GCPhysNestedPage, iPte, SHW_PTE_LOG64(pPt->a[iPte])));
            }
        }
    }
    else
# endif /* PGM_SYNC_N_PAGES */
    {
        unsigned const iPte = (GCPhysNestedPage >> SHW_PT_SHIFT) & SHW_PT_MASK;
        PGM_BTH_NAME(NestedSyncPageWorker)(pVCpu, &pPt->a[iPte], GCPhysPage, pShwPage, iPte, pGstWalkAll->u.Ept.Pte);
        Log7Func(("4K: GCPhysPage=%RGp iPte=%u ShwPte=%08llx\n", GCPhysPage, iPte, SHW_PTE_LOG64(pPt->a[iPte])));
    }

    return VINF_SUCCESS;
}


/**
 * Sync a shadow page table for a nested-guest page table.
 *
 * The shadow page table is not present in the shadow PDE.
 *
 * Handles mapping conflicts.
 *
 * A precondition for this method is that the shadow PDE is not present.  The
 * caller must take the PGM lock before checking this and continue to hold it
 * when calling this method.
 *
 * @returns VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   GCPhysNestedPage    The nested-guest physical page address of the page
 *                              being synced.
 * @param   GCPhysPage          The guest-physical address of the page being synced.
 * @param   pGstWalkAll         The guest page table walk result.
 */
static int PGM_BTH_NAME(NestedSyncPT)(PVMCPUCC pVCpu, RTGCPHYS GCPhysNestedPage, RTGCPHYS GCPhysPage, PPGMPTWALKGST pGstWalkAll)
{
    PGM_A20_ASSERT_MASKED(pVCpu, GCPhysPage);
    Assert(!(GCPhysNestedPage & GUEST_PAGE_OFFSET_MASK));
    Assert(!(GCPhysPage & GUEST_PAGE_OFFSET_MASK));

    PVMCC    pVM   = pVCpu->CTX_SUFF(pVM);
    PPGMPOOL pPool = pVM->pgm.s.CTX_SUFF(pPool);

    Log7Func(("GCPhysNestedPage=%RGp GCPhysPage=%RGp\n", GCPhysNestedPage, GCPhysPage));

    PGM_LOCK_ASSERT_OWNER(pVM);
    STAM_PROFILE_START(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,SyncPT), a);

    PEPTPD         pPd;
    PEPTPDPT       pPdpt;
    unsigned const iPde = (GCPhysNestedPage >> EPT_PD_SHIFT) & EPT_PD_MASK;
    int rc = pgmShwGetNestedEPTPDPtr(pVCpu, GCPhysNestedPage, &pPdpt, &pPd, pGstWalkAll);
    if (RT_SUCCESS(rc))
    { /* likely */ }
    else
    {
        STAM_PROFILE_STOP(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,SyncPT), a);
        AssertRC(rc);
        return rc;
    }
    Assert(pPd);
    PSHWPDE pPde = &pPd->a[iPde];

    unsigned const iPdpt = (GCPhysNestedPage >> EPT_PDPT_SHIFT) & EPT_PDPT_MASK;
    PPGMPOOLPAGE pShwPde = pgmPoolGetPage(pPool, pPdpt->a[iPdpt].u & EPT_PDPTE_PG_MASK);
    Assert(pShwPde->enmKind == PGMPOOLKIND_EPT_PD_FOR_EPT_PD);

    SHWPDE Pde = *pPde;
    Assert(!SHW_PDE_IS_P(Pde));    /* We're only supposed to call SyncPT on PDE!P and conflicts. */

# ifdef PGM_WITH_LARGE_PAGES
    Assert(BTH_IS_NP_ACTIVE(pVM));

    /*
     * Check if the guest is mapping a 2M page.
     */
    if (pGstWalkAll->u.Ept.Pde.u & EPT_E_LEAF)
    {
        PPGMPAGE pPage;
        rc = pgmPhysGetPageEx(pVM, GCPhysPage & X86_PDE2M_PAE_PG_MASK, &pPage);
        AssertRCReturn(rc, rc);

        /* A20 is always enabled in VMX root and non-root operation. */
        Assert(PGM_A20_IS_ENABLED(pVCpu));

        /*
         * Check if we have or can get a 2M backing page here.
         */
        RTHCPHYS HCPhys = NIL_RTHCPHYS;
        if (PGM_PAGE_GET_PDE_TYPE(pPage) == PGM_PAGE_PDE_TYPE_PDE)
        {
            STAM_REL_COUNTER_INC(&pVM->pgm.s.StatLargePageReused);
            AssertRelease(PGM_PAGE_GET_STATE(pPage) == PGM_PAGE_STATE_ALLOCATED);
            HCPhys = PGM_PAGE_GET_HCPHYS(pPage);
        }
        else if (PGM_PAGE_GET_PDE_TYPE(pPage) == PGM_PAGE_PDE_TYPE_PDE_DISABLED)
        {
            /* Recheck the entire 2 MB range to see if we can use it again as a large page. */
            rc = pgmPhysRecheckLargePage(pVM, GCPhysPage, pPage);
            if (RT_SUCCESS(rc))
            {
                Assert(PGM_PAGE_GET_STATE(pPage) == PGM_PAGE_STATE_ALLOCATED);
                Assert(PGM_PAGE_GET_PDE_TYPE(pPage) == PGM_PAGE_PDE_TYPE_PDE);
                HCPhys = PGM_PAGE_GET_HCPHYS(pPage);
            }
        }
        else if (PGMIsUsingLargePages(pVM))
        {
            rc = pgmPhysAllocLargePage(pVM, GCPhysPage);
            if (RT_SUCCESS(rc))
            {
                Assert(PGM_PAGE_GET_STATE(pPage) == PGM_PAGE_STATE_ALLOCATED);
                Assert(PGM_PAGE_GET_PDE_TYPE(pPage) == PGM_PAGE_PDE_TYPE_PDE);
                HCPhys = PGM_PAGE_GET_HCPHYS(pPage);
            }
        }

        /*
         * If we have a 2M backing page, we can map the guest's 2M page right away.
         */
        uint64_t const fGstShwBigPdeFlags = (pGstWalkAll->u.Ept.Pde.u & pVCpu->pgm.s.fGstEptShadowedBigPdeMask)
                                          | EPT_E_MEMTYPE_WB | EPT_E_IGNORE_PAT;
        if (HCPhys != NIL_RTHCPHYS)
        {
            Pde.u = HCPhys | fGstShwBigPdeFlags;
            Assert(!(Pde.u & pVCpu->pgm.s.fGstEptMbzBigPdeMask));
            Assert(Pde.u & EPT_E_LEAF);
            SHW_PDE_ATOMIC_SET2(*pPde, Pde);

            /* Add a reference to the first page only. */
            PGM_BTH_NAME(SyncPageWorkerTrackAddref)(pVCpu, pShwPde, PGM_PAGE_GET_TRACKING(pPage), pPage, iPde);

            Assert(PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_WRITE_MONITORED);

            STAM_PROFILE_STOP(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,SyncPT), a);
            Log7Func(("GstPde=%RGp ShwPde=%RX64 [2M]\n", pGstWalkAll->u.Ept.Pde.u, Pde.u));
            return VINF_SUCCESS;
        }

        /*
         * We didn't get a perfect 2M fit. Split the 2M page into 4K pages.
         * The page ought not to be marked as a big (2M) page at this point.
         */
        Assert(PGM_PAGE_GET_PDE_TYPE(pPage) != PGM_PAGE_PDE_TYPE_PDE);

        /* Determine the right kind of large page to avoid incorrect cached entry reuse. */
        PGMPOOLACCESS enmAccess;
        {
            /*
             * Mode-based execute control for EPT not supported.
             *
             * However, Windows 10 with Hyper-V enabled sets the EPT_E_USER_EXECUTE bit but does
             * not enable "mode-based execute control for EPT" in the VT-x secondary VM-execution
             * controls. The CPU ignores this bit when the control isn't set. Hence, the assertion
             * below is commented out.
             */
            /* Assert(!(pGstWalkAll->u.Ept.Pde.u & EPT_E_USER_EXECUTE)); */
            Assert(!pVCpu->CTX_SUFF(pVM)->cpum.ro.GuestFeatures.fVmxModeBasedExecuteEpt);
            bool const fNoExecute = !(pGstWalkAll->u.Ept.Pde.u & EPT_E_EXECUTE);
            if (pGstWalkAll->u.Ept.Pde.u & EPT_E_WRITE)
                enmAccess = fNoExecute ? PGMPOOLACCESS_SUPERVISOR_RW_NX : PGMPOOLACCESS_SUPERVISOR_RW;
            else
                enmAccess = fNoExecute ? PGMPOOLACCESS_SUPERVISOR_R_NX  : PGMPOOLACCESS_SUPERVISOR_R;
        }

        /*
         * Allocate & map a 4K shadow table to cover the 2M guest page.
         */
        PPGMPOOLPAGE   pShwPage;
        RTGCPHYS const GCPhysPt = pGstWalkAll->u.Ept.Pde.u & EPT_PDE2M_PG_MASK;
        rc = pgmPoolAlloc(pVM, GCPhysPt, PGMPOOLKIND_EPT_PT_FOR_EPT_2MB, enmAccess, PGM_A20_IS_ENABLED(pVCpu),
                          pShwPde->idx, iPde, false /*fLockPage*/, &pShwPage);
        if (   rc == VINF_SUCCESS
            || rc == VINF_PGM_CACHED_PAGE)
        { /* likely */ }
        else
        {
           STAM_PROFILE_STOP(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,SyncPT), a);
           AssertMsgFailedReturn(("rc=%Rrc\n", rc), RT_FAILURE_NP(rc) ? rc : VERR_IPE_UNEXPECTED_INFO_STATUS);
        }

        PSHWPT pPt = (PSHWPT)PGMPOOL_PAGE_2_PTR_V2(pVM, pVCpu, pShwPage);
        Assert(pPt);
        Assert(PGMPOOL_PAGE_IS_NESTED(pShwPage));
        if (rc == VINF_SUCCESS)
        {
            /* The 4K PTEs shall inherit the flags of the 2M PDE page sans the leaf bit. */
            uint64_t const fGstShwPteFlags = fGstShwBigPdeFlags & ~EPT_E_LEAF;

            /* Sync each 4K pages in the 2M range. */
            for (unsigned iPte = 0; iPte < RT_ELEMENTS(pPt->a); iPte++)
            {
                RTGCPHYS const GCPhysSubPage = GCPhysPt | (iPte << GUEST_PAGE_SHIFT);
                SLATPTE GstSlatPte;
                GstSlatPte.u = GCPhysSubPage | fGstShwPteFlags;
                Assert(!(GstSlatPte.u & pVCpu->pgm.s.fGstEptMbzPteMask));
                PGM_BTH_NAME(NestedSyncPageWorker)(pVCpu, &pPt->a[iPte], GCPhysSubPage, pShwPage, iPte, GstSlatPte);
                Log7Func(("GstPte=%RGp ShwPte=%RX64 iPte=%u [2M->4K]\n", pGstWalkAll->u.Ept.Pte, pPt->a[iPte].u, iPte));
                if (RT_UNLIKELY(VM_FF_IS_SET(pVM, VM_FF_PGM_NO_MEMORY)))
                    break;
            }
        }
        else
        {
            Assert(rc == VINF_PGM_CACHED_PAGE);
#  if defined(VBOX_STRICT) && defined(DEBUG_ramshankar)
            /* Paranoia - Verify address of each of the subpages are what they should be. */
            RTGCPHYS GCPhysSubPage = GCPhysPt;
            for (unsigned iPte = 0; iPte < RT_ELEMENTS(pPt->a); iPte++, GCPhysSubPage += GUEST_PAGE_SIZE)
            {
                PPGMPAGE pSubPage;
                rc = pgmPhysGetPageEx(pVM, GCPhysSubPage, &pSubPage);
                AssertRC(rc);
                AssertMsg(   PGM_PAGE_GET_HCPHYS(pSubPage) == SHW_PTE_GET_HCPHYS(pPt->a[iPte])
                          || !SHW_PTE_IS_P(pPt->a[iPte]),
                          ("PGM 2M page and shadow PTE conflict. GCPhysSubPage=%RGp Page=%RHp Shw=%RHp\n",
                           GCPhysSubPage, PGM_PAGE_GET_HCPHYS(pSubPage), SHW_PTE_GET_HCPHYS(pPt->a[iPte])));
            }
#  endif
            rc = VINF_SUCCESS; /* Cached entry; assume it's still fully valid. */
        }

        /* Save the new PDE. */
        uint64_t const fShwPdeFlags = pGstWalkAll->u.Ept.Pde.u & pVCpu->pgm.s.fGstEptShadowedPdeMask;
        Pde.u = pShwPage->Core.Key | fShwPdeFlags;
        Assert(!(Pde.u & EPT_E_LEAF));
        Assert(!(Pde.u & pVCpu->pgm.s.fGstEptMbzPdeMask));
        SHW_PDE_ATOMIC_SET2(*pPde, Pde);
        STAM_PROFILE_STOP(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,SyncPT), a);
        Log7Func(("GstPde=%RGp ShwPde=%RX64 iPde=%u\n", pGstWalkAll->u.Ept.Pde.u, pPde->u, iPde));
        return rc;
    }
# endif /* PGM_WITH_LARGE_PAGES */

    /*
     * Allocate & map the shadow page table.
     */
    PSHWPT       pPt;
    PPGMPOOLPAGE pShwPage;

    RTGCPHYS const GCPhysPt = pGstWalkAll->u.Ept.Pde.u & EPT_PDE_PG_MASK;
    rc = pgmPoolAlloc(pVM, GCPhysPt, PGMPOOLKIND_EPT_PT_FOR_EPT_PT, PGMPOOLACCESS_DONTCARE,
                      PGM_A20_IS_ENABLED(pVCpu), pShwPde->idx, iPde, false /*fLockPage*/, &pShwPage);
    if (   rc == VINF_SUCCESS
        || rc == VINF_PGM_CACHED_PAGE)
    { /* likely */ }
    else
    {
       STAM_PROFILE_STOP(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,SyncPT), a);
       AssertMsgFailedReturn(("rc=%Rrc\n", rc), RT_FAILURE_NP(rc) ? rc : VERR_IPE_UNEXPECTED_INFO_STATUS);
    }

    pPt = (PSHWPT)PGMPOOL_PAGE_2_PTR_V2(pVM, pVCpu, pShwPage);
    Assert(pPt);
    Assert(PGMPOOL_PAGE_IS_NESTED(pShwPage));

    if (rc == VINF_SUCCESS)
    {
        /* Sync the page we've already translated through SLAT. */
        const unsigned iPte = (GCPhysNestedPage >> SHW_PT_SHIFT) & SHW_PT_MASK;
        PGM_BTH_NAME(NestedSyncPageWorker)(pVCpu, &pPt->a[iPte], GCPhysPage, pShwPage, iPte, pGstWalkAll->u.Ept.Pte);
        Log7Func(("GstPte=%RGp ShwPte=%RX64 iPte=%u\n", pGstWalkAll->u.Ept.Pte.u, pPt->a[iPte].u, iPte));

        /* Sync the rest of page table (expensive but might be cheaper than nested-guest VM-exits in hardware). */
        for (unsigned iPteCur = 0; iPteCur < RT_ELEMENTS(pPt->a); iPteCur++)
        {
            if (iPteCur != iPte)
            {
                PGMPTWALKGST GstWalkPt;
                PGMPTWALK    WalkPt;
                GCPhysNestedPage &= ~(SHW_PT_MASK << SHW_PT_SHIFT);
                GCPhysNestedPage |= (iPteCur << GUEST_PAGE_SHIFT);
                int const rc2 = pgmGstSlatWalk(pVCpu, GCPhysNestedPage, false /*fIsLinearAddrValid*/, 0 /*GCPtrNested*/,
                                               &WalkPt, &GstWalkPt);
                if (RT_SUCCESS(rc2))
                {
                    PGM_BTH_NAME(NestedSyncPageWorker)(pVCpu, &pPt->a[iPteCur], WalkPt.GCPhys, pShwPage, iPteCur,
                                                       GstWalkPt.u.Ept.Pte);
                    Log7Func(("GstPte=%RGp ShwPte=%RX64 iPte=%u\n", GstWalkPt.u.Ept.Pte.u, pPt->a[iPteCur].u, iPteCur));
                }
                else
                {
                    /*
                     * This could be MMIO pages reserved by the nested-hypevisor or genuinely not-present pages.
                     * Ensure the shadow tables entry is not-present.
                     */
                    /** @todo We currently don't configure these to cause EPT misconfigs but rather trap
                     *        them using EPT violations and walk the guest EPT tables to determine
                     *        whether they are EPT misconfigs VM-exits for the nested-hypervisor. We
                     *        could optimize this by using a specific combination of reserved bits
                     *        which we could immediately identify as EPT misconfigs of the
                     *        nested-hypervisor without having to walk its EPT tables. However, tracking
                     *        non-present entries might be tricky...
                     */
                    AssertMsg(!pPt->a[iPteCur].u, ("%RX64\n", pPt->a[iPteCur].u));
                }
                if (RT_UNLIKELY(VM_FF_IS_SET(pVM, VM_FF_PGM_NO_MEMORY)))
                    break;
            }
        }
    }
    else
    {
        Assert(rc == VINF_PGM_CACHED_PAGE);
# if defined(VBOX_STRICT) && defined(DEBUG_ramshankar)
        /* Paranoia - Verify address of the page is what it should be. */
        PPGMPAGE pPage;
        rc = pgmPhysGetPageEx(pVM, GCPhysPage, &pPage);
        AssertRC(rc);
        const unsigned iPte = (GCPhysNestedPage >> SHW_PT_SHIFT) & SHW_PT_MASK;
        AssertMsg(PGM_PAGE_GET_HCPHYS(pPage) == SHW_PTE_GET_HCPHYS(pPt->a[iPte]) || !SHW_PTE_IS_P(pPt->a[iPte]),
                  ("PGM page and shadow PTE address conflict. GCPhysNestedPage=%RGp GCPhysPage=%RGp Page=%RHp Shw=%RHp\n",
                   GCPhysNestedPage, GCPhysPage, PGM_PAGE_GET_HCPHYS(pPage), SHW_PTE_GET_HCPHYS(pPt->a[iPte])));
        Log7Func(("GstPte=%RGp ShwPte=%RX64 iPte=%u [cache]\n", pGstWalkAll->u.Ept.Pte.u, pPt->a[iPte].u,  iPte));
# endif
        rc = VINF_SUCCESS; /* Cached entry; assume it's still fully valid. */
    }

    /* Save the new PDE. */
    uint64_t const fShwPdeFlags = pGstWalkAll->u.Ept.Pde.u & pVCpu->pgm.s.fGstEptShadowedPdeMask;
    Assert(!(pGstWalkAll->u.Ept.Pde.u & EPT_E_LEAF));
    Assert(!(pGstWalkAll->u.Ept.Pde.u & pVCpu->pgm.s.fGstEptMbzPdeMask));
    Pde.u = pShwPage->Core.Key | fShwPdeFlags;
    SHW_PDE_ATOMIC_SET2(*pPde, Pde);
    Log7Func(("GstPde=%RGp ShwPde=%RX64 iPde=%u\n", pGstWalkAll->u.Ept.Pde.u, pPde->u, iPde));

    STAM_PROFILE_STOP(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,SyncPT), a);
    return rc;
}

#endif  /* !IN_RING3 && VBOX_WITH_NESTED_HWVIRT_VMX_EPT && PGM_SHW_TYPE == PGM_TYPE_EPT*/
#if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE) && PGM_SHW_TYPE != PGM_TYPE_NONE

/**
 * Handle dirty bit tracking faults.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   uErr        Page fault error code.
 * @param   pPdeSrc     Guest page directory entry.
 * @param   pPdeDst     Shadow page directory entry.
 * @param   GCPtrPage   Guest context page address.
 */
static int PGM_BTH_NAME(CheckDirtyPageFault)(PVMCPUCC pVCpu, uint32_t uErr, PSHWPDE pPdeDst, GSTPDE const *pPdeSrc,
                                             RTGCPTR GCPtrPage)
{
    PVMCC       pVM   = pVCpu->CTX_SUFF(pVM);
    PPGMPOOL    pPool = pVM->pgm.s.CTX_SUFF(pPool);
    NOREF(uErr);

    PGM_LOCK_ASSERT_OWNER(pVM);

    /*
     * Handle big page.
     */
    if ((pPdeSrc->u & X86_PDE_PS) && GST_IS_PSE_ACTIVE(pVCpu))
    {
        if ((pPdeDst->u & (X86_PDE_P | PGM_PDFLAGS_TRACK_DIRTY)) == (X86_PDE_P | PGM_PDFLAGS_TRACK_DIRTY))
        {
            STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,DirtyPageTrap));
            Assert(pPdeSrc->u & X86_PDE_RW);

            /* Note: No need to invalidate this entry on other VCPUs as a stale TLB entry will not harm; write access will simply
             *       fault again and take this path to only invalidate the entry (see below). */
            SHWPDE PdeDst = *pPdeDst;
            PdeDst.u &= ~(SHWUINT)PGM_PDFLAGS_TRACK_DIRTY;
            PdeDst.u |= X86_PDE_RW | X86_PDE_A;
            SHW_PDE_ATOMIC_SET2(*pPdeDst, PdeDst);
            PGM_INVL_BIG_PG(pVCpu, GCPtrPage);
            return VINF_PGM_HANDLED_DIRTY_BIT_FAULT;    /* restarts the instruction. */
        }

# ifdef IN_RING0
        /* Check for stale TLB entry; only applies to the SMP guest case. */
        if (   pVM->cCpus > 1
            && (pPdeDst->u & (X86_PDE_P | X86_PDE_RW | X86_PDE_A)) == (X86_PDE_P | X86_PDE_RW | X86_PDE_A))
        {
            PPGMPOOLPAGE    pShwPage = pgmPoolGetPage(pPool, pPdeDst->u & SHW_PDE_PG_MASK);
            if (pShwPage)
            {
                PSHWPT      pPTDst   = (PSHWPT)PGMPOOL_PAGE_2_PTR_V2(pVM, pVCpu, pShwPage);
                PSHWPTE     pPteDst  = &pPTDst->a[(GCPtrPage >> SHW_PT_SHIFT) & SHW_PT_MASK];
                if (SHW_PTE_IS_P_RW(*pPteDst))
                {
                    /* Stale TLB entry. */
                    STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,DirtyPageStale));
                    PGM_INVL_PG(pVCpu, GCPtrPage);
                    return VINF_PGM_HANDLED_DIRTY_BIT_FAULT;    /* restarts the instruction. */
                }
            }
        }
# endif /* IN_RING0 */
        return VINF_PGM_NO_DIRTY_BIT_TRACKING;
    }

    /*
     * Map the guest page table.
     */
    PGSTPT pPTSrc;
    int rc = PGM_GCPHYS_2_PTR_V2(pVM, pVCpu, GST_GET_PDE_GCPHYS(*pPdeSrc), &pPTSrc);
    AssertRCReturn(rc, rc);

    if (SHW_PDE_IS_P(*pPdeDst))
    {
        GSTPTE const  *pPteSrc = &pPTSrc->a[(GCPtrPage >> GST_PT_SHIFT) & GST_PT_MASK];
        const GSTPTE   PteSrc  = *pPteSrc;

        /*
         * Map shadow page table.
         */
        PPGMPOOLPAGE    pShwPage = pgmPoolGetPage(pPool, pPdeDst->u & SHW_PDE_PG_MASK);
        if (pShwPage)
        {
            PSHWPT      pPTDst   = (PSHWPT)PGMPOOL_PAGE_2_PTR_V2(pVM, pVCpu, pShwPage);
            PSHWPTE     pPteDst  = &pPTDst->a[(GCPtrPage >> SHW_PT_SHIFT) & SHW_PT_MASK];
            if (SHW_PTE_IS_P(*pPteDst))    /** @todo Optimize accessed bit emulation? */
            {
                if (SHW_PTE_IS_TRACK_DIRTY(*pPteDst))
                {
                    PPGMPAGE pPage  = pgmPhysGetPage(pVM, GST_GET_PTE_GCPHYS(PteSrc));
                    SHWPTE   PteDst = *pPteDst;

                    LogFlow(("DIRTY page trap addr=%RGv\n", GCPtrPage));
                    STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,DirtyPageTrap));

                    Assert(PteSrc.u & X86_PTE_RW);

                    /* Note: No need to invalidate this entry on other VCPUs as a stale TLB
                     *       entry will not harm; write access will simply fault again and
                     *       take this path to only invalidate the entry.
                     */
                    if (RT_LIKELY(pPage))
                    {
                        if (PGM_PAGE_HAS_ACTIVE_HANDLERS(pPage) && !PGM_PAGE_IS_HNDL_PHYS_NOT_IN_HM(pPage))
                        {
                            //AssertMsgFailed(("%R[pgmpage] - we don't set PGM_PTFLAGS_TRACK_DIRTY for these pages\n", pPage));
                            Assert(!PGM_PAGE_HAS_ACTIVE_ALL_HANDLERS(pPage));
                            /* Assuming write handlers here as the PTE is present (otherwise we wouldn't be here). */
                            SHW_PTE_SET_RO(PteDst);
                        }
                        else
                        {
                            if (   PGM_PAGE_GET_STATE(pPage) == PGM_PAGE_STATE_WRITE_MONITORED
                                && PGM_PAGE_GET_TYPE(pPage)  == PGMPAGETYPE_RAM)
                            {
                                rc = pgmPhysPageMakeWritable(pVM, pPage, GST_GET_PTE_GCPHYS(PteSrc));
                                AssertRC(rc);
                            }
                            if (PGM_PAGE_GET_STATE(pPage) == PGM_PAGE_STATE_ALLOCATED)
                                SHW_PTE_SET_RW(PteDst);
                            else
                            {
                                /* Still applies to shared pages. */
                                Assert(!PGM_PAGE_IS_ZERO(pPage));
                                SHW_PTE_SET_RO(PteDst);
                            }
                        }
                    }
                    else
                        SHW_PTE_SET_RW(PteDst);  /** @todo r=bird: This doesn't make sense to me. */

                    SHW_PTE_SET(PteDst, (SHW_PTE_GET_U(PteDst) | X86_PTE_D | X86_PTE_A) & ~(uint64_t)PGM_PTFLAGS_TRACK_DIRTY);
                    SHW_PTE_ATOMIC_SET2(*pPteDst, PteDst);
                    PGM_INVL_PG(pVCpu, GCPtrPage);
                    return VINF_PGM_HANDLED_DIRTY_BIT_FAULT;    /* restarts the instruction. */
                }

# ifdef IN_RING0
                /* Check for stale TLB entry; only applies to the SMP guest case. */
                if (    pVM->cCpus > 1
                    &&  SHW_PTE_IS_RW(*pPteDst)
                    &&  SHW_PTE_IS_A(*pPteDst))
                {
                    /* Stale TLB entry. */
                    STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,DirtyPageStale));
                    PGM_INVL_PG(pVCpu, GCPtrPage);
                    return VINF_PGM_HANDLED_DIRTY_BIT_FAULT;    /* restarts the instruction. */
                }
# endif
            }
        }
        else
            AssertMsgFailed(("pgmPoolGetPageByHCPhys %RGp failed!\n", pPdeDst->u & SHW_PDE_PG_MASK));
    }

    return VINF_PGM_NO_DIRTY_BIT_TRACKING;
}

#endif /* PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE) && PGM_SHW_TYPE != PGM_TYPE_NONE */

/**
 * Sync a shadow page table.
 *
 * The shadow page table is not present in the shadow PDE.
 *
 * Handles mapping conflicts.
 *
 * This is called by VerifyAccessSyncPage, PrefetchPage, InvalidatePage (on
 * conflict), and Trap0eHandler.
 *
 * A precondition for this method is that the shadow PDE is not present.  The
 * caller must take the PGM lock before checking this and continue to hold it
 * when calling this method.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   iPDSrc      Page directory index.
 * @param   pPDSrc      Source page directory (i.e. Guest OS page directory).
 *                      Assume this is a temporary mapping.
 * @param   GCPtrPage   GC Pointer of the page that caused the fault
 */
static int PGM_BTH_NAME(SyncPT)(PVMCPUCC pVCpu, unsigned iPDSrc, PGSTPD pPDSrc, RTGCPTR GCPtrPage)
{
    PVMCC       pVM   = pVCpu->CTX_SUFF(pVM);
    PPGMPOOL    pPool = pVM->pgm.s.CTX_SUFF(pPool); NOREF(pPool);

#if 0 /* rarely useful; leave for debugging. */
    STAM_COUNTER_INC(&pVCpu->pgm.s.StatSyncPtPD[iPDSrc]);
#endif
    LogFlow(("SyncPT: GCPtrPage=%RGv\n", GCPtrPage)); RT_NOREF_PV(GCPtrPage);

    PGM_LOCK_ASSERT_OWNER(pVM);

#if (   PGM_GST_TYPE == PGM_TYPE_32BIT \
     || PGM_GST_TYPE == PGM_TYPE_PAE \
     || PGM_GST_TYPE == PGM_TYPE_AMD64) \
 && !PGM_TYPE_IS_NESTED_OR_EPT(PGM_SHW_TYPE) \
 && PGM_SHW_TYPE != PGM_TYPE_NONE
    int             rc       = VINF_SUCCESS;

    STAM_PROFILE_START(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,SyncPT), a);

    /*
     * Some input validation first.
     */
    AssertMsg(iPDSrc == ((GCPtrPage >> GST_PD_SHIFT) & GST_PD_MASK), ("iPDSrc=%x GCPtrPage=%RGv\n", iPDSrc, GCPtrPage));

    /*
     * Get the relevant shadow PDE entry.
     */
# if PGM_SHW_TYPE == PGM_TYPE_32BIT
    const unsigned  iPDDst   = GCPtrPage >> SHW_PD_SHIFT;
    PSHWPDE         pPdeDst  = pgmShwGet32BitPDEPtr(pVCpu, GCPtrPage);
    AssertReturn(pPdeDst, VERR_INTERNAL_ERROR_3);

    /* Fetch the pgm pool shadow descriptor. */
    PPGMPOOLPAGE    pShwPde  = pVCpu->pgm.s.CTX_SUFF(pShwPageCR3);
    Assert(pShwPde);

# elif PGM_SHW_TYPE == PGM_TYPE_PAE
    const unsigned  iPDDst   = (GCPtrPage >> SHW_PD_SHIFT) & SHW_PD_MASK;
    PPGMPOOLPAGE    pShwPde  = NULL;
    PX86PDPAE       pPDDst;
    PSHWPDE         pPdeDst;

    /* Fetch the pgm pool shadow descriptor. */
    rc = pgmShwGetPaePoolPagePD(pVCpu, GCPtrPage, &pShwPde);
    AssertRCSuccessReturn(rc, rc);
    Assert(pShwPde);

    pPDDst  = (PX86PDPAE)PGMPOOL_PAGE_2_PTR_V2(pVM, pVCpu, pShwPde);
    pPdeDst = &pPDDst->a[iPDDst];

# elif PGM_SHW_TYPE == PGM_TYPE_AMD64
    const unsigned  iPdpt    = (GCPtrPage >> X86_PDPT_SHIFT) & X86_PDPT_MASK_AMD64;
    const unsigned  iPDDst   = (GCPtrPage >> SHW_PD_SHIFT) & SHW_PD_MASK;
    PX86PDPAE       pPDDst   = NULL;            /* initialized to shut up gcc */
    PX86PDPT        pPdptDst = NULL;            /* initialized to shut up gcc */
    rc = pgmShwGetLongModePDPtr(pVCpu, GCPtrPage, NULL, &pPdptDst, &pPDDst);
    AssertRCSuccessReturn(rc, rc);
    Assert(pPDDst);
    PSHWPDE         pPdeDst  = &pPDDst->a[iPDDst];

# endif
    SHWPDE          PdeDst   = *pPdeDst;

# if PGM_GST_TYPE == PGM_TYPE_AMD64
    /* Fetch the pgm pool shadow descriptor. */
    PPGMPOOLPAGE    pShwPde  = pgmPoolGetPage(pPool, pPdptDst->a[iPdpt].u & X86_PDPE_PG_MASK);
    Assert(pShwPde);
# endif

    Assert(!SHW_PDE_IS_P(PdeDst)); /* We're only supposed to call SyncPT on PDE!P.*/

    /*
     * Sync the page directory entry.
     */
    GSTPDE      PdeSrc = pPDSrc->a[iPDSrc];
    const bool  fPageTable = !(PdeSrc.u & X86_PDE_PS) || !GST_IS_PSE_ACTIVE(pVCpu);
    if (   (PdeSrc.u & X86_PDE_P)
        && (fPageTable ? GST_IS_PDE_VALID(pVCpu, PdeSrc) : GST_IS_BIG_PDE_VALID(pVCpu, PdeSrc)) )
    {
        /*
         * Allocate & map the page table.
         */
        PSHWPT          pPTDst;
        PPGMPOOLPAGE    pShwPage;
        RTGCPHYS        GCPhys;
        if (fPageTable)
        {
            GCPhys = GST_GET_PDE_GCPHYS(PdeSrc);
# if PGM_SHW_TYPE == PGM_TYPE_PAE && PGM_GST_TYPE == PGM_TYPE_32BIT
            /* Select the right PDE as we're emulating a 4kb page table with 2 shadow page tables. */
            GCPhys = PGM_A20_APPLY(pVCpu, GCPhys | ((iPDDst & 1) * (GUEST_PAGE_SIZE / 2)));
# endif
            rc = pgmPoolAlloc(pVM, GCPhys, BTH_PGMPOOLKIND_PT_FOR_PT, PGMPOOLACCESS_DONTCARE, PGM_A20_IS_ENABLED(pVCpu),
                              pShwPde->idx, iPDDst, false /*fLockPage*/,
                              &pShwPage);
        }
        else
        {
            PGMPOOLACCESS enmAccess;
# if PGM_WITH_NX(PGM_GST_TYPE, PGM_SHW_TYPE)
            const bool  fNoExecute = (PdeSrc.u & X86_PDE_PAE_NX) && GST_IS_NX_ACTIVE(pVCpu);
# else
            const bool  fNoExecute = false;
# endif

            GCPhys = GST_GET_BIG_PDE_GCPHYS(pVM, PdeSrc);
# if PGM_SHW_TYPE == PGM_TYPE_PAE && PGM_GST_TYPE == PGM_TYPE_32BIT
            /* Select the right PDE as we're emulating a 4MB page directory with two 2 MB shadow PDEs.*/
            GCPhys = PGM_A20_APPLY(pVCpu, GCPhys | (GCPtrPage & (1 << X86_PD_PAE_SHIFT)));
# endif
            /* Determine the right kind of large page to avoid incorrect cached entry reuse. */
            if (PdeSrc.u & X86_PDE_US)
            {
                if (PdeSrc.u & X86_PDE_RW)
                    enmAccess = (fNoExecute) ? PGMPOOLACCESS_USER_RW_NX : PGMPOOLACCESS_USER_RW;
                else
                    enmAccess = (fNoExecute) ? PGMPOOLACCESS_USER_R_NX  : PGMPOOLACCESS_USER_R;
            }
            else
            {
                if (PdeSrc.u & X86_PDE_RW)
                    enmAccess = (fNoExecute) ? PGMPOOLACCESS_SUPERVISOR_RW_NX : PGMPOOLACCESS_SUPERVISOR_RW;
                else
                    enmAccess = (fNoExecute) ? PGMPOOLACCESS_SUPERVISOR_R_NX  : PGMPOOLACCESS_SUPERVISOR_R;
            }
            rc = pgmPoolAlloc(pVM, GCPhys, BTH_PGMPOOLKIND_PT_FOR_BIG, enmAccess, PGM_A20_IS_ENABLED(pVCpu),
                              pShwPde->idx, iPDDst, false /*fLockPage*/,
                              &pShwPage);
        }
        if (rc == VINF_SUCCESS)
            pPTDst = (PSHWPT)PGMPOOL_PAGE_2_PTR_V2(pVM, pVCpu, pShwPage);
        else if (rc == VINF_PGM_CACHED_PAGE)
        {
            /*
             * The PT was cached, just hook it up.
             */
            if (fPageTable)
                PdeDst.u = pShwPage->Core.Key | GST_GET_PDE_SHW_FLAGS(pVCpu, PdeSrc);
            else
            {
                PdeDst.u = pShwPage->Core.Key | GST_GET_BIG_PDE_SHW_FLAGS(pVCpu, PdeSrc);
                /* (see explanation and assumptions further down.) */
                if ((PdeSrc.u & (X86_PDE_RW | X86_PDE4M_D)) == X86_PDE_RW)
                {
                    STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,DirtyPageBig));
                    PdeDst.u |= PGM_PDFLAGS_TRACK_DIRTY;
                    PdeDst.u &= ~(SHWUINT)X86_PDE_RW;
                }
            }
            SHW_PDE_ATOMIC_SET2(*pPdeDst, PdeDst);
            PGM_DYNMAP_UNUSED_HINT(pVCpu, pPdeDst);
            return VINF_SUCCESS;
        }
        else
            AssertMsgFailedReturn(("rc=%Rrc\n", rc), RT_FAILURE_NP(rc) ? rc : VERR_IPE_UNEXPECTED_INFO_STATUS);
        /** @todo Why do we bother preserving X86_PDE_AVL_MASK here?
         * Both PGM_PDFLAGS_MAPPING and PGM_PDFLAGS_TRACK_DIRTY should be
         * irrelevant at this point. */
        PdeDst.u &= X86_PDE_AVL_MASK;
        PdeDst.u |= pShwPage->Core.Key;

        /*
         * Page directory has been accessed (this is a fault situation, remember).
         */
        /** @todo
         * Well, when the caller is PrefetchPage or InvalidatePage is isn't a
         * fault situation.  What's more, the Trap0eHandler has already set the
         * accessed bit.  So, it's actually just VerifyAccessSyncPage which
         * might need setting the accessed flag.
         *
         * The best idea is to leave this change to the caller and add an
         * assertion that it's set already. */
        pPDSrc->a[iPDSrc].u |= X86_PDE_A;
        if (fPageTable)
        {
            /*
             * Page table - 4KB.
             *
             * Sync all or just a few entries depending on PGM_SYNC_N_PAGES.
             */
            Log2(("SyncPT:   4K  %RGv PdeSrc:{P=%d RW=%d U=%d raw=%08llx}\n",
                  GCPtrPage, PdeSrc.u & X86_PTE_P, !!(PdeSrc.u & X86_PTE_RW), !!(PdeSrc.u & X86_PDE_US), (uint64_t)PdeSrc.u));
            PGSTPT pPTSrc;
            rc = PGM_GCPHYS_2_PTR(pVM, GST_GET_PDE_GCPHYS(PdeSrc), &pPTSrc);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Start by syncing the page directory entry so CSAM's TLB trick works.
                 */
                PdeDst.u = (PdeDst.u & (SHW_PDE_PG_MASK | X86_PDE_AVL_MASK))
                         | GST_GET_PDE_SHW_FLAGS(pVCpu, PdeSrc);
                SHW_PDE_ATOMIC_SET2(*pPdeDst, PdeDst);
                PGM_DYNMAP_UNUSED_HINT(pVCpu, pPdeDst);

                /*
                 * Directory/page user or supervisor privilege: (same goes for read/write)
                 *
                 * Directory    Page    Combined
                 * U/S          U/S     U/S
                 *  0            0       0
                 *  0            1       0
                 *  1            0       0
                 *  1            1       1
                 *
                 * Simple AND operation. Table listed for completeness.
                 *
                 */
                STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,SyncPT4K));
# ifdef PGM_SYNC_N_PAGES
                unsigned        iPTBase   = (GCPtrPage >> SHW_PT_SHIFT) & SHW_PT_MASK;
                unsigned        iPTDst    = iPTBase;
                const unsigned  iPTDstEnd = RT_MIN(iPTDst + PGM_SYNC_NR_PAGES / 2, RT_ELEMENTS(pPTDst->a));
                if (iPTDst <= PGM_SYNC_NR_PAGES / 2)
                    iPTDst = 0;
                else
                    iPTDst -= PGM_SYNC_NR_PAGES / 2;
# else /* !PGM_SYNC_N_PAGES */
                unsigned        iPTDst    = 0;
                const unsigned  iPTDstEnd = RT_ELEMENTS(pPTDst->a);
# endif /* !PGM_SYNC_N_PAGES */
                RTGCPTR         GCPtrCur  = (GCPtrPage & ~(RTGCPTR)((1 << SHW_PD_SHIFT) - 1))
                                          | ((RTGCPTR)iPTDst << GUEST_PAGE_SHIFT);
# if PGM_SHW_TYPE == PGM_TYPE_PAE && PGM_GST_TYPE == PGM_TYPE_32BIT
                /* Select the right PDE as we're emulating a 4kb page table with 2 shadow page tables. */
                const unsigned  offPTSrc  = ((GCPtrPage >> SHW_PD_SHIFT) & 1) * 512;
# else
                const unsigned  offPTSrc  = 0;
# endif
                for (; iPTDst < iPTDstEnd; iPTDst++, GCPtrCur += GUEST_PAGE_SIZE)
                {
                    const unsigned iPTSrc = iPTDst + offPTSrc;
                    const GSTPTE   PteSrc = pPTSrc->a[iPTSrc];
                    if (PteSrc.u & X86_PTE_P)
                    {
                        PGM_BTH_NAME(SyncPageWorker)(pVCpu, &pPTDst->a[iPTDst], PdeSrc, PteSrc, pShwPage, iPTDst);
                        Log2(("SyncPT:   4K+ %RGv PteSrc:{P=%d RW=%d U=%d raw=%08llx}%s dst.raw=%08llx iPTSrc=%x PdeSrc.u=%x physpte=%RGp\n",
                              GCPtrCur,
                              PteSrc.u & X86_PTE_P,
                              !!(PteSrc.u & PdeSrc.u & X86_PTE_RW),
                              !!(PteSrc.u & PdeSrc.u & X86_PTE_US),
                              (uint64_t)PteSrc.u,
                              SHW_PTE_IS_TRACK_DIRTY(pPTDst->a[iPTDst]) ? " Track-Dirty" : "", SHW_PTE_LOG64(pPTDst->a[iPTDst]), iPTSrc, PdeSrc.au32[0],
                              (RTGCPHYS)(GST_GET_PDE_GCPHYS(PdeSrc) + iPTSrc*sizeof(PteSrc)) ));
                    }
                    /* else: the page table was cleared by the pool */
                } /* for PTEs */
            }
        }
        else
        {
            /*
             * Big page - 2/4MB.
             *
             * We'll walk the ram range list in parallel and optimize lookups.
             * We will only sync one shadow page table at a time.
             */
            STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,SyncPT4M));

            /**
             * @todo It might be more efficient to sync only a part of the 4MB
             *       page (similar to what we do for 4KB PDs).
             */

            /*
             * Start by syncing the page directory entry.
             */
            PdeDst.u = (PdeDst.u & (SHW_PDE_PG_MASK | (X86_PDE_AVL_MASK & ~PGM_PDFLAGS_TRACK_DIRTY)))
                     | GST_GET_BIG_PDE_SHW_FLAGS(pVCpu, PdeSrc);

            /*
             * If the page is not flagged as dirty and is writable, then make it read-only
             * at PD level, so we can set the dirty bit when the page is modified.
             *
             * ASSUMES that page access handlers are implemented on page table entry level.
             *      Thus we will first catch the dirty access and set PDE.D and restart. If
             *      there is an access handler, we'll trap again and let it work on the problem.
             */
            /** @todo move the above stuff to a section in the PGM documentation. */
            Assert(!(PdeDst.u & PGM_PDFLAGS_TRACK_DIRTY));
            if ((PdeSrc.u & (X86_PDE_RW | X86_PDE4M_D)) == X86_PDE_RW)
            {
                STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,DirtyPageBig));
                PdeDst.u |= PGM_PDFLAGS_TRACK_DIRTY;
                PdeDst.u &= ~(SHWUINT)X86_PDE_RW;
            }
            SHW_PDE_ATOMIC_SET2(*pPdeDst, PdeDst);
            PGM_DYNMAP_UNUSED_HINT(pVCpu, pPdeDst);

            /*
             * Fill the shadow page table.
             */
            /* Get address and flags from the source PDE. */
            SHWPTE PteDstBase;
            SHW_PTE_SET(PteDstBase, GST_GET_BIG_PDE_SHW_FLAGS_4_PTE(pVCpu, PdeSrc));

            /* Loop thru the entries in the shadow PT. */
            const RTGCPTR   GCPtr  = (GCPtrPage >> SHW_PD_SHIFT) << SHW_PD_SHIFT; NOREF(GCPtr);
            Log2(("SyncPT:   BIG %RGv PdeSrc:{P=%d RW=%d U=%d raw=%08llx} Shw=%RGv GCPhys=%RGp %s\n",
                  GCPtrPage, PdeSrc.u & X86_PDE_P, !!(PdeSrc.u & X86_PDE_RW), !!(PdeSrc.u & X86_PDE_US), (uint64_t)PdeSrc.u, GCPtr,
                  GCPhys, PdeDst.u & PGM_PDFLAGS_TRACK_DIRTY ? " Track-Dirty" : ""));
            unsigned        iPTDst = 0;
            while (     iPTDst < RT_ELEMENTS(pPTDst->a)
                   &&   !VM_FF_IS_SET(pVM, VM_FF_PGM_NO_MEMORY))
            {
                PPGMRAMRANGE const pRam   = pgmPhysGetRangeAtOrAbove(pVM, GCPhys);
                if (pRam && GCPhys >= pRam->GCPhys)
                {
# ifndef PGM_WITH_A20
                    unsigned iHCPage = (GCPhys - pRam->GCPhys) >> GUEST_PAGE_SHIFT;
# endif
                    do
                    {
                        /* Make shadow PTE. */
# ifdef PGM_WITH_A20
                        PPGMPAGE    pPage = &pRam->aPages[(GCPhys - pRam->GCPhys) >> GUEST_PAGE_SHIFT];
# else
                        PPGMPAGE    pPage = &pRam->aPages[iHCPage];
# endif
                        SHWPTE      PteDst;

# ifndef VBOX_WITH_NEW_LAZY_PAGE_ALLOC
                        /* Try to make the page writable if necessary. */
                        if (    PGM_PAGE_GET_TYPE(pPage)  == PGMPAGETYPE_RAM
                            &&  (   PGM_PAGE_IS_ZERO(pPage)
                                 || (   SHW_PTE_IS_RW(PteDstBase)
                                     && PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_ALLOCATED
#  ifdef VBOX_WITH_REAL_WRITE_MONITORED_PAGES
                                     && PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_WRITE_MONITORED
#  endif
#  ifdef VBOX_WITH_PAGE_SHARING
                                     && PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_SHARED
#  endif
                                     && !PGM_PAGE_IS_BALLOONED(pPage))
                                 )
                           )
                        {
                            rc = pgmPhysPageMakeWritable(pVM, pPage, GCPhys);
                            AssertRCReturn(rc, rc);
                            if (VM_FF_IS_SET(pVM, VM_FF_PGM_NO_MEMORY))
                                break;
                        }
# endif

                        if (PGM_PAGE_HAS_ACTIVE_HANDLERS(pPage) && !PGM_PAGE_IS_HNDL_PHYS_NOT_IN_HM(pPage))
                            PGM_BTH_NAME(SyncHandlerPte)(pVM, pVCpu, pPage, GCPhys, SHW_PTE_GET_U(PteDstBase), &PteDst);
                        else if (PGM_PAGE_IS_BALLOONED(pPage))
                            SHW_PTE_SET(PteDst, 0); /* Handle ballooned pages at #PF time. */
                        else
                            SHW_PTE_SET(PteDst, PGM_PAGE_GET_HCPHYS(pPage) | SHW_PTE_GET_U(PteDstBase));

                        /* Only map writable pages writable. */
                        if (    SHW_PTE_IS_P_RW(PteDst)
                            &&  PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_ALLOCATED)
                        {
# ifndef VBOX_WITH_NEW_LAZY_PAGE_ALLOC
                            /* Still applies to shared pages. */
                            Assert(!PGM_PAGE_IS_ZERO(pPage));
# endif
                            SHW_PTE_SET_RO(PteDst);   /** @todo this isn't quite working yet... */
                            Log3(("SyncPT: write-protecting %RGp pPage=%R[pgmpage] at %RGv\n", GCPhys, pPage, (RTGCPTR)(GCPtr | (iPTDst << SHW_PT_SHIFT))));
                        }

                        if (SHW_PTE_IS_P(PteDst))
                            PGM_BTH_NAME(SyncPageWorkerTrackAddref)(pVCpu, pShwPage, PGM_PAGE_GET_TRACKING(pPage), pPage, iPTDst);

                        /* commit it (not atomic, new table) */
                        pPTDst->a[iPTDst] = PteDst;
                        Log4(("SyncPT: BIG %RGv PteDst:{P=%d RW=%d U=%d raw=%08llx}%s\n",
                              (RTGCPTR)(GCPtr | (iPTDst << SHW_PT_SHIFT)), SHW_PTE_IS_P(PteDst), SHW_PTE_IS_RW(PteDst), SHW_PTE_IS_US(PteDst), SHW_PTE_LOG64(PteDst),
                              SHW_PTE_IS_TRACK_DIRTY(PteDst) ? " Track-Dirty" : ""));

                        /* advance */
                        GCPhys += GUEST_PAGE_SIZE;
                        PGM_A20_APPLY_TO_VAR(pVCpu, GCPhys);
# ifndef PGM_WITH_A20
                        iHCPage++;
# endif
                        iPTDst++;
                    } while (   iPTDst < RT_ELEMENTS(pPTDst->a)
                             && GCPhys <= pRam->GCPhysLast);
                }
                else if (pRam)
                {
                    Log(("Invalid pages at %RGp\n", GCPhys));
                    do
                    {
                        SHW_PTE_SET(pPTDst->a[iPTDst], 0); /* Invalid page, we must handle them manually. */
                        GCPhys += GUEST_PAGE_SIZE;
                        iPTDst++;
                    } while (   iPTDst < RT_ELEMENTS(pPTDst->a)
                             && GCPhys < pRam->GCPhys);
                    PGM_A20_APPLY_TO_VAR(pVCpu,GCPhys);
                }
                else
                {
                    Log(("Invalid pages at %RGp (2)\n", GCPhys));
                    for ( ; iPTDst < RT_ELEMENTS(pPTDst->a); iPTDst++)
                        SHW_PTE_SET(pPTDst->a[iPTDst], 0); /* Invalid page, we must handle them manually. */
                }
            } /* while more PTEs */
        } /* 4KB / 4MB */
    }
    else
        AssertRelease(!SHW_PDE_IS_P(PdeDst));

    STAM_PROFILE_STOP(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,SyncPT), a);
    if (RT_FAILURE(rc))
        STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,SyncPTFailed));
    return rc;

#elif (PGM_GST_TYPE == PGM_TYPE_REAL || PGM_GST_TYPE == PGM_TYPE_PROT) \
    && !PGM_TYPE_IS_NESTED(PGM_SHW_TYPE) \
    && (PGM_SHW_TYPE != PGM_TYPE_EPT || PGM_GST_TYPE == PGM_TYPE_PROT) \
    && PGM_SHW_TYPE != PGM_TYPE_NONE
    NOREF(iPDSrc); NOREF(pPDSrc);

    STAM_PROFILE_START(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,SyncPT), a);

    /*
     * Validate input a little bit.
     */
    int             rc = VINF_SUCCESS;
# if PGM_SHW_TYPE == PGM_TYPE_32BIT
    const unsigned  iPDDst  = (GCPtrPage >> SHW_PD_SHIFT) & SHW_PD_MASK;
    PSHWPDE         pPdeDst = pgmShwGet32BitPDEPtr(pVCpu, GCPtrPage);
    AssertReturn(pPdeDst, VERR_INTERNAL_ERROR_3);

    /* Fetch the pgm pool shadow descriptor. */
    PPGMPOOLPAGE    pShwPde = pVCpu->pgm.s.CTX_SUFF(pShwPageCR3);
    Assert(pShwPde);

# elif PGM_SHW_TYPE == PGM_TYPE_PAE
    const unsigned  iPDDst  = (GCPtrPage >> SHW_PD_SHIFT) & SHW_PD_MASK;
    PPGMPOOLPAGE    pShwPde = NULL;             /* initialized to shut up gcc */
    PX86PDPAE       pPDDst;
    PSHWPDE         pPdeDst;

    /* Fetch the pgm pool shadow descriptor. */
    rc = pgmShwGetPaePoolPagePD(pVCpu, GCPtrPage, &pShwPde);
    AssertRCSuccessReturn(rc, rc);
    Assert(pShwPde);

    pPDDst  = (PX86PDPAE)PGMPOOL_PAGE_2_PTR_V2(pVM, pVCpu, pShwPde);
    pPdeDst = &pPDDst->a[iPDDst];

# elif PGM_SHW_TYPE == PGM_TYPE_AMD64
    const unsigned  iPdpt   = (GCPtrPage >> X86_PDPT_SHIFT) & X86_PDPT_MASK_AMD64;
    const unsigned  iPDDst  = (GCPtrPage >> SHW_PD_SHIFT) & SHW_PD_MASK;
    PX86PDPAE       pPDDst  = NULL;             /* initialized to shut up gcc */
    PX86PDPT        pPdptDst= NULL;             /* initialized to shut up gcc */
    rc = pgmShwGetLongModePDPtr(pVCpu, GCPtrPage, NULL, &pPdptDst, &pPDDst);
    AssertRCSuccessReturn(rc, rc);
    Assert(pPDDst);
    PSHWPDE         pPdeDst = &pPDDst->a[iPDDst];

    /* Fetch the pgm pool shadow descriptor. */
    PPGMPOOLPAGE    pShwPde = pgmPoolGetPage(pPool, pPdptDst->a[iPdpt].u & X86_PDPE_PG_MASK);
    Assert(pShwPde);

# elif PGM_SHW_TYPE == PGM_TYPE_EPT
    const unsigned  iPdpt   = (GCPtrPage >> EPT_PDPT_SHIFT) & EPT_PDPT_MASK;
    const unsigned  iPDDst  = ((GCPtrPage >> SHW_PD_SHIFT) & SHW_PD_MASK);
    PEPTPD          pPDDst;
    PEPTPDPT        pPdptDst;

    rc = pgmShwGetEPTPDPtr(pVCpu, GCPtrPage, &pPdptDst, &pPDDst);
    if (rc != VINF_SUCCESS)
    {
        STAM_PROFILE_STOP(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,SyncPT), a);
        AssertRC(rc);
        return rc;
    }
    Assert(pPDDst);
    PSHWPDE         pPdeDst = &pPDDst->a[iPDDst];

    /* Fetch the pgm pool shadow descriptor. */
    /** @todo r=bird: didn't pgmShwGetEPTPDPtr just do this lookup already? */
    PPGMPOOLPAGE pShwPde = pgmPoolGetPage(pPool, pPdptDst->a[iPdpt].u & EPT_PDPTE_PG_MASK);
    Assert(pShwPde);
# endif
    SHWPDE          PdeDst = *pPdeDst;

    Assert(!SHW_PDE_IS_P(PdeDst)); /* We're only supposed to call SyncPT on PDE!P and conflicts.*/

# if defined(PGM_WITH_LARGE_PAGES) && PGM_SHW_TYPE != PGM_TYPE_32BIT && PGM_SHW_TYPE != PGM_TYPE_PAE
    if (BTH_IS_NP_ACTIVE(pVM))
    {
        Assert(!VM_IS_NEM_ENABLED(pVM));

        /* Check if we allocated a big page before for this 2 MB range. */
        PPGMPAGE pPage;
        rc = pgmPhysGetPageEx(pVM, PGM_A20_APPLY(pVCpu, GCPtrPage & X86_PDE2M_PAE_PG_MASK), &pPage);
        if (RT_SUCCESS(rc))
        {
            RTHCPHYS HCPhys = NIL_RTHCPHYS;
            if (PGM_PAGE_GET_PDE_TYPE(pPage) == PGM_PAGE_PDE_TYPE_PDE)
            {
                if (PGM_A20_IS_ENABLED(pVCpu))
                {
                    STAM_REL_COUNTER_INC(&pVM->pgm.s.StatLargePageReused);
                    AssertRelease(PGM_PAGE_GET_STATE(pPage) == PGM_PAGE_STATE_ALLOCATED);
                    HCPhys = PGM_PAGE_GET_HCPHYS(pPage);
                }
                else
                {
                    PGM_PAGE_SET_PDE_TYPE(pVM, pPage, PGM_PAGE_PDE_TYPE_PDE_DISABLED);
                    pVM->pgm.s.cLargePagesDisabled++;
                }
            }
            else if (   PGM_PAGE_GET_PDE_TYPE(pPage) == PGM_PAGE_PDE_TYPE_PDE_DISABLED
                     && PGM_A20_IS_ENABLED(pVCpu))
            {
                /* Recheck the entire 2 MB range to see if we can use it again as a large page. */
                rc = pgmPhysRecheckLargePage(pVM, GCPtrPage, pPage);
                if (RT_SUCCESS(rc))
                {
                    Assert(PGM_PAGE_GET_STATE(pPage) == PGM_PAGE_STATE_ALLOCATED);
                    Assert(PGM_PAGE_GET_PDE_TYPE(pPage) == PGM_PAGE_PDE_TYPE_PDE);
                    HCPhys = PGM_PAGE_GET_HCPHYS(pPage);
                }
            }
#  if !defined(VBOX_WITH_NEW_LAZY_PAGE_ALLOC) && !defined(PGM_WITH_PAGE_ZEROING_DETECTION) /* This code is too aggresive! */
            else if (   PGMIsUsingLargePages(pVM)
                     && PGM_A20_IS_ENABLED(pVCpu))
            {
                rc = pgmPhysAllocLargePage(pVM, GCPtrPage);
                if (RT_SUCCESS(rc))
                {
                    Assert(PGM_PAGE_GET_STATE(pPage) == PGM_PAGE_STATE_ALLOCATED);
                    Assert(PGM_PAGE_GET_PDE_TYPE(pPage) == PGM_PAGE_PDE_TYPE_PDE);
                    HCPhys = PGM_PAGE_GET_HCPHYS(pPage);
                }
                else
                    LogFlow(("pgmPhysAllocLargePage failed with %Rrc\n", rc));
            }
#  endif

            if (HCPhys != NIL_RTHCPHYS)
            {
#  if PGM_SHW_TYPE == PGM_TYPE_EPT
                PdeDst.u = HCPhys | EPT_E_READ | EPT_E_WRITE | EPT_E_EXECUTE | EPT_E_LEAF | EPT_E_IGNORE_PAT | EPT_E_MEMTYPE_WB
                         | (PdeDst.u & X86_PDE_AVL_MASK) /** @todo do we need this? */;
#  else
                PdeDst.u = HCPhys | X86_PDE_P | X86_PDE_RW | X86_PDE_US | X86_PDE_PS
                         | (PdeDst.u & X86_PDE_AVL_MASK) /** @todo PGM_PD_FLAGS? */;
#  endif
                SHW_PDE_ATOMIC_SET2(*pPdeDst, PdeDst);

                Log(("SyncPT: Use large page at %RGp PDE=%RX64\n", GCPtrPage, PdeDst.u));
                /* Add a reference to the first page only. */
                PGM_BTH_NAME(SyncPageWorkerTrackAddref)(pVCpu, pShwPde, PGM_PAGE_GET_TRACKING(pPage), pPage, iPDDst);

                STAM_PROFILE_STOP(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,SyncPT), a);
                return VINF_SUCCESS;
            }
        }
    }
# endif /* defined(PGM_WITH_LARGE_PAGES) && PGM_SHW_TYPE != PGM_TYPE_32BIT && PGM_SHW_TYPE != PGM_TYPE_PAE */

    /*
     * Allocate & map the page table.
     */
    PSHWPT          pPTDst;
    PPGMPOOLPAGE    pShwPage;
    RTGCPHYS        GCPhys;

    /* Virtual address = physical address */
    GCPhys = PGM_A20_APPLY(pVCpu, GCPtrPage & X86_PAGE_4K_BASE_MASK);
    rc = pgmPoolAlloc(pVM, GCPhys & ~(RT_BIT_64(SHW_PD_SHIFT) - 1), BTH_PGMPOOLKIND_PT_FOR_PT, PGMPOOLACCESS_DONTCARE,
                      PGM_A20_IS_ENABLED(pVCpu), pShwPde->idx, iPDDst, false /*fLockPage*/,
                      &pShwPage);
    if (    rc == VINF_SUCCESS
        ||  rc == VINF_PGM_CACHED_PAGE)
        pPTDst = (PSHWPT)PGMPOOL_PAGE_2_PTR_V2(pVM, pVCpu, pShwPage);
    else
    {
       STAM_PROFILE_STOP(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,SyncPT), a);
       AssertMsgFailedReturn(("rc=%Rrc\n", rc), RT_FAILURE_NP(rc) ? rc : VERR_IPE_UNEXPECTED_INFO_STATUS);
    }

    if (rc == VINF_SUCCESS)
    {
        /* New page table; fully set it up. */
        Assert(pPTDst);

        /* Mask away the page offset. */
        GCPtrPage &= ~(RTGCPTR)GUEST_PAGE_OFFSET_MASK;

        for (unsigned iPTDst = 0; iPTDst < RT_ELEMENTS(pPTDst->a); iPTDst++)
        {
            RTGCPTR GCPtrCurPage = PGM_A20_APPLY(pVCpu, (GCPtrPage & ~(RTGCPTR)(SHW_PT_MASK << SHW_PT_SHIFT))
                                                      | (iPTDst << GUEST_PAGE_SHIFT));

            PGM_BTH_NAME(SyncPageWorker)(pVCpu, &pPTDst->a[iPTDst], GCPtrCurPage, pShwPage, iPTDst);
            Log2(("SyncPage: 4K+ %RGv PteSrc:{P=1 RW=1 U=1} PteDst=%08llx%s\n",
                  GCPtrCurPage,
                  SHW_PTE_LOG64(pPTDst->a[iPTDst]),
                  SHW_PTE_IS_TRACK_DIRTY(pPTDst->a[iPTDst]) ? " Track-Dirty" : ""));

            if (RT_UNLIKELY(VM_FF_IS_SET(pVM, VM_FF_PGM_NO_MEMORY)))
                break;
        }
    }
    else
        rc = VINF_SUCCESS; /* Cached entry; assume it's still fully valid. */

    /* Save the new PDE. */
# if PGM_SHW_TYPE == PGM_TYPE_EPT
    PdeDst.u = pShwPage->Core.Key | EPT_E_READ | EPT_E_WRITE | EPT_E_EXECUTE
             | (PdeDst.u & X86_PDE_AVL_MASK /** @todo do we really need this? */);
# else
    PdeDst.u = pShwPage->Core.Key | X86_PDE_P | X86_PDE_RW | X86_PDE_US | X86_PDE_A
             | (PdeDst.u & X86_PDE_AVL_MASK /** @todo use a PGM_PD_FLAGS define */);
# endif
    SHW_PDE_ATOMIC_SET2(*pPdeDst, PdeDst);

    STAM_PROFILE_STOP(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,SyncPT), a);
    if (RT_FAILURE(rc))
        STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,SyncPTFailed));
    return rc;

#else
    NOREF(iPDSrc); NOREF(pPDSrc);
    AssertReleaseMsgFailed(("Shw=%d Gst=%d is not implemented!\n", PGM_SHW_TYPE, PGM_GST_TYPE));
    return VERR_PGM_NOT_USED_IN_MODE;
#endif
}



/**
 * Prefetch a page/set of pages.
 *
 * Typically used to sync commonly used pages before entering raw mode
 * after a CR3 reload.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtrPage   Page to invalidate.
 */
PGM_BTH_DECL(int, PrefetchPage)(PVMCPUCC pVCpu, RTGCPTR GCPtrPage)
{
#if (   PGM_GST_TYPE == PGM_TYPE_32BIT \
     || PGM_GST_TYPE == PGM_TYPE_REAL \
     || PGM_GST_TYPE == PGM_TYPE_PROT \
     || PGM_GST_TYPE == PGM_TYPE_PAE \
     || PGM_GST_TYPE == PGM_TYPE_AMD64 ) \
 && !PGM_TYPE_IS_NESTED_OR_EPT(PGM_SHW_TYPE) \
 && PGM_SHW_TYPE != PGM_TYPE_NONE
    /*
     * Check that all Guest levels thru the PDE are present, getting the
     * PD and PDE in the processes.
     */
    int             rc     = VINF_SUCCESS;
# if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
#  if PGM_GST_TYPE == PGM_TYPE_32BIT
    const unsigned  iPDSrc = (uint32_t)GCPtrPage >> GST_PD_SHIFT;
    PGSTPD          pPDSrc = pgmGstGet32bitPDPtr(pVCpu);
#  elif PGM_GST_TYPE == PGM_TYPE_PAE
    unsigned        iPDSrc;
    X86PDPE         PdpeSrc;
    PGSTPD          pPDSrc = pgmGstGetPaePDPtr(pVCpu, GCPtrPage, &iPDSrc, &PdpeSrc);
    if (!pPDSrc)
        return VINF_SUCCESS; /* not present */
#  elif PGM_GST_TYPE == PGM_TYPE_AMD64
    unsigned        iPDSrc;
    PX86PML4E       pPml4eSrc;
    X86PDPE         PdpeSrc;
    PGSTPD          pPDSrc = pgmGstGetLongModePDPtr(pVCpu, GCPtrPage, &pPml4eSrc, &PdpeSrc, &iPDSrc);
    if (!pPDSrc)
        return VINF_SUCCESS; /* not present */
#  endif
    const GSTPDE    PdeSrc = pPDSrc->a[iPDSrc];
# else
    PGSTPD          pPDSrc = NULL;
    const unsigned  iPDSrc = 0;
    GSTPDE const    PdeSrc = { X86_PDE_P | X86_PDE_RW | X86_PDE_US | X86_PDE_A }; /* faked so we don't have to #ifdef everything */
# endif

    if ((PdeSrc.u & (X86_PDE_P | X86_PDE_A)) == (X86_PDE_P | X86_PDE_A))
    {
        PVMCC pVM = pVCpu->CTX_SUFF(pVM);
        PGM_LOCK_VOID(pVM);

# if PGM_SHW_TYPE == PGM_TYPE_32BIT
        const X86PDE    PdeDst = pgmShwGet32BitPDE(pVCpu, GCPtrPage);
# elif PGM_SHW_TYPE == PGM_TYPE_PAE
        const unsigned  iPDDst = ((GCPtrPage >> SHW_PD_SHIFT) & SHW_PD_MASK);
        PX86PDPAE       pPDDst;
        X86PDEPAE       PdeDst;
#   if PGM_GST_TYPE != PGM_TYPE_PAE
        X86PDPE         PdpeSrc;

        /* Fake PDPT entry; access control handled on the page table level, so allow everything. */
        PdpeSrc.u  = X86_PDPE_P;   /* rw/us are reserved for PAE pdpte's; accessed bit causes invalid VT-x guest state errors */
#   endif
        rc = pgmShwSyncPaePDPtr(pVCpu, GCPtrPage, PdpeSrc.u, &pPDDst);
        if (rc != VINF_SUCCESS)
        {
            PGM_UNLOCK(pVM);
            AssertRC(rc);
            return rc;
        }
        Assert(pPDDst);
        PdeDst = pPDDst->a[iPDDst];

# elif PGM_SHW_TYPE == PGM_TYPE_AMD64
        const unsigned  iPDDst = ((GCPtrPage >> SHW_PD_SHIFT) & SHW_PD_MASK);
        PX86PDPAE       pPDDst;
        X86PDEPAE       PdeDst;

#  if PGM_GST_TYPE == PGM_TYPE_PROT
        /* AMD-V nested paging */
        X86PML4E        Pml4eSrc;
        X86PDPE         PdpeSrc;
        PX86PML4E       pPml4eSrc = &Pml4eSrc;

        /* Fake PML4 & PDPT entry; access control handled on the page table level, so allow everything. */
        Pml4eSrc.u = X86_PML4E_P | X86_PML4E_RW | X86_PML4E_US | X86_PML4E_A;
        PdpeSrc.u  = X86_PDPE_P | X86_PDPE_RW | X86_PDPE_US | X86_PDPE_A;
#  endif

        rc = pgmShwSyncLongModePDPtr(pVCpu, GCPtrPage, pPml4eSrc->u, PdpeSrc.u, &pPDDst);
        if (rc != VINF_SUCCESS)
        {
            PGM_UNLOCK(pVM);
            AssertRC(rc);
            return rc;
        }
        Assert(pPDDst);
        PdeDst = pPDDst->a[iPDDst];
# endif
        if (!(PdeDst.u & X86_PDE_P))
        {
            /** @todo r=bird: This guy will set the A bit on the PDE,
             *    probably harmless. */
            rc = PGM_BTH_NAME(SyncPT)(pVCpu, iPDSrc, pPDSrc, GCPtrPage);
        }
        else
        {
            /* Note! We used to sync PGM_SYNC_NR_PAGES pages, which triggered assertions in CSAM, because
             *       R/W attributes of nearby pages were reset. Not sure how that could happen. Anyway, it
             *       makes no sense to prefetch more than one page.
             */
            rc = PGM_BTH_NAME(SyncPage)(pVCpu, PdeSrc, GCPtrPage, 1, 0);
            if (RT_SUCCESS(rc))
                rc = VINF_SUCCESS;
        }
        PGM_UNLOCK(pVM);
    }
    return rc;

#elif PGM_TYPE_IS_NESTED_OR_EPT(PGM_SHW_TYPE) || PGM_SHW_TYPE == PGM_TYPE_NONE
    NOREF(pVCpu); NOREF(GCPtrPage);
    return VINF_SUCCESS; /* ignore */
#else
    AssertCompile(0);
#endif
}




/**
 * Syncs a page during a PGMVerifyAccess() call.
 *
 * @returns VBox status code (informational included).
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtrPage   The address of the page to sync.
 * @param   fPage       The effective guest page flags.
 * @param   uErr        The trap error code.
 * @remarks This will normally never be called on invalid guest page
 *          translation entries.
 */
PGM_BTH_DECL(int, VerifyAccessSyncPage)(PVMCPUCC pVCpu, RTGCPTR GCPtrPage, unsigned fPage, unsigned uErr)
{
    PVMCC pVM = pVCpu->CTX_SUFF(pVM); NOREF(pVM);

    LogFlow(("VerifyAccessSyncPage: GCPtrPage=%RGv fPage=%#x uErr=%#x\n", GCPtrPage, fPage, uErr));
    RT_NOREF_PV(GCPtrPage); RT_NOREF_PV(fPage); RT_NOREF_PV(uErr);

    Assert(!pVM->pgm.s.fNestedPaging);
#if   (   PGM_GST_TYPE == PGM_TYPE_32BIT \
       || PGM_GST_TYPE == PGM_TYPE_REAL \
       || PGM_GST_TYPE == PGM_TYPE_PROT \
       || PGM_GST_TYPE == PGM_TYPE_PAE \
       || PGM_GST_TYPE == PGM_TYPE_AMD64 ) \
    && !PGM_TYPE_IS_NESTED_OR_EPT(PGM_SHW_TYPE) \
    && PGM_SHW_TYPE != PGM_TYPE_NONE

    /*
     * Get guest PD and index.
     */
    /** @todo Performance: We've done all this a jiffy ago in the
     *        PGMGstGetPage call. */
# if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
#  if PGM_GST_TYPE == PGM_TYPE_32BIT
    const unsigned  iPDSrc = (uint32_t)GCPtrPage >> GST_PD_SHIFT;
    PGSTPD          pPDSrc = pgmGstGet32bitPDPtr(pVCpu);

#  elif PGM_GST_TYPE == PGM_TYPE_PAE
    unsigned        iPDSrc = 0;
    X86PDPE         PdpeSrc;
    PGSTPD          pPDSrc = pgmGstGetPaePDPtr(pVCpu, GCPtrPage, &iPDSrc, &PdpeSrc);
    if (RT_UNLIKELY(!pPDSrc))
    {
        Log(("PGMVerifyAccess: access violation for %RGv due to non-present PDPTR\n", GCPtrPage));
        return VINF_EM_RAW_GUEST_TRAP;
    }

#  elif PGM_GST_TYPE == PGM_TYPE_AMD64
    unsigned        iPDSrc = 0;         /* shut up gcc */
    PX86PML4E       pPml4eSrc = NULL;   /* ditto */
    X86PDPE         PdpeSrc;
    PGSTPD          pPDSrc = pgmGstGetLongModePDPtr(pVCpu, GCPtrPage, &pPml4eSrc, &PdpeSrc, &iPDSrc);
    if (RT_UNLIKELY(!pPDSrc))
    {
        Log(("PGMVerifyAccess: access violation for %RGv due to non-present PDPTR\n", GCPtrPage));
        return VINF_EM_RAW_GUEST_TRAP;
    }
#  endif

# else  /* !PGM_WITH_PAGING */
    PGSTPD          pPDSrc = NULL;
    const unsigned  iPDSrc = 0;
# endif /* !PGM_WITH_PAGING */
    int             rc = VINF_SUCCESS;

    PGM_LOCK_VOID(pVM);

    /*
     * First check if the shadow pd is present.
     */
# if PGM_SHW_TYPE == PGM_TYPE_32BIT
    PX86PDE         pPdeDst = pgmShwGet32BitPDEPtr(pVCpu, GCPtrPage);
    AssertReturn(pPdeDst, VERR_INTERNAL_ERROR_3);

# elif PGM_SHW_TYPE == PGM_TYPE_PAE
    PX86PDEPAE      pPdeDst;
    const unsigned  iPDDst = ((GCPtrPage >> SHW_PD_SHIFT) & SHW_PD_MASK);
    PX86PDPAE       pPDDst;
#   if PGM_GST_TYPE != PGM_TYPE_PAE
    /* Fake PDPT entry; access control handled on the page table level, so allow everything. */
    X86PDPE         PdpeSrc;
    PdpeSrc.u  = X86_PDPE_P;   /* rw/us are reserved for PAE pdpte's; accessed bit causes invalid VT-x guest state errors */
#   endif
    rc = pgmShwSyncPaePDPtr(pVCpu, GCPtrPage, PdpeSrc.u, &pPDDst);
    if (rc != VINF_SUCCESS)
    {
        PGM_UNLOCK(pVM);
        AssertRC(rc);
        return rc;
    }
    Assert(pPDDst);
    pPdeDst = &pPDDst->a[iPDDst];

# elif PGM_SHW_TYPE == PGM_TYPE_AMD64
    const unsigned  iPDDst = ((GCPtrPage >> SHW_PD_SHIFT) & SHW_PD_MASK);
    PX86PDPAE       pPDDst;
    PX86PDEPAE      pPdeDst;

#  if PGM_GST_TYPE == PGM_TYPE_PROT
    /* AMD-V nested paging: Fake PML4 & PDPT entry; access control handled on the page table level, so allow everything. */
    X86PML4E        Pml4eSrc;
    X86PDPE         PdpeSrc;
    PX86PML4E       pPml4eSrc = &Pml4eSrc;
    Pml4eSrc.u = X86_PML4E_P | X86_PML4E_RW | X86_PML4E_US | X86_PML4E_A;
    PdpeSrc.u  = X86_PDPE_P  | X86_PDPE_RW  | X86_PDPE_US  | X86_PDPE_A;
#  endif

    rc = pgmShwSyncLongModePDPtr(pVCpu, GCPtrPage, pPml4eSrc->u, PdpeSrc.u, &pPDDst);
    if (rc != VINF_SUCCESS)
    {
        PGM_UNLOCK(pVM);
        AssertRC(rc);
        return rc;
    }
    Assert(pPDDst);
    pPdeDst = &pPDDst->a[iPDDst];
# endif

    if (!(pPdeDst->u & X86_PDE_P))
    {
        rc = PGM_BTH_NAME(SyncPT)(pVCpu, iPDSrc, pPDSrc, GCPtrPage);
        if (rc != VINF_SUCCESS)
        {
            PGM_DYNMAP_UNUSED_HINT(pVCpu, pPdeDst);
            PGM_UNLOCK(pVM);
            AssertRC(rc);
            return rc;
        }
    }

# if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
    /* Check for dirty bit fault */
    rc = PGM_BTH_NAME(CheckDirtyPageFault)(pVCpu, uErr, pPdeDst, &pPDSrc->a[iPDSrc], GCPtrPage);
    if (rc == VINF_PGM_HANDLED_DIRTY_BIT_FAULT)
        Log(("PGMVerifyAccess: success (dirty)\n"));
    else
# endif
    {
# if PGM_WITH_PAGING(PGM_GST_TYPE, PGM_SHW_TYPE)
        GSTPDE PdeSrc       = pPDSrc->a[iPDSrc];
# else
        GSTPDE const PdeSrc = { X86_PDE_P | X86_PDE_RW | X86_PDE_US | X86_PDE_A }; /* faked so we don't have to #ifdef everything */
# endif

        Assert(rc != VINF_EM_RAW_GUEST_TRAP);
        if (uErr & X86_TRAP_PF_US)
            STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,PageOutOfSyncUser));
        else /* supervisor */
            STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,PageOutOfSyncSupervisor));

        rc = PGM_BTH_NAME(SyncPage)(pVCpu, PdeSrc, GCPtrPage, 1, 0);
        if (RT_SUCCESS(rc))
        {
            /* Page was successfully synced */
            Log2(("PGMVerifyAccess: success (sync)\n"));
            rc = VINF_SUCCESS;
        }
        else
        {
            Log(("PGMVerifyAccess: access violation for %RGv rc=%Rrc\n", GCPtrPage, rc));
            rc = VINF_EM_RAW_GUEST_TRAP;
        }
    }
    PGM_DYNMAP_UNUSED_HINT(pVCpu, pPdeDst);
    PGM_UNLOCK(pVM);
    return rc;

#else  /* PGM_TYPE_IS_NESTED_OR_EPT(PGM_SHW_TYPE) */

    AssertLogRelMsgFailed(("Shw=%d Gst=%d is not implemented!\n", PGM_GST_TYPE, PGM_SHW_TYPE));
    return VERR_PGM_NOT_USED_IN_MODE;
#endif /* PGM_TYPE_IS_NESTED_OR_EPT(PGM_SHW_TYPE) */
}


/**
 * Syncs the paging hierarchy starting at CR3.
 *
 * @returns VBox status code, R0/RC may return VINF_PGM_SYNC_CR3, no other
 *          informational status codes.
 * @retval  VERR_PGM_NO_HYPERVISOR_ADDRESS in raw-mode when we're unable to map
 *          the VMM into guest context.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   cr0         Guest context CR0 register.
 * @param   cr3         Guest context CR3 register. Not subjected to the A20
 *                      mask.
 * @param   cr4         Guest context CR4 register.
 * @param   fGlobal     Including global page directories or not
 */
PGM_BTH_DECL(int, SyncCR3)(PVMCPUCC pVCpu, uint64_t cr0, uint64_t cr3, uint64_t cr4, bool fGlobal)
{
    PVMCC pVM = pVCpu->CTX_SUFF(pVM); NOREF(pVM);
    NOREF(cr0); NOREF(cr3); NOREF(cr4); NOREF(fGlobal);

    LogFlow(("SyncCR3 FF=%d fGlobal=%d\n", !!VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_PGM_SYNC_CR3), fGlobal));

#if !PGM_TYPE_IS_NESTED_OR_EPT(PGM_SHW_TYPE) && PGM_SHW_TYPE != PGM_TYPE_NONE
# ifdef PGMPOOL_WITH_OPTIMIZED_DIRTY_PT
    PGM_LOCK_VOID(pVM);
    PPGMPOOL pPool = pVM->pgm.s.CTX_SUFF(pPool);
    if (pPool->cDirtyPages)
        pgmPoolResetDirtyPages(pVM);
    PGM_UNLOCK(pVM);
# endif
#endif /* !NESTED && !EPT */

#if PGM_TYPE_IS_NESTED_OR_EPT(PGM_SHW_TYPE) || PGM_SHW_TYPE == PGM_TYPE_NONE
    /*
     * Nested / EPT / None - No work.
     */
    return VINF_SUCCESS;

#elif PGM_SHW_TYPE == PGM_TYPE_AMD64
    /*
     * AMD64 (Shw & Gst) - No need to check all paging levels; we zero
     * out the shadow parts when the guest modifies its tables.
     */
    return VINF_SUCCESS;

#else /* !PGM_TYPE_IS_NESTED_OR_EPT(PGM_SHW_TYPE) && PGM_SHW_TYPE != PGM_TYPE_AMD64 */

    return VINF_SUCCESS;
#endif /* !PGM_TYPE_IS_NESTED_OR_EPT(PGM_SHW_TYPE) && PGM_SHW_TYPE != PGM_TYPE_AMD64 */
}




#ifdef VBOX_STRICT

/**
 * Checks that the shadow page table is in sync with the guest one.
 *
 * @returns The number of errors.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   cr3         Guest context CR3 register.
 * @param   cr4         Guest context CR4 register.
 * @param   GCPtr       Where to start. Defaults to 0.
 * @param   cb          How much to check. Defaults to everything.
 */
PGM_BTH_DECL(unsigned, AssertCR3)(PVMCPUCC pVCpu, uint64_t cr3, uint64_t cr4, RTGCPTR GCPtr, RTGCPTR cb)
{
    NOREF(pVCpu); NOREF(cr3); NOREF(cr4); NOREF(GCPtr); NOREF(cb);
#if PGM_TYPE_IS_NESTED_OR_EPT(PGM_SHW_TYPE) || PGM_SHW_TYPE == PGM_TYPE_NONE
    return 0;
#else
    unsigned cErrors = 0;
    PVMCC    pVM     = pVCpu->CTX_SUFF(pVM);
    PPGMPOOL pPool   = pVM->pgm.s.CTX_SUFF(pPool); NOREF(pPool);

# if PGM_GST_TYPE == PGM_TYPE_PAE
    /** @todo currently broken; crashes below somewhere */
    AssertFailed();
# endif

# if   PGM_GST_TYPE == PGM_TYPE_32BIT \
    || PGM_GST_TYPE == PGM_TYPE_PAE \
    || PGM_GST_TYPE == PGM_TYPE_AMD64

    bool            fBigPagesSupported = GST_IS_PSE_ACTIVE(pVCpu);
    PPGMCPU         pPGM = &pVCpu->pgm.s;
    RTGCPHYS        GCPhysGst;              /* page address derived from the guest page tables. */
    RTHCPHYS        HCPhysShw;              /* page address derived from the shadow page tables. */
#  ifndef IN_RING0
    RTHCPHYS        HCPhys;                 /* general usage. */
#  endif
    int             rc;

    /*
     * Check that the Guest CR3 and all its mappings are correct.
     */
    AssertMsgReturn(pPGM->GCPhysCR3 == PGM_A20_APPLY(pVCpu, cr3 & GST_CR3_PAGE_MASK),
                    ("Invalid GCPhysCR3=%RGp cr3=%RGp\n", pPGM->GCPhysCR3, (RTGCPHYS)cr3),
                    false);
#  if !defined(IN_RING0) && PGM_GST_TYPE != PGM_TYPE_AMD64
#   if 0
#    if PGM_GST_TYPE == PGM_TYPE_32BIT
    rc = PGMShwGetPage(pVCpu, (RTRCUINTPTR)pPGM->pGst32BitPdRC, NULL, &HCPhysShw);
#    else
    rc = PGMShwGetPage(pVCpu, (RTRCUINTPTR)pPGM->pGstPaePdptRC, NULL, &HCPhysShw);
#    endif
    AssertRCReturn(rc, 1);
    HCPhys = NIL_RTHCPHYS;
    rc = pgmRamGCPhys2HCPhys(pVM, PGM_A20_APPLY(pVCpu, cr3 & GST_CR3_PAGE_MASK), &HCPhys);
    AssertMsgReturn(HCPhys == HCPhysShw, ("HCPhys=%RHp HCPhyswShw=%RHp (cr3)\n", HCPhys, HCPhysShw), false);
#   endif
#   if PGM_GST_TYPE == PGM_TYPE_32BIT && defined(IN_RING3)
    pgmGstGet32bitPDPtr(pVCpu);
    RTGCPHYS GCPhys;
    rc = PGMR3DbgR3Ptr2GCPhys(pVM->pUVM, pPGM->pGst32BitPdR3, &GCPhys);
    AssertRCReturn(rc, 1);
    AssertMsgReturn(PGM_A20_APPLY(pVCpu, cr3 & GST_CR3_PAGE_MASK) == GCPhys, ("GCPhys=%RGp cr3=%RGp\n", GCPhys, (RTGCPHYS)cr3), false);
#   endif
#  endif /* !IN_RING0 */

    /*
     * Get and check the Shadow CR3.
     */
#  if PGM_SHW_TYPE == PGM_TYPE_32BIT
    unsigned        cPDEs       = X86_PG_ENTRIES;
    unsigned        cIncrement  = X86_PG_ENTRIES * GUEST_PAGE_SIZE;
#  elif PGM_SHW_TYPE == PGM_TYPE_PAE
#   if PGM_GST_TYPE == PGM_TYPE_32BIT
    unsigned        cPDEs       = X86_PG_PAE_ENTRIES * 4;   /* treat it as a 2048 entry table. */
#   else
    unsigned        cPDEs       = X86_PG_PAE_ENTRIES;
#   endif
    unsigned        cIncrement  = X86_PG_PAE_ENTRIES * GUEST_PAGE_SIZE;
#  elif PGM_SHW_TYPE == PGM_TYPE_AMD64
    unsigned        cPDEs       = X86_PG_PAE_ENTRIES;
    unsigned        cIncrement  = X86_PG_PAE_ENTRIES * GUEST_PAGE_SIZE;
#  endif
    if (cb != ~(RTGCPTR)0)
        cPDEs = RT_MIN(cb >> SHW_PD_SHIFT, 1);

/** @todo call the other two PGMAssert*() functions. */

#  if PGM_GST_TYPE == PGM_TYPE_AMD64
    unsigned iPml4 = (GCPtr >> X86_PML4_SHIFT) & X86_PML4_MASK;

    for (; iPml4 < X86_PG_PAE_ENTRIES; iPml4++)
    {
        PX86PML4E const pPml4eSrc = pgmGstGetLongModePML4EPtr(pVCpu, iPml4);
        AssertContinueStmt(pPml4eSrc, cErrors++);

        PX86PML4E const pPml4eDst = pgmShwGetLongModePML4EPtr(pVCpu, iPml4);
        AssertContinueStmt(pPml4eDst, cErrors++);

        /* Fetch the pgm pool shadow descriptor if the shadow pml4e is present. */
        if (!(pPml4eDst->u & X86_PML4E_P))
        {
            GCPtr += _2M * UINT64_C(512) * UINT64_C(512);
            continue;
        }

        PPGMPOOLPAGE pShwPdpt = pgmPoolGetPage(pPool, pPml4eDst->u & X86_PML4E_PG_MASK);
        RTGCPHYS GCPhysPdptSrc = PGM_A20_APPLY(pVCpu, pPml4eSrc->u & X86_PML4E_PG_MASK);

        if ((pPml4eSrc->u & X86_PML4E_P) != (pPml4eDst->u & X86_PML4E_P))
        {
            AssertMsgFailed(("Present bit doesn't match! pPml4eDst.u=%#RX64 pPml4eSrc.u=%RX64\n", pPml4eDst->u, pPml4eSrc->u));
            GCPtr += _2M * UINT64_C(512) * UINT64_C(512);
            cErrors++;
            continue;
        }

        if (GCPhysPdptSrc != pShwPdpt->GCPhys)
        {
            AssertMsgFailed(("Physical address doesn't match! iPml4 %d pPml4eDst.u=%#RX64 pPml4eSrc.u=%RX64 Phys %RX64 vs %RX64\n", iPml4, pPml4eDst->u, pPml4eSrc->u, pShwPdpt->GCPhys, GCPhysPdptSrc));
            GCPtr += _2M * UINT64_C(512) * UINT64_C(512);
            cErrors++;
            continue;
        }

        if (   (pPml4eDst->u & (X86_PML4E_US | X86_PML4E_RW | X86_PML4E_NX))
            != (pPml4eSrc->u & (X86_PML4E_US | X86_PML4E_RW | X86_PML4E_NX)))
        {
            AssertMsgFailed(("User/Write/NoExec bits don't match! pPml4eDst.u=%#RX64 pPml4eSrc.u=%RX64\n", pPml4eDst->u, pPml4eSrc->u));
            GCPtr += _2M * UINT64_C(512) * UINT64_C(512);
            cErrors++;
            continue;
        }
#  else  /* PGM_GST_TYPE != PGM_TYPE_AMD64 */
    {
#  endif /* PGM_GST_TYPE != PGM_TYPE_AMD64 */

#  if PGM_GST_TYPE == PGM_TYPE_AMD64 || PGM_GST_TYPE == PGM_TYPE_PAE
        /*
         * Check the PDPTEs too.
         */
        unsigned iPdpt = (GCPtr >> SHW_PDPT_SHIFT) & SHW_PDPT_MASK;

        for (;iPdpt <= SHW_PDPT_MASK; iPdpt++)
        {
            unsigned        iPDSrc  = 0;        /* initialized to shut up gcc */
            PPGMPOOLPAGE    pShwPde = NULL;
            PX86PDPE        pPdpeDst;
            RTGCPHYS        GCPhysPdeSrc;
            X86PDPE         PdpeSrc;
            PdpeSrc.u = 0;                      /* initialized to shut up gcc 4.5 */
#   if PGM_GST_TYPE == PGM_TYPE_PAE
            PGSTPD          pPDSrc    = pgmGstGetPaePDPtr(pVCpu, GCPtr, &iPDSrc, &PdpeSrc);
            PX86PDPT        pPdptDst  = pgmShwGetPaePDPTPtr(pVCpu);
#   else
            PX86PML4E       pPml4eSrcIgn;
            PX86PDPT        pPdptDst;
            PX86PDPAE       pPDDst;
            PGSTPD          pPDSrc    = pgmGstGetLongModePDPtr(pVCpu, GCPtr, &pPml4eSrcIgn, &PdpeSrc, &iPDSrc);

            rc = pgmShwGetLongModePDPtr(pVCpu, GCPtr, NULL, &pPdptDst, &pPDDst);
            if (rc != VINF_SUCCESS)
            {
                AssertMsg(rc == VERR_PAGE_DIRECTORY_PTR_NOT_PRESENT, ("Unexpected rc=%Rrc\n", rc));
                GCPtr += 512 * _2M;
                continue;   /* next PDPTE */
            }
            Assert(pPDDst);
#   endif
            Assert(iPDSrc == 0);

            pPdpeDst = &pPdptDst->a[iPdpt];

            if (!(pPdpeDst->u & X86_PDPE_P))
            {
                GCPtr += 512 * _2M;
                continue;   /* next PDPTE */
            }

            pShwPde      = pgmPoolGetPage(pPool, pPdpeDst->u & X86_PDPE_PG_MASK);
            GCPhysPdeSrc = PGM_A20_APPLY(pVCpu, PdpeSrc.u & X86_PDPE_PG_MASK);

            if ((pPdpeDst->u & X86_PDPE_P) != (PdpeSrc.u & X86_PDPE_P))
            {
                AssertMsgFailed(("Present bit doesn't match! pPdpeDst.u=%#RX64 pPdpeSrc.u=%RX64\n", pPdpeDst->u, PdpeSrc.u));
                GCPtr += 512 * _2M;
                cErrors++;
                continue;
            }

            if (GCPhysPdeSrc != pShwPde->GCPhys)
            {
#   if PGM_GST_TYPE == PGM_TYPE_AMD64
                AssertMsgFailed(("Physical address doesn't match! iPml4 %d iPdpt %d pPdpeDst.u=%#RX64 pPdpeSrc.u=%RX64 Phys %RX64 vs %RX64\n", iPml4, iPdpt, pPdpeDst->u, PdpeSrc.u, pShwPde->GCPhys, GCPhysPdeSrc));
#   else
                AssertMsgFailed(("Physical address doesn't match! iPdpt %d pPdpeDst.u=%#RX64 pPdpeSrc.u=%RX64 Phys %RX64 vs %RX64\n", iPdpt, pPdpeDst->u, PdpeSrc.u, pShwPde->GCPhys, GCPhysPdeSrc));
#   endif
                GCPtr += 512 * _2M;
                cErrors++;
                continue;
            }

#   if PGM_GST_TYPE == PGM_TYPE_AMD64
            if (    (pPdpeDst->u & (X86_PDPE_US | X86_PDPE_RW | X86_PDPE_LM_NX))
                !=  (PdpeSrc.u   & (X86_PDPE_US | X86_PDPE_RW | X86_PDPE_LM_NX)))
            {
                AssertMsgFailed(("User/Write/NoExec bits don't match! pPdpeDst.u=%#RX64 pPdpeSrc.u=%RX64\n", pPdpeDst->u, PdpeSrc.u));
                GCPtr += 512 * _2M;
                cErrors++;
                continue;
            }
#   endif

#  else  /* PGM_GST_TYPE != PGM_TYPE_AMD64 && PGM_GST_TYPE != PGM_TYPE_PAE */
        {
#  endif /* PGM_GST_TYPE != PGM_TYPE_AMD64 && PGM_GST_TYPE != PGM_TYPE_PAE */
#  if PGM_GST_TYPE == PGM_TYPE_32BIT
            GSTPD const    *pPDSrc = pgmGstGet32bitPDPtr(pVCpu);
#   if PGM_SHW_TYPE == PGM_TYPE_32BIT
            PCX86PD         pPDDst = pgmShwGet32BitPDPtr(pVCpu);
#   endif
#  endif /* PGM_GST_TYPE == PGM_TYPE_32BIT */
            /*
            * Iterate the shadow page directory.
            */
            GCPtr = (GCPtr >> SHW_PD_SHIFT) << SHW_PD_SHIFT;
            unsigned iPDDst = (GCPtr >> SHW_PD_SHIFT) & SHW_PD_MASK;

            for (;
                iPDDst < cPDEs;
                iPDDst++, GCPtr += cIncrement)
            {
#  if PGM_SHW_TYPE == PGM_TYPE_PAE
                const SHWPDE PdeDst = *pgmShwGetPaePDEPtr(pVCpu, GCPtr);
#  else
                const SHWPDE PdeDst = pPDDst->a[iPDDst];
#  endif
                if (   (PdeDst.u & X86_PDE_P)
                    || ((PdeDst.u & (X86_PDE_P | PGM_PDFLAGS_TRACK_DIRTY)) == (X86_PDE_P | PGM_PDFLAGS_TRACK_DIRTY)) )
                {
                    HCPhysShw = PdeDst.u & SHW_PDE_PG_MASK;
                    PPGMPOOLPAGE pPoolPage = pgmPoolGetPage(pPool, HCPhysShw);
                    if (!pPoolPage)
                    {
                        AssertMsgFailed(("Invalid page table address %RHp at %RGv! PdeDst=%#RX64\n",
                                        HCPhysShw, GCPtr, (uint64_t)PdeDst.u));
                        cErrors++;
                        continue;
                    }
                    const SHWPT *pPTDst = (const SHWPT *)PGMPOOL_PAGE_2_PTR_V2(pVM, pVCpu, pPoolPage);

                    if (PdeDst.u & (X86_PDE4M_PWT | X86_PDE4M_PCD))
                    {
                        AssertMsgFailed(("PDE flags PWT and/or PCD is set at %RGv! These flags are not virtualized! PdeDst=%#RX64\n",
                                        GCPtr, (uint64_t)PdeDst.u));
                        cErrors++;
                    }

                    if (PdeDst.u & (X86_PDE4M_G | X86_PDE4M_D))
                    {
                        AssertMsgFailed(("4K PDE reserved flags at %RGv! PdeDst=%#RX64\n",
                                        GCPtr, (uint64_t)PdeDst.u));
                        cErrors++;
                    }

                    const GSTPDE PdeSrc = pPDSrc->a[(iPDDst >> (GST_PD_SHIFT - SHW_PD_SHIFT)) & GST_PD_MASK];
                    if (!(PdeSrc.u & X86_PDE_P))
                    {
                        AssertMsgFailed(("Guest PDE at %RGv is not present! PdeDst=%#RX64 PdeSrc=%#RX64\n",
                                        GCPtr, (uint64_t)PdeDst.u, (uint64_t)PdeSrc.u));
                        cErrors++;
                        continue;
                    }

                    if (   !(PdeSrc.u & X86_PDE_PS)
                        || !fBigPagesSupported)
                    {
                        GCPhysGst = GST_GET_PDE_GCPHYS(PdeSrc);
#  if PGM_SHW_TYPE == PGM_TYPE_PAE && PGM_GST_TYPE == PGM_TYPE_32BIT
                        GCPhysGst = PGM_A20_APPLY(pVCpu, GCPhysGst | ((iPDDst & 1) * (GUEST_PAGE_SIZE / 2)));
#  endif
                    }
                    else
                    {
#  if PGM_GST_TYPE == PGM_TYPE_32BIT
                        if (PdeSrc.u & X86_PDE4M_PG_HIGH_MASK)
                        {
                            AssertMsgFailed(("Guest PDE at %RGv is using PSE36 or similar! PdeSrc=%#RX64\n",
                                            GCPtr, (uint64_t)PdeSrc.u));
                            cErrors++;
                            continue;
                        }
#  endif
                        GCPhysGst = GST_GET_BIG_PDE_GCPHYS(pVM, PdeSrc);
#  if PGM_SHW_TYPE == PGM_TYPE_PAE && PGM_GST_TYPE == PGM_TYPE_32BIT
                        GCPhysGst = PGM_A20_APPLY(pVCpu, GCPhysGst | (GCPtr & RT_BIT(X86_PAGE_2M_SHIFT)));
#  endif
                    }

                    if (   pPoolPage->enmKind
                        != (!(PdeSrc.u & X86_PDE_PS) || !fBigPagesSupported ? BTH_PGMPOOLKIND_PT_FOR_PT : BTH_PGMPOOLKIND_PT_FOR_BIG))
                    {
                        AssertMsgFailed(("Invalid shadow page table kind %d at %RGv! PdeSrc=%#RX64\n",
                                        pPoolPage->enmKind, GCPtr, (uint64_t)PdeSrc.u));
                        cErrors++;
                    }

                    PPGMPAGE pPhysPage = pgmPhysGetPage(pVM, GCPhysGst);
                    if (!pPhysPage)
                    {
                        AssertMsgFailed(("Cannot find guest physical address %RGp in the PDE at %RGv! PdeSrc=%#RX64\n",
                                        GCPhysGst, GCPtr, (uint64_t)PdeSrc.u));
                        cErrors++;
                        continue;
                    }

                    if (GCPhysGst != pPoolPage->GCPhys)
                    {
                        AssertMsgFailed(("GCPhysGst=%RGp != pPage->GCPhys=%RGp at %RGv\n",
                                        GCPhysGst, pPoolPage->GCPhys, GCPtr));
                        cErrors++;
                        continue;
                    }

                    if (    !(PdeSrc.u & X86_PDE_PS)
                        ||  !fBigPagesSupported)
                    {
                        /*
                         * Page Table.
                         */
                        const GSTPT *pPTSrc;
                        rc = PGM_GCPHYS_2_PTR_V2(pVM, pVCpu, PGM_A20_APPLY(pVCpu, GCPhysGst & ~(RTGCPHYS)(GUEST_PAGE_SIZE - 1)),
                                                 &pPTSrc);
                        if (RT_FAILURE(rc))
                        {
                            AssertMsgFailed(("Cannot map/convert guest physical address %RGp in the PDE at %RGv! PdeSrc=%#RX64\n",
                                            GCPhysGst, GCPtr, (uint64_t)PdeSrc.u));
                            cErrors++;
                            continue;
                        }
                        if (    (PdeSrc.u & (X86_PDE_P | X86_PDE_US | X86_PDE_RW/* | X86_PDE_A*/))
                            !=  (PdeDst.u & (X86_PDE_P | X86_PDE_US | X86_PDE_RW/* | X86_PDE_A*/)))
                        {
                            /// @todo We get here a lot on out-of-sync CR3 entries. The access handler should zap them to avoid false alarms here!
                            // (This problem will go away when/if we shadow multiple CR3s.)
                            AssertMsgFailed(("4K PDE flags mismatch at %RGv! PdeSrc=%#RX64 PdeDst=%#RX64\n",
                                            GCPtr, (uint64_t)PdeSrc.u, (uint64_t)PdeDst.u));
                            cErrors++;
                            continue;
                        }
                        if (PdeDst.u & PGM_PDFLAGS_TRACK_DIRTY)
                        {
                            AssertMsgFailed(("4K PDEs cannot have PGM_PDFLAGS_TRACK_DIRTY set! GCPtr=%RGv PdeDst=%#RX64\n",
                                            GCPtr, (uint64_t)PdeDst.u));
                            cErrors++;
                            continue;
                        }

                        /* iterate the page table. */
#  if PGM_SHW_TYPE == PGM_TYPE_PAE && PGM_GST_TYPE == PGM_TYPE_32BIT
                        /* Select the right PDE as we're emulating a 4kb page table with 2 shadow page tables. */
                        const unsigned offPTSrc  = ((GCPtr >> SHW_PD_SHIFT) & 1) * 512;
#  else
                        const unsigned offPTSrc  = 0;
#  endif
                        for (unsigned iPT = 0, off = 0;
                            iPT < RT_ELEMENTS(pPTDst->a);
                            iPT++, off += GUEST_PAGE_SIZE)
                        {
                            const SHWPTE PteDst = pPTDst->a[iPT];

                            /* skip not-present and dirty tracked entries. */
                            if (!(SHW_PTE_GET_U(PteDst) & (X86_PTE_P | PGM_PTFLAGS_TRACK_DIRTY))) /** @todo deal with ALL handlers and CSAM !P pages! */
                                continue;
                            Assert(SHW_PTE_IS_P(PteDst));

                            const GSTPTE PteSrc = pPTSrc->a[iPT + offPTSrc];
                            if (!(PteSrc.u & X86_PTE_P))
                            {
#  ifdef IN_RING3
                                PGMAssertHandlerAndFlagsInSync(pVM);
                                DBGFR3PagingDumpEx(pVM->pUVM, pVCpu->idCpu, DBGFPGDMP_FLAGS_CURRENT_CR3 | DBGFPGDMP_FLAGS_CURRENT_MODE
                                                   | DBGFPGDMP_FLAGS_GUEST | DBGFPGDMP_FLAGS_HEADER | DBGFPGDMP_FLAGS_PRINT_CR3,
                                                   0, 0, UINT64_MAX, 99, NULL);
#  endif
                                AssertMsgFailed(("Out of sync (!P) PTE at %RGv! PteSrc=%#RX64 PteDst=%#RX64 pPTSrc=%RGv iPTSrc=%x PdeSrc=%x physpte=%RGp\n",
                                                GCPtr + off, (uint64_t)PteSrc.u, SHW_PTE_LOG64(PteDst), pPTSrc, iPT + offPTSrc, PdeSrc.au32[0],
                                                (uint64_t)GST_GET_PDE_GCPHYS(PdeSrc) + (iPT + offPTSrc) * sizeof(PteSrc)));
                                cErrors++;
                                continue;
                            }

                            uint64_t fIgnoreFlags = GST_PTE_PG_MASK | X86_PTE_AVL_MASK | X86_PTE_G | X86_PTE_D | X86_PTE_PWT | X86_PTE_PCD | X86_PTE_PAT;
#  if 1 /** @todo sync accessed bit properly... */
                            fIgnoreFlags |= X86_PTE_A;
#  endif

                            /* match the physical addresses */
                            HCPhysShw = SHW_PTE_GET_HCPHYS(PteDst);
                            GCPhysGst = GST_GET_PTE_GCPHYS(PteSrc);

#  ifdef IN_RING3
                            rc = PGMPhysGCPhys2HCPhys(pVM, GCPhysGst, &HCPhys);
                            if (RT_FAILURE(rc))
                            {
#   if 0
                                if (HCPhysShw != MMR3PageDummyHCPhys(pVM)) /** @todo this is wrong. */
                                {
                                    AssertMsgFailed(("Cannot find guest physical address %RGp at %RGv! PteSrc=%#RX64 PteDst=%#RX64\n",
                                                     GCPhysGst, GCPtr + off, (uint64_t)PteSrc.u, SHW_PTE_LOG64(PteDst)));
                                    cErrors++;
                                    continue;
                                }
#   endif
                            }
                            else if (HCPhysShw != (HCPhys & SHW_PTE_PG_MASK))
                            {
                                AssertMsgFailed(("Out of sync (phys) at %RGv! HCPhysShw=%RHp HCPhys=%RHp GCPhysGst=%RGp PteSrc=%#RX64 PteDst=%#RX64\n",
                                                 GCPtr + off, HCPhysShw, HCPhys, GCPhysGst, (uint64_t)PteSrc.u, SHW_PTE_LOG64(PteDst)));
                                cErrors++;
                                continue;
                            }
#  endif

                            pPhysPage = pgmPhysGetPage(pVM, GCPhysGst);
                            if (!pPhysPage)
                            {
#  if 0
                                if (HCPhysShw != MMR3PageDummyHCPhys(pVM))  /** @todo this is wrong. */
                                {
                                    AssertMsgFailed(("Cannot find guest physical address %RGp at %RGv! PteSrc=%#RX64 PteDst=%#RX64\n",
                                                     GCPhysGst, GCPtr + off, (uint64_t)PteSrc.u, SHW_PTE_LOG64(PteDst)));
                                    cErrors++;
                                    continue;
                                }
#  endif
                                if (SHW_PTE_IS_RW(PteDst))
                                {
                                    AssertMsgFailed(("Invalid guest page at %RGv is writable! GCPhysGst=%RGp PteSrc=%#RX64 PteDst=%#RX64\n",
                                                     GCPtr + off, GCPhysGst, (uint64_t)PteSrc.u, SHW_PTE_LOG64(PteDst)));
                                    cErrors++;
                                }
                                fIgnoreFlags |= X86_PTE_RW;
                            }
                            else if (HCPhysShw != PGM_PAGE_GET_HCPHYS(pPhysPage))
                            {
                                AssertMsgFailed(("Out of sync (phys) at %RGv! HCPhysShw=%RHp pPhysPage:%R[pgmpage] GCPhysGst=%RGp PteSrc=%#RX64 PteDst=%#RX64\n",
                                                GCPtr + off, HCPhysShw, pPhysPage, GCPhysGst, (uint64_t)PteSrc.u, SHW_PTE_LOG64(PteDst)));
                                cErrors++;
                                continue;
                            }

                            /* flags */
                            if (PGM_PAGE_HAS_ACTIVE_HANDLERS(pPhysPage) && !PGM_PAGE_IS_HNDL_PHYS_NOT_IN_HM(pPhysPage))
                            {
                                if (!PGM_PAGE_HAS_ACTIVE_ALL_HANDLERS(pPhysPage))
                                {
                                    if (SHW_PTE_IS_RW(PteDst))
                                    {
                                        AssertMsgFailed(("WRITE access flagged at %RGv but the page is writable! pPhysPage=%R[pgmpage] PteSrc=%#RX64 PteDst=%#RX64\n",
                                                         GCPtr + off, pPhysPage, (uint64_t)PteSrc.u, SHW_PTE_LOG64(PteDst)));
                                        cErrors++;
                                        continue;
                                    }
                                    fIgnoreFlags |= X86_PTE_RW;
                                }
                                else
                                {
                                    if (   SHW_PTE_IS_P(PteDst)
#  if PGM_SHW_TYPE == PGM_TYPE_EPT || PGM_SHW_TYPE == PGM_TYPE_PAE || PGM_SHW_TYPE == PGM_TYPE_AMD64
                                        && !PGM_PAGE_IS_MMIO(pPhysPage)
#  endif
                                       )
                                    {
                                        AssertMsgFailed(("ALL access flagged at %RGv but the page is present! pPhysPage=%R[pgmpage] PteSrc=%#RX64 PteDst=%#RX64\n",
                                                         GCPtr + off, pPhysPage, (uint64_t)PteSrc.u, SHW_PTE_LOG64(PteDst)));
                                        cErrors++;
                                        continue;
                                    }
                                    fIgnoreFlags |= X86_PTE_P;
                                }
                            }
                            else
                            {
                                if ((PteSrc.u & (X86_PTE_RW | X86_PTE_D)) == X86_PTE_RW)
                                {
                                    if (SHW_PTE_IS_RW(PteDst))
                                    {
                                        AssertMsgFailed(("!DIRTY page at %RGv is writable! PteSrc=%#RX64 PteDst=%#RX64\n",
                                                         GCPtr + off, (uint64_t)PteSrc.u, SHW_PTE_LOG64(PteDst)));
                                        cErrors++;
                                        continue;
                                    }
                                    if (!SHW_PTE_IS_TRACK_DIRTY(PteDst))
                                    {
                                        AssertMsgFailed(("!DIRTY page at %RGv is not marked TRACK_DIRTY! PteSrc=%#RX64 PteDst=%#RX64\n",
                                                         GCPtr + off, (uint64_t)PteSrc.u, SHW_PTE_LOG64(PteDst)));
                                        cErrors++;
                                        continue;
                                    }
                                    if (SHW_PTE_IS_D(PteDst))
                                    {
                                        AssertMsgFailed(("!DIRTY page at %RGv is marked DIRTY! PteSrc=%#RX64 PteDst=%#RX64\n",
                                                         GCPtr + off, (uint64_t)PteSrc.u, SHW_PTE_LOG64(PteDst)));
                                        cErrors++;
                                    }
#  if 0 /** @todo sync access bit properly... */
                                    if (PteDst.n.u1Accessed != PteSrc.n.u1Accessed)
                                    {
                                        AssertMsgFailed(("!DIRTY page at %RGv is has mismatching accessed bit! PteSrc=%#RX64 PteDst=%#RX64\n",
                                                        GCPtr + off, (uint64_t)PteSrc.u, SHW_PTE_LOG64(PteDst)));
                                        cErrors++;
                                    }
                                    fIgnoreFlags |= X86_PTE_RW;
#  else
                                    fIgnoreFlags |= X86_PTE_RW | X86_PTE_A;
#  endif
                                }
                                else if (SHW_PTE_IS_TRACK_DIRTY(PteDst))
                                {
                                    /* access bit emulation (not implemented). */
                                    if ((PteSrc.u & X86_PTE_A) || SHW_PTE_IS_P(PteDst))
                                    {
                                        AssertMsgFailed(("PGM_PTFLAGS_TRACK_DIRTY set at %RGv but no accessed bit emulation! PteSrc=%#RX64 PteDst=%#RX64\n",
                                                         GCPtr + off, (uint64_t)PteSrc.u, SHW_PTE_LOG64(PteDst)));
                                        cErrors++;
                                        continue;
                                    }
                                    if (!SHW_PTE_IS_A(PteDst))
                                    {
                                        AssertMsgFailed(("!ACCESSED page at %RGv is has the accessed bit set! PteSrc=%#RX64 PteDst=%#RX64\n",
                                                         GCPtr + off, (uint64_t)PteSrc.u, SHW_PTE_LOG64(PteDst)));
                                        cErrors++;
                                    }
                                    fIgnoreFlags |= X86_PTE_P;
                                }
#  ifdef DEBUG_sandervl
                                fIgnoreFlags |= X86_PTE_D | X86_PTE_A;
#  endif
                            }

                            if (    (PteSrc.u & ~fIgnoreFlags)                != (SHW_PTE_GET_U(PteDst) & ~fIgnoreFlags)
                                &&  (PteSrc.u & ~(fIgnoreFlags | X86_PTE_RW)) != (SHW_PTE_GET_U(PteDst) & ~fIgnoreFlags)
                            )
                            {
                                AssertMsgFailed(("Flags mismatch at %RGv! %#RX64 != %#RX64 fIgnoreFlags=%#RX64 PteSrc=%#RX64 PteDst=%#RX64\n",
                                                 GCPtr + off, (uint64_t)PteSrc.u & ~fIgnoreFlags, SHW_PTE_LOG64(PteDst) & ~fIgnoreFlags,
                                                 fIgnoreFlags, (uint64_t)PteSrc.u, SHW_PTE_LOG64(PteDst)));
                                cErrors++;
                                continue;
                            }
                        } /* foreach PTE */
                    }
                    else
                    {
                        /*
                        * Big Page.
                        */
                        uint64_t fIgnoreFlags = X86_PDE_AVL_MASK | GST_PDE_PG_MASK | X86_PDE4M_G | X86_PDE4M_D | X86_PDE4M_PS | X86_PDE4M_PWT | X86_PDE4M_PCD;
                        if ((PdeSrc.u & (X86_PDE_RW | X86_PDE4M_D)) == X86_PDE_RW)
                        {
                            if (PdeDst.u & X86_PDE_RW)
                            {
                                AssertMsgFailed(("!DIRTY page at %RGv is writable! PdeSrc=%#RX64 PdeDst=%#RX64\n",
                                                GCPtr, (uint64_t)PdeSrc.u, (uint64_t)PdeDst.u));
                                cErrors++;
                                continue;
                            }
                            if (!(PdeDst.u & PGM_PDFLAGS_TRACK_DIRTY))
                            {
                                AssertMsgFailed(("!DIRTY page at %RGv is not marked TRACK_DIRTY! PteSrc=%#RX64 PteDst=%#RX64\n",
                                                GCPtr, (uint64_t)PdeSrc.u, (uint64_t)PdeDst.u));
                                cErrors++;
                                continue;
                            }
#  if 0 /** @todo sync access bit properly... */
                            if (PdeDst.n.u1Accessed != PdeSrc.b.u1Accessed)
                            {
                                AssertMsgFailed(("!DIRTY page at %RGv is has mismatching accessed bit! PteSrc=%#RX64 PteDst=%#RX64\n",
                                                GCPtr, (uint64_t)PdeSrc.u, (uint64_t)PdeDst.u));
                                cErrors++;
                            }
                            fIgnoreFlags |= X86_PTE_RW;
#  else
                            fIgnoreFlags |= X86_PTE_RW | X86_PTE_A;
#  endif
                        }
                        else if (PdeDst.u & PGM_PDFLAGS_TRACK_DIRTY)
                        {
                            /* access bit emulation (not implemented). */
                            if ((PdeSrc.u & X86_PDE_A) || SHW_PDE_IS_P(PdeDst))
                            {
                                AssertMsgFailed(("PGM_PDFLAGS_TRACK_DIRTY set at %RGv but no accessed bit emulation! PdeSrc=%#RX64 PdeDst=%#RX64\n",
                                                GCPtr, (uint64_t)PdeSrc.u, (uint64_t)PdeDst.u));
                                cErrors++;
                                continue;
                            }
                            if (!SHW_PDE_IS_A(PdeDst))
                            {
                                AssertMsgFailed(("!ACCESSED page at %RGv is has the accessed bit set! PdeSrc=%#RX64 PdeDst=%#RX64\n",
                                                GCPtr, (uint64_t)PdeSrc.u, (uint64_t)PdeDst.u));
                                cErrors++;
                            }
                            fIgnoreFlags |= X86_PTE_P;
                        }

                        if ((PdeSrc.u & ~fIgnoreFlags) != (PdeDst.u & ~fIgnoreFlags))
                        {
                            AssertMsgFailed(("Flags mismatch (B) at %RGv! %#RX64 != %#RX64 fIgnoreFlags=%#RX64 PdeSrc=%#RX64 PdeDst=%#RX64\n",
                                            GCPtr, (uint64_t)PdeSrc.u & ~fIgnoreFlags, (uint64_t)PdeDst.u & ~fIgnoreFlags,
                                            fIgnoreFlags, (uint64_t)PdeSrc.u, (uint64_t)PdeDst.u));
                            cErrors++;
                        }

                        /* iterate the page table. */
                        for (unsigned iPT = 0, off = 0;
                            iPT < RT_ELEMENTS(pPTDst->a);
                            iPT++, off += GUEST_PAGE_SIZE, GCPhysGst = PGM_A20_APPLY(pVCpu, GCPhysGst + GUEST_PAGE_SIZE))
                        {
                            const SHWPTE PteDst = pPTDst->a[iPT];

                            if (SHW_PTE_IS_TRACK_DIRTY(PteDst))
                            {
                                AssertMsgFailed(("The PTE at %RGv emulating a 2/4M page is marked TRACK_DIRTY! PdeSrc=%#RX64 PteDst=%#RX64\n",
                                                 GCPtr + off, (uint64_t)PdeSrc.u, SHW_PTE_LOG64(PteDst)));
                                cErrors++;
                            }

                            /* skip not-present entries. */
                            if (!SHW_PTE_IS_P(PteDst)) /** @todo deal with ALL handlers and CSAM !P pages! */
                                continue;

                            fIgnoreFlags = X86_PTE_PAE_PG_MASK | X86_PTE_AVL_MASK | X86_PTE_PWT | X86_PTE_PCD | X86_PTE_PAT | X86_PTE_D | X86_PTE_A | X86_PTE_G | X86_PTE_PAE_NX;

                            /* match the physical addresses */
                            HCPhysShw = SHW_PTE_GET_HCPHYS(PteDst);

#  ifdef IN_RING3
                            rc = PGMPhysGCPhys2HCPhys(pVM, GCPhysGst, &HCPhys);
                            if (RT_FAILURE(rc))
                            {
#   if 0
                                if (HCPhysShw != MMR3PageDummyHCPhys(pVM))  /** @todo this is wrong. */
                                {
                                    AssertMsgFailed(("Cannot find guest physical address %RGp at %RGv! PdeSrc=%#RX64 PteDst=%#RX64\n",
                                                     GCPhysGst, GCPtr + off, (uint64_t)PdeSrc.u, SHW_PTE_LOG64(PteDst)));
                                    cErrors++;
                                }
#   endif
                            }
                            else if (HCPhysShw != (HCPhys & X86_PTE_PAE_PG_MASK))
                            {
                                AssertMsgFailed(("Out of sync (phys) at %RGv! HCPhysShw=%RHp HCPhys=%RHp GCPhysGst=%RGp PdeSrc=%#RX64 PteDst=%#RX64\n",
                                                 GCPtr + off, HCPhysShw, HCPhys, GCPhysGst, (uint64_t)PdeSrc.u, SHW_PTE_LOG64(PteDst)));
                                cErrors++;
                                continue;
                            }
#  endif
                            pPhysPage = pgmPhysGetPage(pVM, GCPhysGst);
                            if (!pPhysPage)
                            {
#  if 0 /** @todo make MMR3PageDummyHCPhys an 'All' function! */
                                if (HCPhysShw != MMR3PageDummyHCPhys(pVM))  /** @todo this is wrong. */
                                {
                                    AssertMsgFailed(("Cannot find guest physical address %RGp at %RGv! PdeSrc=%#RX64 PteDst=%#RX64\n",
                                                     GCPhysGst, GCPtr + off, (uint64_t)PdeSrc.u, SHW_PTE_LOG64(PteDst)));
                                    cErrors++;
                                    continue;
                                }
#  endif
                                if (SHW_PTE_IS_RW(PteDst))
                                {
                                    AssertMsgFailed(("Invalid guest page at %RGv is writable! GCPhysGst=%RGp PdeSrc=%#RX64 PteDst=%#RX64\n",
                                                     GCPtr + off, GCPhysGst, (uint64_t)PdeSrc.u, SHW_PTE_LOG64(PteDst)));
                                    cErrors++;
                                }
                                fIgnoreFlags |= X86_PTE_RW;
                            }
                            else if (HCPhysShw != PGM_PAGE_GET_HCPHYS(pPhysPage))
                            {
                                AssertMsgFailed(("Out of sync (phys) at %RGv! HCPhysShw=%RHp pPhysPage=%R[pgmpage] GCPhysGst=%RGp PdeSrc=%#RX64 PteDst=%#RX64\n",
                                                 GCPtr + off, HCPhysShw, pPhysPage, GCPhysGst, (uint64_t)PdeSrc.u, SHW_PTE_LOG64(PteDst)));
                                cErrors++;
                                continue;
                            }

                            /* flags */
                            if (PGM_PAGE_HAS_ACTIVE_HANDLERS(pPhysPage))
                            {
                                if (!PGM_PAGE_HAS_ACTIVE_ALL_HANDLERS(pPhysPage))
                                {
                                    if (PGM_PAGE_GET_HNDL_PHYS_STATE(pPhysPage) != PGM_PAGE_HNDL_PHYS_STATE_DISABLED)
                                    {
                                        if (   SHW_PTE_IS_RW(PteDst)
                                            && !PGM_PAGE_IS_HNDL_PHYS_NOT_IN_HM(pPhysPage))
                                        {
                                            AssertMsgFailed(("WRITE access flagged at %RGv but the page is writable! pPhysPage=%R[pgmpage] PdeSrc=%#RX64 PteDst=%#RX64\n",
                                                             GCPtr + off, pPhysPage, (uint64_t)PdeSrc.u, SHW_PTE_LOG64(PteDst)));
                                            cErrors++;
                                            continue;
                                        }
                                        fIgnoreFlags |= X86_PTE_RW;
                                    }
                                }
                                else
                                {
                                    if (   SHW_PTE_IS_P(PteDst)
                                        && !PGM_PAGE_IS_HNDL_PHYS_NOT_IN_HM(pPhysPage)
#  if PGM_SHW_TYPE == PGM_TYPE_EPT || PGM_SHW_TYPE == PGM_TYPE_PAE || PGM_SHW_TYPE == PGM_TYPE_AMD64
                                        && !PGM_PAGE_IS_MMIO(pPhysPage)
#  endif
                                        )
                                    {
                                        AssertMsgFailed(("ALL access flagged at %RGv but the page is present! pPhysPage=%R[pgmpage] PdeSrc=%#RX64 PteDst=%#RX64\n",
                                                         GCPtr + off, pPhysPage, (uint64_t)PdeSrc.u, SHW_PTE_LOG64(PteDst)));
                                        cErrors++;
                                        continue;
                                    }
                                    fIgnoreFlags |= X86_PTE_P;
                                }
                            }

                            if (    (PdeSrc.u & ~fIgnoreFlags)                != (SHW_PTE_GET_U(PteDst) & ~fIgnoreFlags)
                                &&  (PdeSrc.u & ~(fIgnoreFlags | X86_PTE_RW)) != (SHW_PTE_GET_U(PteDst) & ~fIgnoreFlags) /* lazy phys handler dereg. */
                            )
                            {
                                AssertMsgFailed(("Flags mismatch (BT) at %RGv! %#RX64 != %#RX64 fIgnoreFlags=%#RX64 PdeSrc=%#RX64 PteDst=%#RX64\n",
                                                 GCPtr + off, (uint64_t)PdeSrc.u & ~fIgnoreFlags, SHW_PTE_LOG64(PteDst) & ~fIgnoreFlags,
                                                 fIgnoreFlags, (uint64_t)PdeSrc.u, SHW_PTE_LOG64(PteDst)));
                                cErrors++;
                                continue;
                            }
                        } /* for each PTE */
                    }
                }
                /* not present */

            } /* for each PDE */

        } /* for each PDPTE */

    } /* for each PML4E */

#  ifdef DEBUG
    if (cErrors)
        LogFlow(("AssertCR3: cErrors=%d\n", cErrors));
#  endif
# endif /* GST is in {32BIT, PAE, AMD64} */
    return cErrors;
#endif /* !PGM_TYPE_IS_NESTED_OR_EPT(PGM_SHW_TYPE) && PGM_SHW_TYPE != PGM_TYPE_NONE */
}
#endif /* VBOX_STRICT */


/**
 * Sets up the CR3 for shadow paging
 *
 * @returns Strict VBox status code.
 * @retval  VINF_SUCCESS.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   GCPhysCR3       The physical address in the CR3 register. (A20 mask
 *                          already applied.)
 */
PGM_BTH_DECL(int, MapCR3)(PVMCPUCC pVCpu, RTGCPHYS GCPhysCR3)
{
    PVMCC pVM = pVCpu->CTX_SUFF(pVM); NOREF(pVM);
    int rc = VINF_SUCCESS;

    /* Update guest paging info. */
#if PGM_GST_TYPE == PGM_TYPE_32BIT \
 || PGM_GST_TYPE == PGM_TYPE_PAE \
 || PGM_GST_TYPE == PGM_TYPE_AMD64

    LogFlow(("MapCR3: %RGp\n", GCPhysCR3));
    PGM_A20_ASSERT_MASKED(pVCpu, GCPhysCR3);

# if PGM_GST_TYPE == PGM_TYPE_PAE
    if (   !pVCpu->pgm.s.CTX_SUFF(fPaePdpesAndCr3Mapped)
        ||  pVCpu->pgm.s.GCPhysPaeCR3 != GCPhysCR3)
# endif
    {
        /*
         * Map the page CR3 points at.
         */
        RTHCPTR HCPtrGuestCR3;
        rc = pgmGstMapCr3(pVCpu, GCPhysCR3, &HCPtrGuestCR3);
        if (RT_SUCCESS(rc))
        {
# if PGM_GST_TYPE == PGM_TYPE_32BIT
#  ifdef IN_RING3
            pVCpu->pgm.s.pGst32BitPdR3 = (PX86PD)HCPtrGuestCR3;
            pVCpu->pgm.s.pGst32BitPdR0 = NIL_RTR0PTR;
#  else
            pVCpu->pgm.s.pGst32BitPdR3 = NIL_RTR3PTR;
            pVCpu->pgm.s.pGst32BitPdR0 = (PX86PD)HCPtrGuestCR3;
#  endif

# elif PGM_GST_TYPE == PGM_TYPE_PAE
#  ifdef IN_RING3
            pVCpu->pgm.s.pGstPaePdptR3 = (PX86PDPT)HCPtrGuestCR3;
            pVCpu->pgm.s.pGstPaePdptR0 = NIL_RTR0PTR;
#  else
            pVCpu->pgm.s.pGstPaePdptR3 = NIL_RTR3PTR;
            pVCpu->pgm.s.pGstPaePdptR0 = (PX86PDPT)HCPtrGuestCR3;
#  endif

            X86PDPE aGstPaePdpes[X86_PG_PAE_PDPE_ENTRIES];
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
            /*
             * When EPT is enabled by the nested-hypervisor and the nested-guest is in PAE mode,
             * the guest-CPU context would've already been updated with the 4 PAE PDPEs specified
             * in the virtual VMCS. The PDPEs can differ from those in guest memory referenced by
             * the translated nested-guest CR3. We -MUST- use the PDPEs provided in the virtual VMCS
             * rather than those in guest memory.
             *
             * See Intel spec. 26.3.2.4 "Loading Page-Directory-Pointer-Table Entries".
             */
            if (pVCpu->pgm.s.enmGuestSlatMode == PGMSLAT_EPT)
                CPUMGetGuestPaePdpes(pVCpu, &aGstPaePdpes[0]);
            else
#endif
            {
                /* Update CPUM with the PAE PDPEs referenced by CR3. */
                memcpy(&aGstPaePdpes, HCPtrGuestCR3, sizeof(aGstPaePdpes));
                CPUMSetGuestPaePdpes(pVCpu, &aGstPaePdpes[0]);
            }

            /*
             * Map the 4 PAE PDPEs.
             */
            rc = PGMGstMapPaePdpes(pVCpu, &aGstPaePdpes[0]);
            if (RT_SUCCESS(rc))
            {
#  ifdef IN_RING3
                pVCpu->pgm.s.fPaePdpesAndCr3MappedR3 = true;
                pVCpu->pgm.s.fPaePdpesAndCr3MappedR0 = false;
#  else
                pVCpu->pgm.s.fPaePdpesAndCr3MappedR3 = false;
                pVCpu->pgm.s.fPaePdpesAndCr3MappedR0 = true;
#  endif
                pVCpu->pgm.s.GCPhysPaeCR3 = GCPhysCR3;
            }

# elif PGM_GST_TYPE == PGM_TYPE_AMD64
#  ifdef IN_RING3
            pVCpu->pgm.s.pGstAmd64Pml4R3 = (PX86PML4)HCPtrGuestCR3;
            pVCpu->pgm.s.pGstAmd64Pml4R0 = NIL_RTR0PTR;
#  else
            pVCpu->pgm.s.pGstAmd64Pml4R3 = NIL_RTR3PTR;
            pVCpu->pgm.s.pGstAmd64Pml4R0 = (PX86PML4)HCPtrGuestCR3;
#  endif
# endif
        }
        else
            AssertMsgFailed(("rc=%Rrc GCPhysGuestPD=%RGp\n", rc, GCPhysCR3));
    }
#endif

    /*
     * Update shadow paging info for guest modes with paging (32-bit, PAE, AMD64).
     */
# if  (   (   PGM_SHW_TYPE == PGM_TYPE_32BIT \
           || PGM_SHW_TYPE == PGM_TYPE_PAE    \
           || PGM_SHW_TYPE == PGM_TYPE_AMD64) \
       && (   PGM_GST_TYPE != PGM_TYPE_REAL   \
           && PGM_GST_TYPE != PGM_TYPE_PROT))

    Assert(!pVM->pgm.s.fNestedPaging);
    PGM_A20_ASSERT_MASKED(pVCpu, GCPhysCR3);

    /*
     * Update the shadow root page as well since that's not fixed.
     */
    PPGMPOOL     pPool             = pVM->pgm.s.CTX_SUFF(pPool);
    PPGMPOOLPAGE pOldShwPageCR3    = pVCpu->pgm.s.CTX_SUFF(pShwPageCR3);
    PPGMPOOLPAGE pNewShwPageCR3;

    PGM_LOCK_VOID(pVM);

# ifdef PGMPOOL_WITH_OPTIMIZED_DIRTY_PT
    if (pPool->cDirtyPages)
        pgmPoolResetDirtyPages(pVM);
# endif

    Assert(!(GCPhysCR3 >> (GUEST_PAGE_SHIFT + 32))); /** @todo what is this for? */
    int const rc2 = pgmPoolAlloc(pVM, GCPhysCR3 & GST_CR3_PAGE_MASK, BTH_PGMPOOLKIND_ROOT, PGMPOOLACCESS_DONTCARE,
                                 PGM_A20_IS_ENABLED(pVCpu), NIL_PGMPOOL_IDX, UINT32_MAX, true /*fLockPage*/, &pNewShwPageCR3);
    AssertFatalRC(rc2);

    pVCpu->pgm.s.pShwPageCR3R3 = pgmPoolConvertPageToR3(pPool, pNewShwPageCR3);
    pVCpu->pgm.s.pShwPageCR3R0 = pgmPoolConvertPageToR0(pPool, pNewShwPageCR3);

    /* Set the current hypervisor CR3. */
    CPUMSetHyperCR3(pVCpu, PGMGetHyperCR3(pVCpu));

    /* Clean up the old CR3 root. */
    if (    pOldShwPageCR3
        &&  pOldShwPageCR3 != pNewShwPageCR3    /* @todo can happen due to incorrect syncing between REM & PGM; find the real cause */)
    {
        Assert(pOldShwPageCR3->enmKind != PGMPOOLKIND_FREE);

        /* Mark the page as unlocked; allow flushing again. */
        pgmPoolUnlockPage(pPool, pOldShwPageCR3);

        pgmPoolFreeByPage(pPool, pOldShwPageCR3, NIL_PGMPOOL_IDX, UINT32_MAX);
    }
    PGM_UNLOCK(pVM);
# else
    NOREF(GCPhysCR3);
# endif

    return rc;
}

/**
 * Unmaps the shadow CR3.
 *
 * @returns VBox status, no specials.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
PGM_BTH_DECL(int, UnmapCR3)(PVMCPUCC pVCpu)
{
    LogFlow(("UnmapCR3\n"));

    int   rc  = VINF_SUCCESS;
    PVMCC pVM = pVCpu->CTX_SUFF(pVM); NOREF(pVM);

    /*
     * Update guest paging info.
     */
#if PGM_GST_TYPE == PGM_TYPE_32BIT
    pVCpu->pgm.s.pGst32BitPdR3 = 0;
    pVCpu->pgm.s.pGst32BitPdR0 = 0;

#elif PGM_GST_TYPE == PGM_TYPE_PAE
    pVCpu->pgm.s.pGstPaePdptR3 = 0;
    pVCpu->pgm.s.pGstPaePdptR0 = 0;
    for (unsigned i = 0; i < X86_PG_PAE_PDPE_ENTRIES; i++)
    {
        pVCpu->pgm.s.apGstPaePDsR3[i]    = 0;
        pVCpu->pgm.s.apGstPaePDsR0[i]    = 0;
        pVCpu->pgm.s.aGCPhysGstPaePDs[i] = NIL_RTGCPHYS;
    }

#elif PGM_GST_TYPE == PGM_TYPE_AMD64
    pVCpu->pgm.s.pGstAmd64Pml4R3 = 0;
    pVCpu->pgm.s.pGstAmd64Pml4R0 = 0;

#else /* prot/real mode stub */
    /* nothing to do */
#endif

    /*
     * PAE PDPEs (and CR3) might have been mapped via PGMGstMapPaePdpesAtCr3()
     * prior to switching to PAE in pfnMapCr3(), so we need to clear them here.
     */
    pVCpu->pgm.s.fPaePdpesAndCr3MappedR3 = false;
    pVCpu->pgm.s.fPaePdpesAndCr3MappedR0 = false;
    pVCpu->pgm.s.GCPhysPaeCR3            = NIL_RTGCPHYS;

    /*
     * Update shadow paging info.
     */
#if  (   (   PGM_SHW_TYPE == PGM_TYPE_32BIT \
          || PGM_SHW_TYPE == PGM_TYPE_PAE \
          || PGM_SHW_TYPE == PGM_TYPE_AMD64))
# if PGM_GST_TYPE != PGM_TYPE_REAL
    Assert(!pVM->pgm.s.fNestedPaging);
# endif
    PGM_LOCK_VOID(pVM);

    if (pVCpu->pgm.s.CTX_SUFF(pShwPageCR3))
    {
        PPGMPOOL pPool = pVM->pgm.s.CTX_SUFF(pPool);

# ifdef PGMPOOL_WITH_OPTIMIZED_DIRTY_PT
        if (pPool->cDirtyPages)
            pgmPoolResetDirtyPages(pVM);
# endif

        /* Mark the page as unlocked; allow flushing again. */
        pgmPoolUnlockPage(pPool, pVCpu->pgm.s.CTX_SUFF(pShwPageCR3));

        pgmPoolFreeByPage(pPool, pVCpu->pgm.s.CTX_SUFF(pShwPageCR3), NIL_PGMPOOL_IDX, UINT32_MAX);
        pVCpu->pgm.s.pShwPageCR3R3 = 0;
        pVCpu->pgm.s.pShwPageCR3R0 = 0;
    }

    PGM_UNLOCK(pVM);
#endif

    return rc;
}

