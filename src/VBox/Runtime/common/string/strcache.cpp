/* $Id: strcache.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - String Cache.
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
#include <iprt/strcache.h>
#include "internal/iprt.h"

#include <iprt/alloca.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/critsect.h>
#include <iprt/errcore.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/once.h>
#include <iprt/param.h>
#include <iprt/string.h>

#include "internal/strhash.h"
#include "internal/magics.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Special NIL pointer for the hash table.  It differs from NULL in that it is
 * a valid hash table entry when doing a lookup. */
#define PRTSTRCACHEENTRY_NIL                ((PRTSTRCACHEENTRY)~(uintptr_t)1)

/** Calcuates the increment when handling a collision.
 * The current formula makes sure it's always odd so we cannot possibly end
 * up a cyclic loop with an even sized table.  It also takes more bits from
 * the length part. */
#define RTSTRCACHE_COLLISION_INCR(uHashLen) ( ((uHashLen >> 8) | 1) )

/** The initial hash table size. Must be power of two. */
#define RTSTRCACHE_INITIAL_HASH_SIZE        512
/** The hash table growth factor. */
#define RTSTRCACHE_HASH_GROW_FACTOR         4

/**
 * The RTSTRCACHEENTRY size threshold at which we stop using our own allocator
 * and switch to the application heap, expressed as a power of two.
 *
 * Using a 1KB as a reasonable limit here.
 */
#ifdef RTSTRCACHE_WITH_MERGED_ALLOCATOR
# define RTSTRCACHE_HEAP_THRESHOLD_BIT      10
#else
# define RTSTRCACHE_HEAP_THRESHOLD_BIT      9
#endif
/** The RTSTRCACHE_HEAP_THRESHOLD_BIT as a byte limit. */
#define RTSTRCACHE_HEAP_THRESHOLD           RT_BIT_32(RTSTRCACHE_HEAP_THRESHOLD_BIT)
/** Big (heap) entry size alignment. */
#define RTSTRCACHE_HEAP_ENTRY_SIZE_ALIGN    16

#ifdef RTSTRCACHE_WITH_MERGED_ALLOCATOR
/**
 * The RTSTRCACHEENTRY size threshold at which we start using the merge free
 * list for allocations, expressed as a power of two.
 */
# define RTSTRCACHE_MERGED_THRESHOLD_BIT    6

/** The number of bytes (power of two) that the merged allocation lists should
 * be grown by.  Must be much greater than RTSTRCACHE_MERGED_THRESHOLD. */
# define RTSTRCACHE_MERGED_GROW_SIZE        _32K
#endif

/** The number of bytes (power of two) that the fixed allocation lists should
 * be grown by. */
#define RTSTRCACHE_FIXED_GROW_SIZE          _32K

/** The number of fixed sized lists. */
#define RTSTRCACHE_NUM_OF_FIXED_SIZES       12


/** Validates a string cache handle, translating RTSTRCACHE_DEFAULT when found,
 * and returns rc if not valid. */
#define RTSTRCACHE_VALID_RETURN_RC(pStrCache, rc) \
    do { \
        if ((pStrCache) == RTSTRCACHE_DEFAULT) \
        { \
            int rcOnce = RTOnce(&g_rtStrCacheOnce, rtStrCacheInitDefault, NULL); \
            if (RT_FAILURE(rcOnce)) \
                return (rc); \
            (pStrCache) = g_hrtStrCacheDefault; \
        } \
        else \
        { \
            AssertPtrReturn((pStrCache), (rc)); \
            AssertReturn((pStrCache)->u32Magic == RTSTRCACHE_MAGIC, (rc)); \
        } \
    } while (0)



/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * String cache entry.
 */
typedef struct RTSTRCACHEENTRY
{
    /** The number of references. */
    uint32_t volatile   cRefs;
    /** The lower 16-bit hash value. */
    uint16_t            uHash;
    /** The string length (excluding the terminator).
     * If this is set to RTSTRCACHEENTRY_BIG_LEN, this is a BIG entry
     * (RTSTRCACHEBIGENTRY). */
    uint16_t            cchString;
    /** The string. */
    char                szString[8];
} RTSTRCACHEENTRY;
AssertCompileSize(RTSTRCACHEENTRY, 16);
/** Pointer to a string cache entry. */
typedef RTSTRCACHEENTRY *PRTSTRCACHEENTRY;
/** Pointer to a const string cache entry. */
typedef RTSTRCACHEENTRY *PCRTSTRCACHEENTRY;

/** RTSTCACHEENTRY::cchString value for big cache entries. */
#define RTSTRCACHEENTRY_BIG_LEN UINT16_MAX

/**
 * Big string cache entry.
 *
 * These are allocated individually from the application heap.
 */
typedef struct RTSTRCACHEBIGENTRY
{
    /** List entry. */
    RTLISTNODE          ListEntry;
    /** The string length. */
    uint32_t            cchString;
    /** The full hash value / padding. */
    uint32_t            uHash;
    /** The core entry. */
    RTSTRCACHEENTRY     Core;
} RTSTRCACHEBIGENTRY;
AssertCompileSize(RTSTRCACHEENTRY, 16);
/** Pointer to a big string cache entry. */
typedef RTSTRCACHEBIGENTRY *PRTSTRCACHEBIGENTRY;
/** Pointer to a const big string cache entry. */
typedef RTSTRCACHEBIGENTRY *PCRTSTRCACHEBIGENTRY;


/**
 * A free string cache entry.
 */
typedef struct RTSTRCACHEFREE
{
    /** Zero value indicating that it's a free entry (no refs, no hash). */
    uint32_t                uZero;
    /** Number of free bytes.  Only used for > 32 byte allocations. */
    uint32_t                cbFree;
    /** Pointer to the next free item. */
    struct RTSTRCACHEFREE  *pNext;
} RTSTRCACHEFREE;
AssertCompileSize(RTSTRCACHEENTRY, 16);
AssertCompileMembersAtSameOffset(RTSTRCACHEENTRY, cRefs,    RTSTRCACHEFREE, uZero);
AssertCompileMembersAtSameOffset(RTSTRCACHEENTRY, szString, RTSTRCACHEFREE, pNext);
/** Pointer to a free string cache entry. */
typedef RTSTRCACHEFREE *PRTSTRCACHEFREE;

