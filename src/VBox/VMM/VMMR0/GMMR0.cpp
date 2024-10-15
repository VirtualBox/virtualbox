/* $Id: GMMR0.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * GMM - Global Memory Manager.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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


/** @page pg_gmm    GMM - The Global Memory Manager
 *
 * As the name indicates, this component is responsible for global memory
 * management. Currently only guest RAM is allocated from the GMM, but this
 * may change to include shadow page tables and other bits later.
 *
 * Guest RAM is managed as individual pages, but allocated from the host OS
 * in chunks for reasons of portability / efficiency. To minimize the memory
 * footprint all tracking structure must be as small as possible without
 * unnecessary performance penalties.
 *
 * The allocation chunks has fixed sized, the size defined at compile time
 * by the #GMM_CHUNK_SIZE \#define.
 *
 * Each chunk is given an unique ID. Each page also has a unique ID. The
 * relationship between the two IDs is:
 * @code
 *  GMM_CHUNK_SHIFT = log2(GMM_CHUNK_SIZE / GUEST_PAGE_SIZE);
 *  idPage = (idChunk << GMM_CHUNK_SHIFT) | iPage;
 * @endcode
 * Where iPage is the index of the page within the chunk. This ID scheme
 * permits for efficient chunk and page lookup, but it relies on the chunk size
 * to be set at compile time. The chunks are organized in an AVL tree with their
 * IDs being the keys.
 *
 * @todo Scope the chunk+page IDs based on config setting: per VM,
 *       per user (default), or global.  This will prevent ring-3 code screwing
 *       around with random page IDs from accessing someone else's data in the
 *       default config.  This would let us move HCPhys out of PGMPAGE when
 *       restricting it to ring-0 only, w/o requiring any additional ring-0 per
 *       page data (prereq mmio2 must go via GMM).  See @bugref{10696} for more.
 *
 * The physical address of each page in an allocation chunk is maintained by
 * the #RTR0MEMOBJ and obtained using #RTR0MemObjGetPagePhysAddr. There is no
 * need to duplicate this information (it'll cost 8-bytes per page if we did).
 *
 * So what do we need to track per page? Most importantly we need to know
 * which state the page is in:
 *   - Private - Allocated for (eventually) backing one particular VM page.
 *   - Shared  - Readonly page that is used by one or more VMs and treated
 *               as COW by PGM.
 *   - Free    - Not used by anyone.
 *
 * For the page replacement operations (sharing, defragmenting and freeing)
 * to be somewhat efficient, private pages needs to be associated with a
 * particular page in a particular VM.
 *
 * Tracking the usage of shared pages is impractical and expensive, so we'll
 * settle for a reference counting system instead.
 *
 * Free pages will be chained on LIFOs
 *
 * On 64-bit systems we will use a 64-bit bitfield per page, while on 32-bit
 * systems a 32-bit bitfield will have to suffice because of address space
 * limitations. The #GMMPAGE structure shows the details.
 *
 *
 * @section sec_gmm_alloc_strat Page Allocation Strategy
 *
 * The strategy for allocating pages has to take fragmentation and shared
 * pages into account, or we may end up with with 2000 chunks with only
 * a few pages in each. Shared pages cannot easily be reallocated because
 * of the inaccurate usage accounting (see above). Private pages can be
 * reallocated by a defragmentation thread in the same manner that sharing
 * is done.
 *
 * The first approach is to manage the free pages in two sets depending on
 * whether they are mainly for the allocation of shared or private pages.
 * In the initial implementation there will be almost no possibility for
 * mixing shared and private pages in the same chunk (only if we're really
 * stressed on memory), but when we implement forking of VMs and have to
 * deal with lots of COW pages it'll start getting kind of interesting.
 *
 * The sets are lists of chunks with approximately the same number of
 * free pages. Say the chunk size is 1MB, meaning 256 pages, and a set
 * consists of 16 lists. So, the first list will contain the chunks with
 * 1-7 free pages, the second covers 8-15, and so on. The chunks will be
 * moved between the lists as pages are freed up or allocated.
 *
 *
 * @section sec_gmm_costs       Costs
 *
 * The per page cost in kernel space is 32-bit plus whatever RTR0MEMOBJ
 * entails. In addition there is the chunk cost of approximately
 * (sizeof(RT0MEMOBJ) + sizeof(CHUNK)) / 2^CHUNK_SHIFT bytes per page.
 *
 * On Windows the per page #RTR0MEMOBJ cost is 32-bit on 32-bit windows
 * and 64-bit on 64-bit windows (a PFN_NUMBER in the MDL). So, 64-bit per page.
 * The cost on Linux is identical, but here it's because of sizeof(struct page *).
 *
 *
 * @section sec_gmm_legacy      Legacy Mode for Non-Tier-1 Platforms
 *
 * In legacy mode the page source is locked user pages and not
 * #RTR0MemObjAllocPhysNC, this means that a page can only be allocated
 * by the VM that locked it. We will make no attempt at implementing
 * page sharing on these systems, just do enough to make it all work.
 *
 * @note With 6.1 really dropping 32-bit support, the legacy mode is obsoleted
 *       under the assumption that there is sufficient kernel virtual address
 *       space to map all of the guest memory allocations.  So, we'll be using
 *       #RTR0MemObjAllocPage on some platforms as an alternative to
 *       #RTR0MemObjAllocPhysNC.
 *
 *
 * @subsection sub_gmm_locking  Serializing
 *
 * One simple fast mutex will be employed in the initial implementation, not
 * two as mentioned in @ref sec_pgmPhys_Serializing.
 *
 * @see @ref sec_pgmPhys_Serializing
 *
 *
 * @section sec_gmm_overcommit  Memory Over-Commitment Management
 *
 * The GVM will have to do the system wide memory over-commitment
 * management. My current ideas are:
 *      - Per VM oc policy that indicates how much to initially commit
 *        to it and what to do in a out-of-memory situation.
 *      - Prevent overtaxing the host.
 *
 * There are some challenges here, the main ones are configurability and
 * security. Should we for instance permit anyone to request 100% memory
 * commitment? Who should be allowed to do runtime adjustments of the
 * config. And how to prevent these settings from being lost when the last
 * VM process exits? The solution is probably to have an optional root
 * daemon the will keep VMMR0.r0 in memory and enable the security measures.
 *
 *
 *
 * @section sec_gmm_numa        NUMA
 *
 * NUMA considerations will be designed and implemented a bit later.
 *
 * The preliminary guesses is that we will have to try allocate memory as
 * close as possible to the CPUs the VM is executed on (EMT and additional CPU
 * threads). Which means it's mostly about allocation and sharing policies.
 * Both the scheduler and allocator interface will to supply some NUMA info
 * and we'll need to have a way to calc access costs.
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_GMM
#include <VBox/rawpci.h>
#include <VBox/vmm/gmm.h>
#include "GMMR0Internal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/vmm/pgm.h>
#include <VBox/log.h>
#include <VBox/param.h>
#include <VBox/err.h>
#include <VBox/VMMDev.h>
#include <iprt/asm.h>
#include <iprt/avl.h>
#ifdef VBOX_STRICT
# include <iprt/crc.h>
#endif
#include <iprt/critsect.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/memobj.h>
#include <iprt/mp.h>
#include <iprt/semaphore.h>
#include <iprt/spinlock.h>
#include <iprt/string.h>
#include <iprt/time.h>

/* This is 64-bit only code now. */
#if HC_ARCH_BITS != 64 || ARCH_BITS != 64
# error "This is 64-bit only code"
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def VBOX_USE_CRIT_SECT_FOR_GIANT
 * Use a critical section instead of a fast mutex for the giant GMM lock.
 *
 * @remarks This is primarily a way of avoiding the deadlock checks in the
 *          windows driver verifier. */
#if defined(RT_OS_WINDOWS) || defined(RT_OS_DARWIN) || defined(DOXYGEN_RUNNING)
# define VBOX_USE_CRIT_SECT_FOR_GIANT
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to set of free chunks.  */
typedef struct GMMCHUNKFREESET *PGMMCHUNKFREESET;

/**
 * The per-page tracking structure employed by the GMM.
 *
 * Because of the different layout on 32-bit and 64-bit hosts in earlier
 * versions of the code, macros are used to get and set some of the data.
 */
typedef union GMMPAGE
{
    /** Unsigned integer view. */
    uint64_t u;

    /** The common view. */
    struct GMMPAGECOMMON
    {
        uint32_t    uStuff1 : 32;
        uint32_t    uStuff2 : 30;
        /** The page state. */
        uint32_t    u2State : 2;
    } Common;

    /** The view of a private page. */
    struct GMMPAGEPRIVATE
    {
        /** The guest page frame number. (Max addressable: 2 ^ 44 - 16) */
        uint32_t    pfn;
        /** The GVM handle. (64K VMs) */
        uint32_t    hGVM : 16;
        /** Reserved. */
        uint32_t    u16Reserved : 14;
        /** The page state. */
        uint32_t    u2State : 2;
    } Private;

    /** The view of a shared page. */
    struct GMMPAGESHARED
    {
        /** The host page frame number. (Max addressable: 2 ^ 44 - 16) */
        uint32_t    pfn;
        /** The reference count (64K VMs). */
        uint32_t    cRefs : 16;
        /** Used for debug checksumming. */
        uint32_t    u14Checksum : 14;
        /** The page state. */
        uint32_t    u2State : 2;
    } Shared;

    /** The view of a free page. */
    struct GMMPAGEFREE
    {
        /** The index of the next page in the free list. UINT16_MAX is NIL. */
        uint16_t    iNext;
        /** Reserved. Checksum or something? */
        uint16_t    u16Reserved0;
        /** Reserved. Checksum or something? */
        uint32_t    u30Reserved1 : 29;
        /** Set if the page was zeroed. */
        uint32_t    fZeroed : 1;
        /** The page state. */
        uint32_t    u2State : 2;
    } Free;
} GMMPAGE;
AssertCompileSize(GMMPAGE, sizeof(RTHCUINTPTR));
/** Pointer to a GMMPAGE. */
typedef GMMPAGE *PGMMPAGE;


/** @name The Page States.
 * @{ */
/** A private page. */
#define GMM_PAGE_STATE_PRIVATE          0
/** A shared page. */
#define GMM_PAGE_STATE_SHARED           2
/** A free page. */
#define GMM_PAGE_STATE_FREE             3
/** @} */


/** @def GMM_PAGE_IS_PRIVATE
 *
 * @returns true if private, false if not.
 * @param   pPage       The GMM page.
 */
#define GMM_PAGE_IS_PRIVATE(pPage)  ( (pPage)->Common.u2State == GMM_PAGE_STATE_PRIVATE )

/** @def GMM_PAGE_IS_SHARED
 *
 * @returns true if shared, false if not.
 * @param   pPage       The GMM page.
 */
#define GMM_PAGE_IS_SHARED(pPage)   ( (pPage)->Common.u2State == GMM_PAGE_STATE_SHARED )

/** @def GMM_PAGE_IS_FREE
 *
 * @returns true if free, false if not.
 * @param   pPage       The GMM page.
 */
#define GMM_PAGE_IS_FREE(pPage)     ( (pPage)->Common.u2State == GMM_PAGE_STATE_FREE )

/** @def GMM_PAGE_PFN_LAST
 * The last valid guest pfn range.
 * @remark Some of the values outside the range has special meaning,
 *         see GMM_PAGE_PFN_UNSHAREABLE.
 */
#define GMM_PAGE_PFN_LAST            UINT32_C(0xfffffff0)
AssertCompile(GMM_PAGE_PFN_LAST == (GMM_GCPHYS_LAST >> GUEST_PAGE_SHIFT));

/** @def GMM_PAGE_PFN_UNSHAREABLE
 * Indicates that this page isn't used for normal guest memory and thus isn't shareable.
 */
#define GMM_PAGE_PFN_UNSHAREABLE    UINT32_C(0xfffffff1)
AssertCompile(GMM_PAGE_PFN_UNSHAREABLE == (GMM_GCPHYS_UNSHAREABLE >> GUEST_PAGE_SHIFT));


/**
 * A GMM allocation chunk ring-3 mapping record.
 *
 * This should really be associated with a session and not a VM, but
 * it's simpler to associated with a VM and cleanup with the VM object
 * is destroyed.
 */
typedef struct GMMCHUNKMAP
{
    /** The mapping object. */
    RTR0MEMOBJ          hMapObj;
    /** The VM owning the mapping. */
    PGVM                pGVM;
} GMMCHUNKMAP;
/** Pointer to a GMM allocation chunk mapping. */
typedef struct GMMCHUNKMAP *PGMMCHUNKMAP;


/**
 * A GMM allocation chunk.
 */
typedef struct GMMCHUNK
{
    /** The AVL node core.
     * The Key is the chunk ID.  (Giant mtx.) */
    AVLU32NODECORE      Core;
    /** The memory object.
     * Either from RTR0MemObjAllocPhysNC or RTR0MemObjLockUser depending on
     * what the host can dish up with.  (Chunk mtx protects mapping accesses
     * and related frees.) */
    RTR0MEMOBJ          hMemObj;
#ifndef VBOX_WITH_LINEAR_HOST_PHYS_MEM
    /** Pointer to the kernel mapping. */
    uint8_t            *pbMapping;
#endif
    /** Pointer to the next chunk in the free list.  (Giant mtx.) */
    PGMMCHUNK           pFreeNext;
    /** Pointer to the previous chunk in the free list. (Giant mtx.) */
    PGMMCHUNK           pFreePrev;
    /** Pointer to the free set this chunk belongs to.  NULL for
     * chunks with no free pages. (Giant mtx.) */
    PGMMCHUNKFREESET    pSet;
    /** List node in the chunk list (GMM::ChunkList).  (Giant mtx.) */
    RTLISTNODE          ListNode;
    /** Pointer to an array of mappings.  (Chunk mtx.) */
    PGMMCHUNKMAP        paMappingsX;
    /** The number of mappings.  (Chunk mtx.) */
    uint16_t            cMappingsX;
    /** The mapping lock this chunk is using using.  UINT8_MAX if nobody is mapping
     * or freeing anything.  (Giant mtx.) */
    uint8_t volatile    iChunkMtx;
    /** GMM_CHUNK_FLAGS_XXX. (Giant mtx.) */
    uint8_t             fFlags;
    /** The head of the list of free pages. UINT16_MAX is the NIL value.
     *  (Giant mtx.) */
    uint16_t            iFreeHead;
    /** The number of free pages.  (Giant mtx.) */
    uint16_t            cFree;
    /** The GVM handle of the VM that first allocated pages from this chunk, this
     * is used as a preference when there are several chunks to choose from.
     * When in bound memory mode this isn't a preference any longer.  (Giant
     * mtx.) */
    uint16_t            hGVM;
    /** The ID of the NUMA node the memory mostly resides on.  (Reserved for
     *  future use.)  (Giant mtx.) */
    uint16_t            idNumaNode;
    /** The number of private pages.  (Giant mtx.) */
    uint16_t            cPrivate;
    /** The number of shared pages.  (Giant mtx.) */
    uint16_t            cShared;
    /** The UID this chunk is associated with. */
    RTUID               uidOwner;
    uint32_t            u32Padding;
    /** The pages.  (Giant mtx.) */
    GMMPAGE             aPages[GMM_CHUNK_NUM_PAGES];
} GMMCHUNK;

/** Indicates that the NUMA properies of the memory is unknown. */
#define GMM_CHUNK_NUMA_ID_UNKNOWN   UINT16_C(0xfffe)

/** @name GMM_CHUNK_FLAGS_XXX - chunk flags.
 * @{ */
/** Indicates that the chunk is a large page (2MB). */
#define GMM_CHUNK_FLAGS_LARGE_PAGE  UINT16_C(0x0001)
/** @}  */


/**
 * An allocation chunk TLB entry.
 */
typedef struct GMMCHUNKTLBE
{
    /** The chunk id. */
    uint32_t            idChunk;
    /** Pointer to the chunk. */
    PGMMCHUNK           pChunk;
} GMMCHUNKTLBE;
/** Pointer to an allocation chunk TLB entry. */
typedef GMMCHUNKTLBE *PGMMCHUNKTLBE;


/** The number of entries in the allocation chunk TLB. */
#define GMM_CHUNKTLB_ENTRIES        32
/** Gets the TLB entry index for the given Chunk ID. */
#define GMM_CHUNKTLB_IDX(idChunk)   ( (idChunk) & (GMM_CHUNKTLB_ENTRIES - 1) )

/**
 * An allocation chunk TLB.
 */
typedef struct GMMCHUNKTLB
{
    /** The TLB entries. */
    GMMCHUNKTLBE    aEntries[GMM_CHUNKTLB_ENTRIES];
} GMMCHUNKTLB;
/** Pointer to an allocation chunk TLB. */
typedef GMMCHUNKTLB *PGMMCHUNKTLB;


/**
 * The GMM instance data.
 */
typedef struct GMM
{
    /** Magic / eye catcher. GMM_MAGIC */
    uint32_t            u32Magic;
    /** The number of threads waiting on the mutex. */
    uint32_t            cMtxContenders;
#ifdef VBOX_USE_CRIT_SECT_FOR_GIANT
    /** The critical section protecting the GMM.
     * More fine grained locking can be implemented later if necessary. */
    RTCRITSECT          GiantCritSect;
#else
    /** The fast mutex protecting the GMM.
     * More fine grained locking can be implemented later if necessary. */
    RTSEMFASTMUTEX      hMtx;
#endif
#ifdef VBOX_STRICT
    /** The current mutex owner. */
    RTNATIVETHREAD      hMtxOwner;
#endif
    /** Spinlock protecting the AVL tree.
     * @todo Make this a read-write spinlock as we should allow concurrent
     *       lookups. */
    RTSPINLOCK          hSpinLockTree;
    /** The chunk tree.
     * Protected by hSpinLockTree. */
    PAVLU32NODECORE     pChunks;
    /** Chunk freeing generation - incremented whenever a chunk is freed.  Used
     * for validating the per-VM chunk TLB entries.  Valid range is 1 to 2^62
     * (exclusive), though higher numbers may temporarily occure while
     * invalidating the individual TLBs during wrap-around processing. */
    uint64_t volatile   idFreeGeneration;
    /** The chunk TLB.
     * Protected by hSpinLockTree. */
    GMMCHUNKTLB         ChunkTLB;
    /** The private free set. */
    GMMCHUNKFREESET     PrivateX;
    /** The shared free set. */
    GMMCHUNKFREESET     Shared;

    /** Shared module tree (global).
     * @todo separate trees for distinctly different guest OSes. */
    PAVLLU32NODECORE    pGlobalSharedModuleTree;
    /** Sharable modules (count of nodes in pGlobalSharedModuleTree). */
    uint32_t            cShareableModules;

    /** The chunk list.  For simplifying the cleanup process and avoid tree
     * traversal. */
    RTLISTANCHOR        ChunkList;

    /** The maximum number of pages we're allowed to allocate.
     * @gcfgm{GMM/MaxPages,64-bit, Direct.}
     * @gcfgm{GMM/PctPages,32-bit, Relative to the number of host pages.} */
    uint64_t            cMaxPages;
    /** The number of pages that has been reserved.
     * The deal is that cReservedPages - cOverCommittedPages <= cMaxPages. */
    uint64_t            cReservedPages;
    /** The number of pages that we have over-committed in reservations. */
    uint64_t            cOverCommittedPages;
    /** The number of actually allocated (committed if you like) pages. */
    uint64_t            cAllocatedPages;
    /** The number of pages that are shared. A subset of cAllocatedPages. */
    uint64_t            cSharedPages;
    /** The number of pages that are actually shared between VMs. */
    uint64_t            cDuplicatePages;
    /** The number of pages that are shared that has been left behind by
     * VMs not doing proper cleanups. */
    uint64_t            cLeftBehindSharedPages;
    /** The number of allocation chunks.
     * (The number of pages we've allocated from the host can be derived from this.) */
    uint32_t            cChunks;
    /** The number of current ballooned pages. */
    uint64_t            cBalloonedPages;

#ifdef VBOX_WITH_LINEAR_HOST_PHYS_MEM
    /** Whether #RTR0MemObjAllocPhysNC works.   */
    bool                fHasWorkingAllocPhysNC;
#else
    bool                fPadding;
#endif
    /** The bound memory mode indicator.
     * When set, the memory will be bound to a specific VM and never
     * shared. This is always set if fLegacyAllocationMode is set.
     * (Also determined at initialization time.) */
    bool                fBoundMemoryMode;
    /** The number of registered VMs. */
    uint16_t            cRegisteredVMs;

    /** The index of the next mutex to use. */
    uint32_t            iNextChunkMtx;
    /** Chunk locks for reducing lock contention without having to allocate
     * one lock per chunk. */
    struct
    {
        /** The mutex */
        RTSEMFASTMUTEX      hMtx;
        /** The number of threads currently using this mutex. */
        uint32_t volatile   cUsers;
    } aChunkMtx[64];

    /** The number of freed chunks ever.  This is used as list generation to
     * avoid restarting the cleanup scanning when the list wasn't modified. */
    uint32_t volatile   cFreedChunks;
    /** The previous allocated Chunk ID.
     * Used as a hint to avoid scanning the whole bitmap. */
    uint32_t            idChunkPrev;
    /** Spinlock protecting idChunkPrev & bmChunkId.  */
    RTSPINLOCK          hSpinLockChunkId;
    /** Chunk ID allocation bitmap.
     * Bits of allocated IDs are set, free ones are clear.
     * The NIL id (0) is marked allocated. */
    uint32_t            bmChunkId[(GMM_CHUNKID_LAST + 1 + 31) / 32];
} GMM;
/** Pointer to the GMM instance. */
typedef GMM *PGMM;

/** The value of GMM::u32Magic (Katsuhiro Otomo). */
#define GMM_MAGIC       UINT32_C(0x19540414)


/**
 * GMM chunk mutex state.
 *
 * This is returned by gmmR0ChunkMutexAcquire and is used by the other
 * gmmR0ChunkMutex* methods.
 */
typedef struct GMMR0CHUNKMTXSTATE
{
    PGMM                pGMM;
    /** The index of the chunk mutex. */
    uint8_t             iChunkMtx;
    /** The relevant flags (GMMR0CHUNK_MTX_XXX). */
    uint8_t             fFlags;
} GMMR0CHUNKMTXSTATE;
/** Pointer to a chunk mutex state. */
typedef GMMR0CHUNKMTXSTATE *PGMMR0CHUNKMTXSTATE;

/** @name GMMR0CHUNK_MTX_XXX
 * @{ */
#define GMMR0CHUNK_MTX_INVALID          UINT32_C(0)
#define GMMR0CHUNK_MTX_KEEP_GIANT       UINT32_C(1)
#define GMMR0CHUNK_MTX_RETAKE_GIANT     UINT32_C(2)
#define GMMR0CHUNK_MTX_DROP_GIANT       UINT32_C(3)
#define GMMR0CHUNK_MTX_END              UINT32_C(4)
/** @} */


/** The maximum number of shared modules per-vm. */
#define GMM_MAX_SHARED_PER_VM_MODULES   2048
/** The maximum number of shared modules GMM is allowed to track. */
#define GMM_MAX_SHARED_GLOBAL_MODULES   16834


/**
 * Argument packet for gmmR0SharedModuleCleanup.
 */
typedef struct GMMR0SHMODPERVMDTORARGS
{
    PGVM    pGVM;
    PGMM    pGMM;
} GMMR0SHMODPERVMDTORARGS;

/**
 * Argument packet for gmmR0CheckSharedModule.
 */
typedef struct GMMCHECKSHAREDMODULEINFO
{
    PGVM                    pGVM;
    VMCPUID                 idCpu;
} GMMCHECKSHAREDMODULEINFO;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Pointer to the GMM instance data. */
static PGMM g_pGMM = NULL;

/** Macro for obtaining and validating the g_pGMM pointer.
 *
 * On failure it will return from the invoking function with the specified
 * return value.
 *
 * @param   pGMM    The name of the pGMM variable.
 * @param   rc      The return value on failure. Use VERR_GMM_INSTANCE for VBox
 *                  status codes.
 */
#define GMM_GET_VALID_INSTANCE(pGMM, rc) \
    do { \
        (pGMM) = g_pGMM; \
        AssertPtrReturn((pGMM), (rc)); \
        AssertMsgReturn((pGMM)->u32Magic == GMM_MAGIC, ("%p - %#x\n", (pGMM), (pGMM)->u32Magic), (rc)); \
    } while (0)

/** Macro for obtaining and validating the g_pGMM pointer, void function
 * variant.
 *
 * On failure it will return from the invoking function.
 *
 * @param   pGMM    The name of the pGMM variable.
 */
#define GMM_GET_VALID_INSTANCE_VOID(pGMM) \
    do { \
        (pGMM) = g_pGMM; \
        AssertPtrReturnVoid((pGMM)); \
        AssertMsgReturnVoid((pGMM)->u32Magic == GMM_MAGIC, ("%p - %#x\n", (pGMM), (pGMM)->u32Magic)); \
    } while (0)


/** @def GMM_CHECK_SANITY_UPON_ENTERING
 * Checks the sanity of the GMM instance data before making changes.
 *
 * This is macro is a stub by default and must be enabled manually in the code.
 *
 * @returns true if sane, false if not.
 * @param   pGMM    The name of the pGMM variable.
 */
#if defined(VBOX_STRICT) && defined(GMMR0_WITH_SANITY_CHECK) && 0
# define GMM_CHECK_SANITY_UPON_ENTERING(pGMM)   (RT_LIKELY(gmmR0SanityCheck((pGMM), __PRETTY_FUNCTION__, __LINE__) == 0))
#else
# define GMM_CHECK_SANITY_UPON_ENTERING(pGMM)   (true)
#endif

/** @def GMM_CHECK_SANITY_UPON_LEAVING
 * Checks the sanity of the GMM instance data after making changes.
 *
 * This is macro is a stub by default and must be enabled manually in the code.
 *
 * @returns true if sane, false if not.
 * @param   pGMM    The name of the pGMM variable.
 */
#if defined(VBOX_STRICT) && defined(GMMR0_WITH_SANITY_CHECK) && 0
# define GMM_CHECK_SANITY_UPON_LEAVING(pGMM)    (gmmR0SanityCheck((pGMM), __PRETTY_FUNCTION__, __LINE__) == 0)
#else
# define GMM_CHECK_SANITY_UPON_LEAVING(pGMM)    (true)
#endif

/** @def GMM_CHECK_SANITY_IN_LOOPS
 * Checks the sanity of the GMM instance in the allocation loops.
 *
 * This is macro is a stub by default and must be enabled manually in the code.
 *
 * @returns true if sane, false if not.
 * @param   pGMM    The name of the pGMM variable.
 */
#if defined(VBOX_STRICT) && defined(GMMR0_WITH_SANITY_CHECK) && 0
# define GMM_CHECK_SANITY_IN_LOOPS(pGMM)        (gmmR0SanityCheck((pGMM), __PRETTY_FUNCTION__, __LINE__) == 0)
#else
# define GMM_CHECK_SANITY_IN_LOOPS(pGMM)        (true)
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int)    gmmR0TermDestroyChunk(PAVLU32NODECORE pNode, void *pvGMM);
static bool                 gmmR0CleanupVMScanChunk(PGMM pGMM, PGVM pGVM, PGMMCHUNK pChunk);
DECLINLINE(void)            gmmR0UnlinkChunk(PGMMCHUNK pChunk);
DECLINLINE(void)            gmmR0LinkChunk(PGMMCHUNK pChunk, PGMMCHUNKFREESET pSet);
DECLINLINE(void)            gmmR0SelectSetAndLinkChunk(PGMM pGMM, PGVM pGVM, PGMMCHUNK pChunk);
#ifdef GMMR0_WITH_SANITY_CHECK
static uint32_t             gmmR0SanityCheck(PGMM pGMM, const char *pszFunction, unsigned uLineNo);
#endif
static bool                 gmmR0FreeChunk(PGMM pGMM, PGVM pGVM, PGMMCHUNK pChunk, bool fRelaxedSem);
DECLINLINE(void)            gmmR0FreePrivatePage(PGMM pGMM, PGVM pGVM, uint32_t idPage, PGMMPAGE pPage);
DECLINLINE(void)            gmmR0FreeSharedPage(PGMM pGMM, PGVM pGVM, uint32_t idPage, PGMMPAGE pPage);
static int                  gmmR0UnmapChunkLocked(PGMM pGMM, PGVM pGVM, PGMMCHUNK pChunk);
#ifdef VBOX_WITH_PAGE_SHARING
static void                 gmmR0SharedModuleCleanup(PGMM pGMM, PGVM pGVM);
# ifdef VBOX_STRICT
static uint32_t             gmmR0StrictPageChecksum(PGMM pGMM, PGVM pGVM, uint32_t idPage);
# endif
#endif



/**
 * Initializes the GMM component.
 *
 * This is called when the VMMR0.r0 module is loaded and protected by the
 * loader semaphore.
 *
 * @returns VBox status code.
 */
