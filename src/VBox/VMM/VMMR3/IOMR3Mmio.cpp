/* $Id: IOMR3Mmio.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IOM - Input / Output Monitor, MMIO related APIs.
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
#include <VBox/sup.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/pdmdev.h>
#include "IOMInternal.h"
#include <VBox/vmm/vm.h>

#include <VBox/param.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <VBox/log.h>
#include <VBox/err.h>

#include "IOMInline.h"


#ifdef VBOX_WITH_STATISTICS

/**
 * Register statistics for a MMIO entry.
 */
void iomR3MmioRegStats(PVM pVM, PIOMMMIOENTRYR3 pRegEntry)
{
    bool const           fDoRZ  = pRegEntry->fRing0 || pRegEntry->fRawMode;
    PIOMMMIOSTATSENTRY   pStats = &pVM->iom.s.paMmioStats[pRegEntry->idxStats];

    /* Format the prefix: */
    char                 szName[80];
    size_t               cchPrefix = RTStrPrintf(szName, sizeof(szName), "/IOM/MmioRegions/%RGp-%RGp",
                                                 pRegEntry->GCPhysMapping, pRegEntry->GCPhysMapping + pRegEntry->cbRegion - 1);

    /* Mangle the description if this isn't the first device instance: */
    const char          *pszDesc     = pRegEntry->pszDesc;
    char                *pszFreeDesc = NULL;
    if (pRegEntry->pDevIns && pRegEntry->pDevIns->iInstance > 0 && pszDesc)
        pszDesc = pszFreeDesc = RTStrAPrintf2("%u / %s", pRegEntry->pDevIns->iInstance, pszDesc);

    /* Register statistics: */
    int rc = STAMR3Register(pVM, &pRegEntry->idxSelf, STAMTYPE_U16, STAMVISIBILITY_ALWAYS, szName, STAMUNIT_NONE, pszDesc); AssertRC(rc);
    RTStrFree(pszFreeDesc);

# define SET_NM_SUFFIX(a_sz) memcpy(&szName[cchPrefix], a_sz, sizeof(a_sz))
    SET_NM_SUFFIX("/Read-Complicated");
    rc = STAMR3Register(pVM, &pStats->ComplicatedReads, STAMTYPE_COUNTER, STAMVISIBILITY_USED, szName, STAMUNIT_OCCURENCES, NULL); AssertRC(rc);
    SET_NM_SUFFIX("/Read-FFor00");
    rc = STAMR3Register(pVM, &pStats->FFor00Reads, STAMTYPE_COUNTER, STAMVISIBILITY_USED, szName, STAMUNIT_OCCURENCES,     NULL); AssertRC(rc);
    SET_NM_SUFFIX("/Read-R3");
    rc = STAMR3Register(pVM, &pStats->ProfReadR3,  STAMTYPE_PROFILE, STAMVISIBILITY_USED, szName, STAMUNIT_TICKS_PER_CALL, NULL); AssertRC(rc);
    if (fDoRZ)
    {
        SET_NM_SUFFIX("/Read-RZ");
        rc = STAMR3Register(pVM, &pStats->ProfReadRZ,  STAMTYPE_PROFILE, STAMVISIBILITY_USED, szName, STAMUNIT_TICKS_PER_CALL, NULL); AssertRC(rc);
        SET_NM_SUFFIX("/Read-RZtoR3");
        rc = STAMR3Register(pVM, &pStats->ReadRZToR3,  STAMTYPE_COUNTER, STAMVISIBILITY_USED, szName, STAMUNIT_OCCURENCES,     NULL); AssertRC(rc);
    }
    SET_NM_SUFFIX("/Read-Total");
    rc = STAMR3Register(pVM, &pStats->Reads,       STAMTYPE_COUNTER, STAMVISIBILITY_USED, szName, STAMUNIT_OCCURENCES,     NULL); AssertRC(rc);

    SET_NM_SUFFIX("/Write-Complicated");
    rc = STAMR3Register(pVM, &pStats->ComplicatedWrites, STAMTYPE_COUNTER, STAMVISIBILITY_USED, szName, STAMUNIT_OCCURENCES, NULL); AssertRC(rc);
    SET_NM_SUFFIX("/Write-R3");
    rc = STAMR3Register(pVM, &pStats->ProfWriteR3,  STAMTYPE_PROFILE, STAMVISIBILITY_USED, szName, STAMUNIT_TICKS_PER_CALL, NULL); AssertRC(rc);
    if (fDoRZ)
    {
        SET_NM_SUFFIX("/Write-RZ");
        rc = STAMR3Register(pVM, &pStats->ProfWriteRZ, STAMTYPE_PROFILE, STAMVISIBILITY_USED, szName, STAMUNIT_TICKS_PER_CALL, NULL); AssertRC(rc);
        SET_NM_SUFFIX("/Write-RZtoR3");
        rc = STAMR3Register(pVM, &pStats->WriteRZToR3, STAMTYPE_COUNTER, STAMVISIBILITY_USED, szName, STAMUNIT_OCCURENCES,     NULL); AssertRC(rc);
        SET_NM_SUFFIX("/Write-RZtoR3-Commit");
        rc = STAMR3Register(pVM, &pStats->CommitRZToR3, STAMTYPE_COUNTER, STAMVISIBILITY_USED, szName, STAMUNIT_OCCURENCES,    NULL); AssertRC(rc);
    }
    SET_NM_SUFFIX("/Write-Total");
    rc = STAMR3Register(pVM, &pStats->Writes,      STAMTYPE_COUNTER, STAMVISIBILITY_USED, szName, STAMUNIT_OCCURENCES,     NULL); AssertRC(rc);
}


