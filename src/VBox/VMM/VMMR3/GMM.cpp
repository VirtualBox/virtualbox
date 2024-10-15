/* $Id: GMM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * GMM - Global Memory Manager, ring-3 request wrappers.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_GMM
#include <VBox/vmm/gmm.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/vmcc.h>
#include <VBox/sup.h>
#include <VBox/err.h>
#include <VBox/param.h>

#include <iprt/assert.h>
#include <VBox/log.h>
#include <iprt/mem.h>
#include <iprt/string.h>


/**
 * @see GMMR0InitialReservation
 */
GMMR3DECL(int)  GMMR3InitialReservation(PVM pVM, uint64_t cBasePages, uint32_t cShadowPages, uint32_t cFixedPages,
                                        GMMOCPOLICY enmPolicy, GMMPRIORITY enmPriority)
{
    if (!SUPR3IsDriverless())
    {
        GMMINITIALRESERVATIONREQ Req;
        Req.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
        Req.Hdr.cbReq = sizeof(Req);
        Req.cBasePages = cBasePages;
        Req.cShadowPages = cShadowPages;
        Req.cFixedPages = cFixedPages;
        Req.enmPolicy = enmPolicy;
        Req.enmPriority = enmPriority;
        return VMMR3CallR0(pVM, VMMR0_DO_GMM_INITIAL_RESERVATION, 0, &Req.Hdr);
    }
    return VINF_SUCCESS;
}


/**
 * @see GMMR0UpdateReservation
 */
GMMR3DECL(int)  GMMR3UpdateReservation(PVM pVM, uint64_t cBasePages, uint32_t cShadowPages, uint32_t cFixedPages)
{
    if (!SUPR3IsDriverless())
    {
        GMMUPDATERESERVATIONREQ Req;
        Req.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
        Req.Hdr.cbReq = sizeof(Req);
        Req.cBasePages = cBasePages;
        Req.cShadowPages = cShadowPages;
        Req.cFixedPages = cFixedPages;
        return VMMR3CallR0(pVM, VMMR0_DO_GMM_UPDATE_RESERVATION, 0, &Req.Hdr);
    }
    return VINF_SUCCESS;
}


/**
 * Prepares a GMMR0AllocatePages request.
 *
 * @returns VINF_SUCCESS or VERR_NO_TMP_MEMORY.
 * @param       pVM         The cross context VM structure.
 * @param[out]  ppReq       Where to store the pointer to the request packet.
 * @param       cPages      The number of pages that's to be allocated.
 * @param       enmAccount  The account to charge.
 */
GMMR3DECL(int) GMMR3AllocatePagesPrepare(PVM pVM, PGMMALLOCATEPAGESREQ *ppReq, uint32_t cPages, GMMACCOUNT enmAccount)
{
    uint32_t cb = RT_UOFFSETOF_DYN(GMMALLOCATEPAGESREQ, aPages[cPages]);
    PGMMALLOCATEPAGESREQ pReq = (PGMMALLOCATEPAGESREQ)RTMemTmpAllocZ(cb);
    if (!pReq)
        return VERR_NO_TMP_MEMORY;

    pReq->Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    pReq->Hdr.cbReq = cb;
    pReq->enmAccount = enmAccount;
    pReq->cPages = cPages;
    NOREF(pVM);
    *ppReq = pReq;
    return VINF_SUCCESS;
}


/**
 * Performs a GMMR0AllocatePages request.
 *
 * This will call VMSetError on failure.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pReq        Pointer to the request (returned by GMMR3AllocatePagesPrepare).
 */
GMMR3DECL(int) GMMR3AllocatePagesPerform(PVM pVM, PGMMALLOCATEPAGESREQ pReq)
{
    int rc = VMMR3CallR0(pVM, VMMR0_DO_GMM_ALLOCATE_PAGES, 0, &pReq->Hdr);
    if (RT_SUCCESS(rc))
    {
#ifdef LOG_ENABLED
        for (uint32_t iPage = 0; iPage < pReq->cPages; iPage++)
            Log3(("GMMR3AllocatePagesPerform: idPage=%#x HCPhys=%RHp fZeroed=%d\n",
                  pReq->aPages[iPage].idPage, pReq->aPages[iPage].HCPhysGCPhys, pReq->aPages[iPage].fZeroed));
#endif
        return rc;
    }
    return VMSetError(pVM, rc, RT_SRC_POS, N_("GMMR0AllocatePages failed to allocate %u pages"), pReq->cPages);
}


