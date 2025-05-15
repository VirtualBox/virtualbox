/* $Id: memobj-r0drv-linux.c 109574 2025-05-15 18:36:29Z knut.osmundsen@oracle.com $ */
/** @file
 * IPRT - Ring-0 Memory Objects, Linux.
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
#include "the-linux-kernel.h"

#include <iprt/memobj.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/process.h>
#include <iprt/string.h>
#include "internal/memobj.h"
#include "internal/iprt.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/* early 2.6 kernels */
#ifndef PAGE_SHARED_EXEC
# define PAGE_SHARED_EXEC PAGE_SHARED
#endif
#ifndef PAGE_READONLY_EXEC
# define PAGE_READONLY_EXEC PAGE_READONLY
#endif

/** @def IPRT_USE_ALLOC_VM_AREA_FOR_EXEC
 * Whether we use alloc_vm_area (3.2+) for executable memory.
 * This is a must for 5.8+, but we enable it all the way back to 3.2.x for
 * better W^R compliance (fExecutable flag). */
#if RTLNX_VER_RANGE(3,2,0, 5,10,0) || defined(DOXYGEN_RUNNING)
# define IPRT_USE_ALLOC_VM_AREA_FOR_EXEC
#endif
/** @def IPRT_USE_APPLY_TO_PAGE_RANGE_FOR_EXEC
 * alloc_vm_area was removed with 5.10 so we have to resort to a different way
 * to allocate executable memory.
 * It would be possible to remove IPRT_USE_ALLOC_VM_AREA_FOR_EXEC and use
 * this path execlusively for 3.2+ but no time to test it really works on every
 * supported kernel, so better play safe for now.
 */
#if RTLNX_VER_MIN(5,10,0) || defined(DOXYGEN_RUNNING)
# define IPRT_USE_APPLY_TO_PAGE_RANGE_FOR_EXEC
#endif

/*
 * 2.6.29+ kernels don't work with remap_pfn_range() anymore because
 * track_pfn_vma_new() is apparently not defined for non-RAM pages.
 * It should be safe to use vm_insert_page() older kernels as well.
 */
#if RTLNX_VER_MIN(2,6,23)
# define VBOX_USE_INSERT_PAGE
#endif
#if    defined(CONFIG_X86_PAE) \
    && (   defined(HAVE_26_STYLE_REMAP_PAGE_RANGE) \
        || RTLNX_VER_RANGE(2,6,0,  2,6,11) )
# define VBOX_USE_PAE_HACK
#endif

/* gfp_t was introduced in 2.6.14, define it for earlier. */
#if RTLNX_VER_MAX(2,6,14)
# define gfp_t  unsigned
#endif

/*
 * Wrappers around mmap_lock/mmap_sem difference.
 */
#if RTLNX_VER_MIN(5,8,0)
# define LNX_MM_DOWN_READ(a_pMm)    down_read(&(a_pMm)->mmap_lock)
# define LNX_MM_UP_READ(a_pMm)        up_read(&(a_pMm)->mmap_lock)
# define LNX_MM_DOWN_WRITE(a_pMm)   down_write(&(a_pMm)->mmap_lock)
# define LNX_MM_UP_WRITE(a_pMm)       up_write(&(a_pMm)->mmap_lock)
#else
# define LNX_MM_DOWN_READ(a_pMm)    down_read(&(a_pMm)->mmap_sem)
# define LNX_MM_UP_READ(a_pMm)        up_read(&(a_pMm)->mmap_sem)
# define LNX_MM_DOWN_WRITE(a_pMm)   down_write(&(a_pMm)->mmap_sem)
# define LNX_MM_UP_WRITE(a_pMm)       up_write(&(a_pMm)->mmap_sem)
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The Linux version of the memory object structure.
 */
typedef struct RTR0MEMOBJLNX
{
    /** The core structure. */
    RTR0MEMOBJINTERNAL  Core;
    /** Set if the allocation is contiguous.
     * This means it has to be given back as one chunk. */
    bool                fContiguous;
    /** Set if executable allocation. */
    bool                fExecutable;
    /** Set if we've vmap'ed the memory into ring-0. */
    bool                fMappedToRing0;
    /** This is non-zero if large page allocation. */
    uint8_t             cLargePageOrder;
#ifdef IPRT_USE_ALLOC_VM_AREA_FOR_EXEC
    /** Return from alloc_vm_area() that we now need to use for executable
     *  memory. */
    struct vm_struct   *pArea;
    /** PTE array that goes along with pArea (must be freed). */
    pte_t             **papPtesForArea;
#endif
    /** The pages in the apPages array. */
    size_t              cPages;
    /** Array of struct page pointers. (variable size) */
    RT_FLEXIBLE_ARRAY_EXTENSION
    struct page        *apPages[RT_FLEXIBLE_ARRAY];
} RTR0MEMOBJLNX;
/** Pointer to the linux memory object. */
typedef RTR0MEMOBJLNX *PRTR0MEMOBJLNX;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/*
 * Linux allows only a coarse selection of zones for
 * allocations matching a particular maximum physical address.
 *
 * Sorted from high to low physical address!
 */
static const struct
{
    RTHCPHYS    PhysHighest;
    gfp_t       fGfp;
} g_aZones[] =
{
    { NIL_RTHCPHYS,      GFP_KERNEL },
#if (defined(RT_ARCH_AMD64) || defined(CONFIG_X86_PAE)) && defined(GFP_DMA32)
    { _4G - 1,           GFP_DMA32  }, /* ZONE_DMA32: 0-4GB */
#elif defined(RT_ARCH_ARM32) || defined(RT_ARCH_ARM64)
    { _4G - 1,           GFP_DMA    }, /* ZONE_DMA: 0-4GB */
#endif
#if defined(RT_ARCH_AMD64)
    { _16M - 1,          GFP_DMA    }, /* ZONE_DMA: 0-16MB */
#elif defined(RT_ARCH_X86)
    { 896 * _1M - 1,     GFP_USER   }, /* ZONE_NORMAL (32-bit hosts): 0-896MB */
#endif
};


static void rtR0MemObjLinuxFreePages(PRTR0MEMOBJLNX pMemLnx);


/**
 * Helper that converts from a RTR0PROCESS handle to a linux task.
 *
 * @returns The corresponding Linux task.
 * @param   R0Process   IPRT ring-0 process handle.
 */
static struct task_struct *rtR0ProcessToLinuxTask(RTR0PROCESS R0Process)
{
    /** @todo fix rtR0ProcessToLinuxTask!! */
    /** @todo many (all?) callers currently assume that we return 'current'! */
    return R0Process == RTR0ProcHandleSelf() ? current : NULL;
}


/**
 * Compute order. Some functions allocate 2^order pages.
 *
 * @returns order.
 * @param   cPages      Number of pages.
 */
static int rtR0MemObjLinuxOrder(size_t cPages)
{
    int     iOrder;
    size_t  cTmp;

    for (iOrder = 0, cTmp = cPages; cTmp >>= 1; ++iOrder)
        ;
    if (cPages & ~((size_t)1 << iOrder))
        ++iOrder;

    return iOrder;
}


/**
 * Converts from RTMEM_PROT_* to Linux PAGE_*.
 *
 * @returns Linux page protection constant.
 * @param   fProt       The IPRT protection mask.
 * @param   fKernel     Whether it applies to kernel or user space.
 */
static pgprot_t rtR0MemObjLinuxConvertProt(unsigned fProt, bool fKernel)
{
    switch (fProt)
    {
        default:
            AssertMsgFailed(("%#x %d\n", fProt, fKernel)); RT_FALL_THRU();
        case RTMEM_PROT_NONE:
            return PAGE_NONE;

        case RTMEM_PROT_READ:
            return fKernel ? PAGE_KERNEL_RO         : PAGE_READONLY;

        case RTMEM_PROT_WRITE:
        case RTMEM_PROT_WRITE | RTMEM_PROT_READ:
            return fKernel ? PAGE_KERNEL            : PAGE_SHARED;

        case RTMEM_PROT_EXEC:
        case RTMEM_PROT_EXEC | RTMEM_PROT_READ:
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
            if (fKernel)
            {
# if RTLNX_VER_MIN(6,6,0) || RTLNX_SUSE_MAJ_PREREQ(15, 6)
                /* In kernel 6.6 mk_pte() macro was fortified with additional
                 * check which does not allow to use our custom mask anymore
                 * (see kernel commit ae1f05a617dcbc0a732fbeba0893786cd009536c).
                 * For this particular mapping case, an existing mask PAGE_KERNEL_ROX
                 * can be used instead. PAGE_KERNEL_ROX was introduced in
                 * kernel 5.8, however, lets apply it for kernels 6.6 and newer
                 * to be on a safe side.
                 */
                return PAGE_KERNEL_ROX;
# else
                pgprot_t fPg = MY_PAGE_KERNEL_EXEC;
                pgprot_val(fPg) &= ~_PAGE_RW;
                return fPg;
# endif
            }
            return PAGE_READONLY_EXEC;
#else
            return fKernel ? MY_PAGE_KERNEL_EXEC    : PAGE_READONLY_EXEC;
#endif

        case RTMEM_PROT_WRITE | RTMEM_PROT_EXEC:
        case RTMEM_PROT_WRITE | RTMEM_PROT_EXEC | RTMEM_PROT_READ:
            return fKernel ? MY_PAGE_KERNEL_EXEC    : PAGE_SHARED_EXEC;
    }
}


/**
 * Worker for rtR0MemObjNativeReserveUser and rtR0MemObjNativerMapUser that creates
 * an empty user space mapping.
 *
 * We acquire the mmap_sem/mmap_lock of the task!
 *
 * @returns Pointer to the mapping.
 *          (void *)-1 on failure.
 * @param   R3PtrFixed  (RTR3PTR)-1 if anywhere, otherwise a specific location.
 * @param   cb          The size of the mapping.
 * @param   uAlignment  The alignment of the mapping.
 * @param   pTask       The Linux task to create this mapping in.
 * @param   fProt       The RTMEM_PROT_* mask.
 */
static void *rtR0MemObjLinuxDoMmap(RTR3PTR R3PtrFixed, size_t cb, size_t uAlignment, struct task_struct *pTask, unsigned fProt)
{
    unsigned fLnxProt;
    unsigned long ulAddr;

    Assert(pTask == current); /* do_mmap */
    RT_NOREF_PV(pTask);

    /*
     * Convert from IPRT protection to mman.h PROT_ and call do_mmap.
     */
    fProt &= (RTMEM_PROT_NONE | RTMEM_PROT_READ | RTMEM_PROT_WRITE | RTMEM_PROT_EXEC);
    if (fProt == RTMEM_PROT_NONE)
        fLnxProt = PROT_NONE;
    else
    {
        fLnxProt = 0;
        if (fProt & RTMEM_PROT_READ)
            fLnxProt |= PROT_READ;
        if (fProt & RTMEM_PROT_WRITE)
            fLnxProt |= PROT_WRITE;
        if (fProt & RTMEM_PROT_EXEC)
            fLnxProt |= PROT_EXEC;
    }

    if (R3PtrFixed != (RTR3PTR)-1)
    {
#if RTLNX_VER_MIN(3,5,0)
        ulAddr = vm_mmap(NULL, R3PtrFixed, cb, fLnxProt, MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED, 0);
#else
        LNX_MM_DOWN_WRITE(pTask->mm);
        ulAddr = do_mmap(NULL, R3PtrFixed, cb, fLnxProt, MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED, 0);
        LNX_MM_UP_WRITE(pTask->mm);
#endif
    }
    else
    {
#if RTLNX_VER_MIN(3,5,0)
        ulAddr = vm_mmap(NULL, 0, cb, fLnxProt, MAP_SHARED | MAP_ANONYMOUS, 0);
#else
        LNX_MM_DOWN_WRITE(pTask->mm);
        ulAddr = do_mmap(NULL, 0, cb, fLnxProt, MAP_SHARED | MAP_ANONYMOUS, 0);
        LNX_MM_UP_WRITE(pTask->mm);
#endif
        if (    !(ulAddr & ~PAGE_MASK)
            &&  (ulAddr & (uAlignment - 1)))
        {
            /** @todo implement uAlignment properly... We'll probably need to make some dummy mappings to fill
             * up alignment gaps. This is of course complicated by fragmentation (which we might have cause
             * ourselves) and further by there begin two mmap strategies (top / bottom). */
            /* For now, just ignore uAlignment requirements... */
        }
    }


    if (ulAddr & ~PAGE_MASK) /* ~PAGE_MASK == PAGE_OFFSET_MASK */
        return (void *)-1;
    return (void *)ulAddr;
}


