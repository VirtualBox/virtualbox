/* $Id: PDMBlkCache.cpp 109162 2025-04-09 18:32:56Z alexander.eichner@oracle.com $ */
/** @file
 * PDM Block Cache.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

/** @page pg_pdm_block_cache     PDM Block Cache - The I/O cache
 * This component implements an I/O cache based on the 2Q cache algorithm.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_PDM_BLK_CACHE
#include "PDMInternal.h"
#include <iprt/asm.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/trace.h>
#include <VBox/log.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/uvm.h>
#include <VBox/vmm/vm.h>

#include "PDMBlkCacheInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifdef VBOX_STRICT
# define PDMACFILECACHE_IS_CRITSECT_OWNER(Cache) \
    do \
    { \
     AssertMsg(RTCritSectIsOwner(&Cache->CritSect), \
               ("Thread does not own critical section\n"));\
    } while (0)

# define PDMACFILECACHE_EP_IS_SEMRW_WRITE_OWNER(pEpCache) \
    do \
    { \
        AssertMsg(RTSemRWIsWriteOwner(pEpCache->SemRWEntries), \
                  ("Thread is not exclusive owner of the per endpoint RW semaphore\n")); \
    } while (0)

# define PDMACFILECACHE_EP_IS_SEMRW_READ_OWNER(pEpCache) \
    do \
    { \
        AssertMsg(RTSemRWIsReadOwner(pEpCache->SemRWEntries), \
                  ("Thread is not read owner of the per endpoint RW semaphore\n")); \
    } while (0)

#else
# define PDMACFILECACHE_IS_CRITSECT_OWNER(Cache) do { } while (0)
# define PDMACFILECACHE_EP_IS_SEMRW_WRITE_OWNER(pEpCache) do { } while (0)
# define PDMACFILECACHE_EP_IS_SEMRW_READ_OWNER(pEpCache) do { } while (0)
#endif

#define PDM_BLK_CACHE_SAVED_STATE_VERSION 1

/* Enable to enable some tracing in the block cache code for investigating issues. */
/*#define VBOX_BLKCACHE_TRACING 1*/


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

static PPDMBLKCACHEENTRY pdmBlkCacheEntryAlloc(PPDMBLKCACHE pBlkCache,
                                               uint64_t off, size_t cbData, uint8_t *pbBuffer);
static bool pdmBlkCacheAddDirtyEntry(PPDMBLKCACHE pBlkCache, PPDMBLKCACHEENTRY pEntry);


/**
 * Add message to the VM trace buffer.
 *
 * @param   pBlkCache     The block cache.
 * @param   pszFmt        The format string.
 * @param   ...           Additional parameters for the string formatter.
 */
DECLINLINE(void) pdmBlkCacheR3TraceMsgF(PPDMBLKCACHE pBlkCache, const char *pszFmt, ...)
{
#if defined(VBOX_BLKCACHE_TRACING)
    va_list va;
    va_start(va, pszFmt);
    RTTraceBufAddMsgV(pBlkCache->pCache->pVM->CTX_SUFF(hTraceBuf), pszFmt, va);
    va_end(va);
#else
    RT_NOREF2(pBlkCache, pszFmt);
#endif
}

/**
 * Decrement the reference counter of the given cache entry.
 *
 * @param   pEntry    The entry to release.
 */
DECLINLINE(void) pdmBlkCacheEntryRelease(PPDMBLKCACHEENTRY pEntry)
{
    AssertMsg(pEntry->cRefs > 0, ("Trying to release a not referenced entry\n"));
    ASMAtomicDecU32(&pEntry->cRefs);
}

/**
 * Increment the reference counter of the given cache entry.
 *
 * @param   pEntry    The entry to reference.
 */
DECLINLINE(void) pdmBlkCacheEntryRef(PPDMBLKCACHEENTRY pEntry)
{
    ASMAtomicIncU32(&pEntry->cRefs);
}

#ifdef VBOX_STRICT
static void pdmBlkCacheValidate(PPDMBLKCACHEGLOBAL pCache)
{
    /* Amount of cached data should never exceed the maximum amount. */
    AssertMsg(pCache->cbCached <= pCache->cbMax,
              ("Current amount of cached data exceeds maximum\n"));

    /* The amount of cached data in the LRU and FRU list should match cbCached */
    AssertMsg(pCache->LruRecentlyUsedIn.cbCached + pCache->LruFrequentlyUsed.cbCached == pCache->cbCached,
              ("Amount of cached data doesn't match\n"));

    AssertMsg(pCache->LruRecentlyUsedOut.cbCached <= pCache->cbRecentlyUsedOutMax,
              ("Paged out list exceeds maximum\n"));
}
#endif

DECLINLINE(void) pdmBlkCacheLockEnter(PPDMBLKCACHEGLOBAL pCache)
{
    RTCritSectEnter(&pCache->CritSect);
#ifdef VBOX_STRICT
    pdmBlkCacheValidate(pCache);
#endif
}

DECLINLINE(void) pdmBlkCacheLockLeave(PPDMBLKCACHEGLOBAL pCache)
{
#ifdef VBOX_STRICT
    pdmBlkCacheValidate(pCache);
#endif
    RTCritSectLeave(&pCache->CritSect);
}

DECLINLINE(void) pdmBlkCacheSub(PPDMBLKCACHEGLOBAL pCache, uint32_t cbAmount)
{
    PDMACFILECACHE_IS_CRITSECT_OWNER(pCache);
    pCache->cbCached -= cbAmount;
}

DECLINLINE(void) pdmBlkCacheAdd(PPDMBLKCACHEGLOBAL pCache, uint32_t cbAmount)
{
    PDMACFILECACHE_IS_CRITSECT_OWNER(pCache);
    pCache->cbCached += cbAmount;
}

DECLINLINE(void) pdmBlkCacheListAdd(PPDMBLKLRULIST pList, uint32_t cbAmount)
{
    pList->cbCached += cbAmount;
}

DECLINLINE(void) pdmBlkCacheListSub(PPDMBLKLRULIST pList, uint32_t cbAmount)
{
    pList->cbCached -= cbAmount;
}

#ifdef PDMACFILECACHE_WITH_LRULIST_CHECKS
/**
 * Checks consistency of a LRU list.
 *
 * @param    pList         The LRU list to check.
 * @param    pNotInList    Element which is not allowed to occur in the list.
 */
static void pdmBlkCacheCheckList(PPDMBLKLRULIST pList, PPDMBLKCACHEENTRY pNotInList)
{
    PPDMBLKCACHEENTRY pCurr = pList->pHead;

    /* Check that there are no double entries and no cycles in the list. */
    while (pCurr)
    {
        PPDMBLKCACHEENTRY pNext = pCurr->pNext;

        while (pNext)
        {
            AssertMsg(pCurr != pNext,
                      ("Entry %#p is at least two times in list %#p or there is a cycle in the list\n",
                      pCurr, pList));
            pNext = pNext->pNext;
        }

        AssertMsg(pCurr != pNotInList, ("Not allowed entry %#p is in list\n", pCurr));

        if (!pCurr->pNext)
            AssertMsg(pCurr == pList->pTail, ("End of list reached but last element is not list tail\n"));

        pCurr = pCurr->pNext;
    }
}
#endif

/**
 * Unlinks a cache entry from the LRU list it is assigned to.
 *
 * @param   pEntry    The entry to unlink.
 */
static void pdmBlkCacheEntryRemoveFromList(PPDMBLKCACHEENTRY pEntry)
{
    PPDMBLKLRULIST pList = pEntry->pList;
    PPDMBLKCACHEENTRY pPrev, pNext;

    LogFlowFunc((": Deleting entry %#p from list %#p\n", pEntry, pList));

    AssertPtr(pList);

#ifdef PDMACFILECACHE_WITH_LRULIST_CHECKS
    pdmBlkCacheCheckList(pList, NULL);
#endif

    pPrev = pEntry->pPrev;
    pNext = pEntry->pNext;

    AssertMsg(pEntry != pPrev, ("Entry links to itself as previous element\n"));
    AssertMsg(pEntry != pNext, ("Entry links to itself as next element\n"));

    if (pPrev)
        pPrev->pNext = pNext;
    else
    {
        pList->pHead = pNext;

        if (pNext)
            pNext->pPrev = NULL;
    }

    if (pNext)
        pNext->pPrev = pPrev;
    else
    {
        pList->pTail = pPrev;

        if (pPrev)
            pPrev->pNext = NULL;
    }

    pEntry->pList    = NULL;
    pEntry->pPrev    = NULL;
    pEntry->pNext    = NULL;
    pdmBlkCacheListSub(pList, pEntry->cbData);
#ifdef PDMACFILECACHE_WITH_LRULIST_CHECKS
    pdmBlkCacheCheckList(pList, pEntry);
#endif
}

/**
 * Adds a cache entry to the given LRU list unlinking it from the currently
 * assigned list if needed.
 *
 * @param    pList    List to the add entry to.
 * @param    pEntry   Entry to add.
 */
static void pdmBlkCacheEntryAddToList(PPDMBLKLRULIST pList, PPDMBLKCACHEENTRY pEntry)
{
    LogFlowFunc((": Adding entry %#p to list %#p\n", pEntry, pList));
#ifdef PDMACFILECACHE_WITH_LRULIST_CHECKS
    pdmBlkCacheCheckList(pList, NULL);
#endif

    /* Remove from old list if needed */
    if (pEntry->pList)
        pdmBlkCacheEntryRemoveFromList(pEntry);

    pEntry->pNext = pList->pHead;
    if (pList->pHead)
        pList->pHead->pPrev = pEntry;
    else
    {
        Assert(!pList->pTail);
        pList->pTail = pEntry;
    }

    pEntry->pPrev    = NULL;
    pList->pHead     = pEntry;
    pdmBlkCacheListAdd(pList, pEntry->cbData);
    pEntry->pList    = pList;
#ifdef PDMACFILECACHE_WITH_LRULIST_CHECKS
    pdmBlkCacheCheckList(pList, NULL);
#endif
}

/**
 * Destroys a LRU list freeing all entries.
 *
 * @param   pList    Pointer to the LRU list to destroy.
 *
 * @note The caller must own the critical section of the cache.
 */
static void pdmBlkCacheDestroyList(PPDMBLKLRULIST pList)
{
    while (pList->pHead)
    {
        PPDMBLKCACHEENTRY pEntry = pList->pHead;

        pList->pHead = pEntry->pNext;

        AssertMsg(!(pEntry->fFlags & (PDMBLKCACHE_ENTRY_IO_IN_PROGRESS | PDMBLKCACHE_ENTRY_IS_DIRTY)),
                  ("Entry is dirty and/or still in progress fFlags=%#x\n", pEntry->fFlags));

        RTMemPageFree(pEntry->pbData, pEntry->cbData);
        RTMemFree(pEntry);
    }
}

/**
 * Tries to remove the given amount of bytes from a given list in the cache
 * moving the entries to one of the given ghosts lists
 *
 * @returns Amount of data which could be freed.
 * @param    pCache           Pointer to the global cache data.
 * @param    cbData           The amount of the data to free.
 * @param    pListSrc         The source list to evict data from.
 * @param    pGhostListDst    Where the ghost list removed entries should be
 *                            moved to, NULL if the entry should be freed.
 * @param    fReuseBuffer     Flag whether a buffer should be reused if it has
 *                            the same size
 * @param    ppbBuffer        Where to store the address of the buffer if an
 *                            entry with the same size was found and
 *                            fReuseBuffer is true.
 *
 * @note    This function may return fewer bytes than requested because entries
 *          may be marked as non evictable if they are used for I/O at the
 *          moment.
 */
static size_t pdmBlkCacheEvictPagesFrom(PPDMBLKCACHEGLOBAL pCache, size_t cbData,
                                        PPDMBLKLRULIST pListSrc, PPDMBLKLRULIST pGhostListDst,
                                        bool fReuseBuffer, uint8_t **ppbBuffer)
{
    size_t cbEvicted = 0;

    PDMACFILECACHE_IS_CRITSECT_OWNER(pCache);

    AssertMsg(cbData > 0, ("Evicting 0 bytes not possible\n"));
    AssertMsg(   !pGhostListDst
              || (pGhostListDst == &pCache->LruRecentlyUsedOut),
              ("Destination list must be NULL or the recently used but paged out list\n"));

    if (fReuseBuffer)
    {
        AssertPtr(ppbBuffer);
        *ppbBuffer = NULL;
    }

    /* Start deleting from the tail. */
    PPDMBLKCACHEENTRY pEntry = pListSrc->pTail;

    while ((cbEvicted < cbData) && pEntry)
    {
        PPDMBLKCACHEENTRY pCurr = pEntry;

        pEntry = pEntry->pPrev;

        /* We can't evict pages which are currently in progress or dirty but not in progress */
        if (   !(pCurr->fFlags & PDMBLKCACHE_NOT_EVICTABLE)
            && (ASMAtomicReadU32(&pCurr->cRefs) == 0))
        {
            /* Ok eviction candidate. Grab the endpoint semaphore and check again
             * because somebody else might have raced us. */
            PPDMBLKCACHE pBlkCache = pCurr->pBlkCache;
            RTSemRWRequestWrite(pBlkCache->SemRWEntries, RT_INDEFINITE_WAIT);

            if (!(pCurr->fFlags & PDMBLKCACHE_NOT_EVICTABLE)
                && (ASMAtomicReadU32(&pCurr->cRefs) == 0))
            {
                LogFlow(("Evicting entry %#p (%u bytes)\n", pCurr, pCurr->cbData));

                if (fReuseBuffer && pCurr->cbData == cbData)
                {
                    STAM_COUNTER_INC(&pCache->StatBuffersReused);
                    *ppbBuffer = pCurr->pbData;
                }
                else if (pCurr->pbData)
                    RTMemPageFree(pCurr->pbData, pCurr->cbData);

                pCurr->pbData = NULL;
                cbEvicted += pCurr->cbData;

                pdmBlkCacheEntryRemoveFromList(pCurr);
                pdmBlkCacheSub(pCache, pCurr->cbData);

                if (pGhostListDst)
                {
                    RTSemRWReleaseWrite(pBlkCache->SemRWEntries);

                    PPDMBLKCACHEENTRY pGhostEntFree = pGhostListDst->pTail;

                    /* We have to remove the last entries from the paged out list. */
                    while (   pGhostListDst->cbCached + pCurr->cbData > pCache->cbRecentlyUsedOutMax
                           && pGhostEntFree)
                    {
                        PPDMBLKCACHEENTRY pFree = pGhostEntFree;
                        PPDMBLKCACHE pBlkCacheFree = pFree->pBlkCache;

                        pGhostEntFree = pGhostEntFree->pPrev;

                        RTSemRWRequestWrite(pBlkCacheFree->SemRWEntries, RT_INDEFINITE_WAIT);

                        if (ASMAtomicReadU32(&pFree->cRefs) == 0)
                        {
                            pdmBlkCacheEntryRemoveFromList(pFree);

                            STAM_PROFILE_ADV_START(&pCache->StatTreeRemove, Cache);
                            RTAvlrU64Remove(pBlkCacheFree->pTree, pFree->Core.Key);
                            STAM_PROFILE_ADV_STOP(&pCache->StatTreeRemove, Cache);

                            RTMemFree(pFree);
                        }

                        RTSemRWReleaseWrite(pBlkCacheFree->SemRWEntries);
                    }

                    if (pGhostListDst->cbCached + pCurr->cbData > pCache->cbRecentlyUsedOutMax)
                    {
                        /* Couldn't remove enough entries. Delete */
                        STAM_PROFILE_ADV_START(&pCache->StatTreeRemove, Cache);
                        RTAvlrU64Remove(pCurr->pBlkCache->pTree, pCurr->Core.Key);
                        STAM_PROFILE_ADV_STOP(&pCache->StatTreeRemove, Cache);

                        RTMemFree(pCurr);
                    }
                    else
                        pdmBlkCacheEntryAddToList(pGhostListDst, pCurr);
                }
                else
                {
                    /* Delete the entry from the AVL tree it is assigned to. */
                    STAM_PROFILE_ADV_START(&pCache->StatTreeRemove, Cache);
                    RTAvlrU64Remove(pCurr->pBlkCache->pTree, pCurr->Core.Key);
                    STAM_PROFILE_ADV_STOP(&pCache->StatTreeRemove, Cache);

                    RTSemRWReleaseWrite(pBlkCache->SemRWEntries);
                    RTMemFree(pCurr);
                }
            }
            else
            {
                LogFlow(("Someone raced us, entry %#p (%u bytes) cannot be evicted any more (fFlags=%#x cRefs=%#x)\n",
                         pCurr, pCurr->cbData, pCurr->fFlags, pCurr->cRefs));
                RTSemRWReleaseWrite(pBlkCache->SemRWEntries);
            }

        }
        else
            LogFlow(("Entry %#p (%u bytes) is still in progress and can't be evicted\n", pCurr, pCurr->cbData));
    }

    return cbEvicted;
}

