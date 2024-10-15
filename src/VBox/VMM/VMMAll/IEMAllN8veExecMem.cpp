/* $Id: IEMAllN8veExecMem.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEM - Native Recompiler, Executable Memory Allocator.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_IEM_RE_NATIVE
#define IEM_WITH_OPAQUE_DECODER_STATE
#define VMM_INCLUDED_SRC_include_IEMMc_h /* block IEMMc.h inclusion. */
#include <VBox/vmm/iem.h>
#include <VBox/vmm/cpum.h>
#include "IEMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#if   defined(RT_ARCH_AMD64)
# include <iprt/x86.h>
#elif defined(RT_ARCH_ARM64)
# include <iprt/armv8.h>
#endif

#ifdef RT_OS_WINDOWS
# include <iprt/formats/pecoff.h> /* this is incomaptible with windows.h, thus: */
extern "C" DECLIMPORT(uint8_t) __cdecl RtlAddFunctionTable(void *pvFunctionTable, uint32_t cEntries, uintptr_t uBaseAddress);
extern "C" DECLIMPORT(uint8_t) __cdecl RtlDelFunctionTable(void *pvFunctionTable);
#else
# include <iprt/formats/dwarf.h>
# if defined(RT_OS_DARWIN)
#  include <libkern/OSCacheControl.h>
#  include <mach/mach.h>
#  include <mach/mach_vm.h>
#  define IEMNATIVE_USE_LIBUNWIND
extern "C" void  __register_frame(const void *pvFde);
extern "C" void  __deregister_frame(const void *pvFde);
# else
#  ifdef DEBUG_bird /** @todo not thread safe yet */
#   define IEMNATIVE_USE_GDB_JIT
#  endif
#  ifdef IEMNATIVE_USE_GDB_JIT
#   include <iprt/critsect.h>
#   include <iprt/once.h>
#   include <iprt/formats/elf64.h>
#  endif
extern "C" void  __register_frame_info(void *pvBegin, void *pvObj); /* found no header for these two */
extern "C" void *__deregister_frame_info(void *pvBegin);           /* (returns pvObj from __register_frame_info call) */
# endif
#endif

#include "IEMN8veRecompiler.h"


/*********************************************************************************************************************************
*   Executable Memory Allocator                                                                                                  *
*********************************************************************************************************************************/
/** The chunk sub-allocation unit size in bytes. */
#define IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SIZE      256
/** The chunk sub-allocation unit size as a shift factor. */
#define IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SHIFT     8
/** Enables adding a header to the sub-allocator allocations.
 * This is useful for freeing up executable memory among other things.  */
#define IEMEXECMEM_ALT_SUB_WITH_ALLOC_HEADER
/** Use alternative pruning. */
#define IEMEXECMEM_ALT_SUB_WITH_ALT_PRUNING


#if defined(IN_RING3) && !defined(RT_OS_WINDOWS)
# ifdef IEMNATIVE_USE_GDB_JIT
#   define IEMNATIVE_USE_GDB_JIT_ET_DYN

/** GDB JIT: Code entry.   */
typedef struct GDBJITCODEENTRY
{
    struct GDBJITCODEENTRY *pNext;
    struct GDBJITCODEENTRY *pPrev;
    uint8_t                *pbSymFile;
    uint64_t                cbSymFile;
} GDBJITCODEENTRY;

/** GDB JIT: Actions. */
typedef enum GDBJITACTIONS : uint32_t
{
    kGdbJitaction_NoAction = 0, kGdbJitaction_Register, kGdbJitaction_Unregister
} GDBJITACTIONS;

/** GDB JIT: Descriptor. */
typedef struct GDBJITDESCRIPTOR
{
    uint32_t            uVersion;
    GDBJITACTIONS       enmAction;
    GDBJITCODEENTRY    *pRelevant;
    GDBJITCODEENTRY    *pHead;
    /** Our addition: */
    GDBJITCODEENTRY    *pTail;
} GDBJITDESCRIPTOR;

/** GDB JIT: Our simple symbol file data. */
typedef struct GDBJITSYMFILE
{
    Elf64_Ehdr          EHdr;
#  ifndef IEMNATIVE_USE_GDB_JIT_ET_DYN
    Elf64_Shdr          aShdrs[5];
#  else
    Elf64_Shdr          aShdrs[7];
    Elf64_Phdr          aPhdrs[2];
#  endif
    /** The dwarf ehframe data for the chunk. */
    uint8_t             abEhFrame[512];
    char                szzStrTab[128];
    Elf64_Sym           aSymbols[3];
#  ifdef IEMNATIVE_USE_GDB_JIT_ET_DYN
    Elf64_Sym           aDynSyms[2];
    Elf64_Dyn           aDyn[6];
#  endif
} GDBJITSYMFILE;

extern "C" GDBJITDESCRIPTOR __jit_debug_descriptor;
extern "C" DECLEXPORT(void) __jit_debug_register_code(void);

/** Init once for g_IemNativeGdbJitLock. */
static RTONCE     g_IemNativeGdbJitOnce = RTONCE_INITIALIZER;
/** Init once for the critical section. */
static RTCRITSECT g_IemNativeGdbJitLock;

/** GDB reads the info here. */
GDBJITDESCRIPTOR __jit_debug_descriptor = { 1, kGdbJitaction_NoAction, NULL, NULL };

/** GDB sets a breakpoint on this and checks __jit_debug_descriptor when hit. */
DECL_NO_INLINE(RT_NOTHING, DECLEXPORT(void)) __jit_debug_register_code(void)
{
    ASMNopPause();
}

/** @callback_method_impl{FNRTONCE} */
static DECLCALLBACK(int32_t) iemNativeGdbJitInitOnce(void *pvUser)
{
    RT_NOREF(pvUser);
    return RTCritSectInit(&g_IemNativeGdbJitLock);
}


# endif /* IEMNATIVE_USE_GDB_JIT */

/**
 * Per-chunk unwind info for non-windows hosts.
 */
typedef struct IEMEXECMEMCHUNKEHFRAME
{
# ifdef IEMNATIVE_USE_LIBUNWIND
    /** The offset of the FDA into abEhFrame. */
    uintptr_t               offFda;
# else
    /** 'struct object' storage area. */
    uint8_t                 abObject[1024];
# endif
#  ifdef IEMNATIVE_USE_GDB_JIT
#   if 0
    /** The GDB JIT 'symbol file' data. */
    GDBJITSYMFILE           GdbJitSymFile;
#   endif
    /** The GDB JIT list entry. */
    GDBJITCODEENTRY         GdbJitEntry;
#  endif
    /** The dwarf ehframe data for the chunk. */
    uint8_t                 abEhFrame[512];
} IEMEXECMEMCHUNKEHFRAME;
/** Pointer to per-chunk info info for non-windows hosts. */
typedef IEMEXECMEMCHUNKEHFRAME *PIEMEXECMEMCHUNKEHFRAME;
#endif


/**
 * An chunk of executable memory.
 */
typedef struct IEMEXECMEMCHUNK
{
    /** Number of free items in this chunk. */
    uint32_t                cFreeUnits;
    /** Hint were to start searching for free space in the allocation bitmap. */
    uint32_t                idxFreeHint;
    /** Pointer to the readable/writeable view of the memory chunk. */
    void                   *pvChunkRw;
    /** Pointer to the readable/executable view of the memory chunk. */
    void                   *pvChunkRx;
    /** Pointer to the context structure detailing the per chunk common code. */
    PCIEMNATIVEPERCHUNKCTX  pCtx;
#ifdef IN_RING3
    /**
     * Pointer to the unwind information.
     *
     * This is used during C++ throw and longjmp (windows and probably most other
     * platforms).  Some debuggers (windbg) makes use of it as well.
     *
     * Windows: This is allocated from hHeap on windows because (at least for
     *          AMD64) the UNWIND_INFO structure address in the
     *          RUNTIME_FUNCTION entry is an RVA and the chunk is the "image".
     *
     * Others:  Allocated from the regular heap to avoid unnecessary executable data
     *          structures.  This points to an IEMEXECMEMCHUNKEHFRAME structure. */
    void                   *pvUnwindInfo;
#elif defined(IN_RING0)
    /** Allocation handle. */
    RTR0MEMOBJ              hMemObj;
#endif
} IEMEXECMEMCHUNK;
/** Pointer to a memory chunk. */
typedef IEMEXECMEMCHUNK *PIEMEXECMEMCHUNK;


/**
 * Executable memory allocator for the native recompiler.
 */
