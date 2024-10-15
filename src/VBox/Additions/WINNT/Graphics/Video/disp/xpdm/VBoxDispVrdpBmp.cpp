/* $Id: VBoxDispVrdpBmp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox XPDM Display driver
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#include "VBoxDisp.h"
#include <iprt/crc.h>
#include <VBox/RemoteDesktop/VRDEOrders.h>

/*
 * Cache has a fixed number of preallocated entries. Entries are linked in the MRU lists.
 *
 * A new bitmap hash is added to the "temporary" list, and the caller is told that the
 * bitmap was not cached. If the hash is used again, then it is moved to the "cached" list.
 * This protects against "cache, memblt, cache, memblt, ..." sequences.
 *
 * "Temporary" list contains free and temporary entries. Temporary entries are at the head,
 * free entries are at the tail. New temporary entries are inserted in the head.
 *
 * "Cached" list contains cached entries. When a entry is used, it is moved to the head.
 *
 * The purpose of the cache is to answer whether the bitmap was already encountered
 * before.
 *
 * No serialization because the code is executed under vboxHwBuffer* semaphore.
 */

static uint64_t surfHash (const SURFOBJ *pso, uint32_t cbLine)
{
    uint64_t u64CRC = RTCrc64Start ();

    uint32_t h   = pso->sizlBitmap.cy;
    uint8_t *pu8 = (uint8_t *)pso->pvScan0;

    while (h > 0)
    {
        u64CRC = RTCrc64Process (u64CRC, pu8, cbLine);
        pu8 += pso->lDelta;
        h--;
    }

    u64CRC = RTCrc64Finish (u64CRC);

    return u64CRC;
} /* Hash function end. */


static BOOL bcComputeHash (const SURFOBJ *pso, VRDPBCHASH *phash)
{
    uint32_t cbLine;

    int bytesPerPixel = format2BytesPerPixel (pso);

    if (bytesPerPixel == 0)
    {
        return FALSE;
    }

    phash->cx            = (uint16_t)pso->sizlBitmap.cx;
    phash->cy            = (uint16_t)pso->sizlBitmap.cy;
    phash->bytesPerPixel = bytesPerPixel;

    cbLine               = pso->sizlBitmap.cx * bytesPerPixel;
    phash->hash64        = surfHash (pso, cbLine);

    memset (phash->padding, 0, sizeof (phash->padding));

    return TRUE;
}

static void bcRemoveFromCached(VRDPBC *pCache, VRDPBCENTRY *pEntry)
{
    if (pEntry->prev)
    {
        pEntry->prev->next = pEntry->next;
    }
    else
    {
        pCache->headCached = pEntry->next;
    }

    if (pEntry->next)
    {
        pEntry->next->prev = pEntry->prev;
    }
    else
    {
        pCache->tailCached = pEntry->prev;
    }
}

static void bcRemoveFromTmp(VRDPBC *pCache, VRDPBCENTRY *pEntry)
{
    if (pEntry->prev)
    {
        pEntry->prev->next = pEntry->next;
    }
    else
    {
        pCache->headTmp = pEntry->next;
    }

    if (pEntry->next)
    {
        pEntry->next->prev = pEntry->prev;
    }
    else
    {
        pCache->tailTmp = pEntry->prev;
    }
}

static void bcInsertHeadCached(VRDPBC *pCache, VRDPBCENTRY *pEntry)
{
    pEntry->prev = NULL;
    pEntry->next = pCache->headCached;

    if (pCache->headCached)
    {
        pCache->headCached->prev = pEntry;
    }
    else
    {
        pCache->tailCached = pEntry;
    }

    pCache->headCached = pEntry;
}

static void bcInsertHeadTmp(VRDPBC *pCache, VRDPBCENTRY *pEntry)
{
    pEntry->prev = NULL;
    pEntry->next = pCache->headTmp;

    if (pCache->headTmp)
    {
        pCache->headTmp->prev = pEntry;
    }
    else
    {
        pCache->tailTmp = pEntry;
    }

    pCache->headTmp = pEntry;
}