static bool pdmBlkCacheReclaim(PPDMBLKCACHEGLOBAL pCache, size_t cbData, bool fReuseBuffer, uint8_t **ppbBuffer)
{
    size_t cbRemoved = 0;

    if ((pCache->cbCached + cbData) < pCache->cbMax)
        return true;
    else if ((pCache->LruRecentlyUsedIn.cbCached + cbData) > pCache->cbRecentlyUsedInMax)
    {
        /* Try to evict as many bytes as possible from A1in */
        cbRemoved = pdmBlkCacheEvictPagesFrom(pCache, cbData, &pCache->LruRecentlyUsedIn,
                                                 &pCache->LruRecentlyUsedOut, fReuseBuffer, ppbBuffer);

        /*
         * If it was not possible to remove enough entries
         * try the frequently accessed cache.
         */
        if (cbRemoved < cbData)
        {
            Assert(!fReuseBuffer || !*ppbBuffer); /* It is not possible that we got a buffer with the correct size but we didn't freed enough data. */

            /*
             * If we removed something we can't pass the reuse buffer flag anymore because
             * we don't need to evict that much data
             */
            if (!cbRemoved)
                cbRemoved += pdmBlkCacheEvictPagesFrom(pCache, cbData, &pCache->LruFrequentlyUsed,
                                                          NULL, fReuseBuffer, ppbBuffer);
            else
                cbRemoved += pdmBlkCacheEvictPagesFrom(pCache, cbData - cbRemoved, &pCache->LruFrequentlyUsed,
                                                          NULL, false, NULL);
        }
    }
    else
    {
        /* We have to remove entries from frequently access list. */
        cbRemoved = pdmBlkCacheEvictPagesFrom(pCache, cbData, &pCache->LruFrequentlyUsed,
                                                 NULL, fReuseBuffer, ppbBuffer);
    }

    LogFlowFunc((": removed %u bytes, requested %u\n", cbRemoved, cbData));
    return (cbRemoved >= cbData);
}

DECLINLINE(int) pdmBlkCacheEnqueue(PPDMBLKCACHE pBlkCache, uint64_t off, size_t cbXfer, PPDMBLKCACHEIOXFER pIoXfer)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("%s: Enqueuing hIoXfer=%#p enmXferDir=%d\n",
                 __FUNCTION__, pIoXfer, pIoXfer->enmXferDir));

    ASMAtomicIncU32(&pBlkCache->cIoXfersActive);
    pdmBlkCacheR3TraceMsgF(pBlkCache, "BlkCache: I/O req %#p (%RTbool , %d) queued (%u now active)",
                           pIoXfer, pIoXfer->fIoCache, pIoXfer->enmXferDir, pBlkCache->cIoXfersActive);

    switch (pBlkCache->enmType)
    {
        case PDMBLKCACHETYPE_DEV:
        {
            rc = pBlkCache->u.Dev.pfnXferEnqueue(pBlkCache->u.Dev.pDevIns,
                                                 pIoXfer->enmXferDir,
                                                 off, cbXfer,
                                                 &pIoXfer->SgBuf, pIoXfer);
            break;
        }
        case PDMBLKCACHETYPE_DRV:
        {
            rc = pBlkCache->u.Drv.pfnXferEnqueue(pBlkCache->u.Drv.pDrvIns,
                                                 pIoXfer->enmXferDir,
                                                 off, cbXfer,
                                                 &pIoXfer->SgBuf, pIoXfer);
            break;
        }
        case PDMBLKCACHETYPE_USB:
        {
            rc = pBlkCache->u.Usb.pfnXferEnqueue(pBlkCache->u.Usb.pUsbIns,
                                                 pIoXfer->enmXferDir,
                                                 off, cbXfer,
                                                 &pIoXfer->SgBuf, pIoXfer);
            break;
        }
        case PDMBLKCACHETYPE_INTERNAL:
        {
            rc = pBlkCache->u.Int.pfnXferEnqueue(pBlkCache->u.Int.pvUser,
                                                 pIoXfer->enmXferDir,
                                                 off, cbXfer,
                                                 &pIoXfer->SgBuf, pIoXfer);
            break;
        }
        default:
            AssertMsgFailed(("Unknown block cache type!\n"));
    }

    if (RT_FAILURE(rc))
    {
        pdmBlkCacheR3TraceMsgF(pBlkCache, "BlkCache: Queueing I/O req %#p failed %Rrc", pIoXfer, rc);
        ASMAtomicDecU32(&pBlkCache->cIoXfersActive);
    }

    LogFlowFunc(("%s: returns rc=%Rrc\n", __FUNCTION__, rc));
    return rc;
}

/**
 * Initiates a read I/O task for the given entry.
 *
 * @returns VBox status code.
 * @param   pEntry    The entry to fetch the data to.
 */
static int pdmBlkCacheEntryReadFromMedium(PPDMBLKCACHEENTRY pEntry)
{
    PPDMBLKCACHE pBlkCache = pEntry->pBlkCache;
    LogFlowFunc((": Reading data into cache entry %#p\n", pEntry));

    /* Make sure no one evicts the entry while it is accessed. */
    pEntry->fFlags |= PDMBLKCACHE_ENTRY_IO_IN_PROGRESS;

    PPDMBLKCACHEIOXFER pIoXfer = (PPDMBLKCACHEIOXFER)RTMemAllocZ(sizeof(PDMBLKCACHEIOXFER));
    if (RT_UNLIKELY(!pIoXfer))
        return VERR_NO_MEMORY;

    AssertMsg(pEntry->pbData, ("Entry is in ghost state\n"));

    pIoXfer->fIoCache = true;
    pIoXfer->pEntry = pEntry;
    pIoXfer->SgSeg.pvSeg = pEntry->pbData;
    pIoXfer->SgSeg.cbSeg = pEntry->cbData;
    pIoXfer->enmXferDir  = PDMBLKCACHEXFERDIR_READ;
    RTSgBufInit(&pIoXfer->SgBuf, &pIoXfer->SgSeg, 1);

    return pdmBlkCacheEnqueue(pBlkCache, pEntry->Core.Key, pEntry->cbData, pIoXfer);
}

/**
 * Initiates a write I/O task for the given entry.
 *
 * @returns VBox status code.
 * @param   pEntry  The entry to read the data from.
 */
static int pdmBlkCacheEntryWriteToMedium(PPDMBLKCACHEENTRY pEntry)
{
    PPDMBLKCACHE pBlkCache = pEntry->pBlkCache;
    LogFlowFunc((": Writing data from cache entry %#p\n", pEntry));

    /* Make sure no one evicts the entry while it is accessed. */
    pEntry->fFlags |= PDMBLKCACHE_ENTRY_IO_IN_PROGRESS;

    PPDMBLKCACHEIOXFER pIoXfer = (PPDMBLKCACHEIOXFER)RTMemAllocZ(sizeof(PDMBLKCACHEIOXFER));
    if (RT_UNLIKELY(!pIoXfer))
        return VERR_NO_MEMORY;

    AssertMsg(pEntry->pbData, ("Entry is in ghost state\n"));

    pIoXfer->fIoCache = true;
    pIoXfer->pEntry = pEntry;
    pIoXfer->SgSeg.pvSeg = pEntry->pbData;
    pIoXfer->SgSeg.cbSeg = pEntry->cbData;
    pIoXfer->enmXferDir  = PDMBLKCACHEXFERDIR_WRITE;
    RTSgBufInit(&pIoXfer->SgBuf, &pIoXfer->SgSeg, 1);

    return pdmBlkCacheEnqueue(pBlkCache, pEntry->Core.Key, pEntry->cbData, pIoXfer);
}

/**
 * Passthrough a part of a request directly to the I/O manager handling the
 * endpoint.
 *
 * @returns VBox status code.
 * @param   pBlkCache       The endpoint cache.
 * @param   pReq            The request.
 * @param   pSgBuf          The scatter/gather buffer.
 * @param   offStart        Offset to start transfer from.
 * @param   cbData          Amount of data to transfer.
 * @param   enmXferDir      The transfer type (read/write)
 */
static int pdmBlkCacheRequestPassthrough(PPDMBLKCACHE pBlkCache, PPDMBLKCACHEREQ pReq,
                                         PRTSGBUF pSgBuf, uint64_t offStart, size_t cbData,
                                         PDMBLKCACHEXFERDIR enmXferDir)
{

    PPDMBLKCACHEIOXFER pIoXfer = (PPDMBLKCACHEIOXFER)RTMemAllocZ(sizeof(PDMBLKCACHEIOXFER));
    if (RT_UNLIKELY(!pIoXfer))
        return VERR_NO_MEMORY;

    ASMAtomicIncU32(&pReq->cXfersPending);
    pIoXfer->fIoCache    = false;
    pIoXfer->pReq        = pReq;
    pIoXfer->enmXferDir  = enmXferDir;
    if (pSgBuf)
    {
        RTSgBufClone(&pIoXfer->SgBuf, pSgBuf);
        RTSgBufAdvance(pSgBuf, cbData);
    }

    return pdmBlkCacheEnqueue(pBlkCache, offStart, cbData, pIoXfer);
}

/**
 * Commit a single dirty entry to the endpoint
 *
 * @param   pEntry    The entry to commit.
 */
static void pdmBlkCacheEntryCommit(PPDMBLKCACHEENTRY pEntry)
{
    AssertMsg(   (pEntry->fFlags & PDMBLKCACHE_ENTRY_IS_DIRTY)
              && !(pEntry->fFlags & PDMBLKCACHE_ENTRY_IO_IN_PROGRESS),
              ("Invalid flags set for entry %#p\n", pEntry));

    pdmBlkCacheEntryWriteToMedium(pEntry);
}

/**
 * Commit all dirty entries for a single endpoint.
 *
 * @param   pBlkCache    The endpoint cache to commit.
 */
static void pdmBlkCacheCommit(PPDMBLKCACHE pBlkCache)
{
    uint32_t cbCommitted = 0;

    /* Return if the cache was suspended. */
    if (pBlkCache->fSuspended)
        return;

    RTSemRWRequestWrite(pBlkCache->SemRWEntries, RT_INDEFINITE_WAIT);

    /* The list is moved to a new header to reduce locking overhead. */
    RTLISTANCHOR ListDirtyNotCommitted;

    RTSpinlockAcquire(pBlkCache->LockList);
    RTListMove(&ListDirtyNotCommitted, &pBlkCache->ListDirtyNotCommitted);
    RTSpinlockRelease(pBlkCache->LockList);

    if (!RTListIsEmpty(&ListDirtyNotCommitted))
    {
        PPDMBLKCACHEENTRY pEntry = RTListGetFirst(&ListDirtyNotCommitted, PDMBLKCACHEENTRY, NodeNotCommitted);

        while (!RTListNodeIsLast(&ListDirtyNotCommitted, &pEntry->NodeNotCommitted))
        {
            PPDMBLKCACHEENTRY pNext = RTListNodeGetNext(&pEntry->NodeNotCommitted, PDMBLKCACHEENTRY,
                                                        NodeNotCommitted);
            pdmBlkCacheEntryCommit(pEntry);
            cbCommitted += pEntry->cbData;
            RTListNodeRemove(&pEntry->NodeNotCommitted);
            pEntry = pNext;
        }

        /* Commit the last endpoint */
        Assert(RTListNodeIsLast(&ListDirtyNotCommitted, &pEntry->NodeNotCommitted));
        pdmBlkCacheEntryCommit(pEntry);
        cbCommitted += pEntry->cbData;
        RTListNodeRemove(&pEntry->NodeNotCommitted);
        AssertMsg(RTListIsEmpty(&ListDirtyNotCommitted),
                  ("Committed all entries but list is not empty\n"));
    }

    RTSemRWReleaseWrite(pBlkCache->SemRWEntries);
    AssertMsg(pBlkCache->pCache->cbDirty >= cbCommitted,
              ("Number of committed bytes exceeds number of dirty bytes\n"));
    uint32_t cbDirtyOld = ASMAtomicSubU32(&pBlkCache->pCache->cbDirty, cbCommitted);

    /* Reset the commit timer if we don't have any dirty bits. */
    if (   !(cbDirtyOld - cbCommitted)
        && pBlkCache->pCache->u32CommitTimeoutMs != 0)
        TMTimerStop(pBlkCache->pCache->pVM, pBlkCache->pCache->hTimerCommit);
}

/**
 * Commit all dirty entries in the cache.
 *
 * @param   pCache    The global cache instance.
 */
