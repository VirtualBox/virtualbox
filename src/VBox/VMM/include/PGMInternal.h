/* $Id: PGMInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PGM - Internal header file.
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

#ifndef VMM_INCLUDED_SRC_include_PGMInternal_h
#define VMM_INCLUDED_SRC_include_PGMInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <VBox/types.h>
#include <VBox/err.h>
#include <VBox/dbg.h>
#include <VBox/vmm/stam.h>
#include <VBox/param.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/pdmcritsect.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/dis.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/log.h>
#include <VBox/vmm/gmm.h>
#include <VBox/vmm/hm.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/avl.h>
#include <iprt/critsect.h>
#include <iprt/list-off32.h>
#include <iprt/sha.h>
#include <iprt/cpp/hardavlrange.h>



/** @defgroup grp_pgm_int   Internals
 * @ingroup grp_pgm
 * @internal
 * @{
 */


/** @name PGM Compile Time Config
 * @{
 */

/**
 * Check and skip global PDEs for non-global flushes
 */
#define PGM_SKIP_GLOBAL_PAGEDIRS_ON_NONGLOBAL_FLUSH

/**
 * Optimization for PAE page tables that are modified often
 */
#ifndef VBOX_VMM_TARGET_ARMV8
# define PGMPOOL_WITH_OPTIMIZED_DIRTY_PT
#endif

/**
 * Large page support enabled only on 64 bits hosts; applies to nested paging only.
 */
#define PGM_WITH_LARGE_PAGES

/**
 * Enables optimizations for MMIO handlers that exploits X86_TRAP_PF_RSVD and
 * VMX_EXIT_EPT_MISCONFIG.
 */
#define PGM_WITH_MMIO_OPTIMIZATIONS

/**
 * Sync N pages instead of a whole page table
 */
#define PGM_SYNC_N_PAGES

/**
 * Number of pages to sync during a page fault
 *
 * When PGMPOOL_WITH_GCPHYS_TRACKING is enabled using high values here
 * causes a lot of unnecessary extents and also is slower than taking more \#PFs.
 *
 * Note that \#PFs are much more expensive in the VT-x/AMD-V case due to
 * world switch overhead, so let's sync more.
 */
#ifdef IN_RING0
/* Chose 32 based on the compile test in @bugref{4219}; 64 shows worse stats.
 * 32 again shows better results than 16; slightly more overhead in the \#PF handler,
 * but ~5% fewer faults.
 */
# define PGM_SYNC_NR_PAGES              32
#else
# define PGM_SYNC_NR_PAGES              8
#endif

/** Maximum number of RAM ranges.
 * @note This can be increased to 4096 (at least when targeting x86). */
#define PGM_MAX_RAM_RANGES              3072

/** Maximum pages per RAM range.
 *
 * The PGMRAMRANGE structures for the high memory can get very big. There
 * used to be some limitations on SUPR3PageAllocEx allocation sizes, so
 * traditionally we limited this to 16MB chunks.  These days we do ~64 MB
 * chunks each covering 16GB of guest RAM, making sure each range is a
 * multiple of 1GB to enable eager hosts to use 1GB pages for NEM mode.
 *
 * See also pgmPhysMmio2CalcChunkCount.
 */
#define PGM_MAX_PAGES_PER_RAM_RANGE     _4M
#if defined(X86_PD_PAE_SHIFT) && defined(AssertCompile)
AssertCompile(RT_ALIGN_32(PGM_MAX_PAGES_PER_RAM_RANGE, X86_PD_PAE_SHIFT - X86_PAGE_SHIFT)); /* NEM large page requirement: 1GB pages. */
#endif

/** The maximum number of MMIO2 ranges. */
#define PGM_MAX_MMIO2_RANGES            32
/** The maximum number of pages in a MMIO2 PCI region.
 *
 * The memory for a MMIO2 PCI region is a single chunk of host virtual memory,
 * but may be handled internally by PGM as a set of multiple MMIO2/RAM ranges,
 * since PGM_MAX_PAGES_PER_RAM_RANGE is currently lower than this value (4 GiB
 * vs 16 GiB).
 */
#define PGM_MAX_PAGES_PER_MMIO2_REGION  _16M

/** Maximum number of ROM ranges. */
#define PGM_MAX_ROM_RANGES              16
/** The maximum pages per ROM range.
 * Currently 512K pages, or 2GB with 4K pages.  */
#define PGM_MAX_PAGES_PER_ROM_RANGE     _512K
AssertCompile(PGM_MAX_PAGES_PER_ROM_RANGE <= PGM_MAX_PAGES_PER_RAM_RANGE);

/**
 * Number of PGMPhysRead/Write cache entries (must be <= sizeof(uint64_t))
 */
#define PGM_MAX_PHYSCACHE_ENTRIES       64
#define PGM_MAX_PHYSCACHE_ENTRIES_MASK  (PGM_MAX_PHYSCACHE_ENTRIES-1)


/** @def PGMPOOL_CFG_MAX_GROW
 * The maximum number of pages to add to the pool in one go.
 */
#define PGMPOOL_CFG_MAX_GROW            (_2M >> GUEST_PAGE_SHIFT) /** @todo or HOST_PAGE_SHIFT ? */

/** @def VBOX_STRICT_PGM_HANDLER_VIRTUAL
 * Enables some extra assertions for virtual handlers (mainly phys2virt related).
 */
#ifdef VBOX_STRICT
# define VBOX_STRICT_PGM_HANDLER_VIRTUAL
#endif

/** @def VBOX_WITH_NEW_LAZY_PAGE_ALLOC
 * Enables the experimental lazy page allocation code.
 * @todo Problems with zero page tracking, see \@bugref{10509}.  */
#if defined(DOXYGEN_RUNNING)
# define VBOX_WITH_NEW_LAZY_PAGE_ALLOC
#endif

/** @def PGM_WITH_PAGE_ZEROING_DETECTION
 * Enables the code for detecting guest zeroing page in the ZERO state and
 * tries to avoid allocating them in response.
 * This kind of relies upon VBOX_WITH_NEW_LAZY_PAGE_ALLOC to work atm.  */
#if defined(DOXYGEN_RUNNING)
# define PGM_WITH_PAGE_ZEROING_DETECTION
#endif

/** @def VBOX_WITH_REAL_WRITE_MONITORED_PAGES
 * Enables real write monitoring of pages, i.e. mapping them read-only and
 * only making them writable when getting a write access \#PF. */
#define VBOX_WITH_REAL_WRITE_MONITORED_PAGES

/** @def VBOX_WITH_PGM_NEM_MODE
 * Enabled the NEM memory management mode in PGM.  See PGM::fNemMode for
 * details. */
#ifdef DOXYGEN_RUNNING
# define VBOX_WITH_PGM_NEM_MODE
#endif

/** @} */


/** @name PDPT and PML4 flags.
 * These are placed in the three bits available for system programs in
 * the PDPT and PML4 entries.
 * @{ */
/** The entry is a permanent one and it's must always be present.
 * Never free such an entry. */
#define PGM_PLXFLAGS_PERMANENT          RT_BIT_64(10)
/** PGM specific bits in PML4 entries. */
#define PGM_PML4_FLAGS                  0
/** PGM specific bits in PDPT entries. */
#define PGM_PDPT_FLAGS                  (PGM_PLXFLAGS_PERMANENT)
/** @} */

/** @name Page directory flags.
 * These are placed in the three bits available for system programs in
 * the page directory entries.
 * @{ */
/** Indicates the original entry was a big page.
 * @remarks This is currently only used for statistics and can be recycled.  */
#define PGM_PDFLAGS_BIG_PAGE            RT_BIT_64(9)
/** Made read-only to facilitate dirty bit tracking. */
#define PGM_PDFLAGS_TRACK_DIRTY         RT_BIT_64(11)
/** @} */

/** @name Page flags.
 * These are placed in the three bits available for system programs in
 * the page entries.
 * @{ */
/** Made read-only to facilitate dirty bit tracking. */
#define PGM_PTFLAGS_TRACK_DIRTY         RT_BIT_64(9)
/** @} */

/** @name Defines used to indicate the shadow and guest paging in the templates.
 * @{ */
#define PGM_TYPE_REAL                   1
#define PGM_TYPE_PROT                   2
#define PGM_TYPE_32BIT                  3
#define PGM_TYPE_PAE                    4
#define PGM_TYPE_AMD64                  5
#define PGM_TYPE_NESTED_32BIT           6
#define PGM_TYPE_NESTED_PAE             7
#define PGM_TYPE_NESTED_AMD64           8
#define PGM_TYPE_EPT                    9
#define PGM_TYPE_NONE                   10 /**< Dummy shadow paging mode for NEM. */
#define PGM_TYPE_END                    (PGM_TYPE_NONE + 1)
#define PGM_TYPE_FIRST_SHADOW           PGM_TYPE_32BIT /**< The first type used by shadow paging. */
/** @} */

/** @name Defines used to indicate the second-level
 * address translation (SLAT) modes in the templates.
 * @{ */
#define PGM_SLAT_TYPE_DIRECT            (PGM_TYPE_END + 1)
#define PGM_SLAT_TYPE_EPT               (PGM_TYPE_END + 2)
#define PGM_SLAT_TYPE_32BIT             (PGM_TYPE_END + 3)
#define PGM_SLAT_TYPE_PAE               (PGM_TYPE_END + 4)
#define PGM_SLAT_TYPE_AMD64             (PGM_TYPE_END + 5)
/** @} */

/** Macro for checking if the guest is using paging.
 * @param   uGstType   PGM_TYPE_*
 * @param   uShwType   PGM_TYPE_*
 * @remark  ASSUMES certain order of the PGM_TYPE_* values.
 */
#define PGM_WITH_PAGING(uGstType, uShwType)  \
    (   (uGstType) >= PGM_TYPE_32BIT \
     && (uShwType) < PGM_TYPE_NESTED_32BIT)

/** Macro for checking if the guest supports the NX bit.
 * @param   uGstType   PGM_TYPE_*
 * @param   uShwType   PGM_TYPE_*
 * @remark  ASSUMES certain order of the PGM_TYPE_* values.
 */
#define PGM_WITH_NX(uGstType, uShwType)  \
    (   (uGstType) >= PGM_TYPE_PAE \
     && (uShwType) < PGM_TYPE_NESTED_32BIT)

/** Macro for checking for nested.
 * @param   uType   PGM_TYPE_*
 */
#define PGM_TYPE_IS_NESTED(uType) \
     (   (uType) == PGM_TYPE_NESTED_32BIT \
      || (uType) == PGM_TYPE_NESTED_PAE \
      || (uType) == PGM_TYPE_NESTED_AMD64)

/** Macro for checking for nested or EPT.
 * @param   uType   PGM_TYPE_*
 */
#define PGM_TYPE_IS_NESTED_OR_EPT(uType) \
      (   (uType) == PGM_TYPE_NESTED_32BIT \
       || (uType) == PGM_TYPE_NESTED_PAE \
       || (uType) == PGM_TYPE_NESTED_AMD64 \
       || (uType) == PGM_TYPE_EPT)



/** @def PGM_HCPHYS_2_PTR
 * Maps a HC physical page pool address to a virtual address.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   HCPhys      The HC physical address to map to a virtual one.
 * @param   ppv         Where to store the virtual address. No need to cast
 *                      this.
 *
 * @remark  There is no need to assert on the result.
 */
#define PGM_HCPHYS_2_PTR(pVM, pVCpu, HCPhys, ppv) pgmPoolHCPhys2Ptr(pVM, HCPhys, (void **)(ppv))

/** @def PGM_GCPHYS_2_PTR_V2
 * Maps a GC physical page address to a virtual address.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 * @param   pVCpu   The cross context virtual CPU structure of the calling EMT.
 * @param   GCPhys  The GC physical address to map to a virtual one.
 * @param   ppv     Where to store the virtual address. No need to cast this.
 *
 * @remark  Use with care as we don't have so much dynamic mapping space in
 *          ring-0 on 32-bit darwin and in RC.
 * @remark  There is no need to assert on the result.
 */
#define PGM_GCPHYS_2_PTR_V2(pVM, pVCpu, GCPhys, ppv) \
    pgmPhysGCPhys2R3Ptr(pVM, GCPhys, (PRTR3PTR)(ppv)) /** @todo this isn't asserting! */

/** @def PGM_GCPHYS_2_PTR
 * Maps a GC physical page address to a virtual address.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 * @param   GCPhys  The GC physical address to map to a virtual one.
 * @param   ppv     Where to store the virtual address. No need to cast this.
 *
 * @remark  Use with care as we don't have so much dynamic mapping space in
 *          ring-0 on 32-bit darwin and in RC.
 * @remark  There is no need to assert on the result.
 */
#define PGM_GCPHYS_2_PTR(pVM, GCPhys, ppv) PGM_GCPHYS_2_PTR_V2(pVM, VMMGetCpu(pVM), GCPhys, ppv)

/** @def PGM_GCPHYS_2_PTR_BY_VMCPU
 * Maps a GC physical page address to a virtual address.
 *
 * @returns VBox status code.
 * @param   pVCpu   The cross context virtual CPU structure of the calling EMT.
 * @param   GCPhys  The GC physical address to map to a virtual one.
 * @param   ppv     Where to store the virtual address. No need to cast this.
 *
 * @remark  Use with care as we don't have so much dynamic mapping space in
 *          ring-0 on 32-bit darwin and in RC.
 * @remark  There is no need to assert on the result.
 */
#define PGM_GCPHYS_2_PTR_BY_VMCPU(pVCpu, GCPhys, ppv) PGM_GCPHYS_2_PTR_V2((pVCpu)->CTX_SUFF(pVM), pVCpu, GCPhys, ppv)

/** @def PGM_GCPHYS_2_PTR_EX
 * Maps a unaligned GC physical page address to a virtual address.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 * @param   GCPhys  The GC physical address to map to a virtual one.
 * @param   ppv     Where to store the virtual address. No need to cast this.
 *
 * @remark  Use with care as we don't have so much dynamic mapping space in
 *          ring-0 on 32-bit darwin and in RC.
 * @remark  There is no need to assert on the result.
 */
#define PGM_GCPHYS_2_PTR_EX(pVM, GCPhys, ppv) \
    pgmPhysGCPhys2R3Ptr(pVM, GCPhys, (PRTR3PTR)(ppv)) /** @todo this isn't asserting! */

/** @def PGM_DYNMAP_UNUSED_HINT
 * Hints to the dynamic mapping code in RC and R0/darwin that the specified page
 * is no longer used.
 *
 * For best effect only apply this to the page that was mapped most recently.
 *
 * @param   pVCpu   The cross context virtual CPU structure of the calling EMT.
 * @param   pvPage  The pool page.
 */
#define PGM_DYNMAP_UNUSED_HINT(pVCpu, pvPage)  do {} while (0)

/** @def PGM_DYNMAP_UNUSED_HINT_VM
 * Hints to the dynamic mapping code in RC and R0/darwin that the specified page
 * is no longer used.
 *
 * For best effect only apply this to the page that was mapped most recently.
 *
 * @param   pVM     The cross context VM structure.
 * @param   pvPage  The pool page.
 */
#define PGM_DYNMAP_UNUSED_HINT_VM(pVM, pvPage)  PGM_DYNMAP_UNUSED_HINT(VMMGetCpu(pVM), pvPage)


/** @def PGM_INVL_PG
 * Invalidates a page.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCVirt      The virtual address of the page to invalidate.
 */
#ifdef IN_RING0
# define PGM_INVL_PG(pVCpu, GCVirt)             HMInvalidatePage(pVCpu, (RTGCPTR)(GCVirt))
#elif defined(IN_RING3)
# define PGM_INVL_PG(pVCpu, GCVirt)             HMInvalidatePage(pVCpu, (RTGCPTR)(GCVirt))
#else
# error "Not IN_RING0 or IN_RING3!"
#endif

/** @def PGM_INVL_PG_ALL_VCPU
 * Invalidates a page on all VCPUs
 *
 * @param   pVM         The cross context VM structure.
 * @param   GCVirt      The virtual address of the page to invalidate.
 */
#if defined(VBOX_VMM_TARGET_ARMV8)
# define PGM_INVL_PG_ALL_VCPU(pVM, GCVirt)      do { } while(0)
#else
# define PGM_INVL_PG_ALL_VCPU(pVM, GCVirt)      HMInvalidatePageOnAllVCpus(pVM, (RTGCPTR)(GCVirt))
#endif

/** @def PGM_INVL_BIG_PG
 * Invalidates a 4MB page directory entry.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCVirt      The virtual address within the page directory to invalidate.
 */
#if defined(VBOX_VMM_TARGET_ARMV8)
# define PGM_INVL_BIG_PG(pVCpu, GCVirt)         do { } while(0)
#else
# define PGM_INVL_BIG_PG(pVCpu, GCVirt)         HMFlushTlb(pVCpu)
#endif

/** @def PGM_INVL_VCPU_TLBS()
 * Invalidates the TLBs of the specified VCPU
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 */
#if defined(VBOX_VMM_TARGET_ARMV8)
# define PGM_INVL_VCPU_TLBS(pVCpu)             do { } while(0)
#else
# define PGM_INVL_VCPU_TLBS(pVCpu)             HMFlushTlb(pVCpu)
#endif

/** @def PGM_INVL_ALL_VCPU_TLBS()
 * Invalidates the TLBs of all VCPUs
 *
 * @param   pVM         The cross context VM structure.
 */
#if defined(VBOX_VMM_TARGET_ARMV8)
# define PGM_INVL_ALL_VCPU_TLBS(pVM)            do { } while(0)
#else
# define PGM_INVL_ALL_VCPU_TLBS(pVM)            HMFlushTlbOnAllVCpus(pVM)
#endif


/** @name Safer Shadow PAE PT/PTE
 * For helping avoid misinterpreting invalid PAE/AMD64 page table entries as
 * present.
 *
 * @{
 */
#if 1
/**
 * For making sure that u1Present and X86_PTE_P checks doesn't mistake
 * invalid entries for present.
 * @sa X86PTEPAE.
 */
typedef union PGMSHWPTEPAE
{
    /** Unsigned integer view */
    X86PGPAEUINT    uCareful;
    /* Not other views. */
} PGMSHWPTEPAE;

# define PGMSHWPTEPAE_IS_P(Pte)                 ( ((Pte).uCareful & (X86_PTE_P | X86_PTE_PAE_MBZ_MASK_NX)) == X86_PTE_P )
# define PGMSHWPTEPAE_IS_RW(Pte)                ( !!((Pte).uCareful & X86_PTE_RW))
# define PGMSHWPTEPAE_IS_US(Pte)                ( !!((Pte).uCareful & X86_PTE_US))
# define PGMSHWPTEPAE_IS_A(Pte)                 ( !!((Pte).uCareful & X86_PTE_A))
# define PGMSHWPTEPAE_IS_D(Pte)                 ( !!((Pte).uCareful & X86_PTE_D))
# define PGMSHWPTEPAE_IS_TRACK_DIRTY(Pte)       ( !!((Pte).uCareful & PGM_PTFLAGS_TRACK_DIRTY) )
# define PGMSHWPTEPAE_IS_P_RW(Pte)              ( ((Pte).uCareful & (X86_PTE_P | X86_PTE_RW | X86_PTE_PAE_MBZ_MASK_NX)) == (X86_PTE_P | X86_PTE_RW) )
# define PGMSHWPTEPAE_GET_LOG(Pte)              ( (Pte).uCareful )
# define PGMSHWPTEPAE_GET_HCPHYS(Pte)           ( (Pte).uCareful & X86_PTE_PAE_PG_MASK )
# define PGMSHWPTEPAE_GET_U(Pte)                ( (Pte).uCareful ) /**< Use with care. */
# define PGMSHWPTEPAE_SET(Pte, uVal)            do { (Pte).uCareful = (uVal); } while (0)
# define PGMSHWPTEPAE_SET2(Pte, Pte2)           do { (Pte).uCareful = (Pte2).uCareful; } while (0)
# define PGMSHWPTEPAE_ATOMIC_SET(Pte, uVal)     do { ASMAtomicWriteU64(&(Pte).uCareful, (uVal)); } while (0)
# define PGMSHWPTEPAE_ATOMIC_SET2(Pte, Pte2)    do { ASMAtomicWriteU64(&(Pte).uCareful, (Pte2).uCareful); } while (0)
# define PGMSHWPTEPAE_SET_RO(Pte)               do { (Pte).uCareful &= ~(X86PGPAEUINT)X86_PTE_RW; } while (0)
# define PGMSHWPTEPAE_SET_RW(Pte)               do { (Pte).uCareful |= X86_PTE_RW; } while (0)

/**
 * For making sure that u1Present and X86_PTE_P checks doesn't mistake
 * invalid entries for present.
 * @sa X86PTPAE.
 */
typedef struct PGMSHWPTPAE
{
    PGMSHWPTEPAE  a[X86_PG_PAE_ENTRIES];
} PGMSHWPTPAE;

#else
typedef X86PTEPAE           PGMSHWPTEPAE;
typedef X86PTPAE            PGMSHWPTPAE;
# define PGMSHWPTEPAE_IS_P(Pte)                 ( (Pte).n.u1Present )
# define PGMSHWPTEPAE_IS_RW(Pte)                ( (Pte).n.u1Write )
# define PGMSHWPTEPAE_IS_US(Pte)                ( (Pte).n.u1User )
# define PGMSHWPTEPAE_IS_A(Pte)                 ( (Pte).n.u1Accessed )
# define PGMSHWPTEPAE_IS_D(Pte)                 ( (Pte).n.u1Dirty )
# define PGMSHWPTEPAE_IS_TRACK_DIRTY(Pte)       ( !!((Pte).u & PGM_PTFLAGS_TRACK_DIRTY) )
# define PGMSHWPTEPAE_IS_P_RW(Pte)              ( ((Pte).u & (X86_PTE_P | X86_PTE_RW)) == (X86_PTE_P | X86_PTE_RW) )
# define PGMSHWPTEPAE_GET_LOG(Pte)              ( (Pte).u )
# define PGMSHWPTEPAE_GET_HCPHYS(Pte)           ( (Pte).u & X86_PTE_PAE_PG_MASK )
# define PGMSHWPTEPAE_GET_U(Pte)                ( (Pte).u ) /**< Use with care. */
# define PGMSHWPTEPAE_SET(Pte, uVal)            do { (Pte).u = (uVal); } while (0)
# define PGMSHWPTEPAE_SET2(Pte, Pte2)           do { (Pte).u = (Pte2).u; } while (0)
# define PGMSHWPTEPAE_ATOMIC_SET(Pte, uVal)     do { ASMAtomicWriteU64(&(Pte).u, (uVal)); } while (0)
# define PGMSHWPTEPAE_ATOMIC_SET2(Pte, Pte2)    do { ASMAtomicWriteU64(&(Pte).u, (Pte2).u); } while (0)
# define PGMSHWPTEPAE_SET_RO(Pte)               do { (Pte).u &= ~(X86PGPAEUINT)X86_PTE_RW; } while (0)
# define PGMSHWPTEPAE_SET_RW(Pte)               do { (Pte).u |= X86_PTE_RW; } while (0)

#endif

/** Pointer to a shadow PAE PTE. */
typedef PGMSHWPTEPAE       *PPGMSHWPTEPAE;
/** Pointer to a const shadow PAE PTE. */
typedef PGMSHWPTEPAE const *PCPGMSHWPTEPAE;

/** Pointer to a shadow PAE page table. */
typedef PGMSHWPTPAE        *PPGMSHWPTPAE;
/** Pointer to a const shadow PAE page table. */
typedef PGMSHWPTPAE const  *PCPGMSHWPTPAE;
/** @} */


/** The physical access handler type handle count (power of two). */
#define PGMPHYSHANDLERTYPE_COUNT        0x20
/** Mask for getting the array index from an access handler type handle.
 * The other handle bits are random and non-zero to avoid mixups due to zero
 * initialized fields. */
#define PGMPHYSHANDLERTYPE_IDX_MASK     0x1f

/**
 * Physical page access handler type registration, ring-0 part.
 */
typedef struct PGMPHYSHANDLERTYPEINTR0
{
    /** The handle value for verfication.  */
    PGMPHYSHANDLERTYPE                  hType;
    /** The kind of accesses we're handling. */
    PGMPHYSHANDLERKIND                  enmKind;
    /** The PGM_PAGE_HNDL_PHYS_STATE_XXX value corresponding to enmKind. */
    uint8_t                             uState;
    /** Whether to keep the PGM lock when calling the handler.
     * @sa PGMPHYSHANDLER_F_KEEP_PGM_LOCK  */
    bool                                fKeepPgmLock;
    /** Set if this is registered by a device instance and uUser should be
     * translated from a device instance ID to a pointer.
     * @sa PGMPHYSHANDLER_F_R0_DEVINS_IDX  */
    bool                                fRing0DevInsIdx;
    /** See PGMPHYSHANDLER_F_NOT_IN_HM. */
    bool                                fNotInHm : 1;
    /** Pointer to the ring-0 callback function. */
    R0PTRTYPE(PFNPGMPHYSHANDLER)        pfnHandler;
    /** Pointer to the ring-0 callback function for \#PFs, can be NULL. */
    R0PTRTYPE(PFNPGMRZPHYSPFHANDLER)    pfnPfHandler;
    /** Description / Name. For easing debugging. */
    R0PTRTYPE(const char *)             pszDesc;
} PGMPHYSHANDLERTYPEINTR0;
/** Pointer to a physical access handler type registration. */
typedef PGMPHYSHANDLERTYPEINTR0 *PPGMPHYSHANDLERTYPEINTR0;

/**
 * Physical page access handler type registration, shared/ring-3 part.
 */
typedef struct PGMPHYSHANDLERTYPEINTR3
{
    /** The handle value for verfication.  */
    PGMPHYSHANDLERTYPE                  hType;
    /** The kind of accesses we're handling. */
    PGMPHYSHANDLERKIND                  enmKind;
    /** The PGM_PAGE_HNDL_PHYS_STATE_XXX value corresponding to enmKind. */
    uint8_t                             uState;
    /** Whether to keep the PGM lock when calling the handler.
     * @sa PGMPHYSHANDLER_F_KEEP_PGM_LOCK  */
    bool                                fKeepPgmLock;
    /** Set if this is registered by a device instance and uUser should be
     * translated from a device instance ID to a pointer.
     * @sa PGMPHYSHANDLER_F_R0_DEVINS_IDX  */
    bool                                fRing0DevInsIdx;
    /** Set by ring-0 if the handler is ring-0 enabled (for debug). */
    bool                                fRing0Enabled : 1;
    /** See PGMPHYSHANDLER_F_NOT_IN_HM. */
    bool                                fNotInHm : 1;
    /** Pointer to the ring-3 callback function. */
    R3PTRTYPE(PFNPGMPHYSHANDLER)        pfnHandler;
    /** Description / Name. For easing debugging. */
    R3PTRTYPE(const char *)             pszDesc;
} PGMPHYSHANDLERTYPEINTR3;
/** Pointer to a physical access handler type registration. */
typedef PGMPHYSHANDLERTYPEINTR3 *PPGMPHYSHANDLERTYPEINTR3;

/** Pointer to a physical access handler type record for the current context. */
typedef CTX_SUFF(PPGMPHYSHANDLERTYPEINT) PPGMPHYSHANDLERTYPEINT;
/** Pointer to a const physical access handler type record for the current context. */
typedef CTX_SUFF(PGMPHYSHANDLERTYPEINT) const *PCPGMPHYSHANDLERTYPEINT;
/** Dummy physical access handler type record. */
extern CTX_SUFF(PGMPHYSHANDLERTYPEINT) const g_pgmHandlerPhysicalDummyType;


/**
 * Physical page access handler structure.
 *
 * This is used to keep track of physical address ranges
 * which are being monitored in some kind of way.
 */
