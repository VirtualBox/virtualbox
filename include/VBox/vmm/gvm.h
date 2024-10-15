/* $Id: gvm.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * GVM - The Global VM Data.
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

#ifndef VBOX_INCLUDED_vmm_gvm_h
#define VBOX_INCLUDED_vmm_gvm_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifndef USING_VMM_COMMON_DEFS
# error "Compile job does not include VMM_COMMON_DEFS from src/VBox/Config.kmk - make sure you really need to include this file!"
#endif
#include <VBox/types.h>
#include <VBox/vmm/vm.h>
#include <VBox/param.h>
#include <iprt/thread.h>
#include <iprt/assertcompile.h>


/** @defgroup grp_gvmcpu    GVMCPU - The Global VMCPU Data
 * @ingroup grp_vmm
 * @{
 */

#if defined(__cplusplus) && !defined(GVM_C_STYLE_STRUCTURES)
typedef struct GVMCPU : public VMCPU
#else
typedef struct GVMCPU
#endif
{
#if !defined(__cplusplus) || defined(GVM_C_STYLE_STRUCTURES)
    VMCPU           s;
#endif

    /** VCPU id (0 - (pVM->cCpus - 1). */
    VMCPUID             idCpu;
    /** Padding. */
    uint32_t            uPadding0;

    /** Handle to the EMT thread. */
    RTNATIVETHREAD      hEMT;

    /** Pointer to the global (ring-0) VM structure this CPU belongs to. */
    R0PTRTYPE(PGVM)     pGVM;
    /** Pointer to the GVM structure, for CTX_SUFF use in VMMAll code.  */
    PGVM                pVMR0;
    /** The ring-3 address of this structure (only VMCPU part). */
    PVMCPUR3            pVCpuR3;

    /** Padding so the noisy stuff on a 64 byte boundrary.
     * @note Keeping this working for 32-bit header syntax checking.  */
    uint8_t             abPadding1[HC_ARCH_BITS == 32 ? 40 : 24];

    /** Which host CPU ID is this EMT running on.
     * Only valid when in RC or HMR0 with scheduling disabled. */
    RTCPUID volatile    idHostCpu;
    /** The CPU set index corresponding to idHostCpu, UINT32_MAX if not valid.
     * @remarks Best to make sure iHostCpuSet shares cache line with idHostCpu! */
    uint32_t volatile   iHostCpuSet;

    /** Padding so gvmm starts on a 64 byte boundrary.
     * @note Keeping this working for 32-bit header syntax checking.  */
    uint8_t             abPadding2[56];

    /** The GVMM per vcpu data. */
    union
    {
#ifdef VMM_INCLUDED_SRC_VMMR0_GVMMR0Internal_h
        struct GVMMPERVCPU  s;
#endif
        uint8_t             padding[256];
    } gvmm;

    /** The HM per vcpu data. */
    union
    {
#if defined(VMM_INCLUDED_SRC_include_HMInternal_h) && defined(IN_RING0)
        struct HMR0PERVCPU  s;
#endif
        uint8_t             padding[1024];
    } hmr0;

#ifdef VBOX_WITH_NEM_R0
    /** The NEM per vcpu data. */
    union
    {
# if defined(VMM_INCLUDED_SRC_include_NEMInternal_h) && defined(IN_RING0)
        struct NEMR0PERVCPU s;
# endif
        uint8_t             padding[64];
    } nemr0;
#endif

    union
    {
#if defined(VMM_INCLUDED_SRC_include_VMMInternal_h) && defined(IN_RING0)
        struct VMMR0PERVCPU s;
#endif
        uint8_t             padding[896];
    } vmmr0;

    union
    {
#if defined(VMM_INCLUDED_SRC_include_PGMInternal_h) && defined(IN_RING0)
        struct PGMR0PERVCPU s;
#endif
        uint8_t             padding[576];
    } pgmr0;

    /** Padding the structure size to page boundrary. */
#ifdef VBOX_WITH_NEM_R0
    uint8_t                 abPadding3[16384 - 64*2 - 256 - 1024 - 64 - 896 - 576];
#else
    uint8_t                 abPadding3[16384 - 64*2 - 256 - 1024      - 896 - 576];
#endif
} GVMCPU;
#ifndef IN_TSTVMSTRUCT
# if RT_GNUC_PREREQ(4, 6) && defined(__cplusplus)
#  pragma GCC diagnostic push
# endif
# if RT_GNUC_PREREQ(4, 3) && defined(__cplusplus)
#  pragma GCC diagnostic ignored "-Winvalid-offsetof"
# endif
AssertCompileMemberAlignment(GVMCPU, idCpu,  16384);
AssertCompileMemberAlignment(GVMCPU, gvmm,   64);
# ifdef VBOX_WITH_NEM_R0
AssertCompileMemberAlignment(GVMCPU, nemr0,  64);
# endif
AssertCompileSizeAlignment(GVMCPU,           16384);
# if RT_GNUC_PREREQ(4, 6) && defined(__cplusplus)
#  pragma GCC diagnostic pop
# endif
#endif

/** @} */

/** @defgroup grp_gvm   GVM - The Global VM Data
 * @ingroup grp_vmm
 * @{
 */

/**
 * The Global VM Data.
 *
 * This is a ring-0 only structure where we put items we don't need to
 * share with ring-3 or GC, like for instance various RTR0MEMOBJ handles.
 *
 * Unlike VM, there are no special alignment restrictions here. The
 * paddings are checked by compile time assertions.
 */
