/* $Id: memobj-r0drv-haiku.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Ring-0 Memory Objects, Haiku.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#include "the-haiku-kernel.h"

#include <iprt/memobj.h>
#include <iprt/mem.h>
#include <iprt/err.h>
#include <iprt/assert.h>
#include <iprt/log.h>
#include <iprt/param.h>
#include <iprt/process.h>
#include "internal/memobj.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The Haiku version of the memory object structure.
 */
typedef struct RTR0MEMOBJHAIKU
{
    /** The core structure. */
    RTR0MEMOBJINTERNAL  Core;
    /** Area identifier */
    area_id             AreaId;
} RTR0MEMOBJHAIKU, *PRTR0MEMOBJHAIKU;


//MALLOC_DEFINE(M_IPRTMOBJ, "iprtmobj", "IPRT - R0MemObj");
#if 0
/**
 * Gets the virtual memory map the specified object is mapped into.
 *
 * @returns VM map handle on success, NULL if no map.
 * @param   pMem                The memory object.
 */
static vm_map_t rtR0MemObjHaikuGetMap(PRTR0MEMOBJINTERNAL pMem)
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
#endif


int rtR0MemObjNativeFree(RTR0MEMOBJ pMem)
{
    PRTR0MEMOBJHAIKU pMemHaiku = (PRTR0MEMOBJHAIKU)pMem;
    int rc = B_OK;

    switch (pMemHaiku->Core.enmType)
    {
        case RTR0MEMOBJTYPE_PAGE:
        case RTR0MEMOBJTYPE_LOW:
        case RTR0MEMOBJTYPE_CONT:
        case RTR0MEMOBJTYPE_MAPPING:
        case RTR0MEMOBJTYPE_PHYS:
        case RTR0MEMOBJTYPE_PHYS_NC:
        {
            if (pMemHaiku->AreaId > -1)
                rc = delete_area(pMemHaiku->AreaId);

            AssertMsg(rc == B_OK, ("%#x", rc));
            break;
        }

        case RTR0MEMOBJTYPE_LOCK:
        {
            team_id team = B_SYSTEM_TEAM;

            if (pMemHaiku->Core.u.Lock.R0Process != NIL_RTR0PROCESS)
                team = ((team_id)pMemHaiku->Core.u.Lock.R0Process);

            rc = unlock_memory_etc(team, pMemHaiku->Core.pv, pMemHaiku->Core.cb, B_READ_DEVICE);
            AssertMsg(rc == B_OK, ("%#x", rc));
            break;
        }

        case RTR0MEMOBJTYPE_RES_VIRT:
        {
            team_id team = B_SYSTEM_TEAM;
            if (pMemHaiku->Core.u.Lock.R0Process != NIL_RTR0PROCESS)
                team = ((team_id)pMemHaiku->Core.u.Lock.R0Process);

            rc = vm_unreserve_address_range(team, pMemHaiku->Core.pv, pMemHaiku->Core.cb);
            AssertMsg(rc == B_OK, ("%#x", rc));
            break;
        }

        default:
            AssertMsgFailed(("enmType=%d\n", pMemHaiku->Core.enmType));
            return VERR_INTERNAL_ERROR;
    }

    return VINF_SUCCESS;
}