typedef struct PGMPHYSHANDLER
{
    /** @name Tree stuff.
     * @{  */
    /** First address. */
    RTGCPHYS                            Key;
    /** Last address. */
    RTGCPHYS                            KeyLast;
    uint32_t                            idxLeft;
    uint32_t                            idxRight;
    uint8_t                             cHeight;
    /**  @}  */
    uint8_t                             abPadding[3];
    /** Number of pages to update. */
    uint32_t                            cPages;
    /** Set if we have pages that have been aliased. */
    uint32_t                            cAliasedPages;
    /** Set if we have pages that have temporarily been disabled. */
    uint32_t                            cTmpOffPages;
    /** Registered handler type handle.
     * @note Marked volatile to prevent re-reading after validation. */
    PGMPHYSHANDLERTYPE volatile         hType;
    /** User argument for the handlers. */
    uint64_t                            uUser;
    /** Description / Name. For easing debugging. */
    R3PTRTYPE(const char *)             pszDesc;
    /** Profiling of this handler.
     * @note VBOX_WITH_STATISTICS only, but included to keep structure stable. */
    STAMPROFILE                         Stat;
} PGMPHYSHANDLER;
AssertCompileSize(PGMPHYSHANDLER, 12*8);
/** Pointer to a physical page access handler structure. */
typedef PGMPHYSHANDLER *PPGMPHYSHANDLER;

/**
 * Gets the type record for a physical handler (no reference added).
 * @returns PCPGMPHYSHANDLERTYPEINT, can be NULL
 * @param   a_pVM           The cross context VM structure.
 * @param   a_pPhysHandler  Pointer to the physical handler structure
 *                          (PGMPHYSHANDLER).
 */
#define PGMPHYSHANDLER_GET_TYPE(a_pVM, a_pPhysHandler) \
    pgmHandlerPhysicalTypeHandleToPtr(a_pVM, (a_pPhysHandler) ? (a_pPhysHandler)->hType : NIL_PGMPHYSHANDLERTYPE)

/**
 * Gets the type record for a physical handler, never returns NULL.
 *
 * @returns PCPGMPHYSHANDLERTYPEINT, never NULL.
 * @param   a_pVM           The cross context VM structure.
 * @param   a_pPhysHandler  Pointer to the physical handler structure
 *                          (PGMPHYSHANDLER).
 */
#define PGMPHYSHANDLER_GET_TYPE_NO_NULL(a_pVM, a_pPhysHandler) \
    pgmHandlerPhysicalTypeHandleToPtr2(a_pVM, (a_pPhysHandler) ? (a_pPhysHandler)->hType : NIL_PGMPHYSHANDLERTYPE)

/** Physical access handler allocator. */
typedef RTCHardAvlTreeSlabAllocator<PGMPHYSHANDLER>   PGMPHYSHANDLERALLOCATOR;

/** Physical access handler tree. */
typedef RTCHardAvlRangeTree<PGMPHYSHANDLER, RTGCPHYS> PGMPHYSHANDLERTREE;
/** Pointer to a physical access handler tree. */
typedef PGMPHYSHANDLERTREE                           *PPGMPHYSHANDLERTREE;


/**
 * A Physical Guest Page tracking structure.
 *
 * The format of this structure is complicated because we have to fit a lot
 * of information into as few bits as possible. The format is also subject
 * to change (there is one coming up soon). Which means that for we'll be
 * using PGM_PAGE_GET_*, PGM_PAGE_IS_ and PGM_PAGE_SET_* macros for *all*
 * accesses to the structure.
 */
typedef union PGMPAGE
{
    /** 64-bit integer view. */
    uint64_t    au64[2];
    /** 16-bit view. */
    uint32_t    au32[4];
    /** 16-bit view. */
    uint16_t    au16[8];
    /** 8-bit view. */
    uint8_t     au8[16];

    /** Structured view. */
    struct
    {
        /** 1:0   - The physical handler state (PGM_PAGE_HNDL_PHYS_STATE_*). */
        uint64_t    u2HandlerPhysStateY : 2;
        /** 2     - Don't apply the physical handler in HM mode (nested APIC hack). */
        uint64_t    fHandlerPhysNotInHm : 1;
        /** 3     - Flag indicating that a write monitored page was written to when set. */
        uint64_t    fWrittenToY         : 1;
        /** 4     - Set when the page is write monitored because it's an IEM TB code
         * page.  Save recompiled code the need to verify opcode bytes.
         *
         * IEM fetches this flag as part of the TLB queries.  The flag is cleared when
         * the page is made writable and IEM is informed and will invalidate its
         * physical TLB layer.
         *
         * @note Can possibly be set on ROM pages that are not in the monitored state. */
        uint64_t    fCodePageY          : 1;
        /** 7:5   - Unused. */
        uint64_t    u2Unused0           : 3;
        /** 9:8   - Paging structure needed to map the page
         * (PGM_PAGE_PDE_TYPE_*). */
        uint64_t    u2PDETypeY          : 2;
        /** 11:10 - NEM state bits. */
        uint64_t    u2NemStateY         : 2;
        /** 12:48 - The host physical frame number (shift left to get the
         *  address). */
        uint64_t    HCPhysFN            : 36;
        /** 50:48 - The page state. */
        uint64_t    uStateY             : 3;
        /** 51:53 - The page type (PGMPAGETYPE). */
        uint64_t    uTypeY              : 3;
        /** 63:54 - PTE index for usage tracking (page pool). */
        uint64_t    u10PteIdx           : 10;

        /** The GMM page ID.
         * @remarks In the current implementation, MMIO2 and pages aliased to
         *          MMIO2 pages will be exploiting this field to calculate the
         *          ring-3 mapping address corresponding to the page.
         *          Later we may consider including MMIO2 management into GMM. */
        uint32_t    idPage;
        /** Usage tracking (page pool). */
        uint16_t    u16TrackingY;
        /** The number of read locks on this page. */
        uint8_t     cReadLocksY;
        /** The number of write locks on this page. */
        uint8_t     cWriteLocksY;
    } s;
} PGMPAGE;
AssertCompileSize(PGMPAGE, 16);
/** Pointer to a physical guest page. */
typedef PGMPAGE *PPGMPAGE;
/** Pointer to a const physical guest page. */
typedef const PGMPAGE *PCPGMPAGE;
/** Pointer to a physical guest page pointer. */
typedef PPGMPAGE *PPPGMPAGE;


/**
 * Clears the page structure.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_CLEAR(a_pPage) \
    do { \
        (a_pPage)->au64[0] = 0; \
        (a_pPage)->au64[1] = 0; \
    } while (0)

/**
 * Initializes the page structure.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 * @param   a_HCPhys    The host physical address of the page.
 * @param   a_idPage    The (GMM) page ID of the page.
 * @param   a_uType     The page type (PGMPAGETYPE).
 * @param   a_uState    The page state (PGM_PAGE_STATE_XXX).
 */
#define PGM_PAGE_INIT(a_pPage, a_HCPhys, a_idPage, a_uType, a_uState) \
    do { \
        RTHCPHYS SetHCPhysTmp = (a_HCPhys); \
        AssertFatalMsg(!(SetHCPhysTmp & ~UINT64_C(0x0000fffffffff000)), ("%RHp\n", SetHCPhysTmp)); \
        (a_pPage)->au64[0]           = SetHCPhysTmp; \
        (a_pPage)->au64[1]           = 0; \
        (a_pPage)->s.idPage          = (a_idPage); \
        (a_pPage)->s.uStateY         = (a_uState); \
        (a_pPage)->s.uTypeY          = (a_uType); \
    } while (0)

/**
 * Initializes the page structure of a ZERO page.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 * @param   a_pVM       The VM handle (for getting the zero page address).
 * @param   a_uType     The page type (PGMPAGETYPE).
 */
#define PGM_PAGE_INIT_ZERO(a_pPage, a_pVM, a_uType)  \
    PGM_PAGE_INIT((a_pPage), (a_pVM)->pgm.s.HCPhysZeroPg, NIL_GMM_PAGEID, (a_uType), PGM_PAGE_STATE_ZERO)


/** @name The Page state, PGMPAGE::uStateY.
 * @{ */
/** The zero page.
 * This is a per-VM page that's never ever mapped writable. */
#define PGM_PAGE_STATE_ZERO             0U
/** A allocated page.
 * This is a per-VM page allocated from the page pool (or wherever
 * we get MMIO2 pages from if the type is MMIO2).
 */
#define PGM_PAGE_STATE_ALLOCATED        1U
/** A allocated page that's being monitored for writes.
 * The shadow page table mappings are read-only. When a write occurs, the
 * fWrittenTo member is set, the page remapped as read-write and the state
 * moved back to allocated. */
#define PGM_PAGE_STATE_WRITE_MONITORED  2U
/** The page is shared, aka. copy-on-write.
 * This is a page that's shared with other VMs. */
#define PGM_PAGE_STATE_SHARED           3U
/** The page is ballooned, so no longer available for this VM. */
#define PGM_PAGE_STATE_BALLOONED        4U
/** @} */


/** Asserts lock ownership in some of the PGM_PAGE_XXX macros. */
#if defined(VBOX_STRICT) && 0 /** @todo triggers in pgmRZDynMapGCPageV2Inlined */
# define PGM_PAGE_ASSERT_LOCK(a_pVM)  PGM_LOCK_ASSERT_OWNER(a_pVM)
#else
# define PGM_PAGE_ASSERT_LOCK(a_pVM)  do { } while (0)
#endif

/**
 * Gets the page state.
 * @returns page state (PGM_PAGE_STATE_*).
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 *
 * @remarks See PGM_PAGE_GET_HCPHYS_NA for remarks about GCC and strict
 *          builds.
 */
#define PGM_PAGE_GET_STATE_NA(a_pPage)          ( (a_pPage)->s.uStateY )
#if defined(__GNUC__) && defined(VBOX_STRICT)
# define PGM_PAGE_GET_STATE(a_pPage)        __extension__ ({ PGM_PAGE_ASSERT_LOCK(pVM); PGM_PAGE_GET_STATE_NA(a_pPage); })
#else
# define PGM_PAGE_GET_STATE                     PGM_PAGE_GET_STATE_NA
#endif

/**
 * Sets the page state.
 * @param   a_pVM       The VM handle, only used for lock ownership assertions.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 * @param   a_uState    The new page state.
 */
#define PGM_PAGE_SET_STATE(a_pVM, a_pPage, a_uState) \
    do { (a_pPage)->s.uStateY = (a_uState); PGM_PAGE_ASSERT_LOCK(a_pVM); } while (0)


/**
 * Gets the host physical address of the guest page.
 * @returns host physical address (RTHCPHYS).
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 *
 * @remarks In strict builds on gcc platforms, this macro will make some ugly
 *          assumption about a valid pVM variable/parameter being in the
 *          current context.  It will use this pVM variable to assert that the
 *          PGM lock is held.  Use the PGM_PAGE_GET_HCPHYS_NA in contexts where
 *          pVM is not around.
 */
#if 0
# define PGM_PAGE_GET_HCPHYS_NA(a_pPage)        ( (a_pPage)->s.HCPhysFN << 12 )
# define PGM_PAGE_GET_HCPHYS                    PGM_PAGE_GET_HCPHYS_NA
#else
# define PGM_PAGE_GET_HCPHYS_NA(a_pPage)        ( (a_pPage)->au64[0] & UINT64_C(0x0000fffffffff000) )
# if defined(__GNUC__) && defined(VBOX_STRICT)
#  define PGM_PAGE_GET_HCPHYS(a_pPage)      __extension__ ({ PGM_PAGE_ASSERT_LOCK(pVM); PGM_PAGE_GET_HCPHYS_NA(a_pPage); })
# else
#  define PGM_PAGE_GET_HCPHYS                   PGM_PAGE_GET_HCPHYS_NA
# endif
#endif

/**
 * Sets the host physical address of the guest page.
 *
 * @param   a_pVM       The VM handle, only used for lock ownership assertions.
 * @param   a_pPage      Pointer to the physical guest page tracking structure.
 * @param   a_HCPhys     The new host physical address.
 */
#define PGM_PAGE_SET_HCPHYS(a_pVM, a_pPage, a_HCPhys) \
    do { \
        RTHCPHYS const SetHCPhysTmp = (a_HCPhys); \
        AssertFatal(!(SetHCPhysTmp & ~UINT64_C(0x0000fffffffff000))); \
        (a_pPage)->s.HCPhysFN = SetHCPhysTmp >> 12; \
        PGM_PAGE_ASSERT_LOCK(a_pVM); \
    } while (0)

/**
 * Get the Page ID.
 * @returns The Page ID; NIL_GMM_PAGEID if it's a ZERO page.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_GET_PAGEID(a_pPage)            (  (uint32_t)(a_pPage)->s.idPage )

/**
 * Sets the Page ID.
 * @param   a_pVM       The VM handle, only used for lock ownership assertions.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 * @param   a_idPage    The new page ID.
 */
#define PGM_PAGE_SET_PAGEID(a_pVM, a_pPage, a_idPage) \
    do { \
        (a_pPage)->s.idPage = (a_idPage); \
        PGM_PAGE_ASSERT_LOCK(a_pVM); \
    } while (0)

/**
 * Get the Chunk ID.
 * @returns The Chunk ID; NIL_GMM_CHUNKID if it's a ZERO page.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_GET_CHUNKID(a_pPage)           ( PGM_PAGE_GET_PAGEID(a_pPage) >> GMM_CHUNKID_SHIFT )

/**
 * Get the index of the page within the allocation chunk.
 * @returns The page index.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_GET_PAGE_IN_CHUNK(a_pPage)     ( PGM_PAGE_GET_PAGEID(a_pPage) & GMM_PAGEID_IDX_MASK )

/**
 * Gets the page type.
 * @returns The page type.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 *
 * @remarks See PGM_PAGE_GET_HCPHYS_NA for remarks about GCC and strict
 *          builds.
 */
#define PGM_PAGE_GET_TYPE_NA(a_pPage)           ( (a_pPage)->s.uTypeY )
#if defined(__GNUC__) && defined(VBOX_STRICT)
# define PGM_PAGE_GET_TYPE(a_pPage)         __extension__ ({ PGM_PAGE_ASSERT_LOCK(pVM); PGM_PAGE_GET_TYPE_NA(a_pPage); })
#else
# define PGM_PAGE_GET_TYPE                      PGM_PAGE_GET_TYPE_NA
#endif

/**
 * Sets the page type.
 *
 * @param   a_pVM       The VM handle, only used for lock ownership assertions.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 * @param   a_enmType   The new page type (PGMPAGETYPE).
 */
#define PGM_PAGE_SET_TYPE(a_pVM, a_pPage, a_enmType) \
    do { (a_pPage)->s.uTypeY = (a_enmType); PGM_PAGE_ASSERT_LOCK(a_pVM); } while (0)

/**
 * Gets the page table index
 * @returns The page table index.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_GET_PTE_INDEX(a_pPage)         ( (a_pPage)->s.u10PteIdx )

/**
 * Sets the page table index.
 * @param   a_pVM       The VM handle, only used for lock ownership assertions.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 * @param   a_iPte      New page table index.
 */
#define PGM_PAGE_SET_PTE_INDEX(a_pVM, a_pPage, a_iPte) \
    do { (a_pPage)->s.u10PteIdx = (a_iPte); PGM_PAGE_ASSERT_LOCK(a_pVM); } while (0)

/**
 * Checks if the page is marked for MMIO, no MMIO2 aliasing.
 * @returns true/false.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_IS_MMIO(a_pPage)               ( (a_pPage)->s.uTypeY == PGMPAGETYPE_MMIO )

/**
 * Checks if the page is marked for MMIO, including both aliases.
 * @returns true/false.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_IS_MMIO_OR_ALIAS(a_pPage)      (   (a_pPage)->s.uTypeY == PGMPAGETYPE_MMIO \
                                                 || (a_pPage)->s.uTypeY == PGMPAGETYPE_MMIO2_ALIAS_MMIO \
                                                 || (a_pPage)->s.uTypeY == PGMPAGETYPE_SPECIAL_ALIAS_MMIO \
                                                 )

/**
 * Checks if the page is marked for MMIO, including special aliases.
 * @returns true/false.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_IS_MMIO_OR_SPECIAL_ALIAS(a_pPage) (   (a_pPage)->s.uTypeY == PGMPAGETYPE_MMIO \
                                                    || (a_pPage)->s.uTypeY == PGMPAGETYPE_SPECIAL_ALIAS_MMIO )

/**
 * Checks if the page is a special aliased MMIO page.
 * @returns true/false.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_IS_SPECIAL_ALIAS_MMIO(a_pPage) ( (a_pPage)->s.uTypeY == PGMPAGETYPE_SPECIAL_ALIAS_MMIO )

/**
 * Checks if the page is backed by the ZERO page.
 * @returns true/false.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_IS_ZERO(a_pPage)               ( (a_pPage)->s.uStateY == PGM_PAGE_STATE_ZERO )

/**
 * Checks if the page is backed by a SHARED page.
 * @returns true/false.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_IS_SHARED(a_pPage)             ( (a_pPage)->s.uStateY == PGM_PAGE_STATE_SHARED )

/**
 * Checks if the page is ballooned.
 * @returns true/false.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_IS_BALLOONED(a_pPage)          ( (a_pPage)->s.uStateY == PGM_PAGE_STATE_BALLOONED )

/**
 * Checks if the page is allocated.
 * @returns true/false.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_IS_ALLOCATED(a_pPage)          ( (a_pPage)->s.uStateY == PGM_PAGE_STATE_ALLOCATED )


/**
 * Marks the page as written to (for GMM change monitoring).
 * @param   a_pVM       The VM handle, only used for lock ownership assertions.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_SET_WRITTEN_TO(a_pVM, a_pPage) \
    do { (a_pPage)->s.fWrittenToY = 1; PGM_PAGE_ASSERT_LOCK(a_pVM); } while (0)

/**
 * Clears the written-to indicator.
 * @param   a_pVM       The VM handle, only used for lock ownership assertions.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_CLEAR_WRITTEN_TO(a_pVM, a_pPage) \
    do { (a_pPage)->s.fWrittenToY = 0; PGM_PAGE_ASSERT_LOCK(a_pVM); } while (0)

/**
 * Checks if the page was marked as written-to.
 * @returns true/false.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_IS_WRITTEN_TO(a_pPage)         ( (a_pPage)->s.fWrittenToY )


/**
 * Marks the page as an IEM code page (being write monitored or a ROM page).
 * @param   a_pVM       The VM handle, only used for lock ownership assertions.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_SET_CODE_PAGE(a_pVM, a_pPage) \
    do { (a_pPage)->s.fCodePageY = 1; PGM_PAGE_ASSERT_LOCK(a_pVM); } while (0)

/**
 * Clears the code page indicator.
 * @param   a_pVM       The VM handle, only used for lock ownership assertions.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_CLEAR_CODE_PAGE(a_pVM, a_pPage) \
    do { (a_pPage)->s.fCodePageY = 0; PGM_PAGE_ASSERT_LOCK(a_pVM); } while (0)

/**
 * Checks if the page is an IEM code page (implies write monitored or ROM page).
 * @returns true/false.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_IS_CODE_PAGE(a_pPage)         ( (a_pPage)->s.fCodePageY )


/** @name PT usage values (PGMPAGE::u2PDEType).
 *
 * @{ */
/** Either as a PT or PDE. */
#define PGM_PAGE_PDE_TYPE_DONTCARE              0
/** Must use a page table to map the range. */
#define PGM_PAGE_PDE_TYPE_PT                    1
/** Can use a page directory entry to map the continuous range. */
#define PGM_PAGE_PDE_TYPE_PDE                   2
/** Can use a page directory entry to map the continuous range - temporarily disabled (by page monitoring). */
#define PGM_PAGE_PDE_TYPE_PDE_DISABLED          3
/** @} */

/**
 * Set the PDE type of the page
 * @param   a_pVM       The VM handle, only used for lock ownership assertions.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 * @param   a_uType     PGM_PAGE_PDE_TYPE_*.
 */
#define PGM_PAGE_SET_PDE_TYPE(a_pVM, a_pPage, a_uType) \
    do { (a_pPage)->s.u2PDETypeY = (a_uType); PGM_PAGE_ASSERT_LOCK(a_pVM); } while (0)

/**
 * Checks if the page was marked being part of a large page
 * @returns true/false.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_GET_PDE_TYPE(a_pPage)          ( (a_pPage)->s.u2PDETypeY )

/** @name Physical Access Handler State values (PGMPAGE::u2HandlerPhysStateY).
 *
 * @remarks The values are assigned in order of priority, so we can calculate
 *          the correct state for a page with different handlers installed.
 * @{ */
/** No handler installed. */
#define PGM_PAGE_HNDL_PHYS_STATE_NONE           0
/** Monitoring is temporarily disabled. */
#define PGM_PAGE_HNDL_PHYS_STATE_DISABLED       1
/** Write access is monitored. */
#define PGM_PAGE_HNDL_PHYS_STATE_WRITE          2
/** All access is monitored. */
#define PGM_PAGE_HNDL_PHYS_STATE_ALL            3
/** @} */

/**
 * Gets the physical access handler state of a page.
 * @returns PGM_PAGE_HNDL_PHYS_STATE_* value.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_GET_HNDL_PHYS_STATE(a_pPage)   ( (a_pPage)->s.u2HandlerPhysStateY )

/**
 * Sets the physical access handler state of a page.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 * @param   a_uState    The new state value.
 * @param   a_fNotInHm  The PGMPHYSHANDLER_F_NOT_HM bit.
 */
#define PGM_PAGE_SET_HNDL_PHYS_STATE(a_pPage, a_uState, a_fNotInHm) \
    do { (a_pPage)->s.u2HandlerPhysStateY = (a_uState);  (a_pPage)->s.fHandlerPhysNotInHm = (a_fNotInHm); } while (0)

/**
 * Sets the physical access handler state of a page.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 * @param   a_uState    The new state value.
 */
#define PGM_PAGE_SET_HNDL_PHYS_STATE_ONLY(a_pPage, a_uState) \
    do { (a_pPage)->s.u2HandlerPhysStateY = (a_uState); } while (0)

/**
 * Checks if the page has any physical access handlers, including temporarily disabled ones.
 * @returns true/false
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_HAS_ANY_PHYSICAL_HANDLERS(a_pPage) \
    ( PGM_PAGE_GET_HNDL_PHYS_STATE(a_pPage) != PGM_PAGE_HNDL_PHYS_STATE_NONE )

/**
 * Checks if the page has any active physical access handlers.
 * @returns true/false
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_HAS_ACTIVE_PHYSICAL_HANDLERS(a_pPage) \
    ( PGM_PAGE_GET_HNDL_PHYS_STATE(a_pPage) >= PGM_PAGE_HNDL_PHYS_STATE_WRITE )

/**
 * Checks if the page has any access handlers, including temporarily disabled ones.
 * @returns true/false
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_HAS_ANY_HANDLERS(a_pPage) \
    ( PGM_PAGE_GET_HNDL_PHYS_STATE(a_pPage) != PGM_PAGE_HNDL_PHYS_STATE_NONE )

/**
 * Checks if the page has any active access handlers.
 * @returns true/false
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_HAS_ACTIVE_HANDLERS(a_pPage) \
     (PGM_PAGE_GET_HNDL_PHYS_STATE(a_pPage) >= PGM_PAGE_HNDL_PHYS_STATE_WRITE )

/**
 * Checks if the page has any active access handlers catching all accesses.
 * @returns true/false
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_HAS_ACTIVE_ALL_HANDLERS(a_pPage) \
    ( PGM_PAGE_GET_HNDL_PHYS_STATE(a_pPage) == PGM_PAGE_HNDL_PHYS_STATE_ALL )

/** @def PGM_PAGE_IS_HNDL_PHYS_NOT_IN_HM
 * Checks if the physical handlers of the page should be ignored in shadow page
 * tables and such.
 * @returns true/false
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_IS_HNDL_PHYS_NOT_IN_HM(a_pPage) ((a_pPage)->s.fHandlerPhysNotInHm)

/** @def PGM_PAGE_GET_TRACKING
 * Gets the packed shadow page pool tracking data associated with a guest page.
 * @returns uint16_t containing the data.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_GET_TRACKING_NA(a_pPage)       ( (a_pPage)->s.u16TrackingY )
#if defined(__GNUC__) && defined(VBOX_STRICT)
# define PGM_PAGE_GET_TRACKING(a_pPage)     __extension__ ({ PGM_PAGE_ASSERT_LOCK(pVM); PGM_PAGE_GET_TRACKING_NA(a_pPage); })
#else
# define PGM_PAGE_GET_TRACKING                  PGM_PAGE_GET_TRACKING_NA
#endif

/** @def PGM_PAGE_SET_TRACKING
 * Sets the packed shadow page pool tracking data associated with a guest page.
 * @param   a_pVM       The VM handle, only used for lock ownership assertions.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 * @param   a_u16TrackingData   The tracking data to store.
 */
#define PGM_PAGE_SET_TRACKING(a_pVM, a_pPage, a_u16TrackingData) \
    do { (a_pPage)->s.u16TrackingY = (a_u16TrackingData); PGM_PAGE_ASSERT_LOCK(a_pVM); } while (0)

/** @def PGM_PAGE_GET_TD_CREFS
 * Gets the @a cRefs tracking data member.
 * @returns cRefs.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_GET_TD_CREFS(a_pPage) \
    ((PGM_PAGE_GET_TRACKING(a_pPage) >> PGMPOOL_TD_CREFS_SHIFT)    & PGMPOOL_TD_CREFS_MASK)
#define PGM_PAGE_GET_TD_CREFS_NA(a_pPage) \
    ((PGM_PAGE_GET_TRACKING_NA(a_pPage) >> PGMPOOL_TD_CREFS_SHIFT) & PGMPOOL_TD_CREFS_MASK)

/** @def PGM_PAGE_GET_TD_IDX
 * Gets the @a idx tracking data member.
 * @returns idx.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_GET_TD_IDX(a_pPage) \
    ((PGM_PAGE_GET_TRACKING(a_pPage) >> PGMPOOL_TD_IDX_SHIFT)      & PGMPOOL_TD_IDX_MASK)
#define PGM_PAGE_GET_TD_IDX_NA(a_pPage) \
    ((PGM_PAGE_GET_TRACKING_NA(a_pPage) >> PGMPOOL_TD_IDX_SHIFT)   & PGMPOOL_TD_IDX_MASK)


/** Max number of locks on a page. */
#define PGM_PAGE_MAX_LOCKS                      UINT8_C(254)

/** Get the read lock count.
 * @returns count.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_GET_READ_LOCKS(a_pPage)        ( (a_pPage)->s.cReadLocksY )

/** Get the write lock count.
 * @returns count.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_GET_WRITE_LOCKS(a_pPage)       ( (a_pPage)->s.cWriteLocksY )

/** Decrement the read lock counter.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_DEC_READ_LOCKS(a_pPage)        do { --(a_pPage)->s.cReadLocksY; } while (0)

/** Decrement the write lock counter.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_DEC_WRITE_LOCKS(a_pPage)       do { --(a_pPage)->s.cWriteLocksY; } while (0)

/** Increment the read lock counter.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_INC_READ_LOCKS(a_pPage)        do { ++(a_pPage)->s.cReadLocksY; } while (0)

/** Increment the write lock counter.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_INC_WRITE_LOCKS(a_pPage)       do { ++(a_pPage)->s.cWriteLocksY; } while (0)


/** Gets the NEM state.
 * @returns NEM state value (two bits).
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 */
#define PGM_PAGE_GET_NEM_STATE(a_pPage)         ((a_pPage)->s.u2NemStateY)

/** Sets the NEM state.
 * @param   a_pPage     Pointer to the physical guest page tracking structure.
 * @param   a_u2State   The NEM state value (specific to NEM impl.).
 */
#define PGM_PAGE_SET_NEM_STATE(a_pPage, a_u2State) \
    do { Assert((a_u2State) < 4); (a_pPage)->s.u2NemStateY = (a_u2State); } while (0)


#if 0
/** Enables sanity checking of write monitoring using CRC-32. */
# define PGMLIVESAVERAMPAGE_WITH_CRC32
#endif

/**
 * Per page live save tracking data.
 */
