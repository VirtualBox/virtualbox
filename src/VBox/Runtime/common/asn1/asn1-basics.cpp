/* $Id: asn1-basics.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - ASN.1, Basic Operations.
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

#include <iprt/alloca.h>
#include <iprt/bignum.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/uni.h>

#include <iprt/formats/asn1.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * ASN.1 content/value allocation.
 *
 * The currently most frequent use of the RTAsn1 module is to decode ASN.1 byte
 * streams.  In that scenario we do not allocate memory for the raw content
 * bytes, but share it with the byte stream.  Also, a great number of RTASN1CORE
 * structures will never need to have any content bytes allocated with this.
 *
 * So, in order to avoid adding an extra 16 (64-bit) or 8 (32-bit) bytes to each
 * RTASN1CORE structure just to keep track of the occational content allocation,
 * we put the allocator tracking structure inside the allocation.  During
 * allocator operations it lives temporarily on the stack.
 */
typedef struct RTASN1MEMCONTENT
{
    /** The allocation tracker. */
    RTASN1ALLOCATION    Allocation;
#if ARCH_BITS == 32
    uint32_t            Padding; /**< Alignment padding. */
#endif
    /** The content bytes, i.e. what RTASN1CORE::uData.pv points to.  Use a 64-bit
     * type here to emphasize that it's 8-byte aligned on all platforms.  */
    uint64_t            au64Content[1];
} RTASN1MEMCONTENT;
AssertCompileMemberAlignment(RTASN1MEMCONTENT, au64Content, 8);
/** Pointer to a ASN.1 content allocation. */
typedef RTASN1MEMCONTENT *PRTASN1MEMCONTENT;



RTDECL(int) RTAsn1MemResizeArray(PRTASN1ARRAYALLOCATION pAllocation, void ***ppapvArray, uint32_t cCurrent, uint32_t cNew)
{
    AssertReturn(pAllocation->pAllocator != NULL, VERR_WRONG_ORDER);
    AssertReturn(pAllocation->cbEntry > 0, VERR_WRONG_ORDER);
    AssertReturn(cCurrent <= pAllocation->cEntriesAllocated, VERR_INVALID_PARAMETER);
    AssertReturn(cCurrent <= pAllocation->cPointersAllocated, VERR_INVALID_PARAMETER);
    AssertReturn(cNew < _1M, VERR_OUT_OF_RANGE);
    Assert(pAllocation->cEntriesAllocated <= pAllocation->cPointersAllocated);

    /*
     * Is there sufficent space allocated already?
     *
     * We keep unused entires ZEROed, therefore we must always call the allocator
     * when shrinking (this also helps with the electric fence allocator).
     */
    if (cNew <= pAllocation->cEntriesAllocated)
    {
        if (cCurrent <= cNew)
            return VINF_SUCCESS;
        pAllocation->pAllocator->pfnShrinkArray(pAllocation->pAllocator, pAllocation, ppapvArray, cCurrent, cNew);
        return VINF_SUCCESS;
    }

    /*
     * Must grow (or do initial alloc).
     */
    pAllocation->cResizeCalls++;
    return pAllocation->pAllocator->pfnGrowArray(pAllocation->pAllocator, pAllocation, ppapvArray, cNew);
}


RTDECL(void) RTAsn1MemFreeArray(PRTASN1ARRAYALLOCATION pAllocation, void **papvArray)
{
    Assert(pAllocation->pAllocator != NULL);
    if (papvArray)
    {
        pAllocation->pAllocator->pfnFreeArray(pAllocation->pAllocator, pAllocation, papvArray);
        Assert(pAllocation->cPointersAllocated == 0);
        Assert(pAllocation->cEntriesAllocated == 0);
    }
}


RTDECL(int) RTAsn1MemAllocZ(PRTASN1ALLOCATION pAllocation, void **ppvMem, size_t cbMem)
{
    AssertReturn(pAllocation->pAllocator != NULL, VERR_WRONG_ORDER);
    AssertPtr(ppvMem);
    Assert(cbMem > 0);
    int rc = pAllocation->pAllocator->pfnAlloc(pAllocation->pAllocator, pAllocation, ppvMem, cbMem);
    Assert(pAllocation->cbAllocated >= cbMem || RT_FAILURE_NP(rc));
    return rc;
}