/**
 * Worker that destroys a user space mapping.
 * Undoes what rtR0MemObjLinuxDoMmap did.
 *
 * We acquire the mmap_sem/mmap_lock of the task!
 *
 * @param   pv          The ring-3 mapping.
 * @param   cb          The size of the mapping.
 * @param   pTask       The Linux task to destroy this mapping in.
 */
static void rtR0MemObjLinuxDoMunmap(void *pv, size_t cb, struct task_struct *pTask)
{
#if RTLNX_VER_MIN(3,5,0)
    Assert(pTask == current); RT_NOREF_PV(pTask);
    vm_munmap((unsigned long)pv, cb);
#elif defined(USE_RHEL4_MUNMAP)
    LNX_MM_DOWN_WRITE(pTask->mm);
    do_munmap(pTask->mm, (unsigned long)pv, cb, 0); /* should it be 1 or 0? */
    LNX_MM_UP_WRITE(pTask->mm);
#else
    LNX_MM_DOWN_WRITE(pTask->mm);
    do_munmap(pTask->mm, (unsigned long)pv, cb);
    LNX_MM_UP_WRITE(pTask->mm);
#endif
}


/**
 * Internal worker that allocates physical pages and creates the memory object for them.
 *
 * @returns IPRT status code.
 * @param   ppMemLnx    Where to store the memory object pointer.
 * @param   enmType     The object type.
 * @param   cb          The number of bytes to allocate.
 * @param   uAlignment  The alignment of the physical memory.
 *                      Only valid if fContiguous == true, ignored otherwise.
 * @param   fFlagsLnx   The page allocation flags (GPFs).
 * @param   fContiguous Whether the allocation must be contiguous.
 * @param   fExecutable Whether the memory must be executable.
 * @param   rcNoMem     What to return when we're out of pages.
 * @param   pszTag      Allocation tag used for statistics and such.
 */
static int rtR0MemObjLinuxAllocPages(PRTR0MEMOBJLNX *ppMemLnx, RTR0MEMOBJTYPE enmType, size_t cb,
                                     size_t uAlignment, gfp_t fFlagsLnx, bool fContiguous, bool fExecutable, int rcNoMem,
                                     const char *pszTag)
{
    size_t          iPage;
    size_t const    cPages = cb >> PAGE_SHIFT;
    struct page    *paPages;

    /*
     * Allocate a memory object structure that's large enough to contain
     * the page pointer array.
     */
    PRTR0MEMOBJLNX  pMemLnx = (PRTR0MEMOBJLNX)rtR0MemObjNew(RT_UOFFSETOF_DYN(RTR0MEMOBJLNX, apPages[cPages]), enmType,
                                                            NULL, cb, pszTag);
    if (!pMemLnx)
        return VERR_NO_MEMORY;
    pMemLnx->Core.fFlags |= RTR0MEMOBJ_FLAGS_UNINITIALIZED_AT_ALLOC;
    pMemLnx->cPages = cPages;

    if (cPages > 255)
    {
# ifdef __GFP_REPEAT
        /* Try hard to allocate the memory, but the allocation attempt might fail. */
        fFlagsLnx |= __GFP_REPEAT;
# endif
# ifdef __GFP_NOMEMALLOC
        /* Introduced with Linux 2.6.12: Don't use emergency reserves */
        fFlagsLnx |= __GFP_NOMEMALLOC;
# endif
    }

    /*
     * Allocate the pages.
     * For small allocations we'll try contiguous first and then fall back on page by page.
     */
#if RTLNX_VER_MIN(2,4,22)
    if (    fContiguous
        ||  cb <= PAGE_SIZE * 2)
    {
# ifdef VBOX_USE_INSERT_PAGE
        paPages = alloc_pages(fFlagsLnx | __GFP_COMP | __GFP_NOWARN, rtR0MemObjLinuxOrder(cPages));
# else
        paPages = alloc_pages(fFlagsLnx | __GFP_NOWARN, rtR0MemObjLinuxOrder(cPages));
# endif
        if (paPages)
        {
            fContiguous = true;
            for (iPage = 0; iPage < cPages; iPage++)
                pMemLnx->apPages[iPage] = &paPages[iPage];
        }
        else if (fContiguous)
        {
            rtR0MemObjDelete(&pMemLnx->Core);
            return rcNoMem;
        }
    }

    if (!fContiguous)
    {
        /** @todo Try use alloc_pages_bulk_array when available, it should be faster
         *        than a alloc_page loop.  Put it in #ifdefs similar to
         *        IPRT_USE_APPLY_TO_PAGE_RANGE_FOR_EXEC. */
        for (iPage = 0; iPage < cPages; iPage++)
        {
            pMemLnx->apPages[iPage] = alloc_page(fFlagsLnx | __GFP_NOWARN);
            if (RT_UNLIKELY(!pMemLnx->apPages[iPage]))
            {
                while (iPage-- > 0)
                    __free_page(pMemLnx->apPages[iPage]);
                rtR0MemObjDelete(&pMemLnx->Core);
                return rcNoMem;
            }
        }
    }

#else /* < 2.4.22 */
    /** @todo figure out why we didn't allocate page-by-page on 2.4.21 and older... */
    paPages = alloc_pages(fFlagsLnx, rtR0MemObjLinuxOrder(cPages));
    if (!paPages)
    {
        rtR0MemObjDelete(&pMemLnx->Core);
        return rcNoMem;
    }
    for (iPage = 0; iPage < cPages; iPage++)
    {
        pMemLnx->apPages[iPage] = &paPages[iPage];
        if (fExecutable)
            MY_SET_PAGES_EXEC(pMemLnx->apPages[iPage], 1);
        if (PageHighMem(pMemLnx->apPages[iPage]))
            BUG();
    }

    fContiguous = true;
#endif /* < 2.4.22 */
    pMemLnx->fContiguous = fContiguous;
    pMemLnx->fExecutable = fExecutable;

#if RTLNX_VER_MAX(4,5,0)
    /*
     * Reserve the pages.
     *
     * Linux >= 4.5 with CONFIG_DEBUG_VM panics when setting PG_reserved on compound
     * pages. According to Michal Hocko this shouldn't be necessary anyway because
     * as pages which are not on the LRU list are never evictable.
     */
    for (iPage = 0; iPage < cPages; iPage++)
        SetPageReserved(pMemLnx->apPages[iPage]);
#endif

    /*
     * Note that the physical address of memory allocated with alloc_pages(flags, order)
     * is always 2^(PAGE_SHIFT+order)-aligned.
     */
    if (   fContiguous
        && uAlignment > PAGE_SIZE)
    {
        /*
         * Check for alignment constraints. The physical address of memory allocated with
         * alloc_pages(flags, order) is always 2^(PAGE_SHIFT+order)-aligned.
         */
        if (RT_UNLIKELY(page_to_phys(pMemLnx->apPages[0]) & (uAlignment - 1)))
        {
            /*
             * This should never happen!
             */
            printk("rtR0MemObjLinuxAllocPages(cb=0x%lx, uAlignment=0x%lx): alloc_pages(..., %d) returned physical memory at 0x%lx!\n",
                   (unsigned long)cb, (unsigned long)uAlignment, rtR0MemObjLinuxOrder(cPages), (unsigned long)page_to_phys(pMemLnx->apPages[0]));
            rtR0MemObjLinuxFreePages(pMemLnx);
            return rcNoMem;
        }
    }

    *ppMemLnx = pMemLnx;
    return VINF_SUCCESS;
}


/**
 * Frees the physical pages allocated by the rtR0MemObjLinuxAllocPages() call.
 *
 * This method does NOT free the object.
 *
 * @param   pMemLnx     The object which physical pages should be freed.
 */
static void rtR0MemObjLinuxFreePages(PRTR0MEMOBJLNX pMemLnx)
{
    size_t iPage = pMemLnx->cPages;
    if (iPage > 0)
    {
        /*
         * Restore the page flags.
         */
        while (iPage-- > 0)
        {
#if RTLNX_VER_MAX(4,5,0)
            /* See SetPageReserved() in rtR0MemObjLinuxAllocPages() */
            ClearPageReserved(pMemLnx->apPages[iPage]);
#endif
#if RTLNX_VER_MAX(2,4,22)
            if (pMemLnx->fExecutable)
                MY_SET_PAGES_NOEXEC(pMemLnx->apPages[iPage], 1);
#endif
        }

        /*
         * Free the pages.
         */
#if RTLNX_VER_MIN(2,4,22)
        if (!pMemLnx->fContiguous)
        {
            iPage = pMemLnx->cPages;
            while (iPage-- > 0)
                __free_page(pMemLnx->apPages[iPage]);
        }
        else
#endif
            __free_pages(pMemLnx->apPages[0], rtR0MemObjLinuxOrder(pMemLnx->cPages));

        pMemLnx->cPages = 0;
    }
}


#ifdef IPRT_USE_APPLY_TO_PAGE_RANGE_FOR_EXEC
/**
 * User data passed to the apply_to_page_range() callback.
 */
typedef struct LNXAPPLYPGRANGE
{
    /** Pointer to the memory object. */
    PRTR0MEMOBJLNX pMemLnx;
    /** The page protection flags to apply. */
    pgprot_t       fPg;
} LNXAPPLYPGRANGE;
/** Pointer to the user data. */
typedef LNXAPPLYPGRANGE *PLNXAPPLYPGRANGE;
/** Pointer to the const user data. */
typedef const LNXAPPLYPGRANGE *PCLNXAPPLYPGRANGE;

/**
 * Callback called in apply_to_page_range().
 *
 * @returns Linux status code.
 * @param   pPte                Pointer to the page table entry for the given address.
 * @param   uAddr               The address to apply the new protection to.
 * @param   pvUser              The opaque user data.
 */
static int rtR0MemObjLinuxApplyPageRange(pte_t *pPte, unsigned long uAddr, void *pvUser)
{
    PCLNXAPPLYPGRANGE pArgs = (PCLNXAPPLYPGRANGE)pvUser;
    PRTR0MEMOBJLNX pMemLnx = pArgs->pMemLnx;
    size_t idxPg = (uAddr - (unsigned long)pMemLnx->Core.pv) >> PAGE_SHIFT;

    set_pte(pPte, mk_pte(pMemLnx->apPages[idxPg], pArgs->fPg));
    return 0;
}
#endif


/**
 * Maps the allocation into ring-0.
 *
 * This will update the RTR0MEMOBJLNX::Core.pv and RTR0MEMOBJ::fMappedToRing0 members.
 *
 * Contiguous mappings that isn't in 'high' memory will already be mapped into kernel
 * space, so we'll use that mapping if possible. If execute access is required, we'll
 * play safe and do our own mapping.
 *
 * @returns IPRT status code.
 * @param   pMemLnx     The linux memory object to map.
 * @param   fExecutable Whether execute access is required.
 */
