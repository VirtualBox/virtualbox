/** @file
 * PGM - Page Monitor / Monitor.
 */

/*
 * Copyright (C) 2006-2025 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_vmm_pgm_h
#define VBOX_INCLUDED_vmm_pgm_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/types.h>
#include <VBox/sup.h>
#include <VBox/vmm/vmapi.h>
#include <VBox/vmm/gmm.h>               /* for PGMMREGISTERSHAREDMODULEREQ */
#include <VBox/vmm/hm_vmx.h>
#include <iprt/x86.h>
#include <VBox/param.h>

RT_C_DECLS_BEGIN

/** @defgroup grp_pgm   The Page Monitor / Manager API
 * @ingroup grp_vmm
 * @{
 */

/**
 * FNPGMRELOCATE callback mode.
 */
typedef enum PGMRELOCATECALL
{
    /** The callback is for checking if the suggested address is suitable. */
    PGMRELOCATECALL_SUGGEST = 1,
    /** The callback is for executing the relocation. */
    PGMRELOCATECALL_RELOCATE
} PGMRELOCATECALL;


/**
 * Callback function which will be called when PGM is trying to find
 * a new location for the mapping.
 *
 * The callback is called in two modes, 1) the check mode and 2) the relocate mode.
 * In 1) the callback should say if it objects to a suggested new location. If it
 * accepts the new location, it is called again for doing it's relocation.
 *
 *
 * @returns true if the location is ok.
 * @returns false if another location should be found.
 * @param   pVM         The cross context VM structure.
 * @param   GCPtrOld    The old virtual address.
 * @param   GCPtrNew    The new virtual address.
 * @param   enmMode     Used to indicate the callback mode.
 * @param   pvUser      User argument.
 * @remark  The return value is no a failure indicator, it's an acceptance
 *          indicator. Relocation can not fail!
 */
typedef DECLCALLBACKTYPE(bool, FNPGMRELOCATE,(PVM pVM, RTGCPTR GCPtrOld, RTGCPTR GCPtrNew, PGMRELOCATECALL enmMode, void *pvUser));
/** Pointer to a relocation callback function. */
typedef FNPGMRELOCATE *PFNPGMRELOCATE;


/**
 * Memory access origin.
 */
typedef enum PGMACCESSORIGIN
{
    /** Invalid zero value. */
    PGMACCESSORIGIN_INVALID = 0,
    /** IEM is access memory. */
    PGMACCESSORIGIN_IEM,
    /** HM is access memory. */
    PGMACCESSORIGIN_HM,
    /** Some device is access memory. */
    PGMACCESSORIGIN_DEVICE,
    /** Someone debugging is access memory. */
    PGMACCESSORIGIN_DEBUGGER,
    /** SELM is access memory. */
    PGMACCESSORIGIN_SELM,
    /** FTM is access memory. */
    PGMACCESSORIGIN_FTM,
    /** REM is access memory. */
    PGMACCESSORIGIN_REM,
    /** IOM is access memory. */
    PGMACCESSORIGIN_IOM,
    /** End of valid values. */
    PGMACCESSORIGIN_END,
    /** Type size hack. */
    PGMACCESSORIGIN_32BIT_HACK = 0x7fffffff
} PGMACCESSORIGIN;


/**
 * Physical page access handler kind.
 */
typedef enum PGMPHYSHANDLERKIND
{
    /** Invalid zero value. */
    PGMPHYSHANDLERKIND_INVALID = 0,
    /** MMIO range. Pages are not present, all access is done in interpreter or recompiler. */
    PGMPHYSHANDLERKIND_MMIO,
    /** Handler all write access to a physical page range. */
    PGMPHYSHANDLERKIND_WRITE,
    /** Handler all access to a physical page range. */
    PGMPHYSHANDLERKIND_ALL,
    /** End of the valid values. */
    PGMPHYSHANDLERKIND_END,
    /** Type size hack. */
    PGMPHYSHANDLERKIND_32BIT_HACK = 0x7fffffff
} PGMPHYSHANDLERKIND;

/**
 * Guest Access type
 */
typedef enum PGMACCESSTYPE
{
    /** Read access. */
    PGMACCESSTYPE_READ = 1,
    /** Write access. */
    PGMACCESSTYPE_WRITE
} PGMACCESSTYPE;


/** @def PGM_ALL_CB_DECL
 * Macro for declaring a handler callback for all contexts.  The handler
 * callback is static in ring-3, and exported in RC and R0.
 * @sa PGM_ALL_CB2_DECL.
 */
#if defined(IN_RC) || defined(IN_RING0)
# ifdef __cplusplus
#  define PGM_ALL_CB_DECL(type)     extern "C" DECLCALLBACK(DECLEXPORT(type))
# else
#  define PGM_ALL_CB_DECL(type)     DECLCALLBACK(DECLEXPORT(type))
# endif
#else
# define PGM_ALL_CB_DECL(type)      static DECLCALLBACK(type)
#endif

/** @def PGM_ALL_CB2_DECL
 * Macro for declaring a handler callback for all contexts.  The handler
 * callback is hidden in ring-3, and exported in RC and R0.
 * @sa PGM_ALL_CB2_DECL.
 */
#if defined(IN_RC) || defined(IN_RING0)
# ifdef __cplusplus
#  define PGM_ALL_CB2_DECL(type)    extern "C" DECLCALLBACK(DECLEXPORT(type))
# else
#  define PGM_ALL_CB2_DECL(type)    DECLCALLBACK(DECLEXPORT(type))
# endif
#else
# define PGM_ALL_CB2_DECL(type)     DECL_HIDDEN_CALLBACK(type)
#endif

/** @def PGM_ALL_CB2_PROTO
 * Macro for declaring a handler callback for all contexts.  The handler
 * callback is hidden in ring-3, and exported in RC and R0.
 * @param   fnType      The callback function type.
 * @sa PGM_ALL_CB2_DECL.
 */
#if defined(IN_RC) || defined(IN_RING0)
# ifdef __cplusplus
#  define PGM_ALL_CB2_PROTO(fnType)    extern "C" DECLEXPORT(fnType)
# else
#  define PGM_ALL_CB2_PROTO(fnType)    DECLEXPORT(fnType)
# endif
#else
# define PGM_ALL_CB2_PROTO(fnType)     DECLHIDDEN(fnType)
#endif


/**
 * \#PF Handler callback for physical access handler ranges in RC and R0.
 *
 * @returns Strict VBox status code (appropriate for ring-0 and raw-mode).
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   uErrorCode  CPU Error code.
 * @param   pCtx        Pointer to the register context for the CPU.
 * @param   pvFault     The fault address (cr2).
 * @param   GCPhysFault The GC physical address corresponding to pvFault.
 * @param   uUser       User argument (not a pointer).
 * @thread  EMT(pVCpu)
 */
typedef DECLCALLBACKTYPE(VBOXSTRICTRC, FNPGMRZPHYSPFHANDLER,(PVMCC pVM, PVMCPUCC pVCpu, RTGCUINT uErrorCode, PCPUMCTX pCtx,
                                                             RTGCPTR pvFault, RTGCPHYS GCPhysFault, uint64_t uUser));
/** Pointer to PGM access callback. */
typedef FNPGMRZPHYSPFHANDLER *PFNPGMRZPHYSPFHANDLER;


/**
 * Access handler callback for physical access handler ranges.
 *
 * The handler can not raise any faults, it's mainly for monitoring write access
 * to certain pages (like MMIO).
 *
 * @returns Strict VBox status code in ring-0 and raw-mode context, in ring-3
 *          the only supported informational status code is
 *          VINF_PGM_HANDLER_DO_DEFAULT.
 * @retval  VINF_SUCCESS if the handler have carried out the operation.
 * @retval  VINF_PGM_HANDLER_DO_DEFAULT if the caller should carry out the
 *          access operation.
 * @retval  VINF_EM_XXX in ring-0 and raw-mode context.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   GCPhys          The physical address the guest is writing to.
 * @param   pvPhys          The HC mapping of that address.
 * @param   pvBuf           What the guest is reading/writing.
 * @param   cbBuf           How much it's reading/writing.
 * @param   enmAccessType   The access type.
 * @param   enmOrigin       The origin of this call.
 * @param   uUser           User argument (not a pointer).
 * @thread  EMT(pVCpu)
 */
typedef DECLCALLBACKTYPE(VBOXSTRICTRC, FNPGMPHYSHANDLER,(PVMCC pVM, PVMCPUCC pVCpu, RTGCPHYS GCPhys, void *pvPhys,
                                                         void *pvBuf, size_t cbBuf, PGMACCESSTYPE enmAccessType,
                                                         PGMACCESSORIGIN enmOrigin, uint64_t uUser));
/** Pointer to PGM access callback. */
typedef FNPGMPHYSHANDLER *PFNPGMPHYSHANDLER;


/** @todo r=aeichner This doesn't seem to be used outside of the VMM module, so we might make
 *                   all APIs (PGMGetGuestMode(), etc.) internal and split this up into an
 *                   x86 and arm specific header. */
/**
 * Paging mode.
 *
 * @note    Part of saved state.  Change with extreme care.
 * @note    Due to PGMGetShadowMode() and the possibility that we will be
 *          running ARMv8 VMs on a AMD64 hosts, it's safer to combine these
 *          modes. We could rethink this if we start using PGMMODE exclusively
 *          for the guest mode and come up with a different enum for the host.
 */
typedef enum PGMMODE
{
    /** The usual invalid value. */
    PGMMODE_INVALID = 0,

    /** @name X86
     * @{ */
    /** Real mode. */
    PGMMODE_REAL,
    /** Protected mode, no paging. */
    PGMMODE_PROTECTED,
    /** 32-bit paging. */
    PGMMODE_32_BIT,
    /** PAE paging. */
    PGMMODE_PAE,
    /** PAE paging with NX enabled. */
    PGMMODE_PAE_NX,
    /** 64-bit AMD paging (long mode). */
    PGMMODE_AMD64,
    /** 64-bit AMD paging (long mode) with NX enabled. */
    PGMMODE_AMD64_NX,
    /** 32-bit nested paging mode (shadow only; guest physical to host physical). */
    PGMMODE_NESTED_32BIT,
    /** PAE nested paging mode (shadow only; guest physical to host physical). */
    PGMMODE_NESTED_PAE,
    /** AMD64 nested paging mode (shadow only; guest physical to host physical). */
    PGMMODE_NESTED_AMD64,
    /** Extended paging (Intel) mode. */
    PGMMODE_EPT,
    /** @} */

    /** ARMv8: Paging is not enabled by the guest.
     * AMD64 host: Special mode used by NEM to indicate no shadow paging
     * necessary.  Not used by X86 guests. */
    PGMMODE_NONE = 32,

    /** @name ARMv8
     * @{ */
    /** VMSAv8-32 Virtual Memory System Architecture v8 - 32-bit variant enabled. */
    PGMMODE_VMSA_V8_32,
    /** VMSAv8-64 Virtual Memory System Architecture v8 - 64-bit variant enabled. */
    PGMMODE_VMSA_V8_64,
    /** @} */

    /** The max number of modes */
    PGMMODE_MAX,
    /** 32bit hackishness. */
    PGMMODE_32BIT_HACK = 0x7fffffff
} PGMMODE;


/**
 * Second level address translation (SLAT) mode.
 */
typedef enum PGMSLAT
{
    /** The usual invalid value. */
    PGMSLAT_INVALID = 0,
    /** No second level translation. */
    PGMSLAT_DIRECT,
    /** Intel Extended Page Tables (EPT). */
    PGMSLAT_EPT,
    /** AMD-V Nested Paging 32-bit. */
    PGMSLAT_32BIT,
    /** AMD-V Nested Paging PAE. */
    PGMSLAT_PAE,
    /** AMD-V Nested Paging 64-bit. */
    PGMSLAT_AMD64,
    /** 32bit hackishness. */
    PGMSLAT_32BIT_HACK = 0x7fffffff
} PGMSLAT;


/** @name PGMPTWALK::fFailed flags.
 * These flags indicate the type of a page-walk failure.
 * @{
 */
typedef uint32_t PGMWALKFAIL;
/** No fault. */
#define PGM_WALKFAIL_SUCCESS                        UINT32_C(0)

/** Not present (X86_TRAP_PF_P). */
#define PGM_WALKFAIL_NOT_PRESENT                    RT_BIT_32(0)
/** Reserved bit set in table entry (X86_TRAP_PF_RSVD). */
#define PGM_WALKFAIL_RESERVED_BITS                  RT_BIT_32(1)
/** Bad physical address (VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS). */
#define PGM_WALKFAIL_BAD_PHYSICAL_ADDRESS           RT_BIT_32(2)