#if defined(__cplusplus) && !defined(GVM_C_STYLE_STRUCTURES)
typedef struct GVM : public VM
#else
typedef struct GVM
#endif
{
#if !defined(__cplusplus) || defined(GVM_C_STYLE_STRUCTURES)
    VM              s;
#endif
    /** Magic / eye-catcher (GVM_MAGIC). */
    uint32_t        u32Magic;
    /** The global VM handle for this VM. */
    uint32_t        hSelf;
    /** Pointer to this structure (for validation purposes). */
    PGVM            pSelf;
    /** The ring-3 mapping of the VM structure. */
    PVMR3           pVMR3;
    /** The support driver session the VM is associated with. */
    PSUPDRVSESSION  pSession;
    /** Number of Virtual CPUs, i.e. how many entries there are in aCpus.
     * Same same as VM::cCpus. */
    uint32_t        cCpus;
    /** Padding so gvmm starts on a 64 byte boundrary.   */
    uint8_t         abPadding[HC_ARCH_BITS == 32 ? 12 + 28 : 28];

    /** The GVMM per vm data. */
    union
    {
#ifdef VMM_INCLUDED_SRC_VMMR0_GVMMR0Internal_h
        struct GVMMPERVM    s;
#endif
        uint8_t             padding[4352];
    } gvmm;

    /** The GMM per vm data. */
    union
    {
#ifdef VMM_INCLUDED_SRC_VMMR0_GMMR0Internal_h
        struct GMMPERVM     s;
#endif
        uint8_t             padding[1024];
    } gmm;

    /** The HM per vm data. */
    union
    {
#if defined(VMM_INCLUDED_SRC_include_HMInternal_h) && defined(IN_RING0)
        struct HMR0PERVM    s;
#endif
        uint8_t             padding[256];
    } hmr0;

#ifdef VBOX_WITH_NEM_R0
    /** The NEM per vcpu data. */
    union
    {
# if defined(VMM_INCLUDED_SRC_include_NEMInternal_h) && defined(IN_RING0)
        struct NEMR0PERVM   s;
# endif
        uint8_t             padding[256];
    } nemr0;
#endif

    /** The RAWPCIVM per vm data. */
    union
    {
#ifdef VBOX_INCLUDED_rawpci_h
        struct RAWPCIPERVM s;
#endif
        uint8_t             padding[64];
    } rawpci;

    union
    {
#if defined(VMM_INCLUDED_SRC_include_PDMInternal_h) && defined(IN_RING0)
        struct PDMR0PERVM   s;
#endif
        uint8_t             padding[3008];
    } pdmr0;

    union
    {
#if defined(VMM_INCLUDED_SRC_include_PGMInternal_h) && defined(IN_RING0)
        struct PGMR0PERVM   s;
#endif
        uint8_t             padding[90112];
    } pgmr0;

    union
    {
#if defined(VMM_INCLUDED_SRC_include_IOMInternal_h) && defined(IN_RING0)
        struct IOMR0PERVM   s;
#endif
        uint8_t             padding[512];
    } iomr0;

    union
    {
#if defined(VMM_INCLUDED_SRC_include_APICInternal_h) && defined(IN_RING0)
        struct APICR0PERVM  s;
#endif
        uint8_t             padding[64];
    } apicr0;

    union
    {
#if defined(VMM_INCLUDED_SRC_include_DBGFInternal_h) && defined(IN_RING0)
        struct DBGFR0PERVM   s;
#endif
        uint8_t             padding[1024];
    } dbgfr0;

    union
    {
#if defined(VMM_INCLUDED_SRC_include_TMInternal_h) && defined(IN_RING0)
        TMR0PERVM           s;
#endif
        uint8_t             padding[192];
    } tmr0;

    union
    {
#if defined(VMM_INCLUDED_SRC_include_VMMInternal_h) && defined(IN_RING0)
        VMMR0PERVM          s;
#endif
        uint8_t             padding[704];
    } vmmr0;

    /** Padding so aCpus starts on a page boundrary.  */
#ifdef VBOX_WITH_NEM_R0
    uint8_t         abPadding2[16384*7 - 64 - 4352 - 1024 - 256 - 256 - 64 - 3008 - 90112 - 512 - 64 - 1024 - 192 - 704 - sizeof(PGVMCPU) * VMM_MAX_CPU_COUNT];
#else
    uint8_t         abPadding2[16384*7 - 64 - 4352 - 1024 - 256 -       64 - 3008 - 90112 - 512 - 64 - 1024 - 192 - 704 - sizeof(PGVMCPU) * VMM_MAX_CPU_COUNT];
#endif

    /** For simplifying CPU enumeration in VMMAll code. */
    PGVMCPU         apCpusR0[VMM_MAX_CPU_COUNT];

    /** GVMCPU array for the configured number of virtual CPUs. */
    GVMCPU          aCpus[1];
} GVM;
#if 0
#if RT_GNUC_PREREQ(4, 6) && defined(__cplusplus)
# pragma GCC diagnostic push
#endif
#if RT_GNUC_PREREQ(4, 3) && defined(__cplusplus)
# pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
AssertCompileMemberAlignment(GVM, u32Magic, 64);
AssertCompileMemberAlignment(GVM, gvmm,     64);
AssertCompileMemberAlignment(GVM, gmm,      64);
#ifdef VBOX_WITH_NEM_R0
AssertCompileMemberAlignment(GVM, nemr0,    64);
#endif
AssertCompileMemberAlignment(GVM, rawpci,   64);
AssertCompileMemberAlignment(GVM, pdmr0,    64);
AssertCompileMemberAlignment(GVM, aCpus,    16384);
AssertCompileSizeAlignment(GVM,             16384);
#if RT_GNUC_PREREQ(4, 6) && defined(__cplusplus)
# pragma GCC diagnostic pop
#endif
#endif

/** The GVM::u32Magic value (Wayne Shorter). */
#define GVM_MAGIC       0x19330825

/** @} */

#endif /* !VBOX_INCLUDED_vmm_gvm_h */

