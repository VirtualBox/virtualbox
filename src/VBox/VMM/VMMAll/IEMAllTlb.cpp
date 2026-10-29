/* $Id: IEMAllTlb.cpp 112403 2026-01-11 19:29:08Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - TLB Management.
 */

/*
 * Copyright (C) 2011-2026 Oracle and/or its affiliates.
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
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP   LOG_GROUP_IEM
#define VMCPU_INCL_CPUM_GST_CTX
#include <VBox/vmm/iem.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/dbgf.h>
#include "IEMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/x86.h>

#include "IEMInline.h"
#ifdef VBOX_VMM_TARGET_X86
# include "target-x86/IEMAllTlbInline-x86.h"
#elif defined(VBOX_VMM_TARGET_ARMV8)
# include "target-armv8/IEMAllTlbInline-armv8.h"
#endif



#if defined(IEM_WITH_CODE_TLB_IN_CUR_CTX) || defined(IEM_WITH_DATA_TLB_IN_CUR_CTX)
/**
 * Worker for iemTlbInvalidateAll.
 */
template<bool a_fGlobal>
DECL_FORCE_INLINE(void) iemTlbInvalidateOne(IEMTLB *pTlb)
{
    if (!a_fGlobal)
        pTlb->cTlsFlushes++;
    else
        pTlb->cTlsGlobalFlushes++;

    pTlb->uTlbRevision += IEMTLB_REVISION_INCR;
    if (RT_LIKELY(pTlb->uTlbRevision != 0))
    { /* very likely */ }
    else
    {
        pTlb->uTlbRevision = IEMTLB_REVISION_INCR;
        pTlb->cTlbRevisionRollovers++;
        unsigned i = RT_ELEMENTS(pTlb->aEntries) / 2;
        while (i-- > 0)
            pTlb->aEntries[i * 2].uTag = 0;
    }

# ifdef VBOX_VMM_TARGET_X86
    pTlb->cTlbNonGlobalLargePageCurLoads    = 0;
    pTlb->NonGlobalLargePageRange.uLastTag  = 0;
    pTlb->NonGlobalLargePageRange.uFirstTag = UINT64_MAX;
# else
    pTlb->cTlbLargePageCurLoads    = 0;
    pTlb->LargePageRange.uLastTag  = 0;
    pTlb->LargePageRange.uFirstTag = UINT64_MAX;
# endif

# ifdef VBOX_VMM_TARGET_X86
    if (a_fGlobal)
    {
        pTlb->uTlbRevisionGlobal += IEMTLB_REVISION_INCR;
        if (RT_LIKELY(pTlb->uTlbRevisionGlobal != 0))
        { /* very likely */ }
        else
        {
            pTlb->uTlbRevisionGlobal = IEMTLB_REVISION_INCR;
            pTlb->cTlbRevisionRollovers++;
            unsigned i = RT_ELEMENTS(pTlb->aEntries) / 2;
            while (i-- > 0)
                pTlb->aEntries[i * 2 + 1].uTag = 0;
        }

        pTlb->cTlbGlobalLargePageCurLoads    = 0;
        pTlb->GlobalLargePageRange.uLastTag  = 0;
        pTlb->GlobalLargePageRange.uFirstTag = UINT64_MAX;
    }
# endif
}
#endif


/**
 * Worker for IEMTlbInvalidateAll and IEMTlbInvalidateAllGlobal.
 */
template<bool a_fGlobal>
DECL_FORCE_INLINE(void) iemTlbInvalidateAll(PVMCPUCC pVCpu)
{
#if defined(IEM_WITH_CODE_TLB_IN_CUR_CTX) || defined(IEM_WITH_DATA_TLB_IN_CUR_CTX)
    Log10(("IEMTlbInvalidateAll\n"));

# ifdef IEM_WITH_CODE_TLB_IN_CUR_CTX
    ICORE_R3(pVCpu).cbInstrBufTotal = 0;
    iemTlbInvalidateOne<a_fGlobal>(&ITLBS_R3(pVCpu).Code);
#  ifdef VBOX_VMM_TARGET_X86
    if (a_fGlobal)
        IEMTLBTRACE_FLUSH_GLOBAL(pVCpu, ITLBS_R3(pVCpu).Code.uTlbRevision, ITLBS_R3(pVCpu).Code.uTlbRevisionGlobal, false);
    else
#  endif
        IEMTLBTRACE_FLUSH(pVCpu, ITLBS_R3(pVCpu).Code.uTlbRevision, false);
# endif

# ifdef IEM_WITH_DATA_TLB_IN_CUR_CTX
    iemTlbInvalidateOne<a_fGlobal>(&ITLBS_R3(pVCpu).Data);
#  ifdef VBOX_VMM_TARGET_X86
    if (a_fGlobal)
        IEMTLBTRACE_FLUSH_GLOBAL(pVCpu, ITLBS_R3(pVCpu).Data.uTlbRevision, ITLBS_R3(pVCpu).Data.uTlbRevisionGlobal, true);
    else
#  endif
        IEMTLBTRACE_FLUSH(pVCpu, ITLBS_R3(pVCpu).Data.uTlbRevision, true);
# endif
#else
    RT_NOREF(pVCpu);
#endif
}