/** EPT violation - Intel. */
#define PGM_WALKFAIL_EPT_VIOLATION                  RT_BIT_32(3)
/** EPT violation, convertible to \#VE exception - Intel. */
#define PGM_WALKFAIL_EPT_VIOLATION_CONVERTIBLE      RT_BIT_32(4)
/** EPT misconfiguration - Intel. */
#define PGM_WALKFAIL_EPT_MISCONFIG                  RT_BIT_32(5)
/** Mask of all EPT induced page-walk failures - Intel. */
#define PGM_WALKFAIL_EPT                            (  PGM_WALKFAIL_EPT_VIOLATION \
                                                     | PGM_WALKFAIL_EPT_VIOLATION_CONVERTIBLE \
                                                     | PGM_WALKFAIL_EPT_MISCONFIG)

/** Access denied: Not writable (VERR_ACCESS_DENIED). */
#define PGM_WALKFAIL_NOT_WRITABLE                   RT_BIT_32(6)
/** Access denied: Not executable (VERR_ACCESS_DENIED). */
#define PGM_WALKFAIL_NOT_EXECUTABLE                 RT_BIT_32(7)
/** Access denied: Not user/supervisor mode accessible (VERR_ACCESS_DENIED). */
#define PGM_WALKFAIL_NOT_ACCESSIBLE_BY_MODE         RT_BIT_32(8)

/** The level the problem arrised at.
 * PTE is level 1, PDE is level 2, PDPE is level 3, PML4 is level 4, CR3 is
 * level 8.  This is 0 on success. */
#define PGM_WALKFAIL_LEVEL_MASK                     UINT32_C(0x0000f800)
/** Level shift (see PGM_WALKFAIL_LEVEL_MASK).   */
#define PGM_WALKFAIL_LEVEL_SHIFT                    11

/** @} */


/** PGM page-table attributes.
 *
 * This is VirtualBox's combined page table attributes.  This combines
 * attributes from the regular page/translation tables and the nested page
 * tables / stage 2 translation tables. */
typedef uint64_t PGMPTATTRS;
/** Pointer to a PGMPTATTRS type. */
typedef PGMPTATTRS *PPGMPTATTRS;

#if defined(VBOX_VMM_TARGET_X86) || defined(DOXYGEN_RUNNING)
/** @name PGM_PTATTRS_XXX - PGM page-table attributes, x86 edition.
 *
 * The following bits map 1:1 (shifted by PGM_PTATTRS_EPT_SHIFT) to the Intel EPT
 * attributes as these are unique to EPT and fit within 64-bits despite the shift:
 *   - EPT_R         : Read access.
 *   - EPT_W         : Write access.
 *   - EPT_X_SUPER   : Execute or execute for supervisor-mode linear addr access.
 *   - EPT_MEMTYPE   : EPT memory type.
 *   - EPT_IGNORE_PAT: Ignore PAT memory type.
 *   - EPT_X_USER    : Execute access for user-mode linear addresses.
 *
 * For regular page tables, the R bit is always 1 (same as P bit).
 * For Intel EPT, the EPT_R and EPT_W bits are copied to R and W bits respectively.
 *
 * The following EPT attributes are mapped to the following positions because they
 * exist in the regular page tables at these positions OR are exclusive to EPT and
 * have been mapped to arbitrarily chosen positions:
 *   - EPT_A               : Accessed                (EPT bit  8 maps to bit  5).
 *   - EPT_D               : Dirty                   (EPT bit  9 maps to bit  6).
 *   - EPT_SUPER_SHW_STACK : Supervisor Shadow Stack (EPT bit 60 maps to bit 24).
 *   - EPT_SUPPRESS_VE_XCPT: Suppress \#VE exception (EPT bit 63 maps to bit 25).
 *
 * Bits 12, 11:9 and 43 are deliberately kept unused (correspond to bit PS and bits
 * 11:9 in the regular page-table structures and to bit 11 in the EPT structures
 * respectively) as bit 12 is the page-size bit and bits 11:9 are reserved for
 * use by software and we may want to use/preserve them in the future.
 *
 * @{ */
/** Read bit (always 1 for regular PT, copy of EPT_R for EPT). */
#define PGM_PTATTRS_R_SHIFT                         0
#define PGM_PTATTRS_R_MASK                          RT_BIT_64(PGM_PTATTRS_R_SHIFT)
/** Write access bit (aka read/write bit for regular PT). */
#define PGM_PTATTRS_W_SHIFT                         1
#define PGM_PTATTRS_W_MASK                          RT_BIT_64(PGM_PTATTRS_W_SHIFT)
/** User-mode access bit. */
#define PGM_PTATTRS_US_SHIFT                        2
#define PGM_PTATTRS_US_MASK                         RT_BIT_64(PGM_PTATTRS_US_SHIFT)
/** Write through cache bit. */
#define PGM_PTATTRS_PWT_SHIFT                       3
#define PGM_PTATTRS_PWT_MASK                        RT_BIT_64(PGM_PTATTRS_PWT_SHIFT)
/** Cache disabled bit. */
#define PGM_PTATTRS_PCD_SHIFT                       4
#define PGM_PTATTRS_PCD_MASK                        RT_BIT_64(PGM_PTATTRS_PCD_SHIFT)
/** Accessed bit. */
#define PGM_PTATTRS_A_SHIFT                         5
#define PGM_PTATTRS_A_MASK                          RT_BIT_64(PGM_PTATTRS_A_SHIFT)
/** Dirty bit. */
#define PGM_PTATTRS_D_SHIFT                         6
#define PGM_PTATTRS_D_MASK                          RT_BIT_64(PGM_PTATTRS_D_SHIFT)
/** The PAT bit. */
#define PGM_PTATTRS_PAT_SHIFT                       7
#define PGM_PTATTRS_PAT_MASK                        RT_BIT_64(PGM_PTATTRS_PAT_SHIFT)
/** The global bit. */
#define PGM_PTATTRS_G_SHIFT                         8
#define PGM_PTATTRS_G_MASK                          RT_BIT_64(PGM_PTATTRS_G_SHIFT)
/** Reserved (bits 12:9) unused. */
#define PGM_PTATTRS_RSVD_12_9_SHIFT                 9
#define PGM_PTATTRS_RSVD_12_9_MASK                  UINT64_C(0x0000000000001e00)
/** Read access bit - EPT only. */
#define PGM_PTATTRS_EPT_R_SHIFT                     13
#define PGM_PTATTRS_EPT_R_MASK                      RT_BIT_64(PGM_PTATTRS_EPT_R_SHIFT)
/** Write access bit - EPT only. */
#define PGM_PTATTRS_EPT_W_SHIFT                     14
#define PGM_PTATTRS_EPT_W_MASK                      RT_BIT_64(PGM_PTATTRS_EPT_W_SHIFT)
/** Execute or execute access for supervisor-mode linear addresses - EPT only. */
#define PGM_PTATTRS_EPT_X_SUPER_SHIFT               15
#define PGM_PTATTRS_EPT_X_SUPER_MASK                RT_BIT_64(PGM_PTATTRS_EPT_X_SUPER_SHIFT)
/** EPT memory type - EPT only. */
#define PGM_PTATTRS_EPT_MEMTYPE_SHIFT               16
#define PGM_PTATTRS_EPT_MEMTYPE_MASK                UINT64_C(0x0000000000070000)
/** Ignore PAT memory type - EPT only. */
#define PGM_PTATTRS_EPT_IGNORE_PAT_SHIFT            19
#define PGM_PTATTRS_EPT_IGNORE_PAT_MASK             RT_BIT_64(PGM_PTATTRS_EPT_IGNORE_PAT_SHIFT)
/** Leaf paging entry (big or regular) - EPT only. */
#define PGM_PTATTRS_EPT_LEAF_SHIFT                  20
#define PGM_PTATTRS_EPT_LEAF_MASK                   RT_BIT_64(PGM_PTATTRS_EPT_LEAF_SHIFT)
/** Accessed bit - EPT only. */
#define PGM_PTATTRS_EPT_A_SHIFT                     21
#define PGM_PTATTRS_EPT_A_MASK                      RT_BIT_64(PGM_PTATTRS_EPT_A_SHIFT)
/** Dirty bit - EPT only. */
#define PGM_PTATTRS_EPT_D_SHIFT                     22
#define PGM_PTATTRS_EPT_D_MASK                      RT_BIT_64(PGM_PTATTRS_EPT_D_SHIFT)
/** Execute access for user-mode linear addresses - EPT only. */
#define PGM_PTATTRS_EPT_X_USER_SHIFT                23
#define PGM_PTATTRS_EPT_X_USER_MASK                 RT_BIT_64(PGM_PTATTRS_EPT_X_USER_SHIFT)
/** Reserved (bits 29:24) - unused. */
#define PGM_PTATTRS_RSVD_29_24_SHIFT                24
#define PGM_PTATTRS_RSVD_29_24_MASK                 UINT64_C(0x000000003f000000)
/** Verify Guest Paging - EPT only. */
#define PGM_PTATTRS_EPT_VGP_SHIFT                   30
#define PGM_PTATTRS_EPT_VGP_MASK                    RT_BIT_64(PGM_PTATTRS_EPT_VGP_SHIFT)
/** Paging-write - EPT only. */
#define PGM_PTATTRS_EPT_PW_SHIFT                    31
#define PGM_PTATTRS_EPT_PW_MASK                     RT_BIT_64(PGM_PTATTRS_EPT_PW_SHIFT)
/** Reserved (bit 32) - unused. */
#define PGM_PTATTRS_RSVD_32_SHIFT                   32
#define PGM_PTATTRS_RSVD_32_MASK                    UINT64_C(0x0000000100000000)
/** Supervisor shadow stack - EPT only. */
#define PGM_PTATTRS_EPT_SSS_SHIFT                   33
#define PGM_PTATTRS_EPT_SSS_MASK                    RT_BIT_64(PGM_PTATTRS_EPT_SSS_SHIFT)
/** Sub-page write permission - EPT only. */
#define PGM_PTATTRS_EPT_SPP_SHIFT                   34
#define PGM_PTATTRS_EPT_SPP_MASK                    RT_BIT_64(PGM_PTATTRS_EPT_SPP_SHIFT)
/** Reserved (bit 35) - unused. */
#define PGM_PTATTRS_RSVD_35_SHIFT                   35
#define PGM_PTATTRS_RSVD_35_MASK                    UINT64_C(0x0000000800000000)
/** Suppress \#VE exception - EPT only. */
#define PGM_PTATTRS_EPT_SVE_SHIFT                   36
#define PGM_PTATTRS_EPT_SVE_MASK                    RT_BIT_64(PGM_PTATTRS_EPT_SVE_SHIFT)
/** Reserved (bits 62:37) - unused. */
#define PGM_PTATTRS_RSVD_62_37_SHIFT                37
#define PGM_PTATTRS_RSVD_62_37_MASK                 UINT64_C(0x7fffffe000000000)
/** No-execute bit. */
#define PGM_PTATTRS_NX_SHIFT                        63
#define PGM_PTATTRS_NX_MASK                         RT_BIT_64(PGM_PTATTRS_NX_SHIFT)

RT_BF_ASSERT_COMPILE_CHECKS(PGM_PTATTRS_, UINT64_C(0), UINT64_MAX,
                            (R, W, US, PWT, PCD, A, D, PAT, G, RSVD_12_9, EPT_R, EPT_W, EPT_X_SUPER, EPT_MEMTYPE, EPT_IGNORE_PAT,
                             EPT_LEAF, EPT_A, EPT_D, EPT_X_USER, RSVD_29_24, EPT_VGP, EPT_PW, RSVD_32, EPT_SSS, EPT_SPP,
                             RSVD_35, EPT_SVE, RSVD_62_37, NX));

/** The bit position where the EPT specific attributes begin. */
#define PGM_PTATTRS_EPT_SHIFT                       PGM_PTATTRS_EPT_R_SHIFT
/** The mask of EPT bits (bits 36:ATTR_SHIFT). In the future we might choose to
 *  use higher unused bits for something else, in that case adjust this mask. */
#define PGM_PTATTRS_EPT_MASK                        UINT64_C(0x0000001fffffe000)

/** The mask of all PGM page attribute bits for regular page-tables. */
#define PGM_PTATTRS_PT_VALID_MASK                   (  PGM_PTATTRS_R_MASK \
                                                     | PGM_PTATTRS_W_MASK \
                                                     | PGM_PTATTRS_US_MASK \
                                                     | PGM_PTATTRS_PWT_MASK \
                                                     | PGM_PTATTRS_PCD_MASK \
                                                     | PGM_PTATTRS_A_MASK \
                                                     | PGM_PTATTRS_D_MASK \
                                                     | PGM_PTATTRS_PAT_MASK \
                                                     | PGM_PTATTRS_G_MASK \
                                                     | PGM_PTATTRS_NX_MASK)