static void pdmBlkCacheCommitDirtyEntries(PPDMBLKCACHEGLOBAL pCache)
{
    bool fCommitInProgress = ASMAtomicXchgBool(&pCache->fCommitInProgress, true);

    if (!fCommitInProgress)
    {
        pdmBlkCacheLockEnter(pCache);
        Assert(!RTListIsEmpty(&pCache->ListUsers));

        PPDMBLKCACHE pBlkCache = RTListGetFirst(&pCache->ListUsers, PDMBLKCACHE, NodeCacheUser);
        AssertPtr(pBlkCache);

        while (!RTListNodeIsLast(&pCache->ListUsers, &pBlkCache->NodeCacheUser))
        {
            pdmBlkCacheCommit(pBlkCache);

            pBlkCache = RTListNodeGetNext(&pBlkCache->NodeCacheUser, PDMBLKCACHE,
                                          NodeCacheUser);
        }

        /* Commit the last endpoint */
        Assert(RTListNodeIsLast(&pCache->ListUsers, &pBlkCache->NodeCacheUser));
        pdmBlkCacheCommit(pBlkCache);

        pdmBlkCacheLockLeave(pCache);
        ASMAtomicWriteBool(&pCache->fCommitInProgress, false);
    }
}

/**
 * Adds the given entry as a dirty to the cache.
 *
 * @returns Flag whether the amount of dirty bytes in the cache exceeds the threshold
 * @param   pBlkCache    The endpoint cache the entry belongs to.
 * @param   pEntry       The entry to add.
 */
static bool pdmBlkCacheAddDirtyEntry(PPDMBLKCACHE pBlkCache, PPDMBLKCACHEENTRY pEntry)
{
    bool fDirtyBytesExceeded = false;
    PPDMBLKCACHEGLOBAL pCache = pBlkCache->pCache;

    /* If the commit timer is disabled we commit right away. */
    if (pCache->u32CommitTimeoutMs == 0)
    {
        pEntry->fFlags |= PDMBLKCACHE_ENTRY_IS_DIRTY;
        pdmBlkCacheEntryCommit(pEntry);
    }
    else if (!(pEntry->fFlags & PDMBLKCACHE_ENTRY_IS_DIRTY))
    {
        pEntry->fFlags |= PDMBLKCACHE_ENTRY_IS_DIRTY;

        RTSpinlockAcquire(pBlkCache->LockList);
        RTListAppend(&pBlkCache->ListDirtyNotCommitted, &pEntry->NodeNotCommitted);
        RTSpinlockRelease(pBlkCache->LockList);

        uint32_t cbDirty = ASMAtomicAddU32(&pCache->cbDirty, pEntry->cbData);

        /* Prevent committing if the VM was suspended. */
        if (RT_LIKELY(!ASMAtomicReadBool(&pCache->fIoErrorVmSuspended)))
            fDirtyBytesExceeded = (cbDirty + pEntry->cbData >= pCache->cbCommitDirtyThreshold);
        else if (!cbDirty && pCache->u32CommitTimeoutMs > 0)
        {
            /* Arm the commit timer. */
            TMTimerSetMillies(pCache->pVM, pCache->hTimerCommit, pCache->u32CommitTimeoutMs);
        }
    }

    return fDirtyBytesExceeded;
}

static PPDMBLKCACHE pdmR3BlkCacheFindById(PPDMBLKCACHEGLOBAL pBlkCacheGlobal, const char *pcszId)
{
    bool fFound = false;

    PPDMBLKCACHE pBlkCache;
    RTListForEach(&pBlkCacheGlobal->ListUsers, pBlkCache, PDMBLKCACHE, NodeCacheUser)
    {
        if (!RTStrCmp(pBlkCache->pszId, pcszId))
        {
            fFound = true;
            break;
        }
    }

    return fFound ?  pBlkCache : NULL;
}

/**
 * @callback_method_impl{FNTMTIMERINT, Commit timer callback.}
 */
static DECLCALLBACK(void) pdmBlkCacheCommitTimerCallback(PVM pVM, TMTIMERHANDLE hTimer, void *pvUser)
{
    PPDMBLKCACHEGLOBAL pCache = (PPDMBLKCACHEGLOBAL)pvUser;
    RT_NOREF(pVM, hTimer);

    LogFlowFunc(("Commit interval expired, commiting dirty entries\n"));

    if (   ASMAtomicReadU32(&pCache->cbDirty) > 0
        && !ASMAtomicReadBool(&pCache->fIoErrorVmSuspended))
        pdmBlkCacheCommitDirtyEntries(pCache);

    LogFlowFunc(("Entries committed, going to sleep\n"));
}

static DECLCALLBACK(int) pdmR3BlkCacheSaveExec(PVM pVM, PSSMHANDLE pSSM)
{
    PPDMBLKCACHEGLOBAL pBlkCacheGlobal = pVM->pUVM->pdm.s.pBlkCacheGlobal;

    AssertPtr(pBlkCacheGlobal);

    pdmBlkCacheLockEnter(pBlkCacheGlobal);

    SSMR3PutU32(pSSM, pBlkCacheGlobal->cRefs);

    /* Go through the list and save all dirty entries. */
    PPDMBLKCACHE pBlkCache;
    RTListForEach(&pBlkCacheGlobal->ListUsers, pBlkCache, PDMBLKCACHE, NodeCacheUser)
    {
        uint32_t cEntries = 0;
        PPDMBLKCACHEENTRY pEntry;

        RTSemRWRequestRead(pBlkCache->SemRWEntries, RT_INDEFINITE_WAIT);
        SSMR3PutU32(pSSM, (uint32_t)strlen(pBlkCache->pszId));
        SSMR3PutStrZ(pSSM, pBlkCache->pszId);

        /* Count the number of entries to safe. */
        RTListForEach(&pBlkCache->ListDirtyNotCommitted, pEntry, PDMBLKCACHEENTRY, NodeNotCommitted)
        {
            cEntries++;
        }

        SSMR3PutU32(pSSM, cEntries);

        /* Walk the list of all dirty entries and save them. */
        RTListForEach(&pBlkCache->ListDirtyNotCommitted, pEntry, PDMBLKCACHEENTRY, NodeNotCommitted)
        {
            /* A few sanity checks. */
            AssertMsg(!pEntry->cRefs, ("The entry is still referenced\n"));
            AssertMsg(pEntry->fFlags & PDMBLKCACHE_ENTRY_IS_DIRTY, ("Entry is not dirty\n"));
            AssertMsg(!(pEntry->fFlags & ~PDMBLKCACHE_ENTRY_IS_DIRTY), ("Invalid flags set\n"));
            AssertMsg(!pEntry->pWaitingHead && !pEntry->pWaitingTail, ("There are waiting requests\n"));
            AssertMsg(   pEntry->pList == &pBlkCacheGlobal->LruRecentlyUsedIn
                      || pEntry->pList == &pBlkCacheGlobal->LruFrequentlyUsed,
                      ("Invalid list\n"));
            AssertMsg(pEntry->cbData == pEntry->Core.KeyLast - pEntry->Core.Key + 1,
                      ("Size and range do not match\n"));

            /* Save */
            SSMR3PutU64(pSSM, pEntry->Core.Key);
            SSMR3PutU32(pSSM, pEntry->cbData);
            SSMR3PutMem(pSSM, pEntry->pbData, pEntry->cbData);
        }

        RTSemRWReleaseRead(pBlkCache->SemRWEntries);
    }

    pdmBlkCacheLockLeave(pBlkCacheGlobal);

    /* Terminator */
    return SSMR3PutU32(pSSM, UINT32_MAX);
}

static DECLCALLBACK(int) pdmR3BlkCacheLoadExec(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PPDMBLKCACHEGLOBAL pBlkCacheGlobal = pVM->pUVM->pdm.s.pBlkCacheGlobal;
    uint32_t           cRefs;

    NOREF(uPass);
    AssertPtr(pBlkCacheGlobal);

    pdmBlkCacheLockEnter(pBlkCacheGlobal);

    if (uVersion != PDM_BLK_CACHE_SAVED_STATE_VERSION)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    SSMR3GetU32(pSSM, &cRefs);

    /*
     * Fewer users in the saved state than in the current VM are allowed
     * because that means that there are only new ones which don't have any saved state
     * which can get lost.
     * More saved state entries than registered cache users are only allowed if the
     * missing users don't have any data saved in the cache.
     */
    int rc = VINF_SUCCESS;
    char *pszId = NULL;

    while (   cRefs > 0
           && RT_SUCCESS(rc))
    {
        PPDMBLKCACHE pBlkCache = NULL;
        uint32_t cbId = 0;

        SSMR3GetU32(pSSM, &cbId);
        Assert(cbId > 0);

        cbId++; /* Include terminator */
        pszId = (char *)RTMemAllocZ(cbId * sizeof(char));
        if (!pszId)
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        rc = SSMR3GetStrZ(pSSM, pszId, cbId);
        AssertRC(rc);

        /* Search for the block cache with the provided id. */
        pBlkCache = pdmR3BlkCacheFindById(pBlkCacheGlobal, pszId);

        /* Get the entries */
        uint32_t cEntries;
        SSMR3GetU32(pSSM, &cEntries);

        if (!pBlkCache && (cEntries > 0))
        {
            rc = SSMR3SetCfgError(pSSM, RT_SRC_POS,
                                  N_("The VM is missing a block device and there is data in the cache. Please make sure the source and target VMs have compatible storage configurations"));
            break;
        }

        RTMemFree(pszId);
        pszId = NULL;

        while (cEntries > 0)
        {
            PPDMBLKCACHEENTRY pEntry;
            uint64_t off;
            uint32_t cbEntry;

            SSMR3GetU64(pSSM, &off);
            SSMR3GetU32(pSSM, &cbEntry);

            pEntry = pdmBlkCacheEntryAlloc(pBlkCache, off, cbEntry, NULL);
            if (!pEntry)
            {
                rc = VERR_NO_MEMORY;
                break;
            }

            rc = SSMR3GetMem(pSSM, pEntry->pbData, cbEntry);
            if (RT_FAILURE(rc))
            {
                RTMemFree(pEntry->pbData);
                RTMemFree(pEntry);
                break;
            }

            /* Insert into the tree. */
            bool fInserted = RTAvlrU64Insert(pBlkCache->pTree, &pEntry->Core);
            Assert(fInserted); NOREF(fInserted);

            /* Add to the dirty list. */
            pdmBlkCacheAddDirtyEntry(pBlkCache, pEntry);
            pdmBlkCacheEntryAddToList(&pBlkCacheGlobal->LruRecentlyUsedIn, pEntry);
            pdmBlkCacheAdd(pBlkCacheGlobal, cbEntry);
            pdmBlkCacheEntryRelease(pEntry);
            cEntries--;
        }

        cRefs--;
    }

    if (pszId)
        RTMemFree(pszId);

    if (cRefs && RT_SUCCESS(rc))
        rc = SSMR3SetCfgError(pSSM, RT_SRC_POS,
                              N_("Unexpected error while restoring state. Please make sure the source and target VMs have compatible storage configurations"));

    pdmBlkCacheLockLeave(pBlkCacheGlobal);

    if (RT_SUCCESS(rc))
    {
        uint32_t u32 = 0;
        rc = SSMR3GetU32(pSSM, &u32);
        if (RT_SUCCESS(rc))
            AssertMsgReturn(u32 == UINT32_MAX, ("%#x\n", u32), VERR_SSM_DATA_UNIT_FORMAT_CHANGED);
    }

    return rc;
}