static int rtR0MemObjLinuxVMap(PRTR0MEMOBJLNX pMemLnx, bool fExecutable)
{
    int rc = VINF_SUCCESS;

    /*
     * Choose mapping strategy.
     */
    bool fMustMap = fExecutable
                 || !pMemLnx->fContiguous;
    if (!fMustMap)
    {
        size_t iPage = pMemLnx->cPages;
        while (iPage-- > 0)
            if (PageHighMem(pMemLnx->apPages[iPage]))
            {
                fMustMap = true;
                break;
            }
    }

    Assert(!pMemLnx->Core.pv);
    Assert(!pMemLnx->fMappedToRing0);

    if (fMustMap)
    {
        /*
         * Use vmap - 2.4.22 and later.
         */
#if RTLNX_VER_MIN(2,4,22) && (defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86) || defined(RT_ARCH_ARM64))
        pgprot_t fPg;
# if defined(RT_ARCH_ARM64)
        /* ARM64 architecture has no _PAGE_NX, _PAGE_PRESENT and _PAGE_RW flags.
         * Closest alternatives would be PTE_PXN, PTE_UXN, PROT_DEFAULT and PTE_WRITE. */
#  if RTLNX_VER_MIN(6,5,0)
        pgprot_val(fPg) = _PAGE_KERNEL; /* (PROT_DEFAULT | PTE_PXN | PTE_UXN | PTE_WRITE | PTE_ATTRINDX(MT_NORMAL). */
#  else /* < 6.5.0 */
        pgprot_val(fPg) = PROT_NORMAL; /* (PROT_DEFAULT | PTE_PXN | PTE_UXN | PTE_WRITE | PTE_ATTRINDX(MT_NORMAL). */
#  endif /* 6.5.0 */
# else /* !RT_ARCH_ARM64 */
        pgprot_val(fPg) = _PAGE_PRESENT | _PAGE_RW;
#  ifdef _PAGE_NX
        if (!fExecutable)
            pgprot_val(fPg) |= _PAGE_NX;
#  endif
# endif /* RT_ARCH_ARM64 */

# ifdef IPRT_USE_ALLOC_VM_AREA_FOR_EXEC
        if (fExecutable)
        {
#  if RTLNX_VER_MIN(3,2,51)
            pte_t **papPtes = (pte_t **)kmalloc_array(pMemLnx->cPages, sizeof(papPtes[0]), GFP_KERNEL);
#  else
            pte_t **papPtes = (pte_t **)kmalloc(pMemLnx->cPages * sizeof(papPtes[0]), GFP_KERNEL);
#  endif
            if (papPtes)
            {
                pMemLnx->pArea = alloc_vm_area(pMemLnx->Core.cb, papPtes); /* Note! pArea->nr_pages is not set. */
                if (pMemLnx->pArea)
                {
                    size_t i;
                    Assert(pMemLnx->pArea->size >= pMemLnx->Core.cb);   /* Note! includes guard page. */
                    Assert(pMemLnx->pArea->addr);
#  if !defined(RT_ARCH_ARM64) && defined(_PAGE_NX)
                    pgprot_val(fPg) |= _PAGE_NX; /* Uses RTR0MemObjProtect to clear NX when memory ready, W^X fashion. */
#  endif
                    pMemLnx->papPtesForArea = papPtes;
                    for (i = 0; i < pMemLnx->cPages; i++)
                        *papPtes[i] = mk_pte(pMemLnx->apPages[i], fPg);
                    pMemLnx->Core.pv = pMemLnx->pArea->addr;
                    pMemLnx->fMappedToRing0 = true;
                }
                else
                {
                    kfree(papPtes);
                    rc = VERR_MAP_FAILED;
                }
            }
            else
                rc = VERR_MAP_FAILED;
        }
        else
# endif
        {
#  if !defined(RT_ARCH_ARM64) && defined(IPRT_USE_APPLY_TO_PAGE_RANGE_FOR_EXEC)
            if (fExecutable)
                pgprot_val(fPg) |= _PAGE_NX; /* Uses RTR0MemObjProtect to clear NX when memory ready, W^X fashion. */
#  endif

# ifdef VM_MAP
            pMemLnx->Core.pv = vmap(&pMemLnx->apPages[0], pMemLnx->cPages, VM_MAP, fPg);
# else
            pMemLnx->Core.pv = vmap(&pMemLnx->apPages[0], pMemLnx->cPages, VM_ALLOC, fPg);
# endif
            if (pMemLnx->Core.pv)
                pMemLnx->fMappedToRing0 = true;
            else
                rc = VERR_MAP_FAILED;
        }
#else   /* < 2.4.22 */
        rc = VERR_NOT_SUPPORTED;
#endif
    }
    else
    {
        /*
         * Use the kernel RAM mapping.
         */
        pMemLnx->Core.pv = phys_to_virt(page_to_phys(pMemLnx->apPages[0]));
        Assert(pMemLnx->Core.pv);
    }

    return rc;
}


/**
 * Undoes what rtR0MemObjLinuxVMap() did.
 *
 * @param   pMemLnx     The linux memory object.
 */
static void rtR0MemObjLinuxVUnmap(PRTR0MEMOBJLNX pMemLnx)
{
#if RTLNX_VER_MIN(2,4,22)
# ifdef IPRT_USE_ALLOC_VM_AREA_FOR_EXEC
    if (pMemLnx->pArea)
    {
#  if 0
        pte_t **papPtes = pMemLnx->papPtesForArea;
        size_t  i;
        for (i = 0; i < pMemLnx->cPages; i++)
            *papPtes[i] = 0;
#  endif
        free_vm_area(pMemLnx->pArea);
        kfree(pMemLnx->papPtesForArea);
        pMemLnx->pArea = NULL;
        pMemLnx->papPtesForArea = NULL;
    }
    else
# endif
    if (pMemLnx->fMappedToRing0)
    {
        Assert(pMemLnx->Core.pv);
        vunmap(pMemLnx->Core.pv);
        pMemLnx->fMappedToRing0 = false;
    }
#else /* < 2.4.22 */
    Assert(!pMemLnx->fMappedToRing0);
#endif
    pMemLnx->Core.pv = NULL;
}


DECLHIDDEN(int) rtR0MemObjNativeFree(RTR0MEMOBJ pMem)
{
    IPRT_LINUX_SAVE_EFL_AC();
    PRTR0MEMOBJLNX pMemLnx = (PRTR0MEMOBJLNX)pMem;

    /*
     * Release any memory that we've allocated or locked.
     */
    switch (pMemLnx->Core.enmType)
    {
        case RTR0MEMOBJTYPE_PAGE:
        case RTR0MEMOBJTYPE_LOW:
        case RTR0MEMOBJTYPE_CONT:
        case RTR0MEMOBJTYPE_PHYS:
        case RTR0MEMOBJTYPE_PHYS_NC:
            rtR0MemObjLinuxVUnmap(pMemLnx);
            rtR0MemObjLinuxFreePages(pMemLnx);
            break;

        case RTR0MEMOBJTYPE_LARGE_PAGE:
        {
            uint32_t const cLargePages = pMemLnx->Core.cb >> (pMemLnx->cLargePageOrder + PAGE_SHIFT);
            uint32_t       iLargePage;
            for (iLargePage = 0; iLargePage < cLargePages; iLargePage++)
                __free_pages(pMemLnx->apPages[iLargePage << pMemLnx->cLargePageOrder], pMemLnx->cLargePageOrder);
            pMemLnx->cPages = 0;

#ifdef IPRT_USE_ALLOC_VM_AREA_FOR_EXEC
            Assert(!pMemLnx->pArea);
            Assert(!pMemLnx->papPtesForArea);
#endif
            break;
        }

        case RTR0MEMOBJTYPE_LOCK:
            if (pMemLnx->Core.u.Lock.R0Process != NIL_RTR0PROCESS)
            {
                struct task_struct *pTask = rtR0ProcessToLinuxTask(pMemLnx->Core.u.Lock.R0Process);
                size_t              iPage;
                Assert(pTask);
                if (pTask && pTask->mm)
                    LNX_MM_DOWN_READ(pTask->mm);

                iPage = pMemLnx->cPages;
                while (iPage-- > 0)
                {
                    if (!PageReserved(pMemLnx->apPages[iPage]))
                        SetPageDirty(pMemLnx->apPages[iPage]);
#if RTLNX_VER_MIN(4,6,0)
                    put_page(pMemLnx->apPages[iPage]);
#else
                    page_cache_release(pMemLnx->apPages[iPage]);
#endif
                }

                if (pTask && pTask->mm)
                    LNX_MM_UP_READ(pTask->mm);
            }
            /* else: kernel memory - nothing to do here. */
            break;

        case RTR0MEMOBJTYPE_RES_VIRT:
            Assert(pMemLnx->Core.pv);
            if (pMemLnx->Core.u.ResVirt.R0Process != NIL_RTR0PROCESS)
            {
                struct task_struct *pTask = rtR0ProcessToLinuxTask(pMemLnx->Core.u.Lock.R0Process);
                Assert(pTask);
                if (pTask && pTask->mm)
                    rtR0MemObjLinuxDoMunmap(pMemLnx->Core.pv, pMemLnx->Core.cb, pTask);
            }
            else
            {
                vunmap(pMemLnx->Core.pv);

                Assert(pMemLnx->cPages == 1 && pMemLnx->apPages[0] != NULL);
                __free_page(pMemLnx->apPages[0]);
                pMemLnx->apPages[0] = NULL;
                pMemLnx->cPages = 0;
            }
            pMemLnx->Core.pv = NULL;
            break;

        case RTR0MEMOBJTYPE_MAPPING:
            Assert(pMemLnx->cPages == 0); Assert(pMemLnx->Core.pv);
            if (pMemLnx->Core.u.ResVirt.R0Process != NIL_RTR0PROCESS)
            {
                struct task_struct *pTask = rtR0ProcessToLinuxTask(pMemLnx->Core.u.Lock.R0Process);
                Assert(pTask);
                if (pTask && pTask->mm)
                    rtR0MemObjLinuxDoMunmap(pMemLnx->Core.pv, pMemLnx->Core.cb, pTask);
            }
            else
                vunmap(pMemLnx->Core.pv);
            pMemLnx->Core.pv = NULL;
            break;

        default:
            AssertMsgFailed(("enmType=%d\n", pMemLnx->Core.enmType));
            return VERR_INTERNAL_ERROR;
    }
    IPRT_LINUX_RESTORE_EFL_ONLY_AC();
    return VINF_SUCCESS;
}


DECLHIDDEN(int) rtR0MemObjNativeAllocPage(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, bool fExecutable, const char *pszTag)
{
    IPRT_LINUX_SAVE_EFL_AC();
    PRTR0MEMOBJLNX pMemLnx;
    int rc;

#if RTLNX_VER_MIN(2,4,22)
    rc = rtR0MemObjLinuxAllocPages(&pMemLnx, RTR0MEMOBJTYPE_PAGE, cb, PAGE_SIZE, GFP_HIGHUSER,
                                   false /* non-contiguous */, fExecutable, VERR_NO_MEMORY, pszTag);
#else
    rc = rtR0MemObjLinuxAllocPages(&pMemLnx, RTR0MEMOBJTYPE_PAGE, cb, PAGE_SIZE, GFP_USER,
                                   false /* non-contiguous */, fExecutable, VERR_NO_MEMORY, pszTag);
#endif
    if (RT_SUCCESS(rc))
    {
        rc = rtR0MemObjLinuxVMap(pMemLnx, fExecutable);
        if (RT_SUCCESS(rc))
        {
            *ppMem = &pMemLnx->Core;
            IPRT_LINUX_RESTORE_EFL_AC();
            return rc;
        }

        rtR0MemObjLinuxFreePages(pMemLnx);
        rtR0MemObjDelete(&pMemLnx->Core);
    }

    IPRT_LINUX_RESTORE_EFL_AC();
    return rc;
}


