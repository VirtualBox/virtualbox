/* $Id: IOMR0IoPort.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IOM - Host Context Ring 0, I/O ports.
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
#define LOG_GROUP LOG_GROUP_IOM_IOPORT
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
 * Initializes the I/O port related members.
 *
 * @param   pGVM    Pointer to the global VM structure.
 */
void iomR0IoPortInitPerVMData(PGVM pGVM)
{
    pGVM->iomr0.s.hIoPortMapObj      = NIL_RTR0MEMOBJ;
    pGVM->iomr0.s.hIoPortMemObj      = NIL_RTR0MEMOBJ;
#ifdef VBOX_WITH_STATISTICS
    pGVM->iomr0.s.hIoPortStatsMapObj = NIL_RTR0MEMOBJ;
    pGVM->iomr0.s.hIoPortStatsMemObj = NIL_RTR0MEMOBJ;
#endif
}


/**
 * Cleans up I/O port related resources.
 */
void iomR0IoPortCleanupVM(PGVM pGVM)
{
    RTR0MemObjFree(pGVM->iomr0.s.hIoPortMapObj, true /*fFreeMappings*/);
    pGVM->iomr0.s.hIoPortMapObj      = NIL_RTR0MEMOBJ;
    RTR0MemObjFree(pGVM->iomr0.s.hIoPortMemObj, true /*fFreeMappings*/);
    pGVM->iomr0.s.hIoPortMemObj      = NIL_RTR0MEMOBJ;
#ifdef VBOX_WITH_STATISTICS
    RTR0MemObjFree(pGVM->iomr0.s.hIoPortStatsMapObj, true /*fFreeMappings*/);
    pGVM->iomr0.s.hIoPortStatsMapObj = NIL_RTR0MEMOBJ;
    RTR0MemObjFree(pGVM->iomr0.s.hIoPortStatsMemObj, true /*fFreeMappings*/);
    pGVM->iomr0.s.hIoPortStatsMemObj = NIL_RTR0MEMOBJ;
#endif
}


/**
 * Implements PDMDEVHLPR0::pfnIoPortSetUpContext.
 *
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   pDevIns         The device instance.
 * @param   hIoPorts        The I/O port handle (already registered in ring-3).
 * @param   pfnOut          The OUT handler callback, optional.
 * @param   pfnIn           The IN handler callback, optional.
 * @param   pfnOutStr       The REP OUTS handler callback, optional.
 * @param   pfnInStr        The REP INS handler callback, optional.
 * @param   pvUser          User argument for the callbacks.
 * @thread  EMT(0)
 * @note    Only callable at VM creation time.
 */
