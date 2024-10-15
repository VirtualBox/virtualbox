/* $Id: PGMR0.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PGM - Page Manager and Monitor, Ring-0.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_PGM
#define VBOX_WITHOUT_PAGING_BIT_FIELDS /* 64-bit bitfields are just asking for trouble. See @bugref{9841} and others. */
#include <VBox/rawpci.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/iem.h>
#include <VBox/vmm/gmm.h>
#include "PGMInternal.h"
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/vmcc.h>
#include <VBox/vmm/gvm.h>
#include "PGMInline.h"
#include <VBox/log.h>
#include <VBox/err.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/memobj.h>
#include <iprt/process.h>
#include <iprt/rand.h>
#include <iprt/string.h>
#include <iprt/time.h>


/*
 * Instantiate the ring-0 header/code templates.
 */
/** @todo r=bird: Gotta love this nested paging hacking we're still carrying with us... (Split PGM_TYPE_NESTED.) */
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_32BIT_PROT(name)
#include "PGMR0Bth.h"
#undef PGM_BTH_NAME

#define PGM_BTH_NAME(name)          PGM_BTH_NAME_PAE_PROT(name)
#include "PGMR0Bth.h"
#undef PGM_BTH_NAME

#define PGM_BTH_NAME(name)          PGM_BTH_NAME_AMD64_PROT(name)
#include "PGMR0Bth.h"
#undef PGM_BTH_NAME

#define PGM_BTH_NAME(name)          PGM_BTH_NAME_EPT_PROT(name)
#include "PGMR0Bth.h"
#undef PGM_BTH_NAME


/**
 * Initializes the per-VM data for the PGM.
 *
 * This is called from under the GVMM lock, so it should only initialize the
 * data so PGMR0CleanupVM and others will work smoothly.
 *
 * @returns VBox status code.
 * @param   pGVM    Pointer to the global VM structure.
 * @param   hMemObj Handle to the memory object backing pGVM.
 */
VMMR0_INT_DECL(int) PGMR0InitPerVMData(PGVM pGVM, RTR0MEMOBJ hMemObj)
{
    AssertCompile(sizeof(pGVM->pgm.s) <= sizeof(pGVM->pgm.padding));
    AssertCompile(sizeof(pGVM->pgmr0.s) <= sizeof(pGVM->pgmr0.padding));
    AssertCompile(sizeof(pGVM->aCpus[0].pgm.s) <= sizeof(pGVM->aCpus[0].pgm.padding));
    AssertCompile(sizeof(pGVM->aCpus[0].pgmr0.s) <= sizeof(pGVM->aCpus[0].pgmr0.padding));

    /* Set the RAM range memory handles to NIL. */
    AssertCompile(RT_ELEMENTS(pGVM->pgmr0.s.acRamRangePages)   == RT_ELEMENTS(pGVM->pgmr0.s.apRamRanges));
    AssertCompile(RT_ELEMENTS(pGVM->pgmr0.s.ahRamRangeMemObjs) == RT_ELEMENTS(pGVM->pgmr0.s.apRamRanges));
    AssertCompile(RT_ELEMENTS(pGVM->pgmr0.s.ahRamRangeMapObjs) == RT_ELEMENTS(pGVM->pgmr0.s.apRamRanges));
    for (uint32_t i = 0; i < RT_ELEMENTS(pGVM->pgmr0.s.ahRamRangeMemObjs); i++)
    {
        pGVM->pgmr0.s.ahRamRangeMemObjs[i] = NIL_RTR0MEMOBJ;
        pGVM->pgmr0.s.ahRamRangeMapObjs[i] = NIL_RTR0MEMOBJ;
    }
    Assert(pGVM->pgmr0.s.idRamRangeMax == 0); /* the structure is ZERO'ed */

    /* Set the MMIO2 range memory handles to NIL. */
    AssertCompile(RT_ELEMENTS(pGVM->pgmr0.s.ahMmio2MemObjs) == RT_ELEMENTS(pGVM->pgmr0.s.apMmio2RamRanges));
    AssertCompile(RT_ELEMENTS(pGVM->pgmr0.s.ahMmio2MapObjs) == RT_ELEMENTS(pGVM->pgmr0.s.apMmio2RamRanges));
    for (uint32_t i = 0; i < RT_ELEMENTS(pGVM->pgmr0.s.ahMmio2MemObjs); i++)
    {
        pGVM->pgmr0.s.ahMmio2MemObjs[i] = NIL_RTR0MEMOBJ;
        pGVM->pgmr0.s.ahMmio2MapObjs[i] = NIL_RTR0MEMOBJ;
    }

    /* Set the ROM range memory handles to NIL. */
    AssertCompile(RT_ELEMENTS(pGVM->pgmr0.s.ahRomRangeMemObjs) == RT_ELEMENTS(pGVM->pgmr0.s.apRomRanges));
    AssertCompile(RT_ELEMENTS(pGVM->pgmr0.s.ahRomRangeMapObjs) == RT_ELEMENTS(pGVM->pgmr0.s.apRomRanges));
    for (uint32_t i = 0; i < RT_ELEMENTS(pGVM->pgmr0.s.ahRomRangeMemObjs); i++)
    {
        pGVM->pgmr0.s.ahRomRangeMemObjs[i] = NIL_RTR0MEMOBJ;
        pGVM->pgmr0.s.ahRomRangeMapObjs[i] = NIL_RTR0MEMOBJ;
    }

    /* Set the physical handler related memory handles to NIL. */
    AssertCompile(RT_ELEMENTS(pGVM->pgmr0.s.ahPoolMemObjs) == RT_ELEMENTS(pGVM->pgmr0.s.ahPoolMapObjs));
    for (uint32_t i = 0; i < RT_ELEMENTS(pGVM->pgmr0.s.ahPoolMemObjs); i++)
    {
        pGVM->pgmr0.s.ahPoolMemObjs[i] = NIL_RTR0MEMOBJ;
        pGVM->pgmr0.s.ahPoolMapObjs[i] = NIL_RTR0MEMOBJ;
    }
    pGVM->pgmr0.s.hPhysHandlerMemObj = NIL_RTR0MEMOBJ;
    pGVM->pgmr0.s.hPhysHandlerMapObj = NIL_RTR0MEMOBJ;

    /*
     * Initialize the handler type table with return to ring-3 callbacks so we
     * don't have to do anything special for ring-3 only registrations.
     *
     * Note! The random bits of the hType value is mainly for prevent trouble
     *       with zero initialized handles w/o needing to sacrifice handle zero.
     */
    for (size_t i = 0; i < RT_ELEMENTS(pGVM->pgm.s.aPhysHandlerTypes); i++)
    {
        pGVM->pgmr0.s.aPhysHandlerTypes[i].hType        = i | (RTRandU64() & ~(uint64_t)PGMPHYSHANDLERTYPE_IDX_MASK);
        pGVM->pgmr0.s.aPhysHandlerTypes[i].enmKind      = PGMPHYSHANDLERKIND_INVALID;
        pGVM->pgmr0.s.aPhysHandlerTypes[i].pfnHandler   = pgmR0HandlerPhysicalHandlerToRing3;
        pGVM->pgmr0.s.aPhysHandlerTypes[i].pfnPfHandler = pgmR0HandlerPhysicalPfHandlerToRing3;

        pGVM->pgm.s.aPhysHandlerTypes[i].hType          = pGVM->pgmr0.s.aPhysHandlerTypes[i].hType;
        pGVM->pgm.s.aPhysHandlerTypes[i].enmKind        = PGMPHYSHANDLERKIND_INVALID;
    }

    /*
     * Get the physical address of the ZERO and MMIO-dummy pages.
     */
    AssertReturn(((uintptr_t)&pGVM->pgm.s.abZeroPg[0] & HOST_PAGE_OFFSET_MASK) == 0, VERR_INTERNAL_ERROR_2);
    pGVM->pgm.s.HCPhysZeroPg    = RTR0MemObjGetPagePhysAddr(hMemObj, RT_UOFFSETOF_DYN(GVM, pgm.s.abZeroPg) >> HOST_PAGE_SHIFT);
    AssertReturn(pGVM->pgm.s.HCPhysZeroPg != NIL_RTHCPHYS, VERR_INTERNAL_ERROR_3);

    AssertReturn(((uintptr_t)&pGVM->pgm.s.abMmioPg[0] & HOST_PAGE_OFFSET_MASK) == 0, VERR_INTERNAL_ERROR_2);
    pGVM->pgm.s.HCPhysMmioPg    = RTR0MemObjGetPagePhysAddr(hMemObj, RT_UOFFSETOF_DYN(GVM, pgm.s.abMmioPg) >> HOST_PAGE_SHIFT);
    AssertReturn(pGVM->pgm.s.HCPhysMmioPg != NIL_RTHCPHYS, VERR_INTERNAL_ERROR_3);

    pGVM->pgm.s.HCPhysInvMmioPg = pGVM->pgm.s.HCPhysMmioPg;

    return RTCritSectInit(&pGVM->pgmr0.s.PoolGrowCritSect);
}


/**
 * Initalize the per-VM PGM for ring-0.
 *
 * @returns VBox status code.
 * @param   pGVM    Pointer to the global VM structure.
 */
