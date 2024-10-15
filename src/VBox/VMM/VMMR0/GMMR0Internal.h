/* $Id: GMMR0Internal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * GMM - The Global Memory Manager, Internal Header.
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

#ifndef VMM_INCLUDED_SRC_VMMR0_GMMR0Internal_h
#define VMM_INCLUDED_SRC_VMMR0_GMMR0Internal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/vmm/gmm.h>
#include <iprt/avl.h>


/**
 * Shared module registration info (per VM)
 */
typedef struct GMMSHAREDMODULEPERVM
{
    /** Tree node. */
    AVLGCPTRNODECORE            Core;
    /** Pointer to global shared module info. */
    PGMMSHAREDMODULE            pGlobalModule;
    /** Pointer to the region addresses.
     *
     * They can differe between VMs because of address space scrambling or
     * simply different loading order. */
    RTGCPTR64                   aRegionsGCPtrs[1];
} GMMSHAREDMODULEPERVM;
/** Pointer to a GMMSHAREDMODULEPERVM. */
typedef GMMSHAREDMODULEPERVM *PGMMSHAREDMODULEPERVM;


/** Pointer to a GMM allocation chunk. */
typedef struct GMMCHUNK *PGMMCHUNK;


/** The GMMCHUNK::cFree shift count employed by gmmR0SelectFreeSetList. */
#define GMM_CHUNK_FREE_SET_SHIFT    4
/** Index of the list containing completely unused chunks.
 * The code ASSUMES this is the last list. */
#define GMM_CHUNK_FREE_SET_UNUSED_LIST  (GMM_CHUNK_NUM_PAGES >> GMM_CHUNK_FREE_SET_SHIFT)

/**
 * A set of free chunks.
 */
typedef struct GMMCHUNKFREESET
{
    /** The number of free pages in the set. */
    uint64_t            cFreePages;
    /** The generation ID for the set.  This is incremented whenever
     *  something is linked or unlinked from this set. */
    uint64_t            idGeneration;
    /** Chunks ordered by increasing number of free pages.
     *  In the final list the chunks are completely unused. */
    PGMMCHUNK           apLists[GMM_CHUNK_FREE_SET_UNUSED_LIST + 1];
} GMMCHUNKFREESET;


/**
 * A per-VM allocation chunk lookup TLB entry (for GMMR0PageIdToVirt).
 */
typedef struct GMMPERVMCHUNKTLBE
{
    /** The GMM::idFreeGeneration value this is valid for. */
    uint64_t            idGeneration;
    /** The chunk. */
    PGMMCHUNK           pChunk;
} GMMPERVMCHUNKTLBE;
/** Poitner to a per-VM allocation chunk TLB entry. */
typedef GMMPERVMCHUNKTLBE *PGMMPERVMCHUNKTLBE;

/** The number of entries in the allocation chunk lookup TLB. */
#define GMMPERVM_CHUNKTLB_ENTRIES           32
/** Gets the TLB entry index for the given Chunk ID. */
#define GMMPERVM_CHUNKTLB_IDX(a_idChunk)    ( (a_idChunk) & (GMMPERVM_CHUNKTLB_ENTRIES - 1) )


/**
 * The per-VM GMM data.
 */
typedef struct GMMPERVM
{
    /** Free set for use in bound mode. */
    GMMCHUNKFREESET     Private;
    /** The VM statistics. */
    GMMVMSTATS          Stats;
    /** Shared module tree (per-vm). */
    PAVLGCPTRNODECORE   pSharedModuleTree;
    /** Hints at the last chunk we allocated some memory from. */
    uint32_t            idLastChunkHint;
    uint32_t            u32Padding;

    /** Spinlock protecting the chunk lookup TLB. */
    RTSPINLOCK          hChunkTlbSpinLock;
    /** The chunk lookup TLB used by GMMR0PageIdToVirt. */
    GMMPERVMCHUNKTLBE   aChunkTlbEntries[GMMPERVM_CHUNKTLB_ENTRIES];
} GMMPERVM;
/** Pointer to the per-VM GMM data. */
typedef GMMPERVM *PGMMPERVM;

#endif /* !VMM_INCLUDED_SRC_VMMR0_GMMR0Internal_h */

