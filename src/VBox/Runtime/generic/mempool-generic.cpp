/* $Id: mempool-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Memory Allocation Pool.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include <iprt/mempool.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/spinlock.h>
#include <iprt/string.h>

#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to a memory pool instance. */
typedef struct RTMEMPOOLINT *PRTMEMPOOLINT;
/** Pointer to a memory pool entry. */
typedef struct RTMEMPOOLENTRY *PRTMEMPOOLENTRY;

/**
 * Memory pool entry.
 */
typedef struct RTMEMPOOLENTRY
{
    /** Pointer to the pool */
    PRTMEMPOOLINT               pMemPool;
    /** Pointer to the next entry. */
    PRTMEMPOOLENTRY volatile    pNext;
    /** Pointer to the previous entry. */
    PRTMEMPOOLENTRY volatile    pPrev;
    /** The number of references to the pool entry. */
    uint32_t volatile           cRefs;
} RTMEMPOOLENTRY;


/**
 * Memory pool instance data.
 */
typedef struct RTMEMPOOLINT
{
    /** Magic number (RTMEMPOOL_MAGIC). */
    uint32_t                    u32Magic;
    /** Spinlock protecting the pool entry list updates. */
    RTSPINLOCK                  hSpinLock;
    /** Head entry pointer. */
    PRTMEMPOOLENTRY volatile    pHead;
    /** The number of entries in the pool (for statistical purposes). */
    uint32_t volatile           cEntries;
    /** User data associated with the pool. */
    void                       *pvUser;
    /** The pool name. (variable length)  */
    char                        szName[8];
} RTMEMPOOLINT;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Validates a memory pool handle, translating RTMEMPOOL_DEFAULT when found,
 * and returns rc if not valid. */
#define RTMEMPOOL_VALID_RETURN_RC(pMemPool, rc) \
    do { \
        if (pMemPool == RTMEMPOOL_DEFAULT) \
            pMemPool = &g_rtMemPoolDefault; \
        else \
        { \
            AssertPtrReturn((pMemPool), (rc)); \
            AssertReturn((pMemPool)->u32Magic == RTMEMPOOL_MAGIC, (rc)); \
        } \
    } while (0)

/** Validates a memory pool entry and returns rc if not valid. */
#define RTMEMPOOL_VALID_ENTRY_RETURN_RC(pEntry, rc) \
    do { \
        AssertPtrReturn(pEntry, (rc)); \
        AssertPtrNullReturn((pEntry)->pMemPool, (rc)); \
        Assert((pEntry)->cRefs < UINT32_MAX / 2); \
        AssertReturn((pEntry)->pMemPool->u32Magic == RTMEMPOOL_MAGIC, (rc)); \
    } while (0)


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The */
static RTMEMPOOLINT g_rtMemPoolDefault =
{
    /* .u32Magic = */           RTMEMPOOL_MAGIC,
    /* .hSpinLock = */          NIL_RTSPINLOCK,
    /* .pHead = */              NULL,
    /* .cEntries = */           0,
    /* .pvUser = */             NULL,
    /* .szName = */             "default"
};



RTDECL(int) RTMemPoolCreate(PRTMEMPOOL phMemPool, const char *pszName)
{
    AssertPtr(phMemPool);
    AssertPtr(pszName);
    Assert(*pszName);

    size_t          cchName  = strlen(pszName);
    PRTMEMPOOLINT   pMemPool = (PRTMEMPOOLINT)RTMemAlloc(RT_UOFFSETOF_DYN(RTMEMPOOLINT, szName[cchName + 1]));
    if (!pMemPool)
        return VERR_NO_MEMORY;
    int rc = RTSpinlockCreate(&pMemPool->hSpinLock, RTSPINLOCK_FLAGS_INTERRUPT_UNSAFE, "RTMemPoolCreate");
    if (RT_SUCCESS(rc))
    {
        pMemPool->u32Magic = RTMEMPOOL_MAGIC;
        pMemPool->pHead = NULL;
        pMemPool->cEntries = 0;
        pMemPool->pvUser = NULL;
        memcpy(pMemPool->szName, pszName, cchName);
        *phMemPool = pMemPool;
        return VINF_SUCCESS;
    }
    RTMemFree(pMemPool);
    return rc;
}
RT_EXPORT_SYMBOL(RTMemPoolCreate);