VMMR0_INT_DECL(int) PGMR0InitVM(PGVM pGVM)
{
    /*
     * Set up the ring-0 context for our access handlers.
     */
    int rc = PGMR0HandlerPhysicalTypeSetUpContext(pGVM, PGMPHYSHANDLERKIND_WRITE, 0 /*fFlags*/,
                                                  pgmPhysRomWriteHandler, pgmPhysRomWritePfHandler,
                                                  "ROM write protection", pGVM->pgm.s.hRomPhysHandlerType);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Register the physical access handler doing dirty MMIO2 tracing.
     */
    rc = PGMR0HandlerPhysicalTypeSetUpContext(pGVM, PGMPHYSHANDLERKIND_WRITE, PGMPHYSHANDLER_F_KEEP_PGM_LOCK,
                                              pgmPhysMmio2WriteHandler, pgmPhysMmio2WritePfHandler,
                                              "MMIO2 dirty page tracing", pGVM->pgm.s.hMmio2DirtyPhysHandlerType);
    AssertLogRelRCReturn(rc, rc);

    /*
     * The page pool.
     */
    return pgmR0PoolInitVM(pGVM);
}


/**
 * Called at the end of the ring-0 initialization to seal access handler types.
 *
 * @param   pGVM    Pointer to the global VM structure.
 */
VMMR0_INT_DECL(void) PGMR0DoneInitVM(PGVM pGVM)
{
    /*
     * Seal all the access handler types. Does both ring-3 and ring-0.
     *
     * Note! Since this is a void function and we don't have any ring-0 state
     *       machinery for marking the VM as bogus, this code will just
     *       override corrupted values as best as it can.
     */
    AssertCompile(RT_ELEMENTS(pGVM->pgmr0.s.aPhysHandlerTypes) == RT_ELEMENTS(pGVM->pgm.s.aPhysHandlerTypes));
    for (size_t i = 0; i < RT_ELEMENTS(pGVM->pgmr0.s.aPhysHandlerTypes); i++)
    {
        PPGMPHYSHANDLERTYPEINTR0 const pTypeR0   = &pGVM->pgmr0.s.aPhysHandlerTypes[i];
        PPGMPHYSHANDLERTYPEINTR3 const pTypeR3   = &pGVM->pgm.s.aPhysHandlerTypes[i];
        PGMPHYSHANDLERKIND       const enmKindR3 = pTypeR3->enmKind;
        PGMPHYSHANDLERKIND       const enmKindR0 = pTypeR0->enmKind;
        AssertLogRelMsgStmt(pTypeR0->hType == pTypeR3->hType,
                            ("i=%u %#RX64 vs %#RX64 %s\n", i, pTypeR0->hType, pTypeR3->hType, pTypeR0->pszDesc),
                            pTypeR3->hType = pTypeR0->hType);
        switch (enmKindR3)
        {
            case PGMPHYSHANDLERKIND_ALL:
            case PGMPHYSHANDLERKIND_MMIO:
                if (   enmKindR0 == enmKindR3
                    || enmKindR0 == PGMPHYSHANDLERKIND_INVALID)
                {
                    pTypeR3->fRing0Enabled = enmKindR0 == enmKindR3;
                    pTypeR0->uState = PGM_PAGE_HNDL_PHYS_STATE_ALL;
                    pTypeR3->uState = PGM_PAGE_HNDL_PHYS_STATE_ALL;
                    continue;
                }
                break;

            case PGMPHYSHANDLERKIND_WRITE:
                if (   enmKindR0 == enmKindR3
                    || enmKindR0 == PGMPHYSHANDLERKIND_INVALID)
                {
                    pTypeR3->fRing0Enabled = enmKindR0 == enmKindR3;
                    pTypeR0->uState = PGM_PAGE_HNDL_PHYS_STATE_WRITE;
                    pTypeR3->uState = PGM_PAGE_HNDL_PHYS_STATE_WRITE;
                    continue;
                }
                break;

            default:
                AssertLogRelMsgFailed(("i=%u enmKindR3=%d\n", i, enmKindR3));
                RT_FALL_THROUGH();
            case PGMPHYSHANDLERKIND_INVALID:
                AssertLogRelMsg(enmKindR0 == PGMPHYSHANDLERKIND_INVALID,
                                ("i=%u enmKind=%d %s\n", i, enmKindR0, pTypeR0->pszDesc));
                AssertLogRelMsg(pTypeR0->pfnHandler == pgmR0HandlerPhysicalHandlerToRing3,
                                ("i=%u pfnHandler=%p %s\n", i, pTypeR0->pfnHandler, pTypeR0->pszDesc));
                AssertLogRelMsg(pTypeR0->pfnPfHandler == pgmR0HandlerPhysicalPfHandlerToRing3,
                                ("i=%u pfnPfHandler=%p %s\n", i, pTypeR0->pfnPfHandler, pTypeR0->pszDesc));

                /* Unused of bad ring-3 entry, make it and the ring-0 one harmless. */
                pTypeR3->enmKind         = PGMPHYSHANDLERKIND_END;
                pTypeR3->fRing0DevInsIdx = false;
                pTypeR3->fKeepPgmLock    = false;
                pTypeR3->uState          = 0;
                break;
        }
        pTypeR3->fRing0Enabled   = false;

        /* Make sure the entry is harmless and goes to ring-3. */
        pTypeR0->enmKind         = PGMPHYSHANDLERKIND_END;
        pTypeR0->pfnHandler      = pgmR0HandlerPhysicalHandlerToRing3;
        pTypeR0->pfnPfHandler    = pgmR0HandlerPhysicalPfHandlerToRing3;
        pTypeR0->fRing0DevInsIdx = false;
        pTypeR0->fKeepPgmLock    = false;
        pTypeR0->uState          = 0;
        pTypeR0->pszDesc         = "invalid";
    }
}


/**
 * Cleans up any loose ends before the GVM structure is destroyed.
 */
VMMR0_INT_DECL(void) PGMR0CleanupVM(PGVM pGVM)
{
    for (uint32_t i = 0; i < RT_ELEMENTS(pGVM->pgmr0.s.ahPoolMemObjs); i++)
    {
        if (pGVM->pgmr0.s.ahPoolMapObjs[i] != NIL_RTR0MEMOBJ)
        {
            int rc = RTR0MemObjFree(pGVM->pgmr0.s.ahPoolMapObjs[i], true /*fFreeMappings*/);
            AssertRC(rc);
            pGVM->pgmr0.s.ahPoolMapObjs[i] = NIL_RTR0MEMOBJ;
        }

        if (pGVM->pgmr0.s.ahPoolMemObjs[i] != NIL_RTR0MEMOBJ)
        {
            int rc = RTR0MemObjFree(pGVM->pgmr0.s.ahPoolMemObjs[i], true /*fFreeMappings*/);
            AssertRC(rc);
            pGVM->pgmr0.s.ahPoolMemObjs[i] = NIL_RTR0MEMOBJ;
        }
    }

    if (pGVM->pgmr0.s.hPhysHandlerMapObj != NIL_RTR0MEMOBJ)
    {
        int rc = RTR0MemObjFree(pGVM->pgmr0.s.hPhysHandlerMapObj, true /*fFreeMappings*/);
        AssertRC(rc);
        pGVM->pgmr0.s.hPhysHandlerMapObj = NIL_RTR0MEMOBJ;
    }

    if (pGVM->pgmr0.s.hPhysHandlerMemObj != NIL_RTR0MEMOBJ)
    {
        int rc = RTR0MemObjFree(pGVM->pgmr0.s.hPhysHandlerMemObj, true /*fFreeMappings*/);
        AssertRC(rc);
        pGVM->pgmr0.s.hPhysHandlerMemObj = NIL_RTR0MEMOBJ;
    }

    for (uint32_t i = 0; i < RT_ELEMENTS(pGVM->pgmr0.s.ahRomRangeMemObjs); i++)
    {
        if (pGVM->pgmr0.s.ahRomRangeMapObjs[i] != NIL_RTR0MEMOBJ)
        {
            int rc = RTR0MemObjFree(pGVM->pgmr0.s.ahRomRangeMapObjs[i], true /*fFreeMappings*/);
            AssertRC(rc);
            pGVM->pgmr0.s.ahRomRangeMapObjs[i] = NIL_RTR0MEMOBJ;
        }

        if (pGVM->pgmr0.s.ahRomRangeMemObjs[i] != NIL_RTR0MEMOBJ)
        {
            int rc = RTR0MemObjFree(pGVM->pgmr0.s.ahRomRangeMemObjs[i], true /*fFreeMappings*/);
            AssertRC(rc);
            pGVM->pgmr0.s.ahRomRangeMemObjs[i] = NIL_RTR0MEMOBJ;
        }
    }

    for (uint32_t i = 0; i < RT_ELEMENTS(pGVM->pgmr0.s.ahMmio2MemObjs); i++)
    {
        if (pGVM->pgmr0.s.ahMmio2MapObjs[i] != NIL_RTR0MEMOBJ)
        {
            int rc = RTR0MemObjFree(pGVM->pgmr0.s.ahMmio2MapObjs[i], true /*fFreeMappings*/);
            AssertRC(rc);
            pGVM->pgmr0.s.ahMmio2MapObjs[i] = NIL_RTR0MEMOBJ;
        }

        if (pGVM->pgmr0.s.ahMmio2MemObjs[i] != NIL_RTR0MEMOBJ)
        {
            int rc = RTR0MemObjFree(pGVM->pgmr0.s.ahMmio2MemObjs[i], true /*fFreeMappings*/);
            AssertRC(rc);
            pGVM->pgmr0.s.ahMmio2MemObjs[i] = NIL_RTR0MEMOBJ;
        }
    }

    uint32_t const cRangesMax = RT_MIN(pGVM->pgmr0.s.idRamRangeMax, RT_ELEMENTS(pGVM->pgmr0.s.ahRamRangeMemObjs) - 1U) + 1U;
    for (uint32_t i = 0; i < cRangesMax; i++)
    {
        if (pGVM->pgmr0.s.ahRamRangeMapObjs[i] != NIL_RTR0MEMOBJ)
        {
            int rc = RTR0MemObjFree(pGVM->pgmr0.s.ahRamRangeMapObjs[i], true /*fFreeMappings*/);
            AssertRC(rc);
            pGVM->pgmr0.s.ahRamRangeMapObjs[i] = NIL_RTR0MEMOBJ;
        }

        if (pGVM->pgmr0.s.ahRamRangeMemObjs[i] != NIL_RTR0MEMOBJ)
        {
            int rc = RTR0MemObjFree(pGVM->pgmr0.s.ahRamRangeMemObjs[i], true /*fFreeMappings*/);
            AssertRC(rc);
            pGVM->pgmr0.s.ahRamRangeMemObjs[i] = NIL_RTR0MEMOBJ;
        }
    }

    if (RTCritSectIsInitialized(&pGVM->pgmr0.s.PoolGrowCritSect))
        RTCritSectDelete(&pGVM->pgmr0.s.PoolGrowCritSect);
}


