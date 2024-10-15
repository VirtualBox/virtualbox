/* $Id: IOMR0Mmio.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IOM - Host Context Ring 0, MMIO.
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
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_IOM_MMIO
#include <VBox/vmm/iom.h>
#include "IOMInternal.h"
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/vmcc.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/memobj.h>
#include <iprt/process.h>
#include <iprt/string.h>



/**
 * Initializes the MMIO related members.
 *
 * @param   pGVM    Pointer to the global VM structure.
 */
void iomR0MmioInitPerVMData(PGVM pGVM)
{
    pGVM->iomr0.s.hMmioMapObj      = NIL_RTR0MEMOBJ;
    pGVM->iomr0.s.hMmioMemObj      = NIL_RTR0MEMOBJ;
#ifdef VBOX_WITH_STATISTICS
    pGVM->iomr0.s.hMmioStatsMapObj = NIL_RTR0MEMOBJ;
    pGVM->iomr0.s.hMmioStatsMemObj = NIL_RTR0MEMOBJ;
#endif
}


/**
 * Cleans up MMIO related resources.
 */
void iomR0MmioCleanupVM(PGVM pGVM)
{
    RTR0MemObjFree(pGVM->iomr0.s.hMmioMapObj, true /*fFreeMappings*/);
    pGVM->iomr0.s.hMmioMapObj      = NIL_RTR0MEMOBJ;
    RTR0MemObjFree(pGVM->iomr0.s.hMmioMemObj, true /*fFreeMappings*/);
    pGVM->iomr0.s.hMmioMemObj      = NIL_RTR0MEMOBJ;
#ifdef VBOX_WITH_STATISTICS
    RTR0MemObjFree(pGVM->iomr0.s.hMmioStatsMapObj, true /*fFreeMappings*/);
    pGVM->iomr0.s.hMmioStatsMapObj = NIL_RTR0MEMOBJ;
    RTR0MemObjFree(pGVM->iomr0.s.hMmioStatsMemObj, true /*fFreeMappings*/);
    pGVM->iomr0.s.hMmioStatsMemObj = NIL_RTR0MEMOBJ;
#endif
}


/**
 * Implements PDMDEVHLPR0::pfnMmioSetUpContext.
 *
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   pDevIns         The device instance.
 * @param   hRegion         The MMIO region handle (already registered in
 *                          ring-3).
 * @param   pfnWrite        The write handler callback, optional.
 * @param   pfnRead         The read handler callback, optional.
 * @param   pfnFill         The fill handler callback, optional.
 * @param   pvUser          User argument for the callbacks.
 * @thread  EMT(0)
 * @note    Only callable at VM creation time.
 */
VMMR0_INT_DECL(int)  IOMR0MmioSetUpContext(PGVM pGVM, PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion, PFNIOMMMIONEWWRITE pfnWrite,
                                           PFNIOMMMIONEWREAD pfnRead, PFNIOMMMIONEWFILL pfnFill, void *pvUser)
{
    /*
     * Validate input and state.
     */
    VM_ASSERT_EMT0_RETURN(pGVM, VERR_VM_THREAD_NOT_EMT);
    VM_ASSERT_STATE_RETURN(pGVM, VMSTATE_CREATING, VERR_VM_INVALID_VM_STATE);
    AssertReturn(hRegion < pGVM->iomr0.s.cMmioAlloc, VERR_IOM_INVALID_MMIO_HANDLE);
    AssertReturn(hRegion < pGVM->iom.s.cMmioRegs, VERR_IOM_INVALID_MMIO_HANDLE);
    AssertPtrReturn(pDevIns, VERR_INVALID_HANDLE);
    AssertReturn(pDevIns->pDevInsForR3 != NIL_RTR3PTR && !(pDevIns->pDevInsForR3 & HOST_PAGE_OFFSET_MASK), VERR_INVALID_PARAMETER);
    AssertReturn(pGVM->iomr0.s.paMmioRing3Regs[hRegion].pDevIns == pDevIns->pDevInsForR3, VERR_IOM_INVALID_MMIO_HANDLE);
    AssertReturn(pGVM->iomr0.s.paMmioRegs[hRegion].pDevIns == NULL, VERR_WRONG_ORDER);
    Assert(pGVM->iomr0.s.paMmioRegs[hRegion].idxSelf == hRegion);

    AssertReturn(pfnWrite || pfnRead || pfnFill, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pfnWrite, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pfnRead, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pfnFill, VERR_INVALID_POINTER);

    uint32_t const fFlags   = pGVM->iomr0.s.paMmioRing3Regs[hRegion].fFlags;
    RTGCPHYS const cbRegion = pGVM->iomr0.s.paMmioRing3Regs[hRegion].cbRegion;
    AssertMsgReturn(cbRegion > 0 && cbRegion <= _1T, ("cbRegion=%#RGp\n", cbRegion), VERR_IOM_INVALID_MMIO_HANDLE);

    /*
     * Do the job.
     */
    pGVM->iomr0.s.paMmioRegs[hRegion].cbRegion          = cbRegion;
    pGVM->iomr0.s.paMmioRegs[hRegion].pvUser            = pvUser;
    pGVM->iomr0.s.paMmioRegs[hRegion].pDevIns           = pDevIns;
    pGVM->iomr0.s.paMmioRegs[hRegion].pfnWriteCallback  = pfnWrite;
    pGVM->iomr0.s.paMmioRegs[hRegion].pfnReadCallback   = pfnRead;
    pGVM->iomr0.s.paMmioRegs[hRegion].pfnFillCallback   = pfnFill;
    pGVM->iomr0.s.paMmioRegs[hRegion].fFlags            = fFlags;
#ifdef VBOX_WITH_STATISTICS
    uint16_t const idxStats = pGVM->iomr0.s.paMmioRing3Regs[hRegion].idxStats;
    pGVM->iomr0.s.paMmioRegs[hRegion].idxStats          = (uint32_t)idxStats < pGVM->iomr0.s.cMmioStatsAllocation
                                                        ? idxStats : UINT16_MAX;
#else
    pGVM->iomr0.s.paMmioRegs[hRegion].idxStats          = UINT16_MAX;
#endif

    pGVM->iomr0.s.paMmioRing3Regs[hRegion].fRing0 = true;

    return VINF_SUCCESS;
}