int pdmR3BlkCacheInit(PVM pVM)
{
    int  rc   = VINF_SUCCESS;
    PUVM pUVM = pVM->pUVM;
    PPDMBLKCACHEGLOBAL pBlkCacheGlobal;

    LogFlowFunc((": pVM=%p\n", pVM));

    VM_ASSERT_EMT(pVM);

    PCFGMNODE pCfgRoot     = CFGMR3GetRoot(pVM);
    PCFGMNODE pCfgBlkCache = CFGMR3GetChild(CFGMR3GetChild(pCfgRoot, "PDM"), "BlkCache");

    pBlkCacheGlobal = (PPDMBLKCACHEGLOBAL)RTMemAllocZ(sizeof(PDMBLKCACHEGLOBAL));
    if (!pBlkCacheGlobal)
        return VERR_NO_MEMORY;

    RTListInit(&pBlkCacheGlobal->ListUsers);
    pBlkCacheGlobal->pVM = pVM;
    pBlkCacheGlobal->cRefs = 0;
    pBlkCacheGlobal->cbCached  = 0;
    pBlkCacheGlobal->fCommitInProgress = false;

    /* Initialize members */
    pBlkCacheGlobal->LruRecentlyUsedIn.pHead    = NULL;
    pBlkCacheGlobal->LruRecentlyUsedIn.pTail    = NULL;
    pBlkCacheGlobal->LruRecentlyUsedIn.cbCached = 0;

    pBlkCacheGlobal->LruRecentlyUsedOut.pHead    = NULL;
    pBlkCacheGlobal->LruRecentlyUsedOut.pTail    = NULL;
    pBlkCacheGlobal->LruRecentlyUsedOut.cbCached = 0;

    pBlkCacheGlobal->LruFrequentlyUsed.pHead    = NULL;
    pBlkCacheGlobal->LruFrequentlyUsed.pTail    = NULL;
    pBlkCacheGlobal->LruFrequentlyUsed.cbCached = 0;

    do
    {
        rc = CFGMR3QueryU32Def(pCfgBlkCache, "CacheSize", &pBlkCacheGlobal->cbMax, 5 * _1M);
        AssertLogRelRCBreak(rc);
        LogFlowFunc(("Maximum number of bytes cached %u\n", pBlkCacheGlobal->cbMax));

        pBlkCacheGlobal->cbRecentlyUsedInMax  = (pBlkCacheGlobal->cbMax / 100) * 25; /* 25% of the buffer size */
        pBlkCacheGlobal->cbRecentlyUsedOutMax = (pBlkCacheGlobal->cbMax / 100) * 50; /* 50% of the buffer size */
        LogFlowFunc(("cbRecentlyUsedInMax=%u cbRecentlyUsedOutMax=%u\n",
                     pBlkCacheGlobal->cbRecentlyUsedInMax, pBlkCacheGlobal->cbRecentlyUsedOutMax));

        /** @todo r=aeichner: Experiment to find optimal default values */
        rc = CFGMR3QueryU32Def(pCfgBlkCache, "CacheCommitIntervalMs", &pBlkCacheGlobal->u32CommitTimeoutMs, 10000 /* 10sec */);
        AssertLogRelRCBreak(rc);
        rc = CFGMR3QueryU32Def(pCfgBlkCache, "CacheCommitThreshold", &pBlkCacheGlobal->cbCommitDirtyThreshold, pBlkCacheGlobal->cbMax / 2);
        AssertLogRelRCBreak(rc);
    } while (0);

    if (RT_SUCCESS(rc))
    {
        STAMR3Register(pVM, &pBlkCacheGlobal->cbMax,
                       STAMTYPE_U32, STAMVISIBILITY_ALWAYS,
                       "/PDM/BlkCache/cbMax",
                       STAMUNIT_BYTES,
                       "Maximum cache size");
        STAMR3Register(pVM, &pBlkCacheGlobal->cbCached,
                       STAMTYPE_U32, STAMVISIBILITY_ALWAYS,
                       "/PDM/BlkCache/cbCached",
                       STAMUNIT_BYTES,
                       "Currently used cache");
        STAMR3Register(pVM, &pBlkCacheGlobal->LruRecentlyUsedIn.cbCached,
                       STAMTYPE_U32, STAMVISIBILITY_ALWAYS,
                       "/PDM/BlkCache/cbCachedMruIn",
                       STAMUNIT_BYTES,
                       "Number of bytes cached in MRU list");
        STAMR3Register(pVM, &pBlkCacheGlobal->LruRecentlyUsedOut.cbCached,
                       STAMTYPE_U32, STAMVISIBILITY_ALWAYS,
                       "/PDM/BlkCache/cbCachedMruOut",
                       STAMUNIT_BYTES,
                       "Number of bytes cached in FRU list");
        STAMR3Register(pVM, &pBlkCacheGlobal->LruFrequentlyUsed.cbCached,
                       STAMTYPE_U32, STAMVISIBILITY_ALWAYS,
                       "/PDM/BlkCache/cbCachedFru",
                       STAMUNIT_BYTES,
                       "Number of bytes cached in FRU ghost list");

#ifdef VBOX_WITH_STATISTICS
        STAMR3Register(pVM, &pBlkCacheGlobal->cHits,
                       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS,
                       "/PDM/BlkCache/CacheHits",
                       STAMUNIT_COUNT, "Number of hits in the cache");
        STAMR3Register(pVM, &pBlkCacheGlobal->cPartialHits,
                       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS,
                       "/PDM/BlkCache/CachePartialHits",
                       STAMUNIT_COUNT, "Number of partial hits in the cache");
        STAMR3Register(pVM, &pBlkCacheGlobal->cMisses,
                       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS,
                       "/PDM/BlkCache/CacheMisses",
                       STAMUNIT_COUNT, "Number of misses when accessing the cache");
        STAMR3Register(pVM, &pBlkCacheGlobal->StatRead,
                       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS,
                       "/PDM/BlkCache/CacheRead",
                       STAMUNIT_BYTES, "Number of bytes read from the cache");
        STAMR3Register(pVM, &pBlkCacheGlobal->StatWritten,
                       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS,
                       "/PDM/BlkCache/CacheWritten",
                       STAMUNIT_BYTES, "Number of bytes written to the cache");
        STAMR3Register(pVM, &pBlkCacheGlobal->StatTreeGet,
                       STAMTYPE_PROFILE_ADV, STAMVISIBILITY_ALWAYS,
                       "/PDM/BlkCache/CacheTreeGet",
                       STAMUNIT_TICKS_PER_CALL, "Time taken to access an entry in the tree");
        STAMR3Register(pVM, &pBlkCacheGlobal->StatTreeInsert,
                       STAMTYPE_PROFILE_ADV, STAMVISIBILITY_ALWAYS,
                       "/PDM/BlkCache/CacheTreeInsert",
                       STAMUNIT_TICKS_PER_CALL, "Time taken to insert an entry in the tree");
        STAMR3Register(pVM, &pBlkCacheGlobal->StatTreeRemove,
                       STAMTYPE_PROFILE_ADV, STAMVISIBILITY_ALWAYS,
                       "/PDM/BlkCache/CacheTreeRemove",
                       STAMUNIT_TICKS_PER_CALL, "Time taken to remove an entry an the tree");
        STAMR3Register(pVM, &pBlkCacheGlobal->StatBuffersReused,
                       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS,
                       "/PDM/BlkCache/CacheBuffersReused",
                       STAMUNIT_COUNT, "Number of times a buffer could be reused");
#endif

        /* Initialize the critical section */
        rc = RTCritSectInit(&pBlkCacheGlobal->CritSect);
    }

    if (RT_SUCCESS(rc))
    {
        /* Create the commit timer */
        if (pBlkCacheGlobal->u32CommitTimeoutMs > 0)
            rc = TMR3TimerCreate(pVM, TMCLOCK_REAL, pdmBlkCacheCommitTimerCallback, pBlkCacheGlobal,
                                 TMTIMER_FLAGS_NO_RING0,  "BlkCache-Commit", &pBlkCacheGlobal->hTimerCommit);

        if (RT_SUCCESS(rc))
        {
            /* Register saved state handler. */
            rc = SSMR3RegisterInternal(pVM, "pdmblkcache", 0, PDM_BLK_CACHE_SAVED_STATE_VERSION, pBlkCacheGlobal->cbMax,
                                       NULL, NULL, NULL,
                                       NULL, pdmR3BlkCacheSaveExec, NULL,
                                       NULL, pdmR3BlkCacheLoadExec, NULL);
            if (RT_SUCCESS(rc))
            {
                LogRel(("BlkCache: Cache successfully initialized. Cache size is %u bytes\n", pBlkCacheGlobal->cbMax));
                LogRel(("BlkCache: Cache commit interval is %u ms\n", pBlkCacheGlobal->u32CommitTimeoutMs));
                LogRel(("BlkCache: Cache commit threshold is %u bytes\n", pBlkCacheGlobal->cbCommitDirtyThreshold));
                pUVM->pdm.s.pBlkCacheGlobal = pBlkCacheGlobal;
                return VINF_SUCCESS;
            }
        }

        RTCritSectDelete(&pBlkCacheGlobal->CritSect);
    }

    RTMemFree(pBlkCacheGlobal);

    LogFlowFunc((": returns rc=%Rrc\n", rc));
    return rc;
}

void pdmR3BlkCacheTerm(PVM pVM)
{
    PPDMBLKCACHEGLOBAL pBlkCacheGlobal = pVM->pUVM->pdm.s.pBlkCacheGlobal;

    if (pBlkCacheGlobal)
    {
        /* Make sure no one else uses the cache now */
        pdmBlkCacheLockEnter(pBlkCacheGlobal);

        /* Cleanup deleting all cache entries waiting for in progress entries to finish. */
        pdmBlkCacheDestroyList(&pBlkCacheGlobal->LruRecentlyUsedIn);
        pdmBlkCacheDestroyList(&pBlkCacheGlobal->LruRecentlyUsedOut);
        pdmBlkCacheDestroyList(&pBlkCacheGlobal->LruFrequentlyUsed);

        pdmBlkCacheLockLeave(pBlkCacheGlobal);

        RTCritSectDelete(&pBlkCacheGlobal->CritSect);
        RTMemFree(pBlkCacheGlobal);
        pVM->pUVM->pdm.s.pBlkCacheGlobal = NULL;
    }
}

int pdmR3BlkCacheResume(PVM pVM)
{
    PPDMBLKCACHEGLOBAL pBlkCacheGlobal = pVM->pUVM->pdm.s.pBlkCacheGlobal;

    LogFlowFunc(("pVM=%#p\n", pVM));

    if (   pBlkCacheGlobal
        && ASMAtomicXchgBool(&pBlkCacheGlobal->fIoErrorVmSuspended, false))
    {
        /* The VM was suspended because of an I/O error, commit all dirty entries. */
        pdmBlkCacheCommitDirtyEntries(pBlkCacheGlobal);
    }

    return VINF_SUCCESS;
}

static int pdmR3BlkCacheRetain(PVM pVM, PPPDMBLKCACHE ppBlkCache, const char *pcszId)
{
    int rc = VINF_SUCCESS;
    PPDMBLKCACHE pBlkCache = NULL;
    PPDMBLKCACHEGLOBAL pBlkCacheGlobal = pVM->pUVM->pdm.s.pBlkCacheGlobal;

    if (!pBlkCacheGlobal)
        return VERR_NOT_SUPPORTED;

    /*
     * Check that no other user cache has the same id first,
     * Unique id's are necessary in case the state is saved.
     */
    pdmBlkCacheLockEnter(pBlkCacheGlobal);

    pBlkCache = pdmR3BlkCacheFindById(pBlkCacheGlobal, pcszId);

    if (!pBlkCache)
    {
        pBlkCache = (PPDMBLKCACHE)RTMemAllocZ(sizeof(PDMBLKCACHE));

        if (pBlkCache)
            pBlkCache->pszId = RTStrDup(pcszId);

        if (   pBlkCache
            && pBlkCache->pszId)
        {
            pBlkCache->fSuspended = false;
            pBlkCache->cIoXfersActive = 0;
            pBlkCache->pCache = pBlkCacheGlobal;
            RTListInit(&pBlkCache->ListDirtyNotCommitted);

            rc = RTSpinlockCreate(&pBlkCache->LockList, RTSPINLOCK_FLAGS_INTERRUPT_UNSAFE, "pdmR3BlkCacheRetain");
            if (RT_SUCCESS(rc))
            {
                rc = RTSemRWCreate(&pBlkCache->SemRWEntries);
                if (RT_SUCCESS(rc))
                {
                    pBlkCache->pTree  = (PAVLRU64TREE)RTMemAllocZ(sizeof(AVLRFOFFTREE));
                    if (pBlkCache->pTree)
                    {
#ifdef VBOX_WITH_STATISTICS
                        STAMR3RegisterF(pBlkCacheGlobal->pVM, &pBlkCache->StatWriteDeferred,
                                        STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS,
                                        STAMUNIT_COUNT, "Number of deferred writes",
                                        "/PDM/BlkCache/%s/Cache/DeferredWrites", pBlkCache->pszId);
#endif

                        /* Add to the list of users. */
                        pBlkCacheGlobal->cRefs++;
                        RTListAppend(&pBlkCacheGlobal->ListUsers, &pBlkCache->NodeCacheUser);
                        pdmBlkCacheLockLeave(pBlkCacheGlobal);

                        *ppBlkCache = pBlkCache;
                        LogFlowFunc(("returns success\n"));
                        return VINF_SUCCESS;
                    }

                    rc = VERR_NO_MEMORY;
                    RTSemRWDestroy(pBlkCache->SemRWEntries);
                }

                RTSpinlockDestroy(pBlkCache->LockList);
            }

            RTStrFree(pBlkCache->pszId);
        }
        else
            rc = VERR_NO_MEMORY;

        if (pBlkCache)
            RTMemFree(pBlkCache);
    }
    else
        rc = VERR_ALREADY_EXISTS;

    pdmBlkCacheLockLeave(pBlkCacheGlobal);

    LogFlowFunc(("Leave rc=%Rrc\n", rc));
    return rc;
}

VMMR3DECL(int) PDMR3BlkCacheRetainDriver(PVM pVM, PPDMDRVINS pDrvIns, PPPDMBLKCACHE ppBlkCache,
                                         PFNPDMBLKCACHEXFERCOMPLETEDRV pfnXferComplete,
                                         PFNPDMBLKCACHEXFERENQUEUEDRV pfnXferEnqueue,
                                         PFNPDMBLKCACHEXFERENQUEUEDISCARDDRV pfnXferEnqueueDiscard,
                                         const char *pcszId)
{
    int rc = VINF_SUCCESS;
    PPDMBLKCACHE pBlkCache;

    rc = pdmR3BlkCacheRetain(pVM, &pBlkCache, pcszId);
    if (RT_SUCCESS(rc))
    {
        pBlkCache->enmType                      = PDMBLKCACHETYPE_DRV;
        pBlkCache->u.Drv.pfnXferComplete        = pfnXferComplete;
        pBlkCache->u.Drv.pfnXferEnqueue         = pfnXferEnqueue;
        pBlkCache->u.Drv.pfnXferEnqueueDiscard  = pfnXferEnqueueDiscard;
        pBlkCache->u.Drv.pDrvIns                = pDrvIns;
        *ppBlkCache = pBlkCache;
    }

    LogFlowFunc(("Leave rc=%Rrc\n", rc));
    return rc;
}

VMMR3DECL(int) PDMR3BlkCacheRetainDevice(PVM pVM, PPDMDEVINS pDevIns, PPPDMBLKCACHE ppBlkCache,
                                         PFNPDMBLKCACHEXFERCOMPLETEDEV pfnXferComplete,
                                         PFNPDMBLKCACHEXFERENQUEUEDEV pfnXferEnqueue,
                                         PFNPDMBLKCACHEXFERENQUEUEDISCARDDEV pfnXferEnqueueDiscard,
                                         const char *pcszId)
{
    int rc = VINF_SUCCESS;
    PPDMBLKCACHE pBlkCache;

    rc = pdmR3BlkCacheRetain(pVM, &pBlkCache, pcszId);
    if (RT_SUCCESS(rc))
    {
        pBlkCache->enmType                      = PDMBLKCACHETYPE_DEV;
        pBlkCache->u.Dev.pfnXferComplete        = pfnXferComplete;
        pBlkCache->u.Dev.pfnXferEnqueue         = pfnXferEnqueue;
        pBlkCache->u.Dev.pfnXferEnqueueDiscard  = pfnXferEnqueueDiscard;
        pBlkCache->u.Dev.pDevIns                = pDevIns;
        *ppBlkCache = pBlkCache;
    }

    LogFlowFunc(("Leave rc=%Rrc\n", rc));
    return rc;

}

VMMR3DECL(int) PDMR3BlkCacheRetainUsb(PVM pVM, PPDMUSBINS pUsbIns, PPPDMBLKCACHE ppBlkCache,
                                      PFNPDMBLKCACHEXFERCOMPLETEUSB pfnXferComplete,
                                      PFNPDMBLKCACHEXFERENQUEUEUSB pfnXferEnqueue,
                                      PFNPDMBLKCACHEXFERENQUEUEDISCARDUSB pfnXferEnqueueDiscard,
                                      const char *pcszId)
{
    int rc = VINF_SUCCESS;
    PPDMBLKCACHE pBlkCache;

    rc = pdmR3BlkCacheRetain(pVM, &pBlkCache, pcszId);
    if (RT_SUCCESS(rc))
    {
        pBlkCache->enmType                      = PDMBLKCACHETYPE_USB;
        pBlkCache->u.Usb.pfnXferComplete        = pfnXferComplete;
        pBlkCache->u.Usb.pfnXferEnqueue         = pfnXferEnqueue;
        pBlkCache->u.Usb.pfnXferEnqueueDiscard  = pfnXferEnqueueDiscard;
        pBlkCache->u.Usb.pUsbIns                = pUsbIns;
        *ppBlkCache = pBlkCache;
    }

    LogFlowFunc(("Leave rc=%Rrc\n", rc));
    return rc;

}

