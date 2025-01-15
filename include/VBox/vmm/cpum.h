/** @file
 * CPUM - CPU Monitor(/ Manager).
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

#ifndef VBOX_INCLUDED_vmm_cpum_h
#define VBOX_INCLUDED_vmm_cpum_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/types.h>
#include <VBox/vmm/cpumctx.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/vmapi.h>
#include <VBox/vmm/cpum-common.h>


/** @defgroup grp_cpum      The CPU Monitor / Manager API
 * @ingroup grp_vmm
 * @{
 */

/**
 * CPU Vendor.
 */
typedef enum CPUMCPUVENDOR
{
    CPUMCPUVENDOR_INVALID = 0,
    CPUMCPUVENDOR_INTEL,
    CPUMCPUVENDOR_AMD,
    CPUMCPUVENDOR_VIA,
    CPUMCPUVENDOR_CYRIX,
    CPUMCPUVENDOR_SHANGHAI,
    CPUMCPUVENDOR_HYGON,
    CPUMCPUVENDOR_APPLE,        /**< ARM */
    CPUMCPUVENDOR_UNKNOWN,
    /** 32bit hackishness. */
    CPUMCPUVENDOR_32BIT_HACK = 0x7fffffff
} CPUMCPUVENDOR;


/**
 * CPU architecture.
 */
typedef enum CPUMARCH
{
    /** Invalid zero value. */
    kCpumArch_Invalid = 0,
    /** x86 based architecture (includes 64-bit). */
    kCpumArch_X86,
    /** ARM based architecture (includs both AArch32 and AArch64). */
    kCpumArch_Arm,

    /** @todo RiscV, Mips, ... ;). */

    /*
     * Unknown.
     */
    kCpumArch_Unknown,

    kCpumArch_32BitHack = 0x7fffffff
} CPUMARCH;


/**
 * CPU microarchitectures and in processor generations.
 *
 * @remarks The separation here is sometimes a little bit too finely grained,
 *          and the differences is more like processor generation than micro
 *          arch.  This can be useful, so we'll provide functions for getting at
 *          more coarse grained info.
 */
