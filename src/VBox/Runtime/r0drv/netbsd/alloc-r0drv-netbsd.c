/* $Id: alloc-r0drv-netbsd.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Memory Allocation, Ring-0 Driver, NetBSD.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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
 * ---------------------------------------------------------------------------
 *
 * This code is based on:
 *
 * Copyright (c) 2014 Arto Huusko
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "the-netbsd-kernel.h"
#include "internal/iprt.h"
#include <iprt/mem.h>

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/param.h>

#include "r0drv/alloc-r0drv.h"


DECLHIDDEN(int) rtR0MemAllocEx(size_t cb, uint32_t fFlags, PRTMEMHDR *ppHdr)
{
    size_t      cbAllocated = cb;
    PRTMEMHDR   pHdr        = NULL;

    if (fFlags & RTMEMHDR_FLAG_ZEROED)
        pHdr = kmem_zalloc(cb + sizeof(RTMEMHDR), KM_NOSLEEP);
    else
        pHdr = kmem_alloc(cb + sizeof(RTMEMHDR), KM_NOSLEEP);

    if (RT_UNLIKELY(!pHdr))
        return VERR_NO_MEMORY;

    pHdr->u32Magic   = RTMEMHDR_MAGIC;
    pHdr->fFlags     = fFlags;
    pHdr->cb         = cbAllocated;
    pHdr->cbReq      = cb;

    *ppHdr = pHdr;
    return VINF_SUCCESS;
}


DECLHIDDEN(void) rtR0MemFree(PRTMEMHDR pHdr)
{
    pHdr->u32Magic += 1;

    kmem_free(pHdr, pHdr->cb + sizeof(RTMEMHDR));
}

RTR0DECL(void) RTMemContFree(void *pv, size_t cb)
{
    if (pv)
    {
        cb = round_page(cb);

        paddr_t pa;
        pmap_extract(pmap_kernel(), (vaddr_t)pv, &pa);

        /*
         * Reconstruct pglist to free the physical pages
         */
        struct pglist rlist;
        TAILQ_INIT(&rlist);

        for (paddr_t pa2 = pa ; pa2 < pa + cb ; pa2 += PAGE_SIZE) {
            struct vm_page *page = PHYS_TO_VM_PAGE(pa2);
            TAILQ_INSERT_TAIL(&rlist, page, pageq.queue);
        }

        /* Unmap */
        pmap_kremove((vaddr_t)pv, cb);

        /* Free the virtual space */
        uvm_km_free(kernel_map, (vaddr_t)pv, cb, UVM_KMF_VAONLY);

        /* Free the physical pages */
        uvm_pglistfree(&rlist);
    }
}

RTR0DECL(void *) RTMemContAlloc(PRTCCPHYS pPhys, size_t cb)
{
    /*
     * Validate input.
     */
    AssertPtr(pPhys);
    Assert(cb > 0);

    cb = round_page(cb);

    vaddr_t virt = uvm_km_alloc(kernel_map, cb, 0,
            UVM_KMF_VAONLY | UVM_KMF_WAITVA | UVM_KMF_CANFAIL);
    if (virt == 0)
        return NULL;

    struct pglist rlist;

    if (uvm_pglistalloc(cb, 0, (paddr_t)0xFFFFFFFF,
            PAGE_SIZE, 0, &rlist, 1, 1) != 0)
    {
        uvm_km_free(kernel_map, virt, cb, UVM_KMF_VAONLY);
        return NULL;
    }

    struct vm_page *page;
    vaddr_t virt2 = virt;
    TAILQ_FOREACH(page, &rlist, pageq.queue)
    {
        pmap_kenter_pa(virt2, VM_PAGE_TO_PHYS(page),
                VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE, 0);
        virt2 += PAGE_SIZE;
    }

    page = TAILQ_FIRST(&rlist);
    *pPhys = VM_PAGE_TO_PHYS(page);

    return (void *)virt;
}
