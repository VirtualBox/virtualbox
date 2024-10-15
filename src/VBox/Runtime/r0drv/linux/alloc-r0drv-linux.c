/* $Id: alloc-r0drv-linux.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Memory Allocation, Ring-0 Driver, Linux.
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
#include "internal/iprt.h"
#include <iprt/mem.h>

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include "r0drv/alloc-r0drv.h"

#include "internal/initterm.h"



/**
 * OS specific allocation function.
 */
DECLHIDDEN(int) rtR0MemAllocEx(size_t cb, uint32_t fFlags, PRTMEMHDR *ppHdr)
{
    PRTMEMHDR pHdr;
    IPRT_LINUX_SAVE_EFL_AC();

    /*
     * Allocate.
     */
    if (
#if 1 /* vmalloc has serious performance issues, avoid it. */
           cb <= PAGE_SIZE*16 - sizeof(*pHdr)
#else
           cb <= PAGE_SIZE
#endif
        || (fFlags & RTMEMHDR_FLAG_ANY_CTX)
       )
    {
        fFlags |= RTMEMHDR_FLAG_KMALLOC;
        pHdr = kmalloc(cb + sizeof(*pHdr),
                       fFlags & RTMEMHDR_FLAG_ANY_CTX_ALLOC ? GFP_ATOMIC | __GFP_NOWARN : GFP_KERNEL | __GFP_NOWARN);
        if (RT_UNLIKELY(   !pHdr
                        && cb > PAGE_SIZE
                        && !(fFlags & RTMEMHDR_FLAG_ANY_CTX) ))
        {
            fFlags &= ~RTMEMHDR_FLAG_KMALLOC;
            pHdr = vmalloc(cb + sizeof(*pHdr));
        }
    }
    else
        pHdr = vmalloc(cb + sizeof(*pHdr));
    if (RT_LIKELY(pHdr))
    {
        /*
         * Initialize.
         */
        pHdr->u32Magic  = RTMEMHDR_MAGIC;
        pHdr->fFlags    = fFlags;
        pHdr->cb        = cb;
        pHdr->cbReq     = cb;

        *ppHdr = pHdr;
        IPRT_LINUX_RESTORE_EFL_AC();
        return VINF_SUCCESS;
    }

    IPRT_LINUX_RESTORE_EFL_AC();
    return VERR_NO_MEMORY;
}


/**
 * OS specific free function.
 */
DECLHIDDEN(void) rtR0MemFree(PRTMEMHDR pHdr)
{
    IPRT_LINUX_SAVE_EFL_AC();

    pHdr->u32Magic += 1;
    if (pHdr->fFlags & RTMEMHDR_FLAG_KMALLOC)
        kfree(pHdr);
    else
        vfree(pHdr);

    IPRT_LINUX_RESTORE_EFL_AC();
}



/**
 * Compute order. Some functions allocate 2^order pages.
 *
 * @returns order.
 * @param   cPages      Number of pages.
 */
static int CalcPowerOf2Order(unsigned long cPages)
{
    int             iOrder;
    unsigned long   cTmp;

    for (iOrder = 0, cTmp = cPages; cTmp >>= 1; ++iOrder)
        ;
    if (cPages & ~(1 << iOrder))
        ++iOrder;

    return iOrder;
}


/**
 * Allocates physical contiguous memory (below 4GB).
 * The allocation is page aligned and the content is undefined.
 *
 * @returns Pointer to the memory block. This is page aligned.
 * @param   pPhys   Where to store the physical address.
 * @param   cb      The allocation size in bytes. This is always
 *                  rounded up to PAGE_SIZE.
 */