typedef enum CPUMMICROARCH
{
    kCpumMicroarch_Invalid = 0,

    /*
     * x86 and AMD64 CPUs.
     */

    kCpumMicroarch_Intel_First,

    kCpumMicroarch_Intel_8086 = kCpumMicroarch_Intel_First,
    kCpumMicroarch_Intel_80186,
    kCpumMicroarch_Intel_80286,
    kCpumMicroarch_Intel_80386,
    kCpumMicroarch_Intel_80486,
    kCpumMicroarch_Intel_P5,

    kCpumMicroarch_Intel_P6_Core_Atom_First,
    kCpumMicroarch_Intel_P6 = kCpumMicroarch_Intel_P6_Core_Atom_First,
    kCpumMicroarch_Intel_P6_II,
    kCpumMicroarch_Intel_P6_III,

    kCpumMicroarch_Intel_P6_M_Banias,
    kCpumMicroarch_Intel_P6_M_Dothan,
    kCpumMicroarch_Intel_Core_Yonah,        /**< Core, also known as Enhanced Pentium M. */

    kCpumMicroarch_Intel_Core2_First,
    kCpumMicroarch_Intel_Core2_Merom = kCpumMicroarch_Intel_Core2_First,    /**< 65nm, Merom/Conroe/Kentsfield/Tigerton */
    kCpumMicroarch_Intel_Core2_Penryn,      /**< 45nm, Penryn/Wolfdale/Yorkfield/Harpertown */
    kCpumMicroarch_Intel_Core2_End,

    kCpumMicroarch_Intel_Core7_First,
    kCpumMicroarch_Intel_Core7_Nehalem = kCpumMicroarch_Intel_Core7_First,
    kCpumMicroarch_Intel_Core7_Westmere,
    kCpumMicroarch_Intel_Core7_SandyBridge,
    kCpumMicroarch_Intel_Core7_IvyBridge,
    kCpumMicroarch_Intel_Core7_Haswell,
    kCpumMicroarch_Intel_Core7_Broadwell,
    kCpumMicroarch_Intel_Core7_Skylake,
    kCpumMicroarch_Intel_Core7_KabyLake,
    kCpumMicroarch_Intel_Core7_CoffeeLake,
    kCpumMicroarch_Intel_Core7_WhiskeyLake,
    kCpumMicroarch_Intel_Core7_CascadeLake,
    kCpumMicroarch_Intel_Core7_CannonLake,  /**< Limited 10nm. */
    kCpumMicroarch_Intel_Core7_CometLake,   /**< 10th gen, 14nm desktop + high power mobile.  */
    kCpumMicroarch_Intel_Core7_IceLake,     /**< 10th gen, 10nm mobile and some Xeons.  Actually 'Sunny Cove' march. */
    kCpumMicroarch_Intel_Core7_SunnyCove = kCpumMicroarch_Intel_Core7_IceLake,
    kCpumMicroarch_Intel_Core7_RocketLake,  /**< 11th gen, 14nm desktop + high power mobile.  Aka 'Cypress Cove', backport of 'Willow Cove' to 14nm. */
    kCpumMicroarch_Intel_Core7_CypressCove = kCpumMicroarch_Intel_Core7_RocketLake,
    kCpumMicroarch_Intel_Core7_TigerLake,   /**< 11th gen, 10nm mobile.  Actually 'Willow Cove' march. */
    kCpumMicroarch_Intel_Core7_WillowCove = kCpumMicroarch_Intel_Core7_TigerLake,
    kCpumMicroarch_Intel_Core7_AlderLake,   /**< 12th gen, 10nm all platforms(?). */
    kCpumMicroarch_Intel_Core7_SapphireRapids, /**< 12th? gen, 10nm server? */
    kCpumMicroarch_Intel_Core7_End,

    kCpumMicroarch_Intel_Atom_First,
    kCpumMicroarch_Intel_Atom_Bonnell = kCpumMicroarch_Intel_Atom_First,
    kCpumMicroarch_Intel_Atom_Lincroft,     /**< Second generation bonnell (44nm). */
    kCpumMicroarch_Intel_Atom_Saltwell,     /**< 32nm shrink of Bonnell. */
    kCpumMicroarch_Intel_Atom_Silvermont,   /**< 22nm */
    kCpumMicroarch_Intel_Atom_Airmount,     /**< 14nm */
    kCpumMicroarch_Intel_Atom_Goldmont,     /**< 14nm */
    kCpumMicroarch_Intel_Atom_GoldmontPlus, /**< 14nm */
    kCpumMicroarch_Intel_Atom_Unknown,
    kCpumMicroarch_Intel_Atom_End,


    kCpumMicroarch_Intel_Phi_First,
    kCpumMicroarch_Intel_Phi_KnightsFerry = kCpumMicroarch_Intel_Phi_First,
    kCpumMicroarch_Intel_Phi_KnightsCorner,
    kCpumMicroarch_Intel_Phi_KnightsLanding,
    kCpumMicroarch_Intel_Phi_KnightsHill,
    kCpumMicroarch_Intel_Phi_KnightsMill,
    kCpumMicroarch_Intel_Phi_End,

    kCpumMicroarch_Intel_P6_Core_Atom_End,

    kCpumMicroarch_Intel_NB_First,
    kCpumMicroarch_Intel_NB_Willamette = kCpumMicroarch_Intel_NB_First, /**< 180nm */
    kCpumMicroarch_Intel_NB_Northwood,      /**< 130nm */
    kCpumMicroarch_Intel_NB_Prescott,       /**< 90nm */
    kCpumMicroarch_Intel_NB_Prescott2M,     /**< 90nm */
    kCpumMicroarch_Intel_NB_CedarMill,      /**< 65nm */
    kCpumMicroarch_Intel_NB_Gallatin,       /**< 90nm Xeon, Pentium 4 Extreme Edition ("Emergency Edition"). */
    kCpumMicroarch_Intel_NB_Unknown,
    kCpumMicroarch_Intel_NB_End,

    kCpumMicroarch_Intel_Unknown,
    kCpumMicroarch_Intel_End,

    kCpumMicroarch_AMD_First,
    kCpumMicroarch_AMD_Am286 = kCpumMicroarch_AMD_First,
    kCpumMicroarch_AMD_Am386,
    kCpumMicroarch_AMD_Am486,
    kCpumMicroarch_AMD_Am486Enh,            /**< Covers Am5x86 as well. */
    kCpumMicroarch_AMD_K5,
    kCpumMicroarch_AMD_K6,

    kCpumMicroarch_AMD_K7_First,
    kCpumMicroarch_AMD_K7_Palomino = kCpumMicroarch_AMD_K7_First,
    kCpumMicroarch_AMD_K7_Spitfire,
    kCpumMicroarch_AMD_K7_Thunderbird,
    kCpumMicroarch_AMD_K7_Morgan,
    kCpumMicroarch_AMD_K7_Thoroughbred,
    kCpumMicroarch_AMD_K7_Barton,
    kCpumMicroarch_AMD_K7_Unknown,
    kCpumMicroarch_AMD_K7_End,

    kCpumMicroarch_AMD_K8_First,
    kCpumMicroarch_AMD_K8_130nm = kCpumMicroarch_AMD_K8_First, /**< 130nm Clawhammer, Sledgehammer, Newcastle, Paris, Odessa, Dublin */
    kCpumMicroarch_AMD_K8_90nm,             /**< 90nm shrink */
    kCpumMicroarch_AMD_K8_90nm_DualCore,    /**< 90nm with two cores. */
    kCpumMicroarch_AMD_K8_90nm_AMDV,        /**< 90nm with AMD-V (usually) and two cores (usually). */
    kCpumMicroarch_AMD_K8_65nm,             /**< 65nm shrink. */
    kCpumMicroarch_AMD_K8_End,

    kCpumMicroarch_AMD_K10,
    kCpumMicroarch_AMD_K10_Lion,
    kCpumMicroarch_AMD_K10_Llano,
    kCpumMicroarch_AMD_Bobcat,
    kCpumMicroarch_AMD_Jaguar,

    kCpumMicroarch_AMD_15h_First,
    kCpumMicroarch_AMD_15h_Bulldozer = kCpumMicroarch_AMD_15h_First,
    kCpumMicroarch_AMD_15h_Piledriver,
    kCpumMicroarch_AMD_15h_Steamroller,     /**< Yet to be released, might have different family.  */
    kCpumMicroarch_AMD_15h_Excavator,       /**< Yet to be released, might have different family.  */
    kCpumMicroarch_AMD_15h_Unknown,
    kCpumMicroarch_AMD_15h_End,

    kCpumMicroarch_AMD_16h_First,
    kCpumMicroarch_AMD_16h_End,

    kCpumMicroarch_AMD_Zen_First,
    kCpumMicroarch_AMD_Zen_Ryzen = kCpumMicroarch_AMD_Zen_First,
    kCpumMicroarch_AMD_Zen_End,

    kCpumMicroarch_AMD_Unknown,
    kCpumMicroarch_AMD_End,

    kCpumMicroarch_Hygon_First,
    kCpumMicroarch_Hygon_Dhyana = kCpumMicroarch_Hygon_First,
    kCpumMicroarch_Hygon_Unknown,
    kCpumMicroarch_Hygon_End,

    kCpumMicroarch_VIA_First,
    kCpumMicroarch_Centaur_C6 = kCpumMicroarch_VIA_First,
    kCpumMicroarch_Centaur_C2,
    kCpumMicroarch_Centaur_C3,
    kCpumMicroarch_VIA_C3_M2,
    kCpumMicroarch_VIA_C3_C5A,          /**< 180nm Samuel - Cyrix III, C3, 1GigaPro. */
    kCpumMicroarch_VIA_C3_C5B,          /**< 150nm Samuel 2 - Cyrix III, C3, 1GigaPro, Eden ESP, XP 2000+. */
    kCpumMicroarch_VIA_C3_C5C,          /**< 130nm Ezra - C3, Eden ESP. */
    kCpumMicroarch_VIA_C3_C5N,          /**< 130nm Ezra-T - C3. */
    kCpumMicroarch_VIA_C3_C5XL,         /**< 130nm Nehemiah - C3, Eden ESP, Eden-N. */
    kCpumMicroarch_VIA_C3_C5P,          /**< 130nm Nehemiah+ - C3. */
    kCpumMicroarch_VIA_C7_C5J,          /**< 90nm Esther - C7, C7-D, C7-M, Eden, Eden ULV. */
    kCpumMicroarch_VIA_Isaiah,
    kCpumMicroarch_VIA_Unknown,
    kCpumMicroarch_VIA_End,

    kCpumMicroarch_Shanghai_First,
    kCpumMicroarch_Shanghai_Wudaokou = kCpumMicroarch_Shanghai_First,
    kCpumMicroarch_Shanghai_Unknown,
    kCpumMicroarch_Shanghai_End,

    kCpumMicroarch_Cyrix_First,
    kCpumMicroarch_Cyrix_5x86 = kCpumMicroarch_Cyrix_First,
    kCpumMicroarch_Cyrix_M1,
    kCpumMicroarch_Cyrix_MediaGX,
    kCpumMicroarch_Cyrix_MediaGXm,
    kCpumMicroarch_Cyrix_M2,
    kCpumMicroarch_Cyrix_Unknown,
    kCpumMicroarch_Cyrix_End,

    kCpumMicroarch_NEC_First,
    kCpumMicroarch_NEC_V20 = kCpumMicroarch_NEC_First,
    kCpumMicroarch_NEC_V30,
    kCpumMicroarch_NEC_End,

    /*
     * ARM CPUs.
     */
    kCpumMicroarch_Apple_First,
    kCpumMicroarch_Apple_M1 = kCpumMicroarch_Apple_First,
    kCpumMicroarch_Apple_M2,
    kCpumMicroarch_Apple_End,

    /*
     * Unknown.
     */
    kCpumMicroarch_Unknown,

    kCpumMicroarch_32BitHack = 0x7fffffff
} CPUMMICROARCH;