#ifdef RTSTRCACHE_WITH_MERGED_ALLOCATOR

/**
 * A free string cache entry with merging.
 *
 * This differs from RTSTRCACHEFREE only in having a back pointer for more
 * efficient list management (doubly vs. singly linked lists).
 */
typedef struct RTSTRCACHEFREEMERGE
{
    /** Marker that indicates what kind of entry this is, either . */
    uint32_t                    uMarker;
    /** Number of free bytes.  Only used for > 32 byte allocations. */
    uint32_t                    cbFree;
    /** Pointer to the main node.  NULL for main nodes. */
    struct RTSTRCACHEFREEMERGE *pMain;
    /** The free list entry. */
    RTLISTNODE                  ListEntry;
    /** Pads the size up to the minimum allocation unit for the merge list.
     * This both defines the minimum allocation unit and simplifies pointer
     * manipulation during merging and splitting. */
    uint8_t                     abPadding[ARCH_BITS == 32 ? 44 : 32];
} RTSTRCACHEFREEMERGE;
AssertCompileSize(RTSTRCACHEFREEMERGE, RT_BIT_32(RTSTRCACHE_MERGED_THRESHOLD_BIT));
/** Pointer to a free cache string in the merge list. */
typedef RTSTRCACHEFREEMERGE *PRTSTRCACHEFREEMERGE;

/** RTSTRCACHEFREEMERGE::uMarker value indicating that it's the real free chunk
 *  header.  Must be something that's invalid UTF-8 for both little and big
 *  endian system. */
# define RTSTRCACHEFREEMERGE_MAIN   UINT32_C(0xfffffff1)
/** RTSTRCACHEFREEMERGE::uMarker value indicating that it's part of a larger
 * chunk of free memory.  Must be something that's invalid UTF-8 for both little
 * and big endian system. */
# define RTSTRCACHEFREEMERGE_PART   UINT32_C(0xfffffff2)

#endif /* RTSTRCACHE_WITH_MERGED_ALLOCATOR */

/**
 * Tracking structure chunk of memory used by the 16 byte or 32 byte
 * allocations.
 *
 * This occupies the first entry in the chunk.
 */
typedef struct RTSTRCACHECHUNK
{
    /** The size of the chunk. */
    size_t                      cb;
    /** Pointer to the next chunk. */
    struct RTSTRCACHECHUNK     *pNext;
} RTSTRCACHECHUNK;
AssertCompile(sizeof(RTSTRCACHECHUNK) <= sizeof(RTSTRCACHEENTRY));
/** Pointer to the chunk tracking structure. */
typedef RTSTRCACHECHUNK *PRTSTRCACHECHUNK;


/**
 * Cache instance data.
 */
typedef struct RTSTRCACHEINT
{
    /** The string cache magic (RTSTRCACHE_MAGIC). */
    uint32_t                u32Magic;
    /** Ref counter for the cache handle. */
    uint32_t volatile       cRefs;
    /** The number of strings currently entered in the cache. */
    uint32_t                cStrings;
    /** The size of the hash table. */
    uint32_t                cHashTab;
    /** Pointer to the hash table. */
    PRTSTRCACHEENTRY       *papHashTab;
    /** Free list for allocations of the sizes defined by g_acbFixedLists. */
    PRTSTRCACHEFREE         apFreeLists[RTSTRCACHE_NUM_OF_FIXED_SIZES];
#ifdef RTSTRCACHE_WITH_MERGED_ALLOCATOR
    /** Free lists based on */
    RTLISTANCHOR            aMergedFreeLists[RTSTRCACHE_HEAP_THRESHOLD_BIT - RTSTRCACHE_MERGED_THRESHOLD_BIT + 1];
#endif
    /** List of allocated memory chunks. */
    PRTSTRCACHECHUNK        pChunkList;
    /** List of big cache entries. */
    RTLISTANCHOR            BigEntryList;

    /** @name Statistics
     * @{ */
    /** The total size of all chunks. */
    size_t                  cbChunks;
    /** The total length of all the strings, terminators included. */
    size_t                  cbStrings;
    /** The total size of all the big entries. */
    size_t                  cbBigEntries;
    /** Hash collisions. */
    uint32_t                cHashCollisions;
    /** Secondary hash collisions. */
    uint32_t                cHashCollisions2;
    /** The number of inserts to compare cHashCollisions to. */
    uint32_t                cHashInserts;
    /** The number of rehashes. */
    uint32_t                cRehashes;
    /** @} */

    /** Critical section protecting the cache structures. */
    RTCRITSECT              CritSect;
} RTSTRCACHEINT;
/** Pointer to a cache instance. */
typedef RTSTRCACHEINT *PRTSTRCACHEINT;



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The entry sizes of the fixed lists (RTSTRCACHEINT::apFreeLists). */
static const uint32_t g_acbFixedLists[RTSTRCACHE_NUM_OF_FIXED_SIZES] =
{
    16, 32, 48, 64, 96, 128, 192, 256, 320, 384, 448, 512
};

/** Init once for the default string cache. */
static RTONCE       g_rtStrCacheOnce     = RTONCE_INITIALIZER;
/** The default string cache. */
static RTSTRCACHE   g_hrtStrCacheDefault = NIL_RTSTRCACHE;


/** @callback_method_impl{FNRTONCE, Initializes g_hrtStrCacheDefault} */
static DECLCALLBACK(int) rtStrCacheInitDefault(void *pvUser)
{
    NOREF(pvUser);
    return RTStrCacheCreate(&g_hrtStrCacheDefault, "Default");
}