DECLHIDDEN(int) rtR0MemObjNativeAllocLarge(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, size_t cbLargePage, uint32_t fFlags,
                                           const char *pszTag)
{
#ifdef GFP_TRANSHUGE
    /*
     * Allocate a memory object structure that's large enough to contain
     * the page pointer array.
     */
# ifdef __GFP_MOVABLE
    unsigned const  fGfp            = (GFP_TRANSHUGE | __GFP_ZERO) & ~__GFP_MOVABLE;
# else
    unsigned const  fGfp            = (GFP_TRANSHUGE | __GFP_ZERO);
# endif
    size_t const    cPagesPerLarge  = cbLargePage >> PAGE_SHIFT;
    unsigned const  cLargePageOrder = rtR0MemObjLinuxOrder(cPagesPerLarge);
    size_t const    cLargePages     = cb >> (cLargePageOrder + PAGE_SHIFT);
    size_t const    cPages          = cb >> PAGE_SHIFT;
    PRTR0MEMOBJLNX  pMemLnx;

    Assert(RT_BIT_64(cLargePageOrder + PAGE_SHIFT) == cbLargePage);
    pMemLnx = (PRTR0MEMOBJLNX)rtR0MemObjNew(RT_UOFFSETOF_DYN(RTR0MEMOBJLNX, apPages[cPages]),
                                            RTR0MEMOBJTYPE_LARGE_PAGE, NULL, cb, pszTag);
    if (pMemLnx)
    {
        size_t iLargePage;

        pMemLnx->Core.fFlags    |= RTR0MEMOBJ_FLAGS_ZERO_AT_ALLOC;
        pMemLnx->cLargePageOrder = cLargePageOrder;
        pMemLnx->cPages          = cPages;

        /*
         * Allocate the requested number of large pages.
         */
        for (iLargePage = 0; iLargePage < cLargePages; iLargePage++)
        {
            struct page *paPages = alloc_pages(fGfp, cLargePageOrder);
            if (paPages)
            {
                size_t const iPageBase = iLargePage << cLargePageOrder;
                size_t       iPage     = cPagesPerLarge;
                while (iPage-- > 0)
                    pMemLnx->apPages[iPageBase + iPage] = &paPages[iPage];
            }
            else
            {
                /*Log(("rtR0MemObjNativeAllocLarge: cb=%#zx cPages=%#zx cLargePages=%#zx cLargePageOrder=%u cPagesPerLarge=%#zx iLargePage=%#zx -> failed!\n",
                     cb, cPages, cLargePages, cLargePageOrder, cPagesPerLarge, iLargePage, paPages));*/
                while (iLargePage-- > 0)
                    __free_pages(pMemLnx->apPages[iLargePage << (cLargePageOrder - PAGE_SHIFT)], cLargePageOrder);
                rtR0MemObjDelete(&pMemLnx->Core);
                return VERR_NO_MEMORY;
            }
        }
        *ppMem = &pMemLnx->Core;
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;

#else
    /*
     * We don't call rtR0MemObjFallbackAllocLarge here as it can be a really
     * bad idea to trigger the swap daemon and whatnot.  So, just fail.
     */
    RT_NOREF(ppMem, cb, cbLargePage, fFlags, pszTag);
    return VERR_NOT_SUPPORTED;
#endif
}


DECLHIDDEN(int) rtR0MemObjNativeAllocLow(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, bool fExecutable, const char *pszTag)
{
    IPRT_LINUX_SAVE_EFL_AC();
    PRTR0MEMOBJLNX pMemLnx;
    int rc;

    /* Try to avoid GFP_DMA. GFM_DMA32 was introduced with Linux 2.6.15. */
#if (defined(RT_ARCH_AMD64) || defined(CONFIG_X86_PAE)) && defined(GFP_DMA32)
    /* ZONE_DMA32: 0-4GB */
    rc = rtR0MemObjLinuxAllocPages(&pMemLnx, RTR0MEMOBJTYPE_LOW, cb, PAGE_SIZE, GFP_KERNEL | GFP_DMA32,
                                   false /* non-contiguous */, fExecutable, VERR_NO_LOW_MEMORY, pszTag);
    if (RT_FAILURE(rc))
#endif
#ifdef RT_ARCH_AMD64
        /* ZONE_DMA: 0-16MB */
        rc = rtR0MemObjLinuxAllocPages(&pMemLnx, RTR0MEMOBJTYPE_LOW, cb, PAGE_SIZE, GFP_KERNEL | GFP_DMA,
                                       false /* non-contiguous */, fExecutable, VERR_NO_LOW_MEMORY, pszTag);
#else
# ifdef CONFIG_X86_PAE
# endif
        /* ZONE_NORMAL: 0-896MB */
        rc = rtR0MemObjLinuxAllocPages(&pMemLnx, RTR0MEMOBJTYPE_LOW, cb, PAGE_SIZE, GFP_USER,
                                       false /* non-contiguous */, fExecutable, VERR_NO_LOW_MEMORY, pszTag);
#endif
    if (RT_SUCCESS(rc))
    {
        rc = rtR0MemObjLinuxVMap(pMemLnx, fExecutable);
        if (RT_SUCCESS(rc))
        {
            *ppMem = &pMemLnx->Core;
            IPRT_LINUX_RESTORE_EFL_AC();
            return rc;
        }

        rtR0MemObjLinuxFreePages(pMemLnx);
        rtR0MemObjDelete(&pMemLnx->Core);
    }

    IPRT_LINUX_RESTORE_EFL_AC();
    return rc;
}


DECLHIDDEN(int) rtR0MemObjNativeAllocCont(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, RTHCPHYS PhysHighest,
                                          bool fExecutable, const char *pszTag)
{
    IPRT_LINUX_SAVE_EFL_AC();
    PRTR0MEMOBJLNX pMemLnx;
    int rc;
    uint32_t idxZone;

    /*
     * The last zone must be able to satisfy the PhysHighest requirement or there
     * will be no zone at all.
     */
    if (g_aZones[RT_ELEMENTS(g_aZones) - 1].PhysHighest > PhysHighest)
    {
        IPRT_LINUX_RESTORE_EFL_AC();
        AssertMsgFailedReturn(("No zone can satisfy PhysHighest=%RHp!\n", PhysHighest),
                              VERR_NO_CONT_MEMORY);
    }

    /* Find the first zone matching our PhysHighest requirement. */
    idxZone = 0;
    for (;;)
    {
        if (g_aZones[idxZone].PhysHighest <= PhysHighest)
            break; /* We found a zone satisfying the requirement. */
        idxZone++;
    }

    /* Now try to allocate pages from all the left zones until one succeeds. */
    for (;;)
    {
        rc = rtR0MemObjLinuxAllocPages(&pMemLnx, RTR0MEMOBJTYPE_CONT, cb, PAGE_SIZE, g_aZones[idxZone].fGfp,
                                       true /* contiguous */, fExecutable, VERR_NO_CONT_MEMORY, pszTag);
        idxZone++;
        if (RT_SUCCESS(rc) || idxZone == RT_ELEMENTS(g_aZones))
            break;
    }
    if (RT_SUCCESS(rc))
    {
        rc = rtR0MemObjLinuxVMap(pMemLnx, fExecutable);
        if (RT_SUCCESS(rc))
        {
#if defined(RT_STRICT)
            size_t iPage = pMemLnx->cPages;
            while (iPage-- > 0)
                Assert(page_to_phys(pMemLnx->apPages[iPage]) < PhysHighest);
#endif
            pMemLnx->Core.u.Cont.Phys = page_to_phys(pMemLnx->apPages[0]);
            *ppMem = &pMemLnx->Core;
            IPRT_LINUX_RESTORE_EFL_AC();
            return rc;
        }

        rtR0MemObjLinuxFreePages(pMemLnx);
        rtR0MemObjDelete(&pMemLnx->Core);
    }

    IPRT_LINUX_RESTORE_EFL_AC();
    return rc;
}


/**
 * Worker for rtR0MemObjLinuxAllocPhysSub that tries one allocation strategy.
 *
 * @returns IPRT status code.
 * @param   ppMemLnx    Where to
 * @param   enmType     The object type.
 * @param   cb          The size of the allocation.
 * @param   uAlignment  The alignment of the physical memory.
 *                      Only valid for fContiguous == true, ignored otherwise.
 * @param   PhysHighest See rtR0MemObjNativeAllocPhys.
 * @param   pszTag      Allocation tag used for statistics and such.
 * @param   fGfp        The Linux GFP flags to use for the allocation.
 */
static int rtR0MemObjLinuxAllocPhysSub2(PPRTR0MEMOBJINTERNAL ppMem, RTR0MEMOBJTYPE enmType,
                                        size_t cb, size_t uAlignment, RTHCPHYS PhysHighest, const char *pszTag, gfp_t fGfp)
{
    PRTR0MEMOBJLNX pMemLnx;
    int rc = rtR0MemObjLinuxAllocPages(&pMemLnx, enmType, cb, uAlignment, fGfp,
                                       enmType == RTR0MEMOBJTYPE_PHYS /* contiguous / non-contiguous */,
                                       false /*fExecutable*/, VERR_NO_PHYS_MEMORY, pszTag);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Check the addresses if necessary. (Can be optimized a bit for PHYS.)
     */
    if (PhysHighest != NIL_RTHCPHYS)
    {
        size_t iPage = pMemLnx->cPages;
        while (iPage-- > 0)
            if (page_to_phys(pMemLnx->apPages[iPage]) > PhysHighest)
            {
                rtR0MemObjLinuxFreePages(pMemLnx);
                rtR0MemObjDelete(&pMemLnx->Core);
                return VERR_NO_MEMORY;
            }
    }

    /*
     * Complete the object.
     */
    if (enmType == RTR0MEMOBJTYPE_PHYS)
    {
        pMemLnx->Core.u.Phys.PhysBase = page_to_phys(pMemLnx->apPages[0]);
        pMemLnx->Core.u.Phys.fAllocated = true;
    }
    *ppMem = &pMemLnx->Core;
    return rc;
}


/**
 * Worker for rtR0MemObjNativeAllocPhys and rtR0MemObjNativeAllocPhysNC.
 *
 * @returns IPRT status code.
 * @param   ppMem       Where to store the memory object pointer on success.
 * @param   enmType     The object type.
 * @param   cb          The size of the allocation.
 * @param   uAlignment  The alignment of the physical memory.
 *                      Only valid for enmType == RTR0MEMOBJTYPE_PHYS, ignored otherwise.
 * @param   PhysHighest See rtR0MemObjNativeAllocPhys.
 * @param   pszTag      Allocation tag used for statistics and such.
 */
static int rtR0MemObjLinuxAllocPhysSub(PPRTR0MEMOBJINTERNAL ppMem, RTR0MEMOBJTYPE enmType,
                                       size_t cb, size_t uAlignment, RTHCPHYS PhysHighest, const char *pszTag)
{
    int rc;
    IPRT_LINUX_SAVE_EFL_AC();

    /*
     * There are two clear cases and that's the <=16MB and anything-goes ones.
     * When the physical address limit is somewhere in-between those two we'll
     * just have to try, starting with HIGHUSER and working our way thru the
     * different types, hoping we'll get lucky.
     *
     * We should probably move this physical address restriction logic up to
     * the page alloc function as it would be more efficient there. But since
     * we don't expect this to be a performance issue just yet it can wait.
     */
    if (PhysHighest == NIL_RTHCPHYS)
        /* ZONE_HIGHMEM: the whole physical memory */
        rc = rtR0MemObjLinuxAllocPhysSub2(ppMem, enmType, cb, uAlignment, PhysHighest, pszTag, GFP_HIGHUSER);
    else if (PhysHighest <= _1M * 16)
        /* ZONE_DMA: 0-16MB */
        rc = rtR0MemObjLinuxAllocPhysSub2(ppMem, enmType, cb, uAlignment, PhysHighest, pszTag, GFP_DMA);
    else
    {
        rc = VERR_NO_MEMORY;
        if (RT_FAILURE(rc))
            /* ZONE_HIGHMEM: the whole physical memory */
            rc = rtR0MemObjLinuxAllocPhysSub2(ppMem, enmType, cb, uAlignment, PhysHighest, pszTag, GFP_HIGHUSER);
        if (RT_FAILURE(rc))
            /* ZONE_NORMAL: 0-896MB */
            rc = rtR0MemObjLinuxAllocPhysSub2(ppMem, enmType, cb, uAlignment, PhysHighest, pszTag, GFP_USER);
#ifdef GFP_DMA32
        if (RT_FAILURE(rc))
            /* ZONE_DMA32: 0-4GB */
            rc = rtR0MemObjLinuxAllocPhysSub2(ppMem, enmType, cb, uAlignment, PhysHighest, pszTag, GFP_DMA32);
#endif
        if (RT_FAILURE(rc))
            /* ZONE_DMA: 0-16MB */
            rc = rtR0MemObjLinuxAllocPhysSub2(ppMem, enmType, cb, uAlignment, PhysHighest, pszTag, GFP_DMA);
    }
    IPRT_LINUX_RESTORE_EFL_AC();
    return rc;
}


/**
 * Translates a kernel virtual address to a linux page structure by walking the
 * page tables.
 *
 * @note    We do assume that the page tables will not change as we are walking
 *          them.  This assumption is rather forced by the fact that I could not
 *          immediately see any way of preventing this from happening.  So, we
 *          take some extra care when accessing them.
 *
 *          Because of this, we don't want to use this function on memory where
 *          attribute changes to nearby pages is likely to cause large pages to
 *          be used or split up. So, don't use this for the linear mapping of
 *          physical memory.
 *
 * @returns Pointer to the page structur or NULL if it could not be found.
 * @param   pv      The kernel virtual address.
 */
RTDECL(struct page *) rtR0MemObjLinuxVirtToPage(void *pv)
{
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    unsigned long   ulAddr = (unsigned long)pv;
    unsigned long   pfn;
    struct page    *pPage;
    pte_t          *pEntry;
    union
    {
        pgd_t       Global;
# if RTLNX_VER_MIN(4,12,0)
        p4d_t       Four;
# endif
# if RTLNX_VER_MIN(2,6,11)
        pud_t       Upper;
# endif
        pmd_t       Middle;
        pte_t       Entry;
    } u;

    /* Should this happen in a situation this code will be called in?  And if
     * so, can it change under our feet?  See also
     * "Documentation/vm/active_mm.txt" in the kernel sources. */
    if (RT_UNLIKELY(!current->active_mm))
        return NULL;
    u.Global = *pgd_offset(current->active_mm, ulAddr);
    if (RT_UNLIKELY(pgd_none(u.Global)))
        return NULL;
# if RTLNX_VER_MIN(2,6,11)
#  if RTLNX_VER_MIN(4,12,0)
    u.Four  = *p4d_offset(&u.Global, ulAddr);
    if (RT_UNLIKELY(p4d_none(u.Four)))
        return NULL;
#   if RTLNX_VER_MIN(5,6,0)
    if (p4d_leaf(u.Four))
#   else
    if (p4d_large(u.Four))
#   endif
    {
        pPage = p4d_page(u.Four);
        AssertReturn(pPage, NULL);
        pfn   = page_to_pfn(pPage);      /* doing the safe way... */
        AssertCompile(P4D_SHIFT - PAGE_SHIFT < 31);
        pfn  += (ulAddr >> PAGE_SHIFT) & ((UINT32_C(1) << (P4D_SHIFT - PAGE_SHIFT)) - 1);
        return pfn_to_page(pfn);
    }
    u.Upper = *pud_offset(&u.Four, ulAddr);
#  else /* < 4.12 */
    u.Upper = *pud_offset(&u.Global, ulAddr);
#  endif /* < 4.12 */
    if (RT_UNLIKELY(pud_none(u.Upper)))
        return NULL;
#  if RTLNX_VER_MIN(2,6,25)
#   if RTLNX_VER_MIN(5,6,0)
    if (pud_leaf(u.Upper))
#   else
    if (pud_large(u.Upper))
#   endif
    {
        pPage = pud_page(u.Upper);
        AssertReturn(pPage, NULL);
        pfn  = page_to_pfn(pPage);      /* doing the safe way... */
        pfn += (ulAddr >> PAGE_SHIFT) & ((UINT32_C(1) << (PUD_SHIFT - PAGE_SHIFT)) - 1);
        return pfn_to_page(pfn);
    }
#  endif
    u.Middle = *pmd_offset(&u.Upper, ulAddr);
# else  /* < 2.6.11 */
    u.Middle = *pmd_offset(&u.Global, ulAddr);
# endif /* < 2.6.11 */
    if (RT_UNLIKELY(pmd_none(u.Middle)))
        return NULL;
# if RTLNX_VER_MIN(2,6,0)
#  if RTLNX_VER_MIN(5,6,0)
    if (pmd_leaf(u.Middle))
#  else
    if (pmd_large(u.Middle))
#  endif
    {
        pPage = pmd_page(u.Middle);
        AssertReturn(pPage, NULL);
        pfn  = page_to_pfn(pPage);      /* doing the safe way... */
        pfn += (ulAddr >> PAGE_SHIFT) & ((UINT32_C(1) << (PMD_SHIFT - PAGE_SHIFT)) - 1);
        return pfn_to_page(pfn);
    }
# endif

# if RTLNX_VER_MIN(6,5,0) || RTLNX_RHEL_RANGE(9,4, 9,99)
    pEntry = __pte_map(&u.Middle, ulAddr);
# elif RTLNX_VER_MIN(2,5,5) || defined(pte_offset_map) /* As usual, RHEL 3 had pte_offset_map earlier. */
    pEntry = pte_offset_map(&u.Middle, ulAddr);
# else
    pEntry = pte_offset(&u.Middle, ulAddr);
# endif
    if (RT_UNLIKELY(!pEntry))
        return NULL;
    u.Entry = *pEntry;
# if RTLNX_VER_MIN(2,5,5) || defined(pte_offset_map)
    pte_unmap(pEntry);
# endif

    if (RT_UNLIKELY(!pte_present(u.Entry)))
        return NULL;
    return pte_page(u.Entry);
#else /* !defined(RT_ARCH_AMD64) && !defined(RT_ARCH_X86) */

    if (is_vmalloc_addr(pv))
        return vmalloc_to_page(pv);

    return virt_to_page(pv);
#endif
}
RT_EXPORT_SYMBOL(rtR0MemObjLinuxVirtToPage);


DECLHIDDEN(int) rtR0MemObjNativeAllocPhys(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, RTHCPHYS PhysHighest, size_t uAlignment,
                                          const char *pszTag)
{
    return rtR0MemObjLinuxAllocPhysSub(ppMem, RTR0MEMOBJTYPE_PHYS, cb, uAlignment, PhysHighest, pszTag);
}


DECLHIDDEN(int) rtR0MemObjNativeAllocPhysNC(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, RTHCPHYS PhysHighest, const char *pszTag)
{
    return rtR0MemObjLinuxAllocPhysSub(ppMem, RTR0MEMOBJTYPE_PHYS_NC, cb, PAGE_SIZE, PhysHighest, pszTag);
}


DECLHIDDEN(int) rtR0MemObjNativeEnterPhys(PPRTR0MEMOBJINTERNAL ppMem, RTHCPHYS Phys, size_t cb, uint32_t uCachePolicy,
                                          const char *pszTag)
{
    IPRT_LINUX_SAVE_EFL_AC();

    /*
     * All we need to do here is to validate that we can use
     * ioremap on the specified address (32/64-bit dma_addr_t).
     */
    PRTR0MEMOBJLNX  pMemLnx;
    dma_addr_t      PhysAddr = Phys;
    AssertMsgReturn(PhysAddr == Phys, ("%#llx\n", (unsigned long long)Phys), VERR_ADDRESS_TOO_BIG);

    pMemLnx = (PRTR0MEMOBJLNX)rtR0MemObjNew(sizeof(*pMemLnx), RTR0MEMOBJTYPE_PHYS, NULL, cb, pszTag);
    if (!pMemLnx)
    {
        IPRT_LINUX_RESTORE_EFL_AC();
        return VERR_NO_MEMORY;
    }

    pMemLnx->Core.u.Phys.PhysBase = PhysAddr;
    pMemLnx->Core.u.Phys.fAllocated = false;
    pMemLnx->Core.u.Phys.uCachePolicy = uCachePolicy;
    Assert(!pMemLnx->cPages);
    *ppMem = &pMemLnx->Core;
    IPRT_LINUX_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}

/* openSUSE Leap 42.3 detection :-/ */
#if RTLNX_VER_RANGE(4,4,0,  4,6,0) && defined(FAULT_FLAG_REMOTE)
# define GET_USER_PAGES_API     KERNEL_VERSION(4, 10, 0) /* no typo! */
#else
# define GET_USER_PAGES_API     LINUX_VERSION_CODE
#endif

DECLHIDDEN(int) rtR0MemObjNativeLockUser(PPRTR0MEMOBJINTERNAL ppMem, RTR3PTR R3Ptr, size_t cb, uint32_t fAccess,
                                         RTR0PROCESS R0Process, const char *pszTag)
{
    IPRT_LINUX_SAVE_EFL_AC();
    const int cPages = cb >> PAGE_SHIFT;
    struct task_struct *pTask = rtR0ProcessToLinuxTask(R0Process);
# if GET_USER_PAGES_API < KERNEL_VERSION(6, 5, 0) && !RTLNX_RHEL_RANGE(9,6, 9,99)
    struct vm_area_struct **papVMAs;
# endif
    PRTR0MEMOBJLNX  pMemLnx;
    int             rc      = VERR_NO_MEMORY;
    int  const      fWrite  = fAccess & RTMEM_PROT_WRITE ? 1 : 0;

    /*
     * Check for valid task and size overflows.
     */
    if (!pTask)
        return VERR_NOT_SUPPORTED;
    if (((size_t)cPages << PAGE_SHIFT) != cb)
        return VERR_OUT_OF_RANGE;

    /*
     * Allocate the memory object and a temporary buffer for the VMAs.
     */
    pMemLnx = (PRTR0MEMOBJLNX)rtR0MemObjNew(RT_UOFFSETOF_DYN(RTR0MEMOBJLNX, apPages[cPages]), RTR0MEMOBJTYPE_LOCK,
                                            (void *)R3Ptr, cb, pszTag);
    if (!pMemLnx)
    {
        IPRT_LINUX_RESTORE_EFL_AC();
        return VERR_NO_MEMORY;
    }

# if GET_USER_PAGES_API < KERNEL_VERSION(6, 5, 0) && !RTLNX_RHEL_RANGE(9,6, 9,99)
    papVMAs = (struct vm_area_struct **)RTMemAlloc(sizeof(*papVMAs) * cPages);
    if (papVMAs)
    {
# endif
        LNX_MM_DOWN_READ(pTask->mm);

        /*
         * Get user pages.
         */
/** @todo r=bird: Should we not force read access too? */
#if GET_USER_PAGES_API >= KERNEL_VERSION(4, 6, 0)
        if (R0Process == RTR0ProcHandleSelf())
            rc = get_user_pages(R3Ptr,                  /* Where from. */
                                cPages,                 /* How many pages. */
# if GET_USER_PAGES_API >= KERNEL_VERSION(4, 9, 0)
                                fWrite ? FOLL_WRITE |   /* Write to memory. */
                                         FOLL_FORCE     /* force write access. */
                                       : 0,             /* Write to memory. */
# else
                                fWrite,                 /* Write to memory. */
                                fWrite,                 /* force write access. */
# endif
                                &pMemLnx->apPages[0]    /* Page array. */
# if GET_USER_PAGES_API < KERNEL_VERSION(6, 5, 0) && !RTLNX_SUSE_MAJ_PREREQ(15, 6) && !RTLNX_RHEL_RANGE(9,6, 9,99)
                                , papVMAs               /* vmas */
# endif
                                );
        /*
         * Actually this should not happen at the moment as call this function
         * only for our own process.
         */
        else
            rc = get_user_pages_remote(
# if GET_USER_PAGES_API < KERNEL_VERSION(5, 9, 0)
                                pTask,                  /* Task for fault accounting. */
# endif
                                pTask->mm,              /* Whose pages. */
                                R3Ptr,                  /* Where from. */
                                cPages,                 /* How many pages. */
# if GET_USER_PAGES_API >= KERNEL_VERSION(4, 9, 0)
                                fWrite ? FOLL_WRITE |   /* Write to memory. */
                                         FOLL_FORCE     /* force write access. */
                                       : 0,             /* Write to memory. */
# else
                                fWrite,                 /* Write to memory. */
                                fWrite,                 /* force write access. */
# endif
                                &pMemLnx->apPages[0]    /* Page array. */
# if GET_USER_PAGES_API < KERNEL_VERSION(6, 5, 0) && !RTLNX_RHEL_RANGE(9,6, 9,99)
                                , papVMAs               /* vmas */
# endif
# if GET_USER_PAGES_API >= KERNEL_VERSION(4, 10, 0)
                                , NULL                  /* locked */
# endif
                                );
#else /* GET_USER_PAGES_API < KERNEL_VERSION(4, 6, 0) */
            rc = get_user_pages(pTask,                  /* Task for fault accounting. */
                                pTask->mm,              /* Whose pages. */
                                R3Ptr,                  /* Where from. */
                                cPages,                 /* How many pages. */
/* The get_user_pages API change was back-ported to 4.4.168. */
# if RTLNX_VER_RANGE(4,4,168,  4,5,0)
                                fWrite ? FOLL_WRITE |   /* Write to memory. */
                                         FOLL_FORCE     /* force write access. */
                                       : 0,             /* Write to memory. */
# else
                                fWrite,                 /* Write to memory. */
                                fWrite,                 /* force write access. */
# endif
                                &pMemLnx->apPages[0],    /* Page array. */
                                papVMAs               /* vmas */
                                );
#endif /* GET_USER_PAGES_API < KERNEL_VERSION(4, 6, 0) */
        if (rc == cPages)
        {
            /*
             * Flush dcache (required?), protect against fork and _really_ pin the page
             * table entries. get_user_pages() will protect against swapping out the
             * pages but it will NOT protect against removing page table entries. This
             * can be achieved with
             *   - using mlock / mmap(..., MAP_LOCKED, ...) from userland. This requires
             *     an appropriate limit set up with setrlimit(..., RLIMIT_MEMLOCK, ...).
             *     Usual Linux distributions support only a limited size of locked pages
             *     (e.g. 32KB).
             *   - setting the PageReserved bit (as we do in rtR0MemObjLinuxAllocPages()
             *     or by
             *   - setting the VM_LOCKED flag. This is the same as doing mlock() without
             *     a range check.
             */
            /** @todo The Linux fork() protection will require more work if this API
             * is to be used for anything but locking VM pages. */
            while (rc-- > 0)
            {
                flush_dcache_page(pMemLnx->apPages[rc]);
# if GET_USER_PAGES_API < KERNEL_VERSION(6, 5, 0) && !RTLNX_SUSE_MAJ_PREREQ(15, 6) && !RTLNX_RHEL_RANGE(9,5, 9,99)
#  if RTLNX_VER_MIN(6,3,0)
                vm_flags_set(papVMAs[rc], VM_DONTCOPY | VM_LOCKED);
#  else
                papVMAs[rc]->vm_flags |= VM_DONTCOPY | VM_LOCKED;
#  endif
# endif
            }

            LNX_MM_UP_READ(pTask->mm);

# if GET_USER_PAGES_API < KERNEL_VERSION(6, 5, 0) && !RTLNX_RHEL_RANGE(9,6, 9,99)
            RTMemFree(papVMAs);
# endif

            pMemLnx->Core.u.Lock.R0Process = R0Process;
            pMemLnx->cPages = cPages;
            Assert(!pMemLnx->fMappedToRing0);
            *ppMem = &pMemLnx->Core;

            IPRT_LINUX_RESTORE_EFL_AC();
            return VINF_SUCCESS;
        }

        /*
         * Failed - we need to unlock any pages that we succeeded to lock.
         */
        while (rc-- > 0)
        {
            if (!PageReserved(pMemLnx->apPages[rc]))
                SetPageDirty(pMemLnx->apPages[rc]);
#if RTLNX_VER_MIN(4,6,0)
            put_page(pMemLnx->apPages[rc]);
#else
            page_cache_release(pMemLnx->apPages[rc]);
#endif
        }

        LNX_MM_UP_READ(pTask->mm);

        rc = VERR_LOCK_FAILED;

# if GET_USER_PAGES_API < KERNEL_VERSION(6, 5, 0) && !RTLNX_RHEL_RANGE(9,6, 9,99)
        RTMemFree(papVMAs);
    }
# endif

    rtR0MemObjDelete(&pMemLnx->Core);
    IPRT_LINUX_RESTORE_EFL_AC();
    return rc;
}


DECLHIDDEN(int) rtR0MemObjNativeLockKernel(PPRTR0MEMOBJINTERNAL ppMem, void *pv, size_t cb, uint32_t fAccess, const char *pszTag)
{
    IPRT_LINUX_SAVE_EFL_AC();
    void           *pvLast = (uint8_t *)pv + cb - 1;
    size_t const    cPages = cb >> PAGE_SHIFT;
    PRTR0MEMOBJLNX  pMemLnx;
    bool            fLinearMapping;
    int             rc;
    uint8_t        *pbPage;
    size_t          iPage;
    NOREF(fAccess);

    if (   !RTR0MemKernelIsValidAddr(pv)
        || !RTR0MemKernelIsValidAddr(pv + cb))
        return VERR_INVALID_PARAMETER;

    /*
     * The lower part of the kernel memory has a linear mapping between
     * physical and virtual addresses. So we take a short cut here.  This is
     * assumed to be the cleanest way to handle those addresses (and the code
     * is well tested, though the test for determining it is not very nice).
     * If we ever decide it isn't we can still remove it.
     */
#if 0
    fLinearMapping = (unsigned long)pvLast < VMALLOC_START;
#else
    fLinearMapping = (unsigned long)pv     >= (unsigned long)__va(0)
                  && (unsigned long)pvLast <  (unsigned long)high_memory;
#endif

    /*
     * Allocate the memory object.
     */
    pMemLnx = (PRTR0MEMOBJLNX)rtR0MemObjNew(RT_UOFFSETOF_DYN(RTR0MEMOBJLNX, apPages[cPages]), RTR0MEMOBJTYPE_LOCK,
                                            pv, cb, pszTag);
    if (!pMemLnx)
    {
        IPRT_LINUX_RESTORE_EFL_AC();
        return VERR_NO_MEMORY;
    }

    /*
     * Gather the pages.
     * We ASSUME all kernel pages are non-swappable and non-movable.
     */
    rc     = VINF_SUCCESS;
    pbPage = (uint8_t *)pvLast;
    iPage  = cPages;
    if (!fLinearMapping)
    {
        while (iPage-- > 0)
        {
            struct page *pPage = rtR0MemObjLinuxVirtToPage(pbPage);
            if (RT_UNLIKELY(!pPage))
            {
                rc = VERR_LOCK_FAILED;
                break;
            }
            pMemLnx->apPages[iPage] = pPage;
            pbPage -= PAGE_SIZE;
        }
    }
    else
    {
        while (iPage-- > 0)
        {
            pMemLnx->apPages[iPage] = virt_to_page(pbPage);
            pbPage -= PAGE_SIZE;
        }
    }
    if (RT_SUCCESS(rc))
    {
        /*
         * Complete the memory object and return.
         */
        pMemLnx->Core.u.Lock.R0Process = NIL_RTR0PROCESS;
        pMemLnx->cPages = cPages;
        Assert(!pMemLnx->fMappedToRing0);
        *ppMem = &pMemLnx->Core;

        IPRT_LINUX_RESTORE_EFL_AC();
        return VINF_SUCCESS;
    }

    rtR0MemObjDelete(&pMemLnx->Core);
    IPRT_LINUX_RESTORE_EFL_AC();
    return rc;
}


DECLHIDDEN(int) rtR0MemObjNativeReserveKernel(PPRTR0MEMOBJINTERNAL ppMem, void *pvFixed, size_t cb, size_t uAlignment,
                                              const char *pszTag)
{
#if RTLNX_VER_MIN(2,4,22)
    IPRT_LINUX_SAVE_EFL_AC();
    const size_t cPages = cb >> PAGE_SHIFT;
    struct page *pDummyPage;
    struct page **papPages;

    /* check for unsupported stuff. */
    AssertMsgReturn(pvFixed == (void *)-1, ("%p\n", pvFixed), VERR_NOT_SUPPORTED);
    if (uAlignment > PAGE_SIZE)
        return VERR_NOT_SUPPORTED;

    /*
     * Allocate a dummy page and create a page pointer array for vmap such that
     * the dummy page is mapped all over the reserved area.
     */
    pDummyPage = alloc_page(GFP_HIGHUSER | __GFP_NOWARN);
    if (pDummyPage)
    {
        papPages = RTMemAlloc(sizeof(*papPages) * cPages);
        if (papPages)
        {
            void *pv;
            size_t iPage = cPages;
            while (iPage-- > 0)
                papPages[iPage] = pDummyPage;
# ifdef VM_MAP
            pv = vmap(papPages, cPages, VM_MAP, PAGE_KERNEL_RO);
# else
            pv = vmap(papPages, cPages, VM_ALLOC, PAGE_KERNEL_RO);
# endif
            RTMemFree(papPages);
            if (pv)
            {
                PRTR0MEMOBJLNX pMemLnx = (PRTR0MEMOBJLNX)rtR0MemObjNew(sizeof(*pMemLnx), RTR0MEMOBJTYPE_RES_VIRT, pv, cb, pszTag);
                if (pMemLnx)
                {
                    pMemLnx->Core.u.ResVirt.R0Process = NIL_RTR0PROCESS;
                    pMemLnx->cPages = 1;
                    pMemLnx->apPages[0] = pDummyPage;
                    *ppMem = &pMemLnx->Core;
                    IPRT_LINUX_RESTORE_EFL_AC();
                    return VINF_SUCCESS;
                }
                vunmap(pv);
            }
        }
        __free_page(pDummyPage);
    }
    IPRT_LINUX_RESTORE_EFL_AC();
    return VERR_NO_MEMORY;

#else   /* < 2.4.22 */
    /*
     * Could probably use ioremap here, but the caller is in a better position than us
     * to select some safe physical memory.
     */
    return VERR_NOT_SUPPORTED;
#endif
}


DECLHIDDEN(int) rtR0MemObjNativeReserveUser(PPRTR0MEMOBJINTERNAL ppMem, RTR3PTR R3PtrFixed, size_t cb, size_t uAlignment,
                                            RTR0PROCESS R0Process, const char *pszTag)
{
    IPRT_LINUX_SAVE_EFL_AC();
    PRTR0MEMOBJLNX      pMemLnx;
    void               *pv;
    struct task_struct *pTask = rtR0ProcessToLinuxTask(R0Process);
    if (!pTask)
        return VERR_NOT_SUPPORTED;

    /*
     * Check that the specified alignment is supported.
     */
    if (uAlignment > PAGE_SIZE)
        return VERR_NOT_SUPPORTED;

    /*
     * Let rtR0MemObjLinuxDoMmap do the difficult bits.
     */
    pv = rtR0MemObjLinuxDoMmap(R3PtrFixed, cb, uAlignment, pTask, RTMEM_PROT_NONE);
    if (pv == (void *)-1)
    {
        IPRT_LINUX_RESTORE_EFL_AC();
        return VERR_NO_MEMORY;
    }

    pMemLnx = (PRTR0MEMOBJLNX)rtR0MemObjNew(sizeof(*pMemLnx), RTR0MEMOBJTYPE_RES_VIRT, pv, cb, pszTag);
    if (!pMemLnx)
    {
        rtR0MemObjLinuxDoMunmap(pv, cb, pTask);
        IPRT_LINUX_RESTORE_EFL_AC();
        return VERR_NO_MEMORY;
    }

    pMemLnx->Core.u.ResVirt.R0Process = R0Process;
    *ppMem = &pMemLnx->Core;
    IPRT_LINUX_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}


DECLHIDDEN(int) rtR0MemObjNativeMapKernel(PPRTR0MEMOBJINTERNAL ppMem, RTR0MEMOBJ pMemToMap, void *pvFixed, size_t uAlignment,
                                          unsigned fProt, size_t offSub, size_t cbSub, const char *pszTag)
{
    int rc = VERR_NO_MEMORY;
    PRTR0MEMOBJLNX pMemLnxToMap = (PRTR0MEMOBJLNX)pMemToMap;
    PRTR0MEMOBJLNX pMemLnx;
    IPRT_LINUX_SAVE_EFL_AC();

    /* Fail if requested to do something we can't. */
    AssertMsgReturn(pvFixed == (void *)-1, ("%p\n", pvFixed), VERR_NOT_SUPPORTED);
    if (uAlignment > PAGE_SIZE)
        return VERR_NOT_SUPPORTED;

    /*
     * Create the IPRT memory object.
     */
    if (!cbSub)
        cbSub = pMemLnxToMap->Core.cb - offSub;
    pMemLnx = (PRTR0MEMOBJLNX)rtR0MemObjNew(sizeof(*pMemLnx), RTR0MEMOBJTYPE_MAPPING, NULL, cbSub, pszTag);
    if (pMemLnx)
    {
        if (pMemLnxToMap->cPages)
        {
#if RTLNX_VER_MIN(2,4,22)
            /*
             * Use vmap - 2.4.22 and later.
             */
            pgprot_t fPg = rtR0MemObjLinuxConvertProt(fProt, true /* kernel */);
            /** @todo We don't really care too much for EXEC here... 5.8 always adds NX. */
            Assert(((offSub + cbSub) >> PAGE_SHIFT) <= pMemLnxToMap->cPages);
# ifdef VM_MAP
            pMemLnx->Core.pv = vmap(&pMemLnxToMap->apPages[offSub >> PAGE_SHIFT], cbSub >> PAGE_SHIFT, VM_MAP, fPg);
# else
            pMemLnx->Core.pv = vmap(&pMemLnxToMap->apPages[offSub >> PAGE_SHIFT], cbSub >> PAGE_SHIFT, VM_ALLOC, fPg);
# endif
            if (pMemLnx->Core.pv)
            {
                pMemLnx->fMappedToRing0 = true;
                rc = VINF_SUCCESS;
            }
            else
                rc = VERR_MAP_FAILED;

#else   /* < 2.4.22 */
            /*
             * Only option here is to share mappings if possible and forget about fProt.
             */
            if (rtR0MemObjIsRing3(pMemToMap))
                rc = VERR_NOT_SUPPORTED;
            else
            {
                rc = VINF_SUCCESS;
                if (!pMemLnxToMap->Core.pv)
                    rc = rtR0MemObjLinuxVMap(pMemLnxToMap, !!(fProt & RTMEM_PROT_EXEC));
                if (RT_SUCCESS(rc))
                {
                    Assert(pMemLnxToMap->Core.pv);
                    pMemLnx->Core.pv = (uint8_t *)pMemLnxToMap->Core.pv + offSub;
                }
            }
#endif
        }
        else
        {
            /*
             * MMIO / physical memory.
             */
            Assert(pMemLnxToMap->Core.enmType == RTR0MEMOBJTYPE_PHYS && !pMemLnxToMap->Core.u.Phys.fAllocated);
#if RTLNX_VER_MIN(2,6,25)
            /*
             * ioremap() defaults to no caching since the 2.6 kernels.
             * ioremap_nocache() has been removed finally in 5.6-rc1.
             */
            pMemLnx->Core.pv = pMemLnxToMap->Core.u.Phys.uCachePolicy == RTMEM_CACHE_POLICY_MMIO
                             ? ioremap(pMemLnxToMap->Core.u.Phys.PhysBase + offSub, cbSub)
                             : ioremap_cache(pMemLnxToMap->Core.u.Phys.PhysBase + offSub, cbSub);
#else /* KERNEL_VERSION < 2.6.25 */
            pMemLnx->Core.pv = pMemLnxToMap->Core.u.Phys.uCachePolicy == RTMEM_CACHE_POLICY_MMIO
                             ? ioremap_nocache(pMemLnxToMap->Core.u.Phys.PhysBase + offSub, cbSub)
                             : ioremap(pMemLnxToMap->Core.u.Phys.PhysBase + offSub, cbSub);
#endif /* KERNEL_VERSION < 2.6.25 */
            if (pMemLnx->Core.pv)
            {
                /** @todo fix protection. */
                rc = VINF_SUCCESS;
            }
        }
        if (RT_SUCCESS(rc))
        {
            pMemLnx->Core.u.Mapping.R0Process = NIL_RTR0PROCESS;
            *ppMem = &pMemLnx->Core;
            IPRT_LINUX_RESTORE_EFL_AC();
            return VINF_SUCCESS;
        }
        rtR0MemObjDelete(&pMemLnx->Core);
    }

    IPRT_LINUX_RESTORE_EFL_AC();
    return rc;
}


#ifdef VBOX_USE_PAE_HACK
/**
 * Replace the PFN of a PTE with the address of the actual page.
 *
 * The caller maps a reserved dummy page at the address with the desired access
 * and flags.
 *
 * This hack is required for older Linux kernels which don't provide
 * remap_pfn_range().
 *
 * @returns 0 on success, -ENOMEM on failure.
 * @param   mm          The memory context.
 * @param   ulAddr      The mapping address.
 * @param   Phys        The physical address of the page to map.
 */
static int rtR0MemObjLinuxFixPte(struct mm_struct *mm, unsigned long ulAddr, RTHCPHYS Phys)
{
    int rc = -ENOMEM;
    pgd_t *pgd;

    spin_lock(&mm->page_table_lock);

    pgd = pgd_offset(mm, ulAddr);
    if (!pgd_none(*pgd) && !pgd_bad(*pgd))
    {
        pmd_t *pmd = pmd_offset(pgd, ulAddr);
        if (!pmd_none(*pmd))
        {
            pte_t *ptep = pte_offset_map(pmd, ulAddr);
            if (ptep)
            {
                pte_t pte = *ptep;
                pte.pte_high &= 0xfff00000;
                pte.pte_high |= ((Phys >> 32) & 0x000fffff);
                pte.pte_low  &= 0x00000fff;
                pte.pte_low  |= (Phys & 0xfffff000);
                set_pte(ptep, pte);
                pte_unmap(ptep);
                rc = 0;
            }
        }
    }

    spin_unlock(&mm->page_table_lock);
    return rc;
}
#endif /* VBOX_USE_PAE_HACK */


DECLHIDDEN(int) rtR0MemObjNativeMapUser(PPRTR0MEMOBJINTERNAL ppMem, RTR0MEMOBJ pMemToMap, RTR3PTR R3PtrFixed, size_t uAlignment,
                                        unsigned fProt, RTR0PROCESS R0Process, size_t offSub, size_t cbSub, const char *pszTag)
{
    struct task_struct *pTask        = rtR0ProcessToLinuxTask(R0Process);
    PRTR0MEMOBJLNX      pMemLnxToMap = (PRTR0MEMOBJLNX)pMemToMap;
    int                 rc           = VERR_NO_MEMORY;
    PRTR0MEMOBJLNX      pMemLnx;
#ifdef VBOX_USE_PAE_HACK
    struct page        *pDummyPage;
    RTHCPHYS            DummyPhys;
#endif
    IPRT_LINUX_SAVE_EFL_AC();

    /*
     * Check for restrictions.
     */
    if (!pTask)
        return VERR_NOT_SUPPORTED;
    if (uAlignment > PAGE_SIZE)
        return VERR_NOT_SUPPORTED;

#ifdef VBOX_USE_PAE_HACK
    /*
     * Allocate a dummy page for use when mapping the memory.
     */
    pDummyPage = alloc_page(GFP_USER | __GFP_NOWARN);
    if (!pDummyPage)
    {
        IPRT_LINUX_RESTORE_EFL_AC();
        return VERR_NO_MEMORY;
    }
    SetPageReserved(pDummyPage);
    DummyPhys = page_to_phys(pDummyPage);
#endif

    /*
     * Create the IPRT memory object.
     */
    Assert(!offSub || cbSub);
    if (cbSub == 0)
        cbSub = pMemLnxToMap->Core.cb;
    pMemLnx = (PRTR0MEMOBJLNX)rtR0MemObjNew(sizeof(*pMemLnx), RTR0MEMOBJTYPE_MAPPING, NULL, cbSub, pszTag);
    if (pMemLnx)
    {
        /*
         * Allocate user space mapping.
         */
        void *pv;
        pv = rtR0MemObjLinuxDoMmap(R3PtrFixed, cbSub, uAlignment, pTask, fProt);
        if (pv != (void *)-1)
        {
            /*
             * Map page by page into the mmap area.
             * This is generic, paranoid and not very efficient.
             */
            pgprot_t        fPg       = rtR0MemObjLinuxConvertProt(fProt, false /* user */);
            unsigned long   ulAddrCur = (unsigned long)pv;
            const size_t    cPages    = (offSub + cbSub) >> PAGE_SHIFT;
            size_t          iPage;

            LNX_MM_DOWN_WRITE(pTask->mm);

            rc = VINF_SUCCESS;
            if (pMemLnxToMap->cPages)
            {
                for (iPage = offSub >> PAGE_SHIFT; iPage < cPages; iPage++, ulAddrCur += PAGE_SIZE)
                {
#if RTLNX_VER_MAX(2,6,11)
                    RTHCPHYS Phys = page_to_phys(pMemLnxToMap->apPages[iPage]);
#endif
#if RTLNX_VER_MIN(2,6,0) || defined(HAVE_26_STYLE_REMAP_PAGE_RANGE)
                    struct vm_area_struct *vma = find_vma(pTask->mm, ulAddrCur); /* this is probably the same for all the pages... */
                    AssertBreakStmt(vma, rc = VERR_INTERNAL_ERROR);
#endif
#if RTLNX_VER_MAX(2,6,0) && defined(RT_ARCH_X86)
                    /* remap_page_range() limitation on x86 */
                    AssertBreakStmt(Phys < _4G, rc = VERR_NO_MEMORY);
#endif

#if   defined(VBOX_USE_INSERT_PAGE) && RTLNX_VER_MIN(2,6,22)
                    rc = vm_insert_page(vma, ulAddrCur, pMemLnxToMap->apPages[iPage]);
                    /* Thes flags help making 100% sure some bad stuff wont happen (swap, core, ++).
                     * See remap_pfn_range() in mm/memory.c */

#if    RTLNX_VER_MIN(6,3,0) || RTLNX_RHEL_RANGE(9,5, 9,99)
                    vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);
#elif  RTLNX_VER_MIN(3,7,0)
                    vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
#else
                    vma->vm_flags |= VM_RESERVED;
#endif
#elif RTLNX_VER_MIN(2,6,11)
                    rc = remap_pfn_range(vma, ulAddrCur, page_to_pfn(pMemLnxToMap->apPages[iPage]), PAGE_SIZE, fPg);
#elif defined(VBOX_USE_PAE_HACK)
                    rc = remap_page_range(vma, ulAddrCur, DummyPhys, PAGE_SIZE, fPg);
                    if (!rc)
                        rc = rtR0MemObjLinuxFixPte(pTask->mm, ulAddrCur, Phys);
#elif RTLNX_VER_MIN(2,6,0) || defined(HAVE_26_STYLE_REMAP_PAGE_RANGE)
                    rc = remap_page_range(vma, ulAddrCur, Phys, PAGE_SIZE, fPg);
#else /* 2.4 */
                    rc = remap_page_range(ulAddrCur, Phys, PAGE_SIZE, fPg);
#endif
                    if (rc)
                    {
                        rc = VERR_NO_MEMORY;
                        break;
                    }
                }
            }
            else
            {
                RTHCPHYS Phys;
                if (pMemLnxToMap->Core.enmType == RTR0MEMOBJTYPE_PHYS)
                    Phys = pMemLnxToMap->Core.u.Phys.PhysBase;
                else if (pMemLnxToMap->Core.enmType == RTR0MEMOBJTYPE_CONT)
                    Phys = pMemLnxToMap->Core.u.Cont.Phys;
                else
                {
                    AssertMsgFailed(("%d\n", pMemLnxToMap->Core.enmType));
                    Phys = NIL_RTHCPHYS;
                }
                if (Phys != NIL_RTHCPHYS)
                {
                    for (iPage = offSub >> PAGE_SHIFT; iPage < cPages; iPage++, ulAddrCur += PAGE_SIZE, Phys += PAGE_SIZE)
                    {
#if RTLNX_VER_MIN(2,6,0) || defined(HAVE_26_STYLE_REMAP_PAGE_RANGE)
                        struct vm_area_struct *vma = find_vma(pTask->mm, ulAddrCur); /* this is probably the same for all the pages... */
                        AssertBreakStmt(vma, rc = VERR_INTERNAL_ERROR);
#endif
#if RTLNX_VER_MAX(2,6,0) && defined(RT_ARCH_X86)
                        /* remap_page_range() limitation on x86 */
                        AssertBreakStmt(Phys < _4G, rc = VERR_NO_MEMORY);
#endif

#if   RTLNX_VER_MIN(2,6,11)
                        rc = remap_pfn_range(vma, ulAddrCur, Phys, PAGE_SIZE, fPg);
#elif defined(VBOX_USE_PAE_HACK)
                        rc = remap_page_range(vma, ulAddrCur, DummyPhys, PAGE_SIZE, fPg);
                        if (!rc)
                            rc = rtR0MemObjLinuxFixPte(pTask->mm, ulAddrCur, Phys);
#elif RTLNX_VER_MIN(2,6,0) || defined(HAVE_26_STYLE_REMAP_PAGE_RANGE)
                        rc = remap_page_range(vma, ulAddrCur, Phys, PAGE_SIZE, fPg);
#else /* 2.4 */
                        rc = remap_page_range(ulAddrCur, Phys, PAGE_SIZE, fPg);
#endif
                        if (rc)
                        {
                            rc = VERR_NO_MEMORY;
                            break;
                        }
                    }
                }
            }

#ifdef CONFIG_NUMA_BALANCING
# if RTLNX_VER_MAX(3,13,0) && RTLNX_RHEL_MAX(7,0)
#  define VBOX_NUMA_HACK_OLD
# endif
            if (RT_SUCCESS(rc))
            {
                /** @todo Ugly hack! But right now we have no other means to
                 *        disable automatic NUMA page balancing. */
# ifdef RT_OS_X86
#  ifdef VBOX_NUMA_HACK_OLD
                pTask->mm->numa_next_reset = jiffies + 0x7fffffffUL;
#  endif
                pTask->mm->numa_next_scan  = jiffies + 0x7fffffffUL;
# else
#  ifdef VBOX_NUMA_HACK_OLD
                pTask->mm->numa_next_reset = jiffies + 0x7fffffffffffffffUL;
#  endif
                pTask->mm->numa_next_scan  = jiffies + 0x7fffffffffffffffUL;
# endif
            }
#endif /* CONFIG_NUMA_BALANCING */

            LNX_MM_UP_WRITE(pTask->mm);

            if (RT_SUCCESS(rc))
            {
#ifdef VBOX_USE_PAE_HACK
                __free_page(pDummyPage);
#endif
                pMemLnx->Core.pv = pv;
                pMemLnx->Core.u.Mapping.R0Process = R0Process;
                *ppMem = &pMemLnx->Core;
                IPRT_LINUX_RESTORE_EFL_AC();
                return VINF_SUCCESS;
            }

            /*
             * Bail out.
             */
            rtR0MemObjLinuxDoMunmap(pv, cbSub, pTask);
        }
        rtR0MemObjDelete(&pMemLnx->Core);
    }
#ifdef VBOX_USE_PAE_HACK
    __free_page(pDummyPage);
#endif

    IPRT_LINUX_RESTORE_EFL_AC();
    return rc;
}