static int rtR0MemObjNativeAllocArea(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, bool fExecutable, RTR0MEMOBJTYPE enmType,
                                     RTHCPHYS PhysHighest, size_t uAlignment, const char *pszTag)
{
    NOREF(fExecutable);

    int rc;
    void *pvMap         = NULL;
    const char *pszName = NULL;
    uint32 addressSpec  = B_ANY_KERNEL_ADDRESS;
    uint32 fLock        = ~0U;
    LogFlowFunc(("ppMem=%p cb=%u, fExecutable=%s, enmType=%08x, PhysHighest=%RX64 uAlignment=%u\n", ppMem,(unsigned)cb,
                 fExecutable ? "true" : "false", enmType, PhysHighest,(unsigned)uAlignment));

    switch (enmType)
    {
        case RTR0MEMOBJTYPE_PAGE:
            pszName = "IPRT R0MemObj Alloc";
            fLock = B_FULL_LOCK;
            break;
        case RTR0MEMOBJTYPE_LOW:
            pszName = "IPRT R0MemObj AllocLow";
            fLock = B_32_BIT_FULL_LOCK;
            break;
        case RTR0MEMOBJTYPE_CONT:
            pszName = "IPRT R0MemObj AllocCont";
            fLock = B_32_BIT_CONTIGUOUS;
            break;
#if 0
        case RTR0MEMOBJTYPE_MAPPING:
            pszName = "IPRT R0MemObj Mapping";
            fLock = B_FULL_LOCK;
            break;
#endif
        case RTR0MEMOBJTYPE_PHYS:
            /** @todo alignment  */
            if (uAlignment != PAGE_SIZE)
                return VERR_NOT_SUPPORTED;
            /** @todo r=ramshankar: no 'break' here?? */
        case RTR0MEMOBJTYPE_PHYS_NC:
            pszName = "IPRT R0MemObj AllocPhys";
            fLock   = (PhysHighest < _4G ? B_LOMEM : B_32_BIT_CONTIGUOUS);
            break;
#if 0
        case RTR0MEMOBJTYPE_LOCK:
            break;
#endif
        default:
            return VERR_INTERNAL_ERROR;
    }

    /* Create the object. */
    PRTR0MEMOBJHAIKU pMemHaiku;
    pMemHaiku = (PRTR0MEMOBJHAIKU)rtR0MemObjNew(sizeof(RTR0MEMOBJHAIKU), enmType, NULL, cb, pszTag);
    if (RT_UNLIKELY(!pMemHaiku))
        return VERR_NO_MEMORY;

    rc = pMemHaiku->AreaId = create_area(pszName, &pvMap, addressSpec, cb, fLock, B_READ_AREA | B_WRITE_AREA);
    if (pMemHaiku->AreaId >= 0)
    {
        physical_entry physMap[2];
        pMemHaiku->Core.pv = pvMap;   /* store start address */
        switch (enmType)
        {
            case RTR0MEMOBJTYPE_CONT:
                rc = get_memory_map(pvMap, cb, physMap, 2);
                if (rc == B_OK)
                    pMemHaiku->Core.u.Cont.Phys = physMap[0].address;
                break;

            case RTR0MEMOBJTYPE_PHYS:
            case RTR0MEMOBJTYPE_PHYS_NC:
                rc = get_memory_map(pvMap, cb, physMap, 2);
                if (rc == B_OK)
                {
                    pMemHaiku->Core.u.Phys.PhysBase = physMap[0].address;
                    pMemHaiku->Core.u.Phys.fAllocated = true;
                }
                break;

            default:
                break;
        }
        if (rc >= B_OK)
        {
            *ppMem = &pMemHaiku->Core;
            return VINF_SUCCESS;
        }

        delete_area(pMemHaiku->AreaId);
    }

    rtR0MemObjDelete(&pMemHaiku->Core);
    return RTErrConvertFromHaikuKernReturn(rc);
}


int rtR0MemObjNativeAllocPage(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, bool fExecutable, const char *pszTag)
{
    return rtR0MemObjNativeAllocArea(ppMem, cb, fExecutable, RTR0MEMOBJTYPE_PAGE, 0 /* PhysHighest */, 0 /* uAlignment */, pszTag);
}


DECLHIDDEN(int) rtR0MemObjNativeAllocLarge(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, size_t cbLargePage, uint32_t fFlags,
                                           const char *pszTag)
{
    return rtR0MemObjFallbackAllocLarge(ppMem, cb, cbLargePage, fFlags, pszTag);
}


int rtR0MemObjNativeAllocLow(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, bool fExecutable, const char *pszTag)
{
    return rtR0MemObjNativeAllocArea(ppMem, cb, fExecutable, RTR0MEMOBJTYPE_LOW, 0 /* PhysHighest */, 0 /* uAlignment */, pszTag);
}


