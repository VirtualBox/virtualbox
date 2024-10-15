/* $Id: memcache.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Memory Object Allocation Cache.
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
#include <iprt/memcache.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/critsect.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/system.h>

#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to a cache instance. */
typedef struct RTMEMCACHEINT  *PRTMEMCACHEINT;
/** Pointer to a cache page. */
typedef struct RTMEMCACHEPAGE *PRTMEMCACHEPAGE;



/**
 * A free object.
 *
 * @remarks This only works if the objects don't have a constructor or
 *          destructor and are big enough.
 */
typedef struct RTMEMCACHEFREEOBJ
{
    /** Pointer to the next free object  */
    struct RTMEMCACHEFREEOBJ * volatile pNext;
} RTMEMCACHEFREEOBJ;
/** Pointer to a free object. */
typedef RTMEMCACHEFREEOBJ *PRTMEMCACHEFREEOBJ;


/**
 * A cache page.
 *
 * This is a page of memory that we split up in to a bunch object sized chunks
 * and hand out to the cache users.  The bitmap is updated in an atomic fashion
 * so that we don't have to take any locks when freeing or allocating memory.
 */
typedef struct RTMEMCACHEPAGE
{
    /** Pointer to the cache owning this page.
     * This is used for validation purposes only.  */
    PRTMEMCACHEINT              pCache;
    /** Pointer to the next page.
     * This is marked as volatile since we'll be adding new entries to the list
     * without taking any locks. */
    PRTMEMCACHEPAGE volatile    pNext;
    /** Bitmap tracking allocated blocks. */
    void volatile              *pbmAlloc;
    /** Bitmap tracking which blocks that has been thru the constructor. */
    void volatile              *pbmCtor;
    /** Pointer to the object array. */
    uint8_t                    *pbObjects;
    /** The number of objects on this page.  */
    uint32_t                    cObjects;

    /** Padding to force cFree into the next cache line. (ASSUMES CL = 64) */
    uint8_t                     abPadding[ARCH_BITS == 32 ? 64 - 6*4 : 64 - 5*8 - 4];
    /** The number of free objects. */
    int32_t volatile            cFree;
} RTMEMCACHEPAGE;
AssertCompileMemberOffset(RTMEMCACHEPAGE, cFree, 64);


/**
 * Memory object cache instance.
 */
typedef struct RTMEMCACHEINT
{
    /** Magic value (RTMEMCACHE_MAGIC). */
    uint32_t                    u32Magic;
    /** The object size.  */
    uint32_t                    cbObject;
    /** Object alignment.  */
    uint32_t                    cbAlignment;
    /** The per page object count. */
    uint32_t                    cPerPage;
    /** Number of bits in the bitmap.
     * @remarks This is higher or equal to cPerPage and it is aligned such that
     *          the search operation will be most efficient on x86/AMD64. */
    uint32_t                    cBits;
    /** The maximum number of objects. */
    uint32_t                    cMax;
    /** Whether to the use the free list or not. */
    bool                        fUseFreeList;
    /** Head of the page list. */
    PRTMEMCACHEPAGE             pPageHead;
    /** Poiner to the insertion point in the page list. */
    PRTMEMCACHEPAGE volatile   *ppPageNext;
    /** Constructor callback. */
    PFNMEMCACHECTOR             pfnCtor;
    /** Destructor callback. */
    PFNMEMCACHEDTOR             pfnDtor;
    /** Callback argument. */
    void                       *pvUser;
    /** Critical section serializing page allocation and similar. */
    RTCRITSECT                  CritSect;

    /** The total object count. */
    uint32_t volatile           cTotal;
    /** The number of free objects. */
    int32_t volatile            cFree;
    /** This may point to a page with free entries. */
    PRTMEMCACHEPAGE volatile    pPageHint;
    /** Stack of free items.
     * These are marked as used in the allocation bitmaps.
     *
     * @todo This doesn't scale well when several threads are beating on the
     *       cache.  Also, it totally doesn't work when the objects are too
     *       small. */
    PRTMEMCACHEFREEOBJ volatile pFreeTop;
} RTMEMCACHEINT;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void rtMemCacheFreeList(RTMEMCACHEINT *pThis, PRTMEMCACHEFREEOBJ pHead);