RTR0DECL(void *) RTMemContAlloc(PRTCCPHYS pPhys, size_t cb)
{
    int             cOrder;
    unsigned        cPages;
    void           *pvRet;
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    struct page    *paPages;
#endif
    IPRT_LINUX_SAVE_EFL_AC();

    /*
     * validate input.
     */
    AssertPtr(pPhys);
    Assert(cb > 0);

    /*
     * Allocate page pointer array.
     */
    cb = RT_ALIGN_Z(cb, PAGE_SIZE);
    cPages = cb >> PAGE_SHIFT;
    cOrder = CalcPowerOf2Order(cPages);
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# if (defined(RT_ARCH_AMD64) || defined(CONFIG_X86_PAE)) && defined(GFP_DMA32)
    /* ZONE_DMA32: 0-4GB */
    paPages = alloc_pages(GFP_DMA32 | __GFP_NOWARN, cOrder);
    if (!paPages)
# endif
# ifdef RT_ARCH_AMD64
        /* ZONE_DMA; 0-16MB */
        paPages = alloc_pages(GFP_DMA | __GFP_NOWARN, cOrder);
# else
        /* ZONE_NORMAL: 0-896MB */
        paPages = alloc_pages(GFP_USER | __GFP_NOWARN, cOrder);
# endif
    if (paPages)
    {
        /*
         * Reserve the pages and mark them executable.
         */
        unsigned iPage;
        for (iPage = 0; iPage < cPages; iPage++)
        {
            Assert(!PageHighMem(&paPages[iPage]));
            if (iPage + 1 < cPages)
            {
                AssertMsg(          (uintptr_t)phys_to_virt(page_to_phys(&paPages[iPage])) + PAGE_SIZE
                                ==  (uintptr_t)phys_to_virt(page_to_phys(&paPages[iPage + 1]))
                          &&        page_to_phys(&paPages[iPage]) + PAGE_SIZE
                                ==  page_to_phys(&paPages[iPage + 1]),
                          ("iPage=%i cPages=%u [0]=%#llx,%p [1]=%#llx,%p\n", iPage, cPages,
                           (long long)page_to_phys(&paPages[iPage]),     phys_to_virt(page_to_phys(&paPages[iPage])),
                           (long long)page_to_phys(&paPages[iPage + 1]), phys_to_virt(page_to_phys(&paPages[iPage + 1])) ));
            }

            SetPageReserved(&paPages[iPage]);
        }
        *pPhys = page_to_phys(paPages);
        pvRet = phys_to_virt(page_to_phys(paPages));
    }
    else
        pvRet = NULL;
#else
    pvRet = (void *)__get_free_pages(GFP_DMA32 | __GFP_NOWARN, cOrder);
    if (pvRet)
        *pPhys = virt_to_phys(pvRet);
#endif

    IPRT_LINUX_RESTORE_EFL_AC();
    return pvRet;
}
RT_EXPORT_SYMBOL(RTMemContAlloc);


/**
 * Frees memory allocated using RTMemContAlloc().
 *
 * @param   pv      Pointer to return from RTMemContAlloc().
 * @param   cb      The cb parameter passed to RTMemContAlloc().
 */
RTR0DECL(void) RTMemContFree(void *pv, size_t cb)
{
    if (pv)
    {
        int             cOrder;
        unsigned        cPages;
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
        unsigned        iPage;
        struct page    *paPages;
#endif
        IPRT_LINUX_SAVE_EFL_AC();

        /* validate */
        AssertMsg(!((uintptr_t)pv & PAGE_OFFSET_MASK), ("pv=%p\n", pv));
        Assert(cb > 0);

        /* calc order and get pages */
        cb = RT_ALIGN_Z(cb, PAGE_SIZE);
        cPages = cb >> PAGE_SHIFT;
        cOrder = CalcPowerOf2Order(cPages);
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
        paPages = virt_to_page(pv);

        /*
         * Restore page attributes freeing the pages.
         */
        for (iPage = 0; iPage < cPages; iPage++)
        {
            ClearPageReserved(&paPages[iPage]);
        }
        __free_pages(paPages, cOrder);
#else
        free_pages((uintptr_t)pv, cOrder);
#endif
        IPRT_LINUX_RESTORE_EFL_AC();
    }
}
RT_EXPORT_SYMBOL(RTMemContFree);