/**
 * Worker function for PGMR3PhysAllocateHandyPages and pgmPhysEnsureHandyPage.
 *
 * @returns The following VBox status codes.
 * @retval  VINF_SUCCESS on success. FF cleared.
 * @retval  VINF_EM_NO_MEMORY if we're out of memory. The FF is set in this case.
 *
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   idCpu       The ID of the calling EMT.
 * @param   fRing3      Set if the caller is ring-3.  Determins whether to
 *                      return VINF_EM_NO_MEMORY or not.
 *
 * @thread  EMT(idCpu)
 *
 * @remarks Must be called from within the PGM critical section. The caller
 *          must clear the new pages.
 */
int pgmR0PhysAllocateHandyPages(PGVM pGVM, VMCPUID idCpu, bool fRing3)
{
    /*
     * Validate inputs.
     */
    AssertReturn(idCpu < pGVM->cCpus, VERR_INVALID_CPU_ID); /* caller already checked this, but just to be sure. */
    Assert(pGVM->aCpus[idCpu].hEMT == RTThreadNativeSelf());
    PGM_LOCK_ASSERT_OWNER_EX(pGVM, &pGVM->aCpus[idCpu]);

    /*
     * Check for error injection.
     */
    if (RT_LIKELY(!pGVM->pgm.s.fErrInjHandyPages))
    { /* likely */ }
    else
        return VERR_NO_MEMORY;

    /*
     * Try allocate a full set of handy pages.
     */
    uint32_t const iFirst = pGVM->pgm.s.cHandyPages;
    AssertMsgReturn(iFirst <= RT_ELEMENTS(pGVM->pgm.s.aHandyPages), ("%#x\n", iFirst), VERR_PGM_HANDY_PAGE_IPE);

    uint32_t const cPages = RT_ELEMENTS(pGVM->pgm.s.aHandyPages) - iFirst;
    if (!cPages)
        return VINF_SUCCESS;

    int rc = GMMR0AllocateHandyPages(pGVM, idCpu, cPages, cPages, &pGVM->pgm.s.aHandyPages[iFirst]);
    if (RT_SUCCESS(rc))
    {
        uint32_t const cHandyPages = RT_ELEMENTS(pGVM->pgm.s.aHandyPages); /** @todo allow allocating less... */
        pGVM->pgm.s.cHandyPages = cHandyPages;
        VM_FF_CLEAR(pGVM, VM_FF_PGM_NEED_HANDY_PAGES);
        VM_FF_CLEAR(pGVM, VM_FF_PGM_NO_MEMORY);

#ifdef VBOX_STRICT
        for (uint32_t i = 0; i < cHandyPages; i++)
        {
            Assert(pGVM->pgm.s.aHandyPages[i].idPage != NIL_GMM_PAGEID);
            Assert(pGVM->pgm.s.aHandyPages[i].idPage <= GMM_PAGEID_LAST);
            Assert(pGVM->pgm.s.aHandyPages[i].idSharedPage == NIL_GMM_PAGEID);
            Assert(pGVM->pgm.s.aHandyPages[i].HCPhysGCPhys != NIL_GMMPAGEDESC_PHYS);
            Assert(!(pGVM->pgm.s.aHandyPages[i].HCPhysGCPhys & ~X86_PTE_PAE_PG_MASK));
        }
#endif

        /*
         * Clear the pages.
         */
        for (uint32_t iPage = iFirst; iPage < cHandyPages; iPage++)
        {
            PGMMPAGEDESC pPage = &pGVM->pgm.s.aHandyPages[iPage];
            if (!pPage->fZeroed)
            {
                void *pv = NULL;
#ifdef VBOX_WITH_LINEAR_HOST_PHYS_MEM
                rc = SUPR0HCPhysToVirt(pPage->HCPhysGCPhys, &pv);
#else
                rc = GMMR0PageIdToVirt(pGVM, pPage->idPage, &pv);
#endif
                AssertMsgRCReturn(rc, ("idPage=%#x HCPhys=%RHp rc=%Rrc\n", pPage->idPage, pPage->HCPhysGCPhys, rc), rc);

                RT_BZERO(pv, GUEST_PAGE_SIZE);
                pPage->fZeroed = true;
            }
#ifdef VBOX_STRICT
            else
            {
                void *pv = NULL;
# ifdef VBOX_WITH_LINEAR_HOST_PHYS_MEM
                rc = SUPR0HCPhysToVirt(pPage->HCPhysGCPhys, &pv);
# else
                rc = GMMR0PageIdToVirt(pGVM, pPage->idPage, &pv);
# endif
                AssertMsgRCReturn(rc, ("idPage=%#x HCPhys=%RHp rc=%Rrc\n", pPage->idPage, pPage->HCPhysGCPhys, rc), rc);
                AssertReturn(ASMMemIsZero(pv, GUEST_PAGE_SIZE), VERR_PGM_HANDY_PAGE_IPE);
            }
#endif
            Log3(("PGMR0PhysAllocateHandyPages: idPage=%#x HCPhys=%RGp\n", pPage->idPage, pPage->HCPhysGCPhys));
        }
    }
    else
    {
        /*
         * We should never get here unless there is a genuine shortage of
         * memory (or some internal error). Flag the error so the VM can be
         * suspended ASAP and the user informed. If we're totally out of
         * handy pages we will return failure.
         */
        /* Report the failure. */
        LogRel(("PGM: Failed to procure handy pages; rc=%Rrc cHandyPages=%#x\n"
                "     cAllPages=%#x cPrivatePages=%#x cSharedPages=%#x cZeroPages=%#x\n",
                rc, pGVM->pgm.s.cHandyPages,
                pGVM->pgm.s.cAllPages, pGVM->pgm.s.cPrivatePages, pGVM->pgm.s.cSharedPages, pGVM->pgm.s.cZeroPages));

        GMMMEMSTATSREQ Stats = { { SUPVMMR0REQHDR_MAGIC, sizeof(Stats) }, 0, 0, 0, 0, 0 };
        if (RT_SUCCESS(GMMR0QueryMemoryStatsReq(pGVM, idCpu, &Stats)))
            LogRel(("GMM: Statistics:\n"
                    "     Allocated pages: %RX64\n"
                    "     Free      pages: %RX64\n"
                    "     Shared    pages: %RX64\n"
                    "     Maximum   pages: %RX64\n"
                    "     Ballooned pages: %RX64\n",
                    Stats.cAllocPages, Stats.cFreePages, Stats.cSharedPages, Stats.cMaxPages, Stats.cBalloonedPages));

        if (   rc != VERR_NO_MEMORY
            && rc != VERR_NO_PHYS_MEMORY
            && rc != VERR_LOCK_FAILED)
            for (uint32_t iPage = 0; iPage < RT_ELEMENTS(pGVM->pgm.s.aHandyPages); iPage++)
                LogRel(("PGM: aHandyPages[#%#04x] = {.HCPhysGCPhys=%RHp, .idPage=%#08x, .idSharedPage=%#08x}\n",
                        iPage, pGVM->pgm.s.aHandyPages[iPage].HCPhysGCPhys, pGVM->pgm.s.aHandyPages[iPage].idPage,
                        pGVM->pgm.s.aHandyPages[iPage].idSharedPage));

        /* Set the FFs and adjust rc. */
        VM_FF_SET(pGVM, VM_FF_PGM_NEED_HANDY_PAGES);
        VM_FF_SET(pGVM, VM_FF_PGM_NO_MEMORY);
        if (!fRing3)
            if (   rc == VERR_NO_MEMORY
                || rc == VERR_NO_PHYS_MEMORY
                || rc == VERR_LOCK_FAILED
                || rc == VERR_MAP_FAILED)
                rc = VINF_EM_NO_MEMORY;
    }

    LogFlow(("PGMR0PhysAllocateHandyPages: cPages=%d rc=%Rrc\n", cPages, rc));
    return rc;
}


/**
 * Worker function for PGMR3PhysAllocateHandyPages / VMMR0_DO_PGM_ALLOCATE_HANDY_PAGES.
 *
 * @returns The following VBox status codes.
 * @retval  VINF_SUCCESS on success. FF cleared.
 * @retval  VINF_EM_NO_MEMORY if we're out of memory. The FF is set in this case.
 *
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   idCpu       The ID of the calling EMT.
 *
 * @thread  EMT(idCpu)
 *
 * @remarks Must be called from within the PGM critical section. The caller
 *          must clear the new pages.
 */
VMMR0_INT_DECL(int) PGMR0PhysAllocateHandyPages(PGVM pGVM, VMCPUID idCpu)
{
    /*
     * Validate inputs.
     */
    AssertReturn(idCpu < pGVM->cCpus, VERR_INVALID_CPU_ID); /* caller already checked this, but just to be sure. */
    AssertReturn(pGVM->aCpus[idCpu].hEMT == RTThreadNativeSelf(), VERR_NOT_OWNER);

    /*
     * Enter the PGM lock and call the worker.
     */
    int rc = PGM_LOCK(pGVM);
    if (RT_SUCCESS(rc))
    {
        rc = pgmR0PhysAllocateHandyPages(pGVM, idCpu, true /*fRing3*/);
        PGM_UNLOCK(pGVM);
    }
    return rc;
}