int rtR0MemObjNativeAllocCont(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, RTHCPHYS PhysHighest, bool fExecutable, const char *pszTag)
{
    return rtR0MemObjNativeAllocArea(ppMem, cb, fExecutable, RTR0MEMOBJTYPE_CONT, PhysHighest, 0 /* uAlignment */, pszTag);
}

int rtR0MemObjNativeAllocPhys(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, RTHCPHYS PhysHighest, size_t uAlignment, const char *pszTag)
{
    return rtR0MemObjNativeAllocArea(ppMem, cb, false, RTR0MEMOBJTYPE_PHYS, PhysHighest, uAlignment, pszTag);
}


int rtR0MemObjNativeAllocPhysNC(PPRTR0MEMOBJINTERNAL ppMem, size_t cb, RTHCPHYS PhysHighest, const char *pszTag)
{
    return rtR0MemObjNativeAllocPhys(ppMem, cb, PhysHighest, PAGE_SIZE, pszTag);
}


int rtR0MemObjNativeEnterPhys(PPRTR0MEMOBJINTERNAL ppMem, RTHCPHYS Phys, size_t cb, uint32_t uCachePolicy, const char *pszTag)
{
    AssertReturn(uCachePolicy == RTMEM_CACHE_POLICY_DONT_CARE, VERR_NOT_SUPPORTED);
    LogFlowFunc(("ppMem=%p Phys=%08x cb=%u uCachePolicy=%x\n", ppMem, Phys,(unsigned)cb, uCachePolicy));

    /* Create the object. */
    PRTR0MEMOBJHAIKU pMemHaiku = (PRTR0MEMOBJHAIKU)rtR0MemObjNew(sizeof(*pMemHaiku), RTR0MEMOBJTYPE_PHYS, NULL, cb, pszTag);
    if (!pMemHaiku)
        return VERR_NO_MEMORY;

    /* There is no allocation here, it needs to be mapped somewhere first. */
    pMemHaiku->AreaId = -1;
    pMemHaiku->Core.u.Phys.fAllocated = false;
    pMemHaiku->Core.u.Phys.PhysBase = Phys;
    pMemHaiku->Core.u.Phys.uCachePolicy = uCachePolicy;
    *ppMem = &pMemHaiku->Core;
    return VINF_SUCCESS;
}


/**
 * Worker locking the memory in either kernel or user maps.
 *
 * @returns IPRT status code.
 * @param   ppMem       Where to store the allocated memory object.
 * @param   pvStart     The starting address.
 * @param   cb          The size of the block.
 * @param   fAccess     The mapping protection to apply.
 * @param   R0Process   The process to map the memory to (use NIL_RTR0PROCESS
 *                      for the kernel)
 * @param   fFlags      Memory flags (B_READ_DEVICE indicates the memory is
 *                      intended to be written from a "device").
 * @param   pszTag      Allocation tag used for statistics and such.
 */
static int rtR0MemObjNativeLockInMap(PPRTR0MEMOBJINTERNAL ppMem, void *pvStart, size_t cb, uint32_t fAccess,
                                     RTR0PROCESS R0Process, int fFlags, const char *pszTag)
{
    NOREF(fAccess);
    team_id TeamId = B_SYSTEM_TEAM;

    LogFlowFunc(("ppMem=%p pvStart=%p cb=%u fAccess=%x R0Process=%d fFlags=%x\n", ppMem, pvStart, cb, fAccess, R0Process,
                 fFlags));

    /* Create the object. */
    PRTR0MEMOBJHAIKU pMemHaiku = (PRTR0MEMOBJHAIKU)rtR0MemObjNew(sizeof(*pMemHaiku), RTR0MEMOBJTYPE_LOCK, pvStart, cb, pszTag);
    if (RT_UNLIKELY(!pMemHaiku))
        return VERR_NO_MEMORY;

    if (R0Process != NIL_RTR0PROCESS)
        TeamId = (team_id)R0Process;
    int rc = lock_memory_etc(TeamId, pvStart, cb, fFlags);
    if (rc == B_OK)
    {
        pMemHaiku->AreaId = -1;
        pMemHaiku->Core.u.Lock.R0Process = R0Process;
        *ppMem = &pMemHaiku->Core;
        return VINF_SUCCESS;
    }
    rtR0MemObjDelete(&pMemHaiku->Core);
    return RTErrConvertFromHaikuKernReturn(rc);
}