RTDECL(int) RTStrCacheCreate(PRTSTRCACHE phStrCache, const char *pszName)
{
    int            rc    = VERR_NO_MEMORY;
    PRTSTRCACHEINT pThis = (PRTSTRCACHEINT)RTMemAllocZ(sizeof(*pThis));
    if (pThis)
    {
        pThis->cHashTab   = RTSTRCACHE_INITIAL_HASH_SIZE;
        pThis->papHashTab = (PRTSTRCACHEENTRY*)RTMemAllocZ(sizeof(pThis->papHashTab[0]) * pThis->cHashTab);
        if (pThis->papHashTab)
        {
            rc = RTCritSectInit(&pThis->CritSect);
            if (RT_SUCCESS(rc))
            {
                RTListInit(&pThis->BigEntryList);
#ifdef RTSTRCACHE_WITH_MERGED_ALLOCATOR
                for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aMergedFreeLists); i++)
                    RTListInit(&pThis->aMergedFreeLists[i]);
#endif
                pThis->cRefs    = 1;
                pThis->u32Magic = RTSTRCACHE_MAGIC;

                *phStrCache = pThis;
                return VINF_SUCCESS;
            }
            RTMemFree(pThis->papHashTab);
        }
        RTMemFree(pThis);
    }

    RT_NOREF_PV(pszName);
    return rc;
}
RT_EXPORT_SYMBOL(RTStrCacheCreate);


RTDECL(int) RTStrCacheDestroy(RTSTRCACHE hStrCache)
{
    if (   hStrCache == NIL_RTSTRCACHE
        || hStrCache == RTSTRCACHE_DEFAULT)
        return VINF_SUCCESS;

    PRTSTRCACHEINT pThis = hStrCache;
    RTSTRCACHE_VALID_RETURN_RC(pThis, VERR_INVALID_HANDLE);

    /*
     * Invalidate it. Enter the crit sect just to be on the safe side.
     */
    AssertReturn(ASMAtomicCmpXchgU32(&pThis->u32Magic, RTSTRCACHE_MAGIC_DEAD, RTSTRCACHE_MAGIC), VERR_INVALID_HANDLE);
    RTCritSectEnter(&pThis->CritSect);
    Assert(pThis->cRefs == 1);

    PRTSTRCACHECHUNK pChunk;
    while ((pChunk = pThis->pChunkList) != NULL)
    {
        pThis->pChunkList = pChunk->pNext;
        RTMemPageFree(pChunk, pChunk->cb);
    }

    RTMemFree(pThis->papHashTab);
    pThis->papHashTab = NULL;
    pThis->cHashTab   = 0;

    PRTSTRCACHEBIGENTRY pCur, pNext;
    RTListForEachSafe(&pThis->BigEntryList, pCur, pNext, RTSTRCACHEBIGENTRY, ListEntry)
    {
        RTMemFree(pCur);
    }

    RTCritSectLeave(&pThis->CritSect);
    RTCritSectDelete(&pThis->CritSect);

    RTMemFree(pThis);
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTStrCacheDestroy);


/**
 * Selects the fixed free list index for a given minimum entry size.
 *
 * @returns Free list index.
 * @param   cbMin               Minimum entry size.
 */
DECLINLINE(uint32_t) rtStrCacheSelectFixedList(uint32_t cbMin)
{
    Assert(cbMin <= g_acbFixedLists[RT_ELEMENTS(g_acbFixedLists) - 1]);
    unsigned i = 0;
    while (cbMin > g_acbFixedLists[i])
        i++;
    return i;
}


#ifdef RT_STRICT
# define RTSTRCACHE_CHECK(a_pThis)  do { rtStrCacheCheck(pThis); } while (0)
/**
 * Internal cache check.
 */
static void rtStrCacheCheck(PRTSTRCACHEINT pThis)
{
# ifdef RTSTRCACHE_WITH_MERGED_ALLOCATOR
    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aMergedFreeLists); i++)
    {
        PRTSTRCACHEFREEMERGE pFree;
        RTListForEach(&pThis->aMergedFreeLists[i], pFree, RTSTRCACHEFREEMERGE, ListEntry)
        {
            Assert(pFree->uMarker == RTSTRCACHEFREEMERGE_MAIN);
            Assert(pFree->cbFree > 0);
            Assert(RT_ALIGN_32(pFree->cbFree, sizeof(*pFree)) == pFree->cbFree);
        }
    }
# endif
    RT_NOREF_PV(pThis);
}
#else
# define RTSTRCACHE_CHECK(a_pThis)  do { } while (0)
#endif


/**
 * Finds the first empty hash table entry given a hash+length value.
 *
 * ASSUMES that the hash table isn't full.
 *
 * @returns Hash table index.
 * @param   pThis               The string cache instance.
 * @param   uHashLen            The hash + length (not RTSTRCACHEENTRY_BIG_LEN).
 */
static uint32_t rtStrCacheFindEmptyHashTabEntry(PRTSTRCACHEINT pThis, uint32_t uHashLen)
{
    uint32_t iHash = uHashLen % pThis->cHashTab;
    for (;;)
    {
        PRTSTRCACHEENTRY pEntry = pThis->papHashTab[iHash];
        if (pEntry == NULL || pEntry == PRTSTRCACHEENTRY_NIL)
            return iHash;

        /* Advance. */
        iHash += RTSTRCACHE_COLLISION_INCR(uHashLen);
        iHash %= pThis->cHashTab;
    }
}

/**
 * Grows the hash table.
 *
 * @returns vINF_SUCCESS or VERR_NO_MEMORY.
 * @param   pThis               The string cache instance.
 */
static int rtStrCacheGrowHashTab(PRTSTRCACHEINT pThis)
{
    /*
     * Allocate a new hash table two times the size of the old one.
     */
    uint32_t            cNew   = pThis->cHashTab * RTSTRCACHE_HASH_GROW_FACTOR;
    PRTSTRCACHEENTRY   *papNew = (PRTSTRCACHEENTRY  *)RTMemAllocZ(sizeof(papNew[0]) * cNew);
    if (papNew == NULL)
        return VERR_NO_MEMORY;

    /*
     * Install the new table and move the items from the old table and into the new one.
     */
    PRTSTRCACHEENTRY   *papOld = pThis->papHashTab;
    uint32_t            iOld   = pThis->cHashTab;

    pThis->papHashTab = papNew;
    pThis->cHashTab   = cNew;
    pThis->cRehashes++;

    while (iOld-- > 0)
    {
        PRTSTRCACHEENTRY pEntry = papOld[iOld];
        if (pEntry != NULL && pEntry != PRTSTRCACHEENTRY_NIL)
        {
            uint32_t cchString = pEntry->cchString;
            if (cchString == RTSTRCACHEENTRY_BIG_LEN)
                cchString = RT_FROM_MEMBER(pEntry, RTSTRCACHEBIGENTRY, Core)->cchString;

            uint32_t iHash = rtStrCacheFindEmptyHashTabEntry(pThis, RT_MAKE_U32(pEntry->uHash, cchString));
            pThis->papHashTab[iHash] = pEntry;
        }
    }

    /*
     * Free the old hash table.
     */
    RTMemFree(papOld);
    return VINF_SUCCESS;
}

