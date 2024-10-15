/* $Id: VBoxGuestR0LibPhysHeap.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuestLibR0 - Physical memory heap.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/** @page pg_vbglr0_phys_heap   VBoxGuestLibR0 - Physical memory heap.
 *
 * Traditional heap implementation keeping all blocks in a ordered list and
 * keeping free blocks on additional list via pointers in the user area.  This
 * is similar to @ref grp_rt_heap_simple "RTHeapSimple" and
 * @ref grp_rt_heap_offset "RTHeapOffset" in IPRT, except that this code handles
 * mutiple chunks and has a physical address associated with each chunk and
 * block.  The alignment is fixed (VBGL_PH_ALLOC_ALIGN).
 *
 * When allocating memory, a free block is found that satisfies the request,
 * extending the heap with another chunk if needed.  The block is split if it's
 * too large, and the tail end is put on the free list.
 *
 * When freeing memory, the block being freed is put back on the free list and
 * we use the block list to check whether it can be merged with adjacent blocks.
 *
 * @note The original code managed the blocks in two separate lists for free and
 *       allocated blocks, which had the disadvantage only allowing merging with
 *       the block after the block being freed.  On the plus side, it had the
 *       potential for slightly better locality when examining the free list,
 *       since the next pointer and block size members were closer to one
 *       another.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "VBoxGuestR0LibInternal.h"

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/memobj.h>
#include <iprt/semaphore.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Enables heap dumping. */
#if defined(DOXYGEN_RUNNING) || 0
# define VBGL_PH_DUMPHEAP
#endif

#ifdef VBGL_PH_DUMPHEAP
# define VBGL_PH_DPRINTF(a)             RTAssertMsg2Weak a
#else
# define VBGL_PH_DPRINTF(a)             do { } while (0)
#endif

/** Heap chunk signature */
#define VBGL_PH_CHUNKSIGNATURE          UINT32_C(0xADDCCCCC)
/** Heap chunk allocation unit */
#define VBGL_PH_CHUNKSIZE               (0x10000)

/** Heap block signature */
#define VBGL_PH_BLOCKSIGNATURE          UINT32_C(0xADDBBBBB)

/** The allocation block alignment.
 *
 * This cannot be larger than VBGLPHYSHEAPBLOCK.
 */
#define VBGL_PH_ALLOC_ALIGN             (sizeof(void *))

/** Max number of free nodes to search before just using the best fit.
 *
 * This is used to limit the free list walking during allocation and just get
 * on with the job.  A low number should reduce the cache trashing at the
 * possible cost of heap fragmentation.
 *
 * Picked 16 after comparing the tstVbglR0PhysHeap-1 results w/ uRandSeed=42 for
 * different max values.
 */
#define VBGL_PH_MAX_FREE_SEARCH         16

/** Threshold to stop the block search if a free block is at least this much too big.
 *
 * May cause more fragmation (depending on usage pattern), but should speed up
 * allocation and hopefully reduce cache trashing.
 *
 * Since we merge adjacent free blocks when we can, free blocks should typically
 * be a lot larger that what's requested.  So, it is probably a good idea to
 * just chop up a large block rather than keep searching for a perfect-ish
 * match.
 *
 * Undefine this to disable this trick.
 */
#if defined(DOXYGEN_RUNNING) || 1
# define VBGL_PH_STOP_SEARCH_AT_EXCESS   _4K
#endif

/** Threshold at which to split out a tail free block when allocating.
 *
 * The value gives the amount of user space, i.e. excluding the header.
 *
 * Using 32 bytes based on VMMDev.h request sizes.  The smallest requests are 24
 * bytes, i.e. only the header, at least 4 of these.  There are at least 10 with
 * size 28 bytes and at least 11 with size 32 bytes.  So, 32 bytes would fit
 * some 25 requests out of about 60, which is reasonable.
 */
#define VBGL_PH_MIN_SPLIT_FREE_BLOCK    32


/** The smallest amount of user data that can be allocated.
 *
 * This is to ensure that the block can be converted into a
 * VBGLPHYSHEAPFREEBLOCK structure when freed.  This must be smaller or equal
 * to VBGL_PH_MIN_SPLIT_FREE_BLOCK.
 */
#define VBGL_PH_SMALLEST_ALLOC_SIZE     16

/** The maximum allocation request size. */
#define VBGL_PH_LARGEST_ALLOC_SIZE      RT_ALIGN_32(  _128M  \
                                                    - sizeof(VBGLPHYSHEAPBLOCK) \
                                                    - sizeof(VBGLPHYSHEAPCHUNK) \
                                                    - VBGL_PH_ALLOC_ALIGN, \
                                                    VBGL_PH_ALLOC_ALIGN)

/**
 * Whether to use the RTR0MemObjAllocCont API or RTMemContAlloc for
 * allocating chunks.
 *
 * This can be enabled on hosts where RTMemContAlloc is more complicated than
 * RTR0MemObjAllocCont.  This can also be done if we wish to save code space, as
 * the latter is typically always dragged into the link on guests where the
 * linker cannot eliminiate functions within objects.  Only drawback is that
 * RTR0MemObjAllocCont requires another heap allocation for the handle.
 *
 * Update: Just enable it everywhere so we can more easily use memory above 4G.
 */
#define VBGL_PH_USE_MEMOBJ


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * A heap block (within a chunk).
 *
 * This is used to track a part of a heap chunk that's either free or
 * allocated.  The VBGLPHYSHEAPBLOCK::fAllocated member indicates which it is.
 */
struct VBGLPHYSHEAPBLOCK
{
    /** Magic value (VBGL_PH_BLOCKSIGNATURE). */
    uint32_t u32Signature;

    /** Size of user data in the block. Does not include this block header. */
    uint32_t cbUser : 31;
    /** The top bit indicates whether it's allocated or free. */
    uint32_t fAllocated : 1;

    /** Pointer to the next block on the list. */
    VBGLPHYSHEAPBLOCK  *pNext;
    /** Pointer to the previous block on the list. */
    VBGLPHYSHEAPBLOCK  *pPrev;
    /** Pointer back to the chunk. */
    VBGLPHYSHEAPCHUNK  *pChunk;
};

/**
 * A free block.
 */