/**
 * Cleans up a GMMR0AllocatePages request.
 * @param   pReq        Pointer to the request (returned by GMMR3AllocatePagesPrepare).
 */
GMMR3DECL(void) GMMR3AllocatePagesCleanup(PGMMALLOCATEPAGESREQ pReq)
{
    RTMemTmpFree(pReq);
}


/**
 * Prepares a GMMR0FreePages request.
 *
 * @returns VINF_SUCCESS or VERR_NO_TMP_MEMORY.
 * @param       pVM         The cross context VM structure.
 * @param[out]  ppReq       Where to store the pointer to the request packet.
 * @param       cPages      The number of pages that's to be freed.
 * @param       enmAccount  The account to charge.
 */
GMMR3DECL(int) GMMR3FreePagesPrepare(PVM pVM, PGMMFREEPAGESREQ *ppReq, uint32_t cPages, GMMACCOUNT enmAccount)
{
    uint32_t cb = RT_UOFFSETOF_DYN(GMMFREEPAGESREQ, aPages[cPages]);
    PGMMFREEPAGESREQ pReq = (PGMMFREEPAGESREQ)RTMemTmpAllocZ(cb);
    if (!pReq)
        return VERR_NO_TMP_MEMORY;

    pReq->Hdr.u32Magic  = SUPVMMR0REQHDR_MAGIC;
    pReq->Hdr.cbReq     = cb;
    pReq->enmAccount    = enmAccount;
    pReq->cPages        = cPages;
    NOREF(pVM);
    *ppReq = pReq;
    return VINF_SUCCESS;
}


/**
 * Re-prepares a GMMR0FreePages request.
 *
 * @param       pVM         The cross context VM structure.
 * @param       pReq        A request buffer previously returned by
 *                          GMMR3FreePagesPrepare().
 * @param       cPages      The number of pages originally passed to
 *                          GMMR3FreePagesPrepare().
 * @param       enmAccount  The account to charge.
 */
GMMR3DECL(void) GMMR3FreePagesRePrep(PVM pVM, PGMMFREEPAGESREQ pReq, uint32_t cPages, GMMACCOUNT enmAccount)
{
    Assert(pReq->Hdr.u32Magic == SUPVMMR0REQHDR_MAGIC);
    pReq->Hdr.cbReq     = RT_UOFFSETOF_DYN(GMMFREEPAGESREQ, aPages[cPages]);
    pReq->enmAccount    = enmAccount;
    pReq->cPages        = cPages;
    NOREF(pVM);
}


/**
 * Performs a GMMR0FreePages request.
 * This will call VMSetError on failure.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pReq            Pointer to the request (returned by GMMR3FreePagesPrepare).
 * @param   cActualPages    The number of pages actually freed.
 */
GMMR3DECL(int) GMMR3FreePagesPerform(PVM pVM, PGMMFREEPAGESREQ pReq, uint32_t cActualPages)
{
    /*
     * Adjust the request if we ended up with fewer pages than anticipated.
     */
    if (cActualPages != pReq->cPages)
    {
        AssertReturn(cActualPages < pReq->cPages, VERR_GMM_ACTUAL_PAGES_IPE);
        if (!cActualPages)
            return VINF_SUCCESS;
        pReq->cPages = cActualPages;
        pReq->Hdr.cbReq = RT_UOFFSETOF_DYN(GMMFREEPAGESREQ, aPages[cActualPages]);
    }

    /*
     * Do the job.
     */
    int rc = VMMR3CallR0(pVM, VMMR0_DO_GMM_FREE_PAGES, 0, &pReq->Hdr);
    if (RT_SUCCESS(rc))
        return rc;
    AssertRC(rc);
    return VMSetError(pVM, rc, RT_SRC_POS,
                      N_("GMMR0FreePages failed to free %u pages"),
                      pReq->cPages);
}


/**
 * Cleans up a GMMR0FreePages request.
 * @param   pReq        Pointer to the request (returned by GMMR3FreePagesPrepare).
 */
GMMR3DECL(void) GMMR3FreePagesCleanup(PGMMFREEPAGESREQ pReq)
{
    RTMemTmpFree(pReq);
}


/**
 * Frees allocated pages, for bailing out on failure.
 *
 * This will not call VMSetError on failure but will use AssertLogRel instead.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pAllocReq   The allocation request to undo.
 */