/** The mask of all PGM page attribute bits for EPT. */
#define PGM_PTATTRS_EPT_VALID_MASK                  (  PGM_PTATTRS_EPT_R_MASK \
                                                     | PGM_PTATTRS_EPT_W_MASK \
                                                     | PGM_PTATTRS_EPT_X_SUPER_MASK \
                                                     | PGM_PTATTRS_EPT_MEMTYPE_MASK \
                                                     | PGM_PTATTRS_EPT_IGNORE_PAT_MASK \
                                                     | PGM_PTATTRS_EPT_LEAF_MASK \
                                                     | PGM_PTATTRS_EPT_A_MASK \
                                                     | PGM_PTATTRS_EPT_D_MASK \
                                                     | PGM_PTATTRS_EPT_X_USER_MASK \
                                                     | PGM_PTATTRS_EPT_VGP_MASK \
                                                     | PGM_PTATTRS_EPT_PW_MASK \
                                                     | PGM_PTATTRS_EPT_SSS_MASK \
                                                     | PGM_PTATTRS_EPT_SPP_MASK \
                                                     | PGM_PTATTRS_EPT_SVE_MASK)

/* The mask of all PGM page attribute bits (combined). */
#define PGM_PTATTRS_VALID_MASK                      (PGM_PTATTRS_PT_VALID_MASK | PGM_PTATTRS_EPT_VALID_MASK)

/* Verify bits match the regular PT bits. */
AssertCompile(PGM_PTATTRS_W_SHIFT   == X86_PTE_BIT_RW);
AssertCompile(PGM_PTATTRS_US_SHIFT  == X86_PTE_BIT_US);
AssertCompile(PGM_PTATTRS_PWT_SHIFT == X86_PTE_BIT_PWT);
AssertCompile(PGM_PTATTRS_PCD_SHIFT == X86_PTE_BIT_PCD);
AssertCompile(PGM_PTATTRS_A_SHIFT   == X86_PTE_BIT_A);
AssertCompile(PGM_PTATTRS_D_SHIFT   == X86_PTE_BIT_D);
AssertCompile(PGM_PTATTRS_PAT_SHIFT == X86_PTE_BIT_PAT);
AssertCompile(PGM_PTATTRS_G_SHIFT   == X86_PTE_BIT_G);
AssertCompile(PGM_PTATTRS_W_MASK    == X86_PTE_RW);
AssertCompile(PGM_PTATTRS_US_MASK   == X86_PTE_US);
AssertCompile(PGM_PTATTRS_PWT_MASK  == X86_PTE_PWT);
AssertCompile(PGM_PTATTRS_PCD_MASK  == X86_PTE_PCD);
AssertCompile(PGM_PTATTRS_A_MASK    == X86_PTE_A);
AssertCompile(PGM_PTATTRS_D_MASK    == X86_PTE_D);
AssertCompile(PGM_PTATTRS_PAT_MASK  == X86_PTE_PAT);
AssertCompile(PGM_PTATTRS_G_MASK    == X86_PTE_G);
AssertCompile(PGM_PTATTRS_NX_MASK   == X86_PTE_PAE_NX);

/* Verify those EPT bits that must map 1:1 (after shifting). */
AssertCompile(PGM_PTATTRS_EPT_R_SHIFT          - PGM_PTATTRS_EPT_SHIFT == EPT_E_BIT_READ);
AssertCompile(PGM_PTATTRS_EPT_W_SHIFT          - PGM_PTATTRS_EPT_SHIFT == EPT_E_BIT_WRITE);
AssertCompile(PGM_PTATTRS_EPT_X_SUPER_SHIFT    - PGM_PTATTRS_EPT_SHIFT == EPT_E_BIT_EXECUTE);
AssertCompile(PGM_PTATTRS_EPT_IGNORE_PAT_SHIFT - PGM_PTATTRS_EPT_SHIFT == EPT_E_BIT_IGNORE_PAT);
AssertCompile(PGM_PTATTRS_EPT_X_USER_SHIFT     - PGM_PTATTRS_EPT_SHIFT == EPT_E_BIT_USER_EXECUTE);
/** @}  */
#endif /* VBOX_VMM_TARGET_X86 || DOXYGEN_RUNNING */

#if defined(VBOX_VMM_TARGET_ARMV8) || defined(DOXYGEN_RUNNING)
/** @name PGM_PTATTRS_XXX - PGM page-table attributes, ARMv8 edition.
 *
 * The translation tables on ARMv8 are complicated by compressed and index
 * attributes as well as a myriade of feature dependent field interpretations.
 *
 * The stage 1 effective access attributes are placed in bits 41:32.  A set of
 * leaf bits are copied raw, but NSE had to be shifted down due to nG confusion.
 *
 * The stage 2 effective access attributes are placed in bit 31:24.  Bits taken
 * directly from the leaf translation table entry are shifted down 7 bits to
 * avoid collision with similar bits from the stage 1 leaf.
 *
 * @{ */

/** Stage 2, page/block: D - dirty flag. (shifted down 7) */
#define PGM_PTATTRS_S2_D_SHIFT                      0
#define PGM_PTATTRS_S2_D_MASK                       RT_BIT_64(PGM_PTATTRS_S2_D_SHIFT)
/** Stage 2, page/block: AF - access flag. (shifted down 7) */
#define PGM_PTATTRS_S2_AF_SHIFT                     3
#define PGM_PTATTRS_S2_AF_MASK                      RT_BIT_64(PGM_PTATTRS_S2_AF_SHIFT)
/** Page/block level: NS - Non-secure. */
#define PGM_PTATTRS_NS_SHIFT                        5
#define PGM_PTATTRS_NS_MASK                         RT_BIT_64(PGM_PTATTRS_NS_SHIFT)
/** Page/block level: NSE - Non-secure extension (?) - FEAT_RME, FEAT_SEL2.
 * @note Overlaps with nG, shifted down. */
#define PGM_PTATTRS_NSE_SHIFT                       6
#define PGM_PTATTRS_NSE_MASK                        RT_BIT_64(PGM_PTATTRS_NSE_SHIFT)
/** Page/block level: nD - Not dirty. */
#define PGM_PTATTRS_ND_SHIFT                        7
#define PGM_PTATTRS_ND_MASK                         RT_BIT_64(PGM_PTATTRS_ND_SHIFT)
/** Stage 2, page/block: nT, FEAT_BBM. Only supported with 64KB page size. */
#define PGM_PTATTRS_S2_NT_SHIFT                     9
#define PGM_PTATTRS_S2_NT_MASK                      RT_BIT_64(PGM_PTATTRS_S2_NT_SHIFT)
/** Combined: AF - Access flag.
 * @note The table and page/block AF attributes ANDed together. */
#define PGM_PTATTRS_AF_SHIFT                        10
#define PGM_PTATTRS_AF_MASK                         RT_BIT_64(PGM_PTATTRS_AF_SHIFT)
/** Page/block level: nG - Not global nG bit. */
#define PGM_PTATTRS_NG_SHIFT                        11
#define PGM_PTATTRS_NG_MASK                         RT_BIT_64(PGM_PTATTRS_NG_SHIFT)
/** Page/block level: nT, FEAT_BBM. Only supported with 64KB page size. */
#define PGM_PTATTRS_NT_SHIFT                        16
#define PGM_PTATTRS_NT_MASK                         RT_BIT_64(PGM_PTATTRS_NT_SHIFT)

/** Stage 2: Read access. */
#define PGM_PTATTRS_S2_R_SHIFT                      24
#define PGM_PTATTRS_S2_R_MASK                       RT_BIT_64(PGM_PTATTRS_S2_R_SHIFT)
/** Stage 2: Full write access. */
#define PGM_PTATTRS_S2_W_SHIFT                      25
#define PGM_PTATTRS_S2_W_MASK                       RT_BIT_64(PGM_PTATTRS_S2_W_SHIFT)
/** Stage 2: Privileged execution access. */
#define PGM_PTATTRS_S2_PX_SHIFT                     26
#define PGM_PTATTRS_S2_PX_MASK                      RT_BIT_64(PGM_PTATTRS_S2_PX_SHIFT)
/** Stage 2: Unprivileged execution access. */
#define PGM_PTATTRS_S2_UX_SHIFT                     27
#define PGM_PTATTRS_S2_UX_MASK                      RT_BIT_64(PGM_PTATTRS_S2_UX_SHIFT)
/** Stage 2: Limited write access - only MMU and RCW. */
#define PGM_PTATTRS_S2_W_LIM_SHIFT                  28
#define PGM_PTATTRS_S2_W_LIM_MASK                   RT_BIT_64(PGM_PTATTRS_S2_W_LIM_SHIFT)
/** Stage 2: TopLevel0 - only used with PGM_PTATTRS_S2_W_LIM_MASK. */
#define PGM_PTATTRS_S2_TL0_SHIFT                    29
#define PGM_PTATTRS_S2_TL0_MASK                     RT_BIT_64(PGM_PTATTRS_S2_TL0_SHIFT)
/** Stage 2: TopLevel1 - only used with PGM_PTATTRS_S2_W_LIM_MASK. */
#define PGM_PTATTRS_S2_TL1_SHIFT                    30
#define PGM_PTATTRS_S2_TL1_MASK                     RT_BIT_64(PGM_PTATTRS_S2_TL1_SHIFT)
/** Stage 2: Device memory type. */
#define PGM_PTATTRS_S2_DEVICE_SHIFT                 31
#define PGM_PTATTRS_S2_DEVICE_MASK                  RT_BIT_64(PGM_PTATTRS_S2_DEVICE_SHIFT)

/** Stage 1: Privileged read access. */
#define PGM_PTATTRS_PR_SHIFT                        32
#define PGM_PTATTRS_PR_MASK                         RT_BIT_64(PGM_PTATTRS_PR_SHIFT)
/** Stage 1:  Privileged write access. */
#define PGM_PTATTRS_PW_SHIFT                        33
#define PGM_PTATTRS_PW_MASK                         RT_BIT_64(PGM_PTATTRS_PW_SHIFT)
/** Stage 1:  Privileged execute access. */
#define PGM_PTATTRS_PX_SHIFT                        34
#define PGM_PTATTRS_PX_MASK                         RT_BIT_64(PGM_PTATTRS_PX_SHIFT)
/** Stage 1:  Privileged guarded control stack (GCS) access. */
#define PGM_PTATTRS_PGCS_SHIFT                      35
#define PGM_PTATTRS_PGCS_MASK                       RT_BIT_64(PGM_PTATTRS_PGCS_SHIFT)
/** Stage 1:  Unprivileged read access. */
#define PGM_PTATTRS_UR_SHIFT                        36
#define PGM_PTATTRS_UR_MASK                         RT_BIT_64(PGM_PTATTRS_UR_SHIFT)
/** Stage 1:  Unprivileged write access. */
#define PGM_PTATTRS_UW_SHIFT                        37
#define PGM_PTATTRS_UW_MASK                         RT_BIT_64(PGM_PTATTRS_UW_SHIFT)
/** Stage 1:  Unprivileged execute access. */
#define PGM_PTATTRS_UX_SHIFT                        38
#define PGM_PTATTRS_UX_MASK                         RT_BIT_64(PGM_PTATTRS_UX_SHIFT)
/** Stage 1:  Unprivileged guarded control stack (GCS) access. */
#define PGM_PTATTRS_UGCS_SHIFT                      39
#define PGM_PTATTRS_UGCS_MASK                       RT_BIT_64(PGM_PTATTRS_UGCS_SHIFT)
/** Stage 1:  Privileged write-implies-no-execute access.
 * @todo not sure if we need expose this bit.  */
#define PGM_PTATTRS_PWXN_SHIFT                      40
#define PGM_PTATTRS_PWXN_MASK                       RT_BIT_64(PGM_PTATTRS_PWXN_SHIFT)
/** Stage 1:  Unprivileged write-implies-no-execute access.
 * @todo not sure if we need expose this bit. */
#define PGM_PTATTRS_UWXN_SHIFT                      41
#define PGM_PTATTRS_UWXN_MASK                       RT_BIT_64(PGM_PTATTRS_UWXN_SHIFT)
/** Stage 1: Device memory type. */
#define PGM_PTATTRS_DEVICE_SHIFT                    42
#define PGM_PTATTRS_DEVICE_MASK                     RT_BIT_64(PGM_PTATTRS_DEVICE_SHIFT)

/** Page/block level: Guarded page */
#define PGM_PTATTRS_GP_SHIFT                        50
#define PGM_PTATTRS_GP_MASK                         RT_BIT_64(PGM_PTATTRS_GP_SHIFT)
/** Stage 2, page/block: AssuredOnly. (shifted down 7 bits) */
#define PGM_PTATTRS_S2_AO_SHIFT                     51
#define PGM_PTATTRS_S2_AO_MASK                      RT_BIT_64(PGM_PTATTRS_S2_AO_SHIFT)
/** Stage 2, page/block: Alternate MECID (encryption related). (shifted down
 *  7 bits) */
#define PGM_PTATTRS_S2_AMEC_SHIFT                   56
#define PGM_PTATTRS_S2_AMEC_MASK                    RT_BIT_64(PGM_PTATTRS_S2_AMEC_SHIFT)
/** Page/block level: Alternate MECID (encryption related). */
#define PGM_PTATTRS_AMEC_SHIFT                      63
#define PGM_PTATTRS_AMEC_MASK                       RT_BIT_64(PGM_PTATTRS_AMEC_SHIFT)