struct VBGLPHYSHEAPFREEBLOCK
{
    /** Core block data. */
    VBGLPHYSHEAPBLOCK       Core;
    /** Pointer to the next free list entry. */
    VBGLPHYSHEAPFREEBLOCK  *pNextFree;
    /** Pointer to the previous free list entry. */
    VBGLPHYSHEAPFREEBLOCK  *pPrevFree;
};
AssertCompile(VBGL_PH_SMALLEST_ALLOC_SIZE  >= sizeof(VBGLPHYSHEAPFREEBLOCK) - sizeof(VBGLPHYSHEAPBLOCK));
AssertCompile(VBGL_PH_MIN_SPLIT_FREE_BLOCK >= sizeof(VBGLPHYSHEAPFREEBLOCK) - sizeof(VBGLPHYSHEAPBLOCK));
AssertCompile(VBGL_PH_MIN_SPLIT_FREE_BLOCK >= VBGL_PH_SMALLEST_ALLOC_SIZE);

/**
 * A chunk of memory used by the heap for sub-allocations.
 *
 * There is a list of these.
 */
struct VBGLPHYSHEAPCHUNK
{
    /** Magic value (VBGL_PH_CHUNKSIGNATURE). */
    uint32_t u32Signature;
    /** Size of the chunk. Includes the chunk header. */
    uint32_t cbChunk;
    /** Number of block of any kind. */
    int32_t  cBlocks;
    /** Number of free blocks. */
    int32_t  cFreeBlocks;

    /** Physical address of the chunk (contiguous). */
    RTCCPHYS physAddr;

    /** Pointer to the next chunk. */
    VBGLPHYSHEAPCHUNK  *pNext;
    /** Pointer to the previous chunk. */
    VBGLPHYSHEAPCHUNK  *pPrev;

#if defined(VBGL_PH_USE_MEMOBJ)
    /** The allocation handle. */
    RTR0MEMOBJ          hMemObj;
#endif
#if ARCH_BITS == 64
    /** Pad the size up to 64 bytes. */
# ifdef VBGL_PH_USE_MEMOBJ
    uintptr_t           auPadding2[2];
# else
    uintptr_t           auPadding2[3];
# endif
#endif
};
#if ARCH_BITS == 64
AssertCompileSize(VBGLPHYSHEAPCHUNK, 64);
#endif


/**
 * Debug function that dumps the heap.
 */
#ifndef VBGL_PH_DUMPHEAP
# define dumpheap(pszWhere) do { } while (0)
#else
static void dumpheap(const char *pszWhere)
{
   VBGL_PH_DPRINTF(("VBGL_PH dump at '%s'\n", pszWhere));

   VBGL_PH_DPRINTF(("Chunks:\n"));
   for (VBGLPHYSHEAPCHUNK *pChunk = g_vbgldata.pChunkHead; pChunk; pChunk = pChunk->pNext)
       VBGL_PH_DPRINTF(("%p: pNext = %p, pPrev = %p, sign = %08X, size = %8d, cBlocks = %8d, cFreeBlocks=%8d, phys = %08X\n",
                        pChunk, pChunk->pNext, pChunk->pPrev, pChunk->u32Signature, pChunk->cbChunk,
                        pChunk->cBlocks, pChunk->cFreeBlocks, pChunk->physAddr));

   VBGL_PH_DPRINTF(("Allocated blocks:\n"));
   for (VBGLPHYSHEAPBLOCK *pBlock = g_vbgldata.pBlockHead; pBlock; pBlock = pBlock->pNext)
       VBGL_PH_DPRINTF(("%p: pNext = %p, pPrev = %p, size = %05x, sign = %08X, %s, pChunk = %p\n",
                        pBlock, pBlock->pNext, pBlock->pPrev, pBlock->cbUser,
                        pBlock->u32Signature,  pBlock->fAllocated ? "allocated" : "     free", pBlock->pChunk));

   VBGL_PH_DPRINTF(("Free blocks:\n"));
   for (VBGLPHYSHEAPFREEBLOCK *pBlock = g_vbgldata.pFreeHead; pBlock; pBlock = pBlock->pNextFree)
       VBGL_PH_DPRINTF(("%p: pNextFree = %p, pPrevFree = %p, size = %05x, sign = %08X, pChunk = %p%s\n",
                        pBlock, pBlock->pNextFree, pBlock->pPrevFree, pBlock->Core.cbUser,
                        pBlock->Core.u32Signature, pBlock->Core.pChunk,
                        !pBlock->Core.fAllocated ? "" : " !!allocated-block-on-freelist!!"));

   VBGL_PH_DPRINTF(("VBGL_PH dump at '%s' done\n", pszWhere));
}
#endif


/**
 * Initialize a free block
 */
static void vbglPhysHeapInitFreeBlock(VBGLPHYSHEAPFREEBLOCK *pBlock, VBGLPHYSHEAPCHUNK *pChunk, uint32_t cbUser)
{
    Assert(pBlock != NULL);
    Assert(pChunk != NULL);

    pBlock->Core.u32Signature = VBGL_PH_BLOCKSIGNATURE;
    pBlock->Core.cbUser       = cbUser;
    pBlock->Core.fAllocated   = false;
    pBlock->Core.pNext        = NULL;
    pBlock->Core.pPrev        = NULL;
    pBlock->Core.pChunk       = pChunk;
    pBlock->pNextFree         = NULL;
    pBlock->pPrevFree         = NULL;
}


/**
 * Updates block statistics when a block is added.
 */
DECLINLINE(void) vbglPhysHeapStatsBlockAdded(VBGLPHYSHEAPBLOCK *pBlock)
{
    g_vbgldata.cBlocks      += 1;
    pBlock->pChunk->cBlocks += 1;
    AssertMsg((uint32_t)pBlock->pChunk->cBlocks <= pBlock->pChunk->cbChunk / sizeof(VBGLPHYSHEAPFREEBLOCK),
              ("pChunk=%p: cbChunk=%#x cBlocks=%d\n", pBlock->pChunk, pBlock->pChunk->cbChunk, pBlock->pChunk->cBlocks));
}


/**
 * Links @a pBlock onto the head of block list.
 *
 * This also update the per-chunk block counts.
 */
static void vbglPhysHeapInsertBlock(VBGLPHYSHEAPBLOCK *pBlock)
{
    AssertMsg(pBlock->pNext == NULL, ("pBlock->pNext = %p\n", pBlock->pNext));
    AssertMsg(pBlock->pPrev == NULL, ("pBlock->pPrev = %p\n", pBlock->pPrev));

    /* inserting to head of list */
    VBGLPHYSHEAPBLOCK *pOldHead = g_vbgldata.pBlockHead;

    pBlock->pNext = pOldHead;
    pBlock->pPrev = NULL;

    if (pOldHead)
        pOldHead->pPrev = pBlock;
    g_vbgldata.pBlockHead = pBlock;

    /* Update the stats: */
    vbglPhysHeapStatsBlockAdded(pBlock);
}


