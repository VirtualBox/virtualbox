/* $Id: alloc-r0drv-solaris.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Memory Allocation, Ring-0 Driver, Solaris.
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
#include <iprt/mem.h>

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/log.h>
#include <iprt/param.h>
#include <iprt/thread.h>
#include "r0drv/alloc-r0drv.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
static ddi_dma_attr_t s_rtR0SolDmaAttr =
{
    DMA_ATTR_V0,                /* Version Number */
    (uint64_t)0,                /* Lower limit */
    (uint64_t)0,                /* High limit */
    (uint64_t)0xffffffff,       /* Counter limit */
    (uint64_t)PAGESIZE,         /* Alignment */
    (uint64_t)PAGESIZE,         /* Burst size */
    (uint64_t)PAGESIZE,         /* Effective DMA size */
    (uint64_t)0xffffffff,       /* Max DMA xfer size */
    (uint64_t)0xffffffff,       /* Segment boundary */
    1,                          /* Scatter-gather list length (1 for contiguous) */
    1,                          /* Device granularity */
    0                           /* Bus-specific flags */
};

extern void *contig_alloc(size_t cb, ddi_dma_attr_t *pDmaAttr, size_t uAlign, int fCanSleep);


/**
 * OS specific allocation function.
 */
DECLHIDDEN(int) rtR0MemAllocEx(size_t cb, uint32_t fFlags, PRTMEMHDR *ppHdr)
{
    size_t      cbAllocated = cb;
    PRTMEMHDR   pHdr;
    unsigned fKmFlags = fFlags & RTMEMHDR_FLAG_ANY_CTX_ALLOC ? KM_NOSLEEP : KM_SLEEP;
    if (fFlags & RTMEMHDR_FLAG_ZEROED)
        pHdr = (PRTMEMHDR)kmem_zalloc(cb + sizeof(*pHdr), fKmFlags);
    else
        pHdr = (PRTMEMHDR)kmem_alloc(cb + sizeof(*pHdr), fKmFlags);
    if (RT_UNLIKELY(!pHdr))
    {
        LogRel(("rtMemAllocEx(%u, %#x) failed\n", (unsigned)cb + sizeof(*pHdr), fFlags));
        return VERR_NO_MEMORY;
    }

    pHdr->u32Magic  = RTMEMHDR_MAGIC;
    pHdr->fFlags    = fFlags;
    pHdr->cb        = cbAllocated;
    pHdr->cbReq     = cb;

    *ppHdr = pHdr;
    return VINF_SUCCESS;
}


/**
 * OS specific free function.
 */
DECLHIDDEN(void) rtR0MemFree(PRTMEMHDR pHdr)
{
    pHdr->u32Magic += 1;
    kmem_free(pHdr, pHdr->cb + sizeof(*pHdr));
}


/**
 * Allocates physical memory which satisfy the given constraints.
 *
 * @param   uPhysHi        The upper physical address limit (inclusive).
 * @param   puPhys         Where to store the physical address of the allocated
 *                         memory. Optional, can be NULL.
 * @param   cb             Size of allocation.
 * @param   uAlignment     Alignment.
 * @param   fContig        Whether the memory must be physically contiguous or
 *                         not.
 * @returns Virtual address of allocated memory block or NULL if allocation
 *          failed.
 */
DECLHIDDEN(void *) rtR0SolMemAlloc(uint64_t uPhysHi, uint64_t *puPhys, size_t cb, uint64_t uAlignment, bool fContig)
{
    if ((cb & PAGEOFFSET) != 0)
        return NULL;

    size_t cPages = (cb + PAGESIZE - 1) >> PAGESHIFT;
    if (!cPages)
        return NULL;

    ddi_dma_attr_t DmaAttr = s_rtR0SolDmaAttr;
    DmaAttr.dma_attr_addr_hi    = uPhysHi;
    DmaAttr.dma_attr_align      = uAlignment;
    if (!fContig)
        DmaAttr.dma_attr_sgllen = cPages > INT_MAX ? INT_MAX - 1 : cPages;
    else
        AssertRelease(DmaAttr.dma_attr_sgllen == 1);

    void *pvMem = contig_alloc(cb, &DmaAttr, PAGESIZE, 1 /* can sleep */);
    if (!pvMem)
    {
        LogRel(("rtR0SolMemAlloc failed. cb=%u Align=%u fContig=%d\n", (unsigned)cb, (unsigned)uAlignment, fContig));
        return NULL;
    }

    pfn_t PageFrameNum = hat_getpfnum(kas.a_hat, (caddr_t)pvMem);
    AssertRelease(PageFrameNum != PFN_INVALID);
    if (puPhys)
        *puPhys = (uint64_t)PageFrameNum << PAGESHIFT;

    return pvMem;
}


/**
 * Frees memory allocated using rtR0SolMemAlloc().
 *
 * @param   pv          The memory to free.
 * @param   cb          Size of the memory block
 */
DECLHIDDEN(void) rtR0SolMemFree(void *pv, size_t cb)
{
    if (RT_LIKELY(pv))
        g_pfnrtR0Sol_contig_free(pv, cb);
}


RTR0DECL(void *) RTMemContAlloc(PRTCCPHYS pPhys, size_t cb)
{
    AssertPtrReturn(pPhys, NULL);
    AssertReturn(cb > 0, NULL);
    RT_ASSERT_PREEMPTIBLE();

    /* Allocate physically contiguous (< 4GB) page-aligned memory. */
    uint64_t uPhys;
    void *pvMem = rtR0SolMemAlloc((uint64_t)_4G - 1, &uPhys, cb, PAGESIZE, true /* fContig */);
    if (RT_UNLIKELY(!pvMem))
    {
        LogRel(("RTMemContAlloc failed to allocate %u bytes\n", cb));
        return NULL;
    }

    Assert(uPhys < _4G);
    *pPhys = uPhys;
    return pvMem;
}


RTR0DECL(void) RTMemContFree(void *pv, size_t cb)
{
    RT_ASSERT_PREEMPTIBLE();
    rtR0SolMemFree(pv, cb);
}

