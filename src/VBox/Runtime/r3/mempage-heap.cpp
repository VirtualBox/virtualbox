/* $Id: mempage-heap.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTMemPage*, optimized using heap.
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
#include <iprt/mem.h>

#include <iprt/asm-mem.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/avl.h>
#include <iprt/critsect.h>
#include <iprt/errcore.h>
#include <iprt/list.h>
#include <iprt/once.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include "internal/mem.h"



/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Threshold at which to we switch to simply calling mmap. */
#define RTMEMPAGE_NATIVE_THRESHOLD      _1M
/** The size of a heap block (power of two) - in bytes. */
#define RTMEMPAGE_BLOCK_SIZE            _4M

/** The number of pages per heap block. */
#define RTMEMPAGE_BLOCK_PAGE_COUNT      (RTMEMPAGE_BLOCK_SIZE / PAGE_SIZE)
AssertCompile(RTMEMPAGE_BLOCK_SIZE == RTMEMPAGE_BLOCK_PAGE_COUNT * PAGE_SIZE);


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to a page heap block. */
typedef struct RTHEAPPAGEBLOCK *PRTHEAPPAGEBLOCK;

/**
 * A simple page heap.
 */
typedef struct RTHEAPPAGE
{
    /** Magic number (RTHEAPPAGE_MAGIC). */
    uint32_t            u32Magic;
    /** The number of pages in the heap (in BlockTree). */
    uint32_t            cHeapPages;
    /** The number of currently free pages. */
    uint32_t            cFreePages;
    /** Number of successful calls. */
    uint32_t            cAllocCalls;
    /** Number of successful free calls. */
    uint32_t            cFreeCalls;
    /** The free call number at which we last tried to minimize the heap. */
    uint32_t            uLastMinimizeCall;
    /** Tree of heap blocks. */
    AVLRPVTREE          BlockTree;
    /** Allocation hint no 1 (last freed). */
    PRTHEAPPAGEBLOCK    pHint1;
    /** Allocation hint no 2 (last alloc). */
    PRTHEAPPAGEBLOCK    pHint2;
    /** The allocation chunks for the RTHEAPPAGEBLOCK allocator
     * (RTHEAPPAGEBLOCKALLOCCHUNK). */
    RTLISTANCHOR        BlockAllocatorChunks;
    /** Critical section protecting the heap. */
    RTCRITSECT          CritSect;
    /** Set if the memory must allocated with execute access. */
    bool                fExec;
} RTHEAPPAGE;
#define RTHEAPPAGE_MAGIC     UINT32_C(0xfeedface)
/** Pointer to a page heap. */
typedef RTHEAPPAGE *PRTHEAPPAGE;


/**
 * Describes a page heap block.
 */
typedef struct RTHEAPPAGEBLOCK
{
    /** The AVL tree node core (void pointer range). */
    AVLRPVNODECORE      Core;
    /** The number of free pages. */
    uint32_t            cFreePages;
    /** Pointer back to the heap. */
    PRTHEAPPAGE         pHeap;
    /** Allocation bitmap.  Set bits marks allocated pages. */
    uint32_t            bmAlloc[RTMEMPAGE_BLOCK_PAGE_COUNT / 32];
    /** Allocation boundrary bitmap.  Set bits marks the start of
     *  allocations. */
    uint32_t            bmFirst[RTMEMPAGE_BLOCK_PAGE_COUNT / 32];
    /** Bitmap tracking pages where RTMEMPAGEALLOC_F_ADVISE_LOCKED has been
     *  successfully applied. */
    uint32_t            bmLockedAdviced[RTMEMPAGE_BLOCK_PAGE_COUNT / 32];
    /** Bitmap tracking pages where RTMEMPAGEALLOC_F_ADVISE_NO_DUMP has been
     *  successfully applied. */
    uint32_t            bmNoDumpAdviced[RTMEMPAGE_BLOCK_PAGE_COUNT / 32];
} RTHEAPPAGEBLOCK;


/**
 * Allocation chunk of RTHEAPPAGEBLOCKALLOCCHUNK structures.
 *
 * This is backed by an 64KB allocation and non-present blocks will be marked as
 * allocated in bmAlloc.
 */