/**
 * Invalidates non-global the IEM TLB entries.
 *
 * This is called internally as well as by PGM when moving GC mappings.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the calling
 *                      thread.
 */
VMM_INT_DECL(void) IEMTlbInvalidateAll(PVMCPUCC pVCpu)
{
    iemTlbInvalidateAll<false>(pVCpu);
}


/**
 * Invalidates all the IEM TLB entries.
 *
 * This is called internally as well as by PGM when moving GC mappings.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the calling
 *                      thread.
 */
VMM_INT_DECL(void) IEMTlbInvalidateAllGlobal(PVMCPUCC pVCpu)
{
    iemTlbInvalidateAll<true>(pVCpu);
}


/**
 * Invalidates a page in the TLBs.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the calling
 *                      thread.
 * @param   GCPtr       The address of the page to invalidate
 * @thread EMT(pVCpu)
 */
VMM_INT_DECL(void) IEMTlbInvalidatePage(PVMCPUCC pVCpu, RTGCPTR GCPtr)
{
    IEMTLBTRACE_INVLPG(pVCpu, GCPtr);
#if defined(IEM_WITH_CODE_TLB_IN_CUR_CTX) || defined(IEM_WITH_DATA_TLB_IN_CUR_CTX)
    Log10(("IEMTlbInvalidatePage: GCPtr=%RGv\n", GCPtr));
    GCPtr = IEMTLB_CALC_TAG_NO_REV(pVCpu, GCPtr);
    Assert(!(GCPtr >> (48 - X86_PAGE_SHIFT)));
    uintptr_t const idx = IEMTLB_TAG_TO_INDEX(GCPtr);

# ifdef IEM_WITH_CODE_TLB_IN_CUR_CTX
    iemTlbInvalidatePageWorker<false>(pVCpu, &ITLBS_R3(pVCpu).Code, GCPtr, idx);
# endif
# ifdef IEM_WITH_DATA_TLB_IN_CUR_CTX
    iemTlbInvalidatePageWorker<true>(pVCpu, &ITLBS_R3(pVCpu).Data, GCPtr, idx);
# endif
#else
    NOREF(pVCpu); NOREF(GCPtr);
#endif
}


#if defined(IEM_WITH_CODE_TLB) || defined(IEM_WITH_DATA_TLB)
/**
 * Invalid both TLBs slow fashion following a rollover.
 *
 * Worker for IEMTlbInvalidateAllPhysical,
 * IEMTlbInvalidateAllPhysicalAllCpus, iemOpcodeFetchBytesJmp, iemMemMap,
 * iemMemMapJmp and others.
 *
 * @thread EMT(pVCpu)
 */