/**
 * Links @a pBlock onto the block list after @a pInsertAfter.
 *
 * This also update the per-chunk block counts.
 */
static void vbglPhysHeapInsertBlockAfter(VBGLPHYSHEAPBLOCK *pBlock, VBGLPHYSHEAPBLOCK *pInsertAfter)
{
    AssertMsg(pBlock->pNext == NULL, ("pBlock->pNext = %p\n", pBlock->pNext));
    AssertMsg(pBlock->pPrev == NULL, ("pBlock->pPrev = %p\n", pBlock->pPrev));

    pBlock->pNext = pInsertAfter->pNext;
    pBlock->pPrev = pInsertAfter;

    if (pInsertAfter->pNext)
        pInsertAfter->pNext->pPrev = pBlock;

    pInsertAfter->pNext = pBlock;

    /* Update the stats: */
    vbglPhysHeapStatsBlockAdded(pBlock);
}


/**
 * Unlinks @a pBlock from the block list.
 *
 * This also update the per-chunk block counts.
 */
static void vbglPhysHeapUnlinkBlock(VBGLPHYSHEAPBLOCK *pBlock)
{
    VBGLPHYSHEAPBLOCK *pOtherBlock = pBlock->pNext;
    if (pOtherBlock)
        pOtherBlock->pPrev = pBlock->pPrev;
    /* else: this is tail of list but we do not maintain tails of block lists. so nothing to do. */

    pOtherBlock = pBlock->pPrev;
    if (pOtherBlock)
        pOtherBlock->pNext = pBlock->pNext;
    else
    {
        Assert(g_vbgldata.pBlockHead == pBlock);
        g_vbgldata.pBlockHead = pBlock->pNext;
    }

    pBlock->pNext = NULL;
    pBlock->pPrev = NULL;

    /* Update the stats: */
    g_vbgldata.cBlocks      -= 1;
    pBlock->pChunk->cBlocks -= 1;
    AssertMsg(pBlock->pChunk->cBlocks >= 0,
              ("pChunk=%p: cbChunk=%#x cBlocks=%d\n", pBlock->pChunk, pBlock->pChunk->cbChunk, pBlock->pChunk->cBlocks));
    Assert(g_vbgldata.cBlocks >= 0);
}



/**
 * Updates statistics after adding a free block.
 */
DECLINLINE(void) vbglPhysHeapStatsFreeBlockAdded(VBGLPHYSHEAPFREEBLOCK *pBlock)
{
    g_vbgldata.cFreeBlocks           += 1;
    pBlock->Core.pChunk->cFreeBlocks += 1;
}


/**
 * Links @a pBlock onto head of the free chain.
 *
 * This is used during block freeing and when adding a new chunk.
 *
 * This also update the per-chunk block counts.
 */
static void vbglPhysHeapInsertFreeBlock(VBGLPHYSHEAPFREEBLOCK *pBlock)
{
    Assert(!pBlock->Core.fAllocated);
    AssertMsg(pBlock->pNextFree == NULL, ("pBlock->pNextFree = %p\n", pBlock->pNextFree));
    AssertMsg(pBlock->pPrevFree == NULL, ("pBlock->pPrevFree = %p\n", pBlock->pPrevFree));

    /* inserting to head of list */
    VBGLPHYSHEAPFREEBLOCK *pOldHead = g_vbgldata.pFreeHead;

    pBlock->pNextFree = pOldHead;
    pBlock->pPrevFree = NULL;

    if (pOldHead)
        pOldHead->pPrevFree = pBlock;
    g_vbgldata.pFreeHead = pBlock;

    /* Update the stats: */
    vbglPhysHeapStatsFreeBlockAdded(pBlock);
}


/**
 * Links @a pBlock after @a pInsertAfter.
 *
 * This is used when splitting a free block during allocation to preserve the
 * place in the free list.
 *
 * This also update the per-chunk block counts.
 */
static void vbglPhysHeapInsertFreeBlockAfter(VBGLPHYSHEAPFREEBLOCK *pBlock, VBGLPHYSHEAPFREEBLOCK *pInsertAfter)
{
    Assert(!pBlock->Core.fAllocated);
    AssertMsg(pBlock->pNextFree == NULL, ("pBlock->pNextFree = %p\n", pBlock->pNextFree));
    AssertMsg(pBlock->pPrevFree == NULL, ("pBlock->pPrevFree = %p\n", pBlock->pPrevFree));

    /* inserting after the tiven node */
    pBlock->pNextFree = pInsertAfter->pNextFree;
    pBlock->pPrevFree = pInsertAfter;

    if (pInsertAfter->pNextFree)
        pInsertAfter->pNextFree->pPrevFree = pBlock;

    pInsertAfter->pNextFree = pBlock;

    /* Update the stats: */
    vbglPhysHeapStatsFreeBlockAdded(pBlock);
}


/**
 * Unlinks @a pBlock from the free list.
 *
 * This also update the per-chunk block counts.
 */
static void vbglPhysHeapUnlinkFreeBlock(VBGLPHYSHEAPFREEBLOCK *pBlock)
{
    Assert(!pBlock->Core.fAllocated);

    VBGLPHYSHEAPFREEBLOCK *pOtherBlock = pBlock->pNextFree;
    if (pOtherBlock)
        pOtherBlock->pPrevFree = pBlock->pPrevFree;
    /* else: this is tail of list but we do not maintain tails of block lists. so nothing to do. */

    pOtherBlock = pBlock->pPrevFree;
    if (pOtherBlock)
        pOtherBlock->pNextFree = pBlock->pNextFree;
    else
    {
        Assert(g_vbgldata.pFreeHead == pBlock);
        g_vbgldata.pFreeHead = pBlock->pNextFree;
    }

    pBlock->pNextFree = NULL;
    pBlock->pPrevFree = NULL;

    /* Update the stats: */
    g_vbgldata.cFreeBlocks      -= 1;
    pBlock->Core.pChunk->cFreeBlocks -= 1;
    AssertMsg(pBlock->Core.pChunk->cFreeBlocks >= 0,
              ("pChunk=%p: cbChunk=%#x cFreeBlocks=%d\n",
               pBlock->Core.pChunk, pBlock->Core.pChunk->cbChunk, pBlock->Core.pChunk->cFreeBlocks));
    Assert(g_vbgldata.cFreeBlocks >= 0);
}