GMMR0DECL(int) GMMR0Init(void)
{
    LogFlow(("GMMInit:\n"));

    /* Currently assuming same host and guest page size here.  Can change it to
       dish out guest pages with different size from the host page later if
       needed, though a restriction would be the host page size must be larger
       than the guest page size. */
    AssertCompile(GUEST_PAGE_SIZE == HOST_PAGE_SIZE);
    AssertCompile(GUEST_PAGE_SIZE <= HOST_PAGE_SIZE);

    /*
     * Allocate the instance data and the locks.
     */
    PGMM pGMM = (PGMM)RTMemAllocZ(sizeof(*pGMM));
    if (!pGMM)
        return VERR_NO_MEMORY;

    pGMM->u32Magic = GMM_MAGIC;
    for (unsigned i = 0; i < RT_ELEMENTS(pGMM->ChunkTLB.aEntries); i++)
        pGMM->ChunkTLB.aEntries[i].idChunk = NIL_GMM_CHUNKID;
    RTListInit(&pGMM->ChunkList);
    ASMBitSet(&pGMM->bmChunkId[0], NIL_GMM_CHUNKID);

#ifdef VBOX_USE_CRIT_SECT_FOR_GIANT
    int rc = RTCritSectInit(&pGMM->GiantCritSect);
#else
    int rc = RTSemFastMutexCreate(&pGMM->hMtx);
#endif
    if (RT_SUCCESS(rc))
    {
        unsigned iMtx;
        for (iMtx = 0; iMtx < RT_ELEMENTS(pGMM->aChunkMtx); iMtx++)
        {
            rc = RTSemFastMutexCreate(&pGMM->aChunkMtx[iMtx].hMtx);
            if (RT_FAILURE(rc))
                break;
        }
        pGMM->hSpinLockTree = NIL_RTSPINLOCK;
        if (RT_SUCCESS(rc))
            rc = RTSpinlockCreate(&pGMM->hSpinLockTree, RTSPINLOCK_FLAGS_INTERRUPT_SAFE, "gmm-chunk-tree");
        pGMM->hSpinLockChunkId = NIL_RTSPINLOCK;
        if (RT_SUCCESS(rc))
            rc = RTSpinlockCreate(&pGMM->hSpinLockChunkId, RTSPINLOCK_FLAGS_INTERRUPT_SAFE, "gmm-chunk-id");
        if (RT_SUCCESS(rc))
        {
            /*
             * Figure out how we're going to allocate stuff (only applicable to
             * host with linear physical memory mappings).
             */
            pGMM->fBoundMemoryMode = false;
#ifdef VBOX_WITH_LINEAR_HOST_PHYS_MEM
            pGMM->fHasWorkingAllocPhysNC = false;

            RTR0MEMOBJ hMemObj;
            rc = RTR0MemObjAllocPhysNC(&hMemObj, GMM_CHUNK_SIZE, NIL_RTHCPHYS);
            if (RT_SUCCESS(rc))
            {
                rc = RTR0MemObjFree(hMemObj, true);
                AssertRC(rc);
                pGMM->fHasWorkingAllocPhysNC = true;
            }
            else if (rc != VERR_NOT_SUPPORTED)
                SUPR0Printf("GMMR0Init: Warning! RTR0MemObjAllocPhysNC(, %u, NIL_RTHCPHYS) -> %d!\n", GMM_CHUNK_SIZE, rc);
# endif

            /*
             * Query system page count and guess a reasonable cMaxPages value.
             */
            pGMM->cMaxPages = UINT32_MAX; /** @todo IPRT function for query ram size and such. */

            /*
             * The idFreeGeneration value should be set so we actually trigger the
             * wrap-around invalidation handling during a typical test run.
             */
            pGMM->idFreeGeneration = UINT64_MAX / 4 - 128;

            g_pGMM = pGMM;
#ifdef VBOX_WITH_LINEAR_HOST_PHYS_MEM
            LogFlow(("GMMInit: pGMM=%p fBoundMemoryMode=%RTbool fHasWorkingAllocPhysNC=%RTbool\n", pGMM, pGMM->fBoundMemoryMode, pGMM->fHasWorkingAllocPhysNC));
#else
            LogFlow(("GMMInit: pGMM=%p fBoundMemoryMode=%RTbool\n", pGMM, pGMM->fBoundMemoryMode));
#endif
            return VINF_SUCCESS;
        }

        /*
         * Bail out.
         */
        RTSpinlockDestroy(pGMM->hSpinLockChunkId);
        RTSpinlockDestroy(pGMM->hSpinLockTree);
        while (iMtx-- > 0)
            RTSemFastMutexDestroy(pGMM->aChunkMtx[iMtx].hMtx);
#ifdef VBOX_USE_CRIT_SECT_FOR_GIANT
        RTCritSectDelete(&pGMM->GiantCritSect);
#else
        RTSemFastMutexDestroy(pGMM->hMtx);
#endif
    }

    pGMM->u32Magic = 0;
    RTMemFree(pGMM);
    SUPR0Printf("GMMR0Init: failed! rc=%d\n", rc);
    return rc;
}


/**
 * Terminates the GMM component.
 */
GMMR0DECL(void) GMMR0Term(void)
{
    LogFlow(("GMMTerm:\n"));

    /*
     * Take care / be paranoid...
     */
    PGMM pGMM = g_pGMM;
    if (!RT_VALID_PTR(pGMM))
        return;
    if (pGMM->u32Magic != GMM_MAGIC)
    {
        SUPR0Printf("GMMR0Term: u32Magic=%#x\n", pGMM->u32Magic);
        return;
    }

    /*
     * Undo what init did and free all the resources we've acquired.
     */
    /* Destroy the fundamentals. */
    g_pGMM = NULL;
    pGMM->u32Magic    = ~GMM_MAGIC;
#ifdef VBOX_USE_CRIT_SECT_FOR_GIANT
    RTCritSectDelete(&pGMM->GiantCritSect);
#else
    RTSemFastMutexDestroy(pGMM->hMtx);
    pGMM->hMtx        = NIL_RTSEMFASTMUTEX;
#endif
    RTSpinlockDestroy(pGMM->hSpinLockTree);
    pGMM->hSpinLockTree = NIL_RTSPINLOCK;
    RTSpinlockDestroy(pGMM->hSpinLockChunkId);
    pGMM->hSpinLockChunkId = NIL_RTSPINLOCK;

    /* Free any chunks still hanging around. */
    RTAvlU32Destroy(&pGMM->pChunks, gmmR0TermDestroyChunk, pGMM);

    /* Destroy the chunk locks. */
    for (unsigned iMtx = 0; iMtx < RT_ELEMENTS(pGMM->aChunkMtx); iMtx++)
    {
        Assert(pGMM->aChunkMtx[iMtx].cUsers == 0);
        RTSemFastMutexDestroy(pGMM->aChunkMtx[iMtx].hMtx);
        pGMM->aChunkMtx[iMtx].hMtx = NIL_RTSEMFASTMUTEX;
    }

    /* Finally the instance data itself. */
    RTMemFree(pGMM);
    LogFlow(("GMMTerm: done\n"));
}


/**
 * RTAvlU32Destroy callback.
 *
 * @returns 0
 * @param   pNode   The node to destroy.
 * @param   pvGMM   The GMM handle.
 */
static DECLCALLBACK(int) gmmR0TermDestroyChunk(PAVLU32NODECORE pNode, void *pvGMM)
{
    PGMMCHUNK pChunk = (PGMMCHUNK)pNode;

    if (pChunk->cFree != GMM_CHUNK_NUM_PAGES)
        SUPR0Printf("GMMR0Term: %RKv/%#x: cFree=%d cPrivate=%d cShared=%d cMappings=%d\n", pChunk,
                    pChunk->Core.Key, pChunk->cFree, pChunk->cPrivate, pChunk->cShared, pChunk->cMappingsX);

    int rc = RTR0MemObjFree(pChunk->hMemObj, true /* fFreeMappings */);
    if (RT_FAILURE(rc))
    {
        SUPR0Printf("GMMR0Term: %RKv/%#x: RTRMemObjFree(%RKv,true) -> %d (cMappings=%d)\n", pChunk,
                    pChunk->Core.Key, pChunk->hMemObj, rc, pChunk->cMappingsX);
        AssertRC(rc);
    }
    pChunk->hMemObj = NIL_RTR0MEMOBJ;

    RTMemFree(pChunk->paMappingsX);
    pChunk->paMappingsX = NULL;

    RTMemFree(pChunk);
    NOREF(pvGMM);
    return 0;
}


/**
 * Initializes the per-VM data for the GMM.
 *
 * This is called from within the GVMM lock (from GVMMR0CreateVM)
 * and should only initialize the data members so GMMR0CleanupVM
 * can deal with them. We reserve no memory or anything here,
 * that's done later in GMMR0InitVM.
 *
 * @param   pGVM    Pointer to the Global VM structure.
 */
GMMR0DECL(int) GMMR0InitPerVMData(PGVM pGVM)
{
    AssertCompile(RT_SIZEOFMEMB(GVM,gmm.s) <= RT_SIZEOFMEMB(GVM,gmm.padding));

    pGVM->gmm.s.Stats.enmPolicy = GMMOCPOLICY_INVALID;
    pGVM->gmm.s.Stats.enmPriority = GMMPRIORITY_INVALID;
    pGVM->gmm.s.Stats.fMayAllocate = false;

    pGVM->gmm.s.hChunkTlbSpinLock = NIL_RTSPINLOCK;
    int rc = RTSpinlockCreate(&pGVM->gmm.s.hChunkTlbSpinLock, RTSPINLOCK_FLAGS_INTERRUPT_SAFE, "per-vm-chunk-tlb");
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}


/**
 * Acquires the GMM giant lock.
 *
 * @returns Assert status code from RTSemFastMutexRequest.
 * @param   pGMM        Pointer to the GMM instance.
 */
static int gmmR0MutexAcquire(PGMM pGMM)
{
    ASMAtomicIncU32(&pGMM->cMtxContenders);
#ifdef VBOX_USE_CRIT_SECT_FOR_GIANT
    int rc = RTCritSectEnter(&pGMM->GiantCritSect);
#else
    int rc = RTSemFastMutexRequest(pGMM->hMtx);
#endif
    ASMAtomicDecU32(&pGMM->cMtxContenders);
    AssertRC(rc);
#ifdef VBOX_STRICT
    pGMM->hMtxOwner = RTThreadNativeSelf();
#endif
    return rc;
}


/**
 * Releases the GMM giant lock.
 *
 * @returns Assert status code from RTSemFastMutexRequest.
 * @param   pGMM        Pointer to the GMM instance.
 */
static int gmmR0MutexRelease(PGMM pGMM)
{
#ifdef VBOX_STRICT
    pGMM->hMtxOwner = NIL_RTNATIVETHREAD;
#endif
#ifdef VBOX_USE_CRIT_SECT_FOR_GIANT
    int rc = RTCritSectLeave(&pGMM->GiantCritSect);
#else
    int rc = RTSemFastMutexRelease(pGMM->hMtx);
    AssertRC(rc);
#endif
    return rc;
}


/**
 * Yields the GMM giant lock if there is contention and a certain minimum time
 * has elapsed since we took it.
 *
 * @returns @c true if the mutex was yielded, @c false if not.
 * @param   pGMM            Pointer to the GMM instance.
 * @param   puLockNanoTS    Where the lock acquisition time stamp is kept
 *                          (in/out).
 */
static bool gmmR0MutexYield(PGMM pGMM, uint64_t *puLockNanoTS)
{
    /*
     * If nobody is contending the mutex, don't bother checking the time.
     */
    if (ASMAtomicReadU32(&pGMM->cMtxContenders) == 0)
        return false;

    /*
     * Don't yield if we haven't executed for at least 2 milliseconds.
     */
    uint64_t uNanoNow = RTTimeSystemNanoTS();
    if (uNanoNow - *puLockNanoTS < UINT32_C(2000000))
        return false;

    /*
     * Yield the mutex.
     */
#ifdef VBOX_STRICT
    pGMM->hMtxOwner = NIL_RTNATIVETHREAD;
#endif
    ASMAtomicIncU32(&pGMM->cMtxContenders);
#ifdef VBOX_USE_CRIT_SECT_FOR_GIANT
    int rc1 = RTCritSectLeave(&pGMM->GiantCritSect); AssertRC(rc1);
#else
    int rc1 = RTSemFastMutexRelease(pGMM->hMtx); AssertRC(rc1);
#endif

    RTThreadYield();

#ifdef VBOX_USE_CRIT_SECT_FOR_GIANT
    int rc2 = RTCritSectEnter(&pGMM->GiantCritSect); AssertRC(rc2);
#else
    int rc2 = RTSemFastMutexRequest(pGMM->hMtx); AssertRC(rc2);
#endif
    *puLockNanoTS = RTTimeSystemNanoTS();
    ASMAtomicDecU32(&pGMM->cMtxContenders);
#ifdef VBOX_STRICT
    pGMM->hMtxOwner = RTThreadNativeSelf();
#endif

    return true;
}


/**
 * Acquires a chunk lock.
 *
 * The caller must own the giant lock.
 *
 * @returns Assert status code from RTSemFastMutexRequest.
 * @param   pMtxState   The chunk mutex state info.  (Avoids
 *                      passing the same flags and stuff around
 *                      for subsequent release and drop-giant
 *                      calls.)
 * @param   pGMM        Pointer to the GMM instance.
 * @param   pChunk      Pointer to the chunk.
 * @param   fFlags      Flags regarding the giant lock, GMMR0CHUNK_MTX_XXX.
 */
static int gmmR0ChunkMutexAcquire(PGMMR0CHUNKMTXSTATE pMtxState, PGMM pGMM, PGMMCHUNK pChunk, uint32_t fFlags)
{
    Assert(fFlags > GMMR0CHUNK_MTX_INVALID && fFlags < GMMR0CHUNK_MTX_END);
    Assert(pGMM->hMtxOwner == RTThreadNativeSelf());

    pMtxState->pGMM   = pGMM;
    pMtxState->fFlags = (uint8_t)fFlags;

    /*
     * Get the lock index and reference the lock.
     */
    Assert(pGMM->hMtxOwner == RTThreadNativeSelf());
    uint32_t iChunkMtx = pChunk->iChunkMtx;
    if (iChunkMtx == UINT8_MAX)
    {
        iChunkMtx = pGMM->iNextChunkMtx++;
        iChunkMtx %= RT_ELEMENTS(pGMM->aChunkMtx);

        /* Try get an unused one... */
        if (pGMM->aChunkMtx[iChunkMtx].cUsers)
        {
            iChunkMtx = pGMM->iNextChunkMtx++;
            iChunkMtx %= RT_ELEMENTS(pGMM->aChunkMtx);
            if (pGMM->aChunkMtx[iChunkMtx].cUsers)
            {
                iChunkMtx = pGMM->iNextChunkMtx++;
                iChunkMtx %= RT_ELEMENTS(pGMM->aChunkMtx);
                if (pGMM->aChunkMtx[iChunkMtx].cUsers)
                {
                    iChunkMtx = pGMM->iNextChunkMtx++;
                    iChunkMtx %= RT_ELEMENTS(pGMM->aChunkMtx);
                }
            }
        }

        pChunk->iChunkMtx = iChunkMtx;
    }
    AssertCompile(RT_ELEMENTS(pGMM->aChunkMtx) < UINT8_MAX);
    pMtxState->iChunkMtx = (uint8_t)iChunkMtx;
    ASMAtomicIncU32(&pGMM->aChunkMtx[iChunkMtx].cUsers);

    /*
     * Drop the giant?
     */
    if (fFlags != GMMR0CHUNK_MTX_KEEP_GIANT)
    {
        /** @todo GMM life cycle cleanup (we may race someone
         *        destroying and cleaning up GMM)? */
        gmmR0MutexRelease(pGMM);
    }

    /*
     * Take the chunk mutex.
     */
    int rc = RTSemFastMutexRequest(pGMM->aChunkMtx[iChunkMtx].hMtx);
    AssertRC(rc);
    return rc;
}


/**
 * Releases the GMM giant lock.
 *
 * @returns Assert status code from RTSemFastMutexRequest.
 * @param   pMtxState   Pointer to the chunk mutex state.
 * @param   pChunk      Pointer to the chunk if it's still
 *                      alive, NULL if it isn't.  This is used to deassociate
 *                      the chunk from the mutex on the way out so a new one
 *                      can be selected next time, thus avoiding contented
 *                      mutexes.
 */
static int gmmR0ChunkMutexRelease(PGMMR0CHUNKMTXSTATE pMtxState, PGMMCHUNK pChunk)
{
    PGMM pGMM = pMtxState->pGMM;

    /*
     * Release the chunk mutex and reacquire the giant if requested.
     */
    int rc = RTSemFastMutexRelease(pGMM->aChunkMtx[pMtxState->iChunkMtx].hMtx);
    AssertRC(rc);
    if (pMtxState->fFlags == GMMR0CHUNK_MTX_RETAKE_GIANT)
        rc = gmmR0MutexAcquire(pGMM);
    else
        Assert((pMtxState->fFlags != GMMR0CHUNK_MTX_DROP_GIANT) == (pGMM->hMtxOwner == RTThreadNativeSelf()));

    /*
     * Drop the chunk mutex user reference and deassociate it from the chunk
     * when possible.
     */
    if (   ASMAtomicDecU32(&pGMM->aChunkMtx[pMtxState->iChunkMtx].cUsers) == 0
        && pChunk
        && RT_SUCCESS(rc) )
    {
        if (pMtxState->fFlags != GMMR0CHUNK_MTX_DROP_GIANT)
            pChunk->iChunkMtx = UINT8_MAX;
        else
        {
            rc = gmmR0MutexAcquire(pGMM);
            if (RT_SUCCESS(rc))
            {
                if (pGMM->aChunkMtx[pMtxState->iChunkMtx].cUsers == 0)
                    pChunk->iChunkMtx = UINT8_MAX;
                rc = gmmR0MutexRelease(pGMM);
            }
        }
    }

    pMtxState->pGMM = NULL;
    return rc;
}


/**
 * Drops the giant GMM lock we kept in gmmR0ChunkMutexAcquire while keeping the
 * chunk locked.
 *
 * This only works if gmmR0ChunkMutexAcquire was called with
 * GMMR0CHUNK_MTX_KEEP_GIANT.  gmmR0ChunkMutexRelease will retake the giant
 * mutex, i.e. behave as if GMMR0CHUNK_MTX_RETAKE_GIANT was used.
 *
 * @returns VBox status code (assuming success is ok).
 * @param   pMtxState   Pointer to the chunk mutex state.
 */
static int gmmR0ChunkMutexDropGiant(PGMMR0CHUNKMTXSTATE pMtxState)
{
    AssertReturn(pMtxState->fFlags == GMMR0CHUNK_MTX_KEEP_GIANT, VERR_GMM_MTX_FLAGS);
    Assert(pMtxState->pGMM->hMtxOwner == RTThreadNativeSelf());
    pMtxState->fFlags = GMMR0CHUNK_MTX_RETAKE_GIANT;
    /** @todo GMM life cycle cleanup (we may race someone
     *        destroying and cleaning up GMM)? */
    return gmmR0MutexRelease(pMtxState->pGMM);
}


/**
 * For experimenting with NUMA affinity and such.
 *
 * @returns The current NUMA Node ID.
 */
static uint16_t gmmR0GetCurrentNumaNodeId(void)
{
#if 1
    return GMM_CHUNK_NUMA_ID_UNKNOWN;
#else
    return RTMpCpuId() / 16;
#endif
}



/**
 * Cleans up when a VM is terminating.
 *
 * @param   pGVM    Pointer to the Global VM structure.
 */
GMMR0DECL(void) GMMR0CleanupVM(PGVM pGVM)
{
    LogFlow(("GMMR0CleanupVM: pGVM=%p:{.hSelf=%#x}\n", pGVM, pGVM->hSelf));

    PGMM pGMM;
    GMM_GET_VALID_INSTANCE_VOID(pGMM);

#ifdef VBOX_WITH_PAGE_SHARING
    /*
     * Clean up all registered shared modules first.
     */
    gmmR0SharedModuleCleanup(pGMM, pGVM);
#endif

    gmmR0MutexAcquire(pGMM);
    uint64_t uLockNanoTS = RTTimeSystemNanoTS();
    GMM_CHECK_SANITY_UPON_ENTERING(pGMM);

    /*
     * The policy is 'INVALID' until the initial reservation
     * request has been serviced.
     */
    if (    pGVM->gmm.s.Stats.enmPolicy > GMMOCPOLICY_INVALID
        &&  pGVM->gmm.s.Stats.enmPolicy < GMMOCPOLICY_END)
    {
        /*
         * If it's the last VM around, we can skip walking all the chunk looking
         * for the pages owned by this VM and instead flush the whole shebang.
         *
         * This takes care of the eventuality that a VM has left shared page
         * references behind (shouldn't happen of course, but you never know).
         */
        Assert(pGMM->cRegisteredVMs);
        pGMM->cRegisteredVMs--;

        /*
         * Walk the entire pool looking for pages that belong to this VM
         * and leftover mappings.  (This'll only catch private pages,
         * shared pages will be 'left behind'.)
         */
        /** @todo r=bird: This scanning+freeing could be optimized in bound mode! */
        uint64_t    cPrivatePages = pGVM->gmm.s.Stats.cPrivatePages; /* save */

        unsigned    iCountDown = 64;
        bool        fRedoFromStart;
        PGMMCHUNK   pChunk;
        do
        {
            fRedoFromStart = false;
            RTListForEachReverse(&pGMM->ChunkList, pChunk, GMMCHUNK, ListNode)
            {
                uint32_t const cFreeChunksOld = pGMM->cFreedChunks;
                if (   (   !pGMM->fBoundMemoryMode
                        || pChunk->hGVM == pGVM->hSelf)
                    && gmmR0CleanupVMScanChunk(pGMM, pGVM, pChunk))
                {
                    /* We left the giant mutex, so reset the yield counters. */
                    uLockNanoTS = RTTimeSystemNanoTS();
                    iCountDown  = 64;
                }
                else
                {
                    /* Didn't leave it, so do normal yielding. */
                    if (!iCountDown)
                        gmmR0MutexYield(pGMM, &uLockNanoTS);
                    else
                        iCountDown--;
                }
                if (pGMM->cFreedChunks != cFreeChunksOld)
                {
                    fRedoFromStart = true;
                    break;
                }
            }
        } while (fRedoFromStart);

        if (pGVM->gmm.s.Stats.cPrivatePages)
            SUPR0Printf("GMMR0CleanupVM: hGVM=%#x has %#x private pages that cannot be found!\n", pGVM->hSelf, pGVM->gmm.s.Stats.cPrivatePages);

        pGMM->cAllocatedPages -= cPrivatePages;

        /*
         * Free empty chunks.
         */
        PGMMCHUNKFREESET pPrivateSet = pGMM->fBoundMemoryMode ? &pGVM->gmm.s.Private : &pGMM->PrivateX;
        do
        {
            fRedoFromStart = false;
            iCountDown = 10240;
            pChunk = pPrivateSet->apLists[GMM_CHUNK_FREE_SET_UNUSED_LIST];
            while (pChunk)
            {
                PGMMCHUNK pNext = pChunk->pFreeNext;
                Assert(pChunk->cFree == GMM_CHUNK_NUM_PAGES);
                if (   !pGMM->fBoundMemoryMode
                    || pChunk->hGVM == pGVM->hSelf)
                {
                    uint64_t const idGenerationOld = pPrivateSet->idGeneration;
                    if (gmmR0FreeChunk(pGMM, pGVM, pChunk, true /*fRelaxedSem*/))
                    {
                        /* We've left the giant mutex, restart? (+1 for our unlink) */
                        fRedoFromStart = pPrivateSet->idGeneration != idGenerationOld + 1;
                        if (fRedoFromStart)
                            break;
                        uLockNanoTS = RTTimeSystemNanoTS();
                        iCountDown = 10240;
                    }
                }

                /* Advance and maybe yield the lock. */
                pChunk = pNext;
                if (--iCountDown == 0)
                {
                    uint64_t const idGenerationOld = pPrivateSet->idGeneration;
                    fRedoFromStart = gmmR0MutexYield(pGMM, &uLockNanoTS)
                                  && pPrivateSet->idGeneration != idGenerationOld;
                    if (fRedoFromStart)
                        break;
                    iCountDown = 10240;
                }
            }
        } while (fRedoFromStart);

        /*
         * Account for shared pages that weren't freed.
         */
        if (pGVM->gmm.s.Stats.cSharedPages)
        {
            Assert(pGMM->cSharedPages >= pGVM->gmm.s.Stats.cSharedPages);
            SUPR0Printf("GMMR0CleanupVM: hGVM=%#x left %#x shared pages behind!\n", pGVM->hSelf, pGVM->gmm.s.Stats.cSharedPages);
            pGMM->cLeftBehindSharedPages += pGVM->gmm.s.Stats.cSharedPages;
        }

        /*
         * Clean up balloon statistics in case the VM process crashed.
         */
        Assert(pGMM->cBalloonedPages >= pGVM->gmm.s.Stats.cBalloonedPages);
        pGMM->cBalloonedPages -= pGVM->gmm.s.Stats.cBalloonedPages;

        /*
         * Update the over-commitment management statistics.
         */
        pGMM->cReservedPages -= pGVM->gmm.s.Stats.Reserved.cBasePages
                              + pGVM->gmm.s.Stats.Reserved.cFixedPages
                              + pGVM->gmm.s.Stats.Reserved.cShadowPages;
        switch (pGVM->gmm.s.Stats.enmPolicy)
        {
            case GMMOCPOLICY_NO_OC:
                break;
            default:
                /** @todo Update GMM->cOverCommittedPages */
                break;
        }
    }

    /* zap the GVM data. */
    pGVM->gmm.s.Stats.enmPolicy    = GMMOCPOLICY_INVALID;
    pGVM->gmm.s.Stats.enmPriority  = GMMPRIORITY_INVALID;
    pGVM->gmm.s.Stats.fMayAllocate = false;

    GMM_CHECK_SANITY_UPON_LEAVING(pGMM);
    gmmR0MutexRelease(pGMM);

    /*
     * Destroy the spinlock.
     */
    RTSPINLOCK hSpinlock = NIL_RTSPINLOCK;
    ASMAtomicXchgHandle(&pGVM->gmm.s.hChunkTlbSpinLock, NIL_RTSPINLOCK, &hSpinlock);
    RTSpinlockDestroy(hSpinlock);

    LogFlow(("GMMR0CleanupVM: returns\n"));
}


/**
 * Scan one chunk for private pages belonging to the specified VM.
 *
 * @note    This function may drop the giant mutex!
 *
 * @returns @c true if we've temporarily dropped the giant mutex, @c false if
 *          we didn't.
 * @param   pGMM        Pointer to the GMM instance.
 * @param   pGVM        The global VM handle.
 * @param   pChunk      The chunk to scan.
 */
static bool gmmR0CleanupVMScanChunk(PGMM pGMM, PGVM pGVM, PGMMCHUNK pChunk)
{
    Assert(!pGMM->fBoundMemoryMode || pChunk->hGVM == pGVM->hSelf);

    /*
     * Look for pages belonging to the VM.
     * (Perform some internal checks while we're scanning.)
     */
#ifndef VBOX_STRICT
    if (pChunk->cFree != GMM_CHUNK_NUM_PAGES)
#endif
    {
        unsigned cPrivate = 0;
        unsigned cShared = 0;
        unsigned cFree = 0;

        gmmR0UnlinkChunk(pChunk);       /* avoiding cFreePages updates. */

        uint16_t hGVM = pGVM->hSelf;
        unsigned iPage = (GMM_CHUNK_SIZE >> GUEST_PAGE_SHIFT);
        while (iPage-- > 0)
            if (GMM_PAGE_IS_PRIVATE(&pChunk->aPages[iPage]))
            {
                if (pChunk->aPages[iPage].Private.hGVM == hGVM)
                {
                    /*
                     * Free the page.
                     *
                     * The reason for not using gmmR0FreePrivatePage here is that we
                     * must *not* cause the chunk to be freed from under us - we're in
                     * an AVL tree walk here.
                     */
                    pChunk->aPages[iPage].u = 0;
                    pChunk->aPages[iPage].Free.u2State = GMM_PAGE_STATE_FREE;
                    pChunk->aPages[iPage].Free.fZeroed = false;
                    pChunk->aPages[iPage].Free.iNext   = pChunk->iFreeHead;
                    pChunk->iFreeHead = iPage;
                    pChunk->cPrivate--;
                    pChunk->cFree++;
                    pGVM->gmm.s.Stats.cPrivatePages--;
                    cFree++;
                }
                else
                    cPrivate++;
            }
            else if (GMM_PAGE_IS_FREE(&pChunk->aPages[iPage]))
                cFree++;
            else
                cShared++;

        gmmR0SelectSetAndLinkChunk(pGMM, pGVM, pChunk);

        /*
         * Did it add up?
         */
        if (RT_UNLIKELY(    pChunk->cFree != cFree
                        ||  pChunk->cPrivate != cPrivate
                        ||  pChunk->cShared != cShared))
        {
            SUPR0Printf("gmmR0CleanupVMScanChunk: Chunk %RKv/%#x has bogus stats - free=%d/%d private=%d/%d shared=%d/%d\n",
                        pChunk, pChunk->Core.Key, pChunk->cFree, cFree, pChunk->cPrivate, cPrivate, pChunk->cShared, cShared);
            pChunk->cFree = cFree;
            pChunk->cPrivate = cPrivate;
            pChunk->cShared = cShared;
        }
    }

    /*
     * If not in bound memory mode, we should reset the hGVM field
     * if it has our handle in it.
     */
    if (pChunk->hGVM == pGVM->hSelf)
    {
        if (!g_pGMM->fBoundMemoryMode)
            pChunk->hGVM = NIL_GVM_HANDLE;
        else if (pChunk->cFree != GMM_CHUNK_NUM_PAGES)
        {
            SUPR0Printf("gmmR0CleanupVMScanChunk: %RKv/%#x: cFree=%#x - it should be 0 in bound mode!\n",
                        pChunk, pChunk->Core.Key, pChunk->cFree);
            AssertMsgFailed(("%p/%#x: cFree=%#x - it should be 0 in bound mode!\n", pChunk, pChunk->Core.Key, pChunk->cFree));

            gmmR0UnlinkChunk(pChunk);
            pChunk->cFree = GMM_CHUNK_NUM_PAGES;
            gmmR0SelectSetAndLinkChunk(pGMM, pGVM, pChunk);
        }
    }

    /*
     * Look for a mapping belonging to the terminating VM.
     */
    GMMR0CHUNKMTXSTATE MtxState;
    gmmR0ChunkMutexAcquire(&MtxState, pGMM, pChunk, GMMR0CHUNK_MTX_KEEP_GIANT);
    unsigned cMappings = pChunk->cMappingsX;
    for (unsigned i = 0; i < cMappings; i++)
        if (pChunk->paMappingsX[i].pGVM == pGVM)
        {
            gmmR0ChunkMutexDropGiant(&MtxState);

            RTR0MEMOBJ hMemObj = pChunk->paMappingsX[i].hMapObj;

            cMappings--;
            if (i < cMappings)
                 pChunk->paMappingsX[i] = pChunk->paMappingsX[cMappings];
            pChunk->paMappingsX[cMappings].pGVM    = NULL;
            pChunk->paMappingsX[cMappings].hMapObj = NIL_RTR0MEMOBJ;
            Assert(pChunk->cMappingsX - 1U == cMappings);
            pChunk->cMappingsX = cMappings;

            int rc = RTR0MemObjFree(hMemObj, false /* fFreeMappings (NA) */);
            if (RT_FAILURE(rc))
            {
                SUPR0Printf("gmmR0CleanupVMScanChunk: %RKv/%#x: mapping #%x: RTRMemObjFree(%RKv,false) -> %d \n",
                            pChunk, pChunk->Core.Key, i, hMemObj, rc);
                AssertRC(rc);
            }

            gmmR0ChunkMutexRelease(&MtxState, pChunk);
            return true;
        }

    gmmR0ChunkMutexRelease(&MtxState, pChunk);
    return false;
}