#ifdef RTSTRCACHE_WITH_MERGED_ALLOCATOR

/**
 * Link/Relink into the free right list.
 *
 * @param   pThis               The string cache instance.
 * @param   pFree               The free string entry.
 */
static void rtStrCacheRelinkMerged(PRTSTRCACHEINT pThis, PRTSTRCACHEFREEMERGE pFree)
{
    Assert(pFree->uMarker == RTSTRCACHEFREEMERGE_MAIN);
    Assert(pFree->cbFree > 0);
    Assert(RT_ALIGN_32(pFree->cbFree, sizeof(*pFree)) == pFree->cbFree);

    if (!RTListIsEmpty(&pFree->ListEntry))
        RTListNodeRemove(&pFree->ListEntry);

    uint32_t iList = (ASMBitLastSetU32(pFree->cbFree) - 1) - RTSTRCACHE_MERGED_THRESHOLD_BIT;
    if (iList >= RT_ELEMENTS(pThis->aMergedFreeLists))
        iList = RT_ELEMENTS(pThis->aMergedFreeLists) - 1;

    RTListPrepend(&pThis->aMergedFreeLists[iList], &pFree->ListEntry);
}


/**
 * Allocate a cache entry from the merged free lists.
 *
 * @returns Pointer to the cache entry on success, NULL on allocation error.
 * @param   pThis               The string cache instance.
 * @param   uHash               The full hash of the string.
 * @param   pchString           The string.
 * @param   cchString           The string length.
 * @param   cbEntry             The required entry size.
 */
static PRTSTRCACHEENTRY rtStrCacheAllocMergedEntry(PRTSTRCACHEINT pThis, uint32_t uHash,
                                                   const char *pchString, uint32_t cchString, uint32_t cbEntry)
{
    cbEntry = RT_ALIGN_32(cbEntry, sizeof(RTSTRCACHEFREEMERGE));
    Assert(cbEntry > cchString);

    /*
     * Search the list heads first.
     */
    PRTSTRCACHEFREEMERGE pFree = NULL;

    uint32_t iList = ASMBitLastSetU32(cbEntry) - 1;
    if (!RT_IS_POWER_OF_TWO(cbEntry))
        iList++;
    iList -= RTSTRCACHE_MERGED_THRESHOLD_BIT;

    while (iList < RT_ELEMENTS(pThis->aMergedFreeLists))
    {
        pFree = RTListGetFirst(&pThis->aMergedFreeLists[iList], RTSTRCACHEFREEMERGE, ListEntry);
        if (pFree)
        {
            /*
             * Found something. Should we we split it?  We split from the end
             * to avoid having to update all the sub entries.
             */
            Assert(pFree->uMarker == RTSTRCACHEFREEMERGE_MAIN);
            Assert(pFree->cbFree >= cbEntry);
            Assert(RT_ALIGN_32(pFree->cbFree, sizeof(*pFree)) == pFree->cbFree);

            if (pFree->cbFree == cbEntry)
                RTListNodeRemove(&pFree->ListEntry);
            else
            {
                uint32_t             cRemainder = (pFree->cbFree - cbEntry) / sizeof(*pFree);
                PRTSTRCACHEFREEMERGE pRemainder = pFree;
                pFree += cRemainder;

                Assert((pRemainder->cbFree - cbEntry) == cRemainder * sizeof(*pFree));
                pRemainder->cbFree = cRemainder * sizeof(*pFree);

                rtStrCacheRelinkMerged(pThis, pRemainder);
            }
            break;
        }
        iList++;
    }
    if (!pFree)
    {
        /*
         * Allocate a new block. (We could search the list below in some
         * cases, but it's too much effort to write and execute).
         */
        size_t const     cbChunk = RTSTRCACHE_MERGED_GROW_SIZE; AssertReturn(cbChunk > cbEntry * 2, NULL);
        PRTSTRCACHECHUNK pChunk  = (PRTSTRCACHECHUNK)RTMemPageAlloc(cbChunk);
        if (!pChunk)
            return NULL;
        pChunk->cb    = cbChunk;
        pChunk->pNext = pThis->pChunkList;
        pThis->pChunkList = pChunk;
        pThis->cbChunks  += cbChunk;
        AssertCompile(sizeof(*pChunk) <= sizeof(*pFree));

        /*
         * Get one node for the allocation at hand.
         */
        pFree = (PRTSTRCACHEFREEMERGE)((uintptr_t)pChunk + sizeof(*pFree));

        /*
         * Create a free block out of the remainder (always a reminder).
         */
        PRTSTRCACHEFREEMERGE pNewFree = (PRTSTRCACHEFREEMERGE)((uintptr_t)pFree + cbEntry);
        pNewFree->uMarker = RTSTRCACHEFREEMERGE_MAIN;
        pNewFree->cbFree  = cbChunk - sizeof(*pNewFree) - cbEntry; Assert(pNewFree->cbFree < cbChunk && pNewFree->cbFree > 0);
        pNewFree->pMain   = NULL;
        RTListInit(&pNewFree->ListEntry);

        uint32_t iInternalBlock = pNewFree->cbFree / sizeof(*pNewFree);
        while (iInternalBlock-- > 1)
        {
            pNewFree[iInternalBlock].uMarker = RTSTRCACHEFREEMERGE_PART;
            pNewFree[iInternalBlock].cbFree  = 0;
            pNewFree[iInternalBlock].pMain   = pNewFree;
        }

        rtStrCacheRelinkMerged(pThis, pNewFree);
    }

    /*
     * Initialize the entry.  We zero all bytes we don't use so they cannot
     * accidentally be mistaken for a free entry.
     */
    ASMCompilerBarrier();
    PRTSTRCACHEENTRY pEntry = (PRTSTRCACHEENTRY)pFree;
    pEntry->cRefs       = 1;
    pEntry->uHash       = (uint16_t)uHash;
    pEntry->cchString   = (uint16_t)cchString;
    memcpy(pEntry->szString, pchString, cchString);
    RT_BZERO(&pEntry->szString[cchString], cbEntry - RT_UOFFSETOF(RTSTRCACHEENTRY, szString) - cchString);

    RTSTRCACHE_CHECK(pThis);

    return pEntry;
}

