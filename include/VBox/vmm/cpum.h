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

    /* ARM: */
    CPUMCPUVENDOR_ARM,
    CPUMCPUVENDOR_BROADCOM,
    CPUMCPUVENDOR_QUALCOMM,
    CPUMCPUVENDOR_APPLE,
    CPUMCPUVENDOR_AMPERE,

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
    kCpumMicroarch_Apple_M3,
    kCpumMicroarch_Apple_M4,
    kCpumMicroarch_Apple_End,

    kCpumMicroarch_Qualcomm_First,
    kCpumMicroarch_Qualcomm_Kyro = kCpumMicroarch_Qualcomm_First,
    kCpumMicroarch_Qualcomm_Oryon,
    kCpumMicroarch_Qualcomm_End,

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


/**
 * Common portion of the CPU feature structures.
 */
typedef struct CPUMFEATURESCOMMON
{
    /** The microarchitecture. */
#ifndef VBOX_FOR_DTRACE_LIB
    CPUMMICROARCH   enmMicroarch;
#else
    uint32_t        enmMicroarch;
#endif
    /** The CPU vendor (CPUMCPUVENDOR). */
    uint8_t         enmCpuVendor;
    /** The maximum physical address width of the CPU. */
    uint8_t         cMaxPhysAddrWidth;
    /** The maximum linear address width of the CPU. */
    uint8_t         cMaxLinearAddrWidth;
} CPUMFEATURESCOMMON;

/**
 * CPU features and quirks for X86.
 *
 * This is mostly exploded CPUID info.
 */