AssertCompile(PGM_PTATTRS_ND_MASK       == RT_BIT_64(PGM_PTATTRS_ND_SHIFT));
AssertCompile(PGM_PTATTRS_S2_NT_MASK    == RT_BIT_64(PGM_PTATTRS_S2_NT_SHIFT));
AssertCompile(PGM_PTATTRS_S2_R_MASK     == RT_BIT_64(PGM_PTATTRS_S2_R_SHIFT));
AssertCompile(PGM_PTATTRS_S2_W_MASK     == RT_BIT_64(PGM_PTATTRS_S2_W_SHIFT));
AssertCompile(PGM_PTATTRS_S2_PX_MASK    == RT_BIT_64(PGM_PTATTRS_S2_PX_SHIFT));
AssertCompile(PGM_PTATTRS_S2_W_LIM_MASK == RT_BIT_64(PGM_PTATTRS_S2_W_LIM_SHIFT));
AssertCompile(PGM_PTATTRS_S2_AO_MASK    == RT_BIT_64(PGM_PTATTRS_S2_AO_SHIFT));

/** Stage 1 page/block level bits that are copied raw. */
#define PGM_PTATTRS_S1_LEAF_MASK                    (  PGM_PTATTRS_NS_MASK \
                                                     /*| PGM_PTATTRS_NSE_MASK shifted */ \
                                                     | PGM_PTATTRS_AF_MASK \
                                                     | PGM_PTATTRS_NG_MASK \
                                                     | PGM_PTATTRS_NT_MASK \
                                                     | PGM_PTATTRS_GP_MASK \
                                                     | PGM_PTATTRS_AMEC_MASK )

/** Stage 2 page/block level entry shift down count. */
#define PGM_PTATTRS_S2_LEAF_SHIFT                   7
/** Stage 2 page/block level entry mask of shifted down bits copied. */
#define PGM_PTATTRS_S2_LEAF_MASK                    (  PGM_PTATTRS_S2_D_MASK \
                                                     | PGM_PTATTRS_S2_AF_MASK \
                                                     | PGM_PTATTRS_S2_NT_MASK \
                                                     | PGM_PTATTRS_S2_AO_MASK \
                                                     | PGM_PTATTRS_S2_AMEC_MASK )

/** @} */
#endif /* VBOX_VMM_TARGET_ARMV8 || DOXYGEN_RUNNING */



/**
 * Page table walk information.
 *
 * This provides extensive information regarding page faults (or EPT
 * violations/misconfigurations) while traversing page tables.
 */
typedef struct PGMPTWALK
{
    /** The linear address that is being resolved (input). */
    RTGCPTR         GCPtr;

    /** The second-level (/ stage 2) physical address (input/output).
     *  @remarks only valid if fIsSlat is set. */
    RTGCPHYS        GCPhysNested;

    /** The physical address that is the result of the walk (output). */
    RTGCPHYS        GCPhys;

    /** Set if the walk succeeded. */
    bool            fSucceeded;
    /** Whether this is a second-level (/ stage 2) address translation. */
    bool            fIsSlat;
    /** Whether the linear address (GCPtr) caused the second-level
     *  address translation. */
    bool            fIsLinearAddrValid;
    /** The level problem arrised at.
     * @x86     PTE is level 1, PDE is level 2, PDPE is level 3, PML4 is level 4,
     *          CR3 is level 8.  This is 0 on success.
     * @arm64   TBD.
     * @todo    Check if anyone is using this and unify it between the platforms. */
    uint8_t         uLevel;
    /** Set if the page isn't present. */
    bool            fNotPresent;
    /** Encountered a bad physical address. */
    bool            fBadPhysAddr;
    /** Set if there was reserved bit violations. */
    bool            fRsvdError;
    /** Set if it involves a big page (2/4 MB). */
    bool            fBigPage;
    /** Set if it involves a gigantic page (X86: 1 GB; ARM: ). */
    bool            fGigantPage;
    bool            afPadding[3];
    /** Page-walk failure type, PGM_WALKFAIL_XXX. */
    PGMWALKFAIL     fFailed;

    /** The effective page-table attributes, PGM_PTATTRS_XXX. */
    PGMPTATTRS      fEffective;
} PGMPTWALK;
/** Pointer to page walk information. */
typedef PGMPTWALK *PPGMPTWALK;
/** Pointer to const page walk information. */
typedef PGMPTWALK const *PCPGMPTWALK;


/** @name PGM_WALKINFO_XXX - flag based PGM page table walk info.
 * @{ */
/** Set if the walk succeeded. */
#define PGM_WALKINFO_SUCCEEDED                  RT_BIT_32(0)
/** Whether this is a second-level (/ stage 2) address translation. */
#define PGM_WALKINFO_IS_SLAT                    RT_BIT_32(1)

/** Set if it involves a big page.
 * @x86     2MB (PAE+LM), 4MB (legacy).
 * @arm64   Level 2 block - 2MB (gr=4KB v8), 32MB (gr=16KB v8), 512MB (gr=64KB
 *          v8), 1MB (gr=4K v9), 16MB (gr=16KB v9), 256MB (gr=64KB v9). */
#define PGM_WALKINFO_BIG_PAGE                   RT_BIT_32(7)
/** Set if it involves a gigantic page.
 * @x86     1 GB.
 * @arm64   Level 1 block - 1GB (gr=4KB v8), 256MB (gr=4KB v9), 16GB (gr=16KB
 *          v9), 1TB (gr=64KB v9). */
#define PGM_WALKINFO_GIGANTIC_PAGE              RT_BIT_32(8)

/** @todo Add a level 0 block flag for ARM/VMSAv9. */

/** Whether the linear address (GCPtr) caused the second-level
 * address translation - read the code to figure this one.
 * @todo for PGMPTWALKFAST::fFailed?  */
#define PGM_WALKINFO_IS_LINEAR_ADDR_VALID       RT_BIT_32(10)
/** @} */

/**
 * Fast page table walk information.
 *
 * This is a slimmed down version of PGMPTWALK for use by IEM.
 */
typedef struct PGMPTWALKFAST
{
    /** The linear address that is being resolved (input). */
    RTGCPTR         GCPtr;

    /** The physical address that is the result of the walk (output).
     * This includes the offset mask from the GCPtr input value.  */
    RTGCPHYS        GCPhys;

    /** The second-level physical address (input/output).
     *  @remarks only valid if fIsSlat is set. */
    RTGCPHYS        GCPhysNested;

    /** Walk information PGM_WALKINFO_XXX (output). */
    uint32_t        fInfo;
    /** Page-walk failure type, PGM_WALKFAIL_XXX (output). */
    PGMWALKFAIL     fFailed;

    /** The effective page-table attributes, PGM_PTATTRS_XXX (output). */
    PGMPTATTRS      fEffective;
} PGMPTWALKFAST;
/** Pointer to fast page walk information. */
typedef PGMPTWALKFAST *PPGMPTWALKFAST;
/** Pointer to const fast page walk information. */
typedef PGMPTWALKFAST const *PCPGMPTWALKFAST;

#define PGMPTWALKFAST_ZERO(a_pWalkFast) do { \
        (a_pWalkFast)->GCPtr        = 0; \
        (a_pWalkFast)->GCPhys       = 0; \
        (a_pWalkFast)->GCPhysNested = 0; \
        (a_pWalkFast)->fInfo        = 0; \
        (a_pWalkFast)->fFailed      = 0; \
        (a_pWalkFast)->fEffective   = 0; \
    } while (0)


#ifndef VBOX_VMM_TARGET_ARMV8
/** Macro for checking if the guest is using paging.
 * @param enmMode   PGMMODE_*.
 * @remark  ASSUMES certain order of the PGMMODE_* values.
 */
# define PGMMODE_WITH_PAGING(enmMode) ((enmMode) >= PGMMODE_32_BIT)

/** Macro for checking if it's one of the long mode modes.
 * @param enmMode   PGMMODE_*.
 */
# define PGMMODE_IS_64BIT_MODE(enmMode) ((enmMode) == PGMMODE_AMD64_NX || (enmMode) == PGMMODE_AMD64)

/** Macro for checking if it's one of the AMD64 nested modes.
 * @param enmMode   PGMMODE_*.
 */
# define PGMMODE_IS_NESTED(enmMode)  (   (enmMode) == PGMMODE_NESTED_32BIT \
                                      || (enmMode) == PGMMODE_NESTED_PAE \
                                      || (enmMode) == PGMMODE_NESTED_AMD64)

/** Macro for checking if it's one of the PAE modes.
 * @param enmMode   PGMMODE_*.
 */
# define PGMMODE_IS_PAE(enmMode)     (   (enmMode) == PGMMODE_PAE \
                                      || (enmMode) == PGMMODE_PAE_NX)
#else
/** Macro for checking if the guest is using paging.
 * @param enmMode   PGMMODE_*.
 * @remark  ASSUMES certain order of the PGMMODE_* values.
 */
# define PGMMODE_WITH_PAGING(enmMode) ((enmMode) > PGMMODE_NONE)

/** Macro for checking if it's the 64-bit translation mode.
 * @param enmMode   PGMMODE_*.
 */
# define PGMMODE_IS_64BIT_MODE(enmMode) ((enmMode) == PGMMODE_VMSA_V8_64)
#endif


/**
 * Is the ROM mapped (true) or is the shadow RAM mapped (false).
 *
 * @returns boolean.
 * @param   enmProt     The PGMROMPROT value, must be valid.
 */
#define PGMROMPROT_IS_ROM(enmProt) \
    (    (enmProt) == PGMROMPROT_READ_ROM_WRITE_IGNORE \
      || (enmProt) == PGMROMPROT_READ_ROM_WRITE_RAM )


VMMDECL(bool)           PGMIsLockOwner(PVMCC pVM);

VMMDECL(int)            PGMRegisterStringFormatTypes(void);
VMMDECL(void)           PGMDeregisterStringFormatTypes(void);
VMMDECL(RTHCPHYS)       PGMGetHyperCR3(PVMCPU pVCpu);
VMMDECL(int)            PGMTrap0eHandler(PVMCPUCC pVCpu, RTGCUINT uErr, PCPUMCTX pCtx, RTGCPTR pvFault);
VMMDECL(int)            PGMPrefetchPage(PVMCPUCC pVCpu, RTGCPTR GCPtrPage);
VMMDECL(VBOXSTRICTRC)   PGMInterpretInstruction(PVMCPUCC pVCpu, RTGCPTR pvFault);
VMMDECL(int)            PGMShwGetPage(PVMCPUCC pVCpu, RTGCPTR GCPtr, uint64_t *pfFlags, PRTHCPHYS pHCPhys);
VMMDECL(int)            PGMShwMakePageReadonly(PVMCPUCC pVCpu, RTGCPTR GCPtr, uint32_t fFlags);
VMMDECL(int)            PGMShwMakePageWritable(PVMCPUCC pVCpu, RTGCPTR GCPtr, uint32_t fFlags);
VMMDECL(int)            PGMShwMakePageNotPresent(PVMCPUCC pVCpu, RTGCPTR GCPtr, uint32_t fFlags);
/** @name Flags for PGMShwMakePageReadonly, PGMShwMakePageWritable and
 *        PGMShwMakePageNotPresent
 * @{ */
/** The call is from an access handler for dealing with the a faulting write
 * operation.  The virtual address is within the same page. */
#define PGM_MK_PG_IS_WRITE_FAULT     RT_BIT(0)
/** The page is an MMIO2. */
#define PGM_MK_PG_IS_MMIO2           RT_BIT(1)
/** @}*/
VMMDECL(int)        PGMGstGetPage(PVMCPUCC pVCpu, RTGCPTR GCPtr, PPGMPTWALK pWalk);
/** @name PGMQPAGE_F_XXX - Flags for PGMGstQueryPageFast
 * @{ */
/** Querying for read access, set A bits accordingly. */
#define PGMQPAGE_F_READ         RT_BIT_32(0)
/** Querying for write access, set A bits and D bit accordingly.
 * Don't set leaf entry bits if is read-only.  */
#define PGMQPAGE_F_WRITE        RT_BIT_32(1)
/** Querying for execute access, set A bits accordingly. */
#define PGMQPAGE_F_EXECUTE      RT_BIT_32(2)
/** The query is for a user mode access, so don't set leaf A or D bits
 * unless the effective access allows usermode access.
 * Assume supervisor (priveleged) access when not set. */
#define PGMQPAGE_F_USER_MODE    RT_BIT_32(3)
#if defined( VBOX_VMM_TARGET_X86) || defined(DOXYGEN_RUNNING)
/** X86: Treat CR0.WP as zero when evalutating the access.
 * @note Same value as X86_CR0_WP.  */