typedef struct PGMLIVESAVERAMPAGE
{
    /** Number of times it has been dirtied. */
    uint32_t    cDirtied : 24;
    /** Whether it is currently dirty. */
    uint32_t    fDirty : 1;
    /** Ignore the page.
     *  This is used for pages that has been MMIO, MMIO2 or ROM pages once.  We will
     *  deal with these after pausing the VM and DevPCI have said it bit about
     *  remappings. */
    uint32_t    fIgnore : 1;
    /** Was a ZERO page last time around. */
    uint32_t    fZero : 1;
    /** Was a SHARED page last time around. */
    uint32_t    fShared : 1;
    /** Whether the page is/was write monitored in a previous pass. */
    uint32_t    fWriteMonitored : 1;
    /** Whether the page is/was write monitored earlier in this pass. */
    uint32_t    fWriteMonitoredJustNow : 1;
    /** Bits reserved for future use. */
    uint32_t    u2Reserved : 2;
#ifdef PGMLIVESAVERAMPAGE_WITH_CRC32
    /** CRC-32 for the page. This is for internal consistency checks. */
    uint32_t    u32Crc;
#endif
} PGMLIVESAVERAMPAGE;
#ifdef PGMLIVESAVERAMPAGE_WITH_CRC32
AssertCompileSize(PGMLIVESAVERAMPAGE, 8);
#else
AssertCompileSize(PGMLIVESAVERAMPAGE, 4);
#endif
/** Pointer to the per page live save tracking data. */
typedef PGMLIVESAVERAMPAGE *PPGMLIVESAVERAMPAGE;

/** The max value of PGMLIVESAVERAMPAGE::cDirtied. */
#define PGMLIVSAVEPAGE_MAX_DIRTIED 0x00fffff0


/**
 * RAM range lookup table entry.
 */
typedef union PGMRAMRANGELOOKUPENTRY
{
    RT_GCC_EXTENSION struct
    {
        /** Page aligned start address of the range, with page offset holding the ID. */
        RTGCPHYS                        GCPhysFirstAndId;
        /** The last address in the range (inclusive). Page aligned (-1). */
        RTGCPHYS                        GCPhysLast;
    };
    /** Alternative 128-bit view for atomic updating. */
    RTUINT128U volatile                 u128Volatile;
    /** Alternative 128-bit view for atomic updating. */
    RTUINT128U                          u128Normal;
} PGMRAMRANGELOOKUPENTRY;
/** Pointer to a lookup table entry. */
typedef PGMRAMRANGELOOKUPENTRY *PPGMRAMRANGELOOKUPENTRY;

/** Extracts the ID from  PGMRAMRANGELOOKUPENTRY::GCPhysFirstAndId. */
#define PGMRAMRANGELOOKUPENTRY_GET_ID(a_LookupEntry)    ((uint32_t)((a_LookupEntry).GCPhysFirstAndId & GUEST_PAGE_OFFSET_MASK))
/** Extracts the GCPhysFirst from PGMRAMRANGELOOKUPENTRY::GCPhysFirstAndId. */
#define PGMRAMRANGELOOKUPENTRY_GET_FIRST(a_LookupEntry) (((a_LookupEntry).GCPhysFirstAndId) & ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK)


/**
 * RAM range for GC Phys to HC Phys & R3 Ptr conversion.
 *
 * This structure is addressed via context specific pointer tables.  Lookup is
 * organized via the lookup table (PGMRAMRANGELOOKUPENTRY).
 */
typedef struct PGMRAMRANGE
{
    /** Start of the range. Page aligned. */
    RTGCPHYS                            GCPhys;
    /** Size of the range. (Page aligned of course).
     * Ring-0 duplicates this in a PGMR0PERVM::acRamRangePages (shifted by
     * guest page size). */
    RTGCPHYS                            cb;
    /** PGM_RAM_RANGE_FLAGS_* flags. */
    uint32_t                            fFlags;
    /** NEM specific info, UINT32_MAX if not used. */
    uint32_t                            uNemRange;
    /** Last address in the range (inclusive). Page aligned (-1). */
    RTGCPHYS                            GCPhysLast;
    /** Start of the HC mapping of the range. This is only used for MMIO2 and in NEM mode. */
    R3PTRTYPE(uint8_t *)                pbR3;
    /** The RAM range identifier (index into the pointer table). */
    uint32_t                            idRange;
#if HC_ARCH_BITS != 32
    /** Padding to make aPage aligned on sizeof(PGMPAGE). */
    uint32_t                            au32Alignment2[HC_ARCH_BITS == 32 ? 0 : 1];
#endif
    /** Live save per page tracking data. */
    R3PTRTYPE(PPGMLIVESAVERAMPAGE)      paLSPages;
    /** The range description. */
    R3PTRTYPE(const char *)             pszDesc;

    /** Array of physical guest page tracking structures.
     * @note Number of entries is PGMRAMRANGE::cb / GUEST_PAGE_SIZE. */
    PGMPAGE                             aPages[1];
} PGMRAMRANGE;
AssertCompileMemberAlignment(PGMRAMRANGE, aPages, 16);
/** Pointer to RAM range for GC Phys to HC Phys conversion. */
typedef PGMRAMRANGE *PPGMRAMRANGE;

/** @name PGMRAMRANGE::fFlags
 * @{ */
/** Ad hoc RAM range for an ROM mapping. */
#define PGM_RAM_RANGE_FLAGS_AD_HOC_ROM      RT_BIT(21)
/** Ad hoc RAM range for an MMIO mapping. */
#define PGM_RAM_RANGE_FLAGS_AD_HOC_MMIO     RT_BIT(22)
/** Ad hoc RAM range for an MMIO2 or pre-registered MMIO mapping. */
#define PGM_RAM_RANGE_FLAGS_AD_HOC_MMIO_EX  RT_BIT(23)
/** Valid RAM range flags.  */
#define PGM_RAM_RANGE_FLAGS_VALID_MASK      (PGM_RAM_RANGE_FLAGS_AD_HOC_ROM | PGM_RAM_RANGE_FLAGS_AD_HOC_MMIO | PGM_RAM_RANGE_FLAGS_AD_HOC_MMIO_EX)
/** @} */

/** Tests if a RAM range is an ad hoc one or not.
 * @returns true/false.
 * @param   pRam    The RAM range.
 */
#define PGM_RAM_RANGE_IS_AD_HOC(pRam) \
    (!!( (pRam)->fFlags & (PGM_RAM_RANGE_FLAGS_AD_HOC_ROM | PGM_RAM_RANGE_FLAGS_AD_HOC_MMIO | PGM_RAM_RANGE_FLAGS_AD_HOC_MMIO_EX) ) )

/** The number of entries in the RAM range TLBs (there is one for each
 *  context).  Must be a power of two. */
#define PGM_RAMRANGE_TLB_ENTRIES            64

/**
 * Calculates the RAM range TLB index for the physical address.
 *
 * @returns RAM range TLB index.
 * @param   a_GCPhys    The guest physical address.
 */
#define PGM_RAMRANGE_TLB_IDX(a_GCPhys)      ( ((a_GCPhys) >> 19) & (PGM_RAMRANGE_TLB_ENTRIES - 1) )

/**
 * Calculates the ring-3 address for a_GCPhysPage if the RAM range has a
 * mapping address.
 */
#define PGM_RAMRANGE_CALC_PAGE_R3PTR(a_pRam, a_GCPhysPage) \
    ( (a_pRam)->pbR3 ? (a_pRam)->pbR3 + (a_GCPhysPage) - (a_pRam)->GCPhys : NULL )


/**
 * Per page tracking structure for ROM image.
 *
 * A ROM image may have a shadow page, in which case we may have two pages
 * backing it.  This structure contains the PGMPAGE for both while
 * PGMRAMRANGE have a copy of the active one.  It is important that these
 * aren't out of sync in any regard other than page pool tracking data.
 */
typedef struct PGMROMPAGE
{
    /** The page structure for the virgin ROM page. */
    PGMPAGE     Virgin;
    /** The page structure for the shadow RAM page. */
    PGMPAGE     Shadow;
    /** The current protection setting. */
    PGMROMPROT  enmProt;
    /** Live save status information. Makes use of unused alignment space. */
    struct
    {
        /** The previous protection value. */
        uint8_t u8Prot;
        /** Written to flag set by the handler. */
        bool    fWrittenTo;
        /** Whether the shadow page is dirty or not. */
        bool    fDirty;
        /** Whether it was dirtied in the recently. */
        bool    fDirtiedRecently;
    } LiveSave;
} PGMROMPAGE;
AssertCompileSizeAlignment(PGMROMPAGE, 8);
/** Pointer to a ROM page tracking structure. */
typedef PGMROMPAGE *PPGMROMPAGE;


/**
 * A registered ROM image.
 *
 * This is needed to keep track of ROM image since they generally intrude
 * into a PGMRAMRANGE.  It also keeps track of additional info like the
 * two page sets (read-only virgin and read-write shadow), the current
 * state of each page.
 *
 * Because access handlers cannot easily be executed in a different
 * context, the ROM ranges needs to be accessible and in all contexts.
 */
typedef struct PGMROMRANGE
{
    /** Address of the range. */
    RTGCPHYS                            GCPhys;
    /** Address of the last byte in the range. */
    RTGCPHYS                            GCPhysLast;
    /** Size of the range. */
    RTGCPHYS                            cb;
    /** The flags (PGMPHYS_ROM_FLAGS_*). */
    uint8_t                             fFlags;
    /** The saved state range ID. */
    uint8_t                             idSavedState;
    /** The ID of the associated RAM range. */
#ifdef IN_RING0
    volatile
#endif
    uint16_t                            idRamRange;
    /** The size bits pvOriginal points to. */
    uint32_t                            cbOriginal;
    /** Pointer to the original bits when PGMPHYS_ROM_FLAGS_PERMANENT_BINARY was specified.
     * This is used for strictness checks. */
    R3PTRTYPE(const void *)             pvOriginal;
    /** The ROM description. */
    R3PTRTYPE(const char *)             pszDesc;
#ifdef VBOX_WITH_PGM_NEM_MODE
    /** In simplified memory mode this provides alternate backing for shadowed ROMs.
     * - PGMROMPROT_READ_ROM_WRITE_IGNORE:  Shadow
     * - PGMROMPROT_READ_ROM_WRITE_RAM:     Shadow
     * - PGMROMPROT_READ_RAM_WRITE_IGNORE:  ROM
     * - PGMROMPROT_READ_RAM_WRITE_RAM:     ROM  */
    R3PTRTYPE(uint8_t *)                pbR3Alternate;
    RTR3PTR                             pvAlignment2;
#else
    RTR3PTR                             apvUnused[2];
#endif
    /** The per page tracking structures. */
    PGMROMPAGE                          aPages[1];
} PGMROMRANGE;
/** Pointer to a ROM range. */
typedef PGMROMRANGE *PPGMROMRANGE;


/**
 * Live save per page data for an MMIO2 page.
 *
 * Not using PGMLIVESAVERAMPAGE here because we cannot use normal write monitoring
 * of MMIO2 pages.  The current approach is using some optimistic SHA-1 +
 * CRC-32 for detecting changes as well as special handling of zero pages.  This
 * is a TEMPORARY measure which isn't perfect, but hopefully it is good enough
 * for speeding things up.  (We're using SHA-1 and not SHA-256 or SHA-512
 * because of speed (2.5x and 6x slower).)
 *
 * @todo Implement dirty MMIO2 page reporting that can be enabled during live
 *       save but normally is disabled.  Since we can write monitor guest
 *       accesses on our own, we only need this for host accesses.  Shouldn't be
 *       too difficult for DevVGA, VMMDev might be doable, the planned
 *       networking fun will be fun since it involves ring-0.
 */
typedef struct PGMLIVESAVEMMIO2PAGE
{
    /** Set if the page is considered dirty. */
    bool        fDirty;
    /** The number of scans this page has remained unchanged for.
     * Only updated for dirty pages. */
    uint8_t     cUnchangedScans;
    /** Whether this page was zero at the last scan. */
    bool        fZero;
    /** Alignment padding. */
    bool        fReserved;
    /** CRC-32 for the first half of the page.
     * This is used together with u32CrcH2 to quickly detect changes in the page
     * during the non-final passes. */
    uint32_t    u32CrcH1;
    /** CRC-32 for the second half of the page. */
    uint32_t    u32CrcH2;
    /** SHA-1 for the saved page.
     * This is used in the final pass to skip pages without changes. */
    uint8_t     abSha1Saved[RTSHA1_HASH_SIZE];
} PGMLIVESAVEMMIO2PAGE;
/** Pointer to a live save status data for an MMIO2 page. */
typedef PGMLIVESAVEMMIO2PAGE *PPGMLIVESAVEMMIO2PAGE;

/**
 * A registered MMIO2 (= Device RAM) range.
 *
 * There are a few reason why we need to keep track of these registrations.  One
 * of them is the deregistration & cleanup stuff, while another is that the
 * PGMRAMRANGE associated with such a region may have to be removed from the ram
 * range list.
 *
 * Overlapping with a RAM range has to be 100% or none at all.  The pages in the
 * existing RAM range must not be ROM nor MMIO.  A guru meditation will be
 * raised if a partial overlap or an overlap of ROM pages is encountered.  On an
 * overlap we will free all the existing RAM pages and put in the ram range
 * pages instead.
 */
typedef struct PGMREGMMIO2RANGE
{
    /** The owner of the range (a device). */
    PPDMDEVINSR3                        pDevInsR3;
    /** Pointer to the ring-3 mapping of the allocation. */
    R3PTRTYPE(uint8_t *)                pbR3;
    /** Flags (PGMREGMMIO2RANGE_F_XXX). */
    uint16_t                            fFlags;
    /** The sub device number (internal PCI config (CFGM) number). */
    uint8_t                             iSubDev;
    /** The PCI region number. */
    uint8_t                             iRegion;
    /** The saved state range ID. */
    uint8_t                             idSavedState;
    /** MMIO2 range identifier, for page IDs (PGMPAGE::s.idPage). */
    uint8_t                             idMmio2;
    /** The ID of the associated RAM range. */
#ifdef IN_RING0
    volatile
#endif
    uint16_t                            idRamRange;
    /** The mapping address if mapped, NIL_RTGCPHYS if not.  */
    RTGCPHYS                            GCPhys;
    /** The real size.
     * This may be larger than indicated by RamRange.cb if the range has been
     * reduced during saved state loading. */
    RTGCPHYS                            cbReal;
    /** Pointer to the physical handler for MMIO.
     * If NEM is responsible for tracking dirty pages in simple memory mode, this
     * will be NULL. */
    R3PTRTYPE(PPGMPHYSHANDLER)          pPhysHandlerR3;
    /** Live save per page tracking data for MMIO2. */
    R3PTRTYPE(PPGMLIVESAVEMMIO2PAGE)    paLSPages;
    RTR3PTR                             R3PtrPadding;
} PGMREGMMIO2RANGE;
/** Pointer to a MMIO2 or pre-registered MMIO range. */
typedef PGMREGMMIO2RANGE *PPGMREGMMIO2RANGE;

/** @name PGMREGMMIO2RANGE_F_XXX - Registered MMIO2 range flags.
 * @{ */
/** Set if this is the first chunk in the MMIO2 range. */
#define PGMREGMMIO2RANGE_F_FIRST_CHUNK          UINT16_C(0x0001)
/** Set if this is the last chunk in the MMIO2 range. */
#define PGMREGMMIO2RANGE_F_LAST_CHUNK           UINT16_C(0x0002)
/** Set if the whole range is mapped. */
#define PGMREGMMIO2RANGE_F_MAPPED               UINT16_C(0x0004)
/** Set if it's overlapping, clear if not. */
#define PGMREGMMIO2RANGE_F_OVERLAPPING          UINT16_C(0x0008)
/** This mirrors the PGMPHYS_MMIO2_FLAGS_TRACK_DIRTY_PAGES creation flag.*/
#define PGMREGMMIO2RANGE_F_TRACK_DIRTY_PAGES    UINT16_C(0x0010)
/** Set if the access handler is registered.   */
#define PGMREGMMIO2RANGE_F_IS_TRACKING          UINT16_C(0x0020)
/** Set if dirty page tracking is currently enabled. */
#define PGMREGMMIO2RANGE_F_TRACKING_ENABLED     UINT16_C(0x0040)
/** Set if there are dirty pages in the range.   */
#define PGMREGMMIO2RANGE_F_IS_DIRTY             UINT16_C(0x0080)
/** @} */


/** @name Internal MMIO2 macros.
 * @{ */
/** Makes a MMIO2 page ID out of a MMIO2 range ID and page index number. */
#define PGM_MMIO2_PAGEID_MAKE(a_idMmio2, a_iPage)   ( ((uint32_t)(a_idMmio2) << 24) | (uint32_t)(a_iPage) )
/** Gets the MMIO2 range ID from an MMIO2 page ID. */
#define PGM_MMIO2_PAGEID_GET_MMIO2_ID(a_idPage)     ( (uint8_t)((a_idPage) >> 24) )
/** Gets the MMIO2 page index from an MMIO2 page ID. */
#define PGM_MMIO2_PAGEID_GET_IDX(a_idPage)          ( ((a_idPage) & UINT32_C(0x00ffffff)) )
/** @} */



/**
 * PGMPhysRead/Write cache entry
 */
typedef struct PGMPHYSCACHEENTRY
{
    /** R3 pointer to physical page. */
    R3PTRTYPE(uint8_t *)                pbR3;
    /** GC Physical address for cache entry */
    RTGCPHYS                            GCPhys;
#if HC_ARCH_BITS == 64 && GC_ARCH_BITS == 32
    RTGCPHYS                            u32Padding0; /**< alignment padding. */
#endif
} PGMPHYSCACHEENTRY;

/**
 * PGMPhysRead/Write cache to reduce REM memory access overhead
 */
typedef struct PGMPHYSCACHE
{
    /** Bitmap of valid cache entries */
    uint64_t                            aEntries;
    /** Cache entries */
    PGMPHYSCACHEENTRY                   Entry[PGM_MAX_PHYSCACHE_ENTRIES];
} PGMPHYSCACHE;


/** @name Ring-3 page mapping TLBs
 * @{  */

/** Pointer to an allocation chunk ring-3 mapping. */
typedef struct PGMCHUNKR3MAP *PPGMCHUNKR3MAP;
/** Pointer to an allocation chunk ring-3 mapping pointer. */
typedef PPGMCHUNKR3MAP *PPPGMCHUNKR3MAP;

/**
 * Ring-3 tracking structure for an allocation chunk ring-3 mapping.
 *
 * The primary tree (Core) uses the chunk id as key.
 */
typedef struct PGMCHUNKR3MAP
{
    /** The key is the chunk id. */
    AVLU32NODECORE                      Core;
    /** The time (ChunkR3Map.iNow) this chunk was last used.  Used for unmap
     *  selection. */
    uint32_t                            iLastUsed;
    /** The current reference count. */
    uint32_t volatile                   cRefs;
    /** The current permanent reference count. */
    uint32_t volatile                   cPermRefs;
    /** The mapping address. */
    void                               *pv;
} PGMCHUNKR3MAP;

/**
 * Allocation chunk ring-3 mapping TLB entry.
 */
typedef struct PGMCHUNKR3MAPTLBE
{
    /** The chunk id. */
    uint32_t volatile                   idChunk;
#if HC_ARCH_BITS == 64
    uint32_t                            u32Padding; /**< alignment padding. */
#endif
    /** The chunk map. */
    R3PTRTYPE(PPGMCHUNKR3MAP) volatile  pChunk;
} PGMCHUNKR3MAPTLBE;
/** Pointer to the an allocation chunk ring-3 mapping TLB entry. */
typedef PGMCHUNKR3MAPTLBE *PPGMCHUNKR3MAPTLBE;

/** The number of TLB entries in PGMCHUNKR3MAPTLB.
 * @remark Must be a power of two value. */
#define PGM_CHUNKR3MAPTLB_ENTRIES   64

/**
 * Allocation chunk ring-3 mapping TLB.
 *
 * @remarks We use a TLB to speed up lookups by avoiding walking the AVL.
 *          At first glance this might look kinda odd since AVL trees are
 *          supposed to give the most optimal lookup times of all trees
 *          due to their balancing. However, take a tree with 1023 nodes
 *          in it, that's 10 levels, meaning that most searches has to go
 *          down 9 levels before they find what they want. This isn't fast
 *          compared to a TLB hit. There is the factor of cache misses,
 *          and of course the problem with trees and branch prediction.
 *          This is why we use TLBs in front of most of the trees.
 *
 * @todo    Generalize this TLB + AVL stuff, shouldn't be all that
 *          difficult when we switch to the new inlined AVL trees (from kStuff).
 */
typedef struct PGMCHUNKR3MAPTLB
{
    /** The TLB entries. */
    PGMCHUNKR3MAPTLBE   aEntries[PGM_CHUNKR3MAPTLB_ENTRIES];
} PGMCHUNKR3MAPTLB;

/**
 * Calculates the index of a guest page in the Ring-3 Chunk TLB.
 * @returns Chunk TLB index.
 * @param   idChunk         The Chunk ID.
 */
#define PGM_CHUNKR3MAPTLB_IDX(idChunk)     ( (idChunk) & (PGM_CHUNKR3MAPTLB_ENTRIES - 1) )


/**
 * Ring-3 guest page mapping TLB entry.
 * @remarks used in ring-0 as well at the moment.
 */
typedef struct PGMPAGER3MAPTLBE
{
    /** Address of the page. */
    RTGCPHYS volatile                   GCPhys;
    /** The guest page. */
    R3PTRTYPE(PPGMPAGE) volatile        pPage;
    /** Pointer to the page mapping tracking structure, PGMCHUNKR3MAP. */
    R3PTRTYPE(PPGMCHUNKR3MAP) volatile  pMap;
    /** The address */
    R3PTRTYPE(void *) volatile          pv;
#if HC_ARCH_BITS == 32
    uint32_t                            u32Padding; /**< alignment padding. */
#endif
} PGMPAGER3MAPTLBE;
/** Pointer to an entry in the HC physical TLB. */
typedef PGMPAGER3MAPTLBE *PPGMPAGER3MAPTLBE;


/** The number of entries in the ring-3 guest page mapping TLB.
 * @remarks The value must be a power of two. */
#define PGM_PAGER3MAPTLB_ENTRIES 256

/**
 * Ring-3 guest page mapping TLB.
 * @remarks used in ring-0 as well at the moment.
 */
typedef struct PGMPAGER3MAPTLB
{
    /** The TLB entries. */
    PGMPAGER3MAPTLBE            aEntries[PGM_PAGER3MAPTLB_ENTRIES];
} PGMPAGER3MAPTLB;
/** Pointer to the ring-3 guest page mapping TLB. */
typedef PGMPAGER3MAPTLB *PPGMPAGER3MAPTLB;

/**
 * Calculates the index of the TLB entry for the specified guest page.
 * @returns Physical TLB index.
 * @param   GCPhys      The guest physical address.
 */
#define PGM_PAGER3MAPTLB_IDX(GCPhys)    ( ((GCPhys) >> GUEST_PAGE_SHIFT) & (PGM_PAGER3MAPTLB_ENTRIES - 1) )

/** @} */


/** @name Ring-0 page mapping TLB
 * @{  */
/**
 * Ring-0 guest page mapping TLB entry.
 */
typedef struct PGMPAGER0MAPTLBE
{
    /** Address of the page. */
    RTGCPHYS volatile                   GCPhys;
    /** The guest page. */
    R0PTRTYPE(PPGMPAGE) volatile        pPage;
    /** The address */
    R0PTRTYPE(void *) volatile          pv;
} PGMPAGER0MAPTLBE;
/** Pointer to an entry in the HC physical TLB. */
typedef PGMPAGER0MAPTLBE *PPGMPAGER0MAPTLBE;


/** The number of entries in the ring-0 guest page mapping TLB.
 * @remarks The value must be a power of two. */
#define PGM_PAGER0MAPTLB_ENTRIES 256

/**
 * Ring-0 guest page mapping TLB.
 */
typedef struct PGMPAGER0MAPTLB
{
    /** The TLB entries. */
    PGMPAGER0MAPTLBE            aEntries[PGM_PAGER0MAPTLB_ENTRIES];
} PGMPAGER0MAPTLB;
/** Pointer to the ring-3 guest page mapping TLB. */
typedef PGMPAGER0MAPTLB *PPGMPAGER0MAPTLB;

/**
 * Calculates the index of the TLB entry for the specified guest page.
 * @returns Physical TLB index.
 * @param   GCPhys      The guest physical address.
 */
#define PGM_PAGER0MAPTLB_IDX(GCPhys)    ( ((GCPhys) >> GUEST_PAGE_SHIFT) & (PGM_PAGER0MAPTLB_ENTRIES - 1) )
/** @} */


/** @name Context neutral page mapper TLB.
 *
 * Hoping to avoid some code and bug duplication parts of the GCxxx->CCPtr
 * code is writting in a kind of context neutral way. Time will show whether
 * this actually makes sense or not...
 *
 * @todo this needs to be reconsidered and dropped/redone since the ring-0
 *       context ends up using a global mapping cache on some platforms
 *       (darwin).
 *
 * @{ */
/** @typedef PPGMPAGEMAPTLB
 * The page mapper TLB pointer type for the current context. */
/** @typedef PPGMPAGEMAPTLB
 * The page mapper TLB entry pointer type for the current context. */
/** @typedef PPGMPAGEMAPTLB
 * The page mapper TLB entry pointer pointer type for the current context. */
/** @def PGM_PAGEMAPTLB_ENTRIES
 * The number of TLB entries in the page mapper TLB for the current context. */
/** @def PGM_PAGEMAPTLB_IDX
 * Calculate the TLB index for a guest physical address.
 * @returns The TLB index.
 * @param   GCPhys      The guest physical address. */
/** @typedef PPGMPAGEMAP
 * Pointer to a page mapper unit for current context. */
/** @typedef PPPGMPAGEMAP
 * Pointer to a page mapper unit pointer for current context. */
#if defined(IN_RING0)
typedef PPGMPAGER0MAPTLB                PPGMPAGEMAPTLB;
typedef PPGMPAGER0MAPTLBE               PPGMPAGEMAPTLBE;
typedef PPGMPAGER0MAPTLBE              *PPPGMPAGEMAPTLBE;
# define PGM_PAGEMAPTLB_ENTRIES         PGM_PAGER0MAPTLB_ENTRIES
# define PGM_PAGEMAPTLB_IDX(GCPhys)     PGM_PAGER0MAPTLB_IDX(GCPhys)
typedef struct PGMCHUNKR0MAP           *PPGMPAGEMAP;
typedef struct PGMCHUNKR0MAP          **PPPGMPAGEMAP;
#else
typedef PPGMPAGER3MAPTLB                PPGMPAGEMAPTLB;
typedef PPGMPAGER3MAPTLBE               PPGMPAGEMAPTLBE;
typedef PPGMPAGER3MAPTLBE              *PPPGMPAGEMAPTLBE;
# define PGM_PAGEMAPTLB_ENTRIES         PGM_PAGER3MAPTLB_ENTRIES
# define PGM_PAGEMAPTLB_IDX(GCPhys)     PGM_PAGER3MAPTLB_IDX(GCPhys)
typedef PPGMCHUNKR3MAP                  PPGMPAGEMAP;
typedef PPPGMCHUNKR3MAP                 PPPGMPAGEMAP;
#endif
/** @} */


/** @name PGM Pool Indexes.
 * Aka. the unique shadow page identifier.
 * @{ */
/** NIL page pool IDX. */
#define NIL_PGMPOOL_IDX                 0
/** The first normal index.  There used to be 5 fictive pages up front, now
 * there is only the NIL page. */
#define PGMPOOL_IDX_FIRST               1
/** The last valid index. (inclusive, 14 bits) */
#define PGMPOOL_IDX_LAST                0x3fff
/** @} */

/** The NIL index for the parent chain. */
#define NIL_PGMPOOL_USER_INDEX          ((uint16_t)0xffff)
#define NIL_PGMPOOL_PRESENT_INDEX       ((uint16_t)0xffff)

/**
 * Node in the chain linking a shadowed page to it's parent (user).
 */
#pragma pack(1)
typedef struct PGMPOOLUSER
{
    /** The index to the next item in the chain. NIL_PGMPOOL_USER_INDEX is no next. */
    uint16_t            iNext;
    /** The user page index. */
    uint16_t            iUser;
    /** Index into the user table. */
    uint32_t            iUserTable;
} PGMPOOLUSER, *PPGMPOOLUSER;
typedef const PGMPOOLUSER *PCPGMPOOLUSER;
#pragma pack()


/** The NIL index for the phys ext chain. */
#define NIL_PGMPOOL_PHYSEXT_INDEX       ((uint16_t)0xffff)
/** The NIL pte index for a phys ext chain slot. */
#define NIL_PGMPOOL_PHYSEXT_IDX_PTE     ((uint16_t)0xffff)

/**
 * Node in the chain of physical cross reference extents.
 * @todo Calling this an 'extent' is not quite right, find a better name.
 * @todo find out the optimal size of the aidx array
 */