/* Moves an entry to the head of MRU list. */
static void bcMoveToHeadCached(VRDPBC *pCache, VRDPBCENTRY *pEntry)
{
    if (pEntry->prev)
    {
        /* The entry is not yet in the head. Exclude from list. */
        bcRemoveFromCached(pCache, pEntry);

        /* Insert the entry at the head of MRU list. */
        bcInsertHeadCached(pCache, pEntry);
    }
}

static void bcMoveToHeadTmp(VRDPBC *pCache, VRDPBCENTRY *pEntry)
{
    if (pEntry->prev)
    {
        /* The entry is not yet in the head. Exclude from list. */
        bcRemoveFromTmp(pCache, pEntry);

        /* Insert the entry at the head of MRU list. */
        bcInsertHeadTmp(pCache, pEntry);
    }
}

static void bcMoveTmpToCached(VRDPBC *pCache, VRDPBCENTRY *pEntry)
{
    /* Remove from Tmp list. */
    bcRemoveFromTmp(pCache, pEntry);

    /* Insert the entry at the head of Cached list. */
    bcInsertHeadCached(pCache, pEntry);
}

static void bcMoveCachedToTmp(VRDPBC *pCache, VRDPBCENTRY *pEntry)
{
    /* Remove from cached list. */
    bcRemoveFromCached(pCache, pEntry);

    /* Insert the entry at the head of Tmp list. */
    bcInsertHeadTmp(pCache, pEntry);
}


/* Returns pointer to the entry if the hash already presents in the cache.
 * Moves the found entry to the head of cached MRU list.
 */
static VRDPBCENTRY *bcFindHash (VRDPBC *pCache, const VRDPBCHASH *phash)
{
    /* Search the "Cached" MRU list. */
    VRDPBCENTRY *pEntry = pCache->headCached;

    while (pEntry)
    {
        if (memcmp (&pEntry->hash, phash, sizeof (VRDPBCHASH)) == 0)
        {
            /* Found the entry. Move it to the head of Cached MRU list. */
            bcMoveToHeadCached(pCache, pEntry);

            return pEntry;
        }

        pEntry = pEntry->next;
    }

    /* Search the "Temporary" MRU list. */
    pEntry = pCache->headTmp;

    while (   pEntry
           && pEntry->u32Status != VRDP_BC_ENTRY_STATUS_EMPTY)
    {
        if (memcmp (&pEntry->hash, phash, sizeof (VRDPBCHASH)) == 0)
        {
            /* Found the entry. It will be removed from the list by the caller. */
            return pEntry;
        }

        pEntry = pEntry->next;
    }

    return NULL;
}

/* Returns TRUE is a entry was also deleted to make room for new entry. */
static int bcInsertHash (VRDPBC *pCache, const VRDPBCHASH *phash, VRDPBCHASH *phashDeleted, BOOL bForce)
{
    LOG(("bcInsertHash %p, tmp tail %p, cached tail %p.", pCache, pCache->tailTmp, pCache->tailCached));

    /* Get the free entry to be used. Try Tmp list, then the tail of the Cached list. */
    VRDPBCENTRY *pEntry = pCache->tailTmp;

    if (pEntry != NULL)
    {
        /* Insert to the head of Tmp list. */
        bcMoveToHeadTmp(pCache, pEntry);
        LOG(("bcInsertHash %p, use tmp tail %p.", pCache, pEntry));
    }
    else
    {
        pEntry = pCache->tailCached;
        LOG(("bcInsertHash %p, reuse cached tail %p.", pCache, pEntry, pEntry? pEntry->u32Status: 0));

        if (pEntry != NULL)
        {
            bcMoveCachedToTmp(pCache, pEntry);
        }
    }

    if (!pEntry)
    {
        LOG(("bcInsertHash %p, failed to find an entry!!!", pCache));
        return VRDPBMP_RC_NOT_CACHED;
    }

    BOOL bHashDeleted;
    if (pEntry->u32Status == VRDP_BC_ENTRY_STATUS_CACHED)
    {
        /* The cache is full. Remove the tail hash. */
        memcpy (phashDeleted, &pEntry->hash, sizeof (VRDPBCHASH));
        bHashDeleted = TRUE;
    }
    else
    {
        bHashDeleted = FALSE;
    }

    /* The just inserted entry is at the head of Tmp list, so the temporary
     * entries will be deleted when there is no room in the cache.
     */
    memcpy (&pEntry->hash, phash, sizeof (VRDPBCHASH));

    int rc;
    if (bForce)
    {
        LOG(("Force cache"));
        bcMoveTmpToCached(pCache, pEntry);
        pEntry->u32Status = VRDP_BC_ENTRY_STATUS_CACHED;
        rc = VRDPBMP_RC_CACHED;
    }
    else
    {
        pEntry->u32Status = VRDP_BC_ENTRY_STATUS_TEMPORARY;
        rc = VRDPBMP_RC_NOT_CACHED;
    }

    if (bHashDeleted)
    {
        rc |= VRDPBMP_RC_F_DELETED;
    }

    return rc;
}