RTDECL(int) RTAsn1MemDup(PRTASN1ALLOCATION pAllocation, void **ppvMem, const void *pvSrc, size_t cbMem)
{
    AssertReturn(pAllocation->pAllocator != NULL, VERR_WRONG_ORDER);
    AssertPtr(ppvMem);
    AssertPtr(pvSrc);
    Assert(cbMem > 0);
    int rc = pAllocation->pAllocator->pfnAlloc(pAllocation->pAllocator, pAllocation, ppvMem, cbMem);
    if (RT_SUCCESS(rc))
    {
        Assert(pAllocation->cbAllocated >= cbMem);
        memcpy(*ppvMem, pvSrc, cbMem);
        return VINF_SUCCESS;
    }
    return rc;
}


RTDECL(void) RTAsn1MemFree(PRTASN1ALLOCATION pAllocation, void *pv)
{
    Assert(pAllocation->pAllocator != NULL);
    if (pv)
    {
        pAllocation->pAllocator->pfnFree(pAllocation->pAllocator, pAllocation, pv);
        Assert(pAllocation->cbAllocated == 0);
    }
}


RTDECL(PRTASN1ALLOCATION) RTAsn1MemInitAllocation(PRTASN1ALLOCATION pAllocation, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    pAllocation->cbAllocated = 0;
    pAllocation->cReallocs   = 0;
    pAllocation->uReserved0  = 0;
    pAllocation->pAllocator  = pAllocator;
    return pAllocation;
}


RTDECL(PRTASN1ARRAYALLOCATION) RTAsn1MemInitArrayAllocation(PRTASN1ARRAYALLOCATION pAllocation,
                                                            PCRTASN1ALLOCATORVTABLE pAllocator, size_t cbEntry)
{
    Assert(cbEntry >= sizeof(RTASN1CORE));
    Assert(cbEntry < _1M);
    Assert(RT_ALIGN_Z(cbEntry, sizeof(void *)) == cbEntry);
    pAllocation->cbEntry            = (uint32_t)cbEntry;
    pAllocation->cPointersAllocated = 0;
    pAllocation->cEntriesAllocated  = 0;
    pAllocation->cResizeCalls       = 0;
    pAllocation->uReserved0         = 0;
    pAllocation->pAllocator         = pAllocator;
    return pAllocation;
}


RTDECL(int) RTAsn1ContentAllocZ(PRTASN1CORE pAsn1Core, size_t cb, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    AssertReturn(pAllocator != NULL, VERR_WRONG_ORDER);
    AssertReturn(cb > 0 && cb < _1G, VERR_INVALID_PARAMETER);
    AssertPtr(pAsn1Core);
    AssertReturn(!(pAsn1Core->fFlags & RTASN1CORE_F_ALLOCATED_CONTENT), VERR_INVALID_STATE);

    /* Initialize the temporary allocation tracker. */
    RTASN1ALLOCATION Allocation;
    Allocation.cbAllocated = 0;
    Allocation.cReallocs   = 0;
    Allocation.uReserved0  = 0;
    Allocation.pAllocator  = pAllocator;

    /* Make the allocation. */
    uint32_t            cbAlloc = RT_UOFFSETOF(RTASN1MEMCONTENT, au64Content) + (uint32_t)cb;
    PRTASN1MEMCONTENT   pHdr;
    int rc = pAllocator->pfnAlloc(pAllocator, &Allocation, (void **)&pHdr, cbAlloc);
    if (RT_SUCCESS(rc))
    {
        Assert(Allocation.cbAllocated >= cbAlloc);
        pHdr->Allocation = Allocation;
        pAsn1Core->cb       = (uint32_t)cb;
        pAsn1Core->uData.pv = &pHdr->au64Content[0];
        pAsn1Core->fFlags  |= RTASN1CORE_F_ALLOCATED_CONTENT;
    }

    return rc;
}