/**
 * Allocate another chunk and add it to the heap.
 *
 * @returns Pointer to the free block in the new chunk on success, NULL on
 *          allocation failure.
 * @param   cbMinBlock  The size of the user block we need this chunk for.
 */
static VBGLPHYSHEAPFREEBLOCK *vbglPhysHeapChunkAlloc(uint32_t cbMinBlock)
{
    RTCCPHYS            PhysAddr = NIL_RTHCPHYS;
    VBGLPHYSHEAPCHUNK  *pChunk;
    uint32_t            cbChunk;
#ifdef VBGL_PH_USE_MEMOBJ
    RTR0MEMOBJ          hMemObj = NIL_RTR0MEMOBJ;
    int                 rc;
#endif

    VBGL_PH_DPRINTF(("Allocating new chunk for %#x byte allocation\n", cbMinBlock));
    AssertReturn(cbMinBlock <= VBGL_PH_LARGEST_ALLOC_SIZE, NULL); /* paranoia */

    /*
     * Compute the size of the new chunk, rounding up to next chunk size,
     * which must be power of 2.
     *
     * Note! Using VBGLPHYSHEAPFREEBLOCK here means the minimum block size is
     *       8 or 16 bytes too high, but safer this way since cbMinBlock is
     *       zero during the init code call.
     */
    Assert(RT_IS_POWER_OF_TWO(VBGL_PH_CHUNKSIZE));
    cbChunk = cbMinBlock + sizeof(VBGLPHYSHEAPCHUNK) + sizeof(VBGLPHYSHEAPFREEBLOCK);
    cbChunk = RT_ALIGN_32(cbChunk, VBGL_PH_CHUNKSIZE);

#ifdef VBGL_PH_USE_MEMOBJ
    rc = RTR0MemObjAllocCont(&hMemObj, cbChunk, g_vbgldata.HCPhysMax, false /*fExecutable*/);
    pChunk = (VBGLPHYSHEAPCHUNK *)(RT_SUCCESS(rc) ? RTR0MemObjAddress(hMemObj) : NULL);
    PhysAddr = RT_SUCCESS(rc) ? (RTCCPHYS)RTR0MemObjGetPagePhysAddr(hMemObj, 0 /*iPage*/) : NIL_RTCCPHYS;
#else
    pChunk = (VBGLPHYSHEAPCHUNK *)RTMemContAlloc(&PhysAddr, cbChunk);
#endif
    if (!pChunk)
    {
        /* If the allocation fail, halv the size till and try again. */
        uint32_t cbMinChunk = RT_MAX(cbMinBlock, PAGE_SIZE / 2) + sizeof(VBGLPHYSHEAPCHUNK) + sizeof(VBGLPHYSHEAPFREEBLOCK);
        cbMinChunk = RT_ALIGN_32(cbMinChunk, PAGE_SIZE);
        if (cbChunk > cbMinChunk)
            do
            {
                cbChunk >>= 2;
                cbChunk = RT_ALIGN_32(cbChunk, PAGE_SIZE);
#ifdef VBGL_PH_USE_MEMOBJ
                rc = RTR0MemObjAllocCont(&hMemObj, cbChunk, g_vbgldata.HCPhysMax, false /*fExecutable*/);
                pChunk = (VBGLPHYSHEAPCHUNK *)(RT_SUCCESS(rc) ? RTR0MemObjAddress(hMemObj) : NULL);
                PhysAddr = RT_SUCCESS(rc) ? (RTCCPHYS)RTR0MemObjGetPagePhysAddr(hMemObj, 0 /*iPage*/) : NIL_RTCCPHYS;
#else
                pChunk = (VBGLPHYSHEAPCHUNK *)RTMemContAlloc(&PhysAddr, cbChunk);
#endif
            } while (!pChunk && cbChunk > cbMinChunk);
    }
    if (pChunk)
    {
        VBGLPHYSHEAPCHUNK     *pOldHeadChunk;
        VBGLPHYSHEAPFREEBLOCK *pBlock;
        AssertRelease(   g_vbgldata.HCPhysMax == NIL_RTHCPHYS
                      || (PhysAddr < _4G && PhysAddr + cbChunk <= _4G));

        /*
         * Init the new chunk.
         */
        pChunk->u32Signature     = VBGL_PH_CHUNKSIGNATURE;
        pChunk->cbChunk          = cbChunk;
        pChunk->physAddr         = PhysAddr;
        pChunk->cBlocks          = 0;
        pChunk->cFreeBlocks      = 0;
        pChunk->pNext            = NULL;
        pChunk->pPrev            = NULL;
#ifdef VBGL_PH_USE_MEMOBJ
        pChunk->hMemObj          = hMemObj;
#endif

        /* Initialize the padding too: */
#if ARCH_BITS == 64
        pChunk->auPadding2[0]    = UINT64_C(0xADDCAAA3ADDCAAA2);
        pChunk->auPadding2[1]    = UINT64_C(0xADDCAAA5ADDCAAA4);
# ifndef VBGL_PH_USE_MEMOBJ
        pChunk->auPadding2[2]    = UINT64_C(0xADDCAAA7ADDCAAA6);
# endif
#endif

        /*
         * Initialize the free block, which now occupies entire chunk.
         */
        pBlock = (VBGLPHYSHEAPFREEBLOCK *)(pChunk + 1);
        vbglPhysHeapInitFreeBlock(pBlock, pChunk, cbChunk - sizeof(VBGLPHYSHEAPCHUNK) - sizeof(VBGLPHYSHEAPBLOCK));
        vbglPhysHeapInsertBlock(&pBlock->Core);
        vbglPhysHeapInsertFreeBlock(pBlock);

        /*
         * Add the chunk to the list.
         */
        pOldHeadChunk = g_vbgldata.pChunkHead;
        pChunk->pNext = pOldHeadChunk;
        if (pOldHeadChunk)
            pOldHeadChunk->pPrev = pChunk;
        g_vbgldata.pChunkHead    = pChunk;

        VBGL_PH_DPRINTF(("Allocated chunk %p LB %#x, block %p LB %#x\n", pChunk, cbChunk, pBlock, pBlock->Core.cbUser));
        return pBlock;
    }
    LogRel(("vbglPhysHeapChunkAlloc: failed to alloc %u (%#x) contiguous bytes.\n", cbChunk, cbChunk));
    return NULL;
}


/**
 * Deletes a chunk: Unlinking all its blocks and freeing its memory.
 */