typedef struct CPUMFEATURESX86
{
    /** The microarchitecture. */
#ifndef VBOX_FOR_DTRACE_LIB
    CPUMMICROARCH   enmMicroarch;
#else
    uint32_t        enmMicroarch;
#endif
    /** The CPU vendor (CPUMCPUVENDOR). */
    uint8_t         enmCpuVendor;
    /** The maximum physical address width of the CPU. */
    uint8_t         cMaxPhysAddrWidth;
    /** The maximum linear address width of the CPU. */
    uint8_t         cMaxLinearAddrWidth;

    /** The CPU family. */
    uint8_t         uFamily;
    /** The CPU model. */
    uint8_t         uModel;
    /** The CPU stepping. */
    uint8_t         uStepping;
    /** Max size of the extended state (or FPU state if no XSAVE). */
    uint16_t        cbMaxExtendedState;

    /** Supports MSRs. */
    uint32_t        fMsr : 1;
    /** Supports the page size extension (4/2 MB pages). */
    uint32_t        fPse : 1;
    /** Supports 36-bit page size extension (4 MB pages can map memory above
     *  4GB). */
    uint32_t        fPse36 : 1;
    /** Supports physical address extension (PAE). */
    uint32_t        fPae : 1;
    /** Supports page-global extension (PGE). */
    uint32_t        fPge : 1;
    /** Page attribute table (PAT) support (page level cache control). */
    uint32_t        fPat : 1;
    /** Supports the FXSAVE and FXRSTOR instructions. */
    uint32_t        fFxSaveRstor : 1;
    /** Supports the XSAVE and XRSTOR instructions. */
    uint32_t        fXSaveRstor : 1;
    /** Supports the XSAVEOPT instruction. */
    uint32_t        fXSaveOpt : 1;
    /** The XSAVE/XRSTOR bit in CR4 has been set (only applicable for host!). */
    uint32_t        fOpSysXSaveRstor : 1;
    /** Supports MMX. */
    uint32_t        fMmx : 1;
    /** Supports AMD extensions to MMX instructions. */
    uint32_t        fAmdMmxExts : 1;
    /** Supports SSE. */
    uint32_t        fSse : 1;
    /** Supports SSE2. */
    uint32_t        fSse2 : 1;
    /** Supports SSE3. */
    uint32_t        fSse3 : 1;
    /** Supports SSSE3. */
    uint32_t        fSsse3 : 1;
    /** Supports SSE4.1. */
    uint32_t        fSse41 : 1;
    /** Supports SSE4.2. */
    uint32_t        fSse42 : 1;
    /** Supports AVX. */
    uint32_t        fAvx : 1;
    /** Supports AVX2. */
    uint32_t        fAvx2 : 1;
    /** Supports AVX512 foundation. */
    uint32_t        fAvx512Foundation : 1;
    /** Supports RDTSC. */
    uint32_t        fTsc : 1;
    /** Intel SYSENTER/SYSEXIT support */
    uint32_t        fSysEnter : 1;
    /** Supports MTRR. */
    uint32_t        fMtrr : 1;
    /** First generation APIC. */
    uint32_t        fApic : 1;
    /** Second generation APIC. */
    uint32_t        fX2Apic : 1;
    /** Hypervisor present. */
    uint32_t        fHypervisorPresent : 1;
    /** MWAIT & MONITOR instructions supported. */
    uint32_t        fMonitorMWait : 1;
    /** MWAIT Extensions present. */
    uint32_t        fMWaitExtensions : 1;
    /** Supports CMPXCHG8B. */
    uint32_t        fCmpXchg8b : 1;
    /** Supports CMPXCHG16B in 64-bit mode. */
    uint32_t        fCmpXchg16b : 1;
    /** Supports CLFLUSH. */
    uint32_t        fClFlush : 1;
    /** Supports CLFLUSHOPT. */
    uint32_t        fClFlushOpt : 1;
    /** Supports IA32_PRED_CMD.IBPB. */
    uint32_t        fIbpb : 1;
    /** Supports the IA32_SPEC_CTRL MSR (summary of the next). */
    uint32_t        fSpecCtrlMsr : 1;
    /** Supports IA32_SPEC_CTRL.IBRS. */
    uint32_t        fIbrs : 1;
    /** Supports IA32_SPEC_CTRL.STIBP. */
    uint32_t        fStibp : 1;
    /** Supports IA32_SPEC_CTRL.SSBD. */
    uint32_t        fSsbd : 1;
    /** Supports IA32_SPEC_CTRL.PSFD. */
    uint32_t        fPsfd : 1;
    /** Supports IA32_SPEC_CTRL.IPRED_DIS_U/S. */
    uint32_t        fIpredCtrl : 1;
    /** Supports IA32_SPEC_CTRL.RRSBA_DIS_U/S. */
    uint32_t        fRrsbaCtrl : 1;
    /** Supports IA32_SPEC_CTRL.DDPD_DIS_U. */
    uint32_t        fDdpdU : 1;
    /** Supports IA32_SPEC_CTRL.BHI_S. */
    uint32_t        fBhiCtrl : 1;
    /** Supports IA32_FLUSH_CMD. */
    uint32_t        fFlushCmd : 1;
    /** Supports IA32_ARCH_CAP. */
    uint32_t        fArchCap : 1;
    /** Supports IA32_CORE_CAP. */
    uint32_t        fCoreCap : 1;
    /** Supports MD_CLEAR functionality (VERW, IA32_FLUSH_CMD). */
    uint32_t        fMdsClear : 1;
    /** Whether susceptible to MXCSR configuration dependent timing (MCDT) behaviour. */
    uint32_t        fMcdtNo : 1;
    /** Whether susceptible MONITOR/UMONITOR internal table capacity issues. */
    uint32_t        fMonitorMitgNo : 1;
    /** Supports the UC-lock disable feature. */
    uint32_t        fUcLockDis : 1;
    /** Supports PCID. */
    uint32_t        fPcid : 1;
    /** Supports INVPCID. */
    uint32_t        fInvpcid : 1;
    /** Supports read/write FSGSBASE instructions. */
    uint32_t        fFsGsBase : 1;
    /** Supports BMI1 instructions (ANDN, BEXTR, BLSI, BLSMSK, BLSR, and TZCNT). */
    uint32_t        fBmi1 : 1;
    /** Supports BMI2 instructions (BZHI, MULX, PDEP, PEXT, RORX, SARX, SHRX,
     * and SHLX). */
    uint32_t        fBmi2 : 1;
    /** Supports POPCNT instruction. */
    uint32_t        fPopCnt : 1;
    /** Supports RDRAND instruction. */
    uint32_t        fRdRand : 1;
    /** Supports RDSEED instruction. */
    uint32_t        fRdSeed : 1;
    /** Supports Hardware Lock Elision (HLE). */
    uint32_t        fHle : 1;
    /** Supports Restricted Transactional Memory (RTM - XBEGIN, XEND, XABORT). */
    uint32_t        fRtm : 1;
    /** Supports PCLMULQDQ instruction. */
    uint32_t        fPclMul : 1;
    /** Supports AES-NI (six AESxxx instructions). */
    uint32_t        fAesNi : 1;
    /** Support MOVBE instruction. */
    uint32_t        fMovBe : 1;
    /** Support SHA instructions. */
    uint32_t        fSha : 1;
    /** Support ADX instructions. */
    uint32_t        fAdx : 1;
    /** Supports FMA. */
    uint32_t        fFma : 1;
    /** Supports F16C. */
    uint32_t        fF16c : 1;

    /** Supports AMD 3DNow instructions. */
    uint32_t        f3DNow : 1;
    /** Supports the 3DNow/AMD64 prefetch instructions (could be nops). */
    uint32_t        f3DNowPrefetch : 1;

    /** AMD64: Supports long mode. */
    uint32_t        fLongMode : 1;
    /** AMD64: SYSCALL/SYSRET support. */
    uint32_t        fSysCall : 1;
    /** AMD64: No-execute page table bit. */
    uint32_t        fNoExecute : 1;
    /** AMD64: Supports LAHF & SAHF instructions in 64-bit mode. */
    uint32_t        fLahfSahf : 1;
    /** AMD64: Supports RDTSCP. */
    uint32_t        fRdTscP : 1;
    /** AMD64: Supports MOV CR8 in 32-bit code (lock prefix hack). */
    uint32_t        fMovCr8In32Bit : 1;
    /** AMD64: Supports XOP (similar to VEX3/AVX). */
    uint32_t        fXop : 1;
    /** AMD64: Supports ABM, i.e. the LZCNT instruction. */
    uint32_t        fAbm : 1;
    /** AMD64: Supports TBM (BEXTR, BLCFILL, BLCI, BLCIC, BLCMSK, BLCS,
     *  BLSFILL, BLSIC, T1MSKC, and TZMSK). */
    uint32_t        fTbm : 1;

    /** Indicates that FPU instruction and data pointers may leak.
     * This generally applies to recent AMD CPUs, where the FPU IP and DP pointer
     * is only saved and restored if an exception is pending. */
    uint32_t        fLeakyFxSR : 1;

    /** Supports VEX instruction encoding (AVX, BMI, etc.). */
    uint32_t        fVex : 1;

    /** AMD64: Supports AMD SVM. */
    uint32_t        fSvm : 1;

    /** Support for Intel VMX. */
    uint32_t        fVmx : 1;

    /** Indicates that speculative execution control CPUID bits and MSRs are exposed.
     * The details are different for Intel and AMD but both have similar
     * functionality. */
    uint32_t        fSpeculationControl : 1;

    /** @name MSR_IA32_ARCH_CAPABILITIES
     * @remarks Only safe use after CPUM ring-0 init!
     * @{  */
    /** MSR_IA32_ARCH_CAPABILITIES[0]: RDCL_NO */
    uint32_t        fArchRdclNo : 1;
    /** MSR_IA32_ARCH_CAPABILITIES[1]: IBRS_ALL */
    uint32_t        fArchIbrsAll : 1;
    /** MSR_IA32_ARCH_CAPABILITIES[2]: RSB Alternate */
    uint32_t        fArchRsbOverride : 1;
    /** MSR_IA32_ARCH_CAPABILITIES[3]: SKIP_L1DFL_VMENTRY */
    uint32_t        fArchVmmNeedNotFlushL1d : 1;
    /** MSR_IA32_ARCH_CAPABILITIES[4]: SSB_NO - No Speculative Store Bypass */
    uint32_t        fArchSsbNo : 1;
    /** MSR_IA32_ARCH_CAPABILITIES[5]: MDS_NO - No Microarchitecural Data Sampling */
    uint32_t        fArchMdsNo : 1;
    /** MSR_IA32_ARCH_CAPABILITIES[6]: IF_PSCHANGE_MC_NO */
    uint32_t        fArchIfPschangeMscNo : 1;
    /** MSR_IA32_ARCH_CAPABILITIES[7]: TSX_CTRL (MSR: IA32_TSX_CTRL_MSR[1:0]) */
    uint32_t        fArchTsxCtrl : 1;
    /** MSR_IA32_ARCH_CAPABILITIES[8]: TAA_NO - No Transactional Synchronization
     *  Extensions Asynchronous Abort. */
    uint32_t        fArchTaaNo : 1;
    /** MSR_IA32_ARCH_CAPABILITIES[10]: MISC_PACKAGE_CTRLS (MSR: IA32_UARCH_MISC_CTL) */
    uint32_t        fArchMiscPackageCtrls : 1;
    /** MSR_IA32_ARCH_CAPABILITIES[11]: ENERGY_FILTERING_CTL (MSR: IA32_MISC_PACKAGE_CTLS[0]) */
    uint32_t        fArchEnergyFilteringCtl : 1;
    /** MSR_IA32_ARCH_CAPABILITIES[12]: DOITM (MSR: IA32_UARCH_MISC_CTL[0]) */
    uint32_t        fArchDoitm : 1;
    /** MSR_IA32_ARCH_CAPABILITIES[13]: SBDR_SSDP_NO - No Shared Buffers Data Read
     * nor Sideband Stale Data Propagator issues. */
    uint32_t        fArchSbdrSsdpNo : 1;
    /** MSR_IA32_ARCH_CAPABILITIES[14]: FBSDP_NO - Fill Buffer Stale Data Propagator */
    uint32_t        fArchFbsdpNo : 1;
    /** MSR_IA32_ARCH_CAPABILITIES[15]: PSDP_NO - Primary Stale Data Propagator */
    uint32_t        fArchPsdpNo : 1;
    /** MSR_IA32_ARCH_CAPABILITIES[17]: FB_CLEAR (VERW) */
    uint32_t        fArchFbClear : 1;
    /** MSR_IA32_ARCH_CAPABILITIES[18]: FB_CLEAR_CTRL (MSR: IA32_MCU_OPT_CTRL[3]) */
    uint32_t        fArchFbClearCtrl : 1;
    /** MSR_IA32_ARCH_CAPABILITIES[19]: RRSBA  */
    uint32_t        fArchRrsba : 1;
    /** MSR_IA32_ARCH_CAPABILITIES[20]: BHI_NO */
    uint32_t        fArchBhiNo : 1;
    /** MSR_IA32_ARCH_CAPABILITIES[21]: XAPIC_DISABLE_STATUS (MSR: IA32_XAPIC_DISABLE_STATUS ) */
    uint32_t        fArchXapicDisableStatus : 1;
    /** MSR_IA32_ARCH_CAPABILITIES[23]: OVERCLOCKING_STATUS (MSR: IA32_OVERCLOCKING STATUS) */
    uint32_t        fArchOverclockingStatus : 1;
    /** MSR_IA32_ARCH_CAPABILITIES[24]: PBRSB_NO - No post-barrier Return Stack Buffer predictions */
    uint32_t        fArchPbrsbNo : 1;
    /** MSR_IA32_ARCH_CAPABILITIES[25]: GDS_CTRL (MSR: IA32_MCU_OPT_CTRL[5:4]) */
    uint32_t        fArchGdsCtrl : 1;
    /** MSR_IA32_ARCH_CAPABILITIES[26]: GDS_NO - No Gather Data Sampling  */
    uint32_t        fArchGdsNo : 1;
    /** MSR_IA32_ARCH_CAPABILITIES[27]: RFDS_NO - No Register File Data Sampling */
    uint32_t        fArchRfdsNo : 1;
    /** MSR_IA32_ARCH_CAPABILITIES[28]: RFDS_CLEAR (VERW++)  */
    uint32_t        fArchRfdsClear : 1;
    /** MSR_IA32_ARCH_CAPABILITIES[29]: IGN_UMONITOR_SUPPORT (MSR: IA32_MCU_OPT_CTRL[6]) */
    uint32_t        fArchIgnUmonitorSupport : 1;
    /** MSR_IA32_ARCH_CAPABILITIES[30]: MON_UMON_MITG_SUPPORT (MSR: IA32_MCU_OPT_CTRL[7]) */
    uint32_t        fArchMonUmonMitigSupport : 1;
    /** @} */

    /** Alignment padding / reserved for future use. */
    uint32_t        fPadding0 : 17;
    uint32_t        auPadding[3];

    /** @name SVM
     * @{ */
    /** SVM: Supports Nested-paging. */
    uint32_t        fSvmNestedPaging : 1;
    /** SVM: Support LBR (Last Branch Record) virtualization. */
    uint32_t        fSvmLbrVirt : 1;
    /** SVM: Supports SVM lock. */
    uint32_t        fSvmSvmLock : 1;
    /** SVM: Supports Next RIP save. */
    uint32_t        fSvmNextRipSave : 1;
    /** SVM: Supports TSC rate MSR. */
    uint32_t        fSvmTscRateMsr : 1;
    /** SVM: Supports VMCB clean bits. */
    uint32_t        fSvmVmcbClean : 1;
    /** SVM: Supports Flush-by-ASID. */
    uint32_t        fSvmFlusbByAsid : 1;
    /** SVM: Supports decode assist. */
    uint32_t        fSvmDecodeAssists : 1;
    /** SVM: Supports Pause filter. */
    uint32_t        fSvmPauseFilter : 1;
    /** SVM: Supports Pause filter threshold. */
    uint32_t        fSvmPauseFilterThreshold : 1;
    /** SVM: Supports AVIC (Advanced Virtual Interrupt Controller). */
    uint32_t        fSvmAvic : 1;
    /** SVM: Supports Virtualized VMSAVE/VMLOAD. */
    uint32_t        fSvmVirtVmsaveVmload : 1;
    /** SVM: Supports VGIF (Virtual Global Interrupt Flag). */
    uint32_t        fSvmVGif : 1;
    /** SVM: Supports GMET (Guest Mode Execute Trap Extension). */
    uint32_t        fSvmGmet : 1;
    /** SVM: Supports AVIC in x2APIC mode. */
    uint32_t        fSvmX2Avic : 1;
    /** SVM: Supports SSSCheck (SVM Supervisor Shadow Stack). */
    uint32_t        fSvmSSSCheck : 1;
    /** SVM: Supports SPEC_CTRL virtualization. */
    uint32_t        fSvmSpecCtrl : 1;
    /** SVM: Supports Read-Only Guest Page Table feature. */
    uint32_t        fSvmRoGpt : 1;
    /** SVM: Supports HOST_MCE_OVERRIDE. */
    uint32_t        fSvmHostMceOverride : 1;
    /** SVM: Supports TlbiCtl (INVLPGB/TLBSYNC in VMCB and TLBSYNC intercept). */
    uint32_t        fSvmTlbiCtl : 1;
     /** SVM: Supports NMI virtualization. */
    uint32_t        fSvmVNmi : 1;
    /** SVM: Supports IBS virtualizaiton. */
    uint32_t        fSvmIbsVirt : 1;
    /** SVM: Supports Extended LVT AVIC access changes. */
    uint32_t        fSvmExtLvtAvicAccessChg : 1;
    /** SVM: Supports Guest VMCB address check. */
    uint32_t        fSvmNstVirtVmcbAddrChk : 1;
    /** SVM: Supports Bus Lock Threshold. */
    uint32_t        fSvmBusLockThreshold : 1;
    /** SVM: Padding / reserved for future features (64 bits total w/ max ASID). */
    uint32_t        fSvmPadding0 : 7;
    /** SVM: Maximum supported ASID. */
    uint32_t        uSvmMaxAsid;
    /** @} */


    /** VMX: Maximum physical address width. */
    uint32_t        cVmxMaxPhysAddrWidth : 8;

    /** @name VMX basic controls.
     * @{ */
    /** VMX: Supports INS/OUTS VM-exit instruction info. */
    uint32_t        fVmxInsOutInfo : 1;
    /** @} */

    /** @name VMX Pin-based controls.
     * @{ */
    /** VMX: Supports external interrupt VM-exit. */
    uint32_t        fVmxExtIntExit : 1;
    /** VMX: Supports NMI VM-exit. */
    uint32_t        fVmxNmiExit : 1;
    /** VMX: Supports Virtual NMIs. */
    uint32_t        fVmxVirtNmi : 1;
    /** VMX: Supports preemption timer. */
    uint32_t        fVmxPreemptTimer : 1;
    /** VMX: Supports posted interrupts. */
    uint32_t        fVmxPostedInt : 1;
    /** @} */

    /** @name VMX Processor-based controls.
     * @{ */
    /** VMX: Supports Interrupt-window exiting. */
    uint32_t        fVmxIntWindowExit : 1;
    /** VMX: Supports TSC offsetting. */
    uint32_t        fVmxTscOffsetting : 1;
    /** VMX: Supports HLT exiting. */
    uint32_t        fVmxHltExit : 1;
    /** VMX: Supports INVLPG exiting. */
    uint32_t        fVmxInvlpgExit : 1;
    /** VMX: Supports MWAIT exiting. */
    uint32_t        fVmxMwaitExit : 1;
    /** VMX: Supports RDPMC exiting. */
    uint32_t        fVmxRdpmcExit : 1;
    /** VMX: Supports RDTSC exiting. */
    uint32_t        fVmxRdtscExit : 1;
    /** VMX: Supports CR3-load exiting. */
    uint32_t        fVmxCr3LoadExit : 1;
    /** VMX: Supports CR3-store exiting. */
    uint32_t        fVmxCr3StoreExit : 1;
    /** VMX: Supports tertiary processor-based VM-execution controls. */
    uint32_t        fVmxTertiaryExecCtls : 1;
    /** VMX: Supports CR8-load exiting. */
    uint32_t        fVmxCr8LoadExit : 1;
    /** VMX: Supports CR8-store exiting. */
    uint32_t        fVmxCr8StoreExit : 1;
    /** VMX: Supports TPR shadow. */
    uint32_t        fVmxUseTprShadow : 1;
    /** VMX: Supports NMI-window exiting. */
    uint32_t        fVmxNmiWindowExit : 1;
    /** VMX: Supports Mov-DRx exiting. */
    uint32_t        fVmxMovDRxExit : 1;
    /** VMX: Supports Unconditional I/O exiting. */
    uint32_t        fVmxUncondIoExit : 1;
    /** VMX: Supportgs I/O bitmaps. */
    uint32_t        fVmxUseIoBitmaps : 1;
    /** VMX: Supports Monitor Trap Flag. */
    uint32_t        fVmxMonitorTrapFlag : 1;
    /** VMX: Supports MSR bitmap. */
    uint32_t        fVmxUseMsrBitmaps : 1;
    /** VMX: Supports MONITOR exiting. */
    uint32_t        fVmxMonitorExit : 1;
    /** VMX: Supports PAUSE exiting. */
    uint32_t        fVmxPauseExit : 1;
    /** VMX: Supports secondary processor-based VM-execution controls. */
    uint32_t        fVmxSecondaryExecCtls : 1;
    /** @} */

    /** @name VMX Secondary processor-based controls.
     * @{ */
    /** VMX: Supports virtualize-APIC access. */
    uint32_t        fVmxVirtApicAccess : 1;
    /** VMX: Supports EPT (Extended Page Tables). */
    uint32_t        fVmxEpt : 1;
    /** VMX: Supports descriptor-table exiting. */
    uint32_t        fVmxDescTableExit : 1;
    /** VMX: Supports RDTSCP. */
    uint32_t        fVmxRdtscp : 1;
    /** VMX: Supports virtualize-x2APIC mode. */
    uint32_t        fVmxVirtX2ApicMode : 1;
    /** VMX: Supports VPID. */
    uint32_t        fVmxVpid : 1;
    /** VMX: Supports WBIND exiting. */
    uint32_t        fVmxWbinvdExit : 1;
    /** VMX: Supports Unrestricted guest. */
    uint32_t        fVmxUnrestrictedGuest : 1;
    /** VMX: Supports APIC-register virtualization. */
    uint32_t        fVmxApicRegVirt : 1;
    /** VMX: Supports virtual-interrupt delivery. */
    uint32_t        fVmxVirtIntDelivery : 1;
    /** VMX: Supports Pause-loop exiting. */
    uint32_t        fVmxPauseLoopExit : 1;
    /** VMX: Supports RDRAND exiting. */
    uint32_t        fVmxRdrandExit : 1;
    /** VMX: Supports INVPCID. */
    uint32_t        fVmxInvpcid : 1;
    /** VMX: Supports VM functions. */
    uint32_t        fVmxVmFunc : 1;
    /** VMX: Supports VMCS shadowing. */
    uint32_t        fVmxVmcsShadowing : 1;
    /** VMX: Supports RDSEED exiting. */
    uint32_t        fVmxRdseedExit : 1;
    /** VMX: Supports PML. */
    uint32_t        fVmxPml : 1;
    /** VMX: Supports EPT-violations \#VE. */
    uint32_t        fVmxEptXcptVe : 1;
    /** VMX: Supports conceal VMX from PT. */
    uint32_t        fVmxConcealVmxFromPt : 1;
    /** VMX: Supports XSAVES/XRSTORS. */
    uint32_t        fVmxXsavesXrstors : 1;
    /** VMX: Supports PASID translation. */
    uint32_t        fVmxPasidTranslate : 1;
    /** VMX: Supports mode-based execute control for EPT. */
    uint32_t        fVmxModeBasedExecuteEpt : 1;
    /** VMX: Supports sub-page write permissions for EPT. */
    uint32_t        fVmxSppEpt : 1;
    /** VMX: Supports Intel PT to output guest-physical addresses for EPT. */
    uint32_t        fVmxPtEpt : 1;
    /** VMX: Supports TSC scaling. */
    uint32_t        fVmxUseTscScaling : 1;
    /** VMX: Supports TPAUSE, UMONITOR, or UMWAIT. */
    uint32_t        fVmxUserWaitPause : 1;
    /** VMX: Supports PCONFIG. */
    uint32_t        fVmxPconfig : 1;
    /** VMX: Supports enclave (ENCLV) exiting. */
    uint32_t        fVmxEnclvExit : 1;
    /** VMX: Supports VMM bus-lock detection. */
    uint32_t        fVmxBusLockDetect : 1;
    /** VMX: Supports instruction timeout. */
    uint32_t        fVmxInstrTimeout : 1;
    /** @} */

    /** @name VMX Tertiary processor-based controls.
     * @{ */
    /** VMX: Supports LOADIWKEY exiting. */
    uint32_t        fVmxLoadIwKeyExit : 1;
    /** VMX: Supports hypervisor-managed linear address translation (HLAT). */
    uint32_t        fVmxHlat : 1;
    /** VMX: Supports EPT paging-write control. */
    uint32_t        fVmxEptPagingWrite : 1;
    /** VMX: Supports Guest-paging verification. */
    uint32_t        fVmxGstPagingVerify : 1;
    /** VMX: Supports IPI virtualization. */
    uint32_t        fVmxIpiVirt : 1;
    /** VMX: Supports virtualize IA32_SPEC_CTRL. */
    uint32_t        fVmxVirtSpecCtrl : 1;
    /** @} */

    /** @name VMX VM-entry controls.
     * @{ */
    /** VMX: Supports load-debug controls on VM-entry. */
    uint32_t        fVmxEntryLoadDebugCtls : 1;
    /** VMX: Supports IA32e mode guest. */
    uint32_t        fVmxIa32eModeGuest : 1;
    /** VMX: Supports load guest EFER MSR on VM-entry. */
    uint32_t        fVmxEntryLoadEferMsr : 1;
    /** VMX: Supports load guest PAT MSR on VM-entry. */
    uint32_t        fVmxEntryLoadPatMsr : 1;
    /** @} */

    /** @name VMX VM-exit controls.
     * @{ */
    /** VMX: Supports save debug controls on VM-exit. */
    uint32_t        fVmxExitSaveDebugCtls : 1;
    /** VMX: Supports host-address space size. */
    uint32_t        fVmxHostAddrSpaceSize : 1;
    /** VMX: Supports acknowledge external interrupt on VM-exit. */
    uint32_t        fVmxExitAckExtInt : 1;
    /** VMX: Supports save guest PAT MSR on VM-exit. */
    uint32_t        fVmxExitSavePatMsr : 1;
    /** VMX: Supports load hsot PAT MSR on VM-exit. */
    uint32_t        fVmxExitLoadPatMsr : 1;
    /** VMX: Supports save guest EFER MSR on VM-exit. */
    uint32_t        fVmxExitSaveEferMsr : 1;
    /** VMX: Supports load host EFER MSR on VM-exit. */
    uint32_t        fVmxExitLoadEferMsr : 1;
    /** VMX: Supports save VMX preemption timer on VM-exit. */
    uint32_t        fVmxSavePreemptTimer : 1;
    /** VMX: Supports secondary VM-exit controls. */
    uint32_t        fVmxSecondaryExitCtls : 1;
    /** @} */

    /** @name VMX Miscellaneous data.
     * @{ */
    /** VMX: Supports storing EFER.LMA into IA32e-mode guest field on VM-exit. */
    uint32_t        fVmxExitSaveEferLma : 1;
    /** VMX: Whether Intel PT (Processor Trace) is supported in VMX mode or not. */
    uint32_t        fVmxPt : 1;
    /** VMX: Supports VMWRITE to any valid VMCS field incl. read-only fields, otherwise
     *  VMWRITE cannot modify read-only VM-exit information fields. */
    uint32_t        fVmxVmwriteAll : 1;
    /** VMX: Supports injection of software interrupts, ICEBP on VM-entry for zero
     *  length instructions. */
    uint32_t        fVmxEntryInjectSoftInt : 1;
    /** @} */

    /** VMX: Padding / reserved for future features. */
    uint32_t        fVmxPadding0 : 7;
    /** VMX: Padding / reserved for future, making it a total of 128 bits.  */
    uint32_t        fVmxPadding1;
} CPUMFEATURESX86;
#ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(CPUMFEATURESX86, 64);
AssertCompileMembersAtSameOffset(CPUMFEATURESCOMMON, enmCpuVendor,          CPUMFEATURESX86, enmCpuVendor);
AssertCompileMembersAtSameOffset(CPUMFEATURESCOMMON, enmMicroarch,          CPUMFEATURESX86, enmMicroarch);
AssertCompileMembersAtSameOffset(CPUMFEATURESCOMMON, cMaxPhysAddrWidth,     CPUMFEATURESX86, cMaxPhysAddrWidth);
AssertCompileMembersAtSameOffset(CPUMFEATURESCOMMON, cMaxLinearAddrWidth,   CPUMFEATURESX86, cMaxLinearAddrWidth);
#endif

