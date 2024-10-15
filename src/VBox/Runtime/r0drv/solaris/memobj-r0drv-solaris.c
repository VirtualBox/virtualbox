/* $Id: memobj-r0drv-solaris.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Ring-0 Memory Objects, Solaris.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "the-solaris-kernel.h"
#include "internal/iprt.h"
#include <iprt/memobj.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/param.h>
#include <iprt/process.h>
#include <iprt/string.h>
#include "internal/memobj.h"
#include "memobj-r0drv-solaris.h"

extern caddr_t hat_kpm_pfn2va(pfn_t); /* Found in vm/hat.h on solaris 11.3, but not on older like 10u7. */


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define SOL_IS_KRNL_ADDR(vx)    ((uintptr_t)(vx) >= kernelbase)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The Solaris version of the memory object structure.
 */
typedef struct RTR0MEMOBJSOL
{
    /** The core structure. */
    RTR0MEMOBJINTERNAL  Core;
    /** Pointer to kernel memory cookie. */
    ddi_umem_cookie_t   Cookie;
    /** Shadow locked pages. */
    void               *pvHandle;
    /** Access during locking. */
    int                 fAccess;
    /** Set if large pages are involved in an RTR0MEMOBJTYPE_PHYS allocation. */
    bool                fLargePage;
    /** Whether we have individual pages or a kernel-mapped virtual memory
     * block in an RTR0MEMOBJTYPE_PHYS_NC allocation. */
    bool                fIndivPages;
    /** Set if executable allocation - only RTR0MEMOBJTYPE_PHYS. */
    bool                fExecutable;
} RTR0MEMOBJSOL, *PRTR0MEMOBJSOL;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static vnode_t                  g_PageVnode;
static kmutex_t                 g_OffsetMtx;
static u_offset_t               g_offPage;

static vnode_t                  g_LargePageVnode;
static kmutex_t                 g_LargePageOffsetMtx;
static u_offset_t               g_offLargePage;
static bool                     g_fLargePageNoReloc;


/**
 * Returns the physical address for a virtual address.
 *
 * @param pv        The virtual address.
 *
 * @returns The physical address corresponding to @a pv.
 */
static uint64_t rtR0MemObjSolVirtToPhys(void *pv)
{
    struct hat *pHat         = NULL;
    pfn_t       PageFrameNum = 0;
    uintptr_t   uVirtAddr    = (uintptr_t)pv;

    if (SOL_IS_KRNL_ADDR(pv))
        pHat = kas.a_hat;
    else
    {
        proc_t *pProcess = (proc_t *)RTR0ProcHandleSelf();
        AssertRelease(pProcess);
        pHat = pProcess->p_as->a_hat;
    }

    PageFrameNum = hat_getpfnum(pHat, (caddr_t)(uVirtAddr & PAGEMASK));
    AssertReleaseMsg(PageFrameNum != PFN_INVALID, ("rtR0MemObjSolVirtToPhys failed. pv=%p\n", pv));
    return (((uint64_t)PageFrameNum << PAGE_SHIFT) | (uVirtAddr & PAGE_OFFSET_MASK));
}


/**
 * Returns the physical address for a page.
 *
 * @param    pPage      Pointer to the page.
 *
 * @returns The physical address for a page.
 */
static inline uint64_t rtR0MemObjSolPagePhys(page_t *pPage)
{
    AssertPtr(pPage);
    pfn_t PageFrameNum = page_pptonum(pPage);
    AssertReleaseMsg(PageFrameNum != PFN_INVALID, ("rtR0MemObjSolPagePhys failed pPage=%p\n"));
    return (uint64_t)PageFrameNum << PAGE_SHIFT;
}


/**
 * Allocates one page.
 *
 * @param virtAddr       The virtual address to which this page maybe mapped in
 *                       the future.
 *
 * @returns Pointer to the allocated page, NULL on failure.
 */
static page_t *rtR0MemObjSolPageAlloc(caddr_t virtAddr)
{
    u_offset_t      offPage;
    seg_t           KernelSeg;

    /*
     * 16777215 terabytes of total memory for all VMs or
     * restart 8000 1GB VMs 2147483 times until wraparound!
     */
    mutex_enter(&g_OffsetMtx);
    AssertCompileSize(u_offset_t, sizeof(uint64_t)); NOREF(RTASSERTVAR);
    g_offPage = RT_ALIGN_64(g_offPage, PAGE_SIZE) + PAGE_SIZE;
    offPage   = g_offPage;
    mutex_exit(&g_OffsetMtx);

    KernelSeg.s_as = &kas;
    page_t *pPage = page_create_va(&g_PageVnode, offPage, PAGE_SIZE, PG_WAIT | PG_NORELOC, &KernelSeg, virtAddr);
    if (RT_LIKELY(pPage))
    {
        /*
         * Lock this page into memory "long term" to prevent this page from being paged out
         * when we drop the page lock temporarily (during free). Downgrade to a shared lock
         * to prevent page relocation.
         */
        page_pp_lock(pPage, 0 /* COW */, 1 /* Kernel */);
        page_io_unlock(pPage);
        page_downgrade(pPage);
        Assert(PAGE_LOCKED_SE(pPage, SE_SHARED));
    }

    return pPage;
}


/**
 * Destroys an allocated page.
 *
 * @param pPage         Pointer to the page to be destroyed.
 * @remarks This function expects page in @c pPage to be shared locked.
 */
static void rtR0MemObjSolPageDestroy(page_t *pPage)
{
    /*
     * We need to exclusive lock the pages before freeing them, if upgrading the shared lock to exclusive fails,
     * drop the page lock and look it up from the hash. Record the page offset before we drop the page lock as
     * we cannot touch any page_t members once the lock is dropped.
     */
    AssertPtr(pPage);
    Assert(PAGE_LOCKED_SE(pPage, SE_SHARED));

    u_offset_t offPage = pPage->p_offset;
    int rc = page_tryupgrade(pPage);
    if (!rc)
    {
        page_unlock(pPage);
        page_t *pFoundPage = page_lookup(&g_PageVnode, offPage, SE_EXCL);

        /*
         * Since we allocated the pages as PG_NORELOC we should only get back the exact page always.
         */
        AssertReleaseMsg(pFoundPage == pPage, ("Page lookup failed %p:%llx returned %p, expected %p\n",
                                               &g_PageVnode, offPage, pFoundPage, pPage));
    }
    Assert(PAGE_LOCKED_SE(pPage, SE_EXCL));
    page_pp_unlock(pPage, 0 /* COW */, 1 /* Kernel */);
    page_destroy(pPage, 0 /* move it to the free list */);
}


