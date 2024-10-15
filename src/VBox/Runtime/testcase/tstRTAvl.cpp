/* $Id: tstRTAvl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - AVL trees.
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
#include <iprt/avl.h>
#include <iprt/cpp/hardavlrange.h>

#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/rand.h>
#include <iprt/stdarg.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/time.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct TRACKER
{
    /** The max key value (exclusive). */
    uint32_t    MaxKey;
    /** The last allocated key. */
    uint32_t    LastAllocatedKey;
    /** The number of set bits in the bitmap. */
    uint32_t    cSetBits;
    /** The bitmap size. */
    uint32_t    cbBitmap;
    /** Bitmap containing the allocated nodes. */
    uint8_t     abBitmap[1];
} TRACKER, *PTRACKER;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST g_hTest;
static RTRAND g_hRand;


/**
 * Creates a new tracker.
 *
 * @returns Pointer to the new tracker.
 * @param   MaxKey      The max key value for the tracker. (exclusive)
 */
static PTRACKER TrackerCreate(uint32_t MaxKey)
{
    uint32_t cbBitmap = RT_ALIGN_32(MaxKey, 64) / 8;
    PTRACKER pTracker = (PTRACKER)RTMemAllocZ(RT_UOFFSETOF_DYN(TRACKER, abBitmap[cbBitmap]));
    if (pTracker)
    {
        pTracker->MaxKey = MaxKey;
        pTracker->LastAllocatedKey = MaxKey;
        pTracker->cbBitmap = cbBitmap;
        Assert(pTracker->cSetBits == 0);
    }
    return pTracker;
}


/**
 * Destroys a tracker.
 *
 * @param   pTracker        The tracker.
 */
static void TrackerDestroy(PTRACKER pTracker)
{
    RTMemFree(pTracker);
}


/**
 * Inserts a key range into the tracker.
 *
 * @returns success indicator.
 * @param   pTracker    The tracker.
 * @param   Key         The first key in the range.
 * @param   KeyLast     The last key in the range. (inclusive)
 */
static bool TrackerInsert(PTRACKER pTracker, uint32_t Key, uint32_t KeyLast)
{
    bool fRc = !ASMBitTestAndSet(pTracker->abBitmap, Key);
    if (fRc)
        pTracker->cSetBits++;
    while (KeyLast != Key)
    {
        if (!ASMBitTestAndSet(pTracker->abBitmap, KeyLast))
            pTracker->cSetBits++;
        else
            fRc = false;
        KeyLast--;
    }
    return fRc;
}


/**
 * Removes a key range from the tracker.
 *
 * @returns success indicator.
 * @param   pTracker    The tracker.
 * @param   Key         The first key in the range.
 * @param   KeyLast     The last key in the range. (inclusive)
 */
static bool TrackerRemove(PTRACKER pTracker, uint32_t Key, uint32_t KeyLast)
{
    bool fRc = ASMBitTestAndClear(pTracker->abBitmap, Key);
    if (fRc)
        pTracker->cSetBits--;
    while (KeyLast != Key)
    {
        if (ASMBitTestAndClear(pTracker->abBitmap, KeyLast))
            pTracker->cSetBits--;
        else
            fRc = false;
        KeyLast--;
    }
    return fRc;
}


/**
 * Random key range allocation.
 *
 * @returns success indicator.
 * @param   pTracker    The tracker.
 * @param   pKey        Where to store the first key in the allocated range.
 * @param   pKeyLast    Where to store the first key in the allocated range.
 * @param   cMaxKey     The max range length.
 * @remark  The caller has to call TrackerInsert.
 */
static bool TrackerNewRandomEx(PTRACKER pTracker, uint32_t *pKey, uint32_t *pKeyLast, uint32_t cMaxKeys)
{
    /*
     * Find a key.
     */
    uint32_t Key = RTRandAdvU32Ex(g_hRand, 0, pTracker->MaxKey - 1);
    if (ASMBitTest(pTracker->abBitmap, Key))
    {
        if (pTracker->cSetBits >= pTracker->MaxKey)
            return false;

        int Key2 = ASMBitNextClear(pTracker->abBitmap, pTracker->MaxKey, Key);
        if (Key2 > 0)
            Key = Key2;
        else
        {
            /* we're missing a ASMBitPrevClear function, so just try another, lower, value.*/
            for (;;)
            {
                const uint32_t KeyPrev = Key;
                Key = RTRandAdvU32Ex(g_hRand, 0, KeyPrev - 1);
                if (!ASMBitTest(pTracker->abBitmap, Key))
                    break;
                Key2 = ASMBitNextClear(pTracker->abBitmap, RT_ALIGN_32(KeyPrev, 32), Key);
                if (Key2 > 0)
                {
                    Key = Key2;
                    break;
                }
            }
        }
    }

    /*
     * Determine the range.
     */
    uint32_t KeyLast;
    if (cMaxKeys == 1 || !pKeyLast)
        KeyLast = Key;
    else
    {
        uint32_t cKeys = RTRandAdvU32Ex(g_hRand, 0, RT_MIN(pTracker->MaxKey - Key, cMaxKeys - 1));
        KeyLast = Key + cKeys;
        int Key2 = ASMBitNextSet(pTracker->abBitmap, RT_ALIGN_32(KeyLast, 32), Key);
        if (    Key2 > 0
            &&  (unsigned)Key2 <= KeyLast)
            KeyLast = Key2 - 1;
    }

    /*
     * Return.
     */
    *pKey = Key;
    if (pKeyLast)
        *pKeyLast = KeyLast;
    return true;
}


/**
 * Random single key allocation.
 *
 * @returns success indicator.
 * @param   pTracker    The tracker.
 * @param   pKey        Where to store the allocated key.
 * @remark  The caller has to call TrackerInsert.
 */
static bool TrackerNewRandom(PTRACKER pTracker, uint32_t *pKey)
{
    return TrackerNewRandomEx(pTracker, pKey, NULL, 1);
}


/**
 * Random single key 'lookup'.
 *
 * @returns success indicator.
 * @param   pTracker    The tracker.
 * @param   pKey        Where to store the allocated key.
 * @remark  The caller has to call TrackerRemove.
 */
static bool TrackerFindRandom(PTRACKER pTracker, uint32_t *pKey)
{
    uint32_t Key = RTRandAdvU32Ex(g_hRand, 0, pTracker->MaxKey - 1);
    if (!ASMBitTest(pTracker->abBitmap, Key))
    {
        if (!pTracker->cSetBits)
            return false;

        int Key2 = ASMBitNextSet(pTracker->abBitmap, pTracker->MaxKey, Key);
        if (Key2 > 0)
            Key = Key2;
        else
        {
            /* we're missing a ASMBitPrevSet function, so here's a quick replacement hack. */
            uint32_t const *pu32Bitmap = (uint32_t const *)&pTracker->abBitmap[0];
            Key >>= 5;
            do
            {
                uint32_t u32;
                if ((u32 = pu32Bitmap[Key]) != 0)
                {
                    *pKey = ASMBitLastSetU32(u32) - 1 + (Key << 5);
                    return true;
                }
            } while (Key-- > 0);

            Key2 = ASMBitFirstSet(pTracker->abBitmap, pTracker->MaxKey);
            if (Key2 == -1)
            {
                RTTestIFailed("cSetBits=%u - but ASMBitFirstSet failed to find any", pTracker->cSetBits);
                return false;
            }
            Key = Key2;
        }
    }

    *pKey = Key;
    return true;
}


/**
 * Gets the number of keys in the tree.
 */
static uint32_t TrackerGetCount(PTRACKER pTracker)
{
    return pTracker->cSetBits;
}


/*
bool TrackerAllocSeq(PTRACKER pTracker, uint32_t *pKey, uint32_t *pKeyLast, uint32_t cMaxKeys)
{
    return false;
}*/


