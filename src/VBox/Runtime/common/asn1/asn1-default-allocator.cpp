/* $Id: asn1-default-allocator.cpp 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $ */
/** @file
 * IPRT - ASN.1, Default Allocator.
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
#include "internal/iprt.h"
#include <iprt/asn1.h>

#include <iprt/mem.h>
#include <iprt/errcore.h>
#include <iprt/string.h>


/**
 * Aligns allocation sizes a little.
 *
 * @returns Aligned size.
 * @param   cb                  Requested size.
 */
static size_t rtAsn1DefaultAllocator_AlignSize(size_t cb)
{
    if (cb >= 64)
        return RT_ALIGN_Z(cb, 64);
    if (cb >= 32)
        return RT_ALIGN_Z(cb, 32);
    if (cb >= 16)
        return RT_ALIGN_Z(cb, 16);
    return cb;
}


/** @interface_method_impl{RTASN1ALLOCATORVTABLE,pfnFree} */
static DECLCALLBACK(void) rtAsn1DefaultAllocator_Free(PCRTASN1ALLOCATORVTABLE pThis, PRTASN1ALLOCATION pAllocation, void *pv)
{
    RT_NOREF_PV(pThis);
    RTMemFree(pv);
    pAllocation->cbAllocated = 0;
}


/** @interface_method_impl{RTASN1ALLOCATORVTABLE,pfnAlloc} */
static DECLCALLBACK(int)  rtAsn1DefaultAllocator_Alloc(PCRTASN1ALLOCATORVTABLE pThis, PRTASN1ALLOCATION pAllocation,
                                                       void **ppv, size_t cb)
{
    size_t cbAlloc = rtAsn1DefaultAllocator_AlignSize(cb);
    void *pv = RTMemAllocZ(cbAlloc);
    if (pv)
    {
        *ppv = pv;
        pAllocation->cbAllocated = (uint32_t)cbAlloc;
        return VINF_SUCCESS;
    }
    RT_NOREF_PV(pThis);
    return VERR_NO_MEMORY;
}


/** @interface_method_impl{RTASN1ALLOCATORVTABLE,pfnRealloc} */
static DECLCALLBACK(int)  rtAsn1DefaultAllocator_Realloc(PCRTASN1ALLOCATORVTABLE pThis, PRTASN1ALLOCATION pAllocation,
                                                         void *pvOld, void **ppvNew, size_t cbNew)
{
    Assert(pvOld);
    Assert(cbNew);
    size_t cbAlloc = rtAsn1DefaultAllocator_AlignSize(cbNew);
    void *pv = RTMemRealloc(pvOld, cbAlloc);
    if (pv)
    {
        *ppvNew = pv;
        pAllocation->cbAllocated = (uint32_t)cbAlloc;
        return VINF_SUCCESS;
    }
    RT_NOREF_PV(pThis);
    return VERR_NO_MEMORY;
}


/** @interface_method_impl{RTASN1ALLOCATORVTABLE,pfnFreeArray} */
static DECLCALLBACK(void) rtAsn1DefaultAllocator_FreeArray(PCRTASN1ALLOCATORVTABLE pThis, PRTASN1ARRAYALLOCATION pAllocation,
                                                           void **papvArray)
{
    RT_NOREF_PV(pThis);
    Assert(papvArray);
    Assert(pAllocation->cbEntry);

    uint32_t i = pAllocation->cEntriesAllocated;
    while (i-- > 0)
        RTMemFree(papvArray[i]);
    RTMemFree(papvArray);

    pAllocation->cEntriesAllocated  = 0;
    pAllocation->cPointersAllocated = 0;
}


/** @interface_method_impl{RTASN1ALLOCATORVTABLE,pfnGrowArray} */
static DECLCALLBACK(int) rtAsn1DefaultAllocator_GrowArray(PCRTASN1ALLOCATORVTABLE pThis, PRTASN1ARRAYALLOCATION pAllocation,
                                                          void ***ppapvArray, uint32_t cMinEntries)
{
    RT_NOREF_PV(pThis);

    /*
     * Resize the pointer array.  We do chunks of 64 bytes for now.
     */
    void **papvArray = *ppapvArray;
    uint32_t cPointers = RT_ALIGN_32(cMinEntries, 64 / sizeof(void *));
    if (cPointers > pAllocation->cPointersAllocated)
    {
        void *pvPointers = RTMemRealloc(papvArray, cPointers * sizeof(void *));
        if (pvPointers)
        { /* likely */ }
        else if (cMinEntries > pAllocation->cPointersAllocated)
        {
            cPointers  = cMinEntries;
            pvPointers = RTMemRealloc(*ppapvArray, cPointers * sizeof(void *));
            if (!pvPointers)
                return VERR_NO_MEMORY;
        }
        else
        {
            cPointers  = pAllocation->cPointersAllocated;
            pvPointers = papvArray;
        }

        *ppapvArray = papvArray = (void **)pvPointers;
        RT_BZERO(&papvArray[pAllocation->cPointersAllocated], (cPointers - pAllocation->cPointersAllocated) * sizeof(void *));
        pAllocation->cPointersAllocated = cPointers;
    }

    /*
     * Add more entries.  Do multiple as the array grows.
     *
     * Note! We could possibly optimize this by allocating slabs of entries and
     *       slice them up.  However, keep things as simple as possible for now.
     */
    uint32_t cEntries = cMinEntries;
    if (cEntries > 2)
    {
        if (cEntries > 8)
            cEntries = RT_ALIGN_32(cEntries, 4);
        else
            cEntries = RT_ALIGN_32(cEntries, 2);
        cEntries = RT_MIN(cEntries, cPointers);
        Assert(cEntries >= cMinEntries);
    }
    Assert(cEntries <= pAllocation->cPointersAllocated);

    while (pAllocation->cEntriesAllocated < cEntries)
    {
        void *pv;
        papvArray[pAllocation->cEntriesAllocated] = pv = RTMemAllocZ(pAllocation->cbEntry);
        if (pv)
            pAllocation->cEntriesAllocated++;
        else if (pAllocation->cEntriesAllocated >= cMinEntries)
            break;
        else
            return VERR_NO_MEMORY;
    }

    return VINF_SUCCESS;
}


/** @interface_method_impl{RTASN1ALLOCATORVTABLE,pfnShrinkArray} */
static DECLCALLBACK(void) rtAsn1DefaultAllocator_ShrinkArray(PCRTASN1ALLOCATORVTABLE pThis, PRTASN1ARRAYALLOCATION pAllocation,
                                                             void ***ppapvArray, uint32_t cNew, uint32_t cCurrent)
{
    RT_NOREF_PV(pThis);

    /*
     * For now we only zero the entries being removed.
     */
    void **papvArray = *ppapvArray;
    while (cNew < cCurrent)
    {
        RT_BZERO(papvArray[cNew], pAllocation->cbEntry);
        cNew++;
    }
}



/** The default ASN.1 allocator. */
#if 1 || !defined(IN_RING3) || defined(DOXYGEN_RUNNING)
RT_DECL_DATA_CONST(RTASN1ALLOCATORVTABLE const) g_RTAsn1DefaultAllocator =
#else
RT_DECL_DATA_CONST(RTASN1ALLOCATORVTABLE const) g_RTAsn1DefaultAllocatorDisabled =
#endif
{
    rtAsn1DefaultAllocator_Free,
    rtAsn1DefaultAllocator_Alloc,
    rtAsn1DefaultAllocator_Realloc,
    rtAsn1DefaultAllocator_FreeArray,
    rtAsn1DefaultAllocator_GrowArray,
    rtAsn1DefaultAllocator_ShrinkArray
};

