/* $Id: PGMR0Pool.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PGM Shadow Page Pool, ring-0 specific bits.
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
#define LOG_GROUP LOG_GROUP_PGM_POOL
#define VBOX_WITHOUT_PAGING_BIT_FIELDS /* 64-bit bitfields are just asking for trouble. See @bugref{9841} and others. */
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/hm.h>
#include "PGMInternal.h"
#include <VBox/vmm/vmcc.h>
#include "PGMInline.h"

#include <VBox/log.h>
#include <VBox/err.h>
#include <iprt/mem.h>
#include <iprt/memobj.h>


/**
 * Called by PGMR0InitVM to complete the page pool setup for ring-0.
 *
 * @returns VBox status code.
 * @param   pGVM    Pointer to the global VM structure.
 */
int pgmR0PoolInitVM(PGVM pGVM)
{
    PPGMPOOL pPool = pGVM->pgm.s.pPoolR0;
    AssertPtrReturn(pPool, VERR_PGM_POOL_IPE);

    int rc = PGMR0HandlerPhysicalTypeSetUpContext(pGVM, PGMPHYSHANDLERKIND_WRITE, PGMPHYSHANDLER_F_KEEP_PGM_LOCK,
                                                  pgmPoolAccessHandler, pgmRZPoolAccessPfHandler,
                                                  "Guest Paging Access Handler", pPool->hAccessHandlerType);
    AssertLogRelRCReturn(rc, rc);

    return VINF_SUCCESS;
}


/**
 * Worker for PGMR0PoolGrow.
 */
static int pgmR0PoolGrowInner(PGVM pGVM, PPGMPOOL pPool)
{
    int rc;

    /* With 32-bit guests and no EPT, the CR3 limits the root pages to low
       (below 4 GB) memory. */
    /** @todo change the pool to handle ROOT page allocations specially when
     *        required. */
    bool const fCanUseHighMemory = HMIsNestedPagingActive(pGVM);

    /*
     * Figure out how many pages should allocate.
     */
    uint32_t const cMaxPages = RT_MIN(pPool->cMaxPages, PGMPOOL_IDX_LAST);
    uint32_t const cCurPages = RT_MIN(pPool->cCurPages, cMaxPages);
    if (cCurPages < cMaxPages)
    {
        uint32_t cNewPages = cMaxPages - cCurPages;
        if (cNewPages > PGMPOOL_CFG_MAX_GROW)
            cNewPages = PGMPOOL_CFG_MAX_GROW;
        LogFlow(("PGMR0PoolGrow: Growing the pool by %u (%#x) pages to %u (%#x) pages. fCanUseHighMemory=%RTbool\n",
                 cNewPages, cNewPages, cCurPages + cNewPages, cCurPages + cNewPages, fCanUseHighMemory));

        /* Check that the handles in the arrays entry are both NIL. */
        uintptr_t const idxMemHandle = cCurPages / (PGMPOOL_CFG_MAX_GROW);
        AssertCompile(   (PGMPOOL_IDX_LAST + (PGMPOOL_CFG_MAX_GROW - 1)) / PGMPOOL_CFG_MAX_GROW
                      <= RT_ELEMENTS(pGVM->pgmr0.s.ahPoolMemObjs));
        AssertCompile(RT_ELEMENTS(pGVM->pgmr0.s.ahPoolMemObjs) == RT_ELEMENTS(pGVM->pgmr0.s.ahPoolMapObjs));
        AssertLogRelMsgReturn(   pGVM->pgmr0.s.ahPoolMemObjs[idxMemHandle] == NIL_RTR0MEMOBJ
                              && pGVM->pgmr0.s.ahPoolMapObjs[idxMemHandle] == NIL_RTR0MEMOBJ, ("idxMemHandle=%#x\n", idxMemHandle),
                              VERR_PGM_POOL_IPE);

        /*
         * Allocate the new pages and map them into ring-3.
         */
        RTR0MEMOBJ hMemObj = NIL_RTR0MEMOBJ;
        if (fCanUseHighMemory)
            rc = RTR0MemObjAllocPage(&hMemObj, cNewPages * HOST_PAGE_SIZE, false /*fExecutable*/);
        else
            rc = RTR0MemObjAllocLow(&hMemObj, cNewPages * HOST_PAGE_SIZE, false /*fExecutable*/);
        if (RT_SUCCESS(rc))
        {
            RTR0MEMOBJ hMapObj = NIL_RTR0MEMOBJ;
            rc = RTR0MemObjMapUser(&hMapObj, hMemObj, (RTR3PTR)-1, 0, RTMEM_PROT_READ | RTMEM_PROT_WRITE, NIL_RTR0PROCESS);
            if (RT_SUCCESS(rc))
            {
                pGVM->pgmr0.s.ahPoolMemObjs[idxMemHandle] = hMemObj;
                pGVM->pgmr0.s.ahPoolMapObjs[idxMemHandle] = hMapObj;

                uint8_t *pbRing0 = (uint8_t *)RTR0MemObjAddress(hMemObj);
                RTR3PTR  pbRing3 = RTR0MemObjAddressR3(hMapObj);
                AssertPtr(pbRing0);
                Assert(((uintptr_t)pbRing0 & HOST_PAGE_OFFSET_MASK) == 0);
                Assert(pbRing3 != NIL_RTR3PTR);
                Assert((pbRing3 & HOST_PAGE_OFFSET_MASK) == 0);

                /*
                 * Initialize the new pages.
                 */
                for (unsigned iNewPage = 0; iNewPage < cNewPages; iNewPage++)
                {
                    PPGMPOOLPAGE pPage = &pPool->aPages[cCurPages + iNewPage];
                    pPage->pvPageR0         = &pbRing0[iNewPage * HOST_PAGE_SIZE];
                    pPage->pvPageR3         = pbRing3 + iNewPage * HOST_PAGE_SIZE;
                    pPage->Core.Key         = RTR0MemObjGetPagePhysAddr(hMemObj, iNewPage);
                    AssertFatal(pPage->Core.Key < _4G || fCanUseHighMemory);
                    pPage->GCPhys           = NIL_RTGCPHYS;
                    pPage->enmKind          = PGMPOOLKIND_FREE;
                    pPage->idx              = pPage - &pPool->aPages[0];
                    LogFlow(("PGMR0PoolGrow: insert page #%#x - %RHp\n", pPage->idx, pPage->Core.Key));
                    pPage->iNext            = pPool->iFreeHead;
                    pPage->iUserHead        = NIL_PGMPOOL_USER_INDEX;
                    pPage->iModifiedNext    = NIL_PGMPOOL_IDX;
                    pPage->iModifiedPrev    = NIL_PGMPOOL_IDX;
                    pPage->iMonitoredNext   = NIL_PGMPOOL_IDX;
                    pPage->iMonitoredPrev   = NIL_PGMPOOL_IDX;
                    pPage->iAgeNext         = NIL_PGMPOOL_IDX;
                    pPage->iAgePrev         = NIL_PGMPOOL_IDX;
                    /* commit it */
                    bool fRc = RTAvloHCPhysInsert(&pPool->HCPhysTree, &pPage->Core); Assert(fRc); NOREF(fRc);
                    pPool->iFreeHead = cCurPages + iNewPage;
                    pPool->cCurPages = cCurPages + iNewPage + 1;
                }

                return VINF_SUCCESS;
            }

            RTR0MemObjFree(hMemObj, true /*fFreeMappings*/);
        }
        if (cCurPages > 64)
            LogRelMax(5, ("PGMR0PoolGrow: rc=%Rrc cNewPages=%#x cCurPages=%#x cMaxPages=%#x fCanUseHighMemory=%d\n",
                          rc, cNewPages, cCurPages, cMaxPages, fCanUseHighMemory));
        else
            LogRel(("PGMR0PoolGrow: rc=%Rrc cNewPages=%#x cCurPages=%#x cMaxPages=%#x fCanUseHighMemory=%d\n",
                    rc, cNewPages, cCurPages, cMaxPages, fCanUseHighMemory));
    }
    else
        rc = VINF_SUCCESS;
    return rc;
}