/**
 * Grows the MMIO registration (all contexts) and lookup tables.
 *
 * @returns VBox status code.
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   cReqMinEntries  The minimum growth (absolute).
 * @thread  EMT(0)
 * @note    Only callable at VM creation time.
 */
VMMR0_INT_DECL(int) IOMR0MmioGrowRegistrationTables(PGVM pGVM, uint64_t cReqMinEntries)
{
    /*
     * Validate input and state.
     */
    VM_ASSERT_EMT0_RETURN(pGVM, VERR_VM_THREAD_NOT_EMT);
    VM_ASSERT_STATE_RETURN(pGVM, VMSTATE_CREATING, VERR_VM_INVALID_VM_STATE);
    AssertReturn(cReqMinEntries <= _4K, VERR_IOM_TOO_MANY_MMIO_REGISTRATIONS);
    uint32_t cNewEntries = (uint32_t)cReqMinEntries;
    AssertReturn(cNewEntries >= pGVM->iom.s.cMmioAlloc, VERR_IOM_MMIO_IPE_1);
    uint32_t const cOldEntries = pGVM->iomr0.s.cMmioAlloc;
    ASMCompilerBarrier();
    AssertReturn(cNewEntries >= cOldEntries, VERR_IOM_MMIO_IPE_2);
    AssertReturn(pGVM->iom.s.cMmioRegs >= pGVM->iomr0.s.cMmioMax, VERR_IOM_MMIO_IPE_3);

    /*
     * Allocate the new tables.  We use a single allocation for the three tables (ring-0,
     * ring-3, lookup) and does a partial mapping of the result to ring-3.
     */
    uint32_t const cbRing0  = RT_ALIGN_32(cNewEntries * sizeof(IOMMMIOENTRYR0),     HOST_PAGE_SIZE);
    uint32_t const cbRing3  = RT_ALIGN_32(cNewEntries * sizeof(IOMMMIOENTRYR3),     HOST_PAGE_SIZE);
    uint32_t const cbShared = RT_ALIGN_32(cNewEntries * sizeof(IOMMMIOLOOKUPENTRY), HOST_PAGE_SIZE);
    uint32_t const cbNew    = cbRing0 + cbRing3 + cbShared;

    /* Use the rounded up space as best we can. */
    cNewEntries = RT_MIN(RT_MIN(cbRing0 / sizeof(IOMMMIOENTRYR0), cbRing3 / sizeof(IOMMMIOENTRYR3)),
                         cbShared / sizeof(IOMMMIOLOOKUPENTRY));

    RTR0MEMOBJ hMemObj;
    int rc = RTR0MemObjAllocPage(&hMemObj, cbNew, false /*fExecutable*/);
    if (RT_SUCCESS(rc))
    {
        /*
         * Zero and map it.
         */
        RT_BZERO(RTR0MemObjAddress(hMemObj), cbNew);

        RTR0MEMOBJ hMapObj;
        rc = RTR0MemObjMapUserEx(&hMapObj, hMemObj, (RTR3PTR)-1, HOST_PAGE_SIZE, RTMEM_PROT_READ | RTMEM_PROT_WRITE,
                                 RTR0ProcHandleSelf(), cbRing0, cbNew - cbRing0);
        if (RT_SUCCESS(rc))
        {
            PIOMMMIOENTRYR0       const paRing0    = (PIOMMMIOENTRYR0)RTR0MemObjAddress(hMemObj);
            PIOMMMIOENTRYR3       const paRing3    = (PIOMMMIOENTRYR3)((uintptr_t)paRing0 + cbRing0);
            PIOMMMIOLOOKUPENTRY   const paLookup   = (PIOMMMIOLOOKUPENTRY)((uintptr_t)paRing3 + cbRing3);
            RTR3UINTPTR           const uAddrRing3 = RTR0MemObjAddressR3(hMapObj);

            /*
             * Copy over the old info and initialize the idxSelf and idxStats members.
             */
            if (pGVM->iomr0.s.paMmioRegs != NULL)
            {
                memcpy(paRing0,  pGVM->iomr0.s.paMmioRegs,      sizeof(paRing0[0])  * cOldEntries);
                memcpy(paRing3,  pGVM->iomr0.s.paMmioRing3Regs, sizeof(paRing3[0])  * cOldEntries);
                memcpy(paLookup, pGVM->iomr0.s.paMmioLookup,    sizeof(paLookup[0]) * cOldEntries);
            }

            size_t i = cbRing0 / sizeof(*paRing0);
            while (i-- > cOldEntries)
            {
                paRing0[i].idxSelf  = (uint16_t)i;
                paRing0[i].idxStats = UINT16_MAX;
            }
            i = cbRing3 / sizeof(*paRing3);
            while (i-- > cOldEntries)
            {
                paRing3[i].idxSelf  = (uint16_t)i;
                paRing3[i].idxStats = UINT16_MAX;
            }

            /*
             * Switch the memory handles.
             */
            RTR0MEMOBJ hTmp = pGVM->iomr0.s.hMmioMapObj;
            pGVM->iomr0.s.hMmioMapObj = hMapObj;
            hMapObj = hTmp;

            hTmp = pGVM->iomr0.s.hMmioMemObj;
            pGVM->iomr0.s.hMmioMemObj = hMemObj;
            hMemObj = hTmp;

            /*
             * Update the variables.
             */
            pGVM->iomr0.s.paMmioRegs      = paRing0;
            pGVM->iomr0.s.paMmioRing3Regs = paRing3;
            pGVM->iomr0.s.paMmioLookup    = paLookup;
            pGVM->iom.s.paMmioRegs        = uAddrRing3;
            pGVM->iom.s.paMmioLookup      = uAddrRing3 + cbRing3;
            pGVM->iom.s.cMmioAlloc        = cNewEntries;
            pGVM->iomr0.s.cMmioAlloc      = cNewEntries;

            /*
             * Free the old allocation.
             */
            RTR0MemObjFree(hMapObj, true /*fFreeMappings*/);
        }
        RTR0MemObjFree(hMemObj, true /*fFreeMappings*/);
    }

    return rc;
}