/**
 * Flushes any changes pending in the handy page array.
 *
 * It is very important that this gets done when page sharing is enabled.
 *
 * @returns The following VBox status codes.
 * @retval  VINF_SUCCESS on success. FF cleared.
 *
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   idCpu       The ID of the calling EMT.
 *
 * @thread  EMT(idCpu)
 *
 * @remarks Must be called from within the PGM critical section.
 */
VMMR0_INT_DECL(int) PGMR0PhysFlushHandyPages(PGVM pGVM, VMCPUID idCpu)
{
    /*
     * Validate inputs.
     */
    AssertReturn(idCpu < pGVM->cCpus, VERR_INVALID_CPU_ID); /* caller already checked this, but just to be sure. */
    AssertReturn(pGVM->aCpus[idCpu].hEMT == RTThreadNativeSelf(), VERR_NOT_OWNER);
    PGM_LOCK_ASSERT_OWNER_EX(pGVM, &pGVM->aCpus[idCpu]);

    /*
     * Try allocate a full set of handy pages.
     */
    uint32_t iFirst = pGVM->pgm.s.cHandyPages;
    AssertReturn(iFirst <= RT_ELEMENTS(pGVM->pgm.s.aHandyPages), VERR_PGM_HANDY_PAGE_IPE);
    uint32_t cPages = RT_ELEMENTS(pGVM->pgm.s.aHandyPages) - iFirst;
    if (!cPages)
        return VINF_SUCCESS;
    int rc = GMMR0AllocateHandyPages(pGVM, idCpu, cPages, 0, &pGVM->pgm.s.aHandyPages[iFirst]);

    LogFlow(("PGMR0PhysFlushHandyPages: cPages=%d rc=%Rrc\n", cPages, rc));
    return rc;
}


/**
 * Allocate a large page at @a GCPhys.
 *
 * @returns The following VBox status codes.
 * @retval  VINF_SUCCESS on success.
 * @retval  VINF_EM_NO_MEMORY if we're out of memory.
 *
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   idCpu       The ID of the calling EMT.
 * @param   GCPhys      The guest physical address of the page.
 *
 * @thread  EMT(idCpu)
 *
 * @remarks Must be called from within the PGM critical section. The caller
 *          must clear the new pages.
 */
int pgmR0PhysAllocateLargePage(PGVM pGVM, VMCPUID idCpu, RTGCPHYS GCPhys)
{
    STAM_PROFILE_START(&pGVM->pgm.s.Stats.StatLargePageAlloc2, a);
    PGM_LOCK_ASSERT_OWNER_EX(pGVM, &pGVM->aCpus[idCpu]);

    /*
     * Allocate a large page.
     */
    RTHCPHYS HCPhys = NIL_GMMPAGEDESC_PHYS;
    uint32_t idPage = NIL_GMM_PAGEID;

    if (true) /** @todo pre-allocate 2-3 pages on the allocation thread. */
    {
        uint64_t const nsAllocStart = RTTimeNanoTS();
        if (nsAllocStart < pGVM->pgm.s.nsLargePageRetry)
        {
            LogFlowFunc(("returns VERR_TRY_AGAIN - %RU64 ns left of hold off period\n", pGVM->pgm.s.nsLargePageRetry - nsAllocStart));
            return VERR_TRY_AGAIN;
        }

        int const rc = GMMR0AllocateLargePage(pGVM, idCpu, _2M, &idPage, &HCPhys);

        uint64_t const nsAllocEnd = RTTimeNanoTS();
        uint64_t const cNsElapsed = nsAllocEnd - nsAllocStart;
        STAM_REL_PROFILE_ADD_PERIOD(&pGVM->pgm.s.StatLargePageAlloc, cNsElapsed);
        if (cNsElapsed < RT_NS_100MS)
            pGVM->pgm.s.cLargePageLongAllocRepeats = 0;
        else
        {
            /* If a large page allocation takes more than 100ms back off for a
               while so the host OS can reshuffle memory and make some more large
               pages available.  However if it took over a second, just disable it. */
            STAM_REL_COUNTER_INC(&pGVM->pgm.s.StatLargePageOverflow);
            pGVM->pgm.s.cLargePageLongAllocRepeats++;
            if (cNsElapsed > RT_NS_1SEC)
            {
                LogRel(("PGMR0PhysAllocateLargePage: Disabling large pages after %'RU64 ns allocation time.\n", cNsElapsed));
                PGMSetLargePageUsage(pGVM, false);
            }
            else
            {
                Log(("PGMR0PhysAllocateLargePage: Suspending large page allocations for %u sec after %'RU64 ns allocation time.\n",
                     30 * pGVM->pgm.s.cLargePageLongAllocRepeats, cNsElapsed));
                pGVM->pgm.s.nsLargePageRetry = nsAllocEnd + RT_NS_30SEC * pGVM->pgm.s.cLargePageLongAllocRepeats;
            }
        }

        if (RT_FAILURE(rc))
        {
            Log(("PGMR0PhysAllocateLargePage: Failed: %Rrc\n", rc));
            STAM_REL_COUNTER_INC(&pGVM->pgm.s.StatLargePageAllocFailed);
            if (rc == VERR_NOT_SUPPORTED)
            {
                LogRel(("PGM: Disabling large pages because of VERR_NOT_SUPPORTED status.\n"));
                PGMSetLargePageUsage(pGVM, false);
            }
            return rc;
        }
    }

    STAM_PROFILE_STOP_START(&pGVM->pgm.s.Stats.StatLargePageAlloc2, &pGVM->pgm.s.Stats.StatLargePageSetup, a);

    /*
     * Enter the pages into PGM.
     */
    bool         fFlushTLBs = false;
    VBOXSTRICTRC rc         = VINF_SUCCESS;
    unsigned     cLeft      = _2M / GUEST_PAGE_SIZE;
    while (cLeft-- > 0)
    {
        PPGMPAGE const pPage = pgmPhysGetPage(pGVM, GCPhys);
        AssertReturn(pPage && PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_RAM && PGM_PAGE_IS_ZERO(pPage), VERR_PGM_UNEXPECTED_PAGE_STATE);

        /* Make sure there are no zero mappings. */
        uint16_t const u16Tracking = PGM_PAGE_GET_TRACKING(pPage);
        if (u16Tracking == 0)
            Assert(PGM_PAGE_GET_PTE_INDEX(pPage) == 0);
        else
        {
            STAM_REL_COUNTER_INC(&pGVM->pgm.s.StatLargePageZeroEvict);
            VBOXSTRICTRC rc3 = pgmPoolTrackUpdateGCPhys(pGVM, GCPhys, pPage, true /*fFlushPTEs*/, &fFlushTLBs);
            Log(("PGMR0PhysAllocateLargePage: GCPhys=%RGp: tracking=%#x rc3=%Rrc\n", GCPhys, u16Tracking, VBOXSTRICTRC_VAL(rc3)));
            if (rc3 != VINF_SUCCESS && rc == VINF_SUCCESS)
                rc = rc3; /** @todo not perfect... */
            PGM_PAGE_SET_PTE_INDEX(pGVM, pPage, 0);
            PGM_PAGE_SET_TRACKING(pGVM, pPage, 0);
        }

        /* Setup the new page. */
        PGM_PAGE_SET_HCPHYS(pGVM, pPage, HCPhys);
        PGM_PAGE_SET_STATE(pGVM, pPage, PGM_PAGE_STATE_ALLOCATED);
        PGM_PAGE_SET_PDE_TYPE(pGVM, pPage, PGM_PAGE_PDE_TYPE_PDE);
        PGM_PAGE_SET_PAGEID(pGVM, pPage, idPage);
        Log3(("PGMR0PhysAllocateLargePage: GCPhys=%RGp: idPage=%#x HCPhys=%RGp (old tracking=%#x)\n",
              GCPhys, idPage, HCPhys, u16Tracking));

        /* advance */
        idPage++;
        HCPhys += GUEST_PAGE_SIZE;
        GCPhys += GUEST_PAGE_SIZE;
    }

    STAM_COUNTER_ADD(&pGVM->pgm.s.Stats.StatRZPageReplaceZero, _2M / GUEST_PAGE_SIZE);
    pGVM->pgm.s.cZeroPages    -= _2M / GUEST_PAGE_SIZE;
    pGVM->pgm.s.cPrivatePages += _2M / GUEST_PAGE_SIZE;

    /*
     * Flush all TLBs.
     */
    if (!fFlushTLBs)
    { /* likely as we shouldn't normally map zero pages */ }
    else
    {
        STAM_REL_COUNTER_INC(&pGVM->pgm.s.StatLargePageTlbFlush);
        PGM_INVL_ALL_VCPU_TLBS(pGVM);
    }
    /** @todo this is a little expensive (~3000 ticks) since we'll have to
     * invalidate everything.  Add a version to the TLB? */
    pgmPhysInvalidatePageMapTLB(pGVM, false /*fInRendezvous*/);
    IEMTlbInvalidateAllPhysicalAllCpus(pGVM, idCpu, IEMTLBPHYSFLUSHREASON_ALLOCATED_LARGE);

    STAM_PROFILE_STOP(&pGVM->pgm.s.Stats.StatLargePageSetup, a);
#if 0 /** @todo returning info statuses here might not be a great idea... */
    LogFlow(("PGMR0PhysAllocateLargePage: returns %Rrc\n", VBOXSTRICTRC_VAL(rc) ));
    return VBOXSTRICTRC_TODO(rc);
#else
    LogFlow(("PGMR0PhysAllocateLargePage: returns VINF_SUCCESS (rc=%Rrc)\n", VBOXSTRICTRC_VAL(rc) ));
    return VINF_SUCCESS;
#endif
}


