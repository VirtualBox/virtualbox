/* $Id: asn1-efence-allocator.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - ASN.1, Electric Fense Allocator.
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


/** @interface_method_impl{RTASN1ALLOCATORVTABLE,pfnFree} */
static DECLCALLBACK(void) rtAsn1EFenceAllocator_Free(PCRTASN1ALLOCATORVTABLE pThis, PRTASN1ALLOCATION pAllocation, void *pv)
{
    RT_NOREF_PV(pThis);
    RTMemEfFreeNP(pv);
    pAllocation->cbAllocated = 0;
}


/** @interface_method_impl{RTASN1ALLOCATORVTABLE,pfnAlloc} */
static DECLCALLBACK(int)  rtAsn1EFenceAllocator_Alloc(PCRTASN1ALLOCATORVTABLE pThis, PRTASN1ALLOCATION pAllocation,
                                                      void **ppv, size_t cb)
{
    void *pv = RTMemEfAllocZNP(cb, RTMEM_TAG);
    if (pv)
    {
        *ppv = pv;
        pAllocation->cbAllocated = (uint32_t)cb;
        return VINF_SUCCESS;
    }
    RT_NOREF_PV(pThis);
    return VERR_NO_MEMORY;
}


/** @interface_method_impl{RTASN1ALLOCATORVTABLE,pfnRealloc} */
static DECLCALLBACK(int)  rtAsn1EFenceAllocator_Realloc(PCRTASN1ALLOCATORVTABLE pThis, PRTASN1ALLOCATION pAllocation,
                                                        void *pvOld, void **ppvNew, size_t cbNew)
{
    Assert(pvOld);
    Assert(cbNew);
    void *pv = RTMemEfReallocNP(pvOld, cbNew, RTMEM_TAG);
    if (pv)
    {
        *ppvNew = pv;
        pAllocation->cbAllocated = (uint32_t)cbNew;
        return VINF_SUCCESS;
    }
    RT_NOREF_PV(pThis);
    return VERR_NO_MEMORY;
}


/** @interface_method_impl{RTASN1ALLOCATORVTABLE,pfnFreeArray} */
static DECLCALLBACK(void) rtAsn1EFenceAllocator_FreeArray(PCRTASN1ALLOCATORVTABLE pThis, PRTASN1ARRAYALLOCATION pAllocation,
                                                          void **papvArray)
{
    RT_NOREF_PV(pThis);
    Assert(papvArray);
    Assert(pAllocation->cbEntry);
    Assert(pAllocation->cEntriesAllocated <= pAllocation->cPointersAllocated);

    uint32_t i = pAllocation->cEntriesAllocated;
    while (i-- > 0)
    {
        RTMemEfFreeNP(papvArray[i]);
        papvArray[i] = NULL;
    }
    RTMemEfFreeNP(papvArray);

    pAllocation->cEntriesAllocated  = 0;
    pAllocation->cPointersAllocated = 0;
}


/** @interface_method_impl{RTASN1ALLOCATORVTABLE,pfnGrowArray} */
static DECLCALLBACK(int) rtAsn1EFenceAllocator_GrowArray(PCRTASN1ALLOCATORVTABLE pThis, PRTASN1ARRAYALLOCATION pAllocation,
                                                         void ***ppapvArray, uint32_t cMinEntries)
{
    RT_NOREF_PV(pThis);
    Assert(pAllocation->cbEntry);
    Assert(pAllocation->cEntriesAllocated <= pAllocation->cPointersAllocated);

    /*
     * Resize the pointer array.
     */
    void **papvArray = *ppapvArray;
    void *pvPointers = RTMemEfReallocNP(papvArray, cMinEntries * sizeof(void *), RTMEM_TAG);
    if (pvPointers)
    {
        *ppapvArray = papvArray = (void **)pvPointers;
        if (cMinEntries > pAllocation->cPointersAllocated) /* possible on multiple shrink failures */
            RT_BZERO(&papvArray[pAllocation->cPointersAllocated],
                     (cMinEntries - pAllocation->cPointersAllocated) * sizeof(void *));
        else
            AssertFailed();
        pAllocation->cPointersAllocated = cMinEntries;
    }
    else if (cMinEntries > pAllocation->cPointersAllocated)
        return VERR_NO_MEMORY;
    /* else: possible but unlikely */

    /*
     * Add more entries.
     */
    while (pAllocation->cEntriesAllocated < cMinEntries)
    {
        void *pv;
        papvArray[pAllocation->cEntriesAllocated] = pv = RTMemEfAllocZNP(pAllocation->cbEntry, RTMEM_TAG);
        if (pv)
            pAllocation->cEntriesAllocated++;
        else
            return VERR_NO_MEMORY;
    }

    return VINF_SUCCESS;
}


/** @interface_method_impl{RTASN1ALLOCATORVTABLE,pfnShrinkArray} */
static DECLCALLBACK(void) rtAsn1EFenceAllocator_ShrinkArray(PCRTASN1ALLOCATORVTABLE pThis, PRTASN1ARRAYALLOCATION pAllocation,
                                                            void ***ppapvArray, uint32_t cNew, uint32_t cCurrent)
{
    RT_NOREF_PV(pThis);
    Assert(pAllocation->cbEntry);
    Assert(pAllocation->cEntriesAllocated <= pAllocation->cPointersAllocated);

    /*
     * We always free and resize.
     */
    Assert(pAllocation->cEntriesAllocated == cCurrent);
    Assert(cNew < cCurrent);

    /* Free entries. */
    void **papvArray = *ppapvArray;
    while (cCurrent-- > cNew)
    {
        RTMemEfFreeNP(papvArray[cCurrent]);
        papvArray[cCurrent] = NULL;
    }
    pAllocation->cEntriesAllocated = cNew;

    /* Try resize pointer array.  Failure here is a genuine possibility since the
       efence code will try allocate a new block.  This causes extra fun in the
       grow method above. */
    void *pvPointers = RTMemEfReallocNP(papvArray, cNew * sizeof(void *), RTMEM_TAG);
    if (pvPointers)
    {
        *ppapvArray = (void **)pvPointers;
        pAllocation->cPointersAllocated = cNew;
    }
}


/** The Electric Fence ASN.1 allocator. */
RT_DECL_DATA_CONST(RTASN1ALLOCATORVTABLE const) g_RTAsn1EFenceAllocator =
{
    rtAsn1EFenceAllocator_Free,
    rtAsn1EFenceAllocator_Alloc,
    rtAsn1EFenceAllocator_Realloc,
    rtAsn1EFenceAllocator_FreeArray,
    rtAsn1EFenceAllocator_GrowArray,
    rtAsn1EFenceAllocator_ShrinkArray
};

#if 0 && defined(IN_RING3) /* for efence testing */
RT_DECL_DATA_CONST(RTASN1ALLOCATORVTABLE const) g_RTAsn1DefaultAllocator =
{
    rtAsn1EFenceAllocator_Free,
    rtAsn1EFenceAllocator_Alloc,
    rtAsn1EFenceAllocator_Realloc,
    rtAsn1EFenceAllocator_FreeArray,
    rtAsn1EFenceAllocator_GrowArray,
    rtAsn1EFenceAllocator_ShrinkArray
};
#endif