typedef struct IEMEXECMEMALLOCATOR
{
    /** Magic value (IEMEXECMEMALLOCATOR_MAGIC).  */
    uint32_t                uMagic;

    /** The chunk size. */
    uint32_t                cbChunk;
    /** The maximum number of chunks. */
    uint32_t                cMaxChunks;
    /** The current number of chunks. */
    uint32_t                cChunks;
    /** Hint where to start looking for available memory. */
    uint32_t                idxChunkHint;
    /** Statistics: Current number of allocations. */
    uint32_t                cAllocations;

    /** The total amount of memory available. */
    uint64_t                cbTotal;
    /** Total amount of free memory. */
    uint64_t                cbFree;
    /** Total amount of memory allocated. */
    uint64_t                cbAllocated;

    /** Pointer to the allocation bitmaps for all the chunks (follows aChunks).
     *
     * Since the chunk size is a power of two and the minimum chunk size is a lot
     * higher than the IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SIZE, each chunk will always
     * require a whole number of uint64_t elements in the allocation bitmap.  So,
     * for sake of simplicity, they are allocated as one continous chunk for
     * simplicity/laziness. */
    uint64_t               *pbmAlloc;
    /** Number of units (IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SIZE) per chunk. */
    uint32_t                cUnitsPerChunk;
    /** Number of bitmap elements per chunk (for quickly locating the bitmap
     * portion corresponding to an chunk). */
    uint32_t                cBitmapElementsPerChunk;

    /** Number of times we fruitlessly scanned a chunk for free space. */
    uint64_t                cFruitlessChunkScans;

#ifdef IEMEXECMEM_ALT_SUB_WITH_ALT_PRUNING
    /** The next chunk to prune in. */
    uint32_t                idxChunkPrune;
    /** Where in chunk offset to start pruning at. */
    uint32_t                offChunkPrune;
    /** Profiling the pruning code. */
    STAMPROFILE             StatPruneProf;
    /** Number of bytes recovered by the pruning. */
    STAMPROFILE             StatPruneRecovered;
#endif

#ifdef VBOX_WITH_STATISTICS
    STAMPROFILE             StatAlloc;
    /** Total amount of memory not being usable currently due to IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SIZE. */
    uint64_t                cbUnusable;
    /** Allocation size distribution (in alloc units; 0 is the slop bucket). */
    STAMCOUNTER             aStatSizes[16];
#endif

#if defined(IN_RING3) && !defined(RT_OS_WINDOWS)
    /** Pointer to the array of unwind info running parallel to aChunks (same
     * allocation as this structure, located after the bitmaps).
     * (For Windows, the structures must reside in 32-bit RVA distance to the
     * actual chunk, so they are allocated off the chunk.) */
    PIEMEXECMEMCHUNKEHFRAME paEhFrames;
#endif

    /** The allocation chunks. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    IEMEXECMEMCHUNK         aChunks[RT_FLEXIBLE_ARRAY];
} IEMEXECMEMALLOCATOR;
/** Pointer to an executable memory allocator. */
typedef IEMEXECMEMALLOCATOR *PIEMEXECMEMALLOCATOR;

/** Magic value for IEMEXECMEMALLOCATOR::uMagic (Scott Frederick Turow). */
#define IEMEXECMEMALLOCATOR_MAGIC UINT32_C(0x19490412)


#ifdef IEMEXECMEM_ALT_SUB_WITH_ALLOC_HEADER
/**
 * Allocation header.
 */
typedef struct IEMEXECMEMALLOCHDR
{
    /** Magic value / eyecatcher (IEMEXECMEMALLOCHDR_MAGIC). */
    uint32_t        uMagic;
    /** The allocation chunk (for speeding up freeing). */
    uint32_t        idxChunk;
    /** Pointer to the translation block the allocation belongs to.
     * This is the whole point of the header. */
    PIEMTB          pTb;
} IEMEXECMEMALLOCHDR;
/** Pointer to an allocation header. */
typedef  IEMEXECMEMALLOCHDR *PIEMEXECMEMALLOCHDR;
/** Magic value for IEMEXECMEMALLOCHDR ('ExeM'). */
# define IEMEXECMEMALLOCHDR_MAGIC       UINT32_C(0x4d657845)
#endif


static int iemExecMemAllocatorGrow(PVMCPUCC pVCpu, PIEMEXECMEMALLOCATOR pExecMemAllocator);


#ifdef IEMEXECMEM_ALT_SUB_WITH_ALT_PRUNING
/**
 * Frees up executable memory when we're out space.
 *
 * This is an alternative to iemTbAllocatorFreeupNativeSpace() that frees up
 * space in a more linear fashion from the allocator's point of view.  It may
 * also defragment if implemented & enabled
 */
static void iemExecMemAllocatorPrune(PVMCPU pVCpu, PIEMEXECMEMALLOCATOR pExecMemAllocator)
{
# ifndef IEMEXECMEM_ALT_SUB_WITH_ALLOC_HEADER
#  error "IEMEXECMEM_ALT_SUB_WITH_ALT_PRUNING requires IEMEXECMEM_ALT_SUB_WITH_ALLOC_HEADER"
# endif
    STAM_REL_PROFILE_START(&pExecMemAllocator->StatPruneProf, a);

    /*
     * Before we can start, we must process delayed frees.
     */
    iemTbAllocatorProcessDelayedFrees(pVCpu, pVCpu->iem.s.pTbAllocatorR3);

    AssertCompile(RT_IS_POWER_OF_TWO(IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SIZE));

    uint32_t const cbChunk = pExecMemAllocator->cbChunk;
    AssertReturnVoid(RT_IS_POWER_OF_TWO(cbChunk));
    AssertReturnVoid(cbChunk >= _1M && cbChunk <= _256M); /* see iemExecMemAllocatorInit */

    uint32_t const cChunks = pExecMemAllocator->cChunks;
    AssertReturnVoid(cChunks == pExecMemAllocator->cMaxChunks);
    AssertReturnVoid(cChunks >= 1);

    Assert(!pVCpu->iem.s.pCurTbR3);

    /*
     * Decide how much to prune.  The chunk is is a multiple of two, so we'll be
     * scanning a multiple of two here as well.
     */
    uint32_t cbToPrune = cbChunk;

    /* Never more than 25%. */
    if (cChunks < 4)
        cbToPrune /= cChunks == 1 ? 4 : 2;

    /* Upper limit. In a debug build a 4MB limit averages out at ~0.6ms per call. */
    if (cbToPrune > _4M)
        cbToPrune = _4M;

    /*
     * Adjust the pruning chunk and offset accordingly.
     */
    uint32_t idxChunk = pExecMemAllocator->idxChunkPrune;
    uint32_t offChunk = pExecMemAllocator->offChunkPrune;
    offChunk &= ~(uint32_t)(IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SIZE - 1U);
    if (offChunk >= cbChunk)
    {
        offChunk = 0;
        idxChunk += 1;
    }
    if (idxChunk >= cChunks)
    {
        offChunk = 0;
        idxChunk = 0;
    }

    uint32_t const offPruneEnd = RT_MIN(offChunk + cbToPrune, cbChunk);

    /*
     * Do the pruning.  The current approach is the sever kind.
     */
    uint64_t            cbPruned = 0;
    uint8_t * const     pbChunk  = (uint8_t *)pExecMemAllocator->aChunks[idxChunk].pvChunkRx;
    while (offChunk < offPruneEnd)
    {
        PIEMEXECMEMALLOCHDR pHdr = (PIEMEXECMEMALLOCHDR)&pbChunk[offChunk];

        /* Is this the start of an allocation block for TB? (We typically have
           one allocation at the start of each chunk for the unwind info where
           pTb is NULL.)  */
        if (   pHdr->uMagic   == IEMEXECMEMALLOCHDR_MAGIC
            && pHdr->pTb      != NULL
            && pHdr->idxChunk == idxChunk)
        {
            PIEMTB const pTb = pHdr->pTb;
            AssertPtr(pTb);

            uint32_t const cbBlock = RT_ALIGN_32(pTb->Native.cInstructions * sizeof(IEMNATIVEINSTR) + sizeof(*pHdr),
                                                 IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SIZE);
            AssertBreakStmt(offChunk + cbBlock <= cbChunk, offChunk += IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SIZE); /* paranoia */

            iemTbAllocatorFree(pVCpu, pTb);

            cbPruned += cbBlock;
            offChunk += cbBlock;
        }
        else
            offChunk += IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SIZE;
    }
    STAM_REL_PROFILE_ADD_PERIOD(&pExecMemAllocator->StatPruneRecovered, cbPruned);

    /*
     * Save the current pruning point.
     */
    pExecMemAllocator->offChunkPrune = offChunk;
    pExecMemAllocator->idxChunkPrune = idxChunk;

    /* Set the hint to the start of the pruned region. */
    pExecMemAllocator->idxChunkHint  = idxChunk;
    pExecMemAllocator->aChunks[idxChunk].idxFreeHint = offChunk / IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SIZE;

    STAM_REL_PROFILE_STOP(&pExecMemAllocator->StatPruneProf, a);
}
#endif /* IEMEXECMEM_ALT_SUB_WITH_ALT_PRUNING */


#if defined(VBOX_STRICT) || 0
/**
 * The old bitmap scanner code, for comparison and assertions.
 */
static uint32_t iemExecMemAllocatorFindReqFreeUnitsOld(uint64_t *pbmAlloc, uint32_t cToScan, uint32_t cReqUnits)
{
    /** @todo This can probably be done more efficiently for non-x86 systems. */
    int iBit = ASMBitFirstClear(pbmAlloc, cToScan);
    while (iBit >= 0 && (uint32_t)iBit <= cToScan - cReqUnits)
    {
        uint32_t idxAddBit = 1;
        while (idxAddBit < cReqUnits && !ASMBitTest(pbmAlloc, (uint32_t)iBit + idxAddBit))
            idxAddBit++;
        if (idxAddBit >= cReqUnits)
            return (uint32_t)iBit;
        iBit = ASMBitNextClear(pbmAlloc, cToScan, iBit + idxAddBit - 1);
    }
    return UINT32_MAX;
}
#endif


/**
 * Bitmap scanner code that looks for a bunch of @a cReqUnits zero bits.
 *
 * Booting win11 with a r165098 release build the average native TB size is
 * around 9 units (of 256 bytes).  So, it is unlikely we need to scan any
 * subsequent words once we hit a patch of zeros, thus @a a_fBig.
 *
 * @todo This needs more tweaking. While it *is* faster the the old code,
 *       it doens't seem like it's all that much. :/
 */
template<const bool a_fBig>
static uint32_t iemExecMemAllocatorFindReqFreeUnits(uint64_t *pbmAlloc, uint32_t c64WordsToScan, uint32_t cReqUnits)
{
    /*
     * Scan the (section of the) allocation bitmap in 64-bit words.
     */
    unsigned cPrevLeadingZeros = 0;
    for (uint32_t off = 0; off < c64WordsToScan; off++)
    {
        uint64_t uWord = pbmAlloc[off];
        if (uWord == UINT64_MAX)
        {
            /*
             * Getting thru patches of UINT64_MAX is a frequent problem when the allocator
             * fills up, so it's definitely worth optimizing.
             *
             * The complicated code below is a bit faster on arm. Reducing the per TB cost
             * from 4255ns to 4106ns (best run out of 10).  On win/amd64 there isn't an
             * obvious gain here, at least not with the data currently being profiled.
             */
#if 1
            off++;
            uint32_t cQuads = (c64WordsToScan - off) / 4;

            /* Align. */
            if (cQuads > 1)
                switch (((uintptr_t)&pbmAlloc[off] / sizeof(uint64_t)) & 3)
                {
                    case 0:
                        break;
                    case 1:
                    {
                        uWord           = pbmAlloc[off];
                        uint64_t uWord1 = pbmAlloc[off + 1];
                        uint64_t uWord2 = pbmAlloc[off + 2];
                        if ((uWord & uWord1 & uWord2) == UINT64_MAX)
                        {
                            off   += 3;
                            cQuads = (c64WordsToScan - off) / 4;
                        }
                        else if (uWord == UINT64_MAX)
                        {
                            if (uWord1 != UINT64_MAX)
                            {
                                uWord = uWord1;
                                off  += 1;
                            }
                            else
                            {
                                uWord = uWord2;
                                off  += 2;
                            }
                        }
                        break;
                    }
                    case 2:
                    {
                        uWord           = pbmAlloc[off];
                        uint64_t uWord1 = pbmAlloc[off + 1];
                        if ((uWord & uWord1) == UINT64_MAX)
                        {
                            off   += 2;
                            cQuads = (c64WordsToScan - off) / 4;
                        }
                        else if (uWord == UINT64_MAX)
                        {
                            uWord = uWord1;
                            off  += 1;
                        }
                        break;
                    }
                    case 3:
                        uWord = pbmAlloc[off];
                        if (uWord == UINT64_MAX)
                        {
                            off++;
                            cQuads = (c64WordsToScan - off) / 4;
                        }
                        break;
                }
            if (uWord == UINT64_MAX)
            {
                /* Looping over 32 bytes at a time. */
                for (;;)
                {
                    if (cQuads-- > 0)
                    {
                        uWord           = pbmAlloc[off + 0];
                        uint64_t uWord1 = pbmAlloc[off + 1];
                        uint64_t uWord2 = pbmAlloc[off + 2];
                        uint64_t uWord3 = pbmAlloc[off + 3];
                        if ((uWord & uWord1 & uWord2 & uWord3) == UINT64_MAX)
                            off += 4;
                        else
                        {
                            if (uWord != UINT64_MAX)
                            { }
                            else if (uWord1 != UINT64_MAX)
                            {
                                uWord = uWord1;
                                off += 1;
                            }
                            else if (uWord2 != UINT64_MAX)
                            {
                                uWord = uWord2;
                                off += 2;
                            }
                            else
                            {
                                uWord = uWord3;
                                off += 3;
                            }
                            break;
                        }
                    }
                    else
                    {
                        if (off < c64WordsToScan)
                        {
                            uWord = pbmAlloc[off];
                            if (uWord != UINT64_MAX)
                                break;
                            off++;
                            if (off < c64WordsToScan)
                            {
                                uWord = pbmAlloc[off];
                                if (uWord != UINT64_MAX)
                                    break;
                                off++;
                                if (off < c64WordsToScan)
                                {
                                    uWord = pbmAlloc[off];
                                    if (uWord != UINT64_MAX)
                                        break;
                                    Assert(off + 1 == c64WordsToScan);
                                }
                            }
                        }
                        return UINT32_MAX;
                    }
                }
            }
#else
            do
            {
                off++;
                if (off < c64WordsToScan)
                    uWord = pbmAlloc[off];
                else
                    return UINT32_MAX;
            } while (uWord == UINT64_MAX);
#endif
            cPrevLeadingZeros = 0;
        }

        /*
         * If we get down here, we have a word that isn't UINT64_MAX.
         */
        if (uWord != 0)
        {
            /*
             * Fend of large request we cannot satisfy before the first set bit.
             */
            if (!a_fBig || cReqUnits < 64 + cPrevLeadingZeros)
            {
#ifdef __GNUC__
                unsigned cZerosInWord = __builtin_popcountl(~uWord);
#elif defined(_MSC_VER) && defined(RT_ARCH_AMD64)
                unsigned cZerosInWord = __popcnt64(~uWord);
#elif defined(_MSC_VER) && defined(RT_ARCH_ARM64)
                unsigned cZerosInWord = _CountOneBits64(~uWord);
#else
# pragma message("need popcount intrinsic or something...")
                unsigned cZerosInWord = 0;
                for (uint64_t uTmp = ~uWords; uTmp; cZerosInWord++)
                    uTmp &= uTmp - 1; /* Clears the least significant bit set. */
#endif
                if (cZerosInWord + cPrevLeadingZeros >= cReqUnits)
                {
                    /* Check if we've got a patch of zeros at the trailing end
                       when joined with the previous word: */
#ifdef __GNUC__
                    unsigned cTrailingZeros = __builtin_ctzl(uWord);
#else
                    unsigned cTrailingZeros = ASMBitFirstSetU64(uWord) - 1;
#endif
                    if (cPrevLeadingZeros + cTrailingZeros >= cReqUnits)
                        return off * 64 - cPrevLeadingZeros;

                    /*
                     * Try leading zeros before we get on with the tedious stuff.
                     */
#ifdef __GNUC__
                    cPrevLeadingZeros = __builtin_clzl(uWord);
#else
                    cPrevLeadingZeros = 64 - ASMBitLastSetU64(uWord);
#endif
                    if (cPrevLeadingZeros >= cReqUnits)
                        return (off + 1) * 64 - cPrevLeadingZeros;

                    /*
                     * Check the popcount again sans leading & trailing before looking
                     * inside the word.
                     */
                    cZerosInWord -= cPrevLeadingZeros + cTrailingZeros;
                    if (cZerosInWord >= cReqUnits)
                    {
                        /* 1; 64 - 0 - 1 = 63; */
                        unsigned const iBitLast = 64 - cPrevLeadingZeros - cReqUnits; /** @todo boundrary */
                        unsigned       iBit     = cTrailingZeros;
                        uWord >>= cTrailingZeros;
                        do
                        {
                            Assert(uWord & 1);
#ifdef __GNUC__
                            unsigned iZeroBit = __builtin_ctzl(~uWord);
#else
                            unsigned iZeroBit = ASMBitFirstSetU64(~uWord) - 1;
#endif
                            iBit   += iZeroBit;
                            uWord >>= iZeroBit;
                            Assert(iBit <= iBitLast);
                            Assert((uWord & 1) == 0);
#ifdef __GNUC__
                            unsigned cZeros = __builtin_ctzl(uWord);
#else
                            unsigned cZeros = ASMBitFirstSetU64(uWord) - 1;
#endif
                            if (cZeros >= cReqUnits)
                                return off * 64 + iBit;

                            cZerosInWord -= cZeros;  /* (may underflow as we will count shifted in zeros) */
                            iBit         += cZeros;
                            uWord       >>= cZeros;
                        } while ((int)cZerosInWord >= (int)cReqUnits && iBit < iBitLast);
                    }
                    continue; /* we've already calculated cPrevLeadingZeros */
                }
            }

            /* Update the leading (MSB) zero count. */
#ifdef __GNUC__
            cPrevLeadingZeros = __builtin_clzl(uWord);
#else
            cPrevLeadingZeros = 64 - ASMBitLastSetU64(uWord);
#endif
        }
        /*
         * uWord == 0
         */
        else
        {
            if RT_CONSTEXPR_IF(!a_fBig)
                return off * 64 - cPrevLeadingZeros;
            else /* keep else */
            {
                if (cPrevLeadingZeros + 64 >= cReqUnits)
                    return off * 64 - cPrevLeadingZeros;
                for (uint32_t off2 = off + 1;; off2++)
                {
                    if (off2 < c64WordsToScan)
                    {
                        uWord = pbmAlloc[off2];
                        if (uWord == UINT64_MAX)
                        {
                            cPrevLeadingZeros = 0;
                            break;
                        }
                        if (uWord == 0)
                        {
                            if (cPrevLeadingZeros + (off2 - off + 1) * 64 >= cReqUnits)
                                return off * 64 - cPrevLeadingZeros;
                        }
                        else
                        {
#ifdef __GNUC__
                            unsigned cTrailingZeros = __builtin_ctzl(uWord);
#else
                            unsigned cTrailingZeros = ASMBitFirstSetU64(uWord) - 1;
#endif
                            if (cPrevLeadingZeros + (off2 - off) * 64 + cTrailingZeros >= cReqUnits)
                                return off * 64 - cPrevLeadingZeros;
#ifdef __GNUC__
                            cPrevLeadingZeros = __builtin_clzl(uWord);
#else
                            cPrevLeadingZeros = 64 - ASMBitLastSetU64(uWord);
#endif
                            break;
                        }
                    }
                    else
                        return UINT32_MAX;
                }
            }
        }
    }
    return UINT32_MAX;
}


/**
 * Try allocate a block of @a cReqUnits in the chunk @a idxChunk.
 */
static void *
iemExecMemAllocatorAllocInChunkInt(PIEMEXECMEMALLOCATOR pExecMemAllocator, uint64_t *pbmAlloc, uint32_t idxFirst,
                                   uint32_t cToScan, uint32_t cReqUnits, uint32_t idxChunk, PIEMTB pTb,
                                   void **ppvExec, PCIEMNATIVEPERCHUNKCTX *ppChunkCtx)
{
    /*
     * Shift the bitmap to the idxFirst bit so we can use ASMBitFirstClear.
     */
    Assert(!(cToScan & 63));
    Assert(!(idxFirst & 63));
    Assert(cToScan + idxFirst <= pExecMemAllocator->cUnitsPerChunk);
    pbmAlloc += idxFirst / 64;
    cToScan  += idxFirst & 63;
    Assert(!(cToScan & 63));

#if 1
    uint32_t const iBit = cReqUnits < 64
                        ? iemExecMemAllocatorFindReqFreeUnits<false>(pbmAlloc, cToScan / 64, cReqUnits)
                        : iemExecMemAllocatorFindReqFreeUnits<true>( pbmAlloc, cToScan / 64, cReqUnits);
    Assert(iBit == iemExecMemAllocatorFindReqFreeUnitsOld(pbmAlloc, cToScan, cReqUnits));
#else
    uint32_t const iBit = iemExecMemAllocatorFindReqFreeUnitsOld(pbmAlloc, cToScan, cReqUnits);
#endif
    if (iBit != UINT32_MAX)
    {
        ASMBitSetRange(pbmAlloc, (uint32_t)iBit, (uint32_t)iBit + cReqUnits);

        PIEMEXECMEMCHUNK const pChunk = &pExecMemAllocator->aChunks[idxChunk];
        pChunk->cFreeUnits -= cReqUnits;
        pChunk->idxFreeHint = (uint32_t)iBit + cReqUnits;

        pExecMemAllocator->cAllocations += 1;
        uint32_t const cbReq = cReqUnits << IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SHIFT;
        pExecMemAllocator->cbAllocated  += cbReq;
        pExecMemAllocator->cbFree       -= cbReq;
        pExecMemAllocator->idxChunkHint  = idxChunk;

        void * const pvMemRw = (uint8_t *)pChunk->pvChunkRw
                             + ((idxFirst + (uint32_t)iBit) << IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SHIFT);

        if (ppChunkCtx)
            *ppChunkCtx = pChunk->pCtx;

        /*
         * Initialize the header and return.
         */
# ifdef IEMEXECMEM_ALT_SUB_WITH_ALLOC_HEADER
        PIEMEXECMEMALLOCHDR const pHdr = (PIEMEXECMEMALLOCHDR)pvMemRw;
        pHdr->uMagic   = IEMEXECMEMALLOCHDR_MAGIC;
        pHdr->idxChunk = idxChunk;
        pHdr->pTb      = pTb;

        if (ppvExec)
            *ppvExec = (uint8_t *)pChunk->pvChunkRx
                     + ((idxFirst + (uint32_t)iBit) << IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SHIFT)
                     + sizeof(*pHdr);

        return pHdr + 1;
#else
        if (ppvExec)
            *ppvExec = (uint8_t *)pChunk->pvChunkRx
                     + ((idxFirst + (uint32_t)iBit) << IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SHIFT);

        RT_NOREF(pTb);
        return pvMem;
#endif
    }

    return NULL;
}


/**
 * Converts requested number of bytes into a unit count.
 */
DECL_FORCE_INLINE(uint32_t) iemExecMemAllocBytesToUnits(uint32_t cbReq)
{
#ifdef IEMEXECMEM_ALT_SUB_WITH_ALLOC_HEADER
    return (cbReq + sizeof(IEMEXECMEMALLOCHDR) + IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SIZE - 1)
#else
    return (cbReq + IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SIZE - 1)
#endif
        >> IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SHIFT;
}


DECL_FORCE_INLINE(PIEMNATIVEINSTR)
iemExecMemAllocatorAllocUnitsInChunkInner(PIEMEXECMEMALLOCATOR pExecMemAllocator, uint32_t idxChunk, uint32_t cReqUnits,
                                          PIEMTB pTb, PIEMNATIVEINSTR *ppaExec, PCIEMNATIVEPERCHUNKCTX *ppChunkCtx)
{
    uint64_t * const pbmAlloc = &pExecMemAllocator->pbmAlloc[pExecMemAllocator->cBitmapElementsPerChunk * idxChunk];
    uint32_t const   idxHint  = pExecMemAllocator->aChunks[idxChunk].idxFreeHint & ~(uint32_t)63;
    if (idxHint + cReqUnits <= pExecMemAllocator->cUnitsPerChunk)
    {
        void *pvRet = iemExecMemAllocatorAllocInChunkInt(pExecMemAllocator, pbmAlloc, idxHint,
                                                         pExecMemAllocator->cUnitsPerChunk - idxHint,
                                                         cReqUnits, idxChunk, pTb, (void **)ppaExec, ppChunkCtx);
        if (pvRet)
            return (PIEMNATIVEINSTR)pvRet;
    }
    void *pvRet = iemExecMemAllocatorAllocInChunkInt(pExecMemAllocator, pbmAlloc, 0,
                                                     RT_MIN(pExecMemAllocator->cUnitsPerChunk,
                                                            RT_ALIGN_32(idxHint + cReqUnits, 64*4)),
                                                     cReqUnits, idxChunk, pTb, (void **)ppaExec, ppChunkCtx);
    if (pvRet)
        return (PIEMNATIVEINSTR)pvRet;

    pExecMemAllocator->cFruitlessChunkScans += 1;
    return NULL;
}


DECLINLINE(PIEMNATIVEINSTR)
iemExecMemAllocatorAllocBytesInChunk(PIEMEXECMEMALLOCATOR pExecMemAllocator, uint32_t idxChunk, uint32_t cbReq,
                                     PIEMNATIVEINSTR *ppaExec)
{
    uint32_t const cReqUnits = iemExecMemAllocBytesToUnits(cbReq);
    if (cReqUnits <= pExecMemAllocator->aChunks[idxChunk].cFreeUnits)
        return iemExecMemAllocatorAllocUnitsInChunkInner(pExecMemAllocator, idxChunk, cReqUnits, NULL /*pTb*/,
                                                         ppaExec, NULL /*ppChunkCtx*/);
    return NULL;
}


/**
 * Allocates @a cbReq bytes of executable memory.
 *
 * @returns Pointer to the readable/writeable memory, NULL if out of memory or other problem
 *          encountered.
 * @param   pVCpu       The cross context virtual CPU structure of the
 *                      calling thread.
 * @param   cbReq       How many bytes are required.
 * @param   pTb         The translation block that will be using the allocation.
 * @param   ppaExec     Where to return the pointer to executable view of
 *                      the allocated memory, optional.
 * @param   ppChunkCtx  Where to return the per chunk attached context
 *                      if available, optional.
 */
DECLHIDDEN(PIEMNATIVEINSTR) iemExecMemAllocatorAlloc(PVMCPU pVCpu, uint32_t cbReq, PIEMTB pTb,
                                                     PIEMNATIVEINSTR *ppaExec, PCIEMNATIVEPERCHUNKCTX *ppChunkCtx) RT_NOEXCEPT
{
    PIEMEXECMEMALLOCATOR pExecMemAllocator = pVCpu->iem.s.pExecMemAllocatorR3;
    AssertReturn(pExecMemAllocator && pExecMemAllocator->uMagic == IEMEXECMEMALLOCATOR_MAGIC, NULL);
    AssertMsgReturn(cbReq > 32 && cbReq < _512K, ("%#x\n", cbReq), NULL);
    STAM_PROFILE_START(&pExecMemAllocator->StatAlloc, a);

    uint32_t const cReqUnits = iemExecMemAllocBytesToUnits(cbReq);
    STAM_COUNTER_INC(&pExecMemAllocator->aStatSizes[cReqUnits < RT_ELEMENTS(pExecMemAllocator->aStatSizes) ? cReqUnits : 0]);
    for (unsigned iIteration = 0;; iIteration++)
    {
        if (   cbReq * 2 <= pExecMemAllocator->cbFree
            || (cReqUnits == 1 || pExecMemAllocator->cbFree >= IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SIZE) )
        {
            uint32_t const cChunks       = pExecMemAllocator->cChunks;
            uint32_t const idxChunkHint  = pExecMemAllocator->idxChunkHint < cChunks ? pExecMemAllocator->idxChunkHint : 0;

            /*
             * We do two passes here, the first pass we skip chunks with fewer than cReqUnits * 16,
             * the 2nd pass we skip chunks. The second pass checks the one skipped in the first pass.
             */
            for (uint32_t cMinFreePass = cReqUnits == 1 ? cReqUnits : cReqUnits * 16, cMaxFreePass = UINT32_MAX;;)
            {
                for (uint32_t idxChunk = idxChunkHint; idxChunk < cChunks; idxChunk++)
                    if (   pExecMemAllocator->aChunks[idxChunk].cFreeUnits >= cMinFreePass
                        && pExecMemAllocator->aChunks[idxChunk].cFreeUnits <= cMaxFreePass)
                    {
                        PIEMNATIVEINSTR const pRet = iemExecMemAllocatorAllocUnitsInChunkInner(pExecMemAllocator, idxChunk,
                                                                                               cReqUnits, pTb, ppaExec, ppChunkCtx);
                        if (pRet)
                        {
                            STAM_PROFILE_STOP(&pExecMemAllocator->StatAlloc, a);
#ifdef VBOX_WITH_STATISTICS
                            pExecMemAllocator->cbUnusable += (cReqUnits << IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SHIFT) - cbReq;
#endif
                            return pRet;
                        }
                    }
                for (uint32_t idxChunk = 0; idxChunk < idxChunkHint; idxChunk++)
                    if (   pExecMemAllocator->aChunks[idxChunk].cFreeUnits >= cMinFreePass
                        && pExecMemAllocator->aChunks[idxChunk].cFreeUnits <= cMaxFreePass)
                    {
                        PIEMNATIVEINSTR const pRet = iemExecMemAllocatorAllocUnitsInChunkInner(pExecMemAllocator, idxChunk,
                                                                                               cReqUnits, pTb, ppaExec, ppChunkCtx);
                        if (pRet)
                        {
                            STAM_PROFILE_STOP(&pExecMemAllocator->StatAlloc, a);
#ifdef VBOX_WITH_STATISTICS
                            pExecMemAllocator->cbUnusable += (cReqUnits << IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SHIFT) - cbReq;
#endif
                            return pRet;
                        }
                    }
                if (cMinFreePass <= cReqUnits * 2)
                    break;
                cMaxFreePass = cMinFreePass - 1;
                cMinFreePass = cReqUnits * 2;
            }
        }

        /*
         * Can we grow it with another chunk?
         */
        if (pExecMemAllocator->cChunks < pExecMemAllocator->cMaxChunks)
        {
            int rc = iemExecMemAllocatorGrow(pVCpu, pExecMemAllocator);
            AssertLogRelRCReturn(rc, NULL);

            uint32_t const idxChunk = pExecMemAllocator->cChunks - 1;
            PIEMNATIVEINSTR const pRet = iemExecMemAllocatorAllocUnitsInChunkInner(pExecMemAllocator, idxChunk, cReqUnits, pTb,
                                                                                   ppaExec, ppChunkCtx);
            if (pRet)
            {
                STAM_PROFILE_STOP(&pExecMemAllocator->StatAlloc, a);
#ifdef VBOX_WITH_STATISTICS
                pExecMemAllocator->cbUnusable += (cReqUnits << IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SHIFT) - cbReq;
#endif
                return pRet;
            }
            AssertFailed();
        }

        /*
         * Try prune native TBs once.
         */
        if (iIteration == 0)
        {
#ifdef IEMEXECMEM_ALT_SUB_WITH_ALT_PRUNING
            iemExecMemAllocatorPrune(pVCpu, pExecMemAllocator);
#else
            /* No header included in the instruction count here. */
            uint32_t const cNeededInstrs = RT_ALIGN_32(cbReq, IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SIZE) / sizeof(IEMNATIVEINSTR);
            iemTbAllocatorFreeupNativeSpace(pVCpu, cNeededInstrs);
#endif
        }
        else
        {
            STAM_REL_COUNTER_INC(&pVCpu->iem.s.StatNativeExecMemInstrBufAllocFailed);
            STAM_PROFILE_STOP(&pExecMemAllocator->StatAlloc, a);
            return NULL;
        }
    }
}


/** This is a hook to ensure the instruction cache is properly flushed before the code in the memory
 * given by @a pv and @a cb is executed */
DECLHIDDEN(void) iemExecMemAllocatorReadyForUse(PVMCPUCC pVCpu, void *pv, size_t cb) RT_NOEXCEPT
{
#ifdef RT_OS_DARWIN
    /*
     * We need to synchronize the stuff we wrote to the data cache with the
     * instruction cache, since these aren't coherent on arm (or at least not
     * on Apple Mn CPUs).
     *
     * Note! Since we don't any share JIT'ed code with the other CPUs, we don't
     *       really care whether the dcache is fully flushed back to memory. It
     *       only needs to hit the level 2 cache, which the level 1 instruction
     *       and data caches seems to be sharing.  In ARM terms, we need to reach
     *       a point of unification (PoU), rather than a point of coherhency (PoC).
     *
     * https://developer.apple.com/documentation/apple-silicon/porting-just-in-time-compilers-to-apple-silicon
     *
     * https://developer.arm.com/documentation/den0013/d/Caches/Point-of-coherency-and-unification
     *
     * Experimenting with the approach used by sys_icache_invalidate() and
     * tweaking it a little, could let us shave off a bit of effort.  The thing
     * that slows the apple code down on an M2 (runing Sonoma 13.4), seems to
     * the 'DSB ISH' instructions performed every 20 icache line flushes.
     * Skipping these saves ~100ns or more per TB when profiling the native
     * recompiler on the TBs from a win11 full boot-desktop-shutdow sequence.
     * Thus we will leave DCACHE_ICACHE_SYNC_WITH_WITH_IVAU_DSB undefined if we
     * can.
     *
     * There appears not to be much difference between DSB options 'ISH',
     * 'ISHST', 'NSH' and 'NSHST'.  The latter is theoretically all we need, so
     * we'll use that one.
     *
     * See https://developer.arm.com/documentation/100941/0101/Barriers for
     * details on the barrier options.
     *
     * Note! The CFG value "/IEM/HostICacheInvalidationViaHostAPI" can be used
     *       to disabling the experimental code should it misbehave.
     */
    uint8_t const fHostICacheInvalidation = pVCpu->iem.s.fHostICacheInvalidation;
    if (!(fHostICacheInvalidation & IEMNATIVE_ICACHE_F_USE_HOST_API))
    {
#  define DCACHE_ICACHE_SYNC_DSB_OPTION "nshst"
/*#  define DCACHE_ICACHE_SYNC_WITH_WITH_IVAU_DSB*/

        /* Skipping this is fine, but doesn't impact perf much. */
        __asm__ __volatile__("dsb " DCACHE_ICACHE_SYNC_DSB_OPTION);

        /* Invalidate the icache for the range [pv,pv+cb). */
#  ifdef DCACHE_ICACHE_SYNC_WITH_WITH_IVAU_DSB
        size_t const cIvauDsbEvery= 20;
        unsigned     cDsb         = cIvauDsbEvery;
#  endif
        size_t const cbCacheLine  = 64;
        size_t       cbInvalidate = cb + ((uintptr_t)pv & (cbCacheLine - 1)) ;
        size_t       cCacheLines  = RT_ALIGN_Z(cbInvalidate, cbCacheLine) / cbCacheLine;
        uintptr_t    uPtr         = (uintptr_t)pv & ~(uintptr_t)(cbCacheLine - 1);
        for (;; uPtr += cbCacheLine)
        {
            __asm__ /*__volatile__*/("ic ivau, %0" : : "r" (uPtr));
            cCacheLines -= 1;
            if (!cCacheLines)
                break;
#  ifdef DCACHE_ICACHE_SYNC_WITH_WITH_IVAU_DSB
            cDsb -= 1;
            if (cDsb != 0)
            { /* likely */ }
            else
            {
                __asm__ __volatile__("dsb " DCACHE_ICACHE_SYNC_DSB_OPTION);
                cDsb = cIvauDsbEvery;
            }
#  endif
        }

        /*
         * The DSB here is non-optional it seems.
         *
         * The following ISB can be omitted on M2 without any obvious sideeffects,
         * it produces better number in the above mention profiling scenario.
         * This could be related to the kHasICDSB flag in cpu_capabilities.h,
         * but it doesn't look like that flag is set here (M2, Sonoma 13.4).
         *
         * I've made the inclusion of the ISH barrier as configurable and with
         * a default of skipping it.
         */
        if (!(fHostICacheInvalidation & IEMNATIVE_ICACHE_F_END_WITH_ISH))
            __asm__ __volatile__("dsb " DCACHE_ICACHE_SYNC_DSB_OPTION
                                  ::: "memory");
        else
            __asm__ __volatile__("dsb " DCACHE_ICACHE_SYNC_DSB_OPTION "\n\t"
                                 "isb"
                                 ::: "memory");
    }
    else
        sys_icache_invalidate(pv, cb);

#elif defined(RT_OS_LINUX) && defined(RT_ARCH_ARM64)
    RT_NOREF(pVCpu);

    /* There is __builtin___clear_cache() but it flushes both the instruction and data cache, so do it manually. */
    static uint32_t s_u32CtrEl0 = 0;
    if (!s_u32CtrEl0)
        asm volatile ("mrs %0, ctr_el0":"=r" (s_u32CtrEl0));
    uintptr_t cbICacheLine = (uintptr_t)4 << (s_u32CtrEl0 & 0xf);

    uintptr_t pb = (uintptr_t)pv & ~(cbICacheLine - 1);
    for (; pb < (uintptr_t)pv + cb; pb += cbICacheLine)
        asm volatile ("ic ivau, %0" : : "r" (pb) : "memory");

    asm volatile ("dsb ish\n\t isb\n\t" : : : "memory");

#else
    RT_NOREF(pVCpu, pv, cb);
#endif
}


/**
 * Frees executable memory.
 */
DECLHIDDEN(void) iemExecMemAllocatorFree(PVMCPU pVCpu, void *pv, size_t cb) RT_NOEXCEPT
{
    PIEMEXECMEMALLOCATOR pExecMemAllocator = pVCpu->iem.s.pExecMemAllocatorR3;
    Assert(pExecMemAllocator && pExecMemAllocator->uMagic == IEMEXECMEMALLOCATOR_MAGIC);
    AssertPtr(pv);
#ifdef VBOX_WITH_STATISTICS
    size_t const cbOrig = cb;
#endif
#ifndef IEMEXECMEM_ALT_SUB_WITH_ALLOC_HEADER
    Assert(!((uintptr_t)pv & (IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SIZE - 1)));

    /* Align the size as we did when allocating the block. */
    cb = RT_ALIGN_Z(cb, IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SIZE);

#else
    PIEMEXECMEMALLOCHDR pHdr = (PIEMEXECMEMALLOCHDR)pv - 1;
    Assert(!((uintptr_t)pHdr & (IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SIZE - 1)));
    AssertReturnVoid(pHdr->uMagic == IEMEXECMEMALLOCHDR_MAGIC);
    uint32_t const idxChunk = pHdr->idxChunk;
    AssertReturnVoid(idxChunk < pExecMemAllocator->cChunks);
    pv = pHdr;

    /* Adjust and align the size to cover the whole allocation area. */
    cb = RT_ALIGN_Z(cb + sizeof(*pHdr), IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SIZE);
#endif

    /* Free it / assert sanity. */
    bool           fFound  = false;
    uint32_t const cbChunk = pExecMemAllocator->cbChunk;
#ifndef IEMEXECMEM_ALT_SUB_WITH_ALLOC_HEADER
    uint32_t const cChunks = pExecMemAllocator->cChunks;
    for (uint32_t idxChunk = 0; idxChunk < cChunks; idxChunk++)
#endif
    {
        uintptr_t const offChunk = (uintptr_t)pv - (uintptr_t)pExecMemAllocator->aChunks[idxChunk].pvChunkRx;
        fFound = offChunk < cbChunk;
        if (fFound)
        {
            uint32_t const idxFirst  = (uint32_t)offChunk >> IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SHIFT;
            uint32_t const cReqUnits = (uint32_t)cb       >> IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SHIFT;

            /* Check that it's valid and free it. */
            uint64_t * const pbmAlloc = &pExecMemAllocator->pbmAlloc[pExecMemAllocator->cBitmapElementsPerChunk * idxChunk];
            AssertReturnVoid(ASMBitTest(pbmAlloc, idxFirst));
            for (uint32_t i = 1; i < cReqUnits; i++)
                AssertReturnVoid(ASMBitTest(pbmAlloc, idxFirst + i));
            ASMBitClearRange(pbmAlloc, idxFirst, idxFirst + cReqUnits);

            /* Invalidate the header using the writeable memory view. */
            pHdr = (PIEMEXECMEMALLOCHDR)((uintptr_t)pExecMemAllocator->aChunks[idxChunk].pvChunkRw + offChunk);
#ifdef IEMEXECMEM_ALT_SUB_WITH_ALLOC_HEADER
            pHdr->uMagic   = 0;
            pHdr->idxChunk = 0;
            pHdr->pTb      = NULL;
#endif
            pExecMemAllocator->aChunks[idxChunk].cFreeUnits  += cReqUnits;
            pExecMemAllocator->aChunks[idxChunk].idxFreeHint  = idxFirst;

            /* Update the stats. */
            pExecMemAllocator->cbAllocated  -= cb;
            pExecMemAllocator->cbFree       += cb;
            pExecMemAllocator->cAllocations -= 1;
#ifdef VBOX_WITH_STATISTICS
            pExecMemAllocator->cbUnusable   -= (cReqUnits << IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SHIFT) - cbOrig;
#endif
            return;
        }
    }
    AssertFailed();
}


/**
 * Interface used by iemNativeRecompileAttachExecMemChunkCtx and unwind info
 * generators.
 */
DECLHIDDEN(PIEMNATIVEINSTR)
iemExecMemAllocatorAllocFromChunk(PVMCPU pVCpu, uint32_t idxChunk, uint32_t cbReq, PIEMNATIVEINSTR *ppaExec)
{
    PIEMEXECMEMALLOCATOR pExecMemAllocator = pVCpu->iem.s.pExecMemAllocatorR3;
    AssertReturn(idxChunk < pExecMemAllocator->cChunks, NULL);
    Assert(cbReq < _1M);
    return iemExecMemAllocatorAllocBytesInChunk(pExecMemAllocator, idxChunk, cbReq, ppaExec);
}


/**
 * For getting the per-chunk context detailing common code for a TB.
 *
 * This is for use by the disassembler.
 */
DECLHIDDEN(PCIEMNATIVEPERCHUNKCTX) iemExecMemGetTbChunkCtx(PVMCPU pVCpu, PCIEMTB pTb)
{
    PIEMEXECMEMALLOCATOR pExecMemAllocator = pVCpu->iem.s.pExecMemAllocatorR3;
    if ((pTb->fFlags & IEMTB_F_TYPE_MASK) == IEMTB_F_TYPE_NATIVE)
    {
        uintptr_t const uAddress = (uintptr_t)pTb->Native.paInstructions;
        uint32_t const  cbChunk  = pExecMemAllocator->cbChunk;
        uint32_t        idxChunk = pExecMemAllocator->cChunks;
        while (idxChunk-- > 0)
            if (uAddress - (uintptr_t)pExecMemAllocator->aChunks[idxChunk].pvChunkRx < cbChunk)
                return pExecMemAllocator->aChunks[idxChunk].pCtx;
    }
    return NULL;
}


#ifdef IN_RING3
# ifdef RT_OS_WINDOWS

/**
 * Initializes the unwind info structures for windows hosts.
 */
static int
iemExecMemAllocatorInitAndRegisterUnwindInfoForChunk(PVMCPUCC pVCpu, PIEMEXECMEMALLOCATOR pExecMemAllocator,
                                                     void *pvChunk, uint32_t idxChunk)
{
    RT_NOREF(pVCpu);

    /*
     * The AMD64 unwind opcodes.
     *
     * This is a program that starts with RSP after a RET instruction that
     * ends up in recompiled code, and the operations we describe here will
     * restore all non-volatile registers and bring RSP back to where our
     * RET address is.  This means it's reverse order from what happens in
     * the prologue.
     *
     * Note! Using a frame register approach here both because we have one
     *       and but mainly because the UWOP_ALLOC_LARGE argument values
     *       would be a pain to write initializers for.  On the positive
     *       side, we're impervious to changes in the the stack variable
     *       area can can deal with dynamic stack allocations if necessary.
     */
    static const IMAGE_UNWIND_CODE s_aOpcodes[] =
    {
        { { 16, IMAGE_AMD64_UWOP_SET_FPREG,     0 } },              /* RSP  = RBP - FrameOffset * 10 (0x60) */
        { { 16, IMAGE_AMD64_UWOP_ALLOC_SMALL,   0 } },              /* RSP += 8; */
        { { 14, IMAGE_AMD64_UWOP_PUSH_NONVOL,   X86_GREG_x15 } },   /* R15  = [RSP]; RSP += 8; */
        { { 12, IMAGE_AMD64_UWOP_PUSH_NONVOL,   X86_GREG_x14 } },   /* R14  = [RSP]; RSP += 8; */
        { { 10, IMAGE_AMD64_UWOP_PUSH_NONVOL,   X86_GREG_x13 } },   /* R13  = [RSP]; RSP += 8; */
        { {  8, IMAGE_AMD64_UWOP_PUSH_NONVOL,   X86_GREG_x12 } },   /* R12  = [RSP]; RSP += 8; */
        { {  7, IMAGE_AMD64_UWOP_PUSH_NONVOL,   X86_GREG_xDI } },   /* RDI  = [RSP]; RSP += 8; */
        { {  6, IMAGE_AMD64_UWOP_PUSH_NONVOL,   X86_GREG_xSI } },   /* RSI  = [RSP]; RSP += 8; */
        { {  5, IMAGE_AMD64_UWOP_PUSH_NONVOL,   X86_GREG_xBX } },   /* RBX  = [RSP]; RSP += 8; */
        { {  4, IMAGE_AMD64_UWOP_PUSH_NONVOL,   X86_GREG_xBP } },   /* RBP  = [RSP]; RSP += 8; */
    };
    union
    {
        IMAGE_UNWIND_INFO Info;
        uint8_t abPadding[RT_UOFFSETOF(IMAGE_UNWIND_INFO, aOpcodes) + 16];
    } s_UnwindInfo =
    {
        {
            /* .Version = */        1,
            /* .Flags = */          0,
            /* .SizeOfProlog = */   16, /* whatever */
            /* .CountOfCodes = */   RT_ELEMENTS(s_aOpcodes),
            /* .FrameRegister = */  X86_GREG_xBP,
            /* .FrameOffset = */    (-IEMNATIVE_FP_OFF_LAST_PUSH + 8) / 16 /* we're off by one slot. sigh. */,
        }
    };
    AssertCompile(-IEMNATIVE_FP_OFF_LAST_PUSH < 240 && -IEMNATIVE_FP_OFF_LAST_PUSH > 0);
    AssertCompile((-IEMNATIVE_FP_OFF_LAST_PUSH & 0xf) == 8);

    /*
     * Calc how much space we need and allocate it off the exec heap.
     */
    unsigned const cFunctionEntries = 1;
    unsigned const cbUnwindInfo     = sizeof(s_aOpcodes) + RT_UOFFSETOF(IMAGE_UNWIND_INFO, aOpcodes);
    unsigned const cbNeeded         = sizeof(IMAGE_RUNTIME_FUNCTION_ENTRY) * cFunctionEntries + cbUnwindInfo;
    PIMAGE_RUNTIME_FUNCTION_ENTRY const paFunctions
        = (PIMAGE_RUNTIME_FUNCTION_ENTRY)iemExecMemAllocatorAllocBytesInChunk(pExecMemAllocator, idxChunk, cbNeeded, NULL);
    AssertReturn(paFunctions, VERR_INTERNAL_ERROR_5);
    pExecMemAllocator->aChunks[idxChunk].pvUnwindInfo = paFunctions;

    /*
     * Initialize the structures.
     */
    PIMAGE_UNWIND_INFO const pInfo = (PIMAGE_UNWIND_INFO)&paFunctions[cFunctionEntries];

    paFunctions[0].BeginAddress         = 0;
    paFunctions[0].EndAddress           = pExecMemAllocator->cbChunk;
    paFunctions[0].UnwindInfoAddress    = (uint32_t)((uintptr_t)pInfo - (uintptr_t)pvChunk);

    memcpy(pInfo, &s_UnwindInfo, RT_UOFFSETOF(IMAGE_UNWIND_INFO, aOpcodes));
    memcpy(&pInfo->aOpcodes[0], s_aOpcodes, sizeof(s_aOpcodes));

    /*
     * Register it.
     */
    uint8_t fRet = RtlAddFunctionTable(paFunctions, cFunctionEntries, (uintptr_t)pvChunk);
    AssertReturn(fRet, VERR_INTERNAL_ERROR_3); /* Nothing to clean up on failure, since its within the chunk itself. */

    return VINF_SUCCESS;
}


# else /* !RT_OS_WINDOWS */

/**
 * Emits a LEB128 encoded value between -0x2000 and 0x2000 (both exclusive).
 */
DECLINLINE(RTPTRUNION) iemDwarfPutLeb128(RTPTRUNION Ptr, int32_t iValue)
{
    if (iValue >= 64)
    {
        Assert(iValue < 0x2000);
        *Ptr.pb++ = ((uint8_t)iValue & 0x7f) | 0x80;
        *Ptr.pb++ = (uint8_t)(iValue >> 7) & 0x3f;
    }
    else if (iValue >= 0)
        *Ptr.pb++ = (uint8_t)iValue;
    else if (iValue > -64)
        *Ptr.pb++ = ((uint8_t)iValue & 0x3f) | 0x40;
    else
    {
        Assert(iValue > -0x2000);
        *Ptr.pb++ = ((uint8_t)iValue & 0x7f)        | 0x80;
        *Ptr.pb++ = ((uint8_t)(iValue >> 7) & 0x3f) | 0x40;
    }
    return Ptr;
}


/**
 * Emits an ULEB128 encoded value (up to 64-bit wide).
 */
DECLINLINE(RTPTRUNION) iemDwarfPutUleb128(RTPTRUNION Ptr, uint64_t uValue)
{
    while (uValue >= 0x80)
    {
        *Ptr.pb++ = ((uint8_t)uValue & 0x7f) | 0x80;
        uValue  >>= 7;
    }
    *Ptr.pb++ = (uint8_t)uValue;
    return Ptr;
}


/**
 * Emits a CFA rule as register @a uReg + offset @a off.
 */
DECLINLINE(RTPTRUNION) iemDwarfPutCfaDefCfa(RTPTRUNION Ptr, uint32_t uReg, uint32_t off)
{
    *Ptr.pb++ = DW_CFA_def_cfa;
    Ptr = iemDwarfPutUleb128(Ptr, uReg);
    Ptr = iemDwarfPutUleb128(Ptr, off);
    return Ptr;
}


/**
 * Emits a register (@a uReg) save location:
 *      CFA + @a off * data_alignment_factor
 */
DECLINLINE(RTPTRUNION) iemDwarfPutCfaOffset(RTPTRUNION Ptr, uint32_t uReg, uint32_t off)
{
    if (uReg < 0x40)
        *Ptr.pb++ = DW_CFA_offset | uReg;
    else
    {
        *Ptr.pb++ = DW_CFA_offset_extended;
        Ptr = iemDwarfPutUleb128(Ptr, uReg);
    }
    Ptr = iemDwarfPutUleb128(Ptr, off);
    return Ptr;
}


#  if 0 /* unused */
/**
 * Emits a register (@a uReg) save location, using signed offset:
 *      CFA + @a offSigned * data_alignment_factor
 */
DECLINLINE(RTPTRUNION) iemDwarfPutCfaSignedOffset(RTPTRUNION Ptr, uint32_t uReg, int32_t offSigned)
{
    *Ptr.pb++ = DW_CFA_offset_extended_sf;
    Ptr = iemDwarfPutUleb128(Ptr, uReg);
    Ptr = iemDwarfPutLeb128(Ptr, offSigned);
    return Ptr;
}
#  endif


/**
 * Initializes the unwind info section for non-windows hosts.
 */
static int
iemExecMemAllocatorInitAndRegisterUnwindInfoForChunk(PVMCPUCC pVCpu, PIEMEXECMEMALLOCATOR pExecMemAllocator,
                                                     void *pvChunk, uint32_t idxChunk)
{
    PIEMEXECMEMCHUNKEHFRAME const pEhFrame = &pExecMemAllocator->paEhFrames[idxChunk];
    pExecMemAllocator->aChunks[idxChunk].pvUnwindInfo = pEhFrame; /* not necessary, but whatever */

    RTPTRUNION Ptr = { pEhFrame->abEhFrame };

    /*
     * Generate the CIE first.
     */
#  ifdef IEMNATIVE_USE_LIBUNWIND /* libunwind (llvm, darwin) only supports v1 and v3. */
    uint8_t const iDwarfVer = 3;
#  else
    uint8_t const iDwarfVer = 4;
#  endif
    RTPTRUNION const PtrCie = Ptr;
    *Ptr.pu32++ = 123;                                      /* The CIE length will be determined later. */
    *Ptr.pu32++ = 0 /*UINT32_MAX*/;                         /* I'm a CIE in .eh_frame speak. */
    *Ptr.pb++   = iDwarfVer;                                /* DwARF version */
    *Ptr.pb++   = 0;                                        /* Augmentation. */
    if (iDwarfVer >= 4)
    {
        *Ptr.pb++   = sizeof(uintptr_t);                    /* Address size. */
        *Ptr.pb++   = 0;                                    /* Segment selector size. */
    }
#  ifdef RT_ARCH_AMD64
    Ptr = iemDwarfPutLeb128(Ptr, 1);                        /* Code alignment factor (LEB128 = 1). */
#  else
    Ptr = iemDwarfPutLeb128(Ptr, 4);                        /* Code alignment factor (LEB128 = 4). */
#  endif
    Ptr = iemDwarfPutLeb128(Ptr, -8);                       /* Data alignment factor (LEB128 = -8). */
#  ifdef RT_ARCH_AMD64
    Ptr = iemDwarfPutUleb128(Ptr, DWREG_AMD64_RA);          /* Return address column (ULEB128) */
#  elif defined(RT_ARCH_ARM64)
    Ptr = iemDwarfPutUleb128(Ptr, DWREG_ARM64_LR);          /* Return address column (ULEB128) */
#  else
#   error "port me"
#  endif
    /* Initial instructions: */
#  ifdef RT_ARCH_AMD64
    Ptr = iemDwarfPutCfaDefCfa(Ptr, DWREG_AMD64_RBP, 16);   /* CFA     = RBP + 0x10 - first stack parameter */
    Ptr = iemDwarfPutCfaOffset(Ptr, DWREG_AMD64_RA,  1);    /* Ret RIP = [CFA + 1*-8] */
    Ptr = iemDwarfPutCfaOffset(Ptr, DWREG_AMD64_RBP, 2);    /* RBP     = [CFA + 2*-8] */
    Ptr = iemDwarfPutCfaOffset(Ptr, DWREG_AMD64_RBX, 3);    /* RBX     = [CFA + 3*-8] */
    Ptr = iemDwarfPutCfaOffset(Ptr, DWREG_AMD64_R12, 4);    /* R12     = [CFA + 4*-8] */
    Ptr = iemDwarfPutCfaOffset(Ptr, DWREG_AMD64_R13, 5);    /* R13     = [CFA + 5*-8] */
    Ptr = iemDwarfPutCfaOffset(Ptr, DWREG_AMD64_R14, 6);    /* R14     = [CFA + 6*-8] */
    Ptr = iemDwarfPutCfaOffset(Ptr, DWREG_AMD64_R15, 7);    /* R15     = [CFA + 7*-8] */
#  elif defined(RT_ARCH_ARM64)
#   if 1
    Ptr = iemDwarfPutCfaDefCfa(Ptr, DWREG_ARM64_BP,  16);   /* CFA     = BP + 0x10 - first stack parameter */
#   else
    Ptr = iemDwarfPutCfaDefCfa(Ptr, DWREG_ARM64_SP,  IEMNATIVE_FRAME_VAR_SIZE + IEMNATIVE_FRAME_SAVE_REG_SIZE);
#   endif
    Ptr = iemDwarfPutCfaOffset(Ptr, DWREG_ARM64_LR,   1);   /* Ret PC  = [CFA + 1*-8] */
    Ptr = iemDwarfPutCfaOffset(Ptr, DWREG_ARM64_BP,   2);   /* Ret BP  = [CFA + 2*-8] */
    Ptr = iemDwarfPutCfaOffset(Ptr, DWREG_ARM64_X28,  3);   /* X28     = [CFA + 3*-8] */
    Ptr = iemDwarfPutCfaOffset(Ptr, DWREG_ARM64_X27,  4);   /* X27     = [CFA + 4*-8] */
    Ptr = iemDwarfPutCfaOffset(Ptr, DWREG_ARM64_X26,  5);   /* X26     = [CFA + 5*-8] */
    Ptr = iemDwarfPutCfaOffset(Ptr, DWREG_ARM64_X25,  6);   /* X25     = [CFA + 6*-8] */
    Ptr = iemDwarfPutCfaOffset(Ptr, DWREG_ARM64_X24,  7);   /* X24     = [CFA + 7*-8] */
    Ptr = iemDwarfPutCfaOffset(Ptr, DWREG_ARM64_X23,  8);   /* X23     = [CFA + 8*-8] */
    Ptr = iemDwarfPutCfaOffset(Ptr, DWREG_ARM64_X22,  9);   /* X22     = [CFA + 9*-8] */
    Ptr = iemDwarfPutCfaOffset(Ptr, DWREG_ARM64_X21, 10);   /* X21     = [CFA +10*-8] */
    Ptr = iemDwarfPutCfaOffset(Ptr, DWREG_ARM64_X20, 11);   /* X20     = [CFA +11*-8] */
    Ptr = iemDwarfPutCfaOffset(Ptr, DWREG_ARM64_X19, 12);   /* X19     = [CFA +12*-8] */
    AssertCompile(IEMNATIVE_FRAME_SAVE_REG_SIZE / 8 == 12);
    /** @todo we we need to do something about clearing DWREG_ARM64_RA_SIGN_STATE or something? */
#  else
#   error "port me"
#  endif
    while ((Ptr.u - PtrCie.u) & 3)
        *Ptr.pb++ = DW_CFA_nop;
    /* Finalize the CIE size. */
    *PtrCie.pu32 = Ptr.u - PtrCie.u - sizeof(uint32_t);

    /*
     * Generate an FDE for the whole chunk area.
     */
#  ifdef IEMNATIVE_USE_LIBUNWIND
    pEhFrame->offFda = Ptr.u - (uintptr_t)&pEhFrame->abEhFrame[0];
#  endif
    RTPTRUNION const PtrFde = Ptr;
    *Ptr.pu32++ = 123;                                      /* The CIE length will be determined later. */
    *Ptr.pu32   = Ptr.u - PtrCie.u;                         /* Negated self relative CIE address. */
    Ptr.pu32++;
    *Ptr.pu64++ = (uintptr_t)pvChunk;                       /* Absolute start PC of this FDE. */
    *Ptr.pu64++ = pExecMemAllocator->cbChunk;               /* PC range length for this PDE. */
#  if 0 /* not requried for recent libunwind.dylib nor recent libgcc/glib. */
    *Ptr.pb++ = DW_CFA_nop;
#  endif
    while ((Ptr.u - PtrFde.u) & 3)
        *Ptr.pb++ = DW_CFA_nop;
    /* Finalize the FDE size. */
    *PtrFde.pu32 = Ptr.u - PtrFde.u - sizeof(uint32_t);

    /* Terminator entry. */
    *Ptr.pu32++ = 0;
    *Ptr.pu32++ = 0;            /* just to be sure... */
    Assert(Ptr.u - (uintptr_t)&pEhFrame->abEhFrame[0] <= sizeof(pEhFrame->abEhFrame));

    /*
     * Register it.
     */
#  ifdef IEMNATIVE_USE_LIBUNWIND
    __register_frame(&pEhFrame->abEhFrame[pEhFrame->offFda]);
#  else
    memset(pEhFrame->abObject, 0xf6, sizeof(pEhFrame->abObject)); /* color the memory to better spot usage */
    __register_frame_info(pEhFrame->abEhFrame, pEhFrame->abObject);
#  endif

#  ifdef IEMNATIVE_USE_GDB_JIT
    /*
     * Now for telling GDB about this (experimental).
     *
     * This seems to work best with ET_DYN.
     */
    GDBJITSYMFILE * const pSymFile = (GDBJITSYMFILE *)iemExecMemAllocatorAllocBytesInChunk(pExecMemAllocator, idxChunk,
                                                                                           sizeof(GDBJITSYMFILE), NULL);
    AssertReturn(pSymFile, VERR_INTERNAL_ERROR_5);
    unsigned const offSymFileInChunk = (uintptr_t)pSymFile - (uintptr_t)pvChunk;

    RT_ZERO(*pSymFile);

    /*
     * The ELF header:
     */
    pSymFile->EHdr.e_ident[0]           = ELFMAG0;
    pSymFile->EHdr.e_ident[1]           = ELFMAG1;
    pSymFile->EHdr.e_ident[2]           = ELFMAG2;
    pSymFile->EHdr.e_ident[3]           = ELFMAG3;
    pSymFile->EHdr.e_ident[EI_VERSION]  = EV_CURRENT;
    pSymFile->EHdr.e_ident[EI_CLASS]    = ELFCLASS64;
    pSymFile->EHdr.e_ident[EI_DATA]     = ELFDATA2LSB;
    pSymFile->EHdr.e_ident[EI_OSABI]    = ELFOSABI_NONE;
#   ifdef IEMNATIVE_USE_GDB_JIT_ET_DYN
    pSymFile->EHdr.e_type               = ET_DYN;
#   else
    pSymFile->EHdr.e_type               = ET_REL;
#   endif
#   ifdef RT_ARCH_AMD64
    pSymFile->EHdr.e_machine            = EM_AMD64;
#   elif defined(RT_ARCH_ARM64)
    pSymFile->EHdr.e_machine            = EM_AARCH64;
#   else
#    error "port me"
#   endif
    pSymFile->EHdr.e_version            = 1; /*?*/
    pSymFile->EHdr.e_entry              = 0;
#   if defined(IEMNATIVE_USE_GDB_JIT_ET_DYN)
    pSymFile->EHdr.e_phoff              = RT_UOFFSETOF(GDBJITSYMFILE, aPhdrs);
#   else
    pSymFile->EHdr.e_phoff              = 0;
#   endif
    pSymFile->EHdr.e_shoff              = sizeof(pSymFile->EHdr);
    pSymFile->EHdr.e_flags              = 0;
    pSymFile->EHdr.e_ehsize             = sizeof(pSymFile->EHdr);
#   if defined(IEMNATIVE_USE_GDB_JIT_ET_DYN)
    pSymFile->EHdr.e_phentsize          = sizeof(pSymFile->aPhdrs[0]);
    pSymFile->EHdr.e_phnum              = RT_ELEMENTS(pSymFile->aPhdrs);
#   else
    pSymFile->EHdr.e_phentsize          = 0;
    pSymFile->EHdr.e_phnum              = 0;
#   endif
    pSymFile->EHdr.e_shentsize          = sizeof(pSymFile->aShdrs[0]);
    pSymFile->EHdr.e_shnum              = RT_ELEMENTS(pSymFile->aShdrs);
    pSymFile->EHdr.e_shstrndx           = 0; /* set later */

    uint32_t offStrTab = 0;
#define APPEND_STR(a_szStr) do { \
        memcpy(&pSymFile->szzStrTab[offStrTab], a_szStr, sizeof(a_szStr)); \
        offStrTab += sizeof(a_szStr); \
        Assert(offStrTab < sizeof(pSymFile->szzStrTab)); \
    } while (0)
#define APPEND_STR_FMT(a_szStr, ...) do { \
        offStrTab += RTStrPrintf(&pSymFile->szzStrTab[offStrTab], sizeof(pSymFile->szzStrTab) - offStrTab, a_szStr, __VA_ARGS__); \
        offStrTab++; \
        Assert(offStrTab < sizeof(pSymFile->szzStrTab)); \
    } while (0)

    /*
     * Section headers.
     */
    /* Section header #0: NULL */
    unsigned i = 0;
    APPEND_STR("");
    RT_ZERO(pSymFile->aShdrs[i]);
    i++;

    /* Section header: .eh_frame */
    pSymFile->aShdrs[i].sh_name         = offStrTab;
    APPEND_STR(".eh_frame");
    pSymFile->aShdrs[i].sh_type         = SHT_PROGBITS;
    pSymFile->aShdrs[i].sh_flags        = SHF_ALLOC | SHF_EXECINSTR;