/**
 * Allocate a large page at @a GCPhys.
 *
 * @returns The following VBox status codes.
 * @retval  VINF_SUCCESS on success.
 * @retval  VINF_EM_NO_MEMORY if we're out of memory.
 *
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   idCpu       The ID of the calling EMT.
 * @param   GCPhys      The guest physical address of the page.
 *
 * @thread  EMT(idCpu)
 *
 * @remarks Must be called from within the PGM critical section. The caller
 *          must clear the new pages.
 */
VMMR0_INT_DECL(int) PGMR0PhysAllocateLargePage(PGVM pGVM, VMCPUID idCpu, RTGCPHYS GCPhys)
{
    /*
     * Validate inputs.
     */
    AssertReturn(idCpu < pGVM->cCpus, VERR_INVALID_CPU_ID);
    AssertReturn(pGVM->aCpus[idCpu].hEMT == RTThreadNativeSelf(), VERR_NOT_OWNER);

    int rc = PGM_LOCK(pGVM);
    AssertRCReturn(rc, rc);

    /* The caller might have done this already, but since we're ring-3 callable we
       need to make sure everything is fine before starting the allocation here. */
    for (unsigned i = 0; i < _2M / GUEST_PAGE_SIZE; i++)
    {
        PPGMPAGE pPage;
        rc = pgmPhysGetPageEx(pGVM, GCPhys + i * GUEST_PAGE_SIZE, &pPage);
        AssertRCReturnStmt(rc, PGM_UNLOCK(pGVM), rc);
        AssertReturnStmt(PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_RAM, PGM_UNLOCK(pGVM), VERR_PGM_PHYS_NOT_RAM);
        AssertReturnStmt(PGM_PAGE_IS_ZERO(pPage), PGM_UNLOCK(pGVM), VERR_PGM_UNEXPECTED_PAGE_STATE);
    }

    /*
     * Call common code.
     */
    rc = pgmR0PhysAllocateLargePage(pGVM, idCpu, GCPhys);

    PGM_UNLOCK(pGVM);
    return rc;
}


/**
 * Locate a MMIO2 range.
 *
 * @returns Pointer to the MMIO2 range.
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   pDevIns     The device instance owning the region.
 * @param   hMmio2      Handle to look up.
 */
DECLINLINE(int32_t) pgmR0PhysMmio2ValidateHandle(PGVM pGVM, PPDMDEVINS pDevIns, PGMMMIO2HANDLE hMmio2)
{
    /*
     * We use the lookup table here as list walking is tedious in ring-0 when using
     * ring-3 pointers and this probably will require some kind of refactoring anyway.
     */
    AssertReturn(hMmio2 <= RT_ELEMENTS(pGVM->pgm.s.aMmio2Ranges) && hMmio2 != 0, VERR_INVALID_HANDLE);
    uint32_t const idx = hMmio2 - 1U;
    AssertReturn(pGVM->pgm.s.aMmio2Ranges[idx].pDevInsR3 == pDevIns->pDevInsForR3, VERR_NOT_OWNER);
    AssertReturn(pGVM->pgm.s.aMmio2Ranges[idx].idMmio2 == hMmio2, VERR_INVALID_HANDLE);
    AssertReturn(pGVM->pgmr0.s.ahMmio2MapObjs[idx] != NIL_RTR0MEMOBJ, VERR_INVALID_HANDLE);
    AssertReturn(pGVM->pgmr0.s.acMmio2RangePages[idx] != 0, VERR_INVALID_HANDLE);
    return idx;
}


/**
 * Worker for PDMDEVHLPR0::pfnMmio2SetUpContext.
 *
 * @returns VBox status code.
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   pDevIns     The device instance.
 * @param   hMmio2      The MMIO2 region to map into ring-0 address space.
 * @param   offSub      The offset into the region.
 * @param   cbSub       The size of the mapping, zero meaning all the rest.
 * @param   ppvMapping  Where to return the ring-0 mapping address.
 */
VMMR0_INT_DECL(int) PGMR0PhysMMIO2MapKernel(PGVM pGVM, PPDMDEVINS pDevIns, PGMMMIO2HANDLE hMmio2,
                                            size_t offSub, size_t cbSub, void **ppvMapping)
{
    *ppvMapping = NULL;
    AssertReturn(!(offSub & HOST_PAGE_OFFSET_MASK), VERR_UNSUPPORTED_ALIGNMENT);
    AssertReturn(!(cbSub  & HOST_PAGE_OFFSET_MASK), VERR_UNSUPPORTED_ALIGNMENT);

    /*
     * Validate and translate hMmio2 into an MMIO2 index.
     */
    uint32_t const   idxFirst = pgmR0PhysMmio2ValidateHandle(pGVM, pDevIns, hMmio2);
    AssertReturn((int32_t)idxFirst >= 0, (int32_t)idxFirst);

#ifndef VBOX_WITH_LINEAR_HOST_PHYS_MEM
    uint8_t * const  pbR0     = pGVM->pgmr0.s.apbMmio2Backing[idxFirst];
#else
    RTR0MEMOBJ const hMemObj  = pGVM->pgmr0.s.ahMmio2MemObjs[idxFirst];
#endif
    RTGCPHYS const   cbReal   = (RTGCPHYS)pGVM->pgmr0.s.acMmio2RangePages[idxFirst] << GUEST_PAGE_SHIFT;
    ASMCompilerBarrier();

    AssertReturn(offSub < cbReal, VERR_OUT_OF_RANGE);
    if (cbSub == 0)
        cbSub = cbReal - offSub;
    else
        AssertReturn(cbSub < cbReal && cbSub + offSub <= cbReal, VERR_OUT_OF_RANGE);

#ifndef VBOX_WITH_LINEAR_HOST_PHYS_MEM
    /*
     * Just return the address of the existing ring-0 mapping.
     */
    AssertPtrReturn(pbR0, VERR_INTERNAL_ERROR_4);
    *ppvMapping = &pbR0[offSub];
    return VINF_SUCCESS;
#else
    /*
     * Call IPRT to do the mapping.  Cleanup is done indirectly by telling
     * RTR0MemObjFree to include mappings.  It can only be done once, so no
     * risk of excessive mapping leaks.
     */
    RTR0MEMOBJ hMapObj;
    int rc = RTR0MemObjMapKernelEx(&hMapObj, hMemObj, (void *)-1, 0, RTMEM_PROT_READ | RTMEM_PROT_WRITE, offSub, cbSub);
    if (RT_SUCCESS(rc))
        *ppvMapping = RTR0MemObjAddress(hMapObj);
    return rc;
#endif
}


/**
 * This is called during PGMR3Init to init the physical access handler allocator
 * and tree.
 *
 * @returns VBox status code.
 * @param   pGVM        Pointer to the global VM structure.
 * @param   cEntries    Desired number of physical access handlers to reserve
 *                      space for (will be adjusted).
 * @thread  EMT(0)
 */
VMMR0_INT_DECL(int) PGMR0PhysHandlerInitReqHandler(PGVM pGVM, uint32_t cEntries)
{
    /*
     * Validate the input and state.
     */
    int rc = GVMMR0ValidateGVMandEMT(pGVM, 0);
    AssertRCReturn(rc, rc);
    VM_ASSERT_STATE_RETURN(pGVM, VMSTATE_CREATING, VERR_VM_INVALID_VM_STATE); /** @todo ring-0 safe state check. */

    AssertReturn(pGVM->pgmr0.s.PhysHandlerAllocator.m_paNodes == NULL, VERR_WRONG_ORDER);
    AssertReturn(pGVM->pgm.s.PhysHandlerAllocator.m_paNodes == NULL, VERR_WRONG_ORDER);

    AssertLogRelMsgReturn(cEntries <= _64K, ("%#x\n", cEntries), VERR_OUT_OF_RANGE);

    /*
     * Calculate the table size and allocate it.
     */
    uint32_t       cbTreeAndBitmap = 0;
    uint32_t const cbTotalAligned  = pgmHandlerPhysicalCalcTableSizes(&cEntries, &cbTreeAndBitmap);
    RTR0MEMOBJ     hMemObj         = NIL_RTR0MEMOBJ;
    rc = RTR0MemObjAllocPage(&hMemObj, cbTotalAligned, false);
    if (RT_SUCCESS(rc))
    {
        RTR0MEMOBJ hMapObj = NIL_RTR0MEMOBJ;
        rc = RTR0MemObjMapUser(&hMapObj, hMemObj, (RTR3PTR)-1, 0, RTMEM_PROT_READ | RTMEM_PROT_WRITE, RTR0ProcHandleSelf());
        if (RT_SUCCESS(rc))
        {
            uint8_t *pb = (uint8_t *)RTR0MemObjAddress(hMemObj);
            if (!RTR0MemObjWasZeroInitialized(hMemObj))
                RT_BZERO(pb, cbTotalAligned);

            pGVM->pgmr0.s.PhysHandlerAllocator.initSlabAllocator(cEntries, (PPGMPHYSHANDLER)&pb[cbTreeAndBitmap],
                                                                 (uint64_t *)&pb[sizeof(PGMPHYSHANDLERTREE)]);
            pGVM->pgmr0.s.pPhysHandlerTree = (PPGMPHYSHANDLERTREE)pb;
            pGVM->pgmr0.s.pPhysHandlerTree->initWithAllocator(&pGVM->pgmr0.s.PhysHandlerAllocator);
            pGVM->pgmr0.s.hPhysHandlerMemObj = hMemObj;
            pGVM->pgmr0.s.hPhysHandlerMapObj = hMapObj;

            AssertCompile(sizeof(pGVM->pgm.s.PhysHandlerAllocator) == sizeof(pGVM->pgmr0.s.PhysHandlerAllocator));
            RTR3PTR R3Ptr = RTR0MemObjAddressR3(hMapObj);
            pGVM->pgm.s.pPhysHandlerTree                    = R3Ptr;
            pGVM->pgm.s.PhysHandlerAllocator.m_paNodes      = R3Ptr + cbTreeAndBitmap;
            pGVM->pgm.s.PhysHandlerAllocator.m_pbmAlloc     = R3Ptr + sizeof(PGMPHYSHANDLERTREE);
            pGVM->pgm.s.PhysHandlerAllocator.m_cNodes       = cEntries;
            pGVM->pgm.s.PhysHandlerAllocator.m_cErrors      = 0;
            pGVM->pgm.s.PhysHandlerAllocator.m_idxAllocHint = 0;
            pGVM->pgm.s.PhysHandlerAllocator.m_uPadding     = 0;
            return VINF_SUCCESS;
        }

        RTR0MemObjFree(hMemObj, true /*fFreeMappings*/);
    }
    return rc;
}