typedef struct RTHEAPPAGEBLOCKALLOCCHUNK
{
    /** List entry. */
    RTLISTNODE          ListEntry;
    /** Number of free RTHEAPPAGEBLOCK structures here. */
    uint32_t            cFree;
    /** Number of blocks in aBlocks. */
    uint32_t            cBlocks;
    /** Allocation bitmap. */
    uint32_t            bmAlloc[ARCH_BITS == 32 ? 28 : 26];
    /** Block array. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    RTHEAPPAGEBLOCK     aBlocks[RT_FLEXIBLE_ARRAY];
} RTHEAPPAGEBLOCKALLOCCHUNK;
AssertCompileMemberAlignment(RTHEAPPAGEBLOCKALLOCCHUNK, bmAlloc, 8);
AssertCompileMemberAlignment(RTHEAPPAGEBLOCKALLOCCHUNK, aBlocks, 64);
/** Pointer to an allocation chunk of RTHEAPPAGEBLOCKALLOCCHUNK structures. */
typedef RTHEAPPAGEBLOCKALLOCCHUNK *PRTHEAPPAGEBLOCKALLOCCHUNK;

/** Max number of blocks one RTHEAPPAGEBLOCKALLOCCHUNK can track (896/832). */
#define RTHEAPPAGEBLOCKALLOCCHUNK_MAX_BLOCKS    ((ARCH_BITS == 32 ? 28 : 26) * 32)
/** The chunk size for the block allocator. */
#define RTHEAPPAGEBLOCKALLOCCHUNK_ALLOC_SIZE    _64K


/**
 * Argument package for rtHeapPageAllocCallback.
 */
typedef struct RTHEAPPAGEALLOCARGS
{
    /** The number of pages to allocate. */
    size_t          cPages;
    /** Non-null on success.  */
    void           *pvAlloc;
    /** RTMEMPAGEALLOC_F_XXX. */
    uint32_t        fFlags;
} RTHEAPPAGEALLOCARGS;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Initialize once structure. */
static RTONCE       g_MemPageHeapInitOnce = RTONCE_INITIALIZER;
/** The page heap. */
static RTHEAPPAGE   g_MemPageHeap;
/** The exec page heap. */
static RTHEAPPAGE   g_MemExecHeap;


/**
 * Initializes the heap.
 *
 * @returns IPRT status code.
 * @param   pHeap           The page heap to initialize.
 * @param   fExec           Whether the heap memory should be marked as
 *                          executable or not.
 */
static int RTHeapPageInit(PRTHEAPPAGE pHeap, bool fExec)
{
    int rc = RTCritSectInitEx(&pHeap->CritSect,
                              RTCRITSECT_FLAGS_NO_LOCK_VAL | RTCRITSECT_FLAGS_NO_NESTING | RTCRITSECT_FLAGS_BOOTSTRAP_HACK,
                              NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_NONE, NULL);
    if (RT_SUCCESS(rc))
    {
        pHeap->cHeapPages           = 0;
        pHeap->cFreePages           = 0;
        pHeap->cAllocCalls          = 0;
        pHeap->cFreeCalls           = 0;
        pHeap->uLastMinimizeCall    = 0;
        pHeap->BlockTree            = NULL;
        pHeap->fExec                = fExec;
        RTListInit(&pHeap->BlockAllocatorChunks);
        pHeap->u32Magic             = RTHEAPPAGE_MAGIC;
    }
    return rc;
}


/**
 * Deletes the heap and all the memory it tracks.
 *
 * @returns IPRT status code.
 * @param   pHeap           The page heap to delete.
 */
static int RTHeapPageDelete(PRTHEAPPAGE pHeap)
{
    NOREF(pHeap);
    pHeap->u32Magic = ~RTHEAPPAGE_MAGIC;
    return VINF_SUCCESS;
}


/**
 * Allocates a RTHEAPPAGEBLOCK.
 *
 * @returns Pointer to RTHEAPPAGEBLOCK on success, NULL on failure.
 * @param   pHeap   The heap this is for.
 */
static PRTHEAPPAGEBLOCK rtHeapPageIntBlockAllocatorAlloc(PRTHEAPPAGE pHeap)
{
    /*
     * Locate a chunk with space and grab a block from it.
     */
    PRTHEAPPAGEBLOCKALLOCCHUNK pChunk;
    RTListForEach(&pHeap->BlockAllocatorChunks, pChunk, RTHEAPPAGEBLOCKALLOCCHUNK, ListEntry)
    {
        if (pChunk->cFree > 0)
        {
            uint32_t const cBits = RT_ALIGN_32(pChunk->cBlocks, 64);
            int idxBlock = ASMBitFirstClear(&pChunk->bmAlloc[0], RT_MIN(RTHEAPPAGEBLOCKALLOCCHUNK_MAX_BLOCKS, cBits));
            if (idxBlock >= 0)
            {
                ASMBitSet(&pChunk->bmAlloc[0], idxBlock);
                pChunk->cFree -= 1;
                return &pChunk->aBlocks[idxBlock];
            }
            AssertFailed();
        }
    }

    /*
     * Allocate a new chunk and return the first block in it.
     */
    int rc = rtMemPageNativeAlloc(RTHEAPPAGEBLOCKALLOCCHUNK_ALLOC_SIZE, 0, (void **)&pChunk);
    AssertRCReturn(rc, NULL);
    pChunk->cBlocks = (RTHEAPPAGEBLOCKALLOCCHUNK_ALLOC_SIZE - RT_UOFFSETOF(RTHEAPPAGEBLOCKALLOCCHUNK, aBlocks))
                    / sizeof(pChunk->aBlocks[0]);
    AssertStmt(pChunk->cBlocks < RTHEAPPAGEBLOCKALLOCCHUNK_MAX_BLOCKS, pChunk->cBlocks = RTHEAPPAGEBLOCKALLOCCHUNK_MAX_BLOCKS);
    pChunk->cFree   = pChunk->cBlocks;

    RT_ZERO(pChunk->bmAlloc);
    ASMBitSetRange(pChunk->bmAlloc, pChunk->cBlocks, RTHEAPPAGEBLOCKALLOCCHUNK_MAX_BLOCKS);
    RTListPrepend(&pHeap->BlockAllocatorChunks, &pChunk->ListEntry);

    /*
     * Allocate the first one.
     */
    ASMBitSet(pChunk->bmAlloc, 0);
    pChunk->cFree -= 1;

    return &pChunk->aBlocks[0];
}


/**
 * Frees a RTHEAPPAGEBLOCK.
 *
 * @param   pHeap   The heap this is for.
 * @param   pBlock  The block to free.
 */
static void rtHeapPageIntBlockAllocatorFree(PRTHEAPPAGE pHeap, PRTHEAPPAGEBLOCK pBlock)
{
    /*
     * Locate the chunk the block belongs to and mark it as freed.
     */
    PRTHEAPPAGEBLOCKALLOCCHUNK pChunk;
    RTListForEach(&pHeap->BlockAllocatorChunks, pChunk, RTHEAPPAGEBLOCKALLOCCHUNK, ListEntry)
    {
        if ((uintptr_t)pBlock - (uintptr_t)pChunk < RTHEAPPAGEBLOCKALLOCCHUNK_ALLOC_SIZE)
        {
            uintptr_t const idxBlock = (uintptr_t)(pBlock - &pChunk->aBlocks[0]);
            if (ASMBitTestAndClear(&pChunk->bmAlloc[0], idxBlock))
                pChunk->cFree++;
            else
                AssertMsgFailed(("pBlock=%p idxBlock=%#zx\n", pBlock, idxBlock));
            return;
        }
    }
    AssertFailed();
}


/**
 * Applies flags to an allocation.
 *
 * @return  Flags that eeds to be reverted upon free.
 * @param   pv              The allocation.
 * @param   cb              The size of the allocation (page aligned).
 * @param   fFlags          RTMEMPAGEALLOC_F_XXX.
 */
DECLINLINE(uint32_t) rtMemPageApplyFlags(void *pv, size_t cb, uint32_t fFlags)
{
    uint32_t fHandled = 0;
    if (fFlags & (RTMEMPAGEALLOC_F_ADVISE_LOCKED | RTMEMPAGEALLOC_F_ADVISE_NO_DUMP))
        fHandled = rtMemPageNativeApplyFlags(pv, cb, fFlags);
    if (fFlags & RTMEMPAGEALLOC_F_ZERO)
        RT_BZERO(pv, cb);
    return fHandled;
}


/**
 * Avoids some gotos in rtHeapPageAllocFromBlock.
 *
 * @returns VINF_SUCCESS.
 * @param   pBlock          The block.
 * @param   iPage           The page to start allocating at.
 * @param   cPages          The number of pages.
 * @param   fFlags          RTMEMPAGEALLOC_F_XXX.
 * @param   ppv             Where to return the allocation address.
 */
DECLINLINE(int) rtHeapPageAllocFromBlockSuccess(PRTHEAPPAGEBLOCK pBlock, uint32_t iPage, size_t cPages, uint32_t fFlags, void **ppv)
{
    PRTHEAPPAGE pHeap = pBlock->pHeap;

    ASMBitSet(&pBlock->bmFirst[0], iPage);
    pBlock->cFreePages -= (uint32_t)cPages;
    pHeap->cFreePages  -= (uint32_t)cPages;
    if (!pHeap->pHint2 || pHeap->pHint2->cFreePages < pBlock->cFreePages)
        pHeap->pHint2 = pBlock;
    pHeap->cAllocCalls++;

    void *pv = (uint8_t *)pBlock->Core.Key + (iPage << PAGE_SHIFT);
    *ppv = pv;

    if (fFlags)
    {
        uint32_t fHandled = rtMemPageApplyFlags(pv, cPages << PAGE_SHIFT, fFlags);
        Assert(!(fHandled & ~(RTMEMPAGEALLOC_F_ADVISE_LOCKED | RTMEMPAGEALLOC_F_ADVISE_NO_DUMP)));
        if (fHandled & RTMEMPAGEALLOC_F_ADVISE_LOCKED)
            ASMBitSetRange(&pBlock->bmLockedAdviced[0], iPage, iPage + cPages);
        if (fHandled & RTMEMPAGEALLOC_F_ADVISE_NO_DUMP)
            ASMBitSetRange(&pBlock->bmNoDumpAdviced[0], iPage, iPage + cPages);
    }

    return VINF_SUCCESS;
}


/**
 * Checks if a page range is free in the specified block.
 *
 * @returns @c true if the range is free, @c false if not.
 * @param   pBlock          The block.
 * @param   iFirst          The first page to check.
 * @param   cPages          The number of pages to check.
 */
DECLINLINE(bool) rtHeapPageIsPageRangeFree(PRTHEAPPAGEBLOCK pBlock, uint32_t iFirst, uint32_t cPages)
{
    uint32_t i = iFirst + cPages;
    while (i-- > iFirst)
    {
        if (ASMBitTest(&pBlock->bmAlloc[0], i))
            return false;
        Assert(!ASMBitTest(&pBlock->bmFirst[0], i));
    }
    return true;
}


/**
 * Tries to allocate a chunk of pages from a heap block.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_NO_MEMORY if the allocation failed.
 * @param   pBlock          The block to allocate from.
 * @param   cPages          The size of the allocation.
 * @param   fFlags          RTMEMPAGEALLOC_F_XXX.
 * @param   ppv             Where to return the allocation address on success.
 */
DECLINLINE(int) rtHeapPageAllocFromBlock(PRTHEAPPAGEBLOCK pBlock, size_t cPages, uint32_t fFlags, void **ppv)
{
    if (pBlock->cFreePages >= cPages)
    {
        int iPage = ASMBitFirstClear(&pBlock->bmAlloc[0], RTMEMPAGE_BLOCK_PAGE_COUNT);
        Assert(iPage >= 0);

        /* special case: single page. */
        if (cPages == 1)
        {
            ASMBitSet(&pBlock->bmAlloc[0], iPage);
            return rtHeapPageAllocFromBlockSuccess(pBlock, iPage, cPages, fFlags, ppv);
        }

        while (   iPage >= 0
               && (unsigned)iPage <= RTMEMPAGE_BLOCK_PAGE_COUNT - cPages)
        {
            if (rtHeapPageIsPageRangeFree(pBlock, iPage + 1, (uint32_t)cPages - 1))
            {
                ASMBitSetRange(&pBlock->bmAlloc[0], iPage, iPage + cPages);
                return rtHeapPageAllocFromBlockSuccess(pBlock, iPage, cPages, fFlags, ppv);
            }

            /* next */
            iPage = ASMBitNextSet(&pBlock->bmAlloc[0], RTMEMPAGE_BLOCK_PAGE_COUNT, iPage);
            if (iPage < 0 || (unsigned)iPage >= RTMEMPAGE_BLOCK_PAGE_COUNT - 1)
                break;
            iPage = ASMBitNextClear(&pBlock->bmAlloc[0], RTMEMPAGE_BLOCK_PAGE_COUNT, iPage);
        }
    }

    return VERR_NO_MEMORY;
}


/**
 * RTAvlrPVDoWithAll callback.
 *
 * @returns 0 to continue the enum, non-zero to quit it.
 * @param   pNode           The node.
 * @param   pvUser          The user argument.
 */
static DECLCALLBACK(int) rtHeapPageAllocCallback(PAVLRPVNODECORE pNode, void *pvUser)
{
    PRTHEAPPAGEBLOCK        pBlock = RT_FROM_MEMBER(pNode,  RTHEAPPAGEBLOCK, Core);
    RTHEAPPAGEALLOCARGS    *pArgs  = (RTHEAPPAGEALLOCARGS *)pvUser;
    int rc = rtHeapPageAllocFromBlock(pBlock, pArgs->cPages, pArgs->fFlags, &pArgs->pvAlloc);
    return RT_SUCCESS(rc) ? 1 : 0;
}


/**
 * Worker for RTHeapPageAlloc.
 *
 * @returns IPRT status code
 * @param   pHeap           The heap - locked.
 * @param   cPages          The page count.
 * @param   pszTag          The tag.
 * @param   fFlags          RTMEMPAGEALLOC_F_XXX.
 * @param   ppv             Where to return the address of the allocation
 *                          on success.
 */
static int rtHeapPageAllocLocked(PRTHEAPPAGE pHeap, size_t cPages, const char *pszTag, uint32_t fFlags, void **ppv)
{
    int rc;
    NOREF(pszTag);

    /*
     * Use the hints first.
     */
    if (pHeap->pHint1)
    {
        rc = rtHeapPageAllocFromBlock(pHeap->pHint1, cPages, fFlags, ppv);
        if (rc != VERR_NO_MEMORY)
            return rc;
    }
    if (pHeap->pHint2)
    {
        rc = rtHeapPageAllocFromBlock(pHeap->pHint2, cPages, fFlags, ppv);
        if (rc != VERR_NO_MEMORY)
            return rc;
    }

    /*
     * Search the heap for a block with enough free space.
     *
     * N.B. This search algorithm is not optimal at all. What (hopefully) saves
     *      it are the two hints above.
     */
    if (pHeap->cFreePages >= cPages)
    {
        RTHEAPPAGEALLOCARGS Args;
        Args.cPages  = cPages;
        Args.pvAlloc = NULL;
        Args.fFlags  = fFlags;
        RTAvlrPVDoWithAll(&pHeap->BlockTree, true /*fFromLeft*/, rtHeapPageAllocCallback, &Args);
        if (Args.pvAlloc)
        {
            *ppv = Args.pvAlloc;
            return VINF_SUCCESS;
        }
    }

    /*
     * Didn't find anything, so expand the heap with a new block.
     */
    PRTHEAPPAGEBLOCK const pBlock = rtHeapPageIntBlockAllocatorAlloc(pHeap);
    AssertReturn(pBlock, VERR_NO_MEMORY);

    RTCritSectLeave(&pHeap->CritSect);

    void *pvPages = NULL;
    rc = rtMemPageNativeAlloc(RTMEMPAGE_BLOCK_SIZE, pHeap->fExec ? RTMEMPAGEALLOC_F_EXECUTABLE : 0, &pvPages);

    RTCritSectEnter(&pHeap->CritSect);
    if (RT_FAILURE(rc))
    {
        rtHeapPageIntBlockAllocatorFree(pHeap, pBlock);
        return rc;
    }

    RT_ZERO(*pBlock);
    pBlock->Core.Key        = pvPages;
    pBlock->Core.KeyLast    = (uint8_t *)pvPages + RTMEMPAGE_BLOCK_SIZE - 1;
    pBlock->cFreePages      = RTMEMPAGE_BLOCK_PAGE_COUNT;
    pBlock->pHeap           = pHeap;

    bool fRc = RTAvlrPVInsert(&pHeap->BlockTree, &pBlock->Core); Assert(fRc); NOREF(fRc);
    pHeap->cFreePages      +=  RTMEMPAGE_BLOCK_PAGE_COUNT;
    pHeap->cHeapPages      +=  RTMEMPAGE_BLOCK_PAGE_COUNT;

    /*
     * Grab memory from the new block (cannot fail).
     */
    rc = rtHeapPageAllocFromBlock(pBlock, cPages, fFlags, ppv);
    Assert(rc == VINF_SUCCESS);

    return rc;
}


/**
 * Allocates one or more pages off the heap.
 *
 * @returns IPRT status code.
 * @param   pHeap           The page heap.
 * @param   cPages          The number of pages to allocate.
 * @param   pszTag          The allocation tag.
 * @param   fFlags          RTMEMPAGEALLOC_F_XXX.
 * @param   ppv             Where to return the pointer to the pages.
 */
static int RTHeapPageAlloc(PRTHEAPPAGE pHeap, size_t cPages, const char *pszTag, uint32_t fFlags, void **ppv)
{
    /*
     * Validate input.
     */
    AssertPtr(ppv);
    *ppv = NULL;
    AssertPtrReturn(pHeap, VERR_INVALID_HANDLE);
    AssertReturn(pHeap->u32Magic == RTHEAPPAGE_MAGIC, VERR_INVALID_HANDLE);
    AssertMsgReturn(cPages < RTMEMPAGE_BLOCK_SIZE, ("%#zx\n", cPages), VERR_OUT_OF_RANGE);

    /*
     * Grab the lock and call a worker with many returns.
     */
    int rc = RTCritSectEnter(&pHeap->CritSect);
    if (RT_SUCCESS(rc))
    {
        rc = rtHeapPageAllocLocked(pHeap, cPages, pszTag, fFlags, ppv);
        RTCritSectLeave(&pHeap->CritSect);
    }

    return rc;
}


/**
 * RTAvlrPVDoWithAll callback.
 *
 * @returns 0 to continue the enum, non-zero to quit it.
 * @param   pNode           The node.
 * @param   pvUser          Pointer to a block pointer variable. For returning
 *                          the address of the block to be freed.
 */
static DECLCALLBACK(int) rtHeapPageFindUnusedBlockCallback(PAVLRPVNODECORE pNode, void *pvUser)
{
    PRTHEAPPAGEBLOCK pBlock = RT_FROM_MEMBER(pNode, RTHEAPPAGEBLOCK, Core);
    if (pBlock->cFreePages == RTMEMPAGE_BLOCK_PAGE_COUNT)
    {
        *(PRTHEAPPAGEBLOCK *)pvUser = pBlock;
        return 1;
    }
    return 0;
}


/**
 * Frees an allocation.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_FOUND if pv isn't within any of the memory blocks in the
 *          heap.
 * @retval  VERR_INVALID_POINTER if the given memory range isn't exactly one
 *          allocation block.
 * @param   pHeap           The page heap.
 * @param   pv              Pointer to what RTHeapPageAlloc returned.
 * @param   cPages          The number of pages that was allocated.
 */
static int RTHeapPageFree(PRTHEAPPAGE pHeap, void *pv, size_t cPages)
{
    /*
     * Validate input.
     */
    if (!pv)
        return VINF_SUCCESS;
    AssertPtrReturn(pHeap, VERR_INVALID_HANDLE);
    AssertReturn(pHeap->u32Magic == RTHEAPPAGE_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Grab the lock and look up the page.
     */
    int rc = RTCritSectEnter(&pHeap->CritSect);
    if (RT_SUCCESS(rc))
    {
        PRTHEAPPAGEBLOCK pBlock = (PRTHEAPPAGEBLOCK)RTAvlrPVRangeGet(&pHeap->BlockTree, pv);
        if (pBlock)
        {
            /*
             * Validate the specified address range.
             */
            uint32_t const iPage = (uint32_t)(((uintptr_t)pv - (uintptr_t)pBlock->Core.Key) >> PAGE_SHIFT);
            /* Check the range is within the block. */
            bool fOk = iPage + cPages <= RTMEMPAGE_BLOCK_PAGE_COUNT;
            /* Check that it's the start of an allocation. */
            fOk = fOk && ASMBitTest(&pBlock->bmFirst[0], iPage);
            /* Check that the range ends at an allocation boundrary. */
            fOk = fOk && (   iPage + cPages == RTMEMPAGE_BLOCK_PAGE_COUNT
                          || ASMBitTest(&pBlock->bmFirst[0], iPage + (uint32_t)cPages)
                          || !ASMBitTest(&pBlock->bmAlloc[0], iPage + (uint32_t)cPages));
            /* Check the other pages. */
            uint32_t const iLastPage = iPage + (uint32_t)cPages - 1;
            for (uint32_t i = iPage + 1; i < iLastPage && fOk; i++)
                fOk = ASMBitTest(&pBlock->bmAlloc[0], i)
                   && !ASMBitTest(&pBlock->bmFirst[0], i);
            if (fOk)
            {
                /*
                 * Free the memory.
                 */
                uint32_t fRevert = (ASMBitTest(&pBlock->bmLockedAdviced[0], iPage) ? RTMEMPAGEALLOC_F_ADVISE_LOCKED  : 0)
                                 | (ASMBitTest(&pBlock->bmNoDumpAdviced[0], iPage) ? RTMEMPAGEALLOC_F_ADVISE_NO_DUMP : 0);
                if (fRevert)
                {
                    rtMemPageNativeRevertFlags(pv, cPages << PAGE_SHIFT, fRevert);
                    ASMBitClearRange(&pBlock->bmLockedAdviced[0], iPage, iPage + cPages);
                    ASMBitClearRange(&pBlock->bmNoDumpAdviced[0], iPage, iPage + cPages);
                }
                ASMBitClearRange(&pBlock->bmAlloc[0], iPage, iPage + cPages);
                ASMBitClear(&pBlock->bmFirst[0], iPage);
                pBlock->cFreePages += (uint32_t)cPages;
                pHeap->cFreePages  += (uint32_t)cPages;
                pHeap->cFreeCalls++;
                if (!pHeap->pHint1 || pHeap->pHint1->cFreePages < pBlock->cFreePages)
                    pHeap->pHint1 = pBlock;

                /** @todo Add bitmaps for tracking madvice and mlock so we can undo those. */

                /*
                 * Shrink the heap. Not very efficient because of the AVL tree.
                 */
                if (   pHeap->cFreePages >= RTMEMPAGE_BLOCK_PAGE_COUNT * 3
                    && pHeap->cFreePages >= pHeap->cHeapPages / 2 /* 50% free */
                    && pHeap->cFreeCalls - pHeap->uLastMinimizeCall > RTMEMPAGE_BLOCK_PAGE_COUNT
                   )
                {
                    uint32_t cFreePageTarget = pHeap->cHeapPages / 4; /* 25% free */
                    while (pHeap->cFreePages > cFreePageTarget)
                    {
                        pHeap->uLastMinimizeCall = pHeap->cFreeCalls;

                        pBlock = NULL;
                        RTAvlrPVDoWithAll(&pHeap->BlockTree, false /*fFromLeft*/,
                                          rtHeapPageFindUnusedBlockCallback, &pBlock);
                        if (!pBlock)
                            break;

                        void *pv2 = RTAvlrPVRemove(&pHeap->BlockTree, pBlock->Core.Key); Assert(pv2); NOREF(pv2);
                        pHeap->cHeapPages -= RTMEMPAGE_BLOCK_PAGE_COUNT;
                        pHeap->cFreePages -= RTMEMPAGE_BLOCK_PAGE_COUNT;
                        pHeap->pHint1      = NULL;
                        pHeap->pHint2      = NULL;
                        RTCritSectLeave(&pHeap->CritSect);

                        rtMemPageNativeFree(pBlock->Core.Key, RTMEMPAGE_BLOCK_SIZE);
                        pBlock->Core.Key = pBlock->Core.KeyLast = NULL;
                        pBlock->cFreePages = 0;
                        rtHeapPageIntBlockAllocatorFree(pHeap, pBlock);

                        RTCritSectEnter(&pHeap->CritSect);
                    }
                }
            }
            else
                rc = VERR_INVALID_POINTER;
        }
        else
            rc = VERR_NOT_FOUND; /* Distinct return code for this so RTMemPageFree and others can try alternative heaps. */

        RTCritSectLeave(&pHeap->CritSect);
    }

    return rc;
}


/**
 * Initializes the heap.
 *
 * @returns IPRT status code
 * @param   pvUser              Unused.
 */
static DECLCALLBACK(int) rtMemPageInitOnce(void *pvUser)
{
    NOREF(pvUser);
    int rc = RTHeapPageInit(&g_MemPageHeap, false /*fExec*/);
    if (RT_SUCCESS(rc))
    {
        rc = RTHeapPageInit(&g_MemExecHeap, true /*fExec*/);
        if (RT_SUCCESS(rc))
            return rc;
        RTHeapPageDelete(&g_MemPageHeap);
    }
    return rc;
}


/**
 * Allocates memory from the specified heap.
 *
 * @returns Address of the allocated memory.
 * @param   cb                  The number of bytes to allocate.
 * @param   pszTag              The tag.
 * @param   fFlags              RTMEMPAGEALLOC_F_XXX.
 * @param   pHeap               The heap to use.
 */
static void *rtMemPageAllocInner(size_t cb, const char *pszTag, uint32_t fFlags, PRTHEAPPAGE pHeap)
{
    /*
     * Validate & adjust the input.
     */
    Assert(cb > 0);
    NOREF(pszTag);
    cb = RT_ALIGN_Z(cb, PAGE_SIZE);

    /*
     * If the allocation is relatively large, we use mmap/VirtualAlloc/DosAllocMem directly.
     */
    void *pv = NULL; /* shut up gcc */
    if (cb >= RTMEMPAGE_NATIVE_THRESHOLD)
    {
        int rc = rtMemPageNativeAlloc(cb, fFlags, &pv);
        if (RT_SUCCESS(rc))
        {
            AssertPtr(pv);

            if (fFlags)
                rtMemPageApplyFlags(pv, cb, fFlags);
        }
        else
            pv = NULL;
    }
    else
    {
        int rc = RTOnce(&g_MemPageHeapInitOnce, rtMemPageInitOnce, NULL);
        if (RT_SUCCESS(rc))
            rc = RTHeapPageAlloc(pHeap, cb >> PAGE_SHIFT, pszTag, fFlags, &pv);
        if (RT_FAILURE(rc))
            pv = NULL;
    }

    return pv;
}


RTDECL(void *) RTMemPageAllocTag(size_t cb, const char *pszTag) RT_NO_THROW_DEF
{
    return rtMemPageAllocInner(cb, pszTag, 0, &g_MemPageHeap);
}


RTDECL(void *) RTMemPageAllocZTag(size_t cb, const char *pszTag) RT_NO_THROW_DEF
{
    return rtMemPageAllocInner(cb, pszTag, RTMEMPAGEALLOC_F_ZERO, &g_MemPageHeap);
}


RTDECL(void *) RTMemPageAllocExTag(size_t cb, uint32_t fFlags, const char *pszTag) RT_NO_THROW_DEF
{
    AssertReturn(!(fFlags & ~RTMEMPAGEALLOC_F_VALID_MASK), NULL);
    return rtMemPageAllocInner(cb, pszTag, fFlags,
                               !(fFlags & RTMEMPAGEALLOC_F_EXECUTABLE) ? &g_MemPageHeap : &g_MemExecHeap);
}


RTDECL(void) RTMemPageFree(void *pv, size_t cb) RT_NO_THROW_DEF
{
    /*
     * Validate & adjust the input.
     */
    if (!pv)
        return;
    AssertPtr(pv);
    Assert(cb > 0);
    Assert(!((uintptr_t)pv & PAGE_OFFSET_MASK));
    cb = RT_ALIGN_Z(cb, PAGE_SIZE);

    /*
     * If the allocation is relatively large, we used mmap/VirtualAlloc/DosAllocMem directly.
     */
    if (cb >= RTMEMPAGE_NATIVE_THRESHOLD)
        rtMemPageNativeFree(pv, cb);
    else
    {
        int rc = RTHeapPageFree(&g_MemPageHeap, pv, cb >> PAGE_SHIFT);
        if (rc == VERR_NOT_FOUND)
            rc = RTHeapPageFree(&g_MemExecHeap, pv, cb >> PAGE_SHIFT);
        AssertRC(rc);
    }
}

