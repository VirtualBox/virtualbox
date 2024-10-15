/* $Id: CPUMInternal-armv8.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * CPUM - Internal header file, ARMv8 variant.
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

#ifndef VMM_INCLUDED_SRC_include_CPUMInternal_armv8_h
#define VMM_INCLUDED_SRC_include_CPUMInternal_armv8_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifndef VBOX_FOR_DTRACE_LIB
# include <VBox/cdefs.h>
# include <VBox/types.h>
# include <VBox/vmm/stam.h>
#else
# pragma D depends_on library cpumctx.d
# pragma D depends_on library cpum.d

/* Some fudging. */
typedef uint64_t STAMCOUNTER;
#endif




/** @defgroup grp_cpum_int   Internals
 * @ingroup grp_cpum
 * @internal
 * @{
 */

/** Use flags (CPUM::fUseFlags).
 * @{ */
/** Set to indicate that we should save host DR0-7 and load the hypervisor debug
 * registers in the raw-mode world switchers. (See CPUMRecalcHyperDRx.) */
#define CPUM_USE_DEBUG_REGS_HYPER       RT_BIT(0)
/** Used in ring-0 to indicate that we have loaded the hypervisor debug
 * registers. */
#define CPUM_USED_DEBUG_REGS_HYPER      RT_BIT(1)
/** Used in ring-0 to indicate that we have loaded the guest debug
 * registers (DR0-3 and maybe DR6) for direct use by the guest.
 * DR7 (and AMD-V DR6) are handled via the VMCB. */
#define CPUM_USED_DEBUG_REGS_GUEST      RT_BIT(2)
/** @} */


/** @name CPUM Saved State Version.
 * @{ */
/** The current saved state version. */
#define CPUM_SAVED_STATE_VERSION                1
/** @} */


/**
 * CPU info
 */
typedef struct CPUMINFO
{
    /** The number of system register ranges (CPUMSSREGRANGE) in the array pointed to below. */
    uint32_t                    cSysRegRanges;

    /** Pointer to the sysrem register ranges. */
    R3PTRTYPE(PCPUMSYSREGRANGE) paSysRegRangesR3;

    /** System register ranges. */
    CPUMSYSREGRANGE             aSysRegRanges[128];
} CPUMINFO;
/** Pointer to a CPU info structure. */
typedef CPUMINFO *PCPUMINFO;
/** Pointer to a const CPU info structure. */
typedef CPUMINFO const *CPCPUMINFO;


/**
 * CPUM Data (part of VM)
 */
typedef struct CPUM
{
    /** The (more) portable CPUID level. */
    uint8_t                 u8PortableCpuIdLevel;
    /** Indicates that a state restore is pending.
     * This is used to verify load order dependencies (PGM). */
    bool                    fPendingRestore;
    /** The initial exception level (EL) to start the CPU after a reset,
     * should be either ARMV8_AARCH64_EL_1 or ARMV8_AARCH64_EL_2 for nested virtualization. */
    uint8_t                 bResetEl;

    uint8_t                 abPadding0[5];

    /** The reset value of the program counter. */
    uint64_t                u64ResetPc;

    /** Align to 64-byte boundary. */
    uint8_t                 abPadding1[48];

    /** Host CPU feature information.
     * Externaly visible via the VM structure, aligned on 64-byte boundrary. */
    CPUMFEATURES            HostFeatures;
    /** Guest CPU feature information.
     * Externaly visible via that VM structure, aligned with HostFeatures. */
    CPUMFEATURES            GuestFeatures;
    /** Guest CPU info. */
    CPUMINFO                GuestInfo;
    /** Host CPU ID registers. */
    CPUMIDREGS              HostIdRegs;
    /** Guest CPU ID registers. */
    CPUMIDREGS              GuestIdRegs;

    /** @name System register statistics.
     * @{ */
    STAMCOUNTER             cSysRegWrites;
    STAMCOUNTER             cSysRegWritesToIgnoredBits;
    STAMCOUNTER             cSysRegWritesRaiseExcp;
    STAMCOUNTER             cSysRegWritesUnknown;
    STAMCOUNTER             cSysRegReads;
    STAMCOUNTER             cSysRegReadsRaiseExcp;
    STAMCOUNTER             cSysRegReadsUnknown;
    /** @} */
} CPUM;
#ifndef VBOX_FOR_DTRACE_LIB
AssertCompileMemberOffset(CPUM, HostFeatures, 64);
AssertCompileMemberOffset(CPUM, GuestFeatures, 112);
#endif
/** Pointer to the CPUM instance data residing in the shared VM structure. */
typedef CPUM *PCPUM;

/**
 * CPUM Data (part of VMCPU)
 */
typedef struct CPUMCPU
{
    /** Guest context.
     * Aligned on a 64-byte boundary. */
    CPUMCTX                 Guest;

    /** Use flags.
     * These flags indicates both what is to be used and what has been used. */
    uint32_t                fUseFlags;

    /** Changed flags.
     * These flags indicates to REM (and others) which important guest
     * registers which has been changed since last time the flags were cleared.
     * See the CPUM_CHANGED_* defines for what we keep track of.
     *
     * @todo Obsolete, but will probably be refactored so keep it for reference. */
    uint32_t                fChanged;
} CPUMCPU;
#ifndef VBOX_FOR_DTRACE_LIB
/** @todo Compile time size/alignment assertions. */
#endif
/** Pointer to the CPUMCPU instance data residing in the shared VMCPU structure. */
typedef CPUMCPU *PCPUMCPU;

#ifndef VBOX_FOR_DTRACE_LIB
RT_C_DECLS_BEGIN

# ifdef IN_RING3
DECLHIDDEN(int)       cpumR3DbgInit(PVM pVM);
DECLHIDDEN(int)       cpumR3SysRegStrictInitChecks(void);

void                  cpumR3SaveCpuId(PVM pVM, PSSMHANDLE pSSM);
int                   cpumR3LoadCpuId(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion);

DECLCALLBACK(void)    cpumR3CpuIdInfo(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
DECLCALLBACK(void)    cpumR3CpuFeatInfo(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);

# endif

RT_C_DECLS_END
#endif /* !VBOX_FOR_DTRACE_LIB */

/** @} */

#endif /* !VMM_INCLUDED_SRC_include_CPUMInternal_armv8_h */