/**
 * CPU features and quirks for ARMv8.
 *
 * This is mostly exploded CPU feature register info.
 */
typedef struct CPUMFEATURESARMV8
{
    /** The microarchitecture. */
#ifndef VBOX_FOR_DTRACE_LIB
    CPUMMICROARCH   enmMicroarch;
#else
    uint32_t        enmMicroarch;
#endif
    /** The CPU vendor (CPUMCPUVENDOR). */
    uint8_t         enmCpuVendor;
    /** The maximum physical address width of the CPU. */
    uint8_t         cMaxPhysAddrWidth;
    /** The maximum linear address width of the CPU. */
    uint8_t         cMaxLinearAddrWidth;

    /** The CPU implementer value (from MIDR_EL1). */
    uint8_t         uImplementeter;
    /** The CPU part number (from MIDR_EL1). */
    uint16_t        uPartNum;
    /** The CPU variant (from MIDR_EL1). */
    uint8_t         uVariant;
    /** The CPU revision (from MIDR_EL1). */
    uint8_t         uRevision;

    /** Supports AArch64 (FEAT_AA64). */
    uint32_t        fAa64 : 1;
    /** Supports AArch64 in EL0 (FEAT_AA64EL0). */
    uint32_t        fAa64El0 : 1;
    /** Supports AArch64 in EL1 (FEAT_AA64EL1). */
    uint32_t        fAa64El1 : 1;
    /** Supports AArch64 in EL2 (FEAT_AA64EL2). */
    uint32_t        fAa64El2 : 1;
    /** Supports AArch64 in EL3 (FEAT_AA64EL3). */
    uint32_t        fAa64El3 : 1;

    /** Supports AArch32 (FEAT_AA32). */
    uint32_t        fAa32 : 1;
    /** Supports AArch32 in EL0 (FEAT_AA32EL0). */
    uint32_t        fAa32El0 : 1;
    /** Supports AArch32 in EL1 (FEAT_AA32EL1). */
    uint32_t        fAa32El1 : 1;
    /** Supports AArch32 in EL2 (FEAT_AA32EL2). */
    uint32_t        fAa32El2 : 1;
    /** Supports AArch32 in EL3 (FEAT_AA32EL3). */
    uint32_t        fAa32El3 : 1;

    /** @name Granule sizes supported.
     * @{ */
    /** 4KiB stage 1 translation granule size supported (FEAT_TGran4K). */
    uint32_t        fTGran4K : 1;
    /** 16KiB stage 1 translation granule size supported (FEAT_TGran16K). */
    uint32_t        fTGran16K : 1;
    /** 64KiB stage translation granule size supported (FEAT_TGran64K). */
    uint32_t        fTGran64K : 1;

    /** 4KiB stage 2 translation granule size supported (FEAT_S2TGran4K). */
    uint32_t        fS2TGran4K : 1;
    /** 16KiB stage 2 translation granule size supported (FEAT_S2TGran16K). */
    uint32_t        fS2TGran16K : 1;
    /** 64KiB stage 2ranslation granule size supported (FEAT_S2TGran64K). */
    uint32_t        fS2TGran64K : 1;
    /** @} */

    /** @name pre-2020 Architecture Extensions.
     * @{ */
    /** Supports Advanced SIMD Extension (FEAT_AdvSIMD). */
    uint32_t        fAdvSimd : 1;
    /** Supports Advanced SIMD AES instructions (FEAT_AES). */
    uint32_t        fAes : 1;
    /** Supports 16 bit ASID (FEAT_ASID16). */
    uint32_t        fAsid16 : 1;
    /** Supports Advanced SIMD PMULL instructions (FEAT_PMULL). */
    uint32_t        fPmull : 1;
#if 0 /* AArch32 EL3 stuff which doesn't seem to be detectable outside CPU specs...  */
    /** Supports CP15SDisable2 (_FEAT_CP15SDISABLE2). */
    uint32_t        fCp15SDisable2 : 1;
#endif
    /** Supports Cache Speculation Variant 2 (FEAT_CSV2). */
    uint32_t        fCsv2 : 1;
    /** Supports Cache Speculation Variant 2, version 1.1 (FEAT_CSV2_1p1). */
    uint32_t        fCsv21p1 : 1;
    /** Supports Cache Speculation Variant 2, version 1.2 (FEAT_CSV2_1p2). */
    uint32_t        fCsv21p2 : 1;
    /** Supports Cache Speculation Variant 3 (FEAT_CSV3). */
    uint32_t        fCsv3 : 1;
    /** Supports Data Gahtering Hint (FEAT_DGH). */
    uint32_t        fDgh : 1;
    /** Supports Double Lock (FEAT_DoubleLock). */
    uint32_t        fDoubleLock : 1;
    /** Supports Enhanced Translation Synchronization (FEAT_ETS2). */
    uint32_t        fEts2 : 1;
    /** Supports Floating Point Extensions (FEAT_FP). */
    uint32_t        fFp : 1;
    /** Supports IVIPT Extensions (FEAT_IVIPT). */
    uint32_t        fIvipt : 1;
#if 0 /* difficult to detect (external reg) */
    /** Supports PC Sample-based Profiling Extension (_FEAT_PCSRv8). */
    uint32_t        fPcsrV8 : 1;
#endif
    /** Supports Speculation Restrictions instructions (FEAT_SPECRES). */
    uint32_t        fSpecres : 1;
    /** Supports Reliability, Availability, and Serviceability (RAS) Extension (FEAT_RAS). */
    uint32_t        fRas : 1;
    /** Supports Speculation Barrier (FEAT_SB). */
    uint32_t        fSb : 1;
    /** Supports Advanced SIMD SHA1 instructions (FEAT_SHA1). */
    uint32_t        fSha1 : 1;
    /** Supports Advanced SIMD SHA256 instructions (FEAT_SHA256). */
    uint32_t        fSha256 : 1;
    /** Supports Speculation Store Bypass Safe (FEAT_SSBS). */
    uint32_t        fSsbs : 1;
    /** Supports MRS and MSR instructions for Speculation Store Bypass Safe version 2 (FEAT_SSBS2). */
    uint32_t        fSsbs2 : 1;
    /** Supports CRC32 instructions (FEAT_CRC32). */
    uint32_t        fCrc32 : 1;
    /** Supports intermediate caching of translation table walks (FEAT_nTLBPA). */
    uint32_t        fNTlbpa : 1;
    /** Supports debug with VHE (FEAT_Debugv8p1). */
    uint32_t        fDebugV8p1 : 1;
    /** Supports Hierarchical permission disables in translation tables (FEAT_HPDS). */
    uint32_t        fHpds : 1;
    /** Supports Limited ordering regions (FEAT_LOR). */
    uint32_t        fLor : 1;
    /** Supports Lare Systems Extensons (FEAT_LSE). */
    uint32_t        fLse : 1;
    /** Supports Privileged access never (FEAT_PAN). */
    uint32_t        fPan : 1;
    /** Supports Armv8.1 PMU extensions (FEAT_PMUv3p1). */
    uint32_t        fPmuV3p1 : 1;
    /** Supports Advanced SIMD rouding double multiply accumulate instructions (FEAT_RDM). */
    uint32_t        fRdm : 1;
    /** Supports hardware management of the Access flag and dirty state (FEAT_HAFDBS). */
    uint32_t        fHafdbs : 1;
    /** Supports Virtualization Host Extensions (FEAT_VHE). */
    uint32_t        fVhe : 1;
    /** Supports 16-bit VMID (FEAT_VMID16). */
    uint32_t        fVmid16 : 1;
    /** Supports AArch32 BFloat16 instructions (FEAT_AA32BF16). */
    uint32_t        fAa32Bf16 : 1;
    /** Supports AArch32 Hierarchical permission disables (FEAT_AA32HPD). */
    uint32_t        fAa32Hpd : 1;
    /** Supports AArch32 Int8 matrix multiplication instructions (FEAT_AA32I8MM). */
    uint32_t        fAa32I8mm : 1;
    /** Supports AT S1E1R and AT S1E1W instruction variants affected by PSTATE.PAN (FEAT_PAN2). */
    uint32_t        fPan2 : 1;
    /** Supports AArch64 BFloat16 instructions (FEAT_BF16). */
    uint32_t        fBf16 : 1;
    /** Supports DC CVADP instruction (FEAT_DPB2). */
    uint32_t        fDpb2 : 1;
    /** Supports DC VAP instruction (FEAT_DPB). */
    uint32_t        fDpb : 1;
    /** Supports Debug v8.2 (FEAT_Debugv8p2). */
    uint32_t        fDebugV8p2 : 1;
    /** Supports Advanced SIMD dot product instructions (FEAT_DotProd). */
    uint32_t        fDotProd : 1;
    /** Supports Enhanced Virtualization Traps (FEAT_EVT). */
    uint32_t        fEvt : 1;
    /** Supports Single precision Matrix Multiplication (FEAT_F32MM). */
    uint32_t        fF32mm : 1;
    /** Supports Double precision Matrix Multiplication (FEAT_F64MM). */
    uint32_t        fF64mm : 1;
    /** Supports Floating-point half precision multiplication instructions (FEAT_FHM). */
    uint32_t        fFhm : 1;
    /** Supports Half-precision floating point data processing (FEAT_FP16). */
    uint32_t        fFp16 : 1;
    /** Supports AArch64 Int8 matrix multiplication instructions (FEAT_I8MM). */
    uint32_t        fI8mm : 1;
    /** Supports Implicit Error Synchronization event (FEAT_IESB). */
    uint32_t        fIesb : 1;
    /** Supports Large PA and IPA support (FEAT_LPA). */
    uint32_t        fLpa : 1;
    /** Supports AArch32 Load/Store Multiple instructions atomicity and ordering controls (FEAT_LSMAOC). */
    uint32_t        fLsmaoc : 1;
    /** Supports Large VA support (FEAT_LVA). */
    uint32_t        fLva : 1;
    /** Supports Memory Partitioning and Monitoring Extension (FEAT_MPAM). */
    uint32_t        fMpam : 1;
#if 0 /* difficult to detect (external reg) */
    /** Supports PC Sample-based Profiling Extension, version 8.2 (_FEAT_PCSRv8p2). */
    uint32_t        fPcsrV8p2 : 1;
#endif
    /** Supports Advanced SIMD SHA3 instructions (FEAT_SHA3). */
    uint32_t        fSha3 : 1;
    /** Supports Advanced SIMD SHA512 instructions (FEAT_SHA512). */
    uint32_t        fSha512 : 1;
    /** Supports Advanced SIMD SM3 instructions (FEAT_SM3). */
    uint32_t        fSm3 : 1;
    /** Supports Advanced SIMD SM4 instructions (FEAT_SM4). */
    uint32_t        fSm4 : 1;
    /** Supports Statistical Profiling Extension (FEAT_SPE). */
    uint32_t        fSpe : 1;
    /** Supports Scalable Vector Extension (FEAT_SVE). */
    uint32_t        fSve : 1;
    /** Supports Translation Table Common not private translations (FEAT_TTCNP). */
    uint32_t        fTtcnp : 1;
    /** Supports Hierarchical permission disables, version 2 (FEAT_HPDS2). */
    uint32_t        fHpds2 : 1;
    /** Supports Translation table stage 2 Unprivileged Execute-never (FEAT_XNX). */
    uint32_t        fXnx : 1;
    /** Supports Unprivileged Access Override control (FEAT_UAO). */
    uint32_t        fUao : 1;
    /** Supports VMID-aware PIPT instruction cache (FEAT_VPIPT). */
    uint32_t        fVpipt : 1;
    /** Supports Extended cache index (FEAT_CCIDX). */
    uint32_t        fCcidx : 1;
    /** Supports Floating-point complex number instructions (FEAT_FCMA). */
    uint32_t        fFcma : 1;
#if 0 /* difficult to detect (external reg) */
    /** Supports Debug over Powerdown (_FEAT_DoPD). */
    uint32_t        fDopd : 1;
#endif
    /** Supports Enhanced pointer authentication (FEAT_EPAC). */
    uint32_t        fEpac : 1;
    /** Supports Faulting on AUT* instructions (FEAT_FPAC). */
    uint32_t        fFpac : 1;
    /** Supports Faulting on combined pointer euthentication instructions (FEAT_FPACCOMBINE). */
    uint32_t        fFpacCombine : 1;
    /** Supports JavaScript conversion instructions (FEAT_JSCVT). */
    uint32_t        fJscvt : 1;
    /** Supports Load-Acquire RCpc instructions (FEAT_LRCPC). */
    uint32_t        fLrcpc : 1;
    /** Supports Nexted Virtualization (FEAT_NV). */
    uint32_t        fNv : 1;
    /** Supports QARMA5 pointer authentication algorithm (FEAT_PACQARMA5). */
    uint32_t        fPacQarma5 : 1;
    /** Supports implementation defined pointer authentication algorithm (FEAT_PACIMP). */
    uint32_t        fPacImp : 1;
    /** Supports Pointer authentication (FEAT_PAuth). */
    uint32_t        fPAuth : 1;
    /** Supports Enhancements to pointer authentication (FEAT_PAuth2). */
    uint32_t        fPAuth2 : 1;
    /** Supports Statistical Profiling Extensions version 1.1 (FEAT_SPEv1p1). */
    uint32_t        fSpeV1p1 : 1;
    /** Supports Activity Monitor Extension, version 1 (FEAT_AMUv1). */
    uint32_t        fAmuV1 : 1;
#if 0 /* difficult to detect (external reg) */
    /** Supports Generic Counter Scaling (_FEAT_CNTSC). */
    uint32_t        fCntsc : 1;
#endif
    /** Supports Debug v8.4 (FEAT_Debugv8p4). */
    uint32_t        fDebugV8p4 : 1;
    /** Supports Double Fault Extension (FEAT_DoubleFault). */
    uint32_t        fDoubleFault : 1;
    /** Supports Data Independent Timing instructions (FEAT_DIT). */
    uint32_t        fDit : 1;
    /** Supports Condition flag manipulation isntructions (FEAT_FlagM). */
    uint32_t        fFlagM : 1;
    /** Supports ID space trap handling (FEAT_IDST). */
    uint32_t        fIdst : 1;
    /** Supports Load-Acquire RCpc instructions version 2 (FEAT_LRCPC2). */
    uint32_t        fLrcpc2 : 1;
    /** Supports Large Sytem Extensions version 2 (FEAT_LSE2). */
    uint32_t        fLse2 : 1;
    /** Supports Enhanced nested virtualization support (FEAT_NV2). */
    uint32_t        fNv2 : 1;
    /** Supports Armv8.4 PMU Extensions (FEAT_PMUv3p4). */
    uint32_t        fPmuV3p4 : 1;
    /** Supports RAS Extension v1.1 (FEAT_RASv1p1). */
    uint32_t        fRasV1p1 : 1;
    /** Supports RAS Extension v1.1 System Architecture (FEAT_RASSAv1p1). */
    uint32_t        fRassaV1p1 : 1;
    /** Supports Stage 2 forced Write-Back (FEAT_S2FWB). */
    uint32_t        fS2Fwb : 1;
    /** Supports Secure El2 (FEAT_SEL2). */
    uint32_t        fSecEl2 : 1;
    /** Supports TLB invalidate instructions on Outer Shareable domain (FEAT_TLBIOS). */
    uint32_t        fTlbios : 1;
    /** Supports TLB invalidate range instructions (FEAT_TLBIRANGE). */
    uint32_t        fTlbirange : 1;
    /** Supports Self-hosted Trace Extensions (FEAT_TRF). */
    uint32_t        fTrf : 1;
    /** Supports Translation Table Level (FEAT_TTL). */
    uint32_t        fTtl : 1;
    /** Supports Translation table break-before-make levels (FEAT_BBM). */
    uint32_t        fBbm : 1;
    /** Supports Small translation tables (FEAT_TTST). */
    uint32_t        fTtst : 1;
    /** Supports Branch Target Identification (FEAT_BTI). */
    uint32_t        fBti : 1;
    /** Supports Enhancements to flag manipulation instructions (FEAT_FlagM2). */
    uint32_t        fFlagM2 : 1;
    /** Supports Context synchronization and exception handling (FEAT_ExS). */
    uint32_t        fExs : 1;
    /** Supports Preenting EL0 access to halves of address maps (FEAT_E0PD). */
    uint32_t        fE0Pd : 1;
    /** Supports Floating-point to integer instructions (FEAT_FRINTTS). */
    uint32_t        fFrintts : 1;
    /** Supports Guest translation granule size (FEAT_GTG). */
    uint32_t        fGtg : 1;
    /** Supports Instruction-only Memory Tagging Extension (FEAT_MTE). */
    uint32_t        fMte : 1;
    /** Supports memory Tagging Extension version 2 (FEAT_MTE2). */
    uint32_t        fMte2 : 1;
    /** Supports Armv8.5 PMU Extensions (FEAT_PMUv3p5). */
    uint32_t        fPmuV3p5 : 1;
    /** Supports Random number generator (FEAT_RNG). */
    uint32_t        fRng : 1;
    /** Supports AMU Extensions version 1.1 (FEAT_AMUv1p1). */
    uint32_t        fAmuV1p1 : 1;
    /** Supports Enhanced Counter Virtualization (FEAT_ECV). */
    uint32_t        fEcv : 1;
    /** Supports Fine Grain Traps (FEAT_FGT). */
    uint32_t        fFgt : 1;
    /** Supports Memory Partitioning and Monitoring version 0.1 (FEAT_MPAMv0p1). */
    uint32_t        fMpamV0p1 : 1;
    /** Supports Memory Partitioning and Monitoring version 1.1 (FEAT_MPAMv1p1). */
    uint32_t        fMpamV1p1 : 1;
    /** Supports Multi-threaded PMU Extensions (FEAT_MTPMU). */
    uint32_t        fMtPmu : 1;
    /** Supports Delayed Trapping of WFE (FEAT_TWED). */
    uint32_t        fTwed : 1;
    /** Supports Embbedded Trace Macrocell version 4 (FEAT_ETMv4). */
    uint32_t        fEtmV4 : 1;
    /** Supports Embbedded Trace Macrocell version 4.1 (FEAT_ETMv4p1). */
    uint32_t        fEtmV4p1 : 1;
    /** Supports Embbedded Trace Macrocell version 4.2 (FEAT_ETMv4p2). */
    uint32_t        fEtmV4p2 : 1;
    /** Supports Embbedded Trace Macrocell version 4.3 (FEAT_ETMv4p3). */
    uint32_t        fEtmV4p3 : 1;
    /** Supports Embbedded Trace Macrocell version 4.4 (FEAT_ETMv4p4). */
    uint32_t        fEtmV4p4 : 1;
    /** Supports Embbedded Trace Macrocell version 4.5 (FEAT_ETMv4p5). */
    uint32_t        fEtmV4p5 : 1;
    /** Supports Embbedded Trace Macrocell version 4.6 (FEAT_ETMv4p6). */
    uint32_t        fEtmV4p6 : 1;
    /** Supports Generic Interrupt Controller version 3 (FEAT_GICv3). */
    uint32_t        fGicV3 : 1;
    /** Supports Generic Interrupt Controller version 3.1 (FEAT_GICv3p1). */
    uint32_t        fGicV3p1 : 1;
    /** Supports Trapping Non-secure EL1 writes to ICV_DIR (FEAT_GICv3_TDIR). */
    uint32_t        fGicV3Tdir : 1;
    /** Supports Generic Interrupt Controller version 4 (FEAT_GICv4). */
    uint32_t        fGicV4 : 1;
    /** Supports Generic Interrupt Controller version 4.1 (FEAT_GICv4p1). */
    uint32_t        fGicV4p1 : 1;
    /** Supports PMU extension, version 3 (FEAT_PMUv3). */
    uint32_t        fPmuV3 : 1;
    /** Supports Embedded Trace Extension (FEAT_ETE). */
    uint32_t        fEte : 1;
    /** Supports Embedded Trace Extension, version 1.1 (FEAT_ETEv1p1). */
    uint32_t        fEteV1p1 : 1;
    /** Supports Embedded Trace Extension, version 1.2 (FEAT_ETEv1p2). */
    uint32_t        fEteV1p2 : 1;
    /** Supports Scalable Vector Extension version 2 (FEAT_SVE2). */
    uint32_t        fSve2 : 1;
    /** Supports Scalable Vector AES instructions (FEAT_SVE_AES). */
    uint32_t        fSveAes : 1;
    /** Supports Scalable Vector PMULL instructions (FEAT_SVE_PMULL128). */
    uint32_t        fSvePmull128 : 1;
    /** Supports Scalable Vector Bit Permutes instructions (FEAT_SVE_BitPerm). */
    uint32_t        fSveBitPerm : 1;
    /** Supports Scalable Vector SHA3 instructions (FEAT_SVE_SHA3). */
    uint32_t        fSveSha3 : 1;
    /** Supports Scalable Vector SM4 instructions (FEAT_SVE_SM4). */
    uint32_t        fSveSm4 : 1;
    /** Supports Transactional Memory Extension (FEAT_TME). */
    uint32_t        fTme : 1;
    /** Supports Trace Buffer Extension (FEAT_TRBE). */
    uint32_t        fTrbe : 1;
    /** Supports Scalable Matrix Extension (FEAT_SME). */
    uint32_t        fSme : 1;
    /** Supports mixed-endian (FEAT_MixedEnd). */
    uint32_t        fMixedEnd : 1;
    /** Supports mixed-endian at EL0 (FEAT_MixedEndEL0). */
    uint32_t        fMixedEndEl0 : 1;
    /** Supports enhanced translation synchronization (FEAT_ETS3). */
    uint32_t        fEts3 : 1;
    /** Supports SError interrupt exceptions from speculative reads for memory (FEAT_SpecSEI). */
    uint32_t        fSpecSei : 1;
    /** Supports speculative behaviour of pointer authentication (FEAT_FPACC_SPEC). */
    uint32_t        fFpaccSpec : 1;
    /** Supports cache speculation variant 2 version 2 (FEAT_CSV2_2). */
    uint32_t        fCvs2_2 : 1;
    /** Supports programming of HCR_EL2.E2H (FEAT_E2H0). */
    uint32_t        fE2H0 : 1;
    /** @} */

    /** @name 2020 Architecture Extensions.
     * @{ */
    /** Supports Alternate floating-point behavior (FEAT_AFP). */
    uint32_t        fAfp : 1;
    /** Supports HCRX_EL2 register (FEAT_HCX). */
    uint32_t        fHcx : 1;
    /** Supports Larger phsical address for 4KiB and 16KiB translation granules (FEAT_LPA2). */
    uint32_t        fLpa2 : 1;
    /** Supports 64 byte loads and stores without return (FEAT_LS64). */
    uint32_t        fLs64 : 1;
    /** Supports 64 byte stores with return (FEAT_LS64_V). */
    uint32_t        fLs64V : 1;
    /** Supports 64 byte EL0 stores with return (FEAT_LS64_ACCDATA). */
    uint32_t        fLs64Accdata : 1;
    /** Supports MTE Asymmetric Fault Handling (FEAT_MTE3). */
    uint32_t        fMte3 : 1;
    /** Supports SCTLR_ELx.EPAN (FEAT_PAN3). */
    uint32_t        fPan3 : 1;
    /** Supports Armv8.7 PMU extensions (FEAT_PMUv3p7). */
    uint32_t        fPmuV3p7 : 1;
    /** Supports Increased precision of Reciprocal Extimate and Reciprocal Square Root Estimate (FEAT_RPRES). */
    uint32_t        fRpres : 1;
    /** Supports Realm Management Extension (FEAT_RME). */
    uint32_t        fRme : 1;
    /** Supports Full A64 instruction set support in Streaming SVE mode (FEAT_SME_FA64). */
    uint32_t        fSmeFA64 : 1;
    /** Supports Double-precision floating-point outer product instructions (FEAT_SME_F64F64). */
    uint32_t        fSmeF64F64 : 1;
    /** Supports 16-bit to 64-bit integer widening outer product instructions (FEAT_SME_I16I64). */
    uint32_t        fSmeI16I64 : 1;
    /** Supports Statistical Profiling Extensions version 1.2 (FEAT_SPEv1p2). */
    uint32_t        fSpeV1p2 : 1;
    /** Supports AArch64 Extended BFloat16 instructions (FEAT_EBF16). */
    uint32_t        fEbf16 : 1;
    /** Supports WFE and WFI instructions with timeout (FEAT_WFxT). */
    uint32_t        fWfxt : 1;
    /** Supports XS attribute (FEAT_XS). */
    uint32_t        fXs : 1;
    /** Supports branch Record Buffer Extension (FEAT_BRBE). */
    uint32_t        fBrbe : 1;
    /** @} */

    /** @name 2021 Architecture Extensions.
     * @{ */
    /** Supports Control for cache maintenance permission (FEAT_CMOW). */
    uint32_t        fCmow : 1;
    /** Supports PAC algorithm enhancement (FEAT_CONSTPACFIELD). */
    uint32_t        fConstPacField : 1;
    /** Supports Debug v8.8 (FEAT_Debugv8p8). */
    uint32_t        fDebugV8p8 : 1;
    /** Supports Hinted conditional branches (FEAT_HBC). */
    uint32_t        fHbc : 1;
    /** Supports Setting of MDCR_EL2.HPMN to zero (FEAT_HPMN0). */
    uint32_t        fHpmn0 : 1;
    /** Supports Non-Maskable Interrupts (FEAT_NMI). */
    uint32_t        fNmi : 1;
    /** Supports GIC Non-Maskable Interrupts (FEAT_GICv3_NMI). */
    uint32_t        fGicV3Nmi : 1;
    /** Supports Standardization of memory operations (FEAT_MOPS). */
    uint32_t        fMops : 1;
    /** Supports Pointer authentication - QARMA3 algorithm (FEAT_PACQARMA3). */
    uint32_t        fPacQarma3 : 1;
    /** Supports Event counting threshold (FEAT_PMUv3_TH). */
    uint32_t        fPmuV3Th : 1;
    /** Supports Armv8.8 PMU extensions (FEAT_PMUv3p8). */
    uint32_t        fPmuV3p8 : 1;
#if 0 /* difficult to detect, so ignore for now */
    /** Supports 64-bit external interface to the Performance Monitors (_FEAT_PMUv3_EXT64). */
    uint32_t        fPmuV3Ext64 : 1;
    /** Supports 32-bit external interface to the Performance Monitors (_FEAT_PMUv3_EXT32). */
    uint32_t        fPmuV3Ext32 : 1;
    /** Supports External interface to the Performance Monitors (_FEAT_PMUv3_EXT). */
    uint32_t        fPmuV3Ext : 1;
#endif
    /** Supports Trapping support for RNDR/RNDRRS (FEAT_RNG_TRAP). */
    uint32_t        fRngTrap : 1;
    /** Supports Statistical Profiling Extension version 1.3 (FEAT_SPEv1p3). */
    uint32_t        fSpeV1p3 : 1;
    /** Supports EL0 use of IMPLEMENTATION DEFINEd functionality (FEAT_TIDCP1). */
    uint32_t        fTidcp1 : 1;
    /** Supports Branch Record Buffer Extension version 1.1 (FEAT_BRBEv1p1). */
    uint32_t        fBrbeV1p1 : 1;
    /** @} */

    /** @name 2022 Architecture Extensions.
     * @{ */
    /** Supports Address Breakpoint Linking Extenions (FEAT_ABLE). */
    uint32_t        fAble : 1;
    /** Supports Asynchronous Device error exceptions (FEAT_ADERR). */
    uint32_t        fAderr : 1;
    /** Supports Memory Attribute Index Enhancement (FEAT_AIE). */
    uint32_t        fAie : 1;
    /** Supports Asynchronous Normal error exception (FEAT_ANERR). */
    uint32_t        fAnerr : 1;
    /** Supports Breakpoint Mismatch and Range Extension (FEAT_BWE). */
    uint32_t        fBwe : 1;
    /** Supports Clear Branch History instruction (FEAT_CLRBHB). */
    uint32_t        fClrBhb : 1;
    /** Supports Check Feature Status (FEAT_CHK). */
    uint32_t        fChk : 1;
    /** Supports Common Short Sequence Compression instructions (FEAT_CSSC). */
    uint32_t        fCssc : 1;
    /** Supports Cache Speculation Variant 2 version 3 (FEAT_CSV2_3). */
    uint32_t        fCsv2v3 : 1;
    /** Supports 128-bit Translation Tables, 56 bit PA (FEAT_D128). */
    uint32_t        fD128 : 1;
    /** Supports Debug v8.9 (FEAT_Debugv8p9). */
    uint32_t        fDebugV8p9 : 1;
    /** Supports Enhancements to the Double Fault Extension (FEAT_DoubleFault2). */
    uint32_t        fDoubleFault2 : 1;
    /** Supports Exception based Event Profiling (FEAT_EBEP). */
    uint32_t        fEbep : 1;
    /** Supports Exploitative control using branch history information (FEAT_ECBHB). */
    uint32_t        fEcBhb : 1;
    /** Supports for EDHSR (FEAT_EDHSR). */
    uint32_t        fEdhsr : 1;
    /** Supports Embedded Trace Extension version 1.3 (FEAT_ETEv1p3). */
    uint32_t        fEteV1p3 : 1;
    /** Supports Fine-grained traps 2 (FEAT_FGT2). */
    uint32_t        fFgt2 : 1;
    /** Supports Guarded Control Stack Extension (FEAT_GCS). */
    uint32_t        fGcs : 1;
    /** Supports Hardware managed Access Flag for Table descriptors (FEAT_HAFT). */
    uint32_t        fHaft : 1;
    /** Supports Instrumentation Extension (FEAT_ITE). */
    uint32_t        fIte : 1;
    /** Supports Load-Acquire RCpc instructions version 3 (FEAT_LRCPC3). */
    uint32_t        fLrcpc3 : 1;
    /** Supports 128-bit atomics (FEAT_LSE128). */
    uint32_t        fLse128 : 1;
    /** Supports 56-bit VA (FEAT_LVA3). */
    uint32_t        fLva3 : 1;
    /** Supports Memory Encryption Contexts (FEAT_MEC). */
    uint32_t        fMec : 1;
    /** Supports Enhanced Memory Tagging Extension (FEAT_MTE4). */
    uint32_t        fMte4 : 1;
    /** Supports FAR_ELx on a Tag Check Fault (FEAT_MTE_TAGGED_FAR). */
    uint32_t        fMteTaggedFar : 1;
    /** Supports Store only Tag checking (FEAT_MTE_STORE_ONLY). */
    uint32_t        fMteStoreOnly : 1;
    /** Supports Memory tagging with Address tagging disabled (FEAT_MTE_NO_ADDRESS_TAGS). */
    uint32_t        fMteNoAddressTags : 1;
    /** Supports Memory tagging asymmetric faults (FEAT_MTE_ASYM_FAULT). */
    uint32_t        fMteAsymFault : 1;
    /** Supports Memory Tagging asynchronous faulting (FEAT_MTE_ASYNC). */
    uint32_t        fMteAsync : 1;
#if 0 /* No documentation for this. Removed? */
    /** Supports Allocation tag access permission (_FEAT_MTE_PERM_S1). */
    uint32_t        fMtePermS1 : 1;
#endif
#if 0 /* difficult to detect (external reg) */
    /** Supports Armv8.9 PC Sample-based Profiling Extension (_FEAT_PCSRv8p9). */
    uint32_t        fPcsrV8p9 : 1;
#endif
    /** Supports Permission model enhancements (FEAT_S1PIE). */
    uint32_t        fS1Pie : 1;
    /** Supports Permission model enhancements (FEAT_S2PIE). */
    uint32_t        fS2Pie : 1;
    /** Supports Permission model enhancements (FEAT_S1POE). */
    uint32_t        fS1Poe : 1;
    /** Supports Permission model enhancements (FEAT_S2POE). */
    uint32_t        fS2Poe : 1;
    /** Supports Physical Fault Address Registers (FEAT_PFAR). */
    uint32_t        fPfar : 1;
    /** Supports Armv8.9 PMU extensions (FEAT_PMUv3p9). */
    uint32_t        fPmuV3p9 : 1;
    /** Supports PMU event edge detection (FEAT_PMUv3_EDGE). */
    uint32_t        fPmuV3Edge : 1;
    /** Supports Fixed-function instruction counter (FEAT_PMUv3_ICNTR). */
    uint32_t        fPmuV3Icntr : 1;
    /** Supports PMU Snapshot Extension (FEAT_PMUv3_SS). */
    uint32_t        fPmuV3Ss : 1;
    /** Supports SLC traget for PRFM instructions (FEAT_PRFMSLC). */
    uint32_t        fPrfmSlc : 1;
    /** Supports RAS version 2 (FEAT_RASv2). */
    uint32_t        fRasV2 : 1;
#if 0 /* difficult to detect */
    /** Supports RAS version 2 System Architecture (_FEAT_RASSAv2). */
    uint32_t        fRasSaV2 : 1;
#endif
    /** Supports for Range Prefetch Memory instruction (FEAT_RPRFM). */
    uint32_t        fRprfm : 1;
    /** Supports extensions to SCTLR_ELx (FEAT_SCTLR2). */
    uint32_t        fSctlr2 : 1;
    /** Supports Synchronous Exception-based Event Profiling (FEAT_SEBEP). */
    uint32_t        fSebep : 1;
    /** Supports non-widening half-precision FP16 to FP16 arithmetic for SME2.1 (FEAT_SME_F16F16). */
    uint32_t        fSmeF16F16 : 1;
    /** Supports Scalable Matrix Extension version 2 (FEAT_SME2). */
    uint32_t        fSme2 : 1;
    /** Supports Scalable Matrix Extension version 2.1 (FEAT_SME2p1). */
    uint32_t        fSme2p1 : 1;
    /** Supports Enhanced speculation restriction instructions (FEAT_SPECRES2). */
    uint32_t        fSpecres2 : 1;
    /** Supports System Performance Monitors Extension (FEAT_SPMU). */
    uint32_t        fSpmu : 1;
    /** Supports Statistical profiling Extension version 1.4 (FEAT_SPEv1p4). */
    uint32_t        fSpeV1p4 : 1;
    /** Supports Call Return Branch Records (FEAT_SPE_CRR). */
    uint32_t        fSpeCrr : 1;
    /** Supports Data Source Filtering (FEAT_SPE_FDS). */
    uint32_t        fSpeFds : 1;
    /** Supports Scalable Vector Extension version SVE2.1 (FEAT_SVE2p1). */
    uint32_t        fSve2p1 : 1;
    /** Supports Non-widening BFloat16 to BFloat16 arithmetic for SVE (FEAT_SVE_B16B16). */
    uint32_t        fSveB16B16 : 1;
    /** Supports 128-bit System instructions (FEAT_SYSINSTR128). */
    uint32_t        fSysInstr128 : 1;
    /** Supports 128-bit System registers (FEAT_SYSREG128). */
    uint32_t        fSysReg128 : 1;
    /** Supports Extension to TCR_ELx (FEAT_TCR2). */
    uint32_t        fTcr2 : 1;
    /** Supports Translation Hardening Extension (FEAT_THE). */
    uint32_t        fThe : 1;
    /** Supports Trace Buffer external mode (FEAT_TRBE_EXT). */
    uint32_t        fTrbeExt : 1;
    /** Supports Trace Buffer MPAM extension (FEAT_TRBE_MPAM). */
    uint32_t        fTrbeMpam : 1;
    /** @} */

    /** @name Armv8.4
     * @{  */
    /** Supports FEAT_TRC_SR (FEAT_TRC_SR). */
    uint32_t        fTrcSr : 1;
    /** @} */

    /** @name Armv8.4
     * @{  */
    /** Supports enhanced counter virtualization physical offset (FEAT_ECV_POFF). */
    uint32_t        fEcvPOff : 1;
    /** @} */

    /** @name Armv8.6
     * @{  */
    /** Supports disable cycle counter on SPE freeze (FEAT_SPE_DPFZS). */
    uint32_t        fSpeDpfzs : 1;
    /** @} */

    /** @name Armv8.7
     * @{  */
    /** Supports allocation tag access permission (FEAT_MTE_PERM). */
    uint32_t        fMtePerm : 1;
    /* Support statistical profiling inverse event filter (FEAT_SPE_FnE). */
    uint32_t        fSpeFnE : 1;
    /* Support statistical profiling previous branch target (FEAT_SPE_PBT). */
    uint32_t        fSpePbt : 1;
    /** @} */

    /** @name Armv8.8
     * @{  */
    /** Supports address translation operations that ignore stage 1 (FEAT_ATS1A). */
    uint32_t        fATs1a : 1;
    /** @} */

    /** @name Armv8.9
     * @{  */
    /** Supports canonical tag checking for untagged memory (FEAT_MTE_CANONICAL_TAGS). */
    uint32_t        fMteCanonicalTags : 1;
    /** @} */

    /** @name Armv9.0
     * @{  */
    /** Supports injection of undefined instruction exceptions (FEAT_UINJ). */
    uint32_t        fUinj : 1;
    /** @} */

    /** @name Armv9.2
     * @{  */
    /** Supports floating-point mode register (FEAT_FPMR). */
    uint32_t        fFpmr : 1;
    /* Support statistical profiling extensions for SME (FEAT_SPE_SME). */
    uint32_t        fSpeSme : 1;
    /** @} */

    /** @name Armv9.3
     * @{  */
    /** Supports MPAM PE-side bandwidth controls (FEAT_MPAM_PE_BW_CTRL). */
    uint32_t        fMpamPeBwCtrl : 1;
    /** @} */

    /** @name Armv9.4
     * @{  */
    /** Supports FEAT_SME_B16B16 (FEAT_SME_B16B16). */
    uint32_t        fSmeB16B16 : 1;
    /** Support TLBI VMALL for dirty state (FEAT_TLBIW). */
    uint32_t        fTlbiW : 1;
    /** Support hardware dirty state tracking structure (FEAT_HDBSS). */
    uint32_t        fHdbss : 1;
    /** Support hardware accelerator for cleaning dirty state (FEAT_HACDBS). */
    uint32_t        fHacdbs : 1;
    /** Support for concurrent use of two ASIDs (FEAT_ASID2). */
    uint32_t        fAsid2 : 1;
    /** Support delegated SError exception injection (FEAT_E3DSE). */
    uint32_t        fE3Dse : 1;
    /** Support fine-grained write trap EL3 (FEAT_FGWTE3). */
    uint32_t        fFgwtE3 : 1;
    /** Support RME granule protection check 2 extension (FEAT_RME_GPC2). */
    uint32_t        fRmeGpc2 : 1;
    /** Support streaming scalable vector bit permutes instructions (FEAT_SSVE_BitPerm). */
    uint32_t        fSsveBitPerm : 1;
    /** Support performance monitors extensions for SME (FEAT_PMUv3_SME). */
    uint32_t        fPmuV3Sme : 1;
    /* Support performance monitors event counting linking extensions (FEAT_PMUv3_TH2). */
    uint32_t        fPmuV3Th2 : 1;
    /* Support statistical profiling alternate clock domain extensions (FEAT_SPE_ALTCLK). */
    uint32_t        fSpeAltClk : 1;
    /* Support statistical profiling extended filtering by type (FEAT_SPE_EFT). */
    uint32_t        fSpeEft : 1;
    /* Support statistical profiling floating-point and SIMD flag extensions (FEAT_SPE_FPF). */
    uint32_t        fSpeFpf : 1;
    /** @} */

    /** @name Armv9.5
     * @{  */
    /** Supports FEAT_CPA (FEAT_CPA). */
    uint32_t        fCpa : 1;
    /** Supports FEAT_FAMINMAX (FEAT_FAMINMAX). */
    uint32_t        fFaMinMax : 1;
    /** Supports FEAT_FP8 (FEAT_FP8). */
    uint32_t        fFp8 : 1;
    /** Supports FEAT_FP8DOT2 (FEAT_FP8DOT2). */
    uint32_t        fFp8Dot2 : 1;
    /** Supports FEAT_FP8DOT4 (FEAT_FP8DOT4). */
    uint32_t        fFp8Dot4 : 1;
    /** Supports FEAT_FP8FMA (FEAT_FP8FMA). */
    uint32_t        fFp8Fma : 1;
    /** Supports FEAT_LUT (FEAT_LUT). */
    uint32_t        fLut : 1;
    /** Supports FEAT_PAuth_LR (FEAT_PAuth_LR). */
    uint32_t        fPAuthLR : 1;
    /** Supports FEAT_SSVE_FP8DOT2 (FEAT_SSVE_FP8DOT2). */
    uint32_t        fSsveFp8Dot2 : 1;
    /** Supports FEAT_SSVE_FP8DOT4 (FEAT_SSVE_FP8DOT4). */
    uint32_t        fSsveFp8Dot4 : 1;
    /** Supports FEAT_SSVE_FP8FMA (FEAT_SSVE_FP8FMA). */
    uint32_t        fSsveFp8Fma : 1;
    /** Supports FEAT_SME_F8F16 (FEAT_SME_F8F16). */
    uint32_t        fSmeF8F16 : 1;
    /** Supports FEAT_SME_F8F32 (FEAT_SME_F8F32). */
    uint32_t        fSmeF8F32 : 1;
    /** Supports FEAT_SME_LUTv2 (FEAT_SME_LUTv2). */
    uint32_t        fSmeLutv2 : 1;
    /** Support system performance monitors extension version 2 (FEAT_SPMU2). */
    uint32_t        fSpmu2 : 1;
    /** Support breakpoint and watchpoint enhancements 2  (FEAT_BWE2). */
    uint32_t        fBwe2 : 1;
    /** Support enhanced software step extension (FEAT_STEP2). */
    uint32_t        fStep2 : 1;
    /** Support checked pointer arithmetic (FEAT_CPA2). */
    uint32_t        fCpa2 : 1;
    /** Supports enhanced nested virtualization (FEAT_NV2p1). */
    uint32_t        fNV2p1 : 1;
    /** Supports RME granular data isolation extension (FEAT_RME_GDI). */
    uint32_t        fRmeGdi : 1;
    /** Support RME granule protection check 3 extension (FEAT_RME_GPC3). */
    uint32_t        fRmeGpc3 : 1;
    /** Support streaming SVE mode AES + 128-bit polynomial multiply long instr. (FEAT_SSVE_AES). */
    uint32_t        fSsveAes : 1;
#if 0 /* difficult to detect (external reg) */
    /** Support MPAM default resource control (_FEAT_MPAM_MSC_DCTRL). */
    uint32_t        fMpamMscDCtrl : 1;
    /** Support MPAM domain PARTID translation (_FEAT_MPAM_MSC_DOMAINS). */
    uint32_t        fMpamMscDomains : 1;
#endif
    /** @} */

    /** @name Armv9.6
     * @{  */
    /** Supports FEAT_CMPBR (FEAT_CMPBR). */
    uint32_t        fCmpBr : 1;
    /** Supports FEAT_F8F16MM (FEAT_F8F16MM). */
    uint32_t        fF8F16mm : 1;
    /** Supports FEAT_F8F32MM (FEAT_F8F32MM). */
    uint32_t        fF8F32mm : 1;
    /** Supports FEAT_FPRCVT (FEAT_FPRCVT). */
    uint32_t        fFpRcvt : 1;
    /** Supports FEAT_LSFE (FEAT_LSFE). */
    uint32_t        fLsfe : 1;
    /** Supports FEAT_LSUI (FEAT_LSUI). */
    uint32_t        fLsui : 1;
    /** Supports FEAT_PCDPHINT (FEAT_PCDPHINT). */
    uint32_t        fPCDPHint : 1;
    /** Supports FEAT_SME2p2 (FEAT_SME2p2). */
    uint32_t        fSme2p2 : 1;
    /** Supports FEAT_SSVE_FEXPA (FEAT_SSVE_FEXPA). */
    uint32_t        fSsveFexpa : 1;
    /** Supports FEAT_SVE2p2 (FEAT_SVE2p2). */
    uint32_t        fSve2p2 : 1;
    /** Supports FEAT_SVE_BFSCALE (FEAT_SVE_BFSCALE). */
    uint32_t        fSveBfscale : 1;
    /** Supports FEAT_SVE_F16F32MM (FEAT_SVE_F16F32MM). */
    uint32_t        fSveF16F32mm : 1;
    /** Supports FEAT_SME_MOP4 (FEAT_SME_MOP4). */
    uint32_t        fSmeMop4 : 1;
    /** Supports FEAT_SME_TMOP (FEAT_SME_TMOP). */
    uint32_t        fSmeTmop : 1;
    /** Supports FEAT_SVE_AES2 (FEAT_SVE_AES2). */
    uint32_t        fSveAes2 : 1;
    /** Supports FEAT_SPEv1p5 (FEAT_SPEv1p5). */
    uint32_t        fSpev1p5 : 1;
    /** Supports FEAT_TRBEv1p1 (FEAT_TRBEv1p1). */
    uint32_t        fTrbev1p1 : 1;
    /** Supports profiling exception extension (FEAT_SPE_EXC). */
    uint32_t        fSpeExc : 1;
    /** Supports statistical profiling physical addressing mode extension (FEAT_SPE_nVM). */
    uint32_t        fSpeNvm : 1;
    /** Supports trace buffer profiling exception extension (FEAT_TRBE_EXC). */
    uint32_t        fTrbeExc : 1;
    /** Supports LS64 for write-back cachable memory (FEAT_LS64WB). */
    uint32_t        fLs64WB : 1;
    /** Supports outer cacheable cache maint. operation (FEAT_OCCMO). */
    uint32_t        fOccmo : 1;
    /** Supports floating-point to/from integer in scalar fp reg (FEAT_IDTE3). */
    uint32_t        fIdte3 : 1;
    /** @} */

    /** Padding to the required size to match CPUMFEATURESX86. */
    uint32_t        auPadding[2];
} CPUMFEATURESARMV8;
#ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(CPUMFEATURESARMV8, 64);
AssertCompileMembersAtSameOffset(CPUMFEATURESCOMMON, enmMicroarch,          CPUMFEATURESARMV8, enmMicroarch);
AssertCompileMembersAtSameOffset(CPUMFEATURESCOMMON, enmCpuVendor,          CPUMFEATURESARMV8, enmCpuVendor);
AssertCompileMembersAtSameOffset(CPUMFEATURESCOMMON, cMaxPhysAddrWidth,     CPUMFEATURESARMV8, cMaxPhysAddrWidth);
AssertCompileMembersAtSameOffset(CPUMFEATURESCOMMON, cMaxLinearAddrWidth,   CPUMFEATURESARMV8, cMaxLinearAddrWidth);
#endif