/**
 * Grows the MMIO statistics table.
 *
 * @returns VBox status code.
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   cReqMinEntries  The minimum growth (absolute).
 * @thread  EMT(0)
 * @note    Only callable at VM creation time.
 */
VMMR0_INT_DECL(int) IOMR0MmioGrowStatisticsTable(PGVM pGVM, uint64_t cReqMinEntries)
{
    /*
     * Validate input and state.
     */
    VM_ASSERT_EMT0_RETURN(pGVM, VERR_VM_THREAD_NOT_EMT);
    VM_ASSERT_STATE_RETURN(pGVM, VMSTATE_CREATING, VERR_VM_INVALID_VM_STATE);
    AssertReturn(cReqMinEntries <= _64K, VERR_IOM_TOO_MANY_MMIO_REGISTRATIONS);
    uint32_t cNewEntries = (uint32_t)cReqMinEntries;
#ifdef VBOX_WITH_STATISTICS
    uint32_t const cOldEntries = pGVM->iomr0.s.cMmioStatsAllocation;
    ASMCompilerBarrier();
#else
    uint32_t const cOldEntries = 0;
#endif
    AssertReturn(cNewEntries > cOldEntries, VERR_IOM_MMIO_IPE_1);
    AssertReturn(pGVM->iom.s.cMmioStatsAllocation == cOldEntries, VERR_IOM_MMIO_IPE_1);
    AssertReturn(pGVM->iom.s.cMmioStats <= cOldEntries, VERR_IOM_MMIO_IPE_2);
#ifdef VBOX_WITH_STATISTICS
    AssertReturn(!pGVM->iomr0.s.fMmioStatsFrozen, VERR_WRONG_ORDER);
#endif

    /*
     * Allocate a new table, zero it and map it.
     */
#ifndef VBOX_WITH_STATISTICS
    AssertFailedReturn(VERR_NOT_SUPPORTED);
#else
    uint32_t const cbNew = RT_ALIGN_32(cNewEntries * sizeof(IOMMMIOSTATSENTRY), HOST_PAGE_SIZE);
    cNewEntries = cbNew / sizeof(IOMMMIOSTATSENTRY);

    RTR0MEMOBJ hMemObj;
    int rc = RTR0MemObjAllocPage(&hMemObj, cbNew, false /*fExecutable*/);
    if (RT_SUCCESS(rc))
    {
        RT_BZERO(RTR0MemObjAddress(hMemObj), cbNew);

        RTR0MEMOBJ hMapObj;
        rc = RTR0MemObjMapUser(&hMapObj, hMemObj, (RTR3PTR)-1, HOST_PAGE_SIZE,
                               RTMEM_PROT_READ | RTMEM_PROT_WRITE, RTR0ProcHandleSelf());
        if (RT_SUCCESS(rc))
        {
            PIOMMMIOSTATSENTRY pMmioStats = (PIOMMMIOSTATSENTRY)RTR0MemObjAddress(hMemObj);

            /*
             * Anything to copy over and free up?
             */
            if (pGVM->iomr0.s.paMmioStats)
                memcpy(pMmioStats, pGVM->iomr0.s.paMmioStats, cOldEntries * sizeof(IOMMMIOSTATSENTRY));

            /*
             * Switch the memory handles.
             */
            RTR0MEMOBJ hTmp = pGVM->iomr0.s.hMmioStatsMapObj;
            pGVM->iomr0.s.hMmioStatsMapObj = hMapObj;
            hMapObj = hTmp;

            hTmp = pGVM->iomr0.s.hMmioStatsMemObj;
            pGVM->iomr0.s.hMmioStatsMemObj = hMemObj;
            hMemObj = hTmp;

            /*
             * Update the variables.
             */
            pGVM->iomr0.s.paMmioStats          = pMmioStats;
            pGVM->iom.s.paMmioStats            = RTR0MemObjAddressR3(pGVM->iomr0.s.hMmioStatsMapObj);
            pGVM->iom.s.cMmioStatsAllocation   = cNewEntries;
            pGVM->iomr0.s.cMmioStatsAllocation = cNewEntries;

            /*
             * Free the old allocation.
             */
            RTR0MemObjFree(hMapObj, true /*fFreeMappings*/);
        }
        RTR0MemObjFree(hMemObj, true /*fFreeMappings*/);
    }
    return rc;
#endif /* VBOX_WITH_STATISTICS */
}