VMMR0_INT_DECL(int)  IOMR0IoPortSetUpContext(PGVM pGVM, PPDMDEVINS pDevIns, IOMIOPORTHANDLE hIoPorts,
                                             PFNIOMIOPORTNEWOUT pfnOut,  PFNIOMIOPORTNEWIN pfnIn,
                                             PFNIOMIOPORTNEWOUTSTRING pfnOutStr, PFNIOMIOPORTNEWINSTRING pfnInStr, void *pvUser)
{
    /*
     * Validate input and state.
     */
    VM_ASSERT_EMT0_RETURN(pGVM, VERR_VM_THREAD_NOT_EMT);
    VM_ASSERT_STATE_RETURN(pGVM, VMSTATE_CREATING, VERR_VM_INVALID_VM_STATE);
    AssertReturn(hIoPorts < pGVM->iomr0.s.cIoPortAlloc, VERR_IOM_INVALID_IOPORT_HANDLE);
    AssertReturn(hIoPorts < pGVM->iom.s.cIoPortRegs, VERR_IOM_INVALID_IOPORT_HANDLE);
    AssertPtrReturn(pDevIns, VERR_INVALID_HANDLE);
    AssertReturn(pDevIns->pDevInsForR3 != NIL_RTR3PTR && !(pDevIns->pDevInsForR3 & HOST_PAGE_OFFSET_MASK), VERR_INVALID_PARAMETER);
    AssertReturn(pGVM->iomr0.s.paIoPortRing3Regs[hIoPorts].pDevIns == pDevIns->pDevInsForR3, VERR_IOM_INVALID_IOPORT_HANDLE);
    AssertReturn(pGVM->iomr0.s.paIoPortRegs[hIoPorts].pDevIns == NULL, VERR_WRONG_ORDER);
    Assert(pGVM->iomr0.s.paIoPortRegs[hIoPorts].idxSelf == hIoPorts);

    AssertReturn(pfnOut || pfnIn || pfnOutStr || pfnInStr, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pfnOut, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pfnIn, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pfnOutStr, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pfnInStr, VERR_INVALID_POINTER);

    uint16_t const fFlags = pGVM->iomr0.s.paIoPortRing3Regs[hIoPorts].fFlags;
    RTIOPORT const cPorts = pGVM->iomr0.s.paIoPortRing3Regs[hIoPorts].cPorts;
    AssertMsgReturn(cPorts > 0 && cPorts <= _8K, ("cPorts=%s\n", cPorts), VERR_IOM_INVALID_IOPORT_HANDLE);

    /*
     * Do the job.
     */
    pGVM->iomr0.s.paIoPortRegs[hIoPorts].pvUser             = pvUser;
    pGVM->iomr0.s.paIoPortRegs[hIoPorts].pDevIns            = pDevIns;
    pGVM->iomr0.s.paIoPortRegs[hIoPorts].pfnOutCallback     = pfnOut;
    pGVM->iomr0.s.paIoPortRegs[hIoPorts].pfnInCallback      = pfnIn;
    pGVM->iomr0.s.paIoPortRegs[hIoPorts].pfnOutStrCallback  = pfnOutStr;
    pGVM->iomr0.s.paIoPortRegs[hIoPorts].pfnInStrCallback   = pfnInStr;
    pGVM->iomr0.s.paIoPortRegs[hIoPorts].cPorts             = cPorts;
    pGVM->iomr0.s.paIoPortRegs[hIoPorts].fFlags             = fFlags;
#ifdef VBOX_WITH_STATISTICS
    uint16_t const idxStats = pGVM->iomr0.s.paIoPortRing3Regs[hIoPorts].idxStats;
    pGVM->iomr0.s.paIoPortRegs[hIoPorts].idxStats           = (uint32_t)idxStats + cPorts <= pGVM->iomr0.s.cIoPortStatsAllocation
                                                            ? idxStats : UINT16_MAX;
#else
    pGVM->iomr0.s.paIoPortRegs[hIoPorts].idxStats           = UINT16_MAX;
#endif

    pGVM->iomr0.s.paIoPortRing3Regs[hIoPorts].fRing0 = true;

    return VINF_SUCCESS;
}


/**
 * Grows the I/O port registration (all contexts) and lookup tables.
 *
 * @returns VBox status code.
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   cReqMinEntries  The minimum growth (absolute).
 * @thread  EMT(0)
 * @note    Only callable at VM creation time.
 */