RTDECL(int) RTMemCacheCreate(PRTMEMCACHE phMemCache, size_t cbObject, size_t cbAlignment, uint32_t cMaxObjects,
                             PFNMEMCACHECTOR pfnCtor, PFNMEMCACHEDTOR pfnDtor, void *pvUser, uint32_t fFlags)

{
    AssertPtr(phMemCache);
    AssertPtrNull(pfnCtor);
    AssertPtrNull(pfnDtor);
    AssertReturn(!pfnDtor || pfnCtor, VERR_INVALID_PARAMETER);
    AssertReturn(cbObject > 0, VERR_INVALID_PARAMETER);

    size_t const cbPage = RTSystemGetPageSize();
    AssertReturn(cbObject <= cbPage / 8, VERR_INVALID_PARAMETER);
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);

    if (cbAlignment == 0)
    {
        if (cbObject <= 2)
            cbAlignment = cbObject;
        else if (cbObject <= 4)
            cbAlignment = 4;
        else if (cbObject <= 8)
            cbAlignment = 8;
        else if (cbObject <= 16)
            cbAlignment = 16;
        else if (cbObject <= 32)
            cbAlignment = 32;
        else
            cbAlignment = 64;
    }
    else
    {
        AssertReturn(!((cbAlignment - 1) & cbAlignment), VERR_NOT_POWER_OF_TWO);
        AssertReturn(cbAlignment <= 64, VERR_OUT_OF_RANGE);
    }

    /*
     * Allocate and initialize the instance memory.
     */
    RTMEMCACHEINT *pThis = (RTMEMCACHEINT *)RTMemAlloc(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;
    int rc = RTCritSectInit(&pThis->CritSect);
    if (RT_FAILURE(rc))
    {
        RTMemFree(pThis);
        return rc;
    }

    pThis->u32Magic         = RTMEMCACHE_MAGIC;
    pThis->cbObject         = (uint32_t)RT_ALIGN_Z(cbObject, cbAlignment);
    pThis->cbAlignment      = (uint32_t)cbAlignment;
    pThis->cPerPage         = (uint32_t)((cbPage - RT_ALIGN_Z(sizeof(RTMEMCACHEPAGE), cbAlignment)) / pThis->cbObject);
    while (  RT_ALIGN_Z(sizeof(RTMEMCACHEPAGE), 8)
           + pThis->cPerPage * pThis->cbObject
           + RT_ALIGN(pThis->cPerPage, 64) / 8 * 2
           > cbPage)
        pThis->cPerPage--;
    pThis->cBits            = RT_ALIGN(pThis->cPerPage, 64);
    pThis->cMax             = cMaxObjects;
    pThis->fUseFreeList     = cbObject >= sizeof(RTMEMCACHEFREEOBJ)
                           && !pfnCtor
                           && !pfnDtor;
    pThis->pPageHead        = NULL;
    pThis->ppPageNext       = &pThis->pPageHead;
    pThis->pfnCtor          = pfnCtor;
    pThis->pfnDtor          = pfnDtor;
    pThis->pvUser           = pvUser;
    pThis->cTotal           = 0;
    pThis->cFree            = 0;
    pThis->pPageHint        = NULL;
    pThis->pFreeTop         = NULL;

    *phMemCache = pThis;
    return VINF_SUCCESS;
}


RTDECL(int) RTMemCacheDestroy(RTMEMCACHE hMemCache)
{
    RTMEMCACHEINT *pThis = hMemCache;
    if (!pThis)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTMEMCACHE_MAGIC, VERR_INVALID_HANDLE);

#if 0 /*def RT_STRICT - don't require eveything to be freed. Caches are very convenient for lazy cleanup. */
    uint32_t cFree = pThis->cFree;
    for (PRTMEMCACHEFREEOBJ pFree = pThis->pFreeTop; pFree && cFree < pThis->cTotal + 5; pFree = pFree->pNext)
        cFree++;
    AssertMsg(cFree == pThis->cTotal, ("cFree=%u cTotal=%u\n", cFree, pThis->cTotal));