/**
 * Chameleon wrapper structure for the host CPU features.
 *
 * This is used for the globally readable g_CpumHostFeatures variable, which is
 * initialized once during VMMR0 load for ring-0 and during CPUMR3Init in
 * ring-3.  To reflect this immutability after load/init, we use this wrapper
 * structure to switch it between const and non-const depending on the context.
 * Only two files sees it as non-const (CPUMR0.cpp and CPUM.cpp).
 */
typedef union CPUHOSTFEATURES
{
    /** Fields common to all CPU types. */
    CPUMFEATURESCOMMON Common;
    /** The host specific structure. */
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    CPUMFEATURESX86
#elif defined(RT_ARCH_ARM64)
    CPUMFEATURESARMV8
#else
# error "port me"
#endif
#ifndef CPUM_WITH_NONCONST_HOST_FEATURES
    const
#endif
                    s;
} CPUHOSTFEATURES;
#ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(CPUHOSTFEATURES, 64);
#endif
/** Pointer to a const host CPU feature structure. */
typedef CPUHOSTFEATURES const *PCCPUHOSTFEATURES;

/** Host CPU features.
 * @note In ring-3, only valid after CPUMR3Init.  In ring-0, valid after
 *       module init. */
extern CPUHOSTFEATURES g_CpumHostFeatures;