GMMR3DECL(void) GMMR3FreeAllocatedPages(PVM pVM, GMMALLOCATEPAGESREQ const *pAllocReq)
{
    uint32_t cb = RT_UOFFSETOF_DYN(GMMFREEPAGESREQ, aPages[pAllocReq->cPages]);
    PGMMFREEPAGESREQ pReq = (PGMMFREEPAGESREQ)RTMemTmpAllocZ(cb);
    AssertLogRelReturnVoid(pReq);

    pReq->Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    pReq->Hdr.cbReq = cb;
    pReq->enmAccount = pAllocReq->enmAccount;
    pReq->cPages = pAllocReq->cPages;
    uint32_t iPage = pAllocReq->cPages;
    while (iPage-- > 0)
    {
        Assert(pAllocReq->aPages[iPage].idPage != NIL_GMM_PAGEID);
        pReq->aPages[iPage].idPage = pAllocReq->aPages[iPage].idPage;
    }

    int rc = VMMR3CallR0(pVM, VMMR0_DO_GMM_FREE_PAGES, 0, &pReq->Hdr);
    AssertLogRelRC(rc);

    RTMemTmpFree(pReq);
}


/**
 * @see GMMR0BalloonedPages
 */
GMMR3DECL(int)  GMMR3BalloonedPages(PVM pVM, GMMBALLOONACTION enmAction, uint32_t cBalloonedPages)
{
    int rc;
    if (!SUPR3IsDriverless())
    {
        GMMBALLOONEDPAGESREQ Req;
        Req.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
        Req.Hdr.cbReq = sizeof(Req);
        Req.enmAction = enmAction;
        Req.cBalloonedPages = cBalloonedPages;

        rc = VMMR3CallR0(pVM, VMMR0_DO_GMM_BALLOONED_PAGES, 0, &Req.Hdr);
    }
    /*
     * Ignore reset and fail all other requests.
     */
    else if (enmAction == GMMBALLOONACTION_RESET && cBalloonedPages == 0)
        rc = VINF_SUCCESS;
    else
        rc = VERR_SUP_DRIVERLESS;
    return rc;
}


/**
 * @note Caller does the driverless check.
 * @see  GMMR0QueryVMMMemoryStatsReq
 */
GMMR3DECL(int)  GMMR3QueryHypervisorMemoryStats(PVM pVM, uint64_t *pcTotalAllocPages, uint64_t *pcTotalFreePages, uint64_t *pcTotalBalloonPages, uint64_t *puTotalBalloonSize)
{
    GMMMEMSTATSREQ Req;
    Req.Hdr.u32Magic     = SUPVMMR0REQHDR_MAGIC;
    Req.Hdr.cbReq        = sizeof(Req);
    Req.cAllocPages      = 0;
    Req.cFreePages       = 0;
    Req.cBalloonedPages  = 0;
    Req.cSharedPages     = 0;

    *pcTotalAllocPages   = 0;
    *pcTotalFreePages    = 0;
    *pcTotalBalloonPages = 0;
    *puTotalBalloonSize  = 0;

    /* Must be callable from any thread, so can't use VMMR3CallR0. */
    int rc = SUPR3CallVMMR0Ex(VMCC_GET_VMR0_FOR_CALL(pVM), NIL_VMCPUID, VMMR0_DO_GMM_QUERY_HYPERVISOR_MEM_STATS, 0, &Req.Hdr);
    if (rc == VINF_SUCCESS)
    {
        *pcTotalAllocPages   = Req.cAllocPages;
        *pcTotalFreePages    = Req.cFreePages;
        *pcTotalBalloonPages = Req.cBalloonedPages;
        *puTotalBalloonSize  = Req.cSharedPages;
    }
    return rc;
}


/**
 * @see GMMR0QueryMemoryStatsReq
 */
GMMR3DECL(int)  GMMR3QueryMemoryStats(PVM pVM, uint64_t *pcAllocPages, uint64_t *pcMaxPages, uint64_t *pcBalloonPages)
{
    GMMMEMSTATSREQ Req;
    Req.Hdr.u32Magic    = SUPVMMR0REQHDR_MAGIC;
    Req.Hdr.cbReq       = sizeof(Req);
    Req.cAllocPages     = 0;
    Req.cFreePages      = 0;
    Req.cBalloonedPages = 0;

    *pcAllocPages      = 0;
    *pcMaxPages         = 0;
    *pcBalloonPages     = 0;

    int rc = VMMR3CallR0(pVM, VMMR0_DO_GMM_QUERY_MEM_STATS, 0, &Req.Hdr);
    if (rc == VINF_SUCCESS)
    {
        *pcAllocPages   = Req.cAllocPages;
        *pcMaxPages     = Req.cMaxPages;
        *pcBalloonPages = Req.cBalloonedPages;
    }
    return rc;
}