/**
 * The initial resource reservations.
 *
 * This will make memory reservations according to policy and priority. If there aren't
 * sufficient resources available to sustain the VM this function will fail and all
 * future allocations requests will fail as well.
 *
 * These are just the initial reservations made very very early during the VM creation
 * process and will be adjusted later in the GMMR0UpdateReservation call after the
 * ring-3 init has completed.
 *
 * @returns VBox status code.
 * @retval  VERR_GMM_MEMORY_RESERVATION_DECLINED
 * @retval  VERR_GMM_
 *
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   idCpu           The VCPU id - must be zero.
 * @param   cBasePages      The number of pages that may be allocated for the base RAM and ROMs.
 *                          This does not include MMIO2 and similar.
 * @param   cShadowPages    The number of pages that may be allocated for shadow paging structures.
 * @param   cFixedPages     The number of pages that may be allocated for fixed objects like the
 *                          hyper heap, MMIO2 and similar.
 * @param   enmPolicy       The OC policy to use on this VM.
 * @param   enmPriority     The priority in an out-of-memory situation.
 *
 * @thread  The creator thread / EMT(0).
 */
GMMR0DECL(int) GMMR0InitialReservation(PGVM pGVM, VMCPUID idCpu, uint64_t cBasePages, uint32_t cShadowPages,
                                       uint32_t cFixedPages, GMMOCPOLICY enmPolicy, GMMPRIORITY enmPriority)
{
    LogFlow(("GMMR0InitialReservation: pGVM=%p cBasePages=%#llx cShadowPages=%#x cFixedPages=%#x enmPolicy=%d enmPriority=%d\n",
             pGVM, cBasePages, cShadowPages, cFixedPages, enmPolicy, enmPriority));

    /*
     * Validate, get basics and take the semaphore.
     */
    AssertReturn(idCpu == 0, VERR_INVALID_CPU_ID);
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_GMM_INSTANCE);
    int rc = GVMMR0ValidateGVMandEMT(pGVM, idCpu);
    if (RT_FAILURE(rc))
        return rc;

    AssertReturn(cBasePages, VERR_INVALID_PARAMETER);
    AssertReturn(cShadowPages, VERR_INVALID_PARAMETER);
    AssertReturn(cFixedPages, VERR_INVALID_PARAMETER);
    AssertReturn(enmPolicy > GMMOCPOLICY_INVALID && enmPolicy < GMMOCPOLICY_END, VERR_INVALID_PARAMETER);
    AssertReturn(enmPriority > GMMPRIORITY_INVALID && enmPriority < GMMPRIORITY_END, VERR_INVALID_PARAMETER);

    gmmR0MutexAcquire(pGMM);
    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {
        if (    !pGVM->gmm.s.Stats.Reserved.cBasePages
            &&  !pGVM->gmm.s.Stats.Reserved.cFixedPages
            &&  !pGVM->gmm.s.Stats.Reserved.cShadowPages)
        {
            /*
             * Check if we can accommodate this.
             */
            /* ... later ... */
            if (RT_SUCCESS(rc))
            {
                /*
                 * Update the records.
                 */
                pGVM->gmm.s.Stats.Reserved.cBasePages   = cBasePages;
                pGVM->gmm.s.Stats.Reserved.cFixedPages  = cFixedPages;
                pGVM->gmm.s.Stats.Reserved.cShadowPages = cShadowPages;
                pGVM->gmm.s.Stats.enmPolicy             = enmPolicy;
                pGVM->gmm.s.Stats.enmPriority           = enmPriority;
                pGVM->gmm.s.Stats.fMayAllocate          = true;

                pGMM->cReservedPages += cBasePages + cFixedPages + cShadowPages;
                pGMM->cRegisteredVMs++;
            }
        }
        else
            rc = VERR_WRONG_ORDER;
        GMM_CHECK_SANITY_UPON_LEAVING(pGMM);
    }
    else
        rc = VERR_GMM_IS_NOT_SANE;
    gmmR0MutexRelease(pGMM);
    LogFlow(("GMMR0InitialReservation: returns %Rrc\n", rc));
    return rc;
}


/**
 * VMMR0 request wrapper for GMMR0InitialReservation.
 *
 * @returns see GMMR0InitialReservation.
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   idCpu           The VCPU id.
 * @param   pReq            Pointer to the request packet.
 */
GMMR0DECL(int) GMMR0InitialReservationReq(PGVM pGVM, VMCPUID idCpu, PGMMINITIALRESERVATIONREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pGVM, VERR_INVALID_POINTER);
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(*pReq), ("%#x != %#x\n", pReq->Hdr.cbReq, sizeof(*pReq)), VERR_INVALID_PARAMETER);

    return GMMR0InitialReservation(pGVM, idCpu, pReq->cBasePages, pReq->cShadowPages,
                                   pReq->cFixedPages, pReq->enmPolicy, pReq->enmPriority);
}


/**
 * This updates the memory reservation with the additional MMIO2 and ROM pages.
 *
 * @returns VBox status code.
 * @retval  VERR_GMM_MEMORY_RESERVATION_DECLINED
 *
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   idCpu           The VCPU id.
 * @param   cBasePages      The number of pages that may be allocated for the base RAM and ROMs.
 *                          This does not include MMIO2 and similar.
 * @param   cShadowPages    The number of pages that may be allocated for shadow paging structures.
 * @param   cFixedPages     The number of pages that may be allocated for fixed objects like the
 *                          hyper heap, MMIO2 and similar.
 *
 * @thread  EMT(idCpu)
 */
GMMR0DECL(int) GMMR0UpdateReservation(PGVM pGVM, VMCPUID idCpu, uint64_t cBasePages,
                                      uint32_t cShadowPages, uint32_t cFixedPages)
{
    LogFlow(("GMMR0UpdateReservation: pGVM=%p cBasePages=%#llx cShadowPages=%#x cFixedPages=%#x\n",
             pGVM, cBasePages, cShadowPages, cFixedPages));

    /*
     * Validate, get basics and take the semaphore.
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_GMM_INSTANCE);
    int rc = GVMMR0ValidateGVMandEMT(pGVM, idCpu);
    if (RT_FAILURE(rc))
        return rc;

    AssertReturn(cBasePages, VERR_INVALID_PARAMETER);
    AssertReturn(cShadowPages, VERR_INVALID_PARAMETER);
    AssertReturn(cFixedPages, VERR_INVALID_PARAMETER);

    gmmR0MutexAcquire(pGMM);
    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {
        if (    pGVM->gmm.s.Stats.Reserved.cBasePages
            &&  pGVM->gmm.s.Stats.Reserved.cFixedPages
            &&  pGVM->gmm.s.Stats.Reserved.cShadowPages)
        {
            /*
             * Check if we can accommodate this.
             */
            /* ... later ... */
            if (RT_SUCCESS(rc))
            {
                /*
                 * Update the records.
                 */
                pGMM->cReservedPages -= pGVM->gmm.s.Stats.Reserved.cBasePages
                                      + pGVM->gmm.s.Stats.Reserved.cFixedPages
                                      + pGVM->gmm.s.Stats.Reserved.cShadowPages;
                pGMM->cReservedPages += cBasePages + cFixedPages + cShadowPages;

                pGVM->gmm.s.Stats.Reserved.cBasePages   = cBasePages;
                pGVM->gmm.s.Stats.Reserved.cFixedPages  = cFixedPages;
                pGVM->gmm.s.Stats.Reserved.cShadowPages = cShadowPages;
            }
        }
        else
            rc = VERR_WRONG_ORDER;
        GMM_CHECK_SANITY_UPON_LEAVING(pGMM);
    }
    else
        rc = VERR_GMM_IS_NOT_SANE;
    gmmR0MutexRelease(pGMM);
    LogFlow(("GMMR0UpdateReservation: returns %Rrc\n", rc));
    return rc;
}


/**
 * VMMR0 request wrapper for GMMR0UpdateReservation.
 *
 * @returns see GMMR0UpdateReservation.
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   idCpu           The VCPU id.
 * @param   pReq            Pointer to the request packet.
 */
GMMR0DECL(int) GMMR0UpdateReservationReq(PGVM pGVM, VMCPUID idCpu, PGMMUPDATERESERVATIONREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(*pReq), ("%#x != %#x\n", pReq->Hdr.cbReq, sizeof(*pReq)), VERR_INVALID_PARAMETER);

    return GMMR0UpdateReservation(pGVM, idCpu, pReq->cBasePages, pReq->cShadowPages, pReq->cFixedPages);
}

#ifdef GMMR0_WITH_SANITY_CHECK

/**
 * Performs sanity checks on a free set.
 *
 * @returns Error count.
 *
 * @param   pGMM        Pointer to the GMM instance.
 * @param   pSet        Pointer to the set.
 * @param   pszSetName  The set name.
 * @param   pszFunction The function from which it was called.
 * @param   uLine       The line number.
 */
static uint32_t gmmR0SanityCheckSet(PGMM pGMM, PGMMCHUNKFREESET pSet, const char *pszSetName,
                                    const char *pszFunction, unsigned uLineNo)
{
    uint32_t cErrors = 0;

    /*
     * Count the free pages in all the chunks and match it against pSet->cFreePages.
     */
    uint32_t cPages = 0;
    for (unsigned i = 0; i < RT_ELEMENTS(pSet->apLists); i++)
    {
        for (PGMMCHUNK pCur = pSet->apLists[i]; pCur; pCur = pCur->pFreeNext)
        {
            /** @todo check that the chunk is hash into the right set. */
            cPages += pCur->cFree;
        }
    }
    if (RT_UNLIKELY(cPages != pSet->cFreePages))
    {
        SUPR0Printf("GMM insanity: found %#x pages in the %s set, expected %#x. (%s, line %u)\n",
                    cPages, pszSetName, pSet->cFreePages, pszFunction, uLineNo);
        cErrors++;
    }

    return cErrors;
}


/**
 * Performs some sanity checks on the GMM while owning lock.
 *
 * @returns Error count.
 *
 * @param   pGMM        Pointer to the GMM instance.
 * @param   pszFunction The function from which it is called.
 * @param   uLineNo     The line number.
 */
static uint32_t gmmR0SanityCheck(PGMM pGMM, const char *pszFunction, unsigned uLineNo)
{
    uint32_t cErrors = 0;

    cErrors += gmmR0SanityCheckSet(pGMM, &pGMM->PrivateX, "private", pszFunction, uLineNo);
    cErrors += gmmR0SanityCheckSet(pGMM, &pGMM->Shared,   "shared",  pszFunction, uLineNo);
    /** @todo add more sanity checks. */

    return cErrors;
}

#endif /* GMMR0_WITH_SANITY_CHECK */

/**
 * Looks up a chunk in the tree and fill in the TLB entry for it.
 *
 * This is not expected to fail and will bitch if it does.
 *
 * @returns Pointer to the allocation chunk, NULL if not found.
 * @param   pGMM        Pointer to the GMM instance.
 * @param   idChunk     The ID of the chunk to find.
 * @param   pTlbe       Pointer to the TLB entry.
 *
 * @note    Caller owns spinlock.
 */
static PGMMCHUNK gmmR0GetChunkSlow(PGMM pGMM, uint32_t idChunk, PGMMCHUNKTLBE pTlbe)
{
    PGMMCHUNK pChunk = (PGMMCHUNK)RTAvlU32Get(&pGMM->pChunks, idChunk);
    AssertMsgReturn(pChunk, ("Chunk %#x not found!\n", idChunk), NULL);
    pTlbe->idChunk = idChunk;
    pTlbe->pChunk = pChunk;
    return pChunk;
}


/**
 * Finds a allocation chunk, spin-locked.
 *
 * This is not expected to fail and will bitch if it does.
 *
 * @returns Pointer to the allocation chunk, NULL if not found.
 * @param   pGMM        Pointer to the GMM instance.
 * @param   idChunk     The ID of the chunk to find.
 */
DECLINLINE(PGMMCHUNK) gmmR0GetChunkLocked(PGMM pGMM, uint32_t idChunk)
{
    /*
     * Do a TLB lookup, branch if not in the TLB.
     */
    PGMMCHUNKTLBE pTlbe  = &pGMM->ChunkTLB.aEntries[GMM_CHUNKTLB_IDX(idChunk)];
    PGMMCHUNK     pChunk = pTlbe->pChunk;
    if (   pChunk == NULL
        || pTlbe->idChunk != idChunk)
        pChunk = gmmR0GetChunkSlow(pGMM, idChunk, pTlbe);
    return pChunk;
}


/**
 * Finds a allocation chunk.
 *
 * This is not expected to fail and will bitch if it does.
 *
 * @returns Pointer to the allocation chunk, NULL if not found.
 * @param   pGMM        Pointer to the GMM instance.
 * @param   idChunk     The ID of the chunk to find.
 */
DECLINLINE(PGMMCHUNK) gmmR0GetChunk(PGMM pGMM, uint32_t idChunk)
{
    RTSpinlockAcquire(pGMM->hSpinLockTree);
    PGMMCHUNK pChunk = gmmR0GetChunkLocked(pGMM, idChunk);
    RTSpinlockRelease(pGMM->hSpinLockTree);
    return pChunk;
}


/**
 * Finds a page.
 *
 * This is not expected to fail and will bitch if it does.
 *
 * @returns Pointer to the page, NULL if not found.
 * @param   pGMM        Pointer to the GMM instance.
 * @param   idPage      The ID of the page to find.
 */
DECLINLINE(PGMMPAGE) gmmR0GetPage(PGMM pGMM, uint32_t idPage)
{
    PGMMCHUNK pChunk = gmmR0GetChunk(pGMM, idPage >> GMM_CHUNKID_SHIFT);
    if (RT_LIKELY(pChunk))
        return &pChunk->aPages[idPage & GMM_PAGEID_IDX_MASK];
    return NULL;
}


#if 0 /* unused */
/**
 * Gets the host physical address for a page given by it's ID.
 *
 * @returns The host physical address or NIL_RTHCPHYS.
 * @param   pGMM        Pointer to the GMM instance.
 * @param   idPage      The ID of the page to find.
 */
DECLINLINE(RTHCPHYS) gmmR0GetPageHCPhys(PGMM pGMM,  uint32_t idPage)
{
    PGMMCHUNK pChunk = gmmR0GetChunk(pGMM, idPage >> GMM_CHUNKID_SHIFT);
    if (RT_LIKELY(pChunk))
        return RTR0MemObjGetPagePhysAddr(pChunk->hMemObj, idPage & GMM_PAGEID_IDX_MASK);
    return NIL_RTHCPHYS;
}
#endif /* unused */


/**
 * Selects the appropriate free list given the number of free pages.
 *
 * @returns Free list index.
 * @param   cFree       The number of free pages in the chunk.
 */
DECLINLINE(unsigned) gmmR0SelectFreeSetList(unsigned cFree)
{
    unsigned iList = cFree >> GMM_CHUNK_FREE_SET_SHIFT;
    AssertMsg(iList < RT_SIZEOFMEMB(GMMCHUNKFREESET, apLists) / RT_SIZEOFMEMB(GMMCHUNKFREESET, apLists[0]),
              ("%d (%u)\n", iList, cFree));
    return iList;
}


/**
 * Unlinks the chunk from the free list it's currently on (if any).
 *
 * @param   pChunk      The allocation chunk.
 */
DECLINLINE(void) gmmR0UnlinkChunk(PGMMCHUNK pChunk)
{
    PGMMCHUNKFREESET pSet = pChunk->pSet;
    if (RT_LIKELY(pSet))
    {
        pSet->cFreePages -= pChunk->cFree;
        pSet->idGeneration++;

        PGMMCHUNK pPrev = pChunk->pFreePrev;
        PGMMCHUNK pNext = pChunk->pFreeNext;
        if (pPrev)
            pPrev->pFreeNext = pNext;
        else
            pSet->apLists[gmmR0SelectFreeSetList(pChunk->cFree)] = pNext;
        if (pNext)
            pNext->pFreePrev = pPrev;

        pChunk->pSet = NULL;
        pChunk->pFreeNext = NULL;
        pChunk->pFreePrev = NULL;
    }
    else
    {
        Assert(!pChunk->pFreeNext);
        Assert(!pChunk->pFreePrev);
        Assert(!pChunk->cFree);
    }
}


/**
 * Links the chunk onto the appropriate free list in the specified free set.
 *
 * If no free entries, it's not linked into any list.
 *
 * @param   pChunk      The allocation chunk.
 * @param   pSet        The free set.
 */
DECLINLINE(void) gmmR0LinkChunk(PGMMCHUNK pChunk, PGMMCHUNKFREESET pSet)
{
    Assert(!pChunk->pSet);
    Assert(!pChunk->pFreeNext);
    Assert(!pChunk->pFreePrev);

    if (pChunk->cFree > 0)
    {
        pChunk->pSet = pSet;
        pChunk->pFreePrev = NULL;
        unsigned const iList = gmmR0SelectFreeSetList(pChunk->cFree);
        pChunk->pFreeNext = pSet->apLists[iList];
        if (pChunk->pFreeNext)
            pChunk->pFreeNext->pFreePrev = pChunk;
        pSet->apLists[iList] = pChunk;

        pSet->cFreePages += pChunk->cFree;
        pSet->idGeneration++;
    }
}


/**
 * Links the chunk onto the appropriate free list in the specified free set.
 *
 * If no free entries, it's not linked into any list.
 *
 * @param   pGMM        Pointer to the GMM instance.
 * @param   pGVM        Pointer to the kernel-only VM instace data.
 * @param   pChunk      The allocation chunk.
 */
DECLINLINE(void) gmmR0SelectSetAndLinkChunk(PGMM pGMM, PGVM pGVM, PGMMCHUNK pChunk)
{
    PGMMCHUNKFREESET pSet;
    if (pGMM->fBoundMemoryMode)
        pSet = &pGVM->gmm.s.Private;
    else if (pChunk->cShared)
        pSet = &pGMM->Shared;
    else
        pSet = &pGMM->PrivateX;
    gmmR0LinkChunk(pChunk, pSet);
}


/**
 * Frees a Chunk ID.
 *
 * @param   pGMM        Pointer to the GMM instance.
 * @param   idChunk     The Chunk ID to free.
 */
static void gmmR0FreeChunkId(PGMM pGMM, uint32_t idChunk)
{
    AssertReturnVoid(idChunk != NIL_GMM_CHUNKID);
    RTSpinlockAcquire(pGMM->hSpinLockChunkId); /* We could probably skip the locking here, I think. */

    AssertMsg(ASMBitTest(&pGMM->bmChunkId[0], idChunk), ("%#x\n", idChunk));
    ASMAtomicBitClear(&pGMM->bmChunkId[0], idChunk);

    RTSpinlockRelease(pGMM->hSpinLockChunkId);
}


/**
 * Allocates a new Chunk ID.
 *
 * @returns The Chunk ID.
 * @param   pGMM        Pointer to the GMM instance.
 */
static uint32_t gmmR0AllocateChunkId(PGMM pGMM)
{
    AssertCompile(!((GMM_CHUNKID_LAST + 1) & 31)); /* must be a multiple of 32 */
    AssertCompile(NIL_GMM_CHUNKID == 0);

    RTSpinlockAcquire(pGMM->hSpinLockChunkId);

    /*
     * Try the next sequential one.
     */
    int32_t idChunk = ++pGMM->idChunkPrev;
    if (   (uint32_t)idChunk <= GMM_CHUNKID_LAST
        && idChunk > NIL_GMM_CHUNKID)
    {
        if (!ASMAtomicBitTestAndSet(&pGMM->bmChunkId[0], idChunk))
        {
            RTSpinlockRelease(pGMM->hSpinLockChunkId);
            return idChunk;
        }

        /*
         * Scan sequentially from the last one.
         */
        if ((uint32_t)idChunk < GMM_CHUNKID_LAST)
        {
            idChunk = ASMBitNextClear(&pGMM->bmChunkId[0], GMM_CHUNKID_LAST + 1, idChunk);
            if (   idChunk > NIL_GMM_CHUNKID
                && (uint32_t)idChunk <= GMM_CHUNKID_LAST)
            {
                AssertMsgReturnStmt(!ASMAtomicBitTestAndSet(&pGMM->bmChunkId[0], idChunk), ("%#x\n", idChunk),
                                    RTSpinlockRelease(pGMM->hSpinLockChunkId), NIL_GMM_CHUNKID);

                pGMM->idChunkPrev = idChunk;
                RTSpinlockRelease(pGMM->hSpinLockChunkId);
                return idChunk;
            }
        }
    }

    /*
     * Ok, scan from the start.
     * We're not racing anyone, so there is no need to expect failures or have restart loops.
     */
    idChunk = ASMBitFirstClear(&pGMM->bmChunkId[0], GMM_CHUNKID_LAST + 1);
    AssertMsgReturnStmt(idChunk > NIL_GMM_CHUNKID && (uint32_t)idChunk <= GMM_CHUNKID_LAST, ("%#x\n", idChunk),
                        RTSpinlockRelease(pGMM->hSpinLockChunkId), NIL_GVM_HANDLE);
    AssertMsgReturnStmt(!ASMAtomicBitTestAndSet(&pGMM->bmChunkId[0], idChunk), ("%#x\n", idChunk),
                        RTSpinlockRelease(pGMM->hSpinLockChunkId), NIL_GMM_CHUNKID);

    pGMM->idChunkPrev = idChunk;
    RTSpinlockRelease(pGMM->hSpinLockChunkId);
    return idChunk;
}


/**
 * Allocates one private page.
 *
 * Worker for gmmR0AllocatePages.
 *
 * @param   pChunk      The chunk to allocate it from.
 * @param   hGVM        The GVM handle of the VM requesting memory.
 * @param   pPageDesc   The page descriptor.
 */
static void gmmR0AllocatePage(PGMMCHUNK pChunk, uint32_t hGVM, PGMMPAGEDESC pPageDesc)
{
    /* update the chunk stats. */
    if (pChunk->hGVM == NIL_GVM_HANDLE)
        pChunk->hGVM = hGVM;
    Assert(pChunk->cFree);
    pChunk->cFree--;
    pChunk->cPrivate++;

    /* unlink the first free page. */
    const uint32_t iPage = pChunk->iFreeHead;
    AssertReleaseMsg(iPage < RT_ELEMENTS(pChunk->aPages), ("%d\n", iPage));
    PGMMPAGE pPage = &pChunk->aPages[iPage];
    Assert(GMM_PAGE_IS_FREE(pPage));
    pChunk->iFreeHead = pPage->Free.iNext;
    Log3(("A pPage=%p iPage=%#x/%#x u2State=%d iFreeHead=%#x iNext=%#x\n",
          pPage, iPage, (pChunk->Core.Key << GMM_CHUNKID_SHIFT) | iPage,
          pPage->Common.u2State, pChunk->iFreeHead, pPage->Free.iNext));

    bool const fZeroed = pPage->Free.fZeroed;

    /* make the page private. */
    pPage->u = 0;
    AssertCompile(GMM_PAGE_STATE_PRIVATE == 0);
    pPage->Private.hGVM = hGVM;
    AssertCompile(NIL_RTHCPHYS >= GMM_GCPHYS_LAST);
    AssertCompile(GMM_GCPHYS_UNSHAREABLE >= GMM_GCPHYS_LAST);
    if (pPageDesc->HCPhysGCPhys <= GMM_GCPHYS_LAST)
        pPage->Private.pfn = pPageDesc->HCPhysGCPhys >> GUEST_PAGE_SHIFT;
    else
        pPage->Private.pfn = GMM_PAGE_PFN_UNSHAREABLE; /* unshareable / unassigned - same thing. */

    /* update the page descriptor. */
    pPageDesc->idSharedPage = NIL_GMM_PAGEID;
    pPageDesc->idPage       = (pChunk->Core.Key << GMM_CHUNKID_SHIFT) | iPage;
    RTHCPHYS const HCPhys = RTR0MemObjGetPagePhysAddr(pChunk->hMemObj, iPage);
    Assert(HCPhys != NIL_RTHCPHYS); Assert(HCPhys < NIL_GMMPAGEDESC_PHYS);
    pPageDesc->HCPhysGCPhys = HCPhys;
    pPageDesc->fZeroed      = fZeroed;
}


/**
 * Picks the free pages from a chunk.
 *
 * @returns The new page descriptor table index.
 * @param   pChunk      The chunk.
 * @param   hGVM        The affinity of the chunk. NIL_GVM_HANDLE for no
 *                      affinity.
 * @param   iPage       The current page descriptor table index.
 * @param   cPages      The total number of pages to allocate.
 * @param   paPages     The page descriptor table (input + ouput).
 */
static uint32_t gmmR0AllocatePagesFromChunk(PGMMCHUNK pChunk, uint16_t const hGVM, uint32_t iPage, uint32_t cPages,
                                            PGMMPAGEDESC paPages)
{
    PGMMCHUNKFREESET pSet = pChunk->pSet; Assert(pSet);
    gmmR0UnlinkChunk(pChunk);

    for (; pChunk->cFree && iPage < cPages; iPage++)
        gmmR0AllocatePage(pChunk, hGVM, &paPages[iPage]);

    gmmR0LinkChunk(pChunk, pSet);
    return iPage;
}


/**
 * Registers a new chunk of memory.
 *
 * This is called by gmmR0AllocateOneChunk and GMMR0AllocateLargePage.
 *
 * In the  GMMR0AllocateLargePage case the GMM_CHUNK_FLAGS_LARGE_PAGE flag is
 * set and the chunk will be registered as fully allocated to save time.
 *
 * @returns VBox status code.  On success, the giant GMM lock will be held, the
 *          caller must release it (ugly).
 * @param   pGMM        Pointer to the GMM instance.
 * @param   pSet        Pointer to the set.
 * @param   hMemObj     The memory object for the chunk.
 * @param   hGVM        The affinity of the chunk. NIL_GVM_HANDLE for no
 *                      affinity.
 * @param   pSession    Same as @a hGVM.
 * @param   fChunkFlags The chunk flags, GMM_CHUNK_FLAGS_XXX.
 * @param   cPages      The number of pages requested.  Zero for large pages.
 * @param   paPages     The page descriptor table (input + output).  NULL for
 *                      large pages.
 * @param   piPage      The pointer to the page descriptor table index variable.
 *                      This will be updated.  NULL for large pages.
 * @param   ppChunk     Chunk address (out).
 *
 * @remarks The caller must not own the giant GMM mutex.
 *          The giant GMM mutex will be acquired and returned acquired in
 *          the success path.   On failure, no locks will be held.
 */