# define PGMQPAGE_F_CR0_WP0     RT_BIT_32(16)
#endif
/** @todo ARM: security, s2, GCS, ++ */
/** The valid flag mask.   */
#if defined( VBOX_VMM_TARGET_X86) || defined(DOXYGEN_RUNNING)
# define PGMQPAGE_F_VALID_MASK  UINT32_C(0x0001000f)
#else
# define PGMQPAGE_F_VALID_MASK  UINT32_C(0x0000000f)
#endif
/** @} */
VMM_INT_DECL(int)   PGMGstQueryPageFast(PVMCPUCC pVCpu, RTGCPTR GCPtr, uint32_t fFlags, PPGMPTWALKFAST pWalkFast);
VMMDECL(int)        PGMGstModifyPage(PVMCPUCC pVCpu, RTGCPTR GCPtr, size_t cb, uint64_t fFlags, uint64_t fMask);
VMM_INT_DECL(bool)  PGMGstArePaePdpesValid(PVMCPUCC pVCpu, PCX86PDPE paPaePdpes);
VMM_INT_DECL(int)   PGMGstMapPaePdpes(PVMCPUCC pVCpu, PCX86PDPE paPaePdpes);
VMM_INT_DECL(int)   PGMGstMapPaePdpesAtCr3(PVMCPUCC pVCpu, uint64_t cr3);

VMMDECL(int)        PGMInvalidatePage(PVMCPUCC pVCpu, RTGCPTR GCPtrPage);
#if defined(VBOX_VMM_TARGET_X86) || defined(VBOX_VMM_TARGET_AGNOSTIC)
VMM_INT_DECL(int)   PGMFlushTLB(PVMCPUCC pVCpu, uint64_t cr3, bool fGlobal);
VMM_INT_DECL(int)   PGMSyncCR3(PVMCPUCC pVCpu, uint64_t cr0, uint64_t cr3, uint64_t cr4, bool fGlobal);
VMM_INT_DECL(int)   PGMUpdateCR3(PVMCPUCC pVCpu, uint64_t cr3);
VMM_INT_DECL(int)   PGMChangeMode(PVMCPUCC pVCpu, uint64_t cr0, uint64_t cr4, uint64_t efer, bool fForce);
VMM_INT_DECL(int)   PGMHCChangeMode(PVMCC pVM, PVMCPUCC pVCpu, PGMMODE enmGuestMode, bool fForce);
#elif defined(VBOX_VMM_TARGET_ARMV8)
VMM_INT_DECL(int)   PGMChangeMode(PVMCPUCC pVCpu, uint8_t bEl, uint64_t u64RegSctlr, uint64_t u64RegTcr);
#endif
VMMDECL(void)       PGMCr0WpEnabled(PVMCPUCC pVCpu);
VMMDECL(PGMMODE)    PGMGetGuestMode(PVMCPU pVCpu);
VMMDECL(PGMMODE)    PGMGetShadowMode(PVMCPU pVCpu);
VMMDECL(PGMMODE)    PGMGetHostMode(PVM pVM);
VMMDECL(const char *) PGMGetModeName(PGMMODE enmMode);
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
VMM_INT_DECL(const char *) PGMGetSlatModeName(PGMSLAT enmSlatMode);
#endif
VMM_INT_DECL(void)  PGMNotifyNxeChanged(PVMCPU pVCpu, bool fNxe);
VMMDECL(bool)       PGMHasDirtyPages(PVM pVM);
VMM_INT_DECL(void)  PGMSetGuestEptPtr(PVMCPUCC pVCpu, uint64_t uEptPtr);

/** PGM physical access handler type registration handle (heap offset, valid
 * cross contexts without needing fixing up).  Callbacks and handler type is
 * associated with this and it is shared by all handler registrations. */
typedef uint64_t PGMPHYSHANDLERTYPE;
/** Pointer to a PGM physical handler type registration handle. */
typedef PGMPHYSHANDLERTYPE *PPGMPHYSHANDLERTYPE;
/** NIL value for PGM physical access handler type handle. */
#define NIL_PGMPHYSHANDLERTYPE  UINT64_MAX
VMMDECL(int)        PGMHandlerPhysicalRegister(PVMCC pVM, RTGCPHYS GCPhys, RTGCPHYS GCPhysLast, PGMPHYSHANDLERTYPE hType,
                                               uint64_t uUser, R3PTRTYPE(const char *) pszDesc);
VMMDECL(int)        PGMHandlerPhysicalRegisterVmxApicAccessPage(PVMCC pVM, RTGCPHYS GCPhys, PGMPHYSHANDLERTYPE hType);
VMMDECL(int)        PGMHandlerPhysicalModify(PVMCC pVM, RTGCPHYS GCPhysCurrent, RTGCPHYS GCPhys, RTGCPHYS GCPhysLast);
VMMDECL(int)        PGMHandlerPhysicalDeregister(PVMCC pVM, RTGCPHYS GCPhys);
VMMDECL(int)        PGMHandlerPhysicalChangeUserArg(PVMCC pVM, RTGCPHYS GCPhys, uint64_t uUser);
VMMDECL(int)        PGMHandlerPhysicalSplit(PVMCC pVM, RTGCPHYS GCPhys, RTGCPHYS GCPhysSplit);
VMMDECL(int)        PGMHandlerPhysicalJoin(PVMCC pVM, RTGCPHYS GCPhys1, RTGCPHYS GCPhys2);
VMMDECL(int)        PGMHandlerPhysicalPageTempOff(PVMCC pVM, RTGCPHYS GCPhys, RTGCPHYS GCPhysPage);
VMMDECL(int)        PGMHandlerPhysicalPageAliasMmio2(PVMCC pVM, RTGCPHYS GCPhys, RTGCPHYS GCPhysPage,
                                                     PPDMDEVINS pDevIns, PGMMMIO2HANDLE hMmio2, RTGCPHYS offMMio2PageRemap);
VMMDECL(int)        PGMHandlerPhysicalPageAliasHC(PVMCC pVM, RTGCPHYS GCPhys, RTGCPHYS GCPhysPage, RTHCPHYS HCPhysPageRemap);
VMMDECL(int)        PGMHandlerPhysicalReset(PVMCC pVM, RTGCPHYS GCPhys);
VMMDECL(bool)       PGMHandlerPhysicalIsRegistered(PVMCC pVM, RTGCPHYS GCPhys);

/** @name PGMPHYSHANDLER_F_XXX - flags for PGMR3HandlerPhysicalTypeRegister and PGMR0HandlerPhysicalTypeRegister
 * @{ */
/** Whether to hold the PGM lock while calling the handler or not.
 *  Mainly an optimization for PGM callers. */
#define PGMPHYSHANDLER_F_KEEP_PGM_LOCK  RT_BIT_32(0)
/** The uUser value is a ring-0 device instance index that needs translating
 * into a PDMDEVINS pointer before calling the handler.  This is a hack to make
 * it possible to use access handlers in devices. */
#define PGMPHYSHANDLER_F_R0_DEVINS_IDX  RT_BIT_32(1)
/** Don't apply the access handler to VT-x and AMD-V. Only works with full pages.
 * This is a trick for the VT-x APIC access page in nested VT-x setups. */
#define PGMPHYSHANDLER_F_NOT_IN_HM      RT_BIT_32(2)
/** Mask of valid bits.   */
#define PGMPHYSHANDLER_F_VALID_MASK     UINT32_C(7)
/** @} */


/**
 * Page type.
 *
 * @remarks This enum has to fit in a 3-bit field (see PGMPAGE::u3Type).
 * @remarks This is used in the saved state, so changes to it requires bumping
 *          the saved state version.
 * @todo    So, convert to \#defines!
 */
typedef enum PGMPAGETYPE
{
    /** The usual invalid zero entry. */
    PGMPAGETYPE_INVALID = 0,
    /** RAM page. (RWX) */
    PGMPAGETYPE_RAM,
    /** MMIO2 page. (RWX) */
    PGMPAGETYPE_MMIO2,
    /** MMIO2 page aliased over an MMIO page. (RWX)
     * See PGMHandlerPhysicalPageAlias(). */
    PGMPAGETYPE_MMIO2_ALIAS_MMIO,
    /** Special page aliased over an MMIO page. (RWX)
     * See PGMHandlerPhysicalPageAliasHC(), but this is generally only used for
     * VT-x's APIC access page at the moment.  Treated as MMIO by everyone except
     * the shadow paging code. */
    PGMPAGETYPE_SPECIAL_ALIAS_MMIO,
    /** Shadowed ROM. (RWX) */
    PGMPAGETYPE_ROM_SHADOW,
    /** ROM page. (R-X) */
    PGMPAGETYPE_ROM,
    /** MMIO page. (---) */
    PGMPAGETYPE_MMIO,
    /** End of valid entries. */
    PGMPAGETYPE_END
} PGMPAGETYPE;
AssertCompile(PGMPAGETYPE_END == 8);

/** @name PGM page type predicates.
 * @{ */
#define PGMPAGETYPE_IS_READABLE(a_enmType)  ( (a_enmType) <= PGMPAGETYPE_ROM )
#define PGMPAGETYPE_IS_WRITEABLE(a_enmType) ( (a_enmType) <= PGMPAGETYPE_ROM_SHADOW )
#define PGMPAGETYPE_IS_RWX(a_enmType)       ( (a_enmType) <= PGMPAGETYPE_ROM_SHADOW )
#define PGMPAGETYPE_IS_ROX(a_enmType)       ( (a_enmType) == PGMPAGETYPE_ROM )
#define PGMPAGETYPE_IS_NP(a_enmType)        ( (a_enmType) == PGMPAGETYPE_MMIO )
/** @} */

/**
 * A physical memory range.
 *
 * @note This layout adheres to to GIM Hyper-V specs (asserted while compiling
 * GIM Hyper-V that uses the PGM API).
 */
typedef struct PGMPHYSRANGE
{
    /** The first address in the range. */
    RTGCPHYS        GCPhysStart;
    /** The number of pages in the range. */
    uint64_t        cPages;
} PGMPHYSRANGE;
AssertCompileSize(PGMPHYSRANGE, 16);

/**
 * A list of physical memory ranges.
 *
 * @note This layout adheres to to GIM Hyper-V specs (asserted while compiling
 * GIM Hyper-V that uses the PGM API).
 */