int rtR0MemObjNativeLockUser(PPRTR0MEMOBJINTERNAL ppMem, RTR3PTR R3Ptr, size_t cb, uint32_t fAccess, RTR0PROCESS R0Process,
                             const char *pszTag)
{
    return rtR0MemObjNativeLockInMap(ppMem, (void *)R3Ptr, cb, fAccess, R0Process, B_READ_DEVICE, pszTag);
}


int rtR0MemObjNativeLockKernel(PPRTR0MEMOBJINTERNAL ppMem, void *pv, size_t cb, uint32_t fAccess, const char *pszTag)
{
    return rtR0MemObjNativeLockInMap(ppMem, pv, cb, fAccess, NIL_RTR0PROCESS, B_READ_DEVICE, pszTag);
}


#if 0
/** @todo Reserve address space */
/**
 * Worker for the two virtual address space reservers.
 *
 * We're leaning on the examples provided by mmap and vm_mmap in vm_mmap.c here.
 */
static int rtR0MemObjNativeReserveInMap(PPRTR0MEMOBJINTERNAL ppMem, void *pvFixed, size_t cb, size_t uAlignment,
                                        RTR0PROCESS R0Process)
{
    int rc;
    team_id TeamId = B_SYSTEM_TEAM;

    LogFlowFunc(("ppMem=%p pvFixed=%p cb=%u uAlignment=%u R0Process=%d\n", ppMem, pvFixed, (unsigned)cb, uAlignment, R0Process));

    if (R0Process != NIL_RTR0PROCESS)
    team = (team_id)R0Process;

    /* Check that the specified alignment is supported. */
    if (uAlignment > PAGE_SIZE)
    return VERR_NOT_SUPPORTED;

    /* Create the object. */
    PRTR0MEMOBJHAIKU pMemHaiku = (PRTR0MEMOBJHAIKU)rtR0MemObjNew(sizeof(*pMemHaiku), RTR0MEMOBJTYPE_RES_VIRT, NULL, cb);
    if (!pMemHaiku)
    return VERR_NO_MEMORY;

    /* Ask the kernel to reserve the address range. */
    //XXX: vm_reserve_address_range ?
    return VERR_NOT_SUPPORTED;
}
#endif


int rtR0MemObjNativeReserveKernel(PPRTR0MEMOBJINTERNAL ppMem, void *pvFixed, size_t cb, size_t uAlignment, const char *pszTag)
{
    RT_NOREF(ppMem, pvFixed, cb, uAlignment, pszTag);
    return VERR_NOT_SUPPORTED;
}


int rtR0MemObjNativeReserveUser(PPRTR0MEMOBJINTERNAL ppMem, RTR3PTR R3PtrFixed, size_t cb, size_t uAlignment,
                                RTR0PROCESS R0Process, const char *pszTag)
{
    RT_NOREF(ppMem, R3PtrFixed, cb, uAlignment, R0Process, pszTag);
    return VERR_NOT_SUPPORTED;
}