#   if defined(IEMNATIVE_USE_GDB_JIT_ET_DYN) || defined(IEMNATIVE_USE_GDB_JIT_ELF_RVAS)
    pSymFile->aShdrs[i].sh_offset
        = pSymFile->aShdrs[i].sh_addr   = RT_UOFFSETOF(GDBJITSYMFILE, abEhFrame);
#   else
    pSymFile->aShdrs[i].sh_addr         = (uintptr_t)&pSymFile->abEhFrame[0];
    pSymFile->aShdrs[i].sh_offset       = 0;
#   endif

    pSymFile->aShdrs[i].sh_size         = sizeof(pEhFrame->abEhFrame);
    pSymFile->aShdrs[i].sh_link         = 0;
    pSymFile->aShdrs[i].sh_info         = 0;
    pSymFile->aShdrs[i].sh_addralign    = 1;
    pSymFile->aShdrs[i].sh_entsize      = 0;
    memcpy(pSymFile->abEhFrame, pEhFrame->abEhFrame, sizeof(pEhFrame->abEhFrame));
    i++;

    /* Section header: .shstrtab */
    unsigned const iShStrTab = i;
    pSymFile->EHdr.e_shstrndx           = iShStrTab;
    pSymFile->aShdrs[i].sh_name         = offStrTab;
    APPEND_STR(".shstrtab");
    pSymFile->aShdrs[i].sh_type         = SHT_STRTAB;
    pSymFile->aShdrs[i].sh_flags        = SHF_ALLOC;