/** Predicate macro for catching netburst CPUs. */
#define CPUMMICROARCH_IS_INTEL_NETBURST(a_enmMicroarch) \
    ((a_enmMicroarch) >= kCpumMicroarch_Intel_NB_First && (a_enmMicroarch) <= kCpumMicroarch_Intel_NB_End)

/** Predicate macro for catching Core7 CPUs. */
#define CPUMMICROARCH_IS_INTEL_CORE7(a_enmMicroarch) \
    ((a_enmMicroarch) >= kCpumMicroarch_Intel_Core7_First && (a_enmMicroarch) <= kCpumMicroarch_Intel_Core7_End)

/** Predicate macro for catching Core 2 CPUs. */
#define CPUMMICROARCH_IS_INTEL_CORE2(a_enmMicroarch) \
    ((a_enmMicroarch) >= kCpumMicroarch_Intel_Core2_First && (a_enmMicroarch) <= kCpumMicroarch_Intel_Core2_End)

/** Predicate macro for catching Atom CPUs, Silvermont and upwards. */
#define CPUMMICROARCH_IS_INTEL_SILVERMONT_PLUS(a_enmMicroarch) \
    ((a_enmMicroarch) >= kCpumMicroarch_Intel_Atom_Silvermont && (a_enmMicroarch) <= kCpumMicroarch_Intel_Atom_End)