RTDECL(int) RTAsn1ContentDup(PRTASN1CORE pAsn1Core, void const *pvSrc, size_t cbSrc, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    int rc = RTAsn1ContentAllocZ(pAsn1Core, cbSrc, pAllocator);
    if (RT_SUCCESS(rc))
        memcpy((void *)pAsn1Core->uData.pv, pvSrc, cbSrc);
    return rc;
}


RTDECL(int) RTAsn1ContentReallocZ(PRTASN1CORE pAsn1Core, size_t cb, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    /* Validate input. */
    AssertPtr(pAsn1Core);
    AssertReturn(cb < _1G, VERR_INVALID_PARAMETER);

    if (cb > 0)
    {
        /*
         * Case 1 - Initial allocation.
         */
        uint32_t cbNeeded  = RT_UOFFSETOF(RTASN1MEMCONTENT, au64Content) + (uint32_t)cb;
        if (!(pAsn1Core->fFlags & RTASN1CORE_F_ALLOCATED_CONTENT))
            return RTAsn1ContentAllocZ(pAsn1Core, cb, pAllocator);

        /* Locate the header. */
        PRTASN1MEMCONTENT pHdr = RT_FROM_MEMBER(pAsn1Core->uData.pv, RTASN1MEMCONTENT, au64Content);

        /*
         * Case 2 - Reallocation using the same allocator.
         */
        if (   pHdr->Allocation.pAllocator == pAllocator
            || !pAllocator)
        {
            pHdr->Allocation.cReallocs++;

            /* Modify the allocation if necessary. */
            if (pHdr->Allocation.cbAllocated < cbNeeded)
            {
                RTASN1ALLOCATION Allocation = pHdr->Allocation;
                int rc = Allocation.pAllocator->pfnRealloc(Allocation.pAllocator, &Allocation, pHdr, (void **)&pHdr, cbNeeded);
                if (RT_FAILURE(rc))
                    return rc;
                Assert(Allocation.cbAllocated >= cbNeeded);
                pAsn1Core->uData.pv = &pHdr->au64Content[0];
                pHdr->Allocation    = Allocation;
            }

            /* Clear any additional memory we're letting the user use and
               update the content size. */
            if (pAsn1Core->cb < cb)
                RT_BZERO((uint8_t *)&pAsn1Core->uData.pu8[pAsn1Core->cb], cb - pAsn1Core->cb);
            pAsn1Core->cb = (uint32_t)cb;
        }
        /*
         * Case 3 - Reallocation using a different allocator.
         */
        else
        {
            /* Initialize the temporary allocation tracker. */
            RTASN1ALLOCATION Allocation;
            Allocation.cbAllocated = 0;
            Allocation.cReallocs   = pHdr->Allocation.cReallocs + 1;
            Allocation.uReserved0  = 0;
            Allocation.pAllocator  = pAllocator;

            /* Make the allocation. */
            PRTASN1MEMCONTENT pHdrNew;
            int rc = pAllocator->pfnAlloc(pAllocator, &Allocation, (void **)&pHdrNew, cbNeeded);
            if (RT_FAILURE(rc))
                return rc;
            Assert(Allocation.cbAllocated >= cbNeeded);

            /* Duplicate the old content and zero any new memory we might've added. */
            if (pAsn1Core->cb >= cb)
                memcpy(&pHdrNew->au64Content[0], &pHdr->au64Content[0], cb);
            else
            {
                memcpy(&pHdrNew->au64Content[0], &pHdr->au64Content[0], pAsn1Core->cb);
                RT_BZERO((uint8_t *)&pHdrNew->au64Content[0] + pAsn1Core->cb, cb - pAsn1Core->cb);
            }

            /* Update the core. */
            pHdrNew->Allocation = Allocation;
            pAsn1Core->uData.pv = &pHdrNew->au64Content[0];
            pAsn1Core->fFlags  |= RTASN1CORE_F_ALLOCATED_CONTENT; /* free cleared it. */
            pAsn1Core->cb       = (uint32_t)cb;

            /* Free the old content. */
            Allocation = pHdr->Allocation;
            Allocation.pAllocator->pfnFree(Allocation.pAllocator, &Allocation, pHdr);
            Assert(Allocation.cbAllocated == 0);
        }
    }
    /*
     * Case 4 - It's a request to free the memory.
     */
    else
        RTAsn1ContentFree(pAsn1Core);
    return VINF_SUCCESS;
}