int rtR0MemObjNativeMapKernel(PPRTR0MEMOBJINTERNAL ppMem, RTR0MEMOBJ pMemToMap, void *pvFixed, size_t uAlignment,
                              unsigned fProt, size_t offSub, size_t cbSub, const char *pszTag)
{
    PRTR0MEMOBJHAIKU pMemToMapHaiku = (PRTR0MEMOBJHAIKU)pMemToMap;
    PRTR0MEMOBJHAIKU pMemHaiku;
    area_id area = -1;
    void *pvMap = pvFixed;
    uint32 uAddrSpec = B_EXACT_ADDRESS;
    uint32 fProtect = 0;
    int rc = VERR_MAP_FAILED;
    AssertMsgReturn(!offSub && !cbSub, ("%#x %#x\n", offSub, cbSub), VERR_NOT_SUPPORTED);
    AssertMsgReturn(pvFixed == (void *)-1, ("%p\n", pvFixed), VERR_NOT_SUPPORTED);
#if 0
    /** @todo r=ramshankar: Wrong format specifiers, fix later! */
    dprintf("%s(%p, %p, %p, %d, %x, %u, %u)\n", __FUNCTION__, ppMem, pMemToMap, pvFixed, uAlignment,
            fProt, offSub, cbSub);
#endif
    /* Check that the specified alignment is supported. */
    if (uAlignment > PAGE_SIZE)
        return VERR_NOT_SUPPORTED;

    /* We can't map anything to the first page, sorry. */
    if (pvFixed == 0)
        return VERR_NOT_SUPPORTED;

    if (fProt & RTMEM_PROT_READ)
        fProtect |= B_KERNEL_READ_AREA;
    if (fProt & RTMEM_PROT_WRITE)
        fProtect |= B_KERNEL_WRITE_AREA;

    /*
     * Either the object we map has an area associated with, which we can clone,
     * or it's a physical address range which we must map.
     */
    if (pMemToMapHaiku->AreaId > -1)
    {
        if (pvFixed == (void *)-1)
            uAddrSpec = B_ANY_KERNEL_ADDRESS;

        rc = area = clone_area("IPRT R0MemObj MapKernel", &pvMap, uAddrSpec, fProtect, pMemToMapHaiku->AreaId);
        LogFlow(("rtR0MemObjNativeMapKernel: clone_area uAddrSpec=%d fProtect=%x AreaId=%d rc=%d\n", uAddrSpec, fProtect,
                 pMemToMapHaiku->AreaId, rc));
    }
    else if (pMemToMapHaiku->Core.enmType == RTR0MEMOBJTYPE_PHYS)
    {
        /* map_physical_memory() won't let you choose where. */
        if (pvFixed != (void *)-1)
            return VERR_NOT_SUPPORTED;
        uAddrSpec = B_ANY_KERNEL_ADDRESS;

        rc = area = map_physical_memory("IPRT R0MemObj MapKernelPhys", (phys_addr_t)pMemToMapHaiku->Core.u.Phys.PhysBase,
                                        pMemToMapHaiku->Core.cb, uAddrSpec, fProtect, &pvMap);
    }
    else
        return VERR_NOT_SUPPORTED;

    if (rc >= B_OK)
    {
        /* Create the object. */
        pMemHaiku = (PRTR0MEMOBJHAIKU)rtR0MemObjNew(sizeof(RTR0MEMOBJHAIKU), RTR0MEMOBJTYPE_MAPPING, pvMap,
                                                    pMemToMapHaiku->Core.cb, pszTag);
        if (RT_UNLIKELY(!pMemHaiku))
            return VERR_NO_MEMORY;

        pMemHaiku->Core.u.Mapping.R0Process = NIL_RTR0PROCESS;
        pMemHaiku->Core.pv = pvMap;
        pMemHaiku->AreaId = area;
        *ppMem = &pMemHaiku->Core;
        return VINF_SUCCESS;
    }
    rc = VERR_MAP_FAILED;

    /** @todo finish the implementation. */

    rtR0MemObjDelete(&pMemHaiku->Core);
    return rc;
}