#endif /* RTSTRCACHE_WITH_MERGED_ALLOCATOR */

/**
 * Allocate a cache entry from the heap.
 *
 * @returns Pointer to the cache entry on success, NULL on allocation error.
 * @param   pThis               The string cache instance.
 * @param   uHash               The full hash of the string.
 * @param   pchString           The string.
 * @param   cchString           The string length.
 */
static PRTSTRCACHEENTRY rtStrCacheAllocHeapEntry(PRTSTRCACHEINT pThis, uint32_t uHash,
                                                 const char *pchString, uint32_t cchString)
{
    /*
     * Allocate a heap block for storing the string. We do some size aligning
     * here to encourage the heap to give us optimal alignment.
     */
    size_t              cbEntry   = RT_UOFFSETOF_DYN(RTSTRCACHEBIGENTRY, Core.szString[cchString + 1]);
    PRTSTRCACHEBIGENTRY pBigEntry = (PRTSTRCACHEBIGENTRY)RTMemAlloc(RT_ALIGN_Z(cbEntry, RTSTRCACHE_HEAP_ENTRY_SIZE_ALIGN));
    if (!pBigEntry)
        return NULL;

    /*
     * Initialize the block.
     */
    RTListAppend(&pThis->BigEntryList, &pBigEntry->ListEntry);
    pThis->cbBigEntries        += cbEntry;
    pBigEntry->cchString        = cchString;
    pBigEntry->uHash            = uHash;
    pBigEntry->Core.cRefs       = 1;
    pBigEntry->Core.uHash       = (uint16_t)uHash;
    pBigEntry->Core.cchString   = RTSTRCACHEENTRY_BIG_LEN;
    /* The following is to try avoid gcc warnings/errors regarding array bounds: */
    char *pszDst = (char *)memcpy(pBigEntry->Core.szString, pchString, cchString);
    pszDst[cchString] = '\0';
    ASMCompilerBarrier();

    return &pBigEntry->Core;
}


/**
 * Allocate a cache entry from a fixed size free list.
 *
 * @returns Pointer to the cache entry on success, NULL on allocation error.
 * @param   pThis               The string cache instance.
 * @param   uHash               The full hash of the string.
 * @param   pchString           The string.
 * @param   cchString           The string length.
 * @param   iFreeList           Which free list.
 */
static PRTSTRCACHEENTRY rtStrCacheAllocFixedEntry(PRTSTRCACHEINT pThis, uint32_t uHash,
                                                  const char *pchString, uint32_t cchString, uint32_t iFreeList)
{
    /*
     * Get an entry from the free list. If empty, allocate another chunk of
     * memory and split it up into free entries of the desired size.
     */
    PRTSTRCACHEFREE pFree = pThis->apFreeLists[iFreeList];
    if (!pFree)
    {
        PRTSTRCACHECHUNK pChunk = (PRTSTRCACHECHUNK)RTMemPageAlloc(RTSTRCACHE_FIXED_GROW_SIZE);
        if (!pChunk)
            return NULL;
        pChunk->cb = RTSTRCACHE_FIXED_GROW_SIZE;
        pChunk->pNext = pThis->pChunkList;
        pThis->pChunkList = pChunk;
        pThis->cbChunks  += RTSTRCACHE_FIXED_GROW_SIZE;

        PRTSTRCACHEFREE pPrev   = NULL;
        uint32_t const  cbEntry = g_acbFixedLists[iFreeList];
        uint32_t        cLeft   = RTSTRCACHE_FIXED_GROW_SIZE / cbEntry - 1;
        pFree = (PRTSTRCACHEFREE)((uintptr_t)pChunk + cbEntry);

        Assert(sizeof(*pChunk) <= cbEntry);
        Assert(sizeof(*pFree)  <= cbEntry);
        Assert(cbEntry < RTSTRCACHE_FIXED_GROW_SIZE / 16);

        while (cLeft-- > 0)
        {
            pFree->uZero  = 0;
            pFree->cbFree = cbEntry;
            pFree->pNext  = pPrev;
            pPrev = pFree;
            pFree = (PRTSTRCACHEFREE)((uintptr_t)pFree + cbEntry);
        }

        Assert(pPrev);
        pThis->apFreeLists[iFreeList] = pFree = pPrev;
    }

    /*
     * Unlink it.
     */
    pThis->apFreeLists[iFreeList] = pFree->pNext;
    ASMCompilerBarrier();

    /*
     * Initialize the entry.
     */
    PRTSTRCACHEENTRY pEntry = (PRTSTRCACHEENTRY)pFree;
    pEntry->cRefs     = 1;
    pEntry->uHash     = (uint16_t)uHash;
    pEntry->cchString = (uint16_t)cchString;
    memcpy(pEntry->szString, pchString, cchString);
    pEntry->szString[cchString] = '\0';

    return pEntry;
}


/**
 * Looks up a string in the hash table.
 *
 * @returns Pointer to the string cache entry, NULL + piFreeHashTabEntry if not
 *          found.
 * @param   pThis               The string cache instance.
 * @param   uHashLen            The hash + length (not RTSTRCACHEENTRY_BIG_LEN).
 * @param   cchString           The real length.
 * @param   pchString           The string.
 * @param   piFreeHashTabEntry  Where to store the index insertion index if NULL
 *                              is returned (same as what
 *                              rtStrCacheFindEmptyHashTabEntry would return).
 * @param   pcCollisions        Where to return a collision counter.
 */