/** The target CPU feature structure.
 * @todo this should have a chameleon wrapper as well (ring-0).  */
#ifndef VBOX_VMM_TARGET_ARMV8
typedef CPUMFEATURESX86   CPUMFEATURES;
#else
typedef CPUMFEATURESARMV8 CPUMFEATURES;
#endif
/** Pointer to a CPU feature structure. */
typedef CPUMFEATURES *PCPUMFEATURES;
/** Pointer to a const CPU feature structure. */
typedef CPUMFEATURES const *PCCPUMFEATURES;


/**
 * CPUID leaf on x86.
 * @note Used by both x86 hosts and guest.
 * @todo s/CPUMCPUIDLEAF/CPUMX86CPUIDLEAF/
 */
typedef struct CPUMCPUIDLEAF
{
    /** The leaf number. */
    uint32_t    uLeaf;
    /** The sub-leaf number. */
    uint32_t    uSubLeaf;
    /** Sub-leaf mask.  This is 0 when sub-leaves aren't used. */
    uint32_t    fSubLeafMask;

    /** The EAX value. */
    uint32_t    uEax;
    /** The EBX value. */
    uint32_t    uEbx;
    /** The ECX value. */
    uint32_t    uEcx;
    /** The EDX value. */
    uint32_t    uEdx;

    /** Flags. */
    uint32_t    fFlags;
} CPUMCPUIDLEAF;
#ifndef VBOX_FOR_DTRACE_LIB
AssertCompileSize(CPUMCPUIDLEAF, 32);
#endif
/** Pointer to a CPUID leaf. */
typedef CPUMCPUIDLEAF *PCPUMCPUIDLEAF;
/** Pointer to a const CPUID leaf. */
typedef CPUMCPUIDLEAF const *PCCPUMCPUIDLEAF;