/* Currently not used on 32-bits, define it to shut up gcc. */
#if HC_ARCH_BITS == 64
/**
 * Allocates physical, non-contiguous memory of pages.
 *
 * @param puPhys    Where to store the physical address of first page. Optional,
 *                  can be NULL.
 * @param cb        The size of the allocation.
 *
 * @return Array of allocated pages, NULL on failure.
 */
static page_t **rtR0MemObjSolPagesAlloc(uint64_t *puPhys, size_t cb)
{
    /*
     * VM1:
     * The page freelist and cachelist both hold pages that are not mapped into any address space.
     * The cachelist is not really free pages but when memory is exhausted they'll be moved to the
     * free lists, it's the total of the free+cache list that we see on the 'free' column in vmstat.
     *
     * VM2:
     * @todo Document what happens behind the scenes in VM2 regarding the free and cachelist.
     */

    /*
     * Non-pageable memory reservation request for _4K pages, don't sleep.
     */
    size_t cPages = (cb + PAGE_SIZE - 1) >> PAGE_SHIFT;
    int rc = page_resv(cPages, KM_NOSLEEP);
    if (rc)
    {
        size_t   cbPages = cPages * sizeof(page_t *);
        page_t **ppPages = kmem_zalloc(cbPages, KM_SLEEP);
        if (RT_LIKELY(ppPages))
        {
            /*
             * Get pages from kseg, the 'virtAddr' here is only for colouring but unfortunately
             * we don't yet have the 'virtAddr' to which this memory may be mapped.
             */
            caddr_t virtAddr = 0;
            for (size_t i = 0; i < cPages; i++, virtAddr += PAGE_SIZE)
            {
                /*
                 * Get a page from the free list locked exclusively. The page will be named (hashed in)
                 * and we rely on it during free. The page we get will be shared locked to prevent the page
                 * from being relocated.
                 */
                page_t *pPage = rtR0MemObjSolPageAlloc(virtAddr);
                if (RT_UNLIKELY(!pPage))
                {
                    /*
                     * No page found, release whatever pages we grabbed so far.
                     */
                    for (size_t k = 0; k < i; k++)
                        rtR0MemObjSolPageDestroy(ppPages[k]);
                    kmem_free(ppPages, cbPages);
                    page_unresv(cPages);
                    return NULL;
                }

                ppPages[i] = pPage;
            }

            if (puPhys)
                *puPhys = rtR0MemObjSolPagePhys(ppPages[0]);
            return ppPages;
        }

        page_unresv(cPages);
    }

    return NULL;
}
#endif  /* HC_ARCH_BITS == 64 */


/**
 * Frees the allocates pages.
 *
 * @param ppPages       Pointer to the page list.
 * @param cbPages       Size of the allocation.
 */
static void rtR0MemObjSolPagesFree(page_t **ppPages, size_t cb)
{
    size_t cPages  = (cb + PAGE_SIZE - 1) >> PAGE_SHIFT;
    size_t cbPages = cPages * sizeof(page_t *);
    for (size_t iPage = 0; iPage < cPages; iPage++)
        rtR0MemObjSolPageDestroy(ppPages[iPage]);

    kmem_free(ppPages, cbPages);
    page_unresv(cPages);
}


/**
 * Allocates one large page.
 *
 * @param puPhys        Where to store the physical address of the allocated
 *                      page. Optional, can be NULL.
 * @param cbLargePage   Size of the large page.
 *
 * @returns Pointer to a list of pages that cover the large page, NULL on
 *        failure.
 */
static page_t **rtR0MemObjSolLargePageAlloc(uint64_t *puPhys, size_t cbLargePage)
{
    /*
     * Check PG_NORELOC support for large pages. Using this helps prevent _1G page
     * fragementation on systems that support it.
     */
    static bool fPageNoRelocChecked = false;
    if (fPageNoRelocChecked == false)
    {
        fPageNoRelocChecked = true;
        g_fLargePageNoReloc = false;
        if (   g_pfnrtR0Sol_page_noreloc_supported
            && g_pfnrtR0Sol_page_noreloc_supported(cbLargePage))
        {
            g_fLargePageNoReloc = true;
        }
    }

    /*
     * Non-pageable memory reservation request for _4K pages, don't sleep.
     */
    size_t cPages       = (cbLargePage + PAGE_SIZE - 1) >> PAGE_SHIFT;
    size_t cbPages      = cPages * sizeof(page_t *);
    u_offset_t offPage  = 0;
    int rc = page_resv(cPages, KM_NOSLEEP);
    if (rc)
    {
        page_t **ppPages = kmem_zalloc(cbPages, KM_SLEEP);
        if (RT_LIKELY(ppPages))
        {
            mutex_enter(&g_LargePageOffsetMtx);
            AssertCompileSize(u_offset_t, sizeof(uint64_t)); NOREF(RTASSERTVAR);
            g_offLargePage = RT_ALIGN_64(g_offLargePage, cbLargePage) + cbLargePage;
            offPage        = g_offLargePage;
            mutex_exit(&g_LargePageOffsetMtx);

            seg_t KernelSeg;
            KernelSeg.s_as = &kas;
            page_t *pRootPage = page_create_va_large(&g_LargePageVnode, offPage, cbLargePage,
                                                     PG_EXCL | (g_fLargePageNoReloc ? PG_NORELOC : 0), &KernelSeg,
                                                     0 /* vaddr */,NULL /* locality group */);
            if (pRootPage)
            {
                /*
                 * Split it into sub-pages, downgrade each page to a shared lock to prevent page relocation.
                 */
                page_t *pPageList = pRootPage;
                for (size_t iPage = 0; iPage < cPages; iPage++)
                {
                    page_t *pPage = pPageList;
                    AssertPtr(pPage);
                    AssertMsg(page_pptonum(pPage) == iPage + page_pptonum(pRootPage),
                        ("%p:%lx %lx+%lx\n", pPage, page_pptonum(pPage), iPage, page_pptonum(pRootPage)));
                    AssertMsg(pPage->p_szc == pRootPage->p_szc, ("Size code mismatch %p %d %d\n", pPage,
                                                                 (int)pPage->p_szc, (int)pRootPage->p_szc));

                    /*
                     * Lock the page into memory "long term". This prevents callers of page_try_demote_pages() (such as the
                     * pageout scanner) from demoting the large page into smaller pages while we temporarily release the
                     * exclusive lock (during free). We pass "0, 1" since we've already accounted for availrmem during
                     * page_resv().
                     */
                    page_pp_lock(pPage, 0 /* COW */, 1 /* Kernel */);

                    page_sub(&pPageList, pPage);
                    page_io_unlock(pPage);
                    page_downgrade(pPage);
                    Assert(PAGE_LOCKED_SE(pPage, SE_SHARED));

                    ppPages[iPage] = pPage;
                }
                Assert(pPageList == NULL);
                Assert(ppPages[0] == pRootPage);

                uint64_t uPhys = rtR0MemObjSolPagePhys(pRootPage);
                AssertMsg(!(uPhys & (cbLargePage - 1)), ("%llx %zx\n", uPhys, cbLargePage));
                if (puPhys)
                    *puPhys = uPhys;
                return ppPages;
            }

            /*
             * Don't restore offPrev in case of failure (race condition), we have plenty of offset space.
             * The offset must be unique (for the same vnode) or we'll encounter panics on page_create_va_large().
             */
            kmem_free(ppPages, cbPages);
        }

        page_unresv(cPages);
    }
    return NULL;
}