DECLHIDDEN(int) rtR0MemObjNativeProtect(PRTR0MEMOBJINTERNAL pMem, size_t offSub, size_t cbSub, uint32_t fProt)
{
# ifdef IPRT_USE_ALLOC_VM_AREA_FOR_EXEC
    /*
     * Currently only supported when we've got addresses PTEs from the kernel.
     */
    PRTR0MEMOBJLNX pMemLnx = (PRTR0MEMOBJLNX)pMem;
    if (pMemLnx->pArea && pMemLnx->papPtesForArea)
    {
        pgprot_t const  fPg     = rtR0MemObjLinuxConvertProt(fProt, true /*fKernel*/);
        size_t const    cPages  = (offSub + cbSub) >> PAGE_SHIFT;
        pte_t         **papPtes = pMemLnx->papPtesForArea;
        size_t          i;

        for (i = offSub >> PAGE_SHIFT; i < cPages; i++)
        {
            set_pte(papPtes[i], mk_pte(pMemLnx->apPages[i], fPg));
        }
        preempt_disable();
        __flush_tlb_all();
        preempt_enable();
        return VINF_SUCCESS;
    }
# elif defined(IPRT_USE_APPLY_TO_PAGE_RANGE_FOR_EXEC)
    PRTR0MEMOBJLNX pMemLnx = (PRTR0MEMOBJLNX)pMem;
    if (   pMemLnx->fExecutable
        && pMemLnx->fMappedToRing0)
    {
        LNXAPPLYPGRANGE Args;
        Args.pMemLnx = pMemLnx;
        Args.fPg = rtR0MemObjLinuxConvertProt(fProt, true /*fKernel*/);
        int rcLnx = apply_to_page_range(current->active_mm, (unsigned long)pMemLnx->Core.pv + offSub, cbSub,
                                        rtR0MemObjLinuxApplyPageRange, (void *)&Args);
        if (rcLnx)
            return VERR_NOT_SUPPORTED;

        return VINF_SUCCESS;
    }
# endif

    NOREF(pMem);
    NOREF(offSub);
    NOREF(cbSub);
    NOREF(fProt);
    return VERR_NOT_SUPPORTED;
}