#   if defined(IEMNATIVE_USE_GDB_JIT_ET_DYN) || defined(IEMNATIVE_USE_GDB_JIT_ELF_RVAS)
    pSymFile->aShdrs[i].sh_offset
        = pSymFile->aShdrs[i].sh_addr   = RT_UOFFSETOF(GDBJITSYMFILE, szzStrTab);
#   else
    pSymFile->aShdrs[i].sh_addr         = (uintptr_t)&pSymFile->szzStrTab[0];
    pSymFile->aShdrs[i].sh_offset       = 0;
#   endif
    pSymFile->aShdrs[i].sh_size         = sizeof(pSymFile->szzStrTab);
    pSymFile->aShdrs[i].sh_link         = 0;
    pSymFile->aShdrs[i].sh_info         = 0;
    pSymFile->aShdrs[i].sh_addralign    = 1;
    pSymFile->aShdrs[i].sh_entsize      = 0;
    i++;

    /* Section header: .symbols */
    pSymFile->aShdrs[i].sh_name         = offStrTab;
    APPEND_STR(".symtab");
    pSymFile->aShdrs[i].sh_type         = SHT_SYMTAB;
    pSymFile->aShdrs[i].sh_flags        = SHF_ALLOC;
    pSymFile->aShdrs[i].sh_offset
        = pSymFile->aShdrs[i].sh_addr   = RT_UOFFSETOF(GDBJITSYMFILE, aSymbols);
    pSymFile->aShdrs[i].sh_size         = sizeof(pSymFile->aSymbols);
    pSymFile->aShdrs[i].sh_link         = iShStrTab;
    pSymFile->aShdrs[i].sh_info         = RT_ELEMENTS(pSymFile->aSymbols);
    pSymFile->aShdrs[i].sh_addralign    = sizeof(pSymFile->aSymbols[0].st_value);
    pSymFile->aShdrs[i].sh_entsize      = sizeof(pSymFile->aSymbols[0]);
    i++;