/**
 * Prints an unbuffered char.
 * @param   ch  The char.
 */
static void ProgressChar(char ch)
{
    //RTTestIPrintf(RTTESTLVL_INFO, "%c", ch);
    RTTestIPrintf(RTTESTLVL_SUB_TEST, "%c", ch);
}

/**
 * Prints a progress indicator label.
 * @param   cMax        The max number of operations (exclusive).
 * @param   pszFormat   The format string.
 * @param   ...         The arguments to the format string.
 */
DECLINLINE(void) ProgressPrintf(unsigned cMax, const char *pszFormat, ...)
{
    if (cMax < 10000)
        return;

    va_list va;
    va_start(va, pszFormat);
    //RTTestIPrintfV(RTTESTLVL_INFO, pszFormat, va);
    RTTestIPrintfV(RTTESTLVL_SUB_TEST, pszFormat, va);
    va_end(va);
}


/**
 * Prints a progress indicator dot.
 * @param   iCur    The current operation. (can be descending too)
 * @param   cMax    The max number of operations (exclusive).
 */
DECLINLINE(void) Progress(unsigned iCur, unsigned cMax)
{
    if (cMax < 10000)
        return;
    if (!(iCur % (cMax / 20)))
        ProgressChar('.');
}


static int avlogcphys(unsigned cMax)
{
    /*
     * Simple linear insert and remove.
     */
    if (cMax >= 10000)
        RTTestISubF("oGCPhys(%d): linear left", cMax);
    PAVLOGCPHYSTREE pTree = (PAVLOGCPHYSTREE)RTMemAllocZ(sizeof(*pTree));
    unsigned i;
    for (i = 0; i < cMax; i++)
    {
        Progress(i, cMax);
        PAVLOGCPHYSNODECORE pNode = (PAVLOGCPHYSNODECORE)RTMemAlloc(sizeof(*pNode));
        pNode->Key = i;
        if (!RTAvloGCPhysInsert(pTree, pNode))
        {
            RTTestIFailed("linear left insert i=%d\n", i);
            return 1;
        }
        /* negative. */
        AVLOGCPHYSNODECORE Node = *pNode;
        if (RTAvloGCPhysInsert(pTree, &Node))
        {
            RTTestIFailed("linear left negative insert i=%d\n", i);
            return 1;
        }
    }

    ProgressPrintf(cMax, "~");
    for (i = 0; i < cMax; i++)
    {
        Progress(i, cMax);
        PAVLOGCPHYSNODECORE pNode = RTAvloGCPhysRemove(pTree, i);
        if (!pNode)
        {
            RTTestIFailed("linear left remove i=%d\n", i);
            return 1;
        }
        memset(pNode, 0xcc, sizeof(*pNode));
        RTMemFree(pNode);

        /* negative */
        pNode = RTAvloGCPhysRemove(pTree, i);
        if (pNode)
        {
            RTTestIFailed("linear left negative remove i=%d\n", i);
            return 1;
        }
    }

    /*
     * Simple linear insert and remove from the right.
     */
    if (cMax >= 10000)
        RTTestISubF("oGCPhys(%d): linear right", cMax);
    for (i = 0; i < cMax; i++)
    {
        Progress(i, cMax);
        PAVLOGCPHYSNODECORE pNode = (PAVLOGCPHYSNODECORE)RTMemAlloc(sizeof(*pNode));
        pNode->Key = i;
        if (!RTAvloGCPhysInsert(pTree, pNode))
        {
            RTTestIFailed("linear right insert i=%d\n", i);
            return 1;
        }
        /* negative. */
        AVLOGCPHYSNODECORE Node = *pNode;
        if (RTAvloGCPhysInsert(pTree, &Node))
        {
            RTTestIFailed("linear right negative insert i=%d\n", i);
            return 1;
        }
    }

    ProgressPrintf(cMax, "~");
    while (i-- > 0)
    {
        Progress(i, cMax);
        PAVLOGCPHYSNODECORE pNode = RTAvloGCPhysRemove(pTree, i);
        if (!pNode)
        {
            RTTestIFailed("linear right remove i=%d\n", i);
            return 1;
        }
        memset(pNode, 0xcc, sizeof(*pNode));
        RTMemFree(pNode);

        /* negative */
        pNode = RTAvloGCPhysRemove(pTree, i);
        if (pNode)
        {
            RTTestIFailed("linear right negative remove i=%d\n", i);
            return 1;
        }
    }

    /*
     * Linear insert but root based removal.
     */
    if (cMax >= 10000)
        RTTestISubF("oGCPhys(%d): linear root", cMax);
    for (i = 0; i < cMax; i++)
    {
        Progress(i, cMax);
        PAVLOGCPHYSNODECORE pNode = (PAVLOGCPHYSNODECORE)RTMemAlloc(sizeof(*pNode));
        pNode->Key = i;
        if (!RTAvloGCPhysInsert(pTree, pNode))
        {
            RTTestIFailed("linear root insert i=%d\n", i);
            return 1;
        }
        /* negative. */
        AVLOGCPHYSNODECORE Node = *pNode;
        if (RTAvloGCPhysInsert(pTree, &Node))
        {
            RTTestIFailed("linear root negative insert i=%d\n", i);
            return 1;
        }
    }

    ProgressPrintf(cMax, "~");
    while (i-- > 0)
    {
        Progress(i, cMax);
        PAVLOGCPHYSNODECORE pNode = (PAVLOGCPHYSNODECORE)((intptr_t)pTree + *pTree);
        RTGCPHYS Key = pNode->Key;
        pNode = RTAvloGCPhysRemove(pTree, Key);
        if (!pNode)
        {
            RTTestIFailed("linear root remove i=%d Key=%d\n", i, (unsigned)Key);
            return 1;
        }
        memset(pNode, 0xcc, sizeof(*pNode));
        RTMemFree(pNode);

        /* negative */
        pNode = RTAvloGCPhysRemove(pTree, Key);
        if (pNode)
        {
            RTTestIFailed("linear root negative remove i=%d Key=%d\n", i, (unsigned)Key);
            return 1;
        }
    }
    if (*pTree)
    {
        RTTestIFailed("sparse remove didn't remove it all!\n");
        return 1;
    }

    /*
     * Make a sparsely populated tree and remove the nodes using best fit in 5 cycles.
     */
    const unsigned cMaxSparse = RT_ALIGN(cMax, 32);
    if (cMaxSparse >= 10000)
        RTTestISubF("oGCPhys(%d): sparse", cMax);
    for (i = 0; i < cMaxSparse; i += 8)
    {
        Progress(i, cMaxSparse);
        PAVLOGCPHYSNODECORE pNode = (PAVLOGCPHYSNODECORE)RTMemAlloc(sizeof(*pNode));
        pNode->Key = i;
        if (!RTAvloGCPhysInsert(pTree, pNode))
        {
            RTTestIFailed("sparse insert i=%d\n", i);
            return 1;
        }
        /* negative. */
        AVLOGCPHYSNODECORE Node = *pNode;
        if (RTAvloGCPhysInsert(pTree, &Node))
        {
            RTTestIFailed("sparse negative insert i=%d\n", i);
            return 1;
        }
    }

    /* Remove using best fit in 5 cycles. */
    ProgressPrintf(cMaxSparse, "~");
    unsigned j;
    for (j = 0; j < 4; j++)
    {
        for (i = 0; i < cMaxSparse; i += 8 * 4)
        {
            Progress(i, cMax); // good enough
            PAVLOGCPHYSNODECORE pNode = RTAvloGCPhysRemoveBestFit(pTree, i, true);
            if (!pNode)
            {
                RTTestIFailed("sparse remove i=%d j=%d\n", i, j);
                return 1;
            }
            if (pNode->Key - (unsigned long)i >= 8 * 4)
            {
                RTTestIFailed("sparse remove i=%d j=%d Key=%d\n", i, j, (unsigned)pNode->Key);
                return 1;
            }
            memset(pNode, 0xdd, sizeof(*pNode));
            RTMemFree(pNode);
        }
    }
    if (*pTree)
    {
        RTTestIFailed("sparse remove didn't remove it all!\n");
        return 1;
    }
    RTMemFree(pTree);
    ProgressPrintf(cMaxSparse, "\n");
    return 0;
}