static void vbglPhysHeapChunkDelete(VBGLPHYSHEAPCHUNK *pChunk)
{
    uintptr_t uEnd, uCur;
    Assert(pChunk != NULL);
    AssertMsg(pChunk->u32Signature == VBGL_PH_CHUNKSIGNATURE, ("pChunk->u32Signature = %08X\n", pChunk->u32Signature));

    VBGL_PH_DPRINTF(("Deleting chunk %p size %x\n", pChunk, pChunk->cbChunk));

    /*
     * First scan the chunk and unlink all blocks from the lists.
     *
     * Note! We could do this by finding the first and last block list entries
     *       and just drop the whole chain relating to this chunk, rather than
     *       doing it one by one.  But doing it one by one is simpler and will
     *       continue to work if the block list ends in an unsorted state.
     */
    uEnd = (uintptr_t)pChunk + pChunk->cbChunk;
    uCur = (uintptr_t)(pChunk + 1);

    while (uCur < uEnd)
    {
        VBGLPHYSHEAPBLOCK *pBlock = (VBGLPHYSHEAPBLOCK *)uCur;
        Assert(pBlock->u32Signature == VBGL_PH_BLOCKSIGNATURE);
        Assert(pBlock->pChunk == pChunk);

        uCur += pBlock->cbUser + sizeof(VBGLPHYSHEAPBLOCK);
        Assert(uCur == (uintptr_t)pBlock->pNext || uCur >= uEnd);

        if (!pBlock->fAllocated)
            vbglPhysHeapUnlinkFreeBlock((VBGLPHYSHEAPFREEBLOCK *)pBlock);
        vbglPhysHeapUnlinkBlock(pBlock);
    }

    AssertMsg(uCur == uEnd, ("uCur = %p, uEnd = %p, pChunk->cbChunk = %08X\n", uCur, uEnd, pChunk->cbChunk));

    /*
     * Unlink the chunk from the chunk list.
     */
    if (pChunk->pNext)
        pChunk->pNext->pPrev = pChunk->pPrev;
    /* else: we do not maintain tail pointer. */

    if (pChunk->pPrev)
        pChunk->pPrev->pNext = pChunk->pNext;
    else
    {
        Assert(g_vbgldata.pChunkHead == pChunk);
        g_vbgldata.pChunkHead = pChunk->pNext;
    }

    /*
     * Finally, free the chunk memory.
     */
#ifdef VBGL_PH_USE_MEMOBJ
    RTR0MemObjFree(pChunk->hMemObj, true /*fFreeMappings*/);
#else
    RTMemContFree(pChunk, pChunk->cbChunk);
#endif
}


DECLR0VBGL(void *) VbglR0PhysHeapAlloc(uint32_t cb)
{
    VBGLPHYSHEAPFREEBLOCK  *pBlock;
    VBGLPHYSHEAPFREEBLOCK  *pIter;
    int32_t                 cLeft;
#ifdef VBGL_PH_STOP_SEARCH_AT_EXCESS
    uint32_t                cbAlwaysSplit;
#endif
    int                     rc;

    /*
     * Make sure we don't allocate anything too small to turn into a free node
     * and align the size to prevent pointer misalignment and whatnot.
     */
    cb = RT_MAX(cb, VBGL_PH_SMALLEST_ALLOC_SIZE);
    cb = RT_ALIGN_32(cb, VBGL_PH_ALLOC_ALIGN);
    AssertCompile(VBGL_PH_ALLOC_ALIGN <= sizeof(pBlock->Core));

    rc = RTSemFastMutexRequest(g_vbgldata.hMtxHeap);
    AssertRCReturn(rc, NULL);

    dumpheap("pre alloc");

    /*
     * Search the free list.  We do this in linear fashion as we don't expect
     * there to be many blocks in the heap.
     */
#ifdef VBGL_PH_STOP_SEARCH_AT_EXCESS
    cbAlwaysSplit = cb + VBGL_PH_STOP_SEARCH_AT_EXCESS;
#endif
    cLeft         = VBGL_PH_MAX_FREE_SEARCH;
    pBlock        = NULL;
    if (cb <= PAGE_SIZE / 4 * 3)
    {
        /* Smaller than 3/4 page:  Prefer a free block that can keep the request within a single page,
           so HGCM processing in VMMDev can use page locks instead of several reads and writes. */
        VBGLPHYSHEAPFREEBLOCK *pFallback = NULL;
        for (pIter = g_vbgldata.pFreeHead; pIter != NULL; pIter = pIter->pNextFree, cLeft--)
        {
            AssertBreak(pIter->Core.u32Signature == VBGL_PH_BLOCKSIGNATURE);
            if (pIter->Core.cbUser >= cb)
            {
                if (pIter->Core.cbUser == cb)
                {
                    if (PAGE_SIZE - ((uintptr_t)(pIter + 1) & PAGE_OFFSET_MASK) >= cb)
                    {
                        pBlock = pIter;
                        break;
                    }
                    pFallback = pIter;
                }
                else
                {
                    if (!pFallback || pIter->Core.cbUser < pFallback->Core.cbUser)
                        pFallback = pIter;
                    if (PAGE_SIZE - ((uintptr_t)(pIter + 1) & PAGE_OFFSET_MASK) >= cb)
                    {
                        if (!pBlock || pIter->Core.cbUser < pBlock->Core.cbUser)
                            pBlock = pIter;
#ifdef VBGL_PH_STOP_SEARCH_AT_EXCESS
                        else if (pIter->Core.cbUser >= cbAlwaysSplit)
                        {
                            pBlock = pIter;
                            break;
                        }
#endif
                    }
                }

                if (cLeft > 0)
                { /* likely */ }
                else
                    break;
            }
        }

        if (!pBlock)
            pBlock = pFallback;
    }
    else
    {
        /* Large than 3/4 page:  Find closest free list match. */
        for (pIter = g_vbgldata.pFreeHead; pIter != NULL; pIter = pIter->pNextFree, cLeft--)
        {
            AssertBreak(pIter->Core.u32Signature == VBGL_PH_BLOCKSIGNATURE);
            if (pIter->Core.cbUser >= cb)
            {
                if (pIter->Core.cbUser == cb)
                {
                    /* Exact match - we're done! */
                    pBlock = pIter;
                    break;
                }

#ifdef VBGL_PH_STOP_SEARCH_AT_EXCESS
                if (pIter->Core.cbUser >= cbAlwaysSplit)
                {
                    /* Really big block - no point continue searching! */
                    pBlock = pIter;
                    break;
                }
#endif
                /* Looking for a free block with nearest size. */
                if (!pBlock || pIter->Core.cbUser < pBlock->Core.cbUser)
                    pBlock = pIter;

                if (cLeft > 0)
                { /* likely */ }
                else
                    break;
            }
        }
    }

    if (!pBlock)
    {
        /* No free blocks, allocate a new chunk, the only free block of the
           chunk will be returned. */
        pBlock = vbglPhysHeapChunkAlloc(cb);
    }

    if (pBlock)
    {
        /* We have a free block, either found or allocated. */
        AssertMsg(pBlock->Core.u32Signature == VBGL_PH_BLOCKSIGNATURE,
                  ("pBlock = %p, pBlock->u32Signature = %08X\n", pBlock, pBlock->Core.u32Signature));
        AssertMsg(!pBlock->Core.fAllocated, ("pBlock = %p\n", pBlock));

        /*
         * If the block is too large, split off a free block with the unused space.
         *
         * We do this before unlinking the block so we can preserve the location
         * in the free list.
         *
         * Note! We cannot split off and return the tail end here, because that may
         *       violate the same page requirements for requests smaller than 3/4 page.
         */
        AssertCompile(VBGL_PH_MIN_SPLIT_FREE_BLOCK >= sizeof(*pBlock) - sizeof(pBlock->Core));
        if (pBlock->Core.cbUser >= sizeof(VBGLPHYSHEAPBLOCK) * 2 + VBGL_PH_MIN_SPLIT_FREE_BLOCK + cb)
        {
            pIter = (VBGLPHYSHEAPFREEBLOCK *)((uintptr_t)(&pBlock->Core + 1) + cb);
            vbglPhysHeapInitFreeBlock(pIter, pBlock->Core.pChunk, pBlock->Core.cbUser - cb - sizeof(VBGLPHYSHEAPBLOCK));

            pBlock->Core.cbUser = cb;

            /* Insert the new 'pIter' block after the 'pBlock' in the block list
               and in the free list. */
            vbglPhysHeapInsertBlockAfter(&pIter->Core, &pBlock->Core);
            vbglPhysHeapInsertFreeBlockAfter(pIter, pBlock);
        }

        /*
         * Unlink the block from the free list and mark it as allocated.
         */
        vbglPhysHeapUnlinkFreeBlock(pBlock);
        pBlock->Core.fAllocated = true;

        dumpheap("post alloc");

        /*
         * Return success.
         */
        rc = RTSemFastMutexRelease(g_vbgldata.hMtxHeap);

        VBGL_PH_DPRINTF(("VbglR0PhysHeapAlloc: returns %p size %x\n", pBlock + 1, pBlock->Core.cbUser));
        return &pBlock->Core + 1;
    }

    /*
     * Return failure.
     */
    rc = RTSemFastMutexRelease(g_vbgldata.hMtxHeap);
    AssertRC(rc);

    VBGL_PH_DPRINTF(("VbglR0PhysHeapAlloc: returns NULL (requested %#x bytes)\n", cb));
    return NULL;
}