/** @name CPUMCPUIDLEAF::fFlags
 * @{ */
/** Indicates working intel leaf 0xb where the lower 8 ECX bits are not modified
 * and EDX containing the extended APIC ID. */
#define CPUMCPUIDLEAF_F_INTEL_TOPOLOGY_SUBLEAVES    RT_BIT_32(0)
/** The leaf contains an APIC ID that needs changing to that of the current CPU. */
#define CPUMCPUIDLEAF_F_CONTAINS_APIC_ID            RT_BIT_32(1)
/** The leaf contains an OSXSAVE which needs individual handling on each CPU. */
#define CPUMCPUIDLEAF_F_CONTAINS_OSXSAVE            RT_BIT_32(2)
/** The leaf contains an APIC feature bit which is tied to APICBASE.EN. */
#define CPUMCPUIDLEAF_F_CONTAINS_APIC               RT_BIT_32(3)
/** Mask of the valid flags. */
#define CPUMCPUIDLEAF_F_VALID_MASK                  UINT32_C(0xf)
/** @} */


/**
 * Method used to deal with unknown CPUID leaves on x86.
 */
typedef enum CPUMUNKNOWNCPUID
{
    /** Invalid zero value. */
    CPUMUNKNOWNCPUID_INVALID = 0,
    /** Use given default values (DefCpuId). */
    CPUMUNKNOWNCPUID_DEFAULTS,
    /** Return the last standard leaf.
     * Intel Sandy Bridge has been observed doing this. */
    CPUMUNKNOWNCPUID_LAST_STD_LEAF,
    /** Return the last standard leaf, with ecx observed.
     * Intel Sandy Bridge has been observed doing this. */
    CPUMUNKNOWNCPUID_LAST_STD_LEAF_WITH_ECX,
    /** The register values are passed thru unmodified. */
    CPUMUNKNOWNCPUID_PASSTHRU,
    /** End of valid value. */
    CPUMUNKNOWNCPUID_END,
    /** Ensure 32-bit type. */
    CPUMUNKNOWNCPUID_32BIT_HACK = 0x7fffffff
} CPUMUNKNOWNCPUID;
/** Pointer to unknown CPUID leaf method. */
typedef CPUMUNKNOWNCPUID *PCPUMUNKNOWNCPUID;