/** Predicate macro for catching AMD Family OFh CPUs (aka K8).    */
#define CPUMMICROARCH_IS_AMD_FAM_0FH(a_enmMicroarch) \
    ((a_enmMicroarch) >= kCpumMicroarch_AMD_K8_First && (a_enmMicroarch) <= kCpumMicroarch_AMD_K8_End)

/** Predicate macro for catching AMD Family 10H CPUs (aka K10).    */
#define CPUMMICROARCH_IS_AMD_FAM_10H(a_enmMicroarch) ((a_enmMicroarch) == kCpumMicroarch_AMD_K10)

/** Predicate macro for catching AMD Family 11H CPUs (aka Lion).    */
#define CPUMMICROARCH_IS_AMD_FAM_11H(a_enmMicroarch) ((a_enmMicroarch) == kCpumMicroarch_AMD_K10_Lion)

/** Predicate macro for catching AMD Family 12H CPUs (aka Llano).    */
#define CPUMMICROARCH_IS_AMD_FAM_12H(a_enmMicroarch) ((a_enmMicroarch) == kCpumMicroarch_AMD_K10_Llano)

/** Predicate macro for catching AMD Family 14H CPUs (aka Bobcat).    */
#define CPUMMICROARCH_IS_AMD_FAM_14H(a_enmMicroarch) ((a_enmMicroarch) == kCpumMicroarch_AMD_Bobcat)