VMMR3DECL(int) PDMR3BlkCacheRetainInt(PVM pVM, void *pvUser, PPPDMBLKCACHE ppBlkCache,
                                      PFNPDMBLKCACHEXFERCOMPLETEINT pfnXferComplete,
                                      PFNPDMBLKCACHEXFERENQUEUEINT pfnXferEnqueue,
                                      PFNPDMBLKCACHEXFERENQUEUEDISCARDINT pfnXferEnqueueDiscard,
                                      const char *pcszId)
{
    int rc = VINF_SUCCESS;
    PPDMBLKCACHE pBlkCache;

    rc = pdmR3BlkCacheRetain(pVM, &pBlkCache, pcszId);
    if (RT_SUCCESS(rc))
    {
        pBlkCache->enmType                      = PDMBLKCACHETYPE_INTERNAL;
        pBlkCache->u.Int.pfnXferComplete        = pfnXferComplete;
        pBlkCache->u.Int.pfnXferEnqueue         = pfnXferEnqueue;
        pBlkCache->u.Int.pfnXferEnqueueDiscard  = pfnXferEnqueueDiscard;
        pBlkCache->u.Int.pvUser                 = pvUser;
        *ppBlkCache = pBlkCache;
    }

    LogFlowFunc(("Leave rc=%Rrc\n", rc));
    return rc;

}

/**
 * Callback for the AVL destroy routine. Frees a cache entry for this endpoint.
 *
 * @returns IPRT status code.
 * @param    pNode     The node to destroy.
 * @param    pvUser    Opaque user data.
 */
static DECLCALLBACK(int) pdmBlkCacheEntryDestroy(PAVLRU64NODECORE pNode, void *pvUser)
{
    PPDMBLKCACHEENTRY  pEntry = (PPDMBLKCACHEENTRY)pNode;
    PPDMBLKCACHEGLOBAL pCache = (PPDMBLKCACHEGLOBAL)pvUser;
    PPDMBLKCACHE pBlkCache = pEntry->pBlkCache;

    while (ASMAtomicReadU32(&pEntry->fFlags) & PDMBLKCACHE_ENTRY_IO_IN_PROGRESS)
    {
        /* Leave the locks to let the I/O thread make progress but reference the entry to prevent eviction. */
        pdmBlkCacheEntryRef(pEntry);
        RTSemRWReleaseWrite(pBlkCache->SemRWEntries);
        pdmBlkCacheLockLeave(pCache);

        RTThreadSleep(250);

        /* Re-enter all locks */
        pdmBlkCacheLockEnter(pCache);
        RTSemRWRequestWrite(pBlkCache->SemRWEntries, RT_INDEFINITE_WAIT);
        pdmBlkCacheEntryRelease(pEntry);
    }

    AssertMsg(!(pEntry->fFlags & PDMBLKCACHE_ENTRY_IO_IN_PROGRESS),
                ("Entry is dirty and/or still in progress fFlags=%#x\n", pEntry->fFlags));

    bool fUpdateCache =    pEntry->pList == &pCache->LruFrequentlyUsed
                        || pEntry->pList == &pCache->LruRecentlyUsedIn;

    pdmBlkCacheEntryRemoveFromList(pEntry);

    if (fUpdateCache)
        pdmBlkCacheSub(pCache, pEntry->cbData);

    RTMemPageFree(pEntry->pbData, pEntry->cbData);
    RTMemFree(pEntry);

    return VINF_SUCCESS;
}

VMMR3DECL(void) PDMR3BlkCacheRelease(PPDMBLKCACHE pBlkCache)
{
    PPDMBLKCACHEGLOBAL pCache = pBlkCache->pCache;

    /*
     * Commit all dirty entries now (they are waited on for completion during the
     * destruction of the AVL tree below).
     * The exception is if the VM was paused because of an I/O error before.
     */
    if (!ASMAtomicReadBool(&pCache->fIoErrorVmSuspended))
        pdmBlkCacheCommit(pBlkCache);

    /* Make sure nobody is accessing the cache while we delete the tree. */
    pdmBlkCacheLockEnter(pCache);
    RTSemRWRequestWrite(pBlkCache->SemRWEntries, RT_INDEFINITE_WAIT);
    RTAvlrU64Destroy(pBlkCache->pTree, pdmBlkCacheEntryDestroy, pCache);
    RTSemRWReleaseWrite(pBlkCache->SemRWEntries);

    RTSpinlockDestroy(pBlkCache->LockList);

    pCache->cRefs--;
    RTListNodeRemove(&pBlkCache->NodeCacheUser);

    pdmBlkCacheLockLeave(pCache);

    RTMemFree(pBlkCache->pTree);
    pBlkCache->pTree = NULL;
    RTSemRWDestroy(pBlkCache->SemRWEntries);

#ifdef VBOX_WITH_STATISTICS
    STAMR3DeregisterF(pCache->pVM->pUVM, "/PDM/BlkCache/%s/Cache/DeferredWrites", pBlkCache->pszId);
#endif

    RTStrFree(pBlkCache->pszId);
    RTMemFree(pBlkCache);
}

VMMR3DECL(void) PDMR3BlkCacheReleaseDevice(PVM pVM, PPDMDEVINS pDevIns)
{
    LogFlow(("%s: pDevIns=%p\n", __FUNCTION__, pDevIns));

    /*
     * Validate input.
     */
    if (!pDevIns)
        return;
    VM_ASSERT_EMT(pVM);

    PPDMBLKCACHEGLOBAL pBlkCacheGlobal = pVM->pUVM->pdm.s.pBlkCacheGlobal;
    PPDMBLKCACHE pBlkCache, pBlkCacheNext;

    /* Return silently if not supported. */
    if (!pBlkCacheGlobal)
        return;

    pdmBlkCacheLockEnter(pBlkCacheGlobal);

    RTListForEachSafe(&pBlkCacheGlobal->ListUsers, pBlkCache, pBlkCacheNext, PDMBLKCACHE, NodeCacheUser)
    {
        if (    pBlkCache->enmType == PDMBLKCACHETYPE_DEV
            &&  pBlkCache->u.Dev.pDevIns == pDevIns)
            PDMR3BlkCacheRelease(pBlkCache);
    }

    pdmBlkCacheLockLeave(pBlkCacheGlobal);
}

VMMR3DECL(void) PDMR3BlkCacheReleaseDriver(PVM pVM, PPDMDRVINS pDrvIns)
{
    LogFlow(("%s: pDrvIns=%p\n", __FUNCTION__, pDrvIns));

    /*
     * Validate input.
     */
    if (!pDrvIns)
        return;
    VM_ASSERT_EMT(pVM);

    PPDMBLKCACHEGLOBAL pBlkCacheGlobal = pVM->pUVM->pdm.s.pBlkCacheGlobal;
    PPDMBLKCACHE pBlkCache, pBlkCacheNext;

    /* Return silently if not supported. */
    if (!pBlkCacheGlobal)
        return;

    pdmBlkCacheLockEnter(pBlkCacheGlobal);

    RTListForEachSafe(&pBlkCacheGlobal->ListUsers, pBlkCache, pBlkCacheNext, PDMBLKCACHE, NodeCacheUser)
    {
        if (    pBlkCache->enmType == PDMBLKCACHETYPE_DRV
            &&  pBlkCache->u.Drv.pDrvIns == pDrvIns)
            PDMR3BlkCacheRelease(pBlkCache);
    }

    pdmBlkCacheLockLeave(pBlkCacheGlobal);
}

VMMR3DECL(void) PDMR3BlkCacheReleaseUsb(PVM pVM, PPDMUSBINS pUsbIns)
{
    LogFlow(("%s: pUsbIns=%p\n", __FUNCTION__, pUsbIns));

    /*
     * Validate input.
     */
    if (!pUsbIns)
        return;
    VM_ASSERT_EMT(pVM);

    PPDMBLKCACHEGLOBAL pBlkCacheGlobal = pVM->pUVM->pdm.s.pBlkCacheGlobal;
    PPDMBLKCACHE pBlkCache, pBlkCacheNext;

    /* Return silently if not supported. */
    if (!pBlkCacheGlobal)
        return;

    pdmBlkCacheLockEnter(pBlkCacheGlobal);

    RTListForEachSafe(&pBlkCacheGlobal->ListUsers, pBlkCache, pBlkCacheNext, PDMBLKCACHE, NodeCacheUser)
    {
        if (    pBlkCache->enmType == PDMBLKCACHETYPE_USB
            &&  pBlkCache->u.Usb.pUsbIns == pUsbIns)
            PDMR3BlkCacheRelease(pBlkCache);
    }

    pdmBlkCacheLockLeave(pBlkCacheGlobal);
}

static PPDMBLKCACHEENTRY pdmBlkCacheGetCacheEntryByOffset(PPDMBLKCACHE pBlkCache, uint64_t off)
{
    STAM_PROFILE_ADV_START(&pBlkCache->pCache->StatTreeGet, Cache);

    RTSemRWRequestRead(pBlkCache->SemRWEntries, RT_INDEFINITE_WAIT);
    PPDMBLKCACHEENTRY pEntry = (PPDMBLKCACHEENTRY)RTAvlrU64RangeGet(pBlkCache->pTree, off);
    if (pEntry)
        pdmBlkCacheEntryRef(pEntry);
    RTSemRWReleaseRead(pBlkCache->SemRWEntries);

    STAM_PROFILE_ADV_STOP(&pBlkCache->pCache->StatTreeGet, Cache);

    return pEntry;
}

/**
 * Return the best fit cache entries for the given offset.
 *
 * @param   pBlkCache    The endpoint cache.
 * @param   off          The offset.
 * @param   ppEntryAbove Where to store the pointer to the best fit entry above
 *                       the given offset. NULL if not required.
 */
static void pdmBlkCacheGetCacheBestFitEntryByOffset(PPDMBLKCACHE pBlkCache, uint64_t off, PPDMBLKCACHEENTRY *ppEntryAbove)
{
    STAM_PROFILE_ADV_START(&pBlkCache->pCache->StatTreeGet, Cache);

    RTSemRWRequestRead(pBlkCache->SemRWEntries, RT_INDEFINITE_WAIT);
    if (ppEntryAbove)
    {
        *ppEntryAbove = (PPDMBLKCACHEENTRY)RTAvlrU64GetBestFit(pBlkCache->pTree, off, true /*fAbove*/);
        if (*ppEntryAbove)
            pdmBlkCacheEntryRef(*ppEntryAbove);
    }

    RTSemRWReleaseRead(pBlkCache->SemRWEntries);

    STAM_PROFILE_ADV_STOP(&pBlkCache->pCache->StatTreeGet, Cache);
}

static void pdmBlkCacheInsertEntry(PPDMBLKCACHE pBlkCache, PPDMBLKCACHEENTRY pEntry)
{
    STAM_PROFILE_ADV_START(&pBlkCache->pCache->StatTreeInsert, Cache);
    RTSemRWRequestWrite(pBlkCache->SemRWEntries, RT_INDEFINITE_WAIT);
    bool fInserted = RTAvlrU64Insert(pBlkCache->pTree, &pEntry->Core);
    AssertMsg(fInserted, ("Node was not inserted into tree\n")); NOREF(fInserted);
    STAM_PROFILE_ADV_STOP(&pBlkCache->pCache->StatTreeInsert, Cache);
    RTSemRWReleaseWrite(pBlkCache->SemRWEntries);
}

/**
 * Allocates and initializes a new entry for the cache.
 * The entry has a reference count of 1.
 *
 * @returns Pointer to the new cache entry or NULL if out of memory.
 * @param   pBlkCache The cache the entry belongs to.
 * @param   off       Start offset.
 * @param   cbData    Size of the cache entry.
 * @param   pbBuffer  Pointer to the buffer to use.
 *                    NULL if a new buffer should be allocated.
 *                    The buffer needs to have the same size of the entry.
 */
static PPDMBLKCACHEENTRY pdmBlkCacheEntryAlloc(PPDMBLKCACHE pBlkCache, uint64_t off, size_t cbData, uint8_t *pbBuffer)
{
    AssertReturn(cbData <= UINT32_MAX, NULL);
    PPDMBLKCACHEENTRY pEntryNew = (PPDMBLKCACHEENTRY)RTMemAllocZ(sizeof(PDMBLKCACHEENTRY));

    if (RT_UNLIKELY(!pEntryNew))
        return NULL;

    pEntryNew->Core.Key      = off;
    pEntryNew->Core.KeyLast  = off + cbData - 1;
    pEntryNew->pBlkCache     = pBlkCache;
    pEntryNew->fFlags        = 0;
    pEntryNew->cRefs         = 1; /* We are using it now. */
    pEntryNew->pList         = NULL;
    pEntryNew->cbData        = (uint32_t)cbData;
    pEntryNew->pWaitingHead  = NULL;
    pEntryNew->pWaitingTail  = NULL;
    if (pbBuffer)
        pEntryNew->pbData    = pbBuffer;
    else
        pEntryNew->pbData    = (uint8_t *)RTMemPageAlloc(cbData);

    if (RT_UNLIKELY(!pEntryNew->pbData))
    {
        RTMemFree(pEntryNew);
        return NULL;
    }

    return pEntryNew;
}

/**
 * Checks that a set of flags is set/clear acquiring the R/W semaphore
 * in exclusive mode.
 *
 * @returns true if the flag in fSet is set and the one in fClear is clear.
 *          false otherwise.
 *          The R/W semaphore is only held if true is returned.
 *
 * @param   pBlkCache   The endpoint cache instance data.
 * @param   pEntry           The entry to check the flags for.
 * @param   fSet             The flag which is tested to be set.
 * @param   fClear           The flag which is tested to be clear.
 */
DECLINLINE(bool) pdmBlkCacheEntryFlagIsSetClearAcquireLock(PPDMBLKCACHE pBlkCache,
                                                           PPDMBLKCACHEENTRY pEntry,
                                                           uint32_t fSet, uint32_t fClear)
{
    uint32_t fFlags = ASMAtomicReadU32(&pEntry->fFlags);
    bool fPassed = ((fFlags & fSet) && !(fFlags & fClear));

    if (fPassed)
    {
        /* Acquire the lock and check again because the completion callback might have raced us. */
        RTSemRWRequestWrite(pBlkCache->SemRWEntries, RT_INDEFINITE_WAIT);

        fFlags = ASMAtomicReadU32(&pEntry->fFlags);
        fPassed = ((fFlags & fSet) && !(fFlags & fClear));

        /* Drop the lock if we didn't passed the test. */
        if (!fPassed)
            RTSemRWReleaseWrite(pBlkCache->SemRWEntries);
    }

    return fPassed;
}

/**
 * Adds a segment to the waiting list for a cache entry
 * which is currently in progress.
 *
 * @param   pEntry      The cache entry to add the segment to.
 * @param   pWaiter     The waiter entry to add.
 */
DECLINLINE(void) pdmBlkCacheEntryAddWaiter(PPDMBLKCACHEENTRY pEntry, PPDMBLKCACHEWAITER pWaiter)
{
    pWaiter->pNext = NULL;

    if (pEntry->pWaitingHead)
    {
        AssertPtr(pEntry->pWaitingTail);

        pEntry->pWaitingTail->pNext = pWaiter;
        pEntry->pWaitingTail = pWaiter;
    }
    else
    {
        Assert(!pEntry->pWaitingTail);

        pEntry->pWaitingHead = pWaiter;
        pEntry->pWaitingTail = pWaiter;
    }
}