static int gmmR0RegisterChunk(PGMM pGMM, PGMMCHUNKFREESET pSet, RTR0MEMOBJ hMemObj, uint16_t hGVM, PSUPDRVSESSION pSession,
                              uint16_t fChunkFlags, uint32_t cPages, PGMMPAGEDESC paPages, uint32_t *piPage, PGMMCHUNK *ppChunk)
{
    /*
     * Validate input & state.
     */
    Assert(pGMM->hMtxOwner != RTThreadNativeSelf());
    Assert(hGVM != NIL_GVM_HANDLE || pGMM->fBoundMemoryMode);
    Assert(fChunkFlags == 0 || fChunkFlags == GMM_CHUNK_FLAGS_LARGE_PAGE);
    if (!(fChunkFlags &= GMM_CHUNK_FLAGS_LARGE_PAGE))
    {
        AssertPtr(paPages);
        AssertPtr(piPage);
        Assert(cPages > 0);
        Assert(cPages > *piPage);
    }
    else
    {
        Assert(cPages == 0);
        Assert(!paPages);
        Assert(!piPage);
    }

#ifndef VBOX_WITH_LINEAR_HOST_PHYS_MEM
    /*
     * Get a ring-0 mapping of the object.
     */
    uint8_t *pbMapping = (uint8_t *)RTR0MemObjAddress(hMemObj);
    if (!pbMapping)
    {
        RTR0MEMOBJ hMapObj;
        int rc = RTR0MemObjMapKernel(&hMapObj, hMemObj, (void *)-1, 0,  RTMEM_PROT_READ | RTMEM_PROT_WRITE);
        if (RT_SUCCESS(rc))
            pbMapping = (uint8_t *)RTR0MemObjAddress(hMapObj);
        else
            return rc;
        AssertPtr(pbMapping);
    }
#endif

    /*
     * Allocate a chunk and an ID for it.
     */
    int rc;
    PGMMCHUNK pChunk = (PGMMCHUNK)RTMemAllocZ(sizeof(*pChunk));
    if (pChunk)
    {
        pChunk->Core.Key = gmmR0AllocateChunkId(pGMM);
        if (   pChunk->Core.Key != NIL_GMM_CHUNKID
            && pChunk->Core.Key <= GMM_CHUNKID_LAST)
        {
            /*
             * Initialize it.
             */
            pChunk->hMemObj     = hMemObj;
#ifndef VBOX_WITH_LINEAR_HOST_PHYS_MEM
            pChunk->pbMapping   = pbMapping;
#endif
            pChunk->hGVM        = hGVM;
            pChunk->idNumaNode  = gmmR0GetCurrentNumaNodeId();
            pChunk->iChunkMtx   = UINT8_MAX;
            pChunk->fFlags      = fChunkFlags;
            pChunk->uidOwner    = pSession ? SUPR0GetSessionUid(pSession) : NIL_RTUID;
            /*pChunk->cShared   = 0; */

            uint32_t const iDstPageFirst = piPage ? *piPage : cPages;
            if (!(fChunkFlags & GMM_CHUNK_FLAGS_LARGE_PAGE))
            {
                /*
                 * Allocate the requested number of pages from the start of the chunk,
                 * queue the rest (if any) on the free list.
                 */
                uint32_t const cPagesAlloc = RT_MIN(cPages - iDstPageFirst, GMM_CHUNK_NUM_PAGES);
                pChunk->cPrivate    = cPagesAlloc;
                pChunk->cFree       = GMM_CHUNK_NUM_PAGES - cPagesAlloc;
                pChunk->iFreeHead   = GMM_CHUNK_NUM_PAGES > cPagesAlloc ? cPagesAlloc : UINT16_MAX;

                /* Alloc pages: */
                uint32_t const idPageChunk = pChunk->Core.Key << GMM_CHUNKID_SHIFT;
                uint32_t       iDstPage    = iDstPageFirst;
                uint32_t       iPage;
                for (iPage = 0; iPage < cPagesAlloc; iPage++, iDstPage++)
                {
                    if (paPages[iDstPage].HCPhysGCPhys <= GMM_GCPHYS_LAST)
                        pChunk->aPages[iPage].Private.pfn = paPages[iDstPage].HCPhysGCPhys >> GUEST_PAGE_SHIFT;
                    else
                        pChunk->aPages[iPage].Private.pfn = GMM_PAGE_PFN_UNSHAREABLE; /* unshareable / unassigned - same thing. */
                    pChunk->aPages[iPage].Private.hGVM    = hGVM;
                    pChunk->aPages[iPage].Private.u2State = GMM_PAGE_STATE_PRIVATE;

                    paPages[iDstPage].HCPhysGCPhys = RTR0MemObjGetPagePhysAddr(hMemObj, iPage);
                    paPages[iDstPage].fZeroed      = true;
                    paPages[iDstPage].idPage       = idPageChunk | iPage;
                    paPages[iDstPage].idSharedPage = NIL_GMM_PAGEID;
                }
                *piPage = iDstPage;

                /* Build free list: */
                if (iPage < RT_ELEMENTS(pChunk->aPages))
                {
                    Assert(pChunk->iFreeHead == iPage);
                    for (; iPage < RT_ELEMENTS(pChunk->aPages) - 1; iPage++)
                    {
                        pChunk->aPages[iPage].Free.u2State = GMM_PAGE_STATE_FREE;
                        pChunk->aPages[iPage].Free.fZeroed = true;
                        pChunk->aPages[iPage].Free.iNext   = iPage + 1;
                    }
                    pChunk->aPages[RT_ELEMENTS(pChunk->aPages) - 1].Free.u2State = GMM_PAGE_STATE_FREE;
                    pChunk->aPages[RT_ELEMENTS(pChunk->aPages) - 1].Free.fZeroed = true;
                    pChunk->aPages[RT_ELEMENTS(pChunk->aPages) - 1].Free.iNext   = UINT16_MAX;
                }
                else
                    Assert(pChunk->iFreeHead == UINT16_MAX);
            }
            else
            {
                /*
                 * Large page: Mark all pages as privately allocated (watered down gmmR0AllocatePage).
                 */
                pChunk->cFree       = 0;
                pChunk->cPrivate    = GMM_CHUNK_NUM_PAGES;
                pChunk->iFreeHead   = UINT16_MAX;

                for (unsigned iPage = 0; iPage < RT_ELEMENTS(pChunk->aPages); iPage++)
                {
                    pChunk->aPages[iPage].Private.pfn     = GMM_PAGE_PFN_UNSHAREABLE;
                    pChunk->aPages[iPage].Private.hGVM    = hGVM;
                    pChunk->aPages[iPage].Private.u2State = GMM_PAGE_STATE_PRIVATE;
                }
            }

            /*
             * Zero the memory if it wasn't zeroed by the host already.
             * This simplifies keeping secret kernel bits from userland and brings
             * everyone to the same level wrt allocation zeroing.
             */
            rc = VINF_SUCCESS;
            if (!RTR0MemObjWasZeroInitialized(hMemObj))
            {
#ifdef VBOX_WITH_LINEAR_HOST_PHYS_MEM
                if (!(fChunkFlags & GMM_CHUNK_FLAGS_LARGE_PAGE))
                {
                    for (uint32_t iPage = 0; iPage < GMM_CHUNK_SIZE / HOST_PAGE_SIZE; iPage++)
                    {
                        void *pvPage = NULL;
                        rc = SUPR0HCPhysToVirt(RTR0MemObjGetPagePhysAddr(hMemObj, iPage), &pvPage);
                        AssertRCBreak(rc);
                        RT_BZERO(pvPage, HOST_PAGE_SIZE);
                    }
                }
                else
                {
                    /* Can do the whole large page in one go. */
                    void *pvPage = NULL;
                    rc = SUPR0HCPhysToVirt(RTR0MemObjGetPagePhysAddr(hMemObj, 0), &pvPage);
                    AssertRC(rc);
                    if (RT_SUCCESS(rc))
                        RT_BZERO(pvPage, GMM_CHUNK_SIZE);
                }
#else
                RT_BZERO(pbMapping, GMM_CHUNK_SIZE);
#endif
            }
            if (RT_SUCCESS(rc))
            {
                *ppChunk = pChunk;

                /*
                 * Allocate a Chunk ID and insert it into the tree.
                 * This has to be done behind the mutex of course.
                 */
                rc = gmmR0MutexAcquire(pGMM);
                if (RT_SUCCESS(rc))
                {
                    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
                    {
                        RTSpinlockAcquire(pGMM->hSpinLockTree);
                        if (RTAvlU32Insert(&pGMM->pChunks, &pChunk->Core))
                        {
                            pGMM->cChunks++;
                            RTListAppend(&pGMM->ChunkList, &pChunk->ListNode);
                            RTSpinlockRelease(pGMM->hSpinLockTree);

                            gmmR0LinkChunk(pChunk, pSet);

                            LogFlow(("gmmR0RegisterChunk: pChunk=%p id=%#x cChunks=%d\n", pChunk, pChunk->Core.Key, pGMM->cChunks));
                            GMM_CHECK_SANITY_UPON_LEAVING(pGMM);
                            return VINF_SUCCESS;
                        }

                        /*
                         * Bail out.
                         */
                        RTSpinlockRelease(pGMM->hSpinLockTree);
                        rc = VERR_GMM_CHUNK_INSERT;
                    }
                    else
                        rc = VERR_GMM_IS_NOT_SANE;
                    gmmR0MutexRelease(pGMM);
                }
                *ppChunk = NULL;
            }

            /* Undo any page allocations. */
            if (!(fChunkFlags & GMM_CHUNK_FLAGS_LARGE_PAGE))
            {
                uint32_t const cToFree = pChunk->cPrivate;
                Assert(*piPage - iDstPageFirst == cToFree);
                for (uint32_t iDstPage = iDstPageFirst, iPage = 0; iPage < cToFree; iPage++, iDstPage++)
                {
                    paPages[iDstPageFirst].fZeroed = false;
                    if (pChunk->aPages[iPage].Private.pfn == GMM_PAGE_PFN_UNSHAREABLE)
                        paPages[iDstPageFirst].HCPhysGCPhys = NIL_GMMPAGEDESC_PHYS;
                    else
                        paPages[iDstPageFirst].HCPhysGCPhys = (RTHCPHYS)pChunk->aPages[iPage].Private.pfn << GUEST_PAGE_SHIFT;
                    paPages[iDstPageFirst].idPage       = NIL_GMM_PAGEID;
                    paPages[iDstPageFirst].idSharedPage = NIL_GMM_PAGEID;
                }
                *piPage = iDstPageFirst;
            }

            gmmR0FreeChunkId(pGMM, pChunk->Core.Key);
        }
        else
            rc = VERR_GMM_CHUNK_INSERT;
        RTMemFree(pChunk);
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}


/**
 * Allocate a new chunk, immediately pick the requested pages from it, and adds
 * what's remaining to the specified free set.
 *
 * @note    This will leave the giant mutex while allocating the new chunk!
 *
 * @returns VBox status code.
 * @param   pGMM        Pointer to the GMM instance data.
 * @param   pGVM        Pointer to the kernel-only VM instace data.
 * @param   pSet        Pointer to the free set.
 * @param   cPages      The number of pages requested.
 * @param   paPages     The page descriptor table (input + output).
 * @param   piPage      The pointer to the page descriptor table index variable.
 *                      This will be updated.
 */
static int gmmR0AllocateChunkNew(PGMM pGMM, PGVM pGVM, PGMMCHUNKFREESET pSet, uint32_t cPages,
                                 PGMMPAGEDESC paPages, uint32_t *piPage)
{
    gmmR0MutexRelease(pGMM);

    RTR0MEMOBJ hMemObj;
    int rc;
#ifdef VBOX_WITH_LINEAR_HOST_PHYS_MEM
    if (pGMM->fHasWorkingAllocPhysNC)
        rc = RTR0MemObjAllocPhysNC(&hMemObj, GMM_CHUNK_SIZE, NIL_RTHCPHYS);
    else
#endif
        rc = RTR0MemObjAllocPage(&hMemObj, GMM_CHUNK_SIZE, false /*fExecutable*/);
    if (RT_SUCCESS(rc))
    {
        PGMMCHUNK pIgnored;
        rc = gmmR0RegisterChunk(pGMM, pSet, hMemObj, pGVM->hSelf, pGVM->pSession, 0 /*fChunkFlags*/,
                                cPages, paPages, piPage, &pIgnored);
        if (RT_SUCCESS(rc))
            return VINF_SUCCESS;

        /* bail out */
        RTR0MemObjFree(hMemObj, true /* fFreeMappings */);
    }

    int rc2 = gmmR0MutexAcquire(pGMM);
    AssertRCReturn(rc2, RT_FAILURE(rc) ? rc : rc2);
    return rc;

}


/**
 * As a last restort we'll pick any page we can get.
 *
 * @returns The new page descriptor table index.
 * @param   pSet        The set to pick from.
 * @param   pGVM        Pointer to the global VM structure.
 * @param   uidSelf     The UID of the caller.
 * @param   iPage       The current page descriptor table index.
 * @param   cPages      The total number of pages to allocate.
 * @param   paPages     The page descriptor table (input + ouput).
 */
static uint32_t gmmR0AllocatePagesIndiscriminately(PGMMCHUNKFREESET pSet, PGVM pGVM, RTUID uidSelf,
                                                   uint32_t iPage, uint32_t cPages, PGMMPAGEDESC paPages)
{
    unsigned iList = RT_ELEMENTS(pSet->apLists);
    while (iList-- > 0)
    {
        PGMMCHUNK pChunk = pSet->apLists[iList];
        while (pChunk)
        {
            PGMMCHUNK pNext = pChunk->pFreeNext;
            if (   pChunk->uidOwner == uidSelf
                || (   pChunk->cMappingsX == 0
                    && pChunk->cFree == (GMM_CHUNK_SIZE >> GUEST_PAGE_SHIFT)))
            {
                iPage = gmmR0AllocatePagesFromChunk(pChunk, pGVM->hSelf, iPage, cPages, paPages);
                if (iPage >= cPages)
                    return iPage;
            }

            pChunk = pNext;
        }
    }
    return iPage;
}


/**
 * Pick pages from empty chunks on the same NUMA node.
 *
 * @returns The new page descriptor table index.
 * @param   pSet        The set to pick from.
 * @param   pGVM        Pointer to the global VM structure.
 * @param   uidSelf     The UID of the caller.
 * @param   iPage       The current page descriptor table index.
 * @param   cPages      The total number of pages to allocate.
 * @param   paPages     The page descriptor table (input + ouput).
 */
static uint32_t gmmR0AllocatePagesFromEmptyChunksOnSameNode(PGMMCHUNKFREESET pSet, PGVM pGVM, RTUID uidSelf,
                                                            uint32_t iPage, uint32_t cPages, PGMMPAGEDESC paPages)
{
    PGMMCHUNK pChunk = pSet->apLists[GMM_CHUNK_FREE_SET_UNUSED_LIST];
    if (pChunk)
    {
        uint16_t const idNumaNode = gmmR0GetCurrentNumaNodeId();
        while (pChunk)
        {
            PGMMCHUNK pNext = pChunk->pFreeNext;

            if (   pChunk->idNumaNode == idNumaNode
                && (   pChunk->uidOwner == uidSelf
                    || pChunk->cMappingsX == 0))
            {
                pChunk->hGVM     = pGVM->hSelf;
                pChunk->uidOwner = uidSelf;
                iPage = gmmR0AllocatePagesFromChunk(pChunk, pGVM->hSelf, iPage, cPages, paPages);
                if (iPage >= cPages)
                {
                    pGVM->gmm.s.idLastChunkHint = pChunk->cFree ? pChunk->Core.Key : NIL_GMM_CHUNKID;
                    return iPage;
                }
            }

            pChunk = pNext;
        }
    }
    return iPage;
}


/**
 * Pick pages from non-empty chunks on the same NUMA node.
 *
 * @returns The new page descriptor table index.
 * @param   pSet        The set to pick from.
 * @param   pGVM        Pointer to the global VM structure.
 * @param   uidSelf     The UID of the caller.
 * @param   iPage       The current page descriptor table index.
 * @param   cPages      The total number of pages to allocate.
 * @param   paPages     The page descriptor table (input + ouput).
 */
static uint32_t gmmR0AllocatePagesFromSameNode(PGMMCHUNKFREESET pSet, PGVM pGVM, RTUID const uidSelf,
                                               uint32_t iPage, uint32_t cPages, PGMMPAGEDESC paPages)
{
    /** @todo start by picking from chunks with about the right size first?  */
    uint16_t const  idNumaNode = gmmR0GetCurrentNumaNodeId();
    unsigned        iList      = GMM_CHUNK_FREE_SET_UNUSED_LIST;
    while (iList-- > 0)
    {
        PGMMCHUNK pChunk = pSet->apLists[iList];
        while (pChunk)
        {
            PGMMCHUNK pNext = pChunk->pFreeNext;

            if (   pChunk->idNumaNode == idNumaNode
                && pChunk->uidOwner   == uidSelf)
            {
                iPage = gmmR0AllocatePagesFromChunk(pChunk, pGVM->hSelf, iPage, cPages, paPages);
                if (iPage >= cPages)
                {
                    pGVM->gmm.s.idLastChunkHint = pChunk->cFree ? pChunk->Core.Key : NIL_GMM_CHUNKID;
                    return iPage;
                }
            }

            pChunk = pNext;
        }
    }
    return iPage;
}


/**
 * Pick pages that are in chunks already associated with the VM.
 *
 * @returns The new page descriptor table index.
 * @param   pGMM        Pointer to the GMM instance data.
 * @param   pGVM        Pointer to the global VM structure.
 * @param   pSet        The set to pick from.
 * @param   iPage       The current page descriptor table index.
 * @param   cPages      The total number of pages to allocate.
 * @param   paPages     The page descriptor table (input + ouput).
 */
static uint32_t gmmR0AllocatePagesAssociatedWithVM(PGMM pGMM, PGVM pGVM, PGMMCHUNKFREESET pSet,
                                                   uint32_t iPage, uint32_t cPages, PGMMPAGEDESC paPages)
{
    uint16_t const hGVM = pGVM->hSelf;

    /* Hint. */
    if (pGVM->gmm.s.idLastChunkHint != NIL_GMM_CHUNKID)
    {
        PGMMCHUNK pChunk = gmmR0GetChunk(pGMM, pGVM->gmm.s.idLastChunkHint);
        if (pChunk && pChunk->cFree)
        {
            iPage = gmmR0AllocatePagesFromChunk(pChunk, hGVM, iPage, cPages, paPages);
            if (iPage >= cPages)
                return iPage;
        }
    }

    /* Scan. */
    for (unsigned iList = 0; iList < RT_ELEMENTS(pSet->apLists); iList++)
    {
        PGMMCHUNK pChunk = pSet->apLists[iList];
        while (pChunk)
        {
            PGMMCHUNK pNext = pChunk->pFreeNext;

            if (pChunk->hGVM == hGVM)
            {
                iPage = gmmR0AllocatePagesFromChunk(pChunk, hGVM, iPage, cPages, paPages);
                if (iPage >= cPages)
                {
                    pGVM->gmm.s.idLastChunkHint = pChunk->cFree ? pChunk->Core.Key : NIL_GMM_CHUNKID;
                    return iPage;
                }
            }

            pChunk = pNext;
        }
    }
    return iPage;
}



/**
 * Pick pages in bound memory mode.
 *
 * @returns The new page descriptor table index.
 * @param   pGVM        Pointer to the global VM structure.
 * @param   iPage       The current page descriptor table index.
 * @param   cPages      The total number of pages to allocate.
 * @param   paPages     The page descriptor table (input + ouput).
 */
static uint32_t gmmR0AllocatePagesInBoundMode(PGVM pGVM, uint32_t iPage, uint32_t cPages, PGMMPAGEDESC paPages)
{
    for (unsigned iList = 0; iList < RT_ELEMENTS(pGVM->gmm.s.Private.apLists); iList++)
    {
        PGMMCHUNK pChunk = pGVM->gmm.s.Private.apLists[iList];
        while (pChunk)
        {
            Assert(pChunk->hGVM == pGVM->hSelf);
            PGMMCHUNK pNext = pChunk->pFreeNext;
            iPage = gmmR0AllocatePagesFromChunk(pChunk, pGVM->hSelf, iPage, cPages, paPages);
            if (iPage >= cPages)
                return iPage;
            pChunk = pNext;
        }
    }
    return iPage;
}


/**
 * Checks if we should start picking pages from chunks of other VMs because
 * we're getting close to the system memory or reserved limit.
 *
 * @returns @c true if we should, @c false if we should first try allocate more
 *          chunks.
 */
static bool gmmR0ShouldAllocatePagesInOtherChunksBecauseOfLimits(PGVM pGVM)
{
    /*
     * Don't allocate a new chunk if we're
     */
    uint64_t cPgReserved  = pGVM->gmm.s.Stats.Reserved.cBasePages
                          + pGVM->gmm.s.Stats.Reserved.cFixedPages
                          - pGVM->gmm.s.Stats.cBalloonedPages
                          /** @todo what about shared pages? */;
    uint64_t cPgAllocated = pGVM->gmm.s.Stats.Allocated.cBasePages
                          + pGVM->gmm.s.Stats.Allocated.cFixedPages;
    uint64_t cPgDelta = cPgReserved - cPgAllocated;
    if (cPgDelta < GMM_CHUNK_NUM_PAGES * 4)
        return true;
    /** @todo make the threshold configurable, also test the code to see if
     *        this ever kicks in (we might be reserving too much or smth). */

    /*
     * Check how close we're to the max memory limit and how many fragments
     * there are?...
     */
    /** @todo  */

    return false;
}


/**
 * Checks if we should start picking pages from chunks of other VMs because
 * there is a lot of free pages around.
 *
 * @returns @c true if we should, @c false if we should first try allocate more
 *          chunks.
 */
static bool gmmR0ShouldAllocatePagesInOtherChunksBecauseOfLotsFree(PGMM pGMM)
{
    /*
     * Setting the limit at 16 chunks (32 MB) at the moment.
     */
    if (pGMM->PrivateX.cFreePages >= GMM_CHUNK_NUM_PAGES * 16)
        return true;
    return false;
}


/**
 * Common worker for GMMR0AllocateHandyPages and GMMR0AllocatePages.
 *
 * @returns VBox status code:
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_GMM_HIT_GLOBAL_LIMIT if we've exhausted the available pages.
 * @retval  VERR_GMM_HIT_VM_ACCOUNT_LIMIT if we've hit the VM account limit,
 *          that is we're trying to allocate more than we've reserved.
 *
 * @param   pGMM        Pointer to the GMM instance data.
 * @param   pGVM        Pointer to the VM.
 * @param   cPages      The number of pages to allocate.
 * @param   paPages     Pointer to the page descriptors. See GMMPAGEDESC for
 *                      details on what is expected on input.
 * @param   enmAccount  The account to charge.
 *
 * @remarks Caller owns the giant GMM lock.
 */
static int gmmR0AllocatePagesNew(PGMM pGMM, PGVM pGVM, uint32_t cPages, PGMMPAGEDESC paPages, GMMACCOUNT enmAccount)
{
    Assert(pGMM->hMtxOwner == RTThreadNativeSelf());

    /*
     * Check allocation limits.
     */
    if (RT_LIKELY(pGMM->cAllocatedPages + cPages <= pGMM->cMaxPages))
    { /* likely */ }
    else
        return VERR_GMM_HIT_GLOBAL_LIMIT;

    switch (enmAccount)
    {
        case GMMACCOUNT_BASE:
            if (RT_LIKELY(   pGVM->gmm.s.Stats.Allocated.cBasePages + pGVM->gmm.s.Stats.cBalloonedPages + cPages
                          <= pGVM->gmm.s.Stats.Reserved.cBasePages))
            { /* likely */ }
            else
            {
                Log(("gmmR0AllocatePages:Base: Reserved=%#llx Allocated+Ballooned+Requested=%#llx+%#llx+%#x!\n",
                     pGVM->gmm.s.Stats.Reserved.cBasePages, pGVM->gmm.s.Stats.Allocated.cBasePages,
                     pGVM->gmm.s.Stats.cBalloonedPages, cPages));
                return VERR_GMM_HIT_VM_ACCOUNT_LIMIT;
            }
            break;
        case GMMACCOUNT_SHADOW:
            if (RT_LIKELY(pGVM->gmm.s.Stats.Allocated.cShadowPages + cPages <= pGVM->gmm.s.Stats.Reserved.cShadowPages))
            { /* likely */ }
            else
            {
                Log(("gmmR0AllocatePages:Shadow: Reserved=%#x Allocated+Requested=%#x+%#x!\n",
                     pGVM->gmm.s.Stats.Reserved.cShadowPages, pGVM->gmm.s.Stats.Allocated.cShadowPages, cPages));
                return VERR_GMM_HIT_VM_ACCOUNT_LIMIT;
            }
            break;
        case GMMACCOUNT_FIXED:
            if (RT_LIKELY(pGVM->gmm.s.Stats.Allocated.cFixedPages + cPages <= pGVM->gmm.s.Stats.Reserved.cFixedPages))
            { /* likely */ }
            else
            {
                Log(("gmmR0AllocatePages:Fixed: Reserved=%#x Allocated+Requested=%#x+%#x!\n",
                     pGVM->gmm.s.Stats.Reserved.cFixedPages, pGVM->gmm.s.Stats.Allocated.cFixedPages, cPages));
                return VERR_GMM_HIT_VM_ACCOUNT_LIMIT;
            }
            break;
        default:
            AssertMsgFailedReturn(("enmAccount=%d\n", enmAccount), VERR_IPE_NOT_REACHED_DEFAULT_CASE);
    }

    /*
     * Update the accounts before we proceed because we might be leaving the
     * protection of the global mutex and thus run the risk of permitting
     * too much memory to be allocated.
     */
    switch (enmAccount)
    {
        case GMMACCOUNT_BASE:   pGVM->gmm.s.Stats.Allocated.cBasePages   += cPages; break;
        case GMMACCOUNT_SHADOW: pGVM->gmm.s.Stats.Allocated.cShadowPages += cPages; break;
        case GMMACCOUNT_FIXED:  pGVM->gmm.s.Stats.Allocated.cFixedPages  += cPages; break;
        default:                AssertMsgFailedReturn(("enmAccount=%d\n", enmAccount), VERR_IPE_NOT_REACHED_DEFAULT_CASE);
    }
    pGVM->gmm.s.Stats.cPrivatePages += cPages;
    pGMM->cAllocatedPages           += cPages;

    /*
     * Bound mode is also relatively straightforward.
     */
    uint32_t iPage = 0;
    int rc = VINF_SUCCESS;
    if (pGMM->fBoundMemoryMode)
    {
        iPage = gmmR0AllocatePagesInBoundMode(pGVM, iPage, cPages, paPages);
        if (iPage < cPages)
            do
                rc = gmmR0AllocateChunkNew(pGMM, pGVM, &pGVM->gmm.s.Private, cPages, paPages, &iPage);
            while (iPage < cPages && RT_SUCCESS(rc));
    }
    /*
     * Shared mode is trickier as we should try archive the same locality as
     * in bound mode, but smartly make use of non-full chunks allocated by
     * other VMs if we're low on memory.
     */
    else
    {
        RTUID const uidSelf = SUPR0GetSessionUid(pGVM->pSession);

        /* Pick the most optimal pages first. */
        iPage = gmmR0AllocatePagesAssociatedWithVM(pGMM, pGVM, &pGMM->PrivateX, iPage, cPages, paPages);
        if (iPage < cPages)
        {
            /* Maybe we should try getting pages from chunks "belonging" to
               other VMs before allocating more chunks? */
            bool fTriedOnSameAlready = false;
            if (gmmR0ShouldAllocatePagesInOtherChunksBecauseOfLimits(pGVM))
            {
                iPage = gmmR0AllocatePagesFromSameNode(&pGMM->PrivateX, pGVM, uidSelf, iPage, cPages, paPages);
                fTriedOnSameAlready = true;
            }

            /* Allocate memory from empty chunks. */
            if (iPage < cPages)
                iPage = gmmR0AllocatePagesFromEmptyChunksOnSameNode(&pGMM->PrivateX, pGVM, uidSelf, iPage, cPages, paPages);

            /* Grab empty shared chunks. */
            if (iPage < cPages)
                iPage = gmmR0AllocatePagesFromEmptyChunksOnSameNode(&pGMM->Shared, pGVM, uidSelf, iPage, cPages, paPages);

            /* If there is a lof of free pages spread around, try not waste
               system memory on more chunks. (Should trigger defragmentation.) */
            if (   !fTriedOnSameAlready
                && gmmR0ShouldAllocatePagesInOtherChunksBecauseOfLotsFree(pGMM))
            {
                iPage = gmmR0AllocatePagesFromSameNode(&pGMM->PrivateX, pGVM, uidSelf, iPage, cPages, paPages);
                if (iPage < cPages)
                    iPage = gmmR0AllocatePagesIndiscriminately(&pGMM->PrivateX, pGVM, uidSelf, iPage, cPages, paPages);
            }

            /*
             * Ok, try allocate new chunks.
             */
            if (iPage < cPages)
            {
                do
                    rc = gmmR0AllocateChunkNew(pGMM, pGVM, &pGMM->PrivateX, cPages, paPages, &iPage);
                while (iPage < cPages && RT_SUCCESS(rc));

#if 0 /* We cannot mix chunks with different UIDs. */
                /* If the host is out of memory, take whatever we can get. */
                if (   (rc == VERR_NO_MEMORY || rc == VERR_NO_PHYS_MEMORY)
                    && pGMM->PrivateX.cFreePages + pGMM->Shared.cFreePages >= cPages - iPage)
                {
                    iPage = gmmR0AllocatePagesIndiscriminately(&pGMM->PrivateX, pGVM, iPage, cPages, paPages);
                    if (iPage < cPages)
                        iPage = gmmR0AllocatePagesIndiscriminately(&pGMM->Shared, pGVM, iPage, cPages, paPages);
                    AssertRelease(iPage == cPages);
                    rc = VINF_SUCCESS;
                }
#endif
            }
        }
    }

    /*
     * Clean up on failure.  Since this is bound to be a low-memory condition
     * we will give back any empty chunks that might be hanging around.
     */
    if (RT_SUCCESS(rc))
    { /* likely */ }
    else
    {
        /* Update the statistics. */
        pGVM->gmm.s.Stats.cPrivatePages -= cPages;
        pGMM->cAllocatedPages           -= cPages - iPage;
        switch (enmAccount)
        {
            case GMMACCOUNT_BASE:   pGVM->gmm.s.Stats.Allocated.cBasePages   -= cPages; break;
            case GMMACCOUNT_SHADOW: pGVM->gmm.s.Stats.Allocated.cShadowPages -= cPages; break;
            case GMMACCOUNT_FIXED:  pGVM->gmm.s.Stats.Allocated.cFixedPages  -= cPages; break;
            default:                AssertMsgFailedReturn(("enmAccount=%d\n", enmAccount), VERR_IPE_NOT_REACHED_DEFAULT_CASE);
        }

        /* Release the pages. */
        while (iPage-- > 0)
        {
            uint32_t idPage = paPages[iPage].idPage;
            PGMMPAGE pPage = gmmR0GetPage(pGMM, idPage);
            if (RT_LIKELY(pPage))
            {
                Assert(GMM_PAGE_IS_PRIVATE(pPage));
                Assert(pPage->Private.hGVM == pGVM->hSelf);
                gmmR0FreePrivatePage(pGMM, pGVM, idPage, pPage);
            }
            else
                AssertMsgFailed(("idPage=%#x\n", idPage));

            paPages[iPage].idPage       = NIL_GMM_PAGEID;
            paPages[iPage].idSharedPage = NIL_GMM_PAGEID;
            paPages[iPage].HCPhysGCPhys = NIL_GMMPAGEDESC_PHYS;
            paPages[iPage].fZeroed      = false;
        }

        /* Free empty chunks. */
        /** @todo  */

        /* return the fail status on failure */
        return rc;
    }
    return VINF_SUCCESS;
}


/**
 * Updates the previous allocations and allocates more pages.
 *
 * The handy pages are always taken from the 'base' memory account.
 * The allocated pages are not cleared and will contains random garbage.
 *
 * @returns VBox status code:
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_NOT_OWNER if the caller is not an EMT.
 * @retval  VERR_GMM_PAGE_NOT_FOUND if one of the pages to update wasn't found.
 * @retval  VERR_GMM_PAGE_NOT_PRIVATE if one of the pages to update wasn't a
 *          private page.
 * @retval  VERR_GMM_PAGE_NOT_SHARED if one of the pages to update wasn't a
 *          shared page.
 * @retval  VERR_GMM_NOT_PAGE_OWNER if one of the pages to be updated wasn't
 *          owned by the VM.
 * @retval  VERR_GMM_HIT_GLOBAL_LIMIT if we've exhausted the available pages.
 * @retval  VERR_GMM_HIT_VM_ACCOUNT_LIMIT if we've hit the VM account limit,
 *          that is we're trying to allocate more than we've reserved.
 *
 * @param   pGVM                The global (ring-0) VM structure.
 * @param   idCpu               The VCPU id.
 * @param   cPagesToUpdate      The number of pages to update (starting from the head).
 * @param   cPagesToAlloc       The number of pages to allocate (starting from the head).
 * @param   paPages             The array of page descriptors.
 *                              See GMMPAGEDESC for details on what is expected on input.
 * @thread  EMT(idCpu)
 */
GMMR0DECL(int) GMMR0AllocateHandyPages(PGVM pGVM, VMCPUID idCpu, uint32_t cPagesToUpdate,
                                       uint32_t cPagesToAlloc, PGMMPAGEDESC paPages)
{
    LogFlow(("GMMR0AllocateHandyPages: pGVM=%p cPagesToUpdate=%#x cPagesToAlloc=%#x paPages=%p\n",
             pGVM, cPagesToUpdate, cPagesToAlloc, paPages));

    /*
     * Validate & get basics.
     * (This is a relatively busy path, so make predictions where possible.)
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_GMM_INSTANCE);
    int rc = GVMMR0ValidateGVMandEMT(pGVM, idCpu);
    if (RT_FAILURE(rc))
        return rc;

    AssertPtrReturn(paPages, VERR_INVALID_PARAMETER);
    AssertMsgReturn(    (cPagesToUpdate && cPagesToUpdate < 1024)
                    ||  (cPagesToAlloc  && cPagesToAlloc < 1024),
                    ("cPagesToUpdate=%#x cPagesToAlloc=%#x\n", cPagesToUpdate, cPagesToAlloc),
                    VERR_INVALID_PARAMETER);

    unsigned iPage = 0;
    for (; iPage < cPagesToUpdate; iPage++)
    {
        AssertMsgReturn(    (    paPages[iPage].HCPhysGCPhys <= GMM_GCPHYS_LAST
                             && !(paPages[iPage].HCPhysGCPhys & GUEST_PAGE_OFFSET_MASK))
                        ||  paPages[iPage].HCPhysGCPhys == NIL_GMMPAGEDESC_PHYS
                        ||  paPages[iPage].HCPhysGCPhys == GMM_GCPHYS_UNSHAREABLE,
                        ("#%#x: %RHp\n", iPage, paPages[iPage].HCPhysGCPhys),
                        VERR_INVALID_PARAMETER);
        /* ignore fZeroed here */
        AssertMsgReturn(    paPages[iPage].idPage <= GMM_PAGEID_LAST
                        /*||  paPages[iPage].idPage == NIL_GMM_PAGEID*/,
                        ("#%#x: %#x\n", iPage, paPages[iPage].idPage), VERR_INVALID_PARAMETER);
        AssertMsgReturn(   paPages[iPage].idSharedPage == NIL_GMM_PAGEID
                        || paPages[iPage].idSharedPage <= GMM_PAGEID_LAST,
                        ("#%#x: %#x\n", iPage, paPages[iPage].idSharedPage), VERR_INVALID_PARAMETER);
    }

    for (; iPage < cPagesToAlloc; iPage++)
    {
        AssertMsgReturn(paPages[iPage].HCPhysGCPhys == NIL_GMMPAGEDESC_PHYS, ("#%#x: %RHp\n", iPage, paPages[iPage].HCPhysGCPhys), VERR_INVALID_PARAMETER);
        AssertMsgReturn(paPages[iPage].fZeroed      == false,          ("#%#x: %#x\n", iPage, paPages[iPage].fZeroed),       VERR_INVALID_PARAMETER);
        AssertMsgReturn(paPages[iPage].idPage       == NIL_GMM_PAGEID, ("#%#x: %#x\n", iPage, paPages[iPage].idPage),        VERR_INVALID_PARAMETER);
        AssertMsgReturn(paPages[iPage].idSharedPage == NIL_GMM_PAGEID, ("#%#x: %#x\n", iPage, paPages[iPage].idSharedPage),  VERR_INVALID_PARAMETER);
    }

    /*
     * Take the semaphore
     */
    VMMR0EMTBLOCKCTX Ctx;
    PGVMCPU          pGVCpu = &pGVM->aCpus[idCpu];
    rc = VMMR0EmtPrepareToBlock(pGVCpu, VINF_SUCCESS, "GMMR0AllocateHandyPages", pGMM, &Ctx);
    AssertRCReturn(rc, rc);

    rc = gmmR0MutexAcquire(pGMM);
    if (   RT_SUCCESS(rc)
        && GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {
        /* No allocations before the initial reservation has been made! */
        if (RT_LIKELY(    pGVM->gmm.s.Stats.Reserved.cBasePages
                      &&  pGVM->gmm.s.Stats.Reserved.cFixedPages
                      &&  pGVM->gmm.s.Stats.Reserved.cShadowPages))
        {
            /*
             * Perform the updates.
             * Stop on the first error.
             */
            for (iPage = 0; iPage < cPagesToUpdate; iPage++)
            {
                if (paPages[iPage].idPage != NIL_GMM_PAGEID)
                {
                    PGMMPAGE pPage = gmmR0GetPage(pGMM, paPages[iPage].idPage);
                    if (RT_LIKELY(pPage))
                    {
                        if (RT_LIKELY(GMM_PAGE_IS_PRIVATE(pPage)))
                        {
                            if (RT_LIKELY(pPage->Private.hGVM == pGVM->hSelf))
                            {
                                AssertCompile(NIL_RTHCPHYS > GMM_GCPHYS_LAST && GMM_GCPHYS_UNSHAREABLE > GMM_GCPHYS_LAST);
                                if (RT_LIKELY(paPages[iPage].HCPhysGCPhys <= GMM_GCPHYS_LAST))
                                    pPage->Private.pfn = paPages[iPage].HCPhysGCPhys >> GUEST_PAGE_SHIFT;
                                else if (paPages[iPage].HCPhysGCPhys == GMM_GCPHYS_UNSHAREABLE)
                                    pPage->Private.pfn = GMM_PAGE_PFN_UNSHAREABLE;
                                /* else: NIL_RTHCPHYS nothing */

                                paPages[iPage].idPage       = NIL_GMM_PAGEID;
                                paPages[iPage].HCPhysGCPhys = NIL_GMMPAGEDESC_PHYS;
                                paPages[iPage].fZeroed      = false;
                            }
                            else
                            {
                                Log(("GMMR0AllocateHandyPages: #%#x/%#x: Not owner! hGVM=%#x hSelf=%#x\n",
                                     iPage, paPages[iPage].idPage, pPage->Private.hGVM, pGVM->hSelf));
                                rc = VERR_GMM_NOT_PAGE_OWNER;
                                break;
                            }
                        }
                        else
                        {
                            Log(("GMMR0AllocateHandyPages: #%#x/%#x: Not private! %.*Rhxs (type %d)\n", iPage, paPages[iPage].idPage, sizeof(*pPage), pPage, pPage->Common.u2State));
                            rc = VERR_GMM_PAGE_NOT_PRIVATE;
                            break;
                        }
                    }
                    else
                    {
                        Log(("GMMR0AllocateHandyPages: #%#x/%#x: Not found! (private)\n", iPage, paPages[iPage].idPage));
                        rc = VERR_GMM_PAGE_NOT_FOUND;
                        break;
                    }
                }

                if (paPages[iPage].idSharedPage == NIL_GMM_PAGEID)
                { /* likely */ }
                else
                {
                    PGMMPAGE pPage = gmmR0GetPage(pGMM, paPages[iPage].idSharedPage);
                    if (RT_LIKELY(pPage))
                    {
                        if (RT_LIKELY(GMM_PAGE_IS_SHARED(pPage)))
                        {
                            AssertCompile(NIL_RTHCPHYS > GMM_GCPHYS_LAST && GMM_GCPHYS_UNSHAREABLE > GMM_GCPHYS_LAST);
                            Assert(pPage->Shared.cRefs);
                            Assert(pGVM->gmm.s.Stats.cSharedPages);
                            Assert(pGVM->gmm.s.Stats.Allocated.cBasePages);

                            Log(("GMMR0AllocateHandyPages: free shared page %x cRefs=%d\n", paPages[iPage].idSharedPage, pPage->Shared.cRefs));
                            pGVM->gmm.s.Stats.cSharedPages--;
                            pGVM->gmm.s.Stats.Allocated.cBasePages--;
                            if (!--pPage->Shared.cRefs)
                                gmmR0FreeSharedPage(pGMM, pGVM, paPages[iPage].idSharedPage, pPage);
                            else
                            {
                                Assert(pGMM->cDuplicatePages);
                                pGMM->cDuplicatePages--;
                            }

                            paPages[iPage].idSharedPage = NIL_GMM_PAGEID;
                        }
                        else
                        {
                            Log(("GMMR0AllocateHandyPages: #%#x/%#x: Not shared!\n", iPage, paPages[iPage].idSharedPage));
                            rc = VERR_GMM_PAGE_NOT_SHARED;
                            break;
                        }
                    }
                    else
                    {
                        Log(("GMMR0AllocateHandyPages: #%#x/%#x: Not found! (shared)\n", iPage, paPages[iPage].idSharedPage));
                        rc = VERR_GMM_PAGE_NOT_FOUND;
                        break;
                    }
                }
            } /* for each page to update */

            if (RT_SUCCESS(rc) && cPagesToAlloc > 0)
            {
#ifdef VBOX_STRICT
                for (iPage = 0; iPage < cPagesToAlloc; iPage++)
                {
                    Assert(paPages[iPage].HCPhysGCPhys  == NIL_GMMPAGEDESC_PHYS);
                    Assert(paPages[iPage].fZeroed       == false);
                    Assert(paPages[iPage].idPage        == NIL_GMM_PAGEID);
                    Assert(paPages[iPage].idSharedPage  == NIL_GMM_PAGEID);
                }
#endif

                /*
                 * Join paths with GMMR0AllocatePages for the allocation.
                 * Note! gmmR0AllocateMoreChunks may leave the protection of the mutex!
                 */
                rc = gmmR0AllocatePagesNew(pGMM, pGVM, cPagesToAlloc, paPages, GMMACCOUNT_BASE);
            }
        }
        else
            rc = VERR_WRONG_ORDER;
        GMM_CHECK_SANITY_UPON_LEAVING(pGMM);
        gmmR0MutexRelease(pGMM);
    }
    else if (RT_SUCCESS(rc))
    {
        gmmR0MutexRelease(pGMM);
        rc = VERR_GMM_IS_NOT_SANE;
    }
    VMMR0EmtResumeAfterBlocking(pGVCpu, &Ctx);

    LogFlow(("GMMR0AllocateHandyPages: returns %Rrc\n", rc));
    return rc;
}


/**
 * Allocate one or more pages.
 *
 * This is typically used for ROMs and MMIO2 (VRAM) during VM creation.
 * The allocated pages are not cleared and will contain random garbage.
 *
 * @returns VBox status code:
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_NOT_OWNER if the caller is not an EMT.
 * @retval  VERR_GMM_HIT_GLOBAL_LIMIT if we've exhausted the available pages.
 * @retval  VERR_GMM_HIT_VM_ACCOUNT_LIMIT if we've hit the VM account limit,
 *          that is we're trying to allocate more than we've reserved.
 *
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   idCpu       The VCPU id.
 * @param   cPages      The number of pages to allocate.
 * @param   paPages     Pointer to the page descriptors.
 *                      See GMMPAGEDESC for details on what is expected on
 *                      input.
 * @param   enmAccount  The account to charge.
 *
 * @thread  EMT.
 */
GMMR0DECL(int) GMMR0AllocatePages(PGVM pGVM, VMCPUID idCpu, uint32_t cPages, PGMMPAGEDESC paPages, GMMACCOUNT enmAccount)
{
    LogFlow(("GMMR0AllocatePages: pGVM=%p cPages=%#x paPages=%p enmAccount=%d\n", pGVM, cPages, paPages, enmAccount));

    /*
     * Validate, get basics and take the semaphore.
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_GMM_INSTANCE);
    int rc = GVMMR0ValidateGVMandEMT(pGVM, idCpu);
    if (RT_FAILURE(rc))
        return rc;

    AssertPtrReturn(paPages, VERR_INVALID_PARAMETER);
    AssertMsgReturn(enmAccount > GMMACCOUNT_INVALID && enmAccount < GMMACCOUNT_END, ("%d\n", enmAccount), VERR_INVALID_PARAMETER);
    AssertMsgReturn(cPages > 0 && cPages < RT_BIT(32 - GUEST_PAGE_SHIFT), ("%#x\n", cPages), VERR_INVALID_PARAMETER);

    for (unsigned iPage = 0; iPage < cPages; iPage++)
    {
        AssertMsgReturn(    paPages[iPage].HCPhysGCPhys == NIL_GMMPAGEDESC_PHYS
                        ||  paPages[iPage].HCPhysGCPhys == GMM_GCPHYS_UNSHAREABLE
                        ||  (    enmAccount == GMMACCOUNT_BASE
                             &&  paPages[iPage].HCPhysGCPhys <= GMM_GCPHYS_LAST
                             && !(paPages[iPage].HCPhysGCPhys & GUEST_PAGE_OFFSET_MASK)),
                        ("#%#x: %RHp enmAccount=%d\n", iPage, paPages[iPage].HCPhysGCPhys, enmAccount),
                        VERR_INVALID_PARAMETER);
        AssertMsgReturn(paPages[iPage].fZeroed      == false,          ("#%#x: %#x\n", iPage, paPages[iPage].fZeroed), VERR_INVALID_PARAMETER);
        AssertMsgReturn(paPages[iPage].idPage       == NIL_GMM_PAGEID, ("#%#x: %#x\n", iPage, paPages[iPage].idPage), VERR_INVALID_PARAMETER);
        AssertMsgReturn(paPages[iPage].idSharedPage == NIL_GMM_PAGEID, ("#%#x: %#x\n", iPage, paPages[iPage].idSharedPage), VERR_INVALID_PARAMETER);
    }

    /*
     * Grab the giant mutex and get working.
     */
    gmmR0MutexAcquire(pGMM);
    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {

        /* No allocations before the initial reservation has been made! */
        if (RT_LIKELY(    pGVM->gmm.s.Stats.Reserved.cBasePages
                      &&  pGVM->gmm.s.Stats.Reserved.cFixedPages
                      &&  pGVM->gmm.s.Stats.Reserved.cShadowPages))
            rc = gmmR0AllocatePagesNew(pGMM, pGVM, cPages, paPages, enmAccount);
        else
            rc = VERR_WRONG_ORDER;
        GMM_CHECK_SANITY_UPON_LEAVING(pGMM);
    }
    else
        rc = VERR_GMM_IS_NOT_SANE;
    gmmR0MutexRelease(pGMM);

    LogFlow(("GMMR0AllocatePages: returns %Rrc\n", rc));
    return rc;
}


/**
 * VMMR0 request wrapper for GMMR0AllocatePages.
 *
 * @returns see GMMR0AllocatePages.
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   idCpu       The VCPU id.
 * @param   pReq        Pointer to the request packet.
 */
GMMR0DECL(int) GMMR0AllocatePagesReq(PGVM pGVM, VMCPUID idCpu, PGMMALLOCATEPAGESREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq >= RT_UOFFSETOF(GMMALLOCATEPAGESREQ, aPages[0]),
                    ("%#x < %#x\n", pReq->Hdr.cbReq, RT_UOFFSETOF(GMMALLOCATEPAGESREQ, aPages[0])),
                    VERR_INVALID_PARAMETER);
    AssertMsgReturn(pReq->Hdr.cbReq == RT_UOFFSETOF_DYN(GMMALLOCATEPAGESREQ, aPages[pReq->cPages]),
                    ("%#x != %#x\n", pReq->Hdr.cbReq, RT_UOFFSETOF_DYN(GMMALLOCATEPAGESREQ, aPages[pReq->cPages])),
                    VERR_INVALID_PARAMETER);

    return GMMR0AllocatePages(pGVM, idCpu, pReq->cPages, &pReq->aPages[0], pReq->enmAccount);
}