/** Predicate macro for catching AMD Family 15H CPUs (bulldozer and it's
 * decendants). */
#define CPUMMICROARCH_IS_AMD_FAM_15H(a_enmMicroarch) \
    ((a_enmMicroarch) >= kCpumMicroarch_AMD_15h_First && (a_enmMicroarch) <= kCpumMicroarch_AMD_15h_End)

/** Predicate macro for catching AMD Family 16H CPUs. */
#define CPUMMICROARCH_IS_AMD_FAM_16H(a_enmMicroarch) \
    ((a_enmMicroarch) >= kCpumMicroarch_AMD_16h_First && (a_enmMicroarch) <= kCpumMicroarch_AMD_16h_End)

/** Predicate macro for catching AMD Zen Family CPUs. */
#define CPUMMICROARCH_IS_AMD_FAM_ZEN(a_enmMicroarch) \
    ((a_enmMicroarch) >= kCpumMicroarch_AMD_Zen_First && (a_enmMicroarch) <= kCpumMicroarch_AMD_Zen_End)

/** Predicate macro for catching Apple (ARM) CPUs. */
#define CPUMMICROARCH_IS_APPLE(a_enmMicroarch) \
    ((a_enmMicroarch) >= kCpumMicroarch_Apple_First && (a_enmMicroarch) <= kCpumMicroarch_Apple_End)


/*
 * Include the target specific header.
 * This uses several of the above types, so it must be postponed till here.
 */
#ifndef VBOX_VMM_TARGET_ARMV8
# include <VBox/vmm/cpum-x86-amd64.h>
#else
# include <VBox/vmm/cpum-armv8.h>
#endif


RT_C_DECLS_BEGIN

#ifndef VBOX_FOR_DTRACE_LIB

VMMDECL(void)           CPUMSetChangedFlags(PVMCPU pVCpu, uint32_t fChangedAdd);
VMMDECL(PCPUMCTX)       CPUMQueryGuestCtxPtr(PVMCPU pVCpu);
VMMDECL(CPUMMODE)       CPUMGetGuestMode(PVMCPU pVCpu);
VMMDECL(uint32_t)       CPUMGetGuestCodeBits(PVMCPU pVCpu);
VMMDECL(DISCPUMODE)     CPUMGetGuestDisMode(PVMCPU pVCpu);

/** @name Guest Register Getters.
 * @{ */