/**
 * Updates a physical access handler type with ring-0 callback functions.
 *
 * The handler type must first have been registered in ring-3.
 *
 * @returns VBox status code.
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   enmKind         The kind of access handler.
 * @param   fFlags          PGMPHYSHANDLER_F_XXX
 * @param   pfnHandler      Pointer to the ring-0 handler callback.
 * @param   pfnPfHandler    Pointer to the ring-0 \#PF handler callback.
 *                          callback.  Can be NULL (not recommended though).
 * @param   pszDesc         The type description.
 * @param   hType           The handle to do ring-0 callback registrations for.
 * @thread  EMT(0)
 */
VMMR0_INT_DECL(int) PGMR0HandlerPhysicalTypeSetUpContext(PGVM pGVM, PGMPHYSHANDLERKIND enmKind, uint32_t fFlags,
                                                         PFNPGMPHYSHANDLER pfnHandler, PFNPGMRZPHYSPFHANDLER pfnPfHandler,
                                                         const char *pszDesc, PGMPHYSHANDLERTYPE hType)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pfnHandler, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pfnPfHandler, VERR_INVALID_POINTER);

    AssertPtrReturn(pszDesc, VERR_INVALID_POINTER);
    AssertReturn(   enmKind == PGMPHYSHANDLERKIND_WRITE
                 || enmKind == PGMPHYSHANDLERKIND_ALL
                 || enmKind == PGMPHYSHANDLERKIND_MMIO,
                 VERR_INVALID_PARAMETER);
    AssertMsgReturn(!(fFlags & ~PGMPHYSHANDLER_F_VALID_MASK), ("%#x\n", fFlags), VERR_INVALID_FLAGS);

    PPGMPHYSHANDLERTYPEINTR0 const pTypeR0 = &pGVM->pgmr0.s.aPhysHandlerTypes[hType & PGMPHYSHANDLERTYPE_IDX_MASK];
    AssertMsgReturn(hType == pTypeR0->hType, ("%#RX64, expected=%#RX64\n", hType, pTypeR0->hType), VERR_INVALID_HANDLE);
    AssertCompile(RT_ELEMENTS(pGVM->pgmr0.s.aPhysHandlerTypes) == RT_ELEMENTS(pGVM->pgm.s.aPhysHandlerTypes));
    AssertCompile(RT_ELEMENTS(pGVM->pgmr0.s.aPhysHandlerTypes) == PGMPHYSHANDLERTYPE_IDX_MASK + 1);
    AssertReturn(pTypeR0->enmKind == PGMPHYSHANDLERKIND_INVALID, VERR_ALREADY_INITIALIZED);

    int rc = GVMMR0ValidateGVMandEMT(pGVM, 0);
    AssertRCReturn(rc, rc);
    VM_ASSERT_STATE_RETURN(pGVM, VMSTATE_CREATING, VERR_VM_INVALID_VM_STATE); /** @todo ring-0 safe state check. */

    PPGMPHYSHANDLERTYPEINTR3 const pTypeR3 = &pGVM->pgm.s.aPhysHandlerTypes[hType & PGMPHYSHANDLERTYPE_IDX_MASK];
    AssertMsgReturn(pTypeR3->enmKind == enmKind,
                    ("%#x: %d, expected %d\n", hType, pTypeR3->enmKind, enmKind),
                    VERR_INVALID_HANDLE);
    AssertMsgReturn(pTypeR3->fKeepPgmLock == RT_BOOL(fFlags & PGMPHYSHANDLER_F_KEEP_PGM_LOCK),
                    ("%#x: %d, fFlags=%#x\n", hType, pTypeR3->fKeepPgmLock, fFlags),
                    VERR_INVALID_HANDLE);
    AssertMsgReturn(pTypeR3->fRing0DevInsIdx == RT_BOOL(fFlags & PGMPHYSHANDLER_F_R0_DEVINS_IDX),
                    ("%#x: %d, fFlags=%#x\n", hType, pTypeR3->fRing0DevInsIdx, fFlags),
                    VERR_INVALID_HANDLE);
    AssertMsgReturn(pTypeR3->fNotInHm == RT_BOOL(fFlags & PGMPHYSHANDLER_F_NOT_IN_HM),
                    ("%#x: %d, fFlags=%#x\n", hType, pTypeR3->fNotInHm, fFlags),
                    VERR_INVALID_HANDLE);

    /*
     * Update the entry.
     */
    pTypeR0->enmKind          = enmKind;
    pTypeR0->uState           = enmKind == PGMPHYSHANDLERKIND_WRITE
                              ? PGM_PAGE_HNDL_PHYS_STATE_WRITE : PGM_PAGE_HNDL_PHYS_STATE_ALL;
    pTypeR0->fKeepPgmLock     = RT_BOOL(fFlags & PGMPHYSHANDLER_F_KEEP_PGM_LOCK);
    pTypeR0->fRing0DevInsIdx  = RT_BOOL(fFlags & PGMPHYSHANDLER_F_R0_DEVINS_IDX);
    pTypeR0->fNotInHm         = RT_BOOL(fFlags & PGMPHYSHANDLER_F_NOT_IN_HM);
    pTypeR0->pfnHandler       = pfnHandler;
    pTypeR0->pfnPfHandler     = pfnPfHandler;
    pTypeR0->pszDesc          = pszDesc;

    pTypeR3->fRing0Enabled    = true;

    LogFlow(("PGMR0HandlerPhysicalTypeRegister: hType=%#x: enmKind=%d fFlags=%#x pfnHandler=%p pfnPfHandler=%p pszDesc=%s\n",
             hType, enmKind, fFlags, pfnHandler, pfnPfHandler, pszDesc));
    return VINF_SUCCESS;
}


#ifdef VBOX_WITH_PCI_PASSTHROUGH
/* Interface sketch.  The interface belongs to a global PCI pass-through
   manager.  It shall use the global VM handle, not the user VM handle to
   store the per-VM info (domain) since that is all ring-0 stuff, thus
   passing pGVM here.  I've tentitively prefixed the functions 'GPciRawR0',
   we can discuss the PciRaw code re-organtization when I'm back from
   vacation.

   I've implemented the initial IOMMU set up below.  For things to work
   reliably, we will probably need add a whole bunch of checks and
   GPciRawR0GuestPageUpdate call to the PGM code.  For the present,
   assuming nested paging (enforced) and prealloc (enforced), no
   ballooning (check missing), page sharing (check missing) or live
   migration (check missing), it might work fine.  At least if some
   VM power-off hook is present and can tear down the IOMMU page tables. */

/**
 * Tells the global PCI pass-through manager that we are about to set up the
 * guest page to host page mappings for the specfied VM.
 *
 * @returns VBox status code.
 *
 * @param   pGVM                The ring-0 VM structure.
 */
VMMR0_INT_DECL(int) GPciRawR0GuestPageBeginAssignments(PGVM pGVM)
{
    NOREF(pGVM);
    return VINF_SUCCESS;
}


/**
 * Assigns a host page mapping for a guest page.
 *
 * This is only used when setting up the mappings, i.e. between
 * GPciRawR0GuestPageBeginAssignments and GPciRawR0GuestPageEndAssignments.
 *
 * @returns VBox status code.
 * @param   pGVM                The ring-0 VM structure.
 * @param   GCPhys              The address of the guest page (page aligned).
 * @param   HCPhys              The address of the host page (page aligned).
 */
VMMR0_INT_DECL(int) GPciRawR0GuestPageAssign(PGVM pGVM, RTGCPHYS GCPhys, RTHCPHYS HCPhys)
{
    AssertReturn(!(GCPhys & HOST_PAGE_OFFSET_MASK), VERR_INTERNAL_ERROR_3);
    AssertReturn(!(HCPhys & HOST_PAGE_OFFSET_MASK), VERR_INTERNAL_ERROR_3);

    if (pGVM->rawpci.s.pfnContigMemInfo)
        /** @todo what do we do on failure? */
        pGVM->rawpci.s.pfnContigMemInfo(&pGVM->rawpci.s, HCPhys, GCPhys, HOST_PAGE_SIZE, PCIRAW_MEMINFO_MAP);

    return VINF_SUCCESS;
}


/**
 * Indicates that the specified guest page doesn't exists but doesn't have host
 * page mapping we trust PCI pass-through with.
 *
 * This is only used when setting up the mappings, i.e. between
 * GPciRawR0GuestPageBeginAssignments and GPciRawR0GuestPageEndAssignments.
 *
 * @returns VBox status code.
 * @param   pGVM                The ring-0 VM structure.
 * @param   GCPhys              The address of the guest page (page aligned).
 * @param   HCPhys              The address of the host page (page aligned).
 */