DECLHIDDEN(RTHCPHYS) rtR0MemObjNativeGetPagePhysAddr(PRTR0MEMOBJINTERNAL pMem, size_t iPage)
{
    PRTR0MEMOBJLNX  pMemLnx = (PRTR0MEMOBJLNX)pMem;

    if (pMemLnx->cPages)
        return page_to_phys(pMemLnx->apPages[iPage]);

    switch (pMemLnx->Core.enmType)
    {
        case RTR0MEMOBJTYPE_CONT:
            return pMemLnx->Core.u.Cont.Phys     + (iPage << PAGE_SHIFT);

        case RTR0MEMOBJTYPE_PHYS:
            return pMemLnx->Core.u.Phys.PhysBase + (iPage << PAGE_SHIFT);

            /* the parent knows */
        case RTR0MEMOBJTYPE_MAPPING:
            return rtR0MemObjNativeGetPagePhysAddr(pMemLnx->Core.uRel.Child.pParent, iPage);

            /* cPages > 0 */
        case RTR0MEMOBJTYPE_LOW:
        case RTR0MEMOBJTYPE_LOCK:
        case RTR0MEMOBJTYPE_PHYS_NC:
        case RTR0MEMOBJTYPE_PAGE:
        case RTR0MEMOBJTYPE_LARGE_PAGE:
        default:
            AssertMsgFailed(("%d\n", pMemLnx->Core.enmType));
            RT_FALL_THROUGH();

        case RTR0MEMOBJTYPE_RES_VIRT:
            return NIL_RTHCPHYS;
    }
}