/**
 * The register set returned by an x86 CPUID operation.
 */
typedef struct CPUMCPUID
{
    uint32_t uEax;
    uint32_t uEbx;
    uint32_t uEcx;
    uint32_t uEdx;
} CPUMCPUID;
/** Pointer to a CPUID leaf. */
typedef CPUMCPUID *PCPUMCPUID;
/** Pointer to a const CPUID leaf. */
typedef const CPUMCPUID *PCCPUMCPUID;


/**
 * ARMv8 CPU ID registers.
 */
typedef struct CPUMARMV8IDREGS
{
    /** Content of the ID_AA64PFR0_EL1 register. */
    uint64_t        u64RegIdAa64Pfr0El1;
    /** Content of the ID_AA64PFR1_EL1 register. */
    uint64_t        u64RegIdAa64Pfr1El1;
    /** Content of the ID_AA64DFR0_EL1 register. */
    uint64_t        u64RegIdAa64Dfr0El1;
    /** Content of the ID_AA64DFR1_EL1 register. */
    uint64_t        u64RegIdAa64Dfr1El1;
    /** Content of the ID_AA64AFR0_EL1 register. */
    uint64_t        u64RegIdAa64Afr0El1;
    /** Content of the ID_AA64AFR1_EL1 register. */
    uint64_t        u64RegIdAa64Afr1El1;
    /** Content of the ID_AA64ISAR0_EL1 register. */
    uint64_t        u64RegIdAa64Isar0El1;
    /** Content of the ID_AA64ISAR1_EL1 register. */
    uint64_t        u64RegIdAa64Isar1El1;
    /** Content of the ID_AA64ISAR2_EL1 register. */
    uint64_t        u64RegIdAa64Isar2El1;
    /** Content of the ID_AA64MMFR0_EL1 register. */
    uint64_t        u64RegIdAa64Mmfr0El1;
    /** Content of the ID_AA64MMFR1_EL1 register. */
    uint64_t        u64RegIdAa64Mmfr1El1;
    /** Content of the ID_AA64MMFR2_EL1 register. */
    uint64_t        u64RegIdAa64Mmfr2El1;
    /** Content of the CLIDR_EL1 register. */
    uint64_t        u64RegClidrEl1;
    /** Content of the CTR_EL0 register. */
    uint64_t        u64RegCtrEl0;
    /** Content of the DCZID_EL0 register. */
    uint64_t        u64RegDczidEl0;
    /** @todo we need MIDR_EL1 here, possibly also MPIDR_EL1 and REVIDR_EL1. */
} CPUMARMV8IDREGS;
/** Pointer to CPU ID registers. */
typedef CPUMARMV8IDREGS *PCPUMARMV8IDREGS;
/** Pointer to a const CPU ID registers structure. */
typedef CPUMARMV8IDREGS const *PCCPUMARMV8IDREGS;


/** For identifying the extended database entry type. */
typedef enum CPUMDBENTRYTYPE
{
    CPUMDBENTRYTYPE_INVALID = 0,
    CPUMDBENTRYTYPE_X86,
    CPUMDBENTRYTYPE_ARM,
    CPUMDBENTRYTYPE_END,
    CPUMDBENTRYTYPE_32BIT_HACK = 0x7fffffff
} CPUMDBENTRYTYPE;

/**
 * CPU database entry, common parts.
 */
typedef struct CPUMDBENTRY
{
    /** The CPU name. */
    const char     *pszName;
    /** The full CPU name. */
    const char     *pszFullName;
    /** The CPU vendor. */
    CPUMCPUVENDOR   enmVendor;
    /** The microarchitecture. */
    CPUMMICROARCH   enmMicroarch;
    /** Flags - CPUMDB_F_XXX. */
    uint32_t        fFlags;
    /** The database entry type. */
    CPUMDBENTRYTYPE enmEntryType;
} CPUMDBENTRY;
/** Pointer to a const CPU database entry. */
typedef CPUMDBENTRY const *PCCPUMDBENTRY;

/** @name CPUMDB_F_XXX - CPUDBENTRY::fFlags
 * @{ */
/** Should execute all in IEM.
 * @todo Implement this - currently done in Main...  */
#define CPUMDB_F_EXECUTE_ALL_IN_IEM         RT_BIT_32(0)
/** @} */