#pragma pack(1)
typedef struct PGMPOOLPHYSEXT
{
    /** The index to the next item in the chain. NIL_PGMPOOL_PHYSEXT_INDEX is no next. */
    uint16_t            iNext;
    /** Alignment. */
    uint16_t            u16Align;
    /** The user page index. */
    uint16_t            aidx[3];
    /** The page table index or NIL_PGMPOOL_PHYSEXT_IDX_PTE if unknown. */
    uint16_t            apte[3];
} PGMPOOLPHYSEXT, *PPGMPOOLPHYSEXT;
typedef const PGMPOOLPHYSEXT *PCPGMPOOLPHYSEXT;
#pragma pack()


/**
 * The kind of page that's being shadowed.
 */
typedef enum PGMPOOLKIND
{
    /** The virtual invalid 0 entry. */
    PGMPOOLKIND_INVALID = 0,
    /** The entry is free (=unused). */
    PGMPOOLKIND_FREE,

    /** Shw: 32-bit page table;     Gst: no paging. */
    PGMPOOLKIND_32BIT_PT_FOR_PHYS,
    /** Shw: 32-bit page table;     Gst: 32-bit page table. */
    PGMPOOLKIND_32BIT_PT_FOR_32BIT_PT,
    /** Shw: 32-bit page table;     Gst: 4MB page. */
    PGMPOOLKIND_32BIT_PT_FOR_32BIT_4MB,
    /** Shw: PAE page table;        Gst: no paging. */
    PGMPOOLKIND_PAE_PT_FOR_PHYS,
    /** Shw: PAE page table;        Gst: 32-bit page table. */
    PGMPOOLKIND_PAE_PT_FOR_32BIT_PT,
    /** Shw: PAE page table;        Gst: Half of a 4MB page. */
    PGMPOOLKIND_PAE_PT_FOR_32BIT_4MB,
    /** Shw: PAE page table;        Gst: PAE page table. */
    PGMPOOLKIND_PAE_PT_FOR_PAE_PT,
    /** Shw: PAE page table;        Gst: 2MB page. */
    PGMPOOLKIND_PAE_PT_FOR_PAE_2MB,

    /** Shw: 32-bit page directory. Gst: 32-bit page directory. */
    PGMPOOLKIND_32BIT_PD,
    /** Shw: 32-bit page directory. Gst: no paging. */
    PGMPOOLKIND_32BIT_PD_PHYS,
    /** Shw: PAE page directory 0;  Gst: 32-bit page directory. */
    PGMPOOLKIND_PAE_PD0_FOR_32BIT_PD,
    /** Shw: PAE page directory 1;  Gst: 32-bit page directory. */
    PGMPOOLKIND_PAE_PD1_FOR_32BIT_PD,
    /** Shw: PAE page directory 2;  Gst: 32-bit page directory. */
    PGMPOOLKIND_PAE_PD2_FOR_32BIT_PD,
    /** Shw: PAE page directory 3;  Gst: 32-bit page directory. */
    PGMPOOLKIND_PAE_PD3_FOR_32BIT_PD,
    /** Shw: PAE page directory;    Gst: PAE page directory. */
    PGMPOOLKIND_PAE_PD_FOR_PAE_PD,
    /** Shw: PAE page directory;    Gst: no paging.             Note: +NP. */
    PGMPOOLKIND_PAE_PD_PHYS,

    /** Shw: PAE page directory pointer table (legacy, 4 entries);  Gst 32 bits paging. */
    PGMPOOLKIND_PAE_PDPT_FOR_32BIT,
    /** Shw: PAE page directory pointer table (legacy, 4 entries);  Gst PAE PDPT. */
    PGMPOOLKIND_PAE_PDPT,
    /** Shw: PAE page directory pointer table (legacy, 4 entries);  Gst: no paging. */
    PGMPOOLKIND_PAE_PDPT_PHYS,

    /** Shw: 64-bit page directory pointer table;   Gst: 64-bit page directory pointer table. */
    PGMPOOLKIND_64BIT_PDPT_FOR_64BIT_PDPT,
    /** Shw: 64-bit page directory pointer table;   Gst: no paging. */
    PGMPOOLKIND_64BIT_PDPT_FOR_PHYS,
    /** Shw: 64-bit page directory table;           Gst: 64-bit page directory table. */
    PGMPOOLKIND_64BIT_PD_FOR_64BIT_PD,
    /** Shw: 64-bit page directory table;           Gst: no paging. */
    PGMPOOLKIND_64BIT_PD_FOR_PHYS, /* 24 */

    /** Shw: 64-bit PML4;                           Gst: 64-bit PML4. */
    PGMPOOLKIND_64BIT_PML4,

    /** Shw: EPT page directory pointer table;      Gst: no paging. */
    PGMPOOLKIND_EPT_PDPT_FOR_PHYS,
    /** Shw: EPT page directory table;              Gst: no paging. */
    PGMPOOLKIND_EPT_PD_FOR_PHYS,
    /** Shw: EPT page table;                        Gst: no paging. */
    PGMPOOLKIND_EPT_PT_FOR_PHYS,

    /** Shw: Root Nested paging table. */
    PGMPOOLKIND_ROOT_NESTED,

    /** Shw: EPT page table;                        Gst: EPT page table. */
    PGMPOOLKIND_EPT_PT_FOR_EPT_PT,
    /** Shw: EPT page table;                        Gst: 2MB page. */
    PGMPOOLKIND_EPT_PT_FOR_EPT_2MB,
    /** Shw: EPT page directory table;              Gst: EPT page directory. */
    PGMPOOLKIND_EPT_PD_FOR_EPT_PD,
    /** Shw: EPT page directory pointer table;      Gst: EPT page directory pointer table. */
    PGMPOOLKIND_EPT_PDPT_FOR_EPT_PDPT,
    /** Shw: EPT PML4;                              Gst: EPT PML4. */
    PGMPOOLKIND_EPT_PML4_FOR_EPT_PML4,

    /** The last valid entry. */
    PGMPOOLKIND_LAST = PGMPOOLKIND_EPT_PML4_FOR_EPT_PML4
} PGMPOOLKIND;

/**
 * The access attributes of the page; only applies to big pages.
 */
typedef enum
{
    PGMPOOLACCESS_DONTCARE = 0,
    PGMPOOLACCESS_USER_RW,
    PGMPOOLACCESS_USER_R,
    PGMPOOLACCESS_USER_RW_NX,
    PGMPOOLACCESS_USER_R_NX,
    PGMPOOLACCESS_SUPERVISOR_RW,
    PGMPOOLACCESS_SUPERVISOR_R,
    PGMPOOLACCESS_SUPERVISOR_RW_NX,
    PGMPOOLACCESS_SUPERVISOR_R_NX
} PGMPOOLACCESS;

/**
 * The tracking data for a page in the pool.
 */
typedef struct PGMPOOLPAGE
{
    /** AVL node code with the (HC) physical address of this page. */
    AVLOHCPHYSNODECORE  Core;
    /** Pointer to the R3 mapping of the page. */
    R3PTRTYPE(void *)   pvPageR3;
    /** Pointer to the R0 mapping of the page. */
    R0PTRTYPE(void *)   pvPageR0;
    /** The guest physical address. */
    RTGCPHYS            GCPhys;
    /** The kind of page we're shadowing. (This is really a PGMPOOLKIND enum.) */
    uint8_t             enmKind;
    /** The subkind of page we're shadowing. (This is really a PGMPOOLACCESS enum.) */
    uint8_t             enmAccess;
    /** This supplements enmKind and enmAccess */
    bool                fA20Enabled : 1;

    /** Used to indicate that the page is zeroed. */
    bool                fZeroed : 1;
    /** Used to indicate that a PT has non-global entries. */
    bool                fSeenNonGlobal : 1;
    /** Used to indicate that we're monitoring writes to the guest page. */
    bool                fMonitored : 1;
    /** Used to indicate that the page is in the cache (e.g. in the GCPhys hash).
     * (All pages are in the age list.) */
    bool                fCached : 1;
    /** This is used by the R3 access handlers when invoked by an async thread.
     * It's a hack required because of REMR3NotifyHandlerPhysicalDeregister. */
    bool volatile       fReusedFlushPending : 1;
    /** Used to mark the page as dirty (write monitoring is temporarily
     *  off). */
    bool                fDirty : 1;
    bool                fPadding1 : 1;
    bool                fPadding2;

    /** The index of this page. */
    uint16_t            idx;
    /** The next entry in the list this page currently resides in.
     * It's either in the free list or in the GCPhys hash. */
    uint16_t            iNext;
    /** Head of the user chain. NIL_PGMPOOL_USER_INDEX if not currently in use. */
    uint16_t            iUserHead;
    /** The number of present entries. */
    uint16_t            cPresent;
    /** The first entry in the table which is present. */
    uint16_t            iFirstPresent;
    /** The number of modifications to the monitored page. */
    uint16_t            cModifications;
    /** The next modified page. NIL_PGMPOOL_IDX if tail. */
    uint16_t            iModifiedNext;
    /** The previous modified page. NIL_PGMPOOL_IDX if head. */
    uint16_t            iModifiedPrev;
    /** The next page sharing access handler. NIL_PGMPOOL_IDX if tail. */
    uint16_t            iMonitoredNext;
    /** The previous page sharing access handler. NIL_PGMPOOL_IDX if head. */
    uint16_t            iMonitoredPrev;
    /** The next page in the age list. */
    uint16_t            iAgeNext;
    /** The previous page in the age list. */
    uint16_t            iAgePrev;
    /** Index into PGMPOOL::aDirtyPages if fDirty is set. */
    uint8_t             idxDirtyEntry;

    /** @name Access handler statistics to determine whether the guest is
     *        (re)initializing a page table.
     * @{ */
    RTGCPTR             GCPtrLastAccessHandlerRip;
    RTGCPTR             GCPtrLastAccessHandlerFault;
    uint64_t            cLastAccessHandler;
    /** @} */
    /** Used to indicate that this page can't be flushed. Important for cr3 root pages or shadow pae pd pages. */
    uint32_t volatile   cLocked;
#if GC_ARCH_BITS == 64
    uint32_t            u32Alignment3;
#endif
# ifdef VBOX_STRICT
    RTGCPTR             GCPtrDirtyFault;
# endif
} PGMPOOLPAGE;
/** Pointer to a pool page. */
typedef PGMPOOLPAGE *PPGMPOOLPAGE;
/** Pointer to a const pool page. */
typedef PGMPOOLPAGE const *PCPGMPOOLPAGE;
/** Pointer to a pool page pointer. */
typedef PGMPOOLPAGE **PPPGMPOOLPAGE;


/** The hash table size. */
# define PGMPOOL_HASH_SIZE      0x8000
/** The hash function. */
# define PGMPOOL_HASH(GCPhys)   ( ((GCPhys) >> GUEST_PAGE_SHIFT) & (PGMPOOL_HASH_SIZE - 1) )


/**
 * The shadow page pool instance data.
 *
 * It's all one big allocation made at init time, except for the
 * pages that is. The user nodes follows immediately after the
 * page structures.
 */
typedef struct PGMPOOL
{
    /** The VM handle - R3 Ptr. */
    PVMR3                       pVMR3;
    /** The VM handle - R0 Ptr. */
    R0PTRTYPE(PVMCC)            pVMR0;
    /** The ring-3 pointer to this structure. */
    R3PTRTYPE(struct PGMPOOL *) pPoolR3;
    /** The ring-0 pointer to this structure. */
    R0PTRTYPE(struct PGMPOOL *) pPoolR0;
    /** The max pool size. This includes the special IDs. */
    uint16_t                    cMaxPages;
    /** The current pool size. */
    uint16_t                    cCurPages;
    /** The head of the free page list. */
    uint16_t                    iFreeHead;
    /* Padding. */
    uint16_t                    u16Padding;
    /** Head of the chain of free user nodes. */
    uint16_t                    iUserFreeHead;
    /** The number of user nodes we've allocated. */
    uint16_t                    cMaxUsers;
    /** The number of present page table entries in the entire pool. */
    uint32_t                    cPresent;
    /** Pointer to the array of user nodes - R3 pointer. */
    R3PTRTYPE(PPGMPOOLUSER)     paUsersR3;
    /** Pointer to the array of user nodes - R0 pointer. */
    R0PTRTYPE(PPGMPOOLUSER)     paUsersR0;
    /** Head of the chain of free phys ext nodes. */
    uint16_t                    iPhysExtFreeHead;
    /** The number of user nodes we've allocated. */
    uint16_t                    cMaxPhysExts;
    uint32_t                    u32Padding0b;
    /** Pointer to the array of physical xref extent nodes - R3 pointer. */
    R3PTRTYPE(PPGMPOOLPHYSEXT)  paPhysExtsR3;
    /** Pointer to the array of physical xref extent nodes - R0 pointer. */
    R0PTRTYPE(PPGMPOOLPHYSEXT)  paPhysExtsR0;
    /** Hash table for GCPhys addresses. */
    uint16_t                    aiHash[PGMPOOL_HASH_SIZE];
    /** The head of the age list. */
    uint16_t                    iAgeHead;
    /** The tail of the age list. */
    uint16_t                    iAgeTail;
    /** Set if the cache is enabled. */
    bool                        fCacheEnabled;
    /** Alignment padding. */
    bool                        afPadding1[3];
    /** Head of the list of modified pages. */
    uint16_t                    iModifiedHead;
    /** The current number of modified pages. */
    uint16_t                    cModifiedPages;
    /** Alignment padding. */
    uint32_t                    u32Padding2;
    /** Physical access handler type registration handle. */
    PGMPHYSHANDLERTYPE          hAccessHandlerType;
    /** Next available slot (in aDirtyPages). */
    uint32_t                    idxFreeDirtyPage;
    /** Number of active dirty pages. */
    uint32_t                    cDirtyPages;
    /** Array of current dirty pgm pool page indices. */
    uint16_t                    aidxDirtyPages[16];
    /** Array running in parallel to aidxDirtyPages with the page data. */
    struct
    {
        uint64_t                aPage[512];
    } aDirtyPages[16];

    /** The number of pages currently in use. */
    uint16_t                    cUsedPages;
#ifdef VBOX_WITH_STATISTICS
    /** The high water mark for cUsedPages. */
    uint16_t                    cUsedPagesHigh;
    uint32_t                    Alignment1;         /**< Align the next member on a 64-bit boundary. */
    /** Profiling pgmPoolAlloc(). */
    STAMPROFILEADV              StatAlloc;
    /** Profiling pgmR3PoolClearDoIt(). */
    STAMPROFILE                 StatClearAll;
    /** Profiling pgmR3PoolReset(). */
    STAMPROFILE                 StatR3Reset;
    /** Profiling pgmPoolFlushPage(). */
    STAMPROFILE                 StatFlushPage;
    /** Profiling pgmPoolFree(). */
    STAMPROFILE                 StatFree;
    /** Counting explicit flushes by PGMPoolFlushPage(). */
    STAMCOUNTER                 StatForceFlushPage;
    /** Counting explicit flushes of dirty pages by PGMPoolFlushPage(). */
    STAMCOUNTER                 StatForceFlushDirtyPage;
    /** Counting flushes for reused pages. */
    STAMCOUNTER                 StatForceFlushReused;
    /** Profiling time spent zeroing pages. */
    STAMPROFILE                 StatZeroPage;
    /** Profiling of pgmPoolTrackDeref. */
    STAMPROFILE                 StatTrackDeref;
    /** Profiling pgmTrackFlushGCPhysPT. */
    STAMPROFILE                 StatTrackFlushGCPhysPT;
    /** Profiling pgmTrackFlushGCPhysPTs. */
    STAMPROFILE                 StatTrackFlushGCPhysPTs;
    /** Profiling pgmTrackFlushGCPhysPTsSlow. */
    STAMPROFILE                 StatTrackFlushGCPhysPTsSlow;
    /** Number of times we've been out of user records. */
    STAMCOUNTER                 StatTrackFreeUpOneUser;
    /** Nr of flushed entries. */
    STAMCOUNTER                 StatTrackFlushEntry;
    /** Nr of updated entries. */
    STAMCOUNTER                 StatTrackFlushEntryKeep;
    /** Profiling deref activity related tracking GC physical pages. */
    STAMPROFILE                 StatTrackDerefGCPhys;
    /** Number of linear searches for a HCPhys in the ram ranges. */
    STAMCOUNTER                 StatTrackLinearRamSearches;
    /** The number of failing pgmPoolTrackPhysExtAlloc calls. */
    STAMCOUNTER                 StamTrackPhysExtAllocFailures;

    /** Profiling the RC/R0 \#PF access handler. */
    STAMPROFILE                 StatMonitorPfRZ;
    /** Profiling the RC/R0 access we've handled (except REP STOSD). */
    STAMPROFILE                 StatMonitorPfRZHandled;
    /** Times we've failed interpreting the instruction. */
    STAMCOUNTER                 StatMonitorPfRZEmulateInstr;
    /** Profiling the pgmPoolFlushPage calls made from the RC/R0 access handler. */
    STAMPROFILE                 StatMonitorPfRZFlushPage;
    /** Times we've detected a page table reinit. */
    STAMCOUNTER                 StatMonitorPfRZFlushReinit;
    /** Counting flushes for pages that are modified too often. */
    STAMCOUNTER                 StatMonitorPfRZFlushModOverflow;
    /** Times we've detected fork(). */
    STAMCOUNTER                 StatMonitorPfRZFork;
    /** Times we've failed interpreting a patch code instruction. */
    STAMCOUNTER                 StatMonitorPfRZIntrFailPatch1;
    /** Times we've failed interpreting a patch code instruction during flushing. */
    STAMCOUNTER                 StatMonitorPfRZIntrFailPatch2;
    /** The number of times we've seen rep prefixes we can't handle. */
    STAMCOUNTER                 StatMonitorPfRZRepPrefix;
    /** Profiling the REP STOSD cases we've handled. */
    STAMPROFILE                 StatMonitorPfRZRepStosd;

    /** Profiling the R0/RC regular access handler. */
    STAMPROFILE                 StatMonitorRZ;
    /** Profiling the pgmPoolFlushPage calls made from the regular access handler in R0/RC. */
    STAMPROFILE                 StatMonitorRZFlushPage;
    /** Per access size counts indexed by size minus 1, last for larger. */
    STAMCOUNTER                 aStatMonitorRZSizes[16+3];
    /** Missaligned access counts indexed by offset - 1. */
    STAMCOUNTER                 aStatMonitorRZMisaligned[7];

    /** Nr of handled PT faults. */
    STAMCOUNTER                 StatMonitorRZFaultPT;
    /** Nr of handled PD faults. */
    STAMCOUNTER                 StatMonitorRZFaultPD;
    /** Nr of handled PDPT faults. */
    STAMCOUNTER                 StatMonitorRZFaultPDPT;
    /** Nr of handled PML4 faults. */
    STAMCOUNTER                 StatMonitorRZFaultPML4;

    /** Profiling the R3 access handler. */
    STAMPROFILE                 StatMonitorR3;
    /** Profiling the pgmPoolFlushPage calls made from the R3 access handler. */
    STAMPROFILE                 StatMonitorR3FlushPage;
    /** Per access size counts indexed by size minus 1, last for larger. */
    STAMCOUNTER                 aStatMonitorR3Sizes[16+3];
    /** Missaligned access counts indexed by offset - 1. */
    STAMCOUNTER                 aStatMonitorR3Misaligned[7];
    /** Nr of handled PT faults. */
    STAMCOUNTER                 StatMonitorR3FaultPT;
    /** Nr of handled PD faults. */
    STAMCOUNTER                 StatMonitorR3FaultPD;
    /** Nr of handled PDPT faults. */
    STAMCOUNTER                 StatMonitorR3FaultPDPT;
    /** Nr of handled PML4 faults. */
    STAMCOUNTER                 StatMonitorR3FaultPML4;

    /** Times we've called pgmPoolResetDirtyPages (and there were dirty page). */
    STAMCOUNTER                 StatResetDirtyPages;
    /** Times we've called pgmPoolAddDirtyPage. */
    STAMCOUNTER                 StatDirtyPage;
    /** Times we've had to flush duplicates for dirty page management. */
    STAMCOUNTER                 StatDirtyPageDupFlush;
    /** Times we've had to flush because of overflow. */
    STAMCOUNTER                 StatDirtyPageOverFlowFlush;

    /** The high water mark for cModifiedPages. */
    uint16_t                    cModifiedPagesHigh;
    uint16_t                    Alignment2[3];      /**< Align the next member on a 64-bit boundary. */

    /** The number of cache hits. */
    STAMCOUNTER                 StatCacheHits;
    /** The number of cache misses. */
    STAMCOUNTER                 StatCacheMisses;
    /** The number of times we've got a conflict of 'kind' in the cache. */
    STAMCOUNTER                 StatCacheKindMismatches;
    /** Number of times we've been out of pages. */
    STAMCOUNTER                 StatCacheFreeUpOne;
    /** The number of cacheable allocations. */
    STAMCOUNTER                 StatCacheCacheable;
    /** The number of uncacheable allocations. */
    STAMCOUNTER                 StatCacheUncacheable;
#else
    uint32_t                    Alignment3;         /**< Align the next member on a 64-bit boundary. */
#endif
    /** Profiling PGMR0PoolGrow(). */
    STAMPROFILE                 StatGrow;
    /** The AVL tree for looking up a page by its HC physical address. */
    AVLOHCPHYSTREE              HCPhysTree;
    uint32_t                    Alignment4;         /**< Align the next member on a 64-bit boundary. */
    /** Array of pages. (cMaxPages in length)
     * The Id is the index into thist array.
     */
    PGMPOOLPAGE                 aPages[PGMPOOL_IDX_FIRST];
} PGMPOOL, *PPGMPOOL, **PPPGMPOOL;
AssertCompileMemberAlignment(PGMPOOL, iModifiedHead, 8);
AssertCompileMemberAlignment(PGMPOOL, aDirtyPages, 8);
AssertCompileMemberAlignment(PGMPOOL, cUsedPages, 8);
#ifdef VBOX_WITH_STATISTICS
AssertCompileMemberAlignment(PGMPOOL, StatAlloc, 8);
#endif
AssertCompileMemberAlignment(PGMPOOL, aPages, 8);


/** @def PGMPOOL_PAGE_2_PTR
 * Maps a pool page pool into the current context.
 *
 * @returns VBox status code.
 * @param   a_pVM       Pointer to the VM.
 * @param   a_pPage     The pool page.
 *
 * @remark  In RC this uses PGMGCDynMapHCPage(), so it will consume of the
 *          small page window employeed by that function. Be careful.
 * @remark  There is no need to assert on the result.
 */
#if defined(VBOX_STRICT) || 1 /* temporarily going strict here */
# define PGMPOOL_PAGE_2_PTR(a_pVM, a_pPage)     pgmPoolMapPageStrict(a_pPage, __FUNCTION__)
DECLINLINE(void *) pgmPoolMapPageStrict(PPGMPOOLPAGE a_pPage, const char *pszCaller)
{
    RT_NOREF(pszCaller);
    AssertPtr(a_pPage);
    AssertMsg(RT_VALID_PTR(a_pPage->CTX_SUFF(pvPage)),
              ("enmKind=%d idx=%#x HCPhys=%RHp GCPhys=%RGp pvPageR3=%p pvPageR0=%p caller=%s\n",
               a_pPage->enmKind, a_pPage->idx, a_pPage->Core.Key, a_pPage->GCPhys, a_pPage->pvPageR3, a_pPage->pvPageR0, pszCaller));
    return a_pPage->CTX_SUFF(pvPage);
}
#else
# define PGMPOOL_PAGE_2_PTR(pVM, a_pPage)       ((a_pPage)->CTX_SUFF(pvPage))
#endif


/** @def PGMPOOL_PAGE_2_PTR_V2
 * Maps a pool page pool into the current context, taking both VM and VMCPU.
 *
 * @returns VBox status code.
 * @param   a_pVM       Pointer to the VM.
 * @param   a_pVCpu     The current CPU.
 * @param   a_pPage     The pool page.
 *
 * @remark  In RC this uses PGMGCDynMapHCPage(), so it will consume of the
 *          small page window employeed by that function. Be careful.
 * @remark  There is no need to assert on the result.
 */
#define PGMPOOL_PAGE_2_PTR_V2(a_pVM, a_pVCpu, a_pPage)     PGMPOOL_PAGE_2_PTR((a_pVM), (a_pPage))


/** @def PGMPOOL_PAGE_IS_NESTED
 * Checks whether the given pool page is a nested-guest pool page.
 *
 * @returns @c true if a nested-guest pool page, @c false otherwise.
 * @param   a_pPage     The pool page.
 * @todo    We can optimize the conditionals later.
 */
#define PGMPOOL_PAGE_IS_NESTED(a_pPage)         PGMPOOL_PAGE_IS_KIND_NESTED((a_pPage)->enmKind)
#define PGMPOOL_PAGE_IS_KIND_NESTED(a_enmKind)  (   (a_enmKind) == PGMPOOLKIND_EPT_PT_FOR_EPT_PT     \
                                                 || (a_enmKind) == PGMPOOLKIND_EPT_PT_FOR_EPT_2MB    \
                                                 || (a_enmKind) == PGMPOOLKIND_EPT_PD_FOR_EPT_PD     \
                                                 || (a_enmKind) == PGMPOOLKIND_EPT_PDPT_FOR_EPT_PDPT \
                                                 || (a_enmKind) == PGMPOOLKIND_EPT_PML4_FOR_EPT_PML4)

/** @name Per guest page tracking data.
 * This is currently as a 16-bit word in the PGMPAGE structure, the idea though
 * is to use more bits for it and split it up later on. But for now we'll play
 * safe and change as little as possible.
 *
 * The 16-bit word has two parts:
 *
 * The first 14-bit forms the @a idx field. It is either the index of a page in
 * the shadow page pool, or and index into the extent list.
 *
 * The 2 topmost bits makes up the @a cRefs field, which counts the number of
 * shadow page pool references to the page. If cRefs equals
 * PGMPOOL_CREFS_PHYSEXT, then the @a idx field is an indext into the extent
 * (misnomer) table and not the shadow page pool.
 *
 * See PGM_PAGE_GET_TRACKING and PGM_PAGE_SET_TRACKING for how to get and set
 * the 16-bit word.
 *
 * @{ */
/** The shift count for getting to the cRefs part. */
#define PGMPOOL_TD_CREFS_SHIFT          14
/** The mask applied after shifting the tracking data down by
 * PGMPOOL_TD_CREFS_SHIFT. */
#define PGMPOOL_TD_CREFS_MASK           0x3
/** The cRefs value used to indicate that the idx is the head of a
 * physical cross reference list. */
#define PGMPOOL_TD_CREFS_PHYSEXT        PGMPOOL_TD_CREFS_MASK
/** The shift used to get idx. */
#define PGMPOOL_TD_IDX_SHIFT            0
/** The mask applied to the idx after shifting down by PGMPOOL_TD_IDX_SHIFT. */
#define PGMPOOL_TD_IDX_MASK             0x3fff
/** The idx value when we're out of of PGMPOOLPHYSEXT entries or/and there are
 * simply too many mappings of this page. */
#define PGMPOOL_TD_IDX_OVERFLOWED       PGMPOOL_TD_IDX_MASK

/** @def PGMPOOL_TD_MAKE
 * Makes a 16-bit tracking data word.
 *
 * @returns tracking data.
 * @param   cRefs       The @a cRefs field. Must be within bounds!
 * @param   idx         The @a idx field. Must also be within bounds! */
#define PGMPOOL_TD_MAKE(cRefs, idx)     ( ((cRefs) << PGMPOOL_TD_CREFS_SHIFT) | (idx) )

/** @def PGMPOOL_TD_GET_CREFS
 * Get the @a cRefs field from a tracking data word.
 *
 * @returns The @a cRefs field
 * @param   u16         The tracking data word.
 * @remarks This will only return 1 or PGMPOOL_TD_CREFS_PHYSEXT for a
 *          non-zero @a u16. */
#define PGMPOOL_TD_GET_CREFS(u16)       ( ((u16) >> PGMPOOL_TD_CREFS_SHIFT) & PGMPOOL_TD_CREFS_MASK )

/** @def PGMPOOL_TD_GET_IDX
 * Get the @a idx field from a tracking data word.
 *
 * @returns The @a idx field
 * @param   u16         The tracking data word. */