/**
 * @see GMMR0MapUnmapChunk
 */
GMMR3DECL(int)  GMMR3MapUnmapChunk(PVM pVM, uint32_t idChunkMap, uint32_t idChunkUnmap, PRTR3PTR ppvR3)
{
    GMMMAPUNMAPCHUNKREQ Req;
    Req.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    Req.Hdr.cbReq = sizeof(Req);
    Req.idChunkMap = idChunkMap;
    Req.idChunkUnmap = idChunkUnmap;
    Req.pvR3 = NULL;
    int rc = VMMR3CallR0(pVM, VMMR0_DO_GMM_MAP_UNMAP_CHUNK, 0, &Req.Hdr);
    if (RT_SUCCESS(rc) && ppvR3)
        *ppvR3 = Req.pvR3;
    return rc;
}


/**
 * @see GMMR0FreeLargePage
 */
GMMR3DECL(int)  GMMR3FreeLargePage(PVM pVM,  uint32_t idPage)
{
    GMMFREELARGEPAGEREQ Req;
    Req.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    Req.Hdr.cbReq = sizeof(Req);
    Req.idPage = idPage;
    return VMMR3CallR0(pVM, VMMR0_DO_GMM_FREE_LARGE_PAGE, 0, &Req.Hdr);
}


/**
 * @see GMMR0RegisterSharedModule
 */
GMMR3DECL(int) GMMR3RegisterSharedModule(PVM pVM, PGMMREGISTERSHAREDMODULEREQ pReq)
{
    pReq->Hdr.u32Magic  = SUPVMMR0REQHDR_MAGIC;
    pReq->Hdr.cbReq     = RT_UOFFSETOF_DYN(GMMREGISTERSHAREDMODULEREQ, aRegions[pReq->cRegions]);
    int rc = VMMR3CallR0(pVM, VMMR0_DO_GMM_REGISTER_SHARED_MODULE, 0, &pReq->Hdr);
    if (rc == VINF_SUCCESS)
        rc = pReq->rc;
    return rc;
}


/**
 * @see GMMR0RegisterSharedModule
 */
GMMR3DECL(int) GMMR3UnregisterSharedModule(PVM pVM, PGMMUNREGISTERSHAREDMODULEREQ pReq)
{
    pReq->Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    pReq->Hdr.cbReq = sizeof(*pReq);
    return VMMR3CallR0(pVM, VMMR0_DO_GMM_UNREGISTER_SHARED_MODULE, 0, &pReq->Hdr);
}


/**
 * @see GMMR0ResetSharedModules
 */
GMMR3DECL(int) GMMR3ResetSharedModules(PVM pVM)
{
    if (!SUPR3IsDriverless())
        return VMMR3CallR0(pVM, VMMR0_DO_GMM_RESET_SHARED_MODULES, 0, NULL);
    return VINF_SUCCESS;
}


/**
 * @see GMMR0CheckSharedModules
 */
GMMR3DECL(int)  GMMR3CheckSharedModules(PVM pVM)
{
    return VMMR3CallR0(pVM, VMMR0_DO_GMM_CHECK_SHARED_MODULES, 0, NULL);
}


#if defined(VBOX_STRICT) && HC_ARCH_BITS == 64
/**
 * @see GMMR0FindDuplicatePage
 */
GMMR3DECL(bool) GMMR3IsDuplicatePage(PVM pVM, uint32_t idPage)
{
    GMMFINDDUPLICATEPAGEREQ Req;
    Req.Hdr.u32Magic = SUPVMMR0REQHDR_MAGIC;
    Req.Hdr.cbReq    = sizeof(Req);
    Req.idPage       = idPage;
    Req.fDuplicate   = false;

    /* Must be callable from any thread, so can't use VMMR3CallR0. */
    int rc = SUPR3CallVMMR0Ex(VMCC_GET_VMR0_FOR_CALL(pVM), NIL_VMCPUID, VMMR0_DO_GMM_FIND_DUPLICATE_PAGE, 0, &Req.Hdr);
    if (rc == VINF_SUCCESS)
        return Req.fDuplicate;
    return false;
}
#endif /* VBOX_STRICT && HC_ARCH_BITS == 64 */

