/* $Id: GIMR0Hv.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Guest Interface Manager (GIM), Hyper-V - Host Context Ring-0.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_GIM
#include <VBox/vmm/gim.h>
#include <VBox/vmm/tm.h>
#include "GIMInternal.h"
#include "GIMHvInternal.h"
#include <VBox/vmm/vmcc.h>

#include <VBox/err.h>

#include <iprt/spinlock.h>


#if 0
/**
 * Allocates and maps one physically contiguous page. The allocated page is
 * zero'd out.
 *
 * @returns IPRT status code.
 * @param   pMemObj         Pointer to the ring-0 memory object.
 * @param   ppVirt          Where to store the virtual address of the
 *                          allocation.
 * @param   pPhys           Where to store the physical address of the
 *                          allocation.
 */
static int gimR0HvPageAllocZ(PRTR0MEMOBJ pMemObj, PRTR0PTR ppVirt, PRTHCPHYS pHCPhys)
{
    AssertPtr(pMemObj);
    AssertPtr(ppVirt);
    AssertPtr(pHCPhys);

    int rc = RTR0MemObjAllocCont(pMemObj, HOST_PAGE_SIZE, NIL_RTHCPHYS /*PhysHighest*/, false /* fExecutable */);
    if (RT_FAILURE(rc))
        return rc;
    *ppVirt  = RTR0MemObjAddress(*pMemObj);
    *pHCPhys = RTR0MemObjGetPagePhysAddr(*pMemObj, 0 /* iPage */);
    ASMMemZero32(*ppVirt, HOST_PAGE_SIZE);
    return VINF_SUCCESS;
}


/**
 * Frees and unmaps an allocated physical page.
 *
 * @param   pMemObj         Pointer to the ring-0 memory object.
 * @param   ppVirt          Where to re-initialize the virtual address of
 *                          allocation as 0.
 * @param   pHCPhys         Where to re-initialize the physical address of the
 *                          allocation as 0.
 */
static void gimR0HvPageFree(PRTR0MEMOBJ pMemObj, PRTR0PTR ppVirt, PRTHCPHYS pHCPhys)
{
    AssertPtr(pMemObj);
    AssertPtr(ppVirt);
    AssertPtr(pHCPhys);
    if (*pMemObj != NIL_RTR0MEMOBJ)
    {
        int rc = RTR0MemObjFree(*pMemObj, true /* fFreeMappings */);
        AssertRC(rc);
        *pMemObj = NIL_RTR0MEMOBJ;
        *ppVirt  = 0;
        *pHCPhys = 0;
    }
}
#endif

/**
 * Updates Hyper-V's reference TSC page.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   u64Offset   The computed TSC offset.
 * @thread  EMT.
 */
VMM_INT_DECL(int) gimR0HvUpdateParavirtTsc(PVMCC pVM, uint64_t u64Offset)
{
    Assert(GIMIsEnabled(pVM));
    bool fHvTscEnabled = MSR_GIM_HV_REF_TSC_IS_ENABLED(pVM->gim.s.u.Hv.u64TscPageMsr);
    if (RT_UNLIKELY(!fHvTscEnabled))
        return VERR_GIM_PVTSC_NOT_ENABLED;

    /** @todo this is buggy when large pages are used due to a PGM limitation, see
     *        @bugref{7532}.
     *
     *        In any case, we do not ever update this page while the guest is
     *        running after setting it up (in ring-3, see gimR3HvEnableTscPage()) as
     *        the TSC offset is handled in the VMCS/VMCB (HM) or by trapping RDTSC
     *        (raw-mode). */
#if 0
    PCGIMHV          pcHv     = &pVM->gim.s.u.Hv;
    PCGIMMMIO2REGION pcRegion = &pcHv->aMmio2Regions[GIM_HV_REF_TSC_PAGE_REGION_IDX];
    PGIMHVREFTSC     pRefTsc  = (PGIMHVREFTSC)pcRegion->CTX_SUFF(pvPage);
    Assert(pRefTsc);

    /*
     * Hyper-V reports the reference time in 100 nanosecond units.
     */
    uint64_t u64Tsc100Ns = pcHv->cTscTicksPerSecond / RT_NS_10MS;
    int64_t i64TscOffset = (int64_t)u64Offset / u64Tsc100Ns;

    /*
     * The TSC page can be simulatenously read by other VCPUs in the guest. The
     * spinlock is only for protecting simultaneous hypervisor writes from other
     * EMTs.
     */
    RTSpinlockAcquire(pcHv->hSpinlockR0);
    if (pRefTsc->i64TscOffset != i64TscOffset)
    {
        if (pRefTsc->u32TscSequence < UINT32_C(0xfffffffe))
            ASMAtomicIncU32(&pRefTsc->u32TscSequence);
        else
            ASMAtomicWriteU32(&pRefTsc->u32TscSequence, 1);
        ASMAtomicWriteS64(&pRefTsc->i64TscOffset, i64TscOffset);
    }
    RTSpinlockRelease(pcHv->hSpinlockR0);

    Assert(pRefTsc->u32TscSequence != 0);
    Assert(pRefTsc->u32TscSequence != UINT32_C(0xffffffff));
#else
    NOREF(u64Offset);
#endif
    return VINF_SUCCESS;
}


/**
 * Does ring-0 per-VM GIM Hyper-V initialization.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 */
VMMR0_INT_DECL(int) gimR0HvInitVM(PVMCC pVM)
{
    AssertPtr(pVM);
    Assert(GIMIsEnabled(pVM));

    PGIMHV pHv = &pVM->gim.s.u.Hv;
    Assert(pHv->hSpinlockR0 == NIL_RTSPINLOCK);

    int rc = RTSpinlockCreate(&pHv->hSpinlockR0, RTSPINLOCK_FLAGS_INTERRUPT_UNSAFE, "Hyper-V");
    return rc;
}


/**
 * Does ring-0 per-VM GIM Hyper-V termination.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 */
VMMR0_INT_DECL(int) gimR0HvTermVM(PVMCC pVM)
{
    AssertPtr(pVM);
    Assert(GIMIsEnabled(pVM));

    PGIMHV pHv = &pVM->gim.s.u.Hv;
    RTSpinlockDestroy(pHv->hSpinlockR0);
    pHv->hSpinlockR0 = NIL_RTSPINLOCK;

    return VINF_SUCCESS;
}

