/* $Id: MMInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * MM - Internal header file.
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

#ifndef VMM_INCLUDED_SRC_include_MMInternal_h
#define VMM_INCLUDED_SRC_include_MMInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <VBox/types.h>
#include <VBox/sup.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/pdmcritsect.h>
#include <iprt/assert.h>
#include <iprt/avl.h>
#include <iprt/critsect.h>



/** @defgroup grp_mm_int   Internals
 * @ingroup grp_mm
 * @internal
 * @{
 */


/** @name MMR3Heap - VM Ring-3 Heap Internals
 * @{
 */

/** @def MMR3HEAP_SIZE_ALIGNMENT
 * The allocation size alignment of the MMR3Heap.
 */
#define MMR3HEAP_SIZE_ALIGNMENT     16

/** @def MMR3HEAP_WITH_STATISTICS
 * Enable MMR3Heap statistics.
 */
#if !defined(MMR3HEAP_WITH_STATISTICS) && defined(VBOX_WITH_STATISTICS)
# define MMR3HEAP_WITH_STATISTICS
#endif

/**
 * Heap statistics record.
 * There is one global and one per allocation tag.
 */
typedef struct MMHEAPSTAT
{
    /** Core avl node, key is the tag. */
    AVLULNODECORE           Core;
    /** Pointer to the heap the memory belongs to. */
    struct MMHEAP          *pHeap;
#ifdef MMR3HEAP_WITH_STATISTICS
    /** Number of bytes currently allocated. */
    size_t                  cbCurAllocated;
    /** Number of allocation. */
    uint64_t                cAllocations;
    /** Number of reallocations. */
    uint64_t                cReallocations;
    /** Number of frees. */
    uint64_t                cFrees;
    /** Failures. */
    uint64_t                cFailures;
    /** Number of bytes allocated (sum). */
    uint64_t                cbAllocated;
    /** Number of bytes freed. */
    uint64_t                cbFreed;
#endif
} MMHEAPSTAT;
#if defined(MMR3HEAP_WITH_STATISTICS) && defined(IN_RING3)
AssertCompileMemberAlignment(MMHEAPSTAT, cAllocations, 8);
AssertCompileSizeAlignment(MMHEAPSTAT, 8);
#endif
/** Pointer to heap statistics record. */
typedef MMHEAPSTAT *PMMHEAPSTAT;




/**
 * Additional heap block header for relating allocations to the VM.
 */
typedef struct MMHEAPHDR
{
    /** Pointer to the next record. */
    struct MMHEAPHDR       *pNext;
    /** Pointer to the previous record. */
    struct MMHEAPHDR       *pPrev;
    /** Pointer to the heap statistics record.
     * (Where the a PVM can be found.) */
    PMMHEAPSTAT             pStat;
    /** Size of the allocation (including this header). */
    size_t                  cbSize;
} MMHEAPHDR;
/** Pointer to MM heap header. */
typedef MMHEAPHDR *PMMHEAPHDR;


/** MM Heap structure. */
typedef struct MMHEAP
{
    /** Lock protecting the heap. */
    RTCRITSECT              Lock;
    /** Heap block list head. */
    PMMHEAPHDR              pHead;
    /** Heap block list tail. */
    PMMHEAPHDR              pTail;
    /** Heap per tag statistics tree. */
    PAVLULNODECORE          pStatTree;
    /** The VM handle. */
    PUVM                    pUVM;
    /** Heap global statistics. */
    MMHEAPSTAT              Stat;
} MMHEAP;
/** Pointer to MM Heap structure. */
typedef MMHEAP *PMMHEAP;

/** @} */

/**
 * MM Data (part of VM)
 */
typedef struct MM
{
    /** Set if MMR3InitPaging has been called. */
    bool                        fDoneMMR3InitPaging;
    /** Padding. */
    bool                        afPadding1[7];

    /** Size of the base RAM in bytes. (The CFGM RamSize value.) */
    uint64_t                    cbRamBase;
    /** Number of bytes of RAM above 4GB, starting at address 4GB.  */
    uint64_t                    cbRamAbove4GB;
    /** Size of the below 4GB RAM hole. */
    uint32_t                    cbRamHole;
    /** Number of bytes of RAM below 4GB, starting at address 0.  */
    uint32_t                    cbRamBelow4GB;
    /** The number of base RAM pages that PGM has reserved (GMM).
     * @remarks Shadow ROMs will be counted twice (RAM+ROM), so it won't be 1:1 with
     *          what the guest sees. */
    uint64_t                    cBasePages;
    /** The number of handy pages that PGM has reserved (GMM).
     * These are kept out of cBasePages and thus out of the saved state. */
    uint32_t                    cHandyPages;
    /** The number of shadow pages PGM has reserved (GMM). */
    uint32_t                    cShadowPages;
    /** The number of fixed pages we've reserved (GMM). */
    uint32_t                    cFixedPages;
    /** Padding. */
    uint32_t                    u32Padding2;
} MM;
/** Pointer to MM Data (part of VM). */
typedef MM *PMM;


/**
 * MM data kept in the UVM.
 */
typedef struct MMUSERPERVM
{
    /** Pointer to the MM R3 Heap. */
    R3PTRTYPE(PMMHEAP)          pHeap;
} MMUSERPERVM;
/** Pointer to the MM data kept in the UVM. */
typedef MMUSERPERVM *PMMUSERPERVM;


RT_C_DECLS_BEGIN

int  mmR3UpdateReservation(PVM pVM);

int  mmR3HeapCreateU(PUVM pUVM, PMMHEAP *ppHeap);
void mmR3HeapDestroy(PMMHEAP pHeap);

const char *mmGetTagName(MMTAG enmTag);

RT_C_DECLS_END

/** @} */

#endif /* !VMM_INCLUDED_SRC_include_MMInternal_h */