/**
 * Frees the large page.
 *
 * @param    ppPages        Pointer to the list of small pages that cover the
 *                          large page.
 * @param    cbLargePage    Size of the allocation (i.e. size of the large
 *                          page).
 */
static void rtR0MemObjSolLargePageFree(page_t **ppPages, size_t cbLargePage)
{
    Assert(ppPages);
    Assert(cbLargePage > PAGE_SIZE);

    bool   fDemoted   = false;
    size_t cPages     = (cbLargePage + PAGE_SIZE - 1) >> PAGE_SHIFT;
    size_t cbPages    = cPages * sizeof(page_t *);
    page_t *pPageList = ppPages[0];

    for (size_t iPage = 0; iPage < cPages; iPage++)
    {
        /*
         * We need the pages exclusively locked, try upgrading the shared lock.
         * If it fails, drop the shared page lock (cannot access any page_t members once this is done)
         * and lookup the page from the page hash locking it exclusively.
         */
        page_t    *pPage    = ppPages[iPage];
        u_offset_t offPage  = pPage->p_offset;
        int rc = page_tryupgrade(pPage);
        if (!rc)
        {
            page_unlock(pPage);
            page_t *pFoundPage = page_lookup(&g_LargePageVnode, offPage, SE_EXCL);
            AssertRelease(pFoundPage);

            if (g_fLargePageNoReloc)
            {
                /*
                 * This can only be guaranteed if PG_NORELOC is used while allocating the pages.
                 */
                AssertReleaseMsg(pFoundPage == pPage,
                             ("lookup failed %p:%llu returned %p, expected %p\n", &g_LargePageVnode, offPage,
                              pFoundPage, pPage));
            }

            /*
             * Check for page demotion (regardless of relocation). Some places in Solaris (e.g. VM1 page_retire())
             * could possibly demote the large page to _4K pages between our call to page_unlock() and page_lookup().
             */
            if (page_get_pagecnt(pFoundPage->p_szc) == 1)   /* Base size of only _4K associated with this page. */
                fDemoted = true;
            pPage          = pFoundPage;
            ppPages[iPage] = pFoundPage;
        }
        Assert(PAGE_LOCKED_SE(pPage, SE_EXCL));
        page_pp_unlock(pPage, 0 /* COW */, 1 /* Kernel */);
    }

    if (fDemoted)
    {
        for (size_t iPage = 0; iPage < cPages; iPage++)
        {
            Assert(page_get_pagecnt(ppPages[iPage]->p_szc) == 1);
            page_destroy(ppPages[iPage], 0 /* move it to the free list */);
        }
    }
    else
    {
        /*
         * Although we shred the adjacent pages in the linked list, page_destroy_pages works on
         * adjacent pages via array increments. So this does indeed free all the pages.
         */
        AssertPtr(pPageList);
        page_destroy_pages(pPageList);
    }
    kmem_free(ppPages, cbPages);
    page_unresv(cPages);
}


/**
 * Unmaps kernel/user-space mapped memory.
 *
 * @param    pv         Pointer to the mapped memory block.
 * @param    cb         Size of the memory block.
 */
static void rtR0MemObjSolUnmap(void *pv, size_t cb)
{
    if (SOL_IS_KRNL_ADDR(pv))
    {
        hat_unload(kas.a_hat, pv, cb, HAT_UNLOAD | HAT_UNLOAD_UNLOCK);
        vmem_free(heap_arena, pv, cb);
    }
    else
    {
        struct as *pAddrSpace = ((proc_t *)RTR0ProcHandleSelf())->p_as;
        AssertPtr(pAddrSpace);
        as_rangelock(pAddrSpace);
        as_unmap(pAddrSpace, pv, cb);
        as_rangeunlock(pAddrSpace);
    }
}


/**
 * Lock down memory mappings for a virtual address.
 *
 * @param    pv             Pointer to the memory to lock down.
 * @param    cb             Size of the memory block.
 * @param    fAccess        Page access rights (S_READ, S_WRITE, S_EXEC)
 *
 * @returns IPRT status code.
 */
static int rtR0MemObjSolLock(void *pv, size_t cb, int fPageAccess)
{
    /*
     * Kernel memory mappings on x86/amd64 are always locked, only handle user-space memory.
     */
    if (!SOL_IS_KRNL_ADDR(pv))
    {
        proc_t *pProc = (proc_t *)RTR0ProcHandleSelf();
        AssertPtr(pProc);
        faultcode_t rc = as_fault(pProc->p_as->a_hat, pProc->p_as, (caddr_t)pv, cb, F_SOFTLOCK, fPageAccess);
        if (rc)
        {
            LogRel(("rtR0MemObjSolLock failed for pv=%pv cb=%lx fPageAccess=%d rc=%d\n", pv, cb, fPageAccess, rc));
            return VERR_LOCK_FAILED;
        }
    }
    return VINF_SUCCESS;
}


/**
 * Unlock memory mappings for a virtual address.
 *
 * @param    pv             Pointer to the locked memory.
 * @param    cb             Size of the memory block.
 * @param    fPageAccess    Page access rights (S_READ, S_WRITE, S_EXEC).
 */