int rtR0MemObjNativeMapUser(PPRTR0MEMOBJINTERNAL ppMem, RTR0MEMOBJ pMemToMap, RTR3PTR R3PtrFixed, size_t uAlignment,
                            unsigned fProt, RTR0PROCESS R0Process, size_t offSub, size_t cbSub, const char *pszTag)
{
#if 0
    /*
     * Check for unsupported stuff.
     */
    AssertMsgReturn(R0Process == RTR0ProcHandleSelf(), ("%p != %p\n", R0Process, RTR0ProcHandleSelf()), VERR_NOT_SUPPORTED);
    AssertMsgReturn(R3PtrFixed == (RTR3PTR)-1, ("%p\n", R3PtrFixed), VERR_NOT_SUPPORTED);
    if (uAlignment > PAGE_SIZE)
        return VERR_NOT_SUPPORTED;
    AssertMsgReturn(!offSub && !cbSub, ("%#zx %#zx\n", offSub, cbSub), VERR_NOT_SUPPORTED); /** @todo implement sub maps */

    int                rc;
    PRTR0MEMOBJHAIKU pMemToMapHaiku = (PRTR0MEMOBJHAIKU)pMemToMap;
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
    PROC_LOCK(pProc);
    vm_offset_t AddrR3 = round_page((vm_offset_t)pProc->p_vmspace->vm_daddr + lim_max(pProc, RLIMIT_DATA));
    PROC_UNLOCK(pProc);

    /* Insert the object in the map. */
    rc = vm_map_find(pProcMap,              /* Map to insert the object in */
                     NULL,                  /* Object to map */
                     0,                     /* Start offset in the object */
                     &AddrR3,               /* Start address IN/OUT */
                     pMemToMap->cb,         /* Size of the mapping */
                     TRUE,                  /* Whether a suitable address should be searched for first */
                     ProtectionFlags,       /* protection flags */
                     VM_PROT_ALL,           /* Maximum protection flags */
                     0);                    /* Copy on write */

    /* Map the memory page by page into the destination map. */
    if (rc == KERN_SUCCESS)
    {
        size_t         cPages       = pMemToMap->cb >> PAGE_SHIFT;;
        pmap_t         pPhysicalMap = pProcMap->pmap;
        vm_offset_t    AddrR3Dst    = AddrR3;

        if (   pMemToMap->enmType == RTR0MEMOBJTYPE_PHYS
            || pMemToMap->enmType == RTR0MEMOBJTYPE_PHYS_NC
            || pMemToMap->enmType == RTR0MEMOBJTYPE_PAGE)
        {
            /* Mapping physical allocations */
            Assert(cPages == pMemToMapHaiku->u.Phys.cPages);

            /* Insert the memory page by page into the mapping. */
            for (uint32_t iPage = 0; iPage < cPages; iPage++)
            {
                vm_page_t pPage = pMemToMapHaiku->u.Phys.apPages[iPage];

                MY_PMAP_ENTER(pPhysicalMap, AddrR3Dst, pPage, ProtectionFlags, TRUE);
                AddrR3Dst += PAGE_SIZE;
            }
        }
        else
        {
            /* Mapping cont or low memory types */
            vm_offset_t AddrToMap = (vm_offset_t)pMemToMap->pv;

            for (uint32_t iPage = 0; iPage < cPages; iPage++)
            {
                vm_page_t pPage = PHYS_TO_VM_PAGE(vtophys(AddrToMap));

                MY_PMAP_ENTER(pPhysicalMap, AddrR3Dst, pPage, ProtectionFlags, TRUE);
                AddrR3Dst += PAGE_SIZE;
                AddrToMap += PAGE_SIZE;
            }
        }
    }

    if (RT_SUCCESS(rc))
    {
        /*
         * Create a mapping object for it.
         */
        PRTR0MEMOBJHAIKU pMemHaiku = (PRTR0MEMOBJHAIKU)rtR0MemObjNew(sizeof(RTR0MEMOBJHAIKU), RTR0MEMOBJTYPE_MAPPING,
                                                                     (void *)AddrR3, pMemToMap->cb, pszTag);
        if (pMemHaiku)
        {
            Assert((vm_offset_t)pMemHaiku->Core.pv == AddrR3);
            pMemHaiku->Core.u.Mapping.R0Process = R0Process;
            *ppMem = &pMemHaiku->Core;
            return VINF_SUCCESS;
        }

        rc = vm_map_remove(pProcMap, ((vm_offset_t)AddrR3), ((vm_offset_t)AddrR3) + pMemToMap->cb);
        AssertMsg(rc == KERN_SUCCESS, ("Deleting mapping failed\n"));
    }
#else
    RT_NOREF(ppMem, pMemToMap, R3PtrFixed, uAlignment, fProt, R0Process, offSub, cbSub, pszTag);
#endif
    return VERR_NOT_SUPPORTED;
}