static DECLCALLBACK(int) avlogcphysCallbackCounter(PAVLOGCPHYSNODECORE pNode, void *pvUser)
{
    RT_NOREF(pNode);
    *(uint32_t *)pvUser += 1;
    return 0;
}

static int avlogcphysRand(unsigned cMax, unsigned cMax2, uint32_t fCountMask)
{
    PAVLOGCPHYSTREE pTree = (PAVLOGCPHYSTREE)RTMemAllocZ(sizeof(*pTree));
    unsigned i;

    /*
     * Random tree.
     */
    if (cMax >= 10000)
        RTTestISubF("oGCPhys(%d, %d): random", cMax, cMax2);
    PTRACKER pTracker = TrackerCreate(cMax2);
    if (!pTracker)
    {
        RTTestIFailed("failed to create %d tracker!\n", cMax2);
        return 1;
    }

    /* Insert a number of nodes in random order. */
    for (i = 0; i < cMax; i++)
    {
        Progress(i, cMax);
        uint32_t Key;
        if (!TrackerNewRandom(pTracker, &Key))
        {
            RTTestIFailed("failed to allocate node no. %d\n", i);
            TrackerDestroy(pTracker);
            return 1;
        }
        PAVLOGCPHYSNODECORE pNode = (PAVLOGCPHYSNODECORE)RTMemAlloc(sizeof(*pNode));
        pNode->Key = Key;
        if (!RTAvloGCPhysInsert(pTree, pNode))
        {
            RTTestIFailed("random insert i=%d Key=%#x\n", i, Key);
            return 1;
        }
        /* negative. */
        AVLOGCPHYSNODECORE Node = *pNode;
        if (RTAvloGCPhysInsert(pTree, &Node))
        {
            RTTestIFailed("linear negative insert i=%d Key=%#x\n", i, Key);
            return 1;
        }
        TrackerInsert(pTracker, Key, Key);

        if (!(i & fCountMask))
        {
            uint32_t cCount = 0;
            RTAvloGCPhysDoWithAll(pTree, i & 1, avlogcphysCallbackCounter, &cCount);
            if (cCount != TrackerGetCount(pTracker))
                RTTestIFailed("wrong tree count after random insert i=%d: %u, expected %u", i, cCount, TrackerGetCount(pTracker));
        }
    }

    {
        uint32_t cCount = 0;
        RTAvloGCPhysDoWithAll(pTree, i & 1, avlogcphysCallbackCounter, &cCount);
        if (cCount != TrackerGetCount(pTracker))
            RTTestIFailed("wrong tree count after random insert i=%d: %u, expected %u", i, cCount, TrackerGetCount(pTracker));
    }


    /* delete the nodes in random order. */
    ProgressPrintf(cMax, "~");
    while (i-- > 0)
    {
        Progress(i, cMax);
        uint32_t Key;
        if (!TrackerFindRandom(pTracker, &Key))
        {
            RTTestIFailed("failed to find free node no. %d\n", i);
            TrackerDestroy(pTracker);
            return 1;
        }

        PAVLOGCPHYSNODECORE pNode = RTAvloGCPhysRemove(pTree, Key);
        if (!pNode)
        {
            RTTestIFailed("random remove i=%d Key=%#x\n", i, Key);
            return 1;
        }
        if (pNode->Key != Key)
        {
            RTTestIFailed("random remove i=%d Key=%#x pNode->Key=%#x\n", i, Key, (unsigned)pNode->Key);
            return 1;
        }
        TrackerRemove(pTracker, Key, Key);
        memset(pNode, 0xdd, sizeof(*pNode));
        RTMemFree(pNode);

        if (!(i & fCountMask))
        {
            uint32_t cCount = 0;
            RTAvloGCPhysDoWithAll(pTree, i & 1, avlogcphysCallbackCounter, &cCount);
            if (cCount != TrackerGetCount(pTracker))
                RTTestIFailed("wrong tree count after random remove i=%d: %u, expected %u", i, cCount, TrackerGetCount(pTracker));
        }
    }
    {
        uint32_t cCount = 0;
        RTAvloGCPhysDoWithAll(pTree, i & 1, avlogcphysCallbackCounter, &cCount);
        if (cCount != TrackerGetCount(pTracker))
            RTTestIFailed("wrong tree count after random insert i=%d: %u, expected %u", i, cCount, TrackerGetCount(pTracker));
    }
    if (*pTree)
    {
        RTTestIFailed("random remove didn't remove it all!\n");
        return 1;
    }
    ProgressPrintf(cMax, "\n");
    TrackerDestroy(pTracker);
    RTMemFree(pTree);
    return 0;
}