#define PGMPOOL_TD_GET_IDX(u16)         ( ((u16) >> PGMPOOL_TD_IDX_SHIFT)   & PGMPOOL_TD_IDX_MASK   )
/** @} */



/** @name A20 gate macros
 * @{ */
#define PGM_WITH_A20
#ifdef PGM_WITH_A20
# define PGM_A20_IS_ENABLED(a_pVCpu)                        ((a_pVCpu)->pgm.s.fA20Enabled)
# define PGM_A20_APPLY(a_pVCpu, a_GCPhys)                   ((a_GCPhys) & (a_pVCpu)->pgm.s.GCPhysA20Mask)
# define PGM_A20_APPLY_TO_VAR(a_pVCpu, a_GCPhysVar)         \
    do { a_GCPhysVar &= (a_pVCpu)->pgm.s.GCPhysA20Mask; } while (0)
# define PGM_A20_ASSERT_MASKED(pVCpu, a_GCPhys)             Assert(PGM_A20_APPLY(pVCpu, a_GCPhys) == (a_GCPhys))
#else
# define PGM_A20_IS_ENABLED(a_pVCpu)                        (true)
# define PGM_A20_APPLY(a_pVCpu, a_GCPhys)                   (a_GCPhys)
# define PGM_A20_APPLY_TO_VAR(a_pVCpu, a_GCPhysVar)         do { } while (0)
# define PGM_A20_ASSERT_MASKED(pVCpu, a_GCPhys)             do { } while (0)
#endif
/** @} */


/**
 * Guest page table walk for the AMD64 mode.
 */
typedef struct PGMPTWALKGSTAMD64
{
    PX86PML4        pPml4;
    PX86PML4E       pPml4e;
    X86PML4E        Pml4e;

    PX86PDPT        pPdpt;
    PX86PDPE        pPdpe;
    X86PDPE         Pdpe;

    PX86PDPAE       pPd;
    PX86PDEPAE      pPde;
    X86PDEPAE       Pde;

    PX86PTPAE       pPt;
    PX86PTEPAE      pPte;
    X86PTEPAE       Pte;
} PGMPTWALKGSTAMD64;
/** Pointer to a AMD64 guest page table walk. */
typedef PGMPTWALKGSTAMD64 *PPGMPTWALKGSTAMD64;
/** Pointer to a const AMD64 guest page table walk. */
typedef PGMPTWALKGSTAMD64 const *PCPGMPTWALKGSTAMD64;

/**
 * Guest page table walk for the EPT mode.
 */
typedef struct PGMPTWALKGSTEPT
{
    PEPTPML4        pPml4;
    PEPTPML4E       pPml4e;
    EPTPML4E        Pml4e;

    PEPTPDPT        pPdpt;
    PEPTPDPTE       pPdpte;
    EPTPDPTE        Pdpte;

    PEPTPD          pPd;
    PEPTPDE         pPde;
    EPTPDE          Pde;

    PEPTPT          pPt;
    PEPTPTE         pPte;
    EPTPTE          Pte;
} PGMPTWALKGSTEPT;
/** Pointer to an EPT guest page table walk. */
typedef PGMPTWALKGSTEPT *PPGMPTWALKGSTEPT;
/** Pointer to a const EPT guest page table walk. */
typedef PGMPTWALKGSTEPT const *PCPGMPTWALKGSTEPT;

/**
 * Guest page table walk for the PAE mode.
 */
typedef struct PGMPTWALKGSTPAE
{
    PX86PDPT        pPdpt;
    PX86PDPE        pPdpe;
    X86PDPE         Pdpe;

    PX86PDPAE       pPd;
    PX86PDEPAE      pPde;
    X86PDEPAE       Pde;

    PX86PTPAE       pPt;
    PX86PTEPAE      pPte;
    X86PTEPAE       Pte;
} PGMPTWALKGSTPAE;
/** Pointer to a PAE guest page table walk. */
typedef PGMPTWALKGSTPAE *PPGMPTWALKGSTPAE;
/** Pointer to a const AMD64 guest page table walk. */
typedef PGMPTWALKGSTPAE const *PCPGMPTWALKGSTPAE;

/**
 * Guest page table walk for the 32-bit mode.
 */
typedef struct PGMPTWALKGST32BIT
{
    PX86PD          pPd;
    PX86PDE         pPde;
    X86PDE          Pde;

    PX86PT          pPt;
    PX86PTE         pPte;
    X86PTE          Pte;
} PGMPTWALKGST32BIT;
/** Pointer to a 32-bit guest page table walk. */
typedef PGMPTWALKGST32BIT *PPGMPTWALKGST32BIT;
/** Pointer to a const 32-bit guest page table walk. */
typedef PGMPTWALKGST32BIT const *PCPGMPTWALKGST32BIT;

/**
 * Which part of PGMPTWALKGST that is valid.
 */
typedef enum PGMPTWALKGSTTYPE
{
    /** Customary invalid 0 value. */
    PGMPTWALKGSTTYPE_INVALID = 0,
    /**  PGMPTWALKGST::u.Amd64 is valid. */
    PGMPTWALKGSTTYPE_AMD64,
    /**  PGMPTWALKGST::u.Pae is valid. */
    PGMPTWALKGSTTYPE_PAE,
    /**  PGMPTWALKGST::u.Legacy is valid. */
    PGMPTWALKGSTTYPE_32BIT,
    /** PGMPTWALKGST::u.Ept is valid. */
    PGMPTWALKGSTTYPE_EPT,
    /** Customary 32-bit type hack. */
    PGMPTWALKGSTTYPE_32BIT_HACK = 0x7fff0000
} PGMPTWALKGSTTYPE;

/**
 * Combined guest page table walk result.
 */
typedef struct PGMPTWALKGST
{
    union
    {
        /** The page walker for AMD64. */
        PGMPTWALKGSTAMD64   Amd64;
        /** The page walker for PAE (32-bit). */
        PGMPTWALKGSTPAE     Pae;
        /** The page walker for 32-bit paging (called legacy due to C naming
         * convension). */
        PGMPTWALKGST32BIT   Legacy;
        /** The page walker for EPT (SLAT). */
        PGMPTWALKGSTEPT     Ept;
    } u;
    /** Indicates which part of the union is valid. */
    PGMPTWALKGSTTYPE        enmType;
} PGMPTWALKGST;
/** Pointer to a combined guest page table walk result. */
typedef PGMPTWALKGST *PPGMPTWALKGST;
/** Pointer to a read-only combined guest page table walk result. */
typedef PGMPTWALKGST const *PCPGMPTWALKGST;


/** @name Paging mode macros
 * @{
 */
#ifdef IN_RING3
# define PGM_CTX(a,b)                   a##R3##b
# define PGM_CTX_STR(a,b)               a "R3" b
# define PGM_CTX_DECL(type)             DECLCALLBACK(type)
#elif defined(IN_RING0)
# define PGM_CTX(a,b)                   a##R0##b
# define PGM_CTX_STR(a,b)               a "R0" b
# define PGM_CTX_DECL(type)             VMMDECL(type)
#else
# error "Not IN_RING3 or IN_RING0!"
#endif

#define PGM_GST_NAME_REAL(name)                         PGM_CTX(pgm,GstReal##name)
#define PGM_GST_NAME_RC_REAL_STR(name)                  "pgmRCGstReal" #name
#define PGM_GST_NAME_R0_REAL_STR(name)                  "pgmR0GstReal" #name
#define PGM_GST_NAME_PROT(name)                         PGM_CTX(pgm,GstProt##name)
#define PGM_GST_NAME_RC_PROT_STR(name)                  "pgmRCGstProt" #name
#define PGM_GST_NAME_R0_PROT_STR(name)                  "pgmR0GstProt" #name
#define PGM_GST_NAME_32BIT(name)                        PGM_CTX(pgm,Gst32Bit##name)
#define PGM_GST_NAME_RC_32BIT_STR(name)                 "pgmRCGst32Bit" #name
#define PGM_GST_NAME_R0_32BIT_STR(name)                 "pgmR0Gst32Bit" #name
#define PGM_GST_NAME_PAE(name)                          PGM_CTX(pgm,GstPAE##name)
#define PGM_GST_NAME_RC_PAE_STR(name)                   "pgmRCGstPAE" #name
#define PGM_GST_NAME_R0_PAE_STR(name)                   "pgmR0GstPAE" #name
#define PGM_GST_NAME_AMD64(name)                        PGM_CTX(pgm,GstAMD64##name)
#define PGM_GST_NAME_RC_AMD64_STR(name)                 "pgmRCGstAMD64" #name
#define PGM_GST_NAME_R0_AMD64_STR(name)                 "pgmR0GstAMD64" #name
#define PGM_GST_DECL(type, name)                        PGM_CTX_DECL(type) PGM_GST_NAME(name)

#define PGM_GST_SLAT_NAME_EPT(name)                     PGM_CTX(pgm,GstSlatEpt##name)
#define PGM_GST_SLAT_NAME_RC_EPT_STR(name)              "pgmRCGstSlatEpt" #name
#define PGM_GST_SLAT_NAME_R0_EPT_STR(name)              "pgmR0GstSlatEpt" #name
#define PGM_GST_SLAT_DECL(type, name)                   PGM_CTX_DECL(type) PGM_GST_SLAT_NAME(name)

#define PGM_SHW_NAME_32BIT(name)                        PGM_CTX(pgm,Shw32Bit##name)
#define PGM_SHW_NAME_RC_32BIT_STR(name)                 "pgmRCShw32Bit" #name
#define PGM_SHW_NAME_R0_32BIT_STR(name)                 "pgmR0Shw32Bit" #name
#define PGM_SHW_NAME_PAE(name)                          PGM_CTX(pgm,ShwPAE##name)
#define PGM_SHW_NAME_RC_PAE_STR(name)                   "pgmRCShwPAE" #name
#define PGM_SHW_NAME_R0_PAE_STR(name)                   "pgmR0ShwPAE" #name
#define PGM_SHW_NAME_AMD64(name)                        PGM_CTX(pgm,ShwAMD64##name)
#define PGM_SHW_NAME_RC_AMD64_STR(name)                 "pgmRCShwAMD64" #name
#define PGM_SHW_NAME_R0_AMD64_STR(name)                 "pgmR0ShwAMD64" #name
#define PGM_SHW_NAME_NESTED_32BIT(name)                 PGM_CTX(pgm,ShwNested32Bit##name)
#define PGM_SHW_NAME_RC_NESTED_32BIT_STR(name)          "pgmRCShwNested32Bit" #name
#define PGM_SHW_NAME_R0_NESTED_32BIT_STR(name)          "pgmR0ShwNested32Bit" #name
#define PGM_SHW_NAME_NESTED_PAE(name)                   PGM_CTX(pgm,ShwNestedPAE##name)
#define PGM_SHW_NAME_RC_NESTED_PAE_STR(name)            "pgmRCShwNestedPAE" #name
#define PGM_SHW_NAME_R0_NESTED_PAE_STR(name)            "pgmR0ShwNestedPAE" #name
#define PGM_SHW_NAME_NESTED_AMD64(name)                 PGM_CTX(pgm,ShwNestedAMD64##name)
#define PGM_SHW_NAME_RC_NESTED_AMD64_STR(name)          "pgmRCShwNestedAMD64" #name
#define PGM_SHW_NAME_R0_NESTED_AMD64_STR(name)          "pgmR0ShwNestedAMD64" #name
#define PGM_SHW_NAME_EPT(name)                          PGM_CTX(pgm,ShwEPT##name)
#define PGM_SHW_NAME_RC_EPT_STR(name)                   "pgmRCShwEPT" #name
#define PGM_SHW_NAME_R0_EPT_STR(name)                   "pgmR0ShwEPT" #name
#define PGM_SHW_NAME_NONE(name)                         PGM_CTX(pgm,ShwNone##name)
#define PGM_SHW_NAME_RC_NONE_STR(name)                  "pgmRCShwNone" #name
#define PGM_SHW_NAME_R0_NONE_STR(name)                  "pgmR0ShwNone" #name
#define PGM_SHW_DECL(type, name)                        PGM_CTX_DECL(type) PGM_SHW_NAME(name)

/*                   Shw_Gst */
#define PGM_BTH_NAME_32BIT_REAL(name)                   PGM_CTX(pgm,Bth32BitReal##name)
#define PGM_BTH_NAME_32BIT_PROT(name)                   PGM_CTX(pgm,Bth32BitProt##name)
#define PGM_BTH_NAME_32BIT_32BIT(name)                  PGM_CTX(pgm,Bth32Bit32Bit##name)
#define PGM_BTH_NAME_PAE_REAL(name)                     PGM_CTX(pgm,BthPAEReal##name)
#define PGM_BTH_NAME_PAE_PROT(name)                     PGM_CTX(pgm,BthPAEProt##name)
#define PGM_BTH_NAME_PAE_32BIT(name)                    PGM_CTX(pgm,BthPAE32Bit##name)
#define PGM_BTH_NAME_PAE_PAE(name)                      PGM_CTX(pgm,BthPAEPAE##name)
#define PGM_BTH_NAME_AMD64_PROT(name)                   PGM_CTX(pgm,BthAMD64Prot##name)
#define PGM_BTH_NAME_AMD64_AMD64(name)                  PGM_CTX(pgm,BthAMD64AMD64##name)
#define PGM_BTH_NAME_NESTED_32BIT_REAL(name)            PGM_CTX(pgm,BthNested32BitReal##name)
#define PGM_BTH_NAME_NESTED_32BIT_PROT(name)            PGM_CTX(pgm,BthNested32BitProt##name)
#define PGM_BTH_NAME_NESTED_32BIT_32BIT(name)           PGM_CTX(pgm,BthNested32Bit32Bit##name)
#define PGM_BTH_NAME_NESTED_32BIT_PAE(name)             PGM_CTX(pgm,BthNested32BitPAE##name)
#define PGM_BTH_NAME_NESTED_32BIT_AMD64(name)           PGM_CTX(pgm,BthNested32BitAMD64##name)
#define PGM_BTH_NAME_NESTED_PAE_REAL(name)              PGM_CTX(pgm,BthNestedPAEReal##name)
#define PGM_BTH_NAME_NESTED_PAE_PROT(name)              PGM_CTX(pgm,BthNestedPAEProt##name)
#define PGM_BTH_NAME_NESTED_PAE_32BIT(name)             PGM_CTX(pgm,BthNestedPAE32Bit##name)
#define PGM_BTH_NAME_NESTED_PAE_PAE(name)               PGM_CTX(pgm,BthNestedPAEPAE##name)
#define PGM_BTH_NAME_NESTED_PAE_AMD64(name)             PGM_CTX(pgm,BthNestedPAEAMD64##name)
#define PGM_BTH_NAME_NESTED_AMD64_REAL(name)            PGM_CTX(pgm,BthNestedAMD64Real##name)
#define PGM_BTH_NAME_NESTED_AMD64_PROT(name)            PGM_CTX(pgm,BthNestedAMD64Prot##name)
#define PGM_BTH_NAME_NESTED_AMD64_32BIT(name)           PGM_CTX(pgm,BthNestedAMD6432Bit##name)
#define PGM_BTH_NAME_NESTED_AMD64_PAE(name)             PGM_CTX(pgm,BthNestedAMD64PAE##name)
#define PGM_BTH_NAME_NESTED_AMD64_AMD64(name)           PGM_CTX(pgm,BthNestedAMD64AMD64##name)
#define PGM_BTH_NAME_EPT_REAL(name)                     PGM_CTX(pgm,BthEPTReal##name)
#define PGM_BTH_NAME_EPT_PROT(name)                     PGM_CTX(pgm,BthEPTProt##name)
#define PGM_BTH_NAME_EPT_32BIT(name)                    PGM_CTX(pgm,BthEPT32Bit##name)
#define PGM_BTH_NAME_EPT_PAE(name)                      PGM_CTX(pgm,BthEPTPAE##name)
#define PGM_BTH_NAME_EPT_AMD64(name)                    PGM_CTX(pgm,BthEPTAMD64##name)
#define PGM_BTH_NAME_NONE_REAL(name)                    PGM_CTX(pgm,BthNoneReal##name)
#define PGM_BTH_NAME_NONE_PROT(name)                    PGM_CTX(pgm,BthNoneProt##name)
#define PGM_BTH_NAME_NONE_32BIT(name)                   PGM_CTX(pgm,BthNone32Bit##name)
#define PGM_BTH_NAME_NONE_PAE(name)                     PGM_CTX(pgm,BthNonePAE##name)
#define PGM_BTH_NAME_NONE_AMD64(name)                   PGM_CTX(pgm,BthNoneAMD64##name)

#define PGM_BTH_NAME_RC_32BIT_REAL_STR(name)            "pgmRCBth32BitReal" #name
#define PGM_BTH_NAME_RC_32BIT_PROT_STR(name)            "pgmRCBth32BitProt" #name
#define PGM_BTH_NAME_RC_32BIT_32BIT_STR(name)           "pgmRCBth32Bit32Bit" #name
#define PGM_BTH_NAME_RC_PAE_REAL_STR(name)              "pgmRCBthPAEReal" #name
#define PGM_BTH_NAME_RC_PAE_PROT_STR(name)              "pgmRCBthPAEProt" #name
#define PGM_BTH_NAME_RC_PAE_32BIT_STR(name)             "pgmRCBthPAE32Bit" #name
#define PGM_BTH_NAME_RC_PAE_PAE_STR(name)               "pgmRCBthPAEPAE" #name
#define PGM_BTH_NAME_RC_AMD64_AMD64_STR(name)           "pgmRCBthAMD64AMD64" #name
#define PGM_BTH_NAME_RC_NESTED_32BIT_REAL_STR(name)     "pgmRCBthNested32BitReal" #name
#define PGM_BTH_NAME_RC_NESTED_32BIT_PROT_STR(name)     "pgmRCBthNested32BitProt" #name
#define PGM_BTH_NAME_RC_NESTED_32BIT_32BIT_STR(name)    "pgmRCBthNested32Bit32Bit" #name
#define PGM_BTH_NAME_RC_NESTED_32BIT_PAE_STR(name)      "pgmRCBthNested32BitPAE" #name
#define PGM_BTH_NAME_RC_NESTED_32BIT_AMD64_STR(name)    "pgmRCBthNested32BitAMD64" #name
#define PGM_BTH_NAME_RC_NESTED_PAE_REAL_STR(name)       "pgmRCBthNestedPAEReal" #name
#define PGM_BTH_NAME_RC_NESTED_PAE_PROT_STR(name)       "pgmRCBthNestedPAEProt" #name
#define PGM_BTH_NAME_RC_NESTED_PAE_32BIT_STR(name)      "pgmRCBthNestedPAE32Bit" #name
#define PGM_BTH_NAME_RC_NESTED_PAE_PAE_STR(name)        "pgmRCBthNestedPAEPAE" #name
#define PGM_BTH_NAME_RC_NESTED_PAE_AMD64_STR(name)      "pgmRCBthNestedPAEAMD64" #name
#define PGM_BTH_NAME_RC_NESTED_AMD64_REAL_STR(name)     "pgmRCBthNestedAMD64Real" #name
#define PGM_BTH_NAME_RC_NESTED_AMD64_PROT_STR(name)     "pgmRCBthNestedAMD64Prot" #name
#define PGM_BTH_NAME_RC_NESTED_AMD64_32BIT_STR(name)    "pgmRCBthNestedAMD6432Bit" #name
#define PGM_BTH_NAME_RC_NESTED_AMD64_PAE_STR(name)      "pgmRCBthNestedAMD64PAE" #name
#define PGM_BTH_NAME_RC_NESTED_AMD64_AMD64_STR(name)    "pgmRCBthNestedAMD64AMD64" #name
#define PGM_BTH_NAME_RC_EPT_REAL_STR(name)              "pgmRCBthEPTReal" #name
#define PGM_BTH_NAME_RC_EPT_PROT_STR(name)              "pgmRCBthEPTProt" #name
#define PGM_BTH_NAME_RC_EPT_32BIT_STR(name)             "pgmRCBthEPT32Bit" #name
#define PGM_BTH_NAME_RC_EPT_PAE_STR(name)               "pgmRCBthEPTPAE" #name
#define PGM_BTH_NAME_RC_EPT_AMD64_STR(name)             "pgmRCBthEPTAMD64" #name

#define PGM_BTH_NAME_R0_32BIT_REAL_STR(name)            "pgmR0Bth32BitReal" #name
#define PGM_BTH_NAME_R0_32BIT_PROT_STR(name)            "pgmR0Bth32BitProt" #name
#define PGM_BTH_NAME_R0_32BIT_32BIT_STR(name)           "pgmR0Bth32Bit32Bit" #name
#define PGM_BTH_NAME_R0_PAE_REAL_STR(name)              "pgmR0BthPAEReal" #name
#define PGM_BTH_NAME_R0_PAE_PROT_STR(name)              "pgmR0BthPAEProt" #name
#define PGM_BTH_NAME_R0_PAE_32BIT_STR(name)             "pgmR0BthPAE32Bit" #name
#define PGM_BTH_NAME_R0_PAE_PAE_STR(name)               "pgmR0BthPAEPAE" #name
#define PGM_BTH_NAME_R0_AMD64_PROT_STR(name)            "pgmR0BthAMD64Prot" #name
#define PGM_BTH_NAME_R0_AMD64_AMD64_STR(name)           "pgmR0BthAMD64AMD64" #name
#define PGM_BTH_NAME_R0_NESTED_32BIT_REAL_STR(name)     "pgmR0BthNested32BitReal" #name
#define PGM_BTH_NAME_R0_NESTED_32BIT_PROT_STR(name)     "pgmR0BthNested32BitProt" #name
#define PGM_BTH_NAME_R0_NESTED_32BIT_32BIT_STR(name)    "pgmR0BthNested32Bit32Bit" #name
#define PGM_BTH_NAME_R0_NESTED_32BIT_PAE_STR(name)      "pgmR0BthNested32BitPAE" #name
#define PGM_BTH_NAME_R0_NESTED_32BIT_AMD64_STR(name)    "pgmR0BthNested32BitAMD64" #name
#define PGM_BTH_NAME_R0_NESTED_PAE_REAL_STR(name)       "pgmR0BthNestedPAEReal" #name
#define PGM_BTH_NAME_R0_NESTED_PAE_PROT_STR(name)       "pgmR0BthNestedPAEProt" #name
#define PGM_BTH_NAME_R0_NESTED_PAE_32BIT_STR(name)      "pgmR0BthNestedPAE32Bit" #name
#define PGM_BTH_NAME_R0_NESTED_PAE_PAE_STR(name)        "pgmR0BthNestedPAEPAE" #name
#define PGM_BTH_NAME_R0_NESTED_PAE_AMD64_STR(name)      "pgmR0BthNestedPAEAMD64" #name
#define PGM_BTH_NAME_R0_NESTED_AMD64_REAL_STR(name)     "pgmR0BthNestedAMD64Real" #name
#define PGM_BTH_NAME_R0_NESTED_AMD64_PROT_STR(name)     "pgmR0BthNestedAMD64Prot" #name
#define PGM_BTH_NAME_R0_NESTED_AMD64_32BIT_STR(name)    "pgmR0BthNestedAMD6432Bit" #name
#define PGM_BTH_NAME_R0_NESTED_AMD64_PAE_STR(name)      "pgmR0BthNestedAMD64PAE" #name
#define PGM_BTH_NAME_R0_NESTED_AMD64_AMD64_STR(name)    "pgmR0BthNestedAMD64AMD64" #name
#define PGM_BTH_NAME_R0_EPT_REAL_STR(name)              "pgmR0BthEPTReal" #name
#define PGM_BTH_NAME_R0_EPT_PROT_STR(name)              "pgmR0BthEPTProt" #name
#define PGM_BTH_NAME_R0_EPT_32BIT_STR(name)             "pgmR0BthEPT32Bit" #name
#define PGM_BTH_NAME_R0_EPT_PAE_STR(name)               "pgmR0BthEPTPAE" #name
#define PGM_BTH_NAME_R0_EPT_AMD64_STR(name)             "pgmR0BthEPTAMD64" #name

#define PGM_BTH_DECL(type, name)        PGM_CTX_DECL(type) PGM_BTH_NAME(name)
/** @} */


/**
 * Function pointers for guest paging.
 */
typedef struct PGMMODEDATAGST
{
    /** The guest mode type. */
    uint32_t                        uType;
    DECLCALLBACKMEMBER(int, pfnGetPage,(PVMCPUCC pVCpu, RTGCPTR GCPtr, PPGMPTWALK pWalk));
    DECLCALLBACKMEMBER(int, pfnQueryPageFast,(PVMCPUCC pVCpu, RTGCPTR GCPtr, uint32_t fFlags, PPGMPTWALKFAST pWalk));
    DECLCALLBACKMEMBER(int, pfnModifyPage,(PVMCPUCC pVCpu, RTGCPTR GCPtr, size_t cbPages, uint64_t fFlags, uint64_t fMask));
    DECLCALLBACKMEMBER(int, pfnEnter,(PVMCPUCC pVCpu, RTGCPHYS GCPhysCR3));
    DECLCALLBACKMEMBER(int, pfnExit,(PVMCPUCC pVCpu));
#ifdef IN_RING3
    DECLCALLBACKMEMBER(int, pfnRelocate,(PVMCPUCC pVCpu, RTGCPTR offDelta)); /**< Only in ring-3. */
#endif
} PGMMODEDATAGST;

/** The length of g_aPgmGuestModeData. */
#if VBOX_WITH_64_BITS_GUESTS
# define PGM_GUEST_MODE_DATA_ARRAY_SIZE     (PGM_TYPE_AMD64 + 1)
#else
# define PGM_GUEST_MODE_DATA_ARRAY_SIZE     (PGM_TYPE_PAE + 1)
#endif
/** The guest mode data array. */
extern PGMMODEDATAGST const g_aPgmGuestModeData[PGM_GUEST_MODE_DATA_ARRAY_SIZE];


/**
 * Function pointers for shadow paging.
 */
typedef struct PGMMODEDATASHW
{
    /** The shadow mode type. */
    uint32_t                        uType;
    DECLCALLBACKMEMBER(int, pfnGetPage,(PVMCPUCC pVCpu, RTGCPTR GCPtr, uint64_t *pfFlags, PRTHCPHYS pHCPhys));
    DECLCALLBACKMEMBER(int, pfnModifyPage,(PVMCPUCC pVCpu, RTGCPTR GCPtr, size_t cbPages, uint64_t fFlags,
                                                   uint64_t fMask, uint32_t fOpFlags));
    DECLCALLBACKMEMBER(int, pfnEnter,(PVMCPUCC pVCpu));
    DECLCALLBACKMEMBER(int, pfnExit,(PVMCPUCC pVCpu));
#ifdef IN_RING3
    DECLCALLBACKMEMBER(int, pfnRelocate,(PVMCPUCC pVCpu, RTGCPTR offDelta)); /**< Only in ring-3. */
#endif
} PGMMODEDATASHW;

/** The length of g_aPgmShadowModeData. */
#define PGM_SHADOW_MODE_DATA_ARRAY_SIZE     PGM_TYPE_END
/** The shadow mode data array. */
extern PGMMODEDATASHW const g_aPgmShadowModeData[PGM_SHADOW_MODE_DATA_ARRAY_SIZE];


/**
 * Function pointers for guest+shadow paging.
 */