static void rtR0MemObjSolUnlock(void *pv, size_t cb, int fPageAccess)
{
    if (!SOL_IS_KRNL_ADDR(pv))
    {
        proc_t *pProcess = (proc_t *)RTR0ProcHandleSelf();
        AssertPtr(pProcess);
        as_fault(pProcess->p_as->a_hat, pProcess->p_as, (caddr_t)pv, cb, F_SOFTUNLOCK, fPageAccess);
    }
}


/**
 * Maps a list of physical pages into user address space.
 *
 * @param    pVirtAddr      Where to store the virtual address of the mapping.
 * @param    fPageAccess    Page access rights (PROT_READ, PROT_WRITE,
 *                          PROT_EXEC)
 * @param    paPhysAddrs    Array of physical addresses to pages.
 * @param    cb             Size of memory being mapped.
 *
 * @returns IPRT status code.
 */
static int rtR0MemObjSolUserMap(caddr_t *pVirtAddr, unsigned fPageAccess, uint64_t *paPhysAddrs, size_t cb, size_t cbPageSize)
{
    struct as *pAddrSpace = ((proc_t *)RTR0ProcHandleSelf())->p_as;
    int rc;
    SEGVBOX_CRARGS Args;

    Args.paPhysAddrs = paPhysAddrs;
    Args.fPageAccess = fPageAccess;
    Args.cbPageSize  = cbPageSize;

    as_rangelock(pAddrSpace);
    if (g_frtSolOldMapAddr)
        g_rtSolMapAddr.u.pfnSol_map_addr_old(pVirtAddr, cb, 0 /* offset */, 0 /* vacalign */, MAP_SHARED);
    else
        g_rtSolMapAddr.u.pfnSol_map_addr(pVirtAddr, cb, 0 /* offset */, MAP_SHARED);
    if (*pVirtAddr != NULL)
        rc = as_map(pAddrSpace, *pVirtAddr, cb, rtR0SegVBoxSolCreate, &Args);
    else
        rc = ENOMEM;
    as_rangeunlock(pAddrSpace);

    return RTErrConvertFromErrno(rc);
}


DECLHIDDEN(int) rtR0MemObjNativeFree(RTR0MEMOBJ pMem)
{
    PRTR0MEMOBJSOL pMemSolaris = (PRTR0MEMOBJSOL)pMem;

    switch (pMemSolaris->Core.enmType)
    {
        case RTR0MEMOBJTYPE_LOW:
            rtR0SolMemFree(pMemSolaris->Core.pv, pMemSolaris->Core.cb);
            break;

        case RTR0MEMOBJTYPE_PHYS:
            if (pMemSolaris->Core.u.Phys.fAllocated)
            {
                if (pMemSolaris->fLargePage)
                    rtR0MemObjSolLargePageFree(pMemSolaris->pvHandle, pMemSolaris->Core.cb);
                else
                    rtR0SolMemFree(pMemSolaris->Core.pv, pMemSolaris->Core.cb);
            }
            break;

        case RTR0MEMOBJTYPE_PHYS_NC:
            if (pMemSolaris->fIndivPages)
                rtR0MemObjSolPagesFree(pMemSolaris->pvHandle, pMemSolaris->Core.cb);
            else
                rtR0SolMemFree(pMemSolaris->Core.pv, pMemSolaris->Core.cb);
            break;

        case RTR0MEMOBJTYPE_PAGE:
            if (!pMemSolaris->fExecutable)
                ddi_umem_free(pMemSolaris->Cookie);
            else
                segkmem_free(heaptext_arena, pMemSolaris->Core.pv, pMemSolaris->Core.cb);
            break;

        case RTR0MEMOBJTYPE_LOCK:
            rtR0MemObjSolUnlock(pMemSolaris->Core.pv, pMemSolaris->Core.cb, pMemSolaris->fAccess);
            break;

        case RTR0MEMOBJTYPE_MAPPING:
            rtR0MemObjSolUnmap(pMemSolaris->Core.pv, pMemSolaris->Core.cb);
            break;

        case RTR0MEMOBJTYPE_RES_VIRT:
            if (pMemSolaris->Core.u.ResVirt.R0Process == NIL_RTR0PROCESS)
                vmem_xfree(heap_arena, pMemSolaris->Core.pv, pMemSolaris->Core.cb);
            else
                AssertFailed();
            break;

        case RTR0MEMOBJTYPE_CONT: /* we don't use this type here. */
        default:
            AssertMsgFailed(("enmType=%d\n", pMemSolaris->Core.enmType));
            return VERR_INTERNAL_ERROR;
    }

    return VINF_SUCCESS;
}


DECLHIDDEN(int) rtR0MemObjNativeAllocPage(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, bool fExecutable, const char *pszTag)
{
    /* Create the object. */
    PRTR0MEMOBJSOL pMemSolaris = (PRTR0MEMOBJSOL)rtR0MemObjNew(sizeof(*pMemSolaris), RTR0MEMOBJTYPE_PAGE, NULL, cb, pszTag);
    if (pMemSolaris)
    {
        void *pvMem;
        if (!fExecutable)
        {
            pMemSolaris->Core.fFlags |= RTR0MEMOBJ_FLAGS_ZERO_AT_ALLOC;
            pvMem = ddi_umem_alloc(cb, DDI_UMEM_SLEEP, &pMemSolaris->Cookie);
        }
        else
        {
            pMemSolaris->Core.fFlags |= RTR0MEMOBJ_FLAGS_UNINITIALIZED_AT_ALLOC; /** @todo does segkmem_alloc zero the memory? */
            pvMem = segkmem_alloc(heaptext_arena, cb, KM_SLEEP);
        }
        if (pvMem)
        {
            pMemSolaris->Core.pv  = pvMem;
            pMemSolaris->pvHandle = NULL;
            pMemSolaris->fExecutable = fExecutable;
            *ppMem = &pMemSolaris->Core;
            return VINF_SUCCESS;
        }
        rtR0MemObjDelete(&pMemSolaris->Core);
        return VERR_NO_PAGE_MEMORY;
    }
    return VERR_NO_MEMORY;
}


DECLHIDDEN(int) rtR0MemObjNativeAllocLarge(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, size_t cbLargePage, uint32_t fFlags,
                                           const char *pszTag)
{
    return rtR0MemObjFallbackAllocLarge(ppMem, cb, cbLargePage, fFlags, pszTag);
}