static int avlrogcphys(void)
{
    unsigned            i;
    unsigned            j;
    unsigned            k;
    PAVLROGCPHYSTREE    pTree = (PAVLROGCPHYSTREE)RTMemAllocZ(sizeof(*pTree));

    AssertCompileSize(AVLOGCPHYSNODECORE, 24);
    AssertCompileSize(AVLROGCPHYSNODECORE, 32);

    RTTestISubF("RTAvlroGCPhys");

    /*
     * Simple linear insert, get and remove.
     */
    /* insert */
    for (i = 0; i < 65536; i += 4)
    {
        PAVLROGCPHYSNODECORE pNode = (PAVLROGCPHYSNODECORE)RTMemAlloc(sizeof(*pNode));
        pNode->Key = i;
        pNode->KeyLast = i + 3;
        if (!RTAvlroGCPhysInsert(pTree, pNode))
        {
            RTTestIFailed("linear insert i=%d\n", (unsigned)i);
            return 1;
        }

        /* negative. */
        AVLROGCPHYSNODECORE Node = *pNode;
        for (j = i + 3; j > i - 32; j--)
        {
            for (k = i; k < i + 32; k++)
            {
                Node.Key = RT_MIN(j, k);
                Node.KeyLast = RT_MAX(k, j);
                if (RTAvlroGCPhysInsert(pTree, &Node))
                {
                    RTTestIFailed("linear negative insert i=%d j=%d k=%d\n", i, j, k);
                    return 1;
                }
            }
        }
    }

    /* do gets. */
    for (i = 0; i < 65536; i += 4)
    {
        PAVLROGCPHYSNODECORE pNode = RTAvlroGCPhysGet(pTree, i);
        if (!pNode)
        {
            RTTestIFailed("linear get i=%d\n", i);
            return 1;
        }
        if (pNode->Key > i || pNode->KeyLast < i)
        {
            RTTestIFailed("linear get i=%d Key=%d KeyLast=%d\n", i, (unsigned)pNode->Key, (unsigned)pNode->KeyLast);
            return 1;
        }

        for (j = 0; j < 4; j++)
        {
            if (RTAvlroGCPhysRangeGet(pTree, i + j) != pNode)
            {
                RTTestIFailed("linear range get i=%d j=%d\n", i, j);
                return 1;
            }
        }

        /* negative. */
        if (    RTAvlroGCPhysGet(pTree, i + 1)
            ||  RTAvlroGCPhysGet(pTree, i + 2)
            ||  RTAvlroGCPhysGet(pTree, i + 3))
        {
            RTTestIFailed("linear negative get i=%d + n\n", i);
            return 1;
        }

    }

    /* remove */
    for (i = 0; i < 65536; i += 4)
    {
        PAVLROGCPHYSNODECORE pNode = RTAvlroGCPhysRemove(pTree, i);
        if (!pNode)
        {
            RTTestIFailed("linear remove i=%d\n", i);
            return 1;
        }
        memset(pNode, 0xcc, sizeof(*pNode));
        RTMemFree(pNode);

        /* negative */
        if (    RTAvlroGCPhysRemove(pTree, i)
            ||  RTAvlroGCPhysRemove(pTree, i + 1)
            ||  RTAvlroGCPhysRemove(pTree, i + 2)
            ||  RTAvlroGCPhysRemove(pTree, i + 3))
        {
            RTTestIFailed("linear negative remove i=%d + n\n", i);
            return 1;
        }
    }

    /*
     * Make a sparsely populated tree.
     */
    for (i = 0; i < 65536; i += 8)
    {
        PAVLROGCPHYSNODECORE pNode = (PAVLROGCPHYSNODECORE)RTMemAlloc(sizeof(*pNode));
        pNode->Key = i;
        pNode->KeyLast = i + 3;
        if (!RTAvlroGCPhysInsert(pTree, pNode))
        {
            RTTestIFailed("sparse insert i=%d\n", i);
            return 1;
        }
        /* negative. */
        AVLROGCPHYSNODECORE Node = *pNode;
        const RTGCPHYS jMin = i > 32 ? i - 32 : 1;
        const RTGCPHYS kMax = i + 32;
        for (j = pNode->KeyLast; j >= jMin; j--)
        {
            for (k = pNode->Key; k < kMax; k++)
            {
                Node.Key = RT_MIN(j, k);
                Node.KeyLast = RT_MAX(k, j);
                if (RTAvlroGCPhysInsert(pTree, &Node))
                {
                    RTTestIFailed("sparse negative insert i=%d j=%d k=%d\n", i, j, k);
                    return 1;
                }
            }
        }
    }

    /*
     * Get and Remove using range matching in 5 cycles.
     */
    for (j = 0; j < 4; j++)
    {
        for (i = 0; i < 65536; i += 8 * 4)
        {
            /* gets */
            RTGCPHYS  KeyBase = i + j * 8;
            PAVLROGCPHYSNODECORE pNode = RTAvlroGCPhysGet(pTree, KeyBase);
            if (!pNode)
            {
                RTTestIFailed("sparse get i=%d j=%d KeyBase=%d\n", i, j, (unsigned)KeyBase);
                return 1;
            }
            if (pNode->Key > KeyBase || pNode->KeyLast < KeyBase)
            {
                RTTestIFailed("sparse get i=%d j=%d KeyBase=%d pNode->Key=%d\n", i, j, (unsigned)KeyBase, (unsigned)pNode->Key);
                return 1;
            }
            for (k = KeyBase; k < KeyBase + 4; k++)
            {
                if (RTAvlroGCPhysRangeGet(pTree, k) != pNode)
                {
                    RTTestIFailed("sparse range get i=%d j=%d k=%d\n", i, j, k);
                    return 1;
                }
            }

            /* negative gets */
            for (k = i + j; k < KeyBase + 8; k++)
            {
                if (    k != KeyBase
                    &&  RTAvlroGCPhysGet(pTree, k))
                {
                    RTTestIFailed("sparse negative get i=%d j=%d k=%d\n", i, j, k);
                    return 1;
                }
            }
            for (k = i + j; k < KeyBase; k++)
            {
                if (RTAvlroGCPhysRangeGet(pTree, k))
                {
                    RTTestIFailed("sparse negative range get i=%d j=%d k=%d\n", i, j, k);
                    return 1;
                }
            }
            for (k = KeyBase + 4; k < KeyBase + 8; k++)
            {
                if (RTAvlroGCPhysRangeGet(pTree, k))
                {
                    RTTestIFailed("sparse negative range get i=%d j=%d k=%d\n", i, j, k);
                    return 1;
                }
            }

            /* remove */
            RTGCPHYS Key = KeyBase + ((i / 19) % 4);
            if (RTAvlroGCPhysRangeRemove(pTree, Key) != pNode)
            {
                RTTestIFailed("sparse remove i=%d j=%d Key=%d\n", i, j, (unsigned)Key);
                return 1;
            }
            memset(pNode, 0xdd, sizeof(*pNode));
            RTMemFree(pNode);
        }
    }
    if (*pTree)
    {
        RTTestIFailed("sparse remove didn't remove it all!\n");
        return 1;
    }


    /*
     * Realworld testcase.
     */
    struct
    {
        AVLROGCPHYSTREE     Tree;
        AVLROGCPHYSNODECORE aNode[4];
    }   s1, s2, s3;
    RT_ZERO(s1);
    RT_ZERO(s2);
    RT_ZERO(s3);

    s1.aNode[0].Key        = 0x00030000;
    s1.aNode[0].KeyLast    = 0x00030fff;
    s1.aNode[1].Key        = 0x000a0000;
    s1.aNode[1].KeyLast    = 0x000bffff;
    s1.aNode[2].Key        = 0xe0000000;
    s1.aNode[2].KeyLast    = 0xe03fffff;
    s1.aNode[3].Key        = 0xfffe0000;
    s1.aNode[3].KeyLast    = 0xfffe0ffe;
    for (i = 0; i < RT_ELEMENTS(s1.aNode); i++)
    {
        PAVLROGCPHYSNODECORE pNode = &s1.aNode[i];
        if (!RTAvlroGCPhysInsert(&s1.Tree, pNode))
        {
            RTTestIFailed("real insert i=%d\n", i);
            return 1;
        }
        if (RTAvlroGCPhysInsert(&s1.Tree, pNode))
        {
            RTTestIFailed("real negative insert i=%d\n", i);
            return 1;
        }
        if (RTAvlroGCPhysGet(&s1.Tree, pNode->Key) != pNode)
        {
            RTTestIFailed("real get (1) i=%d\n", i);
            return 1;
        }
        if (RTAvlroGCPhysGet(&s1.Tree, pNode->KeyLast) != NULL)
        {
            RTTestIFailed("real negative get (2) i=%d\n", i);
            return 1;
        }
        if (RTAvlroGCPhysRangeGet(&s1.Tree, pNode->Key) != pNode)
        {
            RTTestIFailed("real range get (1) i=%d\n", i);
            return 1;
        }
        if (RTAvlroGCPhysRangeGet(&s1.Tree, pNode->Key + 1) != pNode)
        {
            RTTestIFailed("real range get (2) i=%d\n", i);
            return 1;
        }
        if (RTAvlroGCPhysRangeGet(&s1.Tree, pNode->KeyLast) != pNode)
        {
            RTTestIFailed("real range get (3) i=%d\n", i);
            return 1;
        }
    }

    s3 = s1;
    s1 = s2;
    for (i = 0; i < RT_ELEMENTS(s3.aNode); i++)
    {
        PAVLROGCPHYSNODECORE pNode = &s3.aNode[i];
        if (RTAvlroGCPhysGet(&s3.Tree, pNode->Key) != pNode)
        {
            RTTestIFailed("real get (10) i=%d\n", i);
            return 1;
        }
        if (RTAvlroGCPhysRangeGet(&s3.Tree, pNode->Key) != pNode)
        {
            RTTestIFailed("real range get (10) i=%d\n", i);
            return 1;
        }

        j = pNode->Key + 1;
        do
        {
            if (RTAvlroGCPhysGet(&s3.Tree, j) != NULL)
            {
                RTTestIFailed("real negative get (11) i=%d j=%#x\n", i, j);
                return 1;
            }
            if (RTAvlroGCPhysRangeGet(&s3.Tree, j) != pNode)
            {
                RTTestIFailed("real range get (11) i=%d j=%#x\n", i, j);
                return 1;
            }
        } while (j++ < pNode->KeyLast);
    }

    return 0;
}