#   if defined(IEMNATIVE_USE_GDB_JIT_ET_DYN)
    /* Section header: .symbols */
    pSymFile->aShdrs[i].sh_name         = offStrTab;
    APPEND_STR(".dynsym");
    pSymFile->aShdrs[i].sh_type         = SHT_DYNSYM;
    pSymFile->aShdrs[i].sh_flags        = SHF_ALLOC;
    pSymFile->aShdrs[i].sh_offset
        = pSymFile->aShdrs[i].sh_addr   = RT_UOFFSETOF(GDBJITSYMFILE, aDynSyms);
    pSymFile->aShdrs[i].sh_size         = sizeof(pSymFile->aDynSyms);
    pSymFile->aShdrs[i].sh_link         = iShStrTab;
    pSymFile->aShdrs[i].sh_info         = RT_ELEMENTS(pSymFile->aDynSyms);
    pSymFile->aShdrs[i].sh_addralign    = sizeof(pSymFile->aDynSyms[0].st_value);
    pSymFile->aShdrs[i].sh_entsize      = sizeof(pSymFile->aDynSyms[0]);
    i++;
#   endif

#   if defined(IEMNATIVE_USE_GDB_JIT_ET_DYN)
    /* Section header: .dynamic */
    pSymFile->aShdrs[i].sh_name         = offStrTab;
    APPEND_STR(".dynamic");
    pSymFile->aShdrs[i].sh_type         = SHT_DYNAMIC;
    pSymFile->aShdrs[i].sh_flags        = SHF_ALLOC;
    pSymFile->aShdrs[i].sh_offset
        = pSymFile->aShdrs[i].sh_addr   = RT_UOFFSETOF(GDBJITSYMFILE, aDyn);
    pSymFile->aShdrs[i].sh_size         = sizeof(pSymFile->aDyn);
    pSymFile->aShdrs[i].sh_link         = iShStrTab;
    pSymFile->aShdrs[i].sh_info         = 0;
    pSymFile->aShdrs[i].sh_addralign    = 1;
    pSymFile->aShdrs[i].sh_entsize      = sizeof(pSymFile->aDyn[0]);
    i++;