RTDECL(void) RTAsn1ContentFree(PRTASN1CORE pAsn1Core)
{
    if (pAsn1Core)
    {
        pAsn1Core->cb = 0;
        if (pAsn1Core->fFlags & RTASN1CORE_F_ALLOCATED_CONTENT)
        {
            pAsn1Core->fFlags &= ~RTASN1CORE_F_ALLOCATED_CONTENT;
            AssertReturnVoid(pAsn1Core->uData.pv);

            PRTASN1MEMCONTENT pHdr       = RT_FROM_MEMBER(pAsn1Core->uData.pv, RTASN1MEMCONTENT, au64Content);
            RTASN1ALLOCATION  Allocation = pHdr->Allocation;

            Allocation.pAllocator->pfnFree(Allocation.pAllocator, &Allocation, pHdr);
            Assert(Allocation.cbAllocated == 0);
        }
        pAsn1Core->uData.pv = NULL;
    }
}



/*
 * Virtual method table based API.
 */

RTDECL(void) RTAsn1VtDelete(PRTASN1CORE pThisCore)
{
    if (pThisCore)
    {
        PCRTASN1COREVTABLE pOps = pThisCore->pOps;
        if (pOps)
            pOps->pfnDtor(pThisCore);
    }
}


/**
 * Context data passed by RTAsn1VtDeepEnum to it's worker callbacks.
 */
typedef struct RTASN1DEEPENUMCTX
{
    PFNRTASN1ENUMCALLBACK   pfnCallback;
    void                   *pvUser;
} RTASN1DEEPENUMCTX;


static DECLCALLBACK(int) rtAsn1VtDeepEnumDepthFirst(PRTASN1CORE pThisCore, const char *pszName, uint32_t uDepth, void *pvUser)
{
    AssertReturn(pThisCore, VINF_SUCCESS);

    if (pThisCore->pOps && pThisCore->pOps->pfnEnum)
    {
        int rc = pThisCore->pOps->pfnEnum(pThisCore, rtAsn1VtDeepEnumDepthFirst, uDepth, pvUser);
        if (rc != VINF_SUCCESS)
            return rc;
    }

    RTASN1DEEPENUMCTX *pCtx = (RTASN1DEEPENUMCTX *)pvUser;
    return pCtx->pfnCallback(pThisCore, pszName, uDepth, pCtx->pvUser);
}


static DECLCALLBACK(int) rtAsn1VtDeepEnumDepthLast(PRTASN1CORE pThisCore, const char *pszName, uint32_t uDepth, void *pvUser)
{
    AssertReturn(pThisCore, VINF_SUCCESS);

    RTASN1DEEPENUMCTX *pCtx = (RTASN1DEEPENUMCTX *)pvUser;
    int rc = pCtx->pfnCallback(pThisCore, pszName, uDepth, pCtx->pvUser);
    if (rc == VINF_SUCCESS)
    {
        if (pThisCore->pOps && pThisCore->pOps->pfnEnum)
            rc = pThisCore->pOps->pfnEnum(pThisCore, rtAsn1VtDeepEnumDepthFirst, uDepth, pvUser);
    }
    return rc;
}


RTDECL(int) RTAsn1VtDeepEnum(PRTASN1CORE pThisCore, bool fDepthFirst, uint32_t uDepth,
                             PFNRTASN1ENUMCALLBACK pfnCallback, void *pvUser)
{
    int rc;
    if (RTAsn1Core_IsPresent(pThisCore))
    {
        PCRTASN1COREVTABLE pOps = pThisCore->pOps;
        if (pOps && pOps->pfnEnum)
        {
            RTASN1DEEPENUMCTX Ctx;
            Ctx.pfnCallback = pfnCallback;
            Ctx.pvUser      = pvUser;
            rc = pOps->pfnEnum(pThisCore, fDepthFirst ? rtAsn1VtDeepEnumDepthFirst : rtAsn1VtDeepEnumDepthLast, uDepth, &Ctx);
        }
        else
            rc = VINF_SUCCESS;
    }
    else
        rc = VINF_SUCCESS;
    return rc;
}


