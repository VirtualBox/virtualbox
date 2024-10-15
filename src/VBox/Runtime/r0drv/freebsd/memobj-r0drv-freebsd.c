/* $Id: memobj-r0drv-freebsd.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Ring-0 Memory Objects, FreeBSD.
 */

/*
 * Contributed by knut st. osmundsen, Andriy Gapon.
 *
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
 * --------------------------------------------------------------------
 *
 * This code is based on:
 *
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
 * Copyright (c) 2011 Andriy Gapon <avg@FreeBSD.org>
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
#include "the-freebsd-kernel.h"

#include <iprt/memobj.h>
#include <iprt/mem.h>
#include <iprt/err.h>
#include <iprt/assert.h>
#include <iprt/log.h>
#include <iprt/param.h>
#include <iprt/process.h>
#include <iprt/string.h>
#include "internal/memobj.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The FreeBSD version of the memory object structure.
 */
typedef struct RTR0MEMOBJFREEBSD
{
    /** The core structure. */
    RTR0MEMOBJINTERNAL  Core;
    /** The VM object associated with the allocation. */
    vm_object_t         pObject;
} RTR0MEMOBJFREEBSD, *PRTR0MEMOBJFREEBSD;


MALLOC_DEFINE(M_IPRTMOBJ, "iprtmobj", "IPRT - R0MemObj");


/**
 * Gets the virtual memory map the specified object is mapped into.
 *
 * @returns VM map handle on success, NULL if no map.
 * @param   pMem                The memory object.
 */
static vm_map_t rtR0MemObjFreeBSDGetMap(PRTR0MEMOBJINTERNAL pMem)
{
    switch (pMem->enmType)
    {
        case RTR0MEMOBJTYPE_PAGE:
        case RTR0MEMOBJTYPE_LOW:
        case RTR0MEMOBJTYPE_CONT:
            return kernel_map;

        case RTR0MEMOBJTYPE_PHYS:
        case RTR0MEMOBJTYPE_PHYS_NC:
            return NULL; /* pretend these have no mapping atm. */

        case RTR0MEMOBJTYPE_LOCK:
            return pMem->u.Lock.R0Process == NIL_RTR0PROCESS
                ? kernel_map
                : &((struct proc *)pMem->u.Lock.R0Process)->p_vmspace->vm_map;

        case RTR0MEMOBJTYPE_RES_VIRT:
            return pMem->u.ResVirt.R0Process == NIL_RTR0PROCESS
                ? kernel_map
                : &((struct proc *)pMem->u.ResVirt.R0Process)->p_vmspace->vm_map;

        case RTR0MEMOBJTYPE_MAPPING:
            return pMem->u.Mapping.R0Process == NIL_RTR0PROCESS
                ? kernel_map
                : &((struct proc *)pMem->u.Mapping.R0Process)->p_vmspace->vm_map;

        default:
            return NULL;
    }
}


DECLHIDDEN(int) rtR0MemObjNativeFree(RTR0MEMOBJ pMem)
{
    PRTR0MEMOBJFREEBSD pMemFreeBSD = (PRTR0MEMOBJFREEBSD)pMem;
    int rc;

    switch (pMemFreeBSD->Core.enmType)
    {
        case RTR0MEMOBJTYPE_PAGE:
        case RTR0MEMOBJTYPE_LOW:
        case RTR0MEMOBJTYPE_CONT:
            rc = vm_map_remove(kernel_map,
                                (vm_offset_t)pMemFreeBSD->Core.pv,
                                (vm_offset_t)pMemFreeBSD->Core.pv + pMemFreeBSD->Core.cb);
            AssertMsg(rc == KERN_SUCCESS, ("%#x", rc));
            break;

        case RTR0MEMOBJTYPE_LOCK:
        {
            vm_map_t pMap = kernel_map;

            if (pMemFreeBSD->Core.u.Lock.R0Process != NIL_RTR0PROCESS)
                pMap = &((struct proc *)pMemFreeBSD->Core.u.Lock.R0Process)->p_vmspace->vm_map;

            rc = vm_map_unwire(pMap,
                               (vm_offset_t)pMemFreeBSD->Core.pv,
                               (vm_offset_t)pMemFreeBSD->Core.pv + pMemFreeBSD->Core.cb,
                               VM_MAP_WIRE_SYSTEM | VM_MAP_WIRE_NOHOLES);
            AssertMsg(rc == KERN_SUCCESS, ("%#x", rc));
            break;
        }

        case RTR0MEMOBJTYPE_RES_VIRT:
        {
            vm_map_t pMap = kernel_map;
            if (pMemFreeBSD->Core.u.ResVirt.R0Process != NIL_RTR0PROCESS)
                pMap = &((struct proc *)pMemFreeBSD->Core.u.ResVirt.R0Process)->p_vmspace->vm_map;
            rc = vm_map_remove(pMap,
                               (vm_offset_t)pMemFreeBSD->Core.pv,
                               (vm_offset_t)pMemFreeBSD->Core.pv + pMemFreeBSD->Core.cb);
            AssertMsg(rc == KERN_SUCCESS, ("%#x", rc));
            break;
        }

        case RTR0MEMOBJTYPE_MAPPING:
        {
            vm_map_t pMap = kernel_map;

            if (pMemFreeBSD->Core.u.Mapping.R0Process != NIL_RTR0PROCESS)
                pMap = &((struct proc *)pMemFreeBSD->Core.u.Mapping.R0Process)->p_vmspace->vm_map;
            rc = vm_map_remove(pMap,
                               (vm_offset_t)pMemFreeBSD->Core.pv,
                               (vm_offset_t)pMemFreeBSD->Core.pv + pMemFreeBSD->Core.cb);
            AssertMsg(rc == KERN_SUCCESS, ("%#x", rc));
            break;
        }

        case RTR0MEMOBJTYPE_PHYS:
        case RTR0MEMOBJTYPE_PHYS_NC:
        {
            VM_OBJECT_WLOCK(pMemFreeBSD->pObject);
            vm_page_t pPage = vm_page_find_least(pMemFreeBSD->pObject, 0);
#if __FreeBSD_version < 1000000
            vm_page_lock_queues();
#endif
            for (vm_page_t pPage = vm_page_find_least(pMemFreeBSD->pObject, 0);
                 pPage != NULL;
                 pPage = vm_page_next(pPage))
            {
                vm_page_unwire(pPage, 0);
            }
#if __FreeBSD_version < 1000000
            vm_page_unlock_queues();
#endif
            VM_OBJECT_WUNLOCK(pMemFreeBSD->pObject);
            vm_object_deallocate(pMemFreeBSD->pObject);
            break;
        }

        default:
            AssertMsgFailed(("enmType=%d\n", pMemFreeBSD->Core.enmType));
            return VERR_INTERNAL_ERROR;
    }

    return VINF_SUCCESS;
}