void iemTlbInvalidateAllPhysicalSlow(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    Log10(("iemTlbInvalidateAllPhysicalSlow\n"));
# ifdef VBOX_VMM_TARGET_ARMV8
    uint64_t const uTlbPhysRevAndStuff0 = (ITLBS_R3(pVCpu).Code.uTlbPhysRevAndStuff0 & IEMTLB_STUFF_BITS)
                                        | (IEMTLB_PHYS_REV_INCR * 2);
    uint64_t const uTlbPhysRevAndStuff1 = (ITLBS_R3(pVCpu).Code.uTlbPhysRevAndStuff1 & IEMTLB_STUFF_BITS)
                                        | (IEMTLB_PHYS_REV_INCR * 2);
    ASMAtomicWriteU64(&ITLBS_R3(pVCpu).Code.uTlbPhysRevAndStuff0, uTlbPhysRevAndStuff0);
    ASMAtomicWriteU64(&ITLBS_R3(pVCpu).Code.uTlbPhysRevAndStuff1, uTlbPhysRevAndStuff1);
    ASMAtomicWriteU64(&ITLBS_R3(pVCpu).Data.uTlbPhysRevAndStuff0, uTlbPhysRevAndStuff0);
    ASMAtomicWriteU64(&ITLBS_R3(pVCpu).Data.uTlbPhysRevAndStuff1, uTlbPhysRevAndStuff1);
# else
    ASMAtomicWriteU64(&ITLBS_R3(pVCpu).Code.uTlbPhysRev, IEMTLB_PHYS_REV_INCR * 2);
    ASMAtomicWriteU64(&ITLBS_R3(pVCpu).Data.uTlbPhysRev, IEMTLB_PHYS_REV_INCR * 2);
# endif

    unsigned i;
# ifdef IEM_WITH_CODE_TLB
    i = RT_ELEMENTS(ITLBS_R3(pVCpu).Code.aEntries);
    while (i-- > 0)
    {
        ITLBS_R3(pVCpu).Code.aEntries[i].pbMappingR3       = NULL;
        ITLBS_R3(pVCpu).Code.aEntries[i].fFlagsAndPhysRev &= ~(  IEMTLBE_F_PG_NO_WRITE   | IEMTLBE_F_PG_NO_READ
                                                               | IEMTLBE_F_PG_UNASSIGNED | IEMTLBE_F_PHYS_REV);
    }
    ITLBS_R3(pVCpu).Code.cTlbPhysRevRollovers++;
    ITLBS_R3(pVCpu).Code.cTlbPhysRevFlushes++;
# endif
# ifdef IEM_WITH_DATA_TLB
    i = RT_ELEMENTS(ITLBS_R3(pVCpu).Data.aEntries);
    while (i-- > 0)
    {
        ITLBS_R3(pVCpu).Data.aEntries[i].pbMappingR3       = NULL;
        ITLBS_R3(pVCpu).Data.aEntries[i].fFlagsAndPhysRev &= ~(  IEMTLBE_F_PG_NO_WRITE   | IEMTLBE_F_PG_NO_READ
                                                               | IEMTLBE_F_PG_UNASSIGNED | IEMTLBE_F_PHYS_REV);
    }
    ITLBS_R3(pVCpu).Data.cTlbPhysRevRollovers++;
    ITLBS_R3(pVCpu).Data.cTlbPhysRevFlushes++;
# endif

}
#endif


/**
 * Invalidates the host physical aspects of the IEM TLBs.
 *
 * This is called internally as well as by PGM when moving GC mappings.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the calling
 *                      thread.
 * @note    Currently not used.
 */
VMM_INT_DECL(void) IEMTlbInvalidateAllPhysical(PVMCPUCC pVCpu)
{
#if defined(IEM_WITH_CODE_TLB) || defined(IEM_WITH_DATA_TLB)
    /* Note! This probably won't end up looking exactly like this, but it give an idea... */
    Log10(("IEMTlbInvalidateAllPhysical\n"));

# ifdef IEM_WITH_CODE_TLB
    ICORE_R3(pVCpu).cbInstrBufTotal = 0;
# endif
# ifdef VBOX_VMM_TARGET_ARMV8
    uint64_t const uTlbPhysRev1 = ITLBS_R3(pVCpu).Code.uTlbPhysRevAndStuff1 + IEMTLB_PHYS_REV_INCR;
    uint64_t const uTlbPhysRev  = ITLBS_R3(pVCpu).Code.uTlbPhysRevAndStuff0 + IEMTLB_PHYS_REV_INCR;
# else
    uint64_t const uTlbPhysRev  = ITLBS_R3(pVCpu).Code.uTlbPhysRev + IEMTLB_PHYS_REV_INCR;
# endif
    if (RT_LIKELY(uTlbPhysRev > IEMTLB_PHYS_REV_INCR * 2))
    {
# ifdef VBOX_VMM_TARGET_ARMV8
        ITLBS_R3(pVCpu).Code.uTlbPhysRevAndStuff0 = uTlbPhysRev;
        ITLBS_R3(pVCpu).Code.uTlbPhysRevAndStuff1 = uTlbPhysRev1;
        ITLBS_R3(pVCpu).Data.uTlbPhysRevAndStuff0 = uTlbPhysRev;
        ITLBS_R3(pVCpu).Data.uTlbPhysRevAndStuff1 = uTlbPhysRev1;
# else
        ITLBS_R3(pVCpu).Code.uTlbPhysRev = uTlbPhysRev;
        ITLBS_R3(pVCpu).Data.uTlbPhysRev = uTlbPhysRev;
# endif
        ITLBS_R3(pVCpu).Code.cTlbPhysRevFlushes++;
        ITLBS_R3(pVCpu).Data.cTlbPhysRevFlushes++;
    }
    else
        iemTlbInvalidateAllPhysicalSlow(pVCpu);

#else
    NOREF(pVCpu);
#endif
}


/**
 * Invalidates the host physical aspects of the IEM TLBs.
 *
 * This is called internally as well as by PGM when moving GC mappings.
 *
 * @param   pVM         The cross context VM structure.
 * @param   idCpuCaller The ID of the calling EMT if available to the caller,
 *                      otherwise NIL_VMCPUID.
 * @param   enmReason   The reason we're called.
 *
 * @remarks Caller holds the PGM lock.
 */