VMMR0_INT_DECL(int) GPciRawR0GuestPageUnassign(PGVM pGVM, RTGCPHYS GCPhys)
{
    AssertReturn(!(GCPhys & HOST_PAGE_OFFSET_MASK), VERR_INTERNAL_ERROR_3);

    if (pGVM->rawpci.s.pfnContigMemInfo)
        /** @todo what do we do on failure? */
        pGVM->rawpci.s.pfnContigMemInfo(&pGVM->rawpci.s, 0, GCPhys, HOST_PAGE_SIZE, PCIRAW_MEMINFO_UNMAP);

    return VINF_SUCCESS;
}


/**
 * Tells the global PCI pass-through manager that we have completed setting up
 * the guest page to host page mappings for the specfied VM.
 *
 * This complements GPciRawR0GuestPageBeginAssignments and will be called even
 * if some page assignment failed.
 *
 * @returns VBox status code.
 *
 * @param   pGVM                The ring-0 VM structure.
 */
VMMR0_INT_DECL(int) GPciRawR0GuestPageEndAssignments(PGVM pGVM)
{
    NOREF(pGVM);
    return VINF_SUCCESS;
}


/**
 * Tells the global PCI pass-through manager that a guest page mapping has
 * changed after the initial setup.
 *
 * @returns VBox status code.
 * @param   pGVM                The ring-0 VM structure.
 * @param   GCPhys              The address of the guest page (page aligned).
 * @param   HCPhys              The new host page address or NIL_RTHCPHYS if
 *                              now unassigned.
 */
VMMR0_INT_DECL(int) GPciRawR0GuestPageUpdate(PGVM pGVM, RTGCPHYS GCPhys, RTHCPHYS HCPhys)
{
    AssertReturn(!(GCPhys & HOST_PAGE_OFFSET_MASK), VERR_INTERNAL_ERROR_4);
    AssertReturn(!(HCPhys & HOST_PAGE_OFFSET_MASK) || HCPhys == NIL_RTHCPHYS, VERR_INTERNAL_ERROR_4);
    NOREF(pGVM);
    return VINF_SUCCESS;
}

#endif /* VBOX_WITH_PCI_PASSTHROUGH */


/**
 * Sets up the IOMMU when raw PCI device is enabled.
 *
 * @note    This is a hack that will probably be remodelled and refined later!
 *
 * @returns VBox status code.
 *
 * @param   pGVM                The global (ring-0) VM structure.
 */
VMMR0_INT_DECL(int) PGMR0PhysSetupIoMmu(PGVM pGVM)
{
    int rc = GVMMR0ValidateGVM(pGVM);
    if (RT_FAILURE(rc))
        return rc;

#ifdef VBOX_WITH_PCI_PASSTHROUGH
# error fixme
    if (pGVM->pgm.s.fPciPassthrough)
    {
        /*
         * The Simplistic Approach - Enumerate all the pages and call tell the
         * IOMMU about each of them.
         */
        PGM_LOCK_VOID(pGVM);
        rc = GPciRawR0GuestPageBeginAssignments(pGVM);
        if (RT_SUCCESS(rc))
        {
            for (PPGMRAMRANGE pRam = pGVM->pgm.s.pRamRangesXR0; RT_SUCCESS(rc) && pRam; pRam = pRam->pNextR0)
            {
                PPGMPAGE    pPage  = &pRam->aPages[0];
                RTGCPHYS    GCPhys = pRam->GCPhys;
                uint32_t    cLeft  = pRam->cb >> GUEST_PAGE_SHIFT;
                while (cLeft-- > 0)
                {
                    /* Only expose pages that are 100% safe for now. */
                    if (   PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_RAM
                        && PGM_PAGE_GET_STATE(pPage) == PGM_PAGE_STATE_ALLOCATED
                        && !PGM_PAGE_HAS_ANY_HANDLERS(pPage))
                        rc = GPciRawR0GuestPageAssign(pGVM, GCPhys, PGM_PAGE_GET_HCPHYS(pPage));
                    else
                        rc = GPciRawR0GuestPageUnassign(pGVM, GCPhys);

                    /* next */
                    pPage++;
                    GCPhys += HOST_PAGE_SIZE;
                }
            }

            int rc2 = GPciRawR0GuestPageEndAssignments(pGVM);
            if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
                rc = rc2;
        }
        PGM_UNLOCK(pGVM);
    }
    else
#endif
        rc = VERR_NOT_SUPPORTED;
    return rc;
}


/**
 * \#PF Handler for nested paging.
 *
 * @returns VBox status code (appropriate for trap handling and GC return).
 * @param   pGVM                The global (ring-0) VM structure.
 * @param   pGVCpu              The global (ring-0) CPU structure of the calling
 *                              EMT.
 * @param   enmShwPagingMode    Paging mode for the nested page tables.
 * @param   uErr                The trap error code.
 * @param   pCtx                Pointer to the register context for the CPU.
 * @param   GCPhysFault         The fault address.
 */
VMMR0DECL(int) PGMR0Trap0eHandlerNestedPaging(PGVM pGVM, PGVMCPU pGVCpu, PGMMODE enmShwPagingMode, RTGCUINT uErr,
                                              PCPUMCTX pCtx, RTGCPHYS GCPhysFault)
{
    int rc;

    LogFlow(("PGMTrap0eHandler: uErr=%RGx GCPhysFault=%RGp eip=%RGv\n", uErr, GCPhysFault, (RTGCPTR)pCtx->rip));
    STAM_PROFILE_START(&pGVCpu->pgm.s.StatRZTrap0e, a);
    STAM_STATS({ pGVCpu->pgmr0.s.pStatTrap0eAttributionR0 = NULL; } );

    /* AMD uses the host's paging mode; Intel has a single mode (EPT). */
    AssertMsg(   enmShwPagingMode == PGMMODE_32_BIT || enmShwPagingMode == PGMMODE_PAE      || enmShwPagingMode == PGMMODE_PAE_NX
              || enmShwPagingMode == PGMMODE_AMD64  || enmShwPagingMode == PGMMODE_AMD64_NX || enmShwPagingMode == PGMMODE_EPT,
              ("enmShwPagingMode=%d\n", enmShwPagingMode));

    /* Reserved shouldn't end up here. */
    Assert(!(uErr & X86_TRAP_PF_RSVD));

#ifdef VBOX_WITH_STATISTICS
    /*
     * Error code stats.
     */
    if (uErr & X86_TRAP_PF_US)
    {
        if (!(uErr & X86_TRAP_PF_P))
        {
            if (uErr & X86_TRAP_PF_RW)
                STAM_COUNTER_INC(&pGVCpu->pgm.s.Stats.StatRZTrap0eUSNotPresentWrite);
            else
                STAM_COUNTER_INC(&pGVCpu->pgm.s.Stats.StatRZTrap0eUSNotPresentRead);
        }
        else if (uErr & X86_TRAP_PF_RW)
            STAM_COUNTER_INC(&pGVCpu->pgm.s.Stats.StatRZTrap0eUSWrite);
        else if (uErr & X86_TRAP_PF_RSVD)
            STAM_COUNTER_INC(&pGVCpu->pgm.s.Stats.StatRZTrap0eUSReserved);
        else if (uErr & X86_TRAP_PF_ID)
            STAM_COUNTER_INC(&pGVCpu->pgm.s.Stats.StatRZTrap0eUSNXE);
        else
            STAM_COUNTER_INC(&pGVCpu->pgm.s.Stats.StatRZTrap0eUSRead);
    }
    else
    {   /* Supervisor */
        if (!(uErr & X86_TRAP_PF_P))
        {
            if (uErr & X86_TRAP_PF_RW)
                STAM_COUNTER_INC(&pGVCpu->pgm.s.Stats.StatRZTrap0eSVNotPresentWrite);
            else
                STAM_COUNTER_INC(&pGVCpu->pgm.s.Stats.StatRZTrap0eSVNotPresentRead);
        }
        else if (uErr & X86_TRAP_PF_RW)
            STAM_COUNTER_INC(&pGVCpu->pgm.s.Stats.StatRZTrap0eSVWrite);
        else if (uErr & X86_TRAP_PF_ID)
            STAM_COUNTER_INC(&pGVCpu->pgm.s.Stats.StatRZTrap0eSNXE);
        else if (uErr & X86_TRAP_PF_RSVD)
            STAM_COUNTER_INC(&pGVCpu->pgm.s.Stats.StatRZTrap0eSVReserved);
    }
#endif

    /*
     * Call the worker.
     *
     * Note! We pretend the guest is in protected mode without paging, so we
     *       can use existing code to build the nested page tables.
     */
/** @todo r=bird: Gotta love this nested paging hacking we're still carrying with us... (Split PGM_TYPE_NESTED.) */
    bool fLockTaken = false;
    switch (enmShwPagingMode)
    {
        case PGMMODE_32_BIT:
            rc = PGM_BTH_NAME_32BIT_PROT(Trap0eHandler)(pGVCpu, uErr, pCtx, GCPhysFault, &fLockTaken);
            break;
        case PGMMODE_PAE:
        case PGMMODE_PAE_NX:
            rc = PGM_BTH_NAME_PAE_PROT(Trap0eHandler)(pGVCpu, uErr, pCtx, GCPhysFault, &fLockTaken);
            break;
        case PGMMODE_AMD64:
        case PGMMODE_AMD64_NX:
            rc = PGM_BTH_NAME_AMD64_PROT(Trap0eHandler)(pGVCpu, uErr, pCtx, GCPhysFault, &fLockTaken);
            break;
        case PGMMODE_EPT:
            rc = PGM_BTH_NAME_EPT_PROT(Trap0eHandler)(pGVCpu, uErr, pCtx, GCPhysFault, &fLockTaken);
            break;
        default:
            AssertFailed();
            rc = VERR_INVALID_PARAMETER;
            break;
    }
    if (fLockTaken)
    {
        PGM_LOCK_ASSERT_OWNER(pGVM);
        PGM_UNLOCK(pGVM);
    }

    if (rc == VINF_PGM_SYNCPAGE_MODIFIED_PDE)
        rc = VINF_SUCCESS;
    /*
     * Handle the case where we cannot interpret the instruction because we cannot get the guest physical address
     * via its page tables, see @bugref{6043}.
     */
    else if (   rc == VERR_PAGE_NOT_PRESENT                 /* SMP only ; disassembly might fail. */
             || rc == VERR_PAGE_TABLE_NOT_PRESENT           /* seen with UNI & SMP */
             || rc == VERR_PAGE_DIRECTORY_PTR_NOT_PRESENT   /* seen with SMP */
             || rc == VERR_PAGE_MAP_LEVEL4_NOT_PRESENT)     /* precaution */
    {
        Log(("WARNING: Unexpected VERR_PAGE_TABLE_NOT_PRESENT (%d) for page fault at %RGp error code %x (rip=%RGv)\n", rc, GCPhysFault, uErr, pCtx->rip));
        /* Some kind of inconsistency in the SMP case; it's safe to just execute the instruction again; not sure about
           single VCPU VMs though. */
        rc = VINF_SUCCESS;
    }

    STAM_STATS({ if (!pGVCpu->pgmr0.s.pStatTrap0eAttributionR0)
                    pGVCpu->pgmr0.s.pStatTrap0eAttributionR0 = &pGVCpu->pgm.s.Stats.StatRZTrap0eTime2Misc; });
    STAM_PROFILE_STOP_EX(&pGVCpu->pgm.s.Stats.StatRZTrap0e, pGVCpu->pgmr0.s.pStatTrap0eAttributionR0, a);
    return rc;
}