VMMR0_INT_DECL(int) IOMR0IoPortGrowRegistrationTables(PGVM pGVM, uint64_t cReqMinEntries)
{
    /*
     * Validate input and state.
     */
    VM_ASSERT_EMT0_RETURN(pGVM, VERR_VM_THREAD_NOT_EMT);
    VM_ASSERT_STATE_RETURN(pGVM, VMSTATE_CREATING, VERR_VM_INVALID_VM_STATE);
    AssertReturn(cReqMinEntries <= _4K, VERR_IOM_TOO_MANY_IOPORT_REGISTRATIONS);
    uint32_t cNewEntries = (uint32_t)cReqMinEntries;
    AssertReturn(cNewEntries >= pGVM->iom.s.cIoPortAlloc, VERR_IOM_IOPORT_IPE_1);
    uint32_t const cOldEntries = pGVM->iomr0.s.cIoPortAlloc;
    ASMCompilerBarrier();
    AssertReturn(cNewEntries >= cOldEntries, VERR_IOM_IOPORT_IPE_2);
    AssertReturn(pGVM->iom.s.cIoPortRegs >= pGVM->iomr0.s.cIoPortMax, VERR_IOM_IOPORT_IPE_3);

    /*
     * Allocate the new tables.  We use a single allocation for the three tables (ring-0,
     * ring-3, lookup) and does a partial mapping of the result to ring-3.
     */
    uint32_t const cbRing0  = RT_ALIGN_32(cNewEntries * sizeof(IOMIOPORTENTRYR0),     HOST_PAGE_SIZE);
    uint32_t const cbRing3  = RT_ALIGN_32(cNewEntries * sizeof(IOMIOPORTENTRYR3),     HOST_PAGE_SIZE);
    uint32_t const cbShared = RT_ALIGN_32(cNewEntries * sizeof(IOMIOPORTLOOKUPENTRY), HOST_PAGE_SIZE);
    uint32_t const cbNew    = cbRing0 + cbRing3 + cbShared;

    /* Use the rounded up space as best we can. */
    cNewEntries = RT_MIN(RT_MIN(cbRing0 / sizeof(IOMIOPORTENTRYR0), cbRing3 / sizeof(IOMIOPORTENTRYR3)),
                         cbShared / sizeof(IOMIOPORTLOOKUPENTRY));

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
            PIOMIOPORTENTRYR0     const paRing0    = (PIOMIOPORTENTRYR0)RTR0MemObjAddress(hMemObj);
            PIOMIOPORTENTRYR3     const paRing3    = (PIOMIOPORTENTRYR3)((uintptr_t)paRing0 + cbRing0);
            PIOMIOPORTLOOKUPENTRY const paLookup   = (PIOMIOPORTLOOKUPENTRY)((uintptr_t)paRing3 + cbRing3);
            RTR3UINTPTR           const uAddrRing3 = RTR0MemObjAddressR3(hMapObj);

            /*
             * Copy over the old info and initialize the idxSelf and idxStats members.
             */
            if (pGVM->iomr0.s.paIoPortRegs != NULL)
            {
                memcpy(paRing0,  pGVM->iomr0.s.paIoPortRegs,      sizeof(paRing0[0])  * cOldEntries);
                memcpy(paRing3,  pGVM->iomr0.s.paIoPortRing3Regs, sizeof(paRing3[0])  * cOldEntries);
                memcpy(paLookup, pGVM->iomr0.s.paIoPortLookup,    sizeof(paLookup[0]) * cOldEntries);
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
            RTR0MEMOBJ hTmp = pGVM->iomr0.s.hIoPortMapObj;
            pGVM->iomr0.s.hIoPortMapObj = hMapObj;
            hMapObj = hTmp;

            hTmp = pGVM->iomr0.s.hIoPortMemObj;
            pGVM->iomr0.s.hIoPortMemObj = hMemObj;
            hMemObj = hTmp;

            /*
             * Update the variables.
             */
            pGVM->iomr0.s.paIoPortRegs      = paRing0;
            pGVM->iomr0.s.paIoPortRing3Regs = paRing3;
            pGVM->iomr0.s.paIoPortLookup    = paLookup;
            pGVM->iom.s.paIoPortRegs        = uAddrRing3;
            pGVM->iom.s.paIoPortLookup      = uAddrRing3 + cbRing3;
            pGVM->iom.s.cIoPortAlloc        = cNewEntries;
            pGVM->iomr0.s.cIoPortAlloc      = cNewEntries;

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
 * Grows the I/O port statistics table.
 *
 * @returns VBox status code.
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   cReqMinEntries  The minimum growth (absolute).
 * @thread  EMT(0)
 * @note    Only callable at VM creation time.
 */
VMMR0_INT_DECL(int) IOMR0IoPortGrowStatisticsTable(PGVM pGVM, uint64_t cReqMinEntries)
{
    /*
     * Validate input and state.
     */
    VM_ASSERT_EMT0_RETURN(pGVM, VERR_VM_THREAD_NOT_EMT);
    VM_ASSERT_STATE_RETURN(pGVM, VMSTATE_CREATING, VERR_VM_INVALID_VM_STATE);
    AssertReturn(cReqMinEntries <= _64K, VERR_IOM_TOO_MANY_IOPORT_REGISTRATIONS);
    uint32_t cNewEntries = (uint32_t)cReqMinEntries;
#ifdef VBOX_WITH_STATISTICS
    uint32_t const cOldEntries = pGVM->iomr0.s.cIoPortStatsAllocation;
    ASMCompilerBarrier();
#else
    uint32_t const cOldEntries = 0;
#endif
    AssertReturn(cNewEntries > cOldEntries, VERR_IOM_IOPORT_IPE_1);
    AssertReturn(pGVM->iom.s.cIoPortStatsAllocation == cOldEntries, VERR_IOM_IOPORT_IPE_1);
    AssertReturn(pGVM->iom.s.cIoPortStats <= cOldEntries, VERR_IOM_IOPORT_IPE_2);
#ifdef VBOX_WITH_STATISTICS
    AssertReturn(!pGVM->iomr0.s.fIoPortStatsFrozen, VERR_WRONG_ORDER);
#endif

    /*
     * Allocate a new table, zero it and map it.
     */
#ifndef VBOX_WITH_STATISTICS
    AssertFailedReturn(VERR_NOT_SUPPORTED);
#else
    uint32_t const cbNew = RT_ALIGN_32(cNewEntries * sizeof(IOMIOPORTSTATSENTRY), HOST_PAGE_SIZE);
    cNewEntries = cbNew / sizeof(IOMIOPORTSTATSENTRY);

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
            PIOMIOPORTSTATSENTRY pIoPortStats = (PIOMIOPORTSTATSENTRY)RTR0MemObjAddress(hMemObj);

            /*
             * Anything to copy over and free up?
             */
            if (pGVM->iomr0.s.paIoPortStats)
                memcpy(pIoPortStats, pGVM->iomr0.s.paIoPortStats, cOldEntries * sizeof(IOMIOPORTSTATSENTRY));

            /*
             * Switch the memory handles.
             */
            RTR0MEMOBJ hTmp = pGVM->iomr0.s.hIoPortStatsMapObj;
            pGVM->iomr0.s.hIoPortStatsMapObj = hMapObj;
            hMapObj = hTmp;

            hTmp = pGVM->iomr0.s.hIoPortStatsMemObj;
            pGVM->iomr0.s.hIoPortStatsMemObj = hMemObj;
            hMemObj = hTmp;

            /*
             * Update the variables.
             */
            pGVM->iomr0.s.paIoPortStats          = pIoPortStats;
            pGVM->iom.s.paIoPortStats            = RTR0MemObjAddressR3(pGVM->iomr0.s.hIoPortStatsMapObj);
            pGVM->iom.s.cIoPortStatsAllocation   = cNewEntries;
            pGVM->iomr0.s.cIoPortStatsAllocation = cNewEntries;

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
 * indices to the ring-0 I/O port registration table.
 *
 * This simplifies keeping statistics for I/O port ranges that are ring-3 only.
 *
 * After this call, IOMR0IoPortGrowStatisticsTable() will stop working.
 *
 * @returns VBox status code.
 * @param   pGVM            The global (ring-0) VM structure.
 * @thread  EMT(0)
 * @note    Only callable at VM creation time.
 */
VMMR0_INT_DECL(int) IOMR0IoPortSyncStatisticsIndices(PGVM pGVM)
{
    VM_ASSERT_EMT0_RETURN(pGVM, VERR_VM_THREAD_NOT_EMT);
    VM_ASSERT_STATE_RETURN(pGVM, VMSTATE_CREATING, VERR_VM_INVALID_VM_STATE);

#ifdef VBOX_WITH_STATISTICS
    /*
     * First, freeze the statistics array:
     */
    pGVM->iomr0.s.fIoPortStatsFrozen = true;

    /*
     * Second, synchronize the indices:
     */
    uint32_t const          cRegs        = RT_MIN(pGVM->iom.s.cIoPortRegs, pGVM->iomr0.s.cIoPortAlloc);
    uint32_t const          cStatsAlloc  = pGVM->iomr0.s.cIoPortStatsAllocation;
    PIOMIOPORTENTRYR0       paIoPortRegs   = pGVM->iomr0.s.paIoPortRegs;
    IOMIOPORTENTRYR3 const *paIoPortRegsR3 = pGVM->iomr0.s.paIoPortRing3Regs;
    AssertReturn((paIoPortRegs && paIoPortRegsR3) || cRegs == 0, VERR_IOM_IOPORT_IPE_3);

    for (uint32_t i = 0 ; i < cRegs; i++)
    {
        uint16_t idxStats = paIoPortRegsR3[i].idxStats;
        paIoPortRegs[i].idxStats = idxStats < cStatsAlloc ? idxStats : UINT16_MAX;
    }

#else
    RT_NOREF(pGVM);
#endif
    return VINF_SUCCESS;
}