typedef struct PGMMODEDATABTH
{
    /** The shadow mode type. */
    uint32_t                        uShwType;
    /** The guest mode type. */
    uint32_t                        uGstType;

    DECLCALLBACKMEMBER(int, pfnInvalidatePage,(PVMCPUCC pVCpu, RTGCPTR GCPtrPage));
    DECLCALLBACKMEMBER(int, pfnSyncCR3,(PVMCPUCC pVCpu, uint64_t cr0, uint64_t cr3, uint64_t cr4, bool fGlobal));
    DECLCALLBACKMEMBER(int, pfnPrefetchPage,(PVMCPUCC pVCpu, RTGCPTR GCPtrPage));
    DECLCALLBACKMEMBER(int, pfnVerifyAccessSyncPage,(PVMCPUCC pVCpu, RTGCPTR GCPtrPage, unsigned fFlags, unsigned uError));
    DECLCALLBACKMEMBER(int, pfnMapCR3,(PVMCPUCC pVCpu, RTGCPHYS GCPhysCR3));
    DECLCALLBACKMEMBER(int, pfnUnmapCR3,(PVMCPUCC pVCpu));
    DECLCALLBACKMEMBER(int, pfnEnter,(PVMCPUCC pVCpu, RTGCPHYS GCPhysCR3));
#ifndef IN_RING3
    DECLCALLBACKMEMBER(int, pfnTrap0eHandler,(PVMCPUCC pVCpu, RTGCUINT uErr, PCPUMCTX pCtx, RTGCPTR pvFault, bool *pfLockTaken));
    DECLCALLBACKMEMBER(int, pfnNestedTrap0eHandler,(PVMCPUCC pVCpu, RTGCUINT uErr, PCPUMCTX pCtx, RTGCPHYS GCPhysNested,
                                                    bool fIsLinearAddrValid, RTGCPTR GCPtrNested, PPGMPTWALK pWalk,
                                                    bool *pfLockTaken));
#endif
#ifdef VBOX_STRICT
    DECLCALLBACKMEMBER(unsigned, pfnAssertCR3,(PVMCPUCC pVCpu, uint64_t cr3, uint64_t cr4, RTGCPTR GCPtr, RTGCPTR cb));
#endif
} PGMMODEDATABTH;

/** The length of g_aPgmBothModeData. */
#define PGM_BOTH_MODE_DATA_ARRAY_SIZE       ((PGM_TYPE_END     - PGM_TYPE_FIRST_SHADOW) * PGM_TYPE_END)
/** The guest+shadow mode data array. */
extern PGMMODEDATABTH const g_aPgmBothModeData[PGM_BOTH_MODE_DATA_ARRAY_SIZE];


#ifdef VBOX_WITH_STATISTICS
/**
 * PGM statistics.
 */
typedef struct PGMSTATS
{
    /* R3 only: */
    STAMCOUNTER StatR3DetectedConflicts;            /**< R3: Number of times PGMR3MapHasConflicts() detected a conflict. */
    STAMPROFILE StatR3ResolveConflict;              /**< R3: pgmR3SyncPTResolveConflict() profiling (includes the entire relocation). */

    /* R3+RZ */
    STAMCOUNTER StatRZChunkR3MapTlbHits;            /**< RC/R0: Ring-3/0 chunk mapper TLB hits. */
    STAMCOUNTER StatRZChunkR3MapTlbMisses;          /**< RC/R0: Ring-3/0 chunk mapper TLB misses. */
    STAMCOUNTER StatRZPageMapTlbHits;               /**< RC/R0: Ring-3/0 page mapper TLB hits. */
    STAMCOUNTER StatRZPageMapTlbMisses;             /**< RC/R0: Ring-3/0 page mapper TLB misses. */
    STAMCOUNTER StatPageMapTlbFlushes;              /**< ALL: Ring-3/0 page mapper TLB flushes. */
    STAMCOUNTER StatPageMapTlbFlushEntry;           /**< ALL: Ring-3/0 page mapper TLB flushes. */
    STAMCOUNTER StatR3ChunkR3MapTlbHits;            /**< R3: Ring-3/0 chunk mapper TLB hits. */
    STAMCOUNTER StatR3ChunkR3MapTlbMisses;          /**< R3: Ring-3/0 chunk mapper TLB misses. */
    STAMCOUNTER StatR3PageMapTlbHits;               /**< R3: Ring-3/0 page mapper TLB hits. */
    STAMCOUNTER StatR3PageMapTlbMisses;             /**< R3: Ring-3/0 page mapper TLB misses. */
    STAMCOUNTER StatRZRamRangeTlbHits;              /**< RC/R0: RAM range TLB hits. */
    STAMCOUNTER StatRZRamRangeTlbMisses;            /**< RC/R0: RAM range TLB misses. */
    STAMCOUNTER StatR3RamRangeTlbHits;              /**< R3: RAM range TLB hits. */
    STAMCOUNTER StatR3RamRangeTlbMisses;            /**< R3: RAM range TLB misses. */
    STAMCOUNTER StatR3PhysHandlerReset;             /**< R3: The number of times PGMHandlerPhysicalReset is called. */
    STAMCOUNTER StatRZPhysHandlerReset;             /**< RC/R0: The number of times PGMHandlerPhysicalReset is called. */
    STAMCOUNTER StatR3PhysHandlerLookupHits;        /**< R3: Number of cache hits when looking up physical handlers. */
    STAMCOUNTER StatR3PhysHandlerLookupMisses;      /**< R3: Number of cache misses when looking up physical handlers. */
    STAMCOUNTER StatRZPhysHandlerLookupHits;        /**< RC/R0: Number of cache hits when lookup up physical handlers. */
    STAMCOUNTER StatRZPhysHandlerLookupMisses;      /**< RC/R0: Number of cache misses when looking up physical handlers */
    STAMCOUNTER StatRZPageReplaceShared;            /**< RC/R0: Times a shared page has been replaced by a private one. */
    STAMCOUNTER StatRZPageReplaceZero;              /**< RC/R0: Times the zero page has been replaced by a private one. */
/// @todo    STAMCOUNTER StatRZPageHandyAllocs;              /**< RC/R0: The number of times we've executed GMMR3AllocateHandyPages. */
    STAMCOUNTER StatR3PageReplaceShared;            /**< R3: Times a shared page has been replaced by a private one. */
    STAMCOUNTER StatR3PageReplaceZero;              /**< R3: Times the zero page has been replaced by a private one. */
/// @todo    STAMCOUNTER StatR3PageHandyAllocs;              /**< R3: The number of times we've executed GMMR3AllocateHandyPages. */

    /* RC only: */
    STAMCOUNTER StatRCInvlPgConflict;               /**< RC: Number of times PGMInvalidatePage() detected a mapping conflict. */
    STAMCOUNTER StatRCInvlPgSyncMonCR3;             /**< RC: Number of times PGMInvalidatePage() ran into PGM_SYNC_MONITOR_CR3. */

    STAMCOUNTER StatRZPhysRead;
    STAMCOUNTER StatRZPhysReadBytes;
    STAMCOUNTER StatRZPhysWrite;
    STAMCOUNTER StatRZPhysWriteBytes;
    STAMCOUNTER StatR3PhysRead;
    STAMCOUNTER StatR3PhysReadBytes;
    STAMCOUNTER StatR3PhysWrite;
    STAMCOUNTER StatR3PhysWriteBytes;
    STAMCOUNTER StatRCPhysRead;
    STAMCOUNTER StatRCPhysReadBytes;
    STAMCOUNTER StatRCPhysWrite;
    STAMCOUNTER StatRCPhysWriteBytes;

    STAMCOUNTER StatRZPhysSimpleRead;
    STAMCOUNTER StatRZPhysSimpleReadBytes;
    STAMCOUNTER StatRZPhysSimpleWrite;
    STAMCOUNTER StatRZPhysSimpleWriteBytes;
    STAMCOUNTER StatR3PhysSimpleRead;
    STAMCOUNTER StatR3PhysSimpleReadBytes;
    STAMCOUNTER StatR3PhysSimpleWrite;
    STAMCOUNTER StatR3PhysSimpleWriteBytes;
    STAMCOUNTER StatRCPhysSimpleRead;
    STAMCOUNTER StatRCPhysSimpleReadBytes;
    STAMCOUNTER StatRCPhysSimpleWrite;
    STAMCOUNTER StatRCPhysSimpleWriteBytes;

    STAMCOUNTER StatTrackVirgin;                    /**< The number of first time shadowings. */
    STAMCOUNTER StatTrackAliased;                   /**< The number of times switching to cRef2, i.e. the page is being shadowed by two PTs. */
    STAMCOUNTER StatTrackAliasedMany;               /**< The number of times we're tracking using cRef2. */
    STAMCOUNTER StatTrackAliasedLots;               /**< The number of times we're hitting pages which has overflowed cRef2. */
    STAMCOUNTER StatTrackNoExtentsLeft;             /**< The number of times the extent list was exhausted. */
    STAMCOUNTER StatTrackOverflows;                 /**< The number of times the extent list grows to long. */
    STAMPROFILE StatTrackDeref;                     /**< Profiling of SyncPageWorkerTrackDeref (expensive). */

    STAMPROFILE StatLargePageAlloc2;                /**< Time spent setting up newly allocated large pages. */
    STAMPROFILE StatLargePageSetup;                 /**< Time spent setting up newly allocated large pages. */
    /** pgmPhysIsValidLargePage profiling - R3 */
    STAMPROFILE StatR3IsValidLargePage;
    /** pgmPhysIsValidLargePage profiling - RZ*/
    STAMPROFILE StatRZIsValidLargePage;

    STAMPROFILE StatChunkAging;
    STAMPROFILE StatChunkFindCandidate;
    STAMPROFILE StatChunkUnmap;
    STAMPROFILE StatChunkMap;
} PGMSTATS;
#endif /* VBOX_WITH_STATISTICS */


/**
 * PGM Data (part of VM)
 */
typedef struct PGM
{
    /** The zero page. */
    uint8_t                         abZeroPg[RT_MAX(HOST_PAGE_SIZE, GUEST_PAGE_SIZE)];
    /** The MMIO placeholder page. */
    uint8_t                         abMmioPg[RT_MAX(HOST_PAGE_SIZE, GUEST_PAGE_SIZE)];

    /** @name   RAM, MMIO2 and ROM ranges
     * @{ */
    /** The RAM range lookup table. */
    PGMRAMRANGELOOKUPENTRY          aRamRangeLookup[PGM_MAX_RAM_RANGES];
    /** The ring-3 RAM range pointer table. */
    R3PTRTYPE(PPGMRAMRANGE)         apRamRanges[PGM_MAX_RAM_RANGES];
    /** MMIO2 ranges.  Indexed by idMmio2 minus 1. */
    PGMREGMMIO2RANGE                aMmio2Ranges[PGM_MAX_MMIO2_RANGES];
    /** The ring-3 RAM range pointer table running parallel to aMmio2Ranges. */
    R3PTRTYPE(PPGMRAMRANGE)         apMmio2RamRanges[PGM_MAX_MMIO2_RANGES];
    /** The ring-3 ROM range pointer table. */
    R3PTRTYPE(PPGMROMRANGE)         apRomRanges[PGM_MAX_ROM_RANGES];
    /** Union of generation ID and lookup count. */
    union PGMRAMRANGEGENANDLOOKUPCOUNT
    {
        /* Combined view of both the generation ID and the count for atomic updating/reading. */
        uint64_t volatile           u64Combined;
        RT_GCC_EXTENSION struct
        {
            /** Generation ID for the RAM ranges.
             * This member is incremented twice everytime a RAM range is mapped or
             * unmapped, so odd numbers means aRamRangeLookup is being modified and even
             * means the update has completed. */
            uint32_t volatile       idGeneration;
            /** The number of active entries in aRamRangeLookup. */
            uint32_t volatile       cLookupEntries;
        };
    } RamRangeUnion;
    /** The max RAM range ID (mirroring PGMR0PERVM::idRamRangeMax). */
    uint32_t                        idRamRangeMax;
    /** The number of MMIO2 ranges (serves as the next MMIO2 ID). */
    uint8_t                         cMmio2Ranges;
    /** The number of ROM ranges. */
    uint8_t                         cRomRanges;
    uint8_t                         abAlignment1[2];
    /** @}  */

    /** @name   The zero page (abPagePg).
     * @{ */
    /** The host physical address of the zero page. */
    RTHCPHYS                        HCPhysZeroPg;
    /** @}*/

    /** @name   The Invalid MMIO page (abMmioPg).
     * This page is filled with 0xfeedface.
     * @{ */
    /** The host physical address of the invalid MMIO page. */
    RTHCPHYS                        HCPhysMmioPg;
    /** The host pysical address of the invalid MMIO page plus all invalid
     * physical address bits set.  This is used to trigger X86_TRAP_PF_RSVD.
     * @remarks Check fLessThan52PhysicalAddressBits before use. */
    RTHCPHYS                        HCPhysInvMmioPg;
    /** @} */

    /** @cfgm{/RamPreAlloc, boolean, false}
     * Indicates whether the base RAM should all be allocated before starting
     * the VM (default), or if it should be allocated when first written to.
     */
    bool                            fRamPreAlloc;
#ifdef VBOX_WITH_PGM_NEM_MODE
    /** Set if we're operating in NEM memory mode.
     *
     * NEM mode implies that memory is allocated in big chunks for each RAM range
     * rather than on demand page by page.  Memory is also not locked and PGM has
     * therefore no physical addresses for them.  Page sharing is out of the
     * question.  Ballooning depends on the native execution engine, but probably
     * pointless as well.  */
    bool                            fNemMode;
# define PGM_IS_IN_NEM_MODE(a_pVM)  ((a_pVM)->pgm.s.fNemMode)
#else
# define PGM_IS_IN_NEM_MODE(a_pVM)  (false)
#endif
    /** Indicates whether write monitoring is currently in use.
     * This is used to prevent conflicts between live saving and page sharing
     * detection. */
    bool                            fPhysWriteMonitoringEngaged;
    /** Set if the CPU has less than 52-bit physical address width.
     * This is used */
    bool                            fLessThan52PhysicalAddressBits;
    /** Set when nested paging is active.
     * This is meant to save calls to HMIsNestedPagingActive and let the
     * compilers optimize the code better.  Whether we use nested paging or
     * not is something we find out during VMM initialization and we won't
     * change this later on. */
    bool                            fNestedPaging;
    /** We're not in a state which permits writes to guest memory.
     * (Only used in strict builds.) */
    bool                            fNoMorePhysWrites;
    /** @cfgm{/PageFusionAllowed, boolean, false}
     * Whether page fusion is allowed. */
    bool                            fPageFusionAllowed;
    /** @cfgm{/PGM/PciPassThrough, boolean, false}
     * Whether PCI passthrough is enabled. */
    bool                            fPciPassthrough;
    /** Restore original ROM page content when resetting after loading state.
     * The flag is set by pgmR3LoadRomRanges and cleared at reset.  This
     * enables the VM to start using an updated ROM without requiring powering
     * down the VM, just rebooting or resetting it. */
    bool                            fRestoreRomPagesOnReset;
    /** Whether to automatically clear all RAM pages on reset. */
    bool                            fZeroRamPagesOnReset;
    /** Large page enabled flag. */
    bool                            fUseLargePages;
    /** Alignment padding. */
#ifndef VBOX_WITH_PGM_NEM_MODE
    bool                            afAlignment2[2];
#else
    bool                            afAlignment2[1];
#endif
    /** The host paging mode. (This is what SUPLib reports.) */
    SUPPAGINGMODE                   enmHostMode;

    /** Physical access handler type for ROM protection. */
    PGMPHYSHANDLERTYPE              hRomPhysHandlerType;
    /** Physical access handler type for MMIO2 dirty page tracing. */
    PGMPHYSHANDLERTYPE              hMmio2DirtyPhysHandlerType;

    /** 4 MB page mask; 32 or 36 bits depending on PSE-36 (identical for all VCPUs) */
    RTGCPHYS                        GCPhys4MBPSEMask;
    /** Mask containing the invalid bits of a guest physical address.
     * @remarks this does not stop at bit 52. */
    RTGCPHYS                        GCPhysInvAddrMask;


    /** RAM range TLB for R3. */
    R3PTRTYPE(PPGMRAMRANGE)         apRamRangesTlb[PGM_RAMRANGE_TLB_ENTRIES];
    /** Shadow Page Pool - R3 Ptr. */
    R3PTRTYPE(PPGMPOOL)             pPoolR3;

    /** Shadow Page Pool - R0 Ptr. */
    R0PTRTYPE(PPGMPOOL)             pPoolR0;

    /** Hack: Number of deprecated page mapping locks taken by the current lock
     *  owner via pgmPhysGCPhys2CCPtrInternalDepr. */
    uint32_t                        cDeprecatedPageLocks;

    /** Registered physical access handler types. */
    uint32_t                        cPhysHandlerTypes;
    /** Physical access handler types.
     * Initialized to callback causing guru meditations and invalid enmKind. */
    PGMPHYSHANDLERTYPEINTR3         aPhysHandlerTypes[PGMPHYSHANDLERTYPE_COUNT];
    /** Physical handler allocator, ring-3 edition. */
#ifdef IN_RING3
    PGMPHYSHANDLERALLOCATOR         PhysHandlerAllocator;
#else
    RTCHardAvlTreeSlabAllocatorR3_T PhysHandlerAllocator;
#endif
    /** The pointer to the ring-3 mapping of the physical access handler tree. */
    R3PTRTYPE(PPGMPHYSHANDLERTREE)  pPhysHandlerTree;
    /** Caching the last physical handler we looked. */
    uint32_t                        idxLastPhysHandler;

    uint32_t                        au64Padding3[9];

    /** PGM critical section.
     * This protects the physical, ram ranges, and the page flag updating (some of
     * it anyway).
     */
    PDMCRITSECT                     CritSectX;

    /**
     * Data associated with managing the ring-3 mappings of the allocation chunks.
     */
    struct
    {
        /** The chunk mapping TLB. */
        PGMCHUNKR3MAPTLB            Tlb;
        /** The chunk tree, ordered by chunk id. */
        R3PTRTYPE(PAVLU32NODECORE)  pTree;
#if HC_ARCH_BITS == 32
        uint32_t                    u32Alignment0;
#endif
        /** The number of mapped chunks. */
        uint32_t                    c;
        /** @cfgm{/PGM/MaxRing3Chunks, uint32_t, host dependent}
         * The maximum number of mapped chunks.  On 64-bit this is unlimited by default,
         * on 32-bit it defaults to 1 or 3 GB depending on the host. */
        uint32_t                    cMax;
        /** The current time.  This is incremented whenever a chunk is inserted. */
        uint32_t                    iNow;
        /** Alignment padding. */
        uint32_t                    au32Alignment1[3];
    } ChunkR3Map;

    /** The page mapping TLB for ring-3. */
    PGMPAGER3MAPTLB                 PhysTlbR3;
    /** The page mapping TLB for ring-0. */
    PGMPAGER0MAPTLB                 PhysTlbR0;

    /** The number of handy pages. */
    uint32_t                        cHandyPages;

    /** The number of large handy pages. */
    uint32_t                        cLargeHandyPages;

    /**
     * Array of handy pages.
     *
     * This array is used in a two way communication between pgmPhysAllocPage
     * and GMMR0AllocateHandyPages, with PGMR3PhysAllocateHandyPages serving as
     * an intermediary.
     *
     * The size of this array is important, see pgmPhysEnsureHandyPage for details.
     * (The current size of 32 pages, means 128 KB of handy memory.)
     */
    GMMPAGEDESC                     aHandyPages[PGM_HANDY_PAGES];

    /**
     * Array of large handy pages. (currently size 1)
     *
     * This array is used in a two way communication between pgmPhysAllocLargePage
     * and GMMR0AllocateLargePage, with PGMR3PhysAllocateLargePage serving as
     * an intermediary.
     */
    GMMPAGEDESC                     aLargeHandyPage[1];
    /** When to try allocate large pages again after a failure. */
    uint64_t                        nsLargePageRetry;
    /** Number of repeated long allocation times.   */
    uint32_t                        cLargePageLongAllocRepeats;
    uint32_t                        uPadding4;

    /**
     * Live save data.
     */
    struct
    {
        /** Per type statistics. */
        struct
        {
            /** The number of ready pages. */
            uint32_t                cReadyPages;
            /** The number of dirty pages. */
            uint32_t                cDirtyPages;
            /** The number of ready zero pages. */
            uint32_t                cZeroPages;
            /** The number of write monitored pages. */
            uint32_t                cMonitoredPages;
        }                           Rom,
                                    Mmio2,
                                    Ram;
        /** The number of ignored pages in the RAM ranges (i.e. MMIO, MMIO2 and ROM). */
        uint32_t                    cIgnoredPages;
        /** Indicates that a live save operation is active. */
        bool                        fActive;
        /** Padding. */
        bool                        afReserved[2];
        /** The next history index. */
        uint8_t                     iDirtyPagesHistory;
        /** History of the total amount of dirty pages. */
        uint32_t                    acDirtyPagesHistory[64];
        /** Short term dirty page average. */
        uint32_t                    cDirtyPagesShort;
        /** Long term dirty page average. */
        uint32_t                    cDirtyPagesLong;
        /** The number of saved pages.  This is used to get some kind of estimate of the
         * link speed so we can decide when we're done.  It is reset after the first
         * 7 passes so the speed estimate doesn't get inflated by the initial set of
         * zero pages. */
        uint64_t                    cSavedPages;
        /** The nanosecond timestamp when cSavedPages was 0. */
        uint64_t                    uSaveStartNS;
        /** Pages per second (for statistics). */
        uint32_t                    cPagesPerSecond;
        uint32_t                    cAlignment;
    } LiveSave;

    /** @name   Error injection.
     * @{ */
    /** Inject handy page allocation errors pretending we're completely out of
     * memory. */
    bool volatile                   fErrInjHandyPages;
    /** Padding. */
    bool                            afReserved[3];
    /** @} */

    /** @name Release Statistics
     * @{ */
    uint32_t                        cAllPages;              /**< The total number of pages. (Should be Private + Shared + Zero + Pure MMIO.) */
    uint32_t                        cPrivatePages;          /**< The number of private pages. */
    uint32_t                        cSharedPages;           /**< The number of shared pages. */
    uint32_t                        cReusedSharedPages;     /**< The number of reused shared pages. */
    uint32_t                        cZeroPages;             /**< The number of zero backed pages. */
    uint32_t                        cPureMmioPages;         /**< The number of pure MMIO pages. */
    uint32_t                        cMonitoredPages;        /**< The number of write monitored pages. */
    uint32_t                        cWrittenToPages;        /**< The number of previously write monitored pages. */
    uint32_t                        cWriteLockedPages;      /**< The number of write locked pages. */
    uint32_t                        cReadLockedPages;       /**< The number of read locked pages. */
    uint32_t                        cBalloonedPages;        /**< The number of ballooned pages. */
    uint32_t                        cMappedChunks;          /**< Number of times we mapped a chunk. */
    uint32_t                        cUnmappedChunks;        /**< Number of times we unmapped a chunk. */
    uint32_t                        cLargePages;            /**< The number of large pages. */
    uint32_t                        cLargePagesDisabled;    /**< The number of disabled large pages. */
/*    uint32_t                        aAlignment4[1]; */

    STAMPROFILE                     StatLargePageAlloc;     /**< Time spent by the host OS for large page allocation. */
    STAMCOUNTER                     StatLargePageAllocFailed; /**< Count allocation failures. */
    STAMCOUNTER                     StatLargePageOverflow;  /**< The number of times allocating a large pages takes more than the allowed period. */
    STAMCOUNTER                     StatLargePageReused;    /**< The number of large pages we've reused.*/
    STAMCOUNTER                     StatLargePageRefused;   /**< The number of times we couldn't use a large page.*/
    STAMCOUNTER                     StatLargePageRecheck;   /**< The number of times we rechecked a disabled large page.*/
    STAMCOUNTER                     StatLargePageTlbFlush;  /**< The number of a full VCPU TLB flush was required after allocation. */
    STAMCOUNTER                     StatLargePageZeroEvict; /**< The number of zero page mappings we had to evict when allocating a large page. */

    STAMPROFILE                     StatShModCheck;         /**< Profiles shared module checks. */

    STAMPROFILE                     StatMmio2QueryAndResetDirtyBitmap; /**< Profiling PGMR3PhysMmio2QueryAndResetDirtyBitmap. */
    /** @} */

#ifdef VBOX_WITH_STATISTICS
    /** These are optional statistics that used to be on the hyper heap. */
    PGMSTATS                        Stats;
#endif
} PGM;
#ifndef IN_TSTVMSTRUCTGC /* HACK */
AssertCompileMemberAlignment(PGM, CritSectX, 8);
AssertCompileMemberAlignment(PGM, CritSectX, 16);
AssertCompileMemberAlignment(PGM, CritSectX, 32);
AssertCompileMemberAlignment(PGM, CritSectX, 64);
AssertCompileMemberAlignment(PGM, ChunkR3Map, 16);
AssertCompileMemberAlignment(PGM, PhysTlbR3, 8);
AssertCompileMemberAlignment(PGM, PhysTlbR3, 16);
AssertCompileMemberAlignment(PGM, PhysTlbR3, 32);
AssertCompileMemberAlignment(PGM, PhysTlbR0, 32);
AssertCompileMemberAlignment(PGM, HCPhysZeroPg, 8);
AssertCompileMemberAlignment(PGM, aHandyPages, 8);
#endif /* !IN_TSTVMSTRUCTGC */
/** Pointer to the PGM instance data. */
typedef PGM *PPGM;


#ifdef VBOX_WITH_STATISTICS
/**
 * Per CPU statistis for PGM (used to be on the heap).
 */