/**
 * Allocate a large page to represent guest RAM
 *
 * The allocated pages are zeroed upon return.
 *
 * @returns VBox status code:
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_NOT_OWNER if the caller is not an EMT.
 * @retval  VERR_GMM_HIT_GLOBAL_LIMIT if we've exhausted the available pages.
 * @retval  VERR_GMM_HIT_VM_ACCOUNT_LIMIT if we've hit the VM account limit,
 *          that is we're trying to allocate more than we've reserved.
 * @retval  VERR_TRY_AGAIN if the host is temporarily out of large pages.
 * @returns see GMMR0AllocatePages.
 *
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   idCpu       The VCPU id.
 * @param   cbPage      Large page size.
 * @param   pIdPage     Where to return the GMM page ID of the page.
 * @param   pHCPhys     Where to return the host physical address of the page.
 */
GMMR0DECL(int)  GMMR0AllocateLargePage(PGVM pGVM, VMCPUID idCpu, uint32_t cbPage, uint32_t *pIdPage, RTHCPHYS *pHCPhys)
{
    LogFlow(("GMMR0AllocateLargePage: pGVM=%p cbPage=%x\n", pGVM, cbPage));

    AssertPtrReturn(pIdPage, VERR_INVALID_PARAMETER);
    *pIdPage = NIL_GMM_PAGEID;
    AssertPtrReturn(pHCPhys, VERR_INVALID_PARAMETER);
    *pHCPhys = NIL_RTHCPHYS;
    AssertReturn(cbPage == GMM_CHUNK_SIZE, VERR_INVALID_PARAMETER);

    /*
     * Validate GVM + idCpu, get basics and take the semaphore.
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_GMM_INSTANCE);
    int rc = GVMMR0ValidateGVMandEMT(pGVM, idCpu);
    AssertRCReturn(rc, rc);

    VMMR0EMTBLOCKCTX Ctx;
    PGVMCPU          pGVCpu = &pGVM->aCpus[idCpu];
    rc = VMMR0EmtPrepareToBlock(pGVCpu, VINF_SUCCESS, "GMMR0AllocateLargePage", pGMM, &Ctx);
    AssertRCReturn(rc, rc);

    rc = gmmR0MutexAcquire(pGMM);
    if (RT_SUCCESS(rc))
    {
        if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
        {
            /*
             * Check the quota.
             */
            /** @todo r=bird: Quota checking could be done w/o the giant mutex but using
             *        a VM specific mutex... */
            if (RT_LIKELY(   pGVM->gmm.s.Stats.Allocated.cBasePages + pGVM->gmm.s.Stats.cBalloonedPages + GMM_CHUNK_NUM_PAGES
                          <= pGVM->gmm.s.Stats.Reserved.cBasePages))
            {
                /*
                 * Allocate a new large page chunk.
                 *
                 * Note! We leave the giant GMM lock temporarily as the allocation might
                 *       take a long time.  gmmR0RegisterChunk will retake it (ugly).
                 */
                AssertCompile(GMM_CHUNK_SIZE == _2M);
                gmmR0MutexRelease(pGMM);

                RTR0MEMOBJ hMemObj;
                rc = RTR0MemObjAllocLarge(&hMemObj, GMM_CHUNK_SIZE, GMM_CHUNK_SIZE, RTMEMOBJ_ALLOC_LARGE_F_FAST);
                if (RT_SUCCESS(rc))
                {
                    *pHCPhys = RTR0MemObjGetPagePhysAddr(hMemObj, 0);

                    /*
                     * Register the chunk as fully allocated.
                     * Note! As mentioned above, this will return owning the mutex on success.
                     */
                    PGMMCHUNK              pChunk = NULL;
                    PGMMCHUNKFREESET const pSet   = pGMM->fBoundMemoryMode ? &pGVM->gmm.s.Private : &pGMM->PrivateX;
                    rc = gmmR0RegisterChunk(pGMM, pSet, hMemObj, pGVM->hSelf, pGVM->pSession, GMM_CHUNK_FLAGS_LARGE_PAGE,
                                            0 /*cPages*/, NULL /*paPages*/, NULL /*piPage*/, &pChunk);
                    if (RT_SUCCESS(rc))
                    {
                        /*
                         * The gmmR0RegisterChunk call already marked all pages allocated,
                         * so we just have to fill in the return values and update stats now.
                         */
                        *pIdPage = pChunk->Core.Key << GMM_CHUNKID_SHIFT;

                        /* Update accounting. */
                        pGVM->gmm.s.Stats.Allocated.cBasePages += GMM_CHUNK_NUM_PAGES;
                        pGVM->gmm.s.Stats.cPrivatePages        += GMM_CHUNK_NUM_PAGES;
                        pGMM->cAllocatedPages                  += GMM_CHUNK_NUM_PAGES;

                        gmmR0LinkChunk(pChunk, pSet);
                        gmmR0MutexRelease(pGMM);

                        VMMR0EmtResumeAfterBlocking(pGVCpu, &Ctx);
                        LogFlow(("GMMR0AllocateLargePage: returns VINF_SUCCESS\n"));
                        return VINF_SUCCESS;
                    }

                    /*
                     * Bail out.
                     */
                    RTR0MemObjFree(hMemObj, true /* fFreeMappings */);
                    *pHCPhys = NIL_RTHCPHYS;
                }
                /** @todo r=bird: Turn VERR_NO_MEMORY etc into VERR_TRY_AGAIN?  Docs say we
                 *        return it, but I am sure IPRT doesn't... */
            }
            else
            {
                Log(("GMMR0AllocateLargePage: Reserved=%#llx Allocated+Requested=%#llx+%#x!\n",
                     pGVM->gmm.s.Stats.Reserved.cBasePages, pGVM->gmm.s.Stats.Allocated.cBasePages, GMM_CHUNK_NUM_PAGES));
                gmmR0MutexRelease(pGMM);
                rc = VERR_GMM_HIT_VM_ACCOUNT_LIMIT;
            }
        }
        else
        {
            gmmR0MutexRelease(pGMM);
            rc = VERR_GMM_IS_NOT_SANE;
        }
    }

    VMMR0EmtResumeAfterBlocking(pGVCpu, &Ctx);
    LogFlow(("GMMR0AllocateLargePage: returns %Rrc\n", rc));
    return rc;
}


/**
 * Free a large page.
 *
 * @returns VBox status code:
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   idCpu       The VCPU id.
 * @param   idPage      The large page id.
 */
GMMR0DECL(int)  GMMR0FreeLargePage(PGVM pGVM, VMCPUID idCpu, uint32_t idPage)
{
    LogFlow(("GMMR0FreeLargePage: pGVM=%p idPage=%x\n", pGVM, idPage));

    /*
     * Validate, get basics and take the semaphore.
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_GMM_INSTANCE);
    int rc = GVMMR0ValidateGVMandEMT(pGVM, idCpu);
    if (RT_FAILURE(rc))
        return rc;

    gmmR0MutexAcquire(pGMM);
    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {
        const unsigned cPages = GMM_CHUNK_NUM_PAGES;

        if (RT_UNLIKELY(pGVM->gmm.s.Stats.Allocated.cBasePages < cPages))
        {
            Log(("GMMR0FreeLargePage: allocated=%#llx cPages=%#x!\n", pGVM->gmm.s.Stats.Allocated.cBasePages, cPages));
            gmmR0MutexRelease(pGMM);
            return VERR_GMM_ATTEMPT_TO_FREE_TOO_MUCH;
        }

        PGMMPAGE pPage = gmmR0GetPage(pGMM, idPage);
        if (RT_LIKELY(   pPage
                      && GMM_PAGE_IS_PRIVATE(pPage)))
        {
            PGMMCHUNK pChunk = gmmR0GetChunk(pGMM, idPage >> GMM_CHUNKID_SHIFT);
            Assert(pChunk);
            Assert(pChunk->cFree < GMM_CHUNK_NUM_PAGES);
            Assert(pChunk->cPrivate > 0);

            /* Release the memory immediately. */
            gmmR0FreeChunk(pGMM, NULL, pChunk, false /*fRelaxedSem*/); /** @todo this can be relaxed too! */

            /* Update accounting. */
            pGVM->gmm.s.Stats.Allocated.cBasePages -= cPages;
            pGVM->gmm.s.Stats.cPrivatePages        -= cPages;
            pGMM->cAllocatedPages                  -= cPages;
        }
        else
            rc = VERR_GMM_PAGE_NOT_FOUND;
    }
    else
        rc = VERR_GMM_IS_NOT_SANE;

    gmmR0MutexRelease(pGMM);
    LogFlow(("GMMR0FreeLargePage: returns %Rrc\n", rc));
    return rc;
}


/**
 * VMMR0 request wrapper for GMMR0FreeLargePage.
 *
 * @returns see GMMR0FreeLargePage.
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   idCpu       The VCPU id.
 * @param   pReq        Pointer to the request packet.
 */
GMMR0DECL(int) GMMR0FreeLargePageReq(PGVM pGVM, VMCPUID idCpu, PGMMFREELARGEPAGEREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(GMMFREEPAGESREQ),
                    ("%#x != %#x\n", pReq->Hdr.cbReq, sizeof(GMMFREEPAGESREQ)),
                    VERR_INVALID_PARAMETER);

    return GMMR0FreeLargePage(pGVM, idCpu, pReq->idPage);
}


/**
 * @callback_method_impl{FNGVMMR0ENUMCALLBACK,
 * Used by gmmR0FreeChunkFlushPerVmTlbs().}
 */
static DECLCALLBACK(int) gmmR0InvalidatePerVmChunkTlbCallback(PGVM pGVM, void *pvUser)
{
    RT_NOREF(pvUser);
    if (pGVM->gmm.s.hChunkTlbSpinLock != NIL_RTSPINLOCK)
    {
        RTSpinlockAcquire(pGVM->gmm.s.hChunkTlbSpinLock);
        uintptr_t i = RT_ELEMENTS(pGVM->gmm.s.aChunkTlbEntries);
        while (i-- > 0)
        {
            pGVM->gmm.s.aChunkTlbEntries[i].idGeneration = UINT64_MAX;
            pGVM->gmm.s.aChunkTlbEntries[i].pChunk       = NULL;
        }
        RTSpinlockRelease(pGVM->gmm.s.hChunkTlbSpinLock);
    }
    return VINF_SUCCESS;
}


/**
 * Called by gmmR0FreeChunk when we reach the threshold for wrapping around the
 * free generation ID value.
 *
 * This is done at 2^62 - 1, which allows us to drop all locks and as it will
 * take a while before 12 exa (2 305 843 009 213 693 952) calls to
 * gmmR0FreeChunk can be made and causes a real wrap-around.  We do two
 * invalidation passes and resets the generation ID between then.  This will
 * make sure there are no false positives.
 *
 * @param   pGMM        Pointer to the GMM instance.
 */
static void gmmR0FreeChunkFlushPerVmTlbs(PGMM pGMM)
{
    /*
     * First invalidation pass.
     */
    int rc = GVMMR0EnumVMs(gmmR0InvalidatePerVmChunkTlbCallback, NULL);
    AssertRCSuccess(rc);

    /*
     * Reset the generation number.
     */
    RTSpinlockAcquire(pGMM->hSpinLockTree);
    ASMAtomicWriteU64(&pGMM->idFreeGeneration, 1);
    RTSpinlockRelease(pGMM->hSpinLockTree);

    /*
     * Second invalidation pass.
     */
    rc = GVMMR0EnumVMs(gmmR0InvalidatePerVmChunkTlbCallback, NULL);
    AssertRCSuccess(rc);
}


/**
 * Frees a chunk, giving it back to the host OS.
 *
 * @param   pGMM        Pointer to the GMM instance.
 * @param   pGVM        This is set when called from GMMR0CleanupVM so we can
 *                      unmap and free the chunk in one go.
 * @param   pChunk      The chunk to free.
 * @param   fRelaxedSem Whether we can release the semaphore while doing the
 *                      freeing (@c true) or not.
 */
static bool gmmR0FreeChunk(PGMM pGMM, PGVM pGVM, PGMMCHUNK pChunk, bool fRelaxedSem)
{
    Assert(pChunk->Core.Key != NIL_GMM_CHUNKID);

    GMMR0CHUNKMTXSTATE MtxState;
    gmmR0ChunkMutexAcquire(&MtxState, pGMM, pChunk, GMMR0CHUNK_MTX_KEEP_GIANT);

    /*
     * Cleanup hack! Unmap the chunk from the callers address space.
     * This shouldn't happen, so screw lock contention...
     */
    if (pChunk->cMappingsX && pGVM)
        gmmR0UnmapChunkLocked(pGMM, pGVM, pChunk);

    /*
     * If there are current mappings of the chunk, then request the
     * VMs to unmap them. Reposition the chunk in the free list so
     * it won't be a likely candidate for allocations.
     */
    if (pChunk->cMappingsX)
    {
        /** @todo R0 -> VM request */
        /* The chunk can be mapped by more than one VM if fBoundMemoryMode is false! */
        Log(("gmmR0FreeChunk: chunk still has %d mappings; don't free!\n", pChunk->cMappingsX));
        gmmR0ChunkMutexRelease(&MtxState, pChunk);
        return false;
    }


    /*
     * Save and trash the handle.
     */
    RTR0MEMOBJ const hMemObj = pChunk->hMemObj;
    pChunk->hMemObj = NIL_RTR0MEMOBJ;

    /*
     * Unlink it from everywhere.
     */
    gmmR0UnlinkChunk(pChunk);

    RTSpinlockAcquire(pGMM->hSpinLockTree);

    RTListNodeRemove(&pChunk->ListNode);

    PAVLU32NODECORE pCore = RTAvlU32Remove(&pGMM->pChunks, pChunk->Core.Key);
    Assert(pCore == &pChunk->Core); NOREF(pCore);

    PGMMCHUNKTLBE pTlbe = &pGMM->ChunkTLB.aEntries[GMM_CHUNKTLB_IDX(pChunk->Core.Key)];
    if (pTlbe->pChunk == pChunk)
    {
        pTlbe->idChunk = NIL_GMM_CHUNKID;
        pTlbe->pChunk = NULL;
    }

    Assert(pGMM->cChunks > 0);
    pGMM->cChunks--;

    uint64_t const idFreeGeneration = ASMAtomicIncU64(&pGMM->idFreeGeneration);

    RTSpinlockRelease(pGMM->hSpinLockTree);

    pGMM->cFreedChunks++;

    /* Drop the lock. */
    gmmR0ChunkMutexRelease(&MtxState, NULL);
    if (fRelaxedSem)
        gmmR0MutexRelease(pGMM);

    /*
     * Flush per VM chunk TLBs if we're getting remotely close to a generation wraparound.
     */
    if (idFreeGeneration == UINT64_MAX / 4)
        gmmR0FreeChunkFlushPerVmTlbs(pGMM);

    /*
     * Free the Chunk ID and all memory associated with the chunk.
     */
    gmmR0FreeChunkId(pGMM, pChunk->Core.Key);
    pChunk->Core.Key = NIL_GMM_CHUNKID;

    RTMemFree(pChunk->paMappingsX);
    pChunk->paMappingsX = NULL;

    RTMemFree(pChunk);

#ifndef VBOX_WITH_LINEAR_HOST_PHYS_MEM
    int rc = RTR0MemObjFree(hMemObj, true /* fFreeMappings */);
#else
    int rc = RTR0MemObjFree(hMemObj, false /* fFreeMappings */);
#endif
    AssertLogRelRC(rc);

    if (fRelaxedSem)
        gmmR0MutexAcquire(pGMM);
    return fRelaxedSem;
}