static PRTSTRCACHEENTRY rtStrCacheLookUp(PRTSTRCACHEINT pThis, uint32_t uHashLen, uint32_t cchString, const char *pchString,
                                         uint32_t *piFreeHashTabEntry, uint32_t *pcCollisions)
{
    *piFreeHashTabEntry = UINT32_MAX;
    *pcCollisions = 0;

    uint16_t cchStringFirst = RT_UOFFSETOF_DYN(RTSTRCACHEENTRY, szString[cchString + 1]) < RTSTRCACHE_HEAP_THRESHOLD
                            ? (uint16_t)cchString : RTSTRCACHEENTRY_BIG_LEN;
    uint32_t iHash          = uHashLen % pThis->cHashTab;
    for (;;)
    {
        PRTSTRCACHEENTRY pEntry = pThis->papHashTab[iHash];

        /* Give up if NULL, but record the index for insertion. */
        if (pEntry == NULL)
        {
            if (*piFreeHashTabEntry == UINT32_MAX)
                *piFreeHashTabEntry = iHash;
            return NULL;
        }

        if (pEntry != PRTSTRCACHEENTRY_NIL)
        {
            /* Compare. */
            if (   pEntry->uHash     == (uint16_t)uHashLen
                && pEntry->cchString == cchStringFirst)
            {
                if (pEntry->cchString != RTSTRCACHEENTRY_BIG_LEN)
                {
                    if (   !memcmp(pEntry->szString, pchString, cchString)
                        && pEntry->szString[cchString] == '\0')
                        return pEntry;
                }
                else
                {
                    PRTSTRCACHEBIGENTRY pBigEntry = RT_FROM_MEMBER(pEntry, RTSTRCACHEBIGENTRY, Core);
                    if (   pBigEntry->cchString == cchString
                        && !memcmp(pBigEntry->Core.szString, pchString, cchString))
                        return &pBigEntry->Core;
                }
            }

            if (*piFreeHashTabEntry == UINT32_MAX)
                *pcCollisions += 1;
        }
        /* Record the first NIL index for insertion in case we don't get a hit. */
        else if (*piFreeHashTabEntry == UINT32_MAX)
            *piFreeHashTabEntry = iHash;

        /* Advance. */
        iHash += RTSTRCACHE_COLLISION_INCR(uHashLen);
        iHash %= pThis->cHashTab;
    }
}


RTDECL(const char *) RTStrCacheEnterN(RTSTRCACHE hStrCache, const char *pchString, size_t cchString)
{
    PRTSTRCACHEINT pThis = hStrCache;
    RTSTRCACHE_VALID_RETURN_RC(pThis, NULL);


    /*
     * Calculate the hash and figure the exact string length, then look for an existing entry.
     */
    uint32_t const uHash    = sdbmN(pchString, cchString, &cchString);
    uint32_t const uHashLen = RT_MAKE_U32(uHash, cchString);
    AssertReturn(cchString < _1G, NULL);
    uint32_t const cchString32 = (uint32_t)cchString;

    RTCritSectEnter(&pThis->CritSect);
    RTSTRCACHE_CHECK(pThis);

    uint32_t cCollisions;
    uint32_t iFreeHashTabEntry;
    PRTSTRCACHEENTRY pEntry = rtStrCacheLookUp(pThis, uHashLen, cchString32, pchString, &iFreeHashTabEntry, &cCollisions);
    if (pEntry)
    {
        uint32_t cRefs = ASMAtomicIncU32(&pEntry->cRefs);
        Assert(cRefs < UINT32_MAX / 2); NOREF(cRefs);
    }
    else
    {
        /*
         * Allocate a new entry.
         */
        uint32_t cbEntry = cchString32 + 1U + RT_UOFFSETOF(RTSTRCACHEENTRY, szString);
        if (cbEntry >= RTSTRCACHE_HEAP_THRESHOLD)
            pEntry = rtStrCacheAllocHeapEntry(pThis, uHash, pchString, cchString32);
#ifdef RTSTRCACHE_WITH_MERGED_ALLOCATOR
        else if (cbEntry >= RT_BIT_32(RTSTRCACHE_MERGED_THRESHOLD_BIT))
            pEntry = rtStrCacheAllocMergedEntry(pThis, uHash, pchString, cchString32, cbEntry);
#endif
        else
            pEntry = rtStrCacheAllocFixedEntry(pThis, uHash, pchString, cchString32,
                                               rtStrCacheSelectFixedList(cbEntry));
        if (!pEntry)
        {
            RTSTRCACHE_CHECK(pThis);
            RTCritSectLeave(&pThis->CritSect);
            return NULL;
        }

        /*
         * Insert it into the hash table.
         */
        if (pThis->cHashTab - pThis->cStrings < pThis->cHashTab / 2)
        {
            int rc = rtStrCacheGrowHashTab(pThis);
            if (RT_SUCCESS(rc))
                iFreeHashTabEntry = rtStrCacheFindEmptyHashTabEntry(pThis, uHashLen);
            else if (pThis->cHashTab - pThis->cStrings <= pThis->cHashTab / 8) /* 12.5% full => error */
            {
                pThis->papHashTab[iFreeHashTabEntry] = pEntry;
                pThis->cStrings++;
                pThis->cHashInserts++;
                pThis->cHashCollisions += cCollisions > 0;
                pThis->cHashCollisions2 += cCollisions > 1;
                pThis->cbStrings += cchString32 + 1;
                RTStrCacheRelease(hStrCache, pEntry->szString);

                RTSTRCACHE_CHECK(pThis);
                RTCritSectLeave(&pThis->CritSect);
                return NULL;
            }
        }

        pThis->papHashTab[iFreeHashTabEntry] = pEntry;
        pThis->cStrings++;
        pThis->cHashInserts++;
        pThis->cHashCollisions += cCollisions > 0;
        pThis->cHashCollisions2 += cCollisions > 1;
        pThis->cbStrings += cchString32 + 1;
        Assert(pThis->cStrings < pThis->cHashTab && pThis->cStrings > 0);
    }

    RTSTRCACHE_CHECK(pThis);
    RTCritSectLeave(&pThis->CritSect);
    return pEntry->szString;
}
RT_EXPORT_SYMBOL(RTStrCacheEnterN);


RTDECL(const char *) RTStrCacheEnter(RTSTRCACHE hStrCache, const char *psz)
{
    return RTStrCacheEnterN(hStrCache, psz, strlen(psz));
}
RT_EXPORT_SYMBOL(RTStrCacheEnter);