#endif

    /*
     * Destroy it.
     */
    AssertReturn(ASMAtomicCmpXchgU32(&pThis->u32Magic, RTMEMCACHE_MAGIC_DEAD, RTMEMCACHE_MAGIC), VERR_INVALID_HANDLE);
    RTCritSectDelete(&pThis->CritSect);

    while (pThis->pPageHead)
    {
        PRTMEMCACHEPAGE pPage = pThis->pPageHead;
        pThis->pPageHead = pPage->pNext;
        pPage->cFree = 0;

        if (pThis->pfnDtor)
        {
            uint32_t iObj = pPage->cObjects;
            while (iObj-- > 0)
                if (ASMBitTestAndClear(pPage->pbmCtor, iObj))
                    pThis->pfnDtor(hMemCache, pPage->pbObjects + iObj * pThis->cbObject, pThis->pvUser);
        }

        RTMemPageFree(pPage, RTSystemGetPageSize());
    }

    RTMemFree(pThis);
    return VINF_SUCCESS;
}


/**
 * Grows the cache.
 *
 * @returns IPRT status code.
 * @param   pThis               The memory cache instance.
 */
static int rtMemCacheGrow(RTMEMCACHEINT *pThis)
{
    /*
     * Enter the critical section here to avoid allocation races leading to
     * wasted memory (++) and make it easier to link in the new page.
     */
    RTCritSectEnter(&pThis->CritSect);
    int rc = VINF_SUCCESS;
    if (pThis->cFree < 0)
    {
        /*
         * Allocate and initialize the new page.
         *
         * We put the constructor bitmap at the lower end right after cFree.
         * We then push the object array to the end of the page and place the
         * allocation bitmap below it.  The hope is to increase the chance that
         * the allocation bitmap is in a different cache line than cFree since
         * this increases performance markably when lots of threads are beating
         * on the cache.
         */
        size_t const cbPage = RTSystemGetPageSize();
        PRTMEMCACHEPAGE pPage = (PRTMEMCACHEPAGE)RTMemPageAllocZ(cbPage);
        if (pPage)
        {
            uint32_t const cObjects = RT_MIN(pThis->cPerPage, pThis->cMax - pThis->cTotal);

            pPage->pCache       = pThis;
            pPage->pNext        = NULL;
            pPage->cFree        = cObjects;
            pPage->cObjects     = cObjects;
            uint8_t *pb = (uint8_t *)(pPage + 1);
            pb = RT_ALIGN_PT(pb, 8, uint8_t *);
            pPage->pbmCtor      = pb;
            pb = (uint8_t *)pPage + cbPage - pThis->cbObject * cObjects;
            pPage->pbObjects    = pb;   Assert(RT_ALIGN_P(pb, pThis->cbAlignment) == pb);
            pb -= pThis->cBits / 8;
            pb = (uint8_t *)((uintptr_t)pb & ~(uintptr_t)7);
            pPage->pbmAlloc     = pb;
            Assert((uintptr_t)pPage->pbmCtor + pThis->cBits / 8 <= (uintptr_t)pPage->pbmAlloc);

            /* Mark the bitmap padding and any unused objects as allocated. */
            for (uint32_t iBit = cObjects; iBit < pThis->cBits; iBit++)
                ASMBitSet(pPage->pbmAlloc, iBit);

            /* Make it the hint. */
            ASMAtomicWritePtr(&pThis->pPageHint, pPage);

            /* Link the page in at the end of the list. */
            ASMAtomicWritePtr(pThis->ppPageNext, pPage);
            pThis->ppPageNext = &pPage->pNext;

            /* Add it to the page counts. */
            ASMAtomicAddS32(&pThis->cFree, cObjects);
            ASMAtomicAddU32(&pThis->cTotal, cObjects);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    RTCritSectLeave(&pThis->CritSect);
    return rc;
}


/**
 * Grabs a an object in a page.
 * @returns New cFree value on success (0 or higher), -1 on failure.
 * @param   pPage               Pointer to the page.
 */
DECL_FORCE_INLINE(int32_t) rtMemCacheGrabObj(PRTMEMCACHEPAGE pPage)
{
    if (ASMAtomicUoReadS32(&pPage->cFree) > 0)
    {
        int32_t cFreeNew = ASMAtomicDecS32(&pPage->cFree);
        if (cFreeNew >= 0)
            return cFreeNew;
        ASMAtomicIncS32(&pPage->cFree);
    }
    return -1;
}


RTDECL(int) RTMemCacheAllocEx(RTMEMCACHE hMemCache, void **ppvObj)
{
    RTMEMCACHEINT *pThis = hMemCache;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->u32Magic == RTMEMCACHE_MAGIC, VERR_INVALID_PARAMETER);

    /*
     * Try grab a free object from the stack.
     */
    PRTMEMCACHEFREEOBJ pObj = ASMAtomicUoReadPtrT(&pThis->pFreeTop, PRTMEMCACHEFREEOBJ);
    if (pObj)
    {
        pObj = ASMAtomicXchgPtrT(&pThis->pFreeTop, NULL, PRTMEMCACHEFREEOBJ);
        if (pObj)
        {
            if (pObj->pNext)
            {
                Assert(pObj->pNext != pObj);
                PRTMEMCACHEFREEOBJ pAllocRace = ASMAtomicXchgPtrT(&pThis->pFreeTop, pObj->pNext, PRTMEMCACHEFREEOBJ);
                if (pAllocRace)
                    rtMemCacheFreeList(pThis, pAllocRace);
            }

            pObj->pNext = NULL;
            *ppvObj = pObj;
            return VINF_SUCCESS;
        }
    }

    /*
     * Try grab a free object at the cache level.
     */
    int32_t cNewFree = ASMAtomicDecS32(&pThis->cFree);
    if (RT_LIKELY(cNewFree < 0))
    {
        uint32_t cTotal = ASMAtomicUoReadU32(&pThis->cTotal);
        if (   (uint32_t)(cTotal + -cNewFree) > pThis->cMax
            || (uint32_t)(cTotal + -cNewFree) <= cTotal)
        {
            ASMAtomicIncS32(&pThis->cFree);
            return VERR_MEM_CACHE_MAX_SIZE;
        }

        int rc = rtMemCacheGrow(pThis);
        if (RT_FAILURE(rc))
        {
            ASMAtomicIncS32(&pThis->cFree);
            return rc;
        }
    }

    /*
     * Grab a free object at the page level.
     */
    PRTMEMCACHEPAGE pPage = ASMAtomicUoReadPtrT(&pThis->pPageHint, PRTMEMCACHEPAGE);
    int32_t iObj = pPage ? rtMemCacheGrabObj(pPage) : -1;
    if (iObj < 0)
    {
        for (unsigned cLoops = 0; ; cLoops++)
        {
            for (pPage = pThis->pPageHead; pPage; pPage = pPage->pNext)
            {
                iObj = rtMemCacheGrabObj(pPage);
                if (iObj >= 0)
                {
                    if (iObj > 0)
                        ASMAtomicWritePtr(&pThis->pPageHint, pPage);
                    break;
                }
            }
            if (iObj >= 0)
                break;
            Assert(cLoops != 2);
            Assert(cLoops < 10);
        }
    }
    Assert(iObj >= 0);
    Assert((uint32_t)iObj < pThis->cMax);

    /*
     * Find a free object in the allocation bitmap.  Use the new cFree count
     * as a hint.
     */
    if (ASMAtomicBitTestAndSet(pPage->pbmAlloc, iObj))
    {
        for (unsigned cLoops2 = 0;; cLoops2++)
        {
            iObj = ASMBitFirstClear(pPage->pbmAlloc, pThis->cBits);
            if (RT_LIKELY(iObj >= 0))
            {
                if (!ASMAtomicBitTestAndSet(pPage->pbmAlloc, iObj))
                    break;
            }
            else
                ASMMemoryFence();
            Assert(cLoops2 != 40);
        }
        Assert(iObj >= 0);
    }
    void *pvObj = &pPage->pbObjects[iObj * pThis->cbObject];
    Assert((uintptr_t)pvObj - (uintptr_t)pPage < RTSystemGetPageSize());

    /*
     * Call the constructor?
     */
    if (   pThis->pfnCtor
        && !ASMAtomicBitTestAndSet(pPage->pbmCtor, iObj))
    {
        int rc = pThis->pfnCtor(hMemCache, pvObj, pThis->pvUser);
        if (RT_FAILURE(rc))
        {
            ASMAtomicBitClear(pPage->pbmCtor, iObj);
            RTMemCacheFree(pThis, pvObj);
            return rc;
        }
    }

    *ppvObj = pvObj;
    return VINF_SUCCESS;
}


RTDECL(void *) RTMemCacheAlloc(RTMEMCACHE hMemCache)
{
    void *pvObj;
    int rc = RTMemCacheAllocEx(hMemCache, &pvObj);
    if (RT_SUCCESS(rc))
        return pvObj;
    return NULL;
}



/**
 * Really frees one object.
 *
 * @param   pThis               The memory cache.
 * @param   pvObj               The memory object to free.
 */
static void rtMemCacheFreeOne(RTMEMCACHEINT *pThis, void *pvObj)
{
    /* Note: Do *NOT* attempt to poison the object! */

    /*
     * Find the cache page.  The page structure is at the start of the page.
     */
    PRTMEMCACHEPAGE pPage = (PRTMEMCACHEPAGE)(((uintptr_t)pvObj) & ~RTSystemGetPageOffsetMask());
    Assert(pPage->pCache == pThis);
    Assert(ASMAtomicUoReadS32(&pPage->cFree) < (int32_t)pThis->cPerPage);

    /*
     * Clear the bitmap bit and update the two object counter. Order matters!
     */
    uintptr_t offObj = (uintptr_t)pvObj - (uintptr_t)pPage->pbObjects;
    uintptr_t iObj   = offObj / pThis->cbObject;
    Assert(iObj * pThis->cbObject == offObj);
    Assert(iObj < pThis->cPerPage);
    AssertReturnVoid(ASMAtomicBitTestAndClear(pPage->pbmAlloc, iObj));

    ASMAtomicIncS32(&pPage->cFree);
    ASMAtomicIncS32(&pThis->cFree);
}


/**
 * Really frees a list of 'freed' object.
 *
 * @param   pThis               The memory cache.
 * @param   pHead               The head of the list.
 */
static void rtMemCacheFreeList(RTMEMCACHEINT *pThis, PRTMEMCACHEFREEOBJ pHead)
{
    while (pHead)
    {
        PRTMEMCACHEFREEOBJ pFreeMe = pHead;
        pHead = pHead->pNext;
        pFreeMe->pNext = NULL;
        ASMCompilerBarrier();
        rtMemCacheFreeOne(pThis, pFreeMe);
    }
}



RTDECL(void) RTMemCacheFree(RTMEMCACHE hMemCache, void *pvObj)
{
    if (!pvObj)
        return;

    RTMEMCACHEINT *pThis = hMemCache;
    AssertPtrReturnVoid(pThis);
    AssertReturnVoid(pThis->u32Magic == RTMEMCACHE_MAGIC);

    AssertPtr(pvObj);
    Assert(RT_ALIGN_P(pvObj, pThis->cbAlignment) == pvObj);

    if (!pThis->fUseFreeList)
        rtMemCacheFreeOne(pThis, pvObj);
    else
    {
# ifdef RT_STRICT
        /* This is the same as the other branch, except it's not actually freed. */
        PRTMEMCACHEPAGE pPage = (PRTMEMCACHEPAGE)(((uintptr_t)pvObj) & ~RTSystemGetPageOffsetMask());
        Assert(pPage->pCache == pThis);
        Assert(ASMAtomicUoReadS32(&pPage->cFree) < (int32_t)pThis->cPerPage);
        uintptr_t offObj = (uintptr_t)pvObj - (uintptr_t)pPage->pbObjects;
        uintptr_t iObj   = offObj / pThis->cbObject;
        Assert(iObj * pThis->cbObject == offObj);
        Assert(iObj < pThis->cPerPage);
        AssertReturnVoid(ASMBitTest(pPage->pbmAlloc, (int32_t)iObj));
# endif

        /*
         * Push it onto the free stack.
         */
        PRTMEMCACHEFREEOBJ pObj = (PRTMEMCACHEFREEOBJ)pvObj;
        pObj->pNext = ASMAtomicXchgPtrT(&pThis->pFreeTop, NULL, PRTMEMCACHEFREEOBJ);
        PRTMEMCACHEFREEOBJ pFreeRace = ASMAtomicXchgPtrT(&pThis->pFreeTop, pObj, PRTMEMCACHEFREEOBJ);
        if (pFreeRace)
            rtMemCacheFreeList(pThis, pFreeRace);
    }
}