/** CPU core type. */
typedef enum CPUMCORETYPE
{
    kCpumCoreType_Invalid = 0,
    kCpumCoreType_Efficiency,
    kCpumCoreType_Performance,
    kCpumCoreType_Unknown,
    kCpumCoreType_End,
    kCpumCoreType_32BitHack = 0x7fffffff
} CPUMCORETYPE;


/**
 * CPU database entry for x86.
 */
typedef struct CPUMDBENTRYX86
{
    CPUMDBENTRY     Core;
    /** The CPU family. */
    uint8_t         uFamily;
    /** The CPU model. */
    uint8_t         uModel;
    /** The CPU stepping. */
    uint8_t         uStepping;
    /** Scalable bus frequency used for reporting other frequencies. */
    uint64_t        uScalableBusFreq;
    /** The maximum physical address with of the CPU.  This should correspond to
     * the value in CPUID leaf 0x80000008 when present. */
    uint8_t         cMaxPhysAddrWidth;
    /** The MXCSR mask. */
    uint32_t        fMxCsrMask;
    /** Pointer to an array of CPUID leaves.  */
    PCCPUMCPUIDLEAF paCpuIdLeaves;
    /** The number of CPUID leaves in the array paCpuIdLeaves points to. */
    uint32_t        cCpuIdLeaves;
    /** The method used to deal with unknown CPUID leaves. */
    CPUMUNKNOWNCPUID enmUnknownCpuId;
    /** The default unknown CPUID value. */
    CPUMCPUID       DefUnknownCpuId;

    /** MSR mask.  Several microarchitectures ignore the higher bits of ECX in
     *  the RDMSR and WRMSR instructions. */
    uint32_t        fMsrMask;

    /** The number of ranges in the table pointed to b paMsrRanges. */
    uint32_t        cMsrRanges;
    /** MSR ranges for this CPU. */
    struct CPUMMSRRANGE const *paMsrRanges;
} CPUMDBENTRYX86;
/** Pointer to a const X86 CPU database entry. */
typedef CPUMDBENTRYX86 const *PCCPUMDBENTRYX86;


/**
 * CPU database entry for ARM.
 */
typedef struct CPUMDBENTRYARM
{
    /** The common parts. */
    CPUMDBENTRY                     Core;

    /** System register values common to all the core variations. */
    struct SUPARMSYSREGVAL const   *paSysRegCmnVals;
    /** Number of entries in the table paSysRegCmnVals points to. */
    uint32_t                        cSysRegCmnVals;
    /** Number of core variants in aVariants below. */
    uint32_t                        cVariants;

    /** CPU core variation details. */
    struct
    {
        /** The name of this CPU core variation. */
        const char                     *pszName;
        /** MIDR_EL1 for this CPU core variation. */
        union
        {
            struct
            {
                /** CPU revision. */
                uint32_t                u4Revision    :  4;
                /** Part number. */
                uint32_t                u12PartNum    : 12;
                /** ARM architecture indicator. */
                uint32_t                u4Arch        :  4;
                /** Implementer specific variant. */
                uint32_t                u4Variant     :  4;
                /** The implementer. */
                uint32_t                u8Implementer :  8;
            } s;
            uint64_t                    u64;
        } Midr;
        /** The CPU core type. */
        CPUMCORETYPE                    enmCoreType;
        /** Number of entries in the table paSysRegVals points to. */
        uint32_t                        cSysRegVals;
        /** System register values specific to this CPU core variant. */
        struct SUPARMSYSREGVAL const   *paSysRegVals;
    } aVariants[2];
} CPUMDBENTRYARM;
/** Pointer to a const ARM CPU database entry. */
typedef CPUMDBENTRYARM const *PCCPUMDBENTRYARM;


/*
 * Include the target specific header.
 * This uses several of the above types, so it must be postponed till here.
 */
#ifndef VBOX_VMM_TARGET_ARMV8
# include <VBox/vmm/cpum-x86-amd64.h>
#else
# include <VBox/vmm/cpum-armv8.h>
#endif


/**
 * Common CPUID info dumper state.
 */
typedef struct CPUMCPUIDINFOSTATE
{
    /** Output helper. */
    PCDBGFINFOHLP   pHlp;
    /** Verbosity level. */
    unsigned        iVerbosity;
    /** RT_MAX(strlen(pszLabel), strlen(pszLabel2)). */
    unsigned        cchLabelMax;
    /** The short label, exactly 3 characters long. */
    const char     *pszShort;
    /** The full label. */
    const char     *pszLabel;
    /** strlen(pszLabel). */
    unsigned        cchLabel;

    /** strlen(pszLabel2). */
    unsigned        cchLabel2;
    /** The short label, exactly 3 characters long.  NULL if no 2nd data series. */
    const char     *pszShort2;
    /** The full label.  NULL if no 2nd data series. */
    const char     *pszLabel2;
} CPUMCPUIDINFOSTATE;
/** Pointer to a common CPUID info dumper state. */
typedef CPUMCPUIDINFOSTATE *PCPUMCPUIDINFOSTATE;


/**
 * X86 CPUID info dumper state.
 */
typedef struct CPUMCPUIDINFOSTATEX86
{
    CPUMCPUIDINFOSTATE      Cmn;

    CPUMFEATURESX86 const  *pFeatures;
    PCCPUMCPUIDLEAF         paLeaves;
    uint32_t                cLeaves;

    uint32_t                cLeaves2;
    PCCPUMCPUIDLEAF         paLeaves2;
} CPUMCPUIDINFOSTATEX86;
/** Pointer to a x86 CPUID info dumper state. */
typedef CPUMCPUIDINFOSTATEX86 *PCPUMCPUIDINFOSTATEX86;


/**
 * ARMv8 CPUID info dumper state.
 */
typedef struct CPUMCPUIDINFOSTATEARMV8
{
    CPUMCPUIDINFOSTATE          Cmn;

    CPUMFEATURESARMV8 const    *pFeatures;
    struct SUPARMSYSREGVAL     *paIdRegs;
    uint32_t                    cIdRegs;

    uint32_t                    cIdRegs2;
    struct SUPARMSYSREGVAL     *paIdRegs2;
} CPUMCPUIDINFOSTATEARMV8;
/** Pointer to a ARMv8 CPUID info dumper state. */
typedef CPUMCPUIDINFOSTATEARMV8 *PCPUMCPUIDINFOSTATEARMV8;



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

VMMDECL(const char *)   CPUMMicroarchName(CPUMMICROARCH enmMicroarch);
VMMDECL(const char *)   CPUMCpuVendorName(CPUMCPUVENDOR enmVendor);

VMMDECL(CPUMCPUVENDOR)  CPUMCpuIdDetectX86VendorEx(uint32_t uEAX, uint32_t uEBX, uint32_t uECX, uint32_t uEDX);
#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
VMMDECL(int)            CPUMCpuIdCollectLeavesFromX86Host(PCPUMCPUIDLEAF *ppaLeaves, uint32_t *pcLeaves);
VMM_INT_DECL(void)      CPUMCpuIdApplyX86HostArchCapabilities(PVMCC pVM, bool fHasArchCap, uint64_t fHostArchVal);
#endif
VMMDECL(int)            CPUMCpuIdExplodeFeaturesX86(PCCPUMCPUIDLEAF paLeaves, uint32_t cLeaves, CPUMFEATURESX86 *pFeatures);
#if defined(RT_ARCH_ARM64)
VMMDECL(int)            CPUMCpuIdCollectIdSysRegsFromArmV8Host(struct SUPARMSYSREGVAL **ppaSysRegs, uint32_t *pcSysRegs);
#endif
VMMDECL(int)            CPUMCpuIdExplodeFeaturesArmV8FromSysRegs(struct SUPARMSYSREGVAL const *paSysRegs, uint32_t cSysRegs,
                                                                 CPUMFEATURESARMV8 *pFeatures);


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

VMMR3DECL(uint32_t)         CPUMR3DbGetEntries(void);
/** Pointer to CPUMR3DbGetEntries. */
typedef DECLCALLBACKPTR(uint32_t, PFNCPUMDBGETENTRIES, (void));
VMMR3DECL(PCCPUMDBENTRY)    CPUMR3DbGetEntryByIndex(uint32_t idxCpuDb);
/** Pointer to CPUMR3DbGetEntryByIndex. */
typedef DECLCALLBACKPTR(PCCPUMDBENTRY, PFNCPUMDBGETENTRYBYINDEX, (uint32_t idxCpuDb));
VMMR3DECL(PCCPUMDBENTRY)    CPUMR3DbGetEntryByName(const char *pszName);
/** Pointer to CPUMR3DbGetEntryByName. */
typedef DECLCALLBACKPTR(PCCPUMDBENTRY, PFNCPUMDBGETENTRYBYNAME, (const char *pszName));
VMMR3DECL(PCCPUMDBENTRY)    CPUMR3DbGetBestEntryByName(const char *pszName, CPUMDBENTRYTYPE enmEntryType, uint32_t *puScore);
/** Pointer to CPUMR3DbGetBestEntryByName. */
typedef DECLCALLBACKPTR(PCCPUMDBENTRY, PFNCPUMDBGETBESTENTRYBYNAME, (const char *pszName, CPUMDBENTRYTYPE enmEntryType,
                                                                     uint32_t *puScore));
VMMR3DECL(PCCPUMDBENTRYARM) CPUMR3DbGetBestEntryByArm64MainId(uint64_t idMain, uint32_t *puScore);
/** Pointer to CPUMR3DbGetBestEntryByArm64MainId.  */
typedef DECLCALLBACKPTR(PCCPUMDBENTRYARM, PFNCPUMDBGETBESTENTRYBYARM64MAINID, (uint64_t idMain, uint32_t *puScore));
VMMR3DECL(void)             CPUMR3CpuIdPrintArmV8Features(PCDBGFINFOHLP pHlp,
                                                          CPUMFEATURESARMV8 const *pFeatures, const char *pszLabel,
                                                          CPUMFEATURESARMV8 const *pSecondary, const char *pszSecondary);
/** Pointer to CPUMR3CpuIdPrintArmV8Features   */
typedef DECLCALLBACKPTR(void, PFNCPUMCPUIDPRINTARMV8FEATURES,(PCDBGFINFOHLP pHlp,
                                                              CPUMFEATURESARMV8 const *pFeatures, const char *pszLabel,
                                                              CPUMFEATURESARMV8 const *pSecondary, const char *pszSecondary));
VMMR3DECL(int)              CPUMCpuIdDetermineArmV8MicroarchEx(uint64_t idMain, const char *pszCpuName,
                                                               CPUMMICROARCH *penmMicroarch,
                                                               CPUMCPUVENDOR *penmVendor,
                                                               CPUMCORETYPE  *penmCoreType,
                                                               const char   **ppszName,
                                                               const char   **ppszFullName);
/** Pointer to CPUMCpuIdDetermineArmV8MicroarchEx. */
typedef DECLCALLBACKPTR(int, PFNCPUMCPUIDDETERMINEARMV8MICROARCHEX, (uint64_t idMain, const char *pszCpuName,
                                                                     CPUMMICROARCH *penmMicroarch,
                                                                     CPUMCPUVENDOR *penmVendor,
                                                                     CPUMCORETYPE  *penmCoreType,
                                                                     const char   **ppszName,
                                                                     const char   **ppszFullName));
VMMR3DECL(void)             CPUMR3CpuIdInfoX86(PCPUMCPUIDINFOSTATEX86 pThis);
/** Pointer to CPUMR3CpuIdInfoX86. */
typedef DECLCALLBACKPTR(void, PFNCPUMR3CPUIDINFOX86,(PCPUMCPUIDINFOSTATEX86 pThis));

VMMR3DECL(void)             CPUMR3CpuIdInfoArmV8(PCPUMCPUIDINFOSTATEARMV8 pThis);
/** Pointer to CPUMR3CpuIdInfoArmV8. */
typedef DECLCALLBACKPTR(void, PFNCPUMR3CPUIDINFOARMV8,(PCPUMCPUIDINFOSTATEARMV8 pThis));


# if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
VMMR3DECL(uint32_t)         CPUMR3DeterminHostMxCsrMask(void);
# endif

VMMR3_INT_DECL(void)    CPUMR3NemActivateGuestDebugState(PVMCPUCC pVCpu);
VMMR3_INT_DECL(void)    CPUMR3NemActivateHyperDebugState(PVMCPUCC pVCpu);
/** @} */
#endif /* IN_RING3 */

#endif /* !VBOX_FOR_DTRACE_LIB */
/** @} */
RT_C_DECLS_END


#endif /* !VBOX_INCLUDED_vmm_cpum_h */