static const char *rtStrCacheEnterLowerWorker(PRTSTRCACHEINT pThis, const char *pchString, size_t cchString)
{
    /*
     * Try use a dynamic heap buffer first.
     */
    if (cchString < 512)
    {
        char *pszStackBuf = (char *)alloca(cchString + 1);
        if (pszStackBuf)
        {
            memcpy(pszStackBuf, pchString, cchString);
            pszStackBuf[cchString] = '\0';
            RTStrToLower(pszStackBuf);
            return RTStrCacheEnterN(pThis, pszStackBuf, cchString);
        }
    }

    /*
     * Fall back on heap.
     */
    char *pszHeapBuf = (char *)RTMemTmpAlloc(cchString + 1);
    if (!pszHeapBuf)
        return NULL;
    memcpy(pszHeapBuf, pchString, cchString);
    pszHeapBuf[cchString] = '\0';
    RTStrToLower(pszHeapBuf);
    const char *pszRet = RTStrCacheEnterN(pThis, pszHeapBuf, cchString);
    RTMemTmpFree(pszHeapBuf);
    return pszRet;
}

RTDECL(const char *) RTStrCacheEnterLowerN(RTSTRCACHE hStrCache, const char *pchString, size_t cchString)
{
    PRTSTRCACHEINT pThis = hStrCache;
    RTSTRCACHE_VALID_RETURN_RC(pThis, NULL);
    return rtStrCacheEnterLowerWorker(pThis, pchString, RTStrNLen(pchString, cchString));
}
RT_EXPORT_SYMBOL(RTStrCacheEnterLowerN);


RTDECL(const char *) RTStrCacheEnterLower(RTSTRCACHE hStrCache, const char *psz)
{
    PRTSTRCACHEINT pThis = hStrCache;
    RTSTRCACHE_VALID_RETURN_RC(pThis, NULL);
    return rtStrCacheEnterLowerWorker(pThis, psz, strlen(psz));
}
RT_EXPORT_SYMBOL(RTStrCacheEnterLower);


RTDECL(uint32_t) RTStrCacheRetain(const char *psz)
{
    AssertPtr(psz);

    PRTSTRCACHEENTRY pStr = RT_FROM_MEMBER(psz, RTSTRCACHEENTRY, szString);
    Assert(!((uintptr_t)pStr & 15) || pStr->cchString == RTSTRCACHEENTRY_BIG_LEN);

    uint32_t cRefs = ASMAtomicIncU32(&pStr->cRefs);
    Assert(cRefs > 1);
    Assert(cRefs < UINT32_MAX / 2);

    return cRefs;
}
RT_EXPORT_SYMBOL(RTStrCacheRetain);


static uint32_t rtStrCacheFreeEntry(PRTSTRCACHEINT pThis, PRTSTRCACHEENTRY pStr)
{
    RTCritSectEnter(&pThis->CritSect);
    RTSTRCACHE_CHECK(pThis);

    /* Remove it from the hash table. */
    uint32_t cchString = pStr->cchString == RTSTRCACHEENTRY_BIG_LEN
                       ? RT_FROM_MEMBER(pStr, RTSTRCACHEBIGENTRY, Core)->cchString
                       : pStr->cchString;
    uint32_t uHashLen  = RT_MAKE_U32(pStr->uHash, cchString);
    uint32_t iHash     = uHashLen % pThis->cHashTab;
    if (pThis->papHashTab[iHash] == pStr)
        pThis->papHashTab[iHash] = PRTSTRCACHEENTRY_NIL;
    else
    {
        do
        {
            AssertBreak(pThis->papHashTab[iHash] != NULL);
            iHash += RTSTRCACHE_COLLISION_INCR(uHashLen);
            iHash %= pThis->cHashTab;
        } while (pThis->papHashTab[iHash] != pStr);
        if (RT_LIKELY(pThis->papHashTab[iHash] == pStr))
            pThis->papHashTab[iHash] = PRTSTRCACHEENTRY_NIL;
        else
        {
            AssertFailed();
            iHash = pThis->cHashTab;
            while (iHash-- > 0)
                if (pThis->papHashTab[iHash] == pStr)
                    break;
            AssertMsgFailed(("iHash=%u cHashTab=%u\n", iHash, pThis->cHashTab));
        }
    }

    pThis->cStrings--;
    pThis->cbStrings -= cchString;
    Assert(pThis->cStrings < pThis->cHashTab);

    /* Free it. */
    if (pStr->cchString != RTSTRCACHEENTRY_BIG_LEN)
    {
        uint32_t const cbMin = pStr->cchString + 1U + RT_UOFFSETOF(RTSTRCACHEENTRY, szString);
#ifdef RTSTRCACHE_WITH_MERGED_ALLOCATOR
        if (cbMin <= RTSTRCACHE_MAX_FIXED)
#endif
        {
            /*
             * No merging, just add it to the list.
             */
            uint32_t const iFreeList = rtStrCacheSelectFixedList(cbMin);
            ASMCompilerBarrier();
            PRTSTRCACHEFREE pFreeStr = (PRTSTRCACHEFREE)pStr;
            pFreeStr->cbFree   = cbMin;
            pFreeStr->uZero    = 0;
            pFreeStr->pNext    = pThis->apFreeLists[iFreeList];
            pThis->apFreeLists[iFreeList] = pFreeStr;
        }
#ifdef RTSTRCACHE_WITH_MERGED_ALLOCATOR
        else
        {
            /*
             * Complicated mode, we merge with adjecent nodes.
             */
            ASMCompilerBarrier();
            PRTSTRCACHEFREEMERGE pFreeStr = (PRTSTRCACHEFREEMERGE)pStr;
            pFreeStr->cbFree   = RT_ALIGN_32(cbMin, sizeof(*pFreeStr));
            pFreeStr->uMarker  = RTSTRCACHEFREEMERGE_MAIN;
            pFreeStr->pMain    = NULL;
            RTListInit(&pFreeStr->ListEntry);

            /*
             * Merge with previous?
             * (Reading one block back is safe because there is always the
             * RTSTRCACHECHUNK structure at the head of each memory chunk.)
             */
            uint32_t             cInternalBlocks = pFreeStr->cbFree / sizeof(*pFreeStr);
            PRTSTRCACHEFREEMERGE pMain = pFreeStr - 1;
            if (   pMain->uMarker == RTSTRCACHEFREEMERGE_MAIN
                || pMain->uMarker == RTSTRCACHEFREEMERGE_PART)
            {
                while (pMain->uMarker != RTSTRCACHEFREEMERGE_MAIN)
                    pMain--;
                pMain->cbFree += pFreeStr->cbFree;
            }
            else
            {
                pMain = pFreeStr;
                pFreeStr++;
                cInternalBlocks--;
            }

            /*
             * Mark internal blocks in the string we're freeing.
             */
            while (cInternalBlocks-- > 0)
            {
                pFreeStr->uMarker = RTSTRCACHEFREEMERGE_PART;
                pFreeStr->cbFree  = 0;
                pFreeStr->pMain   = pMain;
                RTListInit(&pFreeStr->ListEntry);
                pFreeStr++;
            }

            /*
             * Merge with next? Limitation: We won't try cross page boundraries.
             * (pFreeStr points to the next first free enter after the string now.)
             */
            if (   PAGE_ADDRESS(pFreeStr) == PAGE_ADDRESS(&pFreeStr[-1])
                && pFreeStr->uMarker == RTSTRCACHEFREEMERGE_MAIN)
            {
                pMain->cbFree    += pFreeStr->cbFree;
                cInternalBlocks   = pFreeStr->cbFree / sizeof(*pFreeStr);
                Assert(cInternalBlocks > 0);

                /* Update the main block we merge with. */
                pFreeStr->cbFree  = 0;
                pFreeStr->uMarker = RTSTRCACHEFREEMERGE_PART;
                RTListNodeRemove(&pFreeStr->ListEntry);
                RTListInit(&pFreeStr->ListEntry);

                /* Change the internal blocks we merged in. */
                cInternalBlocks--;
                while (cInternalBlocks-- > 0)
                {
                    pFreeStr++;
                    pFreeStr->pMain = pMain;
                    Assert(pFreeStr->uMarker == RTSTRCACHEFREEMERGE_PART);
                    Assert(!pFreeStr->cbFree);
                }
            }

            /*
             * Add/relink into the appropriate free list.
             */
            rtStrCacheRelinkMerged(pThis, pMain);
        }
#endif /* RTSTRCACHE_WITH_MERGED_ALLOCATOR */
        RTSTRCACHE_CHECK(pThis);
        RTCritSectLeave(&pThis->CritSect);
    }
    else
    {
        /* Big string. */
        PRTSTRCACHEBIGENTRY pBigStr = RT_FROM_MEMBER(pStr, RTSTRCACHEBIGENTRY, Core);
        RTListNodeRemove(&pBigStr->ListEntry);
        pThis->cbBigEntries -= RT_ALIGN_32(RT_UOFFSETOF_DYN(RTSTRCACHEBIGENTRY, Core.szString[cchString + 1]),
                                           RTSTRCACHE_HEAP_ENTRY_SIZE_ALIGN);

        RTSTRCACHE_CHECK(pThis);
        RTCritSectLeave(&pThis->CritSect);

        RTMemFree(pBigStr);
    }

    return 0;
}