typedef struct PGMPHYSRANGES
{
    /** The number of ranges in the list. */
    uint64_t        cRanges;
    /** Array of physical memory ranges. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    PGMPHYSRANGE    aRanges[RT_FLEXIBLE_ARRAY];
} PGMPHYSRANGES;
/** Pointer to a list of physical memory ranges. */
typedef PGMPHYSRANGES *PPGMPHYSRANGES;
/** Pointer to a const list of physical memory ranges. */
typedef PGMPHYSRANGES const *PCPGMPHYSRANGES;


VMM_INT_DECL(PGMPAGETYPE) PGMPhysGetPageType(PVMCC pVM, RTGCPHYS GCPhys);

VMM_INT_DECL(int)   PGMPhysGCPhys2HCPhys(PVMCC pVM, RTGCPHYS GCPhys, PRTHCPHYS pHCPhys);
VMM_INT_DECL(int)   PGMPhysGCPtr2HCPhys(PVMCPUCC pVCpu, RTGCPTR GCPtr, PRTHCPHYS pHCPhys);
VMM_INT_DECL(int)   PGMPhysGCPhys2CCPtr(PVMCC pVM, RTGCPHYS GCPhys, void **ppv, PPGMPAGEMAPLOCK pLock);
VMM_INT_DECL(int)   PGMPhysGCPhys2CCPtrReadOnly(PVMCC pVM, RTGCPHYS GCPhys, void const **ppv, PPGMPAGEMAPLOCK pLock);
VMM_INT_DECL(int)   PGMPhysGCPtr2CCPtr(PVMCPUCC pVCpu, RTGCPTR GCPtr, void **ppv, PPGMPAGEMAPLOCK pLock);
VMM_INT_DECL(int)   PGMPhysGCPtr2CCPtrReadOnly(PVMCPUCC pVCpu, RTGCPTR GCPtr, void const **ppv, PPGMPAGEMAPLOCK pLock);

VMMDECL(bool)       PGMPhysIsA20Enabled(PVMCPU pVCpu);
VMMDECL(bool)       PGMPhysIsGCPhysValid(PVMCC pVM, RTGCPHYS GCPhys);
VMMDECL(bool)       PGMPhysIsGCPhysNormal(PVMCC pVM, RTGCPHYS GCPhys);
VMMDECL(int)        PGMPhysGCPtr2GCPhys(PVMCPUCC pVCpu, RTGCPTR GCPtr, PRTGCPHYS pGCPhys);
VMMDECL(void)       PGMPhysReleasePageMappingLock(PVMCC pVM, PPGMPAGEMAPLOCK pLock);
VMMDECL(void)       PGMPhysBulkReleasePageMappingLocks(PVMCC pVM, uint32_t cPages, PPGMPAGEMAPLOCK paLock);

/** @def PGM_PHYS_RW_IS_SUCCESS
 * Check whether a PGMPhysRead, PGMPhysWrite, PGMPhysReadGCPtr or
 * PGMPhysWriteGCPtr call completed the given task.
 *
 * @returns true if completed, false if not.
 * @param   a_rcStrict      The status code.
 * @sa      IOM_SUCCESS
 */
#ifdef IN_RING3
# define PGM_PHYS_RW_IS_SUCCESS(a_rcStrict) \
    (   (a_rcStrict) == VINF_SUCCESS \
     || (a_rcStrict) == VINF_EM_DBG_STOP \
     || (a_rcStrict) == VINF_EM_DBG_EVENT \
     || (a_rcStrict) == VINF_EM_DBG_BREAKPOINT \
    )
#elif defined(IN_RING0)
# define PGM_PHYS_RW_IS_SUCCESS(a_rcStrict) \
    (   (a_rcStrict) == VINF_SUCCESS \
     || (a_rcStrict) == VINF_IOM_R3_MMIO_COMMIT_WRITE \
     || (a_rcStrict) == VINF_EM_OFF \
     || (a_rcStrict) == VINF_EM_SUSPEND \
     || (a_rcStrict) == VINF_EM_RESET \
     || (a_rcStrict) == VINF_EM_HALT \
     || (a_rcStrict) == VINF_EM_DBG_STOP \
     || (a_rcStrict) == VINF_EM_DBG_EVENT \
     || (a_rcStrict) == VINF_EM_DBG_BREAKPOINT \
    )
#elif defined(IN_RC)
# define PGM_PHYS_RW_IS_SUCCESS(a_rcStrict) \
    (   (a_rcStrict) == VINF_SUCCESS \
     || (a_rcStrict) == VINF_IOM_R3_MMIO_COMMIT_WRITE \
     || (a_rcStrict) == VINF_EM_OFF \
     || (a_rcStrict) == VINF_EM_SUSPEND \
     || (a_rcStrict) == VINF_EM_RESET \
     || (a_rcStrict) == VINF_EM_HALT \
     || (a_rcStrict) == VINF_SELM_SYNC_GDT \
     || (a_rcStrict) == VINF_EM_RAW_EMULATE_INSTR_GDT_FAULT \
     || (a_rcStrict) == VINF_EM_DBG_STOP \
     || (a_rcStrict) == VINF_EM_DBG_EVENT \
     || (a_rcStrict) == VINF_EM_DBG_BREAKPOINT \
    )
#endif
/** @def PGM_PHYS_RW_DO_UPDATE_STRICT_RC
 * Updates the return code with a new result.
 *
 * Both status codes must be successes according to PGM_PHYS_RW_IS_SUCCESS.
 *
 * @param   a_rcStrict      The current return code, to be updated.
 * @param   a_rcStrict2     The new return code to merge in.
 */
#ifdef IN_RING3
# define PGM_PHYS_RW_DO_UPDATE_STRICT_RC(a_rcStrict, a_rcStrict2) \
    do { \
        Assert(rcStrict == VINF_SUCCESS); \
        Assert(rcStrict2 == VINF_SUCCESS); \
    } while (0)
#elif defined(IN_RING0)
# define PGM_PHYS_RW_DO_UPDATE_STRICT_RC(a_rcStrict, a_rcStrict2) \
    do { \
        Assert(PGM_PHYS_RW_IS_SUCCESS(rcStrict)); \
        Assert(PGM_PHYS_RW_IS_SUCCESS(rcStrict2)); \
        AssertCompile(VINF_IOM_R3_MMIO_COMMIT_WRITE > VINF_EM_LAST); \
        if ((a_rcStrict2) == VINF_SUCCESS || (a_rcStrict) == (a_rcStrict2)) \
        { /* likely */ } \
        else if (   (a_rcStrict) == VINF_SUCCESS \
                 || (a_rcStrict) > (a_rcStrict2)) \
            (a_rcStrict) = (a_rcStrict2); \
    } while (0)
#elif defined(IN_RC)
# define PGM_PHYS_RW_DO_UPDATE_STRICT_RC(a_rcStrict, a_rcStrict2) \
    do { \
        Assert(PGM_PHYS_RW_IS_SUCCESS(rcStrict)); \
        Assert(PGM_PHYS_RW_IS_SUCCESS(rcStrict2)); \
        AssertCompile(VINF_SELM_SYNC_GDT > VINF_EM_LAST); \
        AssertCompile(VINF_EM_RAW_EMULATE_INSTR_GDT_FAULT > VINF_EM_LAST); \
        AssertCompile(VINF_EM_RAW_EMULATE_INSTR_GDT_FAULT < VINF_SELM_SYNC_GDT); \
        AssertCompile(VINF_IOM_R3_MMIO_COMMIT_WRITE > VINF_EM_LAST); \
        AssertCompile(VINF_IOM_R3_MMIO_COMMIT_WRITE > VINF_SELM_SYNC_GDT); \
        AssertCompile(VINF_IOM_R3_MMIO_COMMIT_WRITE > VINF_EM_RAW_EMULATE_INSTR_GDT_FAULT); \
        if ((a_rcStrict2) == VINF_SUCCESS || (a_rcStrict) == (a_rcStrict2)) \
        { /* likely */ } \
        else if ((a_rcStrict) == VINF_SUCCESS) \
            (a_rcStrict) = (a_rcStrict2); \
        else if (   (   (a_rcStrict) > (a_rcStrict2) \
                     && (   (a_rcStrict2) <= VINF_EM_RESET  \
                         || (a_rcStrict) != VINF_EM_RAW_EMULATE_INSTR_GDT_FAULT) ) \
                 || (   (a_rcStrict2) == VINF_EM_RAW_EMULATE_INSTR_GDT_FAULT \
                     && (a_rcStrict) > VINF_EM_RESET) ) \
            (a_rcStrict) = (a_rcStrict2); \
    } while (0)
#endif

VMMDECL(VBOXSTRICTRC) PGMPhysRead(PVMCC pVM, RTGCPHYS GCPhys, void *pvBuf, size_t cbRead, PGMACCESSORIGIN enmOrigin);
VMMDECL(VBOXSTRICTRC) PGMPhysWrite(PVMCC pVM, RTGCPHYS GCPhys, const void *pvBuf, size_t cbWrite, PGMACCESSORIGIN enmOrigin);
VMMDECL(VBOXSTRICTRC) PGMPhysReadGCPtr(PVMCPUCC pVCpu, void *pvDst, RTGCPTR GCPtrSrc, size_t cb, PGMACCESSORIGIN enmOrigin);
VMMDECL(VBOXSTRICTRC) PGMPhysWriteGCPtr(PVMCPUCC pVCpu, RTGCPTR GCPtrDst, const void *pvSrc, size_t cb, PGMACCESSORIGIN enmOrigin);

VMMDECL(int)        PGMPhysSimpleReadGCPhys(PVMCC pVM, void *pvDst, RTGCPHYS GCPhysSrc, size_t cb);
VMMDECL(int)        PGMPhysSimpleWriteGCPhys(PVMCC pVM, RTGCPHYS GCPhysDst, const void *pvSrc, size_t cb);
VMMDECL(int)        PGMPhysSimpleReadGCPtr(PVMCPUCC pVCpu, void *pvDst, RTGCPTR GCPtrSrc, size_t cb);
VMMDECL(int)        PGMPhysSimpleWriteGCPtr(PVMCPUCC pVCpu, RTGCPTR GCPtrDst, const void *pvSrc, size_t cb);
VMMDECL(int)        PGMPhysSimpleDirtyWriteGCPtr(PVMCPUCC pVCpu, RTGCPTR GCPtrDst, const void *pvSrc, size_t cb);

VMM_INT_DECL(int)   PGMPhysIemGCPhys2Ptr(PVMCC pVM, PVMCPUCC pVCpu, RTGCPHYS GCPhys, bool fWritable, bool fByPassHandlers, void **ppv, PPGMPAGEMAPLOCK pLock);
VMM_INT_DECL(int)   PGMPhysIemQueryAccess(PVMCC pVM, RTGCPHYS GCPhys, bool fWritable, bool fByPassHandlers);
VMM_INT_DECL(int)   PGMPhysIemGCPhys2PtrNoLock(PVMCC pVM, PVMCPUCC pVCpu, RTGCPHYS GCPhys, uint64_t const volatile *puTlbPhysRev,
                                               R3R0PTRTYPE(uint8_t *) *ppb, uint64_t *pfTlb);
/** @name Flags returned by PGMPhysIemGCPhys2PtrNoLock
 * @{ */
/** @def PGMIEMGCPHYS2PTR_F_NO_WRITE
 * Not writable (IEMTLBE_F_PG_NO_WRITE). */
/** @def PGMIEMGCPHYS2PTR_F_NO_READ
 * Not readable (IEMTLBE_F_PG_NO_READ). */
/** @def PGMIEMGCPHYS2PTR_F_NO_MAPPINGR3
 * No ring-3 mapping (IEMTLBE_F_NO_MAPPINGR3). */
/** @def PGMIEMGCPHYS2PTR_F_UNASSIGNED
 * Unassigned memory (IEMTLBE_F_PG_UNASSIGNED). */
/** @def PGMIEMGCPHYS2PTR_F_CODE_PAGE
 * Write monitored IEM code page (IEMTLBE_F_PG_CODE_PAGE). */
#if defined(VBOX_VMM_TARGET_X86) || defined(DOXYGEN_RUNNING)
# define PGMIEMGCPHYS2PTR_F_NO_WRITE     RT_BIT_32(3)
# define PGMIEMGCPHYS2PTR_F_NO_READ      RT_BIT_32(4)
# define PGMIEMGCPHYS2PTR_F_NO_MAPPINGR3 RT_BIT_32(8)
# define PGMIEMGCPHYS2PTR_F_UNASSIGNED   RT_BIT_32(9)
# define PGMIEMGCPHYS2PTR_F_CODE_PAGE    RT_BIT_32(10)
#elif defined(VBOX_VMM_TARGET_ARMV8)
# define PGMIEMGCPHYS2PTR_F_NO_READ      RT_BIT_32(15)
# define PGMIEMGCPHYS2PTR_F_NO_WRITE     RT_BIT_32(16)
# define PGMIEMGCPHYS2PTR_F_NO_MAPPINGR3 RT_BIT_32(17)
# define PGMIEMGCPHYS2PTR_F_UNASSIGNED   RT_BIT_32(18)
# define PGMIEMGCPHYS2PTR_F_CODE_PAGE    RT_BIT_32(19)
#endif
/** @} */

/** Information returned by PGMPhysNemQueryPageInfo. */
typedef struct PGMPHYSNEMPAGEINFO
{
    /** The host physical address of the page, NIL_HCPHYS if invalid page. */
    RTHCPHYS            HCPhys;
    /** The NEM access mode for the page, NEM_PAGE_PROT_XXX  */
    uint32_t            fNemProt : 8;
    /** The NEM state associated with the PAGE. */
    uint32_t            u2NemState : 2;
    /** The NEM state associated with the PAGE before pgmPhysPageMakeWritable was called. */
    uint32_t            u2OldNemState : 2;
    /** Set if the page has handler. */
    uint32_t            fHasHandlers : 1;
    /** Set if is the zero page backing it. */
    uint32_t            fZeroPage : 1;
    /** Set if the page has handler. */
    PGMPAGETYPE         enmType;
} PGMPHYSNEMPAGEINFO;
/** Pointer to page information for NEM. */
typedef PGMPHYSNEMPAGEINFO *PPGMPHYSNEMPAGEINFO;
/**
 * Callback for checking that the page is in sync while under the PGM lock.
 *
 * NEM passes this callback to PGMPhysNemQueryPageInfo to check that the page is
 * in-sync between PGM and the native hypervisor API in an atomic fashion.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context per virtual CPU structure.  Optional,
 *                      see PGMPhysNemQueryPageInfo.
 * @param   GCPhys      The guest physical address (not A20 masked).
 * @param   pInfo       The page info structure.  This function updates the
 *                      u2NemState memory and the caller will update the PGMPAGE
 *                      copy accordingly.
 * @param   pvUser      Callback user argument.
 */
typedef DECLCALLBACKTYPE(int, FNPGMPHYSNEMCHECKPAGE,(PVMCC pVM, PVMCPUCC pVCpu, RTGCPHYS GCPhys, PPGMPHYSNEMPAGEINFO pInfo, void *pvUser));
/** Pointer to a FNPGMPHYSNEMCHECKPAGE function. */
typedef FNPGMPHYSNEMCHECKPAGE *PFNPGMPHYSNEMCHECKPAGE;

VMM_INT_DECL(int)   PGMPhysNemPageInfoChecker(PVMCC pVM, PVMCPUCC pVCpu, RTGCPHYS GCPhys, bool fMakeWritable,
                                              PPGMPHYSNEMPAGEINFO pInfo, PFNPGMPHYSNEMCHECKPAGE pfnChecker, void *pvUser);

/**
 * Callback for use with PGMPhysNemEnumPagesByState.
 * @returns VBox status code.
 *          Failure status will stop enumeration immediately and return.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context per virtual CPU structure.  Optional,
 *                      see PGMPhysNemEnumPagesByState.
 * @param   GCPhys      The guest physical address (not A20 masked).
 * @param   pu2NemState Pointer to variable with the NEM state.  This can be
 *                      update.
 * @param   pvUser      The user argument.
 */
typedef DECLCALLBACKTYPE(int, FNPGMPHYSNEMENUMCALLBACK,(PVMCC pVM, PVMCPUCC pVCpu, RTGCPHYS GCPhys,
                                                        uint8_t *pu2NemState, void *pvUser));
/** Pointer to a FNPGMPHYSNEMENUMCALLBACK function. */
typedef FNPGMPHYSNEMENUMCALLBACK *PFNPGMPHYSNEMENUMCALLBACK;
VMM_INT_DECL(int) PGMPhysNemEnumPagesByState(PVMCC pVM, PVMCPUCC VCpu, uint8_t uMinState,
                                             PFNPGMPHYSNEMENUMCALLBACK pfnCallback, void *pvUser);


#ifdef VBOX_STRICT
VMMDECL(unsigned)   PGMAssertHandlerAndFlagsInSync(PVMCC pVM);
VMMDECL(unsigned)   PGMAssertNoMappingConflicts(PVM pVM);
VMMDECL(unsigned)   PGMAssertCR3(PVMCC pVM, PVMCPUCC pVCpu, uint64_t cr3, uint64_t cr4);
#endif /* VBOX_STRICT */

VMMDECL(int)        PGMSetLargePageUsage(PVMCC pVM, bool fUseLargePages);

/**
 * Query large page usage state
 *
 * @returns 0 - disabled, 1 - enabled
 * @param   pVM         The cross context VM structure.
 */
#define PGMIsUsingLargePages(pVM)   ((pVM)->pgm.s.fUseLargePages)


/** @defgroup grp_pgm_r0  The PGM Host Context Ring-0 API
 * @{
 */
#ifdef IN_RING0
VMMR0_INT_DECL(int)  PGMR0InitPerVMData(PGVM pGVM, RTR0MEMOBJ hMemObj);
VMMR0_INT_DECL(int)  PGMR0InitVM(PGVM pGVM);
VMMR0_INT_DECL(void) PGMR0DoneInitVM(PGVM pGVM);
VMMR0_INT_DECL(void) PGMR0CleanupVM(PGVM pGVM);
VMMR0_INT_DECL(int)  PGMR0PhysAllocateHandyPages(PGVM pGVM, VMCPUID idCpu);
VMMR0_INT_DECL(int)  PGMR0PhysFlushHandyPages(PGVM pGVM, VMCPUID idCpu);
VMMR0_INT_DECL(int)  PGMR0PhysAllocateLargePage(PGVM pGVM, VMCPUID idCpu, RTGCPHYS GCPhys);
VMMR0_INT_DECL(int)  PGMR0PhysMMIO2MapKernel(PGVM pGVM, PPDMDEVINS pDevIns, PGMMMIO2HANDLE hMmio2,
                                             size_t offSub, size_t cbSub, void **ppvMapping);
VMMR0_INT_DECL(int)  PGMR0PhysSetupIoMmu(PGVM pGVM);
VMMR0_INT_DECL(int)  PGMR0PhysHandlerInitReqHandler(PGVM pGVM, uint32_t cEntries);

VMMR0_INT_DECL(int)  PGMR0HandlerPhysicalTypeSetUpContext(PGVM pGVM, PGMPHYSHANDLERKIND enmKind, uint32_t fFlags,
                                                          PFNPGMPHYSHANDLER pfnHandler, PFNPGMRZPHYSPFHANDLER pfnPfHandler,
                                                          const char *pszDesc, PGMPHYSHANDLERTYPE hType);

VMMR0DECL(int)       PGMR0SharedModuleCheck(PVMCC pVM, PGVM pGVM, VMCPUID idCpu, PGMMSHAREDMODULE pModule,
                                            PCRTGCPTR64 paRegionsGCPtrs);
VMMR0DECL(int)       PGMR0Trap0eHandlerNestedPaging(PGVM pGVM, PGVMCPU pGVCpu, PGMMODE enmShwPagingMode, RTGCUINT uErr,
                                                    PCPUMCTX pCtx, RTGCPHYS pvFault);
VMMR0DECL(VBOXSTRICTRC) PGMR0Trap0eHandlerNPMisconfig(PGVM pGVM, PGVMCPU pGVCpu, PGMMODE enmShwPagingMode,
                                                      PCPUMCTX pCtx, RTGCPHYS GCPhysFault, uint32_t uErr);
VMMR0_INT_DECL(int)  PGMR0PoolGrow(PGVM pGVM, VMCPUID idCpu);

# ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
VMMR0DECL(VBOXSTRICTRC) PGMR0NestedTrap0eHandlerNestedPaging(PGVMCPU pGVCpu, PGMMODE enmShwPagingMode, RTGCUINT uErr,
                                                             PCPUMCTX pCtx, RTGCPHYS GCPhysNestedFault,
                                                             bool fIsLinearAddrValid, RTGCPTR GCPtrNestedFault, PPGMPTWALK pWalk);
# endif
#endif /* IN_RING0 */

/**
 * Request buffer for PGMR0PhysAllocateRamRangeReq / VMMR0_DO_PGM_PHYS_ALLOCATE_RAM_RANGE
 */
typedef struct PGMPHYSALLOCATERAMRANGEREQ
{
    /** The header. */
    SUPVMMR0REQHDR  Hdr;
    /** Input: the GUEST_PAGE_SIZE value (for validation). */
    uint32_t        cbGuestPage;
    /** Input: Number of guest pages in the range. */
    uint32_t        cGuestPages;
    /** Input: The RAM range flags (PGM_RAM_RANGE_FLAGS_XXX). */
    uint32_t        fFlags;
    /** Output: The range identifier. */
    uint32_t        idNewRange;
} PGMPHYSALLOCATERAMRANGEREQ;
/** Pointer to a PGMR0PhysAllocateRamRangeReq / VMMR0_DO_PGM_PHYS_ALLOCATE_RAM_RANGE request buffer. */
typedef PGMPHYSALLOCATERAMRANGEREQ *PPGMPHYSALLOCATERAMRANGEREQ;

VMMR0_INT_DECL(int)  PGMR0PhysAllocateRamRangeReq(PGVM pGVM, PPGMPHYSALLOCATERAMRANGEREQ pReq);


/**
 * Request buffer for PGMR0PhysMmio2RegisterReq / VMMR0_DO_PGM_PHYS_MMIO2_REGISTER
 */
typedef struct PGMPHYSMMIO2REGISTERREQ
{
    /** The header. */
    SUPVMMR0REQHDR          Hdr;
    /** Input: the GUEST_PAGE_SIZE value (for validation). */
    uint32_t                cbGuestPage;
    /** Input: Number of guest pages in the MMIO2 range. */
    uint32_t                cGuestPages;
    /** Input: The MMIO2 ID of the first chunk. */
    uint8_t                 idMmio2;
    /** Input: The number of MMIO2 chunks needed. */
    uint8_t                 cChunks;
    /** Input: The sub-device number. */
    uint8_t                 iSubDev;
    /** Input: The device region number. */
    uint8_t                 iRegion;
    /** Input: Flags (PGMPHYS_MMIO2_FLAGS_XXX). */
    uint32_t                fFlags;
    /** Input: The owner device key. */
    PPDMDEVINSR3            pDevIns;
    /** Input: An existing ring-3 userspace pointer to use for the backing,
     *         Only valid with PGMPHYS_MMIO2_FLAGS_USE_EXISTING_BACKING - must be zeroed otherwise. */
    R3PTRTYPE(uint8_t *)    pbR3;
} PGMPHYSMMIO2REGISTERREQ;
/** Pointer to a PGMR0PhysAllocateRamRangeReq / VMMR0_DO_PGM_PHYS_MMIO2_REGISTER request buffer. */
typedef PGMPHYSMMIO2REGISTERREQ *PPGMPHYSMMIO2REGISTERREQ;

VMMR0_INT_DECL(int)  PGMR0PhysMmio2RegisterReq(PGVM pGVM, PPGMPHYSMMIO2REGISTERREQ pReq);


/*
 * Request buffer for PGMR0PhysMmio2DeregisterReq / VMMR0_DO_PGM_PHYS_MMIO2_DEREGISTER
 */
typedef struct PGMPHYSMMIO2DEREGISTERREQ
{
    /** The header. */
    SUPVMMR0REQHDR          Hdr;
    /** Input: The MMIO2 ID of the first chunk. */
    uint8_t                 idMmio2;
    /** Input: The number of MMIO2 chunks to free. */
    uint8_t                 cChunks;
    /** Input: Reserved and must be zero. */
    uint8_t                 abReserved[6];
    /** Input: The owner device key. */
    PPDMDEVINSR3            pDevIns;
} PGMPHYSMMIO2DEREGISTERREQ;
/** Pointer to a PGMR0PhysMmio2DeregisterReq / VMMR0_DO_PGM_PHYS_MMIO2_DEREGISTER request buffer. */
typedef PGMPHYSMMIO2DEREGISTERREQ *PPGMPHYSMMIO2DEREGISTERREQ;

VMMR0_INT_DECL(int)  PGMR0PhysMmio2DeregisterReq(PGVM pGVM, PPGMPHYSMMIO2DEREGISTERREQ pReq);

/*
 * Request buffer for PGMR0PhysRomAllocateRangeReq / VMMR0_DO_PGM_PHYS_ROM_ALLOCATE_RANGE
 */
typedef struct PGMPHYSROMALLOCATERANGEREQ
{
    /** The header. */
    SUPVMMR0REQHDR  Hdr;
    /** Input: the GUEST_PAGE_SIZE value (for validation). */
    uint32_t        cbGuestPage;
    /** Input: Number of guest pages in the range. */
    uint32_t        cGuestPages;
    /** Input: The ROM range ID (index) to be allocated. */
    uint32_t        idRomRange;
    /** Input: The ROM range flags (PGMPHYS_ROM_FLAGS_XXX). */
    uint32_t        fFlags;
} PGMPHYSROMALLOCATERANGEREQ;
/* Pointer to a PGMR0PhysRomAllocateRangeReq / VMMR0_DO_PGM_PHYS_ROM_ALLOCATE_RANGE request buffer. */
typedef PGMPHYSROMALLOCATERANGEREQ *PPGMPHYSROMALLOCATERANGEREQ;

VMMR0_INT_DECL(int)  PGMR0PhysRomAllocateRangeReq(PGVM pGVM, PPGMPHYSROMALLOCATERANGEREQ pReq);


/** @} */



/** @defgroup grp_pgm_r3  The PGM Host Context Ring-3 API
 * @{
 */
#ifdef IN_RING3
VMMR3_INT_DECL(void)    PGMR3EnableNemMode(PVM pVM);
VMMR3_INT_DECL(bool)    PGMR3IsNemModeEnabled(PVM pVM);
VMMR3DECL(int)      PGMR3Init(PVM pVM);
VMMR3DECL(int)      PGMR3InitFinalize(PVM pVM);
VMMR3_INT_DECL(int) PGMR3InitCompleted(PVM pVM, VMINITCOMPLETED enmWhat);
VMMR3DECL(void)     PGMR3Relocate(PVM pVM, RTGCINTPTR offDelta);
VMMR3DECL(void)     PGMR3ResetCpu(PVM pVM, PVMCPU pVCpu);
VMMR3_INT_DECL(void)    PGMR3Reset(PVM pVM);
VMMR3_INT_DECL(void)    PGMR3ResetNoMorePhysWritesFlag(PVM pVM);
VMMR3_INT_DECL(void)    PGMR3MemSetup(PVM pVM, bool fReset);
VMMR3DECL(int)      PGMR3Term(PVM pVM);

VMMR3DECL(int)      PGMR3PhysRegisterRam(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, const char *pszDesc);
VMMR3DECL(int)      PGMR3PhysChangeMemBalloon(PVM pVM, bool fInflate, unsigned cPages, RTGCPHYS *paPhysPage);
VMMR3DECL(int)      PGMR3PhysWriteProtectRAM(PVM pVM);
VMMR3DECL(uint32_t) PGMR3PhysGetRamRangeCount(PVM pVM);
VMMR3DECL(int)      PGMR3PhysGetRange(PVM pVM, uint32_t iRange, PRTGCPHYS pGCPhysStart, PRTGCPHYS pGCPhysLast,
                                      const char **ppszDesc, bool *pfIsMmio);
VMMR3_INT_DECL(int) PGMR3PhysGetRamBootZeroedRanges(PVM pVM, PPGMPHYSRANGES pRanges, uint32_t cMaxRanges);
VMMR3DECL(int)      PGMR3QueryMemoryStats(PUVM pUVM, uint64_t *pcbTotalMem, uint64_t *pcbPrivateMem, uint64_t *pcbSharedMem, uint64_t *pcbZeroMem);
VMMR3DECL(int)      PGMR3QueryGlobalMemoryStats(PUVM pUVM, uint64_t *pcbAllocMem, uint64_t *pcbFreeMem, uint64_t *pcbBallonedMem, uint64_t *pcbSharedMem);

VMMR3_INT_DECL(int) PGMR3PhysMmioRegister(PVM pVM, PVMCPU pVCpu, RTGCPHYS cb, const char *pszDesc, uint16_t *pidRamRange);
VMMR3_INT_DECL(int) PGMR3PhysMmioMap(PVM pVM, PVMCPU pVCpu, RTGCPHYS GCPhys, RTGCPHYS cb, uint16_t idRamRange,
                                     PGMPHYSHANDLERTYPE hType, uint64_t uUser);
VMMR3_INT_DECL(int) PGMR3PhysMmioUnmap(PVM pVM, PVMCPU pVCpu, RTGCPHYS GCPhys, RTGCPHYS cb, uint16_t idRamRange);
#endif /* IN_RING3 */

/** @name PGMPHYS_MMIO2_FLAGS_XXX - MMIO2 registration flags.
 * @see PGMR3PhysMmio2Register, PDMDevHlpMmio2Create
 * @{ */
/** Track dirty pages.
 * @see PGMR3PhysMmio2QueryAndResetDirtyBitmap(), PGMR3PhysMmio2ControlDirtyPageTracking(). */
#define PGMPHYS_MMIO2_FLAGS_TRACK_DIRTY_PAGES       RT_BIT_32(0)
/** Use the pointer supplied in ppv as the backing storage. */
#define PGMPHYS_MMIO2_FLAGS_USE_EXISTING_BACKING    RT_BIT_32(1)
/** Valid flags. */
#define PGMPHYS_MMIO2_FLAGS_VALID_MASK              UINT32_C(0x00000003)
/** @} */

#ifdef IN_RING3
VMMR3_INT_DECL(int) PGMR3PhysMmio2Register(PVM pVM, PPDMDEVINS pDevIns, uint32_t iSubDev, uint32_t iRegion, RTGCPHYS cb,
                                           uint32_t fFlags, const char *pszDesc, void **ppv, PGMMMIO2HANDLE *phRegion);
VMMR3_INT_DECL(int) PGMR3PhysMmio2Deregister(PVM pVM, PPDMDEVINS pDevIns, PGMMMIO2HANDLE hMmio2);
VMMR3_INT_DECL(int) PGMR3PhysMmio2Map(PVM pVM, PPDMDEVINS pDevIns, PGMMMIO2HANDLE hMmio2, RTGCPHYS GCPhys);
VMMR3_INT_DECL(int) PGMR3PhysMmio2Unmap(PVM pVM, PPDMDEVINS pDevIns, PGMMMIO2HANDLE hMmio2, RTGCPHYS GCPhys);
VMMR3_INT_DECL(int) PGMR3PhysMmio2Reduce(PVM pVM, PPDMDEVINS pDevIns, PGMMMIO2HANDLE hMmio2, RTGCPHYS cbRegion);
VMMR3_INT_DECL(int) PGMR3PhysMmio2ValidateHandle(PVM pVM, PPDMDEVINS pDevIns, PGMMMIO2HANDLE hMmio2);
VMMR3_INT_DECL(RTGCPHYS) PGMR3PhysMmio2GetMappingAddress(PVM pVM, PPDMDEVINS pDevIns, PGMMMIO2HANDLE hMmio2);
VMMR3_INT_DECL(int) PGMR3PhysMmio2ChangeRegionNo(PVM pVM, PPDMDEVINS pDevIns, PGMMMIO2HANDLE hMmio2, uint32_t iNewRegion);
VMMR3_INT_DECL(int) PGMR3PhysMmio2QueryAndResetDirtyBitmap(PVM pVM, PPDMDEVINS pDevIns, PGMMMIO2HANDLE hMmio2,
                                                           void *pvBitmap, size_t cbBitmap);
VMMR3_INT_DECL(int) PGMR3PhysMmio2ControlDirtyPageTracking(PVM pVM, PPDMDEVINS pDevIns, PGMMMIO2HANDLE hMmio2, bool fEnabled);
#endif /* IN_RING3 */

/** @name PGMPHYS_ROM_FLAGS_XXX - ROM registration flags.
 * @see PGMR3PhysRegisterRom, PDMDevHlpROMRegister
 * @{ */
/** Inidicates that ROM shadowing should be enabled. */
#define PGMPHYS_ROM_FLAGS_SHADOWED                  UINT8_C(0x01)
/** Indicates that what pvBinary points to won't go away
 * and can be used for strictness checks. */
#define PGMPHYS_ROM_FLAGS_PERMANENT_BINARY          UINT8_C(0x02)
/** Indicates that the ROM is allowed to be missing from saved state.
 * @note This is a hack for EFI, see @bugref{6940}   */
#define PGMPHYS_ROM_FLAGS_MAYBE_MISSING_FROM_STATE  UINT8_C(0x04)
/** Valid flags.   */
#define PGMPHYS_ROM_FLAGS_VALID_MASK                UINT8_C(0x07)
/** @} */

#ifdef IN_RING3
VMMR3DECL(int)      PGMR3PhysRomRegister(PVM pVM, PPDMDEVINS pDevIns, RTGCPHYS GCPhys, RTGCPHYS cb,
                                         const void *pvBinary, uint32_t cbBinary, uint8_t fFlags, const char *pszDesc);
VMMR3DECL(int)      PGMR3PhysRomProtect(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cb, PGMROMPROT enmProt);
# if defined(VBOX_VMM_TARGET_X86) || defined(VBOX_VMM_TARGET_AGNOSTIC)
VMMDECL(void)       PGMR3PhysSetA20(PVMCPU pVCpu, bool fEnable);
# endif

VMMR3_INT_DECL(int) PGMR3HandlerPhysicalTypeRegister(PVM pVM, PGMPHYSHANDLERKIND enmKind, uint32_t fFlags,
                                                     PFNPGMPHYSHANDLER pfnHandlerR3, const char *pszDesc,
                                                     PPGMPHYSHANDLERTYPE phType);

VMMR3_INT_DECL(int) PGMR3PoolGrow(PVM pVM, PVMCPU pVCpu);

VMMR3DECL(int)      PGMR3PhysTlbGCPhys2Ptr(PVM pVM, RTGCPHYS GCPhys, bool fWritable, void **ppv);
VMMR3DECL(uint8_t)  PGMR3PhysReadU8(PVM pVM, RTGCPHYS GCPhys, PGMACCESSORIGIN enmOrigin);
VMMR3DECL(uint16_t) PGMR3PhysReadU16(PVM pVM, RTGCPHYS GCPhys, PGMACCESSORIGIN enmOrigin);
VMMR3DECL(uint32_t) PGMR3PhysReadU32(PVM pVM, RTGCPHYS GCPhys, PGMACCESSORIGIN enmOrigin);
VMMR3DECL(uint64_t) PGMR3PhysReadU64(PVM pVM, RTGCPHYS GCPhys, PGMACCESSORIGIN enmOrigin);
VMMR3DECL(void)     PGMR3PhysWriteU8(PVM pVM, RTGCPHYS GCPhys, uint8_t Value, PGMACCESSORIGIN enmOrigin);
VMMR3DECL(void)     PGMR3PhysWriteU16(PVM pVM, RTGCPHYS GCPhys, uint16_t Value, PGMACCESSORIGIN enmOrigin);
VMMR3DECL(void)     PGMR3PhysWriteU32(PVM pVM, RTGCPHYS GCPhys, uint32_t Value, PGMACCESSORIGIN enmOrigin);
VMMR3DECL(void)     PGMR3PhysWriteU64(PVM pVM, RTGCPHYS GCPhys, uint64_t Value, PGMACCESSORIGIN enmOrigin);
VMMR3DECL(int)      PGMR3PhysReadExternal(PVM pVM, RTGCPHYS GCPhys, void *pvBuf, size_t cbRead, PGMACCESSORIGIN enmOrigin);
VMMR3DECL(int)      PGMR3PhysWriteExternal(PVM pVM, RTGCPHYS GCPhys, const void *pvBuf, size_t cbWrite, PGMACCESSORIGIN enmOrigin);
VMMR3DECL(int)      PGMR3PhysGCPhys2CCPtrExternal(PVM pVM, RTGCPHYS GCPhys, void **ppv, PPGMPAGEMAPLOCK pLock);
VMMR3DECL(int)      PGMR3PhysGCPhys2CCPtrReadOnlyExternal(PVM pVM, RTGCPHYS GCPhys, void const **ppv, PPGMPAGEMAPLOCK pLock);
VMMR3DECL(int)      PGMR3PhysBulkGCPhys2CCPtrExternal(PVM pVM, uint32_t cPages, PCRTGCPHYS paGCPhysPages,
                                                      void **papvPages, PPGMPAGEMAPLOCK paLocks);
VMMR3DECL(int)      PGMR3PhysBulkGCPhys2CCPtrReadOnlyExternal(PVM pVM, uint32_t cPages, PCRTGCPHYS paGCPhysPages,
                                                              void const **papvPages, PPGMPAGEMAPLOCK paLocks);
VMMR3DECL(int)      PGMR3PhysAllocateHandyPages(PVM pVM);

VMMR3DECL(int)      PGMR3CheckIntegrity(PVM pVM);

VMMR3DECL(int)      PGMR3DbgR3Ptr2GCPhys(PUVM pUVM, RTR3PTR R3Ptr, PRTGCPHYS pGCPhys);
VMMR3DECL(int)      PGMR3DbgR3Ptr2HCPhys(PUVM pUVM, RTR3PTR R3Ptr, PRTHCPHYS pHCPhys);
VMMR3DECL(int)      PGMR3DbgHCPhys2GCPhys(PUVM pUVM, RTHCPHYS HCPhys, PRTGCPHYS pGCPhys);
VMMR3_INT_DECL(int) PGMR3DbgReadGCPhys(PVM pVM, void *pvDst, RTGCPHYS GCPhysSrc, size_t cb, uint32_t fFlags, size_t *pcbRead);
VMMR3_INT_DECL(int) PGMR3DbgWriteGCPhys(PVM pVM, RTGCPHYS GCPhysDst, const void *pvSrc, size_t cb, uint32_t fFlags, size_t *pcbWritten);
VMMR3_INT_DECL(int) PGMR3DbgReadGCPtr(PVM pVM, void *pvDst, RTGCPTR GCPtrSrc, size_t cb, uint32_t fFlags, size_t *pcbRead);
VMMR3_INT_DECL(int) PGMR3DbgWriteGCPtr(PVM pVM, RTGCPTR GCPtrDst, void const *pvSrc, size_t cb, uint32_t fFlags, size_t *pcbWritten);
VMMR3_INT_DECL(int) PGMR3DbgScanPhysical(PVM pVM, RTGCPHYS GCPhys, RTGCPHYS cbRange, RTGCPHYS GCPhysAlign, const uint8_t *pabNeedle, size_t cbNeedle, PRTGCPHYS pGCPhysHit);
VMMR3_INT_DECL(int) PGMR3DbgScanVirtual(PVM pVM, PVMCPU pVCpu, RTGCPTR GCPtr, RTGCPTR cbRange, RTGCPTR GCPtrAlign, const uint8_t *pabNeedle, size_t cbNeedle, PRTGCUINTPTR pGCPhysHit);
VMMR3_INT_DECL(int) PGMR3DumpHierarchyShw(PVM pVM, uint64_t cr3, uint32_t fFlags, uint64_t u64FirstAddr, uint64_t u64LastAddr, uint32_t cMaxDepth, PCDBGFINFOHLP pHlp);
VMMR3_INT_DECL(int) PGMR3DumpHierarchyGst(PVM pVM, uint64_t cr3, uint32_t fFlags, RTGCPTR FirstAddr, RTGCPTR LastAddr, uint32_t cMaxDepth, PCDBGFINFOHLP pHlp);
#endif /* IN_RING3 */

/** @name Page sharing
 * @{ */
#ifdef IN_RING3
VMMR3DECL(int)     PGMR3SharedModuleRegister(PVM pVM, VBOXOSFAMILY enmGuestOS, char *pszModuleName, char *pszVersion,
                                             RTGCPTR GCBaseAddr, uint32_t cbModule,
                                             uint32_t cRegions, VMMDEVSHAREDREGIONDESC const *paRegions);
VMMR3DECL(int)     PGMR3SharedModuleUnregister(PVM pVM, char *pszModuleName, char *pszVersion,
                                               RTGCPTR GCBaseAddr, uint32_t cbModule);
VMMR3DECL(int)     PGMR3SharedModuleCheckAll(PVM pVM);
VMMR3DECL(int)     PGMR3SharedModuleGetPageState(PVM pVM, RTGCPTR GCPtrPage, bool *pfShared, uint64_t *pfPageFlags);
#endif /* IN_RING3 */
/** @} */

/** @} */

RT_C_DECLS_END

/** @} */
#endif /* !VBOX_INCLUDED_vmm_pgm_h */