DECLR0VBGL(RTCCPHYS) VbglR0PhysHeapGetPhysAddr(void *pv)
{
    /*
     * Validate the incoming pointer.
     */
    if (pv != NULL)
    {
        VBGLPHYSHEAPBLOCK *pBlock = (VBGLPHYSHEAPBLOCK *)pv - 1;
        if (   pBlock->u32Signature == VBGL_PH_BLOCKSIGNATURE
            && pBlock->fAllocated)
        {
            /*
             * Calculate and return its physical address.
             */
            VBGLPHYSHEAPCHUNK  *pChunk = pBlock->pChunk;
            return pChunk->physAddr + (uint32_t)((uintptr_t)pv - (uintptr_t)pChunk);
        }

        AssertMsgFailed(("Use after free or corrupt pointer variable: pv=%p pBlock=%p: u32Signature=%#x cb=%#x fAllocated=%d\n",
                         pv, pBlock, pBlock->u32Signature, pBlock->cbUser, pBlock->fAllocated));
    }
    else
        AssertMsgFailed(("Unexpected NULL pointer\n"));
    return 0;
}


DECLR0VBGL(void) VbglR0PhysHeapFree(void *pv)
{
    if (pv != NULL)
    {
        VBGLPHYSHEAPFREEBLOCK *pBlock;

        int rc = RTSemFastMutexRequest(g_vbgldata.hMtxHeap);
        AssertRCReturnVoid(rc);

        dumpheap("pre free");

        /*
         * Validate the block header.
         */
        pBlock = (VBGLPHYSHEAPFREEBLOCK *)((VBGLPHYSHEAPBLOCK *)pv - 1);
        if (   pBlock->Core.u32Signature == VBGL_PH_BLOCKSIGNATURE
            && pBlock->Core.fAllocated
            && pBlock->Core.cbUser >= VBGL_PH_SMALLEST_ALLOC_SIZE)
        {
            VBGLPHYSHEAPCHUNK *pChunk;
            VBGLPHYSHEAPBLOCK *pNeighbour;

            /*
             * Change the block status to freeed.
             */
            VBGL_PH_DPRINTF(("VbglR0PhysHeapFree: %p size %#x\n", pv, pBlock->Core.cbUser));

            pBlock->Core.fAllocated = false;
            pBlock->pNextFree = pBlock->pPrevFree = NULL;
            vbglPhysHeapInsertFreeBlock(pBlock);

            dumpheap("post insert");

            /*
             * Check if the block after this one is also free and we can merge it into this one.
             */
            pChunk = pBlock->Core.pChunk;

            pNeighbour = pBlock->Core.pNext;
            if (   pNeighbour
                && !pNeighbour->fAllocated
                && pNeighbour->pChunk == pChunk)
            {
                Assert((uintptr_t)pBlock + sizeof(pBlock->Core) + pBlock->Core.cbUser == (uintptr_t)pNeighbour);

                /* Adjust size of current memory block */
                pBlock->Core.cbUser += pNeighbour->cbUser + sizeof(VBGLPHYSHEAPBLOCK);

                /* Unlink the following node and invalid it. */
                vbglPhysHeapUnlinkFreeBlock((VBGLPHYSHEAPFREEBLOCK *)pNeighbour);
                vbglPhysHeapUnlinkBlock(pNeighbour);

                pNeighbour->u32Signature = ~VBGL_PH_BLOCKSIGNATURE;
                pNeighbour->cbUser       = UINT32_MAX / 4;

                dumpheap("post merge after");
            }

            /*
             * Same check for the block before us.  This invalidates pBlock.
             */
            pNeighbour = pBlock->Core.pPrev;
            if (   pNeighbour
                && !pNeighbour->fAllocated
                && pNeighbour->pChunk == pChunk)
            {
                Assert((uintptr_t)pNeighbour + sizeof(*pNeighbour) + pNeighbour->cbUser == (uintptr_t)pBlock);

                /* Adjust size of the block before us */
                pNeighbour->cbUser += pBlock->Core.cbUser + sizeof(VBGLPHYSHEAPBLOCK);

                /* Unlink this node and invalid it. */
                vbglPhysHeapUnlinkFreeBlock(pBlock);
                vbglPhysHeapUnlinkBlock(&pBlock->Core);

                pBlock->Core.u32Signature = ~VBGL_PH_BLOCKSIGNATURE;
                pBlock->Core.cbUser       = UINT32_MAX / 8;

                pBlock = NULL; /* invalid */

                dumpheap("post merge before");
            }

            /*
             * If this chunk is now completely unused, delete it if there are
             * more completely free ones.
             */
            if (   pChunk->cFreeBlocks == pChunk->cBlocks
                && (pChunk->pPrev || pChunk->pNext))
            {
                VBGLPHYSHEAPCHUNK *pCurChunk;
                uint32_t           cUnusedChunks = 0;
                for (pCurChunk = g_vbgldata.pChunkHead; pCurChunk; pCurChunk = pCurChunk->pNext)
                {
                    AssertBreak(pCurChunk->u32Signature == VBGL_PH_CHUNKSIGNATURE);
                    if (pCurChunk->cFreeBlocks == pCurChunk->cBlocks)
                    {
                        cUnusedChunks++;
                        if (cUnusedChunks > 1)
                        {
                            /* Delete current chunk, it will also unlink all free blocks
                             * remaining in the chunk from the free list, so the pBlock
                             * will also be invalid after this.
                             */
                            vbglPhysHeapChunkDelete(pChunk);
                            pBlock = NULL;      /* invalid */
                            pChunk = NULL;
                            pNeighbour = NULL;
                            break;
                        }
                    }
                }
            }

            dumpheap("post free");
        }
        else
            AssertMsgFailed(("pBlock: %p: u32Signature=%#x cb=%#x fAllocated=%d - double free?\n",
                             pBlock, pBlock->Core.u32Signature, pBlock->Core.cbUser, pBlock->Core.fAllocated));

        rc = RTSemFastMutexRelease(g_vbgldata.hMtxHeap);
        AssertRC(rc);
    }
}