int rtR0MemObjNativeProtect(PRTR0MEMOBJINTERNAL pMem, size_t offSub, size_t cbSub, uint32_t fProt)
{
    return VERR_NOT_SUPPORTED;
}


RTHCPHYS rtR0MemObjNativeGetPagePhysAddr(PRTR0MEMOBJINTERNAL pMem, size_t iPage)
{
    PRTR0MEMOBJHAIKU pMemHaiku = (PRTR0MEMOBJHAIKU)pMem;
    status_t rc;

    /** @todo r=ramshankar: Validate objects */

    LogFlow(("rtR0MemObjNativeGetPagePhysAddr: pMem=%p enmType=%x iPage=%u\n", pMem, pMemHaiku->Core.enmType,(unsigned)iPage));

    switch (pMemHaiku->Core.enmType)
    {
        case RTR0MEMOBJTYPE_LOCK:
        {
            team_id        TeamId = B_SYSTEM_TEAM;
            physical_entry aPhysMap[2];
            int32          cPhysMap = 2;    /** @todo r=ramshankar: why not use RT_ELEMENTS? */

            if (pMemHaiku->Core.u.Lock.R0Process != NIL_RTR0PROCESS)
                TeamId = (team_id)pMemHaiku->Core.u.Lock.R0Process;
            void *pb = pMemHaiku->Core.pv + (iPage << PAGE_SHIFT);

            rc = get_memory_map_etc(TeamId, pb, B_PAGE_SIZE, aPhysMap, &cPhysMap);
            if (rc < B_OK || cPhysMap < 1)
                return NIL_RTHCPHYS;

            return aPhysMap[0].address;
        }

#if 0
        case RTR0MEMOBJTYPE_MAPPING:
        {
            vm_offset_t pb = (vm_offset_t)pMemHaiku->Core.pv + (iPage << PAGE_SHIFT);

            if (pMemHaiku->Core.u.Mapping.R0Process != NIL_RTR0PROCESS)
            {
                struct proc    *pProc     = (struct proc *)pMemHaiku->Core.u.Mapping.R0Process;
                struct vm_map  *pProcMap  = &pProc->p_vmspace->vm_map;
                pmap_t pPhysicalMap       = pProcMap->pmap;

                return pmap_extract(pPhysicalMap, pb);
            }
            return vtophys(pb);
        }
#endif
        case RTR0MEMOBJTYPE_CONT:
            return pMemHaiku->Core.u.Cont.Phys + (iPage << PAGE_SHIFT);

        case RTR0MEMOBJTYPE_PHYS:
            return pMemHaiku->Core.u.Phys.PhysBase + (iPage << PAGE_SHIFT);

        case RTR0MEMOBJTYPE_LOW:
        case RTR0MEMOBJTYPE_PAGE:
        case RTR0MEMOBJTYPE_PHYS_NC:
        {
            team_id        TeamId = B_SYSTEM_TEAM;
            physical_entry aPhysMap[2];
            int32          cPhysMap = 2;    /** @todo r=ramshankar: why not use RT_ELEMENTS? */

            void *pb = pMemHaiku->Core.pv + (iPage << PAGE_SHIFT);
            rc = get_memory_map_etc(TeamId, pb, B_PAGE_SIZE, aPhysMap, &cPhysMap);
            if (rc < B_OK || cPhysMap < 1)
                return NIL_RTHCPHYS;

            return aPhysMap[0].address;
        }

        case RTR0MEMOBJTYPE_RES_VIRT:
        default:
            return NIL_RTHCPHYS;
    }
}


DECLHIDDEN(int) rtR0MemObjNativeZeroInitWithoutMapping(PRTR0MEMOBJINTERNAL pMem)
{
    RT_NOREF(pMem);
    return VERR_NOT_IMPLEMENTED;
}