static int avlul(void)
{
    RTTestISubF("RTAvlUL");

    /*
     * Simple linear insert and remove.
     */
    PAVLULNODECORE  pTree = 0;
    unsigned        cInserted = 0;
    unsigned        i;

    /* insert */
    for (i = 0; i < 65536; i++, cInserted++)
    {
        PAVLULNODECORE pNode = (PAVLULNODECORE)RTMemAlloc(sizeof(*pNode));
        pNode->Key = i;
        if (!RTAvlULInsert(&pTree, pNode))
        {
            RTTestIFailed("linear insert i=%d\n", i);
            return 1;
        }

        /* negative. */
        AVLULNODECORE Node = *pNode;
        if (RTAvlULInsert(&pTree, &Node))
        {
            RTTestIFailed("linear negative insert i=%d\n", i);
            return 1;
        }

        /* check height */
        uint8_t  const cHeight = pTree ? pTree->uchHeight : 0;
        uint32_t const cMax    = cHeight > 0 ? RT_BIT_32(cHeight) : 1;
        if (cInserted > cMax || cInserted < (cMax >> 2))
            RTTestIFailed("bad tree height after linear insert i=%d: cMax=%#x, cInserted=%#x\n", i, cMax, cInserted);
    }

    for (i = 0; i < 65536; i++, cInserted--)
    {
        PAVLULNODECORE pNode = RTAvlULRemove(&pTree, i);
        if (!pNode)
        {
            RTTestIFailed("linear remove i=%d\n", i);
            return 1;
        }
        pNode->pLeft     = (PAVLULNODECORE)(uintptr_t)0xaaaaaaaa;
        pNode->pRight    = (PAVLULNODECORE)(uintptr_t)0xbbbbbbbb;
        pNode->uchHeight = 'e';
        RTMemFree(pNode);

        /* negative */
        pNode = RTAvlULRemove(&pTree, i);
        if (pNode)
        {
            RTTestIFailed("linear negative remove i=%d\n", i);
            return 1;
        }

        /* check height */
        uint8_t  const cHeight = pTree ? pTree->uchHeight : 0;
        uint32_t const cMax    = cHeight > 0 ? RT_BIT_32(cHeight) : 1;
        if (cInserted > cMax || cInserted < (cMax >> 2))
            RTTestIFailed("bad tree height after linear removal i=%d: cMax=%#x, cInserted=%#x\n", i, cMax, cInserted);
    }

    /*
     * Make a sparsely populated tree.
     */
    for (i = 0; i < 65536; i += 8, cInserted++)
    {
        PAVLULNODECORE pNode = (PAVLULNODECORE)RTMemAlloc(sizeof(*pNode));
        pNode->Key = i;
        if (!RTAvlULInsert(&pTree, pNode))
        {
            RTTestIFailed("linear insert i=%d\n", i);
            return 1;
        }

        /* negative. */
        AVLULNODECORE Node = *pNode;
        if (RTAvlULInsert(&pTree, &Node))
        {
            RTTestIFailed("linear negative insert i=%d\n", i);
            return 1;
        }

        /* check height */
        uint8_t  const cHeight = pTree ? pTree->uchHeight : 0;
        uint32_t const cMax    = cHeight > 0 ? RT_BIT_32(cHeight) : 1;
        if (cInserted > cMax || cInserted < (cMax >> 2))
            RTTestIFailed("bad tree height after sparse insert i=%d: cMax=%#x, cInserted=%#x\n", i, cMax, cInserted);
    }

    /*
     * Remove using best fit in 5 cycles.
     */
    unsigned j;
    for (j = 0; j < 4; j++)
    {
        for (i = 0; i < 65536; i += 8 * 4, cInserted--)
        {
            PAVLULNODECORE pNode = RTAvlULRemoveBestFit(&pTree, i, true);
            //PAVLULNODECORE pNode = RTAvlULRemove(&pTree, i + j * 8);
            if (!pNode)
            {
                RTTestIFailed("sparse remove i=%d j=%d\n", i, j);
                return 1;
            }
            pNode->pLeft     = (PAVLULNODECORE)(uintptr_t)0xdddddddd;
            pNode->pRight    = (PAVLULNODECORE)(uintptr_t)0xcccccccc;
            pNode->uchHeight = 'E';
            RTMemFree(pNode);

            /* check height */
            uint8_t  const cHeight = pTree ? pTree->uchHeight : 0;
            uint32_t const cMax    = cHeight > 0 ? RT_BIT_32(cHeight) : 1;
            if (cInserted > cMax || cInserted < (cMax >> 2))
                RTTestIFailed("bad tree height after sparse removal i=%d: cMax=%#x, cInserted=%#x\n", i, cMax, cInserted);
        }
    }

    return 0;
}


/*********************************************************************************************************************************
*   RTCHardAvlRangeTreeGCPhys                                                                                                    *
*********************************************************************************************************************************/

typedef struct TESTNODE
{
    RTGCPHYS Key;
    RTGCPHYS KeyLast;
    uint32_t idxLeft;
    uint32_t idxRight;
    uint8_t  cHeight;
} MYTESTNODE;

static DECLCALLBACK(int) hardAvlRangeTreeGCPhysEnumCallbackAscBy4(TESTNODE *pNode, void *pvUser)
{
    PRTGCPHYS pExpect = (PRTGCPHYS)pvUser;
    if (pNode->Key != *pExpect)
        RTTestIFailed("Key=%RGp, expected %RGp\n", pNode->Key, *pExpect);
    *pExpect = pNode->Key + 4;
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) hardAvlRangeTreeGCPhysEnumCallbackDescBy4(TESTNODE *pNode, void *pvUser)
{
    PRTGCPHYS pExpect = (PRTGCPHYS)pvUser;
    if (pNode->Key != *pExpect)
        RTTestIFailed("Key=%RGp, expected %RGp\n", pNode->Key, *pExpect);
    *pExpect = pNode->Key - 4;
    return VINF_SUCCESS;
}


static DECLCALLBACK(int) hardAvlRangeTreeGCPhysEnumCallbackCount(TESTNODE *pNode, void *pvUser)
{
    *(uint32_t *)pvUser += 1;
    RT_NOREF(pNode);
    return VINF_SUCCESS;
}


static uint32_t PickClearBit(uint64_t *pbm, uint32_t cItems)
{
    uint32_t idx = RTRandAdvU32Ex(g_hRand, 0, cItems - 1);
    if (ASMBitTest(pbm, idx) == 0)
        return idx;

    /* Scan forward as we've got code for that already: */
    uint32_t const idxOrg = idx;
    idx = ASMBitNextClear(pbm, cItems, idx);
    if ((int32_t)idx >= 0)
        return idx;

    /* Scan backwards bit-by-bit because we don't have code for this: */
    for (idx = idxOrg - 1; idx < cItems; idx--)
        if (ASMBitTest(pbm, idx) == 0)
            return idx;

    AssertFailed();
    RTTestIFailed("no clear bit in bitmap!\n");
    return 0;
}


static uint32_t PickClearBitAndSetIt(uint64_t *pbm, uint32_t cItems)
{
    uint32_t idx = PickClearBit(pbm, cItems);
    RTTESTI_CHECK(ASMBitTestAndSet(pbm, idx) == false);
    return idx;
}


static uint32_t PickSetBit(uint64_t *pbm, uint32_t cItems)
{
    uint32_t idx = RTRandAdvU32Ex(g_hRand, 0, cItems - 1);
    if (ASMBitTest(pbm, idx) == 1)
        return idx;

    /* Scan forward as we've got code for that already: */
    uint32_t const idxOrg = idx;
    idx = ASMBitNextSet(pbm, cItems, idx);
    if ((int32_t)idx >= 0)
        return idx;

    /* Scan backwards bit-by-bit because we don't have code for this: */
    for (idx = idxOrg - 1; idx < cItems; idx--)
        if (ASMBitTest(pbm, idx) == 1)
            return idx;

    AssertFailed();
    RTTestIFailed("no set bit in bitmap!\n");
    return 0;
}