RTDECL(int) RTAsn1VtClone(PRTASN1CORE pThisCore, PRTASN1CORE pSrcCore, PCRTASN1ALLOCATORVTABLE pAllocator)
{
    AssertPtrReturn(pThisCore,  VERR_INVALID_POINTER);
    AssertPtrReturn(pSrcCore,   VERR_INVALID_POINTER);
    AssertPtrReturn(pAllocator, VERR_INVALID_POINTER);

    if (RTAsn1Core_IsPresent(pSrcCore))
    {
        AssertPtrReturn(pSrcCore->pOps, VERR_INVALID_POINTER);
        AssertPtr(pSrcCore->pOps->pfnClone);
        return pSrcCore->pOps->pfnClone(pThisCore, pSrcCore, pAllocator);
    }

    RT_ZERO(*pThisCore);
    return VINF_SUCCESS;
}


RTDECL(int) RTAsn1VtCompare(PCRTASN1CORE pLeftCore, PCRTASN1CORE pRightCore)
{
    int iDiff;
    if (RTAsn1Core_IsPresent(pLeftCore))
    {
        if (RTAsn1Core_IsPresent(pRightCore))
        {
            PCRTASN1COREVTABLE pOps = pLeftCore->pOps;
            if (pOps == pRightCore->pOps)
            {
                AssertPtr(pOps->pfnCompare);
                iDiff = pOps->pfnCompare(pLeftCore, pRightCore);
            }
            else
                iDiff = (uintptr_t)pOps < (uintptr_t)pRightCore->pOps ? -1 : 1;
        }
        else
            iDiff = 1;
    }
    else
        iDiff = 0 - (int)RTAsn1Core_IsPresent(pRightCore);
    return iDiff;
}


RTDECL(int) RTAsn1VtCheckSanity(PCRTASN1CORE pThisCore, uint32_t fFlags,
                                PRTERRINFO pErrInfo, const char *pszErrorTag)
{
    int rc;
    if (RTAsn1Core_IsPresent(pThisCore))
    {
        PCRTASN1COREVTABLE pOps = pThisCore->pOps;
        if (pOps && pOps->pfnCheckSanity)
            rc = pOps->pfnCheckSanity(pThisCore, fFlags, pErrInfo, pszErrorTag);
        else if (pOps)
            rc = RTErrInfoSetF(pErrInfo, VERR_ASN1_NO_CHECK_SANITY_METHOD,
                               "%s: Has no pfnCheckSanity function.", pszErrorTag);
        else
            rc = RTErrInfoSetF(pErrInfo, VERR_ASN1_NO_VTABLE, "%s: Has no Vtable function.", pszErrorTag);
    }
    else
        rc = RTErrInfoSetF(pErrInfo, VERR_ASN1_NOT_PRESENT, "%s: Not present.", pszErrorTag);
    return rc;
}



/*
 * Dummy ASN.1 object.
 */

RTDECL(int) RTAsn1Dummy_InitEx(PRTASN1DUMMY pThis)
{
    return RTAsn1Core_InitEx(&pThis->Asn1Core,
                             UINT32_MAX,
                             ASN1_TAGCLASS_PRIVATE | ASN1_TAGFLAG_CONSTRUCTED,
                             NULL,
                             RTASN1CORE_F_DUMMY);
}


/*
 * ASN.1 SEQUENCE OF object.
 */

RTDECL(int) RTAsn1SeqOfCore_Init(PRTASN1SEQOFCORE pThis, PCRTASN1COREVTABLE pVtable)
{
    return RTAsn1Core_InitEx(&pThis->Asn1Core,
                             ASN1_TAG_SEQUENCE,
                             ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_CONSTRUCTED,
                             pVtable,
                             RTASN1CORE_F_PRESENT);
}


