/* $Id: IEMInline.h 110424 2025-07-28 07:29:06Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - Inlined Functions, Common.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#ifndef VMM_INCLUDED_SRC_include_IEMInline_h
#define VMM_INCLUDED_SRC_include_IEMInline_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/err.h>


/* Documentation and forward declarations for inline functions required for every target: */

RT_NO_WARN_UNUSED_INLINE_PROTOTYPE_BEGIN

RT_NO_WARN_UNUSED_INLINE_PROTOTYPE_END


/**
 * Makes status code addjustments (pass up from I/O and access handler)
 * as well as maintaining statistics.
 *
 * @returns Strict VBox status code to pass up.
 * @param   pVCpu       The cross context virtual CPU structure of the calling thread.
 * @param   rcStrict    The status from executing an instruction.
 */
DECL_FORCE_INLINE(VBOXSTRICTRC) iemExecStatusCodeFiddling(PVMCPUCC pVCpu, VBOXSTRICTRC rcStrict) RT_NOEXCEPT
{
    if (rcStrict != VINF_SUCCESS)
    {
        /* Deal with the cases that should be treated as VINF_SUCCESS first. */
        if (   rcStrict == VINF_IEM_YIELD_PENDING_FF
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX /** @todo r=bird: Why do we need TWO status codes here? */
            || rcStrict == VINF_VMX_VMEXIT
#endif
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
            || rcStrict == VINF_SVM_VMEXIT
#endif
            )
        {
            rcStrict = pVCpu->iem.s.rcPassUp;
            if (RT_LIKELY(rcStrict == VINF_SUCCESS))
            { /* likely */ }
            else
                pVCpu->iem.s.cRetPassUpStatus++;
        }
        else if (RT_SUCCESS(rcStrict))
        {
            AssertMsg(   (rcStrict >= VINF_EM_FIRST && rcStrict <= VINF_EM_LAST)
                      || rcStrict == VINF_IOM_R3_IOPORT_READ
                      || rcStrict == VINF_IOM_R3_IOPORT_WRITE
                      || rcStrict == VINF_IOM_R3_IOPORT_COMMIT_WRITE
                      || rcStrict == VINF_IOM_R3_MMIO_READ
                      || rcStrict == VINF_IOM_R3_MMIO_READ_WRITE
                      || rcStrict == VINF_IOM_R3_MMIO_WRITE
                      || rcStrict == VINF_IOM_R3_MMIO_COMMIT_WRITE
                      || rcStrict == VINF_CPUM_R3_MSR_READ
                      || rcStrict == VINF_CPUM_R3_MSR_WRITE
                      || rcStrict == VINF_EM_RAW_EMULATE_INSTR
                      || rcStrict == VINF_EM_RAW_TO_R3
                      || rcStrict == VINF_EM_TRIPLE_FAULT
                      || rcStrict == VINF_EM_EMULATE_SPLIT_LOCK
                      || rcStrict == VINF_GIM_R3_HYPERCALL
                      /* raw-mode / virt handlers only: */
                      || rcStrict == VINF_EM_RAW_EMULATE_INSTR_GDT_FAULT
                      || rcStrict == VINF_EM_RAW_EMULATE_INSTR_TSS_FAULT
                      || rcStrict == VINF_EM_RAW_EMULATE_INSTR_LDT_FAULT
                      || rcStrict == VINF_EM_RAW_EMULATE_INSTR_IDT_FAULT
                      || rcStrict == VINF_SELM_SYNC_GDT
                      || rcStrict == VINF_CSAM_PENDING_ACTION
                      || rcStrict == VINF_PATM_CHECK_PATCH_PAGE
                      /* nested hw.virt codes: */
                      || rcStrict == VINF_VMX_INTERCEPT_NOT_ACTIVE
                      || rcStrict == VINF_VMX_MODIFIES_BEHAVIOR
                      , ("rcStrict=%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
/** @todo adjust for VINF_EM_RAW_EMULATE_INSTR. */
            int32_t const rcPassUp = pVCpu->iem.s.rcPassUp;
            if (rcPassUp == VINF_SUCCESS)
                pVCpu->iem.s.cRetInfStatuses++;
            else if (   rcPassUp < VINF_EM_FIRST
                     || rcPassUp > VINF_EM_LAST
                     || rcPassUp < VBOXSTRICTRC_VAL(rcStrict))
            {
                LogEx(LOG_GROUP_IEM,("IEM: rcPassUp=%Rrc! rcStrict=%Rrc\n", rcPassUp, VBOXSTRICTRC_VAL(rcStrict)));
                pVCpu->iem.s.cRetPassUpStatus++;
                rcStrict = rcPassUp;
            }
            else
            {
                LogEx(LOG_GROUP_IEM,("IEM: rcPassUp=%Rrc  rcStrict=%Rrc!\n", rcPassUp, VBOXSTRICTRC_VAL(rcStrict)));
                pVCpu->iem.s.cRetInfStatuses++;
            }
        }
        else if (rcStrict == VERR_IEM_ASPECT_NOT_IMPLEMENTED)
            pVCpu->iem.s.cRetAspectNotImplemented++;
        else if (rcStrict == VERR_IEM_INSTR_NOT_IMPLEMENTED)
            pVCpu->iem.s.cRetInstrNotImplemented++;
        else
            pVCpu->iem.s.cRetErrStatuses++;
    }
    else
    {
        rcStrict = pVCpu->iem.s.rcPassUp;
        if (rcStrict != VINF_SUCCESS)
            pVCpu->iem.s.cRetPassUpStatus++;
    }

    /* Just clear it here as well. */
    pVCpu->iem.s.rcPassUp = VINF_SUCCESS;

    return rcStrict;
}


/**
 * Sets the pass up status.
 *
 * @returns VINF_SUCCESS.
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling thread.
 * @param   rcPassUp            The pass up status.  Must be informational.
 *                              VINF_SUCCESS is not allowed.
 */
DECLINLINE(int) iemSetPassUpStatus(PVMCPUCC pVCpu, VBOXSTRICTRC rcPassUp) RT_NOEXCEPT
{
    AssertRC(VBOXSTRICTRC_VAL(rcPassUp)); Assert(rcPassUp != VINF_SUCCESS);

    int32_t const rcOldPassUp = pVCpu->iem.s.rcPassUp;
    if (rcOldPassUp == VINF_SUCCESS)
        pVCpu->iem.s.rcPassUp = VBOXSTRICTRC_VAL(rcPassUp);
    /* If both are EM scheduling codes, use EM priority rules. */
    else if (   rcOldPassUp >= VINF_EM_FIRST && rcOldPassUp <= VINF_EM_LAST
             && rcPassUp    >= VINF_EM_FIRST && rcPassUp    <= VINF_EM_LAST)
    {
        if (rcPassUp < rcOldPassUp)
        {
            LogEx(LOG_GROUP_IEM,("IEM: rcPassUp=%Rrc! rcOldPassUp=%Rrc\n", VBOXSTRICTRC_VAL(rcPassUp), rcOldPassUp));
            pVCpu->iem.s.rcPassUp = VBOXSTRICTRC_VAL(rcPassUp);
        }
        else
            LogEx(LOG_GROUP_IEM,("IEM: rcPassUp=%Rrc  rcOldPassUp=%Rrc!\n", VBOXSTRICTRC_VAL(rcPassUp), rcOldPassUp));
    }
    /* Override EM scheduling with specific status code. */
    else if (rcOldPassUp >= VINF_EM_FIRST && rcOldPassUp <= VINF_EM_LAST)
    {
        LogEx(LOG_GROUP_IEM,("IEM: rcPassUp=%Rrc! rcOldPassUp=%Rrc\n", VBOXSTRICTRC_VAL(rcPassUp), rcOldPassUp));
        pVCpu->iem.s.rcPassUp = VBOXSTRICTRC_VAL(rcPassUp);
    }
    /* Don't override specific status code, first come first served. */
    else
        LogEx(LOG_GROUP_IEM,("IEM: rcPassUp=%Rrc  rcOldPassUp=%Rrc!\n", VBOXSTRICTRC_VAL(rcPassUp), rcOldPassUp));
    return VINF_SUCCESS;
}



/** @name   Memory access.
 *
 * @{
 */

/**
 * Maps a physical page.
 *
 * @returns VBox status code (see PGMR3PhysTlbGCPhys2Ptr).
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   GCPhysMem           The physical address.
 * @param   fAccess             The intended access.
 * @param   ppvMem              Where to return the mapping address.
 * @param   pLock               The PGM lock.
 */
DECLINLINE(int) iemMemPageMap(PVMCPUCC pVCpu, RTGCPHYS GCPhysMem, uint32_t fAccess,
                              void **ppvMem, PPGMPAGEMAPLOCK pLock) RT_NOEXCEPT
{
#ifdef IEM_LOG_MEMORY_WRITES
    if (fAccess & IEM_ACCESS_TYPE_WRITE)
        return VERR_PGM_PHYS_TLB_CATCH_ALL;
#endif

    /** @todo This API may require some improving later.  A private deal with PGM
     *        regarding locking and unlocking needs to be struct.  A couple of TLBs
     *        living in PGM, but with publicly accessible inlined access methods
     *        could perhaps be an even better solution. */
    int rc = PGMPhysIemGCPhys2Ptr(pVCpu->CTX_SUFF(pVM), pVCpu,
                                  GCPhysMem,
                                  RT_BOOL(fAccess & IEM_ACCESS_TYPE_WRITE),
                                  RT_BOOL(pVCpu->iem.s.fExec & IEM_F_BYPASS_HANDLERS),
                                  ppvMem,
                                  pLock);
    /*Log(("PGMPhysIemGCPhys2Ptr %Rrc pLock=%.*Rhxs\n", rc, sizeof(*pLock), pLock));*/
    AssertMsg(rc == VINF_SUCCESS || RT_FAILURE_NP(rc), ("%Rrc\n", rc));

    return rc;
}


/**
 * Unmap a page previously mapped by iemMemPageMap.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   GCPhysMem           The physical address.
 * @param   fAccess             The intended access.
 * @param   pvMem               What iemMemPageMap returned.
 * @param   pLock               The PGM lock.
 */
DECLINLINE(void) iemMemPageUnmap(PVMCPUCC pVCpu, RTGCPHYS GCPhysMem, uint32_t fAccess,
                                 const void *pvMem, PPGMPAGEMAPLOCK pLock) RT_NOEXCEPT
{
    NOREF(pVCpu);
    NOREF(GCPhysMem);
    NOREF(fAccess);
    NOREF(pvMem);
    PGMPhysReleasePageMappingLock(pVCpu->CTX_SUFF(pVM), pLock);
}


/*
 * Unmap helpers.
 */

DECL_INLINE_THROW(void) iemMemCommitAndUnmapRwJmp(PVMCPUCC pVCpu, uint8_t bMapInfo) IEM_NOEXCEPT_MAY_LONGJMP
{
#if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    if (RT_LIKELY(bMapInfo == 0))
        return;
#endif
    iemMemCommitAndUnmapRwSafeJmp(pVCpu, bMapInfo);
}


DECL_INLINE_THROW(void) iemMemCommitAndUnmapAtJmp(PVMCPUCC pVCpu, uint8_t bMapInfo) IEM_NOEXCEPT_MAY_LONGJMP
{
#if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    if (RT_LIKELY(bMapInfo == 0))
        return;
#endif
    iemMemCommitAndUnmapAtSafeJmp(pVCpu, bMapInfo);
}


DECL_INLINE_THROW(void) iemMemCommitAndUnmapWoJmp(PVMCPUCC pVCpu, uint8_t bMapInfo) IEM_NOEXCEPT_MAY_LONGJMP
{
#if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    if (RT_LIKELY(bMapInfo == 0))
        return;
#endif
    iemMemCommitAndUnmapWoSafeJmp(pVCpu, bMapInfo);
}


DECL_INLINE_THROW(void) iemMemCommitAndUnmapRoJmp(PVMCPUCC pVCpu, uint8_t bMapInfo) IEM_NOEXCEPT_MAY_LONGJMP
{
#if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    if (RT_LIKELY(bMapInfo == 0))
        return;
#endif
    iemMemCommitAndUnmapRoSafeJmp(pVCpu, bMapInfo);
}


DECLINLINE(void) iemMemRollbackAndUnmapWo(PVMCPUCC pVCpu, uint8_t bMapInfo) RT_NOEXCEPT
{
#if defined(IEM_WITH_DATA_TLB) && defined(IN_RING3)
    if (RT_LIKELY(bMapInfo == 0))
        return;
#endif
    iemMemRollbackAndUnmapWoSafe(pVCpu, bMapInfo);
}


/**
 * Helper for iemMemMap, iemMemMapJmp and iemMemBounceBufferMapCrossPage.
 */
DECL_FORCE_INLINE(uint32_t)
iemMemCheckDataBreakpoint(PVMCC pVM, PVMCPUCC pVCpu, RTGCPTR GCPtrMem, size_t cbMem, uint32_t fAccess)
{
    bool const  fSysAccess = (fAccess & IEM_ACCESS_WHAT_MASK) == IEM_ACCESS_WHAT_SYS;
    if (fAccess & IEM_ACCESS_TYPE_WRITE)
        return DBGFBpCheckDataWrite(pVM, pVCpu, GCPtrMem, (uint32_t)cbMem, fSysAccess);
    return DBGFBpCheckDataRead(pVM, pVCpu, GCPtrMem, (uint32_t)cbMem, fSysAccess);
}

/** @} */


#if defined(IEM_WITH_TLB_TRACE) && defined(IN_RING3)
/**
 * Adds an entry to the TLB trace buffer.
 *
 * @note Don't use directly, only via the IEMTLBTRACE_XXX macros.
 */
DECLINLINE(void) iemTlbTrace(PVMCPU pVCpu, IEMTLBTRACETYPE enmType, uint64_t u64Param, uint64_t u64Param2 = 0,
                             uint8_t bParam = 0, uint32_t u32Param = 0/*, uint16_t u16Param = 0 */)
{
    uint32_t const          fMask  = RT_BIT_32(pVCpu->iem.s.cTlbTraceEntriesShift) - 1;
    PIEMTLBTRACEENTRY const pEntry = &pVCpu->iem.s.paTlbTraceEntries[pVCpu->iem.s.idxTlbTraceEntry++ & fMask];
    pEntry->u64Param  = u64Param;
    pEntry->u64Param2 = u64Param2;
    pEntry->u16Param  = 0; //u16Param;
    pEntry->u32Param  = u32Param;
    pEntry->bParam    = bParam;
    pEntry->enmType   = enmType;
    pEntry->rip       = pVCpu->cpum.GstCtx.rip + pVCpu->cpum.GstCtx.cs.u64Base;
}
#endif

#endif /* !VMM_INCLUDED_SRC_include_IEMInline_h */