static uint32_t PickSetBitAndClearIt(uint64_t *pbm, uint32_t cItems)
{
    uint32_t idx = PickSetBit(pbm, cItems);
    RTTESTI_CHECK(ASMBitTestAndClear(pbm, idx) == true);
    return idx;
}


/**
 * @return meaningless value, just for shortening 'return RTTestIFailed();'.
 */
static int hardAvlRangeTreeGCPhys(RTTEST hTest)
{
    RTTestISubF("RTCHardAvlRangeTreeGCPhys");

    /*
     * Tree and allocator variables.
     */
    RTCHardAvlTreeSlabAllocator<MYTESTNODE>   Allocator;
    RTCHardAvlRangeTree<MYTESTNODE, RTGCPHYS> Tree(&Allocator);
    AssertCompileSize(Tree, sizeof(uint32_t) * 2 + sizeof(uint64_t) * 3);
    AssertCompileSize(Allocator, sizeof(void *) * 2 + sizeof(uint32_t) * 4);

    /* Initialize the allocator with a decent slab of memory. */
    const uint32_t cItems = 8192;
    void *pvItems;
    RTTESTI_CHECK_RC_RET(RTTestGuardedAlloc(hTest, sizeof(MYTESTNODE) * cItems,
                                            sizeof(uint64_t), false, &pvItems), VINF_SUCCESS, 1);
    void *pbmBitmap;
    RTTESTI_CHECK_RC_RET(RTTestGuardedAlloc(hTest, RT_ALIGN_32(cItems, 64) / 64 * 8,
                                            sizeof(uint64_t), false, &pbmBitmap), VINF_SUCCESS, 1);
    Allocator.initSlabAllocator(cItems, (TESTNODE *)pvItems, (uint64_t *)pbmBitmap);

    uint32_t cInserted = 0;

    /*
     * Simple linear insert, get and remove.
     */
    /* insert */
    for (unsigned i = 0; i < cItems * 4; i += 4, cInserted++)
    {
        MYTESTNODE *pNode = Allocator.allocateNode();
        if (!pNode)
            return RTTestIFailed("out of nodes: i=%#x", i);
        pNode->Key = i;
        pNode->KeyLast = i + 3;
        int rc = Tree.insert(&Allocator, pNode);
        if (rc != VINF_SUCCESS)
            RTTestIFailed("linear insert i=%#x failed: %Rrc", i, rc);

        /* look it up again immediately */
        for (unsigned j = 0; j < 4; j++)
        {
            MYTESTNODE *pNode2;
            rc = Tree.lookup(&Allocator, i + j, &pNode2);
            if (rc != VINF_SUCCESS || pNode2 != pNode)
                return RTTestIFailed("get after insert i=%#x j=%#x: %Rrc pNode=%p pNode2=%p", i, j, rc, pNode, pNode2);
        }

        /* Do negative inserts if we've got more free nodes. */
        if (i / 4 + 1 < cItems)
        {
            MYTESTNODE *pNode2 = Allocator.allocateNode();
            if (!pNode2)
                return RTTestIFailed("out of nodes: i=%#x (#2)", i);
            RTTESTI_CHECK(pNode2 != pNode);

            *pNode2 = *pNode;
            for (unsigned j = i >= 32 ? i - 32 : 0; j <= i + 3; j++)
            {
                for (unsigned k = i; k < i + 32; k++)
                {
                    pNode2->Key     = RT_MIN(j, k);
                    pNode2->KeyLast = RT_MAX(k, j);
                    rc = Tree.insert(&Allocator, pNode2);
                    if (rc != VERR_ALREADY_EXISTS)
                        return RTTestIFailed("linear negative insert: %Rrc, expected VERR_ALREADY_EXISTS; i=%#x j=%#x k=%#x; Key2=%RGp KeyLast2=%RGp vs Key=%RGp KeyLast=%RGp",
                                             rc, i, j, k, pNode2->Key, pNode2->KeyLast, pNode->Key, pNode->KeyLast);
                }
                if (j == 0)
                    break;
            }

            rc = Allocator.freeNode(pNode2);
            if (rc != VINF_SUCCESS)
                return RTTestIFailed("freeNode(pNode2=%p) failed: %Rrc (i=%#x)", pNode2, rc, i);
        }

        /* check the height */
        uint8_t  const cHeight = Tree.getHeight(&Allocator);
        uint32_t const cMax    = RT_BIT_32(cHeight);
        if (cInserted > cMax || cInserted < (cMax >> 4))
            RTTestIFailed("wrong tree height after linear insert i=%#x: cMax=%#x, cInserted=%#x, cHeight=%u\n",
                          i, cMax, cInserted, cHeight);
    }

    /* do gets. */
    for (unsigned i = 0; i < cItems * 4; i += 4)
    {
        MYTESTNODE *pNode;
        int rc = Tree.lookup(&Allocator, i, &pNode);
        if (rc != VINF_SUCCESS || pNode == NULL)
            return RTTestIFailed("linear get i=%#x: %Rrc pNode=%p", i, rc, pNode);
        if (i < pNode->Key || i > pNode->KeyLast)
            return RTTestIFailed("linear get i=%#x Key=%RGp KeyLast=%RGp\n", i, pNode->Key, pNode->KeyLast);

        for (unsigned j = 1; j < 4; j++)
        {
            MYTESTNODE *pNode2;
            rc = Tree.lookup(&Allocator, i + j, &pNode2);
            if (rc != VINF_SUCCESS || pNode2 != pNode)
                return RTTestIFailed("linear get i=%#x j=%#x: %Rrc pNode=%p pNode2=%p", i, j, rc, pNode, pNode2);
        }
    }

    /* negative get */
    for (unsigned i = cItems * 4; i < cItems * 4 * 2; i += 1)
    {
        MYTESTNODE *pNode = (MYTESTNODE *)(uintptr_t)i;
        int rc = Tree.lookup(&Allocator, i, &pNode);
        if (rc != VERR_NOT_FOUND || pNode != NULL)
            return RTTestIFailed("linear negative get i=%#x: %Rrc pNode=%p, expected VERR_NOT_FOUND and NULL", i, rc, pNode);
    }

    /* enumerate */
    {
        RTGCPHYS Expect = 0;
        int rc = Tree.doWithAllFromLeft(&Allocator, hardAvlRangeTreeGCPhysEnumCallbackAscBy4, &Expect);
        if (rc != VINF_SUCCESS)
            RTTestIFailed("enumeration after linear insert failed: %Rrc", rc);

        Expect -= 4;
        rc = Tree.doWithAllFromRight(&Allocator, hardAvlRangeTreeGCPhysEnumCallbackDescBy4, &Expect);
        if (rc != VINF_SUCCESS)
            RTTestIFailed("enumeration after linear insert failed: %Rrc", rc);
    }

    /* remove */
    for (unsigned i = 0, j = 0; i < cItems * 4; i += 4, j += 3, cInserted--)
    {
        MYTESTNODE *pNode;
        int rc = Tree.remove(&Allocator, i + (j % 4), &pNode);
        if (rc != VINF_SUCCESS || pNode == NULL)
            return RTTestIFailed("linear remove(%#x): %Rrc pNode=%p", i + (j % 4), rc, pNode);
        if (i < pNode->Key || i > pNode->KeyLast)
            return RTTestIFailed("linear remove i=%#x Key=%RGp KeyLast=%RGp\n", i, pNode->Key, pNode->KeyLast);

        memset(pNode, 0xcc, sizeof(*pNode));
        Allocator.freeNode(pNode);

        /* negative */
        for (unsigned k = i; k < i + 4; k++)
        {
            pNode = (MYTESTNODE *)(uintptr_t)k;
            rc = Tree.remove(&Allocator, k, &pNode);
            if (rc != VERR_NOT_FOUND || pNode != NULL)
                return RTTestIFailed("linear negative remove(%#x): %Rrc pNode=%p", k, rc, pNode);
        }

        /* check the height */
        uint8_t  const cHeight = Tree.getHeight(&Allocator);
        uint32_t const cMax    = RT_BIT_32(cHeight);
        if (cInserted > cMax || cInserted < (cMax >> 4))
            RTTestIFailed("wrong tree height after linear remove i=%#x: cMax=%#x, cInserted=%#x cHeight=%d\n",
                          i, cMax, cInserted, cHeight);
    }

    /*
     * Randomized stuff.
     */
    uint64_t uSeed = RTRandU64();
    RTRandAdvSeed(g_hRand, uSeed);
    RTTestIPrintf(RTTESTLVL_ALWAYS, "Random seed #1: %#RX64\n", uSeed);

    RTGCPHYS const   cbStep     = RTGCPHYS_MAX / cItems + 1;
    uint64_t * const pbmPresent = (uint64_t *)RTMemAllocZ(RT_ALIGN_32(cItems, 64) / 64 * 8);
    RTTESTI_CHECK_RET(pbmPresent, 1);

    /* insert all in random order */
    cInserted = 0;
    for (unsigned i = 0; i < cItems; i++)
    {
        MYTESTNODE *pNode = Allocator.allocateNode();
        if (!pNode)
            return RTTestIFailed("out of nodes: i=%#x #3", i);

        uint32_t const idx = PickClearBitAndSetIt(pbmPresent, cItems);
        pNode->Key     = idx * cbStep;
        pNode->KeyLast = pNode->Key + cbStep - 1;
        int rc = Tree.insert(&Allocator, pNode);
        if (rc == VINF_SUCCESS)
            cInserted++;
        else
            RTTestIFailed("random insert failed: %Rrc, i=%#x, idx=%#x (%RGp ... %RGp)", rc, i, idx, pNode->Key, pNode->KeyLast);

        MYTESTNODE *pNode2 = (MYTESTNODE *)(intptr_t)i;
        rc = Tree.lookup(&Allocator, pNode->Key, &pNode2);
        if (rc != VINF_SUCCESS || pNode2 != pNode)
            return RTTestIFailed("lookup after random insert %#x: %Rrc pNode=%p pNode2=%p idx=%#x", i, rc, pNode, pNode2, idx);

        uint32_t cCount = 0;
        rc = Tree.doWithAllFromLeft(&Allocator, hardAvlRangeTreeGCPhysEnumCallbackCount, &cCount);
        if (rc != VINF_SUCCESS)
            RTTestIFailed("enum after random insert %#x: %Rrc idx=%#x", i, rc, idx);
        else if (cCount != cInserted)
            RTTestIFailed("wrong count after random removal %#x: %#x, expected %#x", i, cCount, cInserted);

        /* check the height */
        uint8_t  const cHeight = Tree.getHeight(&Allocator);
        uint32_t const cMax    = RT_BIT_32(cHeight);
        if (cInserted > cMax || cInserted < (cMax >> 4))
            RTTestIFailed("wrong tree height after random insert %#x: cMax=%#x, cInserted=%#x, cHeight=%u\n",
                          i, cMax, cInserted, cHeight);
    }

    /* remove all in random order, doing adjacent lookups while at it. */
    for (unsigned i = 0; i < cItems; i++)
    {
        uint32_t const idx = PickSetBitAndClearIt(pbmPresent, cItems);
        RTGCPHYS const Key = idx * cbStep;

        /* pre-removal lookup tests */
        MYTESTNODE *pNode = (MYTESTNODE *)(intptr_t)i;
        int rc = Tree.lookupMatchingOrBelow(&Allocator, Key, &pNode);
        if (rc != VINF_SUCCESS)
            RTTestIFailed("pre-remove lookupMatchingOrBelow failed: %Rrc, i=%#x, idx=%#x (%RGp ... %RGp)",
                          rc, i, idx, Key, Key + cbStep - 1);
        else if (pNode->Key != Key)
            RTTestIFailed("pre-remove lookupMatchingOrBelow returned the wrong node: Key=%RGp, expected %RGp", pNode->Key, Key);

        pNode = (MYTESTNODE *)(intptr_t)i;
        rc = Tree.lookupMatchingOrAbove(&Allocator, Key, &pNode);
        if (rc != VINF_SUCCESS)
            RTTestIFailed("pre-remove lookupMatchingOrAbove failed: %Rrc, i=%#x, idx=%#x (%RGp ... %RGp)",
                          rc, i, idx, Key, Key + cbStep - 1);
        else if (pNode->Key != Key)
            RTTestIFailed("pre-remove lookupMatchingOrAbove returned the wrong node: Key=%RGp, expected %RGp", pNode->Key, Key);

        /* remove */
        pNode = (MYTESTNODE *)(intptr_t)i;
        rc = Tree.remove(&Allocator, Key, &pNode);
        if (rc != VINF_SUCCESS)
            RTTestIFailed("random remove failed: %Rrc, i=%#x, idx=%#x (%RGp ... %RGp)",
                          rc, i, idx, Key, Key + cbStep - 1);
        else
        {
            cInserted--;
            if (    pNode->Key     != Key
                 || pNode->KeyLast != Key + cbStep - 1)
                RTTestIFailed("random remove returned wrong node: %RGp ... %RGp, expected %RGp ... %RGp (i=%#x, idx=%#x)",
                              pNode->Key, pNode->KeyLast, Key, Key + cbStep - 1, i, idx);
            else
            {
                MYTESTNODE *pNode2 = (MYTESTNODE *)(intptr_t)i;
                rc = Tree.lookup(&Allocator, Key, &pNode2);
                if (rc != VERR_NOT_FOUND)
                    RTTestIFailed("lookup after random removal %#x: %Rrc pNode=%p pNode2=%p idx=%#x", i, rc, pNode, pNode2, idx);

                uint32_t cCount = 0;
                rc = Tree.doWithAllFromLeft(&Allocator, hardAvlRangeTreeGCPhysEnumCallbackCount, &cCount);
                if (rc != VINF_SUCCESS)
                    RTTestIFailed("enum after random removal %#x: %Rrc idx=%#x", i, rc, idx);
                else if (cCount != cInserted)
                    RTTestIFailed("wrong count after random removal %#x: %#x, expected %#x", i, cCount, cInserted);
            }

            rc = Allocator.freeNode(pNode);
            if (rc != VINF_SUCCESS)
                RTTestIFailed("free after random removal %#x failed: %Rrc pNode=%p idx=%#x", i, rc, pNode, idx);

            /* post-removal lookup tests */
            pNode = (MYTESTNODE *)(intptr_t)i;
            rc = Tree.lookupMatchingOrBelow(&Allocator, Key, &pNode);
            uint32_t idxAbove;
            if (rc == VINF_SUCCESS)
            {
                uint32_t idxRet = pNode->Key / cbStep;
                RTTESTI_CHECK(ASMBitTest(pbmPresent, idxRet) == true);
                idxAbove = (uint32_t)ASMBitNextSet(pbmPresent, cItems, idxRet);
                if (idxAbove <= idx)
                    RTTestIFailed("post-remove lookupMatchingOrBelow wrong: idxRet=%#x idx=%#x idxAbove=%#x",
                                  idxRet, idx, idxAbove);
            }
            else if (rc == VERR_NOT_FOUND)
            {
                idxAbove = (uint32_t)ASMBitFirstSet(pbmPresent, cItems);
                if (idxAbove <= idx)
                    RTTestIFailed("post-remove lookupMatchingOrBelow wrong: VERR_NOT_FOUND idx=%#x idxAbove=%#x", idx, idxAbove);
            }
            else
            {
                RTTestIFailed("post-remove lookupMatchingOrBelow failed: %Rrc, i=%#x, idx=%#x (%RGp ... %RGp)",
                              rc, i, idx, Key, Key + cbStep - 1);
                idxAbove = (uint32_t)ASMBitNextSet(pbmPresent, cItems, idx);
            }

            pNode = (MYTESTNODE *)(intptr_t)i;
            rc = Tree.lookupMatchingOrAbove(&Allocator, Key, &pNode);
            if (rc == VINF_SUCCESS)
            {
                uint32_t idxRet = pNode->Key / cbStep;
                if (idxRet != idxAbove)
                    RTTestIFailed("post-remove lookupMatchingOrAbove wrong: idxRet=%#x idxAbove=%#x idx=%#x",
                                  idxRet, idxAbove, idx);
            }
            else if (rc == VERR_NOT_FOUND)
            {
                if (idxAbove != UINT32_MAX)
                    RTTestIFailed("post-remove lookupMatchingOrAbove wrong: VERR_NOT_FOUND idxAbove=%#x idx=%#x", idxAbove, idx);
            }
            else
            {
                RTTestIFailed("post-remove lookupMatchingOrAbove failed: %Rrc, i=%#x, idx=%#x (%RGp ... %RGp) idxAbove=%#x",
                              rc, i, idx, Key, Key + cbStep - 1, idxAbove);
            }
        }

        /* check the height */
        uint8_t  const cHeight = Tree.getHeight(&Allocator);
        uint32_t const cMax    = RT_BIT_32(cHeight);
        if (cInserted > cMax || cInserted < (cMax >> 4))
            RTTestIFailed("wrong tree height after random removal %#x: cMax=%#x, cInserted=%#x, cHeight=%u\n",
                          i, cMax, cInserted, cHeight);
    }

    /*
     * Randomized operation.
     */
    uSeed = RTRandU64();
    RTRandAdvSeed(g_hRand, uSeed);
    RTTestIPrintf(RTTESTLVL_ALWAYS, "Random seed #2: %#RX64\n", uSeed);
    uint64_t       cItemsEnumed = 0;
    bool           fAdding      = true;
    uint64_t const nsStart      = RTTimeNanoTS();
    unsigned       i;
    for (i = 0, cInserted = 0; i < _64M; i++)
    {
        /* The operation. */
        bool fDelete;
        if (cInserted == cItems)
        {
            fDelete = true;
            fAdding = false;
        }
        else if (cInserted == 0)
        {
            fDelete = false;
            fAdding = true;
        }
        else
            fDelete = fAdding ? RTRandU32Ex(0, 3) == 1 : RTRandU32Ex(0, 3) != 0;

        if (!fDelete)
        {
            uint32_t const idxInsert = PickClearBitAndSetIt(pbmPresent, cItems);

            MYTESTNODE *pNode = Allocator.allocateNode();
            if (!pNode)
                return RTTestIFailed("out of nodes: cInserted=%#x cItems=%#x i=%#x", cInserted, cItems, i);
            pNode->Key     = idxInsert * cbStep;
            pNode->KeyLast = pNode->Key + cbStep - 1;
            int rc = Tree.insert(&Allocator, pNode);
            if (rc == VINF_SUCCESS)
                cInserted += 1;
            else
            {
                RTTestIFailed("random insert failed: %Rrc - %RGp ... %RGp cInserted=%#x cItems=%#x i=%#x",
                              rc, pNode->Key, pNode->KeyLast, cInserted, cItems, i);
                Allocator.freeNode(pNode);
            }
        }
        else
        {
            uint32_t const idxDelete = PickSetBitAndClearIt(pbmPresent, cItems);

            MYTESTNODE *pNode = (MYTESTNODE *)(intptr_t)idxDelete;
            int rc = Tree.remove(&Allocator, idxDelete * cbStep, &pNode);
            if (rc == VINF_SUCCESS)
            {
                if (   pNode->Key     != idxDelete * cbStep
                    || pNode->KeyLast != idxDelete * cbStep + cbStep - 1)
                    RTTestIFailed("random remove returned wrong node: %RGp ... %RGp, expected %RGp ... %RGp (cInserted=%#x cItems=%#x i=%#x)",
                                  pNode->Key, pNode->KeyLast, idxDelete * cbStep, idxDelete * cbStep + cbStep - 1,
                                  cInserted, cItems, i);

                cInserted -= 1;
                rc = Allocator.freeNode(pNode);
                if (rc != VINF_SUCCESS)
                    RTTestIFailed("free after random removal failed: %Rrc - pNode=%p i=%#x", rc, pNode, i);
            }
            else
                RTTestIFailed("random remove failed: %Rrc - %RGp ... %RGp cInserted=%#x cItems=%#x i=%#x",
                              rc, idxDelete * cbStep, idxDelete * cbStep + cbStep - 1, cInserted, cItems, i);
        }

        /* Count the tree items.  This will make sure the tree is balanced in strict builds. */
        uint32_t cCount = 0;
        int rc = Tree.doWithAllFromLeft(&Allocator, hardAvlRangeTreeGCPhysEnumCallbackCount, &cCount);
        if (rc != VINF_SUCCESS)
            RTTestIFailed("enum after random %s failed: %Rrc - i=%#x", fDelete ? "removal" : "insert", rc, i);
        else if (cCount != cInserted)
            RTTestIFailed("wrong count after random %s: %#x, expected %#x - i=%#x",
                          fDelete ? "removal" : "insert", cCount, cInserted, i);
        cItemsEnumed += cCount;

        /* check the height */
        uint8_t  const cHeight = Tree.getHeight(&Allocator);
        uint32_t const cMax    = RT_BIT_32(cHeight);
        if (cInserted > cMax || cInserted < (cMax >> 4))
            RTTestIFailed("wrong tree height after random %s: cMax=%#x, cInserted=%#x, cHeight=%u - i=%#x\n",
                          fDelete ? "removal" : "insert", cMax, cInserted, cHeight, i);

        /* Check for timeout. */
        if (   (i & 0xffff) == 0
            && RTTimeNanoTS() - nsStart >= RT_NS_15SEC)
            break;
    }
    uint64_t cNsElapsed = RTTimeNanoTS() - nsStart;
    RTTestIPrintf(RTTESTLVL_ALWAYS, "Performed %'u operations and enumerated %'RU64 nodes in %'RU64 ns\n",
                  i, cItemsEnumed, cNsElapsed);

    RTTestIValue("Operations rate",        (uint64_t)i * RT_NS_1SEC / RT_MAX(cNsElapsed, 1), RTTESTUNIT_OCCURRENCES_PER_SEC);
    RTTestIValue("Nodes enumeration rate",
                 (uint64_t)((double)cItemsEnumed * (double)RT_NS_1SEC / (double)RT_MAX(cNsElapsed, 1)),
                 RTTESTUNIT_OCCURRENCES_PER_SEC);

    return 0;
}


int main()
{
    /*
     * Init.
     */
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTAvl", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);
    g_hTest = hTest;

    rc = RTRandAdvCreateParkMiller(&g_hRand);
    if (RT_FAILURE(rc))
    {
        RTTestIFailed("RTRandAdvCreateParkMiller -> %Rrc", rc);
        return RTTestSummaryAndDestroy(hTest);
    }

    /*
     * Testing.
     */
    unsigned i;
    RTTestSub(hTest, "oGCPhys(32..2048)");
    for (i = 32; i < 2048; i++)
        if (avlogcphys(i))
            break;

    avlogcphys(_64K);
    avlogcphys(_512K);
    avlogcphys(_4M);

    RTTestISubF("oGCPhys(32..2048, *1K)");
    for (i = 32; i < 4096; i++)
        if (avlogcphysRand(i, i + _1K, 0xff))
            break;
    for (; i <= _4M; i *= 2)
        if (avlogcphysRand(i, i * 8, i * 2 - 1))
            break;

    avlrogcphys();
    avlul();

    hardAvlRangeTreeGCPhys(hTest);

    /*
     * Done.
     */
    return RTTestSummaryAndDestroy(hTest);
}