VMMDECL(uint64_t)       CPUMGetGuestFlatPC(PVMCPU pVCpu);
VMMDECL(uint64_t)       CPUMGetGuestFlatSP(PVMCPU pVCpu);
VMMDECL(CPUMCPUVENDOR)  CPUMGetGuestCpuVendor(PVM pVM);
VMMDECL(CPUMARCH)       CPUMGetGuestArch(PCVM pVM);
VMMDECL(CPUMMICROARCH)  CPUMGetGuestMicroarch(PCVM pVM);
VMMDECL(void)           CPUMGetGuestAddrWidths(PCVM pVM, uint8_t *pcPhysAddrWidth, uint8_t *pcLinearAddrWidth);
/** @} */

/** @name Misc Guest Predicate Functions.
 * @{  */
VMMDECL(bool)           CPUMIsGuestIn64BitCode(PCVMCPU pVCpu);
/** @} */

VMMDECL(CPUMCPUVENDOR)  CPUMGetHostCpuVendor(PVM pVM);
VMMDECL(CPUMARCH)       CPUMGetHostArch(PCVM pVM);
VMMDECL(CPUMMICROARCH)  CPUMGetHostMicroarch(PCVM pVM);

#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
VMM_INT_DECL(void)      CPUMCpuIdApplyX86HostArchCapabilities(PVMCC pVM, bool fHasArchCap, uint64_t fHostArchVal);
#endif

#ifdef IN_RING3
/** @defgroup grp_cpum_r3    The CPUM ring-3 API
 * @{
 */

VMMR3DECL(int)          CPUMR3Init(PVM pVM);
VMMR3DECL(int)          CPUMR3InitCompleted(PVM pVM, VMINITCOMPLETED enmWhat);
VMMR3DECL(void)         CPUMR3LogCpuIdAndMsrFeatures(PVM pVM);
VMMR3DECL(void)         CPUMR3Relocate(PVM pVM);
VMMR3DECL(int)          CPUMR3Term(PVM pVM);
VMMR3DECL(void)         CPUMR3Reset(PVM pVM);
VMMR3DECL(void)         CPUMR3ResetCpu(PVM pVM, PVMCPU pVCpu);
VMMDECL(bool)           CPUMR3IsStateRestorePending(PVM pVM);
VMMDECL(const char *)       CPUMMicroarchName(CPUMMICROARCH enmMicroarch);
VMMR3DECL(const char *)     CPUMCpuVendorName(CPUMCPUVENDOR enmVendor);

VMMR3DECL(uint32_t)         CPUMR3DbGetEntries(void);
/** Pointer to CPUMR3DbGetEntries. */
typedef DECLCALLBACKPTR(uint32_t, PFNCPUMDBGETENTRIES, (void));
VMMR3DECL(PCCPUMDBENTRY)    CPUMR3DbGetEntryByIndex(uint32_t idxCpuDb);
/** Pointer to CPUMR3DbGetEntryByIndex. */
typedef DECLCALLBACKPTR(PCCPUMDBENTRY, PFNCPUMDBGETENTRYBYINDEX, (uint32_t idxCpuDb));
VMMR3DECL(PCCPUMDBENTRY)    CPUMR3DbGetEntryByName(const char *pszName);
/** Pointer to CPUMR3DbGetEntryByName. */
typedef DECLCALLBACKPTR(PCCPUMDBENTRY, PFNCPUMDBGETENTRYBYNAME, (const char *pszName));

VMMR3_INT_DECL(void)    CPUMR3NemActivateGuestDebugState(PVMCPUCC pVCpu);
VMMR3_INT_DECL(void)    CPUMR3NemActivateHyperDebugState(PVMCPUCC pVCpu);
/** @} */
#endif /* IN_RING3 */

#endif /* !VBOX_FOR_DTRACE_LIB */
/** @} */
RT_C_DECLS_END


#endif /* !VBOX_INCLUDED_vmm_cpum_h */