/**
 * Free page worker.
 *
 * The caller does all the statistic decrementing, we do all the incrementing.
 *
 * @param   pGMM        Pointer to the GMM instance data.
 * @param   pGVM        Pointer to the GVM instance.
 * @param   pChunk      Pointer to the chunk this page belongs to.
 * @param   idPage      The Page ID.
 * @param   pPage       Pointer to the page.
 */
static void gmmR0FreePageWorker(PGMM pGMM, PGVM pGVM, PGMMCHUNK pChunk, uint32_t idPage, PGMMPAGE pPage)
{
    Log3(("F pPage=%p iPage=%#x/%#x u2State=%d iFreeHead=%#x\n",
          pPage, pPage - &pChunk->aPages[0], idPage, pPage->Common.u2State, pChunk->iFreeHead)); NOREF(idPage);

    /*
     * Put the page on the free list.
     */
    pPage->u = 0;
    pPage->Free.u2State = GMM_PAGE_STATE_FREE;
    pPage->Free.fZeroed = false;
    Assert(pChunk->iFreeHead < RT_ELEMENTS(pChunk->aPages) || pChunk->iFreeHead == UINT16_MAX);
    pPage->Free.iNext = pChunk->iFreeHead;
    pChunk->iFreeHead = pPage - &pChunk->aPages[0];

    /*
     * Update statistics (the cShared/cPrivate stats are up to date already),
     * and relink the chunk if necessary.
     */
    unsigned const cFree = pChunk->cFree;
    if (   !cFree
        || gmmR0SelectFreeSetList(cFree) != gmmR0SelectFreeSetList(cFree + 1))
    {
        gmmR0UnlinkChunk(pChunk);
        pChunk->cFree++;
        gmmR0SelectSetAndLinkChunk(pGMM, pGVM, pChunk);
    }
    else
    {
        pChunk->cFree = cFree + 1;
        pChunk->pSet->cFreePages++;
    }

    /*
     * If the chunk becomes empty, consider giving memory back to the host OS.
     *
     * The current strategy is to try give it back if there are other chunks
     * in this free list, meaning if there are at least 240 free pages in this
     * category. Note that since there are probably mappings of the chunk,
     * it won't be freed up instantly, which probably screws up this logic
     * a bit...
     */
    /** @todo Do this on the way out. */
    if (RT_LIKELY(   pChunk->cFree != GMM_CHUNK_NUM_PAGES
                  || pChunk->pFreeNext == NULL
                  || pChunk->pFreePrev == NULL /** @todo this is probably misfiring, see reset... */))
    { /* likely */ }
    else
        gmmR0FreeChunk(pGMM, NULL, pChunk, false);
}


/**
 * Frees a shared page, the page is known to exist and be valid and such.
 *
 * @param   pGMM        Pointer to the GMM instance.
 * @param   pGVM        Pointer to the GVM instance.
 * @param   idPage      The page id.
 * @param   pPage       The page structure.
 */
DECLINLINE(void) gmmR0FreeSharedPage(PGMM pGMM, PGVM pGVM, uint32_t idPage, PGMMPAGE pPage)
{
    PGMMCHUNK pChunk = gmmR0GetChunk(pGMM, idPage >> GMM_CHUNKID_SHIFT);
    Assert(pChunk);
    Assert(pChunk->cFree < GMM_CHUNK_NUM_PAGES);
    Assert(pChunk->cShared > 0);
    Assert(pGMM->cSharedPages > 0);
    Assert(pGMM->cAllocatedPages > 0);
    Assert(!pPage->Shared.cRefs);

    pChunk->cShared--;
    pGMM->cAllocatedPages--;
    pGMM->cSharedPages--;
    gmmR0FreePageWorker(pGMM, pGVM, pChunk, idPage, pPage);
}


/**
 * Frees a private page, the page is known to exist and be valid and such.
 *
 * @param   pGMM        Pointer to the GMM instance.
 * @param   pGVM        Pointer to the GVM instance.
 * @param   idPage      The page id.
 * @param   pPage       The page structure.
 */
DECLINLINE(void) gmmR0FreePrivatePage(PGMM pGMM, PGVM pGVM, uint32_t idPage, PGMMPAGE pPage)
{
    PGMMCHUNK pChunk = gmmR0GetChunk(pGMM, idPage >> GMM_CHUNKID_SHIFT);
    Assert(pChunk);
    Assert(pChunk->cFree < GMM_CHUNK_NUM_PAGES);
    Assert(pChunk->cPrivate > 0);
    Assert(pGMM->cAllocatedPages > 0);

    pChunk->cPrivate--;
    pGMM->cAllocatedPages--;
    gmmR0FreePageWorker(pGMM, pGVM, pChunk, idPage, pPage);
}


/**
 * Common worker for GMMR0FreePages and GMMR0BalloonedPages.
 *
 * @returns VBox status code:
 * @retval  xxx
 *
 * @param   pGMM        Pointer to the GMM instance data.
 * @param   pGVM        Pointer to the VM.
 * @param   cPages      The number of pages to free.
 * @param   paPages     Pointer to the page descriptors.
 * @param   enmAccount  The account this relates to.
 */
static int gmmR0FreePages(PGMM pGMM, PGVM pGVM, uint32_t cPages, PGMMFREEPAGEDESC paPages, GMMACCOUNT enmAccount)
{
    /*
     * Check that the request isn't impossible wrt to the account status.
     */
    switch (enmAccount)
    {
        case GMMACCOUNT_BASE:
            if (RT_UNLIKELY(pGVM->gmm.s.Stats.Allocated.cBasePages < cPages))
            {
                Log(("gmmR0FreePages: allocated=%#llx cPages=%#x!\n", pGVM->gmm.s.Stats.Allocated.cBasePages, cPages));
                return VERR_GMM_ATTEMPT_TO_FREE_TOO_MUCH;
            }
            break;
        case GMMACCOUNT_SHADOW:
            if (RT_UNLIKELY(pGVM->gmm.s.Stats.Allocated.cShadowPages < cPages))
            {
                Log(("gmmR0FreePages: allocated=%#llx cPages=%#x!\n", pGVM->gmm.s.Stats.Allocated.cShadowPages, cPages));
                return VERR_GMM_ATTEMPT_TO_FREE_TOO_MUCH;
            }
            break;
        case GMMACCOUNT_FIXED:
            if (RT_UNLIKELY(pGVM->gmm.s.Stats.Allocated.cFixedPages < cPages))
            {
                Log(("gmmR0FreePages: allocated=%#llx cPages=%#x!\n", pGVM->gmm.s.Stats.Allocated.cFixedPages, cPages));
                return VERR_GMM_ATTEMPT_TO_FREE_TOO_MUCH;
            }
            break;
        default:
            AssertMsgFailedReturn(("enmAccount=%d\n", enmAccount), VERR_IPE_NOT_REACHED_DEFAULT_CASE);
    }

    /*
     * Walk the descriptors and free the pages.
     *
     * Statistics (except the account) are being updated as we go along,
     * unlike the alloc code. Also, stop on the first error.
     */
    int rc = VINF_SUCCESS;
    uint32_t iPage;
    for (iPage = 0; iPage < cPages; iPage++)
    {
        uint32_t idPage = paPages[iPage].idPage;
        PGMMPAGE pPage = gmmR0GetPage(pGMM, idPage);
        if (RT_LIKELY(pPage))
        {
            if (RT_LIKELY(GMM_PAGE_IS_PRIVATE(pPage)))
            {
                if (RT_LIKELY(pPage->Private.hGVM == pGVM->hSelf))
                {
                    Assert(pGVM->gmm.s.Stats.cPrivatePages);
                    pGVM->gmm.s.Stats.cPrivatePages--;
                    gmmR0FreePrivatePage(pGMM, pGVM, idPage, pPage);
                }
                else
                {
                    Log(("gmmR0AllocatePages: #%#x/%#x: not owner! hGVM=%#x hSelf=%#x\n", iPage, idPage,
                         pPage->Private.hGVM, pGVM->hSelf));
                    rc = VERR_GMM_NOT_PAGE_OWNER;
                    break;
                }
            }
            else if (RT_LIKELY(GMM_PAGE_IS_SHARED(pPage)))
            {
                Assert(pGVM->gmm.s.Stats.cSharedPages);
                Assert(pPage->Shared.cRefs);
#if defined(VBOX_WITH_PAGE_SHARING) && defined(VBOX_STRICT)
                if (pPage->Shared.u14Checksum)
                {
                    uint32_t uChecksum = gmmR0StrictPageChecksum(pGMM, pGVM, idPage);
                    uChecksum &= UINT32_C(0x00003fff);
                    AssertMsg(!uChecksum || uChecksum == pPage->Shared.u14Checksum,
                              ("%#x vs %#x - idPage=%#x\n", uChecksum, pPage->Shared.u14Checksum, idPage));
                }
#endif
                pGVM->gmm.s.Stats.cSharedPages--;
                if (!--pPage->Shared.cRefs)
                    gmmR0FreeSharedPage(pGMM, pGVM, idPage, pPage);
                else
                {
                    Assert(pGMM->cDuplicatePages);
                    pGMM->cDuplicatePages--;
                }
            }
            else
            {
                Log(("gmmR0AllocatePages: #%#x/%#x: already free!\n", iPage, idPage));
                rc = VERR_GMM_PAGE_ALREADY_FREE;
                break;
            }
        }
        else
        {
            Log(("gmmR0AllocatePages: #%#x/%#x: not found!\n", iPage, idPage));
            rc = VERR_GMM_PAGE_NOT_FOUND;
            break;
        }
        paPages[iPage].idPage = NIL_GMM_PAGEID;
    }

    /*
     * Update the account.
     */
    switch (enmAccount)
    {
        case GMMACCOUNT_BASE:   pGVM->gmm.s.Stats.Allocated.cBasePages   -= iPage; break;
        case GMMACCOUNT_SHADOW: pGVM->gmm.s.Stats.Allocated.cShadowPages -= iPage; break;
        case GMMACCOUNT_FIXED:  pGVM->gmm.s.Stats.Allocated.cFixedPages  -= iPage; break;
        default:
            AssertMsgFailedReturn(("enmAccount=%d\n", enmAccount), VERR_IPE_NOT_REACHED_DEFAULT_CASE);
    }

    /*
     * Any threshold stuff to be done here?
     */

    return rc;
}


/**
 * Free one or more pages.
 *
 * This is typically used at reset time or power off.
 *
 * @returns VBox status code:
 * @retval  xxx
 *
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   idCpu       The VCPU id.
 * @param   cPages      The number of pages to allocate.
 * @param   paPages     Pointer to the page descriptors containing the page IDs
 *                      for each page.
 * @param   enmAccount  The account this relates to.
 * @thread  EMT.
 */
GMMR0DECL(int) GMMR0FreePages(PGVM pGVM, VMCPUID idCpu, uint32_t cPages, PGMMFREEPAGEDESC paPages, GMMACCOUNT enmAccount)
{
    LogFlow(("GMMR0FreePages: pGVM=%p cPages=%#x paPages=%p enmAccount=%d\n", pGVM, cPages, paPages, enmAccount));

    /*
     * Validate input and get the basics.
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_GMM_INSTANCE);
    int rc = GVMMR0ValidateGVMandEMT(pGVM, idCpu);
    if (RT_FAILURE(rc))
        return rc;

    AssertPtrReturn(paPages, VERR_INVALID_PARAMETER);
    AssertMsgReturn(enmAccount > GMMACCOUNT_INVALID && enmAccount < GMMACCOUNT_END, ("%d\n", enmAccount), VERR_INVALID_PARAMETER);
    AssertMsgReturn(cPages > 0 && cPages < RT_BIT(32 - GUEST_PAGE_SHIFT), ("%#x\n", cPages), VERR_INVALID_PARAMETER);

    for (unsigned iPage = 0; iPage < cPages; iPage++)
        AssertMsgReturn(    paPages[iPage].idPage <= GMM_PAGEID_LAST
                        /*||  paPages[iPage].idPage == NIL_GMM_PAGEID*/,
                        ("#%#x: %#x\n", iPage, paPages[iPage].idPage), VERR_INVALID_PARAMETER);

    /*
     * Take the semaphore and call the worker function.
     */
    gmmR0MutexAcquire(pGMM);
    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {
        rc = gmmR0FreePages(pGMM, pGVM, cPages, paPages, enmAccount);
        GMM_CHECK_SANITY_UPON_LEAVING(pGMM);
    }
    else
        rc = VERR_GMM_IS_NOT_SANE;
    gmmR0MutexRelease(pGMM);
    LogFlow(("GMMR0FreePages: returns %Rrc\n", rc));
    return rc;
}


/**
 * VMMR0 request wrapper for GMMR0FreePages.
 *
 * @returns see GMMR0FreePages.
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   idCpu       The VCPU id.
 * @param   pReq        Pointer to the request packet.
 */
GMMR0DECL(int) GMMR0FreePagesReq(PGVM pGVM, VMCPUID idCpu, PGMMFREEPAGESREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq >= RT_UOFFSETOF(GMMFREEPAGESREQ, aPages[0]),
                    ("%#x < %#x\n", pReq->Hdr.cbReq, RT_UOFFSETOF(GMMFREEPAGESREQ, aPages[0])),
                    VERR_INVALID_PARAMETER);
    AssertMsgReturn(pReq->Hdr.cbReq == RT_UOFFSETOF_DYN(GMMFREEPAGESREQ, aPages[pReq->cPages]),
                    ("%#x != %#x\n", pReq->Hdr.cbReq, RT_UOFFSETOF_DYN(GMMFREEPAGESREQ, aPages[pReq->cPages])),
                    VERR_INVALID_PARAMETER);

    return GMMR0FreePages(pGVM, idCpu, pReq->cPages, &pReq->aPages[0], pReq->enmAccount);
}


/**
 * Report back on a memory ballooning request.
 *
 * The request may or may not have been initiated by the GMM. If it was initiated
 * by the GMM it is important that this function is called even if no pages were
 * ballooned.
 *
 * @returns VBox status code:
 * @retval  VERR_GMM_ATTEMPT_TO_FREE_TOO_MUCH
 * @retval  VERR_GMM_ATTEMPT_TO_DEFLATE_TOO_MUCH
 * @retval  VERR_GMM_OVERCOMMITTED_TRY_AGAIN_IN_A_BIT - reset condition
 *          indicating that we won't necessarily have sufficient RAM to boot
 *          the VM again and that it should pause until this changes (we'll try
 *          balloon some other VM).  (For standard deflate we have little choice
 *          but to hope the VM won't use the memory that was returned to it.)
 *
 * @param   pGVM                The global (ring-0) VM structure.
 * @param   idCpu               The VCPU id.
 * @param   enmAction           Inflate/deflate/reset.
 * @param   cBalloonedPages     The number of pages that was ballooned.
 *
 * @thread  EMT(idCpu)
 */
GMMR0DECL(int) GMMR0BalloonedPages(PGVM pGVM, VMCPUID idCpu, GMMBALLOONACTION enmAction, uint32_t cBalloonedPages)
{
    LogFlow(("GMMR0BalloonedPages: pGVM=%p enmAction=%d cBalloonedPages=%#x\n",
             pGVM, enmAction, cBalloonedPages));

    AssertMsgReturn(cBalloonedPages < RT_BIT(32 - GUEST_PAGE_SHIFT), ("%#x\n", cBalloonedPages), VERR_INVALID_PARAMETER);

    /*
     * Validate input and get the basics.
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_GMM_INSTANCE);
    int rc = GVMMR0ValidateGVMandEMT(pGVM, idCpu);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Take the semaphore and do some more validations.
     */
    gmmR0MutexAcquire(pGMM);
    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {
        switch (enmAction)
        {
            case GMMBALLOONACTION_INFLATE:
            {
                if (RT_LIKELY(pGVM->gmm.s.Stats.Allocated.cBasePages + pGVM->gmm.s.Stats.cBalloonedPages + cBalloonedPages
                              <= pGVM->gmm.s.Stats.Reserved.cBasePages))
                {
                    /*
                     * Record the ballooned memory.
                     */
                    pGMM->cBalloonedPages += cBalloonedPages;
                    if (pGVM->gmm.s.Stats.cReqBalloonedPages)
                    {
                        /* Codepath never taken. Might be interesting in the future to request ballooned memory from guests in low memory conditions.. */
                        AssertFailed();

                        pGVM->gmm.s.Stats.cBalloonedPages            += cBalloonedPages;
                        pGVM->gmm.s.Stats.cReqActuallyBalloonedPages += cBalloonedPages;
                        Log(("GMMR0BalloonedPages: +%#x - Global=%#llx / VM: Total=%#llx Req=%#llx Actual=%#llx (pending)\n",
                             cBalloonedPages, pGMM->cBalloonedPages, pGVM->gmm.s.Stats.cBalloonedPages,
                             pGVM->gmm.s.Stats.cReqBalloonedPages, pGVM->gmm.s.Stats.cReqActuallyBalloonedPages));
                    }
                    else
                    {
                        pGVM->gmm.s.Stats.cBalloonedPages += cBalloonedPages;
                        Log(("GMMR0BalloonedPages: +%#x - Global=%#llx / VM: Total=%#llx (user)\n",
                             cBalloonedPages, pGMM->cBalloonedPages, pGVM->gmm.s.Stats.cBalloonedPages));
                    }
                }
                else
                {
                    Log(("GMMR0BalloonedPages: cBasePages=%#llx Total=%#llx cBalloonedPages=%#llx Reserved=%#llx\n",
                         pGVM->gmm.s.Stats.Allocated.cBasePages, pGVM->gmm.s.Stats.cBalloonedPages, cBalloonedPages,
                         pGVM->gmm.s.Stats.Reserved.cBasePages));
                    rc = VERR_GMM_ATTEMPT_TO_FREE_TOO_MUCH;
                }
                break;
            }

            case GMMBALLOONACTION_DEFLATE:
            {
                /* Deflate. */
                if (pGVM->gmm.s.Stats.cBalloonedPages >= cBalloonedPages)
                {
                    /*
                     * Record the ballooned memory.
                     */
                    Assert(pGMM->cBalloonedPages >= cBalloonedPages);
                    pGMM->cBalloonedPages             -= cBalloonedPages;
                    pGVM->gmm.s.Stats.cBalloonedPages -= cBalloonedPages;
                    if (pGVM->gmm.s.Stats.cReqDeflatePages)
                    {
                        AssertFailed(); /* This is path is for later. */
                        Log(("GMMR0BalloonedPages: -%#x - Global=%#llx / VM: Total=%#llx Req=%#llx\n",
                             cBalloonedPages, pGMM->cBalloonedPages, pGVM->gmm.s.Stats.cBalloonedPages, pGVM->gmm.s.Stats.cReqDeflatePages));

                        /*
                         * Anything we need to do here now when the request has been completed?
                         */
                        pGVM->gmm.s.Stats.cReqDeflatePages = 0;
                    }
                    else
                        Log(("GMMR0BalloonedPages: -%#x - Global=%#llx / VM: Total=%#llx (user)\n",
                             cBalloonedPages, pGMM->cBalloonedPages, pGVM->gmm.s.Stats.cBalloonedPages));
                }
                else
                {
                    Log(("GMMR0BalloonedPages: Total=%#llx cBalloonedPages=%#llx\n", pGVM->gmm.s.Stats.cBalloonedPages, cBalloonedPages));
                    rc = VERR_GMM_ATTEMPT_TO_DEFLATE_TOO_MUCH;
                }
                break;
            }

            case GMMBALLOONACTION_RESET:
            {
                /* Reset to an empty balloon. */
                Assert(pGMM->cBalloonedPages >= pGVM->gmm.s.Stats.cBalloonedPages);

                pGMM->cBalloonedPages             -= pGVM->gmm.s.Stats.cBalloonedPages;
                pGVM->gmm.s.Stats.cBalloonedPages  = 0;
                break;
            }

            default:
                rc = VERR_INVALID_PARAMETER;
                break;
        }
        GMM_CHECK_SANITY_UPON_LEAVING(pGMM);
    }
    else
        rc = VERR_GMM_IS_NOT_SANE;

    gmmR0MutexRelease(pGMM);
    LogFlow(("GMMR0BalloonedPages: returns %Rrc\n", rc));
    return rc;
}


/**
 * VMMR0 request wrapper for GMMR0BalloonedPages.
 *
 * @returns see GMMR0BalloonedPages.
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   idCpu       The VCPU id.
 * @param   pReq        Pointer to the request packet.
 */
GMMR0DECL(int) GMMR0BalloonedPagesReq(PGVM pGVM, VMCPUID idCpu, PGMMBALLOONEDPAGESREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(GMMBALLOONEDPAGESREQ),
                    ("%#x < %#x\n", pReq->Hdr.cbReq, sizeof(GMMBALLOONEDPAGESREQ)),
                    VERR_INVALID_PARAMETER);

    return GMMR0BalloonedPages(pGVM, idCpu, pReq->enmAction, pReq->cBalloonedPages);
}


/**
 * Return memory statistics for the hypervisor
 *
 * @returns VBox status code.
 * @param   pReq        Pointer to the request packet.
 */
GMMR0DECL(int) GMMR0QueryHypervisorMemoryStatsReq(PGMMMEMSTATSREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(GMMMEMSTATSREQ),
                    ("%#x < %#x\n", pReq->Hdr.cbReq, sizeof(GMMMEMSTATSREQ)),
                    VERR_INVALID_PARAMETER);

    /*
     * Validate input and get the basics.
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_GMM_INSTANCE);
    pReq->cAllocPages     = pGMM->cAllocatedPages;
    pReq->cFreePages      = (pGMM->cChunks << (GMM_CHUNK_SHIFT - GUEST_PAGE_SHIFT)) - pGMM->cAllocatedPages;
    pReq->cBalloonedPages = pGMM->cBalloonedPages;
    pReq->cMaxPages       = pGMM->cMaxPages;
    pReq->cSharedPages    = pGMM->cDuplicatePages;
    GMM_CHECK_SANITY_UPON_LEAVING(pGMM);

    return VINF_SUCCESS;
}


/**
 * Return memory statistics for the VM
 *
 * @returns VBox status code.
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   idCpu       Cpu id.
 * @param   pReq        Pointer to the request packet.
 *
 * @thread  EMT(idCpu)
 */
GMMR0DECL(int) GMMR0QueryMemoryStatsReq(PGVM pGVM, VMCPUID idCpu, PGMMMEMSTATSREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(GMMMEMSTATSREQ),
                    ("%#x < %#x\n", pReq->Hdr.cbReq, sizeof(GMMMEMSTATSREQ)),
                    VERR_INVALID_PARAMETER);

    /*
     * Validate input and get the basics.
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_GMM_INSTANCE);
    int rc = GVMMR0ValidateGVMandEMT(pGVM, idCpu);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Take the semaphore and do some more validations.
     */
    gmmR0MutexAcquire(pGMM);
    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {
        pReq->cAllocPages     = pGVM->gmm.s.Stats.Allocated.cBasePages;
        pReq->cBalloonedPages = pGVM->gmm.s.Stats.cBalloonedPages;
        pReq->cMaxPages       = pGVM->gmm.s.Stats.Reserved.cBasePages;
        pReq->cFreePages      = pReq->cMaxPages - pReq->cAllocPages;
    }
    else
        rc = VERR_GMM_IS_NOT_SANE;

    gmmR0MutexRelease(pGMM);
    LogFlow(("GMMR3QueryVMMemoryStats: returns %Rrc\n", rc));
    return rc;
}


/**
 * Worker for gmmR0UnmapChunk and gmmr0FreeChunk.
 *
 * Don't call this in legacy allocation mode!
 *
 * @returns VBox status code.
 * @param   pGMM        Pointer to the GMM instance data.
 * @param   pGVM        Pointer to the Global VM structure.
 * @param   pChunk      Pointer to the chunk to be unmapped.
 */
static int gmmR0UnmapChunkLocked(PGMM pGMM, PGVM pGVM, PGMMCHUNK pChunk)
{
    RT_NOREF_PV(pGMM);

    /*
     * Find the mapping and try unmapping it.
     */
    uint32_t cMappings = pChunk->cMappingsX;
    for (uint32_t i = 0; i < cMappings; i++)
    {
        Assert(pChunk->paMappingsX[i].pGVM && pChunk->paMappingsX[i].hMapObj != NIL_RTR0MEMOBJ);
        if (pChunk->paMappingsX[i].pGVM == pGVM)
        {
            /* unmap */
            int rc = RTR0MemObjFree(pChunk->paMappingsX[i].hMapObj, false /* fFreeMappings (NA) */);
            if (RT_SUCCESS(rc))
            {
                /* update the record. */
                cMappings--;
                if (i < cMappings)
                    pChunk->paMappingsX[i] = pChunk->paMappingsX[cMappings];
                pChunk->paMappingsX[cMappings].hMapObj = NIL_RTR0MEMOBJ;
                pChunk->paMappingsX[cMappings].pGVM    = NULL;
                Assert(pChunk->cMappingsX - 1U == cMappings);
                pChunk->cMappingsX = cMappings;
            }

            return rc;
        }
    }

    Log(("gmmR0UnmapChunk: Chunk %#x is not mapped into pGVM=%p/%#x\n", pChunk->Core.Key, pGVM, pGVM->hSelf));
    return VERR_GMM_CHUNK_NOT_MAPPED;
}


/**
 * Unmaps a chunk previously mapped into the address space of the current process.
 *
 * @returns VBox status code.
 * @param   pGMM        Pointer to the GMM instance data.
 * @param   pGVM        Pointer to the Global VM structure.
 * @param   pChunk      Pointer to the chunk to be unmapped.
 * @param   fRelaxedSem Whether we can release the semaphore while doing the
 *                      mapping (@c true) or not.
 */
static int gmmR0UnmapChunk(PGMM pGMM, PGVM pGVM, PGMMCHUNK pChunk, bool fRelaxedSem)
{
    /*
     * Lock the chunk and if possible leave the giant GMM lock.
     */
    GMMR0CHUNKMTXSTATE MtxState;
    int rc = gmmR0ChunkMutexAcquire(&MtxState, pGMM, pChunk,
                                    fRelaxedSem ? GMMR0CHUNK_MTX_RETAKE_GIANT : GMMR0CHUNK_MTX_KEEP_GIANT);
    if (RT_SUCCESS(rc))
    {
        rc = gmmR0UnmapChunkLocked(pGMM, pGVM, pChunk);
        gmmR0ChunkMutexRelease(&MtxState, pChunk);
    }
    return rc;
}


/**
 * Worker for gmmR0MapChunk.
 *
 * @returns VBox status code.
 * @param   pGMM        Pointer to the GMM instance data.
 * @param   pGVM        Pointer to the Global VM structure.
 * @param   pChunk      Pointer to the chunk to be mapped.
 * @param   ppvR3       Where to store the ring-3 address of the mapping.
 *                      In the VERR_GMM_CHUNK_ALREADY_MAPPED case, this will be
 *                      contain the address of the existing mapping.
 */