#   endif

    /* Section header: .text */
    unsigned const iShText = i;
    pSymFile->aShdrs[i].sh_name         = offStrTab;
    APPEND_STR(".text");
    pSymFile->aShdrs[i].sh_type         = SHT_PROGBITS;
    pSymFile->aShdrs[i].sh_flags        = SHF_ALLOC | SHF_EXECINSTR;
#   if defined(IEMNATIVE_USE_GDB_JIT_ET_DYN) || defined(IEMNATIVE_USE_GDB_JIT_ELF_RVAS)
    pSymFile->aShdrs[i].sh_offset
        = pSymFile->aShdrs[i].sh_addr   = sizeof(GDBJITSYMFILE);
#   else
    pSymFile->aShdrs[i].sh_addr         = (uintptr_t)(pSymFile + 1);
    pSymFile->aShdrs[i].sh_offset       = 0;
#   endif
    pSymFile->aShdrs[i].sh_size         = pExecMemAllocator->cbChunk - offSymFileInChunk - sizeof(GDBJITSYMFILE);
    pSymFile->aShdrs[i].sh_link         = 0;
    pSymFile->aShdrs[i].sh_info         = 0;
    pSymFile->aShdrs[i].sh_addralign    = 1;
    pSymFile->aShdrs[i].sh_entsize      = 0;
    i++;

    Assert(i == RT_ELEMENTS(pSymFile->aShdrs));