DECLHIDDEN(int) rtR0MemObjNativeZeroInitWithoutMapping(PRTR0MEMOBJINTERNAL pMem)
{
    PRTR0MEMOBJLNX const pMemLnx = (PRTR0MEMOBJLNX)pMem;
    size_t const         cPages  = pMemLnx->Core.cb >> PAGE_SHIFT;
    size_t               iPage;
    /** @todo optimize this. */
    for (iPage = 0; iPage < cPages; iPage++)
    {
        void          *pvPage;

        /* Get the physical address of the page. */
        RTHCPHYS const HCPhys = rtR0MemObjNativeGetPagePhysAddr(&pMemLnx->Core, iPage);
        AssertReturn(HCPhys != NIL_RTHCPHYS, VERR_INTERNAL_ERROR_3);
        Assert(!(HCPhys & PAGE_OFFSET_MASK));

        /* Would've like to use valid_phys_addr_range for this test, but it isn't exported. */
        AssertReturn((HCPhys | PAGE_OFFSET_MASK) < __pa(high_memory), VERR_INTERNAL_ERROR_3);

        /* Map it. */
        pvPage = phys_to_virt(HCPhys);
        AssertPtrReturn(pvPage, VERR_INTERNAL_ERROR_3);

        /* Zero it. */
        RT_BZERO(pvPage, PAGE_SIZE);
    }
    return VINF_SUCCESS;
}