/**
 * Called after all devices has been instantiated to copy over the statistics
 * indices to the ring-0 MMIO registration table.
 *
 * This simplifies keeping statistics for MMIO ranges that are ring-3 only.
 *
 * @returns VBox status code.
 * @param   pGVM            The global (ring-0) VM structure.
 * @thread  EMT(0)
 * @note    Only callable at VM creation time.
 */
VMMR0_INT_DECL(int) IOMR0MmioSyncStatisticsIndices(PGVM pGVM)
{
    VM_ASSERT_EMT0_RETURN(pGVM, VERR_VM_THREAD_NOT_EMT);
    VM_ASSERT_STATE_RETURN(pGVM, VMSTATE_CREATING, VERR_VM_INVALID_VM_STATE);

#ifdef VBOX_WITH_STATISTICS
    /*
     * First, freeze the statistics array:
     */
    pGVM->iomr0.s.fMmioStatsFrozen = true;

    /*
     * Second, synchronize the indices:
     */
    uint32_t const          cRegs        = RT_MIN(pGVM->iom.s.cMmioRegs, pGVM->iomr0.s.cMmioAlloc);
    uint32_t const          cStatsAlloc  = pGVM->iomr0.s.cMmioStatsAllocation;
    PIOMMMIOENTRYR0         paMmioRegs   = pGVM->iomr0.s.paMmioRegs;
    IOMMMIOENTRYR3 const   *paMmioRegsR3 = pGVM->iomr0.s.paMmioRing3Regs;
    AssertReturn((paMmioRegs && paMmioRegsR3) || cRegs == 0, VERR_IOM_MMIO_IPE_3);

    for (uint32_t i = 0 ; i < cRegs; i++)
    {
        uint16_t idxStats = paMmioRegsR3[i].idxStats;
        paMmioRegs[i].idxStats = idxStats < cStatsAlloc ? idxStats : UINT16_MAX;
    }

#else
    RT_NOREF(pGVM);
#endif
    return VINF_SUCCESS;
}