static vm_page_t rtR0MemObjFreeBSDContigPhysAllocHelper(vm_object_t pObject, vm_pindex_t iPIndex,
                                                        u_long cPages, vm_paddr_t VmPhysAddrHigh,
                                                        u_long uAlignment, bool fWire)
{
    vm_page_t pPages;
    int cTries = 0;

#if __FreeBSD_version > 1000000
    int fFlags = VM_ALLOC_INTERRUPT | VM_ALLOC_NOBUSY;
    if (fWire)
        fFlags |= VM_ALLOC_WIRED;

    while (cTries <= 1)
    {
        VM_OBJECT_WLOCK(pObject);
        pPages = vm_page_alloc_contig(pObject, iPIndex, fFlags, cPages, 0, VmPhysAddrHigh, uAlignment, 0, VM_MEMATTR_DEFAULT);
        VM_OBJECT_WUNLOCK(pObject);
        if (pPages)
            break;
#if __FreeBSD_version >= 1100092
        if (!vm_page_reclaim_contig(cTries, cPages, 0, VmPhysAddrHigh, PAGE_SIZE, 0))
            break;
#else
        vm_pageout_grow_cache(cTries, 0, VmPhysAddrHigh);
#endif
        cTries++;
    }

    return pPages;
#else
    while (cTries <= 1)
    {
        pPages = vm_phys_alloc_contig(cPages, 0, VmPhysAddrHigh, uAlignment, 0);
        if (pPages)
            break;
        vm_contig_grow_cache(cTries, 0, VmPhysAddrHigh);
        cTries++;
    }

    if (!pPages)
        return pPages;
    VM_OBJECT_WLOCK(pObject);
    for (vm_pindex_t iPage = 0; iPage < cPages; iPage++)
    {
        vm_page_t pPage = pPages + iPage;
        vm_page_insert(pPage, pObject, iPIndex + iPage);
        pPage->valid = VM_PAGE_BITS_ALL;
        if (fWire)
        {
            pPage->wire_count = 1;
            atomic_add_int(&cnt.v_wire_count, 1);
        }
    }
    VM_OBJECT_WUNLOCK(pObject);
    return pPages;
#endif
}

static int rtR0MemObjFreeBSDPhysAllocHelper(vm_object_t pObject, u_long cPages,
                                            vm_paddr_t VmPhysAddrHigh, u_long uAlignment,
                                            bool fContiguous, bool fWire, int rcNoMem)
{
    if (fContiguous)
    {
        if (rtR0MemObjFreeBSDContigPhysAllocHelper(pObject, 0, cPages, VmPhysAddrHigh, uAlignment, fWire) != NULL)
            return VINF_SUCCESS;
        return rcNoMem;
    }

    for (vm_pindex_t iPage = 0; iPage < cPages; iPage++)
    {
        vm_page_t pPage = rtR0MemObjFreeBSDContigPhysAllocHelper(pObject, iPage, 1, VmPhysAddrHigh, uAlignment, fWire);
        if (pPage)
        { /* likely */ }
        else
        {
            /* Free all allocated pages */
            VM_OBJECT_WLOCK(pObject);
            while (iPage-- > 0)
            {
                pPage = vm_page_lookup(pObject, iPage);
#if __FreeBSD_version < 1000000
                vm_page_lock_queues();
#endif
                if (fWire)
                    vm_page_unwire(pPage, 0);
                vm_page_free(pPage);
#if __FreeBSD_version < 1000000
                vm_page_unlock_queues();
#endif
            }
            VM_OBJECT_WUNLOCK(pObject);
            return rcNoMem;
        }
    }
    return VINF_SUCCESS;
}

