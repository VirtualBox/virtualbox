/* $Id: alloc-r0drv-nt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Memory Allocation, Ring-0 Driver, NT.
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
#include "the-nt-kernel.h"
#include "internal/iprt.h"
#include <iprt/mem.h>

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include "r0drv/alloc-r0drv.h"
#include "internal-r0drv-nt.h"


/**
 * OS specific allocation function.
 */
DECLHIDDEN(int) rtR0MemAllocEx(size_t cb, uint32_t fFlags, PRTMEMHDR *ppHdr)
{
    if (!(fFlags & RTMEMHDR_FLAG_ANY_CTX))
    {
        PRTMEMHDR       pHdr;
        POOL_TYPE const enmPoolType = g_uRtNtVersion >= RTNT_MAKE_VERSION(8,0) ? NonPagedPoolNx : NonPagedPool;
        if (g_pfnrtExAllocatePoolWithTag)
            pHdr = (PRTMEMHDR)g_pfnrtExAllocatePoolWithTag(enmPoolType, cb + sizeof(*pHdr), IPRT_NT_POOL_TAG);
        else
        {
            fFlags |= RTMEMHDR_FLAG_UNTAGGED;
            pHdr = (PRTMEMHDR)ExAllocatePool(enmPoolType, cb + sizeof(*pHdr));
        }
        if (RT_LIKELY(pHdr))
        {
            pHdr->u32Magic  = RTMEMHDR_MAGIC;
            pHdr->fFlags    = fFlags;
            pHdr->cb        = (uint32_t)cb; Assert(pHdr->cb == cb);
            pHdr->cbReq     = (uint32_t)cb;
            *ppHdr = pHdr;
            return VINF_SUCCESS;
        }
        return VERR_NO_MEMORY;
    }
    return VERR_NOT_SUPPORTED;
}


/**
 * OS specific free function.
 */
DECLHIDDEN(void) rtR0MemFree(PRTMEMHDR pHdr)
{
    pHdr->u32Magic = RTMEMHDR_MAGIC_DEAD;
    if (g_pfnrtExFreePoolWithTag && !(pHdr->fFlags & RTMEMHDR_FLAG_UNTAGGED))
        g_pfnrtExFreePoolWithTag(pHdr, IPRT_NT_POOL_TAG);
    else
        ExFreePool(pHdr);
}


RTR0DECL(void *) RTMemContAlloc(PRTCCPHYS pPhys, size_t cb)
{
    /*
     * validate input.
     */
    AssertPtr(pPhys);
    Assert(cb > 0);

    /*
     * Allocate and get physical address.
     * Make sure the return is page aligned.
     */
    PHYSICAL_ADDRESS MaxPhysAddr;
    MaxPhysAddr.HighPart = 0;
    MaxPhysAddr.LowPart = 0xffffffff;
    cb = RT_ALIGN_Z(cb, PAGE_SIZE);
    void *pv = MmAllocateContiguousMemory(cb, MaxPhysAddr);
    if (pv)
    {
        if (!((uintptr_t)pv & PAGE_OFFSET_MASK))    /* paranoia */
        {
            PHYSICAL_ADDRESS PhysAddr = MmGetPhysicalAddress(pv);
            if (!PhysAddr.HighPart)                 /* paranoia */
            {
                *pPhys = (RTCCPHYS)PhysAddr.LowPart;
                return pv;
            }

            /* failure */
            AssertMsgFailed(("MMAllocContiguousMemory returned high address! PhysAddr=%RX64\n", (uint64_t)PhysAddr.QuadPart));
        }
        else
            AssertMsgFailed(("MMAllocContiguousMemory didn't return a page aligned address - %p!\n", pv));

        MmFreeContiguousMemory(pv);
    }

    return NULL;
}


RTR0DECL(void) RTMemContFree(void *pv, size_t cb)
{
    if (pv)
    {
        Assert(cb > 0); NOREF(cb);
        AssertMsg(!((uintptr_t)pv & PAGE_OFFSET_MASK), ("pv=%p\n", pv));
        MmFreeContiguousMemory(pv);
    }
}