#   if defined(IEMNATIVE_USE_GDB_JIT_ET_DYN)
    /*
     * The program headers:
     */
    /* Everything in a single LOAD segment: */
    i = 0;
    pSymFile->aPhdrs[i].p_type          = PT_LOAD;
    pSymFile->aPhdrs[i].p_flags         = PF_X | PF_R;
    pSymFile->aPhdrs[i].p_offset
        = pSymFile->aPhdrs[i].p_vaddr
        = pSymFile->aPhdrs[i].p_paddr   = 0;
    pSymFile->aPhdrs[i].p_filesz         /* Size of segment in file. */
        = pSymFile->aPhdrs[i].p_memsz   = pExecMemAllocator->cbChunk - offSymFileInChunk;
    pSymFile->aPhdrs[i].p_align         = HOST_PAGE_SIZE;
    i++;
    /* The .dynamic segment. */
    pSymFile->aPhdrs[i].p_type          = PT_DYNAMIC;
    pSymFile->aPhdrs[i].p_flags         = PF_R;
    pSymFile->aPhdrs[i].p_offset
        = pSymFile->aPhdrs[i].p_vaddr
        = pSymFile->aPhdrs[i].p_paddr   = RT_UOFFSETOF(GDBJITSYMFILE, aDyn);
    pSymFile->aPhdrs[i].p_filesz         /* Size of segment in file. */
        = pSymFile->aPhdrs[i].p_memsz   = sizeof(pSymFile->aDyn);
    pSymFile->aPhdrs[i].p_align         = sizeof(pSymFile->aDyn[0].d_tag);
    i++;

    Assert(i == RT_ELEMENTS(pSymFile->aPhdrs));

    /*
     * The dynamic section:
     */
    i = 0;
    pSymFile->aDyn[i].d_tag             = DT_SONAME;
    pSymFile->aDyn[i].d_un.d_val        = offStrTab;
    APPEND_STR_FMT("iem-exec-chunk-%u-%u", pVCpu->idCpu, idxChunk);
    i++;
    pSymFile->aDyn[i].d_tag             = DT_STRTAB;
    pSymFile->aDyn[i].d_un.d_ptr        = RT_UOFFSETOF(GDBJITSYMFILE, szzStrTab);
    i++;
    pSymFile->aDyn[i].d_tag             = DT_STRSZ;
    pSymFile->aDyn[i].d_un.d_val        = sizeof(pSymFile->szzStrTab);
    i++;
    pSymFile->aDyn[i].d_tag             = DT_SYMTAB;
    pSymFile->aDyn[i].d_un.d_ptr        = RT_UOFFSETOF(GDBJITSYMFILE, aDynSyms);
    i++;
    pSymFile->aDyn[i].d_tag             = DT_SYMENT;
    pSymFile->aDyn[i].d_un.d_val        = sizeof(pSymFile->aDynSyms[0]);
    i++;
    pSymFile->aDyn[i].d_tag             = DT_NULL;
    i++;
    Assert(i == RT_ELEMENTS(pSymFile->aDyn));
#   endif /* IEMNATIVE_USE_GDB_JIT_ET_DYN */

    /*
     * Symbol tables:
     */
    /** @todo gdb doesn't seem to really like this ...   */
    i = 0;
    pSymFile->aSymbols[i].st_name       = 0;
    pSymFile->aSymbols[i].st_shndx      = SHN_UNDEF;
    pSymFile->aSymbols[i].st_value      = 0;
    pSymFile->aSymbols[i].st_size       = 0;
    pSymFile->aSymbols[i].st_info       = ELF64_ST_INFO(STB_LOCAL, STT_NOTYPE);
    pSymFile->aSymbols[i].st_other      = 0 /* STV_DEFAULT */;
#   ifdef IEMNATIVE_USE_GDB_JIT_ET_DYN
    pSymFile->aDynSyms[0] = pSymFile->aSymbols[i];
#   endif
    i++;

    pSymFile->aSymbols[i].st_name       = 0;
    pSymFile->aSymbols[i].st_shndx      = SHN_ABS;
    pSymFile->aSymbols[i].st_value      = 0;
    pSymFile->aSymbols[i].st_size       = 0;
    pSymFile->aSymbols[i].st_info       = ELF64_ST_INFO(STB_LOCAL, STT_FILE);
    pSymFile->aSymbols[i].st_other      = 0 /* STV_DEFAULT */;
    i++;

    pSymFile->aSymbols[i].st_name       = offStrTab;
    APPEND_STR_FMT("iem_exec_chunk_%u_%u", pVCpu->idCpu, idxChunk);
#   if 0
    pSymFile->aSymbols[i].st_shndx      = iShText;
    pSymFile->aSymbols[i].st_value      = 0;
#   else
    pSymFile->aSymbols[i].st_shndx      = SHN_ABS;
    pSymFile->aSymbols[i].st_value      = (uintptr_t)(pSymFile + 1);
#   endif
    pSymFile->aSymbols[i].st_size       = pSymFile->aShdrs[iShText].sh_size;
    pSymFile->aSymbols[i].st_info       = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
    pSymFile->aSymbols[i].st_other      = 0 /* STV_DEFAULT */;
#   ifdef IEMNATIVE_USE_GDB_JIT_ET_DYN
    pSymFile->aDynSyms[1] = pSymFile->aSymbols[i];
    pSymFile->aDynSyms[1].st_value      = (uintptr_t)(pSymFile + 1);
#   endif
    i++;

    Assert(i == RT_ELEMENTS(pSymFile->aSymbols));
    Assert(offStrTab < sizeof(pSymFile->szzStrTab));

    /*
     * The GDB JIT entry and informing GDB.
     */
    pEhFrame->GdbJitEntry.pbSymFile = (uint8_t *)pSymFile;
#   if 1
    pEhFrame->GdbJitEntry.cbSymFile = pExecMemAllocator->cbChunk - ((uintptr_t)pSymFile - (uintptr_t)pvChunk);
#   else
    pEhFrame->GdbJitEntry.cbSymFile = sizeof(GDBJITSYMFILE);
#   endif

    RTOnce(&g_IemNativeGdbJitOnce, iemNativeGdbJitInitOnce, NULL);
    RTCritSectEnter(&g_IemNativeGdbJitLock);
    pEhFrame->GdbJitEntry.pNext      = NULL;
    pEhFrame->GdbJitEntry.pPrev      = __jit_debug_descriptor.pTail;
    if (__jit_debug_descriptor.pTail)
        __jit_debug_descriptor.pTail->pNext = &pEhFrame->GdbJitEntry;
    else
        __jit_debug_descriptor.pHead = &pEhFrame->GdbJitEntry;
    __jit_debug_descriptor.pTail     = &pEhFrame->GdbJitEntry;
    __jit_debug_descriptor.pRelevant = &pEhFrame->GdbJitEntry;

    /* Notify GDB: */
    __jit_debug_descriptor.enmAction = kGdbJitaction_Register;
    __jit_debug_register_code();
    __jit_debug_descriptor.enmAction = kGdbJitaction_NoAction;
    RTCritSectLeave(&g_IemNativeGdbJitLock);

#  else  /* !IEMNATIVE_USE_GDB_JIT */
    RT_NOREF(pVCpu);
#  endif /* !IEMNATIVE_USE_GDB_JIT */

    return VINF_SUCCESS;
}

# endif /* !RT_OS_WINDOWS */
#endif /* IN_RING3 */


/**
 * Adds another chunk to the executable memory allocator.
 *
 * This is used by the init code for the initial allocation and later by the
 * regular allocator function when it's out of memory.
 */