static int rtR0MemObjFreeBSDAllocHelper(PRTR0MEMOBJFREEBSD pMemFreeBSD, bool fExecutable,
                                        vm_paddr_t VmPhysAddrHigh, bool fContiguous, int rcNoMem)
{
    vm_offset_t MapAddress = vm_map_min(kernel_map);
    size_t      cPages = atop(pMemFreeBSD->Core.cb);
    int         rc;

    pMemFreeBSD->pObject = vm_object_allocate(OBJT_PHYS, cPages);

    /* No additional object reference for auto-deallocation upon unmapping. */
#if __FreeBSD_version >= 1000055
    rc = vm_map_find(kernel_map, pMemFreeBSD->pObject, 0,
                     &MapAddress, pMemFreeBSD->Core.cb, 0, VMFS_ANY_SPACE,
                     fExecutable ? VM_PROT_ALL : VM_PROT_RW, VM_PROT_ALL, 0);
#else
    rc = vm_map_find(kernel_map, pMemFreeBSD->pObject, 0,
                     &MapAddress, pMemFreeBSD->Core.cb, VMFS_ANY_SPACE,
                     fExecutable ? VM_PROT_ALL : VM_PROT_RW, VM_PROT_ALL, 0);
#endif

    if (rc == KERN_SUCCESS)
    {
        rc = rtR0MemObjFreeBSDPhysAllocHelper(pMemFreeBSD->pObject, cPages, VmPhysAddrHigh, PAGE_SIZE,
                                              fContiguous, false /*fWire*/, rcNoMem);
        if (RT_SUCCESS(rc))
        {
            vm_map_wire(kernel_map, MapAddress, MapAddress + pMemFreeBSD->Core.cb, VM_MAP_WIRE_SYSTEM | VM_MAP_WIRE_NOHOLES);

            /* Store start address */
            pMemFreeBSD->Core.pv      = (void *)MapAddress;
            pMemFreeBSD->Core.fFlags |= RTR0MEMOBJ_FLAGS_UNINITIALIZED_AT_ALLOC;
            return VINF_SUCCESS;
        }

        vm_map_remove(kernel_map, MapAddress, MapAddress + pMemFreeBSD->Core.cb);
    }
    else
    {
        rc = rcNoMem; /** @todo fix translation (borrow from darwin) */
        vm_object_deallocate(pMemFreeBSD->pObject);
    }

    rtR0MemObjDelete(&pMemFreeBSD->Core);
    return rc;
}