/* Find out whether the surface already in the cache.
 * Insert in the cache if not.
 * Protection against "cache, memblt, cache, memblt, ..." sequence:
 *    first time just append the bitmap hash and mark it as "temporary";
 *    if the hash is used again, mark as cached and tell the caller to cache the bitmap;
 *    remove "temporary" entries before any other.
 *
 */
int vrdpbmpCacheSurface(VRDPBC *pCache, const SURFOBJ *pso, VRDPBCHASH *phash, VRDPBCHASH *phashDeleted, BOOL bForce)
{
    VRDPBCHASH hash;

    BOOL bResult = bcComputeHash (pso, &hash);
    LOG(("vrdpbmpCacheSurface: compute hash %d.", bResult));

    if (!bResult)
    {
        WARN(("MEMBLT: vrdpbmpCacheSurface: could not compute hash."));
        return VRDPBMP_RC_NOT_CACHED;
    }

    *phash = hash;

    VRDPBCENTRY *pEntry = bcFindHash (pCache, &hash);
    LOG(("vrdpbmpCacheSurface: find hash %d.", pEntry? pEntry->u32Status: 0));

    if (pEntry)
    {
        if (pEntry->u32Status == VRDP_BC_ENTRY_STATUS_CACHED)
        {
            return VRDPBMP_RC_ALREADY_CACHED;
        }

        /* The status must be VRDP_BC_ENTRY_STATUS_TEMPORARY here.
         * Update it to *_CACHED.
         */
        if (pEntry->u32Status != VRDP_BC_ENTRY_STATUS_TEMPORARY)
        {
            LOG(("MEMBLT: vrdpbmpCacheSurface: unexpected status %d.", pEntry->u32Status));
            return VRDPBMP_RC_NOT_CACHED;
        }

        bcMoveTmpToCached(pCache, pEntry);

        pEntry->u32Status = VRDP_BC_ENTRY_STATUS_CACHED;
        return VRDPBMP_RC_CACHED;
    }

    int rc = bcInsertHash (pCache, &hash, phashDeleted, bForce);
    LOG(("vrdpbmpCacheSurface: insert hash %x.", rc));

    return rc;
}

/* Setup the initial state of the cache. */
void vrdpbmpReset(VRDPBC *pCache)
{
    int i;

    Assert(sizeof (VRDPBCHASH) == sizeof (VRDEBITMAPHASH));

    LOG(("vrdpbmpReset: %p.", pCache));

    /* Reinitialize the cache structure. */
    memset(pCache, 0, sizeof (VRDPBC));

    pCache->headTmp = &pCache->aEntries[0];
    pCache->tailTmp = &pCache->aEntries[RT_ELEMENTS(pCache->aEntries) - 1];

    for (i = 0; i < RT_ELEMENTS(pCache->aEntries); i++)
    {
        VRDPBCENTRY *pEntry = &pCache->aEntries[i];

        if (pEntry != pCache->tailTmp)
        {
            pEntry->next = &pCache->aEntries[i + 1];
        }

        if (pEntry != pCache->headTmp)
        {
            pEntry->prev = &pCache->aEntries[i - 1];
        }
    }

    pCache->headCached = NULL;
    pCache->tailCached = NULL;
}