/**
 * Grows the shadow page pool.
 *
 * I.e. adds more pages to it, assuming that hasn't reached cMaxPages yet.
 *
 * @returns VBox status code.
 * @param   pGVM    The ring-0 VM structure.
 * @param   idCpu   The ID of the calling EMT.
 * @thread  EMT(idCpu)
 */
VMMR0_INT_DECL(int) PGMR0PoolGrow(PGVM pGVM, VMCPUID idCpu)
{
    /*
     * Validate input.
     */
    PPGMPOOL pPool = pGVM->pgm.s.pPoolR0;
    AssertReturn(pPool->cCurPages < pPool->cMaxPages, VERR_PGM_POOL_MAXED_OUT_ALREADY);
    AssertReturn(pPool->pVMR3 == pGVM->pVMR3, VERR_PGM_POOL_IPE);
    AssertReturn(pPool->pVMR0 == pGVM, VERR_PGM_POOL_IPE);

    AssertReturn(idCpu < pGVM->cCpus, VERR_VM_THREAD_NOT_EMT);
    PGVMCPU const pGVCpu = &pGVM->aCpus[idCpu];

    /*
     * Enter the grow critical section and call worker.
     */
    STAM_REL_PROFILE_START(&pPool->StatGrow, a);

    VMMR0EMTBLOCKCTX Ctx;
    int rc = VMMR0EmtPrepareToBlock(pGVCpu, VINF_SUCCESS, __FUNCTION__, &pGVM->pgmr0.s.PoolGrowCritSect, &Ctx);
    AssertRCReturn(rc, rc);

    rc = RTCritSectEnter(&pGVM->pgmr0.s.PoolGrowCritSect);
    AssertRCReturn(rc, rc);

    rc = pgmR0PoolGrowInner(pGVM, pPool);

    STAM_REL_PROFILE_STOP(&pPool->StatGrow, a);
    RTCritSectLeave(&pGVM->pgmr0.s.PoolGrowCritSect);

    VMMR0EmtResumeAfterBlocking(pGVCpu, &Ctx);
    return rc;
}