DECLHIDDEN(int) rtR0MemObjNativeAllocLow(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, bool fExecutable, const char *pszTag)
{
    AssertReturn(!fExecutable, VERR_NOT_SUPPORTED);

    /* Create the object */
    PRTR0MEMOBJSOL pMemSolaris = (PRTR0MEMOBJSOL)rtR0MemObjNew(sizeof(*pMemSolaris), RTR0MEMOBJTYPE_LOW, NULL, cb, pszTag);
    if (pMemSolaris)
    {
        /* Allocate physically low page-aligned memory. */
        uint64_t uPhysHi = _4G - 1;
        void *pvMem = rtR0SolMemAlloc(uPhysHi, NULL /* puPhys */, cb, PAGE_SIZE, false /* fContig */);
        if (pvMem)
        {
            pMemSolaris->Core.fFlags |= RTR0MEMOBJ_FLAGS_UNINITIALIZED_AT_ALLOC;
            pMemSolaris->Core.pv = pvMem;
            pMemSolaris->pvHandle = NULL;
            *ppMem = &pMemSolaris->Core;
            return VINF_SUCCESS;
        }
        rtR0MemObjDelete(&pMemSolaris->Core);
        return VERR_NO_LOW_MEMORY;
    }
    return VERR_NO_MEMORY;
}


DECLHIDDEN(int) rtR0MemObjNativeAllocCont(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, RTHCPHYS PhysHighest,
                                          bool fExecutable, const char *pszTag)
{
    AssertReturn(!fExecutable, VERR_NOT_SUPPORTED);
    return rtR0MemObjNativeAllocPhys(ppMem, cb, PhysHighest, PAGE_SIZE /* alignment */, pszTag);
}