VMM_INT_DECL(void) IEMTlbInvalidateAllPhysicalAllCpus(PVMCC pVM, VMCPUID idCpuCaller, IEMTLBPHYSFLUSHREASON enmReason)
{
#if defined(IEM_WITH_CODE_TLB) || defined(IEM_WITH_DATA_TLB)
    PVMCPUCC const pVCpuCaller = idCpuCaller >= pVM->cCpus ? VMMGetCpu(pVM) : VMMGetCpuById(pVM, idCpuCaller);
    if (pVCpuCaller)
        VMCPU_ASSERT_EMT(pVCpuCaller);
    Log10(("IEMTlbInvalidateAllPhysicalAllCpus: %d\n", enmReason)); RT_NOREF(enmReason);

    VMCC_FOR_EACH_VMCPU(pVM)
    {
# ifdef IEM_WITH_CODE_TLB
        if (pVCpuCaller == pVCpu)
            ICORE_R3(pVCpu).cbInstrBufTotal = 0;
# endif
        /*
         * The TLBs have the same physical revision at all time (except when its
         * increased), so use the (first) one from Tlbs.Code and increase it.
         */
# ifdef VBOX_VMM_TARGET_ARMV8
        uint64_t const uTlbPhysRevPrev = ASMAtomicUoReadU64(&ITLBS_R3(pVCpu).Code.uTlbPhysRevAndStuff0) & IEMTLBE_F_PHYS_REV;
# else
        uint64_t const uTlbPhysRevPrev = ASMAtomicUoReadU64(&ITLBS_R3(pVCpu).Code.uTlbPhysRev);
# endif
        uint64_t       uTlbPhysRevNew  = uTlbPhysRevPrev + IEMTLB_PHYS_REV_INCR;
        if (RT_LIKELY(uTlbPhysRevNew > IEMTLB_PHYS_REV_INCR * 2))
        { /* likely */}
        else if (pVCpuCaller != pVCpu)
            uTlbPhysRevNew = IEMTLB_PHYS_REV_INCR;
        else
        {
            iemTlbInvalidateAllPhysicalSlow(pVCpu);
            continue;
        }

        /*
         * Apply the increased value on the other copies.
         *
         * The ARM variant ASSUMES that the PGM locking prevents races wrt the
         * physical revision, though it has to deal with other vCPUs changing
         * the ASID, VMID, and stuff while we're here...
         */
# ifdef VBOX_VMM_TARGET_ARMV8
        uint64_t *apuTlbPhysRevAndStuff0[4] =
        {
            &ITLBS_R3(pVCpu).Code.uTlbPhysRevAndStuff0,
            &ITLBS_R3(pVCpu).Code.uTlbPhysRevAndStuff1,
            &ITLBS_R3(pVCpu).Data.uTlbPhysRevAndStuff0,
            &ITLBS_R3(pVCpu).Data.uTlbPhysRevAndStuff1,
        };
        for (unsigned i = 0; i < RT_ELEMENTS(apuTlbPhysRevAndStuff0); i++)
        {
            uint64_t * const puTlbPhysRevAndStuff = apuTlbPhysRevAndStuff0[i];
            for (;;)
            {
                uint64_t const uPrev = ASMAtomicUoReadU64(puTlbPhysRevAndStuff);
                Assert((uPrev & IEMTLBE_F_PHYS_REV) == uTlbPhysRevPrev);
                if (ASMAtomicCmpXchgU64(puTlbPhysRevAndStuff, uTlbPhysRevNew | (uPrev & IEMTLB_STUFF_BITS), uPrev))
                    break;
                ASMNopPause();
            }
        }
        ITLBS_R3(pVCpu).Code.cTlbPhysRevFlushes++;
        ITLBS_R3(pVCpu).Data.cTlbPhysRevFlushes++;

# else
        if (ASMAtomicCmpXchgU64(&ITLBS_R3(pVCpu).Code.uTlbPhysRev, uTlbPhysRevNew, uTlbPhysRevPrev))
            ITLBS_R3(pVCpu).Code.cTlbPhysRevFlushes++;
        else
            AssertFailed();

        if (ASMAtomicCmpXchgU64(&ITLBS_R3(pVCpu).Data.uTlbPhysRev, uTlbPhysRevNew, uTlbPhysRevPrev))
            ITLBS(pVCpu).Data.cTlbPhysRevFlushes++;
        else
            AssertFailed();
# endif
    }
    VMCC_FOR_EACH_VMCPU_END(pVM);

#else
    RT_NOREF(pVM, idCpuCaller, enmReason);
#endif
}