RTDECL(int) RTAsn1SeqOfCore_Clone(PRTASN1SEQOFCORE pThis, PCRTASN1COREVTABLE pVtable, PCRTASN1SEQOFCORE pSrc)
{
    AssertReturn(pSrc->Asn1Core.pOps == pVtable, VERR_ASN1_INTERNAL_ERROR_5);
    return RTAsn1Core_CloneNoContent(&pThis->Asn1Core, &pSrc->Asn1Core);
}


/*
 * ASN.1 SET OF object.
 */

RTDECL(int) RTAsn1SetOfCore_Init(PRTASN1SETOFCORE pThis, PCRTASN1COREVTABLE pVtable)
{
    return RTAsn1Core_InitEx(&pThis->Asn1Core,
                             ASN1_TAG_SET,
                             ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_CONSTRUCTED,
                             pVtable,
                             RTASN1CORE_F_PRESENT);
}


RTDECL(int) RTAsn1SetOfCore_Clone(PRTASN1SETOFCORE pThis, PCRTASN1COREVTABLE pVtable, PCRTASN1SETOFCORE pSrc)
{
    AssertReturn(pSrc->Asn1Core.pOps == pVtable, VERR_ASN1_INTERNAL_ERROR_5);
    return RTAsn1Core_CloneNoContent(&pThis->Asn1Core, &pSrc->Asn1Core);
}


/*
 * ASN.1 SEQUENCE object.
 */

RTDECL(int) RTAsn1SequenceCore_Init(PRTASN1SEQUENCECORE pThis, PCRTASN1COREVTABLE pVtable)
{
    return RTAsn1Core_InitEx(&pThis->Asn1Core,
                             ASN1_TAG_SEQUENCE,
                             ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_CONSTRUCTED,
                             pVtable,
                             RTASN1CORE_F_PRESENT);
}


RTDECL(int) RTAsn1SequenceCore_Clone(PRTASN1SEQUENCECORE pThis, PCRTASN1COREVTABLE pVtable, PCRTASN1SEQUENCECORE pSrc)
{
    AssertReturn(pSrc->Asn1Core.pOps == pVtable, VERR_ASN1_INTERNAL_ERROR_5);
    return RTAsn1Core_CloneNoContent(&pThis->Asn1Core, &pSrc->Asn1Core);
}


/*
 * ASN.1 SEQUENCE object - only used by SPC, so probably doing something wrong there.
 */

RTDECL(int) RTAsn1SetCore_Init(PRTASN1SETCORE pThis, PCRTASN1COREVTABLE pVtable)
{
    return RTAsn1Core_InitEx(&pThis->Asn1Core,
                             ASN1_TAG_SET,
                             ASN1_TAGCLASS_UNIVERSAL | ASN1_TAGFLAG_CONSTRUCTED,
                             pVtable,
                             RTASN1CORE_F_PRESENT);
}


RTDECL(int) RTAsn1SetCore_Clone(PRTASN1SETCORE pThis, PCRTASN1COREVTABLE pVtable, PCRTASN1SETCORE pSrc)
{
    AssertReturn(pSrc->Asn1Core.pOps == pVtable, VERR_ASN1_INTERNAL_ERROR_5);
    return RTAsn1Core_CloneNoContent(&pThis->Asn1Core, &pSrc->Asn1Core);
}


/*
 * ASN.1 Context Tag object.
 */

RTDECL(int) RTAsn1ContextTagN_Init(PRTASN1CONTEXTTAG pThis, uint32_t uTag, PCRTASN1COREVTABLE pVtable)
{
    return RTAsn1Core_InitEx(&pThis->Asn1Core,
                             uTag,
                             ASN1_TAGCLASS_CONTEXT | ASN1_TAGFLAG_CONSTRUCTED,
                             pVtable,
                             RTASN1CORE_F_PRESENT);
}


RTDECL(int) RTAsn1ContextTagN_Clone(PRTASN1CONTEXTTAG pThis, PCRTASN1CONTEXTTAG pSrc, uint32_t uTag)
{
    Assert(pSrc->Asn1Core.uTag == uTag || !RTASN1CORE_IS_PRESENT(&pSrc->Asn1Core)); RT_NOREF_PV(uTag);
    return RTAsn1Core_CloneNoContent(&pThis->Asn1Core, &pSrc->Asn1Core);
}