/**
 * Add a buffer described by the I/O memory context
 * to the entry waiting for completion.
 *
 * @returns VBox status code.
 * @param   pEntry      The entry to add the buffer to.
 * @param   pReq        The request.
 * @param   pSgBuf      The scatter/gather buffer. Will be advanced by cbData.
 * @param   offDiff     Offset from the start of the buffer in the entry.
 * @param   cbData      Amount of data to wait for onthis entry.
 * @param   fWrite      Flag whether the task waits because it wants to write to
 *                      the cache entry.
 */
static int pdmBlkCacheEntryWaitersAdd(PPDMBLKCACHEENTRY pEntry, PPDMBLKCACHEREQ pReq,
                                      PRTSGBUF pSgBuf, uint64_t offDiff, size_t cbData, bool fWrite)
{
    PPDMBLKCACHEWAITER pWaiter  = (PPDMBLKCACHEWAITER)RTMemAllocZ(sizeof(PDMBLKCACHEWAITER));
    if (!pWaiter)
        return VERR_NO_MEMORY;

    ASMAtomicIncU32(&pReq->cXfersPending);
    pWaiter->pReq          = pReq;
    pWaiter->offCacheEntry = offDiff;
    pWaiter->cbTransfer    = cbData;
    pWaiter->fWrite        = fWrite;
    RTSgBufClone(&pWaiter->SgBuf, pSgBuf);
    RTSgBufAdvance(pSgBuf, cbData);

    pdmBlkCacheEntryAddWaiter(pEntry, pWaiter);

    return VINF_SUCCESS;
}

/**
 * Calculate aligned offset and size for a new cache entry which do not
 * intersect with an already existing entry and the file end.
 *
 * @returns The number of bytes the entry can hold of the requested amount
 *          of bytes.
 * @param   pBlkCache       The endpoint cache.
 * @param   off             The start offset.
 * @param   cb              The number of bytes the entry needs to hold at
 *                          least.
 * @param   pcbEntry        Where to store the number of bytes the entry can hold.
 *                          Can be less than given because of other entries.
 */
static uint32_t pdmBlkCacheEntryBoundariesCalc(PPDMBLKCACHE pBlkCache,
                                               uint64_t off, uint32_t cb,
                                               uint32_t *pcbEntry)
{
    /* Get the best fit entries around the offset */
    PPDMBLKCACHEENTRY pEntryAbove = NULL;
    pdmBlkCacheGetCacheBestFitEntryByOffset(pBlkCache, off, &pEntryAbove);

    /* Log the info */
    LogFlow(("%sest fit entry above off=%llu (BestFit=%llu BestFitEnd=%llu BestFitSize=%u)\n",
             pEntryAbove ? "B" : "No b",
             off,
             pEntryAbove ? pEntryAbove->Core.Key : 0,
             pEntryAbove ? pEntryAbove->Core.KeyLast : 0,
             pEntryAbove ? pEntryAbove->cbData : 0));

    uint32_t cbNext;
    uint32_t cbInEntry;
    if (    pEntryAbove
        &&  off + cb > pEntryAbove->Core.Key)
    {
        cbInEntry = (uint32_t)(pEntryAbove->Core.Key - off);
        cbNext = (uint32_t)(pEntryAbove->Core.Key - off);
    }
    else
    {
        cbInEntry = cb;
        cbNext    = cb;
    }

    /* A few sanity checks */
    AssertMsg(!pEntryAbove || off + cbNext <= pEntryAbove->Core.Key,
              ("Aligned size intersects with another cache entry\n"));
    Assert(cbInEntry <= cbNext);

    if (pEntryAbove)
        pdmBlkCacheEntryRelease(pEntryAbove);

    LogFlow(("off=%llu cbNext=%u\n", off, cbNext));

    *pcbEntry  = cbNext;

    return cbInEntry;
}

/**
 * Create a new cache entry evicting data from the cache if required.
 *
 * @returns Pointer to the new cache entry or NULL
 *          if not enough bytes could be evicted from the cache.
 * @param   pBlkCache       The endpoint cache.
 * @param   off             The offset.
 * @param   cb              Number of bytes the cache entry should have.
 * @param   pcbData         Where to store the number of bytes the new
 *                          entry can hold. May be lower than actually
 *                          requested due to another entry intersecting the
 *                          access range.
 */
static PPDMBLKCACHEENTRY pdmBlkCacheEntryCreate(PPDMBLKCACHE pBlkCache, uint64_t off, size_t cb, size_t *pcbData)
{
    uint32_t cbEntry  = 0;

    *pcbData = pdmBlkCacheEntryBoundariesCalc(pBlkCache, off, (uint32_t)cb, &cbEntry);
    AssertReturn(cb <= UINT32_MAX, NULL);

    PPDMBLKCACHEGLOBAL pCache = pBlkCache->pCache;
    pdmBlkCacheLockEnter(pCache);

    PPDMBLKCACHEENTRY pEntryNew = NULL;
    uint8_t          *pbBuffer  = NULL;
    bool fEnough = pdmBlkCacheReclaim(pCache, cbEntry, true, &pbBuffer);
    if (fEnough)
    {
        LogFlow(("Evicted enough bytes (%u requested). Creating new cache entry\n", cbEntry));

        pEntryNew = pdmBlkCacheEntryAlloc(pBlkCache, off, cbEntry, pbBuffer);
        if (RT_LIKELY(pEntryNew))
        {
            pdmBlkCacheEntryAddToList(&pCache->LruRecentlyUsedIn, pEntryNew);
            pdmBlkCacheAdd(pCache, cbEntry);
            pdmBlkCacheLockLeave(pCache);

            pdmBlkCacheInsertEntry(pBlkCache, pEntryNew);

            AssertMsg(   (off >= pEntryNew->Core.Key)
                      && (off + *pcbData <= pEntryNew->Core.KeyLast + 1),
                      ("Overflow in calculation off=%llu\n", off));
        }
        else
            pdmBlkCacheLockLeave(pCache);
    }
    else
        pdmBlkCacheLockLeave(pCache);

    return pEntryNew;
}

static PPDMBLKCACHEREQ pdmBlkCacheReqAlloc(void *pvUser)
{
    PPDMBLKCACHEREQ pReq = (PPDMBLKCACHEREQ)RTMemAlloc(sizeof(PDMBLKCACHEREQ));

    if (RT_LIKELY(pReq))
    {
        pReq->pvUser = pvUser;
        pReq->rcReq  = VINF_SUCCESS;
        pReq->cXfersPending = 0;
    }

    return pReq;
}

static void pdmBlkCacheReqComplete(PPDMBLKCACHE pBlkCache, PPDMBLKCACHEREQ pReq)
{
    switch (pBlkCache->enmType)
    {
        case PDMBLKCACHETYPE_DEV:
        {
            pBlkCache->u.Dev.pfnXferComplete(pBlkCache->u.Dev.pDevIns,
                                             pReq->pvUser, pReq->rcReq);
            break;
        }
        case PDMBLKCACHETYPE_DRV:
        {
            pBlkCache->u.Drv.pfnXferComplete(pBlkCache->u.Drv.pDrvIns,
                                             pReq->pvUser, pReq->rcReq);
            break;
        }
        case PDMBLKCACHETYPE_USB:
        {
            pBlkCache->u.Usb.pfnXferComplete(pBlkCache->u.Usb.pUsbIns,
                                             pReq->pvUser, pReq->rcReq);
            break;
        }
        case PDMBLKCACHETYPE_INTERNAL:
        {
            pBlkCache->u.Int.pfnXferComplete(pBlkCache->u.Int.pvUser,
                                             pReq->pvUser, pReq->rcReq);
            break;
        }
        default:
            AssertMsgFailed(("Unknown block cache type!\n"));
    }

    RTMemFree(pReq);
}

static bool pdmBlkCacheReqUpdate(PPDMBLKCACHE pBlkCache, PPDMBLKCACHEREQ pReq,
                                 int rcReq, bool fCallHandler)
{
    if (RT_FAILURE(rcReq))
        ASMAtomicCmpXchgS32(&pReq->rcReq, rcReq, VINF_SUCCESS);

    AssertMsg(pReq->cXfersPending > 0, ("No transfers are pending for this request\n"));
    uint32_t cXfersPending = ASMAtomicDecU32(&pReq->cXfersPending);

    if (!cXfersPending)
    {
        if (fCallHandler)
            pdmBlkCacheReqComplete(pBlkCache, pReq);
        return true;
    }

    LogFlowFunc(("pReq=%#p cXfersPending=%u\n", pReq, cXfersPending));
    return false;
}

VMMR3DECL(int) PDMR3BlkCacheRead(PPDMBLKCACHE pBlkCache, uint64_t off,
                                 PCRTSGBUF pSgBuf, size_t cbRead, void *pvUser)
{
    int rc = VINF_SUCCESS;
    PPDMBLKCACHEGLOBAL pCache = pBlkCache->pCache;
    PPDMBLKCACHEENTRY  pEntry;
    PPDMBLKCACHEREQ    pReq;

    LogFlowFunc((": pBlkCache=%#p{%s} off=%llu pSgBuf=%#p cbRead=%u pvUser=%#p\n",
                 pBlkCache, pBlkCache->pszId, off, pSgBuf, cbRead, pvUser));

    AssertPtrReturn(pBlkCache, VERR_INVALID_POINTER);
    AssertReturn(!pBlkCache->fSuspended, VERR_INVALID_STATE);

    RTSGBUF SgBuf;
    RTSgBufClone(&SgBuf, pSgBuf);

    /* Allocate new request structure. */
    pReq = pdmBlkCacheReqAlloc(pvUser);
    if (RT_UNLIKELY(!pReq))
        return VERR_NO_MEMORY;

    /* Increment data transfer counter to keep the request valid while we access it. */
    ASMAtomicIncU32(&pReq->cXfersPending);

    while (cbRead)
    {
        size_t cbToRead;

        pEntry = pdmBlkCacheGetCacheEntryByOffset(pBlkCache, off);

        /*
         * If there is no entry we try to create a new one eviciting unused pages
         * if the cache is full. If this is not possible we will pass the request through
         * and skip the caching (all entries may be still in progress so they can't
         * be evicted)
         * If we have an entry it can be in one of the LRU lists where the entry
         * contains data (recently used or frequently used LRU) so we can just read
         * the data we need and put the entry at the head of the frequently used LRU list.
         * In case the entry is in one of the ghost lists it doesn't contain any data.
         * We have to fetch it again evicting pages from either T1 or T2 to make room.
         */
        if (pEntry)
        {
            uint64_t offDiff = off - pEntry->Core.Key;

            AssertMsg(off >= pEntry->Core.Key,
                      ("Overflow in calculation off=%llu OffsetAligned=%llu\n",
                      off, pEntry->Core.Key));

            AssertPtr(pEntry->pList);

            cbToRead = RT_MIN(pEntry->cbData - offDiff, cbRead);

            AssertMsg(off + cbToRead <= pEntry->Core.Key + pEntry->Core.KeyLast + 1,
                      ("Buffer of cache entry exceeded off=%llu cbToRead=%d\n",
                       off, cbToRead));

            cbRead  -= cbToRead;

            if (!cbRead)
                STAM_COUNTER_INC(&pCache->cHits);
            else
                STAM_COUNTER_INC(&pCache->cPartialHits);

            STAM_COUNTER_ADD(&pCache->StatRead, cbToRead);

            /* Ghost lists contain no data. */
            if (   (pEntry->pList == &pCache->LruRecentlyUsedIn)
                || (pEntry->pList == &pCache->LruFrequentlyUsed))
            {
                if (pdmBlkCacheEntryFlagIsSetClearAcquireLock(pBlkCache, pEntry,
                                                              PDMBLKCACHE_ENTRY_IO_IN_PROGRESS,
                                                              PDMBLKCACHE_ENTRY_IS_DIRTY))
                {
                    /* Entry didn't completed yet. Append to the list */
                    pdmBlkCacheEntryWaitersAdd(pEntry, pReq,
                                               &SgBuf, offDiff, cbToRead,
                                               false /* fWrite */);
                    RTSemRWReleaseWrite(pBlkCache->SemRWEntries);
                }
                else
                {
                    /* Read as much as we can from the entry. */
                    RTSgBufCopyFromBuf(&SgBuf, pEntry->pbData + offDiff, cbToRead);
                }

                /* Move this entry to the top position */
                if (pEntry->pList == &pCache->LruFrequentlyUsed)
                {
                    pdmBlkCacheLockEnter(pCache);
                    pdmBlkCacheEntryAddToList(&pCache->LruFrequentlyUsed, pEntry);
                    pdmBlkCacheLockLeave(pCache);
                }
                /* Release the entry */
                pdmBlkCacheEntryRelease(pEntry);
            }
            else
            {
                uint8_t *pbBuffer = NULL;

                LogFlow(("Fetching data for ghost entry %#p from file\n", pEntry));

                pdmBlkCacheLockEnter(pCache);
                pdmBlkCacheEntryRemoveFromList(pEntry); /* Remove it before we remove data, otherwise it may get freed when evicting data. */
                bool fEnough = pdmBlkCacheReclaim(pCache, pEntry->cbData, true, &pbBuffer);

                /* Move the entry to Am and fetch it to the cache. */
                if (fEnough)
                {
                    pdmBlkCacheEntryAddToList(&pCache->LruFrequentlyUsed, pEntry);
                    pdmBlkCacheAdd(pCache, pEntry->cbData);
                    pdmBlkCacheLockLeave(pCache);

                    if (pbBuffer)
                        pEntry->pbData = pbBuffer;
                    else
                        pEntry->pbData = (uint8_t *)RTMemPageAlloc(pEntry->cbData);
                    AssertPtr(pEntry->pbData);

                    pdmBlkCacheEntryWaitersAdd(pEntry, pReq,
                                               &SgBuf, offDiff, cbToRead,
                                               false /* fWrite */);
                    pdmBlkCacheEntryReadFromMedium(pEntry);
                    /* Release the entry */
                    pdmBlkCacheEntryRelease(pEntry);
                }
                else
                {
                    RTSemRWRequestWrite(pBlkCache->SemRWEntries, RT_INDEFINITE_WAIT);
                    STAM_PROFILE_ADV_START(&pCache->StatTreeRemove, Cache);
                    RTAvlrU64Remove(pBlkCache->pTree, pEntry->Core.Key);
                    STAM_PROFILE_ADV_STOP(&pCache->StatTreeRemove, Cache);
                    RTSemRWReleaseWrite(pBlkCache->SemRWEntries);

                    pdmBlkCacheLockLeave(pCache);

                    RTMemFree(pEntry);

                    pdmBlkCacheRequestPassthrough(pBlkCache, pReq,
                                                  &SgBuf, off, cbToRead,
                                                  PDMBLKCACHEXFERDIR_READ);
                }
            }
        }
        else
        {
#ifdef VBOX_WITH_IO_READ_CACHE
            /* No entry found for this offset. Create a new entry and fetch the data to the cache. */
            PPDMBLKCACHEENTRY pEntryNew = pdmBlkCacheEntryCreate(pBlkCache,
                                                                 off, cbRead,
                                                                 &cbToRead);

            cbRead -= cbToRead;

            if (pEntryNew)
            {
                if (!cbRead)
                    STAM_COUNTER_INC(&pCache->cMisses);
                else
                    STAM_COUNTER_INC(&pCache->cPartialHits);

                pdmBlkCacheEntryWaitersAdd(pEntryNew, pReq,
                                           &SgBuf,
                                           off - pEntryNew->Core.Key,
                                           cbToRead,
                                           false /* fWrite */);
                pdmBlkCacheEntryReadFromMedium(pEntryNew);
                pdmBlkCacheEntryRelease(pEntryNew); /* it is protected by the I/O in progress flag now. */
            }
            else
            {
                /*
                 * There is not enough free space in the cache.
                 * Pass the request directly to the I/O manager.
                 */
                LogFlow(("Couldn't evict %u bytes from the cache. Remaining request will be passed through\n", cbToRead));

                pdmBlkCacheRequestPassthrough(pBlkCache, pReq,
                                              &SgBuf, off, cbToRead,
                                              PDMBLKCACHEXFERDIR_READ);
            }
#else
            /* Clip read size if necessary. */
            PPDMBLKCACHEENTRY pEntryAbove;
            pdmBlkCacheGetCacheBestFitEntryByOffset(pBlkCache, off, &pEntryAbove);

            if (pEntryAbove)
            {
                if (off + cbRead > pEntryAbove->Core.Key)
                    cbToRead = pEntryAbove->Core.Key - off;
                else
                    cbToRead = cbRead;

                pdmBlkCacheEntryRelease(pEntryAbove);
            }
            else
                cbToRead = cbRead;

            cbRead -= cbToRead;
            pdmBlkCacheRequestPassthrough(pBlkCache, pReq,
                                          &SgBuf, off, cbToRead,
                                          PDMBLKCACHEXFERDIR_READ);
#endif
        }
        off += cbToRead;
    }

    if (!pdmBlkCacheReqUpdate(pBlkCache, pReq, rc, false))
        rc = VINF_AIO_TASK_PENDING;
    else
    {
        rc = pReq->rcReq;
        RTMemFree(pReq);
    }

    LogFlowFunc((": Leave rc=%Rrc\n", rc));

   return rc;
}