#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
/**
 * Nested \#PF Handler for nested-guest execution using nested paging.
 *
 * @returns Strict VBox status code (appropriate for trap handling and GC return).
 * @param   pGVM                The global (ring-0) VM structure.
 * @param   pGVCpu              The global (ring-0) CPU structure of the calling
 *                              EMT.
 * @param   uErr                The trap error code.
 * @param   pCtx                Pointer to the register context for the CPU.
 * @param   GCPhysNestedFault   The nested-guest physical address causing the fault.
 * @param   fIsLinearAddrValid  Whether translation of a nested-guest linear address
 *                              caused this fault. If @c false, GCPtrNestedFault
 *                              must be 0.
 * @param   GCPtrNestedFault    The nested-guest linear address that caused this
 *                              fault.
 * @param   pWalk               Where to store the SLAT walk result.
 */
VMMR0DECL(VBOXSTRICTRC) PGMR0NestedTrap0eHandlerNestedPaging(PGVMCPU pGVCpu, PGMMODE enmShwPagingMode, RTGCUINT uErr,
                                                             PCPUMCTX pCtx, RTGCPHYS GCPhysNestedFault,
                                                             bool fIsLinearAddrValid, RTGCPTR GCPtrNestedFault, PPGMPTWALK pWalk)
{
    Assert(enmShwPagingMode == PGMMODE_EPT);
    NOREF(enmShwPagingMode);

    bool fLockTaken;
    VBOXSTRICTRC rcStrict = PGM_BTH_NAME_EPT_PROT(NestedTrap0eHandler)(pGVCpu, uErr, pCtx, GCPhysNestedFault,
                                                                       fIsLinearAddrValid, GCPtrNestedFault, pWalk, &fLockTaken);
    if (fLockTaken)
    {
        PGM_LOCK_ASSERT_OWNER(pGVCpu->CTX_SUFF(pVM));
        PGM_UNLOCK(pGVCpu->CTX_SUFF(pVM));
    }
    Assert(rcStrict != VINF_PGM_SYNCPAGE_MODIFIED_PDE); /* This rc isn't used with Nested Paging and nested-EPT. */
    return rcStrict;
}
#endif /* VBOX_WITH_NESTED_HWVIRT_VMX_EPT */


/**
 * \#PF Handler for deliberate nested paging misconfiguration (/reserved bit)
 * employed for MMIO pages.
 *
 * @returns VBox status code (appropriate for trap handling and GC return).
 * @param   pGVM                The global (ring-0) VM structure.
 * @param   pGVCpu              The global (ring-0) CPU structure of the calling
 *                              EMT.
 * @param   enmShwPagingMode    Paging mode for the nested page tables.
 * @param   pCtx                Pointer to the register context for the CPU.
 * @param   GCPhysFault         The fault address.
 * @param   uErr                The error code, UINT32_MAX if not available
 *                              (VT-x).
 */
VMMR0DECL(VBOXSTRICTRC) PGMR0Trap0eHandlerNPMisconfig(PGVM pGVM, PGVMCPU pGVCpu, PGMMODE enmShwPagingMode,
                                                      PCPUMCTX pCtx, RTGCPHYS GCPhysFault, uint32_t uErr)
{
#ifdef PGM_WITH_MMIO_OPTIMIZATIONS
    STAM_PROFILE_START(&pGVCpu->CTX_SUFF(pStats)->StatR0NpMiscfg, a);
    VBOXSTRICTRC rc;

    /*
     * Try lookup the all access physical handler for the address.
     */
    PGM_LOCK_VOID(pGVM);
    PPGMPHYSHANDLER pHandler;
    rc = pgmHandlerPhysicalLookup(pGVM, GCPhysFault, &pHandler);
    if (RT_SUCCESS(rc))
    {
        PCPGMPHYSHANDLERTYPEINT pHandlerType = PGMPHYSHANDLER_GET_TYPE_NO_NULL(pGVM, pHandler);
        if (RT_LIKELY(   pHandlerType->enmKind != PGMPHYSHANDLERKIND_WRITE
                      && !pHandlerType->fNotInHm /*paranoia*/ ))
        {
            /*
             * If the handle has aliases page or pages that have been temporarily
             * disabled, we'll have to take a detour to make sure we resync them
             * to avoid lots of unnecessary exits.
             */
            PPGMPAGE pPage;
            if (   (   pHandler->cAliasedPages
                    || pHandler->cTmpOffPages)
                && (   (pPage = pgmPhysGetPage(pGVM, GCPhysFault)) == NULL
                    || PGM_PAGE_GET_HNDL_PHYS_STATE(pPage) == PGM_PAGE_HNDL_PHYS_STATE_DISABLED)
               )
            {
                Log(("PGMR0Trap0eHandlerNPMisconfig: Resyncing aliases / tmp-off page at %RGp (uErr=%#x) %R[pgmpage]\n", GCPhysFault, uErr, pPage));
                STAM_COUNTER_INC(&pGVCpu->pgm.s.Stats.StatR0NpMiscfgSyncPage);
                rc = pgmShwSyncNestedPageLocked(pGVCpu, GCPhysFault, 1 /*cPages*/, enmShwPagingMode);
                PGM_UNLOCK(pGVM);
            }
            else
            {
                if (pHandlerType->pfnPfHandler)
                {
                    uint64_t const uUser = !pHandlerType->fRing0DevInsIdx ? pHandler->uUser
                                         : (uintptr_t)PDMDeviceRing0IdxToInstance(pGVM, pHandler->uUser);
                    STAM_PROFILE_START(&pHandler->Stat, h);
                    PGM_UNLOCK(pGVM);

                    Log6(("PGMR0Trap0eHandlerNPMisconfig: calling %p(,%#x,,%RGp,%p)\n", pHandlerType->pfnPfHandler, uErr, GCPhysFault, uUser));
                    rc = pHandlerType->pfnPfHandler(pGVM, pGVCpu, uErr == UINT32_MAX ? RTGCPTR_MAX : uErr, pCtx,
                                                    GCPhysFault, GCPhysFault, uUser);

                    STAM_PROFILE_STOP(&pHandler->Stat, h); /* no locking needed, entry is unlikely reused before we get here. */
                }
                else
                {
                    PGM_UNLOCK(pGVM);
                    Log(("PGMR0Trap0eHandlerNPMisconfig: %RGp (uErr=%#x) -> R3\n", GCPhysFault, uErr));
                    rc = VINF_EM_RAW_EMULATE_INSTR;
                }
            }
            STAM_PROFILE_STOP(&pGVCpu->pgm.s.Stats.StatR0NpMiscfg, a);
            return rc;
        }
    }
    else
        AssertMsgReturn(rc == VERR_NOT_FOUND, ("%Rrc GCPhysFault=%RGp\n", VBOXSTRICTRC_VAL(rc), GCPhysFault), rc);

    /*
     * Must be out of sync, so do a SyncPage and restart the instruction.
     *
     * ASSUMES that ALL handlers are page aligned and covers whole pages
     * (assumption asserted in PGMHandlerPhysicalRegisterEx).
     */
    Log(("PGMR0Trap0eHandlerNPMisconfig: Out of sync page at %RGp (uErr=%#x)\n", GCPhysFault, uErr));
    STAM_COUNTER_INC(&pGVCpu->pgm.s.Stats.StatR0NpMiscfgSyncPage);
    rc = pgmShwSyncNestedPageLocked(pGVCpu, GCPhysFault, 1 /*cPages*/, enmShwPagingMode);
    PGM_UNLOCK(pGVM);

    STAM_PROFILE_STOP(&pGVCpu->pgm.s.Stats.StatR0NpMiscfg, a);
    return rc;

#else
    AssertLogRelFailed();
    return VERR_PGM_NOT_USED_IN_MODE;
#endif
}