#ifdef IN_TESTCASE /* For the testcase only */

/**
 * Returns the sum of all free heap blocks.
 *
 * This is the amount of memory you can theoretically allocate if you do
 * allocations exactly matching the free blocks.
 *
 * @returns The size of the free blocks.
 * @returns 0 if heap was safely detected as being bad.
 */
DECLVBGL(size_t) VbglR0PhysHeapGetFreeSize(void)
{
    int rc = RTSemFastMutexRequest(g_vbgldata.hMtxHeap);
    AssertRCReturn(rc, 0);

    size_t cbTotal = 0;
    for (VBGLPHYSHEAPFREEBLOCK *pCurBlock = g_vbgldata.pFreeHead; pCurBlock; pCurBlock = pCurBlock->pNextFree)
    {
        Assert(pCurBlock->Core.u32Signature == VBGL_PH_BLOCKSIGNATURE);
        Assert(!pCurBlock->Core.fAllocated);
        cbTotal += pCurBlock->Core.cbUser;
    }

    RTSemFastMutexRelease(g_vbgldata.hMtxHeap);
    return cbTotal;
}


/**
 * Checks the heap, caller responsible for locking.
 *
 * @returns VINF_SUCCESS if okay, error status if not.
 * @param   pErrInfo    Where to return more error details, optional.
 */