VMMR3DECL(int) PDMR3BlkCacheWrite(PPDMBLKCACHE pBlkCache, uint64_t off, PCRTSGBUF pSgBuf, size_t cbWrite, void *pvUser)
{
    int rc = VINF_SUCCESS;
    PPDMBLKCACHEGLOBAL pCache = pBlkCache->pCache;
    PPDMBLKCACHEENTRY pEntry;
    PPDMBLKCACHEREQ pReq;

    LogFlowFunc((": pBlkCache=%#p{%s} off=%llu pSgBuf=%#p cbWrite=%u pvUser=%#p\n",
                 pBlkCache, pBlkCache->pszId, off, pSgBuf, cbWrite, pvUser));

    AssertPtrReturn(pBlkCache, VERR_INVALID_POINTER);
    AssertReturn(!pBlkCache->fSuspended, VERR_INVALID_STATE);

    RTSGBUF SgBuf;
    RTSgBufClone(&SgBuf, pSgBuf);

    /* Allocate new request structure. */
    pReq = pdmBlkCacheReqAlloc(pvUser);
    if (RT_UNLIKELY(!pReq))
        return VERR_NO_MEMORY;

    /* Increment data transfer counter to keep the request valid while we access it. */
    ASMAtomicIncU32(&pReq->cXfersPending);

    while (cbWrite)
    {
        size_t cbToWrite;

        pEntry = pdmBlkCacheGetCacheEntryByOffset(pBlkCache, off);
        if (pEntry)
        {
            /* Write the data into the entry and mark it as dirty */
            AssertPtr(pEntry->pList);

            uint64_t offDiff = off - pEntry->Core.Key;
            AssertMsg(off >= pEntry->Core.Key, ("Overflow in calculation off=%llu OffsetAligned=%llu\n", off, pEntry->Core.Key));

            cbToWrite = RT_MIN(pEntry->cbData - offDiff, cbWrite);
            cbWrite  -= cbToWrite;

            if (!cbWrite)
                STAM_COUNTER_INC(&pCache->cHits);
            else
                STAM_COUNTER_INC(&pCache->cPartialHits);

            STAM_COUNTER_ADD(&pCache->StatWritten, cbToWrite);

            /* Ghost lists contain no data. */
            if (   (pEntry->pList == &pCache->LruRecentlyUsedIn)
                || (pEntry->pList == &pCache->LruFrequentlyUsed))
            {
                /* Check if the entry is dirty. */
                if (pdmBlkCacheEntryFlagIsSetClearAcquireLock(pBlkCache, pEntry,
                                                              PDMBLKCACHE_ENTRY_IS_DIRTY,
                                                              0))
                {
                    /* If it is already dirty but not in progress just update the data. */
                    if (!(pEntry->fFlags & PDMBLKCACHE_ENTRY_IO_IN_PROGRESS))
                        RTSgBufCopyToBuf(&SgBuf, pEntry->pbData + offDiff, cbToWrite);
                    else
                    {
                        /* The data isn't written to the file yet */
                        pdmBlkCacheEntryWaitersAdd(pEntry, pReq,
                                                   &SgBuf, offDiff, cbToWrite,
                                                   true /* fWrite */);
                        STAM_COUNTER_INC(&pBlkCache->StatWriteDeferred);
                    }

                    RTSemRWReleaseWrite(pBlkCache->SemRWEntries);
                }
                else /* Dirty bit not set */
                {
                    /*
                     * Check if a read is in progress for this entry.
                     * We have to defer processing in that case.
                     */
                    if (pdmBlkCacheEntryFlagIsSetClearAcquireLock(pBlkCache, pEntry,
                                                                  PDMBLKCACHE_ENTRY_IO_IN_PROGRESS,
                                                                  0))
                    {
                        pdmBlkCacheEntryWaitersAdd(pEntry, pReq,
                                                   &SgBuf, offDiff, cbToWrite,
                                                   true /* fWrite */);
                        STAM_COUNTER_INC(&pBlkCache->StatWriteDeferred);
                        RTSemRWReleaseWrite(pBlkCache->SemRWEntries);
                    }
                    else /* I/O in progress flag not set */
                    {
                        /* Write as much as we can into the entry and update the file. */
                        RTSgBufCopyToBuf(&SgBuf, pEntry->pbData + offDiff, cbToWrite);

                        bool fCommit = pdmBlkCacheAddDirtyEntry(pBlkCache, pEntry);
                        if (fCommit)
                            pdmBlkCacheCommitDirtyEntries(pCache);
                    }
                } /* Dirty bit not set */

                /* Move this entry to the top position */
                if (pEntry->pList == &pCache->LruFrequentlyUsed)
                {
                    pdmBlkCacheLockEnter(pCache);
                    pdmBlkCacheEntryAddToList(&pCache->LruFrequentlyUsed, pEntry);
                    pdmBlkCacheLockLeave(pCache);
                }

                pdmBlkCacheEntryRelease(pEntry);
            }
            else /* Entry is on the ghost list */
            {
                uint8_t *pbBuffer = NULL;

                pdmBlkCacheLockEnter(pCache);
                pdmBlkCacheEntryRemoveFromList(pEntry); /* Remove it before we remove data, otherwise it may get freed when evicting data. */
                bool fEnough = pdmBlkCacheReclaim(pCache, pEntry->cbData, true, &pbBuffer);

                if (fEnough)
                {
                    /* Move the entry to Am and fetch it to the cache. */
                    pdmBlkCacheEntryAddToList(&pCache->LruFrequentlyUsed, pEntry);
                    pdmBlkCacheAdd(pCache, pEntry->cbData);
                    pdmBlkCacheLockLeave(pCache);

                    if (pbBuffer)
                        pEntry->pbData = pbBuffer;
                    else
                        pEntry->pbData = (uint8_t *)RTMemPageAlloc(pEntry->cbData);
                    AssertPtr(pEntry->pbData);

                    pdmBlkCacheEntryWaitersAdd(pEntry, pReq,
                                               &SgBuf, offDiff, cbToWrite,
                                               true /* fWrite */);
                    STAM_COUNTER_INC(&pBlkCache->StatWriteDeferred);
                    pdmBlkCacheEntryReadFromMedium(pEntry);

                    /* Release the reference. If it is still needed the I/O in progress flag should protect it now. */
                    pdmBlkCacheEntryRelease(pEntry);
                }
                else
                {
                    RTSemRWRequestWrite(pBlkCache->SemRWEntries, RT_INDEFINITE_WAIT);
                    STAM_PROFILE_ADV_START(&pCache->StatTreeRemove, Cache);
                    RTAvlrU64Remove(pBlkCache->pTree, pEntry->Core.Key);
                    STAM_PROFILE_ADV_STOP(&pCache->StatTreeRemove, Cache);
                    RTSemRWReleaseWrite(pBlkCache->SemRWEntries);

                    pdmBlkCacheLockLeave(pCache);

                    RTMemFree(pEntry);
                    pdmBlkCacheRequestPassthrough(pBlkCache, pReq,
                                                  &SgBuf, off, cbToWrite,
                                                  PDMBLKCACHEXFERDIR_WRITE);
                }
            }
        }
        else /* No entry found */
        {
            /*
             * No entry found. Try to create a new cache entry to store the data in and if that fails
             * write directly to the file.
             */
            PPDMBLKCACHEENTRY pEntryNew = pdmBlkCacheEntryCreate(pBlkCache,
                                                                 off, cbWrite,
                                                                 &cbToWrite);

            cbWrite -= cbToWrite;

            if (pEntryNew)
            {
                uint64_t offDiff = off - pEntryNew->Core.Key;

                STAM_COUNTER_INC(&pCache->cHits);

                /*
                 * Check if it is possible to just write the data without waiting
                 * for it to get fetched first.
                 */
                if (!offDiff && pEntryNew->cbData == cbToWrite)
                {
                    RTSgBufCopyToBuf(&SgBuf, pEntryNew->pbData, cbToWrite);

                    bool fCommit = pdmBlkCacheAddDirtyEntry(pBlkCache, pEntryNew);
                    if (fCommit)
                        pdmBlkCacheCommitDirtyEntries(pCache);
                    STAM_COUNTER_ADD(&pCache->StatWritten, cbToWrite);
                }
                else
                {
                    /* Defer the write and fetch the data from the endpoint. */
                    pdmBlkCacheEntryWaitersAdd(pEntryNew, pReq,
                                               &SgBuf, offDiff, cbToWrite,
                                               true /* fWrite */);
                    STAM_COUNTER_INC(&pBlkCache->StatWriteDeferred);
                    pdmBlkCacheEntryReadFromMedium(pEntryNew);
                }

                pdmBlkCacheEntryRelease(pEntryNew);
            }
            else
            {
                /*
                 * There is not enough free space in the cache.
                 * Pass the request directly to the I/O manager.
                 */
                LogFlow(("Couldn't evict %u bytes from the cache. Remaining request will be passed through\n", cbToWrite));

                STAM_COUNTER_INC(&pCache->cMisses);

                pdmBlkCacheRequestPassthrough(pBlkCache, pReq,
                                              &SgBuf, off, cbToWrite,
                                              PDMBLKCACHEXFERDIR_WRITE);
            }
        }

        off += cbToWrite;
    }

    if (!pdmBlkCacheReqUpdate(pBlkCache, pReq, rc, false))
        rc = VINF_AIO_TASK_PENDING;
    else
    {
        rc = pReq->rcReq;
        RTMemFree(pReq);
    }

    LogFlowFunc((": Leave rc=%Rrc\n", rc));

    return rc;
}

VMMR3DECL(int) PDMR3BlkCacheFlush(PPDMBLKCACHE pBlkCache, void *pvUser)
{
    int rc = VINF_SUCCESS;
    PPDMBLKCACHEREQ pReq;

    LogFlowFunc((": pBlkCache=%#p{%s}\n", pBlkCache, pBlkCache->pszId));

    AssertPtrReturn(pBlkCache, VERR_INVALID_POINTER);
    AssertReturn(!pBlkCache->fSuspended, VERR_INVALID_STATE);

    /* Commit dirty entries in the cache. */
    pdmBlkCacheCommit(pBlkCache);

    /* Allocate new request structure. */
    pReq = pdmBlkCacheReqAlloc(pvUser);
    if (RT_UNLIKELY(!pReq))
        return VERR_NO_MEMORY;

    rc = pdmBlkCacheRequestPassthrough(pBlkCache, pReq, NULL, 0, 0,
                                       PDMBLKCACHEXFERDIR_FLUSH);
    AssertRC(rc);

    LogFlowFunc((": Leave rc=%Rrc\n", rc));
    return VINF_AIO_TASK_PENDING;
}