/**
 * Deregister statistics for a MMIO entry.
 */
static void iomR3MmioDeregStats(PVM pVM, PIOMMMIOENTRYR3 pRegEntry, RTGCPHYS GCPhys)
{
    char szPrefix[80];
    RTStrPrintf(szPrefix, sizeof(szPrefix), "/IOM/MmioRegions/%RGp-%RGp", GCPhys, GCPhys + pRegEntry->cbRegion - 1);
    STAMR3DeregisterByPrefix(pVM->pUVM, szPrefix);
}


/**
 * Grows the statistics table.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   cNewEntries     The minimum number of new entrie.
 * @see     IOMR0IoPortGrowStatisticsTable
 */
static int iomR3MmioGrowStatisticsTable(PVM pVM, uint32_t cNewEntries)
{
    AssertReturn(cNewEntries <= _64K, VERR_IOM_TOO_MANY_MMIO_REGISTRATIONS);

    int rc;
    if (!SUPR3IsDriverless())
    {
        rc = VMMR3CallR0Emt(pVM, pVM->apCpusR3[0], VMMR0_DO_IOM_GROW_MMIO_STATS, cNewEntries, NULL);
        AssertLogRelRCReturn(rc, rc);
        AssertReturn(cNewEntries <= pVM->iom.s.cMmioStatsAllocation, VERR_IOM_MMIO_IPE_2);
    }
    else
    {
        /*
         * Validate input and state.
         */
        uint32_t const cOldEntries = pVM->iom.s.cMmioStatsAllocation;
        AssertReturn(cNewEntries > cOldEntries, VERR_IOM_MMIO_IPE_1);
        AssertReturn(pVM->iom.s.cMmioStats <= cOldEntries, VERR_IOM_MMIO_IPE_2);

        /*
         * Calc size and allocate a new table.
         */
        uint32_t const cbNew = RT_ALIGN_32(cNewEntries * sizeof(IOMMMIOSTATSENTRY), HOST_PAGE_SIZE);
        cNewEntries = cbNew / sizeof(IOMMMIOSTATSENTRY);

        PIOMMMIOSTATSENTRY const paMmioStats = (PIOMMMIOSTATSENTRY)RTMemPageAllocZ(cbNew);
        if (paMmioStats)
        {
            /*
             * Anything to copy over, update and free the old one.
             */
            PIOMMMIOSTATSENTRY const pOldMmioStats = pVM->iom.s.paMmioStats;
            if (pOldMmioStats)
                memcpy(paMmioStats, pOldMmioStats, cOldEntries * sizeof(IOMMMIOSTATSENTRY));

            pVM->iom.s.paMmioStats             = paMmioStats;
            pVM->iom.s.cMmioStatsAllocation    = cNewEntries;

            RTMemPageFree(pOldMmioStats, RT_ALIGN_32(cOldEntries * sizeof(IOMMMIOSTATSENTRY), HOST_PAGE_SIZE));

            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_NO_PAGE_MEMORY;
    }

    return rc;
}

#endif /* VBOX_WITH_STATISTICS */

/**
 * Grows the I/O port registration statistics table.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   cNewEntries     The minimum number of new entrie.
 * @see     IOMR0MmioGrowRegistrationTables
 */
static int iomR3MmioGrowTable(PVM pVM, uint32_t cNewEntries)
{
    AssertReturn(cNewEntries <= _4K, VERR_IOM_TOO_MANY_MMIO_REGISTRATIONS);

    int rc;
    if (!SUPR3IsDriverless())
    {
        rc = VMMR3CallR0Emt(pVM, pVM->apCpusR3[0], VMMR0_DO_IOM_GROW_MMIO_REGS, cNewEntries, NULL);
        AssertLogRelRCReturn(rc, rc);
        AssertReturn(cNewEntries <= pVM->iom.s.cMmioAlloc, VERR_IOM_MMIO_IPE_2);
    }
    else
    {
        /*
         * Validate input and state.
         */
        uint32_t const cOldEntries = pVM->iom.s.cMmioAlloc;
        AssertReturn(cNewEntries >= cOldEntries, VERR_IOM_MMIO_IPE_1);

        /*
         * Allocate the new tables.  We use a single allocation for the three tables (ring-0,
         * ring-3, lookup) and does a partial mapping of the result to ring-3.
         */
        uint32_t const cbRing3  = RT_ALIGN_32(cNewEntries * sizeof(IOMMMIOENTRYR3),     HOST_PAGE_SIZE);
        uint32_t const cbShared = RT_ALIGN_32(cNewEntries * sizeof(IOMMMIOLOOKUPENTRY), HOST_PAGE_SIZE);
        uint32_t const cbNew    = cbRing3 + cbShared;

        /* Use the rounded up space as best we can. */
        cNewEntries = RT_MIN(cbRing3 / sizeof(IOMMMIOENTRYR3), cbShared / sizeof(IOMMMIOLOOKUPENTRY));

        PIOMMMIOENTRYR3 const paRing3 = (PIOMMMIOENTRYR3)RTMemPageAllocZ(cbNew);
        if (paRing3)
        {
            PIOMMMIOLOOKUPENTRY const paLookup = (PIOMMMIOLOOKUPENTRY)((uintptr_t)paRing3 + cbRing3);

            /*
             * Copy over the old info and initialize the idxSelf and idxStats members.
             */
            if (pVM->iom.s.paMmioRegs != NULL)
            {
                memcpy(paRing3,  pVM->iom.s.paMmioRegs,    sizeof(paRing3[0])  * cOldEntries);
                memcpy(paLookup, pVM->iom.s.paMmioLookup,  sizeof(paLookup[0]) * cOldEntries);
            }

            size_t i = cbRing3 / sizeof(*paRing3);
            while (i-- > cOldEntries)
            {
                paRing3[i].idxSelf  = (uint16_t)i;
                paRing3[i].idxStats = UINT16_MAX;
            }

            /*
             * Update the variables and free the old memory.
             */
            void * const pvFree = pVM->iom.s.paMmioRegs;

            pVM->iom.s.paMmioRegs     = paRing3;
            pVM->iom.s.paMmioLookup   = paLookup;
            pVM->iom.s.cMmioAlloc     = cNewEntries;

            RTMemPageFree(pvFree,
                            RT_ALIGN_32(cOldEntries * sizeof(IOMMMIOENTRYR3),     HOST_PAGE_SIZE)
                          + RT_ALIGN_32(cOldEntries * sizeof(IOMMMIOLOOKUPENTRY), HOST_PAGE_SIZE));

            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_NO_PAGE_MEMORY;
    }
    return rc;
}


/**
 * Worker for PDMDEVHLPR3::pfnMmioCreateEx.
 */
VMMR3_INT_DECL(int)  IOMR3MmioCreate(PVM pVM, PPDMDEVINS pDevIns, RTGCPHYS cbRegion, uint32_t fFlags, PPDMPCIDEV pPciDev,
                                     uint32_t iPciRegion, PFNIOMMMIONEWWRITE pfnWrite, PFNIOMMMIONEWREAD pfnRead,
                                     PFNIOMMMIONEWFILL pfnFill, void *pvUser, const char *pszDesc, PIOMMMIOHANDLE phRegion)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(phRegion, VERR_INVALID_POINTER);
    *phRegion = UINT32_MAX;
    PVMCPU const pVCpu = VMMGetCpu(pVM);
    AssertReturn(pVCpu && pVCpu->idCpu == 0, VERR_VM_THREAD_NOT_EMT);
    VM_ASSERT_STATE_RETURN(pVM, VMSTATE_CREATING, VERR_VM_INVALID_VM_STATE);
    AssertReturn(!pVM->iom.s.fMmioFrozen, VERR_WRONG_ORDER);

    AssertPtrReturn(pDevIns, VERR_INVALID_POINTER);

    AssertMsgReturn(cbRegion > 0 && cbRegion <= MM_MMIO_64_MAX, ("cbRegion=%#RGp (max %#RGp)\n", cbRegion, MM_MMIO_64_MAX),
                    VERR_OUT_OF_RANGE);
    AssertMsgReturn(!(cbRegion & GUEST_PAGE_OFFSET_MASK), ("cbRegion=%#RGp\n", cbRegion), VERR_UNSUPPORTED_ALIGNMENT);

    AssertMsgReturn(   !(fFlags & ~IOMMMIO_FLAGS_VALID_MASK)
                    && (fFlags & IOMMMIO_FLAGS_READ_MODE)  <= IOMMMIO_FLAGS_READ_DWORD_QWORD
                    && (fFlags & IOMMMIO_FLAGS_WRITE_MODE) <= IOMMMIO_FLAGS_WRITE_ONLY_DWORD_QWORD,
                    ("%#x\n", fFlags),
                    VERR_INVALID_FLAGS);

    AssertReturn(pfnWrite || pfnRead, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pfnWrite, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pfnRead, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pfnFill, VERR_INVALID_POINTER);

    AssertPtrReturn(pszDesc, VERR_INVALID_POINTER);
    AssertReturn(*pszDesc != '\0', VERR_INVALID_POINTER);
    AssertReturn(strlen(pszDesc) < 128, VERR_INVALID_POINTER);

    /*
     * Ensure that we've got table space for it.
     */
#ifndef VBOX_WITH_STATISTICS
    uint16_t const idxStats      = UINT16_MAX;
#else
    uint32_t const idxStats      = pVM->iom.s.cMmioStats;
    uint32_t const cNewMmioStats = idxStats + 1;
    AssertReturn(cNewMmioStats <= _64K, VERR_IOM_TOO_MANY_MMIO_REGISTRATIONS);
    if (cNewMmioStats > pVM->iom.s.cMmioStatsAllocation)
    {
        int rc = iomR3MmioGrowStatisticsTable(pVM, cNewMmioStats);
        AssertRCReturn(rc, rc);
        AssertReturn(idxStats == pVM->iom.s.cMmioStats, VERR_IOM_MMIO_IPE_1);
    }
#endif

    uint32_t idx = pVM->iom.s.cMmioRegs;
    if (idx >= pVM->iom.s.cMmioAlloc)
    {
        int rc = iomR3MmioGrowTable(pVM, pVM->iom.s.cMmioAlloc + 1);
        AssertRCReturn(rc, rc);
        AssertReturn(idx == pVM->iom.s.cMmioRegs, VERR_IOM_MMIO_IPE_1);
    }

    /*
     * Create a matching ad-hoc RAM range for this MMIO region.
     */
    uint16_t idRamRange = 0;
    int rc = PGMR3PhysMmioRegister(pVM, pVCpu, cbRegion, pszDesc, &idRamRange);
    AssertRCReturn(rc,  rc);

    /*
     * Enter it.
     */
    pVM->iom.s.paMmioRegs[idx].cbRegion           = cbRegion;
    pVM->iom.s.paMmioRegs[idx].GCPhysMapping      = NIL_RTGCPHYS;
    pVM->iom.s.paMmioRegs[idx].pvUser             = pvUser;
    pVM->iom.s.paMmioRegs[idx].pDevIns            = pDevIns;
    pVM->iom.s.paMmioRegs[idx].pfnWriteCallback   = pfnWrite;
    pVM->iom.s.paMmioRegs[idx].pfnReadCallback    = pfnRead;
    pVM->iom.s.paMmioRegs[idx].pfnFillCallback    = pfnFill;
    pVM->iom.s.paMmioRegs[idx].pszDesc            = pszDesc;
    pVM->iom.s.paMmioRegs[idx].pPciDev            = pPciDev;
    pVM->iom.s.paMmioRegs[idx].iPciRegion         = iPciRegion;
    pVM->iom.s.paMmioRegs[idx].idxStats           = (uint16_t)idxStats;
    pVM->iom.s.paMmioRegs[idx].fMapped            = false;
    pVM->iom.s.paMmioRegs[idx].fFlags             = fFlags;
    pVM->iom.s.paMmioRegs[idx].idRamRange         = idRamRange;
    pVM->iom.s.paMmioRegs[idx].idxSelf            = idx;

    pVM->iom.s.cMmioRegs = idx + 1;
#ifdef VBOX_WITH_STATISTICS
    pVM->iom.s.cMmioStats = cNewMmioStats;
#endif
    *phRegion = idx;
    return VINF_SUCCESS;
}


/**
 * Worker for PDMDEVHLPR3::pfnMmioMap.
 */
VMMR3_INT_DECL(int)  IOMR3MmioMap(PVM pVM, PVMCPU pVCpu, PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion, RTGCPHYS GCPhys)
{
    /*
     * Validate input and state.
     */
    VMCPU_ASSERT_EMT_RETURN(pVCpu, VERR_VM_THREAD_NOT_EMT);
    AssertPtrReturn(pDevIns, VERR_INVALID_HANDLE);
    AssertReturn(hRegion < pVM->iom.s.cMmioRegs, VERR_IOM_INVALID_MMIO_HANDLE);
    PIOMMMIOENTRYR3 const pRegEntry = &pVM->iom.s.paMmioRegs[hRegion];
    AssertReturn(pRegEntry->pDevIns == pDevIns, VERR_IOM_INVALID_MMIO_HANDLE);

    RTGCPHYS const cbRegion = pRegEntry->cbRegion;
    AssertMsgReturn(cbRegion > 0 && cbRegion <= MM_MMIO_64_MAX, ("cbRegion=%RGp\n", cbRegion), VERR_IOM_MMIO_IPE_1);
    RTGCPHYS const GCPhysLast = GCPhys + cbRegion - 1;

    AssertLogRelMsgReturn(!(GCPhys & GUEST_PAGE_OFFSET_MASK),
                          ("Misaligned! GCPhys=%RGp LB %RGp %s (%s[#%u])\n",
                           GCPhys, cbRegion, pRegEntry->pszDesc, pDevIns->pReg->szName, pDevIns->iInstance),
                          VERR_IOM_INVALID_MMIO_RANGE);
    AssertLogRelMsgReturn(GCPhysLast > GCPhys,
                          ("Wrapped! GCPhys=%RGp LB %RGp %s (%s[#%u])\n",
                           GCPhys, cbRegion, pRegEntry->pszDesc, pDevIns->pReg->szName, pDevIns->iInstance),
                          VERR_IOM_INVALID_MMIO_RANGE);

    /*
     * Do the mapping.
     */
    int rc = VINF_SUCCESS;
    IOM_LOCK_EXCL(pVM);

    if (!pRegEntry->fMapped)
    {
        uint32_t const cEntries = RT_MIN(pVM->iom.s.cMmioLookupEntries, pVM->iom.s.cMmioRegs);
        Assert(pVM->iom.s.cMmioLookupEntries == cEntries);

        PIOMMMIOLOOKUPENTRY paEntries = pVM->iom.s.paMmioLookup;
        PIOMMMIOLOOKUPENTRY pEntry;
        if (cEntries > 0)
        {
            uint32_t iFirst = 0;
            uint32_t iEnd   = cEntries;
            uint32_t i      = cEntries / 2;
            for (;;)
            {
                pEntry = &paEntries[i];
                if (pEntry->GCPhysLast < GCPhys)
                {
                    i += 1;
                    if (i < iEnd)
                        iFirst = i;
                    else
                    {
                        /* Register with PGM before we shuffle the array: */
                        ASMAtomicWriteU64(&pRegEntry->GCPhysMapping, GCPhys);
                        rc = PGMR3PhysMmioMap(pVM, pVCpu, GCPhys, cbRegion, pRegEntry->idRamRange,
                                              pVM->iom.s.hNewMmioHandlerType, hRegion);
                        AssertRCReturnStmt(rc, ASMAtomicWriteU64(&pRegEntry->GCPhysMapping, NIL_RTGCPHYS); IOM_UNLOCK_EXCL(pVM), rc);

                        /* Insert after the entry we just considered: */
                        pEntry += 1;
                        if (i < cEntries)
                            memmove(pEntry + 1, pEntry, sizeof(*pEntry) * (cEntries - i));
                        break;
                    }
                }
                else if (pEntry->GCPhysFirst > GCPhysLast)
                {
                    if (i > iFirst)
                        iEnd = i;
                    else
                    {
                        /* Register with PGM before we shuffle the array: */
                        ASMAtomicWriteU64(&pRegEntry->GCPhysMapping, GCPhys);
                        rc = PGMR3PhysMmioMap(pVM, pVCpu, GCPhys, cbRegion, pRegEntry->idRamRange,
                                              pVM->iom.s.hNewMmioHandlerType, hRegion);
                        AssertRCReturnStmt(rc, ASMAtomicWriteU64(&pRegEntry->GCPhysMapping, NIL_RTGCPHYS); IOM_UNLOCK_EXCL(pVM), rc);

                        /* Insert at the entry we just considered: */
                        if (i < cEntries)
                            memmove(pEntry + 1, pEntry, sizeof(*pEntry) * (cEntries - i));
                        break;
                    }
                }
                else
                {
                    /* Oops! We've got a conflict. */
                    AssertLogRelMsgFailed(("%RGp..%RGp (%s) conflicts with existing mapping %RGp..%RGp (%s)\n",
                                           GCPhys, GCPhysLast, pRegEntry->pszDesc,
                                           pEntry->GCPhysFirst, pEntry->GCPhysLast, pVM->iom.s.paMmioRegs[pEntry->idx].pszDesc));
                    IOM_UNLOCK_EXCL(pVM);
                    return VERR_IOM_MMIO_RANGE_CONFLICT;
                }

                i = iFirst + (iEnd - iFirst) / 2;
            }
        }
        else
        {
            /* First entry in the lookup table: */
            ASMAtomicWriteU64(&pRegEntry->GCPhysMapping, GCPhys);
            rc = PGMR3PhysMmioMap(pVM, pVCpu, GCPhys, cbRegion, pRegEntry->idRamRange,
                                  pVM->iom.s.hNewMmioHandlerType, hRegion);
            AssertRCReturnStmt(rc, ASMAtomicWriteU64(&pRegEntry->GCPhysMapping, NIL_RTGCPHYS); IOM_UNLOCK_EXCL(pVM), rc);

            pEntry = paEntries;
        }

        /*
         * Fill in the entry and bump the table size.
         */
        pRegEntry->fMapped  = true;
        pEntry->idx         = hRegion;
        pEntry->GCPhysFirst = GCPhys;
        pEntry->GCPhysLast  = GCPhysLast;
        pVM->iom.s.cMmioLookupEntries = cEntries + 1;

#ifdef VBOX_WITH_STATISTICS
        /* Don't register stats here when we're creating the VM as the
           statistics table may still be reallocated. */
        if (pVM->enmVMState >= VMSTATE_CREATED)
            iomR3MmioRegStats(pVM, pRegEntry);
#endif

#ifdef VBOX_STRICT
        /*
         * Assert table sanity.
         */
        AssertMsg(paEntries[0].GCPhysLast >= paEntries[0].GCPhysFirst, ("%RGp %RGp\n", paEntries[0].GCPhysLast, paEntries[0].GCPhysFirst));
        AssertMsg(paEntries[0].idx < pVM->iom.s.cMmioRegs, ("%#x %#x\n", paEntries[0].idx, pVM->iom.s.cMmioRegs));

        RTGCPHYS GCPhysPrev = paEntries[0].GCPhysLast;
        for (size_t i = 1; i <= cEntries; i++)
        {
            AssertMsg(paEntries[i].GCPhysLast >= paEntries[i].GCPhysFirst, ("%u: %RGp %RGp\n", i, paEntries[i].GCPhysLast, paEntries[i].GCPhysFirst));
            AssertMsg(paEntries[i].idx < pVM->iom.s.cMmioRegs, ("%u: %#x %#x\n", i, paEntries[i].idx, pVM->iom.s.cMmioRegs));
            AssertMsg(GCPhysPrev < paEntries[i].GCPhysFirst, ("%u: %RGp %RGp\n", i, GCPhysPrev, paEntries[i].GCPhysFirst));
            GCPhysPrev = paEntries[i].GCPhysLast;
        }
#endif
    }
    else
    {
        AssertFailed();
        rc = VERR_IOM_MMIO_REGION_ALREADY_MAPPED;
    }

    IOM_UNLOCK_EXCL(pVM);
    return rc;
}


/**
 * Worker for PDMDEVHLPR3::pfnMmioUnmap.
 */
VMMR3_INT_DECL(int) IOMR3MmioUnmap(PVM pVM, PVMCPU pVCpu, PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion)
{
    /*
     * Validate input and state.
     */
    VMCPU_ASSERT_EMT_RETURN(pVCpu, VERR_VM_THREAD_NOT_EMT);
    AssertPtrReturn(pDevIns, VERR_INVALID_HANDLE);
    AssertReturn(hRegion < pVM->iom.s.cMmioRegs, VERR_IOM_INVALID_MMIO_HANDLE);
    PIOMMMIOENTRYR3 const pRegEntry = &pVM->iom.s.paMmioRegs[hRegion];
    AssertReturn(pRegEntry->pDevIns == pDevIns, VERR_IOM_INVALID_MMIO_HANDLE);

    /*
     * Do the mapping.
     */
    int rc;
    IOM_LOCK_EXCL(pVM);

    if (pRegEntry->fMapped)
    {
        RTGCPHYS const GCPhys     = pRegEntry->GCPhysMapping;
        RTGCPHYS const GCPhysLast = GCPhys + pRegEntry->cbRegion - 1;
        uint32_t const cEntries   = RT_MIN(pVM->iom.s.cMmioLookupEntries, pVM->iom.s.cMmioRegs);
        Assert(pVM->iom.s.cMmioLookupEntries == cEntries);
        Assert(cEntries > 0);

        PIOMMMIOLOOKUPENTRY paEntries = pVM->iom.s.paMmioLookup;
        uint32_t iFirst = 0;
        uint32_t iEnd   = cEntries;
        uint32_t i      = cEntries / 2;
        for (;;)
        {
            PIOMMMIOLOOKUPENTRY pEntry = &paEntries[i];
            if (pEntry->GCPhysLast < GCPhys)
            {
                i += 1;
                if (i < iEnd)
                    iFirst = i;
                else
                {
                    rc = VERR_IOM_MMIO_IPE_1;
                    AssertLogRelMsgFailedBreak(("%RGp..%RGp (%s) not found!\n", GCPhys, GCPhysLast, pRegEntry->pszDesc));
                }
            }
            else if (pEntry->GCPhysFirst > GCPhysLast)
            {
                if (i > iFirst)
                    iEnd = i;
                else
                {
                    rc = VERR_IOM_MMIO_IPE_1;
                    AssertLogRelMsgFailedBreak(("%RGp..%RGp (%s) not found!\n", GCPhys, GCPhysLast, pRegEntry->pszDesc));
                }
            }
            else if (pEntry->idx == hRegion)
            {
                Assert(pEntry->GCPhysFirst == GCPhys);
                Assert(pEntry->GCPhysLast == GCPhysLast);
#ifdef VBOX_WITH_STATISTICS
                iomR3MmioDeregStats(pVM, pRegEntry, GCPhys);
#endif
                if (i + 1 < cEntries)
                    memmove(pEntry, pEntry + 1, sizeof(*pEntry) * (cEntries - i - 1));
                pVM->iom.s.cMmioLookupEntries = cEntries - 1;

                rc = PGMR3PhysMmioUnmap(pVM, pVCpu, GCPhys, pRegEntry->cbRegion, pRegEntry->idRamRange);
                AssertRC(rc);

                pRegEntry->fMapped = false;
                ASMAtomicWriteU64(&pRegEntry->GCPhysMapping, NIL_RTGCPHYS);
                break;
            }
            else
            {
                AssertLogRelMsgFailed(("Looking for %RGp..%RGp (%s), found %RGp..%RGp (%s) instead!\n",
                                       GCPhys, GCPhysLast, pRegEntry->pszDesc,
                                       pEntry->GCPhysFirst, pEntry->GCPhysLast, pVM->iom.s.paMmioRegs[pEntry->idx].pszDesc));
                rc = VERR_IOM_MMIO_IPE_1;
                break;
            }

            i = iFirst + (iEnd - iFirst) / 2;
        }

#ifdef VBOX_STRICT
        /*
         * Assert table sanity.
         */
        AssertMsg(paEntries[0].GCPhysLast >= paEntries[0].GCPhysFirst, ("%RGp %RGp\n", paEntries[0].GCPhysLast, paEntries[0].GCPhysFirst));
        AssertMsg(paEntries[0].idx < pVM->iom.s.cMmioRegs, ("%#x %#x\n", paEntries[0].idx, pVM->iom.s.cMmioRegs));

        RTGCPHYS GCPhysPrev = paEntries[0].GCPhysLast;
        for (i = 1; i < cEntries - 1; i++)
        {
            AssertMsg(paEntries[i].GCPhysLast >= paEntries[i].GCPhysFirst, ("%u: %RGp %RGp\n", i, paEntries[i].GCPhysLast, paEntries[i].GCPhysFirst));
            AssertMsg(paEntries[i].idx < pVM->iom.s.cMmioRegs, ("%u: %#x %#x\n", i, paEntries[i].idx, pVM->iom.s.cMmioRegs));
            AssertMsg(GCPhysPrev < paEntries[i].GCPhysFirst, ("%u: %RGp %RGp\n", i, GCPhysPrev, paEntries[i].GCPhysFirst));
            GCPhysPrev = paEntries[i].GCPhysLast;
        }
#endif
    }
    else
    {
        AssertFailed();
        rc = VERR_IOM_MMIO_REGION_NOT_MAPPED;
    }

    IOM_UNLOCK_EXCL(pVM);
    return rc;
}


VMMR3_INT_DECL(int)  IOMR3MmioReduce(PVM pVM, PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion, RTGCPHYS cbRegion)
{
    RT_NOREF(pVM, pDevIns, hRegion, cbRegion);
    AssertFailed();
    return VERR_NOT_IMPLEMENTED;
}


/**
 * Validates @a hRegion, making sure it belongs to @a pDevIns.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pDevIns     The device which allegedly owns @a hRegion.
 * @param   hRegion     The handle to validate.
 */
VMMR3_INT_DECL(int)  IOMR3MmioValidateHandle(PVM pVM, PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion)
{
    AssertPtrReturn(pDevIns, VERR_INVALID_HANDLE);
    AssertReturn(hRegion < RT_MIN(pVM->iom.s.cMmioRegs, pVM->iom.s.cMmioAlloc), VERR_IOM_INVALID_MMIO_HANDLE);
    PIOMMMIOENTRYR3 const pRegEntry = &pVM->iom.s.paMmioRegs[hRegion];
    AssertReturn(pRegEntry->pDevIns == pDevIns, VERR_IOM_INVALID_MMIO_HANDLE);
    return VINF_SUCCESS;
}


/**
 * Gets the mapping address of MMIO region @a hRegion.
 *
 * @returns Mapping address if mapped, NIL_RTGCPHYS if not mapped or invalid
 *          input.
 * @param   pVM         The cross context VM structure.
 * @param   pDevIns     The device which allegedly owns @a hRegion.
 * @param   hRegion     The handle to validate.
 */
VMMR3_INT_DECL(RTGCPHYS) IOMR3MmioGetMappingAddress(PVM pVM, PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion)
{
    AssertPtrReturn(pDevIns, NIL_RTGCPHYS);
    AssertReturn(hRegion < RT_MIN(pVM->iom.s.cMmioRegs, pVM->iom.s.cMmioAlloc), NIL_RTGCPHYS);
    PIOMMMIOENTRYR3 const pRegEntry = &pVM->iom.s.paMmioRegs[hRegion];
    AssertReturn(pRegEntry->pDevIns == pDevIns, NIL_RTGCPHYS);
    return pRegEntry->GCPhysMapping;
}


/**
 * Display all registered MMIO ranges.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The info helpers.
 * @param   pszArgs     Arguments, ignored.
 */
DECLCALLBACK(void) iomR3MmioInfo(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF(pszArgs);

    /* No locking needed here as registerations are only happening during VMSTATE_CREATING. */
    pHlp->pfnPrintf(pHlp,
                    "MMIO registrations: %u (%u allocated)\n"
                    " ## Ctx    %.*s %.*s   PCI    Description\n",
                    pVM->iom.s.cMmioRegs, pVM->iom.s.cMmioAlloc,
                    sizeof(RTGCPHYS) * 2, "Size",
                    sizeof(RTGCPHYS) * 2 * 2 + 1, "Mapping");
    PIOMMMIOENTRYR3 paRegs = pVM->iom.s.paMmioRegs;
    for (uint32_t i = 0; i < pVM->iom.s.cMmioRegs; i++)
    {
        const char * const pszRing = paRegs[i].fRing0 ? paRegs[i].fRawMode ? "+0+C" : "+0  "
                                   : paRegs[i].fRawMode ? "+C  " : "    ";
        if (paRegs[i].fMapped && paRegs[i].pPciDev)
            pHlp->pfnPrintf(pHlp, "%3u R3%s %RGp  %RGp-%RGp pci%u/%u %s\n", paRegs[i].idxSelf, pszRing, paRegs[i].cbRegion,
                            paRegs[i].GCPhysMapping, paRegs[i].GCPhysMapping + paRegs[i].cbRegion - 1,
                            paRegs[i].pPciDev->idxSubDev, paRegs[i].iPciRegion, paRegs[i].pszDesc);
        else if (paRegs[i].fMapped && !paRegs[i].pPciDev)
            pHlp->pfnPrintf(pHlp, "%3u R3%s %RGp  %RGp-%RGp        %s\n", paRegs[i].idxSelf, pszRing, paRegs[i].cbRegion,
                            paRegs[i].GCPhysMapping, paRegs[i].GCPhysMapping + paRegs[i].cbRegion - 1, paRegs[i].pszDesc);
        else if (paRegs[i].pPciDev)
            pHlp->pfnPrintf(pHlp, "%3u R3%s %RGp  %.*s pci%u/%u %s\n", paRegs[i].idxSelf, pszRing, paRegs[i].cbRegion,
                            sizeof(RTGCPHYS) * 2, "unmapped", paRegs[i].pPciDev->idxSubDev, paRegs[i].iPciRegion, paRegs[i].pszDesc);
        else
            pHlp->pfnPrintf(pHlp, "%3u R3%s %RGp  %.*s        %s\n", paRegs[i].idxSelf, pszRing, paRegs[i].cbRegion,
                            sizeof(RTGCPHYS) * 2, "unmapped", paRegs[i].pszDesc);
    }
}