static int iemExecMemAllocatorGrow(PVMCPUCC pVCpu, PIEMEXECMEMALLOCATOR pExecMemAllocator)
{
    /* Check that we've room for growth. */
    uint32_t const idxChunk = pExecMemAllocator->cChunks;
    AssertLogRelReturn(idxChunk < pExecMemAllocator->cMaxChunks, VERR_OUT_OF_RESOURCES);

    /* Allocate a chunk. */
#ifdef RT_OS_DARWIN
    void *pvChunk = RTMemPageAllocEx(pExecMemAllocator->cbChunk, 0);
#else
    void *pvChunk = RTMemPageAllocEx(pExecMemAllocator->cbChunk, RTMEMPAGEALLOC_F_EXECUTABLE);
#endif
    AssertLogRelReturn(pvChunk, VERR_NO_EXEC_MEMORY);

#ifdef RT_OS_DARWIN
    /*
     * Because it is impossible to have a RWX memory allocation on macOS try to remap the memory
     * chunk readable/executable somewhere else so we can save us the hassle of switching between
     * protections when exeuctable memory is allocated.
     */
    int               rc           = VERR_NO_EXEC_MEMORY;
    mach_port_t       hPortTask    = mach_task_self();
    mach_vm_address_t AddrChunk    = (mach_vm_address_t)pvChunk;
    mach_vm_address_t AddrRemapped = 0;
    vm_prot_t         ProtCur      = 0;
    vm_prot_t         ProtMax      = 0;
    kern_return_t krc = mach_vm_remap(hPortTask, &AddrRemapped, pExecMemAllocator->cbChunk, 0,
                                      VM_FLAGS_ANYWHERE | VM_FLAGS_RETURN_DATA_ADDR,
                                      hPortTask, AddrChunk, FALSE, &ProtCur, &ProtMax,
                                      VM_INHERIT_NONE);
    if (krc == KERN_SUCCESS)
    {
        krc = mach_vm_protect(mach_task_self(), AddrRemapped, pExecMemAllocator->cbChunk, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
        if (krc == KERN_SUCCESS)
            rc = VINF_SUCCESS;
        else
        {
            AssertLogRelMsgFailed(("mach_vm_protect -> %d (%#x)\n", krc, krc));
            krc = mach_vm_deallocate(hPortTask, AddrRemapped, pExecMemAllocator->cbChunk);
            Assert(krc == KERN_SUCCESS);
        }
    }
    else
        AssertLogRelMsgFailed(("mach_vm_remap -> %d (%#x)\n", krc, krc));
    if (RT_FAILURE(rc))
    {
        RTMemPageFree(pvChunk, pExecMemAllocator->cbChunk);
        return rc;
    }

    void *pvChunkRx = (void *)AddrRemapped;
#else
    int   rc        = VINF_SUCCESS;
    void *pvChunkRx = pvChunk;
#endif

    /*
     * Add the chunk.
     *
     * This must be done before the unwind init so windows can allocate
     * memory from the chunk when using the alternative sub-allocator.
     */
    pExecMemAllocator->aChunks[idxChunk].pvChunkRw    = pvChunk;
    pExecMemAllocator->aChunks[idxChunk].pvChunkRx    = pvChunkRx;
#ifdef IN_RING3
    pExecMemAllocator->aChunks[idxChunk].pvUnwindInfo = NULL;
#endif
    pExecMemAllocator->aChunks[idxChunk].cFreeUnits   = pExecMemAllocator->cUnitsPerChunk;
    pExecMemAllocator->aChunks[idxChunk].idxFreeHint  = 0;
    memset(&pExecMemAllocator->pbmAlloc[pExecMemAllocator->cBitmapElementsPerChunk * idxChunk],
           0, sizeof(pExecMemAllocator->pbmAlloc[0]) * pExecMemAllocator->cBitmapElementsPerChunk);

    pExecMemAllocator->cChunks      = idxChunk + 1;
    pExecMemAllocator->idxChunkHint = idxChunk;

    pExecMemAllocator->cbTotal     += pExecMemAllocator->cbChunk;
    pExecMemAllocator->cbFree      += pExecMemAllocator->cbChunk;

    /* If there is a chunk context init callback call it. */
    rc = iemNativeRecompileAttachExecMemChunkCtx(pVCpu, idxChunk, &pExecMemAllocator->aChunks[idxChunk].pCtx);
#ifdef IN_RING3
    /*
     * Initialize the unwind information (this cannot really fail atm).
     * (This sets pvUnwindInfo.)
     */
    if (RT_SUCCESS(rc))
        rc = iemExecMemAllocatorInitAndRegisterUnwindInfoForChunk(pVCpu, pExecMemAllocator, pvChunkRx, idxChunk);
#endif
    if (RT_SUCCESS(rc))
    { /* likely */ }
    else
    {
        /* Just in case the impossible happens, undo the above up: */
        pExecMemAllocator->cbTotal -= pExecMemAllocator->cbChunk;
        pExecMemAllocator->cbFree  -= pExecMemAllocator->aChunks[idxChunk].cFreeUnits << IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SHIFT;
        pExecMemAllocator->cChunks  = idxChunk;
        memset(&pExecMemAllocator->pbmAlloc[pExecMemAllocator->cBitmapElementsPerChunk * idxChunk],
               0xff, sizeof(pExecMemAllocator->pbmAlloc[0]) * pExecMemAllocator->cBitmapElementsPerChunk);
        pExecMemAllocator->aChunks[idxChunk].pvChunkRw  = NULL;
        pExecMemAllocator->aChunks[idxChunk].cFreeUnits = 0;

# ifdef RT_OS_DARWIN
        krc = mach_vm_deallocate(mach_task_self(), (mach_vm_address_t)pExecMemAllocator->aChunks[idxChunk].pvChunkRx,
                                 pExecMemAllocator->cbChunk);
        Assert(krc == KERN_SUCCESS);
# endif

        RTMemPageFree(pvChunk, pExecMemAllocator->cbChunk);
        return rc;
    }

    return VINF_SUCCESS;
}


/**
 * Initializes the executable memory allocator for native recompilation on the
 * calling EMT.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure of the calling
 *                      thread.
 * @param   cbMax       The max size of the allocator.
 * @param   cbInitial   The initial allocator size.
 * @param   cbChunk     The chunk size, 0 or UINT32_MAX for default (@a cbMax
 *                      dependent).
 */
int iemExecMemAllocatorInit(PVMCPU pVCpu, uint64_t cbMax, uint64_t cbInitial, uint32_t cbChunk) RT_NOEXCEPT
{
    /*
     * Validate input.
     */
    AssertLogRelMsgReturn(cbMax >= _1M && cbMax <= _4G+_4G, ("cbMax=%RU64 (%RX64)\n", cbMax, cbMax), VERR_OUT_OF_RANGE);
    AssertReturn(cbInitial <= cbMax, VERR_OUT_OF_RANGE);
    AssertLogRelMsgReturn(   cbChunk != UINT32_MAX
                          || cbChunk == 0
                          || (   RT_IS_POWER_OF_TWO(cbChunk)
                              && cbChunk >= _1M
                              && cbChunk <= _256M
                              && cbChunk <= cbMax),
                          ("cbChunk=%RU32 (%RX32) cbMax=%RU64\n", cbChunk, cbChunk, cbMax),
                          VERR_OUT_OF_RANGE);

    /*
     * Adjust/figure out the chunk size.
     */
    if (cbChunk == 0 || cbChunk == UINT32_MAX)
    {
        if (cbMax >= _256M)
            cbChunk = _64M;
        else
        {
            if (cbMax < _16M)
                cbChunk = cbMax >= _4M ? _4M : (uint32_t)cbMax;
            else
                cbChunk = (uint32_t)cbMax / 4;
            if (!RT_IS_POWER_OF_TWO(cbChunk))
                cbChunk = RT_BIT_32(ASMBitLastSetU32(cbChunk));
        }
    }
#if   defined(RT_OS_AMD64)
    Assert(cbChunk <= _2G);
#elif defined(RT_OS_ARM64)
    if (cbChunk > _128M)
        cbChunk = _128M; /* Max relative branch distance is +/-2^(25+2) = +/-0x8000000 (134 217 728). */
#endif

    if (cbChunk > cbMax)
        cbMax = cbChunk;
    else
        cbMax = (cbMax - 1 + cbChunk) / cbChunk * cbChunk;
    uint32_t const cMaxChunks = (uint32_t)(cbMax / cbChunk);
    AssertLogRelReturn((uint64_t)cMaxChunks * cbChunk == cbMax, VERR_INTERNAL_ERROR_3);

    /*
     * Allocate and initialize the allocatore instance.
     */
    size_t const offBitmaps = RT_ALIGN_Z(RT_UOFFSETOF_DYN(IEMEXECMEMALLOCATOR, aChunks[cMaxChunks]), RT_CACHELINE_SIZE);
    size_t const cbBitmaps  = (size_t)(cbChunk >> (IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SHIFT + 3)) * cMaxChunks;
    size_t       cbNeeded   = offBitmaps + cbBitmaps;
    AssertCompile(IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SHIFT <= 10);
    Assert(cbChunk > RT_BIT_32(IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SHIFT + 3));
#if defined(IN_RING3) && !defined(RT_OS_WINDOWS)
    size_t const offEhFrames = RT_ALIGN_Z(cbNeeded, RT_CACHELINE_SIZE);
    cbNeeded += sizeof(IEMEXECMEMCHUNKEHFRAME) * cMaxChunks;
#endif
    PIEMEXECMEMALLOCATOR pExecMemAllocator = (PIEMEXECMEMALLOCATOR)RTMemAllocZ(cbNeeded);
    AssertLogRelMsgReturn(pExecMemAllocator, ("cbNeeded=%zx cMaxChunks=%#x cbChunk=%#x\n", cbNeeded, cMaxChunks, cbChunk),
                          VERR_NO_MEMORY);
    pExecMemAllocator->uMagic       = IEMEXECMEMALLOCATOR_MAGIC;
    pExecMemAllocator->cbChunk      = cbChunk;
    pExecMemAllocator->cMaxChunks   = cMaxChunks;
    pExecMemAllocator->cChunks      = 0;
    pExecMemAllocator->idxChunkHint = 0;
    pExecMemAllocator->cAllocations = 0;
    pExecMemAllocator->cbTotal      = 0;
    pExecMemAllocator->cbFree       = 0;
    pExecMemAllocator->cbAllocated  = 0;
#ifdef VBOX_WITH_STATISTICS
    pExecMemAllocator->cbUnusable   = 0;
#endif
    pExecMemAllocator->pbmAlloc                 = (uint64_t *)((uintptr_t)pExecMemAllocator + offBitmaps);
    pExecMemAllocator->cUnitsPerChunk           = cbChunk >> IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SHIFT;
    pExecMemAllocator->cBitmapElementsPerChunk  = cbChunk >> (IEMEXECMEM_ALT_SUB_ALLOC_UNIT_SHIFT + 6);
    memset(pExecMemAllocator->pbmAlloc, 0xff, cbBitmaps); /* Mark everything as allocated. Clear when chunks are added. */
#if defined(IN_RING3) && !defined(RT_OS_WINDOWS)
    pExecMemAllocator->paEhFrames = (PIEMEXECMEMCHUNKEHFRAME)((uintptr_t)pExecMemAllocator + offEhFrames);
#endif
    for (uint32_t i = 0; i < cMaxChunks; i++)
    {
        pExecMemAllocator->aChunks[i].cFreeUnits   = 0;
        pExecMemAllocator->aChunks[i].idxFreeHint  = 0;
        pExecMemAllocator->aChunks[i].pvChunkRw    = NULL;
#ifdef IN_RING0
        pExecMemAllocator->aChunks[i].hMemObj      = NIL_RTR0MEMOBJ;
#else
        pExecMemAllocator->aChunks[i].pvUnwindInfo = NULL;
#endif
    }
    pVCpu->iem.s.pExecMemAllocatorR3 = pExecMemAllocator;

    /*
     * Do the initial allocations.
     */
    while (cbInitial < (uint64_t)pExecMemAllocator->cChunks * pExecMemAllocator->cbChunk)
    {
        int rc = iemExecMemAllocatorGrow(pVCpu, pExecMemAllocator);
        AssertLogRelRCReturn(rc, rc);
    }

    pExecMemAllocator->idxChunkHint = 0;

    /*
     * Register statistics.
     */
    PUVM const pUVM = pVCpu->pUVCpu->pUVM;
    STAMR3RegisterFU(pUVM, &pExecMemAllocator->cAllocations,    STAMTYPE_U32, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,
                     "Current number of allocations",           "/IEM/CPU%u/re/ExecMem/cAllocations", pVCpu->idCpu);
    STAMR3RegisterFU(pUVM, &pExecMemAllocator->cChunks,         STAMTYPE_U32, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                     "Currently allocated chunks",              "/IEM/CPU%u/re/ExecMem/cChunks", pVCpu->idCpu);
    STAMR3RegisterFU(pUVM, &pExecMemAllocator->cMaxChunks,      STAMTYPE_U32, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                     "Maximum number of chunks",                "/IEM/CPU%u/re/ExecMem/cMaxChunks", pVCpu->idCpu);
    STAMR3RegisterFU(pUVM, &pExecMemAllocator->cbChunk,         STAMTYPE_U32, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,
                     "Allocation chunk size",                   "/IEM/CPU%u/re/ExecMem/cbChunk", pVCpu->idCpu);
    STAMR3RegisterFU(pUVM, &pExecMemAllocator->cbAllocated,     STAMTYPE_U64, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,
                     "Number of bytes current allocated",       "/IEM/CPU%u/re/ExecMem/cbAllocated", pVCpu->idCpu);
    STAMR3RegisterFU(pUVM, &pExecMemAllocator->cbFree,          STAMTYPE_U64, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,
                     "Number of bytes current free",            "/IEM/CPU%u/re/ExecMem/cbFree", pVCpu->idCpu);
    STAMR3RegisterFU(pUVM, &pExecMemAllocator->cbTotal,         STAMTYPE_U64, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,
                     "Total number of byte",                    "/IEM/CPU%u/re/ExecMem/cbTotal", pVCpu->idCpu);
#ifdef VBOX_WITH_STATISTICS
    STAMR3RegisterFU(pUVM, &pExecMemAllocator->cbUnusable,      STAMTYPE_U64, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,
                     "Total number of bytes being unusable",    "/IEM/CPU%u/re/ExecMem/cbUnusable", pVCpu->idCpu);
    STAMR3RegisterFU(pUVM, &pExecMemAllocator->StatAlloc,       STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,
                     "Profiling the allocator",                 "/IEM/CPU%u/re/ExecMem/ProfAlloc", pVCpu->idCpu);
    for (unsigned i = 1; i < RT_ELEMENTS(pExecMemAllocator->aStatSizes); i++)
        STAMR3RegisterFU(pUVM, &pExecMemAllocator->aStatSizes[i], STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                         "Number of allocations of this number of allocation units",
                         "/IEM/CPU%u/re/ExecMem/aSize%02u", pVCpu->idCpu, i);
    STAMR3RegisterFU(pUVM, &pExecMemAllocator->aStatSizes[0], STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                     "Number of allocations 16 units or larger", "/IEM/CPU%u/re/ExecMem/aSize16OrLarger", pVCpu->idCpu);
#endif
#ifdef IEMEXECMEM_ALT_SUB_WITH_ALT_PRUNING
    STAMR3RegisterFU(pUVM, &pExecMemAllocator->StatPruneProf,   STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,
                     "Pruning executable memory (alt)",         "/IEM/CPU%u/re/ExecMem/Pruning", pVCpu->idCpu);
    STAMR3RegisterFU(pUVM, &pExecMemAllocator->StatPruneRecovered, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES_PER_CALL,
                     "Bytes recovered while pruning",           "/IEM/CPU%u/re/ExecMem/PruningRecovered", pVCpu->idCpu);
#endif
    STAMR3RegisterFU(pUVM, &pExecMemAllocator->cFruitlessChunkScans, STAMTYPE_U64_RESET, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                     "Chunks fruitlessly scanned for free space", "/IEM/CPU%u/re/ExecMem/FruitlessChunkScans", pVCpu->idCpu);

    return VINF_SUCCESS;
}