RTDECL(int) RTMemPoolDestroy(RTMEMPOOL hMemPool)
{
    if (hMemPool == NIL_RTMEMPOOL)
        return VINF_SUCCESS;
    PRTMEMPOOLINT pMemPool = hMemPool;
    RTMEMPOOL_VALID_RETURN_RC(pMemPool, VERR_INVALID_HANDLE);
    if (pMemPool == &g_rtMemPoolDefault)
        return VINF_SUCCESS;

    /*
     * Invalidate the handle and free all associated resources.
     */
    ASMAtomicWriteU32(&pMemPool->u32Magic, RTMEMPOOL_MAGIC_DEAD);

    int rc = RTSpinlockDestroy(pMemPool->hSpinLock); AssertRC(rc);
    pMemPool->hSpinLock = NIL_RTSPINLOCK;

    PRTMEMPOOLENTRY pEntry = pMemPool->pHead;
    pMemPool->pHead = NULL;
    while (pEntry)
    {
        PRTMEMPOOLENTRY pFree = pEntry;
        Assert(pFree->cRefs > 0 && pFree->cRefs < UINT32_MAX / 2);
        pEntry = pEntry->pNext;

        pFree->pMemPool = NULL;
        pFree->pNext = NULL;
        pFree->pPrev = NULL;
        pFree->cRefs = UINT32_MAX - 3;
        RTMemFree(pFree);
    }

    RTMemFree(pMemPool);

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTMemPoolDestroy);


DECLINLINE(void) rtMemPoolInitAndLink(PRTMEMPOOLINT pMemPool, PRTMEMPOOLENTRY pEntry)
{
    pEntry->pMemPool = pMemPool;
    pEntry->pNext    = NULL;
    pEntry->pPrev    = NULL;
    pEntry->cRefs    = 1;

    if (pMemPool->hSpinLock != NIL_RTSPINLOCK)
    {
        RTSpinlockAcquire(pMemPool->hSpinLock);

        PRTMEMPOOLENTRY pHead = pMemPool->pHead;
        pEntry->pNext = pHead;
        if (pHead)
            pHead->pPrev = pEntry;
        pMemPool->pHead = pEntry;

        RTSpinlockRelease(pMemPool->hSpinLock);
    }

    ASMAtomicIncU32(&pMemPool->cEntries);
}


DECLINLINE(void) rtMemPoolUnlink(PRTMEMPOOLENTRY pEntry)
{
    PRTMEMPOOLINT pMemPool = pEntry->pMemPool;
    if (pMemPool->hSpinLock != NIL_RTSPINLOCK)
    {
        RTSpinlockAcquire(pMemPool->hSpinLock);

        PRTMEMPOOLENTRY pNext = pEntry->pNext;
        PRTMEMPOOLENTRY pPrev = pEntry->pPrev;
        if (pNext)
            pNext->pPrev    = pPrev;
        if (pPrev)
            pPrev->pNext    = pNext;
        else
            pMemPool->pHead = pNext;
        pEntry->pMemPool = NULL;

        RTSpinlockRelease(pMemPool->hSpinLock);
    }
    else
        pEntry->pMemPool = NULL;

    ASMAtomicDecU32(&pMemPool->cEntries);
}


RTDECL(void *) RTMemPoolAlloc(RTMEMPOOL hMemPool, size_t cb) RT_NO_THROW_DEF
{
    PRTMEMPOOLINT pMemPool = hMemPool;
    RTMEMPOOL_VALID_RETURN_RC(pMemPool, NULL);

    PRTMEMPOOLENTRY pEntry = (PRTMEMPOOLENTRY)RTMemAlloc(cb + sizeof(*pEntry));
    if (!pEntry)
        return NULL;
    rtMemPoolInitAndLink(pMemPool, pEntry);

    return pEntry + 1;
}
RT_EXPORT_SYMBOL(RTMemPoolAlloc);


RTDECL(void *) RTMemPoolAllocZ(RTMEMPOOL hMemPool, size_t cb) RT_NO_THROW_DEF
{
    PRTMEMPOOLINT pMemPool = hMemPool;
    RTMEMPOOL_VALID_RETURN_RC(pMemPool, NULL);

    PRTMEMPOOLENTRY pEntry = (PRTMEMPOOLENTRY)RTMemAllocZ(cb + sizeof(*pEntry));
    if (!pEntry)
        return NULL;
    rtMemPoolInitAndLink(pMemPool, pEntry);

    return pEntry + 1;
}
RT_EXPORT_SYMBOL(RTMemPoolAllocZ);


RTDECL(void *) RTMemPoolDup(RTMEMPOOL hMemPool, const void *pvSrc, size_t cb) RT_NO_THROW_DEF
{
    PRTMEMPOOLINT pMemPool = hMemPool;
    RTMEMPOOL_VALID_RETURN_RC(pMemPool, NULL);

    PRTMEMPOOLENTRY pEntry = (PRTMEMPOOLENTRY)RTMemAlloc(cb + sizeof(*pEntry));
    if (!pEntry)
        return NULL;
    memcpy(pEntry + 1, pvSrc, cb);
    rtMemPoolInitAndLink(pMemPool, pEntry);

    return pEntry + 1;
}
RT_EXPORT_SYMBOL(RTMemPoolDup);