typedef struct PGMCPUSTATS
{
    /* Common */
    STAMCOUNTER StatSyncPtPD[X86_PG_ENTRIES];       /**< SyncPT - PD distribution. */
    STAMCOUNTER StatSyncPagePD[X86_PG_ENTRIES];     /**< SyncPage - PD distribution. */

    /* R0 only: */
    STAMPROFILE StatR0NpMiscfg;                     /**< R0: PGMR0Trap0eHandlerNPMisconfig() profiling. */
    STAMCOUNTER StatR0NpMiscfgSyncPage;             /**< R0: SyncPage calls from PGMR0Trap0eHandlerNPMisconfig(). */

    /* RZ only: */
    STAMPROFILE StatRZTrap0e;                       /**< RC/R0: PGMTrap0eHandler() profiling. */
    STAMPROFILE StatRZTrap0eTime2Ballooned;         /**< RC/R0: Profiling of the Trap0eHandler body when the cause is read access to a ballooned page. */
    STAMPROFILE StatRZTrap0eTime2DirtyAndAccessed;  /**< RC/R0: Profiling of the Trap0eHandler body when the cause is dirty and/or accessed bit emulation. */
    STAMPROFILE StatRZTrap0eTime2GuestTrap;         /**< RC/R0: Profiling of the Trap0eHandler body when the cause is a guest trap. */
    STAMPROFILE StatRZTrap0eTime2HndPhys;           /**< RC/R0: Profiling of the Trap0eHandler body when the cause is a physical handler. */
    STAMPROFILE StatRZTrap0eTime2HndUnhandled;      /**< RC/R0: Profiling of the Trap0eHandler body when the cause is access outside the monitored areas of a monitored page. */
    STAMPROFILE StatRZTrap0eTime2InvalidPhys;       /**< RC/R0: Profiling of the Trap0eHandler body when the cause is access to an invalid physical guest address. */
    STAMPROFILE StatRZTrap0eTime2MakeWritable;      /**< RC/R0: Profiling of the Trap0eHandler body when the cause is a page that needed to be made writable. */
    STAMPROFILE StatRZTrap0eTime2Misc;              /**< RC/R0: Profiling of the Trap0eHandler body when the cause is not known. */
    STAMPROFILE StatRZTrap0eTime2OutOfSync;         /**< RC/R0: Profiling of the Trap0eHandler body when the cause is an out-of-sync page. */
    STAMPROFILE StatRZTrap0eTime2OutOfSyncHndPhys;  /**< RC/R0: Profiling of the Trap0eHandler body when the cause is an out-of-sync physical handler page. */
    STAMPROFILE StatRZTrap0eTime2OutOfSyncHndObs;   /**< RC/R0: Profiling of the Trap0eHandler body when the cause is an obsolete handler page. */
    STAMPROFILE StatRZTrap0eTime2PageZeroing;       /**< RC/R0: Profiling of the Trap0eHandler body when the cause is a zero page that is being zeroed. */
    STAMPROFILE StatRZTrap0eTime2SyncPT;            /**< RC/R0: Profiling of the Trap0eHandler body when the cause is lazy syncing of a PT. */
    STAMPROFILE StatRZTrap0eTime2WPEmulation;       /**< RC/R0: Profiling of the Trap0eHandler body when the cause is CR0.WP emulation. */
    STAMPROFILE StatRZTrap0eTime2Wp0RoUsHack;       /**< RC/R0: Profiling of the Trap0eHandler body when the cause is CR0.WP and netware hack to be enabled. */
    STAMPROFILE StatRZTrap0eTime2Wp0RoUsUnhack;     /**< RC/R0: Profiling of the Trap0eHandler body when the cause is CR0.WP and netware hack to be disabled. */
    STAMCOUNTER StatRZTrap0eConflicts;              /**< RC/R0: The number of times \#PF was caused by an undetected conflict. */
    STAMCOUNTER StatRZTrap0eHandlersOutOfSync;      /**< RC/R0: Number of out-of-sync handled pages. */
    STAMCOUNTER StatRZTrap0eHandlersPhysAll;        /**< RC/R0: Number of traps due to physical all-access handlers. */
    STAMCOUNTER StatRZTrap0eHandlersPhysAllOpt;     /**< RC/R0: Number of the physical all-access handler traps using the optimization. */
    STAMCOUNTER StatRZTrap0eHandlersPhysWrite;      /**< RC/R0: Number of traps due to write-physical access handlers. */
    STAMCOUNTER StatRZTrap0eHandlersUnhandled;      /**< RC/R0: Number of traps due to access outside range of monitored page(s). */
    STAMCOUNTER StatRZTrap0eHandlersInvalid;        /**< RC/R0: Number of traps due to access to invalid physical memory. */
    STAMCOUNTER StatRZTrap0eUSNotPresentRead;       /**< RC/R0: \#PF err kind */
    STAMCOUNTER StatRZTrap0eUSNotPresentWrite;      /**< RC/R0: \#PF err kind */
    STAMCOUNTER StatRZTrap0eUSWrite;                /**< RC/R0: \#PF err kind */
    STAMCOUNTER StatRZTrap0eUSReserved;             /**< RC/R0: \#PF err kind */
    STAMCOUNTER StatRZTrap0eUSNXE;                  /**< RC/R0: \#PF err kind */
    STAMCOUNTER StatRZTrap0eUSRead;                 /**< RC/R0: \#PF err kind */
    STAMCOUNTER StatRZTrap0eSVNotPresentRead;       /**< RC/R0: \#PF err kind */
    STAMCOUNTER StatRZTrap0eSVNotPresentWrite;      /**< RC/R0: \#PF err kind */
    STAMCOUNTER StatRZTrap0eSVWrite;                /**< RC/R0: \#PF err kind */
    STAMCOUNTER StatRZTrap0eSVReserved;             /**< RC/R0: \#PF err kind */
    STAMCOUNTER StatRZTrap0eSNXE;                   /**< RC/R0: \#PF err kind */
    STAMCOUNTER StatRZTrap0eGuestPF;                /**< RC/R0: Real guest \#PFs. */
    STAMCOUNTER StatRZTrap0eWPEmulInRZ;             /**< RC/R0: WP=0 virtualization trap, handled. */
    STAMCOUNTER StatRZTrap0eWPEmulToR3;             /**< RC/R0: WP=0 virtualization trap, chickened out. */
    STAMCOUNTER StatRZTrap0ePD[X86_PG_ENTRIES];     /**< RC/R0: PD distribution of the \#PFs. */
    STAMCOUNTER StatRZGuestCR3WriteHandled;         /**< RC/R0: The number of times WriteHandlerCR3() was successfully called. */
    STAMCOUNTER StatRZGuestCR3WriteUnhandled;       /**< RC/R0: The number of times WriteHandlerCR3() was called and we had to fall back to the recompiler. */
    STAMCOUNTER StatRZGuestCR3WriteConflict;        /**< RC/R0: The number of times WriteHandlerCR3() was called and a conflict was detected. */
    STAMCOUNTER StatRZGuestROMWriteHandled;         /**< RC/R0: The number of times pgmPhysRomWriteHandler() was successfully called. */
    STAMCOUNTER StatRZGuestROMWriteUnhandled;       /**< RC/R0: The number of times pgmPhysRomWriteHandler() was called and we had to fall back to the recompiler */
    STAMCOUNTER StatRZDynMapMigrateInvlPg;          /**< RZ: invlpg in PGMR0DynMapMigrateAutoSet. */
    STAMPROFILE StatRZDynMapGCPageInl;              /**< RZ: Calls to pgmRZDynMapGCPageInlined. */
    STAMCOUNTER StatRZDynMapGCPageInlHits;          /**< RZ: Hash table lookup hits. */
    STAMCOUNTER StatRZDynMapGCPageInlMisses;        /**< RZ: Misses that falls back to the code common. */
    STAMCOUNTER StatRZDynMapGCPageInlRamHits;       /**< RZ: 1st ram range hits. */
    STAMCOUNTER StatRZDynMapGCPageInlRamMisses;     /**< RZ: 1st ram range misses, takes slow path. */
    STAMPROFILE StatRZDynMapHCPageInl;              /**< RZ: Calls to pgmRZDynMapHCPageInlined. */
    STAMCOUNTER StatRZDynMapHCPageInlHits;          /**< RZ: Hash table lookup hits. */
    STAMCOUNTER StatRZDynMapHCPageInlMisses;        /**< RZ: Misses that falls back to the code common. */
    STAMPROFILE StatRZDynMapHCPage;                 /**< RZ: Calls to pgmRZDynMapHCPageCommon. */
    STAMCOUNTER StatRZDynMapSetOptimize;            /**< RZ: Calls to pgmRZDynMapOptimizeAutoSet. */
    STAMCOUNTER StatRZDynMapSetSearchFlushes;       /**< RZ: Set search restoring to subset flushes. */
    STAMCOUNTER StatRZDynMapSetSearchHits;          /**< RZ: Set search hits. */
    STAMCOUNTER StatRZDynMapSetSearchMisses;        /**< RZ: Set search misses. */
    STAMCOUNTER StatRZDynMapPage;                   /**< RZ: Calls to pgmR0DynMapPage. */
    STAMCOUNTER StatRZDynMapPageHits0;              /**< RZ: Hits at iPage+0. */
    STAMCOUNTER StatRZDynMapPageHits1;              /**< RZ: Hits at iPage+1. */
    STAMCOUNTER StatRZDynMapPageHits2;              /**< RZ: Hits at iPage+2. */
    STAMCOUNTER StatRZDynMapPageInvlPg;             /**< RZ: invlpg. */
    STAMCOUNTER StatRZDynMapPageSlow;               /**< RZ: Calls to pgmR0DynMapPageSlow. */
    STAMCOUNTER StatRZDynMapPageSlowLoopHits;       /**< RZ: Hits in the pgmR0DynMapPageSlow search loop. */
    STAMCOUNTER StatRZDynMapPageSlowLoopMisses;     /**< RZ: Misses in the pgmR0DynMapPageSlow search loop. */
    //STAMCOUNTER StatRZDynMapPageSlowLostHits;       /**< RZ: Lost hits. */
    STAMCOUNTER StatRZDynMapSubsets;                /**< RZ: Times PGMDynMapPushAutoSubset was called. */
    STAMCOUNTER StatRZDynMapPopFlushes;             /**< RZ: Times PGMDynMapPopAutoSubset flushes the subset. */
    STAMCOUNTER aStatRZDynMapSetFilledPct[11];      /**< RZ: Set fill distribution, percent. */

    /* HC - R3 and (maybe) R0: */

    /* RZ & R3: */
    STAMPROFILE StatRZSyncCR3;                      /**< RC/R0: PGMSyncCR3() profiling. */
    STAMPROFILE StatRZSyncCR3Handlers;              /**< RC/R0: Profiling of the PGMSyncCR3() update handler section. */
    STAMCOUNTER StatRZSyncCR3Global;                /**< RC/R0: The number of global CR3 syncs. */
    STAMCOUNTER StatRZSyncCR3NotGlobal;             /**< RC/R0: The number of non-global CR3 syncs. */
    STAMCOUNTER StatRZSyncCR3DstCacheHit;           /**< RC/R0: The number of times we got some kind of cache hit on a page table. */
    STAMCOUNTER StatRZSyncCR3DstFreed;              /**< RC/R0: The number of times we've had to free a shadow entry. */
    STAMCOUNTER StatRZSyncCR3DstFreedSrcNP;         /**< RC/R0: The number of times we've had to free a shadow entry for which the source entry was not present. */
    STAMCOUNTER StatRZSyncCR3DstNotPresent;         /**< RC/R0: The number of times we've encountered a not present shadow entry for a present guest entry. */
    STAMCOUNTER StatRZSyncCR3DstSkippedGlobalPD;    /**< RC/R0: The number of times a global page directory wasn't flushed. */
    STAMCOUNTER StatRZSyncCR3DstSkippedGlobalPT;    /**< RC/R0: The number of times a page table with only global entries wasn't flushed. */
    STAMPROFILE StatRZSyncPT;                       /**< RC/R0: PGMSyncPT() profiling. */
    STAMCOUNTER StatRZSyncPTFailed;                 /**< RC/R0: The number of times PGMSyncPT() failed. */
    STAMCOUNTER StatRZSyncPT4K;                     /**< RC/R0: Number of 4KB syncs. */
    STAMCOUNTER StatRZSyncPT4M;                     /**< RC/R0: Number of 4MB syncs. */
    STAMCOUNTER StatRZSyncPagePDNAs;                /**< RC/R0: The number of time we've marked a PD not present from SyncPage to virtualize the accessed bit. */
    STAMCOUNTER StatRZSyncPagePDOutOfSync;          /**< RC/R0: The number of time we've encountered an out-of-sync PD in SyncPage. */
    STAMCOUNTER StatRZAccessedPage;                 /**< RC/R0: The number of pages marked not present for accessed bit emulation. */
    STAMPROFILE StatRZDirtyBitTracking;             /**< RC/R0: Profiling the dirty bit tracking in CheckPageFault(). */
    STAMCOUNTER StatRZDirtyPage;                    /**< RC/R0: The number of pages marked read-only for dirty bit tracking. */
    STAMCOUNTER StatRZDirtyPageBig;                 /**< RC/R0: The number of pages marked read-only for dirty bit tracking. */
    STAMCOUNTER StatRZDirtyPageSkipped;             /**< RC/R0: The number of pages already dirty or readonly. */
    STAMCOUNTER StatRZDirtyPageTrap;                /**< RC/R0: The number of traps generated for dirty bit tracking. */
    STAMCOUNTER StatRZDirtyPageStale;               /**< RC/R0: The number of traps generated for dirty bit tracking. (stale tlb entries) */
    STAMCOUNTER StatRZDirtyTrackRealPF;             /**< RC/R0: The number of real pages faults during dirty bit tracking. */
    STAMCOUNTER StatRZDirtiedPage;                  /**< RC/R0: The number of pages marked dirty because of write accesses. */
    STAMCOUNTER StatRZPageAlreadyDirty;             /**< RC/R0: The number of pages already marked dirty because of write accesses. */
    STAMPROFILE StatRZInvalidatePage;               /**< RC/R0: PGMInvalidatePage() profiling. */
    STAMCOUNTER StatRZInvalidatePage4KBPages;       /**< RC/R0: The number of times PGMInvalidatePage() was called for a 4KB page. */
    STAMCOUNTER StatRZInvalidatePage4MBPages;       /**< RC/R0: The number of times PGMInvalidatePage() was called for a 4MB page. */
    STAMCOUNTER StatRZInvalidatePage4MBPagesSkip;   /**< RC/R0: The number of times PGMInvalidatePage() skipped a 4MB page. */
    STAMCOUNTER StatRZInvalidatePagePDNAs;          /**< RC/R0: The number of times PGMInvalidatePage() was called for a not accessed page directory. */
    STAMCOUNTER StatRZInvalidatePagePDNPs;          /**< RC/R0: The number of times PGMInvalidatePage() was called for a not present page directory. */
    STAMCOUNTER StatRZInvalidatePagePDOutOfSync;    /**< RC/R0: The number of times PGMInvalidatePage() was called for an out of sync page directory. */
    STAMCOUNTER StatRZInvalidatePageSizeChanges ;   /**< RC/R0: The number of times PGMInvalidatePage() was called on a page size change (4KB <-> 2/4MB). */
    STAMCOUNTER StatRZInvalidatePageSkipped;        /**< RC/R0: The number of times PGMInvalidatePage() was skipped due to not present shw or pending pending SyncCR3. */
    STAMCOUNTER StatRZPageOutOfSyncUser;            /**< RC/R0: The number of times user page is out of sync was detected in \#PF or VerifyAccessSyncPage. */
    STAMCOUNTER StatRZPageOutOfSyncSupervisor;      /**< RC/R0: The number of times supervisor page is out of sync was detected in in \#PF or VerifyAccessSyncPage. */
    STAMCOUNTER StatRZPageOutOfSyncUserWrite;       /**< RC/R0: The number of times user page is out of sync was detected in \#PF. */
    STAMCOUNTER StatRZPageOutOfSyncSupervisorWrite; /**< RC/R0: The number of times supervisor page is out of sync was detected in in \#PF. */
    STAMCOUNTER StatRZPageOutOfSyncBallloon;        /**< RC/R0: The number of times a ballooned page was accessed (read). */
    STAMPROFILE StatRZPrefetch;                     /**< RC/R0: PGMPrefetchPage. */
    STAMPROFILE StatRZFlushTLB;                     /**< RC/R0: Profiling of the PGMFlushTLB() body. */
    STAMCOUNTER StatRZFlushTLBNewCR3;               /**< RC/R0: The number of times PGMFlushTLB was called with a new CR3, non-global. (switch) */
    STAMCOUNTER StatRZFlushTLBNewCR3Global;         /**< RC/R0: The number of times PGMFlushTLB was called with a new CR3, global. (switch) */
    STAMCOUNTER StatRZFlushTLBSameCR3;              /**< RC/R0: The number of times PGMFlushTLB was called with the same CR3, non-global. (flush) */
    STAMCOUNTER StatRZFlushTLBSameCR3Global;        /**< RC/R0: The number of times PGMFlushTLB was called with the same CR3, global. (flush) */
    STAMPROFILE StatRZGstModifyPage;                /**< RC/R0: Profiling of the PGMGstModifyPage() body */
    STAMCOUNTER StatRZRamRangeTlbHits;              /**< RC/R0: RAM range TLB hits (lockless). */
    STAMCOUNTER StatRZPageMapTlbHits;               /**< RC/R0: Page mapper TLB hits (lockless). */

    STAMPROFILE StatR3SyncCR3;                      /**< R3: PGMSyncCR3() profiling. */
    STAMPROFILE StatR3SyncCR3Handlers;              /**< R3: Profiling of the PGMSyncCR3() update handler section. */
    STAMCOUNTER StatR3SyncCR3Global;                /**< R3: The number of global CR3 syncs. */
    STAMCOUNTER StatR3SyncCR3NotGlobal;             /**< R3: The number of non-global CR3 syncs. */
    STAMCOUNTER StatR3SyncCR3DstFreed;              /**< R3: The number of times we've had to free a shadow entry. */
    STAMCOUNTER StatR3SyncCR3DstFreedSrcNP;         /**< R3: The number of times we've had to free a shadow entry for which the source entry was not present. */
    STAMCOUNTER StatR3SyncCR3DstNotPresent;         /**< R3: The number of times we've encountered a not present shadow entry for a present guest entry. */
    STAMCOUNTER StatR3SyncCR3DstSkippedGlobalPD;    /**< R3: The number of times a global page directory wasn't flushed. */
    STAMCOUNTER StatR3SyncCR3DstSkippedGlobalPT;    /**< R3: The number of times a page table with only global entries wasn't flushed. */
    STAMCOUNTER StatR3SyncCR3DstCacheHit;           /**< R3: The number of times we got some kind of cache hit on a page table. */
    STAMPROFILE StatR3SyncPT;                       /**< R3: PGMSyncPT() profiling. */
    STAMCOUNTER StatR3SyncPTFailed;                 /**< R3: The number of times PGMSyncPT() failed. */
    STAMCOUNTER StatR3SyncPT4K;                     /**< R3: Number of 4KB syncs. */
    STAMCOUNTER StatR3SyncPT4M;                     /**< R3: Number of 4MB syncs. */
    STAMCOUNTER StatR3SyncPagePDNAs;                /**< R3: The number of time we've marked a PD not present from SyncPage to virtualize the accessed bit. */
    STAMCOUNTER StatR3SyncPagePDOutOfSync;          /**< R3: The number of time we've encountered an out-of-sync PD in SyncPage. */
    STAMCOUNTER StatR3AccessedPage;                 /**< R3: The number of pages marked not present for accessed bit emulation. */
    STAMPROFILE StatR3DirtyBitTracking;             /**< R3: Profiling the dirty bit tracking in CheckPageFault(). */
    STAMCOUNTER StatR3DirtyPage;                    /**< R3: The number of pages marked read-only for dirty bit tracking. */
    STAMCOUNTER StatR3DirtyPageBig;                 /**< R3: The number of pages marked read-only for dirty bit tracking. */
    STAMCOUNTER StatR3DirtyPageSkipped;             /**< R3: The number of pages already dirty or readonly. */
    STAMCOUNTER StatR3DirtyPageTrap;                /**< R3: The number of traps generated for dirty bit tracking. */
    STAMCOUNTER StatR3DirtyTrackRealPF;             /**< R3: The number of real pages faults during dirty bit tracking. */
    STAMCOUNTER StatR3DirtiedPage;                  /**< R3: The number of pages marked dirty because of write accesses. */
    STAMCOUNTER StatR3PageAlreadyDirty;             /**< R3: The number of pages already marked dirty because of write accesses. */
    STAMPROFILE StatR3InvalidatePage;               /**< R3: PGMInvalidatePage() profiling. */
    STAMCOUNTER StatR3InvalidatePage4KBPages;       /**< R3: The number of times PGMInvalidatePage() was called for a 4KB page. */
    STAMCOUNTER StatR3InvalidatePage4MBPages;       /**< R3: The number of times PGMInvalidatePage() was called for a 4MB page. */
    STAMCOUNTER StatR3InvalidatePage4MBPagesSkip;   /**< R3: The number of times PGMInvalidatePage() skipped a 4MB page. */
    STAMCOUNTER StatR3InvalidatePagePDNAs;          /**< R3: The number of times PGMInvalidatePage() was called for a not accessed page directory. */
    STAMCOUNTER StatR3InvalidatePagePDNPs;          /**< R3: The number of times PGMInvalidatePage() was called for a not present page directory. */
    STAMCOUNTER StatR3InvalidatePagePDOutOfSync;    /**< R3: The number of times PGMInvalidatePage() was called for an out of sync page directory. */
    STAMCOUNTER StatR3InvalidatePageSizeChanges ;   /**< R3: The number of times PGMInvalidatePage() was called on a page size change (4KB <-> 2/4MB). */
    STAMCOUNTER StatR3InvalidatePageSkipped;        /**< R3: The number of times PGMInvalidatePage() was skipped due to not present shw or pending pending SyncCR3. */
    STAMCOUNTER StatR3PageOutOfSyncUser;            /**< R3: The number of times user page is out of sync was detected in \#PF or VerifyAccessSyncPage. */
    STAMCOUNTER StatR3PageOutOfSyncSupervisor;      /**< R3: The number of times supervisor page is out of sync was detected in in \#PF or VerifyAccessSyncPage. */
    STAMCOUNTER StatR3PageOutOfSyncUserWrite;       /**< R3: The number of times user page is out of sync was detected in \#PF. */
    STAMCOUNTER StatR3PageOutOfSyncSupervisorWrite; /**< R3: The number of times supervisor page is out of sync was detected in in \#PF. */
    STAMCOUNTER StatR3PageOutOfSyncBallloon;        /**< R3: The number of times a ballooned page was accessed (read). */
    STAMPROFILE StatR3Prefetch;                     /**< R3: PGMPrefetchPage. */
    STAMPROFILE StatR3FlushTLB;                     /**< R3: Profiling of the PGMFlushTLB() body. */
    STAMCOUNTER StatR3FlushTLBNewCR3;               /**< R3: The number of times PGMFlushTLB was called with a new CR3, non-global. (switch) */
    STAMCOUNTER StatR3FlushTLBNewCR3Global;         /**< R3: The number of times PGMFlushTLB was called with a new CR3, global. (switch) */
    STAMCOUNTER StatR3FlushTLBSameCR3;              /**< R3: The number of times PGMFlushTLB was called with the same CR3, non-global. (flush) */
    STAMCOUNTER StatR3FlushTLBSameCR3Global;        /**< R3: The number of times PGMFlushTLB was called with the same CR3, global. (flush) */
    STAMPROFILE StatR3GstModifyPage;                /**< R3: Profiling of the PGMGstModifyPage() body */
    STAMCOUNTER StatR3RamRangeTlbHits;              /**< R3: RAM range TLB hits (lockless). */
    STAMCOUNTER StatR3PageMapTlbHits;               /**< R3: Page mapper TLB hits (lockless). */
} PGMCPUSTATS;
#endif /* VBOX_WITH_STATISTICS */


/**
 * PGMCPU Data (part of VMCPU).
 */
typedef struct PGMCPU
{
    /** A20 gate mask.
     * Our current approach to A20 emulation is to let REM do it and don't bother
     * anywhere else. The interesting Guests will be operating with it enabled anyway.
     * But whould need arrise, we'll subject physical addresses to this mask. */
    RTGCPHYS                        GCPhysA20Mask;
    /** A20 gate state - boolean! */
    bool                            fA20Enabled;
    /** Mirror of the EFER.NXE bit.  Managed by PGMNotifyNxeChanged. */
    bool                            fNoExecuteEnabled;
    /** Whether the guest CR3 and PAE PDPEs have been mapped when guest PAE mode is
     *  active. */
    bool                            fPaePdpesAndCr3MappedR3;
    bool                            fPaePdpesAndCr3MappedR0;

    /** What needs syncing (PGM_SYNC_*).
     * This is used to queue operations for PGMSyncCR3, PGMInvalidatePage,
     * PGMFlushTLB, and PGMR3Load. */
    uint32_t                        fSyncFlags;

    /** The shadow paging mode. */
    PGMMODE                         enmShadowMode;
    /** The guest paging mode. */
    PGMMODE                         enmGuestMode;
    /** The guest second level address translation mode. */
    PGMSLAT                         enmGuestSlatMode;
    /** Guest mode data table index (PGM_TYPE_XXX). */
    uint8_t volatile                idxGuestModeData;
    /** Shadow mode data table index (PGM_TYPE_XXX). */
    uint8_t volatile                idxShadowModeData;
    /** Both mode data table index (complicated). */
    uint8_t volatile                idxBothModeData;
    /** Alignment padding. */
    uint8_t                         abPadding[1];

    /** The guest CR3.
     *  When SLAT is active, this is the translated physical address.
     *  When SLAT is inactive, this is the physical address in CR3. */
    RTGCPHYS                        GCPhysCR3;

    /** The nested-guest CR3.
     *  When SLAT is active, this is CR3 prior to translation.
     *  When SLAT is inactive, this is unused (and NIL_RTGCPHYS). */
    RTGCPHYS                        GCPhysNstGstCR3;

    /** The cached guest CR3 when it has been mapped in PAE mode.
     * This allows us to skip remapping the CR3 and PAE PDPEs
     * (in PGMFlushTLB or similar) when it was already done as
     * part of MOV CRx instruction emulation.
     */
    RTGCPHYS                        GCPhysPaeCR3;

    /** @name 32-bit Guest Paging.
     * @{ */
    /** The guest's page directory, R3 pointer. */
    R3PTRTYPE(PX86PD)               pGst32BitPdR3;
    /** The guest's page directory, R0 pointer. */
    R0PTRTYPE(PX86PD)               pGst32BitPdR0;
    /** Mask containing the MBZ bits of a big page PDE. */
    uint32_t                        fGst32BitMbzBigPdeMask;
    /** Set if the page size extension (PSE) is enabled. */
    bool                            fGst32BitPageSizeExtension;
    /** Alignment padding. */
    bool                            afAlignment2[3];
    /** @} */

    /** @name PAE Guest Paging.
     * @{ */
    /** The guest's page directory pointer table, R3 pointer. */
    R3PTRTYPE(PX86PDPT)             pGstPaePdptR3;
    /** The guest's page directory pointer table, R0 pointer. */
    R0PTRTYPE(PX86PDPT)             pGstPaePdptR0;

    /** The guest's page directories, R3 pointers.
     * These are individual pointers and don't have to be adjacent.
     * These don't have to be up-to-date - use pgmGstGetPaePD() to access them. */
    R3PTRTYPE(PX86PDPAE)            apGstPaePDsR3[4];
    /** The guest's page directories, R0 pointers.
     * Same restrictions as apGstPaePDsR3. */
    R0PTRTYPE(PX86PDPAE)            apGstPaePDsR0[4];
    /** The physical addresses of the guest page directories (PAE) pointed to by apGstPagePDsHC/GC. */
    RTGCPHYS                        aGCPhysGstPaePDs[4];
    /** Mask containing the MBZ PTE bits. */
    uint64_t                        fGstPaeMbzPteMask;
    /** Mask containing the MBZ PDE bits. */
    uint64_t                        fGstPaeMbzPdeMask;
    /** Mask containing the MBZ big page PDE bits. */
    uint64_t                        fGstPaeMbzBigPdeMask;
    /** Mask containing the MBZ PDPE bits. */
    uint64_t                        fGstPaeMbzPdpeMask;
    /** @} */

    /** @name AMD64 Guest Paging.
     * @{ */
    /** The guest's page directory pointer table, R3 pointer. */
    R3PTRTYPE(PX86PML4)             pGstAmd64Pml4R3;
    /** The guest's page directory pointer table, R0 pointer. */
    R0PTRTYPE(PX86PML4)             pGstAmd64Pml4R0;
    /** Mask containing the MBZ PTE bits. */
    uint64_t                        fGstAmd64MbzPteMask;
    /** Mask containing the MBZ PDE bits. */
    uint64_t                        fGstAmd64MbzPdeMask;
    /** Mask containing the MBZ big page PDE bits. */
    uint64_t                        fGstAmd64MbzBigPdeMask;
    /** Mask containing the MBZ PDPE bits. */
    uint64_t                        fGstAmd64MbzPdpeMask;
    /** Mask containing the MBZ big page PDPE bits. */
    uint64_t                        fGstAmd64MbzBigPdpeMask;
    /** Mask containing the MBZ PML4E bits. */
    uint64_t                        fGstAmd64MbzPml4eMask;
    /** Mask containing the PDPE bits that we shadow. */
    uint64_t                        fGstAmd64ShadowedPdpeMask;
    /** Mask containing the PML4E bits that we shadow. */
    uint64_t                        fGstAmd64ShadowedPml4eMask;
    /** @} */

    /** @name PAE and AMD64 Guest Paging.
     * @{ */
    /** Mask containing the PTE bits that we shadow. */
    uint64_t                        fGst64ShadowedPteMask;
    /** Mask containing the PDE bits that we shadow. */
    uint64_t                        fGst64ShadowedPdeMask;
    /** Mask containing the big page PDE bits that we shadow in the PDE. */
    uint64_t                        fGst64ShadowedBigPdeMask;
    /** Mask containing the big page PDE bits that we shadow in the PTE. */
    uint64_t                        fGst64ShadowedBigPde4PteMask;
    /** @} */

    /** @name EPT Guest Paging.
     * @{ */
    /** The guest's EPT PML4 table, R3 pointer. */
    R3PTRTYPE(PEPTPML4)             pGstEptPml4R3;
    /** The guest's EPT PML4 table, R0 pointer. */
    R0PTRTYPE(PEPTPML4)             pGstEptPml4R0;
    /** The guest's EPT pointer (copy of virtual VMCS). */
    uint64_t                        uEptPtr;
    /** Copy of the VM's IA32_VMX_EPT_VPID_CAP VPID MSR for faster access. Doesn't
     *  change through the lifetime of the VM. */
    uint64_t                        uEptVpidCapMsr;
    /** Mask containing the MBZ PTE bits. */
    uint64_t                        fGstEptMbzPteMask;
    /** Mask containing the MBZ PDE bits. */
    uint64_t                        fGstEptMbzPdeMask;
    /** Mask containing the MBZ big page (2M) PDE bits. */
    uint64_t                        fGstEptMbzBigPdeMask;
    /** Mask containing the MBZ PDPTE bits. */
    uint64_t                        fGstEptMbzPdpteMask;
    /** Mask containing the MBZ big page (1G) PDPTE bits. */
    uint64_t                        fGstEptMbzBigPdpteMask;
    /** Mask containing the MBZ PML4E bits. */
    uint64_t                        fGstEptMbzPml4eMask;
    /** Mask to determine whether an entry is present. */
    uint64_t                        fGstEptPresentMask;

    /** Mask containing the EPT PTE bits we shadow. */
    uint64_t                        fGstEptShadowedPteMask;
    /** Mask containing the EPT PDE bits we shadow. */
    uint64_t                        fGstEptShadowedPdeMask;
    /** Mask containing the EPT PDE (2M) bits we shadow. */
    uint64_t                        fGstEptShadowedBigPdeMask;
    /** Mask containing the EPT PDPTE bits we shadow. */
    uint64_t                        fGstEptShadowedPdpteMask;
    /** Mask containing the EPT PML4E bits we shadow. */
    uint64_t                        fGstEptShadowedPml4eMask;
    /** @} */

    /** Pointer to the page of the current active CR3 - R3 Ptr. */
    R3PTRTYPE(PPGMPOOLPAGE)         pShwPageCR3R3;
    /** Pointer to the page of the current active CR3 - R0 Ptr. */
    R0PTRTYPE(PPGMPOOLPAGE)         pShwPageCR3R0;

    /** For saving stack space, the disassembler state is allocated here instead of
     * on the stack. */
    DISSTATE                        Dis;

    /** Counts the number of times the netware WP0+RO+US hack has been applied. */
    uint64_t                        cNetwareWp0Hacks;

    /** Count the number of pgm pool access handler calls. */
    uint64_t                        cPoolAccessHandler;

    /** Lockless RAM range TLB for R3. */
    R3PTRTYPE(PPGMRAMRANGE)         apRamRangesTlb[PGM_RAMRANGE_TLB_ENTRIES];

    /** Lockless page mapping TLB for R3. */
    PGMPAGER3MAPTLB                 PhysTlb;

    /** @name Release Statistics
     * @{ */
    /** The number of times the guest has switched mode since last reset or statistics reset. */
    STAMCOUNTER                     cGuestModeChanges;
    /** The number of times the guest has switched mode since last reset or statistics reset. */
    STAMCOUNTER                     cA20Changes;

    STAMCOUNTER                     StatRZRamRangeTlbMisses;        /**< RC/R0: RAM range TLB misses (lockless). */
    STAMCOUNTER                     StatRZRamRangeTlbLocking;       /**< RC/R0: RAM range TLB lookup redone with locking. */
    STAMCOUNTER                     StatRZPageMapTlbMisses;         /**< RC/R0: Page mapper TLB misses (lockless -> locked). */

    STAMCOUNTER                     StatR3RamRangeTlbMisses;        /**< R3: RAM range TLB misses (lockless). */
    STAMCOUNTER                     StatR3RamRangeTlbLocking;       /**< R3: RAM range TLB lookup redone with locking. */
    STAMCOUNTER                     StatR3PageMapTlbMisses;         /**< R3: Page mapper TLB misses (lockless -> locked). */
    /** @} */

#ifdef VBOX_WITH_STATISTICS
    /** These are statistics that used to be on the hyper heap. */
    PGMCPUSTATS                     Stats;
#endif
} PGMCPU;
/** Pointer to the per-cpu PGM data. */
typedef PGMCPU *PPGMCPU;