DECLHIDDEN(int) rtR0MemObjNativeAllocPage(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, bool fExecutable, const char *pszTag)
{
    PRTR0MEMOBJFREEBSD pMemFreeBSD = (PRTR0MEMOBJFREEBSD)rtR0MemObjNew(sizeof(*pMemFreeBSD), RTR0MEMOBJTYPE_PAGE,
                                                                       NULL, cb, pszTag);
    if (pMemFreeBSD)
    {
        int rc = rtR0MemObjFreeBSDAllocHelper(pMemFreeBSD, fExecutable, ~(vm_paddr_t)0, false /*fContiguous*/, VERR_NO_MEMORY);
        if (RT_SUCCESS(rc))
            *ppMem = &pMemFreeBSD->Core;
        else
            rtR0MemObjDelete(&pMemFreeBSD->Core);
        return rc;
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
    PRTR0MEMOBJFREEBSD pMemFreeBSD = (PRTR0MEMOBJFREEBSD)rtR0MemObjNew(sizeof(*pMemFreeBSD), RTR0MEMOBJTYPE_LOW, NULL, cb, pszTag);
    if (pMemFreeBSD)
    {
        int rc = rtR0MemObjFreeBSDAllocHelper(pMemFreeBSD, fExecutable, _4G - 1, false /*fContiguous*/, VERR_NO_LOW_MEMORY);
        if (RT_SUCCESS(rc))
            *ppMem = &pMemFreeBSD->Core;
        else
            rtR0MemObjDelete(&pMemFreeBSD->Core);
        return rc;
    }
    return VERR_NO_MEMORY;
}


DECLHIDDEN(int) rtR0MemObjNativeAllocCont(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, RTHCPHYS PhysHigest,
                                          bool fExecutable, const char *pszTag)
{
    PRTR0MEMOBJFREEBSD pMemFreeBSD = (PRTR0MEMOBJFREEBSD)rtR0MemObjNew(sizeof(*pMemFreeBSD), RTR0MEMOBJTYPE_CONT,
                                                                       NULL, cb, pszTag);
    if (pMemFreeBSD)
    {
        int rc = rtR0MemObjFreeBSDAllocHelper(pMemFreeBSD, fExecutable, PhysHigest, true /*fContiguous*/, VERR_NO_CONT_MEMORY);
        if (RT_SUCCESS(rc))
        {
            pMemFreeBSD->Core.u.Cont.Phys = vtophys(pMemFreeBSD->Core.pv);
            *ppMem = &pMemFreeBSD->Core;
        }
        else
            rtR0MemObjDelete(&pMemFreeBSD->Core);
        return rc;
    }
    return VERR_NO_MEMORY;
}


static int rtR0MemObjFreeBSDAllocPhysPages(PPRTR0MEMOBJINTERNAL ppMem, RTR0MEMOBJTYPE enmType, size_t cb,  RTHCPHYS PhysHighest,
                                           size_t uAlignment, bool fContiguous, int rcNoMem, const char *pszTag)
{
    /* create the object. */
    PRTR0MEMOBJFREEBSD pMemFreeBSD = (PRTR0MEMOBJFREEBSD)rtR0MemObjNew(sizeof(*pMemFreeBSD), enmType, NULL, cb, pszTag);
    if (pMemFreeBSD)
    {
        vm_paddr_t const VmPhysAddrHigh = PhysHighest != NIL_RTHCPHYS ? PhysHighest : ~(vm_paddr_t)0;
        u_long const     cPages         = atop(cb);

        pMemFreeBSD->pObject = vm_object_allocate(OBJT_PHYS, cPages);

        int rc = rtR0MemObjFreeBSDPhysAllocHelper(pMemFreeBSD->pObject, cPages, VmPhysAddrHigh,
                                                  uAlignment, fContiguous, true, rcNoMem);
        if (RT_SUCCESS(rc))
        {
            if (fContiguous)
            {
                Assert(enmType == RTR0MEMOBJTYPE_PHYS);
                VM_OBJECT_WLOCK(pMemFreeBSD->pObject);
                pMemFreeBSD->Core.u.Phys.PhysBase = VM_PAGE_TO_PHYS(vm_page_find_least(pMemFreeBSD->pObject, 0));
                VM_OBJECT_WUNLOCK(pMemFreeBSD->pObject);
                pMemFreeBSD->Core.u.Phys.fAllocated = true;
            }

            pMemFreeBSD->Core.fFlags |= RTR0MEMOBJ_FLAGS_UNINITIALIZED_AT_ALLOC;
            *ppMem = &pMemFreeBSD->Core;
        }
        else
        {
            vm_object_deallocate(pMemFreeBSD->pObject);
            rtR0MemObjDelete(&pMemFreeBSD->Core);
        }
        return rc;
    }
    return VERR_NO_MEMORY;
}


DECLHIDDEN(int) rtR0MemObjNativeAllocPhys(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, RTHCPHYS PhysHighest, size_t uAlignment,
                                          const char *pszTag)
{
    return rtR0MemObjFreeBSDAllocPhysPages(ppMem, RTR0MEMOBJTYPE_PHYS, cb, PhysHighest, uAlignment, true, VERR_NO_MEMORY, pszTag);
}


DECLHIDDEN(int) rtR0MemObjNativeAllocPhysNC(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, RTHCPHYS PhysHighest, const char *pszTag)
{
    return rtR0MemObjFreeBSDAllocPhysPages(ppMem, RTR0MEMOBJTYPE_PHYS_NC, cb, PhysHighest, PAGE_SIZE, false,
                                           VERR_NO_PHYS_MEMORY, pszTag);
}


DECLHIDDEN(int) rtR0MemObjNativeEnterPhys(PPRTR0MEMOBJINTERNAL ppMem, RTHCPHYS Phys, size_t cb, uint32_t uCachePolicy,
                                          const char *pszTag)
{
    AssertReturn(uCachePolicy == RTMEM_CACHE_POLICY_DONT_CARE, VERR_NOT_SUPPORTED);

    /* create the object. */
    PRTR0MEMOBJFREEBSD pMemFreeBSD = (PRTR0MEMOBJFREEBSD)rtR0MemObjNew(sizeof(*pMemFreeBSD), RTR0MEMOBJTYPE_PHYS,
                                                                       NULL, cb, pszTag);
    if (pMemFreeBSD)
    {
        /* there is no allocation here, it needs to be mapped somewhere first. */
        pMemFreeBSD->Core.u.Phys.fAllocated = false;
        pMemFreeBSD->Core.u.Phys.PhysBase = Phys;
        pMemFreeBSD->Core.u.Phys.uCachePolicy = uCachePolicy;
        *ppMem = &pMemFreeBSD->Core;
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;
}


/**
 * Worker locking the memory in either kernel or user maps.
 */
static int rtR0MemObjNativeLockInMap(PPRTR0MEMOBJINTERNAL ppMem, vm_map_t pVmMap,
                                     vm_offset_t AddrStart, size_t cb, uint32_t fAccess,
                                     RTR0PROCESS R0Process, int fFlags, const char *pszTag)
{
    int rc;
    NOREF(fAccess);

    /* create the object. */
    PRTR0MEMOBJFREEBSD pMemFreeBSD = (PRTR0MEMOBJFREEBSD)rtR0MemObjNew(sizeof(*pMemFreeBSD), RTR0MEMOBJTYPE_LOCK,
                                                                       (void *)AddrStart, cb, pszTag);
    if (!pMemFreeBSD)
        return VERR_NO_MEMORY;

    /*
     * We could've used vslock here, but we don't wish to be subject to
     * resource usage restrictions, so we'll call vm_map_wire directly.
     */
    rc = vm_map_wire(pVmMap,                                         /* the map */
                     AddrStart,                                      /* start */
                     AddrStart + cb,                                 /* end */
                     fFlags);                                        /* flags */
    if (rc == KERN_SUCCESS)
    {
        pMemFreeBSD->Core.u.Lock.R0Process = R0Process;
        *ppMem = &pMemFreeBSD->Core;
        return VINF_SUCCESS;
    }
    rtR0MemObjDelete(&pMemFreeBSD->Core);
    return VERR_NO_MEMORY;/** @todo fix mach -> vbox error conversion for freebsd. */
}


DECLHIDDEN(int) rtR0MemObjNativeLockUser(PPRTR0MEMOBJINTERNAL ppMem, RTR3PTR R3Ptr, size_t cb, uint32_t fAccess,
                                         RTR0PROCESS R0Process, const char *pszTag)
{
    return rtR0MemObjNativeLockInMap(ppMem,
                                     &((struct proc *)R0Process)->p_vmspace->vm_map,
                                     (vm_offset_t)R3Ptr,
                                     cb,
                                     fAccess,
                                     R0Process,
                                     VM_MAP_WIRE_USER | VM_MAP_WIRE_NOHOLES,
                                     pszTag);
}


DECLHIDDEN(int) rtR0MemObjNativeLockKernel(PPRTR0MEMOBJINTERNAL ppMem, void *pv, size_t cb, uint32_t fAccess, const char *pszTag)
{
    return rtR0MemObjNativeLockInMap(ppMem,
                                     kernel_map,
                                     (vm_offset_t)pv,
                                     cb,
                                     fAccess,
                                     NIL_RTR0PROCESS,
                                     VM_MAP_WIRE_SYSTEM | VM_MAP_WIRE_NOHOLES,
                                     pszTag);
}


/**
 * Worker for the two virtual address space reservers.
 *
 * We're leaning on the examples provided by mmap and vm_mmap in vm_mmap.c here.
 */
static int rtR0MemObjNativeReserveInMap(PPRTR0MEMOBJINTERNAL ppMem, void *pvFixed, size_t cb, size_t uAlignment,
                                        RTR0PROCESS R0Process, vm_map_t pMap, const char *pszTag)
{
    int rc;

    /*
     * The pvFixed address range must be within the VM space when specified.
     */
    if (   pvFixed != (void *)-1
        && (    (vm_offset_t)pvFixed      < vm_map_min(pMap)
            ||  (vm_offset_t)pvFixed + cb > vm_map_max(pMap)))
        return VERR_INVALID_PARAMETER;

    /*
     * Check that the specified alignment is supported.
     */
    if (uAlignment > PAGE_SIZE)
        return VERR_NOT_SUPPORTED;

    /*
     * Create the object.
     */
    PRTR0MEMOBJFREEBSD pMemFreeBSD = (PRTR0MEMOBJFREEBSD)rtR0MemObjNew(sizeof(*pMemFreeBSD), RTR0MEMOBJTYPE_RES_VIRT,
                                                                       NULL, cb, pszTag);
    if (!pMemFreeBSD)
        return VERR_NO_MEMORY;

    vm_offset_t MapAddress = pvFixed != (void *)-1
                           ? (vm_offset_t)pvFixed
                           : vm_map_min(pMap);
    if (pvFixed != (void *)-1)
        vm_map_remove(pMap,
                      MapAddress,
                      MapAddress + cb);

    rc = vm_map_find(pMap,                          /* map */
                     NULL,                          /* object */
                     0,                             /* offset */
                     &MapAddress,                   /* addr (IN/OUT) */
                     cb,                            /* length */
#if __FreeBSD_version >= 1000055
                     0,                             /* max addr */
#endif
                     pvFixed == (void *)-1 ? VMFS_ANY_SPACE : VMFS_NO_SPACE,
                                                    /* find_space */
                     VM_PROT_NONE,                  /* protection */
                     VM_PROT_ALL,                   /* max(_prot) ?? */
                     0);                            /* cow (copy-on-write) */
    if (rc == KERN_SUCCESS)
    {
        if (R0Process != NIL_RTR0PROCESS)
        {
            rc = vm_map_inherit(pMap,
                                MapAddress,
                                MapAddress + cb,
                                VM_INHERIT_SHARE);
            AssertMsg(rc == KERN_SUCCESS, ("%#x\n", rc));
        }
        pMemFreeBSD->Core.pv = (void *)MapAddress;
        pMemFreeBSD->Core.u.ResVirt.R0Process = R0Process;
        *ppMem = &pMemFreeBSD->Core;
        return VINF_SUCCESS;
    }

    rc = VERR_NO_MEMORY; /** @todo fix translation (borrow from darwin) */
    rtR0MemObjDelete(&pMemFreeBSD->Core);
    return rc;

}


DECLHIDDEN(int) rtR0MemObjNativeReserveKernel(PPRTR0MEMOBJINTERNAL ppMem, void *pvFixed, size_t cb, size_t uAlignment,
                                              const char *pszTag)
{
    return rtR0MemObjNativeReserveInMap(ppMem, pvFixed, cb, uAlignment, NIL_RTR0PROCESS, kernel_map, pszTag);
}


DECLHIDDEN(int) rtR0MemObjNativeReserveUser(PPRTR0MEMOBJINTERNAL ppMem, RTR3PTR R3PtrFixed, size_t cb, size_t uAlignment,
                                            RTR0PROCESS R0Process, const char *pszTag)
{
    return rtR0MemObjNativeReserveInMap(ppMem, (void *)R3PtrFixed, cb, uAlignment, R0Process,
                                        &((struct proc *)R0Process)->p_vmspace->vm_map, pszTag);
}


DECLHIDDEN(int) rtR0MemObjNativeMapKernel(PPRTR0MEMOBJINTERNAL ppMem, RTR0MEMOBJ pMemToMap, void *pvFixed, size_t uAlignment,
                                          unsigned fProt, size_t offSub, size_t cbSub, const char *pszTag)
{
//  AssertMsgReturn(!offSub && !cbSub, ("%#x %#x\n", offSub, cbSub), VERR_NOT_SUPPORTED);
    AssertMsgReturn(pvFixed == (void *)-1, ("%p\n", pvFixed), VERR_NOT_SUPPORTED);

    /*
     * Check that the specified alignment is supported.
     */
    if (uAlignment > PAGE_SIZE)
        return VERR_NOT_SUPPORTED;
    Assert(!offSub || cbSub);

    int                rc;
    PRTR0MEMOBJFREEBSD pMemToMapFreeBSD = (PRTR0MEMOBJFREEBSD)pMemToMap;

    /* calc protection */
    vm_prot_t       ProtectionFlags = 0;
    if ((fProt & RTMEM_PROT_NONE) == RTMEM_PROT_NONE)
        ProtectionFlags = VM_PROT_NONE;
    if ((fProt & RTMEM_PROT_READ) == RTMEM_PROT_READ)
        ProtectionFlags |= VM_PROT_READ;
    if ((fProt & RTMEM_PROT_WRITE) == RTMEM_PROT_WRITE)
        ProtectionFlags |= VM_PROT_WRITE;
    if ((fProt & RTMEM_PROT_EXEC) == RTMEM_PROT_EXEC)
        ProtectionFlags |= VM_PROT_EXECUTE;

    vm_offset_t  Addr = vm_map_min(kernel_map);
    if (cbSub == 0)
        cbSub = pMemToMap->cb - offSub;

    vm_object_reference(pMemToMapFreeBSD->pObject);
    rc = vm_map_find(kernel_map,            /* Map to insert the object in */
                     pMemToMapFreeBSD->pObject, /* Object to map */
                     offSub,                /* Start offset in the object */
                     &Addr,                 /* Start address IN/OUT */
                     cbSub,                 /* Size of the mapping */
#if __FreeBSD_version >= 1000055
                     0,                     /* Upper bound of mapping */
#endif
                     VMFS_ANY_SPACE,        /* Whether a suitable address should be searched for first */
                     ProtectionFlags,       /* protection flags */
                     VM_PROT_ALL,           /* Maximum protection flags */
                     0);                    /* copy-on-write and similar flags */

    if (rc == KERN_SUCCESS)
    {
        rc = vm_map_wire(kernel_map, Addr, Addr + cbSub, VM_MAP_WIRE_SYSTEM | VM_MAP_WIRE_NOHOLES);
        AssertMsg(rc == KERN_SUCCESS, ("%#x\n", rc));

        PRTR0MEMOBJFREEBSD pMemFreeBSD = (PRTR0MEMOBJFREEBSD)rtR0MemObjNew(sizeof(RTR0MEMOBJFREEBSD), RTR0MEMOBJTYPE_MAPPING,
                                                                           (void *)Addr, cbSub, pszTag);
        if (pMemFreeBSD)
        {
            Assert((vm_offset_t)pMemFreeBSD->Core.pv == Addr);
            pMemFreeBSD->Core.u.Mapping.R0Process = NIL_RTR0PROCESS;
            *ppMem = &pMemFreeBSD->Core;
            return VINF_SUCCESS;
        }
        rc = vm_map_remove(kernel_map, Addr, Addr + cbSub);
        AssertMsg(rc == KERN_SUCCESS, ("Deleting mapping failed\n"));
    }
    else
        vm_object_deallocate(pMemToMapFreeBSD->pObject);

    return VERR_NO_MEMORY;
}


DECLHIDDEN(int) rtR0MemObjNativeMapUser(PPRTR0MEMOBJINTERNAL ppMem, RTR0MEMOBJ pMemToMap, RTR3PTR R3PtrFixed, size_t uAlignment,
                                        unsigned fProt, RTR0PROCESS R0Process, size_t offSub, size_t cbSub, const char *pszTag)
{
    /*
     * Check for unsupported stuff.
     */
    AssertMsgReturn(R0Process == RTR0ProcHandleSelf(), ("%p != %p\n", R0Process, RTR0ProcHandleSelf()), VERR_NOT_SUPPORTED);
    if (uAlignment > PAGE_SIZE)
        return VERR_NOT_SUPPORTED;
    Assert(!offSub || cbSub);

    int                rc;
    PRTR0MEMOBJFREEBSD pMemToMapFreeBSD = (PRTR0MEMOBJFREEBSD)pMemToMap;
    struct proc       *pProc            = (struct proc *)R0Process;
    struct vm_map     *pProcMap         = &pProc->p_vmspace->vm_map;

    /* calc protection */
    vm_prot_t       ProtectionFlags = 0;
    if ((fProt & RTMEM_PROT_NONE) == RTMEM_PROT_NONE)
        ProtectionFlags = VM_PROT_NONE;
    if ((fProt & RTMEM_PROT_READ) == RTMEM_PROT_READ)
        ProtectionFlags |= VM_PROT_READ;
    if ((fProt & RTMEM_PROT_WRITE) == RTMEM_PROT_WRITE)
        ProtectionFlags |= VM_PROT_WRITE;
    if ((fProt & RTMEM_PROT_EXEC) == RTMEM_PROT_EXEC)
        ProtectionFlags |= VM_PROT_EXECUTE;

    /* calc mapping address */
    vm_offset_t AddrR3;
    if (R3PtrFixed == (RTR3PTR)-1)
    {
        /** @todo is this needed?. */
        PROC_LOCK(pProc);
        AddrR3 = round_page((vm_offset_t)pProc->p_vmspace->vm_daddr + MY_LIM_MAX_PROC(pProc, RLIMIT_DATA));
        PROC_UNLOCK(pProc);
    }
    else
        AddrR3 = (vm_offset_t)R3PtrFixed;

    if (cbSub == 0)
        cbSub = pMemToMap->cb - offSub;

    /* Insert the pObject in the map. */
    vm_object_reference(pMemToMapFreeBSD->pObject);
    rc = vm_map_find(pProcMap,              /* Map to insert the object in */
                     pMemToMapFreeBSD->pObject, /* Object to map */
                     offSub,                /* Start offset in the object */
                     &AddrR3,               /* Start address IN/OUT */
                     cbSub,                 /* Size of the mapping */
#if __FreeBSD_version >= 1000055
                     0,                     /* Upper bound of the mapping */
#endif
                     R3PtrFixed == (RTR3PTR)-1 ? VMFS_ANY_SPACE : VMFS_NO_SPACE,
                                            /* Whether a suitable address should be searched for first */
                     ProtectionFlags,       /* protection flags */
                     VM_PROT_ALL,           /* Maximum protection flags */
                     0);                    /* copy-on-write and similar flags */

    if (rc == KERN_SUCCESS)
    {
        rc = vm_map_wire(pProcMap, AddrR3, AddrR3 + pMemToMap->cb, VM_MAP_WIRE_USER|VM_MAP_WIRE_NOHOLES);
        AssertMsg(rc == KERN_SUCCESS, ("%#x\n", rc));

        rc = vm_map_inherit(pProcMap, AddrR3, AddrR3 + pMemToMap->cb, VM_INHERIT_SHARE);
        AssertMsg(rc == KERN_SUCCESS, ("%#x\n", rc));

        /*
         * Create a mapping object for it.
         */
        PRTR0MEMOBJFREEBSD pMemFreeBSD = (PRTR0MEMOBJFREEBSD)rtR0MemObjNew(sizeof(RTR0MEMOBJFREEBSD), RTR0MEMOBJTYPE_MAPPING,
                                                                           (void *)AddrR3, pMemToMap->cb, pszTag);
        if (pMemFreeBSD)
        {
            Assert((vm_offset_t)pMemFreeBSD->Core.pv == AddrR3);
            pMemFreeBSD->Core.u.Mapping.R0Process = R0Process;
            *ppMem = &pMemFreeBSD->Core;
            return VINF_SUCCESS;
        }

        rc = vm_map_remove(pProcMap, AddrR3, AddrR3 + pMemToMap->cb);
        AssertMsg(rc == KERN_SUCCESS, ("Deleting mapping failed\n"));
    }
    else
        vm_object_deallocate(pMemToMapFreeBSD->pObject);

    return VERR_NO_MEMORY;
}


DECLHIDDEN(int) rtR0MemObjNativeProtect(PRTR0MEMOBJINTERNAL pMem, size_t offSub, size_t cbSub, uint32_t fProt)
{
    vm_prot_t          ProtectionFlags = 0;
    vm_offset_t        AddrStart       = (uintptr_t)pMem->pv + offSub;
    vm_offset_t        AddrEnd         = AddrStart + cbSub;
    vm_map_t           pVmMap          = rtR0MemObjFreeBSDGetMap(pMem);

    if (!pVmMap)
        return VERR_NOT_SUPPORTED;

    if ((fProt & RTMEM_PROT_NONE) == RTMEM_PROT_NONE)
        ProtectionFlags = VM_PROT_NONE;
    if ((fProt & RTMEM_PROT_READ) == RTMEM_PROT_READ)
        ProtectionFlags |= VM_PROT_READ;
    if ((fProt & RTMEM_PROT_WRITE) == RTMEM_PROT_WRITE)
        ProtectionFlags |= VM_PROT_WRITE;
    if ((fProt & RTMEM_PROT_EXEC) == RTMEM_PROT_EXEC)
        ProtectionFlags |= VM_PROT_EXECUTE;

    int krc = vm_map_protect(pVmMap, AddrStart, AddrEnd, ProtectionFlags, FALSE);
    if (krc == KERN_SUCCESS)
        return VINF_SUCCESS;

    return VERR_NOT_SUPPORTED;
}


DECLHIDDEN(RTHCPHYS) rtR0MemObjNativeGetPagePhysAddr(PRTR0MEMOBJINTERNAL pMem, size_t iPage)
{
    PRTR0MEMOBJFREEBSD pMemFreeBSD = (PRTR0MEMOBJFREEBSD)pMem;

    switch (pMemFreeBSD->Core.enmType)
    {
        case RTR0MEMOBJTYPE_LOCK:
        {
            if (    pMemFreeBSD->Core.u.Lock.R0Process != NIL_RTR0PROCESS
                &&  pMemFreeBSD->Core.u.Lock.R0Process != (RTR0PROCESS)curproc)
            {
                /* later */
                return NIL_RTHCPHYS;
            }

            vm_offset_t pb = (vm_offset_t)pMemFreeBSD->Core.pv + ptoa(iPage);

            struct proc    *pProc     = (struct proc *)pMemFreeBSD->Core.u.Lock.R0Process;
            struct vm_map  *pProcMap  = &pProc->p_vmspace->vm_map;
            pmap_t pPhysicalMap       = vm_map_pmap(pProcMap);

            return pmap_extract(pPhysicalMap, pb);
        }

        case RTR0MEMOBJTYPE_MAPPING:
        {
            vm_offset_t pb = (vm_offset_t)pMemFreeBSD->Core.pv + ptoa(iPage);

            if (pMemFreeBSD->Core.u.Mapping.R0Process != NIL_RTR0PROCESS)
            {
                struct proc    *pProc     = (struct proc *)pMemFreeBSD->Core.u.Mapping.R0Process;
                struct vm_map  *pProcMap  = &pProc->p_vmspace->vm_map;
                pmap_t pPhysicalMap       = vm_map_pmap(pProcMap);

                return pmap_extract(pPhysicalMap, pb);
            }
            return vtophys(pb);
        }

        case RTR0MEMOBJTYPE_PAGE:
        case RTR0MEMOBJTYPE_LOW:
        case RTR0MEMOBJTYPE_PHYS_NC:
        {
            RTHCPHYS addr;

            VM_OBJECT_WLOCK(pMemFreeBSD->pObject);
            addr = VM_PAGE_TO_PHYS(vm_page_lookup(pMemFreeBSD->pObject, iPage));
            VM_OBJECT_WUNLOCK(pMemFreeBSD->pObject);
            return addr;
        }

        case RTR0MEMOBJTYPE_PHYS:
            return pMemFreeBSD->Core.u.Cont.Phys + ptoa(iPage);

        case RTR0MEMOBJTYPE_CONT:
            return pMemFreeBSD->Core.u.Phys.PhysBase + ptoa(iPage);

        case RTR0MEMOBJTYPE_RES_VIRT:
        default:
            return NIL_RTHCPHYS;
    }
}


DECLHIDDEN(int) rtR0MemObjNativeZeroInitWithoutMapping(PRTR0MEMOBJINTERNAL pMem)
{
    PRTR0MEMOBJFREEBSD const pMemFreeBsd = (PRTR0MEMOBJFREEBSD)pMem;
    size_t const             cPages      = pMemSolaris->Core.cb >> PAGE_SHIFT;
    size_t                   iPage;
    for (iPage = 0; iPage < cPages; iPage++)
    {
        void          *pvPage;

        /* Get the physical address of the page. */
        RTHCPHYS const HCPhys = rtR0MemObjNativeGetPagePhysAddr(&pMemFreeBsd->Core, iPage);
        AssertReturn(HCPhys != NIL_RTHCPHYS, VERR_INTERNAL_ERROR_3);
        Assert(!(HCPhys & PAGE_OFFSET_MASK));

        /* Map it. */
        pvPage = (void *)(uintptr_t)pmap_map(NULL, HCPhys, (HCPhys | PAGE_OFFSET_MASK) + 1, VM_PROT_WRITE | VM_PROT_READ);
        AssertPtrReturn(pvPage, VERR_INTERNAL_ERROR_3);

        /* Zero it. */
        RT_BZERO(pvPage, PAGE_SIZE);
    }
    return VINF_SUCCESS;
}