RTDECL(void *) RTMemPoolDupEx(RTMEMPOOL hMemPool, const void *pvSrc, size_t cbSrc, size_t cbExtra) RT_NO_THROW_DEF
{
    PRTMEMPOOLINT pMemPool = hMemPool;
    RTMEMPOOL_VALID_RETURN_RC(pMemPool, NULL);

    PRTMEMPOOLENTRY pEntry = (PRTMEMPOOLENTRY)RTMemAlloc(cbSrc + cbExtra + sizeof(*pEntry));
    if (!pEntry)
        return NULL;
    memcpy(pEntry + 1, pvSrc, cbSrc);
    memset((uint8_t *)(pEntry + 1) + cbSrc, '\0', cbExtra);
    rtMemPoolInitAndLink(pMemPool, pEntry);

    return pEntry + 1;
}
RT_EXPORT_SYMBOL(RTMemPoolDupEx);



RTDECL(void *) RTMemPoolRealloc(RTMEMPOOL hMemPool, void *pvOld, size_t cbNew) RT_NO_THROW_DEF
{
    /*
     * Fend off the odd cases.
     */
    if (!cbNew)
    {
        RTMemPoolRelease(hMemPool, pvOld);
        return NULL;
    }

    if (!pvOld)
        return RTMemPoolAlloc(hMemPool, cbNew);

    /*
     * Real realloc.
     */
    PRTMEMPOOLINT   pNewMemPool = hMemPool;
    RTMEMPOOL_VALID_RETURN_RC(pNewMemPool, NULL);

    PRTMEMPOOLENTRY pOldEntry = (PRTMEMPOOLENTRY)pvOld - 1;
    RTMEMPOOL_VALID_ENTRY_RETURN_RC(pOldEntry, NULL);
    PRTMEMPOOLINT   pOldMemPool = pOldEntry->pMemPool;
    AssertReturn(pOldEntry->cRefs == 1, NULL);

    /*
     * Unlink it from the current pool and try reallocate it.
     */
    rtMemPoolUnlink(pOldEntry);

    PRTMEMPOOLENTRY pEntry = (PRTMEMPOOLENTRY)RTMemRealloc(pOldEntry, cbNew + sizeof(*pEntry));
    if (!pEntry)
    {
        rtMemPoolInitAndLink(pOldMemPool, pOldEntry);
        return NULL;
    }
    rtMemPoolInitAndLink(pNewMemPool, pEntry);

    return pEntry + 1;
}
RT_EXPORT_SYMBOL(RTMemPoolRealloc);


RTDECL(void) RTMemPoolFree(RTMEMPOOL hMemPool, void *pv) RT_NO_THROW_DEF
{
    RTMemPoolRelease(hMemPool, pv);
}
RT_EXPORT_SYMBOL(RTMemPoolFree);


RTDECL(uint32_t) RTMemPoolRetain(void *pv) RT_NO_THROW_DEF
{
    PRTMEMPOOLENTRY pEntry = (PRTMEMPOOLENTRY)pv - 1;
    RTMEMPOOL_VALID_ENTRY_RETURN_RC(pEntry, UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pEntry->cRefs);
    Assert(cRefs < UINT32_MAX / 2);

    return cRefs;
}
RT_EXPORT_SYMBOL(RTMemPoolRetain);


RTDECL(uint32_t) RTMemPoolRelease(RTMEMPOOL hMemPool, void *pv) RT_NO_THROW_DEF
{
    if (!pv)
        return 0;

    PRTMEMPOOLENTRY pEntry = (PRTMEMPOOLENTRY)pv - 1;
    RTMEMPOOL_VALID_ENTRY_RETURN_RC(pEntry, UINT32_MAX);
    Assert(    hMemPool == NIL_RTMEMPOOL
           ||  hMemPool == pEntry->pMemPool
           ||  (hMemPool == RTMEMPOOL_DEFAULT && pEntry->pMemPool == &g_rtMemPoolDefault)); RT_NOREF_PV(hMemPool);
    AssertReturn(pEntry->cRefs > 0, UINT32_MAX);

    uint32_t cRefs = ASMAtomicDecU32(&pEntry->cRefs);
    Assert(cRefs < UINT32_MAX / 2);
    if (!cRefs)
    {
        rtMemPoolUnlink(pEntry);
        pEntry->cRefs = UINT32_MAX - 2;
        RTMemFree(pEntry);
    }

    return cRefs;
}
RT_EXPORT_SYMBOL(RTMemPoolRelease);


RTDECL(uint32_t) RTMemPoolRefCount(void *pv) RT_NO_THROW_DEF
{
    PRTMEMPOOLENTRY pEntry = (PRTMEMPOOLENTRY)pv - 1;
    RTMEMPOOL_VALID_ENTRY_RETURN_RC(pEntry, UINT32_MAX);

    uint32_t cRefs = ASMAtomicReadU32(&pEntry->cRefs);
    Assert(cRefs < UINT32_MAX / 2);

    return cRefs;
}
RT_EXPORT_SYMBOL(RTMemPoolRefCount);