static int gmmR0MapChunkLocked(PGMM pGMM, PGVM pGVM, PGMMCHUNK pChunk, PRTR3PTR ppvR3)
{
    RT_NOREF(pGMM);

    /*
     * Check to see if the chunk is already mapped.
     */
    for (uint32_t i = 0; i < pChunk->cMappingsX; i++)
    {
        Assert(pChunk->paMappingsX[i].pGVM && pChunk->paMappingsX[i].hMapObj != NIL_RTR0MEMOBJ);
        if (pChunk->paMappingsX[i].pGVM == pGVM)
        {
            *ppvR3 = RTR0MemObjAddressR3(pChunk->paMappingsX[i].hMapObj);
            Log(("gmmR0MapChunk: chunk %#x is already mapped at %p!\n", pChunk->Core.Key, *ppvR3));
#ifdef VBOX_WITH_PAGE_SHARING
            /* The ring-3 chunk cache can be out of sync; don't fail. */
            return VINF_SUCCESS;
#else
            return VERR_GMM_CHUNK_ALREADY_MAPPED;
#endif
        }
    }

    /*
     * Do the mapping.
     */
    RTR0MEMOBJ hMapObj;
    int rc = RTR0MemObjMapUser(&hMapObj, pChunk->hMemObj, (RTR3PTR)-1, 0, RTMEM_PROT_READ | RTMEM_PROT_WRITE, NIL_RTR0PROCESS);
    if (RT_SUCCESS(rc))
    {
        /* reallocate the array? assumes few users per chunk (usually one). */
        unsigned iMapping = pChunk->cMappingsX;
        if (   iMapping <= 3
            || (iMapping & 3) == 0)
        {
            unsigned cNewSize = iMapping <= 3
                              ? iMapping + 1
                              : iMapping + 4;
            Assert(cNewSize < 4 || RT_ALIGN_32(cNewSize, 4) == cNewSize);
            if (RT_UNLIKELY(cNewSize > UINT16_MAX))
            {
                rc = RTR0MemObjFree(hMapObj, false /* fFreeMappings (NA) */); AssertRC(rc);
                return VERR_GMM_TOO_MANY_CHUNK_MAPPINGS;
            }

            void *pvMappings = RTMemRealloc(pChunk->paMappingsX, cNewSize * sizeof(pChunk->paMappingsX[0]));
            if (RT_UNLIKELY(!pvMappings))
            {
                rc = RTR0MemObjFree(hMapObj, false /* fFreeMappings (NA) */); AssertRC(rc);
                return VERR_NO_MEMORY;
            }
            pChunk->paMappingsX = (PGMMCHUNKMAP)pvMappings;
        }

        /* insert new entry */
        pChunk->paMappingsX[iMapping].hMapObj = hMapObj;
        pChunk->paMappingsX[iMapping].pGVM    = pGVM;
        Assert(pChunk->cMappingsX == iMapping);
        pChunk->cMappingsX = iMapping + 1;

        *ppvR3 = RTR0MemObjAddressR3(hMapObj);
    }

    return rc;
}


/**
 * Maps a chunk into the user address space of the current process.
 *
 * @returns VBox status code.
 * @param   pGMM        Pointer to the GMM instance data.
 * @param   pGVM        Pointer to the Global VM structure.
 * @param   pChunk      Pointer to the chunk to be mapped.
 * @param   fRelaxedSem Whether we can release the semaphore while doing the
 *                      mapping (@c true) or not.
 * @param   ppvR3       Where to store the ring-3 address of the mapping.
 *                      In the VERR_GMM_CHUNK_ALREADY_MAPPED case, this will be
 *                      contain the address of the existing mapping.
 */
static int gmmR0MapChunk(PGMM pGMM, PGVM pGVM, PGMMCHUNK pChunk, bool fRelaxedSem, PRTR3PTR ppvR3)
{
    /*
     * Take the chunk lock and leave the giant GMM lock when possible, then
     * call the worker function.
     */
    GMMR0CHUNKMTXSTATE MtxState;
    int rc = gmmR0ChunkMutexAcquire(&MtxState, pGMM, pChunk,
                                    fRelaxedSem ? GMMR0CHUNK_MTX_RETAKE_GIANT : GMMR0CHUNK_MTX_KEEP_GIANT);
    if (RT_SUCCESS(rc))
    {
        rc = gmmR0MapChunkLocked(pGMM, pGVM, pChunk, ppvR3);
        gmmR0ChunkMutexRelease(&MtxState, pChunk);
    }

    return rc;
}



#if defined(VBOX_WITH_PAGE_SHARING) || defined(VBOX_STRICT)
/**
 * Check if a chunk is mapped into the specified VM
 *
 * @returns mapped yes/no
 * @param   pGMM        Pointer to the GMM instance.
 * @param   pGVM        Pointer to the Global VM structure.
 * @param   pChunk      Pointer to the chunk to be mapped.
 * @param   ppvR3       Where to store the ring-3 address of the mapping.
 */
static bool gmmR0IsChunkMapped(PGMM pGMM, PGVM pGVM, PGMMCHUNK pChunk, PRTR3PTR ppvR3)
{
    GMMR0CHUNKMTXSTATE MtxState;
    gmmR0ChunkMutexAcquire(&MtxState, pGMM, pChunk, GMMR0CHUNK_MTX_KEEP_GIANT);
    for (uint32_t i = 0; i < pChunk->cMappingsX; i++)
    {
        Assert(pChunk->paMappingsX[i].pGVM && pChunk->paMappingsX[i].hMapObj != NIL_RTR0MEMOBJ);
        if (pChunk->paMappingsX[i].pGVM == pGVM)
        {
            *ppvR3 = RTR0MemObjAddressR3(pChunk->paMappingsX[i].hMapObj);
            gmmR0ChunkMutexRelease(&MtxState, pChunk);
            return true;
        }
    }
    *ppvR3 = NULL;
    gmmR0ChunkMutexRelease(&MtxState, pChunk);
    return false;
}
#endif /* VBOX_WITH_PAGE_SHARING || VBOX_STRICT */


/**
 * Map a chunk and/or unmap another chunk.
 *
 * The mapping and unmapping applies to the current process.
 *
 * This API does two things because it saves a kernel call per mapping when
 * when the ring-3 mapping cache is full.
 *
 * @returns VBox status code.
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   idChunkMap      The chunk to map. NIL_GMM_CHUNKID if nothing to map.
 * @param   idChunkUnmap    The chunk to unmap. NIL_GMM_CHUNKID if nothing to unmap.
 * @param   ppvR3           Where to store the address of the mapped chunk. NULL is ok if nothing to map.
 * @thread  EMT ???
 */
GMMR0DECL(int) GMMR0MapUnmapChunk(PGVM pGVM, uint32_t idChunkMap, uint32_t idChunkUnmap, PRTR3PTR ppvR3)
{
    LogFlow(("GMMR0MapUnmapChunk: pGVM=%p idChunkMap=%#x idChunkUnmap=%#x ppvR3=%p\n",
             pGVM, idChunkMap, idChunkUnmap, ppvR3));

    /*
     * Validate input and get the basics.
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_GMM_INSTANCE);
    int rc = GVMMR0ValidateGVM(pGVM);
    if (RT_FAILURE(rc))
        return rc;

    AssertCompile(NIL_GMM_CHUNKID == 0);
    AssertMsgReturn(idChunkMap <= GMM_CHUNKID_LAST, ("%#x\n", idChunkMap), VERR_INVALID_PARAMETER);
    AssertMsgReturn(idChunkUnmap <= GMM_CHUNKID_LAST, ("%#x\n", idChunkUnmap), VERR_INVALID_PARAMETER);

    if (    idChunkMap == NIL_GMM_CHUNKID
        &&  idChunkUnmap == NIL_GMM_CHUNKID)
        return VERR_INVALID_PARAMETER;

    if (idChunkMap != NIL_GMM_CHUNKID)
    {
        AssertPtrReturn(ppvR3, VERR_INVALID_POINTER);
        *ppvR3 = NIL_RTR3PTR;
    }

    /*
     * Take the semaphore and do the work.
     *
     * The unmapping is done last since it's easier to undo a mapping than
     * undoing an unmapping. The ring-3 mapping cache cannot not be so big
     * that it pushes the user virtual address space to within a chunk of
     * it it's limits, so, no problem here.
     */
    gmmR0MutexAcquire(pGMM);
    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {
        PGMMCHUNK pMap = NULL;
        if (idChunkMap != NIL_GVM_HANDLE)
        {
            pMap = gmmR0GetChunk(pGMM, idChunkMap);
            if (RT_LIKELY(pMap))
                rc = gmmR0MapChunk(pGMM, pGVM, pMap, true /*fRelaxedSem*/, ppvR3);
            else
            {
                Log(("GMMR0MapUnmapChunk: idChunkMap=%#x\n", idChunkMap));
                rc = VERR_GMM_CHUNK_NOT_FOUND;
            }
        }
/** @todo split this operation, the bail out might (theoretcially) not be
 *        entirely safe. */

        if (    idChunkUnmap != NIL_GMM_CHUNKID
            &&  RT_SUCCESS(rc))
        {
            PGMMCHUNK pUnmap = gmmR0GetChunk(pGMM, idChunkUnmap);
            if (RT_LIKELY(pUnmap))
                rc = gmmR0UnmapChunk(pGMM, pGVM, pUnmap, true /*fRelaxedSem*/);
            else
            {
                Log(("GMMR0MapUnmapChunk: idChunkUnmap=%#x\n", idChunkUnmap));
                rc = VERR_GMM_CHUNK_NOT_FOUND;
            }

            if (RT_FAILURE(rc) && pMap)
                gmmR0UnmapChunk(pGMM, pGVM, pMap, false /*fRelaxedSem*/);
        }

        GMM_CHECK_SANITY_UPON_LEAVING(pGMM);
    }
    else
        rc = VERR_GMM_IS_NOT_SANE;
    gmmR0MutexRelease(pGMM);

    LogFlow(("GMMR0MapUnmapChunk: returns %Rrc\n", rc));
    return rc;
}


/**
 * VMMR0 request wrapper for GMMR0MapUnmapChunk.
 *
 * @returns see GMMR0MapUnmapChunk.
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   pReq        Pointer to the request packet.
 */
GMMR0DECL(int)  GMMR0MapUnmapChunkReq(PGVM pGVM, PGMMMAPUNMAPCHUNKREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(*pReq), ("%#x != %#x\n", pReq->Hdr.cbReq, sizeof(*pReq)), VERR_INVALID_PARAMETER);

    return GMMR0MapUnmapChunk(pGVM, pReq->idChunkMap, pReq->idChunkUnmap, &pReq->pvR3);
}


#ifndef VBOX_WITH_LINEAR_HOST_PHYS_MEM
/**
 * Gets the ring-0 virtual address for the given page.
 *
 * This is used by PGM when IEM and such wants to access guest RAM from ring-0.
 * One of the ASSUMPTIONS here is that the @a idPage is used by the VM and the
 * corresponding chunk will remain valid beyond the call (at least till the EMT
 * returns to ring-3).
 *
 * @returns VBox status code.
 * @param   pGVM        Pointer to the kernel-only VM instace data.
 * @param   idPage      The page ID.
 * @param   ppv         Where to store the address.
 * @thread  EMT
 */
GMMR0DECL(int)  GMMR0PageIdToVirt(PGVM pGVM, uint32_t idPage, void **ppv)
{
    *ppv = NULL;
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_GMM_INSTANCE);

    uint32_t const idChunk = idPage >> GMM_CHUNKID_SHIFT;

    /*
     * Start with the per-VM TLB.
     */
    RTSpinlockAcquire(pGVM->gmm.s.hChunkTlbSpinLock);

    PGMMPERVMCHUNKTLBE pTlbe = &pGVM->gmm.s.aChunkTlbEntries[GMMPERVM_CHUNKTLB_IDX(idChunk)];
    PGMMCHUNK pChunk = pTlbe->pChunk;
    if (   pChunk              != NULL
        && pTlbe->idGeneration == ASMAtomicUoReadU64(&pGMM->idFreeGeneration)
        && pChunk->Core.Key    == idChunk)
        pGVM->R0Stats.gmm.cChunkTlbHits++; /* hopefully this is a likely outcome */
    else
    {
        pGVM->R0Stats.gmm.cChunkTlbMisses++;

        /*
         * Look it up in the chunk tree.
         */
        RTSpinlockAcquire(pGMM->hSpinLockTree);
        pChunk = gmmR0GetChunkLocked(pGMM, idChunk);
        if (RT_LIKELY(pChunk))
        {
            pTlbe->idGeneration = pGMM->idFreeGeneration;
            RTSpinlockRelease(pGMM->hSpinLockTree);
            pTlbe->pChunk       = pChunk;
        }
        else
        {
            RTSpinlockRelease(pGMM->hSpinLockTree);
            RTSpinlockRelease(pGVM->gmm.s.hChunkTlbSpinLock);
            AssertMsgFailed(("idPage=%#x\n", idPage));
            return VERR_GMM_PAGE_NOT_FOUND;
        }
    }

    RTSpinlockRelease(pGVM->gmm.s.hChunkTlbSpinLock);

    /*
     * Got a chunk, now validate the page ownership and calcuate it's address.
     */
    const GMMPAGE * const pPage = &pChunk->aPages[idPage & GMM_PAGEID_IDX_MASK];
    if (RT_LIKELY(   (   GMM_PAGE_IS_PRIVATE(pPage)
                      && pPage->Private.hGVM == pGVM->hSelf)
                  || GMM_PAGE_IS_SHARED(pPage)))
    {
        AssertPtr(pChunk->pbMapping);
        *ppv = &pChunk->pbMapping[(idPage & GMM_PAGEID_IDX_MASK) << GUEST_PAGE_SHIFT];
        return VINF_SUCCESS;
    }
    AssertMsgFailed(("idPage=%#x is-private=%RTbool Private.hGVM=%u pGVM->hGVM=%u\n",
                     idPage, GMM_PAGE_IS_PRIVATE(pPage), pPage->Private.hGVM, pGVM->hSelf));
    return VERR_GMM_NOT_PAGE_OWNER;
}
#endif /* !VBOX_WITH_LINEAR_HOST_PHYS_MEM */

#ifdef VBOX_WITH_PAGE_SHARING

# ifdef VBOX_STRICT
/**
 * For checksumming shared pages in strict builds.
 *
 * The purpose is making sure that a page doesn't change.
 *
 * @returns Checksum, 0 on failure.
 * @param   pGMM        The GMM instance data.
 * @param   pGVM        Pointer to the kernel-only VM instace data.
 * @param   idPage      The page ID.
 */
static uint32_t gmmR0StrictPageChecksum(PGMM pGMM, PGVM pGVM, uint32_t idPage)
{
    PGMMCHUNK pChunk = gmmR0GetChunk(pGMM, idPage >> GMM_CHUNKID_SHIFT);
    AssertMsgReturn(pChunk, ("idPage=%#x\n", idPage), 0);

    uint8_t *pbChunk;
    if (!gmmR0IsChunkMapped(pGMM, pGVM, pChunk, (PRTR3PTR)&pbChunk))
        return 0;
    uint8_t const *pbPage = pbChunk + ((idPage & GMM_PAGEID_IDX_MASK) << GUEST_PAGE_SHIFT);

    return RTCrc32(pbPage, GUEST_PAGE_SIZE);
}
# endif /* VBOX_STRICT */


/**
 * Calculates the module hash value.
 *
 * @returns Hash value.
 * @param   pszModuleName   The module name.
 * @param   pszVersion      The module version string.
 */
static uint32_t gmmR0ShModCalcHash(const char *pszModuleName, const char *pszVersion)
{
    return RTStrHash1ExN(3, pszModuleName, RTSTR_MAX, "::", (size_t)2, pszVersion, RTSTR_MAX);
}


/**
 * Finds a global module.
 *
 * @returns Pointer to the global module on success, NULL if not found.
 * @param   pGMM            The GMM instance data.
 * @param   uHash           The hash as calculated by gmmR0ShModCalcHash.
 * @param   cbModule        The module size.
 * @param   enmGuestOS      The guest OS type.
 * @param   cRegions        The number of regions.
 * @param   pszModuleName   The module name.
 * @param   pszVersion      The module version.
 * @param   paRegions       The region descriptions.
 */
static PGMMSHAREDMODULE gmmR0ShModFindGlobal(PGMM pGMM, uint32_t uHash, uint32_t cbModule, VBOXOSFAMILY enmGuestOS,
                                             uint32_t cRegions, const char *pszModuleName, const char *pszVersion,
                                             struct VMMDEVSHAREDREGIONDESC const *paRegions)
{
    for (PGMMSHAREDMODULE pGblMod = (PGMMSHAREDMODULE)RTAvllU32Get(&pGMM->pGlobalSharedModuleTree, uHash);
         pGblMod;
         pGblMod = (PGMMSHAREDMODULE)pGblMod->Core.pList)
    {
        if (pGblMod->cbModule   != cbModule)
            continue;
        if (pGblMod->enmGuestOS != enmGuestOS)
            continue;
        if (pGblMod->cRegions   != cRegions)
            continue;
        if (strcmp(pGblMod->szName, pszModuleName))
            continue;
        if (strcmp(pGblMod->szVersion, pszVersion))
            continue;

        uint32_t i;
        for (i = 0; i < cRegions; i++)
        {
            uint32_t off = paRegions[i].GCRegionAddr & GUEST_PAGE_OFFSET_MASK;
            if (pGblMod->aRegions[i].off != off)
                break;

            uint32_t cb  = RT_ALIGN_32(paRegions[i].cbRegion + off, GUEST_PAGE_SIZE);
            if (pGblMod->aRegions[i].cb != cb)
                break;
        }

        if (i == cRegions)
            return pGblMod;
    }

    return NULL;
}


/**
 * Creates a new global module.
 *
 * @returns VBox status code.
 * @param   pGMM            The GMM instance data.
 * @param   uHash           The hash as calculated by gmmR0ShModCalcHash.
 * @param   cbModule        The module size.
 * @param   enmGuestOS      The guest OS type.
 * @param   cRegions        The number of regions.
 * @param   pszModuleName   The module name.
 * @param   pszVersion      The module version.
 * @param   paRegions       The region descriptions.
 * @param   ppGblMod        Where to return the new module on success.
 */
static int gmmR0ShModNewGlobal(PGMM pGMM, uint32_t uHash, uint32_t cbModule, VBOXOSFAMILY enmGuestOS,
                               uint32_t cRegions, const char *pszModuleName, const char *pszVersion,
                               struct VMMDEVSHAREDREGIONDESC const *paRegions, PGMMSHAREDMODULE *ppGblMod)
{
    Log(("gmmR0ShModNewGlobal: %s %s size %#x os %u rgn %u\n", pszModuleName, pszVersion, cbModule, enmGuestOS, cRegions));
    if (pGMM->cShareableModules >= GMM_MAX_SHARED_GLOBAL_MODULES)
    {
        Log(("gmmR0ShModNewGlobal: Too many modules\n"));
        return VERR_GMM_TOO_MANY_GLOBAL_MODULES;
    }

    PGMMSHAREDMODULE pGblMod = (PGMMSHAREDMODULE)RTMemAllocZ(RT_UOFFSETOF_DYN(GMMSHAREDMODULE, aRegions[cRegions]));
    if (!pGblMod)
    {
        Log(("gmmR0ShModNewGlobal: No memory\n"));
        return VERR_NO_MEMORY;
    }

    pGblMod->Core.Key   = uHash;
    pGblMod->cbModule   = cbModule;
    pGblMod->cRegions   = cRegions;
    pGblMod->cUsers     = 1;
    pGblMod->enmGuestOS = enmGuestOS;
    strcpy(pGblMod->szName, pszModuleName);
    strcpy(pGblMod->szVersion, pszVersion);

    for (uint32_t i = 0; i < cRegions; i++)
    {
        Log(("gmmR0ShModNewGlobal: rgn[%u]=%RGvLB%#x\n", i, paRegions[i].GCRegionAddr, paRegions[i].cbRegion));
        pGblMod->aRegions[i].off        = paRegions[i].GCRegionAddr & GUEST_PAGE_OFFSET_MASK;
        pGblMod->aRegions[i].cb         = paRegions[i].cbRegion + pGblMod->aRegions[i].off;
        pGblMod->aRegions[i].cb         = RT_ALIGN_32(pGblMod->aRegions[i].cb, GUEST_PAGE_SIZE);
        pGblMod->aRegions[i].paidPages  = NULL; /* allocated when needed. */
    }

    bool fInsert = RTAvllU32Insert(&pGMM->pGlobalSharedModuleTree, &pGblMod->Core);
    Assert(fInsert); NOREF(fInsert);
    pGMM->cShareableModules++;

    *ppGblMod = pGblMod;
    return VINF_SUCCESS;
}


/**
 * Deletes a global module which is no longer referenced by anyone.
 *
 * @param   pGMM                The GMM instance data.
 * @param   pGblMod             The module to delete.
 */
static void gmmR0ShModDeleteGlobal(PGMM pGMM, PGMMSHAREDMODULE pGblMod)
{
    Assert(pGblMod->cUsers == 0);
    Assert(pGMM->cShareableModules > 0 && pGMM->cShareableModules <= GMM_MAX_SHARED_GLOBAL_MODULES);

    void *pvTest = RTAvllU32RemoveNode(&pGMM->pGlobalSharedModuleTree, &pGblMod->Core);
    Assert(pvTest == pGblMod); NOREF(pvTest);
    pGMM->cShareableModules--;

    uint32_t i = pGblMod->cRegions;
    while (i-- > 0)
    {
        if (pGblMod->aRegions[i].paidPages)
        {
            /* We don't doing anything to the pages as they are handled by the
               copy-on-write mechanism in PGM. */
            RTMemFree(pGblMod->aRegions[i].paidPages);
            pGblMod->aRegions[i].paidPages = NULL;
        }
    }
    RTMemFree(pGblMod);
}


static int gmmR0ShModNewPerVM(PGVM pGVM, RTGCPTR GCBaseAddr, uint32_t cRegions, const VMMDEVSHAREDREGIONDESC *paRegions,
                              PGMMSHAREDMODULEPERVM *ppRecVM)
{
    if (pGVM->gmm.s.Stats.cShareableModules >= GMM_MAX_SHARED_PER_VM_MODULES)
        return VERR_GMM_TOO_MANY_PER_VM_MODULES;

    PGMMSHAREDMODULEPERVM pRecVM;
    pRecVM = (PGMMSHAREDMODULEPERVM)RTMemAllocZ(RT_UOFFSETOF_DYN(GMMSHAREDMODULEPERVM, aRegionsGCPtrs[cRegions]));
    if (!pRecVM)
        return VERR_NO_MEMORY;

    pRecVM->Core.Key = GCBaseAddr;
    for (uint32_t i = 0; i < cRegions; i++)
        pRecVM->aRegionsGCPtrs[i] = paRegions[i].GCRegionAddr;

    bool fInsert = RTAvlGCPtrInsert(&pGVM->gmm.s.pSharedModuleTree, &pRecVM->Core);
    Assert(fInsert); NOREF(fInsert);
    pGVM->gmm.s.Stats.cShareableModules++;

    *ppRecVM = pRecVM;
    return VINF_SUCCESS;
}


static void gmmR0ShModDeletePerVM(PGMM pGMM, PGVM pGVM, PGMMSHAREDMODULEPERVM pRecVM, bool fRemove)
{
    /*
     * Free the per-VM module.
     */
    PGMMSHAREDMODULE pGblMod = pRecVM->pGlobalModule;
    pRecVM->pGlobalModule    = NULL;

    if (fRemove)
    {
        void *pvTest = RTAvlGCPtrRemove(&pGVM->gmm.s.pSharedModuleTree, pRecVM->Core.Key);
        Assert(pvTest == &pRecVM->Core); NOREF(pvTest);
    }

    RTMemFree(pRecVM);

    /*
     * Release the global module.
     * (In the registration bailout case, it might not be.)
     */
    if (pGblMod)
    {
        Assert(pGblMod->cUsers > 0);
        pGblMod->cUsers--;
        if (pGblMod->cUsers == 0)
            gmmR0ShModDeleteGlobal(pGMM, pGblMod);
    }
}

#endif /* VBOX_WITH_PAGE_SHARING */

/**
 * Registers a new shared module for the VM.
 *
 * @returns VBox status code.
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   idCpu           The VCPU id.
 * @param   enmGuestOS      The guest OS type.
 * @param   pszModuleName   The module name.
 * @param   pszVersion      The module version.
 * @param   GCPtrModBase    The module base address.
 * @param   cbModule        The module size.
 * @param   cRegions        The mumber of shared region descriptors.
 * @param   paRegions       Pointer to an array of shared region(s).
 * @thread  EMT(idCpu)
 */
GMMR0DECL(int) GMMR0RegisterSharedModule(PGVM pGVM, VMCPUID idCpu, VBOXOSFAMILY enmGuestOS, char *pszModuleName,
                                         char *pszVersion, RTGCPTR GCPtrModBase, uint32_t cbModule,
                                         uint32_t cRegions, struct VMMDEVSHAREDREGIONDESC const *paRegions)
{
#ifdef VBOX_WITH_PAGE_SHARING
    /*
     * Validate input and get the basics.
     *
     * Note! Turns out the module size does necessarily match the size of the
     *       regions. (iTunes on XP)
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_GMM_INSTANCE);
    int rc = GVMMR0ValidateGVMandEMT(pGVM, idCpu);
    if (RT_FAILURE(rc))
        return rc;

    if (RT_UNLIKELY(cRegions > VMMDEVSHAREDREGIONDESC_MAX))
        return VERR_GMM_TOO_MANY_REGIONS;

    if (RT_UNLIKELY(cbModule == 0 || cbModule > _1G))
        return VERR_GMM_BAD_SHARED_MODULE_SIZE;

    uint32_t cbTotal = 0;
    for (uint32_t i = 0; i < cRegions; i++)
    {
        if (RT_UNLIKELY(paRegions[i].cbRegion == 0 || paRegions[i].cbRegion > _1G))
            return VERR_GMM_SHARED_MODULE_BAD_REGIONS_SIZE;

        cbTotal += paRegions[i].cbRegion;
        if (RT_UNLIKELY(cbTotal > _1G))
            return VERR_GMM_SHARED_MODULE_BAD_REGIONS_SIZE;
    }

    AssertPtrReturn(pszModuleName, VERR_INVALID_POINTER);
    if (RT_UNLIKELY(!memchr(pszModuleName, '\0', GMM_SHARED_MODULE_MAX_NAME_STRING)))
        return VERR_GMM_MODULE_NAME_TOO_LONG;

    AssertPtrReturn(pszVersion, VERR_INVALID_POINTER);
    if (RT_UNLIKELY(!memchr(pszVersion, '\0', GMM_SHARED_MODULE_MAX_VERSION_STRING)))
        return VERR_GMM_MODULE_NAME_TOO_LONG;

    uint32_t const uHash = gmmR0ShModCalcHash(pszModuleName, pszVersion);
    Log(("GMMR0RegisterSharedModule %s %s base %RGv size %x hash %x\n", pszModuleName, pszVersion, GCPtrModBase, cbModule, uHash));

    /*
     * Take the semaphore and do some more validations.
     */
    gmmR0MutexAcquire(pGMM);
    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {
        /*
         * Check if this module is already locally registered and register
         * it if it isn't.  The base address is a unique module identifier
         * locally.
         */
        PGMMSHAREDMODULEPERVM pRecVM = (PGMMSHAREDMODULEPERVM)RTAvlGCPtrGet(&pGVM->gmm.s.pSharedModuleTree, GCPtrModBase);
        bool fNewModule = pRecVM == NULL;
        if (fNewModule)
        {
            rc = gmmR0ShModNewPerVM(pGVM, GCPtrModBase, cRegions, paRegions, &pRecVM);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Find a matching global module, register a new one if needed.
                 */
                PGMMSHAREDMODULE pGblMod = gmmR0ShModFindGlobal(pGMM, uHash, cbModule, enmGuestOS, cRegions,
                                                                pszModuleName, pszVersion, paRegions);
                if (!pGblMod)
                {
                    Assert(fNewModule);
                    rc = gmmR0ShModNewGlobal(pGMM, uHash, cbModule, enmGuestOS, cRegions,
                                             pszModuleName, pszVersion, paRegions, &pGblMod);
                    if (RT_SUCCESS(rc))
                    {
                        pRecVM->pGlobalModule = pGblMod; /* (One referenced returned by gmmR0ShModNewGlobal.) */
                        Log(("GMMR0RegisterSharedModule: new module %s %s\n", pszModuleName, pszVersion));
                    }
                    else
                        gmmR0ShModDeletePerVM(pGMM, pGVM, pRecVM, true /*fRemove*/);
                }
                else
                {
                    Assert(pGblMod->cUsers > 0 && pGblMod->cUsers < UINT32_MAX / 2);
                    pGblMod->cUsers++;
                    pRecVM->pGlobalModule = pGblMod;

                    Log(("GMMR0RegisterSharedModule: new per vm module %s %s, gbl users %d\n", pszModuleName, pszVersion, pGblMod->cUsers));
                }
            }
        }
        else
        {
            /*
             * Attempt to re-register an existing module.
             */
            PGMMSHAREDMODULE pGblMod = gmmR0ShModFindGlobal(pGMM, uHash, cbModule, enmGuestOS, cRegions,
                                                            pszModuleName, pszVersion, paRegions);
            if (pRecVM->pGlobalModule == pGblMod)
            {
                Log(("GMMR0RegisterSharedModule: already registered %s %s, gbl users %d\n", pszModuleName, pszVersion, pGblMod->cUsers));
                rc = VINF_GMM_SHARED_MODULE_ALREADY_REGISTERED;
            }
            else
            {
                /** @todo may have to unregister+register when this happens in case it's caused
                 * by VBoxService crashing and being restarted... */
                Log(("GMMR0RegisterSharedModule: Address clash!\n"
                     "  incoming at %RGvLB%#x %s %s rgns %u\n"
                     "  existing at %RGvLB%#x %s %s rgns %u\n",
                     GCPtrModBase, cbModule, pszModuleName, pszVersion, cRegions,
                     pRecVM->Core.Key, pRecVM->pGlobalModule->cbModule, pRecVM->pGlobalModule->szName,
                     pRecVM->pGlobalModule->szVersion, pRecVM->pGlobalModule->cRegions));
                rc = VERR_GMM_SHARED_MODULE_ADDRESS_CLASH;
            }
        }
        GMM_CHECK_SANITY_UPON_LEAVING(pGMM);
    }
    else
        rc = VERR_GMM_IS_NOT_SANE;

    gmmR0MutexRelease(pGMM);
    return rc;
#else

    NOREF(pGVM); NOREF(idCpu); NOREF(enmGuestOS); NOREF(pszModuleName); NOREF(pszVersion);
    NOREF(GCPtrModBase); NOREF(cbModule); NOREF(cRegions); NOREF(paRegions);
    return VERR_NOT_IMPLEMENTED;
#endif
}


/**
 * VMMR0 request wrapper for GMMR0RegisterSharedModule.
 *
 * @returns see GMMR0RegisterSharedModule.
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   idCpu       The VCPU id.
 * @param   pReq        Pointer to the request packet.
 */