VMMR3DECL(int) PDMR3BlkCacheDiscard(PPDMBLKCACHE pBlkCache, PCRTRANGE paRanges,
                                    unsigned cRanges, void *pvUser)
{
    int rc = VINF_SUCCESS;
    PPDMBLKCACHEGLOBAL pCache = pBlkCache->pCache;
    PPDMBLKCACHEENTRY pEntry;
    PPDMBLKCACHEREQ pReq;

    LogFlowFunc((": pBlkCache=%#p{%s} paRanges=%#p cRanges=%u pvUser=%#p\n",
                 pBlkCache, pBlkCache->pszId, paRanges, cRanges, pvUser));

    AssertPtrReturn(pBlkCache, VERR_INVALID_POINTER);
    AssertReturn(!pBlkCache->fSuspended, VERR_INVALID_STATE);

    /* Allocate new request structure. */
    pReq = pdmBlkCacheReqAlloc(pvUser);
    if (RT_UNLIKELY(!pReq))
        return VERR_NO_MEMORY;

    /* Increment data transfer counter to keep the request valid while we access it. */
    ASMAtomicIncU32(&pReq->cXfersPending);

    for (unsigned i = 0; i < cRanges; i++)
    {
        uint64_t offCur = paRanges[i].offStart;
        size_t cbLeft = paRanges[i].cbRange;

        while (cbLeft)
        {
            size_t cbThisDiscard = 0;

            pEntry = pdmBlkCacheGetCacheEntryByOffset(pBlkCache, offCur);

            if (pEntry)
            {
                /* Write the data into the entry and mark it as dirty */
                AssertPtr(pEntry->pList);

                uint64_t offDiff = offCur - pEntry->Core.Key;

                AssertMsg(offCur >= pEntry->Core.Key,
                          ("Overflow in calculation offCur=%llu OffsetAligned=%llu\n",
                          offCur, pEntry->Core.Key));

                cbThisDiscard = RT_MIN(pEntry->cbData - offDiff, cbLeft);

                /* Ghost lists contain no data. */
                if (   (pEntry->pList == &pCache->LruRecentlyUsedIn)
                    || (pEntry->pList == &pCache->LruFrequentlyUsed))
                {
                    /* Check if the entry is dirty. */
                    if (pdmBlkCacheEntryFlagIsSetClearAcquireLock(pBlkCache, pEntry,
                                                                  PDMBLKCACHE_ENTRY_IS_DIRTY,
                                                                  0))
                    {
                        /* If it is dirty but not yet in progress remove it. */
                        if (!(pEntry->fFlags & PDMBLKCACHE_ENTRY_IO_IN_PROGRESS))
                        {
                            pdmBlkCacheLockEnter(pCache);
                            pdmBlkCacheEntryRemoveFromList(pEntry);

                            STAM_PROFILE_ADV_START(&pCache->StatTreeRemove, Cache);
                            RTAvlrU64Remove(pBlkCache->pTree, pEntry->Core.Key);
                            STAM_PROFILE_ADV_STOP(&pCache->StatTreeRemove, Cache);

                            pdmBlkCacheLockLeave(pCache);
                        }
                        else
                        {
#if 0
                            /* The data isn't written to the file yet */
                            pdmBlkCacheEntryWaitersAdd(pEntry, pReq,
                                                       &SgBuf, offDiff, cbToWrite,
                                                       true /* fWrite */);
                            STAM_COUNTER_INC(&pBlkCache->StatWriteDeferred);
#endif
                        }

                        RTSemRWReleaseWrite(pBlkCache->SemRWEntries);
                        pdmBlkCacheEntryRelease(pEntry);
                        RTMemFree(pEntry);
                    }
                    else /* Dirty bit not set */
                    {
                        /*
                         * Check if a read is in progress for this entry.
                         * We have to defer processing in that case.
                         */
                        if(pdmBlkCacheEntryFlagIsSetClearAcquireLock(pBlkCache, pEntry,
                                                                     PDMBLKCACHE_ENTRY_IO_IN_PROGRESS,
                                                                     0))
                        {
#if 0
                            pdmBlkCacheEntryWaitersAdd(pEntry, pReq,
                                                       &SgBuf, offDiff, cbToWrite,
                                                       true /* fWrite */);
#endif
                            STAM_COUNTER_INC(&pBlkCache->StatWriteDeferred);
                            RTSemRWReleaseWrite(pBlkCache->SemRWEntries);
                            pdmBlkCacheEntryRelease(pEntry);
                        }
                        else /* I/O in progress flag not set */
                        {
                            pdmBlkCacheLockEnter(pCache);
                            pdmBlkCacheEntryRemoveFromList(pEntry);

                            RTSemRWRequestWrite(pBlkCache->SemRWEntries, RT_INDEFINITE_WAIT);
                            STAM_PROFILE_ADV_START(&pCache->StatTreeRemove, Cache);
                            RTAvlrU64Remove(pBlkCache->pTree, pEntry->Core.Key);
                            STAM_PROFILE_ADV_STOP(&pCache->StatTreeRemove, Cache);
                            RTSemRWReleaseWrite(pBlkCache->SemRWEntries);

                            pdmBlkCacheLockLeave(pCache);

                            RTMemFree(pEntry);
                        }
                    } /* Dirty bit not set */
                }
                else /* Entry is on the ghost list just remove cache entry. */
                {
                    pdmBlkCacheLockEnter(pCache);
                    pdmBlkCacheEntryRemoveFromList(pEntry);

                    RTSemRWRequestWrite(pBlkCache->SemRWEntries, RT_INDEFINITE_WAIT);
                    STAM_PROFILE_ADV_START(&pCache->StatTreeRemove, Cache);
                    RTAvlrU64Remove(pBlkCache->pTree, pEntry->Core.Key);
                    STAM_PROFILE_ADV_STOP(&pCache->StatTreeRemove, Cache);
                    RTSemRWReleaseWrite(pBlkCache->SemRWEntries);

                    pdmBlkCacheLockLeave(pCache);

                    RTMemFree(pEntry);
                }
            }
            /* else: no entry found. */

            offCur += cbThisDiscard;
            cbLeft -= cbThisDiscard;
        }
    }

    if (!pdmBlkCacheReqUpdate(pBlkCache, pReq, rc, false))
        rc = VINF_AIO_TASK_PENDING;
    else
    {
        rc = pReq->rcReq;
        RTMemFree(pReq);
    }

    LogFlowFunc((": Leave rc=%Rrc\n", rc));

    return rc;
}

/**
 * Completes a task segment freeing all resources and completes the task handle
 * if everything was transferred.
 *
 * @returns Next task segment handle.
 * @param   pBlkCache         The endpoint block cache.
 * @param   pWaiter           Task segment to complete.
 * @param   rc                Status code to set.
 */
static PPDMBLKCACHEWAITER pdmBlkCacheWaiterComplete(PPDMBLKCACHE pBlkCache, PPDMBLKCACHEWAITER pWaiter, int rc)
{
    PPDMBLKCACHEWAITER pNext = pWaiter->pNext;
    PPDMBLKCACHEREQ pReq = pWaiter->pReq;

    pdmBlkCacheReqUpdate(pBlkCache, pReq, rc, true);

    RTMemFree(pWaiter);

    return pNext;
}

static void pdmBlkCacheIoXferCompleteEntry(PPDMBLKCACHE pBlkCache, PPDMBLKCACHEIOXFER hIoXfer, int rcIoXfer)
{
    PPDMBLKCACHEENTRY  pEntry    = hIoXfer->pEntry;
    PPDMBLKCACHEGLOBAL pCache    = pBlkCache->pCache;

    /* Reference the entry now as we are clearing the I/O in progress flag
     * which protected the entry till now. */
    pdmBlkCacheEntryRef(pEntry);

    RTSemRWRequestWrite(pBlkCache->SemRWEntries, RT_INDEFINITE_WAIT);
    pEntry->fFlags &= ~PDMBLKCACHE_ENTRY_IO_IN_PROGRESS;

    /* Process waiting segment list. The data in entry might have changed in-between. */
    bool fDirty = false;
    PPDMBLKCACHEWAITER pComplete = pEntry->pWaitingHead;
    PPDMBLKCACHEWAITER pCurr     = pComplete;

    AssertMsg((pCurr && pEntry->pWaitingTail) || (!pCurr && !pEntry->pWaitingTail),
                ("The list tail was not updated correctly\n"));
    pEntry->pWaitingTail = NULL;
    pEntry->pWaitingHead = NULL;

    if (hIoXfer->enmXferDir == PDMBLKCACHEXFERDIR_WRITE)
    {
        /*
         * An error here is difficult to handle as the original request completed already.
         * The error is logged for now and the VM is paused.
         * If the user continues the entry is written again in the hope
         * the user fixed the problem and the next write succeeds.
         */
        if (RT_FAILURE(rcIoXfer))
        {
            LogRel(("I/O cache: Error while writing entry at offset %llu (%u bytes) to medium \"%s\" (rc=%Rrc)\n",
                    pEntry->Core.Key, pEntry->cbData, pBlkCache->pszId, rcIoXfer));

            if (!ASMAtomicXchgBool(&pCache->fIoErrorVmSuspended, true))
            {
                int rc = VMSetRuntimeError(pCache->pVM, VMSETRTERR_FLAGS_SUSPEND | VMSETRTERR_FLAGS_NO_WAIT, "BLKCACHE_IOERR",
                                           N_("The I/O cache encountered an error while updating data in medium \"%s\" (rc=%Rrc). "
                                              "Make sure there is enough free space on the disk and that the disk is working properly. "
                                              "Operation can be resumed afterwards"),
                                           pBlkCache->pszId, rcIoXfer);
                AssertRC(rc);
            }

            /* Mark the entry as dirty again to get it added to the list later on. */
            fDirty = true;
        }

        pEntry->fFlags &= ~PDMBLKCACHE_ENTRY_IS_DIRTY;

        while (pCurr)
        {
            AssertMsg(pCurr->fWrite, ("Completed write entries should never have read tasks attached\n"));

            RTSgBufCopyToBuf(&pCurr->SgBuf, pEntry->pbData + pCurr->offCacheEntry, pCurr->cbTransfer);
            fDirty = true;
            pCurr = pCurr->pNext;
        }
    }
    else
    {
        AssertMsg(hIoXfer->enmXferDir == PDMBLKCACHEXFERDIR_READ, ("Invalid transfer type\n"));
        AssertMsg(!(pEntry->fFlags & PDMBLKCACHE_ENTRY_IS_DIRTY),
                  ("Invalid flags set\n"));

        while (pCurr)
        {
            if (pCurr->fWrite)
            {
                RTSgBufCopyToBuf(&pCurr->SgBuf, pEntry->pbData + pCurr->offCacheEntry, pCurr->cbTransfer);
                fDirty = true;
            }
            else
                RTSgBufCopyFromBuf(&pCurr->SgBuf, pEntry->pbData + pCurr->offCacheEntry, pCurr->cbTransfer);

            pCurr = pCurr->pNext;
        }
    }

    bool fCommit = false;
    if (fDirty)
        fCommit = pdmBlkCacheAddDirtyEntry(pBlkCache, pEntry);

    RTSemRWReleaseWrite(pBlkCache->SemRWEntries);

    /* Dereference so that it isn't protected anymore except we issued anyother write for it. */
    pdmBlkCacheEntryRelease(pEntry);

    if (fCommit)
        pdmBlkCacheCommitDirtyEntries(pCache);

    /* Complete waiters now. */
    while (pComplete)
        pComplete = pdmBlkCacheWaiterComplete(pBlkCache, pComplete, rcIoXfer);
}

VMMR3DECL(void) PDMR3BlkCacheIoXferComplete(PPDMBLKCACHE pBlkCache, PPDMBLKCACHEIOXFER hIoXfer, int rcIoXfer)
{
    LogFlowFunc(("pBlkCache=%#p hIoXfer=%#p rcIoXfer=%Rrc\n", pBlkCache, hIoXfer, rcIoXfer));

    if (hIoXfer->fIoCache)
        pdmBlkCacheIoXferCompleteEntry(pBlkCache, hIoXfer, rcIoXfer);
    else
        pdmBlkCacheReqUpdate(pBlkCache, hIoXfer->pReq, rcIoXfer, true);

    ASMAtomicDecU32(&pBlkCache->cIoXfersActive);
    pdmBlkCacheR3TraceMsgF(pBlkCache, "BlkCache: I/O req %#p (%RTbool) completed (%u now active)",
                           hIoXfer, hIoXfer->fIoCache, pBlkCache->cIoXfersActive);
    RTMemFree(hIoXfer);
}

/**
 * Callback for the AVL do with all routine. Waits for a cachen entry to finish any pending I/O.
 *
 * @returns IPRT status code.
 * @param    pNode     The node to destroy.
 * @param    pvUser    Opaque user data.
 */
static DECLCALLBACK(int) pdmBlkCacheEntryQuiesce(PAVLRU64NODECORE pNode, void *pvUser)
{
    PPDMBLKCACHEENTRY   pEntry    = (PPDMBLKCACHEENTRY)pNode;
    PPDMBLKCACHE        pBlkCache = pEntry->pBlkCache;
    NOREF(pvUser);

    while (ASMAtomicReadU32(&pEntry->fFlags) & PDMBLKCACHE_ENTRY_IO_IN_PROGRESS)
    {
        /* Leave the locks to let the I/O thread make progress but reference the entry to prevent eviction. */
        pdmBlkCacheEntryRef(pEntry);
        RTSemRWReleaseWrite(pBlkCache->SemRWEntries);

        RTThreadSleep(1);

        /* Re-enter all locks and drop the reference. */
        RTSemRWRequestWrite(pBlkCache->SemRWEntries, RT_INDEFINITE_WAIT);
        pdmBlkCacheEntryRelease(pEntry);
    }

    AssertMsg(!(pEntry->fFlags & PDMBLKCACHE_ENTRY_IO_IN_PROGRESS),
                ("Entry is dirty and/or still in progress fFlags=%#x\n", pEntry->fFlags));

    return VINF_SUCCESS;
}

VMMR3DECL(int) PDMR3BlkCacheSuspend(PPDMBLKCACHE pBlkCache)
{
    int rc = VINF_SUCCESS;
    LogFlowFunc(("pBlkCache=%#p\n", pBlkCache));

    AssertPtrReturn(pBlkCache, VERR_INVALID_POINTER);

    if (!ASMAtomicReadBool(&pBlkCache->pCache->fIoErrorVmSuspended))
        pdmBlkCacheCommit(pBlkCache); /* Can issue new I/O requests. */
    ASMAtomicXchgBool(&pBlkCache->fSuspended, true);

    /* Wait for all I/O to complete. */
    RTSemRWRequestWrite(pBlkCache->SemRWEntries, RT_INDEFINITE_WAIT);
    rc = RTAvlrU64DoWithAll(pBlkCache->pTree, true, pdmBlkCacheEntryQuiesce, NULL);
    AssertRC(rc);
    RTSemRWReleaseWrite(pBlkCache->SemRWEntries);

    return rc;
}

VMMR3DECL(int) PDMR3BlkCacheResume(PPDMBLKCACHE pBlkCache)
{
    LogFlowFunc(("pBlkCache=%#p\n", pBlkCache));

    AssertPtrReturn(pBlkCache, VERR_INVALID_POINTER);

    ASMAtomicXchgBool(&pBlkCache->fSuspended, false);

    return VINF_SUCCESS;
}

VMMR3DECL(int) PDMR3BlkCacheClear(PPDMBLKCACHE pBlkCache)
{
    PPDMBLKCACHEGLOBAL pCache = pBlkCache->pCache;

    /*
     * Commit all dirty entries now (they are waited on for completion during the
     * destruction of the AVL tree below).
     * The exception is if the VM was paused because of an I/O error before.
     */
    if (!ASMAtomicReadBool(&pCache->fIoErrorVmSuspended))
        pdmBlkCacheCommit(pBlkCache);

    /* Make sure nobody is accessing the cache while we delete the tree. */
    pdmBlkCacheLockEnter(pCache);
    RTSemRWRequestWrite(pBlkCache->SemRWEntries, RT_INDEFINITE_WAIT);
    RTAvlrU64Destroy(pBlkCache->pTree, pdmBlkCacheEntryDestroy, pCache);
    RTSemRWReleaseWrite(pBlkCache->SemRWEntries);

    pdmBlkCacheLockLeave(pCache);
    return VINF_SUCCESS;
}