static int vbglR0PhysHeapCheckLocked(PRTERRINFO pErrInfo)
{
    /*
     * Scan the blocks in each chunk, walking the block list in parallel.
     */
    const VBGLPHYSHEAPBLOCK *pPrevBlockListEntry = NULL;
    const VBGLPHYSHEAPBLOCK *pCurBlockListEntry  = g_vbgldata.pBlockHead;
    unsigned                 acTotalBlocks[2]    = { 0, 0 };
    for (VBGLPHYSHEAPCHUNK *pCurChunk = g_vbgldata.pChunkHead, *pPrevChunk = NULL; pCurChunk; pCurChunk = pCurChunk->pNext)
    {
        AssertReturn(pCurChunk->u32Signature == VBGL_PH_CHUNKSIGNATURE,
                     RTErrInfoSetF(pErrInfo, VERR_INVALID_MAGIC, "pCurChunk=%p: magic=%#x", pCurChunk, pCurChunk->u32Signature));
        AssertReturn(pCurChunk->pPrev == pPrevChunk,
                     RTErrInfoSetF(pErrInfo, VERR_INTERNAL_ERROR_2,
                                   "pCurChunk=%p: pPrev=%p, expected %p", pCurChunk, pCurChunk->pPrev, pPrevChunk));

        const VBGLPHYSHEAPBLOCK *pCurBlock   = (const VBGLPHYSHEAPBLOCK *)(pCurChunk + 1);
        uintptr_t const          uEnd        = (uintptr_t)pCurChunk + pCurChunk->cbChunk;
        unsigned                 acBlocks[2] = { 0, 0 };
        while ((uintptr_t)pCurBlock < uEnd)
        {
            AssertReturn(pCurBlock->u32Signature == VBGL_PH_BLOCKSIGNATURE,
                         RTErrInfoSetF(pErrInfo, VERR_INVALID_MAGIC,
                                       "pCurBlock=%p: magic=%#x", pCurBlock, pCurBlock->u32Signature));
            AssertReturn(pCurBlock->pChunk == pCurChunk,
                         RTErrInfoSetF(pErrInfo, VERR_INTERNAL_ERROR_2,
                                       "pCurBlock=%p: pChunk=%p, expected %p", pCurBlock, pCurBlock->pChunk, pCurChunk));
            AssertReturn(   pCurBlock->cbUser >= VBGL_PH_SMALLEST_ALLOC_SIZE
                         && pCurBlock->cbUser <= VBGL_PH_LARGEST_ALLOC_SIZE
                         && RT_ALIGN_32(pCurBlock->cbUser, VBGL_PH_ALLOC_ALIGN) == pCurBlock->cbUser,
                         RTErrInfoSetF(pErrInfo, VERR_INTERNAL_ERROR_3,
                                       "pCurBlock=%p: cbUser=%#x", pCurBlock, pCurBlock->cbUser));
            AssertReturn(pCurBlock == pCurBlockListEntry,
                         RTErrInfoSetF(pErrInfo, VERR_INTERNAL_ERROR_4,
                                       "pCurChunk=%p: pCurBlock=%p, pCurBlockListEntry=%p\n",
                                       pCurChunk, pCurBlock, pCurBlockListEntry));
            AssertReturn(pCurBlock->pPrev == pPrevBlockListEntry,
                         RTErrInfoSetF(pErrInfo, VERR_INTERNAL_ERROR_5,
                                       "pCurChunk=%p: pCurBlock->pPrev=%p, pPrevBlockListEntry=%p\n",
                                       pCurChunk, pCurBlock->pPrev, pPrevBlockListEntry));

            acBlocks[pCurBlock->fAllocated] += 1;

            /* advance */
            pPrevBlockListEntry = pCurBlock;
            pCurBlockListEntry  = pCurBlock->pNext;
            pCurBlock = (const VBGLPHYSHEAPBLOCK *)((uintptr_t)(pCurBlock + 1) + pCurBlock->cbUser);
        }
        AssertReturn((uintptr_t)pCurBlock == uEnd,
                     RTErrInfoSetF(pErrInfo, VERR_INTERNAL_ERROR_4,
                                   "pCurBlock=%p uEnd=%p", pCurBlock, uEnd));

        acTotalBlocks[1] += acBlocks[1];
        AssertReturn(acBlocks[0] + acBlocks[1] == (uint32_t)pCurChunk->cBlocks,
                     RTErrInfoSetF(pErrInfo, VERR_INTERNAL_ERROR_4,
                                   "pCurChunk=%p: cBlocks=%u, expected %u",
                                   pCurChunk, pCurChunk->cBlocks, acBlocks[0] + acBlocks[1]));

        acTotalBlocks[0] += acBlocks[0];
        AssertReturn(acBlocks[0] == (uint32_t)pCurChunk->cFreeBlocks,
                     RTErrInfoSetF(pErrInfo, VERR_INTERNAL_ERROR_5,
                                   "pCurChunk=%p: cFreeBlocks=%u, expected %u",
                                   pCurChunk, pCurChunk->cFreeBlocks, acBlocks[0]));

        pPrevChunk = pCurChunk;
    }

    AssertReturn(acTotalBlocks[0] == (uint32_t)g_vbgldata.cFreeBlocks,
                 RTErrInfoSetF(pErrInfo, VERR_INTERNAL_ERROR,
                               "g_vbgldata.cFreeBlocks=%u, expected %u", g_vbgldata.cFreeBlocks, acTotalBlocks[0]));
    AssertReturn(acTotalBlocks[0] + acTotalBlocks[1] == (uint32_t)g_vbgldata.cBlocks,
                 RTErrInfoSetF(pErrInfo, VERR_INTERNAL_ERROR,
                               "g_vbgldata.cBlocks=%u, expected %u", g_vbgldata.cBlocks, acTotalBlocks[0] + acTotalBlocks[1]));

    /*
     * Check that the free list contains the same number of blocks as we
     * encountered during the above scan.
     */
    {
        unsigned cFreeListBlocks = 0;
        for (const VBGLPHYSHEAPFREEBLOCK *pCurBlock = g_vbgldata.pFreeHead, *pPrevBlock = NULL;
             pCurBlock;
             pCurBlock = pCurBlock->pNextFree)
        {
            AssertReturn(pCurBlock->Core.u32Signature == VBGL_PH_BLOCKSIGNATURE,
                         RTErrInfoSetF(pErrInfo, VERR_INVALID_MAGIC,
                                       "pCurBlock=%p/free: magic=%#x", pCurBlock, pCurBlock->Core.u32Signature));
            AssertReturn(pCurBlock->pPrevFree == pPrevBlock,
                         RTErrInfoSetF(pErrInfo, VERR_INTERNAL_ERROR_2,
                                       "pCurBlock=%p/free: pPrev=%p, expected %p", pCurBlock, pCurBlock->pPrevFree, pPrevBlock));
            AssertReturn(pCurBlock->Core.pChunk->u32Signature == VBGL_PH_CHUNKSIGNATURE,
                         RTErrInfoSetF(pErrInfo, VERR_INVALID_MAGIC, "pCurBlock=%p/free: chunk (%p) magic=%#x",
                                       pCurBlock, pCurBlock->Core.pChunk, pCurBlock->Core.pChunk->u32Signature));
            cFreeListBlocks++;
            pPrevBlock = pCurBlock;
        }

        AssertReturn(cFreeListBlocks == acTotalBlocks[0],
                     RTErrInfoSetF(pErrInfo, VERR_INTERNAL_ERROR_3,
                                   "Found %u in free list, expected %u", cFreeListBlocks, acTotalBlocks[0]));
    }
    return VINF_SUCCESS;
}


/**
 * Performs a heap check.
 *
 * @returns Problem description on failure, NULL on success.
 * @param   pErrInfo    Where to return more error details, optional.
 */
DECLVBGL(int) VbglR0PhysHeapCheck(PRTERRINFO pErrInfo)
{
    int rc = RTSemFastMutexRequest(g_vbgldata.hMtxHeap);
    AssertRCReturn(rc, 0);

    rc = vbglR0PhysHeapCheckLocked(pErrInfo);

    RTSemFastMutexRelease(g_vbgldata.hMtxHeap);
    return rc;
}

#endif /* IN_TESTCASE */

DECLR0VBGL(int) VbglR0PhysHeapInit(RTHCPHYS HCPhysMax)
{
    g_vbgldata.HCPhysMax = HCPhysMax;
    g_vbgldata.hMtxHeap  = NIL_RTSEMFASTMUTEX;

    /* Allocate the first chunk of the heap. */
    VBGLPHYSHEAPFREEBLOCK *pBlock = vbglPhysHeapChunkAlloc(0);
    if (pBlock)
        return RTSemFastMutexCreate(&g_vbgldata.hMtxHeap);
    return VERR_NO_CONT_MEMORY;
}

DECLR0VBGL(void) VbglR0PhysHeapTerminate(void)
{
    while (g_vbgldata.pChunkHead)
        vbglPhysHeapChunkDelete(g_vbgldata.pChunkHead);

    RTSemFastMutexDestroy(g_vbgldata.hMtxHeap);
    g_vbgldata.hMtxHeap = NIL_RTSEMFASTMUTEX;
}