GMMR0DECL(int) GMMR0RegisterSharedModuleReq(PGVM pGVM, VMCPUID idCpu, PGMMREGISTERSHAREDMODULEREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(   pReq->Hdr.cbReq >= sizeof(*pReq)
                    && pReq->Hdr.cbReq == RT_UOFFSETOF_DYN(GMMREGISTERSHAREDMODULEREQ, aRegions[pReq->cRegions]),
                    ("%#x != %#x\n", pReq->Hdr.cbReq, sizeof(*pReq)), VERR_INVALID_PARAMETER);

    /* Pass back return code in the request packet to preserve informational codes. (VMMR3CallR0 chokes on them) */
    pReq->rc = GMMR0RegisterSharedModule(pGVM, idCpu, pReq->enmGuestOS, pReq->szName, pReq->szVersion,
                                         pReq->GCBaseAddr, pReq->cbModule, pReq->cRegions, pReq->aRegions);
    return VINF_SUCCESS;
}


/**
 * Unregisters a shared module for the VM
 *
 * @returns VBox status code.
 * @param   pGVM            The global (ring-0) VM structure.
 * @param   idCpu           The VCPU id.
 * @param   pszModuleName   The module name.
 * @param   pszVersion      The module version.
 * @param   GCPtrModBase    The module base address.
 * @param   cbModule        The module size.
 */
GMMR0DECL(int) GMMR0UnregisterSharedModule(PGVM pGVM, VMCPUID idCpu, char *pszModuleName, char *pszVersion,
                                           RTGCPTR GCPtrModBase, uint32_t cbModule)
{
#ifdef VBOX_WITH_PAGE_SHARING
    /*
     * Validate input and get the basics.
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_GMM_INSTANCE);
    int rc = GVMMR0ValidateGVMandEMT(pGVM, idCpu);
    if (RT_FAILURE(rc))
        return rc;

    AssertPtrReturn(pszModuleName, VERR_INVALID_POINTER);
    AssertPtrReturn(pszVersion, VERR_INVALID_POINTER);
    if (RT_UNLIKELY(!memchr(pszModuleName, '\0', GMM_SHARED_MODULE_MAX_NAME_STRING)))
        return VERR_GMM_MODULE_NAME_TOO_LONG;
    if (RT_UNLIKELY(!memchr(pszVersion, '\0', GMM_SHARED_MODULE_MAX_VERSION_STRING)))
        return VERR_GMM_MODULE_NAME_TOO_LONG;

    Log(("GMMR0UnregisterSharedModule %s %s base=%RGv size %x\n", pszModuleName, pszVersion, GCPtrModBase, cbModule));

    /*
     * Take the semaphore and do some more validations.
     */
    gmmR0MutexAcquire(pGMM);
    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {
        /*
         * Locate and remove the specified module.
         */
        PGMMSHAREDMODULEPERVM pRecVM = (PGMMSHAREDMODULEPERVM)RTAvlGCPtrGet(&pGVM->gmm.s.pSharedModuleTree, GCPtrModBase);
        if (pRecVM)
        {
            /** @todo Do we need to do more validations here, like that the
             *        name + version + cbModule matches? */
            NOREF(cbModule);
            Assert(pRecVM->pGlobalModule);
            gmmR0ShModDeletePerVM(pGMM, pGVM, pRecVM, true /*fRemove*/);
        }
        else
            rc = VERR_GMM_SHARED_MODULE_NOT_FOUND;

        GMM_CHECK_SANITY_UPON_LEAVING(pGMM);
    }
    else
        rc = VERR_GMM_IS_NOT_SANE;

    gmmR0MutexRelease(pGMM);
    return rc;
#else

    NOREF(pGVM); NOREF(idCpu); NOREF(pszModuleName); NOREF(pszVersion); NOREF(GCPtrModBase); NOREF(cbModule);
    return VERR_NOT_IMPLEMENTED;
#endif
}


/**
 * VMMR0 request wrapper for GMMR0UnregisterSharedModule.
 *
 * @returns see GMMR0UnregisterSharedModule.
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   idCpu       The VCPU id.
 * @param   pReq        Pointer to the request packet.
 */
GMMR0DECL(int)  GMMR0UnregisterSharedModuleReq(PGVM pGVM, VMCPUID idCpu, PGMMUNREGISTERSHAREDMODULEREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(*pReq), ("%#x != %#x\n", pReq->Hdr.cbReq, sizeof(*pReq)), VERR_INVALID_PARAMETER);

    return GMMR0UnregisterSharedModule(pGVM, idCpu, pReq->szName, pReq->szVersion, pReq->GCBaseAddr, pReq->cbModule);
}

#ifdef VBOX_WITH_PAGE_SHARING

/**
 * Increase the use count of a shared page, the page is known to exist and be valid and such.
 *
 * @param   pGMM        Pointer to the GMM instance.
 * @param   pGVM        Pointer to the GVM instance.
 * @param   pPage       The page structure.
 */
DECLINLINE(void) gmmR0UseSharedPage(PGMM pGMM, PGVM pGVM, PGMMPAGE pPage)
{
    Assert(pGMM->cSharedPages > 0);
    Assert(pGMM->cAllocatedPages > 0);

    pGMM->cDuplicatePages++;

    pPage->Shared.cRefs++;
    pGVM->gmm.s.Stats.cSharedPages++;
    pGVM->gmm.s.Stats.Allocated.cBasePages++;
}


/**
 * Converts a private page to a shared page, the page is known to exist and be valid and such.
 *
 * @param   pGMM        Pointer to the GMM instance.
 * @param   pGVM        Pointer to the GVM instance.
 * @param   HCPhys      Host physical address
 * @param   idPage      The Page ID
 * @param   pPage       The page structure.
 * @param   pPageDesc   Shared page descriptor
 */
DECLINLINE(void) gmmR0ConvertToSharedPage(PGMM pGMM, PGVM pGVM, RTHCPHYS HCPhys, uint32_t idPage, PGMMPAGE pPage,
                                          PGMMSHAREDPAGEDESC pPageDesc)
{
    PGMMCHUNK pChunk = gmmR0GetChunk(pGMM, idPage >> GMM_CHUNKID_SHIFT);
    Assert(pChunk);
    Assert(pChunk->cFree < GMM_CHUNK_NUM_PAGES);
    Assert(GMM_PAGE_IS_PRIVATE(pPage));

    pChunk->cPrivate--;
    pChunk->cShared++;

    pGMM->cSharedPages++;

    pGVM->gmm.s.Stats.cSharedPages++;
    pGVM->gmm.s.Stats.cPrivatePages--;

    /* Modify the page structure. */
    pPage->Shared.pfn         = (uint32_t)(uint64_t)(HCPhys >> GUEST_PAGE_SHIFT);
    pPage->Shared.cRefs       = 1;
#ifdef VBOX_STRICT
    pPageDesc->u32StrictChecksum = gmmR0StrictPageChecksum(pGMM, pGVM, idPage);
    pPage->Shared.u14Checksum = pPageDesc->u32StrictChecksum;
#else
    NOREF(pPageDesc);
    pPage->Shared.u14Checksum = 0;
#endif
    pPage->Shared.u2State     = GMM_PAGE_STATE_SHARED;
}


static int gmmR0SharedModuleCheckPageFirstTime(PGMM pGMM, PGVM pGVM, PGMMSHAREDMODULE pModule,
                                               unsigned idxRegion, unsigned idxPage,
                                               PGMMSHAREDPAGEDESC pPageDesc, PGMMSHAREDREGIONDESC pGlobalRegion)
{
    NOREF(pModule);

    /* Easy case: just change the internal page type. */
    PGMMPAGE pPage = gmmR0GetPage(pGMM, pPageDesc->idPage);
    AssertMsgReturn(pPage, ("idPage=%#x (GCPhys=%RGp HCPhys=%RHp idxRegion=%#x idxPage=%#x) #1\n",
                            pPageDesc->idPage, pPageDesc->GCPhys, pPageDesc->HCPhys, idxRegion, idxPage),
                    VERR_PGM_PHYS_INVALID_PAGE_ID);
    NOREF(idxRegion);

    AssertMsg(pPageDesc->GCPhys == (pPage->Private.pfn << 12), ("desc %RGp gmm %RGp\n", pPageDesc->HCPhys, (pPage->Private.pfn << 12)));

    gmmR0ConvertToSharedPage(pGMM, pGVM, pPageDesc->HCPhys, pPageDesc->idPage, pPage, pPageDesc);

    /* Keep track of these references. */
    pGlobalRegion->paidPages[idxPage] = pPageDesc->idPage;

    return VINF_SUCCESS;
}

/**
 * Checks specified shared module range for changes
 *
 * Performs the following tasks:
 *  - If a shared page is new, then it changes the GMM page type to shared and
 *    returns it in the pPageDesc descriptor.
 *  - If a shared page already exists, then it checks if the VM page is
 *    identical and if so frees the VM page and returns the shared page in
 *    pPageDesc descriptor.
 *
 * @remarks ASSUMES the caller has acquired the GMM semaphore!!
 *
 * @returns VBox status code.
 * @param   pGVM        Pointer to the GVM instance data.
 * @param   pModule     Module description
 * @param   idxRegion   Region index
 * @param   idxPage     Page index
 * @param   pPageDesc   Page descriptor
 */
GMMR0DECL(int) GMMR0SharedModuleCheckPage(PGVM pGVM, PGMMSHAREDMODULE pModule, uint32_t idxRegion, uint32_t idxPage,
                                          PGMMSHAREDPAGEDESC pPageDesc)
{
    int     rc;
    PGMM    pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_GMM_INSTANCE);
    pPageDesc->u32StrictChecksum = 0;

    AssertMsgReturn(idxRegion < pModule->cRegions,
                    ("idxRegion=%#x cRegions=%#x %s %s\n", idxRegion, pModule->cRegions, pModule->szName, pModule->szVersion),
                    VERR_INVALID_PARAMETER);

    uint32_t const cPages = pModule->aRegions[idxRegion].cb >> GUEST_PAGE_SHIFT;
    AssertMsgReturn(idxPage < cPages,
                    ("idxRegion=%#x cRegions=%#x %s %s\n", idxRegion, pModule->cRegions, pModule->szName, pModule->szVersion),
                    VERR_INVALID_PARAMETER);

    LogFlow(("GMMR0SharedModuleCheckRange %s base %RGv region %d idxPage %d\n", pModule->szName, pModule->Core.Key, idxRegion, idxPage));

    /*
     * First time; create a page descriptor array.
     */
    PGMMSHAREDREGIONDESC pGlobalRegion = &pModule->aRegions[idxRegion];
    if (!pGlobalRegion->paidPages)
    {
        Log(("Allocate page descriptor array for %d pages\n", cPages));
        pGlobalRegion->paidPages = (uint32_t *)RTMemAlloc(cPages * sizeof(pGlobalRegion->paidPages[0]));
        AssertReturn(pGlobalRegion->paidPages, VERR_NO_MEMORY);

        /* Invalidate all descriptors. */
        uint32_t i = cPages;
        while (i-- > 0)
            pGlobalRegion->paidPages[i] = NIL_GMM_PAGEID;
    }

    /*
     * We've seen this shared page for the first time?
     */
    if (pGlobalRegion->paidPages[idxPage] == NIL_GMM_PAGEID)
    {
        Log(("New shared page guest %RGp host %RHp\n", pPageDesc->GCPhys, pPageDesc->HCPhys));
        return gmmR0SharedModuleCheckPageFirstTime(pGMM, pGVM, pModule, idxRegion, idxPage, pPageDesc, pGlobalRegion);
    }

    /*
     * We've seen it before...
     */
    Log(("Replace existing page guest %RGp host %RHp id %#x -> id %#x\n",
         pPageDesc->GCPhys, pPageDesc->HCPhys, pPageDesc->idPage, pGlobalRegion->paidPages[idxPage]));
    Assert(pPageDesc->idPage != pGlobalRegion->paidPages[idxPage]);

    /*
     * Get the shared page source.
     */
    PGMMPAGE pPage = gmmR0GetPage(pGMM, pGlobalRegion->paidPages[idxPage]);
    AssertMsgReturn(pPage, ("idPage=%#x (idxRegion=%#x idxPage=%#x) #2\n", pPageDesc->idPage, idxRegion, idxPage),
                    VERR_PGM_PHYS_INVALID_PAGE_ID);

    if (pPage->Common.u2State != GMM_PAGE_STATE_SHARED)
    {
        /*
         * Page was freed at some point; invalidate this entry.
         */
        /** @todo this isn't really bullet proof. */
        Log(("Old shared page was freed -> create a new one\n"));
        pGlobalRegion->paidPages[idxPage] = NIL_GMM_PAGEID;
        return gmmR0SharedModuleCheckPageFirstTime(pGMM, pGVM, pModule, idxRegion, idxPage, pPageDesc, pGlobalRegion);
    }

    Log(("Replace existing page guest host %RHp -> %RHp\n", pPageDesc->HCPhys, ((uint64_t)pPage->Shared.pfn) << GUEST_PAGE_SHIFT));

    /*
     * Calculate the virtual address of the local page.
     */
    PGMMCHUNK pChunk = gmmR0GetChunk(pGMM, pPageDesc->idPage >> GMM_CHUNKID_SHIFT);
    AssertMsgReturn(pChunk, ("idPage=%#x (idxRegion=%#x idxPage=%#x) #4\n", pPageDesc->idPage, idxRegion, idxPage),
                    VERR_PGM_PHYS_INVALID_PAGE_ID);

    uint8_t *pbChunk;
    AssertMsgReturn(gmmR0IsChunkMapped(pGMM, pGVM, pChunk, (PRTR3PTR)&pbChunk),
                    ("idPage=%#x (idxRegion=%#x idxPage=%#x) #3\n", pPageDesc->idPage, idxRegion, idxPage),
                    VERR_PGM_PHYS_INVALID_PAGE_ID);
    uint8_t  *pbLocalPage = pbChunk + ((pPageDesc->idPage & GMM_PAGEID_IDX_MASK) << GUEST_PAGE_SHIFT);

    /*
     * Calculate the virtual address of the shared page.
     */
    pChunk = gmmR0GetChunk(pGMM, pGlobalRegion->paidPages[idxPage] >> GMM_CHUNKID_SHIFT);
    Assert(pChunk); /* can't fail as gmmR0GetPage succeeded. */

    /*
     * Get the virtual address of the physical page; map the chunk into the VM
     * process if not already done.
     */
    if (!gmmR0IsChunkMapped(pGMM, pGVM, pChunk, (PRTR3PTR)&pbChunk))
    {
        Log(("Map chunk into process!\n"));
        rc = gmmR0MapChunk(pGMM, pGVM, pChunk, false /*fRelaxedSem*/, (PRTR3PTR)&pbChunk);
        AssertRCReturn(rc, rc);
    }
    uint8_t *pbSharedPage = pbChunk + ((pGlobalRegion->paidPages[idxPage] & GMM_PAGEID_IDX_MASK) << GUEST_PAGE_SHIFT);

#ifdef VBOX_STRICT
    pPageDesc->u32StrictChecksum = RTCrc32(pbSharedPage, GUEST_PAGE_SIZE);
    uint32_t uChecksum = pPageDesc->u32StrictChecksum & UINT32_C(0x00003fff);
    AssertMsg(!uChecksum || uChecksum == pPage->Shared.u14Checksum || !pPage->Shared.u14Checksum,
              ("%#x vs %#x - idPage=%#x - %s %s\n", uChecksum, pPage->Shared.u14Checksum,
               pGlobalRegion->paidPages[idxPage], pModule->szName, pModule->szVersion));
#endif

    if (memcmp(pbSharedPage, pbLocalPage, GUEST_PAGE_SIZE))
    {
        Log(("Unexpected differences found between local and shared page; skip\n"));
        /* Signal to the caller that this one hasn't changed. */
        pPageDesc->idPage = NIL_GMM_PAGEID;
        return VINF_SUCCESS;
    }

    /*
     * Free the old local page.
     */
    GMMFREEPAGEDESC PageDesc;
    PageDesc.idPage = pPageDesc->idPage;
    rc = gmmR0FreePages(pGMM, pGVM, 1, &PageDesc, GMMACCOUNT_BASE);
    AssertRCReturn(rc, rc);

    gmmR0UseSharedPage(pGMM, pGVM, pPage);

    /*
     * Pass along the new physical address & page id.
     */
    pPageDesc->HCPhys = ((uint64_t)pPage->Shared.pfn) << GUEST_PAGE_SHIFT;
    pPageDesc->idPage = pGlobalRegion->paidPages[idxPage];

    return VINF_SUCCESS;
}


/**
 * RTAvlGCPtrDestroy callback.
 *
 * @returns 0 or VERR_GMM_INSTANCE.
 * @param   pNode       The node to destroy.
 * @param   pvArgs      Pointer to an argument packet.
 */
static DECLCALLBACK(int) gmmR0CleanupSharedModule(PAVLGCPTRNODECORE pNode, void *pvArgs)
{
    gmmR0ShModDeletePerVM(((GMMR0SHMODPERVMDTORARGS *)pvArgs)->pGMM,
                          ((GMMR0SHMODPERVMDTORARGS *)pvArgs)->pGVM,
                          (PGMMSHAREDMODULEPERVM)pNode,
                          false /*fRemove*/);
    return VINF_SUCCESS;
}


/**
 * Used by GMMR0CleanupVM to clean up shared modules.
 *
 * This is called without taking the GMM lock so that it can be yielded as
 * needed here.
 *
 * @param   pGMM        The GMM handle.
 * @param   pGVM        The global VM handle.
 */
static void gmmR0SharedModuleCleanup(PGMM pGMM, PGVM pGVM)
{
    gmmR0MutexAcquire(pGMM);
    GMM_CHECK_SANITY_UPON_ENTERING(pGMM);

    GMMR0SHMODPERVMDTORARGS Args;
    Args.pGVM = pGVM;
    Args.pGMM = pGMM;
    RTAvlGCPtrDestroy(&pGVM->gmm.s.pSharedModuleTree, gmmR0CleanupSharedModule, &Args);

    AssertMsg(pGVM->gmm.s.Stats.cShareableModules == 0, ("%d\n", pGVM->gmm.s.Stats.cShareableModules));
    pGVM->gmm.s.Stats.cShareableModules = 0;

    gmmR0MutexRelease(pGMM);
}

#endif /* VBOX_WITH_PAGE_SHARING */

/**
 * Removes all shared modules for the specified VM
 *
 * @returns VBox status code.
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   idCpu       The VCPU id.
 */
GMMR0DECL(int) GMMR0ResetSharedModules(PGVM pGVM, VMCPUID idCpu)
{
#ifdef VBOX_WITH_PAGE_SHARING
    /*
     * Validate input and get the basics.
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_GMM_INSTANCE);
    int rc = GVMMR0ValidateGVMandEMT(pGVM, idCpu);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Take the semaphore and do some more validations.
     */
    gmmR0MutexAcquire(pGMM);
    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {
        Log(("GMMR0ResetSharedModules\n"));
        GMMR0SHMODPERVMDTORARGS Args;
        Args.pGVM = pGVM;
        Args.pGMM = pGMM;
        RTAvlGCPtrDestroy(&pGVM->gmm.s.pSharedModuleTree, gmmR0CleanupSharedModule, &Args);
        pGVM->gmm.s.Stats.cShareableModules = 0;

        rc = VINF_SUCCESS;
        GMM_CHECK_SANITY_UPON_LEAVING(pGMM);
    }
    else
        rc = VERR_GMM_IS_NOT_SANE;

    gmmR0MutexRelease(pGMM);
    return rc;
#else
    RT_NOREF(pGVM, idCpu);
    return VERR_NOT_IMPLEMENTED;
#endif
}

#ifdef VBOX_WITH_PAGE_SHARING

/**
 * Tree enumeration callback for checking a shared module.
 */
static DECLCALLBACK(int) gmmR0CheckSharedModule(PAVLGCPTRNODECORE pNode, void *pvUser)
{
    GMMCHECKSHAREDMODULEINFO   *pArgs   = (GMMCHECKSHAREDMODULEINFO*)pvUser;
    PGMMSHAREDMODULEPERVM       pRecVM  = (PGMMSHAREDMODULEPERVM)pNode;
    PGMMSHAREDMODULE            pGblMod = pRecVM->pGlobalModule;

    Log(("gmmR0CheckSharedModule: check %s %s base=%RGv size=%x\n",
         pGblMod->szName, pGblMod->szVersion, pGblMod->Core.Key, pGblMod->cbModule));

    int rc = PGMR0SharedModuleCheck(pArgs->pGVM, pArgs->pGVM, pArgs->idCpu, pGblMod, pRecVM->aRegionsGCPtrs);
    if (RT_FAILURE(rc))
        return rc;
    return VINF_SUCCESS;
}

#endif /* VBOX_WITH_PAGE_SHARING */

/**
 * Check all shared modules for the specified VM.
 *
 * @returns VBox status code.
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   idCpu       The calling EMT number.
 * @thread  EMT(idCpu)
 */
GMMR0DECL(int) GMMR0CheckSharedModules(PGVM pGVM, VMCPUID idCpu)
{
#ifdef VBOX_WITH_PAGE_SHARING
    /*
     * Validate input and get the basics.
     */
    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_GMM_INSTANCE);
    int rc = GVMMR0ValidateGVMandEMT(pGVM, idCpu);
    if (RT_FAILURE(rc))
        return rc;

# ifndef DEBUG_sandervl
    /*
     * Take the semaphore and do some more validations.
     */
    gmmR0MutexAcquire(pGMM);
# endif
    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {
        /*
         * Walk the tree, checking each module.
         */
        Log(("GMMR0CheckSharedModules\n"));

        GMMCHECKSHAREDMODULEINFO Args;
        Args.pGVM     = pGVM;
        Args.idCpu    = idCpu;
        rc = RTAvlGCPtrDoWithAll(&pGVM->gmm.s.pSharedModuleTree, true /* fFromLeft */, gmmR0CheckSharedModule, &Args);

        Log(("GMMR0CheckSharedModules done (rc=%Rrc)!\n", rc));
        GMM_CHECK_SANITY_UPON_LEAVING(pGMM);
    }
    else
        rc = VERR_GMM_IS_NOT_SANE;

# ifndef DEBUG_sandervl
    gmmR0MutexRelease(pGMM);
# endif
    return rc;
#else
    RT_NOREF(pGVM, idCpu);
    return VERR_NOT_IMPLEMENTED;
#endif
}

#ifdef VBOX_STRICT

/**
 * Worker for GMMR0FindDuplicatePageReq.
 *
 * @returns true if duplicate, false if not.
 */
static bool gmmR0FindDupPageInChunk(PGMM pGMM, PGVM pGVM, PGMMCHUNK pChunk, uint8_t const *pbSourcePage)
{
    bool fFoundDuplicate = false;
    /* Only take chunks not mapped into this VM process; not entirely correct. */
    uint8_t *pbChunk;
    if (!gmmR0IsChunkMapped(pGMM, pGVM, pChunk, (PRTR3PTR)&pbChunk))
    {
        int rc = gmmR0MapChunk(pGMM, pGVM, pChunk, false /*fRelaxedSem*/, (PRTR3PTR)&pbChunk);
        if (RT_SUCCESS(rc))
        {
            /*
             * Look for duplicate pages
             */
            uintptr_t iPage = GMM_CHUNK_NUM_PAGES;
            while (iPage-- > 0)
            {
                if (GMM_PAGE_IS_PRIVATE(&pChunk->aPages[iPage]))
                {
                    uint8_t *pbDestPage = pbChunk + (iPage  << GUEST_PAGE_SHIFT);
                    if (!memcmp(pbSourcePage, pbDestPage, GUEST_PAGE_SIZE))
                    {
                        fFoundDuplicate = true;
                        break;
                    }
                }
            }
            gmmR0UnmapChunk(pGMM, pGVM, pChunk, false /*fRelaxedSem*/);
        }
    }
    return fFoundDuplicate;
}


/**
 * Find a duplicate of the specified page in other active VMs
 *
 * @returns VBox status code.
 * @param   pGVM        The global (ring-0) VM structure.
 * @param   pReq        Pointer to the request packet.
 */
GMMR0DECL(int) GMMR0FindDuplicatePageReq(PGVM pGVM, PGMMFINDDUPLICATEPAGEREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(*pReq), ("%#x != %#x\n", pReq->Hdr.cbReq, sizeof(*pReq)), VERR_INVALID_PARAMETER);

    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_GMM_INSTANCE);

    int rc = GVMMR0ValidateGVM(pGVM);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Take the semaphore and do some more validations.
     */
    rc = gmmR0MutexAcquire(pGMM);
    if (GMM_CHECK_SANITY_UPON_ENTERING(pGMM))
    {
        uint8_t  *pbChunk;
        PGMMCHUNK pChunk = gmmR0GetChunk(pGMM, pReq->idPage >> GMM_CHUNKID_SHIFT);
        if (pChunk)
        {
            if (gmmR0IsChunkMapped(pGMM, pGVM, pChunk, (PRTR3PTR)&pbChunk))
            {
                uint8_t *pbSourcePage = pbChunk + ((pReq->idPage & GMM_PAGEID_IDX_MASK) << GUEST_PAGE_SHIFT);
                PGMMPAGE pPage = gmmR0GetPage(pGMM, pReq->idPage);
                if (pPage)
                {
                    /*
                     * Walk the chunks
                     */
                    pReq->fDuplicate = false;
                    RTListForEach(&pGMM->ChunkList, pChunk, GMMCHUNK, ListNode)
                    {
                        if (gmmR0FindDupPageInChunk(pGMM, pGVM, pChunk, pbSourcePage))
                        {
                            pReq->fDuplicate = true;
                            break;
                        }
                    }
                }
                else
                {
                    AssertFailed();
                    rc = VERR_PGM_PHYS_INVALID_PAGE_ID;
                }
            }
            else
                AssertFailed();
        }
        else
            AssertFailed();
    }
    else
        rc = VERR_GMM_IS_NOT_SANE;

    gmmR0MutexRelease(pGMM);
    return rc;
}

#endif /* VBOX_STRICT */


/**
 * Retrieves the GMM statistics visible to the caller.
 *
 * @returns VBox status code.
 *
 * @param   pStats      Where to put the statistics.
 * @param   pSession    The current session.
 * @param   pGVM        The GVM to obtain statistics for. Optional.
 */
GMMR0DECL(int) GMMR0QueryStatistics(PGMMSTATS pStats, PSUPDRVSESSION pSession, PGVM pGVM)
{
    LogFlow(("GVMMR0QueryStatistics: pStats=%p pSession=%p pGVM=%p\n", pStats, pSession, pGVM));

    /*
     * Validate input.
     */
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pStats, VERR_INVALID_POINTER);
    pStats->cMaxPages = 0; /* (crash before taking the mutex...) */

    PGMM pGMM;
    GMM_GET_VALID_INSTANCE(pGMM, VERR_GMM_INSTANCE);

    /*
     * Validate the VM handle, if not NULL, and lock the GMM.
     */
    int rc;
    if (pGVM)
    {
        rc = GVMMR0ValidateGVM(pGVM);
        if (RT_FAILURE(rc))
            return rc;
    }

    rc = gmmR0MutexAcquire(pGMM);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Copy out the GMM statistics.
     */
    pStats->cMaxPages                   = pGMM->cMaxPages;
    pStats->cReservedPages              = pGMM->cReservedPages;
    pStats->cOverCommittedPages         = pGMM->cOverCommittedPages;
    pStats->cAllocatedPages             = pGMM->cAllocatedPages;
    pStats->cSharedPages                = pGMM->cSharedPages;
    pStats->cDuplicatePages             = pGMM->cDuplicatePages;
    pStats->cLeftBehindSharedPages      = pGMM->cLeftBehindSharedPages;
    pStats->cBalloonedPages             = pGMM->cBalloonedPages;
    pStats->cChunks                     = pGMM->cChunks;
    pStats->cFreedChunks                = pGMM->cFreedChunks;
    pStats->cShareableModules           = pGMM->cShareableModules;
    pStats->idFreeGeneration            = pGMM->idFreeGeneration;
    RT_ZERO(pStats->au64Reserved);

    /*
     * Copy out the VM statistics.
     */
    if (pGVM)
        pStats->VMStats = pGVM->gmm.s.Stats;
    else
        RT_ZERO(pStats->VMStats);

    gmmR0MutexRelease(pGMM);
    return rc;
}


/**
 * VMMR0 request wrapper for GMMR0QueryStatistics.
 *
 * @returns see GMMR0QueryStatistics.
 * @param   pGVM        The global (ring-0) VM structure. Optional.
 * @param   pReq        Pointer to the request packet.
 */
GMMR0DECL(int) GMMR0QueryStatisticsReq(PGVM pGVM, PGMMQUERYSTATISTICSSREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(*pReq), ("%#x != %#x\n", pReq->Hdr.cbReq, sizeof(*pReq)), VERR_INVALID_PARAMETER);

    return GMMR0QueryStatistics(&pReq->Stats, pReq->pSession, pGVM);
}


/**
 * Resets the specified GMM statistics.
 *
 * @returns VBox status code.
 *
 * @param   pStats      Which statistics to reset, that is, non-zero fields
 *                      indicates which to reset.
 * @param   pSession    The current session.
 * @param   pGVM        The GVM to reset statistics for. Optional.
 */
GMMR0DECL(int) GMMR0ResetStatistics(PCGMMSTATS pStats, PSUPDRVSESSION pSession, PGVM pGVM)
{
    NOREF(pStats); NOREF(pSession); NOREF(pGVM);
    /* Currently nothing we can reset at the moment. */
    return VINF_SUCCESS;
}


/**
 * VMMR0 request wrapper for GMMR0ResetStatistics.
 *
 * @returns see GMMR0ResetStatistics.
 * @param   pGVM        The global (ring-0) VM structure. Optional.
 * @param   pReq        Pointer to the request packet.
 */
GMMR0DECL(int) GMMR0ResetStatisticsReq(PGVM pGVM, PGMMRESETSTATISTICSSREQ pReq)
{
    /*
     * Validate input and pass it on.
     */
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(*pReq), ("%#x != %#x\n", pReq->Hdr.cbReq, sizeof(*pReq)), VERR_INVALID_PARAMETER);

    return GMMR0ResetStatistics(&pReq->Stats, pReq->pSession, pGVM);
}