/** @name PGM::fSyncFlags Flags
 * @note Was part of saved state a long time ago.
 * @{
 */
/* 0 used to be PGM_SYNC_UPDATE_PAGE_BIT_VIRTUAL */
/** Always sync CR3. */
#define PGM_SYNC_ALWAYS                         RT_BIT(1)
/** Check guest mapping in SyncCR3. */
#define PGM_SYNC_MAP_CR3                        RT_BIT(3)
/** Clear the page pool (a light weight flush). */
#define PGM_SYNC_CLEAR_PGM_POOL_BIT             8
#define PGM_SYNC_CLEAR_PGM_POOL                 RT_BIT(PGM_SYNC_CLEAR_PGM_POOL_BIT)
/** @} */


#if defined(IN_RING0) || defined(DOXYGEN_RUNNING)

/**
 * PGM GVMCPU instance data.
 */
typedef struct PGMR0PERVCPU
{
    /** Lockless RAM range TLB for R0. */
    R0PTRTYPE(PPGMRAMRANGE)         apRamRangesTlb[PGM_RAMRANGE_TLB_ENTRIES];

# ifdef VBOX_WITH_STATISTICS
    /** R0: Which statistic this \#PF should be attributed to. */
    R0PTRTYPE(PSTAMPROFILE)         pStatTrap0eAttributionR0;
# endif
} PGMR0PERVCPU;


/**
 * PGM GVM instance data.
 */
typedef struct PGMR0PERVM
{
    /** @name RAM ranges
     * @{  */
    /** The ring-0 RAM range pointer table. */
    R0PTRTYPE(PPGMRAMRANGE)         apRamRanges[PGM_MAX_RAM_RANGES];
    /** Trusted RAM range page counts running parallel to apRamRanges.
     * This keeps the original page count when a range is reduced,
     * only the PGMRAMRANGE::cb member is changed then. */
    uint32_t                        acRamRangePages[PGM_MAX_RAM_RANGES];
    /** The memory objects for the RAM ranges (parallel to apRamRanges). */
    RTR0MEMOBJ                      ahRamRangeMemObjs[PGM_MAX_RAM_RANGES];
    /** The ring-3 mapping objects for the RAM ranges (parallel to apRamRanges). */
    RTR0MEMOBJ                      ahRamRangeMapObjs[PGM_MAX_RAM_RANGES];
    /** The max RAM range ID (safe).   */
    uint32_t                        idRamRangeMax;
    uint8_t                         abAlignment1[64 - sizeof(uint32_t)];
    /** RAM range TLB for R0. */
    R0PTRTYPE(PPGMRAMRANGE)         apRamRangesTlb[PGM_RAMRANGE_TLB_ENTRIES];
    /** @} */

    /** @name MMIO2 ranges
     *  @{ */
    /** The ring-0 RAM range pointer table running parallel to aMmio2Ranges. */
    R0PTRTYPE(PPGMRAMRANGE)         apMmio2RamRanges[PGM_MAX_MMIO2_RANGES];
    /** The memory objects for the MMIO2 backing memory (parallel to
     *  apMmio2RamRanges). */
    RTR0MEMOBJ                      ahMmio2MemObjs[PGM_MAX_MMIO2_RANGES];
    /** The ring-3 mapping objects for the MMIO2 backing memory (parallel
     *  to apMmio2RamRanges & ahMmio2MemObjs). */
    RTR0MEMOBJ                      ahMmio2MapObjs[PGM_MAX_MMIO2_RANGES];
    /** Trusted MMIO2 range sizes (count of guest pages).
     * This keeps the original page count when a range is reduced,
     * only the PGMRAMRANGE::cb member is changed then. */
    uint32_t                        acMmio2RangePages[PGM_MAX_MMIO2_RANGES];
#ifndef VBOX_WITH_LINEAR_HOST_PHYS_MEM
    /** Pointer to the ring-0 mapping of the MMIO2 backings (parallel to
     *  apMmio2RamRanges). */
    R0PTRTYPE(uint8_t *)            apbMmio2Backing[PGM_MAX_MMIO2_RANGES];
#endif
    /** @} */

    /** @name ROM ranges
     *  @{ */
    /** The ring-0 ROM range pointer table. */
    R0PTRTYPE(PPGMROMRANGE)         apRomRanges[PGM_MAX_ROM_RANGES];
    /** The memory objects for each ROM range (parallel to apRomRanges). */
    RTR0MEMOBJ                      ahRomRangeMemObjs[PGM_MAX_ROM_RANGES];
    /** The ring-3 mapping objects for each ROM range (parallel to apRomRanges
     *  & ahRamRangeMemObjs). */
    RTR0MEMOBJ                      ahRomRangeMapObjs[PGM_MAX_ROM_RANGES];
    /** Trusted ROM range sizes (count of guest pages). */
    uint32_t                        acRomRangePages[PGM_MAX_ROM_RANGES];
    /** @} */

    /** @name PGM Pool related stuff.
     * @{ */
    /** Critical section for serializing pool growth. */
    RTCRITSECT                      PoolGrowCritSect;
    /** The memory objects for the pool pages. */
    RTR0MEMOBJ                      ahPoolMemObjs[(PGMPOOL_IDX_LAST + PGMPOOL_CFG_MAX_GROW - 1) / PGMPOOL_CFG_MAX_GROW];
    /** The ring-3 mapping objects for the pool pages. */
    RTR0MEMOBJ                      ahPoolMapObjs[(PGMPOOL_IDX_LAST + PGMPOOL_CFG_MAX_GROW - 1) / PGMPOOL_CFG_MAX_GROW];
    /** @} */

    /** Physical access handler types for ring-0.
     * Initialized to callback causing return to ring-3 and invalid enmKind. */
    PGMPHYSHANDLERTYPEINTR0         aPhysHandlerTypes[PGMPHYSHANDLERTYPE_COUNT];
    /** Physical handler allocator, ring-3 edition. */
    PGMPHYSHANDLERALLOCATOR         PhysHandlerAllocator;
    /** The pointer to the ring-3 mapping of the physical access handler tree. */
    PPGMPHYSHANDLERTREE             pPhysHandlerTree;
    /** The allocation object for the physical access handler tree. */
    RTR0MEMOBJ                      hPhysHandlerMemObj;
    /** The ring-3 mapping object for the physicall access handler tree. */
    RTR0MEMOBJ                      hPhysHandlerMapObj;
} PGMR0PERVM;

#endif /* IN_RING0 || DOXYGEN_RUNNING */

RT_C_DECLS_BEGIN

#if defined(VBOX_STRICT)
int             pgmLockDebug(PVMCC pVM, bool fVoid, RT_SRC_POS_DECL);
# define PGM_LOCK_VOID(a_pVM)       pgmLockDebug((a_pVM), true,  RT_SRC_POS)
# define PGM_LOCK(a_pVM)            pgmLockDebug((a_pVM), false, RT_SRC_POS)
#else
int             pgmLock(PVMCC pVM, bool fVoid);
# define PGM_LOCK_VOID(a_pVM)       pgmLock((a_pVM), true)
# define PGM_LOCK(a_pVM)            pgmLock((a_pVM), false)
#endif
void            pgmUnlock(PVMCC pVM);
# define PGM_UNLOCK(a_pVM)          pgmUnlock((a_pVM))
/**
 * Asserts that the caller owns the PDM lock.
 * This is the internal variant of PGMIsLockOwner.
 * @param   a_pVM           Pointer to the VM.
 */
#define PGM_LOCK_ASSERT_OWNER(a_pVM)    Assert(PDMCritSectIsOwner((a_pVM), &(a_pVM)->pgm.s.CritSectX))
/**
 * Asserts that the caller owns the PDM lock.
 * This is the internal variant of PGMIsLockOwner.
 * @param   a_pVM           Pointer to the VM.
 * @param   a_pVCpu         The current CPU handle.
 */
#define PGM_LOCK_ASSERT_OWNER_EX(a_pVM, a_pVCpu)  Assert(PDMCritSectIsOwnerEx((a_pVCpu), &(a_pVM)->pgm.s.CritSectX))

uint32_t        pgmHandlerPhysicalCalcTableSizes(uint32_t *pcEntries, uint32_t *pcbTreeAndBitmap);
int             pgmHandlerPhysicalExCreate(PVMCC pVM, PGMPHYSHANDLERTYPE hType, uint64_t uUser,
                                           R3PTRTYPE(const char *) pszDesc, PPGMPHYSHANDLER *ppPhysHandler);
int             pgmHandlerPhysicalExDup(PVMCC pVM, PPGMPHYSHANDLER pPhysHandlerSrc, PPGMPHYSHANDLER *ppPhysHandler);
int             pgmHandlerPhysicalExRegister(PVMCC pVM, PPGMPHYSHANDLER pPhysHandler, RTGCPHYS GCPhys, RTGCPHYS GCPhysLast);
int             pgmHandlerPhysicalExDeregister(PVMCC pVM, PPGMPHYSHANDLER pPhysHandler);
int             pgmHandlerPhysicalExDestroy(PVMCC pVM, PPGMPHYSHANDLER pHandler);
void            pgmR3HandlerPhysicalUpdateAll(PVM pVM);
bool            pgmHandlerPhysicalIsAll(PVMCC pVM, RTGCPHYS GCPhys);
void            pgmHandlerPhysicalResetAliasedPage(PVMCC pVM, PPGMPAGE pPage, RTGCPHYS GCPhysPage, PPGMRAMRANGE pRam,
                                                   bool fDoAccounting, bool fFlushIemTlbs);
DECLHIDDEN(int) pgmHandlerPhysicalResetMmio2WithBitmap(PVMCC pVM, RTGCPHYS GCPhys, void *pvBitmap, uint32_t offBitmap);
DECLCALLBACK(void) pgmR3InfoHandlers(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
DECLCALLBACK(FNPGMPHYSHANDLER)      pgmR3HandlerPhysicalHandlerInvalid;
#ifndef IN_RING3
DECLCALLBACK(FNPGMPHYSHANDLER)      pgmR0HandlerPhysicalHandlerToRing3;
DECLCALLBACK(FNPGMRZPHYSPFHANDLER)  pgmR0HandlerPhysicalPfHandlerToRing3;
#endif

int             pgmR3InitSavedState(PVM pVM, uint64_t cbRam);

int             pgmPhysAllocPage(PVMCC pVM, PPGMPAGE pPage, RTGCPHYS GCPhys);
int             pgmPhysAllocLargePage(PVMCC pVM, RTGCPHYS GCPhys);
#ifdef IN_RING0
int             pgmR0PhysAllocateHandyPages(PGVM pGVM, VMCPUID idCpu, bool fRing3);
int             pgmR0PhysAllocateLargePage(PGVM pGVM, VMCPUID idCpu, RTGCPHYS GCPhys);
#endif
int             pgmPhysRecheckLargePage(PVMCC pVM, RTGCPHYS GCPhys, PPGMPAGE pLargePage);
int             pgmPhysPageLoadIntoTlb(PVMCC pVM, RTGCPHYS GCPhys);
int             pgmPhysPageLoadIntoTlbWithPage(PVMCC pVM, PPGMPAGE pPage, RTGCPHYS GCPhys);
#ifdef IN_RING3
DECLHIDDEN(int) pgmPhysPageLoadIntoLocklessTlbWithPage(PVMCPUCC pVCpu, PPGMPAGE pPage, RTGCPHYS GCPhys);
#endif
void            pgmPhysPageMakeWriteMonitoredWritable(PVMCC pVM, PPGMPAGE pPage, RTGCPHYS GCPhys);
int             pgmPhysPageMakeWritable(PVMCC pVM, PPGMPAGE pPage, RTGCPHYS GCPhys);
int             pgmPhysPageMakeWritableAndMap(PVMCC pVM, PPGMPAGE pPage, RTGCPHYS GCPhys, void **ppv);
int             pgmPhysPageMap(PVMCC pVM, PPGMPAGE pPage, RTGCPHYS GCPhys, void **ppv);
int             pgmPhysPageMapReadOnly(PVMCC pVM, PPGMPAGE pPage, RTGCPHYS GCPhys, void const **ppv);
int             pgmPhysPageMapByPageID(PVMCC pVM, uint32_t idPage, RTHCPHYS HCPhys, void **ppv);
int             pgmPhysGCPhys2R3Ptr(PVMCC pVM, RTGCPHYS GCPhys, PRTR3PTR pR3Ptr);
int             pgmPhysGCPhys2CCPtrLockless(PVMCPUCC pVCpu, RTGCPHYS GCPhys, void **ppv);
int             pgmPhysCr3ToHCPtr(PVM pVM, RTGCPHYS GCPhys, PRTR3PTR pR3Ptr);
int             pgmPhysGCPhys2CCPtrInternalDepr(PVMCC pVM, PPGMPAGE pPage, RTGCPHYS GCPhys, void **ppv);
int             pgmPhysGCPhys2CCPtrInternal(PVMCC pVM, PPGMPAGE pPage, RTGCPHYS GCPhys, void **ppv, PPGMPAGEMAPLOCK pLock);
int             pgmPhysGCPhys2CCPtrInternalReadOnly(PVMCC pVM, PPGMPAGE pPage, RTGCPHYS GCPhys, const void **ppv, PPGMPAGEMAPLOCK pLock);
void            pgmPhysReleaseInternalPageMappingLock(PVMCC pVM, PPGMPAGEMAPLOCK pLock);
DECLCALLBACK(FNPGMPHYSHANDLER)      pgmPhysRomWriteHandler;
DECLCALLBACK(FNPGMPHYSHANDLER)      pgmPhysMmio2WriteHandler;
#ifndef IN_RING3
DECLCALLBACK(FNPGMRZPHYSPFHANDLER)  pgmPhysRomWritePfHandler;
DECLCALLBACK(FNPGMRZPHYSPFHANDLER)  pgmPhysMmio2WritePfHandler;
#endif
DECLHIDDEN(uint16_t) pgmPhysMmio2CalcChunkCount(RTGCPHYS cb, uint32_t *pcPagesPerChunk);
DECLHIDDEN(int) pgmPhysMmio2RegisterWorker(PVMCC pVM, uint32_t const cGuestPages, uint8_t const idMmio2,
                                           const uint8_t cChunks, PPDMDEVINSR3 const pDevIns, uint8_t
                                           const iSubDev, uint8_t const iRegion, uint32_t const fFlags);
DECLHIDDEN(int) pgmPhysMmio2DeregisterWorker(PVMCC pVM, uint8_t idMmio2, uint8_t cChunks, PPDMDEVINSR3 pDevIns);
int             pgmPhysFreePage(PVM pVM, PGMMFREEPAGESREQ pReq, uint32_t *pcPendingPages, PPGMPAGE pPage, RTGCPHYS GCPhys,
                                PGMPAGETYPE enmNewType);
#ifdef VBOX_STRICT
DECLHIDDEN(bool) pgmPhysAssertRamRangesLocked(PVMCC pVM, bool fInUpdate, bool fRamRelaxed);
#endif
DECLHIDDEN(void)            pgmR3PhysChunkInvalidateTLB(PVM pVM, bool fInRendezvous);
void            pgmPhysInvalidRamRangeTlbs(PVMCC pVM);
void            pgmPhysInvalidatePageMapTLB(PVMCC pVM, bool fInRendezvous);
void            pgmPhysInvalidatePageMapTLBEntry(PVMCC pVM, RTGCPHYS GCPhys);
DECLHIDDEN(PPGMRAMRANGE)    pgmPhysGetRangeSlow(PVMCC pVM, RTGCPHYS GCPhys);
DECLHIDDEN(PPGMRAMRANGE)    pgmPhysGetRangeAtOrAboveSlow(PVMCC pVM, RTGCPHYS GCPhys);
DECLHIDDEN(PPGMPAGE)        pgmPhysGetPageSlow(PVMCC pVM, RTGCPHYS GCPhys);
DECLHIDDEN(int)             pgmPhysGetPageExSlow(PVMCC pVM, RTGCPHYS GCPhys, PPPGMPAGE ppPage);
DECLHIDDEN(int)             pgmPhysGetPageAndRangeExSlow(PVMCC pVM, RTGCPHYS GCPhys, PPPGMPAGE ppPage, PPGMRAMRANGE *ppRam);
DECLHIDDEN(int)             pgmPhysGetPageAndRangeExSlowLockless(PVMCC pVM, PVMCPUCC pVCpu, RTGCPHYS GCPhys,
                                                                 PGMPAGE volatile **ppPage, PGMRAMRANGE volatile **ppRam);
DECLHIDDEN(int)             pgmPhysRamRangeAllocCommon(PVMCC pVM, uint32_t cPages, uint32_t fFlags, uint32_t *pidNewRange);
DECLHIDDEN(int)             pgmPhysRomRangeAllocCommon(PVMCC pVM, uint32_t cPages, uint8_t idRomRange, uint32_t fFlags);
#ifdef VBOX_WITH_NATIVE_NEM
DECLHIDDEN(void)            pgmPhysSetNemStateForPages(PPGMPAGE paPages, RTGCPHYS cPages, uint8_t u2State);
#endif

#ifdef IN_RING3
int             pgmR3PhysRamPreAllocate(PVM pVM);
int             pgmR3PhysRamReset(PVM pVM);
int             pgmR3PhysRomReset(PVM pVM);
int             pgmR3PhysRamZeroAll(PVM pVM);
int             pgmR3PhysChunkMap(PVM pVM, uint32_t idChunk, PPPGMCHUNKR3MAP ppChunk);
int             pgmR3PhysRamTerm(PVM pVM);
void            pgmR3PhysRomTerm(PVM pVM);
void            pgmR3PhysAssertSharedPageChecksums(PVM pVM);

int             pgmR3PoolInit(PVM pVM);
void            pgmR3PoolRelocate(PVM pVM);
void            pgmR3PoolResetUnpluggedCpu(PVM pVM, PVMCPU pVCpu);
void            pgmR3PoolReset(PVM pVM);
void            pgmR3PoolClearAll(PVM pVM, bool fFlushRemTlb);
DECLCALLBACK(VBOXSTRICTRC) pgmR3PoolClearAllRendezvous(PVM pVM, PVMCPU pVCpu, void *fpvFlushRemTbl);
void            pgmR3PoolWriteProtectPages(PVM pVM);

#endif /* IN_RING3 */
#ifdef IN_RING0
int             pgmR0PoolInitVM(PGVM pGVM);
#endif
int             pgmPoolAlloc(PVMCC pVM, RTGCPHYS GCPhys, PGMPOOLKIND enmKind, PGMPOOLACCESS enmAccess, bool fA20Enabled,
                             uint16_t iUser, uint32_t iUserTable, bool fLockPage, PPPGMPOOLPAGE ppPage);
void            pgmPoolFree(PVM pVM, RTHCPHYS HCPhys, uint16_t iUser, uint32_t iUserTable);
void            pgmPoolFreeByPage(PPGMPOOL pPool, PPGMPOOLPAGE pPage, uint16_t iUser, uint32_t iUserTable);
int             pgmPoolFlushPage(PPGMPOOL pPool, PPGMPOOLPAGE pPage, bool fFlush = true /* DO NOT USE false UNLESS YOU KNOWN WHAT YOU'RE DOING!! */);
void            pgmPoolFlushPageByGCPhys(PVM pVM, RTGCPHYS GCPhys);
PPGMPOOLPAGE    pgmPoolGetPage(PPGMPOOL pPool, RTHCPHYS HCPhys);
PPGMPOOLPAGE    pgmPoolQueryPageForDbg(PPGMPOOL pPool, RTHCPHYS HCPhys);
int             pgmPoolHCPhys2Ptr(PVM pVM, RTHCPHYS HCPhys, void **ppv);
int             pgmPoolSyncCR3(PVMCPUCC pVCpu);
bool            pgmPoolIsDirtyPageSlow(PVMCC pVM, RTGCPHYS GCPhys);
void            pgmPoolInvalidateDirtyPage(PVMCC pVM, RTGCPHYS GCPhysPT);
int             pgmPoolTrackUpdateGCPhys(PVMCC pVM, RTGCPHYS GCPhysPage, PPGMPAGE pPhysPage, bool fFlushPTEs, bool *pfFlushTLBs);
void            pgmPoolTracDerefGCPhysHint(PPGMPOOL pPool, PPGMPOOLPAGE pPage, RTHCPHYS HCPhys, RTGCPHYS GCPhysHint, uint16_t iPte);
uint16_t        pgmPoolTrackPhysExtAddref(PVMCC pVM, PPGMPAGE pPhysPage, uint16_t u16, uint16_t iShwPT, uint16_t iPte);
void            pgmPoolTrackPhysExtDerefGCPhys(PPGMPOOL pPool, PPGMPOOLPAGE pPoolPage, PPGMPAGE pPhysPage, uint16_t iPte);
void            pgmPoolMonitorChainFlush(PPGMPOOL pPool, PPGMPOOLPAGE pPage);
void            pgmPoolMonitorModifiedInsert(PPGMPOOL pPool, PPGMPOOLPAGE pPage);
FNPGMPHYSHANDLER     pgmPoolAccessHandler;
#ifndef IN_RING3
FNPGMRZPHYSPFHANDLER pgmRZPoolAccessPfHandler;
#endif

void            pgmPoolAddDirtyPage(PVMCC pVM, PPGMPOOL pPool, PPGMPOOLPAGE pPage);
void            pgmPoolResetDirtyPages(PVMCC pVM);
void            pgmPoolResetDirtyPage(PVMCC pVM, RTGCPTR GCPtrPage);

/** Gets the ring-0 pointer for the given pool page. */
DECLINLINE(R0PTRTYPE(PPGMPOOLPAGE)) pgmPoolConvertPageToR0(PPGMPOOL pPool, PPGMPOOLPAGE pPage)
{
#ifdef IN_RING3
    size_t offPage = (uintptr_t)pPage - (uintptr_t)pPool;
# ifdef VBOX_STRICT
    size_t iPage   = (offPage - RT_UOFFSETOF(PGMPOOL, aPages)) / sizeof(*pPage);
    AssertReturn(iPage < pPool->cMaxPages, NIL_RTR0PTR);
    AssertReturn(iPage * sizeof(*pPage) + RT_UOFFSETOF(PGMPOOL, aPages) == offPage, NIL_RTR0PTR);
# endif
    return pPool->pPoolR0 + offPage;
#else
    RT_NOREF(pPool);
    return pPage;
#endif
}

/** Gets the ring-3 pointer for the given pool page. */
DECLINLINE(R3PTRTYPE(PPGMPOOLPAGE)) pgmPoolConvertPageToR3(PPGMPOOL pPool, PPGMPOOLPAGE pPage)
{
#ifdef IN_RING3
    RT_NOREF(pPool);
    return pPage;
#else
    size_t offPage = (uintptr_t)pPage - (uintptr_t)pPool;
# ifdef VBOX_STRICT
    size_t iPage   = (offPage - RT_UOFFSETOF(PGMPOOL, aPages)) / sizeof(*pPage);
    AssertReturn(iPage < pPool->cMaxPages, NIL_RTR3PTR);
    AssertReturn(iPage * sizeof(*pPage) + RT_UOFFSETOF(PGMPOOL, aPages) == offPage, NIL_RTR3PTR);
# endif
    return pPool->pPoolR3 + offPage;
#endif
}

int             pgmR3ExitShadowModeBeforePoolFlush(PVMCPU pVCpu);
int             pgmR3ReEnterShadowModeAfterPoolFlush(PVM pVM, PVMCPU pVCpu);
void            pgmR3RefreshShadowModeAfterA20Change(PVMCPU pVCpu);

int             pgmShwMakePageSupervisorAndWritable(PVMCPUCC pVCpu, RTGCPTR GCPtr, bool fBigPage, uint32_t fOpFlags);
int             pgmShwSyncPaePDPtr(PVMCPUCC pVCpu, RTGCPTR GCPtr, X86PGPAEUINT uGstPdpe, PX86PDPAE *ppPD);
int             pgmShwSyncNestedPageLocked(PVMCPUCC pVCpu, RTGCPHYS GCPhysFault, uint32_t cPages, PGMMODE enmShwPagingMode);

int             pgmGstLazyMap32BitPD(PVMCPUCC pVCpu, PX86PD *ppPd);
int             pgmGstLazyMapPaePDPT(PVMCPUCC pVCpu, PX86PDPT *ppPdpt);
int             pgmGstLazyMapPaePD(PVMCPUCC pVCpu, uint32_t iPdpt, PX86PDPAE *ppPd);
int             pgmGstLazyMapPml4(PVMCPUCC pVCpu, PX86PML4 *ppPml4);
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
int             pgmGstLazyMapEptPml4(PVMCPUCC pVCpu, PEPTPML4 *ppPml4);
#endif
int             pgmGstPtWalk(PVMCPUCC pVCpu, RTGCPTR GCPtr, PPGMPTWALK pWalk, PPGMPTWALKGST pGstWalk);
int             pgmGstPtWalkNext(PVMCPUCC pVCpu, RTGCPTR GCPtr, PPGMPTWALK pWalk, PPGMPTWALKGST pGstWalk);

# if defined(VBOX_STRICT) && HC_ARCH_BITS == 64 && defined(IN_RING3)
FNDBGCCMD       pgmR3CmdCheckDuplicatePages;
FNDBGCCMD       pgmR3CmdShowSharedModules;
# endif

void            pgmLogState(PVM pVM);

RT_C_DECLS_END

/** @} */

#endif /* !VMM_INCLUDED_SRC_include_PGMInternal_h */