RTDECL(uint32_t) RTStrCacheRelease(RTSTRCACHE hStrCache, const char *psz)
{
    if (!psz)
        return 0;

    PRTSTRCACHEINT pThis = hStrCache;
    RTSTRCACHE_VALID_RETURN_RC(pThis, UINT32_MAX);

    AssertPtr(psz);
    PRTSTRCACHEENTRY pStr = RT_FROM_MEMBER(psz, RTSTRCACHEENTRY, szString);
    Assert(!((uintptr_t)pStr & 15) || pStr->cchString == RTSTRCACHEENTRY_BIG_LEN);

    /*
     * Drop a reference and maybe free the entry.
     */
    uint32_t cRefs = ASMAtomicDecU32(&pStr->cRefs);
    Assert(cRefs < UINT32_MAX / 2);
    if (!cRefs)
        return rtStrCacheFreeEntry(pThis, pStr);

    return cRefs;
}
RT_EXPORT_SYMBOL(RTStrCacheRelease);


RTDECL(size_t) RTStrCacheLength(const char *psz)
{
    if (!psz)
        return 0;

    AssertPtr(psz);
    PRTSTRCACHEENTRY pStr = RT_FROM_MEMBER(psz, RTSTRCACHEENTRY, szString);
    if (pStr->cchString == RTSTRCACHEENTRY_BIG_LEN)
    {
        PRTSTRCACHEBIGENTRY pBigStr = RT_FROM_MEMBER(psz, RTSTRCACHEBIGENTRY, Core.szString);
        return pBigStr->cchString;
    }
    Assert(!((uintptr_t)pStr & 15));
    return pStr->cchString;
}
RT_EXPORT_SYMBOL(RTStrCacheLength);


RTDECL(bool) RTStrCacheIsRealImpl(void)
{
    return true;
}
RT_EXPORT_SYMBOL(RTStrCacheIsRealImpl);


RTDECL(uint32_t) RTStrCacheGetStats(RTSTRCACHE hStrCache, size_t *pcbStrings, size_t *pcbChunks, size_t *pcbBigEntries,
                                    uint32_t *pcHashCollisions, uint32_t *pcHashCollisions2, uint32_t *pcHashInserts,
                                    uint32_t *pcRehashes)
{
    PRTSTRCACHEINT pThis = hStrCache;
    RTSTRCACHE_VALID_RETURN_RC(pThis, UINT32_MAX);

    RTCritSectEnter(&pThis->CritSect);

    if (pcbStrings)
        *pcbStrings         = pThis->cbStrings;
    if (pcbChunks)
        *pcbChunks          = pThis->cbChunks;
    if (pcbBigEntries)
        *pcbBigEntries      = pThis->cbBigEntries;
    if (pcHashCollisions)
        *pcHashCollisions   = pThis->cHashCollisions;
    if (pcHashCollisions2)
        *pcHashCollisions2  = pThis->cHashCollisions2;
    if (pcHashInserts)
        *pcHashInserts      = pThis->cHashInserts;
    if (pcRehashes)
        *pcRehashes         = pThis->cRehashes;
    uint32_t cStrings       = pThis->cStrings;

    RTCritSectLeave(&pThis->CritSect);
    return cStrings;
}
RT_EXPORT_SYMBOL(RTStrCacheRelease);