DECLHIDDEN(int) rtR0MemObjNativeAllocPhysNC(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, RTHCPHYS PhysHighest, const char *pszTag)
{
#if HC_ARCH_BITS == 64
    PRTR0MEMOBJSOL pMemSolaris = (PRTR0MEMOBJSOL)rtR0MemObjNew(sizeof(*pMemSolaris), RTR0MEMOBJTYPE_PHYS_NC, NULL, cb, pszTag);
    if (pMemSolaris)
    {
        if (PhysHighest == NIL_RTHCPHYS)
        {
            uint64_t PhysAddr = UINT64_MAX;
            void *pvPages = rtR0MemObjSolPagesAlloc(&PhysAddr, cb);
            if (!pvPages)
            {
                LogRel(("rtR0MemObjNativeAllocPhysNC: rtR0MemObjSolPagesAlloc failed for cb=%u.\n", cb));
                rtR0MemObjDelete(&pMemSolaris->Core);
                return VERR_NO_MEMORY;
            }
            Assert(PhysAddr != UINT64_MAX);
            Assert(!(PhysAddr & PAGE_OFFSET_MASK));

            pMemSolaris->Core.pv     = NULL;
            pMemSolaris->pvHandle    = pvPages;
            pMemSolaris->fIndivPages = true;
        }
        else
        {
            /*
             * If we must satisfy an upper limit constraint, it isn't feasible to grab individual pages.
             * We fall back to using contig_alloc().
             */
            uint64_t PhysAddr = UINT64_MAX;
            void *pvMem = rtR0SolMemAlloc(PhysHighest, &PhysAddr, cb, PAGE_SIZE, false /* fContig */);
            if (!pvMem)
            {
                LogRel(("rtR0MemObjNativeAllocPhysNC: rtR0SolMemAlloc failed for cb=%u PhysHighest=%RHp.\n", cb, PhysHighest));
                rtR0MemObjDelete(&pMemSolaris->Core);
                return VERR_NO_MEMORY;
            }
            Assert(PhysAddr != UINT64_MAX);
            Assert(!(PhysAddr & PAGE_OFFSET_MASK));

            pMemSolaris->Core.pv     = pvMem;
            pMemSolaris->pvHandle    = NULL;
            pMemSolaris->fIndivPages = false;
        }
        pMemSolaris->Core.fFlags |= RTR0MEMOBJ_FLAGS_UNINITIALIZED_AT_ALLOC;
        *ppMem = &pMemSolaris->Core;
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;

#else /* 32 bit: */
    return VERR_NOT_SUPPORTED; /* see the RTR0MemObjAllocPhysNC specs */
#endif
}


DECLHIDDEN(int) rtR0MemObjNativeAllocPhys(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, RTHCPHYS PhysHighest, size_t uAlignment,
                                          const char *pszTag)
{
    AssertMsgReturn(PhysHighest >= 16 *_1M, ("PhysHigest=%RHp\n", PhysHighest), VERR_NOT_SUPPORTED);

    PRTR0MEMOBJSOL pMemSolaris = (PRTR0MEMOBJSOL)rtR0MemObjNew(sizeof(*pMemSolaris), RTR0MEMOBJTYPE_PHYS, NULL, cb, pszTag);
    if (RT_UNLIKELY(!pMemSolaris))
        return VERR_NO_MEMORY;

    /*
     * Allocating one large page gets special treatment.
     */
    static uint32_t s_cbLargePage = UINT32_MAX;
    if (s_cbLargePage == UINT32_MAX)
    {
        if (page_num_pagesizes() > 1)
            ASMAtomicWriteU32(&s_cbLargePage, page_get_pagesize(1)); /* Page-size code 1 maps to _2M on Solaris x86/amd64. */
        else
            ASMAtomicWriteU32(&s_cbLargePage, 0);
    }

    uint64_t PhysAddr;
    if (   cb == s_cbLargePage
        && cb == uAlignment
        && PhysHighest == NIL_RTHCPHYS)
    {
        /*
         * Allocate one large page (backed by physically contiguous memory).
         */
        void *pvPages = rtR0MemObjSolLargePageAlloc(&PhysAddr, cb);
        if (RT_LIKELY(pvPages))
        {
            AssertMsg(!(PhysAddr & (cb - 1)), ("%RHp\n", PhysAddr));
            pMemSolaris->Core.fFlags           |= RTR0MEMOBJ_FLAGS_UNINITIALIZED_AT_ALLOC; /*?*/
            pMemSolaris->Core.pv                = NULL;
            pMemSolaris->Core.u.Phys.PhysBase   = PhysAddr;
            pMemSolaris->Core.u.Phys.fAllocated = true;
            pMemSolaris->pvHandle               = pvPages;
            pMemSolaris->fLargePage             = true;

            *ppMem = &pMemSolaris->Core;
            return VINF_SUCCESS;
        }
    }
    else
    {
        /*
         * Allocate physically contiguous memory aligned as specified.
         */
        AssertCompile(NIL_RTHCPHYS == UINT64_MAX); NOREF(RTASSERTVAR);
        PhysAddr = PhysHighest;
        void *pvMem = rtR0SolMemAlloc(PhysHighest, &PhysAddr, cb, uAlignment, true /* fContig */);
        if (RT_LIKELY(pvMem))
        {
            Assert(!(PhysAddr & PAGE_OFFSET_MASK));
            Assert(PhysAddr < PhysHighest);
            Assert(PhysAddr + cb <= PhysHighest);

            pMemSolaris->Core.fFlags           |= RTR0MEMOBJ_FLAGS_UNINITIALIZED_AT_ALLOC;
            pMemSolaris->Core.pv                = pvMem;
            pMemSolaris->Core.u.Phys.PhysBase   = PhysAddr;
            pMemSolaris->Core.u.Phys.fAllocated = true;
            pMemSolaris->pvHandle               = NULL;
            pMemSolaris->fLargePage             = false;

            *ppMem = &pMemSolaris->Core;
            return VINF_SUCCESS;
        }
    }
    rtR0MemObjDelete(&pMemSolaris->Core);
    return VERR_NO_CONT_MEMORY;
}


DECLHIDDEN(int) rtR0MemObjNativeEnterPhys(PPRTR0MEMOBJINTERNAL ppMem, RTHCPHYS Phys, size_t cb, uint32_t uCachePolicy,
                                          const char *pszTag)
{
    AssertReturn(uCachePolicy == RTMEM_CACHE_POLICY_DONT_CARE, VERR_NOT_SUPPORTED);

    /* Create the object. */
    PRTR0MEMOBJSOL pMemSolaris = (PRTR0MEMOBJSOL)rtR0MemObjNew(sizeof(*pMemSolaris), RTR0MEMOBJTYPE_PHYS, NULL, cb, pszTag);
    if (!pMemSolaris)
        return VERR_NO_MEMORY;

    /* There is no allocation here, it needs to be mapped somewhere first. */
    pMemSolaris->Core.u.Phys.fAllocated   = false;
    pMemSolaris->Core.u.Phys.PhysBase     = Phys;
    pMemSolaris->Core.u.Phys.uCachePolicy = uCachePolicy;
    *ppMem = &pMemSolaris->Core;
    return VINF_SUCCESS;
}


DECLHIDDEN(int) rtR0MemObjNativeLockUser(PPRTR0MEMOBJINTERNAL ppMem, RTR3PTR R3Ptr, size_t cb, uint32_t fAccess,
                                         RTR0PROCESS R0Process, const char *pszTag)
{
    AssertReturn(R0Process == RTR0ProcHandleSelf(), VERR_INVALID_PARAMETER);
    NOREF(fAccess);

    /* Create the locking object */
    PRTR0MEMOBJSOL pMemSolaris = (PRTR0MEMOBJSOL)rtR0MemObjNew(sizeof(*pMemSolaris), RTR0MEMOBJTYPE_LOCK,
                                                               (void *)R3Ptr, cb, pszTag);
    if (!pMemSolaris)
        return VERR_NO_MEMORY;

    /* Lock down user pages. */
    int fPageAccess = S_READ;
    if (fAccess & RTMEM_PROT_WRITE)
        fPageAccess = S_WRITE;
    if (fAccess & RTMEM_PROT_EXEC)
        fPageAccess = S_EXEC;
    int rc = rtR0MemObjSolLock((void *)R3Ptr, cb, fPageAccess);
    if (RT_FAILURE(rc))
    {
        LogRel(("rtR0MemObjNativeLockUser: rtR0MemObjSolLock failed rc=%d\n", rc));
        rtR0MemObjDelete(&pMemSolaris->Core);
        return rc;
    }

    /* Fill in the object attributes and return successfully. */
    pMemSolaris->Core.u.Lock.R0Process  = R0Process;
    pMemSolaris->pvHandle               = NULL;
    pMemSolaris->fAccess                = fPageAccess;
    *ppMem = &pMemSolaris->Core;
    return VINF_SUCCESS;
}


DECLHIDDEN(int) rtR0MemObjNativeLockKernel(PPRTR0MEMOBJINTERNAL ppMem, void *pv, size_t cb, uint32_t fAccess, const char *pszTag)
{
    NOREF(fAccess);

    PRTR0MEMOBJSOL pMemSolaris = (PRTR0MEMOBJSOL)rtR0MemObjNew(sizeof(*pMemSolaris), RTR0MEMOBJTYPE_LOCK, pv, cb, pszTag);
    if (!pMemSolaris)
        return VERR_NO_MEMORY;

    /* Lock down kernel pages. */
    int fPageAccess = S_READ;
    if (fAccess & RTMEM_PROT_WRITE)
        fPageAccess = S_WRITE;
    if (fAccess & RTMEM_PROT_EXEC)
        fPageAccess = S_EXEC;
    int rc = rtR0MemObjSolLock(pv, cb, fPageAccess);
    if (RT_FAILURE(rc))
    {
        LogRel(("rtR0MemObjNativeLockKernel: rtR0MemObjSolLock failed rc=%d\n", rc));
        rtR0MemObjDelete(&pMemSolaris->Core);
        return rc;
    }

    /* Fill in the object attributes and return successfully. */
    pMemSolaris->Core.u.Lock.R0Process = NIL_RTR0PROCESS;
    pMemSolaris->pvHandle              = NULL;
    pMemSolaris->fAccess               = fPageAccess;
    *ppMem = &pMemSolaris->Core;
    return VINF_SUCCESS;
}


DECLHIDDEN(int) rtR0MemObjNativeReserveKernel(PPRTR0MEMOBJINTERNAL ppMem, void *pvFixed, size_t cb, size_t uAlignment,
                                              const char *pszTag)
{
    PRTR0MEMOBJSOL  pMemSolaris;

    /*
     * Use xalloc.
     */
    void *pv = vmem_xalloc(heap_arena, cb, uAlignment, 0 /* phase */, 0 /* nocross */,
                           NULL /* minaddr */, NULL /* maxaddr */, VM_SLEEP);
    if (RT_UNLIKELY(!pv))
        return VERR_NO_MEMORY;

    /* Create the object. */
    pMemSolaris = (PRTR0MEMOBJSOL)rtR0MemObjNew(sizeof(*pMemSolaris), RTR0MEMOBJTYPE_RES_VIRT, pv, cb, pszTag);
    if (!pMemSolaris)
    {
        LogRel(("rtR0MemObjNativeReserveKernel failed to alloc memory object.\n"));
        vmem_xfree(heap_arena, pv, cb);
        return VERR_NO_MEMORY;
    }

    pMemSolaris->Core.u.ResVirt.R0Process = NIL_RTR0PROCESS;
    *ppMem = &pMemSolaris->Core;
    return VINF_SUCCESS;
}


DECLHIDDEN(int) rtR0MemObjNativeReserveUser(PPRTR0MEMOBJINTERNAL ppMem, RTR3PTR R3PtrFixed, size_t cb, size_t uAlignment,
                                            RTR0PROCESS R0Process, const char *pszTag)
{
    RT_NOREF(ppMem, R3PtrFixed, cb, uAlignment, R0Process, pszTag);
    return VERR_NOT_SUPPORTED;
}


DECLHIDDEN(int) rtR0MemObjNativeMapKernel(PPRTR0MEMOBJINTERNAL ppMem, RTR0MEMOBJ pMemToMap, void *pvFixed, size_t uAlignment,
                                          unsigned fProt, size_t offSub, size_t cbSub, const char *pszTag)
{
    /* Fail if requested to do something we can't. */
    AssertMsgReturn(pvFixed == (void *)-1, ("%p\n", pvFixed), VERR_NOT_SUPPORTED);
    if (uAlignment > PAGE_SIZE)
        return VERR_NOT_SUPPORTED;

    /*
     * Use xalloc to get address space.
     */
    if (!cbSub)
        cbSub = pMemToMap->cb;
    void *pv = vmem_xalloc(heap_arena, cbSub, uAlignment, 0 /* phase */, 0 /* nocross */,
                           NULL /* minaddr */, NULL /* maxaddr */, VM_SLEEP);
    if (RT_UNLIKELY(!pv))
        return VERR_MAP_FAILED;

    /*
     * Load the pages from the other object into it.
     */
    uint32_t fAttr  = HAT_UNORDERED_OK | HAT_MERGING_OK | HAT_LOADCACHING_OK | HAT_STORECACHING_OK;
    if (fProt & RTMEM_PROT_READ)
        fAttr |= PROT_READ;
    if (fProt & RTMEM_PROT_EXEC)
        fAttr |= PROT_EXEC;
    if (fProt & RTMEM_PROT_WRITE)
        fAttr |= PROT_WRITE;
    fAttr |= HAT_NOSYNC;

    int    rc  = VINF_SUCCESS;
    size_t off = 0;
    while (off < cbSub)
    {
        RTHCPHYS HCPhys = RTR0MemObjGetPagePhysAddr(pMemToMap, (offSub + off) >> PAGE_SHIFT);
        AssertBreakStmt(HCPhys != NIL_RTHCPHYS, rc = VERR_INTERNAL_ERROR_2);
        pfn_t pfn = HCPhys >> PAGE_SHIFT;
        AssertBreakStmt(((RTHCPHYS)pfn << PAGE_SHIFT) == HCPhys, rc = VERR_INTERNAL_ERROR_3);

        hat_devload(kas.a_hat, (uint8_t *)pv + off, PAGE_SIZE, pfn, fAttr, HAT_LOAD_LOCK);

        /* Advance. */
        off += PAGE_SIZE;
    }
    if (RT_SUCCESS(rc))
    {
        /*
         * Create a memory object for the mapping.
         */
        PRTR0MEMOBJSOL pMemSolaris = (PRTR0MEMOBJSOL)rtR0MemObjNew(sizeof(*pMemSolaris), RTR0MEMOBJTYPE_MAPPING,
                                                                   pv, cbSub, pszTag);
        if (pMemSolaris)
        {
            pMemSolaris->Core.u.Mapping.R0Process = NIL_RTR0PROCESS;
            *ppMem = &pMemSolaris->Core;
            return VINF_SUCCESS;
        }

        LogRel(("rtR0MemObjNativeMapKernel failed to alloc memory object.\n"));
        rc = VERR_NO_MEMORY;
    }

    if (off)
        hat_unload(kas.a_hat, pv, off, HAT_UNLOAD | HAT_UNLOAD_UNLOCK);
    vmem_xfree(heap_arena, pv, cbSub);
    return rc;
}


DECLHIDDEN(int) rtR0MemObjNativeMapUser(PPRTR0MEMOBJINTERNAL ppMem, PRTR0MEMOBJINTERNAL pMemToMap, RTR3PTR R3PtrFixed,
                                        size_t uAlignment, unsigned fProt, RTR0PROCESS R0Process, size_t offSub, size_t cbSub,
                                        const char *pszTag)
{
    /*
     * Fend off things we cannot do.
     */
    AssertMsgReturn(R3PtrFixed == (RTR3PTR)-1, ("%p\n", R3PtrFixed), VERR_NOT_SUPPORTED);
    AssertMsgReturn(R0Process == RTR0ProcHandleSelf(), ("%p != %p\n", R0Process, RTR0ProcHandleSelf()), VERR_NOT_SUPPORTED);
    if (uAlignment != PAGE_SIZE)
        return VERR_NOT_SUPPORTED;

    /*
     * Get parameters from the source object and offSub/cbSub.
     */
    PRTR0MEMOBJSOL  pMemToMapSolaris    = (PRTR0MEMOBJSOL)pMemToMap;
    uint8_t        *pb                  = pMemToMapSolaris->Core.pv ? (uint8_t *)pMemToMapSolaris->Core.pv + offSub : NULL;
    size_t const    cb                  = cbSub ? cbSub : pMemToMapSolaris->Core.cb;
    size_t const    cPages              = cb >> PAGE_SHIFT;
    Assert(!offSub || cbSub);
    Assert(!(cb & PAGE_OFFSET_MASK));

    /*
     * Create the mapping object
     */
    PRTR0MEMOBJSOL pMemSolaris;
    pMemSolaris = (PRTR0MEMOBJSOL)rtR0MemObjNew(sizeof(*pMemSolaris), RTR0MEMOBJTYPE_MAPPING, pb, cb, pszTag);
    if (RT_UNLIKELY(!pMemSolaris))
        return VERR_NO_MEMORY;

    /*
     * Gather the physical page address of the pages to be mapped.
     */
    int rc = VINF_SUCCESS;
    uint64_t *paPhysAddrs = kmem_zalloc(sizeof(uint64_t) * cPages, KM_SLEEP);
    if (RT_LIKELY(paPhysAddrs))
    {
        if (   pMemToMapSolaris->Core.enmType == RTR0MEMOBJTYPE_PHYS_NC
            && pMemToMapSolaris->fIndivPages)
        {
            /* Translate individual page_t to physical addresses. */
            page_t **papPages = pMemToMapSolaris->pvHandle;
            AssertPtr(papPages);
            papPages += offSub >> PAGE_SHIFT;
            for (size_t iPage = 0; iPage < cPages; iPage++)
                paPhysAddrs[iPage] = rtR0MemObjSolPagePhys(papPages[iPage]);
        }
        else if (   pMemToMapSolaris->Core.enmType == RTR0MEMOBJTYPE_PHYS
                 && pMemToMapSolaris->fLargePage)
        {
            /* Split up the large page into page-sized chunks. */
            RTHCPHYS Phys = pMemToMapSolaris->Core.u.Phys.PhysBase;
            Phys += offSub;
            for (size_t iPage = 0; iPage < cPages; iPage++, Phys += PAGE_SIZE)
                paPhysAddrs[iPage] = Phys;
        }
        else
        {
            /* Have kernel mapping, just translate virtual to physical. */
            AssertPtr(pb);
            for (size_t iPage = 0; iPage < cPages; iPage++)
            {
                paPhysAddrs[iPage] = rtR0MemObjSolVirtToPhys(pb);
                if (RT_UNLIKELY(paPhysAddrs[iPage] == -(uint64_t)1))
                {
                    LogRel(("rtR0MemObjNativeMapUser: no page to map.\n"));
                    rc = VERR_MAP_FAILED;
                    break;
                }
                pb += PAGE_SIZE;
            }
        }
        if (RT_SUCCESS(rc))
        {
            /*
             * Perform the actual mapping.
             */
            unsigned fPageAccess = PROT_READ;
            if (fProt & RTMEM_PROT_WRITE)
                fPageAccess |= PROT_WRITE;
            if (fProt & RTMEM_PROT_EXEC)
                fPageAccess |= PROT_EXEC;

            caddr_t UserAddr = NULL;
            rc = rtR0MemObjSolUserMap(&UserAddr, fPageAccess, paPhysAddrs, cb, PAGE_SIZE);
            if (RT_SUCCESS(rc))
            {
                pMemSolaris->Core.u.Mapping.R0Process = R0Process;
                pMemSolaris->Core.pv                  = UserAddr;

                *ppMem = &pMemSolaris->Core;
                kmem_free(paPhysAddrs, sizeof(uint64_t) * cPages);
                return VINF_SUCCESS;
            }

            LogRel(("rtR0MemObjNativeMapUser: rtR0MemObjSolUserMap failed rc=%d.\n", rc));
        }

        rc = VERR_MAP_FAILED;
        kmem_free(paPhysAddrs, sizeof(uint64_t) * cPages);
    }
    else
        rc = VERR_NO_MEMORY;
    rtR0MemObjDelete(&pMemSolaris->Core);
    return rc;
}


DECLHIDDEN(int) rtR0MemObjNativeProtect(PRTR0MEMOBJINTERNAL pMem, size_t offSub, size_t cbSub, uint32_t fProt)
{
    NOREF(pMem);
    NOREF(offSub);
    NOREF(cbSub);
    NOREF(fProt);
    return VERR_NOT_SUPPORTED;
}


DECLHIDDEN(RTHCPHYS) rtR0MemObjNativeGetPagePhysAddr(PRTR0MEMOBJINTERNAL pMem, size_t iPage)
{
    PRTR0MEMOBJSOL pMemSolaris = (PRTR0MEMOBJSOL)pMem;

    switch (pMemSolaris->Core.enmType)
    {
        case RTR0MEMOBJTYPE_PHYS_NC:
            if (   pMemSolaris->Core.u.Phys.fAllocated
                || !pMemSolaris->fIndivPages)
            {
                uint8_t *pb = (uint8_t *)pMemSolaris->Core.pv + ((size_t)iPage << PAGE_SHIFT);
                return rtR0MemObjSolVirtToPhys(pb);
            }
            page_t **ppPages = pMemSolaris->pvHandle;
            return rtR0MemObjSolPagePhys(ppPages[iPage]);

        case RTR0MEMOBJTYPE_PAGE:
        case RTR0MEMOBJTYPE_LOW:
        case RTR0MEMOBJTYPE_LOCK:
        {
            uint8_t *pb = (uint8_t *)pMemSolaris->Core.pv + ((size_t)iPage << PAGE_SHIFT);
            return rtR0MemObjSolVirtToPhys(pb);
        }

        /*
         * Although mapping can be handled by rtR0MemObjSolVirtToPhys(offset) like the above case,
         * request it from the parent so that we have a clear distinction between CONT/PHYS_NC.
         */
        case RTR0MEMOBJTYPE_MAPPING:
            return rtR0MemObjNativeGetPagePhysAddr(pMemSolaris->Core.uRel.Child.pParent, iPage);

        case RTR0MEMOBJTYPE_CONT:
        case RTR0MEMOBJTYPE_PHYS:
            AssertFailed(); /* handled by the caller */
        case RTR0MEMOBJTYPE_RES_VIRT:
        default:
            return NIL_RTHCPHYS;
    }
}


DECLHIDDEN(int) rtR0MemObjNativeZeroInitWithoutMapping(PRTR0MEMOBJINTERNAL pMem)
{
#ifdef RT_ARCH_AMD64
    PRTR0MEMOBJSOL const pMemSolaris = (PRTR0MEMOBJSOL)pMem;
    size_t const         cPages      = pMemSolaris->Core.cb >> PAGE_SHIFT;
    size_t               iPage;
    for (iPage = 0; iPage < cPages; iPage++)
    {
        void    *pvPage;

        /* Get the physical address of the page. */
        RTHCPHYS HCPhys = rtR0MemObjNativeGetPagePhysAddr(&pMemSolaris->Core, iPage);
        AssertReturn(HCPhys != NIL_RTHCPHYS, VERR_INTERNAL_ERROR_3);
        Assert(!(HCPhys & PAGE_OFFSET_MASK));

        /* Map it. */
        HCPhys >>= PAGE_SHIFT;
        AssertReturn(HCPhys <= physmax, VERR_INTERNAL_ERROR_3);
        pvPage = hat_kpm_pfn2va(HCPhys);
        AssertPtrReturn(pvPage, VERR_INTERNAL_ERROR_3);

        /* Zero it. */
        RT_BZERO(pvPage, PAGE_SIZE);
    }
    return VINF_SUCCESS;
#else
    RT_NOREF(pMem);
    return VERR_NOT_IMPLEMENTED;
#endif
}

